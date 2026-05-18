#pragma once
#include <d3d11.h>
#include <DirectXMath.h>

class CubeRenderer {
public:
    CubeRenderer();
    ~CubeRenderer();

    bool Initialize(ID3D11Device* device);
    void Cleanup();

    // 🚨 新增 default 参数 radius
    void Render(ID3D11DeviceContext* context, 
                DirectX::XMMATRIX viewProjection, 
                DirectX::XMFLOAT3 position, 
                DirectX::XMFLOAT3 scale, 
                DirectX::XMFLOAT4 color,
                ID3D11ShaderResourceView* texture,
                DirectX::XMFLOAT3 cameraPos,
                int hoverState,
                DirectX::XMMATRIX viewMatrix,
                float radius = 8.0f); 

private:
    ID3D11VertexShader* m_vertexShader;
    ID3D11PixelShader*  m_pixelShader;
    ID3D11InputLayout*  m_inputLayout;
    ID3D11Buffer*       m_vertexBuffer;
    ID3D11Buffer*       m_indexBuffer;
    ID3D11Buffer*       m_constantBuffer;
    ID3D11SamplerState* m_samplerState;

    struct Vertex {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT2 UV;
    };

    struct ConstantBufferType {
        DirectX::XMMATRIX WVP;       
        DirectX::XMMATRIX World;   
        DirectX::XMMATRIX ViewProj;  // 🚨 新增：投影矩阵，用于重算深度
        DirectX::XMFLOAT4 Color;     
        DirectX::XMFLOAT3 LightDir;  
        float padding;
        DirectX::XMFLOAT3 LocalCamPos; 
        int HoverState; 
        DirectX::XMFLOAT3 WorldCamPos; 
        float Radius;                // 🚨 替换原来的 padding2，传入曲面半径！
    };
};