# CrossDim — Comprehensive Technical Analysis

## Table of Contents
1. [main.cpp](#maincpp---3126-lines)
2. [Camera.cpp / Camera.h](#cameracpp--camerah)
3. [SkyboxRenderer.cpp / SkyboxRenderer.h](#skyboxrenderercpp--skyboxrendererh)
4. [CubeRenderer.cpp / CubeRenderer.h](#cuberenderercpp--cuberendererh)
5. [ModelRenderer.cpp / ModelRenderer.h](#modelrenderercpp--modelrendererh)
6. [ObjLoader.cpp / ObjLoader.h](#objloadercpp--objloaderh)
7. [Logger.h](#loggerh)
8. [TextureLoader.h](#textureloaderh)
9. [TrayProxy.h](#trayproxyh)
10. [DesktopManager.h](#desktopmanagerh)
11. [WindowManager.h](#windowmanagerh)
12. [SystemInfo.h](#systeminfoh)
13. [AGENTS.md](#agentsmd)
14. [tasks.json](#tasksjson)
15. [Global Dependencies Map](#global-dependencies-map)
16. [Bug Catalog](#bug-catalog)
17. [Optimization Opportunities](#optimization-opportunities)
18. [State Machine Diagrams](#state-machine-diagrams)

---

# main.cpp — 3126 lines

## 1. Includes & Pragmas (lines 1–59)
```cpp
#ifndef _WIN32_WINNT → #define _WIN32_WINNT 0x0600
```
- **Windows**: `<winsock2.h>`, `<ws2tcpip.h>`, `<windows.h>`, `<windowsx.h>`, `<dwmapi.h>`
- **D3D**: `<d3d11.h>`, `<DirectXCollision.h>`
- **STL**: `<string>`, `<vector>`, `<cstdio>`, `<cstring>`, `<cmath>`, `<algorithm>`, `<unordered_map>`, `<cwchar>`
- **COM**: `<objbase.h>`; Audio: `<mmdeviceapi.h>`, `<endpointvolume.h>`
- **IME**: `<imm.h>`; Network: `<iphlpapi.h>`; Shell: `<shlobj.h>`

```
#define MOD_NOREPEAT 0x4000  // line 27
```

**Project headers** (lines 30–42):
```cpp
#include "imgui.h", "backends/imgui_impl_win32.h", "backends/imgui_impl_dx11.h"
#include "Engine/Camera.h", "Engine/SkyboxRenderer.h", "Engine/CubeRenderer.h"
#include "Engine/TextureLoader.h", "Engine/ModelRenderer.h", "Engine/Logger.h"
#include "Engine/TrayProxy.h"
#include "Shell/DesktopManager.h", "Shell/WindowManager.h", "Shell/SystemInfo.h"
```

**Linker pragmas** (lines 51–59): `d3d11.lib`, `user32.lib`, `ole32.lib`, `imm32.lib`, `iphlpapi.lib`, `dwmapi.lib`, `shell32.lib`, `advapi32.lib`. Subsystem: `windows`.

---

## 2. CrossDimState Enum & State Globals (lines 43–49)
```cpp
enum CrossDimState { STATE_3D_EXPLORE, STATE_2D_WORKBENCH };  // line 43
CrossDimState g_currentState = STATE_3D_EXPLORE;  // line 47
bool g_uiUnlocked = false;                        // line 48 — true = cursor visible + ImGui interactive
bool g_previousUiUnlocked = false;                // line 49 — saved before hijack transition
```
**State transitions**:
- `STATE_3D_EXPLORE → STATE_2D_WORKBENCH`: via `LaunchAppByPath()` (after `CreateProcess`), or Tab hotkey
- `STATE_2D_WORKBENCH → STATE_3D_EXPLORE`: auto-return when `g_hijackedWindows.empty() && g_pendingHijacks.empty()`, or force-clear after 600 stuck frames
- `g_uiUnlocked`: toggled by Tab/VK_TAB (hotkey WM_HOTKEY wParam=1, or WM_KEYDOWN VK_TAB fallback)

**BUG**: `g_previousUiUnlocked` is set in `LaunchAppByPath` (line 425) but that function is also called indirectly when an app launches through other paths (Start Menu tile, context menu "Launch") — those callers also set `g_previousUiUnlocked = g_uiUnlocked` before calling `LaunchAppByPath`. This double-set is redundant but not harmful. However, if `LaunchAppByPath` is called from `EnterFolder` (line 2568 in portal section), `g_previousUiUnlocked` is NOT set, meaning the auto-return state restoration may be incorrect.

---

## 3. D3D Globals & Window Globals (lines 61–72)
| Variable | Type | Description |
|---|---|---|
| `g_pd3dDevice` | `ID3D11Device*` | D3D11 device (line 61) |
| `g_pd3dDeviceContext` | `ID3D11DeviceContext*` | Immediate context (line 62) |
| `g_pSwapChain` | `IDXGISwapChain*` | Swap chain with 4x MSAA (line 63) |
| `g_mainRenderTargetView` | `ID3D11RenderTargetView*` | Main RTV (line 64) |
| `g_mainDepthStencilView` | `ID3D11DepthStencilView*` | 4x MSAA depth-stencil (line 65) |
| `g_mainHwnd` | `HWND` | Fullscreen WS_POPUP window (line 66) |
| `g_taskbarRect` | `RECT` | Current taskbar bounding rect (line 67) |
| `g_taskbarRectValid` | `bool` | Set true when taskbar drawn (line 68) |
| `g_systemTaskbarHidden` | `bool` | Set by `SetSystemTaskbarVisible` (line 69) |
| `g_tabHotkeyRegistered` | `bool` | Whether hotkey 1 registered (line 70) |
| `g_hHeartbeatEvent` | `HANDLE` | Named event `Global\CrossDim_Heartbeat` (line 71) |
| `g_hShutdownEvent` | `HANDLE` | Named event `Global\CrossDim_Shutdown` (line 72) |

---

## 4. Input State (lines 74–107)
| Variable | Type | Description |
|---|---|---|
| `g_mouseDeltaX/Y` | `float` | Accumulated raw mouse deltas (line 74–75) |
| `g_folderCamDX/DY` | `float` | Folder camera mouse deltas (line 76–77) |
| `g_leftClicked` | `bool` | Left click detected this frame (line 79) |
| `g_ctrlHeld` | `bool` | Message-driven Ctrl state (line 80) |
| `g_lButtonHeld` | `bool` | Message-driven left button state (line 81) |
| `g_lastClickTime` | `DWORD` | For double-click detection (line 83) |
| `g_lastClickedApp` | `int` | Index of last clicked app (line 84) |
| `g_grabbedAppIndex` | `int` | Currently dragged cube index (line 86) |
| `g_isDragging` | `bool` | Drag in progress after 150ms hold (line 87) |
| `g_mouseDownTime` | `DWORD` | When grab started (line 88) |
| `g_dropTargetIndex` | `int` | Closest non-selected cube during drag (line 89) |
| `g_rightClickedCubeIndex` | `int` | For context menu (line 90) |
| `g_rightClicked` | `bool` | Right-click flag (line 91) |
| `g_deleteRequested` | `bool` | Delete key pressed (line 92) |
| `g_exitFolderRequested` | `bool` | Exit folder key (line 93) |
| `g_showWindowSwitcher` | `bool` | Alt+Tab overlay active (line 94) |
| `g_switcherSelected` | `int` | Selected window in switcher (line 95) |
| `g_switcherWindows` | `vector<pair<HWND,wstring>>` | Window list for switcher (line 96) |
| `g_isBlankDragging` | `bool` | Marquee selection active (line 98) |
| `g_dragStart/CurrYaw/Pitch` | `float` | Marquee selection angles (lines 99–102) |
| `g_dragPrevRawYaw` | `float` | Previous raw yaw for delta calc (line 103) |
| `g_fpsCurrent` | `float` | Smoothed FPS (line 105) |
| `g_searchFilter` | `char[64]` | Search input buffer (line 106) |
| `g_dragDistance` | `float` | Distance from camera during drag (line 107) |

---

## 5. Pending Hijacks & Hijacked Windows
Defined in `WindowManager.h` (lines 15–31):
```cpp
struct PendingHijack { HWND originalFocus; int frameWait; DWORD processId; };
inline vector<PendingHijack> g_pendingHijacks;
struct HijackedWindow { HWND hwnd; };
inline vector<HijackedWindow> g_hijackedWindows;
inline unordered_map<wstring, ID3D11ShaderResourceView*> g_taskbarIconCache;
inline unordered_map<HWND, ID3D11ShaderResourceView*> g_taskbarWindowIconCache;
inline unordered_map<HWND, int> g_taskbarDynamicOrder;
inline int g_taskbarDynamicOrderCounter = 1;
```
- `g_pendingHijacks`: queue of processes waiting for window creation
- `g_hijackedWindows`: actively managed windows (stripped of caption, resized)
- Cache pruning happens every 60 frames (lines 876–893)

---

## 6. Virtual Desktop State (lines 109–209)
| Variable | Type | Description |
|---|---|---|
| `g_activeDesktopIndex` | `int` | Current desktop slot (line 110) |
| `g_desktopSlots` | `vector<int>` | Desktop IDs (line 111) |
| `g_nextDesktopId` | `int` | Auto-incrementing ID (line 112) |
| `g_targetDesktopIndex` | `int` | Transition target (-1 = no transition, line 113) |
| `g_desktopTransition` | `float` | 0→1 transition progress (line 114) |
| `g_desktopTransitionSpeed` | `float` | 4.0 (line 115) |
| `g_showDesktopOverview` | `bool` | Task View overlay (line 116) |
| `g_desktopWindows` | `vector<vector<HijackedWindow>>` | Per-desktop window cache (line 117) |

**Functions**:
- `GetDesktopSlotCount()` → `(int)g_desktopSlots.size()` (line 119)
- `SwitchToDesktop(int slotIdx)` (lines 121–132): hides current windows, saves state, sets transition
- `CompleteDesktopSwitch()` (lines 134–150): clears/rebuilds g_myApps, restores window visibility for target desktop
- `CreateNewDesktop()` (lines 152–172): increments ID, saves old desktop, clears apps, scans for new
- `CloseDesktop(int slotIdx)` (lines 174–209): erases slot, handles active-index adjustment, shows fallback windows

**BUG**: `CloseDesktop` line 187 sets `fallback = (slotIdx > 0) ? slotIdx - 1 : 1;` — if `slotIdx == 0`, fallback jumps to index 1, which may be out of bounds if only 2 desktops exist and we're closing 0. The subsequent code at line 195 adjusts `g_activeDesktopIndex` after erase, which partially mitigates this but the fallback logic is technically dead code (fallback is computed but never used after the adjustment code).

**BUG**: `CreateNewDesktop` line 165 hardcodes `desktop_0` file deletion deletion is not present — only `CloseDesktop` deletes the `.cddesk` file. `CreateNewDesktop` saves the old desktop state but does not clean up the new slot if it was previously used.

---

## 7. Spatial Folder State (lines 211–389)
| Variable | Type | Description |
|---|---|---|
| `FolderState` | struct | `folderPath`, `displayName`, `cubes`, `cameraPos`, `cameraRot` (lines 212–218) |
| `g_folderStack` | `vector<FolderState>` | Navigation stack (line 219) |
| `g_isInFolder` | `bool` | Whether viewing a folder (line 220) |
| `g_isFolderMaximized` | `bool` | Fullscreen vs windowed portal (line 221) |
| `g_folderWindowRect` | `RECT` | Portal window rect {100, 80, 1300, 850} (line 222) |
| `g_folderCubes` | `vector<AppCube>` | Portal cube set (line 223) |
| `g_folderCamera` | `Camera` | Independent folder camera (line 224) |
| `g_isDraggingFolderWin` | `bool` | Dragging portal window (line 225) |
| `g_folderDragOff` | `ImVec2` | Drag offset (line 226) |
| `g_folderMouseLocked` | `bool` | Portal cursor locked (line 227) |
| `g_folderTransitionT` | `float` | Maximize transition (line 228) |
| `g_folderFrom/ToPos/Rot` | `XMFLOAT3` | Transition endpoints (lines 229–230) |
| `g_desktopBackupCubes` | `vector<AppCube>` | Desktop backup (line 231) |
| `g_desktopBackupCamPos/Rot` | `XMFLOAT3` | Desktop camera backup (line 232) |

**Functions**:
- `IsPathDirectory(const wstring&)` (lines 234–237): `GetFileAttributesW` + `FILE_ATTRIBUTE_DIRECTORY` check
- `EnterFolder(const wstring& path, Camera& camera)` (lines 239–289): backs up desktop, scans folder, creates `FolderState`, enters windowed portal mode
- `ExitFolderToParent(Camera& camera)` (lines 291–323): pops stack, restores cubes/camera, rescans parent
- `ExitFolderToRoot(Camera& camera)` (lines 325–341): clears entire stack, restores desktop
- `MakeFolderWindowed(Camera& mainCam)` (lines 343–364): converts maximized folder to portal window
- `MakeFolderMaximized(Camera& mainCam)` (lines 366–389): converts portal to fullscreen with smoothstep transition

**BUG**: `EnterFolder` line 273 sets `fs.cubes = newCubes;` then nulls icons — but the same `newCubes` have their icons loaded at line 265 and again at line 279. The stack copy at line 273 has null icons (set at line 273), which is correct for storage. However, `LoadIconsForApps(g_folderCubes)` at line 279 reloads icons that were already loaded at line 265 — a redundant GPU load.

**BUG**: `EnterFolder` line 270 iterates `wchar_t ch : fn` and casts to `(char)ch` — this corrupts non-ASCII characters in folder names.

---

## 8. Model Debug Globals (lines 391–412)
| Variable | Type | Description |
|---|---|---|
| `g_modelPosition` | `XMFLOAT3` | `{-2.45, -2.15, 9.3}` (line 392) |
| `g_modelRotation` | `XMFLOAT3` | `{-0.1, 1.0, -0.11}` pitch/yaw/roll (line 394) |
| `g_modelScale` | `float` | 10.0 (line 395) |
| `g_modelPivot` | `XMFLOAT3` | Auto-set from model center (line 396) |
| `g_modelMatParams` | `XMFLOAT4` | {Ambient, Diffuse, RimIntensity, RimSharpness} (line 397) |
| `g_modelColorTint` | `XMFLOAT4` | Color tint (line 398) |
| `g_modelUseIndependentProj` | `bool` | Independent model FOV (line 399) |
| `g_modelFov` | `float` | 55.0 (line 400) |
| `g_modelFovSnapWhenRotate` | `bool` | Snap FOV during rotation (line 401) |
| `g_modelFovSnapSpeed` | `float` | 12.0 (line 402) |
| `g_modelFovReturnSpeed` | `float` | 4.0 (line 403) |
| `g_modelFovCurrent` | `float` | Smoothed FOV (line 404) |
| `g_modelFovRotateHold` | `float` | Hold time after rotation stops (line 405) |
| `g_modelFovRotateHoldTime` | `float` | 0.12s (line 406) |
| `g_modelFovRotateDeadzone` | `float` | 0.2 (line 407) |
| `g_modelRotateAccum` | `float` | Accumulated mouse magnitude (line 408) |
| `g_modelRotateAccumThreshold` | `float` | 6.0 trigger threshold (line 409) |
| `g_modelRotateAccumDecay` | `float` | 10.0 decay rate (line 410) |
| `g_modelLockScreenPos` | `bool` | Lock model screen position (line 411) |
| `g_pivotAutoSet` | `bool` | Pivot captured from model (line 412) |

---

## 9. Static Functions Before wWinMain

### `LaunchAppByPath(LPCWSTR appPath)` — lines 414–436
- If `.exe`: `CreateProcessW` → pushes to `g_pendingHijacks`, transitions to `STATE_2D_WORKBENCH`
- Else: `ShellExecuteW` (non-exe files, folders open via default handler)
- Sets `g_previousUiUnlocked = g_uiUnlocked` and forces `g_currentState = STATE_2D_WORKBENCH`

### `ImGui_ImplWin32_WndProcHandler` — line 438
- External declaration for ImGui's Win32 message handler

---

## 10. InitDevice / CleanupDevice (lines 440–491)

### `InitDevice(HWND hWnd)` — lines 440–483
- Creates `DXGI_SWAP_CHAIN` with 4x MSAA (`SampleDesc.Count = 4`), `DXGI_SWAP_EFFECT_DISCARD`
- Creates render target view from back buffer
- Creates depth-stencil (D24_UNORM_S8_UINT, 4x MSAA)
- Creates alpha-blend state (`SRC_ALPHA` / `INV_SRC_ALPHA`)
- **NOTE**: The 4x MSAA swap chain requires all render targets and depth buffers to match the sample count. The depth-stencil is manually created with `Count=4`, which is correct.

### `CleanupDevice()` — lines 485–491
- Releases depth-stencil, RTV, swap chain, context, device (in that order)

---

## 11. WndProc (lines 494–771)

### Hotkey Cases (lines 516–565)
| wParam | Hotkey | Action |
|---|---|---|
| 1 | Tab (MOD_NOREPEAT) | Toggle `g_uiUnlocked` |
| 2 | Ctrl+Shift+Esc | Launch `taskmgr.exe` |
| 3 | Ctrl+Shift+D | Dump all cube positions to log |
| 4 | Ctrl+Shift+T | Toggle desktop overview |
| 5 | Ctrl+Tab | Open window switcher overlay |

Line 562: VK_TAB in WM_KEYDOWN (fallback when hotkey not registered) — toggles `g_uiUnlocked`.

### Input tracking (lines 567–569)
```cpp
WM_KEYDOWN VK_CONTROL → g_ctrlHeld = true;
WM_KEYUP VK_CONTROL   → g_ctrlHeld = false;
WM_LBUTTONUP          → g_lButtonHeld = false;
```

### ImGui input filtering (lines 571–580)
- If `STATE_2D_WORKBENCH || g_uiUnlocked`, pass to `ImGui_ImplWin32_WndProcHandler`
- If ImGui handled it (and not WM_LBUTTONDOWN), return true (except Shift key passthrough)

### WM_NCHITTEST (lines 583–607)
- Returns `HTCLIENT` when over taskbar or hijacked window chrome buttons
- Returns `HTTRANSPARENT` when over hijacked window content area — passes clicks through to the actual window

### WM_DROPFILES (lines 609–641)
- Handles external drag-drop onto the CrossDim window
- Creates `AppCube` with icon from exe or `SHGetFileInfoW`, pushes to `g_myApps`

### WM_LBUTTONDOWN (lines 642–669)
- Sets `g_leftClicked = true` if:
  - `STATE_3D_EXPLORE` (always)
  - `STATE_2D_WORKBENCH` but not over ImGui and not over hijacked window
- Returns 0 to prevent double processing

### WM_MOUSEWHEEL (lines 675–683)
- During drag, adjusts `g_dragDistance` (3.0–20.0 range)

### WM_INPUT (lines 684–696)
- Only in `STATE_3D_EXPLORE` with cursor locked
- Accumulates raw mouse deltas into `g_mouseDeltaX/Y`

### WM_RBUTTONDOWN/UP (lines 697–704)
- Sets/clears `g_rightClicked` flag

### WM_KEYDOWN (lines 705–755)
- Window switcher: Tab cycles, Esc cancels
- VK_ESCAPE: `PostQuitMessage(0)`
- Ctrl+Alt+number: desktop switch
- Ctrl+Alt+Arrow: window snapping (left/right/up/down/maximize)

### WM_KEYUP (lines 757–766)
- Ctrl release in window switcher: activates selected window
- Ctrl release: `g_ctrlHeld = false`

### WM_DESTROY (line 768)
- `PostQuitMessage(0)`

---

## 12. wWinMain Init Section (lines 773–864)

**COM Init** (lines 774–779):
```
CoInitializeEx(APARTMENTTHREADED) → InitAudioEndpointVolume()
```

**Window Creation** (lines 781–791):
- `WS_POPUP | WS_VISIBLE`, fullscreen size, class name `"CrossDimShell"`
- `g_mainHwnd = hwnd`

**Hotkey Registration** (lines 792–796):
```cpp
RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_TAB);        // Toggle UI
RegisterHotKey(hwnd, 2, MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT, VK_ESCAPE); // Task Manager
RegisterHotKey(hwnd, 3, MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT, 'D');       // Dump cubes
RegisterHotKey(hwnd, 4, MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT, 'T');       // Desktop overview
RegisterHotKey(hwnd, 5, MOD_CONTROL|MOD_NOREPEAT, VK_TAB);              // Window switcher
```

**System Setup** (lines 797–830):
- `Logger::Instance().Init()` — creates `logs/crossdim_YYYYMMDD_HHMMSS.log`
- `SetSystemTaskbarVisible(false)` — hides Explorer taskbar
- `DragAcceptFiles(hwnd, TRUE)` — enable drag-drop
- `RegisterRawInputDevices` — mouse raw input
- `InitDevice(hwnd)` — D3D11 setup
- `CreateEventW` — heartbeat + shutdown events
- Launch `watchdog.exe` via `CreateProcessW`

**ImGui Init** (lines 832–846):
- MS YaHei font (`msyh.ttc`) with `GetGlyphRangesChineseFull()`
- Dark style

**Renderer Init** (lines 848–861):
```cpp
Camera camera;  // default position (0, 1.5, 0)
CubeRenderer cubeRenderer;
SkyboxRenderer skybox;
ModelRenderer modelRenderer;
// all initialized with g_pd3dDevice
modelRenderer.LoadModelAsync("assets/bloom_high/bloom_high.obj");
```

**Desktop Init** (lines 859–864):
```cpp
ScanDesktopForApps(g_myApps);
LoadDesktopState(g_myApps);  // restores saved positions
LoadIconsForApps(g_myApps);
g_desktopSlots = { 0 };
g_desktopWindows = { {} };
g_activeDesktopIndex = 0;
```

---

## 13. Render Loop — Frame by Frame (lines 868–3099)

### 13.1 Prune Caches (lines 876–893)
Every 60 frames:
- Remove dead `HWND` from `g_taskbarWindowIconCache` (releases SRV)
- Remove dead `HWND` from `g_taskbarDynamicOrder`

### 13.2 Delta Time + Heartbeat (lines 896–905)
```cpp
dt = (nowTick - lastTick) / 1000.0f;  // clamped to [0, 0.1]
SetEvent(g_hHeartbeatEvent);          // ping watchdog
g_fpsCurrent = lerp(1/dt, 0.02);      // smoothed FPS
appSpinTime += dt;                     // for selected cube animation
```

### 13.3 Model FOV Logic (lines 907–940)
- Accumulates mouse magnitude → `g_modelRotateAccum`
- If `g_modelRotateAccum > g_modelRotateAccumThreshold` (6.0) AND in 3D explore: snap model FOV to scene FOV (90°)
- Hold time: `g_modelFovRotateHoldTime` (0.12s) after rotation stops
- FOV smoothly interpolates between model FOV (55°) and scene FOV (90°)
- `modelScaleComp = modelTan / sceneTan` — compensates apparent size for FOV change

### 13.4 Folder Transition (lines 943–959)
Smoothstep camera interpolation during folder maximize transition.

### 13.5 Camera Rotation (lines 961–968)
```cpp
camera.Rotate(g_mouseDeltaX, g_mouseDeltaY, 0.15f);
// re-center cursor
```
Only when `STATE_3D_EXPLORE && !g_uiUnlocked`.

### 13.6 Pending Hijack Polling (lines 971–1013)
For each pending hijack:
- Increment `frameWait`
- `FindWindowFromProcessId` → if found with `WS_CAPTION`: strip caption, add `WS_THICKFRAME`, center on screen, push to `g_hijackedWindows`
- If window found without `WS_CAPTION`: log warning, erase pending, auto-return to 3D if queues empty
- Timeout at 120 frames

### 13.7 ImGui Frame — Folder Breadcrumb Bar (lines 1019–1099)
Only in folder maximized mode: breadcrumb bar with Desktop/Folder navigation buttons and minimize/maximize/close controls.

### 13.8 Crosshair (lines 1101–1107)
White crosshair at screen center when in `STATE_3D_EXPLORE`.

### 13.9 Taskbar Rendering (lines 1108–1856)
**Massive section** — see detailed taskbar analysis below:
- Pinned app initialization (hardcoded 6 entries)
- Running window enumeration and matching
- Dynamic taskbar entries for unmatched windows
- Drag-to-reorder (pinned and dynamic sections)
- Left-click: toggle Start Menu, activate/minimize running windows
- Middle-click: close window
- Visual: icon rendering, accent highlights, running indicator dots
- Task View button
- System tray area: time/date, battery, volume, network, IME
- Tray chevron → popup with `QueryTrayIcons()` results

### 13.10 Start Menu Popup (lines 1671–1784)
- Semi-transparent acrylic-style panel
- Search filter input (`g_searchFilter`)
- Grid of pinned app tiles with icons and labels
- Click to launch

### 13.11 Tray Popup (lines 1786–1855)
- Query tray icons every 60 frames
- Show icon + tooltip for each tray entry
- Icons loaded on-demand from exe path

### 13.12 Window Chrome (lines 1858–2031)
- For each hijacked window: title bar with drag handle, close/minimize/maximize buttons
- Drag-to-edge snap: blue preview overlay for half/maximize/quarter snap zones
- Window resize via invisible buttons (currently disabled — see bugs)
- Dead window cleanup: swaps `alive` vector into `g_hijackedWindows`

### 13.13 Folder Window Chrome (lines 2034–2097)
- Windowed portal mode: draggable title bar, breadcrumb path, close/maximize/minimize
- Border rendering

### 13.14 Task View Overlay (lines 2100–2264)
- Bright acrylic background with nested rounded rects
- Window cards with icon, title, click-to-focus
- Bottom desktop bar: per-desktop thumbnails with mini window indicators
- Add desktop button (+), close desktop button (x)
- Click outside or Esc to dismiss

### 13.15 Cleanup Dead Windows (lines 2267–2275)
Removes destroyed HWNDs from `g_hijackedWindows`.

### 13.16 Auto-Return to 3D (lines 2278–2294)
When both queues empty: restore `g_currentState` and `g_uiUnlocked`.

### 13.17 Stuck-in-2D Force Clear (lines 2297–2329)
If in 2D mode with no valid windows for 600+ frames: force-clear queues, unlock cursor, log warning.

### 13.18 Model Debug Panel (lines 2331–2385)
ImGui window with:
- Camera rotation display
- Model transform controls (position, rotation, scale)
- Model projection settings (FOV, snap, hold, deadzone)
- Material controls (ambient, diffuse, rim, color tint)
- "Reset All" button
- Desktop app count + "Reload Desktop Apps" button

### 13.19 Loading Overlay (lines 2387–2400)
Dark semi-transparent overlay with "Loading model..." text while `modelRenderer.IsLoading()`.

### 13.20 D3D11 Clear + Viewport (lines 2402–2424)
```cpp
clear_color = {0.1, 0.1, 0.15, 1.0};
ClearRenderTargetView + ClearDepthStencilView
```
Viewport from client rect dimensions.

### 13.21 Skybox (lines 2434)
```cpp
skybox.Render(context, invViewProj, camera.Position);
```

### 13.22 Model Render (lines 2436–2492)
- `PollFinalizeLoad()` — finalizes async OBJ load
- Auto-captures `g_modelPivot` from model bounds center
- Screen-position lock: when independent FOV is active, compensates model world position to keep it at same screen spot
- World matrix: `T(-Pivot) * S * R * T(renderP)`
- Calls `modelRenderer.Render()` with material params + color tint

### 13.23 Raycasting (lines 2494–2512)
- In `STATE_3D_EXPLORE`: ray from camera forward
- In 2D mode: screen-to-world ray via inverse view-projection

### 13.24 Portal Mouse Detection (lines 2515–2520)
Checks if mouse is inside folder portal window rect.

### 13.25 Cube Raycasting — Desktop (lines 2522–2534)
BoundingBox intersection for each `g_myApps` cube (extents {0.6, 0.6, 0.3}). Sets `IsHovered` on closest hit.

### 13.26 Portal Cube Interaction (lines 2536–2603)
Full mirror of desktop interaction logic but in folder's own camera space:
- Ray pick via folder camera's view-projection
- Double-click to enter subfolder or launch app
- Click to select (with Ctrl for multi-select)
- Drag to move selected cubes (150ms hold delay)

### 13.27 Desktop Cube Click Handling (lines 2606–2653)
- Double-click (< 400ms) on same cube: launches app or enters folder
- Single-click: selects cube, initiates grab if in 3D explore mode
- Click on empty space: starts blank-drag marquee selection
- Right-click: stores index for context menu

### 13.28 Delete Requested (lines 2660–2669)
Removes all selected cubes, saves state.

### 13.29 Cube Dragging (lines 2671–2736)
- 150ms hold before drag activates
- Moves grabbed cube along camera ray at `g_dragDistance`
- Collision resolution: pushes overlapping cubes apart (COL_DIAM = 0.80)
- Repulsion force on nearby non-selected cubes (REPEL_DIST = 1.2)
- Drop-target detection: closest non-selected cube within 1.5 units

### 13.30 Blank-Drag Marquee (lines 2737–2766)
- Tracks yaw/pitch delta from initial click
- Computes spherical sector in camera space
- Selects all cubes within the sector
- Ctrl-hold toggles (adds to existing selection)

### 13.31 Drop-Target Action (lines 2767–2789)
On mouse release:
- If dragging a file cube over an exe cube: `ShellExecuteW("open")` to open source with target
- Saves state after drag complete

### 13.32 Desktop Transition (lines 2792–2806)
Fade alpha based on transition progress (cross-fade at midpoint).

### 13.33 Cube Rendering + Labels (lines 2808–2877)
For each cube:
- Hover state: 0=none, 1=hovered, 2=selected, 3=selected+hovered
- Search filter dims unmatched cubes (alpha * 0.15)
- Selected cubes in 2D mode: spin/orbit/tilt animation
- 3D→2D label projection via view-projection matrix
- Labels skipped inside portal rect in windowed folder mode
- Drop-target "Open with <name>" indicator

### 13.34 Marquee Visual Frame (lines 2879–2928)
- Back face: spherical sector at 92% scale (faded)
- Front face: spherical sector at full scale
- Edge beams: 4 corner connecting lines between front and back faces

### 13.35 Alt+Tab Window Switcher (lines 2931–2980)
- Dark semi-transparent overlay
- 3D card layout in arc in front of camera
- Selected card: larger, brighter, with icon
- Card title labels projected to 2D
- Hint text at top-left

### 13.36 Folder Portal Rendering (lines 2983–3016)
- Scissor rect to portal window bounds
- Separate viewport for folder content
- Gray background plane 30 units in front of folder camera
- Renders folder cubes with independent camera/viewport
- Restores original viewport and scissor rect

### 13.37 Cube Context Menu (lines 3018–3039)
Right-click popup: "Launch" and "Remove" options.

### 13.38 Portal Window Interaction (lines 3042–3093)
Same ray-picking logic as desktop but in folder camera space:
- Double-click navigation
- Single-click selection
- Click-empty deselects all

### 13.39 Final Render + Present (lines 3095–3098)
```cpp
g_leftClicked = false;           // reset once per frame
ImGui::Render();
ImGui_ImplDX11_RenderDrawData(); // draw ImGui
g_pSwapChain->Present(1, 0);     // vsync on
```

---

## 14. Cleanup Section (lines 3101–3125)
```cpp
ImGui_ImplDX11_Shutdown();
ImGui_ImplWin32_Shutdown();
ImGui::DestroyContext();
ShutdownAudioEndpointVolume();
CoUninitialize();
UnregisterHotKey x4;
SetEvent(g_hShutdownEvent);  // signal watchdog
CloseHandle events;
SetSystemTaskbarVisible(true);
SaveDesktopState (g_isFolderMaximized ? g_desktopBackupCubes : g_myApps);
CleanupDevice();
UnregisterClassW;
```

---

# Camera.cpp / Camera.h

## Camera.h (21 lines)
```cpp
class Camera {
    DirectX::XMFLOAT3 Position;     // public
    DirectX::XMFLOAT3 Rotation;     // public (pitch, yaw, roll in degrees)

    void Update();                  // recalculates forward/right/up vectors
    void Rotate(float dx, float dy, float sensitivity);  // modify rotation
    XMMATRIX GetViewMatrix() const;                       // XMMatrixLookAtLH
    XMMATRIX GetProjectionMatrix(float fov, float aspect, float nearZ, float farZ) const;
    XMFLOAT3 GetForward() const;

private:
    XMFLOAT3 m_Forward, m_Right, m_Up;
};
```
- **No `Move()` method** — present in AGENTS.md description but removed from design.

## Camera.cpp (55 lines)
- **Constructor** (lines 5–12): Position {0, 1.5, 0}, Rotation {0, 0, 0}. Forward along +Z, Right along +X, Up along +Y.
- **Update()** (lines 15–32): Converts rotation to radians, builds roll-pitch-yaw matrix, transforms forward vector, computes right via cross(up, forward), recomputes up via cross(forward, right). All vectors normalized.
- **GetViewMatrix()** (lines 34–40): `XMMatrixLookAtLH(pos, pos+forward, up)`.
- **GetProjectionMatrix()** (lines 42–44): `XMMatrixPerspectiveFovLH`.
- **Rotate()** (lines 46–55): Adds dx*sensitivity to yaw, dy*sensitivity to pitch. Clamps pitch to [-89, 89] degrees. Wraps yaw to [0, 360).

**BUG**: The rotation order in the `Rotate` function is Y-then-X (yaw then pitch), but the `Update()` function uses `XMMatrixRotationRollPitchYaw(pitch, yaw, roll)` which applies rotations in order: roll → pitch → yaw (Z-X-Y intrinsic). This means the rotation axes may not behave as intuitively expected — yaw may have a pitch-dependent component.

---

# SkyboxRenderer.cpp / SkyboxRenderer.h

## SkyboxRenderer.h (30 lines)
```cpp
class SkyboxRenderer {
    bool Initialize(ID3D11Device* device);
    void Cleanup();
    void Render(ID3D11DeviceContext* context, XMMATRIX invViewProj, XMFLOAT3 cameraPos);

    struct ConstantBufferType {
        XMMATRIX InvViewProj;
        XMFLOAT3 CameraPos;
        float padding;
    };
};
```

## SkyboxRenderer.cpp (147 lines)
- **Initialize()** (lines 19–122):
  - Compiles inline HLSL at runtime (no `.hlsl` files)
  - **Vertex Shader**: Fullscreen triangle from vertex ID (SV_VertexID): id=0→(-1,1), id=1→(3,1), id=2→(-1,-3). Unprojects to world space, computes ray direction.
  - **Pixel Shader**: Procedural sky gradient with "Win11 Bloom" effect:
    - Distorted ray for tilted oval bloom
    - `float3 ambientBlue = {0.12, 0.38, 0.78}`
    - `float3 midCyan = {0.35, 0.70, 0.95}`
    - `float3 coreWhite = {0.95, 0.98, 1.00}`
    - Smoothstep blending with pow(core, 12) for center hotspot
    - Gentle sine wave variation, dithering noise for banding reduction
  - Rasterizer: CULL_NONE (render both sides)
  - Depth: DISABLED (always on top, no depth write)
- **Render()** (lines 124–147): Updates constant buffer, sets no-cull + no-depth states, draws 3 vertices (fullscreen triangle), restores default states afterward.

**OPTIMIZATION**: Skybox shader is recompiled every time `Initialize()` is called. Could precompile to bytecode or cache in a member.

---

# CubeRenderer.cpp / CubeRenderer.h

## CubeRenderer.h (55 lines)
```cpp
class CubeRenderer {
    void Render(context, viewProjection, position, scale, color, texture,
                cameraPos, hoverState, viewMatrix, radius=8.0f,
                spinAngle=0.0f, orbitAngle=0.0f, tiltAngle=0.0f);

    struct Vertex { XMFLOAT3 Pos; XMFLOAT3 Normal; XMFLOAT2 UV; };

    struct ConstantBufferType {
        XMMATRIX WVP, World, ViewProj;
        XMFLOAT4 Color;
        XMFLOAT3 LightDir; float padding;
        XMFLOAT3 LocalCamPos; int HoverState;
        XMFLOAT3 WorldCamPos; float Radius;
    };
};
```
- 24 vertices (unit cube faces with normals and UVs)
- 36 indices (6 faces × 2 triangles)

## CubeRenderer.cpp (251 lines)

### Shader (lines 23–131) — HLSL compiled at runtime:
**HoverState meanings**:
| State | Description |
|---|---|
| 0 | Normal cube rendering with icon texture ray-march |
| 1 | Hovered: white glow with edge intensity |
| 2 | Selected: brighter fill |
| 3 | Selected + Hovered: full highlight |
| 4 | Fixed solid blue tint (edge intensity only) |
| 5 | Spherical sector selection (marquee front/back face) |
| 6 | Marquee edge beams (solid blue lines) |

**Icon rendering** (lines 103–129):
- Ray-marches from cube surface inward (60 steps)
- Samples icon texture at (cx*1.25, cy*1.25) — the 1.25x expansion is to cut off icon borders
- Stops when alpha > 0.65
- Side faces get 85% brightness

**Marquee rendering** (lines 53–86):
- State 5: computes yaw/pitch from ray direction, checks against sector angles. Writes custom SV_Depth for proper Z order.
- State 6: blue beam edges with alpha 0.9

### Render() (lines 184–250):
- **World matrix handling**:
  - States 4/5: canvas parallel to view (billboarded to camera)
  - State 6: edge beam with pre-computed rotation from Color.x/.y (angles)
  - Normal: face-toward-camera + optional spin/orbit/tilt animation
- **Local camera pos**: transforms world camera into cube's local space for ray-march
- Draws 36 indexed vertices per call

**BUG**: `Render()` creates a temporary `XMVECTOR` in `invView` at line 189, stores result in `invView.r[3] = XMVectorSet(0,0,0,1)` but doesn't reset `r[3].w`. This works because `XMMatrixInverse` on a view matrix properly produces the inverse, but the manual zeroing of translation at `r[3]` is non-standard and may cause subtle issues with non-uniform scale billboard transforms.

---

# ModelRenderer.cpp / ModelRenderer.h

## ModelRenderer.h (60 lines)
```cpp
class ModelRenderer {
    bool Initialize(ID3D11Device*);
    void Cleanup();
    bool LoadModelAsync(const string& filepath);
    void PollFinalizeLoad();                                    // main thread finalization
    bool SetDiffuseTexture(const wstring& path);
    bool SetNormalTexture(const wstring& path);
    void Render(context, viewProjection, world, cameraPos, matParams, colorTint);

    // Async state
    atomic<bool> m_loadInProgress, m_loadReady;
    thread m_loadThread;
    mutex m_pendingMutex;
    vector<ModelVertex> m_pendingVerts;
    vector<unsigned int> m_pendingInds;
    wstring m_pendingDiffusePathW, m_pendingNormalPathW;
    XMFLOAT3 m_modelCenter;
};
```

## ModelRenderer.cpp (364 lines)

**Shader** (lines 48–100): Half-Lambert lighting with normal map support and Fresnel rim light.
- Tangent-space normal mapping (only if `length(tangent) > 0.01`)
- Light direction: `{-0.5, 1.0, -0.8}` (upper-front-left)
- Half-Lambert: `NdotL = dot(N,L)*0.5+0.5` then squared for contrast
- Rim: `pow(1 - viewDot, MatParams.w) * MatParams.z` with blue rim color

**Initialize()** (lines 45–148):
- Compiles VS and PS at runtime
- Input layout: Position(12) + UV(8) + Normal(12) + Tangent(12) = 44 bytes per vertex
- Anisotropic sampler (16x)
- Placeholder 1×1 diffuse (white) and normal ({128,128,255,255}) textures
- CULL_NONE rasterizer (support mirrored UVs)

**LoadModelAsync()** (lines 156–218):
1. Parses OBJ header for `mtllib` and `usemtl`
2. Parses MTL for `map_Kd` (diffuse) and `map_Bump`/`bump` (normal) paths
3. Launches `std::thread` calling `ObjLoader::LoadObj()`
4. Thread fills `m_pendingVerts/Inds` under mutex

**PollFinalizeLoad()** (lines 220–311):
1. Joins load thread
2. Computes model bounds center (`m_modelCenter`)
3. Computes tangent vectors (Gram-Schmidt orthogonalization against normal)
4. Creates immutable vertex/index buffers on main thread (D3D requirement)
5. Loads diffuse/normal textures from paths parsed in MTL

**Render()** (lines 330–364):
- Transforms: `WVP = world * viewProjection` (row-vector convention → transpose)
- Sets VS/PS constant buffers, vertex/index buffers, shader resources
- Sets CULL_NONE rasterizer state, restores after draw

**BUG**: `Render()` binds `m_rasterizerState` every frame and restores null after, but the cube renderer does NOT set its own rasterizer state. If cube rendering is interleaved with model rendering, the model's CULL_NONE may leak into cube draws.

---

# ObjLoader.cpp / ObjLoader.h

## ObjLoader.h (20 lines)
```cpp
struct ModelVertex { XMFLOAT3 Position; XMFLOAT2 UV; XMFLOAT3 Normal; XMFLOAT3 Tangent; };
class ObjLoader { static bool LoadObj(filepath, outVertices, outIndices); };
```

## ObjLoader.cpp (216 lines)

### Binary Cache System (lines 13–104)
- **Format**: `.cdmesh` — binary cache to skip OBJ parsing on subsequent loads
- **Header**: magic `"CDMESH\0\0"`, version=1, vertexStride, objMtime, objSize, vertexCount, indexCount
- **Validation**: checks magic, version, stride, file mtime+size match
- `TryLoadMeshCache()` → returns cached or false
- `WriteMeshCache()` → writes binary after parse

### LoadObj() (lines 123–216)
- Pre-allocates: temp_positions(1M), temp_uvs(1M), temp_normals(1M), uniqueVertices(2M)
- Parses with `fgets` + `sscanf_s` (no C++ streams after cache check)
- **Coordinate conversion**: RH→LH flips Z for positions and normals, flips V for UVs (`uv.y = 1-uv.y`)
- **Winding reversal**: face triangles use `{v[0], v[i+1], v[i]}` order (was `{v[0], v[i], v[i+1]}`)
- Supports face formats: `v/vt/vn`, `v//vn`, `v/vt`, `v`
- Uses `unordered_map<VertexKey, unsigned int>` for vertex deduplication
- Writes `.cdmesh` cache on success

**BUG**: Lines 145–148 check `line[0] == 'v' && line[1] == ' '` — but if a line starts with `"v\n"` (just 'v' with newline), `line[1]` will be `'\n'` not `' '`, which correctly avoids false-positive. However, `line[2]` access in `line[0]=='v' && line[1]=='t' && line[2]==' '` is safe because `fgets` always null-terminates.

**OPTIMIZATION**: The `VertexKeyHash` function (lines 112–115) uses simple XOR/shift, which can cause collisions. A better hash would use a proper combine function like `boost::hash_combine` pattern.

---

# Logger.h (128 lines)

Header-only singleton with macros:
```cpp
#define LOG(fmt, ...)  Logger::Instance().Log(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) Logger::Instance().LogW(fmt, ##__VA_ARGS__)
```

**Class Logger** (lines 9–125):
- `Instance()`: static local singleton (thread-safe in C++11)
- `Init(logPath=""`): creates `logs/crossdim_YYYYMMDD_HHMMSS.log`, sets unbuffered I/O, calls `PruneOldLogs()`
- `Shutdown()`: flushes and closes file
- `Log(fmt, ...)`: formats with `[sssss.mmm]` timestamp, writes to file + `OutputDebugStringA`
- `LogW(fmt, ...)`: wide-char version, converts to UTF-8 then calls `Log()`
- `PruneOldLogs()`: keeps last 10 log files, deletes older ones

**BUG**: `PruneOldLogs()` sorts alphabetically by filename. Since filenames contain timestamps (`crossdim_YYYYMMDD_HHMMSS.log`), alphabetical sorting correctly orders by date. However, if log files from different dates exist, `crossdim_20260730_120000.log` sorts before `crossdim_20260731_120000.log`, which is correct chronological order.

---

# TextureLoader.h (212 lines)

All-static methods, header-only. Uses WIC for file textures, GDI for icons.

### `LoadIconFromHandle(ID3D11Device*, HICON)` — lines 14–66
- Copies icon (non-destructive)
- Creates 512×512 DIB section
- `DrawIconEx` with `DI_NORMAL` (writes alpha)
- Creates D3D11 texture with `DXGI_FORMAT_B8G8R8A8_UNORM`

### `LoadIconFromExe(ID3D11Device*, LPCWSTR exePath)` — lines 68–143
- Tries `PrivateExtractIconsW` (512px)
- Falls back to `ExtractIconExW`, then `SHGetFileInfoW`, then system default icon (#32512 = IDI_APPLICATION)
- Same DIB section + D3D texture creation

### `LoadTextureFromFile(ID3D11Device*, const wstring& filePath)` — lines 147–211
- Uses WIC to decode: Creates factory → decoder → frame → format converter (RGBA8)
- Creates D3D11 texture with mipmap generation (`MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS`)
- SRV with `MipLevels = -1` (all mips)
- Calls `GenerateMips()` on immediate context

**BUG**: `LoadTextureFromFile` line 174: `desc.MipLevels = 0` for full mip chain, but `UpdateSubresource` at line 183 only writes mip level 0. The `GenerateMips()` call at line 204 generates the lower mips from level 0, which is correct. However, if the texture creation fails mid-way, the WIC objects may leak (early returns don't release converter/frame/decoder/factory in all paths).

**BUG**: `LoadIconFromExe` line 133: `if (destroyIcon && hIcon)` — when `PrivateExtractIconsW` succeeds, `destroyIcon` stays false (set at line 70), meaning the icon from `PrivateExtractIconsW` is leaked because `DestroyIcon` is not called for it. According to MSDN, icons from `PrivateExtractIconsW` must be destroyed with `DestroyIcon`.

---

# TrayProxy.h (75 lines)

### `TrayIconEntry` struct (lines 9–13)
```cpp
struct TrayIconEntry { wstring tooltip; int commandId; wstring exePath; };
```

### `FindTrayToolbar()` — lines 15–23
Window hierarchy traversal: `Shell_TrayWnd → TrayNotifyWnd → SysPager → ToolbarWindow32`

### `QueryTrayIcons()` — lines 25–74
- Opens Explorer process with `PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE`
- Uses `TB_GETBUTTON` + `ReadProcessMemory` to enumerate toolbar buttons
- Skips `TBSTATE_HIDDEN` buttons
- Reads tooltip text via `TB_GETBUTTONTEXTW` across process boundary
- Returns `vector<TrayIconEntry>` with tooltips (no icons — icons are in Explorer's process and not directly readable)

**LIMITATION**: This reads tooltip names but NOT the actual tray icons. The icons live in Explorer's image list and can't be extracted across process boundaries without undocumented APIs. The AGENTS.md notes this as the remaining gap (~5%).

---

# DesktopManager.h (288 lines)

### `AppCube` struct (lines 15–24)
```cpp
struct AppCube {
    XMFLOAT3 Position;
    XMFLOAT4 BaseColor;  // default {0.3, 0.3, 0.3, 1.0}
    wstring AppPath;
    string AppName;
    bool IsHovered, IsSelected, WasSelected;
    ID3D11ShaderResourceView* IconTexture;
};
```
### Global: `inline vector<AppCube> g_myApps;` (line 26)
### External: `extern ID3D11Device* g_pd3dDevice;` (line 28)

### `ResolveShortcutTarget(LPCWSTR lnkPath)` — lines 30–45
Uses `IShellLink` + `IPersistFile` COM to resolve `.lnk` to target path.

### `WcsToUtf8(const wstring&)` — lines 47–54
MultiByteToWideChar UTF-8 conversion helper.

### `SearchMatch(const char* filter, const string& label, const wstring& path)` — lines 56–67
Case-insensitive substring match on label and path. Returns true if filter empty.

### `ScanDesktopForApps(vector<AppCube>&)` — lines 69–147
- Reads from both `CSIDL_DESKTOPDIRECTORY` and `CSIDL_COMMON_DESKTOPDIRECTORY`
- Includes: directories, `.lnk` shortcuts, `.exe`, and other files (excluding `.ini`, `.log`, `.tmp`)
- Skips hidden/system files
- Arranges cubes on spherical sector: yaw [-50°, 15°], pitch [-28°, 30°], radius 8.0
- Grid layout on sphere: `cols ≈ sqrt(count * 1.6)`

### `ScanFolderForApps(const wstring& folderPath, vector<AppCube>&)` — lines 149–207
Same as `ScanDesktopForApps` but for arbitrary folder path.

### `SaveDesktopState(const vector<AppCube>&, int desktopIndex=0)` — lines 209–232
Binary format: magic(0x4B534544=DESK) + version(2) + count + per-cube{pathLen, path(wchar_t), Position, nameLen, name(char)}
File: `desktop.cddesk` or `desktop_{id}.cddesk` per virtual desktop.

### `LoadDesktopState(vector<AppCube>&, int desktopIndex=0)` — lines 234–274
- Reads binary format
- Matches by AppPath, updates Position
- If not matched but file exists: creates new cube
- Version validated: 1 or 2 allowed

### `LoadIconsForApps(vector<AppCube>&)` — lines 276–288
Loads icons for all apps without existing IconTexture. Falls back to SHGetFileInfoW.

---

# WindowManager.h (235 lines)

### Data Structures
```cpp
struct PendingHijack { HWND originalFocus; int frameWait; DWORD processId; };
struct HijackedWindow { HWND hwnd; };
struct RunningWindow { HWND hwnd; DWORD pid; wstring path, title; };
struct WindowEnumContext { HWND exclude; vector<RunningWindow>* out; };
struct EnumData { DWORD processId; HWND hwnd; };
```

### Global inline containers (lines 20–30):
```cpp
vector<PendingHijack> g_pendingHijacks;
vector<HijackedWindow> g_hijackedWindows;
unordered_map<wstring, ID3D11ShaderResourceView*> g_taskbarIconCache;
unordered_map<HWND, ID3D11ShaderResourceView*> g_taskbarWindowIconCache;
unordered_map<HWND, int> g_taskbarDynamicOrder;
int g_taskbarDynamicOrderCounter = 1;
```

### `GetYawPitch(XMVECTOR dirVec)` — lines 51–57
Returns `{atan2(dir.x, dir.z), asin(dir.y / length)}`.

### `EnumWindowsProc(HWND, LPARAM)` — lines 59–73
Finds the first visible top-level window belonging to a process ID (no owner, has title).

### `FindWindowFromProcessId(DWORD pid)` — lines 75–79
Uses `EnumWindows` with above callback.

### `GetProcessPath(DWORD pid)` — lines 81–92
`OpenProcess + QueryFullProcessImageNameW`.

### `IsTaskbarWindowCandidate(HWND, HWND exclude)` — lines 94–111
Filters: visible, not self, no owner, not child, not toolwindow, not noactivate, not cloaked (DWM), not Shell_TrayWnd/Progman, has title.

### `EnumRunningWindowsProc` + `EnumerateRunningWindows` — lines 113–136
Enumerates all candidate windows for taskbar display.

### `IsSamePath(const wstring& a, const wstring& b)` — lines 138–147
Case-insensitive full path or filename-only comparison.

### `FindHwndByExeProc` + `FindRunningHwnd(const wstring& appPath)` — lines 149–177
Finds a visible window whose process path ends with the given exe name.

### `GetTaskbarIcon(ID3D11Device*, const wstring& path)` — lines 179–186
Cached icon lookup, loads from exe on miss.

### `SetSystemTaskbarVisible(bool)` — lines 188–198
Shows/hides `Shell_TrayWnd` and all `Shell_SecondaryTrayWnd` windows. Also controls start button visibility implicitly.

### `GetWindowBestIcon(HWND)` — lines 200–208
Tries `WM_GETICON` (BIG→SMALL2→SMALL) → `GCLP_HICON` → `GCLP_HICONSM`.

### `GetWindowIconTexture(ID3D11Device*, HWND, const wstring& path)` — lines 210–235
- Checks per-HWND cache first
- Tries `GetWindowBestIcon` → `LoadIconFromHandle`
- Falls back to `GetTaskbarIcon` by path
- Falls back to system default icon (#32512)
- Caches result per HWND

---

# SystemInfo.h (122 lines)

### Audio (lines 13–50)
```cpp
IAudioEndpointVolume* g_audioEndpoint = nullptr;           // inline global
InitAudioEndpointVolume() → GetDefaultAudioEndpoint(eRender) → Activate
ShutdownAudioEndpointVolume() → Release
GetMasterVolumeLevelScalar() → GetMasterVolumeLevelScalar, clamped [0,1]
```

### IME (lines 52–88)
```cpp
GetImeLabel(HWND, char* buffer, size_t)  // "中 拼" or "英 ENG" (legacy)
GetImeOpenStatus(HWND)                   // ImmGetOpenStatus
SetImeOpenStatus(HWND, bool)
IsChineseImeLayout()                     // PRIMARYLANGID == LANG_CHINESE
ActivateImeLayout(HWND, LPCWSTR klid, bool open)  // LoadKeyboardLayoutW + SetImeOpenStatus
```

### Network (lines 90–122)
```cpp
struct NetworkStatus { bool connected, wifi, ethernet; };
GetNetworkStatus() → GetAdaptersAddresses → iterate → check IfType
```
- `IF_TYPE_IEEE80211` → wifi
- `IF_TYPE_ETHERNET_CSMACD` → ethernet
- Others treated as ethernet
- Skips loopback

---

# AGENTS.md (225 lines)

Project documentation covering:
- Identity and current state
- Tech stack
- Architecture: dual state machine, render loop order, matrix conventions
- Feature roadmap (Phases 1–6)
- Commit message convention: `<type>: <brief> v<version>`
- 6 strict rules (no main thread blocking, matrix conventions, pure C++, no game engine patterns, reversibility, documented APIs only)

---

# tasks.json (71 lines)

Three build tasks:
1. **Build CrossDim**: compiles 11 source files with `/O2 /Zi /EHsc /std:c++17`, links with `d3dcompiler.lib`
2. **Build Watchdog**: compiles `watchdog.cpp` with `advapi32.lib`
3. **Build All**: depends on both above, default build task

Output: `build/CrossDim.exe`, `build/watchdog.exe`

---

# Global Dependencies Map

```
main.cpp
├── Engine/Camera.h         → Camera.cpp
├── Engine/SkyboxRenderer.h → SkyboxRenderer.cpp  (depends on d3dcompiler.lib)
├── Engine/CubeRenderer.h   → CubeRenderer.cpp     (depends on d3dcompiler.lib)
├── Engine/TextureLoader.h  → (header-only, depends on wincodec.lib)
├── Engine/ModelRenderer.h  → ModelRenderer.cpp     (depends on d3dcompiler.lib, ObjLoader.h)
│   └── Engine/ObjLoader.h  → ObjLoader.cpp
├── Engine/Logger.h         → (header-only)
├── Engine/TrayProxy.h      → (header-only)
├── Shell/DesktopManager.h  → (header-only, depends on TextureLoader.h, Logger.h)
│   └── extern g_pd3dDevice (from main.cpp)
├── Shell/WindowManager.h   → (header-only, depends on TextureLoader.h)
│   ├── extern g_systemTaskbarHidden (from main.cpp)
│   └── inline globals: g_pendingHijacks, g_hijackedWindows, g_taskbarIconCache, etc.
├── Shell/SystemInfo.h      → (header-only)
│   └── inline global: g_audioEndpoint
```

---

# Bug Catalog

| # | File:Line | Severity | Description |
|---|---|---|---|
| 1 | main.cpp:2568 | Medium | `EnterFolder` called from portal double-click sets `g_previousUiUnlocked = g_uiUnlocked` before calling `LaunchAppByPath`, but `LaunchAppByPath` itself also sets it at line 425. Not a logic error since the value is the same, but redundant. However, the `IsPathDirectory` branch does NOT set `g_previousUiUnlocked` before entering folder, which may be correct since folders don't transition to 2D mode. |
| 2 | main.cpp:187 | Low | `CloseDesktop` computes `fallback` but never uses it — dead code. |
| 3 | main.cpp:265 | Medium | `EnterFolder` double-loads icons: at line 265 (`LoadIconsForApps(newCubes)`) and line 279 (`LoadIconsForApps(g_folderCubes)`) for the same cubes. Redundant GPU resource allocation. |
| 4 | main.cpp:270 | Low | `for (wchar_t ch : fn) fs.displayName += (char)ch` — corrupts non-ASCII folder names. Wide characters truncated to char. |
| 5 | Camera.cpp:46–47 | Low | Rotation order mismatch: `Rotate()` applies yaw-then-pitch, but `Update()` uses `XMMatrixRotationRollPitchYaw(pitch, yaw, roll)`. The effective rotation order differs from user expectation. |
| 6 | CubeRenderer.cpp:189 | Medium | `invView.r[3] = XMVectorSet(0,0,0,1)` manually zeros translation component of inverse view matrix. While this produces a billboard effect, the `w` component may not be properly handled for non-uniform scaling scenarios. |
| 7 | ModelRenderer.cpp:358 | Medium | Model renderer sets `RSSetState(m_rasterizerState)` (CULL_NONE) but cube renderer does NOT set its own rasterizer state. If render order interleaves, CULL_NONE may leak into cube draws. |
| 8 | TextureLoader.h:133 | Medium | Memory leak: icons from `PrivateExtractIconsW` are not destroyed. The `destroyIcon` flag is false when `PrivateExtractIconsW` succeeds because it's initialized to false at line 70 and never set to true on that path. |
| 9 | TextureLoader.h:155–159 | Low | Early returns in `LoadTextureFromFile` after creating `decoder` and `frame` but before creating `converter` leak those COM objects. |
| 10 | main.cpp:445 | Low | `SampleDesc.Quality = 0` for 4x MSAA — MSDN recommends using `D3D11_STANDARD_MULTISAMPLE_PATTERN` or querying supported quality levels. Quality 0 may not enable MSAA on some hardware. |
| 11 | DesktopManager.h:23 | Medium | `IsSelected` and `WasSelected` fields in `AppCube` are not initialized to false in the constructors at lines 141 and 201 — rely on `= {}` zero-initialization, which is correct but fragile. |
| 12 | main.cpp:1924+ | Low | Window resize handles (InvisibleButtons at window edges) appear to be intended but the rendering code only shows window chrome (title bar buttons), not resize handles at edges. |

---

# Optimization Opportunities

| # | Location | Priority | Description |
|---|---|---|---|
| 1 | SkyboxRenderer.cpp | Low | Cache compiled shader bytecode instead of recompiling HLSL source every `Initialize()` |
| 2 | CubeRenderer.cpp | Low | Same — precompile shaders |
| 3 | main.cpp:2525 | Medium | `BoundingBox` intersection tests run every frame for ALL cubes. Could use spatial partitioning (octree/grid) for large cube counts. |
| 4 | main.cpp:2843 | Low | Per-cube label projection does 3D→2D transform per cube every frame. Could batch or skip off-screen cubes. |
| 5 | ObjLoader.cpp:112 | Low | `VertexKeyHash` uses simple XOR — higher collision rate for large meshes. Use proper hash combine. |
| 6 | main.cpp:1925+ | Medium | Window chrome re-renders all hijacked window decorations every frame. Could track dirty state. |
| 7 | DesktopManager.h:277 | Medium | `LoadIconsForApps` calls `LoadIconFromExe` synchronously during scan. Large desktops block the main thread. Should use async icon loading. |
| 8 | main.cpp:1159 | Low | `EnumerateRunningWindows` called every frame. Could throttle to every N frames. |
| 9 | WindowManager.h:179 | Low | `GetTaskbarIcon` cache lacks size limit — unbounded growth for long-running sessions. |
| 10 | main.cpp:2436 | Low | Model `PollFinalizeLoad` + tangent computation blocks main thread for large models. Tangent computation could be done in worker thread before finalizing. |

---

# State Machine Diagrams

## Dual State Machine
```
┌─────────────────────┐         Tab / LaunchApp         ┌──────────────────────┐
│  STATE_3D_EXPLORE   │ ──────────────────────────────> │  STATE_2D_WORKBENCH  │
│                     │                                  │                      │
│  • Mouse locked     │  <──────────────────────────────│  • Mouse unlocked    │
│  • Camera control   │    Auto-return (empty queues)   │  • ImGui interactive │
│  • Cube interaction │                                  │  • Hijacked windows  │
│  • Raycast targeting│  ───────────────────────────────│  • Window chrome     │
│                     │    Force-clear (600 stuck frames)│  • Taskbar full UI   │
└─────────────────────┘                                  └──────────────────────┘
```

## Folder State Machine
```
                 EnterFolder(path)
Desktop  ────────>  Folder (Maximized)  ───MakeFolderWindowed──>  Folder (Portal)
   ^                 │                         <──MakeFolderMaximized──  │
   │                 │ EnterFolder(sub)                                  │
   │ ExitFolderToRoot │───────>  Deeper Folder                          │
   │                 │                          ExitFolderToParent       │
   └─────────────────┴─────────────────────────────────────────────────┘
```

## Virtual Desktop State Machine
```
                 CreateNewDesktop
Desktop[N]  ──────────────────>  Desktop[N+1]
    │  SwitchToDesktop(i)              │
    │<─────────────────────────────────│
    │                                  
    │  CloseDesktop(i)                 
    └──> Desktop[N-1] (windows minimized/reparented)
```

## Cube Drag State Machine
```
IDLE ──[L-click on cube]──> SELECTED ──[150ms hold]──> DRAGGING
  ^                             │                          │
  └──[release]─────────────────└──[release < 150ms]───────┘
  └──[double-click]──> LAUNCH / ENTER_FOLDER
```

## Marquee Selection State Machine
```
IDLE ──[L-click on empty]──> BLANK_DRAG ──[mouse move]──> SELECTING
  │                              │                            │
  └──[release]──────────────────└──[release]─────────────────┘
```

---

*Generated from full codebase analysis — 3126 lines main.cpp + 6 compiled source files + 5 header-only modules.*
