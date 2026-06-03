# CrossDim — Agent Guidelines

## Project Identity
纯原生 C++ 空间操作系统外壳 (Spatial OS Shell) — NOT a game engine, NOT a wallpaper app.

**当前阶段**：在 Explorer.exe 之上运行的 3D 空间覆盖层（overlay mode）。隐藏系统 taskbar，劫持窗口，渲染 3D 桌面。

**最终目标**：替代 Explorer.exe 成为 Windows 主 Shell。Explorer 降格为后台 COM 服务进程（托盘宿主、文件关联、Shell 扩展代理），CrossDim 接管桌面渲染、窗口管理、文件浏览。

### 当前完成度 vs 理论上限

| Explorer 子系统 | 权重 | 当前覆盖 | 公开 API 上限 | 缺口原因 |
|---|---|---|---|---|
| Taskbar + 托盘指示器 | 25% | ~55% | ~85% | 无法宿主第三方 tray 图标（`ITrayNotify` 未公开） |
| 桌面 / 文件图标 / 拖放 | 20% | ~30% | ~80% | 硬编码 14 个 app cube，无文件系统集成 |
| 文件管理器 | 25% | 0% | ~75% | 完全未开始 |
| 窗口管理 + Alt+Tab | 15% | ~35% | ~80% | 能劫持，有边缘吸附和快捷键，缺 Alt+Tab 替换/虚拟桌面 |
| Start Menu | 5% | ~40% | ~70% | 搜索框是摆设，无动态应用列表 |
| 通知中心 | 7% | 0% | ~25% | WinRT 通知管道受保护，只能做自建覆盖层 |
| 系统工具区 | 3% | ~60% | ~80% | 自绘电池/音量/网络/IME，形态已稳定 |

**当前加权覆盖度：~22% / 公开 API 理论上限：~76% / Explorer 降格架构理论上限：~95%**

## Tech Stack
- **Language:** C++17/20 (MSVC `cl.exe`, `/O2`, `/std:c++17`)

### Compiled source files (`tasks.json` explicit file list)
| File | Lines | Notes |
|---|---|---|
| `src/main.cpp` | 2126 | Entry point. `wWinMain`, state machine, taskbar UI, window chrome, raycasting, drag-drop, model debug |
| `src/Engine/Camera.cpp` | 69 | FPS-style camera (rotation only, `Move()` exists but never called) |
| `src/Engine/SkyboxRenderer.cpp` | 147 | Procedural skybox |
| `src/Engine/CubeRenderer.cpp` | 251 | 3D icon cubes + spherical sector selection |
| `src/Engine/ModelRenderer.cpp` | 350 | Async OBJ model loader + Half-Lambert shaded rendering |
| `src/Engine/ObjLoader.cpp` | 216 | High-perf OBJ/MTL parser with `.cdmesh` binary cache |
| `vendor/imgui/imgui*.cpp` | — | Core ImGui |
| `vendor/imgui/backends/imgui_impl_win32.cpp` | — | Win32 backend |
| `vendor/imgui/backends/imgui_impl_dx11.cpp` | — | DX11 backend |

### Header-only modules (included by `main.cpp`, not compiled separately)
- `src/Engine/Logger.h` (129 lines) — header-only singleton, `logs/` dir, auto-prune old logs.
- `src/Engine/TrayProxy.h` (75 lines) — header-only tray icon query helper.
- `src/Engine/TextureLoader.h` (212 lines) — header-only texture/icon utilities.
- `src/Shell/DesktopManager.h` (224 lines) — `AppCube`, desktop scan, persistence, search.
- `src/Shell/WindowManager.h` (235 lines) — window enumeration, hijack queue, icon cache.
- `src/Shell/SystemInfo.h` (122 lines) — audio, IME, network status.

## Architecture

### Dual State Machine
The app toggles between two modes (defined in `main.cpp:38-44`):
- **`STATE_3D_EXPLORE`** — mouse locked for FPS-style camera, raycast targeting, drag-and-drop of 3D icons, marquee selection.
- **`STATE_2D_WORKBENCH`** — mouse unlocked, ImGui taskbar and 2D UI interaction, hijacked legacy windows overlaid on the 3D scene.

Toggle between states: `Ctrl+Shift+X` registered as global `MOD_NOREPEAT` hotkey on `VK_TAB` (`main.cpp:733`). In 2D mode, pressing Tab closes all hijacked apps and returns to 3D.

Auto-return: when all hijacked windows close and no pending hijacks remain, automatically returns from 2D to 3D (`main.cpp:1896-1902`).

### Key global state (all in `main.cpp`)
| Variable | Role |
|---|---|
| `g_currentState` | Current mode enum (STATE_3D_EXPLORE / STATE_2D_WORKBENCH) |
| `g_uiUnlocked` | true when in 2D workbench (cursor visible, ImGui interactive) |
| `g_mainHwnd` | Main window handle (fullscreen WS_POPUP) |
| `g_pd3dDevice`, `g_pd3dDeviceContext`, `g_pSwapChain` | DX11 globals |
| `g_grabbedAppIndex`, `g_isDragging` | Drag state for 3D cube manipulation |
| `g_isBlankDragging` | Marquee/blank-drag spherical sector selection |
| `g_taskbarRect` | Taskbar bounding rect (updated every frame) |
| `g_hijackedWindows`, `g_pendingHijacks` | Window hijacking state |
| `g_taskbarIconCache`, `g_taskbarWindowIconCache` | D3D SRV icon caches |

### Render Loop Order
1. Frame timing + FOV logic (`main.cpp:810-870`)
2. Camera rotation from raw mouse input (`main.cpp:872-882`)
3. Pending hijack polling (`main.cpp:884-921`)
4. ImGui frame: State Monitor → Taskbar → Start Menu → Tray popup → Window Chrome → Model Debug → Loading overlay
5. Clear render target → Skybox → Model → Cubes + labels → Marquee frame → ImGui draw data → Present

### Matrix Conventions
World matrix build order: `T(-Pivot) * S * R * T(Pos + Pivot)` (`main.cpp:2038-2042`)
Left-handed coordinate system. OBJ importer converts right-handed → left-handed (flip Z, reverse winding).

## Final Architecture Goal: Explorer Downgrade

### Current (Overlay Mode)
```
CrossDim.exe (fullscreen WS_POPUP overlay)
═══════════════════════════════ FUNCTIONAL BARRIER
Explorer.exe (desktop + taskbar + shell COM server)
```
- **Losses**: tray icon hosting, shell context menus, file associations via DDE, shell namespace
- **Overhead**: GPU compositing two layers

### Target (Shell Mode)
```
CrossDim.exe ← Registered as Windows Shell
├── Window management (full control)
├── Desktop rendering (3D scene)
├── File browsing (via public IShellFolder API)
├── Taskbar + tray status (self-rendered, data from external)
└── File ops / associations (via ShellExecuteEx)
     │
Explorer.exe ← Background COM service (no desktop, no taskbar)
├── Hosts third-party tray icons (Discord/Steam/etc.)
├── Provides IContextMenu for file right-click
├── Handles DDE/file-association dispatch
└── Serves shell namespace extensions
```
- **Stays**: Explorer.exe runs silently, only as COM server — `ITrayNotify` and other undocumented interfaces remain functional
- **Gained**: single-layer GPU rendering, full control over window management, no functional barrier
- **Lost**: ~5% edge cases (some shell extensions execute in-process with Explorer and can't be proxied)

### Prerequisite: 5 Safety Nets (must complete BEFORE Shell registration)
1. **Watchdog process** (~80 lines) — standalone `watchdog.exe`, launched by CrossDim. If CrossDim heartbeat stops for 5s (named event), kills CrossDim, restores `Shell` registry key to `explorer.exe`, launches Explorer.
2. **Task Manager emergency path** — `Ctrl+Shift+Esc` global hotkey with maximum priority, bypassing all state logic.
3. **State machine exhaustion testing** — every state transition path verified (see Known Issues below).
4. **Tray icon proxy** (~300 lines) — hook into Explorer's tray toolbar to read icon state, render natively in CrossDim's taskbar.
5. **Startup splash + degraded recovery** — splash window on init; if D3D init fails within 10s, auto-restore Shell registry and launch Explorer.

## Known Issues (all resolved in v0.0.6)

| # | Issue | Status |
|---|---|---|
| 1 | `g_leftClicked` reset twice per frame | Fixed — removed duplicate reset |
| 2 | `g_taskbarWindowIconCache` never prunes dead HWNDs | Fixed — prune every 60 frames |
| 3 | 2D mode diagnostic spam → stuck-in-2D force-clear | Fixed — added force-clear at 600 frames |
| 4 | `GetAsyncKeyState` polling races with ImGui | Fixed — replaced with message-driven `g_ctrlHeld`/`g_lButtonHeld` |
| 5 | `ModelRenderer` destructor doesn't detach async load thread | Fixed — join thread in `Cleanup()` |
| 6 | `ModelRenderer::LoadModel()` dead declaration | Fixed — removed declaration |
| 7 | `m_pendingNormalPathW` never populated | Fixed — parse `map_Bump`/`bump` from MTL; keep fallback on load failure |
| 8 | `desktop.cddesk` no read/write code | Resolved — binary save/load implemented, positions persist across sessions |

## Feature Roadmap

### Phase 1 — Stability & Foundation (~620 lines, Debug: Med-High)
| # | Feature | Lines | Difficulty | Dependencies | Status |
|---|---|---|---|---|---|
| 1.1 | Config persistence (save/load pinned apps, cube positions, preferences via binary format) | ~280 | Medium | None | Partial — cube positions save/load via desktop.cddesk ✅. Pinned apps/preferences not persisted |
| 1.2 | Split `main.cpp` into ShellManager / TaskbarRenderer / CubeInteraction / WindowHijackManager | ~200 new | **High** | None | ✅ — split into `src/Shell/DesktopManager.h` (224L) / `WindowManager.h` (235L) / `SystemInfo.h` (122L). main.cpp 2612→2126 lines |
| 1.3 | Memory leak fixes (icon cache pruning, ModelRenderer thread detach) | ~60 | Low | None | ✅ |
| 1.4 | `GetAsyncKeyState` → message-queue-driven input tracking | ~80 | Medium | None | ✅ |

### Phase 2 — Desktop Completeness (~880 lines, Debug: Medium)
| # | Feature | Lines | Difficulty | Dependencies | Status |
|---|---|---|---|---|---|
| 2.1 | Dynamic desktop icon system (file picker, Start Menu scan, add/remove at runtime) | ~310 | Medium | 1.1 | Partial — save/load, Delete key removal, right-click context menu ✅. File picker/Start Menu scan still TODO |
| 2.2 | File system integration (file/folder cubes, Explorer drag-drop, SHGetFileInfo icons) | ~310 | Med-High | 2.1 | ✅ |
| 2.3 | Camera movement (WASD + scroll zoom, `Camera::Move()` already implemented) | ~100 | Low | None | Removed (excluded from design) |
| 2.4 | Search engine (hook existing Start Menu search box, substring filter on apps/cubes) | ~160 | Low-Med | 2.1 | ✅ |

### Phase 3 — Window Management (~740 lines, Debug: Med-High)
| # | Feature | Lines | Difficulty | Dependencies | Status |
|---|---|---|---|---|---|
| 3.1 | Window snapping (edge snap, keyboard shortcuts, visual indicators) | ~220 | Medium | 1.2 | ✅ — drag-to-edge half/maximize/quarter, blue preview overlay, Ctrl+Alt+Arrows shortcuts |
| 3.2 | Alt+Tab replacement (DWM thumbnails, 3D spatial switcher) | ~280 | **High** | None | — |
| 3.3 | Virtual desktops (multiple workspaces, transition animation, per-desktop cube sets) | ~240 | Med-High | 1.1 | — |

### Phase 4 — Polish & Enhancement (~445 lines, Debug: Low-High)
| # | Feature | Lines | Difficulty | Dependencies | Status |
|---|---|---|---|---|---|
| 4.1 | Enable BloomRenderer (add to tasks.json, integration call) | ~35 | Low | None | Removed (abandoned) |
| 4.2 | MTL parser: normal map (`map_Bump`) support, optional PBR shader upgrade | ~140 | Low-Med | None | ✅ |
| 4.3 | Dynamic system tray (enumerate background processes, replace hardcoded items) | ~90 | Low | None | ✅ |
| 4.4 | Shader hot-reload (file watcher thread + D3D pipeline recompile) | ~180 | **High** | None | — |

### Phase 5 — Safety Nets & Shell Transition
| # | Feature | Lines | Difficulty | Dependencies | Status |
|---|---|---|---|---|---|
| 5.1 | Watchdog process | ~80 | Medium | None | ✅ |
| 5.2 | Task Manager emergency hotkey | ~30 | Low | None | ✅ |
| 5.3 | Tray icon proxy (Explorer toolbar subclassing) | ~300 | **High** | None | ✅ |
| 5.4 | Shell registration module (registry key swap, Explorer variant launch) | ~150 | Medium | 5.1-5.3 | — |
| 5.5 | State machine verification | ~150 | Medium | None | — |

### Phase 6 — Long-term (~730 lines)
| # | Feature | Lines | Difficulty |
|---|---|---|---|
| 6.1 | Notification overlay system | ~210 | Medium |
| 6.2 | Keyboard navigation (Tab between cubes, arrow keys in taskbar) | ~100 | Low |
| 6.3 | Multi-monitor support (per-monitor viewport + taskbar) | ~210 | **High** |
| 6.4 | Accessibility (high contrast, UIA text exposure) | ~100 | Medium |
| 6.5 | File manager module (shell namespace browsing, copy/paste/delete, properties panel) | ~3000-5000 | **Very High** |

## Commit Message Convention
All commits MUST follow this format:

```
<type>: <brief description> v<version>

<type>   — one of: feat, fix, refactor, chore, docs
<brief>  — concise summary, Chinese or English, max 60 chars
<version>— semantic version tag (vMAJOR.MINOR.PATCH)
```

**Examples:**
```
feat: cube position persistence + right-click context menu v0.0.7
fix: fix g_leftClicked double-reset issue v0.0.6
refactor: split main.cpp into Shell modules v0.0.7
chore: unify logs to logs/ dir + auto-prune v0.0.7
```

- **Tags** are lightweight: `git tag v0.0.7`
- **Version bumps**: PATCH for fixes/refactors, MINOR for new features, MAJOR for shell-mode switch
- **Multi-commit versions**: append `-rc1`, `-rc2` etc. before the final tag

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

### 5. Every new module must be reversible
Before registering CrossDim as the system shell, every change must work in overlay mode. The Shell registration step is the final irreversible switch — all preceding work must be rock-solid.

### 6. No undocumented API reliance in core paths
Undocumented COM interfaces (`ITrayNotify`, etc.) may be used for tray icon proxy (Phase 5 safety net), but core functionality (rendering, input, window management) must use only documented Win32/DX11 APIs.
