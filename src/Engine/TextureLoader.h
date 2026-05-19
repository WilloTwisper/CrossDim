#pragma once
#include <d3d11.h>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <wincodec.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")


class TextureLoader {
public:
    static ID3D11ShaderResourceView* LoadIconFromHandle(ID3D11Device* device, HICON iconHandle) {
        if (!device || !iconHandle) return nullptr;
        HICON hIcon = CopyIcon(iconHandle);
        if (!hIcon) return nullptr;

        int texSize = 256;
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texSize;
        bi.bmiHeader.biHeight = -texSize;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
        HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

        memset(pBits, 0, texSize * texSize * 4);
        DrawIconEx(hdcMem, 0, 0, hIcon, texSize, texSize, 0, NULL, DI_NORMAL);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = texSize; desc.Height = texSize;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pBits;
        initData.SysMemPitch = texSize * 4;

        ID3D11Texture2D* pTexture = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &initData, &pTexture);

        SelectObject(hdcMem, hOld);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        DestroyIcon(hIcon);

        if (FAILED(hr)) return nullptr;

        ID3D11ShaderResourceView* pSRV = nullptr;
        device->CreateShaderResourceView(pTexture, nullptr, &pSRV);
        pTexture->Release();

        return pSRV;
    }

    static ID3D11ShaderResourceView* LoadIconFromExe(ID3D11Device* device, LPCWSTR exePath) {
        HICON hIcon = NULL;
        bool destroyIcon = true;
        PrivateExtractIconsW(exePath, 0, 256, 256, &hIcon, NULL, 1, LR_LOADFROMFILE);
        if (!hIcon) {
            ExtractIconExW(exePath, 0, &hIcon, NULL, 1);
            if (!hIcon) {
                SHFILEINFOW sfi = {};
                if (SHGetFileInfoW(exePath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
                    hIcon = sfi.hIcon;
                    destroyIcon = true;
                } else {
                    HICON fallback = (HICON)LoadImageW(NULL, reinterpret_cast<LPCWSTR>(ULONG_PTR(32512)), IMAGE_ICON, 0, 0,
                                                       LR_DEFAULTSIZE | LR_SHARED);
                    if (fallback) {
                        hIcon = CopyIcon(fallback);
                        destroyIcon = true;
                    }
                }
            }
            if (!hIcon) return nullptr;
        }

        int texSize = 256;
        HDC hdcScreen = GetDC(NULL);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        // 🚨 核心修复：使用 DIB Section 拿到干净的带有透明通道的内存！
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texSize;
        bi.bmiHeader.biHeight = -texSize; // 负数表示自上而下
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
        HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

        // 先把内存全部清零 (纯透明)
        memset(pBits, 0, texSize * texSize * 4);

        // 画上图标，DI_NORMAL 会把图标自带的 Alpha 写进去
        DrawIconEx(hdcMem, 0, 0, hIcon, texSize, texSize, 0, NULL, DI_NORMAL);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = texSize; desc.Height = texSize;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; 
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pBits;
        initData.SysMemPitch = texSize * 4;

        ID3D11Texture2D* pTexture = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &initData, &pTexture);

        SelectObject(hdcMem, hOld);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        if (destroyIcon && hIcon) {
            DestroyIcon(hIcon);
        }

        if (FAILED(hr)) return nullptr;

        ID3D11ShaderResourceView* pSRV = nullptr;
        device->CreateShaderResourceView(pTexture, nullptr, &pSRV);
        pTexture->Release();

        return pSRV;
    }

    // Load texture from image file (PNG, JPG, BMP, etc.) using WIC
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
        desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init = {}; init.pSysMem = pixels.data(); init.SysMemPitch = width * 4;
        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &init, &tex);

        converter->Release(); frame->Release(); decoder->Release(); factory->Release();

        if (FAILED(hr) || !tex) return nullptr;

        ID3D11ShaderResourceView* srv = nullptr;
        device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        return srv;
    }
};