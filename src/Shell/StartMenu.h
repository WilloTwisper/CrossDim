#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <thread>
#include <atomic>
#include <mutex>
#include "Engine/TextureLoader.h"
#include "Engine/Logger.h"
#include "Shell/DesktopManager.h"

struct StartProgram {
    std::wstring lnkPath;
    std::wstring targetPath;
    std::string name;
    std::string category;
};

inline std::vector<StartProgram> g_startPrograms;
inline DWORD g_startProgramsTick = 0;
inline std::atomic<bool> g_startMenuScanning{false};
inline std::mutex g_startMenuMutex;

static bool StartNameIsNoise(const std::wstring& nameOnly) {
    static const wchar_t* kSkip[] = {
        L"uninstall", L"卸载", L"help", L"readme", L"release notes",
        L"eula", L"license", L"documentation"
    };
    std::wstring lc = nameOnly;
    for (auto& c : lc) c = (wchar_t)towlower(c);
    for (auto* s : kSkip) {
        if (lc.find(s) != std::wstring::npos) return true;
    }
    return false;
}

static void ScanStartMenuDir(const std::wstring& dir, const std::string& category, std::vector<StartProgram>& out) {
    std::wstring search = dir + L"\\*.*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == L'\0' || (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        std::wstring fname(fd.cFileName);
        std::wstring full = dir + L"\\" + fname;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::string sub = category.empty() ? WcsToUtf8(fname) : (category + " / " + WcsToUtf8(fname));
            ScanStartMenuDir(full, sub, out);
            continue;
        }
        size_t dot = fname.rfind(L'.');
        std::wstring ext = (dot != std::wstring::npos) ? fname.substr(dot) : L"";
        if (_wcsicmp(ext.c_str(), L".lnk") != 0) continue;
        std::wstring nameOnly = (dot != std::wstring::npos) ? fname.substr(0, dot) : fname;
        if (StartNameIsNoise(nameOnly)) continue;
        StartProgram sp;
        sp.lnkPath = full;
        sp.targetPath = ResolveShortcutTarget(full.c_str());
        sp.name = WcsToUtf8(nameOnly);
        sp.category = category;
        out.push_back(std::move(sp));
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static void ScanStartMenu(std::vector<StartProgram>& out) {
    out.clear();
    WCHAR buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, buf)))
        ScanStartMenuDir(buf, "", out);
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, buf)))
        ScanStartMenuDir(buf, "", out);
    std::sort(out.begin(), out.end(), [](const StartProgram& a, const StartProgram& b) {
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    // Dedup by name+target
    std::vector<StartProgram> uniq;
    uniq.reserve(out.size());
    for (auto& p : out) {
        bool dup = false;
        for (const auto& u : uniq) {
            if (_stricmp(u.name.c_str(), p.name.c_str()) == 0 &&
                _wcsicmp(u.targetPath.c_str(), p.targetPath.c_str()) == 0) {
                dup = true; break;
            }
        }
        if (!dup) uniq.push_back(std::move(p));
    }
    out.swap(uniq);
    LOG("[start] scanned %d programs", (int)out.size());
}

static void EnsureStartMenuScanned(bool force = false) {
    DWORD now = GetTickCount();
    {
        std::lock_guard<std::mutex> lock(g_startMenuMutex);
        if (!force && !g_startPrograms.empty() && (now - g_startProgramsTick) < 60000) return;
    }
    if (g_startMenuScanning.exchange(true)) return; // already in progress

    std::thread([force]() {
        HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::vector<StartProgram> scanned;
        ScanStartMenu(scanned);
        for (const auto& p : scanned) {
            if (!p.lnkPath.empty()) TextureLoader::EnqueueIconLoad(p.lnkPath);
        }
        {
            std::lock_guard<std::mutex> lock(g_startMenuMutex);
            g_startPrograms.swap(scanned);
            g_startProgramsTick = GetTickCount();
        }
        g_startMenuScanning.store(false);
        if (comInit == S_OK || comInit == S_FALSE) CoUninitialize();
    }).detach();
}

static const std::wstring& StartLaunchPath(const StartProgram& p) {
    return p.targetPath.empty() ? p.lnkPath : p.targetPath;
}
