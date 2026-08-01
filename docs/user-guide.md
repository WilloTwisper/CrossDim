# CrossDim — User Guide

## 1. Getting Started

- **Run as Administrator** — required for window hijacking and taskbar hiding.
- CrossDim hides the Windows taskbar and replaces it with its own 3D-aware taskbar.

## 2. Hotkey Reference

### 2.1 Global Hotkeys

| Hotkey | Function |
|---|---|
| `Tab` / `Ctrl+Shift+X` | Toggle 3D explore / 2D workbench mode |
| `Ctrl+Shift+Esc` | Launch Task Manager (emergency) |
| `Ctrl+Shift+D` | Dump cube layout to log |
| `Ctrl+Shift+T` | Toggle Task View (virtual desktop overview) |
| `Ctrl+Tab` | Alt+Tab replacement (3D card window switcher) |
| `Esc` | Quit application |

### 2.2 Window Management

| Hotkey | Function |
|---|---|
| `Ctrl+Alt+←` | Snap focused window to left half |
| `Ctrl+Alt+→` | Snap focused window to right half |
| `Ctrl+Alt+↑` | Maximize focused window |
| `Ctrl+Alt+↓` | Restore focused window |
| `Ctrl+Alt+1-9` | Switch to virtual desktop N |

### 2.3 Virtual Desktop (Task View)

| Hotkey | Function |
|---|---|
| `Ctrl+Shift+T` | Open/close Task View overview |
| `Ctrl+Alt+1-9` | Direct switch to desktop N |
| `Esc` | Close Task View |
| Click `+` in Task View | Create new desktop |
| Click `×` on desktop card | Close that desktop |

### 2.4 3D Cube Interaction

| Action | Function |
|---|---|
| Click cube | Select (deselects others) |
| Ctrl+Click | Additive multi-select |
| Double-click | Launch app / enter folder |
| Drag cube | Move it in 3D space |
| Scroll while dragging | Adjust distance from camera |
| Drag empty space | Marquee (spherical sector) selection |
| `Delete` | Remove selected cubes |
| Right-click cube | Context menu (Launch / Remove) |

### 2.5 Spatial File Box (Portal)

| Action | Function |
|---|---|
| Double-click folder cube | Open folder in portal window |
| Click cube inside portal | Select |
| Double-click folder inside portal | Navigate deeper |
| Drag portal title bar | Move portal window |
| Click `[×]` on portal | Exit folder, return to desktop |
| Click `[□]` | Toggle portal windowed/maximized |
| Breadcrumb `Desktop` button | Exit to desktop |

## 3. Taskbar

- **Left**: pinned apps (Start, Search, Files, Edge, Terminal, Code)
- **Center**: running windows (click to focus, dot indicator for running)
- **Right**: system indicators
  - Battery (with charging bolt)
  - Volume (click to adjust)
  - Network (Wi-Fi/Ethernet bars)
  - IME (中/英, 拼/ENG — click to toggle)
  - Clock + date
  - Task View button (multi-window icon)
  - Tray chevron (^) — opens system tray popup

## 4. Start Menu

- Click Start button on taskbar
- Search box filters app tiles + 3D cubes in real time
- Click tile to launch

## 5. Window Management

### 5.1 Hijacked Windows
When you launch an app from a 3D cube, CrossDim:
1. Strips the native title bar
2. Adds a custom ImGui chrome (close/maximize/minimize)
3. Embeds the window in 2D workbench mode

### 5.2 Window Snapping
- Drag a window to the screen edge → blue preview → release to snap
- Left/right edge → half screen
- Top edge → maximize
- Corners → quarter screen
- `Ctrl+Alt+Arrows` for keyboard snapping

## 6. Virtual Desktops

Each desktop has:
- Its own set of 3D cubes (persisted to `desktop_N.cddesk`)
- Its own set of open windows (windows hidden when switching away)
- Independent state

## 7. Files & Persistence

| File | Purpose |
|---|---|
| `desktop.cddesk` | Desktop 0 cube positions (binary) |
| `desktop_N.cddesk` | Virtual desktop N cube positions |
| `logs/crossdim_*.log` | Log files (auto-pruned, keep last 10) |
| `imgui.ini` | ImGui layout state |

## 8. Troubleshooting

| Symptom | Fix |
|---|---|
| No icons on cubes | Check `logs/` for "No icon" messages |
| Windows won't hijack | Run as Administrator |
| Desktop cubes missing after restart | Delete `desktop.cddesk` (corrupt state) |
| D: drive files on desktop | Delete `desktop.cddesk` (old bug pollution) |
| Everything frozen | `Ctrl+Shift+Esc` for Task Manager; watchdog auto-restores |
| Taskbar not hidden | Check Explorer is running normally |
