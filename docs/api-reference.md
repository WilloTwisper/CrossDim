# CrossDim — API Reference

## 1. Engine Layer (src/Engine)

### Camera (Camera.h)
```cpp
class Camera {
    XMFLOAT3 Position;        // world position
    XMFLOAT3 Rotation;        // pitch (x), yaw (y), roll (z) — radians
    XMFLOAT3 GetForward() const;
    void Update();            // recompute forward/right/up from Rotation
    void Rotate(float dx, float dy, float sensitivity);
    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetProjectionMatrix(float fov, float aspect, float nearZ, float farZ) const;
};
```
**Note**: forward = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw)) — left-handed, yaw 0 = +Z.

### SkyboxRenderer (SkyboxRenderer.h)
```cpp
class SkyboxRenderer {
    bool Initialize(ID3D11Device* device);
    void Cleanup();
    void Render(ID3D11DeviceContext* ctx, XMMATRIX invViewProj, XMFLOAT3 cameraPos);
};
```
**Note**: takes INVERSE view-proj, not separate matrices.

### CubeRenderer (CubeRenderer.h)
```cpp
class CubeRenderer {
    bool Initialize(ID3D11Device* device);
    void Cleanup();
    void Render(
        ID3D11DeviceContext* ctx,
        XMMATRIX viewProj,            // combined
        XMFLOAT3 position,            // world position
        XMFLOAT3 scale,               // per-axis scale
        XMFLOAT4 color,               // base color
        ID3D11ShaderResourceView* texture,  // icon texture (nullable)
        XMFLOAT3 cameraPos,           // for billboarding
        int hoverState,               // 0=idle,1=hover,2=selected,3=sel+hover,5=canvas,6=rod
        XMMATRIX viewMatrix,          // for rim light
        float sphereRadius,           // layout sphere radius
        float spinAngle = 0,          // Y rotation
        float orbitAngle = 0,         // X rotation
        float tiltAngle = 0);         // Z rotation
};
```

### ModelRenderer (ModelRenderer.h)
```cpp
class ModelRenderer {
    bool Initialize(ID3D11Device* device);
    void Cleanup();
    void LoadModelAsync(const std::string& path);  // spawns thread, joined in Cleanup
    bool IsLoading() const;
    bool HasModel() const;
    void Render(ID3D11DeviceContext* ctx, XMMATRIX viewProj,
                XMFLOAT3 position, XMFLOAT3 rotation, float scale,
                XMFLOAT4 matParams, XMFLOAT4 tint, ...);
};
```

### ObjLoader (ObjLoader.h)
```cpp
struct ObjMesh { /* vertices, normals, uvs, indices, material */ };
bool LoadObjFile(const std::string& path, ObjMesh& out);
bool SaveCdMesh(const ObjMesh& mesh, const std::string& path);
bool LoadCdMesh(const std::string& path, ObjMesh& out);
```

### Logger (Logger.h) — header-only
```cpp
class Logger {
    static Logger& Instance();
    void Init(const std::string& logPath = "");
    void Shutdown();
    void Log(const char* fmt, ...);
    void LogW(const wchar_t* fmt, ...);
};
#define LOG(fmt, ...)   Logger::Instance().Log(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...)  Logger::Instance().LogW(fmt, ##__VA_ARGS__)
```

### TextureLoader (TextureLoader.h) — header-only
```cpp
namespace TextureLoader {
    ID3D11ShaderResourceView* LoadIconFromExe(ID3D11Device* device, const wchar_t* exePath);
    ID3D11ShaderResourceView* LoadIconFromHandle(ID3D11Device* device, HICON hIcon);
    // LoadIconFromExe: ExtractIconEx → create texture → destroy icon
    // Note: may leak icon handle on failure paths (bug #2)
}
```

### TrayProxy (TrayProxy.h) — header-only
```cpp
struct TrayIconEntry { std::wstring tooltip; int commandId; std::wstring exePath; };
HWND FindTrayToolbar();                       // FindWindow chain
std::vector<TrayIconEntry> QueryTrayIcons();  // TB_GETBUTTON cross-process
```

## 2. Shell Layer (src/Shell)

### DesktopManager.h — header-only
```cpp
struct AppCube {
    XMFLOAT3 Position;
    XMFLOAT4 BaseColor;
    std::wstring AppPath;
    std::string AppName;
    bool IsHovered, IsSelected, WasSelected;
    ID3D11ShaderResourceView* IconTexture;
};
inline std::vector<AppCube> g_myApps;   // active desktop/folder cubes

std::wstring ResolveShortcutTarget(LPCWSTR lnkPath);  // IShellLink
std::string WcsToUtf8(const std::wstring& wstr);
bool SearchMatch(const char* filter, const std::string& label, const std::wstring& path);
void ScanDesktopForApps(std::vector<AppCube>& out);    // user + common desktop
void ScanFolderForApps(const std::wstring& path, std::vector<AppCube>& out);
void SaveDesktopState(const std::vector<AppCube>& apps, int desktopIndex = 0);
void LoadDesktopState(std::vector<AppCube>& apps, int desktopIndex = 0);
void LoadIconsForApps(std::vector<AppCube>& apps);
```

### WindowManager.h — header-only
```cpp
struct PendingHijack { HWND originalFocus; int frameWait; DWORD processId; };
struct HijackedWindow { HWND hwnd; };
struct RunningWindow { HWND hwnd; DWORD pid; std::wstring path; std::wstring title; };

inline std::vector<PendingHijack> g_pendingHijacks;
inline std::vector<HijackedWindow> g_hijackedWindows;
inline std::unordered_map<std::wstring, SRV*> g_taskbarIconCache;       // leak risk
inline std::unordered_map<HWND, SRV*> g_taskbarWindowIconCache;         // pruned
inline std::unordered_map<HWND, int> g_taskbarDynamicOrder;
inline int g_taskbarDynamicOrderCounter;

XMFLOAT2 GetYawPitch(XMVECTOR dir);
HWND FindWindowFromProcessId(DWORD pid);
std::wstring GetProcessPath(DWORD pid);
bool IsTaskbarWindowCandidate(HWND hwnd, HWND exclude);
std::vector<RunningWindow> EnumerateRunningWindows(HWND exclude);
bool IsSamePath(const std::wstring& a, const std::wstring& b);
HWND FindRunningHwnd(const std::wstring& appPath);
ID3D11ShaderResourceView* GetTaskbarIcon(ID3D11Device* device, const std::wstring& path);
void SetSystemTaskbarVisible(bool visible);
HICON GetWindowBestIcon(HWND hwnd);
ID3D11ShaderResourceView* GetWindowIconTexture(ID3D11Device* device, HWND hwnd, const std::wstring& path);
```

### SystemInfo.h — header-only
```cpp
inline IAudioEndpointVolume* g_audioEndpoint;

bool InitAudioEndpointVolume();       // COM, eRender/eConsole
void ShutdownAudioEndpointVolume();
float GetMasterVolumeLevelScalar();   // 0..1
void GetImeLabel(HWND hwnd, char* buf, size_t size);
bool GetImeOpenStatus(HWND hwnd);
void SetImeOpenStatus(HWND hwnd, bool open);
bool IsChineseImeLayout();
void ActivateImeLayout(HWND hwnd, LPCWSTR klid, bool open);
struct NetworkStatus { bool connected, wifi, ethernet; };
NetworkStatus GetNetworkStatus();     // GetAdaptersAddresses
```

## 3. main.cpp — Core Functions

### State & Navigation
```cpp
void LaunchAppByPath(LPCWSTR appPath);            // CreateProcess/ShellExecute + hijack
void EnterFolder(const std::wstring& path, Camera& cam);   // push stack + portal
void ExitFolderToParent(Camera& cam);             // pop one level
void ExitFolderToRoot(Camera& cam);               // pop all → desktop
void MakeFolderWindowed(Camera& cam);             // max → portal window
void MakeFolderMaximized(Camera& cam);            // portal → full screen
void SwitchToDesktop(int slotIdx);                // virtual desktop
void CompleteDesktopSwitch();                     // transition midpoint
void CreateNewDesktop();                          // +1 desktop
void CloseDesktop(int slotIdx);                   // -1 desktop
```

### Render Helpers
```cpp
bool IsPathDirectory(const std::wstring& path);   // GetFileAttributes check
void LoadIconsForApps(std::vector<AppCube>& apps);// (in DesktopManager.h)
```

## 4. Global State (main.cpp, ~80 vars)

### State Machine
| Var | Type | Purpose |
|---|---|---|
| g_currentState | CrossDimState | 3D/2D mode |
| g_uiUnlocked | bool | cursor visible + ImGui interactive |
| g_previousUiUnlocked | bool | saved before hijack |

### D3D / Window
| Var | Type | Purpose |
|---|---|---|
| g_pd3dDevice / g_pd3dDeviceContext | ID3D11* | D3D11 device/context |
| g_pSwapChain | IDXGISwapChain | swap chain |
| g_mainRenderTargetView / g_mainDepthStencilView | ID3D11*View | backbuffer/depth |
| g_mainHwnd | HWND | main window |
| g_taskbarRect / g_taskbarRectValid | RECT/bool | taskbar bounds |
| g_systemTaskbarHidden | bool | Explorer taskbar state |
| g_hHeartbeatEvent / g_hShutdownEvent | HANDLE | watchdog events |

### Input
| Var | Type | Purpose |
|---|---|---|
| g_mouseDeltaX / g_mouseDeltaY | float | raw input accumulation |
| g_leftClicked | bool | frame click latch |
| g_ctrlHeld / g_lButtonHeld | bool | key/button state |
| g_lastClickTime / g_lastClickedApp | DWORD/int | double-click detect |
| g_grabbedAppIndex / g_isDragging | int/bool | cube drag state |
| g_dropTargetIndex | int | drop-target detection |
| g_rightClickedCubeIndex / g_rightClicked | int/bool | context menu |
| g_deleteRequested | bool | Delete key latch |
| g_isBlankDragging + drag angles | bool/floats | marquee state |
| g_searchFilter | char[64] | search box |
| g_dragDistance | float | ray drag distance |

### Virtual Desktop
| Var | Type | Purpose |
|---|---|---|
| g_activeDesktopIndex | int | current desktop slot |
| g_desktopSlots | vector<int> | active desktop IDs |
| g_nextDesktopId | int | ID counter |
| g_targetDesktopIndex / g_desktopTransition | int/float | transition |
| g_desktopWindows | vector<vector<HijackedWindow>> | per-desktop windows |
| g_showDesktopOverview | bool | Task View state |

### Folder (Portal)
| Var | Type | Purpose |
|---|---|---|
| g_folderStack | vector<FolderState> | navigation stack |
| g_isInFolder | bool | portal active |
| g_isFolderMaximized | bool | full vs windowed |
| g_folderWindowRect | RECT | portal window bounds |
| g_folderCubes | vector<AppCube> | folder content |
| g_folderCamera | Camera | portal view camera |
| g_desktopBackupCubes | vector<AppCube> | desktop saved on enter |
| g_folderTransitionT | float | camera transition |
| g_isDraggingFolderWin | bool | portal drag |

### Switcher
| Var | Type | Purpose |
|---|---|---|
| g_showWindowSwitcher | bool | Alt+Tab overlay |
| g_switcherSelected | int | selected card |
| g_switcherWindows | vector<pair<HWND, wstring>> | window list |

## 5. Calling Conventions

1. **All functions are `static` or header-inline** (single TU model)
2. **No RAII for D3D resources** — manual Release() everywhere
3. **Camera passed by reference** to state functions (it lives in wWinMain)
4. **No threads** except ModelRenderer loader (joined on Cleanup)
5. **COM initialized once** (CoInitializeEx in wWinMain)
