#include "CubeRenderer.h"
#include <d3dcompiler.h>
#include <math.h>

using namespace DirectX;

CubeRenderer::CubeRenderer() : m_vertexShader(nullptr), m_pixelShader(nullptr), m_inputLayout(nullptr), 
    m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_constantBuffer(nullptr), m_samplerState(nullptr) {}

CubeRenderer::~CubeRenderer() { Cleanup(); }

void CubeRenderer::Cleanup() {
    if (m_samplerState) { m_samplerState->Release(); m_samplerState = nullptr; }
    if (m_constantBuffer) { m_constantBuffer->Release(); m_constantBuffer = nullptr; }
    if (m_indexBuffer) { m_indexBuffer->Release(); m_indexBuffer = nullptr; }
    if (m_vertexBuffer) { m_vertexBuffer->Release(); m_vertexBuffer = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_pixelShader) { m_pixelShader->Release(); m_pixelShader = nullptr; }
    if (m_vertexShader) { m_vertexShader->Release(); m_vertexShader = nullptr; }
}

bool CubeRenderer::Initialize(ID3D11Device* device) {
   const char* shaderCode = R"(
        cbuffer ConstantBuffer : register(b0) { 
            matrix WVP; matrix World; matrix ViewProj;
            float4 Color; float3 LightDir; float padding; 
            float3 LocalCamPos; int HoverState; 
            float3 WorldCamPos; float Radius; 
        }
        Texture2D shaderTexture : register(t0);
        SamplerState sampleType : register(s0);

        struct VS_IN { float3 pos : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD; };
        struct PS_IN { float4 pos : SV_POSITION; float3 localPos : TEXCOORD0; float2 uv : TEXCOORD1; float3 worldPos : TEXCOORD2; };
        
        // 🚨 核心黑魔法：允许像素着色器直接改写 3D 深度！
        struct PS_OUT { float4 color : SV_Target; float depth : SV_Depth; };

        PS_IN VS_Main(VS_IN input) {
            PS_IN output;
            output.pos = mul(float4(input.pos, 1.0f), WVP);
            output.localPos = input.pos; 
            output.uv = input.uv;
            output.worldPos = mul(float4(input.pos, 1.0f), World).xyz; 
            return output;
        }

        PS_OUT PS_Main(PS_IN input) {
            PS_OUT output;
            output.color = float4(0,0,0,0);
            output.depth = input.pos.z; // 默认深度

            // 🚨 【方案A：完美真实厚度曲面】
            if (HoverState == 5) {
                float3 rayDir = normalize(input.worldPos - WorldCamPos);
                float yaw = atan2(rayDir.x, rayDir.z);
                float pitch = asin(rayDir.y);
                
                float yawDiff = yaw - Color.x;
                if (yawDiff > 3.14159265f) yawDiff -= 6.2831853f;
                if (yawDiff < -3.14159265f) yawDiff += 6.2831853f;
                yawDiff = abs(yawDiff);
                float pitchDiff = abs(pitch - Color.z);
                
                // 裁切出扇区
                if (yawDiff <= Color.y && pitchDiff <= Color.w) {
                    float edgeY = Color.y - yawDiff;
                    float edgeP = Color.w - pitchDiff;
                    bool isBorder = (edgeY < 0.005f || edgeP < 0.005f);
                    output.color = float4(0.0f, 0.47f, 0.83f, isBorder ? 0.9f : 0.15f);
                    
                    // 🚨 空间雕刻：将这片玻璃的真实 3D 曲面深度注入显卡！
                    float3 hitPos = WorldCamPos + rayDir * Radius;
                    float4 clipPos = mul(float4(hitPos, 1.0f), ViewProj);
                    output.depth = clipPos.z / clipPos.w; // 完美 Z-Buffer 遮挡！
                    return output;
                } else {
                    discard; return output;
                }
            }

            // 🚨 【方案B：角连接光柱】
            if (HoverState == 6) {
                output.color = float4(0.0f, 0.47f, 0.83f, 0.9f);
                return output;
            }

            // --- 常规图标渲染 ---
            float edgeStart = 0.480f; float feather = 0.015f; 
            float ex = smoothstep(edgeStart, edgeStart + feather, abs(input.localPos.x));
            float ey = smoothstep(edgeStart, edgeStart + feather, abs(input.localPos.y));
            float ez = smoothstep(edgeStart, edgeStart + feather, abs(input.localPos.z));
            float edgeIntensity = saturate(((ex * ey) + (ey * ez) + (ex * ez)) * 1.5f);

            float4 boxColor = float4(0, 0, 0, 0);
            if (HoverState == 4) { 
                output.color = float4(0.0f, 0.47f, 0.83f, lerp(0.1f, 0.9f, edgeIntensity)); return output; 
            }
            if (HoverState > 0) {
                float currentAlpha = lerp(HoverState >= 2 ? 0.2f : 0.05f, HoverState >= 2 ? 0.9f : 0.4f, edgeIntensity);
                boxColor = float4(0.9f, 0.95f, 1.0f, currentAlpha);
            }

            float3 rayDir = normalize(input.localPos - LocalCamPos);
            float stepSize = 1.732f / 60.0f; 
            float3 stepVec = rayDir * stepSize;
            float3 currentPos = input.localPos + rayDir * 0.001f; 
            
            float4 hitColor = float4(0, 0, 0, 0);
            float isSide = 0.0f;

            for(int i = 0; i < 60; i++) {
                if(abs(currentPos.x) > 0.501f || abs(currentPos.y) > 0.501f || abs(currentPos.z) > 0.501f) break;
                float cx = currentPos.x * 1.25f; float cy = currentPos.y * 1.25f;
                if (abs(cx) < 0.5f && abs(cy) < 0.5f) {
                    float2 uv = float2(cx + 0.5f, 0.5f - cy);
                    float4 tex = shaderTexture.SampleLevel(sampleType, uv, 0);
                    if(tex.a > 0.65f) { 
                        hitColor = float4((tex.r+tex.g+tex.b) < 0.1f ? Color.rgb : tex.rgb, 1.0f);
                        isSide = (abs(currentPos.z * 1.25f) < 0.48f) ? 1.0f : 0.0f;
                        break;
                    }
                }
                currentPos += stepVec; 
            }
            
            if(hitColor.a > 0.1f) { output.color = float4(hitColor.rgb * (isSide > 0.5f ? 0.85f : 1.0f), 1.0f); return output; }
            if (boxColor.a > 0.0f) { output.color = boxColor; return output; }
            discard; return output;
        }
    )";

    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr; ID3DBlob* errBlob = nullptr;
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "VS_Main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "PS_Main", "ps_5_0", 0, 0, &psBlob, &errBlob);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
    vsBlob->Release(); psBlob->Release();

    Vertex vertices[] = {
        { {-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {0, 1} }, { {-0.5f, 0.5f, -0.5f}, {0, 0, -1}, {0, 0} },
        { { 0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 0} }, { { 0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1} },
        { {-0.5f, -0.5f,  0.5f}, {0, 0, 1},  {0, 1} }, { { 0.5f, -0.5f,  0.5f}, {0, 0, 1},  {1, 1} },
        { { 0.5f,  0.5f,  0.5f}, {0, 0, 1},  {1, 0} }, { {-0.5f,  0.5f,  0.5f}, {0, 0, 1},  {0, 0} },
        { {-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {0, 0} }, { {-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {0, 0} },
        { {-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {0, 0} }, { {-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0, 0} },
        { { 0.5f, -0.5f, -0.5f}, {1, 0, 0},  {0, 0} }, { { 0.5f,  0.5f, -0.5f}, {1, 0, 0},  {0, 0} },
        { { 0.5f,  0.5f,  0.5f}, {1, 0, 0},  {0, 0} }, { { 0.5f, -0.5f,  0.5f}, {1, 0, 0},  {0, 0} },
        { {-0.5f,  0.5f, -0.5f}, {0, 1, 0},  {0, 0} }, { {-0.5f,  0.5f,  0.5f}, {0, 1, 0},  {0, 0} },
        { { 0.5f,  0.5f,  0.5f}, {0, 1, 0},  {0, 0} }, { { 0.5f,  0.5f, -0.5f}, {0, 1, 0},  {0, 0} },
        { {-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {0, 0} }, { { 0.5f, -0.5f, -0.5f}, {0, -1, 0}, {0, 0} },
        { { 0.5f, -0.5f,  0.5f}, {0, -1, 0}, {0, 0} }, { {-0.5f, -0.5f,  0.5f}, {0, -1, 0}, {0, 0} }
    };
    D3D11_BUFFER_DESC vbd = {}; vbd.Usage = D3D11_USAGE_DEFAULT; vbd.ByteWidth = sizeof(vertices); vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vInit = {}; vInit.pSysMem = vertices;
    device->CreateBuffer(&vbd, &vInit, &m_vertexBuffer);

    unsigned int indices[] = { 0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11, 12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23 };
    D3D11_BUFFER_DESC ibd = {}; ibd.Usage = D3D11_USAGE_DEFAULT; ibd.ByteWidth = sizeof(indices); ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iInit = {}; iInit.pSysMem = indices;
    device->CreateBuffer(&ibd, &iInit, &m_indexBuffer);

    D3D11_BUFFER_DESC cbd = {}; cbd.Usage = D3D11_USAGE_DEFAULT; cbd.ByteWidth = sizeof(ConstantBufferType); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cbd, nullptr, &m_constantBuffer);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    device->CreateSamplerState(&sampDesc, &m_samplerState);

    return true;
}

void CubeRenderer::Render(ID3D11DeviceContext* context, XMMATRIX viewProjection, XMFLOAT3 position, XMFLOAT3 scale, XMFLOAT4 color, ID3D11ShaderResourceView* texture, XMFLOAT3 cameraPos, int hoverState, XMMATRIX viewMatrix, float radius, float spinAngle, float orbitAngle, float tiltAngle) {
    XMMATRIX world;
    if (hoverState == 4 || hoverState == 5) {
        // 画布永远平行于视野
        XMVECTOR det; XMMATRIX invView = XMMatrixInverse(&det, viewMatrix);
        invView.r[3] = XMVectorSet(0, 0, 0, 1); 
        world = XMMatrixScaling(scale.x, scale.y, scale.z) * invView * XMMatrixTranslation(position.x, position.y, position.z);
    } else if (hoverState == 6) {
        // 角连线光柱，直接利用传入的角度进行完美旋转
        world = XMMatrixScaling(scale.x, scale.y, scale.z) * 
                XMMatrixRotationX(-color.y) * 
                XMMatrixRotationY(color.x) * 
                XMMatrixTranslation(position.x, position.y, position.z);
    } else {
        float angleY = atan2(position.x - cameraPos.x, position.z - cameraPos.z);
        float orbitOffset = 0.04f * scale.x;
        XMFLOAT3 orbitPos = position;
        if (orbitAngle != 0.0f) {
            orbitPos.x += cosf(orbitAngle) * orbitOffset;
            orbitPos.z += sinf(orbitAngle) * orbitOffset;
        }
        XMVECTOR spinAxis = XMVectorSet(cosf(orbitAngle) * 0.6f, 0.8f, sinf(orbitAngle) * 0.6f, 0.0f);
        spinAxis = XMVector3Normalize(spinAxis);
        XMMATRIX spinRot = (spinAngle != 0.0f) ? XMMatrixRotationAxis(spinAxis, spinAngle) : XMMatrixIdentity();
        XMMATRIX tiltRot = (tiltAngle != 0.0f) ? XMMatrixRotationX(tiltAngle) : XMMatrixIdentity();
        XMMATRIX faceRot = XMMatrixRotationY(angleY);
        world = XMMatrixScaling(scale.x, scale.y, scale.z) *
                spinRot *
                tiltRot *
                faceRot *
                XMMatrixTranslation(orbitPos.x, orbitPos.y, orbitPos.z);
    }

    XMVECTOR det; XMMATRIX invWorld = XMMatrixInverse(&det, world);
    XMVECTOR camPosVec = XMLoadFloat3(&cameraPos);
    XMVECTOR localCamPosVec = XMVector3TransformCoord(camPosVec, invWorld);
    XMFLOAT3 localCamPos; XMStoreFloat3(&localCamPos, localCamPosVec);

    ConstantBufferType cb;
    cb.WVP = XMMatrixTranspose(world * viewProjection);
    cb.World = XMMatrixTranspose(world);
    cb.ViewProj = XMMatrixTranspose(viewProjection); // 🚨 传入投影矩阵
    cb.Color = color; 
    cb.LightDir = { -0.5f, -1.0f, 0.5f }; 
    cb.LocalCamPos = localCamPos; 
    cb.HoverState = hoverState; 
    cb.WorldCamPos = cameraPos;  
    cb.Radius = radius;          // 🚨 传入真实半径

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
    context->PSSetSamplers(0, 1, &m_samplerState); 
    
    if (texture) context->PSSetShaderResources(0, 1, &texture);
    else { ID3D11ShaderResourceView* nullSRV = nullptr; context->PSSetShaderResources(0, 1, &nullSRV); }

    context->DrawIndexed(36, 0, 0);
}