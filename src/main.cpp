#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <d3d11.h>
#include <DirectXCollision.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <imm.h>
#include <iphlpapi.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "Engine/Camera.h"
#include "Engine/SkyboxRenderer.h"
#include "Engine/CubeRenderer.h"
#include "Engine/TextureLoader.h"
#include "Engine/ModelRenderer.h"
enum CrossDimState {
    STATE_3D_EXPLORE,
    STATE_2D_WORKBENCH
};
CrossDimState g_currentState = STATE_3D_EXPLORE;
bool g_uiUnlocked = false;

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "iphlpapi.lib")

ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
ID3D11DepthStencilView* g_mainDepthStencilView = nullptr;

float g_mouseDeltaX = 0.0f;
float g_mouseDeltaY = 0.0f;

bool g_leftClicked = false;

DWORD g_lastClickTime = 0;
int   g_lastClickedApp = -1;

int   g_grabbedAppIndex = -1;  
bool  g_isDragging = false;    
DWORD g_mouseDownTime = 0;     

bool  g_isBlankDragging = false;
float g_dragStartYaw = 0.0f;
float g_dragStartPitch = 0.0f;
float g_dragCurrYaw = 0.0f;
float g_dragCurrPitch = 0.0f;
float g_dragPrevRawYaw = 0.0f;

struct PendingHijack {
    HWND originalFocus;
    int frameWait;
};
std::vector<PendingHijack> g_pendingHijacks;

IAudioEndpointVolume* g_audioEndpoint = nullptr;

struct EnumData {
    DWORD processId;
    HWND hwnd;
};

DirectX::XMFLOAT2 GetYawPitch(DirectX::XMVECTOR dirVec) {
    DirectX::XMFLOAT3 dir; 
    DirectX::XMStoreFloat3(&dir, dirVec);
    float yaw = atan2(dir.x, dir.z);
    float pitch = asin(dir.y / sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z));
    return {yaw, pitch};
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
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

HWND FindWindowFromProcessId(DWORD pid) {
    EnumData data = { pid, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    return data.hwnd;
}

bool InitAudioEndpointVolume() {
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

void ShutdownAudioEndpointVolume() {
    if (g_audioEndpoint) {
        g_audioEndpoint->Release();
        g_audioEndpoint = nullptr;
    }
}

float GetMasterVolumeLevelScalar() {
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

void GetImeLabel(HWND hwnd, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) return;
    strcpy_s(buffer, bufferSize, "英 ENG");
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;
    if (ImmGetOpenStatus(imc)) {
        strcpy_s(buffer, bufferSize, "中 拼");
    }
    ImmReleaseContext(hwnd, imc);
}

struct NetworkStatus {
    bool connected;
    bool wifi;
    bool ethernet;
};

NetworkStatus GetNetworkStatus() {
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

struct AppCube {
    DirectX::XMFLOAT3 Position; 
    DirectX::XMFLOAT4 BaseColor;
    LPCWSTR AppPath; 
    std::string AppName;
    bool IsHovered;
    bool IsSelected; 
    bool WasSelected;
    ID3D11ShaderResourceView* IconTexture;
};

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

std::vector<AppCube> g_myApps = {
    { {-5.14f, 3.0f, 6.12f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"C:\\Windows\\System32\\control.exe", "控制面板", false, false, false, nullptr },
    { {-2.73f, 3.0f, 7.51f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"C:\\Windows\\System32\\taskmgr.exe", "任务管理器", false, false, false, nullptr },
    { { 0.00f, 3.0f, 8.00f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"C:\\Windows\\explorer.exe",         "文件管理器", false, false, false, nullptr },
    { { 2.73f, 3.0f, 7.51f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"C:\\Windows\\System32\\cmd.exe",     "命令提示符", false, false, false, nullptr },
    { { 5.14f, 3.0f, 6.12f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\Clash Verge\\clash-verge.exe", "Clash", false, false, false, nullptr },
    { {-5.14f, 0.5f, 6.12f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", "Edge 浏览器", false, false, false, nullptr },
    { {-2.73f, 0.5f, 7.51f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",        "Chrome 浏览器", false, false, false, nullptr },
    { { 0.00f, 0.5f, 8.00f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\Microsoft VS Code\\Code.exe",  "VS Code", false, false, false, nullptr },
    { { 2.73f, 0.5f, 7.51f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\洛克王国：世界(2002304)\\洛克王国：世界.exe",    "洛克王国", false, false, false, nullptr },
    { { 5.14f, 0.5f, 6.12f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\Weixin\\Weixin.exe", "微信", false, false, false, nullptr },
    { {-2.73f, -2.0f, 7.51f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\BaiduNetdisk\\BaiduNetdisk.exe", "百度网盘", false, false, false, nullptr },
    { { 0.00f, -2.0f, 8.00f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\QQ\\QQ.exe", "QQ", false, false, false, nullptr },
    { { 2.73f, -2.0f, 7.51f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\Steam\\Steam.exe", "Steam", false, false, false, nullptr },
    { { 5.14f, -2.0f, 6.12f}, {0.3f, 0.3f, 0.3f, 1.0f}, L"D:\\Apps\\Chrome\\Application\\chrome.exe", "Chrome", false, false, false, nullptr }
};

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
    if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
    }

    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (g_currentState == STATE_3D_EXPLORE && !g_uiUnlocked) {
                g_leftClicked = true;
            } 
            else if (g_currentState == STATE_2D_WORKBENCH || g_uiUnlocked) {
                // 让 ImGui 先处理事件
                bool imguiConsumed = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
                // 只有在 ImGui 没有消费事件且用户明确要求时才切换（通过其他方式，如按 TAB 键）
                // 点击不再自动切换，避免误触
                if (!imguiConsumed) {
                    // 点击在 ImGui 窗口外部时的处理可选择性地切换
                    // 目前保留 ImGui 优先权，确保调参不会被中断
                }
                return 0;  // 防止重复处理
            }
            break;
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
        case WM_KEYDOWN: 
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            if (wParam == VK_TAB) {
                g_uiUnlocked = !g_uiUnlocked;
                if (g_uiUnlocked) {
                    while (ShowCursor(TRUE) < 0);
                    ClipCursor(NULL);
                } else {
                    while (ShowCursor(FALSE) >= 0);
                    
                    RECT rect;
                    GetClientRect(hWnd, &rect);
                    MapWindowPoints(hWnd, nullptr, (POINT*)&rect, 2);
                    ClipCursor(&rect);
                    SetCursorPos(rect.left + (rect.right - rect.left) / 2,
                                 rect.top + (rect.bottom - rect.top) / 2);
                }
            }
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
    RAWINPUTDEVICE rid[1];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = 0;
    rid[0].hwndTarget = hwnd;

    if (RegisterRawInputDevices(rid, 1, sizeof(rid[0])) == FALSE) {
        OutputDebugStringW(L"Raw Input 注册失败！\n");
    }

    if (!InitDevice(hwnd)) { CleanupDevice(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }
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
    
    for (auto& app : g_myApps) {
        app.IconTexture = TextureLoader::LoadIconFromExe(g_pd3dDevice, app.AppPath);
    }


    MSG msg; bool done = false;
        while (!done) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        static ULONGLONG lastTick = GetTickCount64();
        ULONGLONG nowTick = GetTickCount64();
        float dt = (nowTick - lastTick) / 1000.0f;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        lastTick = nowTick;

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
            HWND fgHwnd = GetForegroundWindow();
            if (fgHwnd != NULL && fgHwnd != hwnd && fgHwnd != it->originalFocus) {
                char className[256];
                GetClassNameA(fgHwnd, className, 256);
                if (strcmp(className, "Shell_TrayWnd") != 0 && strcmp(className, "Progman") != 0) {
                    LONG style = GetWindowLong(fgHwnd, GWL_STYLE);
                    if ((style & WS_CAPTION) == WS_CAPTION) {
                        OutputDebugStringW(L"[成功] 捕获到目标弹出的窗口！实施扒衣！\n");
                        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
                        SetWindowLong(fgHwnd, GWL_STYLE, style);
                        LONG exStyle = GetWindowLong(fgHwnd, GWL_EXSTYLE);
                        SetWindowLong(fgHwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
                        SetLayeredWindowAttributes(fgHwnd, 0, 230, LWA_ALPHA);
                        RECT winRect; GetWindowRect(hwnd, &winRect);
                        int w = 1100; int h = 750; 
                        int x = winRect.left + (winRect.right - winRect.left - w) / 2;
                        int y = winRect.top + (winRect.bottom - winRect.top - h) / 2;
                        SetWindowPos(fgHwnd, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);

                        it = g_pendingHijacks.erase(it);
                        continue;
                    }
                }
            }
            
            if (it->frameWait > 600) { 
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
            struct TaskbarApp {
                const char* Id;
                const char* Label;
                LPCWSTR AppPath;
                ImU32 Accent;
                TaskbarAction Action;
                TaskbarIconKind IconKind;
                ID3D11ShaderResourceView* Icon;
            };
            static TaskbarApp taskbarApps[] = {
                { "Taskbar.Start",  "Start",  nullptr, IM_COL32(70, 140, 230, 255), TASKBAR_ACTION_START, TASKBAR_ICON_WINDOWS, nullptr },
                { "Taskbar.Search", "Search", nullptr, IM_COL32(120, 170, 255, 255), TASKBAR_ACTION_NONE,  TASKBAR_ICON_SEARCH, nullptr },
                { "Taskbar.Files",  "Files",  L"C:\\Windows\\explorer.exe", IM_COL32(255, 210, 120, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr },
                { "Taskbar.Edge",   "Edge",   L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", IM_COL32(120, 210, 255, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr },
                { "Taskbar.Terminal", "Terminal", L"C:\\Windows\\System32\\cmd.exe", IM_COL32(140, 255, 170, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr },
                { "Taskbar.Code",   "Code",   L"D:\\Apps\\Microsoft VS Code\\Code.exe", IM_COL32(120, 185, 255, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr }
            };
            static int taskbarActive = -1;
            static bool taskbarStartOpen = false;
            static bool taskbarTrayOpen = false;
            static bool taskbarIconsLoaded = false;

            const int taskbarAppCount = (int)(sizeof(taskbarApps) / sizeof(taskbarApps[0]));
            if (!taskbarIconsLoaded) {
                for (int i = 0; i < taskbarAppCount; ++i) {
                    if (taskbarApps[i].IconKind == TASKBAR_ICON_APP && taskbarApps[i].AppPath) {
                        taskbarApps[i].Icon = TextureLoader::LoadIconFromExe(g_pd3dDevice, taskbarApps[i].AppPath);
                    }
                }
                taskbarIconsLoaded = true;
            }
            float iconSize = 28.0f;
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

            char imeText[16];
            GetImeLabel(hwnd, imeText, sizeof(imeText));
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
            ImVec2 imeSize = ImGui::CalcTextSize(imeText);
            ImVec2 chevronSize = ImGui::CalcTextSize("^");
            float rightAreaWidth = chevronSize.x + imeSize.x + timeBlockWidth + trayIconBox * 3.0f + trayGap * 5.0f;

            ImVec2 barPos = ImVec2(
                viewport->WorkPos.x,
                viewport->WorkPos.y + viewport->WorkSize.y - barHeight
            );

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
                if (!appPath) return;
                WCHAR cmdBuffer[MAX_PATH];
                wcscpy_s(cmdBuffer, appPath);
                STARTUPINFOW si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                    HWND currentFocus = GetForegroundWindow();
                    g_pendingHijacks.push_back({ currentFocus, 0 });
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
            };

            auto drawWindowsGlyph = [&](ImVec2 a, ImVec2 b, ImU32 color) {
                float w = b.x - a.x;
                float gap = w * 0.08f;
                float tile = (w - gap) * 0.5f;
                ImVec2 tl = ImVec2(a.x, a.y);
                ImVec2 tr = ImVec2(a.x + tile + gap, a.y);
                ImVec2 bl = ImVec2(a.x, a.y + tile + gap);
                ImVec2 br = ImVec2(a.x + tile + gap, a.y + tile + gap);
                draw->AddRectFilled(tl, ImVec2(tl.x + tile, tl.y + tile), color, 2.0f);
                draw->AddRectFilled(tr, ImVec2(tr.x + tile, tr.y + tile), color, 2.0f);
                draw->AddRectFilled(bl, ImVec2(bl.x + tile, bl.y + tile), color, 2.0f);
                draw->AddRectFilled(br, ImVec2(br.x + tile, br.y + tile), color, 2.0f);
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
                    draw->AddTriangleFilled(boltA, boltB, boltC, IM_COL32(255, 200, 80, 220));
                    draw->AddTriangleFilled(boltB, boltC, boltD, IM_COL32(255, 200, 80, 220));
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
            float iconRowWidth = taskbarAppCount * iconSize + (taskbarAppCount - 1) * iconGap;
            float iconScale = 1.0f;
            if (centerAreaWidth > 0.0f && iconRowWidth > centerAreaWidth) {
                iconScale = centerAreaWidth / iconRowWidth;
            }
            float drawIconSize = iconSize * iconScale;
            float drawIconGap = iconGap * iconScale;
            float drawRowWidth = taskbarAppCount * drawIconSize + (taskbarAppCount - 1) * drawIconGap;
            if (centerAreaWidth < 0.0f) centerAreaWidth = 0.0f;

            ImVec2 cursor = ImVec2(
                centerAreaLeft + (centerAreaWidth - drawRowWidth) * 0.5f,
                p0.y + (barHeight - drawIconSize) * 0.5f
            );
            for (int i = 0; i < taskbarAppCount; ++i) {
                ImVec2 iconPos = ImVec2(cursor.x + i * (drawIconSize + drawIconGap), cursor.y);
                ImGui::SetCursorScreenPos(iconPos);
                ImGui::InvisibleButton(taskbarApps[i].Id, ImVec2(drawIconSize, drawIconSize));

                bool hovered = ImGui::IsItemHovered();
                bool active = (taskbarActive == i);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    if (taskbarApps[i].Action == TASKBAR_ACTION_START) {
                        if (taskbarStartOpen && taskbarActive == i) {
                            taskbarStartOpen = false;
                            taskbarActive = -1;
                        } else {
                            taskbarStartOpen = true;
                            taskbarActive = i;
                        }
                    } else if (taskbarApps[i].Action == TASKBAR_ACTION_LAUNCH) {
                        taskbarStartOpen = false;
                        taskbarActive = i;
                        launchTaskbarApp(taskbarApps[i].AppPath);
                    } else {
                        taskbarStartOpen = false;
                        taskbarActive = i;
                    }
                }

                ImVec2 b0 = ImGui::GetItemRectMin();
                ImVec2 b1 = ImGui::GetItemRectMax();
                float iconRound = 6.0f * (drawIconSize / 28.0f);
                ImU32 base = (hovered || active) ? IM_COL32(255, 255, 255, 180) : IM_COL32(255, 255, 255, 0);
                if ((base >> 24) > 0) draw->AddRectFilled(b0, b1, base, iconRound);

                bool isAppIcon = (taskbarApps[i].IconKind == TASKBAR_ICON_APP);
                ImVec4 accent = ImGui::ColorConvertU32ToFloat4(taskbarApps[i].Accent);
                accent.w = isAppIcon ? (hovered ? 0.08f : 0.04f) : (active ? 0.45f : (hovered ? 0.25f : 0.12f));
                if (accent.w > 0.001f) {
                    draw->AddRectFilled(b0, b1, ImGui::ColorConvertFloat4ToU32(accent), iconRound);
                }

                ImVec2 center = ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
                ImVec2 glyphMin = ImVec2(b0.x + drawIconSize * 0.22f, b0.y + drawIconSize * 0.22f);
                ImVec2 glyphMax = ImVec2(b1.x - drawIconSize * 0.22f, b1.y - drawIconSize * 0.22f);
                if (taskbarApps[i].IconKind == TASKBAR_ICON_WINDOWS) {
                    drawWindowsGlyph(glyphMin, glyphMax, taskbarApps[i].Accent);
                } else if (taskbarApps[i].IconKind == TASKBAR_ICON_SEARCH) {
                    drawSearchGlyph(center, drawIconSize, IM_COL32(30, 40, 60, 210));
                } else {
                    ID3D11ShaderResourceView* iconSrv = taskbarApps[i].Icon;
                    if (iconSrv) {
                        float imgSize = drawIconSize * 0.72f;
                        ImVec2 imgPos = ImVec2(center.x - imgSize * 0.5f, center.y - imgSize * 0.5f);
                        draw->AddImage((ImTextureID)iconSrv, imgPos, ImVec2(imgPos.x + imgSize, imgPos.y + imgSize));
                    } else {
                        ImVec4 glyphAccent = ImGui::ColorConvertU32ToFloat4(taskbarApps[i].Accent);
                        glyphAccent.w = 0.85f;
                        drawAppGlyph(center, drawIconSize, ImGui::ColorConvertFloat4ToU32(glyphAccent));
                    }
                }

                if (active) {
                    ImVec2 dot = ImVec2((b0.x + b1.x) * 0.5f, b1.y + 3.0f);
                    draw->AddCircleFilled(dot, 2.0f, taskbarApps[i].Accent);
                }
            }

            ImU32 trayColor = IM_COL32(20, 30, 50, 220);
            ImU32 trayMuted = IM_COL32(20, 30, 50, 150);
            ImU32 netColor = netConnected ? trayColor : IM_COL32(20, 30, 50, 110);
            float centerY = p0.y + barHeight * 0.5f;
            float timeX = p1.x - paddingX - timeBlockWidth;
            float timeY = centerY - timeBlockHeight * 0.5f;
            draw->AddText(ImVec2(timeX, timeY), trayColor, timeText);
            draw->AddText(ImVec2(timeX, timeY + timeSize.y), trayMuted, dateText);

            float cursorX = timeX - trayGap;
            float batteryH = trayIconBox * 0.55f;
            cursorX -= trayIconBox;
            drawBatteryIcon(ImVec2(cursorX, centerY - batteryH * 0.5f), trayIconBox, batteryH, batteryPercent, batteryCharging, trayColor);

            cursorX -= trayGap + trayIconBox;
            drawVolumeIcon(ImVec2(cursorX + trayIconBox * 0.5f, centerY), trayIconBox, volumeLevel, trayColor);

            cursorX -= trayGap + trayIconBox;
            drawNetBarsIcon(ImVec2(cursorX + trayIconBox * 0.5f, centerY), trayIconBox, netWifi, netConnected, netColor);

            cursorX -= trayGap + imeSize.x;
            draw->AddText(ImVec2(cursorX, centerY - imeSize.y * 0.5f), trayColor, imeText);

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
                static char startSearch[64] = "";
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * scale);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, 6.0f * scale));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##StartSearch", startSearch, sizeof(startSearch));
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);

                ImGui::Spacing();
                ImGui::Text("Pinned");
                ImGui::Separator();

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * scale);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * scale, 8.0f * scale));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.28f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.38f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.48f));
                if (ImGui::BeginTable("StartPinnedApps", 2, ImGuiTableFlags_SizingStretchSame)) {
                    const int quickIndices[] = { 2, 3, 4, 5 };
                    for (int i = 0; i < (int)(sizeof(quickIndices) / sizeof(quickIndices[0])); ++i) {
                        ImGui::TableNextColumn();
                        int appIndex = quickIndices[i];
                        ImGui::PushID(appIndex);
                        if (ImGui::Button(taskbarApps[appIndex].Label, ImVec2(-1.0f, 36.0f * scale))) {
                            taskbarStartOpen = false;
                            taskbarActive = appIndex;
                            launchTaskbarApp(taskbarApps[appIndex].AppPath);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();

                ImGui::End();
                ImGui::PopStyleVar(3);
            }

            if (taskbarTrayOpen) {
                float panelWidth = 240.0f;
                float panelHeight = 150.0f;
                ImVec2 panelPos = ImVec2(
                    p1.x - panelWidth - paddingX,
                    barPos.y - panelHeight - 10.0f
                );
                ImGui::SetNextWindowPos(panelPos);
                ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
                ImGui::SetNextWindowBgAlpha(0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
                ImGui::Begin("CrossDim Tray", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

                ImDrawList* panelDraw = ImGui::GetWindowDrawList();
                ImVec2 q0 = ImGui::GetWindowPos();
                ImVec2 q1 = ImVec2(q0.x + panelWidth, q0.y + panelHeight);
                panelDraw->AddRectFilled(q0, q1, IM_COL32(244, 246, 250, 235), 12.0f);
                panelDraw->AddRect(q0, q1, IM_COL32(255, 255, 255, 120), 12.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
                ImGui::Text("后台运行");
                ImGui::Separator();
                const char* hiddenItems[] = { "OneDrive", "NVIDIA", "Steam" };
                for (int i = 0; i < (int)(sizeof(hiddenItems) / sizeof(hiddenItems[0])); ++i) {
                    ImGui::BulletText("%s", hiddenItems[i]);
                }
                ImGui::PopStyleColor();

                ImGui::End();
                ImGui::PopStyleVar(3);
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
                    WCHAR cmdBuffer[MAX_PATH]; wcscpy_s(cmdBuffer, g_myApps[hitAppIndex].AppPath);
                    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
                    if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                        HWND currentFocus = GetForegroundWindow();
                        g_pendingHijacks.push_back({ currentFocus, 0 });
                        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                        g_currentState = STATE_2D_WORKBENCH;
                        while (ShowCursor(TRUE) < 0); ClipCursor(NULL);
                    }
                    g_lastClickTime = 0;
                } else {
                    g_lastClickTime = GetTickCount();
                    g_lastClickedApp = hitAppIndex;
                    
                    if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                        if (!g_myApps[hitAppIndex].IsSelected) {
                            for (auto& a : g_myApps) a.IsSelected = false;
                        }
                    }
                    g_myApps[hitAppIndex].IsSelected = true;

                    g_grabbedAppIndex = hitAppIndex;
                    g_mouseDownTime = GetTickCount();
                }
            } else {
                for (auto& a : g_myApps) {
                    if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) a.IsSelected = false;
                    a.WasSelected = a.IsSelected;
                }
                g_isBlankDragging = true;
                
                DirectX::XMFLOAT2 startAngle = GetYawPitch(rayDir);
                g_dragStartYaw = startAngle.x;
                g_dragStartPitch = startAngle.y;
                g_dragCurrYaw = g_dragStartYaw;
                g_dragCurrPitch = g_dragStartPitch;
                g_dragPrevRawYaw = startAngle.x;
            }
            g_leftClicked = false; 
        }

        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            if (g_grabbedAppIndex != -1) {
                if (GetTickCount() - g_mouseDownTime > 150) {
                    g_isDragging = true;
                    
                    DirectX::XMVECTOR newPosVec = DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(rayDir, 8.0f));
                    DirectX::XMFLOAT3 targetPos; DirectX::XMStoreFloat3(&targetPos, newPosVec);
                    
                    DirectX::XMFLOAT3 delta = { 
                        targetPos.x - g_myApps[g_grabbedAppIndex].Position.x,
                        targetPos.y - g_myApps[g_grabbedAppIndex].Position.y,
                        targetPos.z - g_myApps[g_grabbedAppIndex].Position.z 
                    };
                    
                    for (auto& a : g_myApps) {
                        if (a.IsSelected) {
                            a.Position.x += delta.x;
                            a.Position.y += delta.y;
                            a.Position.z += delta.z;
                        }
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
                    
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) a.IsSelected = (a.WasSelected || inside);
                    else a.IsSelected = inside;
                }
            }
        } else {
            g_grabbedAppIndex = -1; g_isDragging = false; g_isBlankDragging = false;
        }

        ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
        
        for (auto& app : g_myApps) {
            int hoverState = app.IsSelected ? (app.IsHovered ? 3 : 2) : (app.IsHovered ? 1 : 0);
            DirectX::XMFLOAT3 appScale = {1.2f, 1.2f, 0.6f}; 
            cubeRenderer.Render(g_pd3dDeviceContext, viewMatrix * projMatrix, app.Position, appScale, app.BaseColor, app.IconTexture, camera.Position, hoverState, viewMatrix);
            
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
    CleanupDevice();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}