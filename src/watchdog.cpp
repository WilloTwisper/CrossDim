#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "advapi32.lib")

static constexpr DWORD kHeartbeatTimeoutMs = 5000;
static constexpr WCHAR kEventName[] = L"Global\\CrossDim_Heartbeat";
static constexpr WCHAR kShutdownEventName[] = L"Global\\CrossDim_Shutdown";
static constexpr WCHAR kRegPath[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
static constexpr WCHAR kShellValue[] = L"Shell";
static constexpr WCHAR kFallbackShell[] = L"explorer.exe";

static void KillAllCrossDim() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        DWORD selfPid = GetCurrentProcessId();
        do {
            if (pe.th32ProcessID == selfPid) continue;
            if (_wcsicmp(pe.szExeFile, L"CrossDim.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

static void RestoreShellKey(HKEY root) {
    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(root, kRegPath, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    if (status != ERROR_SUCCESS) return;

    WCHAR current[512] = {};
    DWORD size = sizeof(current);
    DWORD type = 0;
    status = RegQueryValueExW(hKey, kShellValue, nullptr, &type, (BYTE*)current, &size);
    if (status == ERROR_SUCCESS && type == REG_SZ) {
        if (_wcsicmp(current, kFallbackShell) == 0) {
            RegCloseKey(hKey);
            return;
        }
    }

    RegSetValueExW(hKey, kShellValue, 0, REG_SZ,
                   (const BYTE*)kFallbackShell, (DWORD)(sizeof(kFallbackShell)));
    RegCloseKey(hKey);
}

static void RestoreDefaultShell() {
    RestoreShellKey(HKEY_CURRENT_USER);
    RestoreShellKey(HKEY_LOCAL_MACHINE);
}

static void LaunchExplorer() {
    STARTUPINFOW si = { sizeof(si) };
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};
    WCHAR cmd[] = L"explorer.exe";
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    HANDLE hSingleton = CreateMutexW(nullptr, TRUE, L"Local\\CrossDim_Watchdog");
    if (hSingleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hSingleton);
        return 0;
    }

    auto openEvt = [](LPCWSTR globalName, LPCWSTR localName) -> HANDLE {
        HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, globalName);
        if (h) return h;
        h = OpenEventW(SYNCHRONIZE, FALSE, localName);
        if (h) return h;
        for (int retry = 0; retry < 10; ++retry) {
            Sleep(500);
            h = OpenEventW(SYNCHRONIZE, FALSE, globalName);
            if (h) return h;
            h = OpenEventW(SYNCHRONIZE, FALSE, localName);
            if (h) return h;
        }
        return nullptr;
    };
    HANDLE hHeartbeat = openEvt(kEventName, L"Local\\CrossDim_Heartbeat");
    if (!hHeartbeat) return 1;

    HANDLE hShutdown = openEvt(kShutdownEventName, L"Local\\CrossDim_Shutdown");

    HANDLE hEvents[2] = { hHeartbeat, hShutdown };
    DWORD eventCount = hShutdown ? 2 : 1;

    for (;;) {
        DWORD result = WaitForMultipleObjects(eventCount, hEvents, FALSE, kHeartbeatTimeoutMs);
        if (result == WAIT_OBJECT_0) {
            continue;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            break;
        }

        KillAllCrossDim();
        RestoreDefaultShell();
        LaunchExplorer();
        break;
    }

    CloseHandle(hHeartbeat);
    if (hShutdown) CloseHandle(hShutdown);
    return 0;
}
