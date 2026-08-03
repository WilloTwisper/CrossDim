# CrossDim — Data Format Specifications

## 1. desktop.cddesk (Desktop Cube Persistence)

Binary file storing cube positions for the desktop and each virtual desktop.

### File Naming
| Desktop | File |
|---|---|
| Desktop 0 | `desktop.cddesk` (legacy name, backward compatible) |
| Desktop N (N≥1) | `desktop_<N>.cddesk` |

### Header (12 bytes)
| Field | Type | Size | Value |
|---|---|---|---|
| magic | char[4] | 4 | `0x4B534544` = "DESK" (little-endian) |
| version | uint32 | 4 | 2 |
| count | uint32 | 4 | number of AppCube entries |

### Per-Entry Record
| Field | Type | Size | Notes |
|---|---|---|---|
| pathLen | uint16 | 2 | byte length of the wide string (wchar_t count) |
| appPath | wchar_t[pathLen] | pathLen × 2 | full filesystem path (UTF-16) |
| position | XMFLOAT3 | 12 | 3 × float (x, y, z) |
| nameLen | uint16 | 2 | byte length of the UTF-8 name |
| appName | char[nameLen] | nameLen | display name (UTF-8) |

### Layout Summary
```
+----------+----------+----------+
| magic(4) | ver(4)   | count(4)  |
+----------+----------+----------+
| entry 0 ...                     |
| entry 1 ...                     |
| ...                             |
```

### Version History
| Version | Behavior |
|---|---|
| 1 | Only matched existing desktop cubes by path, restored position |
| 2 | Merged external (non-desktop) cubes from saved state |

### Read/Write Semantics
- **Save** (`SaveDesktopState`): writes ALL current `AppCube` entries (path, position, name)
- **Load** (`LoadDesktopState`):
  - For each entry: if path matches a scanned cube → restore position
  - If path doesn't match and file still exists → add as new cube
  - Missing files are skipped

### Notes
- No endian marker — assumes little-endian (x86/x64)
- Uses Win32 `WriteFile`/`ReadFile` (blocking, but small files; called on exit/drag-end)
- Saved on: exit, drag-end, cube delete, virtual desktop switch
- Explicitly NOT saved while inside a folder (to avoid corruption)

## 2. .cdmesh (OBJ Mesh Binary Cache)

Cache for parsed OBJ meshes. Emitted next to the source `.obj` file (`<file>.obj.cdmesh`).

### Header (`MeshCacheHeader`, 40 bytes)
| Field | Type | Size | Value |
|---|---|---|---|
| magic | char[8] | 8 | `"CDMESH"` + padding |
| version | uint32 | 4 | 1 |
| vertexStride | uint32 | 4 | sizeof(ModelVertex) = 40 |
| objMtime | int64 | 8 | source OBJ modification time |
| objSize | uint64 | 8 | source OBJ file size |
| vertexCount | uint32 | 4 | number of vertices |
| indexCount | uint32 | 4 | number of indices |

### Vertex Record (`ModelVertex`, 40 bytes)
```cpp
struct ModelVertex {
    XMFLOAT3 Position;   // 12 bytes
    XMFLOAT2 UV;         // 8 bytes
    XMFLOAT3 Normal;     // 12 bytes
    XMFLOAT3 Tangent;    // 12 bytes
};
```

### Index Data
- `unsigned int` (4 bytes each) × indexCount

### Validation
Cache is valid only if ALL match:
- magic == "CDMESH"
- version == 1
- vertexStride == sizeof(ModelVertex) == 40
- objMtime == source mtime
- objSize == source size

If any mismatch → cache rejected, re-parsed from OBJ.

### Layout Summary
```
+-------------+-----------------+--------------------+
| header (40) | vertices (40N)  | indices (4M)       |
+-------------+-----------------+--------------------+
```

## 3. Model Vertex Stream Layout

Input layout matching `ModelVertex`:
```cpp
D3D11_INPUT_ELEMENT_DESC layout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0,  0, D3D11_INPUT_PER_VERTEX_DATA },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,     0, 12, D3D11_INPUT_PER_VERTEX_DATA },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 20, D3D11_INPUT_PER_VERTEX_DATA },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,  0, 32, D3D11_INPUT_PER_VERTEX_DATA },
};
```

## 4. imgui.ini (ImGui State)

Standard Dear ImGui ini file. Stores window positions/sizes/docks. Not application-specific.

## 5. Log Files

Format: `logs/crossdim_<YYYYMMDD_HHMMSS>.log` (exe directory + `logs/`).

Line format:
```
[%5u.%03u] message\n
```
- `%5u.%03u` = seconds.milliseconds since process start (GetTickCount64)
- Encoding: UTF-8 (LogW converts via WideCharToMultiByte)

Pruned to latest 10 files on Logger::Init.

## 6. Registry Usage (current + planned)

| Type | Key | Value | Status |
|---|---|---|---|
| Read | `HKCU\...\Console` | — | Not used (console apps only) |
| Read/Write | `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon` | `Shell` | Watchdog restores to explorer.exe (Phase 5.4: CrossDim registers here) |
| Read | `HKCR\lnkfile` | — | Not used (shortcut target via IShellLink, not registry) |

All other state is file-based (`.cddesk`, `.cdmesh`, `logs/`), no application registry keys yet.