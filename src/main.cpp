#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d3d11.h>
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
enum CrossDimState {
    STATE_3D_EXPLORE,
    STATE_2D_WORKBENCH
};
CrossDimState g_currentState = STATE_3D_EXPLORE;
bool g_uiUnlocked = false;
bool g_previousUiUnlocked = false;

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "d3d11.lib")
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

bool  g_isBlankDragging = false;
float g_dragStartYaw = 0.0f;
float g_dragStartPitch = 0.0f;
float g_dragCurrYaw = 0.0f;
float g_dragCurrPitch = 0.0f;
float g_dragPrevRawYaw = 0.0f;

float g_fpsCurrent = 0.0f;
char g_searchFilter[64] = "";
float g_dragDistance = 8.0f;

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

static void LaunchAppByPath(LPCWSTR appPath) {
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
        }
    } else {
        HINSTANCE result = ShellExecuteW(NULL, L"open", appPath, NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)result <= 32) {
            LOGW(L"[desk] ShellExecute failed (%d) for: %s", (int)(INT_PTR)result, appPath);
        }
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool InitDevice(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 4; sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel; const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK) return false;

    ID3D11Texture2D* pBackBuffer; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView); pBackBuffer->Release();
    RECT rect; GetClientRect(hWnd, &rect);
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = rect.right - rect.left;
    depthDesc.Height = rect.bottom - rect.top;
    depthDesc.MipLevels = 1; depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 4; depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    
    ID3D11Texture2D* pDepthStencil;
    g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
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
    
    ID3D11BlendState* blendState;
    g_pd3dDevice->CreateBlendState(&blendDesc, &blendState);
    g_pd3dDeviceContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
    blendState->Release();

    return true;
}

void CleanupDevice() {
    if (g_mainDepthStencilView) { g_mainDepthStencilView->Release(); g_mainDepthStencilView = nullptr; }
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto toggleUiUnlock = [&](HWND hwnd) {
        g_uiUnlocked = !g_uiUnlocked;
        LOG("Tab: toggle uiUnlocked -> %d (state=%s, hij=%zu, pend=%zu)",
            g_uiUnlocked,
            (g_currentState == STATE_2D_WORKBENCH) ? "2D" : "3D",
            g_hijackedWindows.size(), g_pendingHijacks.size());
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
                        float btnSize = 14.0f;
                        float btnGap = 8.0f;
                        float btnPad = 10.0f;
                        float ctrlWidth = btnSize * 3.0f + btnGap * 2.0f;
                        RECT btnRect = { (LONG)(r.right - (LONG)(btnPad + ctrlWidth)), r.top, (LONG)(r.right - btnPad), (LONG)(r.top + barHeight) };
                        if (PtInRect(&btnRect, pt)) return HTCLIENT;
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
                std::string label;
                for (wchar_t ch : nameOnly) label += (char)ch;

                AppCube cube = {};
                cube.Position = DirectX::XMFLOAT3(0.0f, 1.5f, 8.0f);
                cube.BaseColor = DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
                cube.AppPath = fpath;
                cube.AppName = label;
                cube.IconTexture = TextureLoader::LoadIconFromExe(g_pd3dDevice, fpath.c_str());
                if (!cube.IconTexture) {
                    SHFILEINFOW sfi = {};
                    if (SHGetFileInfoW(fpath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
                        if (sfi.hIcon) { cube.IconTexture = TextureLoader::LoadIconFromHandle(g_pd3dDevice, sfi.hIcon); DestroyIcon(sfi.hIcon); }
                    }
                }
                g_myApps.push_back(cube);
                LOGW(L"[drop] Added: %s", fpath.c_str());
            }
            DragFinish(hDrop);
            break;
        }
        case WM_LBUTTONDOWN: {
            g_lButtonHeld = true;
            if (g_currentState == STATE_3D_EXPLORE) {
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
                            g_leftClicked = true;
                        }
                    } else {
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
        case WM_KEYDOWN:
            if (wParam == VK_CONTROL) g_ctrlHeld = true;
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            if (wParam == VK_DELETE) g_deleteRequested = true;
            if (g_ctrlHeld && (GetKeyState(VK_MENU) & 0x8000)) {
                RECT workArea;
                workArea.left = 0;
                workArea.top = 0;
                workArea.right = GetSystemMetrics(SM_CXSCREEN);
                workArea.bottom = g_taskbarRectValid ? g_taskbarRect.top : GetSystemMetrics(SM_CYSCREEN);
                int snapAction = -1;
                if (wParam == VK_LEFT)  snapAction = 0;
                if (wParam == VK_RIGHT) snapAction = 1;
                if (wParam == VK_UP)    snapAction = 2;
                if (wParam == VK_DOWN) {
                    HWND fg = GetForegroundWindow();
                    for (const auto& w : g_hijackedWindows) {
                        if (w.hwnd == fg && IsZoomed(fg)) { ShowWindow(fg, SW_RESTORE); snapAction = -1; break; }
                    }
                    if (snapAction == -1) return 0;
                }
                if (snapAction >= 0) {
                    HWND fg = GetForegroundWindow();
                    for (const auto& w : g_hijackedWindows) {
                        if (w.hwnd == fg) {
                            RECT r;
                            if (snapAction == 2) { r = workArea; }
                            else if (snapAction == 0) { r = workArea; r.right = workArea.left + (workArea.right - workArea.left) / 2; }
                            else { r = workArea; r.left = workArea.left + (workArea.right - workArea.left) / 2; }
                            if (IsZoomed(fg)) ShowWindow(fg, SW_RESTORE);
                            SetWindowPos(fg, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
                            break;
                        }
                    }
                }
                return 0;
            }
            return 0;

        case WM_KEYUP:
            if (wParam == VK_CONTROL) g_ctrlHeld = false;
            return 0;

        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
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
    g_tabHotkeyRegistered = (RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_TAB) != 0);
    RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_ESCAPE);
    RegisterHotKey(hwnd, 3, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D');
    Logger::Instance().Init();
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

    if (!InitDevice(hwnd)) { CleanupDevice(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

    g_hHeartbeatEvent = CreateEventW(nullptr, FALSE, FALSE, L"Global\\CrossDim_Heartbeat");
    g_hShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, L"Global\\CrossDim_Shutdown");
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

    ShowWindow(hwnd, nCmdShow); UpdateWindow(hwnd);
    ShowCursor(FALSE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // 🚨 尝试加载 Windows 微软雅黑字体，支持中文渲染
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 20.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    } else {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    Camera camera;
    CubeRenderer cubeRenderer;
    SkyboxRenderer skybox;
    ModelRenderer modelRenderer;
    
    if (!cubeRenderer.Initialize(g_pd3dDevice)) OutputDebugStringW(L"Cube initialization failed!\n");
    if (!skybox.Initialize(g_pd3dDevice)) OutputDebugStringW(L"Skybox initialization failed!\n");
    if (!modelRenderer.Initialize(g_pd3dDevice)) OutputDebugStringW(L"ModelRenderer initialization failed!\n");

    modelRenderer.LoadModelAsync("assets/bloom_high/bloom_high.obj");
    
    ScanDesktopForApps(g_myApps);
    LoadDesktopState(g_myApps);
    LoadIconsForApps(g_myApps);


    MSG msg; bool done = false;
        while (!done) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        {
            static int s_pruneFrames = 0;
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
                if ((style & WS_CAPTION) == WS_CAPTION) {
                    OutputDebugStringW(L"[成功] 捕获到目标弹出的窗口！实施扒衣！\n");
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
                    if (!exists) g_hijackedWindows.push_back({ target });

                    it = g_pendingHijacks.erase(it);
                    continue;
                } else {
                    LOGW(L"[hijack] window found but no WS_CAPTION (modern app?), go back to 3D: HWND=0x%p", target);
                    it = g_pendingHijacks.erase(it);
                    if (g_pendingHijacks.empty() && g_hijackedWindows.empty()) {
                        g_currentState = STATE_3D_EXPLORE;
                        g_uiUnlocked = true;
                        while (ShowCursor(TRUE) < 0); ClipCursor(NULL);
                    }
                    continue;
                }
            }
            
            if (it->frameWait > 120) { 
                LOGW(L"[hijack] pending hijack PID=%u timed out (120 frames)", it->processId);
                it = g_pendingHijacks.erase(it);
            } else {
                ++it;
            }
        }
  
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
            ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
            ImVec2 center = ImVec2(ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x * 0.5f,
                                   ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y * 0.5f);
            draw_list->AddLine(ImVec2(center.x - 10, center.y), ImVec2(center.x + 10, center.y), IM_COL32(255, 255, 255, 255), 2.0f);
            draw_list->AddLine(ImVec2(center.x, center.y - 10), ImVec2(center.x, center.y + 10), IM_COL32(255, 255, 255, 255), 2.0f);
        }
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            enum TaskbarAction {
                TASKBAR_ACTION_START = 0,
                TASKBAR_ACTION_LAUNCH = 1,
                TASKBAR_ACTION_NONE = 2
            };
            enum TaskbarIconKind {
                TASKBAR_ICON_WINDOWS = 0,
                TASKBAR_ICON_SEARCH = 1,
                TASKBAR_ICON_APP = 2
            };
            struct TaskbarEntry {
                std::string Id;
                std::string Label;
                std::wstring AppPath;
                ImU32 Accent;
                TaskbarAction Action;
                TaskbarIconKind IconKind;
                ID3D11ShaderResourceView* Icon;
                bool Pinned;
                bool Running;
                HWND Hwnd;
                DWORD Pid;
            };
            static std::vector<TaskbarEntry> taskbarPinned;
            static bool taskbarStartOpen = false;
            static bool taskbarTrayOpen = false;
            static bool taskbarPinnedInit = false;
            if (!taskbarPinnedInit) {
                taskbarPinned = {
                    { "Taskbar.Start",  "Start",  L"", IM_COL32(0, 140, 255, 255), TASKBAR_ACTION_START, TASKBAR_ICON_WINDOWS, nullptr, true, false, nullptr, 0 },
                    { "Taskbar.Search", "Search", L"", IM_COL32(120, 170, 255, 255), TASKBAR_ACTION_NONE,  TASKBAR_ICON_SEARCH, nullptr, true, false, nullptr, 0 },
                    { "Taskbar.Files",  "Files",  L"C:\\Windows\\explorer.exe", IM_COL32(255, 210, 120, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 },
                    { "Taskbar.Edge",   "Edge",   L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", IM_COL32(120, 210, 255, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 },
                    { "Taskbar.Terminal", "Terminal", L"C:\\Windows\\System32\\cmd.exe", IM_COL32(140, 255, 170, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 },
                    { "Taskbar.Code",   "Code",   L"D:\\Apps\\Microsoft VS Code\\Code.exe", IM_COL32(120, 185, 255, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 }
                };
                taskbarPinnedInit = true;
            }

            for (auto& entry : taskbarPinned) {
                if (entry.IconKind == TASKBAR_ICON_APP && entry.Icon == nullptr && !entry.AppPath.empty()) {
                    entry.Icon = GetTaskbarIcon(g_pd3dDevice, entry.AppPath);
                }
                entry.Running = false;
                entry.Hwnd = nullptr;
                entry.Pid = 0;
            }

            std::vector<RunningWindow> runningWindows = EnumerateRunningWindows(g_mainHwnd);
            for (const auto& win : runningWindows) {
                if (g_taskbarDynamicOrder.find(win.hwnd) == g_taskbarDynamicOrder.end()) {
                    g_taskbarDynamicOrder[win.hwnd] = g_taskbarDynamicOrderCounter++;
                }
            }
            for (auto it = g_taskbarDynamicOrder.begin(); it != g_taskbarDynamicOrder.end(); ) {
                bool alive = false;
                for (const auto& win : runningWindows) {
                    if (win.hwnd == it->first) { alive = true; break; }
                }
                if (!alive) it = g_taskbarDynamicOrder.erase(it);
                else ++it;
            }
            std::sort(runningWindows.begin(), runningWindows.end(), [&](const RunningWindow& a, const RunningWindow& b) {
                int orderA = 0;
                int orderB = 0;
                auto itA = g_taskbarDynamicOrder.find(a.hwnd);
                auto itB = g_taskbarDynamicOrder.find(b.hwnd);
                if (itA != g_taskbarDynamicOrder.end()) orderA = itA->second;
                if (itB != g_taskbarDynamicOrder.end()) orderB = itB->second;
                return orderA < orderB;
            });
            HWND fgWindow = GetForegroundWindow();
            for (auto& pinned : taskbarPinned) {
                if (pinned.IconKind == TASKBAR_ICON_APP) {
                    pinned.Running = false;
                    pinned.Hwnd = nullptr;
                    pinned.Pid = 0;
                }
            }
            std::vector<TaskbarEntry> taskbarDynamic;
            taskbarDynamic.reserve(runningWindows.size());
            for (const auto& win : runningWindows) {
                bool matched = false;
                for (auto& pinned : taskbarPinned) {
                    if (pinned.IconKind == TASKBAR_ICON_APP && IsSamePath(win.path, pinned.AppPath)) {
                        pinned.Running = true;
                        if (pinned.Hwnd == nullptr || win.hwnd == fgWindow) {
                            pinned.Hwnd = win.hwnd;
                            pinned.Pid = win.pid;
                        }
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    TaskbarEntry dyn = {};
                    char idBuffer[64];
                    sprintf_s(idBuffer, "Taskbar.Run.%p", win.hwnd);
                    dyn.Id = idBuffer;
                    dyn.Label = "Running";
                    dyn.AppPath = win.path;
                    dyn.Accent = IM_COL32(120, 185, 255, 255);
                    dyn.Action = TASKBAR_ACTION_NONE;
                    dyn.IconKind = TASKBAR_ICON_APP;
                    dyn.Icon = GetWindowIconTexture(g_pd3dDevice, win.hwnd, win.path);
                    dyn.Pinned = false;
                    dyn.Running = true;
                    dyn.Hwnd = win.hwnd;
                    dyn.Pid = win.pid;
                    taskbarDynamic.push_back(dyn);
                }
            }
            for (auto& pinned : taskbarPinned) {
                if (pinned.IconKind == TASKBAR_ICON_APP && !pinned.Running) {
                    HWND found = FindRunningHwnd(pinned.AppPath);
                    if (found) {
                        pinned.Running = true;
                        pinned.Hwnd = found;
                    }
                }
            }

            std::vector<TaskbarEntry*> taskbarEntries;
            taskbarEntries.reserve(taskbarPinned.size() + taskbarDynamic.size());
            for (auto& pinned : taskbarPinned) taskbarEntries.push_back(&pinned);
            for (auto& dyn : taskbarDynamic) taskbarEntries.push_back(&dyn);

            const int taskbarAppCount = (int)taskbarEntries.size();
            float iconSize = 32.0f;
            float iconGap = 12.0f;
            float paddingX = 18.0f;
            float paddingY = 8.0f;
            float barHeight = iconSize + paddingY * 2.0f;
            float barWidth = viewport->WorkSize.x;
            float scale = 1.0f;

            SYSTEMTIME st;
            GetLocalTime(&st);
            char timeText[16];
            char dateText[16];
            sprintf_s(timeText, "%02u:%02u", st.wHour, st.wMinute);
            sprintf_s(dateText, "%04u/%02u/%02u", st.wYear, st.wMonth, st.wDay);

            bool imeOpen = GetImeOpenStatus(hwnd);
            bool imeChinese = IsChineseImeLayout();
            const char* imeLeft = imeChinese ? (imeOpen ? "中" : "英") : "";
            const char* imeRight = imeChinese ? "拼" : "ENG";
            NetworkStatus netStatus = GetNetworkStatus();
            bool netConnected = netStatus.connected;
            bool netWifi = netStatus.wifi || !netStatus.ethernet;
            float volumeLevel = GetMasterVolumeLevelScalar();
            int batteryPercent = 100;
            bool batteryCharging = false;
            SYSTEM_POWER_STATUS power = {};
            if (GetSystemPowerStatus(&power)) {
                if (power.BatteryLifePercent != 255) {
                    batteryPercent = (int)power.BatteryLifePercent;
                }
                batteryCharging = (power.ACLineStatus == 1);
            }

            ImVec2 timeSize = ImGui::CalcTextSize(timeText);
            ImVec2 dateSize = ImGui::CalcTextSize(dateText);
            float timeBlockWidth = (timeSize.x > dateSize.x ? timeSize.x : dateSize.x);
            float timeBlockHeight = timeSize.y + dateSize.y;

            float trayIconBox = 16.0f;
            float trayGap = 10.0f;
            ImVec2 imeLeftSize = ImGui::CalcTextSize(imeLeft);
            ImVec2 imeRightSize = ImGui::CalcTextSize(imeRight);
            float imeGap = 10.0f;
            float imeBlockWidth = (imeLeftSize.x > 0.0f ? imeLeftSize.x + imeGap : 0.0f) + imeRightSize.x;
            ImVec2 chevronSize = ImGui::CalcTextSize("^");
            float rightAreaWidth = chevronSize.x + imeBlockWidth + timeBlockWidth + trayIconBox * 3.0f + trayGap * 5.0f;

            ImVec2 barPos = ImVec2(
                viewport->WorkPos.x,
                viewport->WorkPos.y + viewport->WorkSize.y - barHeight
            );
            g_taskbarRect.left = (LONG)barPos.x;
            g_taskbarRect.top = (LONG)barPos.y;
            g_taskbarRect.right = (LONG)(barPos.x + barWidth);
            g_taskbarRect.bottom = (LONG)(barPos.y + barHeight);
            g_taskbarRectValid = true;

            ImGui::SetNextWindowPos(barPos);
            ImGui::SetNextWindowSize(ImVec2(barWidth, barHeight));
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(paddingX, paddingY));
            ImGui::Begin("CrossDim Taskbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetWindowPos();
            ImVec2 p1 = ImVec2(p0.x + barWidth, p0.y + barHeight);
            draw->AddRectFilled(p0, p1, IM_COL32(244, 246, 250, 235), 0.0f);
            draw->AddRectFilledMultiColor(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y + barHeight * 0.5f),
                                          IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 120),
                                          IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
            draw->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y), IM_COL32(255, 255, 255, 120), 1.0f);
            draw->AddLine(ImVec2(p0.x, p1.y - 1.0f), ImVec2(p1.x, p1.y - 1.0f), IM_COL32(0, 0, 0, 18), 1.0f);

            auto launchTaskbarApp = [&](LPCWSTR appPath) {
                if (!appPath || appPath[0] == L'\0') return;
                WCHAR cmdBuffer[MAX_PATH];
                wcscpy_s(cmdBuffer, appPath);
                STARTUPINFOW si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                    HWND currentFocus = GetForegroundWindow();
                    g_pendingHijacks.push_back({ currentFocus, 0, pi.dwProcessId });
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    SetSystemTaskbarVisible(false);
                }
            };

            auto drawWindowsGlyph = [&](ImVec2 a, ImVec2 b, ImU32 color) {
                float w = b.x - a.x;
                float r = w * 0.08f;
                draw->AddRectFilled(a, b, color, r);
                float cx = (a.x + b.x) * 0.5f;
                float cy = (a.y + b.y) * 0.5f;
                ImU32 lineColor = IM_COL32(255, 255, 255, 230);
                draw->AddLine(ImVec2(cx, a.y), ImVec2(cx, b.y), lineColor, 1.6f);
                draw->AddLine(ImVec2(a.x, cy), ImVec2(b.x, cy), lineColor, 1.6f);
            };

            auto drawSearchGlyph = [&](ImVec2 center, float size, ImU32 color) {
                float r = size * 0.28f;
                ImVec2 c = ImVec2(center.x - size * 0.06f, center.y - size * 0.06f);
                draw->AddCircle(c, r, color, 16, 1.6f);
                ImVec2 handleStart = ImVec2(c.x + r * 0.7f, c.y + r * 0.7f);
                ImVec2 handleEnd = ImVec2(handleStart.x + r * 0.9f, handleStart.y + r * 0.9f);
                draw->AddLine(handleStart, handleEnd, color, 1.6f);
            };

            auto drawAppGlyph = [&](ImVec2 center, float size, ImU32 color) {
                float s = size * 0.34f;
                ImVec2 a = ImVec2(center.x - s, center.y - s);
                ImVec2 b = ImVec2(center.x + s, center.y + s);
                draw->AddRectFilled(a, b, color, 4.0f);
            };

            auto drawBatteryIcon = [&](ImVec2 pos, float w, float h, int percent, bool charging, ImU32 color) {
                float capW = w * 0.12f;
                ImVec2 bodyMin = pos;
                ImVec2 bodyMax = ImVec2(pos.x + w - capW - 1.0f, pos.y + h);
                draw->AddRect(bodyMin, bodyMax, color, 2.0f, 0, 1.2f);
                float fillW = (bodyMax.x - bodyMin.x - 4.0f) * (percent < 0 ? 0.0f : (percent > 100 ? 1.0f : (percent / 100.0f)));
                ImVec2 fillMin = ImVec2(bodyMin.x + 2.0f, bodyMin.y + 2.0f);
                ImVec2 fillMax = ImVec2(fillMin.x + fillW, bodyMax.y - 2.0f);
                draw->AddRectFilled(fillMin, fillMax, color, 1.0f);
                ImVec2 capMin = ImVec2(bodyMax.x + 1.0f, pos.y + h * 0.32f);
                ImVec2 capMax = ImVec2(bodyMax.x + 1.0f + capW, pos.y + h * 0.68f);
                draw->AddRectFilled(capMin, capMax, color, 1.0f);
                if (charging) {
                    ImVec2 boltA = ImVec2(bodyMin.x + w * 0.45f, pos.y + h * 0.2f);
                    ImVec2 boltB = ImVec2(bodyMin.x + w * 0.6f, pos.y + h * 0.45f);
                    ImVec2 boltC = ImVec2(bodyMin.x + w * 0.5f, pos.y + h * 0.45f);
                    ImVec2 boltD = ImVec2(bodyMin.x + w * 0.63f, pos.y + h * 0.8f);
                    draw->AddTriangleFilled(boltA, boltB, boltC, IM_COL32(180, 255, 180, 230));
                    draw->AddTriangleFilled(boltB, boltC, boltD, IM_COL32(180, 255, 180, 230));
                }
            };

            auto drawVolumeIcon = [&](ImVec2 center, float size, float level, ImU32 color) {
                float bodyW = size * 0.22f;
                float bodyH = size * 0.32f;
                ImVec2 bodyMin = ImVec2(center.x - size * 0.42f, center.y - bodyH * 0.5f);
                ImVec2 bodyMax = ImVec2(bodyMin.x + bodyW, bodyMin.y + bodyH);
                draw->AddRectFilled(bodyMin, bodyMax, color, 1.0f);
                ImVec2 triA = ImVec2(bodyMax.x, bodyMin.y - bodyH * 0.25f);
                ImVec2 triB = ImVec2(bodyMax.x + size * 0.32f, center.y);
                ImVec2 triC = ImVec2(bodyMax.x, bodyMax.y + bodyH * 0.25f);
                draw->AddTriangleFilled(triA, triB, triC, color);
                if (level > 0.2f) draw->AddCircle(center, size * 0.32f, color, 12, 1.2f);
                if (level > 0.6f) draw->AddCircle(center, size * 0.42f, color, 12, 1.2f);
            };

            auto drawNetBarsIcon = [&](ImVec2 center, float size, bool wifi, bool connected, ImU32 color) {
                float barW = size * 0.12f;
                float gap = size * 0.12f;
                float baseY = center.y + size * 0.22f;
                for (int i = 0; i < 3; ++i) {
                    float h = size * (0.2f + 0.15f * i);
                    float x = center.x - (barW * 1.5f + gap) + i * (barW + gap);
                    draw->AddRectFilled(ImVec2(x, baseY - h), ImVec2(x + barW, baseY), color, 1.0f);
                }
                if (wifi) {
                    draw->AddCircleFilled(ImVec2(center.x, baseY + barW * 0.5f), barW * 0.45f, color);
                }
                if (!connected) {
                    draw->AddLine(ImVec2(center.x - size * 0.4f, center.y - size * 0.35f),
                                  ImVec2(center.x + size * 0.4f, center.y + size * 0.35f),
                                  color, 1.4f);
                }
            };

            float centerAreaLeft = p0.x + paddingX;
            float centerAreaRight = p1.x - paddingX - rightAreaWidth;
            float centerAreaWidth = centerAreaRight - centerAreaLeft;
            float iconRowWidth = taskbarAppCount * iconSize +
                                 (taskbarAppCount > 1 ? (taskbarAppCount - 1) * iconGap : 0.0f);
            float iconScale = 1.0f;
            if (centerAreaWidth > 0.0f && iconRowWidth > centerAreaWidth) {
                iconScale = centerAreaWidth / iconRowWidth;
            }
            float drawIconSize = iconSize * iconScale;
            float drawIconGap = iconGap * iconScale;
            float drawRowWidth = taskbarAppCount * drawIconSize + (taskbarAppCount > 1 ? (taskbarAppCount - 1) * drawIconGap : 0.0f);

            for (auto& pinned : taskbarPinned) {
                if (pinned.Running && pinned.Hwnd) {
                    pinned.Icon = GetWindowIconTexture(g_pd3dDevice, pinned.Hwnd, pinned.AppPath);
                }
            }
            if (centerAreaWidth < 0.0f) centerAreaWidth = 0.0f;

            ImVec2 cursor = ImVec2(
                centerAreaLeft + (centerAreaWidth - drawRowWidth) * 0.5f,
                p0.y + (barHeight - drawIconSize) * 0.5f
            );
            float rowStartX = cursor.x;
            static int dragPinnedIndex = -1;
            static int dragDynamicIndex = -1;
            static ImGuiID dragItemId = 0;
            static bool dragItemMoved = false;
            static ImVec2 dragMouseStart = ImVec2(0, 0);
            const int pinnedCount = (int)taskbarPinned.size();
            const int dynamicCount = (int)taskbarDynamic.size();
            for (int i = 0; i < taskbarAppCount; ++i) {
                ImVec2 iconPos = ImVec2(cursor.x + i * (drawIconSize + drawIconGap), cursor.y);
                ImGui::SetCursorScreenPos(iconPos);
                TaskbarEntry& entry = *taskbarEntries[i];
                ImGui::InvisibleButton(entry.Id.c_str(), ImVec2(drawIconSize, drawIconSize));

                ImGuiID itemId = ImGui::GetItemID();
                if (ImGui::IsItemActivated()) {
                    dragItemId = itemId;
                    dragMouseStart = ImGui::GetIO().MousePos;
                    dragItemMoved = false;
                }
                if (dragItemId == itemId && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !dragItemMoved) {
                    float dx = ImGui::GetIO().MousePos.x - dragMouseStart.x;
                    float dy = ImGui::GetIO().MousePos.y - dragMouseStart.y;
                    if (dx * dx + dy * dy > 6.0f) {
                        dragItemMoved = true;
                    }
                }

                bool hovered = ImGui::IsItemHovered();
                bool isStart = (entry.Action == TASKBAR_ACTION_START);
                bool isRunning = entry.Running;
                bool isForeground = (entry.Hwnd != nullptr && entry.Hwnd == fgWindow);
                bool isActive = isStart ? taskbarStartOpen : isForeground;
                bool wasDragged = (dragItemMoved && dragItemId == itemId);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !wasDragged) {
                    if (isStart) {
                        taskbarStartOpen = !taskbarStartOpen;
                    } else {
                        taskbarStartOpen = false;
                        HWND clickHwnd = (entry.Running && entry.Hwnd) ? entry.Hwnd : FindRunningHwnd(entry.AppPath);
                        if (clickHwnd) {
                            entry.Running = true;
                            entry.Hwnd = clickHwnd;
                            if (clickHwnd == fgWindow) {
                                ShowWindow(clickHwnd, SW_MINIMIZE);
                            } else {
                                ShowWindow(clickHwnd, SW_RESTORE);
                                DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
                                DWORD targetThread = GetWindowThreadProcessId(clickHwnd, nullptr);
                                AttachThreadInput(fgThread, targetThread, TRUE);
                                SetForegroundWindow(clickHwnd);
                                AttachThreadInput(fgThread, targetThread, FALSE);
                            }
                        } else if (entry.Action == TASKBAR_ACTION_LAUNCH && !entry.AppPath.empty()) {
                            launchTaskbarApp(entry.AppPath.c_str());
                        }
                    }
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Middle) && isRunning && entry.Hwnd) {
                    PostMessage(entry.Hwnd, WM_CLOSE, 0, 0);
                }

                if (dragItemMoved && dragItemId == itemId && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    float slot = (ImGui::GetIO().MousePos.x - rowStartX + (drawIconSize * 0.5f)) / (drawIconSize + drawIconGap);
                    int target = (int)slot;
                    if (entry.Pinned) {
                        if (dragPinnedIndex == -1) dragPinnedIndex = i;
                        if (target < 0) target = 0;
                        if (target >= pinnedCount) target = pinnedCount - 1;
                        if (target != dragPinnedIndex && target >= 0 && dragPinnedIndex >= 0 && dragPinnedIndex < pinnedCount) {
                            std::swap(taskbarPinned[dragPinnedIndex], taskbarPinned[target]);
                            dragPinnedIndex = target;
                        }
                    } else if (dynamicCount > 1) {
                        int dynIndex = i - pinnedCount;
                        if (dynIndex >= 0 && dynIndex < dynamicCount) {
                            if (dragDynamicIndex == -1) dragDynamicIndex = dynIndex;
                            int targetDyn = target - pinnedCount;
                            if (targetDyn < 0) targetDyn = 0;
                            if (targetDyn >= dynamicCount) targetDyn = dynamicCount - 1;
                            if (targetDyn != dragDynamicIndex && dragDynamicIndex >= 0 && dragDynamicIndex < dynamicCount) {
                                HWND a = taskbarDynamic[dragDynamicIndex].Hwnd;
                                HWND b = taskbarDynamic[targetDyn].Hwnd;
                                auto itA = g_taskbarDynamicOrder.find(a);
                                auto itB = g_taskbarDynamicOrder.find(b);
                                if (itA != g_taskbarDynamicOrder.end() && itB != g_taskbarDynamicOrder.end()) {
                                    std::swap(itA->second, itB->second);
                                }
                                std::swap(taskbarDynamic[dragDynamicIndex], taskbarDynamic[targetDyn]);
                                dragDynamicIndex = targetDyn;
                            }
                        }
                    }
                }
                ImVec2 b0 = ImGui::GetItemRectMin();
                ImVec2 b1 = ImGui::GetItemRectMax();
                float iconRound = 6.0f * (drawIconSize / 28.0f);
                ImU32 base = (hovered || isActive) ? IM_COL32(255, 255, 255, 180) : IM_COL32(255, 255, 255, 0);
                if ((base >> 24) > 0) draw->AddRectFilled(b0, b1, base, iconRound);

                bool isAppIcon = (entry.IconKind == TASKBAR_ICON_APP);
                if (isAppIcon) {
                    ImVec4 accent = ImGui::ColorConvertU32ToFloat4(entry.Accent);
                    accent.w = hovered ? 0.08f : 0.04f;
                    if (accent.w > 0.001f) {
                        draw->AddRectFilled(b0, b1, ImGui::ColorConvertFloat4ToU32(accent), iconRound);
                    }
                }

                ImVec2 center = ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
                ImVec2 glyphMin = ImVec2(b0.x + drawIconSize * 0.22f, b0.y + drawIconSize * 0.22f);
                ImVec2 glyphMax = ImVec2(b1.x - drawIconSize * 0.22f, b1.y - drawIconSize * 0.22f);
                if (entry.IconKind == TASKBAR_ICON_WINDOWS) {
                    drawWindowsGlyph(glyphMin, glyphMax, entry.Accent);
                } else if (entry.IconKind == TASKBAR_ICON_SEARCH) {
                    drawSearchGlyph(center, drawIconSize, IM_COL32(30, 40, 60, 210));
                } else {
                    ID3D11ShaderResourceView* iconSrv = entry.Icon;
                    if (iconSrv) {
                        float imgSize = drawIconSize * 0.72f;
                        ImVec2 imgPos = ImVec2(center.x - imgSize * 0.5f, center.y - imgSize * 0.5f);
                        draw->AddImage((ImTextureID)iconSrv, imgPos, ImVec2(imgPos.x + imgSize, imgPos.y + imgSize));
                    } else {
                        ImVec4 glyphAccent = ImGui::ColorConvertU32ToFloat4(entry.Accent);
                        glyphAccent.w = 0.85f;
                        drawAppGlyph(center, drawIconSize, ImGui::ColorConvertFloat4ToU32(glyphAccent));
                    }
                }

                if (isRunning) {
                    ImVec2 dot = ImVec2((b0.x + b1.x) * 0.5f, b1.y + 3.0f);
                    float dotRadius = isForeground ? 2.6f : 2.0f;
                    ImU32 dotColor = isForeground ? entry.Accent : IM_COL32(120, 140, 190, 220);
                    draw->AddCircleFilled(dot, dotRadius, dotColor);
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragPinnedIndex = -1;
                dragDynamicIndex = -1;
                dragItemId = 0;
                dragItemMoved = false;
                dragMouseStart = ImVec2(0, 0);
            }

            ImU32 trayColor = IM_COL32(20, 30, 50, 220);
            ImU32 trayMuted = IM_COL32(20, 30, 50, 150);
            ImU32 netColor = netConnected ? trayColor : IM_COL32(20, 30, 50, 110);
            float centerY = p0.y + barHeight * 0.5f;
            float timeX = p1.x - paddingX - timeSize.x;
            float dateX = p1.x - paddingX - dateSize.x;
            float timeY = centerY - timeBlockHeight * 0.5f;
            draw->AddText(ImVec2(timeX, timeY), trayColor, timeText);
            draw->AddText(ImVec2(dateX, timeY + timeSize.y), trayMuted, dateText);

            float cursorX = p1.x - paddingX - timeBlockWidth - trayGap;
            float batteryH = trayIconBox * 0.55f;
            cursorX -= trayIconBox;
            ImU32 batteryColor = batteryCharging ? IM_COL32(80, 200, 80, 240) : trayColor;
            drawBatteryIcon(ImVec2(cursorX, centerY - batteryH * 0.5f), trayIconBox, batteryH, batteryPercent, batteryCharging, batteryColor);

            cursorX -= trayGap + trayIconBox;
            drawVolumeIcon(ImVec2(cursorX + trayIconBox * 0.5f, centerY), trayIconBox, volumeLevel, trayColor);

            cursorX -= trayGap + trayIconBox;
            drawNetBarsIcon(ImVec2(cursorX + trayIconBox * 0.5f, centerY), trayIconBox, netWifi, netConnected, netColor);

            cursorX -= trayGap + imeBlockWidth;
            ImVec2 imeLeftPos = ImVec2(cursorX, centerY - imeLeftSize.y * 0.5f);
            ImVec2 imeRightPos = ImVec2(cursorX + imeLeftSize.x + imeGap, centerY - imeRightSize.y * 0.5f);

            bool imeLeftHover = false;
            if (imeChinese) {
                ImGui::SetCursorScreenPos(ImVec2(imeLeftPos.x - 3.0f, imeLeftPos.y - 2.0f));
                ImGui::InvisibleButton("Ime.Left", ImVec2(imeLeftSize.x + 6.0f, imeLeftSize.y + 4.0f));
                imeLeftHover = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    INPUT inputs[2] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = VK_SHIFT;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = VK_SHIFT;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(imeRightPos.x - 3.0f, imeRightPos.y - 2.0f));
            ImGui::InvisibleButton("Ime.Right", ImVec2(imeRightSize.x + 6.0f, imeRightSize.y + 4.0f));
            bool imeRightHover = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImGui::OpenPopup("Ime.Menu");
            }

            ImU32 imeLeftColor = imeLeftHover ? IM_COL32(20, 30, 50, 240) : trayColor;
            ImU32 imeRightColor = imeRightHover ? IM_COL32(20, 30, 50, 240) : trayColor;
            draw->AddText(imeLeftPos, imeLeftColor, imeLeft);
            draw->AddText(imeRightPos, imeRightColor, imeRight);

            if (ImGui::BeginPopup("Ime.Menu")) {
                if (ImGui::MenuItem("Microsoft Pinyin")) {
                    ActivateImeLayout(hwnd, L"00000804", true);
                }
                if (ImGui::MenuItem("ENG")) {
                    ActivateImeLayout(hwnd, L"00000409", false);
                }
                ImGui::EndPopup();
            }

            cursorX -= trayGap + chevronSize.x;
            ImGui::SetCursorScreenPos(ImVec2(cursorX - 2.0f, centerY - trayIconBox * 0.5f));
            ImGui::InvisibleButton("Taskbar.TrayToggle", ImVec2(chevronSize.x + 4.0f, trayIconBox));
            bool chevronHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                taskbarTrayOpen = !taskbarTrayOpen;
            }
            ImU32 chevronColor = chevronHovered ? IM_COL32(20, 30, 50, 240) : IM_COL32(20, 30, 50, 200);
            draw->AddText(ImVec2(cursorX, centerY - chevronSize.y * 0.5f), chevronColor, taskbarTrayOpen ? "v" : "^");

            ImGui::End();
            ImGui::PopStyleVar(3);

            if (taskbarStartOpen) {
                float panelWidth = 420.0f * scale;
                float panelHeight = 320.0f * scale;
                if (panelWidth > viewport->WorkSize.x - 32.0f) panelWidth = viewport->WorkSize.x - 32.0f;
                if (panelHeight > viewport->WorkSize.y - 120.0f) panelHeight = viewport->WorkSize.y - 120.0f;
                ImVec2 panelPos = ImVec2(
                    viewport->WorkPos.x + (viewport->WorkSize.x - panelWidth) * 0.5f,
                    barPos.y - panelHeight - 12.0f * scale
                );
                ImGui::SetNextWindowPos(panelPos);
                ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
                ImGui::SetNextWindowBgAlpha(0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f * scale);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * scale, 16.0f * scale));
                ImGui::Begin("CrossDim Start", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

                ImDrawList* panelDraw = ImGui::GetWindowDrawList();
                ImVec2 q0 = ImGui::GetWindowPos();
                ImVec2 q1 = ImVec2(q0.x + panelWidth, q0.y + panelHeight);
                float panelRound = 20.0f * scale;
                panelDraw->AddRectFilled(ImVec2(q0.x, q0.y + 2.0f * scale), ImVec2(q1.x, q1.y + 8.0f * scale), IM_COL32(0, 0, 0, 50), panelRound);
                panelDraw->AddRectFilled(q0, q1, IM_COL32(235, 238, 246, 200), panelRound);
                panelDraw->AddRectFilledMultiColor(ImVec2(q0.x, q0.y), ImVec2(q1.x, q0.y + panelHeight * 0.55f),
                                                   IM_COL32(255, 255, 255, 100), IM_COL32(255, 255, 255, 100),
                                                   IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
                panelDraw->AddRect(q0, q1, IM_COL32(255, 255, 255, 120), panelRound, 0, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * scale);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, 6.0f * scale));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##StartSearch", g_searchFilter, sizeof(g_searchFilter));
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);

                ImGui::Spacing();
                ImGui::Text("Pinned");
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

                ImDrawList* startDraw = ImGui::GetWindowDrawList();
                float tileGap = 10.0f * scale;
                float tilePad = 4.0f * scale;
                int tileCols = 4;
                float tileW = (panelWidth - tilePad * 2.0f - tileGap * (tileCols - 1)) / (float)tileCols;
                if (tileW < 54.0f * scale) {
                    tileCols = 3;
                    tileW = (panelWidth - tilePad * 2.0f - tileGap * (tileCols - 1)) / (float)tileCols;
                }
                float tileH = tileW + 22.0f * scale;
                ImVec2 gridStart = ImGui::GetCursorScreenPos();
                float gridX = gridStart.x + tilePad;
                float gridY = gridStart.y;
                int gridCol = 0;

                for (int i = 0; i < (int)taskbarPinned.size(); ++i) {
                    if (taskbarPinned[i].IconKind != TASKBAR_ICON_APP) continue;
                    if (g_searchFilter[0] && !SearchMatch(g_searchFilter, taskbarPinned[i].Label, taskbarPinned[i].AppPath)) continue;
                    ImGui::PushID(i);
                    ImGui::SetCursorScreenPos(ImVec2(gridX, gridY));
                    ImGui::InvisibleButton("StartTile", ImVec2(tileW, tileH));
                    bool hovered = ImGui::IsItemHovered();
                    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

                    ImVec2 b0 = ImGui::GetItemRectMin();
                    ImVec2 b1 = ImGui::GetItemRectMax();
                    ImU32 tileColor = hovered ? IM_COL32(255, 255, 255, 210) : IM_COL32(255, 255, 255, 150);
                    ImU32 tileBorder = hovered ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 120);
                    startDraw->AddRectFilled(b0, b1, tileColor, 12.0f * scale);
                    startDraw->AddRect(b0, b1, tileBorder, 12.0f * scale, 0, 1.0f);

                    ImVec2 iconCenter = ImVec2((b0.x + b1.x) * 0.5f, b0.y + tileW * 0.45f);
                    ID3D11ShaderResourceView* iconSrv = taskbarPinned[i].Icon;
                    if (iconSrv) {
                        float imgSize = tileW * 0.55f;
                        ImVec2 imgPos = ImVec2(iconCenter.x - imgSize * 0.5f, iconCenter.y - imgSize * 0.5f);
                        startDraw->AddImage((ImTextureID)iconSrv, imgPos, ImVec2(imgPos.x + imgSize, imgPos.y + imgSize));
                    } else {
                        ImVec4 glyphAccent = ImGui::ColorConvertU32ToFloat4(taskbarPinned[i].Accent);
                        glyphAccent.w = 0.85f;
                        drawAppGlyph(iconCenter, tileW * 0.75f, ImGui::ColorConvertFloat4ToU32(glyphAccent));
                    }

                    const char* label = taskbarPinned[i].Label.c_str();
                    ImVec2 labelSize = ImGui::CalcTextSize(label);
                    float labelX = b0.x + (tileW - labelSize.x) * 0.5f;
                    float labelY = b1.y - labelSize.y - 6.0f * scale;
                    startDraw->AddText(ImVec2(labelX, labelY), IM_COL32(35, 45, 65, 230), label);

                    if (clicked) {
                        taskbarStartOpen = false;
                        if (!taskbarPinned[i].AppPath.empty()) {
                            launchTaskbarApp(taskbarPinned[i].AppPath.c_str());
                        }
                    }

                    ImGui::PopID();
                    gridCol++;
                    if (gridCol >= tileCols) {
                        gridCol = 0;
                        gridX = gridStart.x + tilePad;
                        gridY += tileH + tileGap;
                    } else {
                        gridX += tileW + tileGap;
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(gridStart.x, gridY + tileH + tileGap));

                ImGui::End();
                ImGui::PopStyleVar(3);
            }

            if (taskbarTrayOpen) {
                static std::vector<TrayIconEntry> s_trayIcons;
                static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> s_trayIconCache;
                static int s_trayQueryFrame = 999;
                s_trayQueryFrame++;
                if (s_trayQueryFrame >= 60) {
                    s_trayQueryFrame = 0;
                    s_trayIcons = QueryTrayIcons();
                    std::vector<RunningWindow> rw = EnumerateRunningWindows(g_mainHwnd);
                    for (auto& icon : s_trayIcons) {
                        if (!icon.exePath.empty()) continue;
                        for (const auto& w : rw) {
                            if (!w.path.empty()) {
                                size_t p = w.path.rfind(L'\\');
                                std::wstring fname = (p != std::wstring::npos) ? w.path.substr(p + 1) : w.path;
                                size_t d = fname.rfind(L'.');
                                std::wstring nameOnly = (d != std::wstring::npos) ? fname.substr(0, d) : fname;
                                if (!nameOnly.empty()
                                    && icon.tooltip.find(nameOnly) != std::wstring::npos) {
                                    icon.exePath = w.path;
                                    break;
                                }
                            }
                        }
                    }
                }
                int iconCount = (int)s_trayIcons.size();
                float rowHeight = 28.0f;
                float panelW = 220.0f;
                float panelH = 50.0f + iconCount * rowHeight;
                if (panelH < 100.0f) panelH = 100.0f;
                if (panelH > 400.0f) panelH = 400.0f;
                ImVec2 panelPos = ImVec2(p1.x - panelW - paddingX, barPos.y - panelH - 10.0f);
                ImGui::SetNextWindowPos(panelPos);
                ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
                ImGui::SetNextWindowBgAlpha(0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
                ImGui::Begin("CrossDim Tray", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
                ImDrawList* panelDraw = ImGui::GetWindowDrawList();
                ImVec2 q0 = ImGui::GetWindowPos();
                ImVec2 q1 = ImVec2(q0.x + panelW, q0.y + panelH);
                panelDraw->AddRectFilled(q0, q1, IM_COL32(244, 246, 250, 235), 12.0f);
                panelDraw->AddRect(q0, q1, IM_COL32(255, 255, 255, 120), 12.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
                ImGui::Text(s_trayIcons.empty() ? "(no tray icons detected)" : "System Tray");
                ImGui::Separator();
                for (const auto& icon : s_trayIcons) {
                    ID3D11ShaderResourceView* iconSrv = nullptr;
                    if (!icon.exePath.empty()) {
                        auto it = s_trayIconCache.find(icon.exePath);
                        if (it != s_trayIconCache.end()) iconSrv = it->second;
                        else {
                            iconSrv = GetTaskbarIcon(g_pd3dDevice, icon.exePath);
                            s_trayIconCache[icon.exePath] = iconSrv;
                        }
                    }
                    float imgSize = 22.0f;
                    if (iconSrv) ImGui::Image((ImTextureID)iconSrv, ImVec2(imgSize, imgSize));
                    else ImGui::Dummy(ImVec2(imgSize, imgSize));
                    ImGui::SameLine();
                    char label[128];
                    WideCharToMultiByte(CP_UTF8, 0, icon.tooltip.c_str(), -1, label, sizeof(label), nullptr, nullptr);
                    ImGui::Text("%s", label);
                }
                ImGui::PopStyleColor();
                ImGui::End();
                ImGui::PopStyleVar(3);
            }
        }

        if ((g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) && !g_hijackedWindows.empty()) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);

            enum SnapZone {
                SNAP_NONE = 0,
                SNAP_LEFT_HALF,
                SNAP_RIGHT_HALF,
                SNAP_MAXIMIZE,
                SNAP_TOP_LEFT,
                SNAP_TOP_RIGHT,
                SNAP_BOTTOM_LEFT,
                SNAP_BOTTOM_RIGHT
            };

            auto detectSnapZone = [](int mx, int my, const RECT& area) -> SnapZone {
                const int thresh = 20;
                bool nearL = (mx - area.left) < thresh;
                bool nearR = (area.right - mx) < thresh;
                bool nearT = (my - area.top) < thresh;
                if (nearT && nearL) return SNAP_TOP_LEFT;
                if (nearT && nearR) return SNAP_TOP_RIGHT;
                if (nearT) return SNAP_MAXIMIZE;
                if (nearL) return SNAP_LEFT_HALF;
                if (nearR) return SNAP_RIGHT_HALF;
                return SNAP_NONE;
            };

            auto getSnapRect = [](SnapZone zone, const RECT& area) -> RECT {
                RECT r = area;
                switch (zone) {
                case SNAP_LEFT_HALF:  r.right = area.left + (area.right - area.left) / 2; break;
                case SNAP_RIGHT_HALF: r.left  = area.left + (area.right - area.left) / 2; break;
                case SNAP_MAXIMIZE:   break;
                case SNAP_TOP_LEFT:   r.right = area.left + (area.right - area.left) / 2;
                                      r.bottom = area.top + (area.bottom - area.top) / 2; break;
                case SNAP_TOP_RIGHT:  r.left  = area.left + (area.right - area.left) / 2;
                                      r.bottom = area.top + (area.bottom - area.top) / 2; break;
                case SNAP_BOTTOM_LEFT:r.right = area.left + (area.right - area.left) / 2;
                                      r.top   = area.top + (area.bottom - area.top) / 2; break;
                case SNAP_BOTTOM_RIGHT:r.left = area.left + (area.right - area.left) / 2;
                                      r.top  = area.top + (area.bottom - area.top) / 2; break;
                }
                return r;
            };

            RECT workArea;
            workArea.left = (LONG)viewport->WorkPos.x;
            workArea.top = (LONG)viewport->WorkPos.y;
            workArea.right = (LONG)(viewport->WorkPos.x + viewport->WorkSize.x);
            workArea.bottom = g_taskbarRectValid ? g_taskbarRect.top : (LONG)(viewport->WorkPos.y + viewport->WorkSize.y);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("CrossDim WindowChrome", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

            ImDrawList* chromeDraw = ImGui::GetWindowDrawList();
            static HWND dragHwnd = nullptr;
            static ImVec2 dragOffset = ImVec2(0.0f, 0.0f);
            static SnapZone dragSnapZone = SNAP_NONE;
            std::vector<HijackedWindow> alive;
            alive.reserve(g_hijackedWindows.size());

            for (const auto& win : g_hijackedWindows) {
                if (!IsWindow(win.hwnd)) continue;
                if (!IsWindowVisible(win.hwnd)) {
                    alive.push_back(win);
                    continue;
                }
                RECT r; GetWindowRect(win.hwnd, &r);
                float barHeight = 30.0f;
                float btnSize = 14.0f;
                float btnGap = 8.0f;
                float btnPad = 10.0f;
                ImVec2 barMin = ImVec2((float)r.left, (float)r.top);
                ImVec2 barMax = ImVec2((float)r.right, (float)r.top + barHeight);

                chromeDraw->AddRectFilled(barMin, barMax, IM_COL32(250, 250, 252, 235), 6.0f);
                chromeDraw->AddRect(barMin, barMax, IM_COL32(255, 255, 255, 120), 6.0f);

                float ctrlWidth = btnSize * 3.0f + btnGap * 2.0f;
                float dragWidth = (barMax.x - barMin.x) - (ctrlWidth + btnPad * 2.0f);
                if (dragWidth < 20.0f) dragWidth = 20.0f;

                ImGui::PushID(win.hwnd);
                ImGui::SetCursorScreenPos(barMin);
                ImGui::InvisibleButton("WinDrag", ImVec2(dragWidth, barHeight));
                if (ImGui::IsItemActivated()) {
                    dragHwnd = win.hwnd;
                    dragSnapZone = SNAP_NONE;
                    dragOffset = ImVec2(ImGui::GetIO().MousePos.x - barMin.x, ImGui::GetIO().MousePos.y - barMin.y);
                }
                if (dragHwnd == win.hwnd && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    int nx = (int)(ImGui::GetIO().MousePos.x - dragOffset.x);
                    int ny = (int)(ImGui::GetIO().MousePos.y - dragOffset.y);
                    SetWindowPos(win.hwnd, HWND_TOP, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                    dragSnapZone = detectSnapZone((int)ImGui::GetIO().MousePos.x, (int)ImGui::GetIO().MousePos.y, workArea);
                }
                if (dragHwnd == win.hwnd && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (dragSnapZone != SNAP_NONE) {
                        RECT snapR = getSnapRect(dragSnapZone, workArea);
                        SetWindowPos(win.hwnd, HWND_TOP, snapR.left, snapR.top,
                                     snapR.right - snapR.left, snapR.bottom - snapR.top,
                                     SWP_NOZORDER);
                    }
                    dragHwnd = nullptr;
                    dragSnapZone = SNAP_NONE;
                }

                float btnY = barMin.y + (barHeight - btnSize) * 0.5f;
                float x = barMax.x - btnPad - btnSize;

                ImGui::SetCursorScreenPos(ImVec2(x, btnY));
                ImGui::InvisibleButton("WinClose", ImVec2(btnSize, btnSize));
                bool closeHover = ImGui::IsItemHovered();
                ImU32 closeColor = closeHover ? IM_COL32(255, 95, 95, 230) : IM_COL32(255, 120, 120, 210);
                chromeDraw->AddCircleFilled(ImVec2(x + btnSize * 0.5f, btnY + btnSize * 0.5f), btnSize * 0.48f, closeColor);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    ShowWindow(win.hwnd, SW_HIDE);
                    PostMessage(win.hwnd, WM_CLOSE, 0, 0);
                    continue;
                }

                x -= btnGap + btnSize;
                ImGui::SetCursorScreenPos(ImVec2(x, btnY));
                ImGui::InvisibleButton("WinMax", ImVec2(btnSize, btnSize));
                bool maxHover = ImGui::IsItemHovered();
                ImU32 maxColor = maxHover ? IM_COL32(255, 210, 90, 230) : IM_COL32(255, 225, 140, 210);
                chromeDraw->AddRectFilled(ImVec2(x + 2.0f, btnY + 2.0f), ImVec2(x + btnSize - 2.0f, btnY + btnSize - 2.0f), maxColor, 3.0f);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    if (IsZoomed(win.hwnd)) ShowWindow(win.hwnd, SW_RESTORE);
                    else ShowWindow(win.hwnd, SW_MAXIMIZE);
                }

                x -= btnGap + btnSize;
                ImGui::SetCursorScreenPos(ImVec2(x, btnY));
                ImGui::InvisibleButton("WinMin", ImVec2(btnSize, btnSize));
                bool minHover = ImGui::IsItemHovered();
                ImU32 minColor = minHover ? IM_COL32(110, 220, 140, 230) : IM_COL32(140, 235, 165, 210);
                chromeDraw->AddRectFilled(ImVec2(x + 3.0f, btnY + btnSize * 0.5f), ImVec2(x + btnSize - 3.0f, btnY + btnSize * 0.5f + 2.0f), minColor, 1.5f);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    ShowWindow(win.hwnd, SW_MINIMIZE);
                }

                ImGui::PopID();
                alive.push_back(win);
            }

            if (dragHwnd != nullptr && dragSnapZone != SNAP_NONE) {
                RECT snapR = getSnapRect(dragSnapZone, workArea);
                ImVec2 p0 = ImVec2((float)snapR.left, (float)snapR.top);
                ImVec2 p1 = ImVec2((float)snapR.right, (float)snapR.bottom);
                chromeDraw->AddRectFilled(p0, p1, IM_COL32(0, 140, 255, 60), 6.0f);
                chromeDraw->AddRect(p0, p1, IM_COL32(0, 160, 255, 180), 6.0f, 0, 2.0f);
            }

            g_hijackedWindows.swap(alive);
            ImGui::End();
            ImGui::PopStyleVar(3);
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

        if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
            ImGui::Begin("Model Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
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
                for (auto& app : g_myApps) {
                    if (app.IconTexture) app.IconTexture->Release();
                }
                g_myApps.clear();
                ScanDesktopForApps(g_myApps);
                LoadDesktopState(g_myApps);
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
        
        int hitAppIndex = -1;
        float minDistance = 9999.0f;
        for (int i = 0; i < g_myApps.size(); i++) {
            g_myApps[i].IsHovered = false;
            DirectX::BoundingBox box(g_myApps[i].Position, DirectX::XMFLOAT3(0.6f, 0.6f, 0.3f));
            float dist;
            if (box.Intersects(rayOrigin, rayDir, dist)) {
                if (dist < minDistance) { minDistance = dist; hitAppIndex = i; }
            }
        }
        if (hitAppIndex != -1) g_myApps[hitAppIndex].IsHovered = true;

        if (g_leftClicked) {
            if (hitAppIndex != -1) {
                if (GetTickCount() - g_lastClickTime < 400 && g_lastClickedApp == hitAppIndex) {
                    g_previousUiUnlocked = g_uiUnlocked;
                    LaunchAppByPath(g_myApps[hitAppIndex].AppPath.c_str());
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

        if (g_rightClicked && hitAppIndex >= 0) {
            g_rightClickedCubeIndex = hitAppIndex;
            g_rightClicked = false;
        }

        if (g_deleteRequested) {
            g_deleteRequested = false;
            for (int i = (int)g_myApps.size() - 1; i >= 0; --i) {
                if (g_myApps[i].IsSelected) {
                    if (g_myApps[i].IconTexture) g_myApps[i].IconTexture->Release();
                    g_myApps.erase(g_myApps.begin() + i);
                }
            }
            SaveDesktopState(g_myApps);
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
            // Drop-target: if dragging a file over an executable, open with
            if (g_isDragging && g_dropTargetIndex >= 0 && g_dropTargetIndex < (int)g_myApps.size() &&
                g_grabbedAppIndex >= 0 && g_grabbedAppIndex < (int)g_myApps.size()) {
                const auto& target = g_myApps[g_dropTargetIndex];
                const auto& source = g_myApps[g_grabbedAppIndex];
                size_t tlen = target.AppPath.size();
                bool targetIsExe = (tlen > 4 && _wcsicmp(target.AppPath.c_str() + tlen - 4, L".exe") == 0);
                if (targetIsExe && !source.AppPath.empty()) {
                    HINSTANCE res = ShellExecuteW(NULL, L"open", target.AppPath.c_str(),
                                                   source.AppPath.c_str(), NULL, SW_SHOWNORMAL);
                    if ((INT_PTR)res > 32) {
                        LOGW(L"[drop] Open with: %s -> %s", source.AppPath.c_str(), target.AppPath.c_str());
                    }
                }
                g_dropTargetIndex = -1;
            }
            bool wasDragging = g_isDragging || g_isBlankDragging;
            g_grabbedAppIndex = -1; g_isDragging = false; g_isBlankDragging = false;
            g_dropTargetIndex = -1;
            if (wasDragging) SaveDesktopState(g_myApps);
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
            // 将文字位置放在方块正下方 0.75 米处
            appPos = DirectX::XMVectorSubtract(appPos, DirectX::XMVectorSet(0.0f, 0.75f, 0.0f, 0.0f)); 
            DirectX::XMVECTOR clipSpace = DirectX::XMVector3TransformCoord(appPos, viewMatrix * projMatrix);
            
            // 确保图标在摄像机前面且未超出视野
            if (DirectX::XMVectorGetZ(clipSpace) > 0.0f && DirectX::XMVectorGetZ(clipSpace) < 1.0f) {
                float screenX = (DirectX::XMVectorGetX(clipSpace) + 1.0f) * 0.5f * width;
                float screenY = (1.0f - DirectX::XMVectorGetY(clipSpace)) * 0.5f * height;
                
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
                    sprintf_s(openWithText, "Open with %s", g_myApps[g_grabbedAppIndex].AppName.c_str());
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

        if (g_rightClickedCubeIndex >= 0 && g_rightClickedCubeIndex < (int)g_myApps.size()) {
            ImGui::OpenPopup("CubeContextMenu");
        }
        if (ImGui::BeginPopup("CubeContextMenu")) {
            int idx = g_rightClickedCubeIndex;
            if (idx >= 0 && idx < (int)g_myApps.size()) {
                ImGui::Text("%s", g_myApps[idx].AppName.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Launch")) {
                    g_previousUiUnlocked = g_uiUnlocked;
                    LaunchAppByPath(g_myApps[idx].AppPath.c_str());
                }
                if (ImGui::MenuItem("Remove")) {
                    if (g_myApps[idx].IconTexture) g_myApps[idx].IconTexture->Release();
                    g_myApps.erase(g_myApps.begin() + idx);
                    SaveDesktopState(g_myApps);
                    g_rightClickedCubeIndex = -1;
                }
            }
            ImGui::EndPopup();
        }
        g_rightClickedCubeIndex = -1;

        g_leftClicked = false;
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ShutdownAudioEndpointVolume();
    if (comUninit) {
        CoUninitialize();
    }
    if (g_tabHotkeyRegistered) {
        UnregisterHotKey(hwnd, 1);
    }
    UnregisterHotKey(hwnd, 2);
    if (g_hShutdownEvent) { SetEvent(g_hShutdownEvent); CloseHandle(g_hShutdownEvent); g_hShutdownEvent = nullptr; }
    if (g_hHeartbeatEvent) { CloseHandle(g_hHeartbeatEvent); g_hHeartbeatEvent = nullptr; }
    SetSystemTaskbarVisible(true);
    SaveDesktopState(g_myApps);
    CleanupDevice();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}