#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <imm.h>
#include <iphlpapi.h>

inline IAudioEndpointVolume* g_audioEndpoint = nullptr;

static bool InitAudioEndpointVolume() {
    if (g_audioEndpoint) return true;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return false;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (SUCCEEDED(hr)) {
        hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
                              (void**)&g_audioEndpoint);
    }
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    return SUCCEEDED(hr) && g_audioEndpoint != nullptr;
}

static void ShutdownAudioEndpointVolume() {
    if (g_audioEndpoint) {
        g_audioEndpoint->Release();
        g_audioEndpoint = nullptr;
    }
}

static float GetMasterVolumeLevelScalar() {
    float level = 0.7f;
    if (g_audioEndpoint) {
        float v = 0.0f;
        if (SUCCEEDED(g_audioEndpoint->GetMasterVolumeLevelScalar(&v))) {
            level = v;
        }
    }
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    return level;
}

static void GetImeLabel(HWND hwnd, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return;
    strcpy_s(buffer, bufferSize, "英 ENG");
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;
    if (ImmGetOpenStatus(imc)) {
        strcpy_s(buffer, bufferSize, "中 拼");
    }
    ImmReleaseContext(hwnd, imc);
}

static bool GetImeOpenStatus(HWND hwnd) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return false;
    BOOL open = ImmGetOpenStatus(imc);
    ImmReleaseContext(hwnd, imc);
    return open != FALSE;
}

static void SetImeOpenStatus(HWND hwnd, bool open) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;
    ImmSetOpenStatus(imc, open ? TRUE : FALSE);
    ImmReleaseContext(hwnd, imc);
}

static bool IsChineseImeLayout() {
    HKL hkl = GetKeyboardLayout(0);
    LANGID lang = LOWORD((UINT_PTR)hkl);
    return PRIMARYLANGID(lang) == LANG_CHINESE;
}

static void ActivateImeLayout(HWND hwnd, LPCWSTR klid, bool open) {
    HKL hkl = LoadKeyboardLayoutW(klid, KLF_ACTIVATE);
    if (hkl) ActivateKeyboardLayout(hkl, 0);
    SetImeOpenStatus(hwnd, open);
}

struct NetworkStatus {
    bool connected;
    bool wifi;
    bool ethernet;
};

static NetworkStatus GetNetworkStatus() {
    NetworkStatus status = { false, false, false };
    ULONG size = 0;
    DWORD flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size);
    if (size == 0) return status;

    std::vector<unsigned char> buffer(size);
    IP_ADAPTER_ADDRESSES* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size) != NO_ERROR) {
        return status;
    }

    for (IP_ADAPTER_ADDRESSES* it = addrs; it != nullptr; it = it->Next) {
        if (it->OperStatus != IfOperStatusUp) continue;
        if (it->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        status.connected = true;
        if (it->IfType == IF_TYPE_IEEE80211) {
            status.wifi = true;
        } else if (it->IfType == IF_TYPE_ETHERNET_CSMACD) {
            status.ethernet = true;
        } else {
            status.ethernet = true;
        }
    }
    return status;
}
