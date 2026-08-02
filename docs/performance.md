# CrossDim — Performance Analysis

## 1. Render Loop Budget

Target: **60 FPS** (16.6ms/frame budget)

| Stage | Est. Cost | Notes |
|---|---|---|
| PeekMessage + dispatch | ~0.2ms | Window messages |
| Icon cache prune (every 60f) | ~0.1ms | EnumWindow loop |
| Frame timing | ~0.01ms | GetTickCount64 |
| Model FOV logic | ~0.01ms | Float math |
| Camera rotation | ~0.02ms | Matrix ops |
| Hijack polling | ~0.1ms | FindWindowFromProcessId (EnumWindows) |
| ImGui NewFrame | ~0.3ms | Input + layout |
| Taskbar rendering | ~0.5ms | Draw list + text |
| Start menu / tray | ~0.3ms | Only when open |
| Window chrome | ~0.3ms | Per hijacked window |
| 3D scene (skybox+model+cubes) | ~2-5ms | GPU bound |
| Cube labels | ~0.2ms | Text projection |
| Marquee cage | ~0.1ms | Only when dragging |
| Portal rendering | ~1-2ms | Second viewport (when active) |
| ImGui Render | ~1ms | Vertex buffer flush |
| Present | ~0.5ms | VSync wait |

**Total: ~7-10ms** — headroom for ~2x more 3D content.

## 2. Hot Paths

### 2.1 Cube Raycasting (O(n) per frame)
```cpp
for (int i = 0; i < g_myApps.size(); i++) {
    BoundingBox box(g_myApps[i].Position, {0.6, 0.6, 0.3});
    if (box.Intersects(rayOrigin, rayDir, dist)) { ... }
}
```
- **Cost**: n × BoundingBox::Intersects (~50ns each)
- **At 100 cubes**: ~5µs — fine
- **At 1000 cubes**: ~50µs — starts to matter
- **Optimization**: spatial hash or octree by yaw/pitch sector

### 2.2 Taskbar Window Enumeration (per frame)
```cpp
std::vector<RunningWindow> runningWindows = EnumerateRunningWindows(g_mainHwnd);
```
- **Cost**: full EnumWindows + GetProcessPath (OpenProcess + QueryFullProcessImageName) per window
- **At 50 windows**: ~2-5ms — **significant!**
- **Optimization**: throttle to every 30-60 frames, not every frame

### 2.3 Icon Loading (lazy, once per app)
```cpp
TextureLoader::LoadIconFromExe(g_pd3dDevice, appPath)
```
- **Cost**: ~5-20ms per EXE (ExtractIcon + CreateTexture)
- **Optimization**: load in a background thread with a queue

### 2.4 Font Rendering (Chinese full range)
- `msyh.ttc` with `GetGlyphRangesChineseFull` = ~20k glyphs
- **Atlas memory**: ~4MB at 20pt
- **Cost per AddText**: atlas lookup — fast

## 3. GPU Resource Usage

| Resource | Count | Size |
|---|---|---|
| Back buffer (1920×1080) | 2 | ~16MB |
| Depth stencil | 1 | ~8MB |
| Icon textures (per app) | ~14-30 | ~64×64×4 = 16KB each |
| Taskbar icon cache | unbounded | **leak risk** |
| Model (bloom_high.obj) | 1 | varies |
| Skybox | 1 | small |

**Total GPU memory**: ~50MB typical — well within budget.

## 4. Memory (CPU) Analysis

| Section | Est. Size |
|---|---|
| g_myApps (100 cubes) | ~50KB |
| g_folderCubes | ~50KB per level |
| g_folderStack (deep nesting) | ~50KB per level |
| g_taskbarIconCache | **unbounded — leak** |
| ImGui context | ~10MB |
| Font atlas | ~4MB |
| Log files | auto-pruned to 10 |

## 5. Identified Performance Issues

### 5.1 HIGH: EnumerateRunningWindows every frame
The taskbar calls `EnumerateRunningWindows` every single frame. With 50+ windows, each requiring `OpenProcess`, this is the #1 CPU hotspot.

**Fix**: cache running windows, refresh every 30 frames (0.5s).

### 5.2 MEDIUM: g_taskbarIconCache unbounded
Every unique EXE path ever loaded stays cached. Long sessions with many apps → memory growth.

**Fix**: prune entries whose windows are dead (like g_taskbarWindowIconCache does).

### 5.3 MEDIUM: Icon loading blocks startup
`LoadIconsForApps` iterates all cubes synchronously at startup.

**Fix**: async queue + progressive display.

### 5.4 LOW: Full EnumWindows for FindRunningHwnd
Called per pinned app on every frame (in taskbar update). Same cost as #5.1.

### 5.5 LOW: BoundingBox allocation per cube per frame
`BoundingBox box(...)` constructs a new box each iteration. Could precompute.

## 6. Scaling Analysis

### 6.1 Cube Count
| Cubes | Raycast | Render | Labels | Verdict |
|---|---|---|---|---|
| 50 | 2.5µs | 1ms | 0.1ms | ✅ Smooth |
| 200 | 10µs | 4ms | 0.4ms | ⚠️ OK |
| 500 | 25µs | 10ms | 1ms | ❌ Needs partitioning |

### 6.2 Window Count
| Windows | Enum Cost | Taskbar Draw | Verdict |
|---|---|---|---|
| 10 | 0.5ms | 0.3ms | ✅ |
| 30 | 1.5ms | 0.5ms | ⚠️ |
| 60 | 3ms | 1ms | ❌ Throttle needed |

## 7. Optimization Roadmap

| Priority | Change | Est. Gain | Effort |
|---|---|---|---|
| P0 | Throttle EnumerateRunningWindows to 30f | 2-5ms saved | ~20 lines |
| P1 | Prune g_taskbarIconCache | Memory stable | ~15 lines |
| P1 | Async icon loading | Startup 300ms→50ms | ~60 lines |
| P2 | Cube spatial partitioning | 10x raycast | ~80 lines |
| P2 | Precompute BoundingBoxes | ~5µs/frame | ~10 lines |
| P3 | Shader bytecode cache | Load 50ms→5ms | ~20 lines |

## 8. Profiling Methodology

1. **Manual timing**: wrap sections with `QueryPerformanceCounter`
2. **DebugView**: OutputDebugString timings every N frames
3. **PIX / RenderDoc**: GPU frame capture (advanced)
4. **Task Manager**: check CPU/GPU% baseline
5. **Log-based**: add `[perf]` tags to Logger

### Suggested perf instrumentation
```cpp
// In render loop
LARGE_INTEGER qpc_start, qpc_end;
QueryPerformanceCounter(&qpc_start);
// ... section ...
QueryPerformanceCounter(&qpc_end);
double ms = (qpc_end.QuadPart - qpc_start.QuadPart) * 1000.0 / freq.QuadPart;
if (ms > 5.0) LOG("[perf] taskbar took %.2fms", ms);
```
