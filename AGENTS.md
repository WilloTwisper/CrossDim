# CrossDim — Agent Guidelines

## Project Identity
纯原生 C++ 空间操作系统外壳 (Spatial OS Shell) — NOT a game engine, NOT a wallpaper app.
Takes over the Windows desktop: hides the native taskbar, hijacks legacy windows via DWM, and renders a 3D spatial workspace on top.

## Tech Stack
- **Language:** C++17/20 (MSVC `cl.exe`, `/O2`)
- **Graphics:** DirectX 11
- **UI:** Dear ImGui (Docking branch in `vendor/imgui/`)
- **Math:** DirectXMath, DirectXCollision
- **System:** Win32 API, DWM API, COM (IAudioEndpointVolume, IMM32, Iphlpapi)
- **No external deps** — no Assimp, no Boost, no CMake, no .sln

## Build & Run
- **Prerequisite:** Visual Studio 2022 Build Tools with C++ desktop workload.
  Compiler path set in `.vscode/settings.json` — update if your MSVC version differs.
- **Build:** `Ctrl+Shift+B` in VS Code (runs `tasks.json`).
- **Output:** `build\CrossDim.exe`
- **Must run as Administrator** — required for hijacking high-privilege windows (Task Manager, etc.).
- **Include paths:** `vendor/imgui`, `src`
- **Linker libs** (via `#pragma comment(lib)` in `main.cpp`): `d3d11.lib`, `user32.lib`, `ole32.lib`, `imm32.lib`, `iphlpapi.lib`, `dwmapi.lib`, `d3dcompiler.lib`

### Compiled source files (`tasks.json` explicit file list)
| File | Notes |
|---|---|
| `src/main.cpp` | Entry point (2213 lines). `wWinMain`, state machine, taskbar, window hijacking, raycasting |
| `src/Engine/Camera.cpp` | FPS-style camera |
| `src/Engine/SkyboxRenderer.cpp` | Procedural skybox |
| `src/Engine/CubeRenderer.cpp` | 3D icon cubes + spherical sector selection |
| `src/Engine/ModelRenderer.cpp` | Async OBJ model loader + shaded rendering |
| `src/Engine/ObjLoader.cpp` | High-perf OBJ/MTL parser with `.cdmesh` binary cache |
| `vendor/imgui/imgui*.cpp` | Core ImGui |
| `vendor/imgui/backends/imgui_impl_win32.cpp` | Win32 backend |
| `vendor/imgui/backends/imgui_impl_dx11.cpp` | DX11 backend |

### Source files NOT in the build
- `src/Engine/BloomRenderer.cpp` / `.h` — exists in source tree but **NOT compiled**. If you modify it, add it to `tasks.json` to see effects.
- `src/Engine/Logger.h` — header-only singleton, included by `main.cpp`.
- `src/Engine/TextureLoader.h` — header-only texture/icon utilities, included by `main.cpp`.

No tests exist. No lint/formatter config at project root.

## Architecture
### Dual State Machine
The app toggles between two modes (defined in `main.cpp`):
- **`STATE_3D_EXPLORE`** — mouse locked for FPS-style camera, raycast targeting, drag-and-drop of 3D icons.
- **`STATE_2D_WORKBENCH`** — mouse unlocked, ImGui taskbar and 2D UI interaction.

Toggle between states: `Ctrl+Shift+X` (registered as global hotkey).

### Key global state (all in `main.cpp`)
- `g_currentState` — current mode enum
- `g_uiUnlocked` — true when in 2D workbench
- `g_mainHwnd` — main window handle
- `g_pd3dDevice`, `g_pd3dDeviceContext`, `g_pSwapChain` — DX11 globals
- `g_grabbedAppIndex`, `g_isDragging` — drag state
- `g_taskbarRect` — taskbar bounding rect

## Strict Rules
### 1. Never block the main thread
Win32 API polling, model loading, and any I/O must use async patterns — state machines (e.g. `frameWait` counters) or `std::thread`. The render loop runs every frame; stalling it freezes the entire desktop shell.

### 2. Matrix math conventions
World matrix build order: `T(-Pivot) * S * R * T(Pos + Pivot)`
For 3D→2D UI projection: use inverse `ViewProjection` to project 3D coords into screen space, then render with ImGui in a 2D pass.

### 3. Keep it pure
Do not add third-party libraries (Assimp, Boost, etc.). Prefer Win32 API, COM, DirectXMath, or lightweight self-contained code.

### 4. Game engine mindset is wrong
This is an OS shell. Do not introduce game-engine patterns (scene graphs, entity-component systems, physics engines). Classic desktop metaphors (drag, multi-select, taskbar) must map naturally into 3D space.
