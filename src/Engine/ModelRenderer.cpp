#include "ModelRenderer.h"
#include "ObjLoader.h"
#include "TextureLoader.h"
#include <d3dcompiler.h>
#include <vector>
#include <fstream>
#include <string>
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>

using namespace DirectX;

// 🚨 增加了 CameraPos 用于高级光照
struct VSConst {
    XMMATRIX WVP;
    XMMATRIX World;
    XMFLOAT3 CameraPos; 
    float padding;
    XMFLOAT4 MatParams; // 🚨 新增：材质参数
    XMFLOAT4 ColorTint; // 🚨 新增：颜色滤镜
};

ModelRenderer::ModelRenderer() : m_device(nullptr), m_vs(nullptr), m_ps(nullptr), m_inputLayout(nullptr), m_vb(nullptr), m_ib(nullptr), m_cb(nullptr), m_indexCount(0), m_diffuseSRV(nullptr), m_sampler(nullptr), m_normalSRV(nullptr), m_rasterizerState(nullptr), m_modelCenter{0.0f,0.0f,0.0f} {}


ModelRenderer::~ModelRenderer() { Cleanup(); }

void ModelRenderer::Cleanup() { /* ... 清理代码保持不变 ... */ 
    if (m_rasterizerState) { m_rasterizerState->Release(); m_rasterizerState = nullptr; } // 🚨 新增清理
    if (m_cb) { m_cb->Release(); m_cb = nullptr; }
    if (m_ib) { m_ib->Release(); m_ib = nullptr; }
    if (m_vb) { m_vb->Release(); m_vb = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_ps) { m_ps->Release(); m_ps = nullptr; }
    if (m_vs) { m_vs->Release(); m_vs = nullptr; }
    if (m_diffuseSRV) { m_diffuseSRV->Release(); m_diffuseSRV = nullptr; }
    if (m_sampler) { m_sampler->Release(); m_sampler = nullptr; }
    if (m_normalSRV) { m_normalSRV->Release(); m_normalSRV = nullptr; }
}

bool ModelRenderer::Initialize(ID3D11Device* device) {
    m_device = device;

    const char* shader = R"(
         cbuffer VSConst : register(b0) { 
            matrix WVP; matrix World; float3 CameraPos; float padding; 
            float4 MatParams; float4 ColorTint; 
        };
        Texture2D DiffuseMap : register(t0);
        Texture2D NormalMap : register(t1);
        SamplerState samp : register(s0);

        struct VS_IN { float3 pos : POSITION; float2 uv : TEXCOORD; float3 normal : NORMAL; float3 tangent : TANGENT; };
        struct VS_OUT { float4 pos : SV_POSITION; float3 normal : NORMAL; float3 tangent : TANGENT; float2 uv : TEXCOORD; float3 worldPos : TEXCOORD1; };

        VS_OUT VSMain(VS_IN input) {
            VS_OUT o;
            float4 worldPos = mul(float4(input.pos,1.0f), World);
            o.pos = mul(worldPos, WVP);
            o.worldPos = worldPos.xyz;
            o.normal = mul((float3x3)World, input.normal);
            o.tangent = mul((float3x3)World, input.tangent);
            o.uv = input.uv;
            return o;
        }

        float4 PSMain(VS_OUT input) : SV_Target {
            float3 N = normalize(input.normal);
            float3 finalN = N; 
            if (length(input.tangent) > 0.01f) {
                float3 T = normalize(input.tangent);
                float3 B = normalize(cross(N, T));
                float3 nmap = NormalMap.Sample(samp, input.uv).xyz * 2.0f - 1.0f;
                finalN = normalize(nmap.x * T + nmap.y * B + nmap.z * N);
            }

            // 🚨 核心修复：改用“从左上角照下来的全局平行光”
            float3 lightDir = normalize(float3(-0.5f, 1.0f, -0.8f));
            
            // 🚨 核心修复：使用 Valve 著名的 Half-Lambert (半兰伯特) 光照！
            // 把 -1~1 的光照结果映射到 0~1，光线会极度柔和地包裹住背面，彻底消灭死黑分界线！
            float NdotL = dot(finalN, lightDir) * 0.5f + 0.5f; 
            NdotL = NdotL * NdotL; // 稍微增加一点明暗对比

            // 菲涅尔边缘光保持不变
            float viewDot = saturate(dot(finalN, normalize(CameraPos - input.worldPos)));
            float fresnel = pow(1.0f - viewDot, MatParams.w);
            float3 rimColor = float3(0.0f, 0.6f, 1.0f) * fresnel * MatParams.z;

            float3 baseCol = DiffuseMap.Sample(samp, input.uv).rgb * ColorTint.rgb;
            
            // MatParams.x 控制暗部环境光，MatParams.y 控制主光
            float3 color = baseCol * (MatParams.x + MatParams.y * NdotL) + rimColor;
            return float4(color, 1.0f);
        }
    )";

    // ...[中间的编译着色器和布局代码保持不变] ...
    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr;
    if (FAILED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, nullptr))) return false;
    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs))) { vsBlob->Release(); return false; }
    if (FAILED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, nullptr))) { vsBlob->Release(); return false; }
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps))) { vsBlob->Release(); psBlob->Release(); return false; }

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    m_device->CreateInputLayout(elems, ARRAYSIZE(elems), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
    vsBlob->Release(); psBlob->Release();

    D3D11_BUFFER_DESC cbd = {}; cbd.Usage = D3D11_USAGE_DEFAULT; cbd.ByteWidth = sizeof(VSConst); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    m_device->CreateBuffer(&cbd, nullptr, &m_cb);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP; sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP; sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS; sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.MaxAnisotropy = 16;
    m_device->CreateSamplerState(&sd, &m_sampler);

    // 占位纹理...
    D3D11_TEXTURE2D_DESC nd = {}; nd.Width = 1; nd.Height = 1; nd.MipLevels = 1; nd.ArraySize = 1; nd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; nd.SampleDesc.Count = 1; nd.Usage = D3D11_USAGE_DEFAULT; nd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    unsigned char normalPixel[4] = {128,128,255,255};
    D3D11_SUBRESOURCE_DATA ninit = {}; ninit.pSysMem = normalPixel; ninit.SysMemPitch = 4;
    ID3D11Texture2D* ntex = nullptr;
    if (SUCCEEDED(m_device->CreateTexture2D(&nd, &ninit, &ntex))) { m_device->CreateShaderResourceView(ntex, nullptr, &m_normalSRV); ntex->Release(); }

    unsigned char whitePixel[4] = {255,255,255,255};
    ID3D11Texture2D* wtex = nullptr;
    if (SUCCEEDED(m_device->CreateTexture2D(&nd, nullptr, &wtex))) {
        ID3D11DeviceContext* ctx = nullptr; m_device->GetImmediateContext(&ctx);
        if (ctx) { ctx->UpdateSubresource(wtex, 0, nullptr, whitePixel, 4, 0); ctx->Release(); }
        m_device->CreateShaderResourceView(wtex, nullptr, &m_diffuseSRV); wtex->Release();
    }
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE; 
    m_device->CreateRasterizerState(&rd, &m_rasterizerState);

    return true;
}

static std::string ExtractLastToken(const std::string& line) {
    std::istringstream ss(line); std::string token; std::string last;
    while (ss >> token) last = token;
    return last;
}

bool ModelRenderer::LoadModelAsync(const std::string& filepath) {
    std::string mtllib, usemtl;
    {
        std::ifstream in(filepath); std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back(); // 🚨 清除恶心的 \r
            if (line.rfind("mtllib ", 0) == 0) mtllib = line.substr(7);
            if (line.rfind("usemtl ", 0) == 0 && usemtl.empty()) usemtl = line.substr(7);
        }
    }

    std::string baseDir = filepath;
    auto pos = baseDir.find_last_of("/\\");
    if (pos != std::string::npos) baseDir = baseDir.substr(0, pos+1); else baseDir = "";

    if (!mtllib.empty()) {
        std::string mtlPath = baseDir + mtllib;
        std::ifstream mtl(mtlPath);
        if (mtl.is_open()) {
            std::string line, current;
            while (std::getline(mtl, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back(); // 🚨 强杀隐形 \r Bug！
                
                if (line.rfind("newmtl ", 0) == 0) current = line.substr(7);
                if (!usemtl.empty() && current != usemtl) continue; // 现在终于能匹配上了！

                if (line.rfind("map_Kd", 0) == 0) {
                    std::string tex = ExtractLastToken(line);
                    std::string full = baseDir + tex;
                    int plen = MultiByteToWideChar(CP_UTF8, 0, full.c_str(), -1, NULL, 0);
                    if (plen > 0) {
                        m_pendingDiffusePathW.resize(plen);
                        MultiByteToWideChar(CP_UTF8, 0, full.c_str(), -1, &m_pendingDiffusePathW[0], plen);
                        if (!m_pendingDiffusePathW.empty() && m_pendingDiffusePathW.back() == L'\0') m_pendingDiffusePathW.pop_back();
                    }
                }
                // 忽略 Bump 解析以节省篇幅，核心漫反射已修复
            }
        }
    }

    if (m_loadInProgress.load()) return false; 
    m_loadInProgress.store(true); m_loadReady.store(false);
    
    m_loadThread = std::thread([this, filepath]() {
        std::vector<ModelVertex> v; std::vector<unsigned int> i;
        bool ok = ObjLoader::LoadObj(filepath, v, i);
        std::lock_guard<std::mutex> lk(m_pendingMutex);
        if (ok) { m_pendingVerts = std::move(v); m_pendingInds = std::move(i); }
        m_loadReady.store(true);
    });

    return true;
}

void ModelRenderer::PollFinalizeLoad() {
    if (!m_loadReady.load()) return;
    // ensure thread finished
    if (m_loadThread.joinable()) m_loadThread.join();

    std::vector<ModelVertex> verts; std::vector<unsigned int> inds;
    {
        std::lock_guard<std::mutex> lk(m_pendingMutex);
        verts = std::move(m_pendingVerts);
        inds = std::move(m_pendingInds);
    }
    m_loadInProgress.store(false);
    m_loadReady.store(false);

    if (verts.empty() || inds.empty()) {
        OutputDebugStringA("ModelRenderer: async load produced no geometry\n");
        return;
    }

    // compute model bounds center (local space) before tangents
    if (!verts.empty()) {
        XMFLOAT3 mn = verts[0].Position;
        XMFLOAT3 mx = verts[0].Position;
        for (size_t vi = 1; vi < verts.size(); ++vi) {
            XMFLOAT3 p = verts[vi].Position;
            mn.x = min(mn.x, p.x); mn.y = min(mn.y, p.y); mn.z = min(mn.z, p.z);
            mx.x = max(mx.x, p.x); mx.y = max(mx.y, p.y); mx.z = max(mx.z, p.z);
        }
        m_modelCenter.x = (mn.x + mx.x) * 0.5f;
        m_modelCenter.y = (mn.y + mx.y) * 0.5f;
        m_modelCenter.z = (mn.z + mx.z) * 0.5f;
    }

    // compute tangents (same as before)
    std::vector<XMFLOAT3> tanAccum(verts.size());
    for (size_t i = 0; i < inds.size(); i += 3) {
        unsigned int i0 = inds[i]; unsigned int i1 = inds[i+1]; unsigned int i2 = inds[i+2];
        XMFLOAT3 p0 = verts[i0].Position; XMFLOAT3 p1 = verts[i1].Position; XMFLOAT3 p2 = verts[i2].Position;
        XMFLOAT2 uv0 = verts[i0].UV; XMFLOAT2 uv1 = verts[i1].UV; XMFLOAT2 uv2 = verts[i2].UV;
        XMVECTOR v0 = XMLoadFloat3(&p0); XMVECTOR v1 = XMLoadFloat3(&p1); XMVECTOR v2 = XMLoadFloat3(&p2);
        XMVECTOR edge1 = v1 - v0; XMVECTOR edge2 = v2 - v0;
        XMFLOAT3 e1, e2; XMStoreFloat3(&e1, edge1); XMStoreFloat3(&e2, edge2);
        float du1 = uv1.x - uv0.x; float dv1 = uv1.y - uv0.y;
        float du2 = uv2.x - uv0.x; float dv2 = uv2.y - uv0.y;
        float r = (du1 * dv2 - du2 * dv1);
        float inv = (r == 0.0f) ? 0.0f : 1.0f / r;
        XMFLOAT3 tangent;
        tangent.x = inv * (dv2 * e1.x - dv1 * e2.x);
        tangent.y = inv * (dv2 * e1.y - dv1 * e2.y);
        tangent.z = inv * (dv2 * e1.z - dv1 * e2.z);
        tanAccum[i0].x += tangent.x; tanAccum[i0].y += tangent.y; tanAccum[i0].z += tangent.z;
        tanAccum[i1].x += tangent.x; tanAccum[i1].y += tangent.y; tanAccum[i1].z += tangent.z;
        tanAccum[i2].x += tangent.x; tanAccum[i2].y += tangent.y; tanAccum[i2].z += tangent.z;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        XMVECTOR n = XMLoadFloat3(&verts[i].Normal);
        XMVECTOR t = XMLoadFloat3(&tanAccum[i]);
        XMVECTOR proj = XMVector3Normalize(XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t)))));
        XMFLOAT3 finalT; XMStoreFloat3(&finalT, proj);
        verts[i].Tangent = finalT;
    }

    // create immutable vertex/index buffers on main thread
    if (m_vb) { m_vb->Release(); m_vb = nullptr; }
    if (m_ib) { m_ib->Release(); m_ib = nullptr; }

    D3D11_BUFFER_DESC vbd = {}; vbd.Usage = D3D11_USAGE_IMMUTABLE; vbd.ByteWidth = sizeof(ModelVertex) * (UINT)verts.size(); vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = {}; vinit.pSysMem = verts.data();
    if (FAILED(m_device->CreateBuffer(&vbd, &vinit, &m_vb))) { OutputDebugStringA("ModelRenderer: CreateVertexBuffer failed\n"); return; }

    D3D11_BUFFER_DESC ibd = {}; ibd.Usage = D3D11_USAGE_IMMUTABLE; ibd.ByteWidth = sizeof(unsigned int) * (UINT)inds.size(); ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = {}; iinit.pSysMem = inds.data();
    if (FAILED(m_device->CreateBuffer(&ibd, &iinit, &m_ib))) { OutputDebugStringA("ModelRenderer: CreateIndexBuffer failed\n"); return; }

    m_indexCount = (int)inds.size();

    // If we parsed texture paths earlier, try to load them now (may be empty)
    if (!m_pendingDiffusePathW.empty()) {
        if (!SetDiffuseTexture(m_pendingDiffusePathW)) {
            OutputDebugStringW(L"ModelRenderer: failed to load diffuse texture\n");
        } else {
            OutputDebugStringW(L"ModelRenderer: loaded diffuse texture\n");
        }
    }
    if (!m_pendingNormalPathW.empty()) {
        if (!SetNormalTexture(m_pendingNormalPathW)) {
            OutputDebugStringW(L"ModelRenderer: failed to load normal texture\n");
        } else {
            OutputDebugStringW(L"ModelRenderer: loaded normal texture\n");
        }
    }
}

bool ModelRenderer::SetDiffuseTexture(const std::wstring& path) {
    if (m_diffuseSRV) { m_diffuseSRV->Release(); m_diffuseSRV = nullptr; }
    m_diffuseSRV = TextureLoader::LoadTextureFromFile(m_device, path);
    return m_diffuseSRV != nullptr;
}

bool ModelRenderer::SetNormalTexture(const std::wstring& path) {
    if (m_normalSRV) { m_normalSRV->Release(); m_normalSRV = nullptr; }
    m_normalSRV = TextureLoader::LoadTextureFromFile(m_device, path);
    return m_normalSRV != nullptr;
}

// 🚨 更新签名
void ModelRenderer::Render(ID3D11DeviceContext* context, const XMMATRIX& viewProjection, const XMMATRIX& world, const XMFLOAT3& cameraPos, XMFLOAT4 matParams, XMFLOAT4 colorTint) {
    if (!m_vb || !m_ib) return;

    VSConst cb;
    XMMATRIX wvp = world * viewProjection;
    cb.WVP = XMMatrixTranspose(wvp);
    cb.World = XMMatrixTranspose(world);
    cb.CameraPos = cameraPos;
    cb.MatParams = matParams; // 传入参数
    cb.ColorTint = colorTint; // 传入颜色
    
    context->UpdateSubresource(m_cb, 0, nullptr, &cb, 0, 0);

    UINT stride = sizeof(ModelVertex); UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
    context->IASetIndexBuffer(m_ib, DXGI_FORMAT_R32_UINT, 0);
    context->IASetInputLayout(m_inputLayout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(m_vs, nullptr, 0);
    context->PSSetShader(m_ps, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_cb);
    context->PSSetConstantBuffers(0, 1, &m_cb);
    ID3D11ShaderResourceView* srvs[2] = { m_diffuseSRV, m_normalSRV };
    context->PSSetShaderResources(0, 2, srvs);
    if (m_sampler) context->PSSetSamplers(0, 1, &m_sampler);

    // 🚨 绑定光栅化状态
    context->RSSetState(m_rasterizerState);

    context->DrawIndexed(m_indexCount, 0, 0);
    
    // 恢复默认光栅化状态（以免影响其他渲染器）
    context->RSSetState(nullptr); 
}