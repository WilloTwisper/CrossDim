# CrossDim — Roadmap & Status

## 1. Current Weighted Coverage

| Explorer Subsystem | Weight | Current | Cap | Gap Reason |
|---|---|---|---|---|
| Taskbar + Tray | 25% | ~55% | ~85% | Can't host third-party tray icons (ITrayNotify undocumented) |
| Desktop / Icons / DnD | 20% | ~30% | ~80% | No full file system integration yet |
| File Manager | 25% | ~10% | ~75% | Spatial file box framework only |
| Window Mgmt + Alt+Tab | 15% | ~55% | ~80% | Hijack+snap+switcher done; no Alt+Tab persistence |
| Start Menu | 5% | ~40% | ~70% | Search works, no dynamic app list |
| Notification Center | 7% | 0% | ~25% | WinRT protected |
| System Tools | 3% | ~60% | ~80% | Stable |

**Weighted: ~35% / API cap: ~76% / Shell-mode cap: ~95%**

## 2. Phase Status

### Phase 1 — Stability & Foundation ✅
| # | Feature | Status |
|---|---|---|
| 1.1 | Config persistence | Partial — cube positions saved; pinned apps/preferences not |
| 1.2 | Split main.cpp | ✅ DesktopManager/WindowManager/SystemInfo modules |
| 1.3 | Memory leak fixes | ✅ Icon cache pruning, thread detach |
| 1.4 | Input tracking | ✅ Message-driven input |

### Phase 2 — Desktop Completeness
| # | Feature | Status |
|---|---|---|
| 2.1 | Dynamic desktop icons | Partial — scan/remove/context menu done; file picker TODO |
| 2.2 | File system integration | ✅ Cubes for files/folders, drag-drop, SHGetFileInfo icons |
| 2.3 | Camera movement | Removed (excluded by design) |
| 2.4 | Search engine | ✅ Substring filter on cubes + start menu |

### Phase 3 — Window Management
| # | Feature | Status |
|---|---|---|
| 3.1 | Window snapping | ✅ Edge snap, keyboard shortcuts, blue preview |
| 3.2 | Alt+Tab replacement | ✅ 3D card switcher (Ctrl+Tab) |
| 3.3 | Virtual desktops | ✅ Multi-desktop + Task View overview |

### Phase 4 — Polish
| # | Feature | Status |
|---|---|---|
| 4.1 | BloomRenderer | Removed (abandoned) |
| 4.2 | MTL normal maps | ✅ |
| 4.3 | Dynamic system tray | ✅ Explorer toolbar proxy |
| 4.4 | Shader hot-reload | TODO |

### Phase 5 — Safety Nets & Shell Transition
| # | Feature | Status |
|---|---|---|
| 5.1 | Watchdog | ✅ |
| 5.2 | Task Manager hotkey | ✅ |
| 5.3 | Tray icon proxy | ✅ |
| 5.4 | Shell registration | TODO — registry swap + Explorer downgrade |
| 5.5 | State machine verification | TODO |

### Phase 6 — Long-term
| # | Feature | Status |
|---|---|---|
| 6.1 | Notification overlay | TODO |
| 6.2 | Keyboard navigation | TODO |
| 6.3 | Multi-monitor | TODO |
| 6.4 | Accessibility | TODO |
| 6.5 | File manager module | Partial — spatial file box framework |

## 3. Immediate Next Steps

### Priority 1: Fix Known Bugs (see analysis.md Bug Catalog)
1. `g_taskbarIconCache` unbounded growth
2. Texture leaks in `LoadIconFromExe`
3. Camera rotation order mismatch
4. Redundant icon double-load in EnterFolder
5. Portal interaction edge cases

### Priority 2: Complete Phase 5.4 — Shell Registration
- Registry `Shell` key swap (`explorer.exe` → `CrossDim.exe`)
- Explorer variant launch (as COM server, no desktop)
- Safety nets already in place (watchdog, emergency hotkey, tray proxy)

### Priority 3: Spatial File Box Polish
- Fix portal cursor locking (currently reverted to free-cursor mode)
- Add drag-and-drop between desktop and portal
- Right-click IContextMenu integration
- Folder state persistence

## 4. Bug Catalog Summary (from analysis.md)

| # | Severity | Bug | Location |
|---|---|---|---|
| 1 | High | g_taskbarIconCache never pruned | WindowManager.h |
| 2 | Medium | LoadIconFromExe icon handle leak | TextureLoader.h:133 |
| 3 | Medium | CULL_NONE state leaks into cube rendering | CubeRenderer |
| 4 | Medium | Camera rotation order mismatch | Camera.cpp |
| 5 | Medium | Redundant icon load in EnterFolder | main.cpp |
| 6-12 | Low-Med | Various edge cases | See analysis.md |

## 5. Optimization Opportunities

| # | Opportunity | Impact |
|---|---|---|
| 1 | Spatial partitioning for cube raycast | High (many cubes) |
| 2 | Async icon loading | Medium (startup time) |
| 3 | Shader bytecode caching | Low |
| 4 | Taskbar icon cache pruning | Medium (memory) |
| 5 | Portal render target reuse | Medium (perf) |

## 6. Architecture Evolution Plan

```
Phase 1-4 (done)     Phase 5 (next)      Phase 6 (long-term)
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ Overlay mode │────▶│ Shell reg    │────▶│ Full shell   │
│ + all features│     │ + Explorer   │     │ + file mgr   │
│               │     │ downgrade    │     │ + notifications│
└─────────────┘     └─────────────┘     └─────────────┘
```

The final architecture: CrossDim.exe registered as Shell; Explorer.exe runs silently as background COM service (tray hosting, shell extensions, DDE dispatch).
