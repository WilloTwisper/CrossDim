#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include "ObjLoader.h"

class ModelRenderer {
public:
    ModelRenderer();
    ~ModelRenderer();

    bool Initialize(ID3D11Device* device);
    void Cleanup();

    // Load OBJ (uses Engine::ObjLoader) into GPU buffers
    bool LoadModelAsync(const std::string& filepath);
    // Poll on main thread to finalize any pending async loads (create D3D buffers, textures)
    void PollFinalizeLoad();
    bool SetDiffuseTexture(const std::wstring& path);
    bool SetNormalTexture(const std::wstring& path);

    // Render with a world matrix and viewProjection matrix
    void Render(ID3D11DeviceContext* context, const DirectX::XMMATRIX& viewProjection, const DirectX::XMMATRIX& world, const DirectX::XMFLOAT3& cameraPos, DirectX::XMFLOAT4 matParams, DirectX::XMFLOAT4 colorTint);

private:
    ID3D11Device* m_device;
    ID3D11VertexShader* m_vs;
    ID3D11PixelShader* m_ps;
    ID3D11InputLayout* m_inputLayout;
    ID3D11Buffer* m_vb;
    ID3D11Buffer* m_ib;
    ID3D11Buffer* m_cb;
    int m_indexCount;
    ID3D11ShaderResourceView* m_diffuseSRV;
    ID3D11SamplerState* m_sampler;
    ID3D11ShaderResourceView* m_normalSRV;
    
    // 🚨 新增：光栅化状态（用于关闭背面剔除，支持镜像翻转）
    ID3D11RasterizerState* m_rasterizerState; 

    // Async load state (保持不变)
    std::atomic<bool> m_loadInProgress{false};
    std::atomic<bool> m_loadReady{false};
    std::thread m_loadThread;
    std::mutex m_pendingMutex;
    std::vector<ModelVertex> m_pendingVerts;
    std::vector<unsigned int> m_pendingInds;
    std::wstring m_pendingDiffusePathW;
    std::wstring m_pendingNormalPathW;
    // Model bounds center (in model/local space)
    DirectX::XMFLOAT3 m_modelCenter;
public:
    DirectX::XMFLOAT3 GetModelCenter() const { return m_modelCenter; }
    bool HasModel() const { return m_indexCount > 0; }
    bool IsLoading() const { return m_loadInProgress.load(); }
};
