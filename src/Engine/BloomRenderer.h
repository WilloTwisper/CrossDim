#pragma once
#include <d3d11.h>
#include <DirectXMath.h>

class BloomRenderer {
public:
    BloomRenderer();
    ~BloomRenderer();

    bool Initialize(ID3D11Device* device);
    void Cleanup();
    
    // 🚨 接口大升级：支持传入世界变换矩阵、厚度、颜色等十几个参数
     void Render(ID3D11DeviceContext* context, DirectX::XMMATRIX viewProjection, DirectX::XMFLOAT3 cameraPos,
                DirectX::XMMATRIX worldMatrix, float width, 
                float archRadius, float archAngle, float foldRadius, float foldAngle, float thickness, 
                DirectX::XMFLOAT3 coreColor, DirectX::XMFLOAT3 edgeColor);
private:
    struct Vertex {
        DirectX::XMFLOAT3 Pos;  // x: u(长度 0~1), y: v(宽度 -1~1), z: w(厚度 -1~1)
        DirectX::XMFLOAT2 UV;
    };

    struct ConstantBufferType {
        DirectX::XMMATRIX WVP;
        DirectX::XMMATRIX World; 
        DirectX::XMFLOAT3 CameraPos; float Width;
        
        // 🚨 换成你提出的“两步弯折”参数！
        DirectX::XMFLOAT4 ArchParams; // x: ArchRadius(∩的半径), y: ArchAngle(∩的弯曲度), z: FoldRadius(⊂的半径), w: FoldAngle(⊂的弯曲度)
        DirectX::XMFLOAT4 StyleParams; // x: Thickness(厚度), y: pad, z: pad, w: pad
        DirectX::XMFLOAT4 ColorCore; 
        DirectX::XMFLOAT4 ColorEdge; 
    };

    ID3D11VertexShader* m_vertexShader;
    ID3D11PixelShader*  m_pixelShader;
    ID3D11InputLayout*  m_inputLayout;
    ID3D11Buffer*       m_vertexBuffer;
    ID3D11Buffer*       m_indexBuffer;
    ID3D11Buffer*       m_constantBuffer;
    ID3D11BlendState*   m_blendState;
    ID3D11RasterizerState* m_rasterizerState;

    int m_indexCount;
};