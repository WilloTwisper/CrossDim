# CrossDim

**The World Outside Windows.**

![C++](https://img.shields.io/badge/Language-C++17-blue.svg)
![DirectX](https://img.shields.io/badge/Graphics-DirectX%2011-lightgreen.svg)
![ImGui](https://img.shields.io/badge/UI-Dear%20ImGui-red.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6.svg)
![Version](https://img.shields.io/badge/Version-v0.0.7-informational.svg)

CrossDim is a native C++ spatial OS shell. Currently runs as a 3D overlay on top of Explorer.exe; the end goal is to replace Explorer as the primary Windows shell.

## Philosophy

We translate classic desktop metaphors (taskbar, icons, window management, drag-and-drop) into a 3D spatial environment — not a game engine. Every interaction must feel intuitive and productive. The design follows: **"same mental model, spatial execution."**

## Current State (v0.0.7)

| Subsystem | Coverage |
|---|---|
| Desktop (3D icon cubes, spherical layout, drag-and-drop) | ~30% |
| Window management (hijack, chrome, edge snapping) | ~35% |
| Taskbar + system tray (IME, audio, network, battery) | ~55% |
| Start menu + search | ~40% |
| File system integration (scan, icon load, persistence) | ~30% |
| Safety nets (watchdog, emergency hotkey) | 100% |

**Weighted coverage: ~22%** of the full shell vision.

## Features

- **DirectX 11 native rendering** — skybox, 3D icon cubes, OBJ model loading with Half-Lambert shading and normal maps
- **Window hijacking** — intercepts Win32 windows, strips legacy chrome, embeds them in 2D workbench mode with custom title bar
- **ImGui taskbar** — replaces the Windows taskbar: pinned apps, running window list, start menu, system tray popup
- **System indicators** — real-time audio volume, IME input method, network status, battery, clock via COM/Win32 APIs
- **3D cube interaction** — FPS-style raycast targeting, drag-and-drop with collision physics, marquee spherical sector selection, Ctrl+click multi-select
- **Window snapping** — drag to screen edges for half/full/quarter layouts, Ctrl+Alt+Arrow shortcuts, blue preview overlay
- **Desktop persistence** — cube positions saved to `desktop.cddesk`; external drag-drop cubes survive restarts; Delete key and right-click context menu for cube removal
- **Safety nets** — watchdog process monitors heartbeat and restores Explorer on crash; Ctrl+Shift+Esc emergency Task Manager hotkey

## Project Structure

```
src/
├── main.cpp                          # Entry point, state machine, WndProc, render loop
├── Engine/
│   ├── Camera.h / .cpp               # FPS-style camera
│   ├── SkyboxRenderer.h / .cpp       # Procedural skybox
│   ├── CubeRenderer.h / .cpp         # 3D icon cube geometry
│   ├── ModelRenderer.h / .cpp        # Async OBJ model loader + shader
│   ├── ObjLoader.h / .cpp            # OBJ/MTL parser with .cdmesh cache
│   ├── Logger.h                      # Header-only singleton (logs/ dir, auto-prune)
│   ├── TextureLoader.h               # Header-only D3D texture/icon utilities
│   └── TrayProxy.h                   # Header-only Explorer tray icon reader
├── Shell/
│   ├── DesktopManager.h              # AppCube, desktop scan, persistence, search
│   ├── WindowManager.h               # Window enumeration, hijack queue, icon cache
│   └── SystemInfo.h                  # Audio, IME, network, battery status
vendor/
└── imgui/                            # Dear ImGui (Docking branch)
```

## Build

- **Prerequisite:** Visual Studio 2022 Build Tools with C++ desktop workload
- **Build:** `Ctrl+Shift+B` in VS Code (runs `.vscode/tasks.json`)
- **Output:** `build\CrossDim.exe`
- **Flags:** `/O2 /EHsc /std:c++17`
- **Must run as Administrator** for high-privilege window hijacking

## License

MIT. See [LICENSE](LICENSE). Dear ImGui included under MIT license.
