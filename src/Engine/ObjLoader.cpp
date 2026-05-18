#include "ObjLoader.h"
#include <stdio.h>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>
#include <string>

using namespace DirectX;

namespace {
    constexpr uint32_t kMeshCacheVersion = 1;

    struct MeshCacheHeader {
        char magic[8];
        uint32_t version;
        uint32_t vertexStride;
        int64_t objMtime;
        uint64_t objSize;
        uint32_t vertexCount;
        uint32_t indexCount;
    };

    bool GetFileInfo(const std::string& path, uint64_t& outSize, int64_t& outMtime) {
        struct _stat64 st;
        if (_stat64(path.c_str(), &st) != 0) return false;
        outSize = static_cast<uint64_t>(st.st_size);
        outMtime = static_cast<int64_t>(st.st_mtime);
        return true;
    }

    std::string MakeCachePath(const std::string& objPath) {
        return objPath + ".cdmesh";
    }

    bool TryLoadMeshCache(const std::string& objPath,
                          std::vector<ModelVertex>& outVertices,
                          std::vector<unsigned int>& outIndices) {
        uint64_t objSize = 0;
        int64_t objMtime = 0;
        if (!GetFileInfo(objPath, objSize, objMtime)) return false;

        std::ifstream in(MakeCachePath(objPath), std::ios::binary);
        if (!in) return false;

        MeshCacheHeader hdr = {};
        in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!in) return false;

        if (std::memcmp(hdr.magic, "CDMESH", 6) != 0) return false;
        if (hdr.version != kMeshCacheVersion) return false;
        if (hdr.vertexStride != sizeof(ModelVertex)) return false;
        if (hdr.objMtime != objMtime || hdr.objSize != objSize) return false;

        outVertices.resize(hdr.vertexCount);
        outIndices.resize(hdr.indexCount);

        if (hdr.vertexCount > 0) {
            in.read(reinterpret_cast<char*>(outVertices.data()), sizeof(ModelVertex) * hdr.vertexCount);
        }
        if (hdr.indexCount > 0) {
            in.read(reinterpret_cast<char*>(outIndices.data()), sizeof(unsigned int) * hdr.indexCount);
        }
        if (!in) {
            outVertices.clear();
            outIndices.clear();
            return false;
        }

        return true;
    }

    void WriteMeshCache(const std::string& objPath,
                        const std::vector<ModelVertex>& vertices,
                        const std::vector<unsigned int>& indices) {
        uint64_t objSize = 0;
        int64_t objMtime = 0;
        if (!GetFileInfo(objPath, objSize, objMtime)) return;

        if (vertices.size() > UINT32_MAX || indices.size() > UINT32_MAX) return;

        std::ofstream out(MakeCachePath(objPath), std::ios::binary | std::ios::trunc);
        if (!out) return;

        MeshCacheHeader hdr = {};
        std::memcpy(hdr.magic, "CDMESH", 6);
        hdr.version = kMeshCacheVersion;
        hdr.vertexStride = static_cast<uint32_t>(sizeof(ModelVertex));
        hdr.objMtime = objMtime;
        hdr.objSize = objSize;
        hdr.vertexCount = static_cast<uint32_t>(vertices.size());
        hdr.indexCount = static_cast<uint32_t>(indices.size());

        out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        if (!vertices.empty()) {
            out.write(reinterpret_cast<const char*>(vertices.data()), sizeof(ModelVertex) * vertices.size());
        }
        if (!indices.empty()) {
            out.write(reinterpret_cast<const char*>(indices.data()), sizeof(unsigned int) * indices.size());
        }
    }
}

struct VertexKey {
    int v, vt, vn;
    bool operator==(const VertexKey& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
};

// 哈希函数，用于 unordered_map 极速查找
struct VertexKeyHash {
    std::size_t operator()(const VertexKey& k) const {
        return ((std::hash<int>()(k.v) ^ (std::hash<int>()(k.vt) << 1)) >> 1) ^ (std::hash<int>()(k.vn) << 1);
    }
};

static int fixIndex(int idx, int size) {
    if (idx < 0) return size + idx; 
    return idx - 1; 
}

bool ObjLoader::LoadObj(const std::string& filepath, std::vector<ModelVertex>& outVertices, std::vector<unsigned int>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    if (TryLoadMeshCache(filepath, outVertices, outIndices)) {
        return true;
    }

    FILE* fp = nullptr;
    fopen_s(&fp, filepath.c_str(), "r");
    if (!fp) return false;

    // 预分配大内存，绝不让 std::vector 中途扩容卡死
    std::vector<XMFLOAT3> temp_positions; temp_positions.reserve(1000000);
    std::vector<XMFLOAT2> temp_uvs;       temp_uvs.reserve(1000000);
    std::vector<XMFLOAT3> temp_normals;   temp_normals.reserve(1000000);
    std::unordered_map<VertexKey, unsigned int, VertexKeyHash> uniqueVertices;
    uniqueVertices.reserve(2000000);

    char line[512];
    // 🚨 极速 C-style 字符串读取，不占用任何额外堆内存！
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == 'v' && line[1] == ' ') {
            XMFLOAT3 p; sscanf_s(line + 2, "%f %f %f", &p.x, &p.y, &p.z);
            // Convert right-handed OBJ (Blender/AI tools) to DirectX left-handed: flip Z.
            p.z = -p.z;
            temp_positions.push_back(p);
        } else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
            XMFLOAT2 uv; sscanf_s(line + 3, "%f %f", &uv.x, &uv.y);
            uv.y = 1.0f - uv.y; temp_uvs.push_back(uv);
        } else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
            XMFLOAT3 n; sscanf_s(line + 3, "%f %f %f", &n.x, &n.y, &n.z);
            // Match position conversion.
            n.z = -n.z;
            temp_normals.push_back(n);
        } else if (line[0] == 'f' && line[1] == ' ') {
            int v[4] = {0}, vt[4] = {0}, vn[4] = {0};
            int count = 0;
            char* ptr = line + 2;
            
            while (*ptr && count < 4) {
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                if (*ptr == '\r' || *ptr == '\n' || *ptr == '\0') break;

                int vi = 0, vti = 0, vni = 0;
                // 暴力极速拆解 f 索引
                if (sscanf_s(ptr, "%d/%d/%d", &vi, &vti, &vni) == 3) { v[count] = vi; vt[count] = vti; vn[count] = vni; } 
                else if (sscanf_s(ptr, "%d//%d", &vi, &vni) == 2) { v[count] = vi; vt[count] = 0; vn[count] = vni; } 
                else if (sscanf_s(ptr, "%d/%d", &vi, &vti) == 2) { v[count] = vi; vt[count] = vti; vn[count] = 0; } 
                else if (sscanf_s(ptr, "%d", &vi) == 1) { v[count] = vi; vt[count] = 0; vn[count] = 0; }
                
                count++;
                while (*ptr && *ptr != ' ' && *ptr != '\t') ptr++;
            }

            if (count >= 3) {
                for (int i = 1; i + 1 < count; ++i) {
                    // Flip winding to preserve front-facing orientation after handedness conversion.
                    VertexKey tri[3] = { {v[0], vt[0], vn[0]}, {v[i+1], vt[i+1], vn[i+1]}, {v[i], vt[i], vn[i]} };
                    for (int k = 0; k < 3; ++k) {
                        VertexKey vk = tri[k];
                        int pi = fixIndex(vk.v, (int)temp_positions.size());
                        int ti = vk.vt != 0 ? fixIndex(vk.vt, (int)temp_uvs.size()) : -1;
                        int ni = vk.vn != 0 ? fixIndex(vk.vn, (int)temp_normals.size()) : -1;
                        VertexKey lookup = {pi, ti, ni};

                        auto it = uniqueVertices.find(lookup);
                        if (it == uniqueVertices.end()) {
                            ModelVertex vertex;
                            vertex.Position = (pi >= 0 && pi < temp_positions.size()) ? temp_positions[pi] : XMFLOAT3(0,0,0);
                            vertex.UV = (ti >= 0 && ti < temp_uvs.size()) ? temp_uvs[ti] : XMFLOAT2(0,0);
                            vertex.Normal = (ni >= 0 && ni < temp_normals.size()) ? temp_normals[ni] : XMFLOAT3(0,1,0);
                            vertex.Tangent = {0,0,0};

                            outVertices.push_back(vertex);
                            unsigned int newIndex = (unsigned int)(outVertices.size() - 1);
                            outIndices.push_back(newIndex);
                            uniqueVertices[lookup] = newIndex;
                        } else {
                            outIndices.push_back(it->second);
                        }
                    }
                }
            }
        }
    }
    fclose(fp);

    if (!outVertices.empty() && !outIndices.empty()) {
        WriteMeshCache(filepath, outVertices, outIndices);
    }

    return true;
}