# CrossDim — State Machines & Transitions

## 1. Dual Mode (3D Explore / 2D Workbench)

### States
```
STATE_3D_EXPLORE    cursor locked, raw input → camera rotation, raycast targeting
STATE_2D_WORKBENCH  cursor visible, ImGui interactive, hijacked windows active
```

### Transition: 3D → 2D
| Trigger | Path |
|---|---|
| Tab hotkey | `toggleUiUnlock` → g_uiUnlocked flips, cursor shown |
| Launch EXE cube | `LaunchAppByPath` → hijack queue pushed → state=2D |
| Open Task View | Ctrl+Shift+T → if 3D, toggle first → overview |

### Transition: 2D → 3D
| Trigger | Path |
|---|---|
| Tab hotkey | `toggleUiUnlock` → cursor hidden, raw input resumed |
| All windows closed | auto-return: `g_hijackedWindows.empty() && g_pendingHijacks.empty()` |
| Stuck force-clear | 600 frames in 2D with no windows → force 3D |

### Invariants
- `g_uiUnlocked == false` ⟺ cursor hidden + clip to screen
- `STATE_2D_WORKBENCH ⟹ g_uiUnlocked == true` (except force-clear edge)

## 2. Window Hijack Queue

### States
```
PENDING (in g_pendingHijacks) → HIJACKED (in g_hijackedWindows) → CLOSED
```

### Flow
```
1. LaunchAppByPath → CreateProcessW → PendingHijack{frameWait=0}
2. Per-frame polling:
   - frameWait++
   - FindWindowFromProcessId(pid)
   - Found? → strip WS_CAPTION, add WS_THICKFRAME, center 1100x750 → HIJACKED
   - frameWait > 120 → TIMEOUT, drop
3. Window closed (X button / user) → ShowWindow(SW_HIDE) + WM_CLOSE → removed from list
```

### Edge Cases
- App shows window without WS_CAPTION (modern apps) → immediately abandon, back to 3D
- Multiple windows per process → only first with title is captured
- CrossDim's own window excluded (hwnd == target check)

## 3. Virtual Desktop Switching

### States
```
IDLE ──SwitchToDesktop(i)──▶ TRANSITIONING (0 < t < 1) ──t≥1──▶ IDLE (desktop=i)
```

### Flow
```
SwitchToDesktop(i):
  1. SaveDesktopState(current)
  2. Hide all windows: ShowWindow(SW_HIDE)
  3. g_desktopWindows[current] = g_hijackedWindows
  4. Set target, transition t=0

Per-frame:
  t += speed * dt (speed=4.0, ~250ms)
  At t=0.5: CompleteDesktopSwitch()
    - Release icons, clear g_myApps
    - g_hijackedWindows = g_desktopWindows[target]
    - ShowWindow(SW_SHOW) all target windows
    - ScanDesktopForApps + LoadDesktopState(target) + LoadIconsForApps
  At t=1.0: finish, reset

CreateNewDesktop:
  - Hide current windows, save to cache
  - New empty window list, g_myApps rescanned
  - New desktop becomes active

CloseDesktop(i):
  - If active: minimize its windows, fallback to neighbor, swap state
  - If inactive: remove slot + cache entry, shift indexes
```

### Invariants
- `g_desktopSlots.size() == g_desktopWindows.size()`
- Active desktop windows are VISIBLE; all others HIDDEN
- Cube sets are independent per desktop (persisted to desktop_N.cddesk)

## 4. Folder Navigation (Portal)

### States
```
DESKTOP (stack empty)
  │ EnterFolder()
  ▼
FOLDER_L1 (stack=[L1])          ← portal windowed OR maximized
  │ EnterFolder()
  ▼
FOLDER_L2 (stack=[L1,L2])
  │ ExitFolderToParent()
  ▼
FOLDER_L1
  │ ExitFolderToRoot()
  ▼
DESKTOP
```

### Flow
```
EnterFolder(path):
  if stack empty:
    save desktop cubes → g_desktopBackupCubes (+ camera)
  else:
    save current folder state → stack.back().cubes
  scan new folder → g_folderCubes (windowed) / g_myApps (maximized)
  push FolderState
  set up portal camera

ExitFolderToParent:
  release g_folderCubes icons
  pop stack
  if stack empty → restore desktop (+ camera), g_isInFolder=false
  else → rescan parent for portal

ExitFolderToRoot:
  release g_folderCubes, clear stack
  restore desktop + camera
```

### Mode Toggle
```
MAXIMIZED ⇄ WINDOWED (via [□] button or Tab)
MAXIMIZED → WINDOWED: save cubes to stack, desktop back to g_myApps, portal shows folder
WINDOWED → MAXIMIZED: desktop saved, g_myApps = folder cubes, camera animates in
```

### Invariants
- `g_isInFolder == (g_folderStack.size() > 0)`
- Desktop cubes always in `g_desktopBackupCubes` while in folder
- `g_folderStack[i].cubes` always has IconTexture == nullptr (re-loaded on restore)

## 5. Cube Drag & Selection

### States
```
IDLE ──click cube──▶ SELECTED ──drag 150ms──▶ DRAGGING ──release──▶ IDLE
IDLE ──click empty──▶ MARQUEE ──release──▶ SELECTED ──▶ IDLE
```

### Click Logic
```
Single click (no ctrl): deselect all, select clicked
Ctrl+click: toggle clicked selection
Double-click (<400ms, same cube): launch / enter folder
Click empty: deselect all
```

### Drag Logic
```
MouseDown on cube → g_grabbedAppIndex set, g_mouseDownTime = now
After 150ms hold → g_isDragging = true
Each frame while dragging:
  newPos = rayOrigin + rayDir * g_dragDistance
  delta = newPos - cubePos
  for all selected: pos += delta
  collision: non-selected cubes push back (COL_RADIUS=0.4, REPEL_DIST=1.2)
  drop-target: nearest non-selected cube within 1.5 units
Scroll during drag → adjust g_dragDistance (3..20)
Release:
  if drop-target is .exe and dragged is file → ShellExecuteW("open", exe, file)
```

### Marquee Logic
```
MouseDown on empty → g_isBlankDragging, record start yaw/pitch
Each frame: accumulate yaw/pitch delta from ray direction
Select all cubes whose yaw/pitch fall within the extents
Ctrl held: additive toggle (WasSelected restore)
Release: finalize selection, clear marquee
Visual: spherical sector wireframe cage (front 7.6, back 8.4)
```

## 6. Alt+Tab Switcher

### States
```
INACTIVE ──Ctrl+Tab──▶ ACTIVE (list built) ──Ctrl released──▶ SWITCH & INACTIVE
                              │
                              └──Tab──▶ next card
                              └──Esc──▶ INACTIVE (no switch)
```

### Flow
```
Ctrl+Tab (hotkey 5):
  build list from g_hijackedWindows (visible only)
  if empty → stay inactive
  g_showWindowSwitcher = true, selected = 0

Active:
  Tab (WM_KEYDOWN) → selected = (selected+1) % n
  Esc → dismiss without switching
  Ctrl release (WM_KEYUP) → SetForegroundWindow(selected) + dismiss

Render:
  dark overlay + 3D cards in arc + title labels
```

## 7. Task View (Desktop Overview)

### States
```
HIDDEN ──Ctrl+Shift+T / taskbar btn──▶ SHOWING ──Esc / click empty──▶ HIDDEN
```

### Flow
```
Show: (if 3D mode, toggle to 2D first)
  - sync g_desktopWindows[active] = g_hijackedWindows
  - fullscreen acrylic overlay
  - top: window cards (click → focus + dismiss)
  - bottom: desktop bar (click → switch, × → close, + → create)
Dismiss: Esc, click empty area, or select item
```

## 8. State Transition Test Matrix

| # | From | Trigger | Expected | Status |
|---|---|---|---|---|
| 1 | 3D | Tab | 2D, cursor visible | ✅ |
| 2 | 2D | Tab | 3D, cursor hidden | ✅ |
| 3 | 3D | Launch EXE | 2D + hijack window | ✅ |
| 4 | 2D | Close all windows | Auto 3D | ✅ |
| 5 | 2D | 600f no windows | Force 3D | ✅ |
| 6 | 3D | Ctrl+Tab | Switcher overlay | ✅ |
| 7 | Switcher | Tab | Cycle selection | ✅ |
| 8 | Switcher | Ctrl release | Switch + dismiss | ✅ |
| 9 | Switcher | Esc | Dismiss no switch | ✅ |
| 10 | Any | Ctrl+Shift+T | Task View | ✅ |
| 11 | TaskView | Create desktop | +1 desktop active | ✅ |
| 12 | TaskView | Close desktop | -1 desktop, fallback | ✅ |
| 13 | 3D | Double-click folder | Portal window | ⚠️ bugs known |
| 14 | Portal | Double-click subfolder | Deeper level | ⚠️ |
| 15 | Portal | Close btn | Back to desktop | ⚠️ |
| 16 | Any | Ctrl+Shift+D | Log dump | ✅ |

## 9. Known State Bugs

1. **Portal cursor lock reverted** — the "locked to portal center" implementation was rolled back due to instability; currently free-cursor mode with portal interaction only when mouse inside window.
2. **Folder exit may leave stale state** — `g_desktopBackupCubes` not always fresh if desktop modified while in portal.
3. **Switcher list stale** — built at Ctrl+Tab press; windows opened/closed after won't update until next invocation.
4. **TaskView sync edge** — `g_desktopWindows[active] = g_hijackedWindows` assumes active index valid.
