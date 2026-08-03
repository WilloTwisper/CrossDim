# CrossDim — Testing Guide

## 1. Test Environment

- Windows 10/11 x64
- Run CrossDim as **Administrator**
- Clean state: delete `desktop.cddesk`, `desktop_*.cddesk` before testing persistence
- Verify `logs/` is populated after launch

## 2. Regression Test Matrix

### 2.1 Startup & Core
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 1 | Clean startup | Delete .cddesk, launch | Desktop cubes appear, icons visible | ☐ |
| 2 | Startup with existing state | Drag cubes, exit, relaunch | Positions restored | ☐ |
| 3 | Taskbar hidden | Launch | No Explorer taskbar visible | ☐ |
| 4 | Watchdog launch | Launch | watchdog.exe running in Task Manager | ☐ |
| 5 | Logger created | Launch | logs/crossdim_*.log exists | ☐ |

### 2.2 State Machine
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 6 | 3D→2D toggle | Press Tab | Cursor visible, taskbar interactive | ☐ |
| 7 | 2D→3D toggle | Press Tab again | Cursor hidden, locked | ☐ |
| 8 | Auto-return | Launch app, close all windows | Auto back to 3D | ☐ |
| 9 | Force-clear | Open 2D, close windows quickly | Force back to 3D (no hang) | ☐ |

### 2.3 Cube Interaction
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 10 | Select | Click cube | Selected (blue) | ☐ |
| 11 | Multi-select | Ctrl+click multiple | All selected | ☐ |
| 12 | Drag | Drag cube 200ms | Cube follows ray | ☐ |
| 13 | Drag distance | Scroll while dragging | Cube moves closer/farther | ☐ |
| 14 | Marquee | Click empty, drag | Sector selection highlights cubes | ☐ |
| 15 | Delete | Select, press Delete | Cubes removed | ☐ |
| 16 | Right-click | Right-click cube | Menu appears | ☐ |
| 17 | Search | Type in start search | Non-matching cubes dim | ☐ |

### 2.4 Window Management
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 18 | Launch EXE | Double-click exe cube | Window hijacked, chrome visible | ☐ |
| 19 | Close via chrome | Click red X | Window closes, app exits | ☐ |
| 20 | Minimize via chrome | Click green _ | Window minimized | ☐ |
| 21 | Maximize via chrome | Click yellow □ | Window maximized | ☐ |
| 22 | Snap left | Drag window to left edge | Half-screen snap | ☐ |
| 23 | Snap right | Drag to right edge | Half-screen snap | ☐ |
| 24 | Snap top | Drag to top | Maximize | ☐ |
| 25 | Snap corner | Drag to corner | Quarter-screen | ☐ |
| 26 | KB snap | Ctrl+Alt+→ | Window snaps right | ☐ |
| 27 | Alt+Tab | Ctrl+Tab | 3D cards appear | ☐ |
| 28 | Alt+Tab cycle | Tab while switcher open | Selection cycles | ☐ |
| 29 | Alt+Tab switch | Release Ctrl | Window focused | ☐ |
| 30 | Alt+Tab cancel | Esc while switcher | Dismiss, no switch | ☐ |

### 2.5 Virtual Desktops
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 31 | Task View open | Ctrl+Shift+T | Overview overlay | ☐ |
| 32 | Create desktop | Click + | New desktop, active | ☐ |
| 33 | Switch desktop | Click other card | Windows hidden/shown correctly | ☐ |
| 34 | Close desktop | Click × on non-active | Desktop removed | ☐ |
| 35 | KB switch | Ctrl+Alt+2 | Switch to desktop 2 | ☐ |
| 36 | Per-desktop cubes | Move cube on desk 1, switch | Desk 2 unaffected | ☐ |

### 2.6 Spatial File Box
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 37 | Open folder | Double-click folder cube | Portal window with folder cubes | ☐ |
| 38 | Portal interaction | Click cube in portal | Select works | ☐ |
| 39 | Portal navigate | Double-click subfolder | Deeper level | ☐ |
| 40 | Portal close | Click X | Back to desktop | ☐ |
| 41 | Portal maximize | Click □ | Full-screen folder view | ☐ |
| 42 | Portal drag | Drag title bar | Window moves | ☐ |
| 43 | Desktop isolation | Mouse in portal | Desktop doesn't rotate | ☐ |
| 44 | Label clipping | Desktop label behind portal | Not visible in portal | ☐ |

### 2.7 Tray & Indicators
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 45 | Volume icon | Check taskbar | Correct level shown | ☐ |
| 46 | Network icon | Check taskbar | Bars reflect status | ☐ |
| 47 | Battery icon | Check taskbar | Percentage + charging | ☐ |
| 48 | IME toggle | Click 中/英 | IME toggles | ☐ |
| 49 | Tray popup | Click ^ | Explorer tray icons listed | ☐ |
| 50 | Clock | Check right side | Time + date correct | ☐ |

### 2.8 Persistence & Cleanup
| # | Test | Steps | Expected | Pass |
|---|---|---|---|---|
| 51 | Exit saves | Move cubes, exit | desktop.cddesk updated | ☐ |
| 52 | Log pruning | Run 12+ times | Only 10 logs kept | ☐ |
| 53 | Icon cache prune | Open/close apps | Memory stable | ☐ |
| 54 | Watchdog restore | Kill CrossDim process | Explorer restored in 5s | ☐ |
| 55 | Graceful exit | Close CrossDim | No Explorer relaunch | ☐ |

## 3. Crash Testing

| # | Scenario | Expected Recovery |
|---|---|---|
| 1 | Kill CrossDim.exe | Watchdog: kill + restore shell + launch Explorer (5s) |
| 2 | Delete desktop.cddesk mid-run | Cubes stay in memory; next save recreates file |
| 3 | Empty desktop folder | No cubes; no crash; taskbar works |
| 4 | Empty folder in portal | Portal shows gray background, no cubes, no crash |
| 5 | Rapid desktop switching | No crash; transition completes |
| 6 | Deep folder nesting | Stack grows; exit-to-root cleans all |

## 4. Performance Smoke Test

| # | Scenario | Target |
|---|---|---|
| 1 | 60+ cubes | Maintain 60 FPS |
| 2 | 10+ hijacked windows | Taskbar smooth (< 5ms per frame overhead) |
| 3 | Long session (1h) | Memory growth < 100MB |
| 4 | Portal open + desktop | Both render without frame drops |

## 5. Known-Broken Areas (deferred)

These are known bugs/incomplete features — **do NOT treat as test failures**:
- Portal cursor lock (reverted to free-cursor mode)
- Right-click context menu shows "black box" in some cases (incomplete IContextMenu)
- `g_taskbarIconCache` memory growth
- Folder state persistence (not yet implemented)
- DWM thumbnail capture (not implemented — switcher uses icons only)

## 6. Log Verification

After each test run, check:
1. `logs/crossdim_*.log` for errors (`[hijack]`, `[drop]`, `[desk]` tags)
2. No "No icon" spam for expected icons
3. No `[force]` messages unless intentionally testing force-clear
4. Watchdog logs (via Event Viewer if available)

## 7. Pre-Release Checklist

- [ ] All regression tests pass
- [ ] No new `[hijack]` timeouts
- [ ] Memory stable after 30min session
- [ ] Clean exit restores Explorer taskbar
- [ ] Watchdog recovery tested once
- [ ] Logs clean of spurious errors
