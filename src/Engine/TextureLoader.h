#pragma once
#include <d3d11.h>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <wincodec.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

// Shared icon cache + async pixel extract. GPU SRVs are created on the main
// thread (D3D requirement); GDI extract runs on a worker so desktop scans
// don't stall the render loop. Cache owns every SRV — cubes/taskbar hold
// non-owning pointers and must not Release them.

class TextureLoader {
public:
    static constexpr int kIconSize = 256;

    static ID3D11ShaderResourceView* LoadIconFromHandle(ID3D11Device* device, HICON iconHandle) {
        if (!device || !iconHandle) return nullptr;
        std::vector<BYTE> bgra;
        if (!BlitIconToBGRA(iconHandle, bgra)) return nullptr;
        return CreateSRVFromBGRA(device, bgra);
    }

    static ID3D11ShaderResourceView* LoadIconFromExe(ID3D11Device* device, LPCWSTR exePath) {
        if (!device || !exePath) return nullptr;
        std::vector<BYTE> bgra;
        if (!ExtractIconBGRA(exePath, bgra)) return nullptr;
        return CreateSRVFromBGRA(device, bgra);
    }

    // Cache hit or synchronous extract+upload. Taskbar / drop-target use this.
    static ID3D11ShaderResourceView* GetCachedIcon(ID3D11Device* device, const std::wstring& path) {
        if (path.empty() || !device) return nullptr;
        auto& st = State();
        {
            std::lock_guard<std::mutex> lock(st.mu);
            auto it = st.cache.find(path);
            if (it != st.cache.end()) return it->second;
            if (st.inflight.count(path)) return nullptr;
            st.inflight.insert(path);
        }
        ID3D11ShaderResourceView* icon = LoadIconFromExe(device, path.c_str());
        if (!icon) {
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) {
                icon = LoadIconFromHandle(device, sfi.hIcon);
                DestroyIcon(sfi.hIcon);
            }
        }
        {
            std::lock_guard<std::mutex> lock(st.mu);
            st.inflight.erase(path);
            auto it = st.cache.find(path);
            if (it != st.cache.end()) {
                if (icon) icon->Release();
                return it->second;
            }
            st.cache[path] = icon;
        }
        return icon;
    }

    static ID3D11ShaderResourceView* PeekCachedIcon(const std::wstring& path) {
        auto& st = State();
        std::lock_guard<std::mutex> lock(st.mu);
        auto it = st.cache.find(path);
        return (it != st.cache.end()) ? it->second : nullptr;
    }

    static void EnqueueIconLoad(const std::wstring& path) {
        if (path.empty()) return;
        auto& st = State();
        {
            std::lock_guard<std::mutex> lock(st.mu);
            if (st.cache.count(path) || st.inflight.count(path)) return;
            st.inflight.insert(path);
            st.queue.push_back(path);
        }
        EnsureWorker();
        if (st.wake) SetEvent(st.wake);
    }

    static void PollIconLoads(ID3D11Device* device) {
        if (!device) return;
        auto& st = State();
        std::vector<IconPixels> ready;
        {
            std::lock_guard<std::mutex> lock(st.mu);
            if (st.ready.empty()) return;
            ready.swap(st.ready);
        }
        for (auto& item : ready) {
            ID3D11ShaderResourceView* srv = nullptr;
            if (!item.bgra.empty()) srv = CreateSRVFromBGRA(device, item.bgra);
            std::lock_guard<std::mutex> lock(st.mu);
            st.inflight.erase(item.path);
            auto it = st.cache.find(item.path);
            if (it != st.cache.end()) {
                if (srv) srv->Release();
                continue;
            }
            st.cache[item.path] = srv;
        }
    }

    static void PruneIconCache(const std::unordered_set<std::wstring>& keep) {
        auto& st = State();
        std::lock_guard<std::mutex> lock(st.mu);
        for (auto it = st.cache.begin(); it != st.cache.end(); ) {
            if (keep.count(it->first)) { ++it; continue; }
            if (it->second) it->second->Release();
            it = st.cache.erase(it);
        }
    }

    static void ShutdownIconCache() {
        auto& st = State();
        st.stop.store(true);
        if (st.wake) SetEvent(st.wake);
        if (st.worker.joinable()) st.worker.join();
        if (st.wake) { CloseHandle(st.wake); st.wake = nullptr; }
        st.started = false;
        std::lock_guard<std::mutex> lock(st.mu);
        for (auto& kv : st.cache) {
            if (kv.second) kv.second->Release();
        }
        st.cache.clear();
        st.inflight.clear();
        st.queue.clear();
        st.ready.clear();
    }

    static ID3D11ShaderResourceView* LoadTextureFromFile(ID3D11Device* device, const std::wstring& filePath) {
        IWICImagingFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return nullptr;

        IWICBitmapDecoder* decoder = nullptr;
        if (FAILED(factory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) { factory->Release(); return nullptr; }

        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(decoder->GetFrame(0, &frame))) { decoder->Release(); factory->Release(); return nullptr; }

        IWICFormatConverter* converter = nullptr;
        factory->CreateFormatConverter(&converter);
        if (!converter) { frame->Release(); decoder->Release(); factory->Release(); return nullptr; }

        converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

        UINT width = 0, height = 0;
        frame->GetSize(&width, &height);

        std::vector<BYTE> pixels(width * height * 4);
        converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data());

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width; desc.Height = height; desc.MipLevels = 0; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &tex);

        ID3D11DeviceContext* ctx = nullptr;
        if (SUCCEEDED(hr) && tex) {
            device->GetImmediateContext(&ctx);
            if (ctx) ctx->UpdateSubresource(tex, 0, nullptr, pixels.data(), width * 4, 0);
        }

        converter->Release(); frame->Release(); decoder->Release(); factory->Release();

        if (FAILED(hr) || !tex) {
            if (ctx) ctx->Release();
            return nullptr;
        }

        ID3D11ShaderResourceView* srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = (UINT)-1;
        device->CreateShaderResourceView(tex, &srvDesc, &srv);

        if (ctx) {
            if (srv) ctx->GenerateMips(srv);
            ctx->Release();
        }

        tex->Release();
        return srv;
    }

private:
    struct IconPixels {
        std::wstring path;
        std::vector<BYTE> bgra;
    };

    struct IconCacheState {
        std::mutex mu;
        std::unordered_map<std::wstring, ID3D11ShaderResourceView*> cache;
        std::unordered_set<std::wstring> inflight;
        std::deque<std::wstring> queue;
        std::vector<IconPixels> ready;
        std::thread worker;
        std::atomic<bool> stop{false};
        HANDLE wake = nullptr;
        bool started = false;
    };

    static IconCacheState& State() {
        static IconCacheState s;
        return s;
    }

    static bool BlitIconToBGRA(HICON hIcon, std::vector<BYTE>& out) {
        const int texSize = kIconSize;
        HDC hdcScreen = GetDC(NULL);
        if (!hdcScreen) return false;
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) { ReleaseDC(NULL, hdcScreen); return false; }

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texSize;
        bi.bmiHeader.biHeight = -texSize;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
        if (!hBitmap || !pBits) {
            if (hBitmap) DeleteObject(hBitmap);
            DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);
        memset(pBits, 0, texSize * texSize * 4);
        DrawIconEx(hdcMem, 0, 0, hIcon, texSize, texSize, 0, NULL, DI_NORMAL);

        out.assign((BYTE*)pBits, (BYTE*)pBits + texSize * texSize * 4);

        SelectObject(hdcMem, hOld);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return true;
    }

    static bool ExtractIconBGRA(LPCWSTR exePath, std::vector<BYTE>& out) {
        HICON hIcon = NULL;
        PrivateExtractIconsW(exePath, 0, kIconSize, kIconSize, &hIcon, NULL, 1, LR_LOADFROMFILE);
        if (!hIcon) ExtractIconExW(exePath, 0, &hIcon, NULL, 1);
        if (!hIcon) {
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(exePath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
                hIcon = sfi.hIcon;
            }
        }
        if (!hIcon) {
            HICON fallback = (HICON)LoadImageW(NULL, reinterpret_cast<LPCWSTR>(ULONG_PTR(32512)), IMAGE_ICON, 0, 0,
                                               LR_DEFAULTSIZE | LR_SHARED);
            if (fallback) hIcon = CopyIcon(fallback);
        }
        if (!hIcon) return false;
        bool ok = BlitIconToBGRA(hIcon, out);
        DestroyIcon(hIcon);
        return ok;
    }

    static ID3D11ShaderResourceView* CreateSRVFromBGRA(ID3D11Device* device, const std::vector<BYTE>& bgra) {
        if (!device || bgra.size() < (size_t)kIconSize * kIconSize * 4) return nullptr;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = kIconSize; desc.Height = kIconSize;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = bgra.data();
        initData.SysMemPitch = kIconSize * 4;

        ID3D11Texture2D* pTexture = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, &initData, &pTexture)) || !pTexture) return nullptr;

        ID3D11ShaderResourceView* pSRV = nullptr;
        device->CreateShaderResourceView(pTexture, nullptr, &pSRV);
        pTexture->Release();
        return pSRV;
    }

    static void EnsureWorker() {
        auto& st = State();
        if (st.started) return;
        std::lock_guard<std::mutex> lock(st.mu);
        if (st.started) return;
        st.stop.store(false);
        st.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        st.worker = std::thread(Worker);
        st.started = true;
    }

    static void Worker() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto& st = State();
        while (!st.stop.load()) {
            std::wstring path;
            {
                std::lock_guard<std::mutex> lock(st.mu);
                if (!st.queue.empty()) {
                    path = std::move(st.queue.front());
                    st.queue.pop_front();
                }
            }
            if (path.empty()) {
                HANDLE wake = st.wake;
                if (!wake) break;
                WaitForSingleObject(wake, 250);
                continue;
            }
            IconPixels item;
            item.path = path;
            ExtractIconBGRA(path.c_str(), item.bgra);
            {
                std::lock_guard<std::mutex> lock(st.mu);
                st.ready.push_back(std::move(item));
            }
        }
        CoUninitialize();
    }
};
