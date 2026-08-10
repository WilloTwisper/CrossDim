#pragma once
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <d3d11.h>

struct TrayIconEntry {
    std::wstring tooltip;
    int commandId;
    std::wstring exePath;
};

static HWND FindTrayToolbar() {
    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTray) return nullptr;
    HWND hNotify = FindWindowExW(hTray, nullptr, L"TrayNotifyWnd", nullptr);
    if (!hNotify) return nullptr;
    HWND hPager = FindWindowExW(hNotify, nullptr, L"SysPager", nullptr);
    if (!hPager) return nullptr;
    return FindWindowExW(hPager, nullptr, L"ToolbarWindow32", nullptr);
}

// Explorer's toolbar lives in another process: if Explorer hangs, a bare
// SendMessage would freeze the whole shell. All cross-process toolbar
// messages go through this timeout wrapper.
static LRESULT TraySendMessage(HWND hToolbar, UINT msg, WPARAM w, LPARAM l, DWORD timeoutMs = 150) {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(hToolbar, msg, w, l, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &result);
    return (LRESULT)result;
}

static std::vector<TrayIconEntry> QueryTrayIcons() {
    std::vector<TrayIconEntry> result;
    HWND hToolbar = FindTrayToolbar();
    if (!hToolbar) return result;

    DWORD pid = 0;
    GetWindowThreadProcessId(hToolbar, &pid);
    if (pid == 0) return result;

    HANDLE hProc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!hProc) return result;

    int btnCount = (int)TraySendMessage(hToolbar, TB_BUTTONCOUNT, 0, 0);
    if (btnCount <= 0 || btnCount > 64) { CloseHandle(hProc); return result; }

    for (int i = 0; i < btnCount; i++) {
        void* remoteTb = VirtualAllocEx(hProc, nullptr, sizeof(TBBUTTON), MEM_COMMIT, PAGE_READWRITE);
        if (!remoteTb) continue;

        TBBUTTON tb = {};
        WriteProcessMemory(hProc, remoteTb, &tb, sizeof(tb), nullptr);
        LRESULT btnOk = TraySendMessage(hToolbar, TB_GETBUTTON, (WPARAM)i, (LPARAM)remoteTb);

        TBBUTTON btn;
        BOOL readOk = ReadProcessMemory(hProc, remoteTb, &btn, sizeof(btn), nullptr);
        VirtualFreeEx(hProc, remoteTb, 0, MEM_RELEASE);

        if (!btnOk || !readOk) continue;
        if (btn.fsState & TBSTATE_HIDDEN) continue;

        TrayIconEntry entry;
        entry.commandId = btn.idCommand;

        void* remoteText = VirtualAllocEx(hProc, nullptr, 256 * sizeof(WCHAR), MEM_COMMIT, PAGE_READWRITE);
        if (remoteText) {
            TraySendMessage(hToolbar, TB_GETBUTTONTEXTW, (WPARAM)btn.idCommand, (LPARAM)remoteText);
            WCHAR localText[256] = {};
            size_t readBytes = 0;
            ReadProcessMemory(hProc, remoteText, localText, sizeof(localText) - sizeof(WCHAR), &readBytes);
            VirtualFreeEx(hProc, remoteText, 0, MEM_RELEASE);
            entry.tooltip = localText;
        }

        if (!entry.tooltip.empty()) {
            result.push_back(entry);
        }
    }

    CloseHandle(hProc);
    return result;
}
