# CrossDim — Architecture Overview

## 1. System Identity

CrossDim is a **native C++ spatial OS shell**. Current deployment: a 3D overlay running on top of Explorer.exe. Target deployment: replace Explorer.exe as the primary Windows shell.

The core philosophy: classic desktop metaphors (taskbar, icons, windows, drag-and-drop) mapped naturally into 3D space — **not** a game engine.

## 2. Top-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    wWinMain (main.cpp)                        │
│  ┌───────────┐  ┌───────────┐  ┌────────────┐  ┌──────────┐  │
│  │ InitDevice │  │  WndProc  │  │ Render Loop │  │ Cleanup  │  │
│  │ (D3D11)    │  │ (messages)│  │ (per-frame) │  │ (shutdown)│ │
│  └─────┬─────┘  └─────┬─────┘  └──────┬─────┘  └──────────┘  │
│        │              │               │                       │
│  ┌─────┴──────────────┴───────────────┴──────────────────┐    │
│  │                  Global State (file-scope)             │    │
│  │  g_currentState, g_uiUnlocked, g_pd3dDevice,           │    │
│  │  g_myApps, g_folderStack, g_desktopSlots, ...          │    │
│  └────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                     Engine Layer (src/Engine)                │
│  Camera  SkyboxRenderer  CubeRenderer  ModelRenderer         │
│  ObjLoader  Logger  TextureLoader  TrayProxy                 │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                      Shell Layer (src/Shell)                 │
│  DesktopManager  WindowManager  SystemInfo                   │
└─────────────────────────────────────────────────────────────┘
```

## 3. Layer Responsibilities

### 3.1 Main Orchestration Layer (main.cpp, ~3126 lines)
- **State machine**: dual-mode (3D explore / 2D workbench), folder navigation stack, virtual desktop switching
- **Input**: raw input routing, click detection, drag state machine, keyboard hotkeys
- **UI**: taskbar, start menu, tray popup, window chrome, model debug panel
- **3D scene**: skybox, model, cube rendering, marquee selection, holographic labels
- **Window hijacking**: pending hijack queue, window chrome, snapping
- **Rendering order**: clear → skybox → model → cubes → labels → portal → ImGui → present

### 3.2 Engine Layer (src/Engine/)
| Module | Responsibility |
|---|---|
| Camera | FPS camera, position/rotation, view/projection matrices |
| SkyboxRenderer | Procedural skybox (vertex/pixel shader, inverse VP) |
| CubeRenderer | 3D icon cubes, spherical sector marquee cage, hover states |
| ModelRenderer | Async OBJ model loading, Half-Lambert shading |
| ObjLoader | OBJ/MTL parser with .cdmesh binary cache |
| Logger | Header-only singleton, logs/ dir, auto-prune |
| TextureLoader | D3D texture from EXE icon / HICON |
| TrayProxy | Explorer tray toolbar cross-process reader |

### 3.3 Shell Layer (src/Shell/)
| Module | Responsibility |
|---|---|
| DesktopManager | AppCube struct, desktop scan, .cddesk persistence, search |
| WindowManager | Window enumeration, hijack queue, icon caches |
| SystemInfo | Audio endpoint, IME layout, network adapter status |

## 4. Data Flow

### 4.1 Startup
```
wWinMain
  → CoInitializeEx
  → InitAudioEndpointVolume (COM)
  → RegisterClassExW / CreateWindowExW (fullscreen WS_POPUP)
  → RegisterHotKey x5 (Tab, Ctrl+Shift+Esc, Ctrl+Shift+D, Ctrl+Shift+T, Ctrl+Tab)
  → Logger::Init
  → SetSystemTaskbarVisible(false)  // hide Explorer taskbar
  → DragAcceptFiles, RegisterRawInputDevices
  → InitDevice (D3D11 swap chain + RTV + DSV)
  → Launch watchdog.exe
  → ImGui init (msyh.ttc font for Chinese)
  → Camera / CubeRenderer / SkyboxRenderer / ModelRenderer init
  → ScanDesktopForApps(g_myApps) → LoadDesktopState → LoadIconsForApps
  → Enter render loop
```

### 4.2 Per-Frame Render Loop
```
1. PeekMessage (process window messages)
2. Prune dead window icon caches (every 60 frames)
3. Frame timing (dt clamped to 0.1s)
4. Model FOV logic
5. Camera rotation (3D explore mode) / folder camera rotation
6. Pending hijack polling (find window, strip chrome, add to hijacked list)
7. ImGui NewFrame
8. Folder breadcrumb bar (if in folder, maximized)
9. Crosshair (3D mode)
10. Taskbar rendering (pinned apps, running windows, system indicators)
11. Start menu / search
12. Tray popup
13. Window chrome (hijacked windows: title bar, buttons, drag, snapping)
14. Task View overlay (virtual desktops)
15. Folder window chrome (windowed mode)
16. Model debug panel (2D mode)
17. 3D scene: clear → skybox → model → cubes + labels → marquee cage
18. Alt+Tab switcher overlay (if active)
19. Folder portal rendering (scissored second viewport)
20. Cube interaction (raycast, click, drag, marquee)
21. Context menu popup
22. ImGui Render → Present
```

### 4.3 Window Hijacking Flow
```
Double-click EXE cube
  → LaunchAppByPath (CreateProcessW)
  → Push PendingHijack { processId, frameWait, originalFocus }
  → Switch to STATE_2D_WORKBENCH
  → Per-frame: FindWindowFromProcessId
    → If found with WS_CAPTION:
      → Strip WS_CAPTION | WS_SYSMENU
      → Add WS_THICKFRAME
      → Center window 1100x750
      → Add to g_hijackedWindows
    → If 120 frames timeout: abandon
```

### 4.4 Folder Navigation Flow
```
Double-click folder cube
  → EnterFolder(path)
  → Save desktop cubes to g_desktopBackupCubes
  → ScanFolderForApps(path) → g_folderCubes
  → Push FolderState to g_folderStack
  → Portal window appears (windowed mode)
  → Interact with folder cubes inside portal
  → Double-click subfolder → EnterFolder (deeper)
  → Click Desktop / Close → ExitFolderToRoot
```

## 5. State Machines

### 5.1 Dual Mode (3D / 2D)
```
STATE_3D_EXPLORE ──Ctrl+Shift+X(Tab hotkey)──→ STATE_2D_WORKBENCH
       ↑                                            │
       └───── all windows closed ───────────────────┘
```

### 5.2 Folder Navigation
```
Desktop (g_folderStack empty)
  └─ EnterFolder → level 1 (portal window)
       └─ EnterFolder → level 2 (subfolder)
            └─ ... (infinite depth)
       ExitFolderToParent ←──────┘
  ExitFolderToRoot ←─────────────────┘
```

### 5.3 Virtual Desktops
```
g_desktopSlots [0, 1, 2, ...]
  SwitchToDesktop(i):
    Save current desktop state
    Hide current windows
    Transition animation (0→1, swap at 0.5)
    Show target windows
```

### 5.4 Cube Drag / Marquee
```
Click cube → select (Ctrl=additive)
Double-click → launch/enter folder
Drag 150ms → grab, move with ray at g_dragDistance
Release → drop-target detection (exe + file → ShellExecute)
Click empty → marquee (spherical sector selection)
```

## 6. Memory Management

### 6.1 Icon Texture Lifecycle
- **Desktop cubes**: loaded at startup, released on desktop switch / reload
- **Folder cubes**: loaded on EnterFolder, released on Exit
- **Taskbar icons**: cached in g_taskbarIconCache (keyed by path), never pruned
- **Window icons**: cached in g_taskbarWindowIconCache (keyed by HWND), pruned every 60 frames

### 6.2 Known Leaks
1. `g_taskbarIconCache` never prunes — unbounded growth
2. `LoadIconFromExe` may leak the icon handle on failure paths
3. Folder navigation stack entries release textures, but intermediate frames may double-release

## 7. Extensibility Points

| Extension Point | How |
|---|---|
| New window decorations | Extend the chrome rendering loop |
| New cube actions | Extend the right-click context menu |
| New system tray indicators | Add ImGui drawing functions in tray section |
| New desktop layouts | Modify `ScanDesktopForApps` layout algorithm |
| New hotkeys | Register in wWinMain + handle in WndProc |
| File manager module | Extend `ScanFolderForApps` + folder navigation stack |
| Shell registration | Add registry swap module (Phase 5.4) |
| Multi-monitor | Abstract viewport/taskbar per monitor |

## 8. Threading Model

- **Single thread** for everything except:
  - `ModelRenderer::LoadModelAsync` spawns a loader thread (joined on cleanup)
  - `watchdog.exe` is a separate process
- All UI, input, rendering happen on the main thread
- This simplifies state management but limits performance

## 9. Security / Robustness

- Run as **Administrator** (required for hijacking high-privilege windows)
- Watchdog restores Explorer on crash (heartbeat + named event)
- Ctrl+Shift+Esc emergency Task Manager
- Stuck-in-2D force-clear after 600 frames

## 10. Rendering Pipeline Details

```
Clear (RTV color + DSV depth)
  → Skybox (invViewProj shader, cube geometry)
  → Model (Half-Lambert, normal maps, rim lighting)
  → Desktop cubes (CubeRenderer, per-cube hover state)
  → Holographic labels (3D→2D projection, ImGui bg_draw_list)
  → Marquee cage (spherical sector wireframe)
  → Portal (scissored viewport + folder camera + gray plane)
  → ImGui (taskbar, chrome, overlays)
  → Present
```

Coordinate system: **Left-handed**, world matrix = T(-Pivot) * S * R * T(Pos+Pivot).
