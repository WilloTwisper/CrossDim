#include "BloomRenderer.h"
#include <d3dcompiler.h>
#include <vector>

using namespace DirectX;

BloomRenderer::BloomRenderer() : m_vertexShader(nullptr), m_pixelShader(nullptr), m_inputLayout(nullptr), 
    m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_constantBuffer(nullptr), m_blendState(nullptr), m_rasterizerState(nullptr), m_indexCount(0) {}

BloomRenderer::~BloomRenderer() { Cleanup(); }

void BloomRenderer::Cleanup() {
    if (m_rasterizerState) { m_rasterizerState->Release(); m_rasterizerState = nullptr; }
    if (m_blendState) { m_blendState->Release(); m_blendState = nullptr; }
    if (m_constantBuffer) { m_constantBuffer->Release(); m_constantBuffer = nullptr; }
    if (m_indexBuffer) { m_indexBuffer->Release(); m_indexBuffer = nullptr; }
    if (m_vertexBuffer) { m_vertexBuffer->Release(); m_vertexBuffer = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_pixelShader) { m_pixelShader->Release(); m_pixelShader = nullptr; }
    if (m_vertexShader) { m_vertexShader->Release(); m_vertexShader = nullptr; }
}

bool BloomRenderer::Initialize(ID3D11Device* device) {
    const char* shaderCode = R"(
        cbuffer ConstantBuffer : register(b0) { 
            matrix WVP; matrix World;
            float3 CameraPos; float Width;
            float4 ArchParams; // x: ArchRadius, y: ArchAngle, z: FoldRadius, w: FoldAngle
            float4 StyleParams; // x: Thickness
            float4 ColorCore; float4 ColorEdge;
        }

        struct VS_IN { float3 pos : POSITION; float2 uv : TEXCOORD; };
        struct PS_IN { float4 pos : SV_POSITION; float3 worldPos : TEXCOORD0; float3 normal : NORMAL; float2 uv : TEXCOORD1; };

        PS_IN VS_Main(VS_IN input) {
            PS_IN output;
            float t = input.pos.x * 2.0f - 1.0f; // 长度 U (-1 到 1)
            float v = input.pos.y;               // 宽度 V (-1 到 1)

            float ArchRadius = ArchParams.x; float ArchAngle = ArchParams.y;
            float FoldRadius = ArchParams.z; float FoldAngle = ArchParams.w;
            float Thickness  = StyleParams.x;
            float ribbonHalfWidth = max(Width * 0.5f, 0.001f);

            // 宽度同时影响折叠半径和折叠角，避免出现“宽度参数无效”与局部撕裂。
            float band = v * ribbonHalfWidth;
            float foldRadius = max(FoldRadius + band, 0.05f);
            float theta = (band / max(FoldRadius, 0.001f)) * FoldAngle;

            // ========================================================
            // 两步弯折：先把横向条带折成 ⊂，再整体拱成 ∩
            // ========================================================
            
            // 第一步：沿横向宽度折出侧面轮廓
            float3 folded;
            folded.x = t; // X 轴保留为纯长度
            folded.y = foldRadius * cos(theta) - FoldRadius;
            folded.z = foldRadius * sin(theta);
            
            // 让厚度是稳定的局部挤出，而不是沿闭合轮廓的扭曲偏移。
            float3 foldNormal = normalize(float3(0.0f, cos(theta), sin(theta)));
            folded += foldNormal * (Thickness * 0.5f);

            // 第二步：沿长度方向再拱成 ∩
            float phi = t * ArchAngle;
            float r_arch = ArchRadius + folded.y;
            
            float3 finalPos;
            finalPos.x = r_arch * sin(phi);
            finalPos.y = r_arch * cos(phi) - ArchRadius;
            finalPos.z = folded.z;

            // ========================================================
            // 用解析切线计算法线，避免三角片在大曲率下跳变。
            // ========================================================
            float3 dT;
            dT.x = ArchAngle * r_arch * cos(phi);
            dT.y = -ArchAngle * r_arch * sin(phi);
            dT.z = 0.0f;

            float3 dV;
            float dBand = ribbonHalfWidth;
            float dTheta = (dBand / max(FoldRadius, 0.001f)) * FoldAngle;
            float dFoldRadius = dBand;
            float dFoldY = dFoldRadius * cos(theta) - foldRadius * sin(theta) * dTheta;
            float dFoldZ = dFoldRadius * sin(theta) + foldRadius * cos(theta) * dTheta;
            dV.x = dFoldY * sin(phi);
            dV.y = dFoldY * cos(phi);
            dV.z = dFoldZ;

            float3 normal = normalize(cross(dT, dV));
            // ========================================================

            float4 worldPos = mul(float4(finalPos, 1.0f), World);
            output.pos = mul(worldPos, WVP);
            output.worldPos = worldPos.xyz;
            output.normal = normalize(mul(normal, (float3x3)World));
            output.uv = input.uv;
            return output;
        }

        float4 PS_Main(PS_IN input) : SV_Target {
            float3 viewDir = normalize(CameraPos - input.worldPos);
            
            // 经典的菲涅尔效应：完美还原“内部深蓝，边缘泛起亮光浅蓝”
            float NdotV = abs(dot(normalize(input.normal), viewDir));
            float fresnel = pow(1.0f - NdotV, 2.0f); 
            
            float3 finalColor = lerp(ColorCore.rgb, ColorEdge.rgb, fresnel);
            
            // 加强首尾渐隐效果：扩大淡出范围并用幂次曲线加强衰减
            float edgeFadeStart = smoothstep(0.0f, 0.15f, input.uv.x);
            float edgeFadeEnd = smoothstep(1.0f, 0.85f, input.uv.x);
            float edgeFade = edgeFadeStart * edgeFadeEnd;
            // 用平方加强衰减曲线
            edgeFade = edgeFade * edgeFade;
            
            float alpha = lerp(0.2f, 0.95f, fresnel) * edgeFade;

            return float4(finalColor, alpha);
        }
    )";

    // 编译着色器...
    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr;
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "VS_Main", "vs_5_0", 0, 0, &vsBlob, nullptr);
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "PS_Main", "ps_5_0", 0, 0, &psBlob, nullptr);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
    vsBlob->Release(); psBlob->Release();

    // 规整的参数化丝带面：移除闭合轮廓拼接，避免高曲率时的碎片锯齿。
    int segU = 160;
    int segV = 32;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    vertices.reserve((segU + 1) * (segV + 1));
    indices.reserve(segU * segV * 6);

    // 铺设顶点：u 负责长度，v 负责宽度。
    for (int i = 0; i <= segU; ++i) {
        float u = (float)i / segU;
        for (int j = 0; j <= segV; ++j) {
            float v = -1.0f + 2.0f * (float)j / segV;
            vertices.push_back({ {u, v, 0.0f}, {u, (float)j / segV} });
        }
    }

    // 用规整网格缝合三角形，避免轮廓首尾拼接造成的尖刺和闪烁。
    for (int i = 0; i < segU; ++i) {
        for (int j = 0; j < segV; ++j) {
            int current = i * (segV + 1) + j;
            int next = current + (segV + 1);
            int right = current + 1;
            int nextRight = next + 1;
            
            indices.push_back(current); indices.push_back(next); indices.push_back(right);
            indices.push_back(right); indices.push_back(next); indices.push_back(nextRight);
        }
    }
    m_indexCount = indices.size();

    D3D11_BUFFER_DESC vbd = {}; vbd.Usage = D3D11_USAGE_IMMUTABLE; vbd.ByteWidth = sizeof(Vertex) * vertices.size(); vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vInit = {}; vInit.pSysMem = vertices.data();
    device->CreateBuffer(&vbd, &vInit, &m_vertexBuffer);

    D3D11_BUFFER_DESC ibd = {}; ibd.Usage = D3D11_USAGE_IMMUTABLE; ibd.ByteWidth = sizeof(unsigned int) * indices.size(); ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iInit = {}; iInit.pSysMem = indices.data();
    device->CreateBuffer(&ibd, &iInit, &m_indexBuffer);

    D3D11_BUFFER_DESC cbd = {}; cbd.Usage = D3D11_USAGE_DEFAULT; cbd.ByteWidth = sizeof(ConstantBufferType); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cbd, nullptr, &m_constantBuffer);

    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; 
    device->CreateRasterizerState(&rd, &m_rasterizerState);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &m_blendState);

    return true;
}

void BloomRenderer::Render(ID3D11DeviceContext* context, XMMATRIX viewProjection, XMFLOAT3 cameraPos,
                           XMMATRIX worldMatrix, float width, 
                           float archRadius, float archAngle, float foldRadius, float foldAngle, float thickness, 
                           XMFLOAT3 coreColor, XMFLOAT3 edgeColor) {
    ConstantBufferType cb;
    cb.WVP = XMMatrixTranspose(viewProjection);
    cb.World = XMMatrixTranspose(worldMatrix);
    cb.CameraPos = cameraPos;
    cb.Width = width;
    
    // 🚨 传入最新的逻辑参数
    cb.ArchParams = { archRadius, archAngle, foldRadius, foldAngle };
    cb.StyleParams = { thickness, 0.0f, 0.0f, 0.0f };
    
    cb.ColorCore = { coreColor.x, coreColor.y, coreColor.z, 1.0f };
    cb.ColorEdge = { edgeColor.x, edgeColor.y, edgeColor.z, 1.0f };

    context->UpdateSubresource(m_constantBuffer, 0, nullptr, &cb, 0, 0);

    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(m_inputLayout);

    context->VSSetShader(m_vertexShader, nullptr, 0);
    context->PSSetShader(m_pixelShader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
    context->PSSetConstantBuffers(0, 1, &m_constantBuffer);

    context->RSSetState(m_rasterizerState);
    context->OMSetBlendState(m_blendState, nullptr, 0xFFFFFFFF);

    context->DrawIndexed(m_indexCount, 0, 0);
}