#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <DirectXCollision.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <cwchar>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <imm.h>
#include <iphlpapi.h>
#include <shlobj.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "Engine/Camera.h"
#include "Engine/SkyboxRenderer.h"
#include "Engine/CubeRenderer.h"
#include "Engine/TextureLoader.h"
#include "Engine/ModelRenderer.h"
#include "Engine/Logger.h"
#include "Engine/TrayProxy.h"
#include "Shell/DesktopManager.h"
#include "Shell/WindowManager.h"
#include "Shell/SystemInfo.h"
#include "Shell/FileOps.h"
#include "Shell/StartMenu.h"
#include "Shell/AIServer.h"
#include "Shell/VirtualDesktop.h"
#include "Shell/FolderPortal.h"
#include "Shell/WindowChrome.h"
#include "Shell/TaskbarUI.h"
#include "Engine/JsonUtils.h"
enum CrossDimState {
    STATE_3D_EXPLORE,
    STATE_2D_WORKBENCH
};
CrossDimState g_currentState = STATE_3D_EXPLORE;
bool g_uiUnlocked = false;
bool g_previousUiUnlocked = false;

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
ID3D11DepthStencilView* g_mainDepthStencilView = nullptr;
HWND g_mainHwnd = nullptr;
RECT g_taskbarRect = { 0, 0, 0, 0 };
bool g_taskbarRectValid = false;
bool g_systemTaskbarHidden = false;
bool g_tabHotkeyRegistered = false;
HANDLE g_hHeartbeatEvent = nullptr;
HANDLE g_hShutdownEvent = nullptr;

float g_mouseDeltaX = 0.0f;
float g_mouseDeltaY = 0.0f;
float g_folderCamDX = 0.0f;
float g_folderCamDY = 0.0f;

bool g_leftClicked = false;
bool g_ctrlHeld = false;
bool g_lButtonHeld = false;

DWORD g_lastClickTime = 0;
int   g_lastClickedApp = -1;

int   g_grabbedAppIndex = -1;
bool  g_isDragging = false;
DWORD g_mouseDownTime = 0;
int   g_dropTargetIndex = -1;
int   g_rightClickedCubeIndex = -1;
bool  g_rightClicked = false;
bool  g_deleteRequested = false;
bool  g_copyRequested = false;
bool  g_cutRequested = false;
bool  g_pasteRequested = false;
bool  g_renameRequested = false;
bool  g_selectAllRequested = false;
bool  g_newFolderRequested = false;
bool  g_needRescan = false;
bool  g_showModelDebug = false;
bool  g_shellMenuPending = false;
bool  g_shellMenuBackground = false;
std::vector<std::wstring> g_shellMenuPaths;
std::wstring g_shellMenuFolder;
POINT g_shellMenuPt = { 0, 0 };
int   g_renameIndex = -1;
char  g_renameBuf[260] = "";
bool  g_exitFolderRequested = false;
bool  g_showWindowSwitcher = false;
int   g_switcherSelected = 0;
static std::vector<std::pair<HWND, std::wstring>> g_switcherWindows;
static int g_aiFrameW = 1;
static int g_aiFrameH = 1;

bool  g_isBlankDragging = false;
float g_dragStartYaw = 0.0f;
float g_dragStartPitch = 0.0f;
float g_dragCurrYaw = 0.0f;
float g_dragCurrPitch = 0.0f;
float g_dragPrevRawYaw = 0.0f;

float g_fpsCurrent = 0.0f;
char g_searchFilter[64] = "";
float g_dragDistance = 8.0f;

// Resolve a runtime asset path: try CWD first, then exe dir, then exe dir's
// parent (covers running from build\ with assets in the project root).
static std::wstring ResolveRuntimePathW(const wchar_t* rel) {
    if (GetFileAttributesW(rel) != INVALID_FILE_ATTRIBUTES) return rel;
    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    WCHAR* s = wcsrchr(exeDir, L'\\');
    if (!s) return rel;
    *s = L'\0';
    std::wstring p = std::wstring(exeDir) + L"\\" + rel;
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    p = std::wstring(exeDir) + L"\\..\\" + rel;
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    return rel;
}



// ==================== AI Control Interface ====================
// These functions run on the main thread (called from the render loop).
// The HTTP server (AIServer.h) exchanges state through atomics/mutexes.
void LaunchAppByPath(LPCWSTR appPath);
extern DirectX::XMFLOAT3 g_modelPosition;
extern DirectX::XMFLOAT3 g_modelRotation;
extern float g_modelScale;
extern float g_modelFov;
extern DirectX::XMFLOAT4 g_modelMatParams;
extern DirectX::XMFLOAT4 g_modelColorTint;

// Heap-backed snprintf: sprintf_s into fixed buffers invokes the invalid
// parameter handler (process termination) on overflow — long CJK window
// titles/paths in UTF-8 easily exceed 512 bytes. This never terminates.
static std::string Fmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list args2; va_copy(args2, args);
    int n = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    if (n <= 0) { va_end(args2); return {}; }
    std::string out(n, '\0');
    vsnprintf(out.data(), n + 1, fmt, args2);
    va_end(args2);
    return out;
}

static void BuildStateSnapshot(Camera& camera) {
    std::string js = "{";
    // Core state
    char coreBuf[256];
    sprintf_s(coreBuf, "\"mode\":\"%s\",\"uiUnlocked\":%s,\"fps\":%.1f,\"tmpFps\":%.1f",
              (g_currentState == STATE_2D_WORKBENCH) ? "2D" : "3D",
              g_uiUnlocked ? "true" : "false", g_fpsCurrent, g_fpsCurrent);
    js += coreBuf;
    // Camera
    char camBuf[128];
    sprintf_s(camBuf, ",\"camera\":{\"pos\":[%.2f,%.2f,%.2f],\"rot\":[%.2f,%.2f,%.2f]}",
              camera.Position.x, camera.Position.y, camera.Position.z,
              camera.Rotation.x, camera.Rotation.y, camera.Rotation.z);
    js += camBuf;
    // Model debug
    char modelBuf[256];
    sprintf_s(modelBuf,
              ",\"model\":{\"pos\":[%.2f,%.2f,%.2f],\"rot\":[%.2f,%.2f,%.2f],\"scale\":%.2f,\"fov\":%.1f}",
              g_modelPosition.x, g_modelPosition.y, g_modelPosition.z,
              g_modelRotation.x, g_modelRotation.y, g_modelRotation.z,
              g_modelScale, g_modelFov);
    js += modelBuf;
    // Cubes
    js += ",\"cubes\":[";
    for (size_t i = 0; i < g_myApps.size(); ++i) {
        if (i) js += ",";
        const auto& c = g_myApps[i];
        js += Fmt("{\"index\":%d,\"name\":\"%s\",\"path\":\"%s\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"selected\":%s}",
                  (int)i, Json::Escape(c.AppName).c_str(), Json::Escape(WcsToUtf8(c.AppPath)).c_str(),
                  c.Position.x, c.Position.y, c.Position.z,
                  c.IsSelected ? "true" : "false");
    }
    js += "]";
    // Virtual desktop
    char dbuf[64];
    sprintf_s(dbuf, ",\"desktop\":%d,\"desktopCount\":%zu", g_activeDesktopIndex, g_desktopSlots.size());
    js += dbuf;
    // Folder context
    js += ",\"folder\":";
    if (g_isInFolder) {
        std::string folderName = g_folderStack.empty() ? "" : (g_folderStack.back().folderPath.empty()
            ? "" : WcsToUtf8(g_folderStack.back().folderPath));
        js += Fmt("{\"active\":true,\"depth\":%zu,\"path\":\"%s\",\"maximized\":%s}",
                  g_folderStack.size(), Json::Escape(folderName).c_str(),
                  g_isFolderMaximized ? "true" : "false");
    } else {
        js += "{\"active\":false}";
    }
    // Windows (hijacked)
    js += ",\"windows\":[";
    bool firstW = true;
    for (const auto& w : g_hijackedWindows) {
        if (!IsWindow(w.hwnd)) continue;
        DWORD pid = 0; GetWindowThreadProcessId(w.hwnd, &pid);
        std::wstring path = GetProcessPath(pid);
        WCHAR title[256]; GetWindowTextW(w.hwnd, title, 256);
        js += Fmt("%s{\"hwnd\":%p,\"pid\":%lu,\"path\":\"%s\",\"title\":\"%s\"}",
                  firstW ? "" : ",", w.hwnd, pid, Json::Escape(WcsToUtf8(path)).c_str(),
                  Json::Escape(WcsToUtf8(title)).c_str());
        firstW = false;
    }
    js += "]";
    // Running (non-hijacked) windows: system-wide window snapshot.
    // Full enumeration is expensive — cache at ~2 Hz (this fn runs at 10 Hz).
    js += ",\"running\":[";
    static std::vector<RunningWindow> s_rwCache;
    static DWORD s_rwTick = 0;
    if (GetTickCount() - s_rwTick > 500) {
        s_rwTick = GetTickCount();
        s_rwCache = EnumerateRunningWindows(g_mainHwnd);
    }
    const std::vector<RunningWindow>& rw = s_rwCache;
    bool firstR = true;
    for (const auto& win : rw) {
        if (firstR) firstR = false; else js += ",";
        js += Fmt("{\"hwnd\":%p,\"pid\":%lu,\"path\":\"%s\",\"title\":\"%s\"}",
                  win.hwnd, win.pid, Json::Escape(WcsToUtf8(win.path)).c_str(),
                  Json::Escape(WcsToUtf8(win.title)).c_str());
    }
    js += "]}";

    std::lock_guard<std::mutex> lock(AIServer::g_reqMutex);
    AIServer::g_sharedStateJson = js;
}

static void ExecuteAIAction(const std::string& actionJson, Camera& camera) {
    std::string action = Json::GetStringField(actionJson, "action");
    AIServer::g_actionResult = "{\"ok\":false,\"error\":\"unknown action\"}";

    if (action == "launch") {
        std::string path = Json::GetStringField(actionJson, "path");
        if (!path.empty()) {
            std::wstring wpath = Utf8ToWcs(path); // JSON is UTF-8; byte-widening breaks CJK
            LaunchAppByPath(wpath.c_str());
            AIServer::g_actionResult = "{\"ok\":true,\"action\":\"launch\",\"path\":\"" + Json::Escape(path) + "\"}";
        } else {
            // Launch by index
            size_t idx = (size_t)atoi(Json::GetStringField(actionJson, "index").c_str());
            if (idx < g_myApps.size()) {
                if (CubeIsDirectory(g_myApps[idx]))
                    EnterFolder(CubeFolderPath(g_myApps[idx]), camera);
                else
                    LaunchCube(g_myApps[idx]);
                AIServer::g_actionResult = "{\"ok\":true,\"action\":\"launch\",\"index\":" + std::to_string(idx) + "}";
            }
        }
    }
    else if (action == "switch_desktop") {
        int idx = atoi(Json::GetStringField(actionJson, "index").c_str());
        if (idx >= 0 && idx < (int)g_desktopSlots.size()) {
            SwitchToDesktop(idx);
            AIServer::g_actionResult = "{\"ok\":true,\"action\":\"switch_desktop\",\"index\":" + std::to_string(idx) + "}";
        }
    }
    else if (action == "create_desktop") {
        CreateNewDesktop();
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"create_desktop\"}";
    }
    else if (action == "close_desktop") {
        int idx = atoi(Json::GetStringField(actionJson, "index").c_str());
        if (idx >= 0 && idx < (int)g_desktopSlots.size()) {
            CloseDesktop(idx);
            AIServer::g_actionResult = "{\"ok\":true,\"action\":\"close_desktop\",\"index\":" + std::to_string(idx) + "}";
        }
    }
    else if (action == "open_folder") {
        std::string path = Json::GetStringField(actionJson, "path");
        if (!path.empty()) {
            std::wstring wpath = Utf8ToWcs(path); // JSON is UTF-8; byte-widening breaks CJK
            if (IsPathDirectory(wpath)) {
                EnterFolder(wpath, camera);
                AIServer::g_actionResult = "{\"ok\":true,\"action\":\"open_folder\",\"path\":\"" + Json::Escape(path) + "\"}";
            }
        } else {
            size_t idx = (size_t)atoi(Json::GetStringField(actionJson, "index").c_str());
            if (idx < g_myApps.size() && CubeIsDirectory(g_myApps[idx])) {
                EnterFolder(CubeFolderPath(g_myApps[idx]), camera);
                AIServer::g_actionResult = "{\"ok\":true,\"action\":\"open_folder\",\"index\":" + std::to_string(idx) + "}";
            }
        }
    }
    else if (action == "exit_folder") {
        ExitFolderToRoot(camera);
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"exit_folder\"}";
    }
    else if (action == "focus_window") {
        std::string hwndStr = Json::GetStringField(actionJson, "hwnd");
        if (!hwndStr.empty()) {
            HWND h = (HWND)(intptr_t)_strtoui64(hwndStr.c_str(), nullptr, 16);
            if (IsWindow(h)) { SetForegroundWindow(h); AIServer::g_actionResult = "{\"ok\":true}"; }
        }
    }
    else if (action == "deselect_all") {
        for (auto& a : g_myApps) a.IsSelected = false;
        AIServer::g_actionResult = "{\"ok\":true}";
    }
    else if (action == "delete_selected") {
        auto paths = SelectedItemPaths();
        int removed = (int)paths.size();
        if (!paths.empty()) {
            FileOps::RecyclePathsAsync(paths);
            auto& tombs = g_desktopRemovedPaths[ActiveDesktopId()];
            for (const auto& p : paths) {
                if (std::find(tombs.begin(), tombs.end(), p) == tombs.end()) tombs.push_back(p);
            }
            g_myApps.erase(std::remove_if(g_myApps.begin(), g_myApps.end(), [](const AppCube& c) {
                return c.IsSelected;
            }), g_myApps.end());
            if (g_isInFolder && !g_isFolderMaximized) {
                g_folderCubes.erase(std::remove_if(g_folderCubes.begin(), g_folderCubes.end(), [](const AppCube& c) {
                    return c.IsSelected;
                }), g_folderCubes.end());
            }
            SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
        }
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"delete_selected\",\"removed\":" + std::to_string(removed) + "}";
    }
    else if (action == "select") {
        size_t idx = (size_t)atoi(Json::GetStringField(actionJson, "index").c_str());
        if (idx < g_myApps.size()) {
            for (auto& a : g_myApps) a.IsSelected = false;
            g_myApps[idx].IsSelected = true;
            AIServer::g_actionResult = "{\"ok\":true,\"index\":" + std::to_string(idx) + "}";
        }
    }
    else if (action == "ping") {
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"ping\",\"reply\":\"pong\"}";
    }
    else if (action == "quit") {
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"quit\"}";
        PostMessageW(g_mainHwnd, WM_CLOSE, 0, 0);
    }
    else if (action == "reload_apps") {
        g_myApps.clear();
        ScanDesktopForApps(g_myApps);
        LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
        SeparateOverlappingCubes(g_myApps);
        LoadIconsForApps(g_myApps);
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"reload_apps\",\"count\":" + std::to_string(g_myApps.size()) + "}";
    }
    else if (action == "toggle_mode") {
        // Flip 3D/2D UI unlock state (mirrors toggleUiUnlock)
        g_uiUnlocked = !g_uiUnlocked;
        if (g_uiUnlocked) {
            while (ShowCursor(TRUE) < 0);
            ClipCursor(NULL);
        } else {
            while (ShowCursor(FALSE) >= 0);
            RECT rect; GetClientRect(g_mainHwnd, &rect);
            MapWindowPoints(g_mainHwnd, nullptr, (POINT*)&rect, 2);
            ClipCursor(&rect);
            SetCursorPos(rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2);
        }
        AIServer::g_actionResult = std::string("{\"ok\":true,\"action\":\"toggle_mode\",\"uiUnlocked\":") +
            (g_uiUnlocked ? "true" : "false") + "}";
    }
    else if (action == "set_camera") {
        // Rotation is (pitch, yaw, roll) in radians; accept both rx/ry/rz and
        // the more intuitive pitch/yaw/roll field names.
        std::string px = Json::GetStringField(actionJson, "px");
        std::string py = Json::GetStringField(actionJson, "py");
        std::string pz = Json::GetStringField(actionJson, "pz");
        std::string rx = Json::GetStringField(actionJson, "rx");
        std::string ry = Json::GetStringField(actionJson, "ry");
        std::string rz = Json::GetStringField(actionJson, "rz");
        if (rx.empty()) rx = Json::GetStringField(actionJson, "pitch");
        if (ry.empty()) ry = Json::GetStringField(actionJson, "yaw");
        if (rz.empty()) rz = Json::GetStringField(actionJson, "roll");
        if (!px.empty()) camera.Position.x = (float)atof(px.c_str());
        if (!py.empty()) camera.Position.y = (float)atof(py.c_str());
        if (!pz.empty()) camera.Position.z = (float)atof(pz.c_str());
        if (!rx.empty()) camera.Rotation.x = (float)atof(rx.c_str());
        if (!ry.empty()) camera.Rotation.y = (float)atof(ry.c_str());
        if (!rz.empty()) camera.Rotation.z = (float)atof(rz.c_str());
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"set_camera\"}";
    }
    else if (action == "set_model") {
        std::string sx = Json::GetStringField(actionJson, "px");
        std::string sy = Json::GetStringField(actionJson, "py");
        std::string sz = Json::GetStringField(actionJson, "pz");
        std::string sc = Json::GetStringField(actionJson, "scale");
        std::string fov = Json::GetStringField(actionJson, "fov");
        if (!sx.empty()) g_modelPosition.x = (float)atof(sx.c_str());
        if (!sy.empty()) g_modelPosition.y = (float)atof(sy.c_str());
        if (!sz.empty()) g_modelPosition.z = (float)atof(sz.c_str());
        if (!sc.empty()) g_modelScale = (float)atof(sc.c_str());
        if (!fov.empty()) g_modelFov = (float)atof(fov.c_str());
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"set_model\"}";
    }
    else if (action == "reset_model") {
        g_modelPosition = DirectX::XMFLOAT3(-2.45f, -2.15f, 9.3f);
        g_modelRotation = DirectX::XMFLOAT3(-0.1f, 1.0f, -0.11f);
        g_modelScale = 10.0f;
        g_modelMatParams = DirectX::XMFLOAT4(0.5f, 1.0f, 0.0f, 0.1f);
        g_modelColorTint = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        AIServer::g_actionResult = "{\"ok\":true,\"action\":\"reset_model\"}";
    }
    else if (action == "set_volume") {
        std::string v = Json::GetStringField(actionJson, "level");
        if (!v.empty()) {
            float lvl = (float)atof(v.c_str());
            bool ok = SetMasterVolumeLevelScalar(lvl);
            AIServer::g_actionResult = "{\"ok\":" + std::string(ok ? "true" : "false") + ",\"action\":\"set_volume\",\"level\":" + std::to_string(lvl) + "}";
        }
    }
    else if (action == "get_log") {
        const std::string& logPath = Logger::Instance().GetCurrentLogPath();
        std::string grepFilter = Json::GetField(actionJson, "grep");
        std::string countStr = Json::GetField(actionJson, "count");
        int maxLines = countStr.empty() ? 200 : atoi(countStr.c_str());
        if (maxLines < 1) maxLines = 200;
        if (maxLines > 2000) maxLines = 2000;
        std::string result = "{\"ok\":true,\"logs\":[";
        // NOTE: use plain fopen here — fopen_s opens the handle non-shareable,
        // which fails (EACCES) because the Logger already holds this file open.
        FILE* f = logPath.empty() ? nullptr : fopen(logPath.c_str(), "r");
        errno_t openErr = f ? 0 : (logPath.empty() ? ENOENT : errno);
        size_t lineCount = 0;
        if (openErr == 0 && f) {
            std::vector<std::string> lines;
            char line[2048];
            while (fgets(line, sizeof(line), f)) {
                std::string s(line);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                if (!grepFilter.empty() && s.find(grepFilter) == std::string::npos) continue;
                lines.push_back(s);
            }
            fclose(f);
            lineCount = lines.size();
            size_t start = (lines.size() > (size_t)maxLines) ? lines.size() - maxLines : 0;
            bool first = true;
            for (size_t i = start; i < lines.size(); ++i) {
                if (!first) result += ",";
                result += "\"" + Json::Escape(lines[i]) + "\"";
                first = false;
            }
        }
        result += "],\"path\":\"" + Json::Escape(logPath) +
            "\",\"openErr\":" + std::to_string((int)openErr) +
            ",\"lineCount\":" + std::to_string(lineCount) + "}";
        AIServer::g_actionResult = result;
    }
    else if (action == "describe_scene") {
        // Build a natural-language description of the current desktop state
        std::string d = "CrossDim 当前状态：";
        d += (g_currentState == STATE_2D_WORKBENCH) ? "处于二维工作台模式" : "处于三维探索模式";
        d += "，uiUnlocked=" + std::string(g_uiUnlocked ? "是" : "否");
        d += "，当前虚拟桌面 " + std::to_string(g_activeDesktopIndex) + "（共 " + std::to_string(g_desktopSlots.size()) + " 个）";
        d += "，FPS ≈ " + std::to_string((int)g_fpsCurrent) + "。";
        if (g_isInFolder) {
            d += "正在文件夹传送门中：";
            std::string fpath = g_folderStack.empty() ? "" : WcsToUtf8(g_folderStack.back().folderPath);
            d += "路径[" + fpath + "]，深度 " + std::to_string(g_folderStack.size());
            d += g_isFolderMaximized ? "，窗口最大化模式" : "，窗口化模式（可拖动）";
            d += "。文件夹内含 " + std::to_string(g_folderCubes.size()) + " 个方块。";
        } else {
            d += "当前在桌面层，无文件夹传送门开启。";
        }
        d += "桌面上有 " + std::to_string(g_myApps.size()) + " 个 3D 方块；";
        int selCount = 0;
        for (const auto& a : g_myApps) if (a.IsSelected) selCount++;
        if (selCount > 0) d += "其中 " + std::to_string(selCount) + " 个被选中。";
        else d += "当前无选中方块。";
        d += "劫持窗口 " + std::to_string(g_hijackedWindows.size()) + " 个：";
        if (!g_hijackedWindows.empty()) {
            int wIdx = 0;
            for (const auto& w : g_hijackedWindows) {
                if (!IsWindow(w.hwnd)) continue;
                WCHAR title[256]; GetWindowTextW(w.hwnd, title, 256);
                std::string t = WcsToUtf8(title);
                if (t.size() > 30) t = t.substr(0, 30) + "...";
                d += " [" + std::string(t.empty() ? "未命名" : t) + "]";
                if (++wIdx >= 5) { d += " 等"; break; }
            }
        }
        std::string result = "{\"ok\":true,\"scene\":\"" + Json::Escape(d) + "\"}";
        AIServer::g_actionResult = result;
    }
}

// Capture current backbuffer as a BMP, base64-encode into JSON.
static void CaptureScreenshot() {
    // The HTTP thread reads/clears the result strings under g_reqMutex —
    // write them under the same lock to rule out races on the timeout path.
    std::lock_guard<std::mutex> reqLock(AIServer::g_reqMutex);
    if (!g_pSwapChain || !g_pd3dDeviceContext || g_aiFrameW < 1 || g_aiFrameH < 1) {
        LOG("[screenshot] FAIL no device swap=%d ctx=%d fw=%d", g_pSwapChain!=nullptr, g_pd3dDeviceContext!=nullptr, g_aiFrameW);
        AIServer::g_screenshotResult = "{\"error\":\"no device\"}";
        AIServer::g_screenshotReady.store(true);
        return;
    }
    ID3D11Texture2D* pBack = nullptr;
    HRESULT hrBuf = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
    if (FAILED(hrBuf) || !pBack) {
        LOG("[screenshot] FAIL get buffer hr=0x%08x", hrBuf);
        AIServer::g_screenshotResult = "{\"error\":\"get buffer failed\"}";
        AIServer::g_screenshotReady.store(true);
        return;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    pBack->GetDesc(&desc);
    // If the backbuffer is multisampled, resolve to a non-MSAA target first
    // (must check the ORIGINAL desc before zeroing SampleDesc).
    ID3D11Texture2D* pResolve = nullptr;
    if (desc.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC rd = desc;
        rd.SampleDesc.Count = 1; rd.SampleDesc.Quality = 0;
        rd.BindFlags = D3D11_BIND_RENDER_TARGET;
        rd.CPUAccessFlags = 0;
        rd.Usage = D3D11_USAGE_DEFAULT;
        rd.MiscFlags = 0;
        if (FAILED(g_pd3dDevice->CreateTexture2D(&rd, nullptr, &pResolve)) || !pResolve) {
            pBack->Release();
            LOG("[screenshot] FAIL resolve target create");
            AIServer::g_screenshotResult = "{\"error\":\"resolve target create failed\"}";
            AIServer::g_screenshotReady.store(true);
            return;
        }
        g_pd3dDeviceContext->ResolveSubresource(pResolve, 0, pBack, 0, desc.Format);
    }
    // Staging texture (never multisampled)
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    ID3D11Texture2D* pStaging = nullptr;
    HRESULT hrStg = g_pd3dDevice->CreateTexture2D(&desc, nullptr, &pStaging);
    if (FAILED(hrStg) || !pStaging) {
        if (pResolve) pResolve->Release();
        pBack->Release();
        LOG("[screenshot] FAIL staging hr=0x%08x w=%u h=%u", hrStg, desc.Width, desc.Height);
        AIServer::g_screenshotResult = "{\"error\":\"staging create failed\"}";
        AIServer::g_screenshotReady.store(true);
        return;
    }
    g_pd3dDeviceContext->CopyResource(pStaging, pResolve ? pResolve : pBack);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hrMap = g_pd3dDeviceContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hrMap)) {
        int srcW = desc.Width, srcH = desc.Height;
        int maxDim = AIServer::g_screenshotMaxDim.load();
        // Compute downscale factor
        int w = srcW, h = srcH;
        if (maxDim > 0) {
            int longEdge = (srcW > srcH) ? srcW : srcH;
            if (longEdge > maxDim) {
                w = srcW * maxDim / longEdge;
                h = srcH * maxDim / longEdge;
                if (w < 1) w = 1; if (h < 1) h = 1;
            }
        }
        int rowBytes = w * 3;  // 24-bit BGR, no padding for BMP row (usually multiple of 4; add padding if needed)
        // BMP rows must be multiple of 4 bytes
        int rowPadded = ((rowBytes + 3) / 4) * 4;
        int fileSize = 54 + rowPadded * h;
        std::vector<unsigned char> bmp(fileSize, 0);
        // File header
        bmp[0] = 'B'; bmp[1] = 'M';
        bmp[2] = (unsigned char)(fileSize); bmp[3] = (unsigned char)(fileSize >> 8);
        bmp[4] = (unsigned char)(fileSize >> 16); bmp[5] = (unsigned char)(fileSize >> 24);
        bmp[10] = 54;
        // DIB header
        bmp[14] = 40;
        *(int*)&bmp[18] = w;            // width
        *(int*)&bmp[22] = h;            // height
        *(short*)&bmp[26] = 1;          // planes
        *(short*)&bmp[28] = 24;         // bpp
        *(int*)&bmp[34] = rowPadded * h; // raw size
        // Pixel data (bottom-up, optional downscale). BMP stores BGR triplets;
        // source channel order depends on the backbuffer format.
        const bool srcBGRA = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                              desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                              desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
                              desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
                              desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
                              desc.Format == DXGI_FORMAT_B8G8R8X8_TYPELESS);
        const int iB = srcBGRA ? 0 : 2; // index of Blue within a 4-byte pixel
        const int iR = srcBGRA ? 2 : 0; // index of Red
        const unsigned char* src = (const unsigned char*)mapped.pData;
        for (int y = 0; y < h; ++y) {
            unsigned char* dst = &bmp[54 + y * rowPadded];
            for (int x = 0; x < w; ++x) {
                int sx, sy;
                if (w == srcW && h == srcH) {
                    sx = x; sy = y;
                } else {
                    // nearest-neighbor sampling
                    sx = x * srcW / w;
                    sy = y * srcH / h;
                }
                int srcRow = (srcH - 1 - sy) * mapped.RowPitch;
                const unsigned char* px = &src[srcRow + sx * 4];
                dst[x*3+0] = px[iB]; // B
                dst[x*3+1] = px[1];  // G
                dst[x*3+2] = px[iR]; // R
            }
        }
        g_pd3dDeviceContext->Unmap(pStaging, 0);

        bool rawMode = AIServer::g_screenshotRawMode.load();
        if (rawMode) {
            // Raw binary BMP: give uint8 bytes to HTTP thread
            AIServer::g_screenshotRaw.assign((const char*)bmp.data(), bmp.size());
            AIServer::g_screenshotRawMime = "image/bmp";
        } else {
            // Base64 encode
            static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string enc;
            enc.reserve((fileSize / 3 + 1) * 4);
            for (int i = 0; i < fileSize; i += 3) {
                unsigned char b0 = bmp[i];
                unsigned char b1 = (i+1 < fileSize) ? bmp[i+1] : 0;
                unsigned char b2 = (i+2 < fileSize) ? bmp[i+2] : 0;
                enc += b64[b0 >> 2];
                enc += b64[((b0 & 3) << 4) | (b1 >> 4)];
                enc += (i+1 < fileSize) ? b64[((b1 & 15) << 2) | (b2 >> 6)] : '=';
                enc += (i+2 < fileSize) ? b64[b2 & 63] : '=';
            }
            AIServer::g_screenshotResult = "{\"width\":" + std::to_string(w) +
                ",\"height\":" + std::to_string(h) +
                ",\"format\":\"bmp\",\"size\":" + std::to_string(fileSize) +
                ",\"data\":\"" + enc + "\"}";
        }
    } else {
        LOG("[screenshot] FAIL map hr=0x%08x", hrMap);
        AIServer::g_screenshotResult = "{\"error\":\"map failed\"}";
    }
    pStaging->Release();
    if (pResolve) pResolve->Release();
    pBack->Release();
    LOG("[screenshot] ok %dx%d", g_aiFrameW, g_aiFrameH);
    AIServer::g_screenshotReady.store(true);
}

// Model debug transform (for imported OBJ)
DirectX::XMFLOAT3 g_modelPosition = { -2.45f, -2.15f, 9.3f }; // position in world space
// Rotation order used by UI and XMMatrixRotationRollPitchYaw: (pitch, yaw, roll)
DirectX::XMFLOAT3 g_modelRotation = { -0.1f, 1.0f, -0.11f }; 
float g_modelScale = 10.0f;
DirectX::XMFLOAT3 g_modelPivot = { 0.0f, 0.0f, 0.0f }; // local-space pivot for rotation/scale
DirectX::XMFLOAT4 g_modelMatParams = { 0.5f, 1.0f, 0.0f, 0.1f }; // Ambient, Diffuse, RimIntensity, RimSharpness
DirectX::XMFLOAT4 g_modelColorTint = { 1.0f, 1.0f, 1.0f, 1.0f }; // 默认白色(不偏色)
bool g_modelUseIndependentProj = true;
float g_modelFov = 55.0f;
bool g_modelFovSnapWhenRotate = true;
float g_modelFovSnapSpeed = 12.0f;
float g_modelFovReturnSpeed = 4.0f;
float g_modelFovCurrent = 55.0f;
float g_modelFovRotateHold = 0.0f;
float g_modelFovRotateHoldTime = 0.12f;
float g_modelFovRotateDeadzone = 0.2f;
float g_modelRotateAccum = 0.0f;
float g_modelRotateAccumThreshold = 6.0f;
float g_modelRotateAccumDecay = 10.0f;
bool g_modelLockScreenPos = true;
bool g_pivotAutoSet = false;

void LaunchAppByPath(LPCWSTR appPath) {
    if (!appPath || appPath[0] == L'\0') return;
    size_t len = wcslen(appPath);
    bool isExe = (len > 4 && _wcsicmp(appPath + len - 4, L".exe") == 0);
    if (isExe) {
        WCHAR cmdBuffer[MAX_PATH]; wcscpy_s(cmdBuffer, appPath);
        STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            HWND currentFocus = GetForegroundWindow();
            g_pendingHijacks.push_back({ currentFocus, 0, pi.dwProcessId });
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            g_previousUiUnlocked = g_uiUnlocked;
            g_currentState = STATE_2D_WORKBENCH;
            while (ShowCursor(TRUE) < 0); ClipCursor(NULL);
            SetSystemTaskbarVisible(false);
            AIServer::EmitEvent("launch", "{\"path\":\"" + Json::Escape(WcsToUtf8(std::wstring(appPath))) + "\"}");
        }
    } else {
        HINSTANCE result = ShellExecuteW(NULL, L"open", appPath, NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)result <= 32) {
            LOGW(L"[desk] ShellExecute failed (%d) for: %s", (int)(INT_PTR)result, appPath);
        }
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void CleanupDevice();

bool InitDevice(HWND hWnd) {
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevelArray, 2, D3D11_SDK_VERSION, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hr)) {
        LOG("[d3d] D3D11CreateDevice hr=0x%08lx", hr);
        return false;
    }

    UINT msaaCount = 4, msaaQuality = 0;
    g_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &msaaQuality);
    if (msaaQuality == 0) { msaaCount = 1; msaaQuality = 1; }

    auto tryCreateSwap = [&](UINT count, UINT quality) -> HRESULT {
        DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = (count > 1) ? 1 : 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = count;
        sd.SampleDesc.Quality = quality;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        HRESULT r = g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDev));
        if (SUCCEEDED(r)) r = dxgiDev->GetAdapter(&adapter);
        if (SUCCEEDED(r)) r = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (SUCCEEDED(r)) r = factory->CreateSwapChain(g_pd3dDevice, &sd, &g_pSwapChain);
        if (dxgiDev) dxgiDev->Release();
        if (adapter) adapter->Release();
        if (factory) factory->Release();
        return r;
    };

    hr = tryCreateSwap(msaaCount, msaaQuality - 1);
    if (FAILED(hr) || !g_pSwapChain) {
        LOG("[d3d] CreateSwapChain MSAA x%u failed hr=0x%08lx, retrying 1x", msaaCount, hr);
        if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
        msaaCount = 1; msaaQuality = 1;
        hr = tryCreateSwap(1, 0);
    }
    if (FAILED(hr) || !g_pSwapChain) {
        LOG("[d3d] CreateSwapChain failed hr=0x%08lx", hr);
        CleanupDevice();
        return false;
    }
    LOG("[d3d] swap chain ready (MSAA x%u quality %u)", msaaCount, msaaQuality - 1);

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || !pBackBuffer) {
        LOG("[d3d] GetBuffer failed hr=0x%08lx", hr);
        CleanupDevice();
        return false;
    }
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    RECT rect; GetClientRect(hWnd, &rect);
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = (rect.right > rect.left) ? (rect.right - rect.left) : 1;
    depthDesc.Height = (rect.bottom > rect.top) ? (rect.bottom - rect.top) : 1;
    depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = msaaCount;
    depthDesc.SampleDesc.Quality = (msaaCount > 1) ? (msaaQuality - 1) : 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    
    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr) || !pDepthStencil) {
        LOG("[d3d] depth buffer failed hr=0x%08lx", hr);
        CleanupDevice();
        return false;
    }
    g_pd3dDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_mainDepthStencilView);
    pDepthStencil->Release();

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    
    ID3D11BlendState* blendState = nullptr;
    g_pd3dDevice->CreateBlendState(&blendDesc, &blendState);
    if (blendState) {
        g_pd3dDeviceContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
        blendState->Release();
    }

    return true;
}

void CleanupDevice() {
    if (g_mainDepthStencilView) { g_mainDepthStencilView->Release(); g_mainDepthStencilView = nullptr; }
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void ResizeD3DBuffers(UINT width, UINT height) {
    if (!g_pd3dDevice || !g_pSwapChain || width == 0 || height == 0) return;
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    if (g_mainDepthStencilView) { g_mainDepthStencilView->Release(); g_mainDepthStencilView = nullptr; }
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }

    HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG("[d3d] ResizeBuffers failed hr=0x%08lx", hr);
        return;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }

    DXGI_SWAP_CHAIN_DESC scd = {};
    g_pSwapChain->GetDesc(&scd);

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc = scd.SampleDesc;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (SUCCEEDED(hr) && pDepthStencil) {
        g_pd3dDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_mainDepthStencilView);
        pDepthStencil->Release();
    }
    LOG("[d3d] Buffers resized to %ux%u", width, height);
}


// ---- Low-level keyboard routing -------------------------------------------
// RegisterHotKey(VK_TAB) used to swallow Tab system-wide. The low-level hook
// lets us decide per press: plain Tab toggles 3D/2D only when CrossDim itself
// is foreground; Ctrl+Tab drives the window switcher when the shell (or a
// hijacked window) is foreground; everything else passes through untouched.
#define WM_APP_CD_TOGGLE   (WM_APP + 1)
#define WM_APP_CD_SWITCHNEXT (WM_APP + 2)
#define WM_APP_CD_SWITCHCONFIRM (WM_APP + 3)
#define WM_APP_CD_SWITCHCANCEL (WM_APP + 4)
#define WM_APP_CD_SNAP     (WM_APP + 5)
#define WM_APP_CD_SWITCHDESKTOP (WM_APP + 6)
#define WM_APP_CD_AIWAKE   (WM_APP + 20)
#define IDT_HEARTBEAT      1

static HHOOK g_keyboardHook = nullptr;

static void OpenOrAdvanceWindowSwitcher(bool reverse = false) {
    if (!g_showWindowSwitcher) {
        g_switcherWindows.clear();
        for (const auto& w : g_hijackedWindows) {
            if (IsWindow(w.hwnd) && IsWindowVisible(w.hwnd)) {
                DWORD pid = 0; GetWindowThreadProcessId(w.hwnd, &pid);
                g_switcherWindows.push_back({ w.hwnd, GetProcessPath(pid) });
            }
        }
        if (!g_switcherWindows.empty()) {
            g_showWindowSwitcher = true;
            g_switcherSelected = (g_switcherWindows.size() > 1) ? 1 : 0;
        }
    } else {
        int n = (int)g_switcherWindows.size();
        if (n > 0) {
            if (reverse) g_switcherSelected = (g_switcherSelected - 1 + n) % n;
            else g_switcherSelected = (g_switcherSelected + 1) % n;
        }
    }
}

static void ConfirmWindowSwitcher() {
    if (!g_showWindowSwitcher) return;
    if (g_switcherSelected >= 0 && g_switcherSelected < (int)g_switcherWindows.size()) {
        HWND target = g_switcherWindows[g_switcherSelected].first;
        if (IsWindow(target)) {
            if (IsIconic(target)) {
                ShowWindow(target, SW_RESTORE);
            }
            DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
            if (fgThread != targetThread) {
                AttachThreadInput(fgThread, targetThread, TRUE);
                SetForegroundWindow(target);
                BringWindowToTop(target);
                AttachThreadInput(fgThread, targetThread, FALSE);
            } else {
                SetForegroundWindow(target);
                BringWindowToTop(target);
            }
            if (g_currentState != STATE_2D_WORKBENCH) {
                g_previousUiUnlocked = g_uiUnlocked;
                g_currentState = STATE_2D_WORKBENCH;
            }
            g_uiUnlocked = true;
            while (ShowCursor(TRUE) < 0);
            ClipCursor(NULL);
        }
    }
    g_showWindowSwitcher = false;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_mainHwnd) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);

        HWND fg = GetForegroundWindow();
        bool shellFg = (fg == g_mainHwnd);
        HWND targetHijacked = nullptr;
        if (!shellFg) {
            for (const auto& w : g_hijackedWindows) {
                if (w.hwnd == fg) { shellFg = true; targetHijacked = w.hwnd; break; }
            }
        } else {
            for (auto it = g_hijackedWindows.rbegin(); it != g_hijackedWindows.rend(); ++it) {
                if (IsWindow(it->hwnd) && IsWindowVisible(it->hwnd) && !IsIconic(it->hwnd)) {
                    targetHijacked = it->hwnd;
                    break;
                }
            }
        }

        if (kb->vkCode == VK_TAB && down && !win) {
            if (ctrl && !alt) {
                if (shellFg) {
                    PostMessageW(g_mainHwnd, WM_APP_CD_SWITCHNEXT, shift ? 1 : 0, 0);
                    return 1;
                }
            } else if (!ctrl && !alt && !shift) {
                if (fg == g_mainHwnd) { PostMessageW(g_mainHwnd, WM_APP_CD_TOGGLE, 0, 0); return 1; }
            }
        }

        if (down && !win && ctrl && alt && !shift && shellFg) {
            // Desktop switching: Ctrl+Alt+1..9
            if (kb->vkCode >= '1' && kb->vkCode <= '9') {
                int slot = (int)(kb->vkCode - '1');
                if (slot < GetDesktopSlotCount()) {
                    PostMessageW(g_mainHwnd, WM_APP_CD_SWITCHDESKTOP, (WPARAM)slot, 0);
                    return 1;
                }
            }
            // Window snapping: Ctrl+Alt+Arrows
            if (targetHijacked) {
                int snapAction = -1;
                if (kb->vkCode == VK_LEFT)  snapAction = 0;
                if (kb->vkCode == VK_RIGHT) snapAction = 1;
                if (kb->vkCode == VK_UP)    snapAction = 2;
                if (kb->vkCode == VK_DOWN)  snapAction = 3;
                if (snapAction >= 0) {
                    PostMessageW(g_mainHwnd, WM_APP_CD_SNAP, (WPARAM)snapAction, (LPARAM)targetHijacked);
                    return 1;
                }
            }
        }

        if (g_showWindowSwitcher) {
            if (shellFg) {
                if (kb->vkCode == VK_CONTROL && up) {
                    PostMessageW(g_mainHwnd, WM_APP_CD_SWITCHCONFIRM, 0, 0); // pass-through: apps still see the keyup
                } else if (kb->vkCode == VK_ESCAPE && down) {
                    PostMessageW(g_mainHwnd, WM_APP_CD_SWITCHCANCEL, 0, 0);
                    return 1; // switcher is modal while open in shell context
                }
            } else {
                // Focus moved to external application: cancel switcher without capturing keys
                PostMessageW(g_mainHwnd, WM_APP_CD_SWITCHCANCEL, 0, 0);
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT menuLr = 0;
    if (FileOps::HandleMenuMsg(hWnd, msg, wParam, lParam, &menuLr)) return menuLr;

    auto toggleUiUnlock = [&](HWND hwnd) {
        g_uiUnlocked = !g_uiUnlocked;
        LOG("Tab: toggle uiUnlocked -> %d (state=%s, hij=%zu, pend=%zu)",
            g_uiUnlocked,
            (g_currentState == STATE_2D_WORKBENCH) ? "2D" : "3D",
            g_hijackedWindows.size(), g_pendingHijacks.size());
        if (g_uiUnlocked) {
            SetFocus(hwnd);
            while (ShowCursor(TRUE) < 0);
            ClipCursor(NULL);
        } else {
            SetFocus(hwnd);
            while (ShowCursor(FALSE) >= 0);

            RECT rect;
            GetClientRect(hwnd, &rect);
            MapWindowPoints(hwnd, nullptr, (POINT*)&rect, 2);
            ClipCursor(&rect);
            SetCursorPos(rect.left + (rect.right - rect.left) / 2,
                         rect.top + (rect.bottom - rect.top) / 2);
        }
    };

    if (msg == WM_HOTKEY && wParam == 1) {
        toggleUiUnlock(hWnd);
        return 0;
    }
    if (msg == WM_HOTKEY && wParam == 2) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        WCHAR cmd[] = L"taskmgr.exe";
        CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
        return 0;
    }
    if (msg == WM_HOTKEY && wParam == 3) {
        LOG("=== Cube Layout Dump (%zu cubes) ===", g_myApps.size());
        for (size_t i = 0; i < g_myApps.size(); ++i) {
            const auto& a = g_myApps[i];
            LOG("  [%2zu] name=%-24s  pos=(% 6.2f, % 6.2f, % 6.2f)",
                i, a.AppName.c_str(), a.Position.x, a.Position.y, a.Position.z);
        }
        LOG("=== End Dump ===");
        return 0;
    }
    if (msg == WM_HOTKEY && wParam == 4) {
        if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
            toggleUiUnlock(hWnd);
        }
        g_showDesktopOverview = !g_showDesktopOverview;
        return 0;
    }
    if (msg == WM_HOTKEY && wParam == 5) {
        OpenOrAdvanceWindowSwitcher(); // fallback path (only if hook install failed)
        return 0;
    }
    // Hook-driven requests (delivered on our thread via PostMessage)
    if (msg == WM_TIMER && wParam == IDT_HEARTBEAT) {
        if (g_hHeartbeatEvent) SetEvent(g_hHeartbeatEvent);
        return 0;
    }
    if (msg == WM_APP_CD_AIWAKE) return 0; // wake PeekMessage; do not toggle UI
    if (msg == WM_APP_CD_TOGGLE) { toggleUiUnlock(hWnd); return 0; }
    if (msg == WM_APP_CD_SWITCHNEXT) { OpenOrAdvanceWindowSwitcher(wParam != 0); return 0; }
    if (msg == WM_APP_CD_SWITCHCONFIRM) { ConfirmWindowSwitcher(); return 0; }
    if (msg == WM_APP_CD_SWITCHCANCEL) { g_showWindowSwitcher = false; return 0; }
    if (msg == WM_APP_CD_SWITCHDESKTOP) { SwitchToDesktop((int)wParam); return 0; }
    if (msg == WM_APP_CD_SNAP) { SnapHijackedWindow((HWND)lParam, (int)wParam); return 0; }
    if (msg == WM_KEYDOWN && wParam == VK_TAB && !g_tabHotkeyRegistered) {
        toggleUiUnlock(hWnd);
        return 0;
    }

    if (msg == WM_KEYDOWN && wParam == VK_CONTROL) g_ctrlHeld = true;
    if (msg == WM_KEYUP && wParam == VK_CONTROL) g_ctrlHeld = false;
    if (msg == WM_LBUTTONUP) g_lButtonHeld = false;

    bool imguiHandled = false;
    if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
        imguiHandled = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        if (imguiHandled && msg != WM_LBUTTONDOWN) {
            if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT)) {
                return DefWindowProc(hWnd, msg, wParam, lParam);
            }
            return true;
        }
    }

    switch (msg) {
        case WM_ACTIVATE: {
            WORD act = LOWORD(wParam);
            if (act == WA_INACTIVE) {
                ClipCursor(nullptr);
                while (ShowCursor(TRUE) < 0);
                g_ctrlHeld = false;
                g_lButtonHeld = false;
            } else {
                SetFocus(hWnd);
                if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
                    while (ShowCursor(FALSE) >= 0);
                    RECT rect;
                    GetClientRect(hWnd, &rect);
                    MapWindowPoints(hWnd, nullptr, (POINT*)&rect, 2);
                    ClipCursor(&rect);
                }
            }
            return 0;
        }
        case WM_KILLFOCUS: {
            ClipCursor(nullptr);
            while (ShowCursor(TRUE) < 0);
            g_ctrlHeld = false;
            g_lButtonHeld = false;
            return 0;
        }
        case WM_SIZE: {
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
                ResizeD3DBuffers((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
            }
            return 0;
        }
        case WM_DISPLAYCHANGE: {
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(hWnd, HWND_TOP, 0, 0, sw, sh, SWP_SHOWWINDOW);
            ResizeD3DBuffers((UINT)sw, (UINT)sh);
            return 0;
        }
        case WM_NCHITTEST: {
            if (g_uiUnlocked) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (g_taskbarRectValid && PtInRect(&g_taskbarRect, pt)) {
                    return HTCLIENT;
                }
                if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
                    return HTCLIENT;
                }
                for (const auto& win : g_hijackedWindows) {
                    if (!IsWindowVisible(win.hwnd)) continue;
                    RECT r; GetWindowRect(win.hwnd, &r);
                    if (PtInRect(&r, pt)) {
                        float barHeight = 30.0f;
                        if (pt.y < r.top + (LONG)barHeight) return HTCLIENT;
                        return HTTRANSPARENT;
                    }
                }
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < fileCount; ++i) {
                WCHAR filePath[MAX_PATH];
                DragQueryFileW(hDrop, i, filePath, MAX_PATH);
                std::wstring fpath(filePath);
                std::wstring fname = fpath;
                size_t slash = fname.rfind(L'\\');
                if (slash != std::wstring::npos) fname = fname.substr(slash + 1);
                size_t dot = fname.rfind(L'.');
                std::wstring nameOnly = (dot != std::wstring::npos) ? fname.substr(0, dot) : fname;
                std::string label = WcsToUtf8(nameOnly);

                AppCube cube = {};
                cube.Position = DirectX::XMFLOAT3(0.0f, 1.5f, 8.0f);
                cube.BaseColor = DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
                cube.AppPath = fpath;
                cube.TargetPath = fpath;
                if (fpath.size() > 4 && _wcsicmp(fpath.c_str() + fpath.size() - 4, L".lnk") == 0) {
                    std::wstring t = ResolveShortcutTarget(fpath.c_str());
                    if (!t.empty()) cube.TargetPath = t;
                }
                cube.AppName = label;
                cube.IconTexture = TextureLoader::GetCachedIcon(g_pd3dDevice, fpath);
                g_myApps.push_back(cube);
                // Re-adding a previously removed path clears its tombstone
                {
                    auto& tombs = g_desktopRemovedPaths[ActiveDesktopId()];
                    tombs.erase(std::remove(tombs.begin(), tombs.end(), fpath), tombs.end());
                }
                SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
                LOGW(L"[drop] Added: %s", fpath.c_str());
            }
            DragFinish(hDrop);
            break;
        }
        case WM_LBUTTONDOWN: {
            g_lButtonHeld = true;
            if (g_currentState == STATE_3D_EXPLORE) {
                SetFocus(hWnd);
                g_leftClicked = true;
            } 
            else if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
                bool wantsCapture = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
                if (!wantsCapture) {
                    if (g_uiUnlocked) {
                        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                        bool inHijacked = false;
                        for (const auto& win : g_hijackedWindows) {
                            if (!IsWindowVisible(win.hwnd)) continue;
                            RECT r; GetWindowRect(win.hwnd, &r);
                            if (PtInRect(&r, pt)) { inHijacked = true; break; }
                        }
                        if (!inHijacked) {
                            SetFocus(hWnd);
                            g_leftClicked = true;
                        }
                    } else {
                        SetFocus(hWnd);
                        g_leftClicked = true;
                    }
                    // 点击在 ImGui 窗口外部时的处理可选择性地切换
                    // 目前保留 ImGui 优先权，确保调参不会被中断
                }
                return 0;  // 防止重复处理
            }
            break;
        }
        case WM_LBUTTONUP: {
            g_lButtonHeld = false;
            break;
        }
        case WM_MOUSEWHEEL: {
            if (g_isDragging && g_grabbedAppIndex != -1) {
                float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
                g_dragDistance -= delta * 0.5f;
                if (g_dragDistance < 3.0f) g_dragDistance = 3.0f;
                if (g_dragDistance > 20.0f) g_dragDistance = 20.0f;
            }
            return 0;
        }
        case WM_INPUT: {
            if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
                RAWINPUT raw;
                UINT dwSize = sizeof(RAWINPUT);
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                    if (raw.header.dwType == RIM_TYPEMOUSE) {
                        g_mouseDeltaX += (float)raw.data.mouse.lLastX;
                        g_mouseDeltaY += (float)raw.data.mouse.lLastY;
                    }
                }
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
        case WM_RBUTTONDOWN: {
            g_rightClicked = true;
            return 0;
        }
        case WM_RBUTTONUP: {
            g_rightClicked = false;
            return 0;
        }
        case WM_KEYDOWN: {
            if (g_showWindowSwitcher) {
                if (wParam == VK_TAB) {
                    int n = (int)g_switcherWindows.size();
                    if (n > 0) g_switcherSelected = (g_switcherSelected + 1) % n;
                    return 0;
                }
                if (wParam == VK_ESCAPE) { g_showWindowSwitcher = false; return 0; }
                return 0;
            }
            if (wParam == VK_CONTROL) g_ctrlHeld = true;
            const bool imguiKbFocus = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
            if (wParam == VK_ESCAPE && !imguiKbFocus) PostQuitMessage(0);
            if (!imguiKbFocus) {
                if (wParam == VK_F3) g_showModelDebug = !g_showModelDebug;
                if (wParam == VK_DELETE) g_deleteRequested = true;
                if (wParam == VK_F2) g_renameRequested = true;
                if (g_ctrlHeld) {
                    if (wParam == 'C') g_copyRequested = true;
                    if (wParam == 'X') g_cutRequested = true;
                    if (wParam == 'V') g_pasteRequested = true;
                    if (wParam == 'A') g_selectAllRequested = true;
                    if (wParam == 'N') g_newFolderRequested = true;
                }
            }
            if (g_ctrlHeld && (GetKeyState(VK_MENU) & 0x8000) && !imguiKbFocus) {
                int slotCount = GetDesktopSlotCount();
                if (wParam >= '1' && wParam <= '0' + slotCount) {
                    SwitchToDesktop((int)(wParam - '1'));
                    return 0;
                }
                HWND fg = GetForegroundWindow();
                HWND targetHijacked = nullptr;
                for (const auto& w : g_hijackedWindows) {
                    if (w.hwnd == fg) { targetHijacked = w.hwnd; break; }
                }
                if (!targetHijacked) {
                    for (auto it = g_hijackedWindows.rbegin(); it != g_hijackedWindows.rend(); ++it) {
                        if (IsWindow(it->hwnd) && IsWindowVisible(it->hwnd) && !IsIconic(it->hwnd)) {
                            targetHijacked = it->hwnd;
                            break;
                        }
                    }
                }
                if (targetHijacked) {
                    int snapAction = -1;
                    if (wParam == VK_LEFT)  snapAction = 0;
                    if (wParam == VK_RIGHT) snapAction = 1;
                    if (wParam == VK_UP)    snapAction = 2;
                    if (wParam == VK_DOWN)  snapAction = 3;
                    if (snapAction >= 0) {
                        SnapHijackedWindow(targetHijacked, snapAction);
                        return 0;
                    }
                }
                return 0;
            }
            return 0;
        }

        case WM_KEYUP:
            if (g_showWindowSwitcher && wParam == VK_CONTROL) {
                ConfirmWindowSwitcher();
                return 0;
            }
            if (wParam == VK_CONTROL) g_ctrlHeld = false;
            return 0;

        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void RestoreHijackedWindowsList(const std::vector<HijackedWindow>& wins) {
    for (const auto& w : wins) {
        if (!IsWindow(w.hwnd)) continue;
        if (w.originalStyle) {
            SetWindowLong(w.hwnd, GWL_STYLE, w.originalStyle);
            SetWindowPos(w.hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        ShowWindow(w.hwnd, SW_SHOW);
    }
}

static void RestoreAllHijackedWindows() {
    RestoreHijackedWindowsList(g_hijackedWindows);
    for (const auto& dWins : g_desktopWindows) {
        RestoreHijackedWindowsList(dWins);
    }
}

static LONG WINAPI CrashRecoveryFilter(EXCEPTION_POINTERS* ep) {
    LOG("[CRASH] Unhandled exception detected (code=0x%08X) - emergency shell recovery executing",
        ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0);
    RestoreAllHijackedWindows();
    SetSystemTaskbarVisible(true);
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    ClipCursor(nullptr);
    while (ShowCursor(TRUE) < 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(CrashRecoveryFilter);

    HANDLE hSingleton = CreateMutexW(nullptr, TRUE, L"Local\\CrossDim_Singleton");
    if (hSingleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hSingleton);
        return 0;
    }

    // Modern Windows 10/11 Per-Monitor DPI Awareness V2
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pSetContext = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetContext) {
            pSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    } else {
        SetProcessDPIAware();
    }

    // Anchor CWD to the exe directory: assets, logs and .cddesk persistence all
    // use relative paths and must not depend on how the process was launched.
    {
        WCHAR exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        WCHAR* slash = wcsrchr(exePath, L'\\');
        if (slash) { *slash = L'\0'; SetCurrentDirectoryW(exePath); }
    }

    HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comUninit = (comInit == S_OK || comInit == S_FALSE);
    bool comCanUse = (comInit == S_OK || comInit == S_FALSE || comInit == RPC_E_CHANGED_MODE);
    if (comCanUse) {
        InitAudioEndpointVolume();
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"CrossDimShell", nullptr };
    RegisterClassExW(&wc);
    
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"CrossDim", 
                                WS_POPUP | WS_VISIBLE, 
                                0, 0, screenW, screenH, 
                                nullptr, nullptr, wc.hInstance, nullptr);
    g_mainHwnd = hwnd;
    // Tab / Ctrl+Tab go through the low-level hook so they only fire in shell
    // context. Global hotkeys remain as a fallback if the hook can't install.
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    Logger::Instance().Init();
    if (g_keyboardHook) {
        g_tabHotkeyRegistered = true; // routing handled by the hook
        LOG("[input] low-level keyboard hook installed (Tab/Ctrl+Tab are context-sensitive)");
    } else {
        LOG("[input] WH_KEYBOARD_LL failed, falling back to global hotkeys");
        g_tabHotkeyRegistered = (RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_TAB) != 0);
        RegisterHotKey(hwnd, 5, MOD_CONTROL | MOD_NOREPEAT, VK_TAB);
    }
    RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_ESCAPE);
    RegisterHotKey(hwnd, 3, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D');
    RegisterHotKey(hwnd, 4, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'T');
    // AI Control Interface: HTTP server on background thread.
    // The notify callback posts a message to wake the render loop quickly.
    AIServer::SetNotifyMain([]() {
        if (g_mainHwnd) PostMessageW(g_mainHwnd, WM_APP_CD_AIWAKE, 0, 0);
    });
    AIServer::Start();
    SetSystemTaskbarVisible(false);
    DragAcceptFiles(hwnd, TRUE);
    RAWINPUTDEVICE rid[1];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = 0;
    rid[0].hwndTarget = hwnd;

    if (RegisterRawInputDevices(rid, 1, sizeof(rid[0])) == FALSE) {
        OutputDebugStringW(L"Raw Input 注册失败！\n");
    }

    if (!InitDevice(hwnd)) {
        LOG("[d3d] InitDevice failed — aborting");
        SetSystemTaskbarVisible(true);
        CleanupDevice();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    LOG("[init] device ok");

    g_hHeartbeatEvent = CreateEventW(nullptr, FALSE, FALSE, L"Global\\CrossDim_Heartbeat");
    if (!g_hHeartbeatEvent)
        g_hHeartbeatEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\CrossDim_Heartbeat");
    g_hShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, L"Global\\CrossDim_Shutdown");
    if (!g_hShutdownEvent)
        g_hShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\CrossDim_Shutdown");
    LOG("[init] heartbeat=%d shutdown=%d", g_hHeartbeatEvent != nullptr, g_hShutdownEvent != nullptr);
    SetTimer(hwnd, IDT_HEARTBEAT, 1000, nullptr);
    {
        STARTUPINFOW wdSi = { sizeof(wdSi) };
        PROCESS_INFORMATION wdPi = {};
        WCHAR wdPath[MAX_PATH];
        GetModuleFileNameW(NULL, wdPath, MAX_PATH);
        std::wstring wdDir(wdPath);
        size_t wdPos = wdDir.rfind(L'\\');
        if (wdPos != std::wstring::npos) wdDir = wdDir.substr(0, wdPos);
        swprintf_s(wdPath, L"%s\\watchdog.exe", wdDir.c_str());
        if (CreateProcessW(wdPath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &wdSi, &wdPi)) {
            CloseHandle(wdPi.hProcess);
            CloseHandle(wdPi.hThread);
        }
    }

    ShowWindow(hwnd, SW_SHOW);
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, sw, sh, SWP_SHOWWINDOW);
        LOG("[init] window %dx%d (screen %dx%d)", sw, sh, screenW, screenH);
    }
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    ShowCursor(FALSE);

    Camera camera;
    // Default framing mirrors the Win11 Bloom wallpaper composition: flower
    // right-of-center, cube wall to the left. Rotation is (pitch, yaw, roll)
    // in DEGREES (engine convention; Camera::Update converts to radians).
    camera.Rotation = DirectX::XMFLOAT3(-10.0f, 15.0f, 0.0f);
    CubeRenderer cubeRenderer;
    SkyboxRenderer skybox;
    ModelRenderer modelRenderer;
    
    LOG("[init] cube renderer");
    if (!cubeRenderer.Initialize(g_pd3dDevice)) LOG("[init] CubeRenderer::Initialize failed");
    LOG("[init] skybox");
    if (!skybox.Initialize(g_pd3dDevice)) LOG("[init] SkyboxRenderer::Initialize failed");
    LOG("[init] model renderer");
    if (!modelRenderer.Initialize(g_pd3dDevice)) LOG("[init] ModelRenderer::Initialize failed");

    modelRenderer.LoadModelAsync(WcsToUtf8(ResolveRuntimePathW(L"assets\\bloom_high\\bloom_high.obj")));
    if (!LoadVirtualDesktopsConfig()) {
        g_desktopSlots = { 0 };
        g_desktopWindows = { {} };
        g_activeDesktopIndex = 0;
        g_nextDesktopId = 1;
    }
    LOG("[init] scan desktop (active desktop id=%d)", ActiveDesktopId());
    ScanDesktopForApps(g_myApps);
    LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
    SeparateOverlappingCubes(g_myApps);
    LoadIconsForApps(g_myApps);
    InitDesktopWatchers();
    LOG("[init] desktop cubes=%d", (int)g_myApps.size());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImVector<ImWchar> fontRanges;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
        ImFontGlyphRangesBuilder gb;
        gb.AddRanges(io.Fonts->GetGlyphRangesDefault());
        gb.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        for (const auto& c : g_myApps) gb.AddText(c.AppName.c_str());
        gb.BuildRanges(&fontRanges);
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 20.0f, NULL, fontRanges.Data);
    } else {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    g_mouseDeltaX = 0.0f;
    g_mouseDeltaY = 0.0f;
    LOG("[init] imgui ready, entering loop");


    MSG msg; bool done = false;
        while (!done) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        {
            static int s_bootFrames = 0;
            if (s_bootFrames < 5) {
                LOG("[init] frame %d", s_bootFrames);
                s_bootFrames++;
            }
        }

        {
            static int s_pruneFrames = 0;
            TextureLoader::PollIconLoads(g_pd3dDevice);
            AssignIconsFromCache(g_myApps);
            AssignIconsFromCache(g_folderCubes);
            if (g_needRescan) {
                g_needRescan = false;
                RescanCurrentView();
            }
            if (++s_pruneFrames >= 60) {
                s_pruneFrames = 0;
                for (auto it = g_taskbarWindowIconCache.begin(); it != g_taskbarWindowIconCache.end(); ) {
                    if (!IsWindow(it->first)) {
                        if (it->second) it->second->Release();
                        it = g_taskbarWindowIconCache.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (auto it = g_taskbarDynamicOrder.begin(); it != g_taskbarDynamicOrder.end(); ) {
                    if (!IsWindow(it->first))
                        it = g_taskbarDynamicOrder.erase(it);
                    else
                        ++it;
                }
                if (g_systemTaskbarHidden) SetSystemTaskbarVisible(false);
            }
        }

        static ULONGLONG lastTick = GetTickCount64();
        ULONGLONG nowTick = GetTickCount64();
        float dt = (nowTick - lastTick) / 1000.0f;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        lastTick = nowTick;
        if (g_hHeartbeatEvent) SetEvent(g_hHeartbeatEvent);
        { float ifps = (dt > 0.0001f) ? (1.0f / dt) : 0.0f; g_fpsCurrent += (ifps - g_fpsCurrent) * 0.02f; }
        static float appSpinTime = 0.0f;
        appSpinTime += dt;

        const float sceneFov = 90.0f;
        float mouseMag = fabsf(g_mouseDeltaX) + fabsf(g_mouseDeltaY);
        if (mouseMag > g_modelFovRotateDeadzone) {
            g_modelRotateAccum += mouseMag;
        }
        g_modelRotateAccum -= g_modelRotateAccumDecay * dt;
        if (g_modelRotateAccum < 0.0f) g_modelRotateAccum = 0.0f;

        bool rotateInput = (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked &&
                            g_modelRotateAccum > g_modelRotateAccumThreshold);
        if (rotateInput) {
            g_modelFovRotateHold = g_modelFovRotateHoldTime;
        } else if (g_modelFovRotateHold > 0.0f) {
            g_modelFovRotateHold -= dt;
            if (g_modelFovRotateHold < 0.0f) g_modelFovRotateHold = 0.0f;
        }
        bool rotateActive = g_modelFovSnapWhenRotate && (rotateInput || g_modelFovRotateHold > 0.0f);
        float modelFovEffective = sceneFov;
        if (g_modelUseIndependentProj) {
            if (g_modelFovCurrent <= 0.0f) g_modelFovCurrent = g_modelFov;
            float targetFov = g_modelFov;
            if (rotateActive) targetFov = sceneFov;
            float speed = rotateActive ? g_modelFovSnapSpeed : g_modelFovReturnSpeed;
            float t = speed * dt;
            if (t > 1.0f) t = 1.0f;
            g_modelFovCurrent += (targetFov - g_modelFovCurrent) * t;
            modelFovEffective = g_modelFovCurrent;
        } else {
            g_modelFovCurrent = sceneFov;
            modelFovEffective = sceneFov;
        }
        float sceneTan = tanf(DirectX::XMConvertToRadians(sceneFov) * 0.5f);
        float modelTan = tanf(DirectX::XMConvertToRadians(modelFovEffective) * 0.5f);
        float modelScaleComp = (sceneTan > 0.0001f && modelTan > 0.0001f) ? (modelTan / sceneTan) : 1.0f;

        if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
            if (g_folderTransitionT > 0.0f && g_folderTransitionT < 1.0f) {
                float t = g_folderTransitionT;
                float ease = t * t * (3.0f - 2.0f * t); // smoothstep
                camera.Position.x = g_folderFromPos.x + (g_folderToPos.x - g_folderFromPos.x) * ease;
                camera.Position.y = g_folderFromPos.y + (g_folderToPos.y - g_folderFromPos.y) * ease;
                camera.Position.z = g_folderFromPos.z + (g_folderToPos.z - g_folderFromPos.z) * ease;
                camera.Rotation.x = g_folderFromRot.x + (g_folderToRot.x - g_folderFromRot.x) * ease;
                camera.Rotation.y = g_folderFromRot.y + (g_folderToRot.y - g_folderFromRot.y) * ease;
                camera.Rotation.z = g_folderFromRot.z + (g_folderToRot.z - g_folderFromRot.z) * ease;
                g_mouseDeltaX = 0.0f; g_mouseDeltaY = 0.0f;
                g_folderTransitionT += dt * 3.5f;
                if (g_folderTransitionT >= 1.0f) {
                    camera.Position = g_folderToPos;
                    camera.Rotation = g_folderToRot;
                    g_folderTransitionT = 1.0f;
                }
            }
            
            if (g_mouseDeltaX != 0.0f || g_mouseDeltaY != 0.0f) {
                camera.Rotate(g_mouseDeltaX, g_mouseDeltaY, 0.15f);
                g_mouseDeltaX = 0.0f; g_mouseDeltaY = 0.0f;
            }
            
            RECT winRect; GetWindowRect(hwnd, &winRect);
            SetCursorPos(winRect.left + (winRect.right - winRect.left) / 2, 
                         winRect.top + (winRect.bottom - winRect.top) / 2);
        }

        for (auto it = g_pendingHijacks.begin(); it != g_pendingHijacks.end(); ) {
            it->frameWait++;
            HWND target = FindWindowFromProcessId(it->processId);
            if (target != NULL && target != hwnd && target != it->originalFocus) {
                LONG style = GetWindowLong(target, GWL_STYLE);
                bool isStandard = ((style & WS_CAPTION) == WS_CAPTION);
                bool isModernBorderless = !(style & WS_CHILD) && (style & WS_VISIBLE) && ((style & WS_THICKFRAME) || (style & WS_POPUP));
                if (isStandard || isModernBorderless) {
                    OutputDebugStringW(L"[成功] 捕获到目标弹出的窗口！实施扒衣！\n");
                    LONG originalStyle = style; // captured for restoration on exit
                    style &= ~(WS_CAPTION | WS_SYSMENU);
                    style |= WS_THICKFRAME;
                    SetWindowLong(target, GWL_STYLE, style);
                    RECT winRect; GetWindowRect(hwnd, &winRect);
                    int w = 1100; int h = 750; 
                    int x = winRect.left + (winRect.right - winRect.left - w) / 2;
                    int y = winRect.top + (winRect.bottom - winRect.top - h) / 2;
                    SetWindowPos(target, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);

                    bool exists = false;
                    for (const auto& win : g_hijackedWindows) {
                        if (win.hwnd == target) { exists = true; break; }
                    }
                    if (!exists) {
                        HijackedWindow hw = {}; hw.hwnd = target; hw.originalStyle = originalStyle;
                        g_hijackedWindows.push_back(hw);
                        WCHAR wtitle[256]; GetWindowTextW(target, wtitle, 256);
                        char hbuf[32];
                        sprintf_s(hbuf, "0x%p", target);
                        AIServer::EmitEvent("window_open", std::string("{\"hwnd\":\"") + hbuf + "\",\"title\":\"" +
                                             Json::Escape(WcsToUtf8(wtitle)) + "\"}");
                    }

                    it = g_pendingHijacks.erase(it);
                    continue;
                }
            }
            
            if (it->frameWait > 120) { 
                LOGW(L"[hijack] pending hijack PID=%u timed out (120 frames)", it->processId);
                it = g_pendingHijacks.erase(it);
                if (g_pendingHijacks.empty() && g_hijackedWindows.empty()) {
                    g_currentState = STATE_3D_EXPLORE;
                    g_uiUnlocked = true;
                    while (ShowCursor(TRUE) < 0); ClipCursor(NULL);
                }
            } else {
                ++it;
            }
        }
  
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Folder breadcrumb top bar (maximized mode only)
        RenderFolderBreadcrumbs(camera);


        if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
            ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
            ImVec2 center = ImVec2(ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x * 0.5f,
                                   ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y * 0.5f);
            draw_list->AddLine(ImVec2(center.x - 10, center.y), ImVec2(center.x + 10, center.y), IM_COL32(255, 255, 255, 255), 2.0f);
            draw_list->AddLine(ImVec2(center.x, center.y - 10), ImVec2(center.x, center.y + 10), IM_COL32(255, 255, 255, 255), 2.0f);
        }
        RenderTaskbar(g_pd3dDevice, hwnd);

        if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
            RenderWindowChrome();
        }

        // Folder window chrome (windowed mode)
        if (g_isInFolder && !g_isFolderMaximized) {
            RenderFolderWindowChrome(camera);
        }

        // Task View overlay
        if (g_showDesktopOverview) {
            RenderDesktopOverview(g_pd3dDevice);
        }

        // Clean up dead hijacked windows every frame
        {
            std::vector<HijackedWindow> live;
            for (const auto& w : g_hijackedWindows) {
                if (IsWindow(w.hwnd)) live.push_back(w);
            }
            if (live.size() != g_hijackedWindows.size()) {
                g_hijackedWindows.swap(live);
            }
        }

        // Auto-return: when all windows gone, restore original 3D state
        if (g_currentState == STATE_2D_WORKBENCH && g_hijackedWindows.empty() && g_pendingHijacks.empty()) {
            LOG("[state] auto-return: 2D->3D, cursor unlocked");
            g_currentState = STATE_3D_EXPLORE;
            g_uiUnlocked = g_previousUiUnlocked;
            if (g_uiUnlocked) {
                while (ShowCursor(TRUE) < 0);
                ClipCursor(NULL);
            } else {
                while (ShowCursor(FALSE) >= 0);
                RECT rect;
                GetClientRect(hwnd, &rect);
                MapWindowPoints(hwnd, nullptr, (POINT*)&rect, 2);
                ClipCursor(&rect);
                SetCursorPos(rect.left + (rect.right - rect.left) / 2,
                             rect.top + (rect.bottom - rect.top) / 2);
            }
        }

        {
            static int s_stuck2DFrames = 0;
            static ULONGLONG s_lastStuckLog = 0;
            if (g_currentState == STATE_2D_WORKBENCH) {
                bool anyValid = false;
                for (const auto& win : g_hijackedWindows) {
                    if (IsWindow(win.hwnd)) { anyValid = true; break; }
                }
                if (!anyValid && g_hijackedWindows.empty()) {
                    anyValid = !g_pendingHijacks.empty();
                }
                if (!anyValid) s_stuck2DFrames++;
                else s_stuck2DFrames = 0;
                if (s_stuck2DFrames > 600) {
                    LOG("[force] stuck in 2D for %d frames, force-clear (hij=%zu, pend=%zu)",
                        s_stuck2DFrames, g_hijackedWindows.size(), g_pendingHijacks.size());
                    g_hijackedWindows.clear();
                    g_pendingHijacks.clear();
                    g_currentState = STATE_3D_EXPLORE;
                    g_uiUnlocked = true;
                    while (ShowCursor(TRUE) < 0);
                    ClipCursor(NULL);
                    s_stuck2DFrames = 0;
                }
                if (s_stuck2DFrames > 0 && s_stuck2DFrames % 300 == 0
                    && nowTick - s_lastStuckLog > 5000) {
                    LOG("[warn] possibly stuck 2D: frames=%d, hij=%zu, pend=%zu",
                        s_stuck2DFrames, g_hijackedWindows.size(), g_pendingHijacks.size());
                    s_lastStuckLog = nowTick;
                }
            } else {
                s_stuck2DFrames = 0;
            }
        }

        if ((g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) && g_showModelDebug) {
            ImGui::Begin("Model Debug", &g_showModelDebug, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextUnformatted("F3 to hide");
            ImGui::Text("Camera Rotation (pitch,yaw,roll): %.2f, %.2f, %.2f",
                        camera.Rotation.x, camera.Rotation.y, camera.Rotation.z);
            ImGui::Separator();
            ImGui::Text("Transform Controls");
            ImGui::Separator();
            ImGui::DragFloat3("Position", &g_modelPosition.x, 0.05f, -50.0f, 50.0f);
            ImGui::DragFloat3("Rotation (pitch,yaw,roll)", &g_modelRotation.x, 0.01f, -6.28f, 6.28f);
            ImGui::DragFloat("Scale", &g_modelScale, 0.01f, 0.001f, 50.0f);

            ImGui::Separator();
            ImGui::Text("Model Projection");
            ImGui::Checkbox("Independent FOV", &g_modelUseIndependentProj);
            ImGui::SliderFloat("Model FOV", &g_modelFov, 30.0f, 120.0f, "%.1f deg");
            ImGui::Checkbox("Snap to scene FOV while rotating", &g_modelFovSnapWhenRotate);
            ImGui::SliderFloat("Snap speed", &g_modelFovSnapSpeed, 1.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("Return speed", &g_modelFovReturnSpeed, 1.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("Rotate hold", &g_modelFovRotateHoldTime, 0.0f, 0.5f, "%.2f s");
            ImGui::SliderFloat("Rotate deadzone", &g_modelFovRotateDeadzone, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Rotate trigger", &g_modelRotateAccumThreshold, 0.0f, 30.0f, "%.1f");
            ImGui::SliderFloat("Rotate decay", &g_modelRotateAccumDecay, 0.0f, 30.0f, "%.1f/s");
            ImGui::Checkbox("Lock screen position", &g_modelLockScreenPos);
            ImGui::Text("Effective Model FOV: %.1f", modelFovEffective);
            ImGui::Text("Size compensation: %.3f", modelScaleComp);

            ImGui::Separator();
            ImGui::Text("Material & Shading");
            ImGui::SliderFloat("Ambient (Shadow Depth)", &g_modelMatParams.x, 0.0f, 1.0f);
            ImGui::SliderFloat("Diffuse (Light Power)", &g_modelMatParams.y, 0.0f, 2.0f);
            ImGui::SliderFloat("Rim Light Intensity", &g_modelMatParams.z, 0.0f, 3.0f);
            ImGui::SliderFloat("Rim Light Sharpness", &g_modelMatParams.w, 0.1f, 10.0f);
            ImGui::ColorEdit3("Color Tint", &g_modelColorTint.x);
            
            if (ImGui::Button("Reset All")) {
                g_modelPosition = DirectX::XMFLOAT3(-2.45f, -2.15f, 9.3f);
                g_modelRotation = DirectX::XMFLOAT3(-0.1f, 1.0f, -0.11f);
                g_modelScale = 10.0f;
                g_modelMatParams = { 0.5f, 1.0f, 0.0f, 0.1f };
                g_modelColorTint = { 1.0f, 1.0f, 1.0f, 1.0f };
            }

            ImGui::Separator();
            ImGui::Text("Desktop Apps: %d cubes", (int)g_myApps.size());
            if (ImGui::Button("Reload Desktop Apps")) {
                g_myApps.clear();
                ScanDesktopForApps(g_myApps);
                LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
                SeparateOverlappingCubes(g_myApps);
                LoadIconsForApps(g_myApps);
            }
            ImGui::End();
        }

        if (modelRenderer.IsLoading() && !modelRenderer.HasModel()) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            ImVec2 p0 = viewport->WorkPos;
            ImVec2 p1 = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x,
                               viewport->WorkPos.y + viewport->WorkSize.y);
            draw->AddRectFilled(p0, p1, IM_COL32(8, 10, 14, 220));

            const char* loadingText = "Loading model...";
            ImVec2 textSize = ImGui::CalcTextSize(loadingText);
            ImVec2 center = ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                          IM_COL32(240, 240, 240, 255), loadingText);
        }

        const float clear_color[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, g_mainDepthStencilView);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        g_pd3dDeviceContext->ClearDepthStencilView(g_mainDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

        g_pd3dDeviceContext->OMSetDepthStencilState(nullptr, 0);
        g_pd3dDeviceContext->RSSetState(nullptr);

        camera.Update();
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        g_aiFrameW = clientRect.right - clientRect.left;
        g_aiFrameH = clientRect.bottom - clientRect.top;
        float width = (float)(clientRect.right - clientRect.left);
        float height = (float)(clientRect.bottom - clientRect.top);
        if (width <= 0) width = 1.0f;
        if (height <= 0) height = 1.0f;

        float aspectRatio = width / height;

        D3D11_VIEWPORT vp; ZeroMemory(&vp, sizeof(D3D11_VIEWPORT));
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width = width; vp.Height = height; 
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        g_pd3dDeviceContext->RSSetViewports(1, &vp);
       
        DirectX::XMMATRIX viewMatrix = camera.GetViewMatrix();
        DirectX::XMMATRIX projMatrix = camera.GetProjectionMatrix(sceneFov, aspectRatio, 0.1f, 1000.0f);
        DirectX::XMMATRIX modelProjMatrix = projMatrix;
        if (g_modelUseIndependentProj) {
            modelProjMatrix = camera.GetProjectionMatrix(modelFovEffective, aspectRatio, 0.1f, 1000.0f);
        }
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&det, viewMatrix * projMatrix);
        skybox.Render(g_pd3dDeviceContext, invViewProj, camera.Position);

        {
            modelRenderer.PollFinalizeLoad();
            // 自动捕获模型的几何中心
            if (!g_pivotAutoSet && modelRenderer.HasModel()) {
                DirectX::XMFLOAT3 center = modelRenderer.GetModelCenter();
                g_modelPivot = center;
                g_pivotAutoSet = true;
            }

            DirectX::XMFLOAT3 P = g_modelPosition;
            DirectX::XMFLOAT3 R = g_modelRotation;
            float S = g_modelScale * modelScaleComp;
            DirectX::XMFLOAT3 Pivot = g_modelPivot;
            DirectX::XMFLOAT3 renderP = P;
            if (g_modelUseIndependentProj && g_modelLockScreenPos && fabsf(modelFovEffective - sceneFov) > 0.01f) {
                DirectX::XMVECTOR pWorld = DirectX::XMLoadFloat3(&P);
                DirectX::XMVECTOR pView = DirectX::XMVector3TransformCoord(pWorld, viewMatrix);
                float pvz = DirectX::XMVectorGetZ(pView);
                if (pvz > 0.0001f) {
                    DirectX::XMVECTOR ndc = DirectX::XMVector3TransformCoord(pView, projMatrix);
                    float ndcX = DirectX::XMVectorGetX(ndc);
                    float ndcY = DirectX::XMVectorGetY(ndc);
                    DirectX::XMFLOAT4X4 projModel;
                    DirectX::XMStoreFloat4x4(&projModel, modelProjMatrix);
                    if (fabsf(projModel._11) > 0.00001f && fabsf(projModel._22) > 0.00001f) {
                        float lockX = ndcX * pvz / projModel._11;
                        float lockY = ndcY * pvz / projModel._22;
                        DirectX::XMVECTOR pViewLocked = DirectX::XMVectorSet(lockX, lockY, pvz, 1.0f);
                        DirectX::XMVECTOR deltaView = DirectX::XMVectorSubtract(pViewLocked, pView);
                        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                        DirectX::XMVECTOR deltaWorld = DirectX::XMVector3TransformNormal(deltaView, invView);
                        DirectX::XMVECTOR pWorldLocked = DirectX::XMVectorAdd(pWorld, deltaWorld);
                        DirectX::XMStoreFloat3(&renderP, pWorldLocked);
                    }
                }
            }

            // =========================================================
            // S.R.T (scale -> rotate -> translate), row-vector order (left-to-right).
            // Pivot is in model space; P is the world-space location of the pivot.
            // =========================================================
            DirectX::XMMATRIX worldMat = 
                DirectX::XMMatrixTranslation(-Pivot.x, -Pivot.y, -Pivot.z) *
                DirectX::XMMatrixScaling(S, S, S) *
                DirectX::XMMatrixRotationRollPitchYaw(R.x, R.y, R.z) *
                DirectX::XMMatrixTranslation(renderP.x, renderP.y, renderP.z);
            // =========================================================

            modelRenderer.Render(
                g_pd3dDeviceContext,
                viewMatrix * modelProjMatrix,
                worldMat,
                camera.Position,
                g_modelMatParams,
                g_modelColorTint
            );
        }

        DirectX::XMVECTOR rayOrigin = DirectX::XMLoadFloat3(&camera.Position);
        DirectX::XMVECTOR rayDir    = DirectX::XMLoadFloat3(&camera.GetForward());
        if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 mouse = ImGui::GetIO().MousePos;
            if (width > 1.0f && height > 1.0f) {
                float mx = (mouse.x - viewport->WorkPos.x) / width;
                float my = (mouse.y - viewport->WorkPos.y) / height;
                if (mx < 0.0f) mx = 0.0f; if (mx > 1.0f) mx = 1.0f;
                if (my < 0.0f) my = 0.0f; if (my > 1.0f) my = 1.0f;
                mx = mx * 2.0f - 1.0f;
                my = 1.0f - my * 2.0f;
                DirectX::XMVECTOR nearClip = DirectX::XMVectorSet(mx, my, 0.0f, 1.0f);
                DirectX::XMVECTOR farClip = DirectX::XMVectorSet(mx, my, 1.0f, 1.0f);
                DirectX::XMVECTOR nearWorld = DirectX::XMVector3TransformCoord(nearClip, invViewProj);
                DirectX::XMVECTOR farWorld = DirectX::XMVector3TransformCoord(farClip, invViewProj);
                rayDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farWorld, rayOrigin));
            }
        }
        
        // Check if mouse is inside the folder portal window
        bool mouseInPortal = false;
        if (g_isInFolder && !g_isFolderMaximized) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            RECT& fwr = g_folderWindowRect;
            mouseInPortal = (mp.x >= fwr.left && mp.x <= fwr.right && mp.y >= fwr.top && mp.y <= fwr.bottom);
        }

        int hitAppIndex = -1;
        float minDistance = 9999.0f;
        if (!mouseInPortal) {
        for (int i = 0; i < g_myApps.size(); i++) {
            g_myApps[i].IsHovered = false;
            float angleY = atan2(g_myApps[i].Position.x - camera.Position.x, g_myApps[i].Position.z - camera.Position.z);
            DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, angleY, 0.0f);
            DirectX::XMFLOAT4 qf; DirectX::XMStoreFloat4(&qf, q);
            DirectX::BoundingOrientedBox obb(g_myApps[i].Position, DirectX::XMFLOAT3(0.6f, 0.6f, 0.3f), qf);
            float dist;
            if (obb.Intersects(rayOrigin, rayDir, dist)) {
                if (dist < minDistance) { minDistance = dist; hitAppIndex = i; }
            }
        }
        if (hitAppIndex != -1) g_myApps[hitAppIndex].IsHovered = true;
        } // !mouseInPortal guard

        // Portal cube interaction (full mirror of desktop)
        int portalHitIdx = -1;
        if (mouseInPortal && !g_folderCubes.empty()) {
            float fwW = (float)(g_folderWindowRect.right - g_folderWindowRect.left);
            float fwH = (float)(g_folderWindowRect.bottom - g_folderWindowRect.top);
            DirectX::XMMATRIX fVP = g_folderCamera.GetViewMatrix() * g_folderCamera.GetProjectionMatrix(90.0f, fwW/(fwH>0?fwH:1.0f), 0.1f, 100.0f);
            DirectX::XMMATRIX fInvVP = DirectX::XMMatrixInverse(nullptr, fVP);
            ImVec2 m = ImGui::GetIO().MousePos;
            float fmx = (m.x - g_folderWindowRect.left) / fwW * 2.0f - 1.0f;
            float fmy = 1.0f - (m.y - g_folderWindowRect.top) / fwH * 2.0f;
            DirectX::XMVECTOR fNear = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(fmx, fmy, 0, 1), fInvVP);
            DirectX::XMVECTOR fFar  = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(fmx, fmy, 1, 1), fInvVP);
            DirectX::XMVECTOR fOri = DirectX::XMLoadFloat3(&g_folderCamera.Position);
            DirectX::XMVECTOR fDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(fFar, fOri));
            float fMin = 9999.0f;
            for (int i = 0; i < (int)g_folderCubes.size(); i++) {
                g_folderCubes[i].IsHovered = false;
                DirectX::BoundingBox box(g_folderCubes[i].Position, DirectX::XMFLOAT3(0.6f,0.6f,0.3f));
                float d; if (box.Intersects(fOri, fDir, d) && d < fMin) { fMin = d; portalHitIdx = i; }
            }
            if (portalHitIdx >= 0) g_folderCubes[portalHitIdx].IsHovered = true;

            static DWORD pLastClickT = 0; static int pLastIdx = -1;
            static int pGrabIdx = -1; static bool pDragging = false;
            static DWORD pDownTime = 0; static float pDragDist = 8.0f;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (portalHitIdx >= 0) {
                    if (GetTickCount() - pLastClickT < 400 && pLastIdx == portalHitIdx) {
                        pLastClickT = 0;
                        if (CubeIsDirectory(g_folderCubes[portalHitIdx])) {
                            EnterFolder(CubeFolderPath(g_folderCubes[portalHitIdx]), camera);
                        } else {
                            LaunchCube(g_folderCubes[portalHitIdx]);
                        }
                    } else {
                        pLastClickT = GetTickCount(); pLastIdx = portalHitIdx;
                        if (!g_ctrlHeld) { for (auto& c : g_folderCubes) c.IsSelected = false; }
                        g_folderCubes[portalHitIdx].IsSelected = true;
                        pGrabIdx = portalHitIdx; pDownTime = GetTickCount();
                        DirectX::XMVECTOR cPos = fOri;
                        DirectX::XMVECTOR aPos = DirectX::XMLoadFloat3(&g_folderCubes[portalHitIdx].Position);
                        pDragDist = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(aPos, cPos)));
                        if (pDragDist < 3.0f) pDragDist = 8.0f;
                    }
                } else {
                    if (!g_ctrlHeld) { for (auto& c : g_folderCubes) c.IsSelected = false; }
                }
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && pGrabIdx >= 0) {
                if (GetTickCount() - pDownTime > 150) {
                    pDragging = true;
                    DirectX::XMVECTOR newPos = DirectX::XMVectorAdd(fOri, DirectX::XMVectorScale(fDir, pDragDist));
                    DirectX::XMFLOAT3 tp; DirectX::XMStoreFloat3(&tp, newPos);
                    DirectX::XMFLOAT3 delta = { tp.x - g_folderCubes[pGrabIdx].Position.x,
                                                 tp.y - g_folderCubes[pGrabIdx].Position.y,
                                                 tp.z - g_folderCubes[pGrabIdx].Position.z };
                    for (int i = 0; i < (int)g_folderCubes.size(); ++i) {
                        if (!g_folderCubes[i].IsSelected) continue;
                        g_folderCubes[i].Position.x += delta.x;
                        g_folderCubes[i].Position.y += delta.y;
                        g_folderCubes[i].Position.z += delta.z;
                    }
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && pGrabIdx >= 0) {
                pGrabIdx = -1; pDragging = false;
            }
            if (g_rightClicked) g_rightClicked = false;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (portalHitIdx >= 0) {
                    if (!g_folderCubes[portalHitIdx].IsSelected) {
                        if (!g_ctrlHeld) { for (auto& c : g_folderCubes) c.IsSelected = false; }
                        g_folderCubes[portalHitIdx].IsSelected = true;
                    }
                    std::vector<std::wstring> paths;
                    for (const auto& c : g_folderCubes)
                        if (c.IsSelected && !c.AppPath.empty()) paths.push_back(c.AppPath);
                    QueueShellItemMenu(paths);
                } else {
                    QueueShellBackgroundMenu(g_folderStack.empty() ? CurrentShellFolder() : g_folderStack.back().folderPath);
                }
            }
        }

        if (!mouseInPortal) {
        if (g_leftClicked) {
            if (hitAppIndex != -1) {
                if (GetTickCount() - g_lastClickTime < 400 && g_lastClickedApp == hitAppIndex) {
                    g_previousUiUnlocked = g_uiUnlocked;
                    if (CubeIsDirectory(g_myApps[hitAppIndex])) {
                        EnterFolder(CubeFolderPath(g_myApps[hitAppIndex]), camera);
                    } else {
                        LaunchCube(g_myApps[hitAppIndex]);
                    }
                    g_lastClickTime = 0;
                } else {
                    g_lastClickTime = GetTickCount();
                    g_lastClickedApp = hitAppIndex;
                    
                    if (!(g_ctrlHeld)) {
                        if (!g_myApps[hitAppIndex].IsSelected) {
                            for (auto& a : g_myApps) a.IsSelected = false;
                        }
                    }
                    g_myApps[hitAppIndex].IsSelected = true;

        bool portalLocked = false;
        if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
                        g_grabbedAppIndex = hitAppIndex;
                        g_mouseDownTime = GetTickCount();
                        DirectX::XMVECTOR cPos = DirectX::XMLoadFloat3(&camera.Position);
                        DirectX::XMVECTOR aPos = DirectX::XMLoadFloat3(&g_myApps[hitAppIndex].Position);
                        g_dragDistance = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(aPos, cPos)));
                        if (g_dragDistance < 3.0f) g_dragDistance = 8.0f;
                    }
                }
            } else {
                for (auto& a : g_myApps) {
                    if (!(g_ctrlHeld)) a.IsSelected = false;
                    a.WasSelected = a.IsSelected;
                }
                if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
                    g_isBlankDragging = true;
                    
                    DirectX::XMFLOAT2 startAngle = GetYawPitch(rayDir);
                    g_dragStartYaw = startAngle.x;
                    g_dragStartPitch = startAngle.y;
                    g_dragCurrYaw = g_dragStartYaw;
                    g_dragCurrPitch = g_dragStartPitch;
                    g_dragPrevRawYaw = startAngle.x;
                }
            }
        }

        if (g_rightClicked) {
            g_rightClicked = false;
            if (hitAppIndex >= 0) {
                if (!g_myApps[hitAppIndex].IsSelected) {
                    if (!g_ctrlHeld) {
                        for (auto& a : g_myApps) a.IsSelected = false;
                    }
                    g_myApps[hitAppIndex].IsSelected = true;
                }
                QueueShellItemMenu(SelectedItemPaths());
            } else {
                QueueShellBackgroundMenu(CurrentShellFolder());
            }
        }

        if (g_selectAllRequested) {
            g_selectAllRequested = false;
            for (auto& a : g_myApps) a.IsSelected = true;
        }
        if (g_copyRequested) {
            g_copyRequested = false;
            FileOps::CopyToClipboard(SelectedItemPaths(), false);
        }
        if (g_cutRequested) {
            g_cutRequested = false;
            FileOps::CopyToClipboard(SelectedItemPaths(), true);
        }
        if (g_pasteRequested) {
            g_pasteRequested = false;
            if (FileOps::PasteFromClipboard(CurrentShellFolder())) g_needRescan = true;
        }
        if (g_newFolderRequested) {
            g_newFolderRequested = false;
            if (FileOps::CreateNewFolder(CurrentShellFolder())) g_needRescan = true;
        }
        if (g_renameRequested) {
            g_renameRequested = false;
            int idx = -1; int nsel = 0;
            for (int i = 0; i < (int)g_myApps.size(); ++i) if (g_myApps[i].IsSelected) { idx = i; nsel++; }
            if (nsel == 1 && idx >= 0) {
                g_renameIndex = idx;
                snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", g_myApps[idx].AppName.c_str());
            }
        }

        if (g_deleteRequested) {
            g_deleteRequested = false;
            auto paths = SelectedItemPaths();
            if (!paths.empty()) {
                FileOps::RecyclePathsAsync(paths);
                auto& tombs = g_desktopRemovedPaths[ActiveDesktopId()];
                for (const auto& p : paths) {
                    if (std::find(tombs.begin(), tombs.end(), p) == tombs.end()) tombs.push_back(p);
                }
                g_myApps.erase(std::remove_if(g_myApps.begin(), g_myApps.end(), [](const AppCube& c) {
                    return c.IsSelected;
                }), g_myApps.end());
                if (g_isInFolder && !g_isFolderMaximized) {
                    g_folderCubes.erase(std::remove_if(g_folderCubes.begin(), g_folderCubes.end(), [](const AppCube& c) {
                        return c.IsSelected;
                    }), g_folderCubes.end());
                }
                SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
            }
        }

        if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked && (g_lButtonHeld)) {
            if (g_grabbedAppIndex != -1) {
                if (GetTickCount() - g_mouseDownTime > 150) {
                    g_isDragging = true;
                    
                    DirectX::XMVECTOR newPosVec = DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(rayDir, g_dragDistance));
                    DirectX::XMFLOAT3 targetPos; DirectX::XMStoreFloat3(&targetPos, newPosVec);
                    
                    DirectX::XMFLOAT3 delta = { 
                        targetPos.x - g_myApps[g_grabbedAppIndex].Position.x,
                        targetPos.y - g_myApps[g_grabbedAppIndex].Position.y,
                        targetPos.z - g_myApps[g_grabbedAppIndex].Position.z 
                    };

                    constexpr float COL_RADIUS = 0.40f;
                    constexpr float COL_DIAM = COL_RADIUS * 2.0f;
                    constexpr float REPEL_DIST = 1.2f;
                    constexpr float REPEL_FORCE = 0.10f;

                    for (int i = 0; i < (int)g_myApps.size(); ++i) {
                        if (!g_myApps[i].IsSelected) continue;

                        DirectX::XMFLOAT3 proposed = {
                            g_myApps[i].Position.x + delta.x,
                            g_myApps[i].Position.y + delta.y,
                            g_myApps[i].Position.z + delta.z
                        };

                        for (int j = 0; j < (int)g_myApps.size(); ++j) {
                            if (g_myApps[j].IsSelected) continue;

                            float dx = proposed.x - g_myApps[j].Position.x;
                            float dy = proposed.y - g_myApps[j].Position.y;
                            float dz = proposed.z - g_myApps[j].Position.z;
                            float dist = sqrtf(dx*dx + dy*dy + dz*dz);

                            if (dist < COL_DIAM && dist > 0.001f) {
                                float push = COL_DIAM - dist;
                                proposed.x += (dx / dist) * push;
                                proposed.y += (dy / dist) * push;
                                proposed.z += (dz / dist) * push;
                            }

                            if (dist < REPEL_DIST && dist > 0.001f) {
                                float force = (REPEL_DIST - dist) / REPEL_DIST * REPEL_FORCE;
                                g_myApps[j].Position.x -= (dx / dist) * force;
                                g_myApps[j].Position.y -= (dy / dist) * force;
                                g_myApps[j].Position.z -= (dz / dist) * force;
                            }
                        }

                        g_myApps[i].Position = proposed;
                    }

                    // Drop-target detection: find closest non-selected cube to the dragged cube
                    g_dropTargetIndex = -1;
                    float bestDist = 1.5f;
                    for (int k = 0; k < (int)g_myApps.size(); ++k) {
                        if (g_myApps[k].IsSelected) continue;
                        float dx = g_myApps[g_grabbedAppIndex].Position.x - g_myApps[k].Position.x;
                        float dy = g_myApps[g_grabbedAppIndex].Position.y - g_myApps[k].Position.y;
                        float dz = g_myApps[g_grabbedAppIndex].Position.z - g_myApps[k].Position.z;
                        float d = sqrtf(dx*dx + dy*dy + dz*dz);
                        if (d < bestDist) { bestDist = d; g_dropTargetIndex = k; }
                    }
                }
            } else if (g_isBlankDragging) {
                DirectX::XMFLOAT2 rawAngle = GetYawPitch(rayDir);
                float deltaYaw = rawAngle.x - g_dragPrevRawYaw;
                
                if (deltaYaw > DirectX::XM_PI) deltaYaw -= DirectX::XM_2PI;
                if (deltaYaw < -DirectX::XM_PI) deltaYaw += DirectX::XM_2PI;
                
                g_dragCurrYaw += deltaYaw;
                g_dragCurrPitch = rawAngle.y;
                g_dragPrevRawYaw = rawAngle.x;
                
                float centerYaw = (g_dragStartYaw + g_dragCurrYaw) / 2.0f;
                float extYaw = abs(g_dragStartYaw - g_dragCurrYaw) / 2.0f;
                float centerPitch = (g_dragStartPitch + g_dragCurrPitch) / 2.0f;
                float extPitch = abs(g_dragStartPitch - g_dragCurrPitch) / 2.0f;
                
                for (auto& a : g_myApps) {
                    DirectX::XMVECTOR appDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&a.Position), rayOrigin));
                    DirectX::XMFLOAT2 appAngle = GetYawPitch(appDir);
                    
                    float dy = appAngle.x - centerYaw;
                    while (dy > DirectX::XM_PI) dy -= DirectX::XM_2PI;
                    while (dy < -DirectX::XM_PI) dy += DirectX::XM_2PI;
                    
                    bool inside = (abs(dy) <= extYaw && abs(appAngle.y - centerPitch) <= extPitch);
                    
                    if (g_ctrlHeld) a.IsSelected = (a.WasSelected || inside);
                    else a.IsSelected = inside;
                }
            }
        } else {
            // Drop-target: folder <- move; exe <- open with
            if (g_isDragging && g_dropTargetIndex >= 0 && g_dropTargetIndex < (int)g_myApps.size() &&
                g_grabbedAppIndex >= 0 && g_grabbedAppIndex < (int)g_myApps.size()) {
                const auto& target = g_myApps[g_dropTargetIndex];
                std::vector<std::wstring> srcPaths;
                for (const auto& c : g_myApps) {
                    if (c.IsSelected && !c.AppPath.empty() && c.AppPath != target.AppPath)
                        srcPaths.push_back(c.AppPath);
                }
                std::wstring destFolder = CubeFolderPath(target);
                if (!destFolder.empty() && !srcPaths.empty()) {
                    LOGW(L"[drop] Move %d item(s) -> %s", (int)srcPaths.size(), destFolder.c_str());
                    FileOps::MovePathsAsync(srcPaths, destFolder);
                } else if (PathLooksLikeExe(CubeLaunchPath(target)) && !srcPaths.empty()) {
                    HINSTANCE res = ShellExecuteW(NULL, L"open", CubeLaunchPath(target).c_str(),
                                                   srcPaths[0].c_str(), NULL, SW_SHOWNORMAL);
                    if ((INT_PTR)res > 32) {
                        LOGW(L"[drop] Open with: %s -> %s", srcPaths[0].c_str(), CubeLaunchPath(target).c_str());
                    }
                }
                g_dropTargetIndex = -1;
            }
            bool wasDragging = g_isDragging || g_isBlankDragging;
            g_grabbedAppIndex = -1; g_isDragging = false; g_isBlankDragging = false;
            g_dropTargetIndex = -1;
            // g_myApps is the desktop unless we're inside a maximized folder
            if (wasDragging && !(g_isInFolder && g_isFolderMaximized))
                SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
        }
        } // !mouseInPortal

        // Virtual desktop transition
        if (g_targetDesktopIndex >= 0) {
            float prev = g_desktopTransition;
            g_desktopTransition += g_desktopTransitionSpeed * dt;
            if (prev < 0.5f && g_desktopTransition >= 0.5f) {
                CompleteDesktopSwitch();
            }
            if (g_desktopTransition >= 1.0f) {
                g_desktopTransition = 1.0f;
            }
        }
        float desktopAlpha = 1.0f;
        if (g_targetDesktopIndex >= 0) {
            float t = g_desktopTransition;
            desktopAlpha = (t < 0.5f) ? (1.0f - t * 2.0f) : ((t - 0.5f) * 2.0f);
        }

        ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
        
        bool is2DMode = (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked);
        for (int appIdx = 0; appIdx < (int)g_myApps.size(); ++appIdx) {
            auto& app = g_myApps[appIdx];
            int hoverState = 0;
            if (g_isDragging && appIdx == g_dropTargetIndex) {
                hoverState = 3; // full highlight for drop target
            } else if (is2DMode) {
                hoverState = app.IsHovered ? 1 : 0;
            } else {
                hoverState = app.IsSelected ? (app.IsHovered ? 3 : 2) : (app.IsHovered ? 1 : 0);
            }
            DirectX::XMFLOAT3 appScale = {1.2f, 1.2f, 0.6f}; 
            DirectX::XMFLOAT4 drawColor = app.BaseColor;
            if (g_searchFilter[0]) {
                if (!SearchMatch(g_searchFilter, app.AppName, app.AppPath)) {
                    drawColor = DirectX::XMFLOAT4(app.BaseColor.x * 0.15f, app.BaseColor.y * 0.15f, app.BaseColor.z * 0.15f, 0.3f);
                }
            }
            drawColor.w *= desktopAlpha;
            float spinAngle = 0.0f;
            float orbitAngle = 0.0f;
            float tiltAngle = 0.0f;
            if (is2DMode && app.IsSelected) {
                spinAngle = appSpinTime * 2.6f;
                orbitAngle = appSpinTime * 0.8f;
                tiltAngle = 0.55f;
            }
            cubeRenderer.Render(g_pd3dDeviceContext, viewMatrix * projMatrix, app.Position, appScale, drawColor, app.IconTexture, camera.Position, hoverState, viewMatrix, 8.0f, spinAngle, orbitAngle, tiltAngle);
            
            // 🚨 3D 全息投影到 2D 屏幕文字
            DirectX::XMVECTOR appPos = DirectX::XMLoadFloat3(&app.Position);
            appPos = DirectX::XMVectorSubtract(appPos, DirectX::XMVectorSet(0.0f, 0.75f, 0.0f, 0.0f)); 
            DirectX::XMVECTOR clipSpace = DirectX::XMVector3TransformCoord(appPos, viewMatrix * projMatrix);
            
            if (DirectX::XMVectorGetZ(clipSpace) > 0.0f && DirectX::XMVectorGetZ(clipSpace) < 1.0f) {
                float screenX = (DirectX::XMVectorGetX(clipSpace) + 1.0f) * 0.5f * width;
                float screenY = (1.0f - DirectX::XMVectorGetY(clipSpace)) * 0.5f * height;
                if (screenX < -80.0f || screenX > width + 80.0f || screenY < -40.0f || screenY > height + 40.0f)
                    continue;

                // Skip labels inside portal window (they belong to the folder's world)
                if (g_isInFolder && !g_isFolderMaximized) {
                    RECT& fwr = g_folderWindowRect;
                    if (screenX >= fwr.left && screenX <= fwr.right && screenY >= fwr.top && screenY <= fwr.bottom) continue;
                }
                
                ImVec2 textSize = ImGui::CalcTextSize(app.AppName.c_str());
                ImVec2 textPos = ImVec2(screenX - textSize.x * 0.5f, screenY - textSize.y * 0.5f);
                
                // 只有被选中或准星指着时才高亮文字，平时是暗灰色
                ImU32 textColor = app.IsSelected ? IM_COL32(0, 200, 255, 255) : 
                                 (app.IsHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 150));
                
                // 画黑色阴影 + 本体字
                bg_draw_list->AddText(ImVec2(textPos.x + 2, textPos.y + 2), IM_COL32(0, 0, 0, 200), app.AppName.c_str());
                bg_draw_list->AddText(textPos, textColor, app.AppName.c_str());

                // Drop-target indicator: "Open with <source>"
                if (g_isDragging && appIdx == g_dropTargetIndex && g_grabbedAppIndex >= 0 && g_grabbedAppIndex < (int)g_myApps.size()) {
                    char openWithText[256];
                    if (CubeIsDirectory(app))
                        sprintf_s(openWithText, "Move to %s", app.AppName.c_str());
                    else
                        sprintf_s(openWithText, "Open with %s", app.AppName.c_str());
                    ImVec2 owSize = ImGui::CalcTextSize(openWithText);
                    float owX = screenX - owSize.x * 0.5f;
                    float owY = screenY - textSize.y - owSize.y - 4.0f;
                    bg_draw_list->AddRectFilled(ImVec2(owX - 4, owY - 2), ImVec2(owX + owSize.x + 4, owY + owSize.y + 2), IM_COL32(0, 0, 0, 160), 4.0f);
                    bg_draw_list->AddText(ImVec2(owX + 1, owY + 1), IM_COL32(0, 0, 0, 200), openWithText);
                    bg_draw_list->AddText(ImVec2(owX, owY), IM_COL32(0, 220, 255, 255), openWithText);
                }
            }
        }

        if (g_isBlankDragging) {
            float centerYaw = (g_dragStartYaw + g_dragCurrYaw) / 2.0f;
            float extYaw = abs(g_dragStartYaw - g_dragCurrYaw) / 2.0f;
            float centerPitch = (g_dragStartPitch + g_dragCurrPitch) / 2.0f;
            float extPitch = abs(g_dragStartPitch - g_dragCurrPitch) / 2.0f;

            float shaderCenterYaw = centerYaw;
            while (shaderCenterYaw > DirectX::XM_PI) shaderCenterYaw -= DirectX::XM_2PI;
            while (shaderCenterYaw < -DirectX::XM_PI) shaderCenterYaw += DirectX::XM_2PI;

            
            float backScale = 0.92;

            DirectX::XMFLOAT4 anglesFront = { shaderCenterYaw, extYaw, centerPitch, extPitch }; 
            DirectX::XMFLOAT4 anglesBack  = { shaderCenterYaw, extYaw * backScale, centerPitch, extPitch * backScale }; 
            DirectX::XMFLOAT3 canvasScale = { -100.0f, -100.0f, -100.0f }; 

            cubeRenderer.Render(g_pd3dDeviceContext, viewMatrix * projMatrix, camera.Position, canvasScale, anglesBack, nullptr, camera.Position, 5, viewMatrix, 8.4f);

            float yawsF[2] = { shaderCenterYaw - extYaw, shaderCenterYaw + extYaw };
            float pitchesF[2] = { centerPitch - extPitch, centerPitch + extPitch };
            
            float yawsB[2] = { shaderCenterYaw - extYaw * backScale, shaderCenterYaw + extYaw * backScale };
            float pitchesB[2] = { centerPitch - extPitch * backScale, centerPitch + extPitch * backScale };
            
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    float dxf = cos(pitchesF[j]) * sin(yawsF[i]);
                    float dyf = sin(pitchesF[j]);
                    float dzf = cos(pitchesF[j]) * cos(yawsF[i]);
                    DirectX::XMVECTOR pFront = DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(DirectX::XMVectorSet(dxf, dyf, dzf, 0), 7.6f));
                    float dxb = cos(pitchesB[j]) * sin(yawsB[i]);
                    float dyb = sin(pitchesB[j]);
                    float dzb = cos(pitchesB[j]) * cos(yawsB[i]);
                    DirectX::XMVECTOR pBack = DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(DirectX::XMVectorSet(dxb, dyb, dzb, 0), 8.4f));
                    DirectX::XMVECTOR pMid = DirectX::XMVectorScale(DirectX::XMVectorAdd(pFront, pBack), 0.5f);
                    DirectX::XMFLOAT3 midPos; DirectX::XMStoreFloat3(&midPos, pMid);
                    DirectX::XMVECTOR dirVec = DirectX::XMVectorSubtract(pBack, pFront);
                    float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(dirVec));
                    dirVec = DirectX::XMVector3Normalize(dirVec);
                    DirectX::XMFLOAT2 dirAngles = GetYawPitch(dirVec);
                
                    DirectX::XMFLOAT3 edgeScale = { 0.015f, 0.015f, length }; 
                    DirectX::XMFLOAT4 edgeRot = { dirAngles.x, dirAngles.y, 0.0f, 0.0f };
                    
                    cubeRenderer.Render(g_pd3dDeviceContext, viewMatrix * projMatrix, midPos, edgeScale, edgeRot, nullptr, camera.Position, 6, viewMatrix, 8.0f);
                }
            }
            cubeRenderer.Render(g_pd3dDeviceContext, viewMatrix * projMatrix, camera.Position, canvasScale, anglesFront, nullptr, camera.Position, 5, viewMatrix, 7.6f);
        }

        // Alt+Tab window switcher overlay
        if (g_showWindowSwitcher && !g_switcherWindows.empty()) {
            ImDrawList* swDraw = ImGui::GetForegroundDrawList();
            ImVec2 swP = ImGui::GetMainViewport()->WorkPos;
            ImVec2 swE = ImVec2(swP.x + ImGui::GetMainViewport()->WorkSize.x, swP.y + ImGui::GetMainViewport()->WorkSize.y);
            swDraw->AddRectFilled(swP, swE, IM_COL32(8, 12, 28, 235));

            int n = (int)g_switcherWindows.size();
            // Arc layout: cards arranged in a horizontal curve in front of camera
            float cardW = 1.4f, cardH = 1.0f, cardD = 0.03f;
            float arcRadius = 5.0f;
            float totalArc = DirectX::XMConvertToRadians(60.0f);
            float startYaw = -totalArc * 0.5f;
            DirectX::XMFLOAT3 camPos = camera.Position;
            DirectX::XMFLOAT3 camFwd = camera.GetForward();

            for (int i = 0; i < n; ++i) {
                bool sel = (i == g_switcherSelected);
                float t = (n > 1) ? (float)i / (n - 1) : 0.5f;
                float yaw = startYaw + totalArc * t;
                // Position in world space relative to camera
                float cx = camPos.x + camFwd.x * arcRadius - sinf(yaw) * (n > 1 ? (i - n/2) * 1.8f : 0.0f);
                float cy = camPos.y + 1.0f;
                float cz = camPos.z + camFwd.z * arcRadius;
                DirectX::XMFLOAT3 cardPos = { cx + sinf(yaw) * 3.0f, cy, cz };
                DirectX::XMFLOAT3 cardScl = sel ? DirectX::XMFLOAT3{cardW*1.15f, cardH*1.15f, cardD} : DirectX::XMFLOAT3{cardW, cardH, cardD};
                DirectX::XMFLOAT4 cardCol = sel ? DirectX::XMFLOAT4{0.25f, 0.3f, 0.45f, 1.0f} : DirectX::XMFLOAT4{0.12f, 0.14f, 0.22f, 0.7f};
                float cardSpin = yaw * 0.5f;

                ID3D11ShaderResourceView* icon = GetWindowIconTexture(g_pd3dDevice, g_switcherWindows[i].first, g_switcherWindows[i].second);
                cubeRenderer.Render(g_pd3dDeviceContext, viewMatrix * projMatrix, cardPos, cardScl, cardCol, icon,
                                    camPos, sel ? 3 : 0, viewMatrix, arcRadius, cardSpin);
                // Card title label
                DirectX::XMVECTOR cp = DirectX::XMLoadFloat3(&cardPos);
                cp = DirectX::XMVectorSubtract(cp, DirectX::XMVectorSet(0.0f, cardH * 0.6f, 0.0f, 0.0f));
                DirectX::XMVECTOR cs = DirectX::XMVector3TransformCoord(cp, viewMatrix * projMatrix);
                if (DirectX::XMVectorGetZ(cs) > 0.0f && DirectX::XMVectorGetZ(cs) < 1.0f) {
                    float sx = (DirectX::XMVectorGetX(cs) + 1.0f) * 0.5f * width;
                    float sy = (1.0f - DirectX::XMVectorGetY(cs)) * 0.5f * height;
                    WCHAR wt[256]; GetWindowTextW(g_switcherWindows[i].first, wt, 256);
                    char ct[256]; WideCharToMultiByte(CP_UTF8, 0, wt, -1, ct, 256, nullptr, nullptr);
                    if (ct[0] == 0) strcpy_s(ct, "(untitled)");
                    ImVec2 ts = ImGui::CalcTextSize(ct);
                    ImU32 tc = sel ? IM_COL32(180, 210, 255, 255) : IM_COL32(120, 130, 150, 180);
                    swDraw->AddText(ImVec2(sx - ts.x * 0.5f, sy), tc, ct);
                }
            }
            // Hint text
            swDraw->AddText(ImVec2(swP.x + 30, swP.y + 30), IM_COL32(150, 160, 180, 200),
                            "Tab = next  |  Release Ctrl = switch  |  Esc = cancel");
        }

        // Folder portal rendering (windowed mode)
        if (g_isInFolder && !g_isFolderMaximized && !g_folderCubes.empty()) {
            RECT wr = g_folderWindowRect;
            D3D11_RECT scissor = { (LONG)wr.left, (LONG)wr.top, (LONG)wr.right, (LONG)wr.bottom };
            g_pd3dDeviceContext->RSSetScissorRects(1, &scissor);
            D3D11_VIEWPORT savedVp, folderVp;
            UINT vpCount = 1;
            g_pd3dDeviceContext->RSGetViewports(&vpCount, &savedVp);
            folderVp.TopLeftX = (float)wr.left; folderVp.TopLeftY = (float)wr.top;
            folderVp.Width = (float)(wr.right - wr.left); folderVp.Height = (float)(wr.bottom - wr.top);
            folderVp.MinDepth = 0.0f; folderVp.MaxDepth = 1.0f;
            g_pd3dDeviceContext->RSSetViewports(1, &folderVp);
            float portalAspect = folderVp.Width / (folderVp.Height > 0 ? folderVp.Height : 1.0f);
            DirectX::XMMATRIX folderView = g_folderCamera.GetViewMatrix();
            DirectX::XMMATRIX folderProj = g_folderCamera.GetProjectionMatrix(90.0f, portalAspect, 0.1f, 100.0f);
            // Light gray background plane behind folder cubes
            DirectX::XMFLOAT3 fw = g_folderCamera.GetForward();
            DirectX::XMFLOAT3 bgPlanePos = { g_folderCamera.Position.x + fw.x * 30.0f,
                                              g_folderCamera.Position.y + fw.y * 30.0f,
                                              g_folderCamera.Position.z + fw.z * 30.0f };
            DirectX::XMFLOAT3 bgPlaneScl = { 60.0f, 40.0f, 0.01f };
            DirectX::XMFLOAT4 bgPlaneClr = { 0.72f, 0.73f, 0.78f, 1.0f };
            cubeRenderer.Render(g_pd3dDeviceContext, folderView * folderProj, bgPlanePos, bgPlaneScl, bgPlaneClr,
                                nullptr, g_folderCamera.Position, 0, folderView, 8.0f);
            for (auto& app : g_folderCubes) {
                int fhState = app.IsSelected ? (app.IsHovered ? 3 : 2) : (app.IsHovered ? 1 : 0);
                DirectX::XMFLOAT3 fScale = { 1.0f, 1.0f, 0.5f };
                DirectX::XMFLOAT4 fColor = app.BaseColor;
                cubeRenderer.Render(g_pd3dDeviceContext, folderView * folderProj, app.Position, fScale, fColor,
                                    app.IconTexture, g_folderCamera.Position, fhState, folderView, 8.0f);
            }
            g_pd3dDeviceContext->RSSetViewports(1, &savedVp);
            D3D11_RECT fullScissor = { 0, 0, (LONG)ImGui::GetMainViewport()->WorkSize.x, (LONG)ImGui::GetMainViewport()->WorkSize.y };
            g_pd3dDeviceContext->RSSetScissorRects(1, &fullScissor);
        }

        if (g_renameIndex >= 0 && g_renameIndex < (int)g_myApps.size()) {
            ImGui::OpenPopup("RenameCube");
            if (ImGui::BeginPopupModal("RenameCube", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Rename");
                ImGui::SetKeyboardFocusHere(0);
                bool committed = ImGui::InputText("##rename", g_renameBuf, sizeof(g_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                if (committed || ImGui::Button("OK")) {
                    int idx = g_renameIndex;
                    std::wstring parent = FileOps::ParentOf(g_myApps[idx].AppPath);
                    std::wstring ext;
                    size_t dot = g_myApps[idx].AppPath.rfind(L'.');
                    size_t sl = g_myApps[idx].AppPath.find_last_of(L"\\/");
                    if (dot != std::wstring::npos && (sl == std::wstring::npos || dot > sl) && !CubeIsDirectory(g_myApps[idx]))
                        ext = g_myApps[idx].AppPath.substr(dot);
                    std::wstring dest = parent + L"\\" + Utf8ToWcs(g_renameBuf) + ext;
                    if (FileOps::RenamePath(g_myApps[idx].AppPath, dest)) g_needRescan = true;
                    g_renameIndex = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { g_renameIndex = -1; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }


        // AI Control Interface: process pending HTTP requests (main thread)
        static bool aiActionShotPending = false; // deferred action-attached screenshot
        {
            // Publish state snapshot (throttled to ~10 Hz)
            static LARGE_INTEGER s_aiFreq = {};
            static LARGE_INTEGER s_aiLast = {};
            if (s_aiFreq.QuadPart == 0) {
                QueryPerformanceFrequency(&s_aiFreq);
                QueryPerformanceCounter(&s_aiLast);
            }
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            if ((now.QuadPart - s_aiLast.QuadPart) * 1000 / s_aiFreq.QuadPart > 100) {
                s_aiLast = now;
                BuildStateSnapshot(camera);
            }

            // Handle action requests
            if (AIServer::g_actionPending.load()) {
                std::string actionJson;
                {
                    std::lock_guard<std::mutex> lock(AIServer::g_reqMutex);
                    actionJson = AIServer::g_pendingAction;
                    AIServer::g_actionPending.store(false);
                }
                if (!actionJson.empty()) {
                    LOG("[ai] action: %s", actionJson.c_str());
                    ExecuteAIAction(actionJson, camera);
                    // Optional: auto-attach screenshot to action result (deferred
                    // until after ImGui render so the 2D UI layer is included)
                    std::string wantShot = Json::GetField(actionJson, "screenshot");
                    if (wantShot == "1" || wantShot == "true") {
                        aiActionShotPending = true;
                    }
                }
                if (!aiActionShotPending)
                    AIServer::g_actionResultReady.store(true);
            }
        }

        g_leftClicked = false;
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // AI screenshots: capture after ImGui render (2D UI visible), before Present
        // (back buffer still valid).
        if (aiActionShotPending) {
            aiActionShotPending = false;
            AIServer::g_screenshotRawMode.store(false);
            CaptureScreenshot();
            std::string shotData;
            {
                std::lock_guard<std::mutex> lock(AIServer::g_reqMutex);
                shotData = AIServer::g_screenshotResult;
                AIServer::g_screenshotResult.clear();
                AIServer::g_screenshotReady.store(false);
            }
            if (!shotData.empty() && AIServer::g_actionResult.size() > 2) {
                AIServer::g_actionResult.pop_back(); // drop final '}'
                AIServer::g_actionResult += ",\"screenshot\":" + shotData + "}";
            }
            AIServer::g_actionResultReady.store(true);
        }
        if (AIServer::g_screenshotRequested.load()) {
            AIServer::g_screenshotRequested.store(false);
            CaptureScreenshot();
        }

        {
            bool focused = (GetForegroundWindow() == hwnd);
            UINT vsync = focused ? 1 : 0;
            HRESULT hrPresent = g_pSwapChain->Present(vsync, 0);
            if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET) {
                HRESULT reason = g_pd3dDevice ? g_pd3dDevice->GetDeviceRemovedReason() : hrPresent;
                LOG("[d3d] FATAL: Device removed/reset hr=0x%08lx reason=0x%08lx. Initiating recovery shutdown.", hrPresent, reason);
                PostQuitMessage(1);
            } else if (hrPresent == DXGI_STATUS_OCCLUDED || !focused) {
                if (g_hHeartbeatEvent) SetEvent(g_hHeartbeatEvent);
            }
        }

        if (g_shellMenuPending) {
            g_shellMenuPending = false;
            ClipCursor(NULL);
            while (ShowCursor(TRUE) < 0);
            bool invoked = false;
            if (g_shellMenuBackground)
                invoked = FileOps::ShowBackgroundMenu(g_mainHwnd, g_shellMenuFolder, g_shellMenuPt);
            else
                invoked = FileOps::ShowItemMenu(g_mainHwnd, g_shellMenuPaths, g_shellMenuPt);
            if (invoked) g_needRescan = true;
            if (!g_uiUnlocked && g_currentState == STATE_3D_EXPLORE) {
                while (ShowCursor(FALSE) >= 0);
                RECT rect; GetClientRect(g_mainHwnd, &rect);
                MapWindowPoints(g_mainHwnd, nullptr, (POINT*)&rect, 2);
                ClipCursor(&rect);
            }
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ShutdownAudioEndpointVolume();
    if (comUninit) {
        CoUninitialize();
    }
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    } else if (g_tabHotkeyRegistered) {
        UnregisterHotKey(hwnd, 1);
        UnregisterHotKey(hwnd, 5);
    }
    UnregisterHotKey(hwnd, 2);
    UnregisterHotKey(hwnd, 3);
    UnregisterHotKey(hwnd, 4);
    KillTimer(hwnd, IDT_HEARTBEAT);
    AIServer::Stop();
    if (g_hShutdownEvent) { SetEvent(g_hShutdownEvent); CloseHandle(g_hShutdownEvent); g_hShutdownEvent = nullptr; }
    if (g_hHeartbeatEvent) { CloseHandle(g_hHeartbeatEvent); g_hHeartbeatEvent = nullptr; }
    // Restore every hijacked/hidden window before leaving (reversibility, rule #5)
    RestoreAllHijackedWindows();
    SetSystemTaskbarVisible(true);
    ShutdownDesktopWatchers();
    // Persist the DESKTOP cube layout. In a maximized folder g_myApps holds the
    // folder's content and the real desktop lives in g_desktopBackupCubes;
    // in every other mode g_myApps is the desktop.
    if (g_isInFolder && g_isFolderMaximized) {
        SaveDesktopState(g_desktopBackupCubes, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
    } else {
        SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
    }
    SaveVirtualDesktopsConfig();
    TextureLoader::ShutdownIconCache();
    ClearWindowIconCache();
    CleanupDevice();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}