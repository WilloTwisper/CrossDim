#include "SkyboxRenderer.h"
#include <d3dcompiler.h>

using namespace DirectX;

SkyboxRenderer::SkyboxRenderer() : m_vertexShader(nullptr), m_pixelShader(nullptr), m_constantBuffer(nullptr),
    m_rasterState(nullptr), m_depthState(nullptr) {}

SkyboxRenderer::~SkyboxRenderer() { Cleanup(); }

void SkyboxRenderer::Cleanup() {
    if (m_depthState) { m_depthState->Release(); m_depthState = nullptr; }
    if (m_rasterState) { m_rasterState->Release(); m_rasterState = nullptr; }
    if (m_constantBuffer) { m_constantBuffer->Release(); m_constantBuffer = nullptr; }
    if (m_pixelShader) { m_pixelShader->Release(); m_pixelShader = nullptr; }
    if (m_vertexShader) { m_vertexShader->Release(); m_vertexShader = nullptr; }
}

bool SkyboxRenderer::Initialize(ID3D11Device* device) {
    const char* shaderCode = R"(
        cbuffer ConstantBuffer : register(b0) {
            matrix InvViewProj;
            float3 CameraPos;
            float padding;
        }

        struct VS_OUT {
            float4 pos : SV_POSITION;
            float3 rayDir : TEXCOORD0;
        };

        VS_OUT VS_Main(uint id : SV_VertexID) {
            VS_OUT output;
            // 最暴力的全屏覆盖三角形：坐标直达屏幕四个角之外！
            float x = (id == 1) ? 3.0f : -1.0f;
            float y = (id == 2) ? -3.0f : 1.0f;
            output.pos = float4(x, y, 0.9999f, 1.0f);

            // 逆推世界坐标，并减去相机坐标得到完美的射线方向
            float4 worldPos = mul(output.pos, InvViewProj);
            output.rayDir = normalize((worldPos.xyz / worldPos.w) - CameraPos);
            return output;
        }

                    float4 PS_Main(VS_OUT input) : SV_Target {
            // 1. 核心光源方向 (正前方 Z 轴)
            float3 bloomCenter = normalize(float3(0.0f, 0.0f, 1.0f));
            
            // 🚨 黑魔法：Win11 的 Bloom 是有倾斜视角的！
            // 我们在计算光晕时，对光线进行微微的垂直压缩和斜向拉伸，让光晕呈现出优雅的“椭圆倾斜”散发感
            float3 distortedRay = normalize(input.rayDir * float3(1.0f, 1.3f, 1.0f));
            distortedRay.x += distortedRay.y * 0.25f; // 制造高级的倾斜流动感
            distortedRay = normalize(distortedRay);

            // 2. 计算光照夹角
            float VoC = dot(distortedRay, bloomCenter);

            // 🚨 极其关键的修复：将 [-1, 1] 映射到 [0, 1]！
            // 这样背面 (0.0) 不会变成死黑，而是保留柔和的全局环境光！
            float normalizedVoC = VoC * 0.5f + 0.5f; 

            // 3. Win11 经典色彩美学 (浅蓝提亮版)
            // 背面和边缘的全局环境色 (明亮的青蓝色)
            float3 ambientBlue = float3(0.12f, 0.38f, 0.78f); 
            // 靠近中心的过渡色 (清澈的高光湖蓝)
            float3 midCyan     = float3(0.35f, 0.70f, 0.95f); 
            // 视觉正中心的爆白高光 (带着微蓝的冷白)
            float3 coreWhite   = float3(0.95f, 0.98f, 1.00f); 

            // 4. 极其丝滑的光晕过渡 (消除所有的硬块噪点)
            float3 finalColor = ambientBlue;
            
            // 用平滑的曲线从环境蓝过渡到湖蓝 (覆盖大半个空间)
            float midBlend = smoothstep(0.2f, 0.85f, normalizedVoC);
            finalColor = lerp(finalColor, midCyan, midBlend);
            
            // 用极高次方的曲线在正中心生成“刺眼但柔和”的纯白中心！
            float coreBlend = pow(normalizedVoC, 12.0f);
            finalColor = lerp(finalColor, coreWhite, coreBlend);

            // 5. 保留一丝若有若无的大尺度波动，增加空间的“厚度感” (绝对不是一小块一小块)
            float gentleWave = sin(input.rayDir.x * 2.0f + input.rayDir.y * 1.5f) * 0.03f;
            finalColor += gentleWave * midBlend;

            // 6. 影视级胶片噪点 (Dithering)，消除大面积纯色渐变带来的色彩断层
            float noiseGrain = frac(sin(dot(input.rayDir.xy, float2(12.9898, 78.233))) * 43758.5453) * 0.012f;
            
            return float4(finalColor + noiseGrain, 1.0f);
        }
    )";

    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr; ID3DBlob* errBlob = nullptr;
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "VS_Main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "PS_Main", "ps_5_0", 0, 0, &psBlob, &errBlob);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);

    if (vsBlob) vsBlob->Release();
    if (psBlob) psBlob->Release();
    if (errBlob) errBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(ConstantBufferType);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cbd, nullptr, &m_constantBuffer);

    // 🚨 黑魔法：创造一个“绝对不剔除背面”的光栅化状态
    D3D11_RASTERIZER_DESC rd = {};
    rd.CullMode = D3D11_CULL_NONE; // 谁也不许剔除！
    rd.FillMode = D3D11_FILL_SOLID;
    device->CreateRasterizerState(&rd, &m_rasterState);

    // 🚨 黑魔法：创造一个“完全无视深度测试”的深度状态
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = FALSE;       // 背景不需要深度测试，直接糊在最底层！
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device->CreateDepthStencilState(&dsd, &m_depthState);

    return true;
}

void SkyboxRenderer::Render(ID3D11DeviceContext* context, XMMATRIX invViewProj, XMFLOAT3 cameraPos) {
    ConstantBufferType cb;
    cb.InvViewProj = XMMatrixTranspose(invViewProj);
    cb.CameraPos = cameraPos;
    context->UpdateSubresource(m_constantBuffer, 0, nullptr, &cb, 0, 0);

    // 1. 强行接管渲染状态
    context->RSSetState(m_rasterState);
    context->OMSetDepthStencilState(m_depthState, 0);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    context->VSSetShader(m_vertexShader, nullptr, 0);
    context->PSSetShader(m_pixelShader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
    
    // 2. 凭空画出这个包围宇宙的三角形！
    context->Draw(3, 0); 

    // 3. 极其重要：画完后把状态恢复原样，以免影响前面图标和划选框的正常渲染！
    context->RSSetState(nullptr);
    context->OMSetDepthStencilState(nullptr, 0);
}