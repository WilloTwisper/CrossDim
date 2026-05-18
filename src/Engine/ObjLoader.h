#pragma once
#include <vector>
#include <string>
#include <DirectXMath.h>
#include <d3d11.h>

struct ModelVertex {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT2 UV;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT3 Tangent;
};

class ObjLoader {
public:
    // 专门为 100 万面优化的极速加载器
    static bool LoadObj(const std::string& filepath, 
                        std::vector<ModelVertex>& outVertices, 
                        std::vector<unsigned int>& outIndices);
};