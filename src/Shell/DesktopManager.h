#pragma once
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <shlobj.h>
#include "Engine/TextureLoader.h"
#include "Engine/Logger.h"

struct AppCube {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT4 BaseColor;
    std::wstring AppPath;
    std::string AppName;
    bool IsHovered;
    bool IsSelected;
    bool WasSelected;
    ID3D11ShaderResourceView* IconTexture;
};

inline std::vector<AppCube> g_myApps;

extern ID3D11Device* g_pd3dDevice;

static std::wstring ResolveShortcutTarget(LPCWSTR lnkPath) {
    std::wstring target;
    IShellLinkW* shellLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink)))) return target;
    IPersistFile* persistFile = nullptr;
    if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile)))) {
        if (SUCCEEDED(persistFile->Load(lnkPath, STGM_READ))) {
            shellLink->Resolve(nullptr, SLR_NO_UI | SLR_UPDATE);
            WCHAR buf[MAX_PATH];
            if (SUCCEEDED(shellLink->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH))) target = buf;
        }
        persistFile->Release();
    }
    shellLink->Release();
    return target;
}

static std::string WcsToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, nullptr, nullptr);
    return result;
}

static bool SearchMatch(const char* searchFilter, const std::string& label, const std::wstring& path) {
    if (!searchFilter || searchFilter[0] == '\0') return true;
    std::string filter(searchFilter);
    for (auto& c : filter) c = (char)tolower((unsigned char)c);
    std::string lcLabel = label;
    for (auto& c : lcLabel) c = (char)tolower((unsigned char)c);
    if (lcLabel.find(filter) != std::string::npos) return true;
    std::string lcPath;
    for (wchar_t ch : path) lcPath += (char)tolower((unsigned char)ch);
    if (lcPath.find(filter) != std::string::npos) return true;
    return false;
}

static void ScanDesktopForApps(std::vector<AppCube>& outApps) {
    WCHAR desktopPaths[2][MAX_PATH];
    int pathCount = 0;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktopPaths[pathCount]))) pathCount++;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, desktopPaths[pathCount]))) pathCount++;
    std::vector<std::wstring> allNames;
    std::vector<std::pair<std::wstring, std::string>> entries;
    for (int pi = 0; pi < pathCount; pi++) {
        std::wstring searchPath = std::wstring(desktopPaths[pi]) + L"\\*.*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) { LOGW(L"[desk] FF failed: %s", desktopPaths[pi]); continue; }
        int found = 0;
        do {
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            std::wstring fname(fd.cFileName);
            std::wstring fullPath = std::wstring(desktopPaths[pi]) + L"\\" + fname;
            size_t dot = fname.rfind(L'.');
            std::wstring ext = (dot != std::wstring::npos) ? fname.substr(dot) : L"";
            std::wstring nameOnly = (dot != std::wstring::npos) ? fname.substr(0, dot) : fname;
            std::wstring targetPath;
            std::string displayName = WcsToUtf8(nameOnly);
            bool include = false;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fname == L"." || fname == L"..") continue;
                targetPath = fullPath; include = true;
            } else if (_wcsicmp(ext.c_str(), L".lnk") == 0) {
                targetPath = ResolveShortcutTarget(fullPath.c_str());
                if (targetPath.empty()) continue;
                IShellLinkW* sl = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) {
                    IPersistFile* pf = nullptr;
                    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
                        if (SUCCEEDED(pf->Load(fullPath.c_str(), STGM_READ))) {
                            WCHAR desc[256];
                            if (SUCCEEDED(sl->GetDescription(desc, 256)) && desc[0]) displayName = WcsToUtf8(desc);
                        }
                        pf->Release();
                    }
                    sl->Release();
                }
                include = true;
            } else if (_wcsicmp(ext.c_str(), L".exe") == 0) {
                targetPath = fullPath; include = true;
            } else if (!ext.empty() && _wcsicmp(ext.c_str(), L".ini") != 0 && _wcsicmp(ext.c_str(), L".log") != 0 && _wcsicmp(ext.c_str(), L".tmp") != 0) {
                targetPath = fullPath; include = true;
            }
            if (include) { entries.push_back({ targetPath, displayName }); found++; }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        LOG("[desk] %ls: %d entries", desktopPaths[pi], found);
    }
    const int count = (int)entries.size();
    if (count == 0) return;
    const float radius = 8.0f;
    const DirectX::XMFLOAT3 sphereCenter = { 0.0f, 1.5f, 0.0f };
    int cols = (int)ceilf(sqrtf((float)count * 1.6f));
    if (cols < 1) cols = 1;
    int rows = (count + cols - 1) / cols;
    const float yawMin = DirectX::XMConvertToRadians(-50.0f);
    const float yawMax = DirectX::XMConvertToRadians(15.0f);
    const float pitchMax = DirectX::XMConvertToRadians(30.0f);
    const float pitchMin = DirectX::XMConvertToRadians(-28.0f);
    for (int idx = 0; idx < count; ++idx) {
        int col = idx % cols;
        int row = idx / cols;
        float yaw = (cols > 1) ? yawMin + (yawMax - yawMin) * col / (float)(cols - 1) : (yawMin + yawMax) * 0.5f;
        float pitch = (rows > 1) ? pitchMax + (pitchMin - pitchMax) * row / (float)(rows - 1) : (pitchMax + pitchMin) * 0.5f;
        float dx = sinf(yaw) * cosf(pitch);
        float dy = sinf(pitch);
        float dz = cosf(yaw) * cosf(pitch);
        AppCube cube = {};
        cube.Position = DirectX::XMFLOAT3(sphereCenter.x + dx * radius, sphereCenter.y + dy * radius, sphereCenter.z + dz * radius);
        cube.BaseColor = DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
        cube.AppPath = entries[idx].first;
        cube.AppName = entries[idx].second;
        outApps.push_back(cube);
    }
}

static void SaveDesktopState(const std::vector<AppCube>& apps) {
    const wchar_t* fname = L"desktop.cddesk";
    HANDLE hFile = CreateFileW(fname, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written;
    DWORD magic = 0x4B534544;
    DWORD version = 2;
    DWORD count = (DWORD)apps.size();
    WriteFile(hFile, &magic, sizeof(magic), &written, nullptr);
    WriteFile(hFile, &version, sizeof(version), &written, nullptr);
    WriteFile(hFile, &count, sizeof(count), &written, nullptr);
    for (const auto& app : apps) {
        WORD pathLen = (WORD)app.AppPath.size();
        WriteFile(hFile, &pathLen, sizeof(pathLen), &written, nullptr);
        WriteFile(hFile, app.AppPath.data(), (DWORD)(pathLen * sizeof(wchar_t)), &written, nullptr);
        WriteFile(hFile, &app.Position, sizeof(app.Position), &written, nullptr);
        WORD nameLen = (WORD)app.AppName.size();
        WriteFile(hFile, &nameLen, sizeof(nameLen), &written, nullptr);
        WriteFile(hFile, app.AppName.data(), (DWORD)nameLen, &written, nullptr);
    }
    CloseHandle(hFile);
}

static void LoadDesktopState(std::vector<AppCube>& apps) {
    const wchar_t* fname = L"desktop.cddesk";
    HANDLE hFile = CreateFileW(fname, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD bytesRead;
    DWORD magic, version, count;
    if (!ReadFile(hFile, &magic, sizeof(magic), &bytesRead, nullptr) || magic != 0x4B534544) { CloseHandle(hFile); return; }
    if (!ReadFile(hFile, &version, sizeof(version), &bytesRead, nullptr) || version < 1 || version > 2) { CloseHandle(hFile); return; }
    if (!ReadFile(hFile, &count, sizeof(count), &bytesRead, nullptr)) { CloseHandle(hFile); return; }
    for (DWORD i = 0; i < count; ++i) {
        WORD pathLen = 0;
        if (!ReadFile(hFile, &pathLen, sizeof(pathLen), &bytesRead, nullptr)) break;
        std::wstring path(pathLen, L'\0');
        if (!ReadFile(hFile, &path[0], (DWORD)(pathLen * sizeof(wchar_t)), &bytesRead, nullptr)) break;
        DirectX::XMFLOAT3 pos;
        if (!ReadFile(hFile, &pos, sizeof(pos), &bytesRead, nullptr)) break;
        WORD nameLen = 0;
        if (!ReadFile(hFile, &nameLen, sizeof(nameLen), &bytesRead, nullptr)) break;
        std::string name(nameLen, '\0');
        if (!ReadFile(hFile, &name[0], (DWORD)nameLen, &bytesRead, nullptr)) break;
        bool matched = false;
        for (auto& app : apps) {
            if (app.AppPath == path) {
                app.Position = pos;
                matched = true;
                break;
            }
        }
        if (!matched && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            AppCube cube = {};
            cube.Position = pos;
            cube.BaseColor = DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
            cube.AppPath = path;
            cube.AppName = name;
            apps.push_back(cube);
        }
    }
    CloseHandle(hFile);
}

static void LoadIconsForApps(std::vector<AppCube>& apps) {
    for (auto& app : apps) {
        if (app.IconTexture) continue;
        app.IconTexture = TextureLoader::LoadIconFromExe(g_pd3dDevice, app.AppPath.c_str());
        if (!app.IconTexture) {
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(app.AppPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
                if (sfi.hIcon) { app.IconTexture = TextureLoader::LoadIconFromHandle(g_pd3dDevice, sfi.hIcon); DestroyIcon(sfi.hIcon); }
            }
        }
        if (!app.IconTexture) LOGW(L"[desk] No icon: %s", app.AppPath.c_str());
    }
}
