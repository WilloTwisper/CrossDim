#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <ole2.h>
#include <oleidl.h>
#include <string>
#include <vector>
#include "Engine/Logger.h"

#ifndef DROPEFFECT_COPY
#define DROPEFFECT_COPY 1
#define DROPEFFECT_MOVE 2
#endif

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

extern HWND g_mainHwnd;

// Win32 shell file operations + real IContextMenu. Cubes are filesystem
// items; these helpers talk to Explorer's documented COM/file APIs so the
// 3D desktop behaves like the classic one (recycle, move, copy, paste, New).

namespace FileOps {

inline IContextMenu2* g_ctxMenu2 = nullptr;
inline IContextMenu3* g_ctxMenu3 = nullptr;

static std::wstring PackMultiSz(const std::vector<std::wstring>& paths) {
    std::wstring s;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        s.append(p);
        s.push_back(L'\0');
    }
    s.push_back(L'\0');
    return s;
}

static bool ShellFileOp(UINT func, const std::vector<std::wstring>& from, const wchar_t* toDir, FILEOP_FLAGS extra = 0) {
    if (from.empty()) return false;
    std::wstring src = PackMultiSz(from);
    std::wstring dst;
    SHFILEOPSTRUCTW op = {};
    op.hwnd = g_mainHwnd;
    op.wFunc = func;
    op.pFrom = src.c_str();
    if (toDir && toDir[0]) {
        dst.assign(toDir);
        dst.push_back(L'\0');
        dst.push_back(L'\0');
        op.pTo = dst.c_str();
    }
    op.fFlags = (FILEOP_FLAGS)(FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR | extra);
    int r = SHFileOperationW(&op);
    if (r != 0) LOG("[fileop] SHFileOperation func=%u err=%d aborted=%d", func, r, (int)op.fAnyOperationsAborted);
    return r == 0 && !op.fAnyOperationsAborted;
}

static bool RecyclePaths(const std::vector<std::wstring>& paths) {
    return ShellFileOp(FO_DELETE, paths, nullptr, FOF_NOCONFIRMATION);
}

static bool MovePaths(const std::vector<std::wstring>& from, const std::wstring& destDir) {
    if (destDir.empty()) return false;
    return ShellFileOp(FO_MOVE, from, destDir.c_str());
}

static bool CopyPaths(const std::vector<std::wstring>& from, const std::wstring& destDir) {
    if (destDir.empty()) return false;
    return ShellFileOp(FO_COPY, from, destDir.c_str());
}

static bool RenamePath(const std::wstring& from, const std::wstring& to) {
    if (from.empty() || to.empty()) return false;
    return ShellFileOp(FO_RENAME, { from }, to.c_str(), FOF_NOCONFIRMATION);
}

static bool CreateNewFolder(const std::wstring& parent, std::wstring* outPath = nullptr) {
    if (parent.empty()) return false;
    for (int n = 0; n < 64; ++n) {
        std::wstring name = (n == 0) ? L"新建文件夹" : (L"新建文件夹 (" + std::to_wstring(n + 1) + L")");
        std::wstring path = parent + L"\\" + name;
        if (CreateDirectoryW(path.c_str(), nullptr)) {
            if (outPath) *outPath = path;
            return true;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}

static UINT DropEffectFormat() {
    static UINT s = 0;
    if (!s) s = RegisterClipboardFormatW(L"Preferred DropEffect");
    return s;
}

static bool CopyToClipboard(const std::vector<std::wstring>& paths, bool cut) {
    if (paths.empty() || !g_mainHwnd) return false;
    size_t chars = 1;
    for (const auto& p : paths) chars += p.size() + 1;
    SIZE_T bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
    HGLOBAL hDrop = GlobalAlloc(GHND, bytes);
    if (!hDrop) return false;
    auto* df = (DROPFILES*)GlobalLock(hDrop);
    if (!df) { GlobalFree(hDrop); return false; }
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    wchar_t* dest = (wchar_t*)(df + 1);
    for (const auto& p : paths) {
        memcpy(dest, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
        dest += p.size() + 1;
    }
    *dest = L'\0';
    GlobalUnlock(hDrop);

    HGLOBAL hEff = GlobalAlloc(GHND, sizeof(DWORD));
    if (hEff) {
        auto* e = (DWORD*)GlobalLock(hEff);
        if (e) { *e = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY; GlobalUnlock(hEff); }
    }

    if (!OpenClipboard(g_mainHwnd)) {
        GlobalFree(hDrop);
        if (hEff) GlobalFree(hEff);
        return false;
    }
    EmptyClipboard();
    SetClipboardData(CF_HDROP, hDrop);
    if (hEff) SetClipboardData(DropEffectFormat(), hEff);
    CloseClipboard();
    return true;
}

static bool PasteFromClipboard(const std::wstring& destDir) {
    if (destDir.empty() || !IsClipboardFormatAvailable(CF_HDROP)) return false;
    if (!OpenClipboard(g_mainHwnd)) return false;
    HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
    if (!drop) { CloseClipboard(); return false; }
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    paths.reserve(n);
    WCHAR buf[MAX_PATH];
    for (UINT i = 0; i < n; ++i) {
        if (DragQueryFileW(drop, i, buf, MAX_PATH)) paths.emplace_back(buf);
    }
    DWORD effect = DROPEFFECT_COPY;
    HANDLE hEff = GetClipboardData(DropEffectFormat());
    if (hEff) {
        auto* e = (DWORD*)GlobalLock(hEff);
        if (e) { effect = *e; GlobalUnlock(hEff); }
    }
    CloseClipboard();
    if (paths.empty()) return false;
    bool ok = (effect & DROPEFFECT_MOVE) ? MovePaths(paths, destDir) : CopyPaths(paths, destDir);
    if (ok && (effect & DROPEFFECT_MOVE)) {
        if (OpenClipboard(g_mainHwnd)) { EmptyClipboard(); CloseClipboard(); }
    }
    return ok;
}

static bool HandleMenuMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* out) {
    (void)hwnd;
    if (g_ctxMenu3) {
        LRESULT lr = 0;
        if (g_ctxMenu3->HandleMenuMsg2(msg, wParam, lParam, &lr) == S_OK) {
            if (out) *out = lr;
            return true;
        }
    }
    if (g_ctxMenu2) {
        if (g_ctxMenu2->HandleMenuMsg(msg, wParam, lParam) == S_OK) {
            if (out) *out = 0;
            return true;
        }
    }
    return false;
}

static void ClearCtxMenu() {
    if (g_ctxMenu3) { g_ctxMenu3->Release(); g_ctxMenu3 = nullptr; }
    if (g_ctxMenu2) { g_ctxMenu2->Release(); g_ctxMenu2 = nullptr; }
}

static bool InvokePopup(IContextMenu* cm, HWND owner, POINT pt) {
    if (!cm) return false;
    cm->QueryInterface(IID_PPV_ARGS(&g_ctxMenu2));
    cm->QueryInterface(IID_PPV_ARGS(&g_ctxMenu3));

    HMENU menu = CreatePopupMenu();
    if (!menu) { ClearCtxMenu(); return false; }
    HRESULT hr = cm->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXTENDEDVERBS);
    if (FAILED(hr)) {
        DestroyMenu(menu);
        ClearCtxMenu();
        return false;
    }

    UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                pt.x, pt.y, owner, nullptr);
    bool invoked = false;
    if (cmd) {
        CMINVOKECOMMANDINFOEX info = {};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        info.hwnd = owner;
        info.lpVerb = MAKEINTRESOURCEA(cmd - 1);
        info.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = pt;
        hr = cm->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        invoked = SUCCEEDED(hr);
        if (FAILED(hr)) LOG("[fileop] InvokeCommand hr=0x%08lx cmd=%u", hr, cmd);
    }
    DestroyMenu(menu);
    ClearCtxMenu();
    return invoked;
}

static std::wstring ParentOf(const std::wstring& path) {
    size_t sl = path.find_last_of(L"\\/");
    if (sl == std::wstring::npos) return {};
    return path.substr(0, sl);
}

static bool ShowItemMenu(HWND owner, const std::vector<std::wstring>& paths, POINT pt) {
    if (paths.empty()) return false;
    std::wstring parentPath = ParentOf(paths[0]);
    std::vector<PIDLIST_ABSOLUTE> keep;
    std::vector<LPCITEMIDLIST> rels;
    IShellFolder* parent = nullptr;

    for (const auto& p : paths) {
        if (ParentOf(p) != parentPath) continue;
        PIDLIST_ABSOLUTE absPidl = ILCreateFromPathW(p.c_str());
        if (!absPidl) continue;
        IShellFolder* folder = nullptr;
        LPCITEMIDLIST rel = nullptr;
        if (FAILED(SHBindToParent(absPidl, IID_PPV_ARGS(&folder), &rel)) || !rel) {
            ILFree(absPidl);
            continue;
        }
        if (!parent) parent = folder;
        else folder->Release();
        keep.push_back(absPidl);
        rels.push_back(rel);
    }
    if (!parent || rels.empty()) {
        for (auto* id : keep) ILFree(id);
        if (parent) parent->Release();
        return false;
    }

    IContextMenu* cm = nullptr;
    HRESULT hr = parent->GetUIObjectOf(owner, (UINT)rels.size(), rels.data(),
                                       IID_IContextMenu, nullptr, (void**)&cm);
    parent->Release();
    bool invoked = false;
    if (SUCCEEDED(hr) && cm) {
        invoked = InvokePopup(cm, owner, pt);
        cm->Release();
    }
    for (auto* id : keep) ILFree(id);
    return invoked;
}

static bool ShowBackgroundMenu(HWND owner, const std::wstring& folder, POINT pt) {
    if (folder.empty()) return false;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(folder.c_str());
    if (!pidl) return false;
    IShellFolder* sf = nullptr;
    HRESULT hr = SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&sf));
    ILFree(pidl);
    if (FAILED(hr) || !sf) return false;
    IContextMenu* cm = nullptr;
    hr = sf->CreateViewObject(owner, IID_IContextMenu, (void**)&cm);
    sf->Release();
    if (FAILED(hr) || !cm) return false;
    bool invoked = InvokePopup(cm, owner, pt);
    cm->Release();
    return invoked;
}

} // namespace FileOps
