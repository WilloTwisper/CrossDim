# CrossDim — Development Guide

## 1. Environment Setup

### Prerequisites
- **Visual Studio 2022 Build Tools** with C++ desktop workload
- **MSVC compiler** at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\<version>\bin\Hostx64\x64\cl.exe`
- Compiler path configured in `.vscode/settings.json` (update if version differs)

### Build
```bash
# Via VS Code
Ctrl+Shift+B

# Manual (PowerShell)
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe" `
  /utf-8 /O2 /Zi /EHsc /std:c++17 /nologo `
  /I "vendor\imgui" /I "src" `
  src\main.cpp src\Engine\Camera.cpp src\Engine\SkyboxRenderer.cpp `
  src\Engine\CubeRenderer.cpp src\Engine\ObjLoader.cpp src\Engine\ModelRenderer.cpp `
  vendor\imgui\imgui.cpp vendor\imgui\imgui_draw.cpp `
  vendor\imgui\imgui_tables.cpp vendor\imgui\imgui_widgets.cpp `
  vendor\imgui\backends\imgui_impl_win32.cpp vendor\imgui\backends\imgui_impl_dx11.cpp `
  d3dcompiler.lib /Fe:build\CrossDim.exe /Fd:build\ /Fo:build\
```

### Watchdog Build
```
cl.exe /O2 /EHsc src\watchdog.cpp advapi32.lib /Fe:build\watchdog.exe
```

## 2. Project Structure

```
src/
├── main.cpp                    # Entry point, state machine, WndProc, render loop (3126 lines)
├── watchdog.cpp                # Watchdog process (heartbeat monitor)
├── Engine/
│   ├── Camera.h/.cpp           # FPS camera
│   ├── SkyboxRenderer.h/.cpp   # Procedural skybox
│   ├── CubeRenderer.h/.cpp     # 3D cubes + marquee cage
│   ├── ModelRenderer.h/.cpp    # Async OBJ model + Half-Lambert
│   ├── ObjLoader.h/.cpp        # OBJ/MTL parser
│   ├── Logger.h                # Singleton logger (header-only)
│   ├── TextureLoader.h         # Icon→D3D texture (header-only)
│   └── TrayProxy.h             # Explorer tray reader (header-only)
├── Shell/
│   ├── DesktopManager.h        # AppCube, desktop scan, persistence
│   ├── WindowManager.h         # Window enumeration, hijack, icons
│   └── SystemInfo.h            # Audio/IME/network
vendor/
└── imgui/                      # Dear ImGui (Docking)
```

## 3. Build Flags

| Flag | Purpose |
|---|---|
| `/O2` | Maximum optimization |
| `/Zi` | Debug info (PDB) |
| `/EHsc` | C++ exceptions, C only |
| `/std:c++17` | C++17 standard (inline variables) |
| `/utf-8` | UTF-8 source encoding |
| `/I vendor\imgui /I src` | Include paths |

## 4. Debugging

### 4.1 Logs
- All logs go to `logs/crossdim_<timestamp>.log` (exe directory)
- Also output to OutputDebugString (DebugView / VS Output)
- Auto-prune keeps last 10 files
- Key log tags:
  - `[desk]` — desktop scanning
  - `[hijack]` — window hijacking
  - `[drop]` — drag-drop operations
  - `[state]` — state machine transitions
  - `[force]` — force-clear watchdog
  - `[dmp]` — cube layout dump

### 4.2 Hotkeys for Debug
- `Ctrl+Shift+D` — dump cube layout to log
- Model Debug panel (2D mode) — transform/FOV/material controls
  - "Reload Desktop Apps" button — rescan + reload state

### 4.3 Common Debug Workflows
1. **Cubes not showing**: check `logs/` for `[desk]` + "No icon" messages
2. **Window won't hijack**: check `[hijack]` log; ensure running as admin
3. **State stuck in 2D**: check `[force]` log for the 600-frame force-clear
4. **Persistence issues**: delete `desktop.cddesk` to reset

## 5. Code Conventions

### 5.1 Naming
- Globals: `g_<name>` (e.g., `g_myApps`)
- Statics: `s_<name>` or `g_` for file-scope
- Members: `m_<name>` (in classes)
- Functions: PascalCase for public, camelCase for local

### 5.2 Global State Rules
- Never block the main thread (I/O must be async)
- All state is file-scope in main.cpp unless in a module header
- Modules use `inline` globals (C++17) or `extern` declarations

### 5.3 Rendering Conventions
- World matrix: `T(-Pivot) * S * R * T(Pos+Pivot)`
- Left-handed coordinate system
- 3D→2D projection: inverse ViewProj → screen space → ImGui draw

### 5.4 Adding a New Hotkey
1. Register in wWinMain: `RegisterHotKey(hwnd, <id>, <mods>, <key>)`
2. Handle `WM_HOTKEY && wParam == <id>` in WndProc
3. Add `UnregisterHotKey(hwnd, <id>)` in cleanup

### 5.5 Adding a New Cube Action
1. Extend right-click context menu in render loop
2. Add handler function (file op, launch, etc.)
3. If persistent: hook into SaveDesktopState/LoadDesktopState

## 6. Memory Management Rules

### 6.1 Icon Texture Ownership
- Every `ID3D11ShaderResourceView*` must have exactly one Release
- When copying cube vectors, ALWAYS null out IconTexture in the copy:
  ```cpp
  std::vector<AppCube> backup = g_myApps;
  for (auto& c : backup) c.IconTexture = nullptr;  // critical!
  ```
- Never copy cubes with live textures unless you control lifetime

### 6.2 Folder Navigation
- `g_folderStack[i].cubes` must have IconTexture = nullptr (they're re-loaded on restore)
- `g_folderCubes` owns live textures while portal is open
- On exit: release g_folderCubes first, then g_myApps, then restore backup

## 7. Adding New System Tray Indicators

1. In taskbar rendering section, find `trayColor`/`trayMuted` declarations
2. Add your indicator drawing before the clock
3. Update `rightAreaWidth` calculation to reserve space
4. Use `drawBatteryIcon` / `drawVolumeIcon` / `drawNetBarsIcon` as templates

## 8. Testing Checklist (Before Commit)

- [ ] Build passes with `/O2 /std:c++17`
- [ ] Launch as admin; taskbar hidden
- [ ] Desktop cubes appear with icons
- [ ] Double-click EXE launches + hijacks (chrome appears)
- [ ] Window snapping works (drag to edges)
- [ ] Ctrl+Tab shows switcher, Tab cycles, release switches
- [ ] Ctrl+Shift+T Task View: create/switch/close desktops
- [ ] Double-click folder: portal opens, cubes visible
- [ ] Portal close returns to desktop
- [ ] Restart: desktop.cddesk restores positions
- [ ] Logs in logs/ dir, auto-prune works
- [ ] Esc quits; watchdog restores Explorer

## 9. Versioning

- Commit format: `<type>: <description> vX.Y.Z`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`
- Tags: `git tag vX.Y.Z`
- PATCH = fixes/refactors, MINOR = new features, MAJOR = shell-mode switch
