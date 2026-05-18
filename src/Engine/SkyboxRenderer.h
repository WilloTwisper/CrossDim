#pragma once
#include <d3d11.h>
#include <DirectXMath.h>

class SkyboxRenderer {
public:
    SkyboxRenderer();
    ~SkyboxRenderer();

    bool Initialize(ID3D11Device* device);
    void Cleanup();
    
    // 🚨 新增 cameraPos 参数，确保宇宙视角绝对正确
    void Render(ID3D11DeviceContext* context, DirectX::XMMATRIX invViewProj, DirectX::XMFLOAT3 cameraPos);

private:
    ID3D11VertexShader* m_vertexShader;
    ID3D11PixelShader*  m_pixelShader;
    ID3D11Buffer*       m_constantBuffer;
    
    // 🚨 新增：强行接管显卡的渲染状态！
    ID3D11RasterizerState*   m_rasterState;
    ID3D11DepthStencilState* m_depthState;

    struct ConstantBufferType {
        DirectX::XMMATRIX InvViewProj; 
        DirectX::XMFLOAT3 CameraPos; // 相机位置
        float padding;
    };
};