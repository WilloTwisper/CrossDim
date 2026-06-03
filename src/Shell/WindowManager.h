#pragma once
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <objbase.h>
#include "Engine/TextureLoader.h"

struct PendingHijack {
    HWND originalFocus;
    int frameWait;
    DWORD processId;
};
inline std::vector<PendingHijack> g_pendingHijacks;

struct HijackedWindow {
    HWND hwnd;
};
inline std::vector<HijackedWindow> g_hijackedWindows;

inline std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_taskbarIconCache;
inline std::unordered_map<HWND, ID3D11ShaderResourceView*> g_taskbarWindowIconCache;
inline std::unordered_map<HWND, int> g_taskbarDynamicOrder;
inline int g_taskbarDynamicOrderCounter = 1;

extern bool g_systemTaskbarHidden;

struct RunningWindow {
    HWND hwnd;
    DWORD pid;
    std::wstring path;
    std::wstring title;
};

struct WindowEnumContext {
    HWND exclude;
    std::vector<RunningWindow>* out;
};

struct EnumData {
    DWORD processId;
    HWND hwnd;
};

static DirectX::XMFLOAT2 GetYawPitch(DirectX::XMVECTOR dirVec) {
    DirectX::XMFLOAT3 dir;
    DirectX::XMStoreFloat3(&dir, dirVec);
    float yaw = atan2(dir.x, dir.z);
    float pitch = asin(dir.y / sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z));
    return {yaw, pitch};
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    EnumData* data = (EnumData*)lParam;
    
    if (pid == data->processId && GetWindow(hwnd, GW_OWNER) == NULL && IsWindowVisible(hwnd)) {
        WCHAR title[256];
        GetWindowTextW(hwnd, title, 256);
        if (wcslen(title) > 0) {
            data->hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static HWND FindWindowFromProcessId(DWORD pid) {
    EnumData data = { pid, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    return data.hwnd;
}

static std::wstring GetProcessPath(DWORD pid) {
    std::wstring path;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return path;
    WCHAR buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, buffer, &size)) {
        path.assign(buffer, size);
    }
    CloseHandle(hProc);
    return path;
}

static bool IsTaskbarWindowCandidate(HWND hwnd, HWND exclude) {
    if (!IsWindowVisible(hwnd)) return false;
    if (hwnd == exclude) return false;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return false;
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    if (style & WS_CHILD) return false;
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return false;
    if (exStyle & WS_EX_NOACTIVATE) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    char className[256];
    GetClassNameA(hwnd, className, 256);
    if (strcmp(className, "Shell_TrayWnd") == 0 || strcmp(className, "Progman") == 0) return false;
    WCHAR title[256];
    if (GetWindowTextW(hwnd, title, 256) == 0) return false;
    return true;
}

static BOOL CALLBACK EnumRunningWindowsProc(HWND hwnd, LPARAM lParam) {
    auto ctx = reinterpret_cast<WindowEnumContext*>(lParam);
    if (!IsTaskbarWindowCandidate(hwnd, ctx->exclude)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    RunningWindow win = {};
    win.hwnd = hwnd;
    win.pid = pid;
    win.path = GetProcessPath(pid);
    WCHAR title[256];
    GetWindowTextW(hwnd, title, 256);
    win.title = title;
    ctx->out->push_back(win);
    return TRUE;
}

static std::vector<RunningWindow> EnumerateRunningWindows(HWND exclude) {
    std::vector<RunningWindow> out;
    WindowEnumContext ctx = { exclude, &out };
    EnumWindows(EnumRunningWindowsProc, (LPARAM)&ctx);
    return out;
}

static bool IsSamePath(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return false;
    if (_wcsicmp(a.c_str(), b.c_str()) == 0) return true;
    size_t posA = a.rfind(L'\\');
    size_t posB = b.rfind(L'\\');
    if (posA != std::wstring::npos && posB != std::wstring::npos) {
        return _wcsicmp(a.c_str() + posA, b.c_str() + posB) == 0;
    }
    return false;
}

struct FindHwndCtx { std::wstring exe; HWND result; };
static BOOL CALLBACK FindHwndByExeProc(HWND hwnd, LPARAM lParam) {
    FindHwndCtx* ctx = reinterpret_cast<FindHwndCtx*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return TRUE;
    WCHAR buf[MAX_PATH];
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, buf, &sz);
    CloseHandle(hProc);
    if (!ok) return TRUE;
    std::wstring p(buf, sz);
    size_t pos = p.rfind(L'\\');
    if (pos != std::wstring::npos && _wcsicmp(p.c_str() + pos, ctx->exe.c_str()) == 0) {
        ctx->result = hwnd;
        return FALSE;
    }
    return TRUE;
}
static HWND FindRunningHwnd(const std::wstring& appPath) {
    size_t pos = appPath.rfind(L'\\');
    std::wstring exe = (pos != std::wstring::npos) ? appPath.substr(pos) : appPath;
    FindHwndCtx ctx = { exe, nullptr };
    EnumWindows(FindHwndByExeProc, (LPARAM)&ctx);
    return ctx.result;
}

static ID3D11ShaderResourceView* GetTaskbarIcon(ID3D11Device* device, const std::wstring& path) {
    if (path.empty() || !device) return nullptr;
    auto it = g_taskbarIconCache.find(path);
    if (it != g_taskbarIconCache.end()) return it->second;
    ID3D11ShaderResourceView* icon = TextureLoader::LoadIconFromExe(device, path.c_str());
    g_taskbarIconCache[path] = icon;
    return icon;
}

static void SetSystemTaskbarVisible(bool visible) {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tray) ShowWindow(tray, visible ? SW_SHOW : SW_HIDE);
    HWND secondary = nullptr;
    while (true) {
        secondary = FindWindowExW(nullptr, secondary, L"Shell_SecondaryTrayWnd", nullptr);
        if (!secondary) break;
        ShowWindow(secondary, visible ? SW_SHOW : SW_HIDE);
    }
    g_systemTaskbarHidden = !visible;
}

static HICON GetWindowBestIcon(HWND hwnd) {
    if (!hwnd) return nullptr;
    HICON hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (!hIcon) hIcon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    return hIcon;
}

static ID3D11ShaderResourceView* GetWindowIconTexture(ID3D11Device* device, HWND hwnd, const std::wstring& path) {
    if (!device || !hwnd) return GetTaskbarIcon(device, path);
    auto it = g_taskbarWindowIconCache.find(hwnd);
    if (it != g_taskbarWindowIconCache.end() && it->second) return it->second;
    ID3D11ShaderResourceView* icon = nullptr;
    HICON hIcon = GetWindowBestIcon(hwnd);
    if (hIcon) {
        icon = TextureLoader::LoadIconFromHandle(device, hIcon);
    }
    if (!icon && !path.empty()) {
        icon = GetTaskbarIcon(device, path);
    }
    if (!icon) {
        HICON fallback = (HICON)LoadImageW(NULL, reinterpret_cast<LPCWSTR>(ULONG_PTR(32512)), IMAGE_ICON, 0, 0,
                                           LR_DEFAULTSIZE | LR_SHARED);
        if (fallback) {
            icon = TextureLoader::LoadIconFromHandle(device, fallback);
        }
    }
    if (icon) {
        g_taskbarWindowIconCache[hwnd] = icon;
    } else if (it != g_taskbarWindowIconCache.end()) {
        g_taskbarWindowIconCache.erase(it);
    }
    return icon;
}
