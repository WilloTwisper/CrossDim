# CrossDim — Watchdog Protocol

## 1. Overview

The watchdog is a standalone process (`watchdog.exe`) that monitors CrossDim's health. If CrossDim crashes or hangs, the watchdog terminates it, restores the default Windows shell, and relaunches Explorer.

**Reason**: CrossDim hides the taskbar and (in future) replaces the shell. If it dies without cleanup, the user is left with no shell. The watchdog is the safety net.

## 2. Process Lifecycle

```
wWinMain (CrossDim)
  ├── CreateEvent("Global\CrossDim_Heartbeat")   // auto-reset event
  ├── CreateEvent("Global\CrossDim_Shutdown")    // manual-reset event
  └── LaunchProcess(watchdog.exe)                // detached
        ├── OpenEvent("Global\CrossDim_Heartbeat")
        ├── OpenEvent("Global\CrossDim_Shutdown")
        └── WaitForMultipleObjects(heartbeat, shutdown, 5000ms)
```

## 3. Protocol

### Named Events
| Event | Namespace | Type | Purpose |
|---|---|---|---|
| `Global\CrossDim_Heartbeat` | Global | Auto-reset | CrossDim sets it every frame; timeout = dead |
| `Global\CrossDim_Shutdown` | Global | Manual-reset | CrossDim sets it on graceful exit |

### Heartbeat
- CrossDim sets `CrossDim_Heartbeat` **every frame** in the render loop:
  ```cpp
  if (g_hHeartbeatEvent) SetEvent(g_hHeartbeatEvent);
  ```
- Watchdog waits on the event with a **5000ms timeout**.
- If the event is signaled within 5s → CrossDim is alive, watchdog loops.
- If timeout expires → CrossDim is dead/hung → recovery.

### Shutdown (graceful)
- On CrossDim exit, it calls:
  ```cpp
  SetEvent(g_hShutdownEvent);   // tell watchdog we're exiting gracefully
  CloseHandle(g_hShutdownEvent);
  CloseHandle(g_hHeartbeatEvent);
  ```
- Watchdog sees `WAIT_OBJECT_0 + 1` (shutdown signaled) → exits normally, **no recovery**.

## 4. Recovery Sequence (on heartbeat timeout)

```
1. KillAllCrossDim()
   - CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)
   - For every process named "CrossDim.exe" (except self):
     - OpenProcess(PROCESS_TERMINATE)
     - TerminateProcess(pid, 0)
2. RestoreDefaultShell()
   - Open HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon
   - If Shell value != "explorer.exe":
     - RegSetValueEx(Shell, "explorer.exe")
3. LaunchExplorer()
   - CreateProcessW("explorer.exe")
4. watchdog exits
```

## 5. Startup Retry Logic

Watchdog waits up to 10 × 500ms = **5s** for the heartbeat event to appear:
- CrossDim creates the event during init, then launches watchdog.
- Watchdog's `OpenEvent` may race with CrossDim's `CreateEvent`.
- If not found after 10 retries → watchdog exits (returns 1) — CrossDim likely failed to start.

## 6. Registry Key

| Path | Value |
|---|---|
| `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon` | `Shell` = `explorer.exe` |

The watchdog only writes `explorer.exe` back if it's NOT already `explorer.exe`. Used for the future Shell-swap mode (CrossDim would set `Shell = CrossDim.exe`, watchdog restores on crash).

## 7. Time Constants

| Constant | Value | Meaning |
|---|---|---|
| `kHeartbeatTimeoutMs` | 5000 | alive check timeout |
| Startup retries | 10 × 500ms | event wait |

## 8. Build

```
cl.exe /O2 /EHsc /nologo src\watchdog.cpp advapi32.lib /Fe:build\watchdog.exe
```

Requires `advapi32.lib` (registry access). Subsystem: windows (no console).

## 9. Failure Modes

| Scenario | Behavior |
|---|---|
| CrossDim running normally | Heartbeat set every frame, watchdog waits |
| CrossDim crashes (process dies) | Heartbeat stops → 5s timeout → kill + restore + relaunch |
| CrossDim hangs (infinite loop) | Heartbeat stops (no frame) → same recovery |
| CrossDim exits gracefully | Shutdown event set → watchdog exits quietly |
| CrossDim fails to start | Watchdog can't find event → exits after 5s |
| Watchdog killed | No recovery — user must manually restore shell |

## 10. Testing

1. **Graceful exit**: start CrossDim, close it → watchdog should exit, no Explorer relaunch.
2. **Crash recovery**: start CrossDim, kill the process → within 5s watchdog restores Explorer.
3. **Hang recovery**: break in debugger / freeze CrossDim → 5s timeout → recovery.
4. **Registry restore**: with Shell = CrossDim.exe, kill CrossDim → Shell restored to explorer.exe.