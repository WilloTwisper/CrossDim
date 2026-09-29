#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <d3d11.h>
#include "imgui.h"
#include "Engine/Logger.h"
#include "Engine/TrayProxy.h"
#include "Engine/TextureLoader.h"
#include "Shell/DesktopManager.h"
#include "Shell/WindowManager.h"
#include "Shell/SystemInfo.h"
#include "Shell/StartMenu.h"
#include "Shell/VirtualDesktop.h"
#include "Shell/FolderPortal.h"

extern RECT g_taskbarRect;
extern bool g_taskbarRectValid;
extern bool g_isDragging;
extern void LaunchAppByPath(LPCWSTR appPath);
extern char g_searchFilter[64];

enum TaskbarAction {
    TASKBAR_ACTION_START = 0,
    TASKBAR_ACTION_LAUNCH = 1,
    TASKBAR_ACTION_NONE = 2
};

enum TaskbarIconKind {
    TASKBAR_ICON_WINDOWS = 0,
    TASKBAR_ICON_SEARCH = 1,
    TASKBAR_ICON_APP = 2
};

struct TaskbarEntry {
    std::string Id;
    std::string Label;
    std::wstring AppPath;
    ImU32 Accent;
    TaskbarAction Action;
    TaskbarIconKind IconKind;
    ID3D11ShaderResourceView* Icon;
    bool Pinned;
    bool Running;
    HWND Hwnd;
    DWORD Pid;
};

inline void RenderTaskbar(ID3D11Device* device, HWND hwnd) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    static std::vector<TaskbarEntry> taskbarPinned;
    static bool taskbarStartOpen = false;
    static bool taskbarTrayOpen = false;
    static bool taskbarPinnedInit = false;
    if (!taskbarPinnedInit) {
        taskbarPinned = {
            { "Taskbar.Start",  "Start",  L"", IM_COL32(0, 140, 255, 255), TASKBAR_ACTION_START, TASKBAR_ICON_WINDOWS, nullptr, true, false, nullptr, 0 },
            { "Taskbar.Search", "Search", L"", IM_COL32(120, 170, 255, 255), TASKBAR_ACTION_NONE,  TASKBAR_ICON_SEARCH, nullptr, true, false, nullptr, 0 },
            { "Taskbar.Files",  "Files",  L"C:\\Windows\\explorer.exe", IM_COL32(255, 210, 120, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 },
            { "Taskbar.Edge",   "Edge",   L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", IM_COL32(120, 210, 255, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 },
            { "Taskbar.Terminal", "Terminal", L"C:\\Windows\\System32\\cmd.exe", IM_COL32(140, 255, 170, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 },
            { "Taskbar.Code",   "Code",   L"D:\\Apps\\Microsoft VS Code\\Code.exe", IM_COL32(120, 185, 255, 255), TASKBAR_ACTION_LAUNCH, TASKBAR_ICON_APP, nullptr, true, false, nullptr, 0 }
        };
        taskbarPinnedInit = true;
    }

    for (auto& entry : taskbarPinned) {
        if (entry.IconKind == TASKBAR_ICON_APP && entry.Icon == nullptr && !entry.AppPath.empty()) {
            entry.Icon = GetTaskbarIcon(device, entry.AppPath);
        }
        entry.Running = false;
        entry.Hwnd = nullptr;
        entry.Pid = 0;
    }

    // Expensive Win32/COM queries are throttled to ~2 Hz. Per-frame full
    // window enumeration (with OpenProcess per window) used to cost
    // several ms of frame time and was the main FPS tax.
    static std::vector<RunningWindow> s_runningWindowsCache;
    static std::unordered_map<std::wstring, HWND> s_pinnedFallbackHwnd;
    static NetworkStatus s_netStatus = {};
    static float s_volumeLevel = 0.5f;
    static bool s_imeOpen = false, s_imeChinese = false;
    static SYSTEM_POWER_STATUS s_power = {};
    static DWORD s_lastSysTick = 0;
    DWORD nowTick = GetTickCount();
    bool slowTick = (nowTick - s_lastSysTick > 500);
    if (slowTick) {
        s_lastSysTick = nowTick;

        // Live filesystem desktop change detection
        if (CheckDesktopChanges() && !g_isDragging && !g_isInFolder) {
            LOG("[desktop] filesystem change detected, reloading apps");
            std::vector<AppCube> freshApps;
            ScanDesktopForApps(freshApps);
            LoadDesktopState(freshApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
            MergeCubeVisuals(g_myApps, freshApps);
            SeparateOverlappingCubes(freshApps);
            LoadIconsForApps(freshApps);
            g_myApps.swap(freshApps);
            PruneUnusedIconCache();
        }

        s_runningWindowsCache = EnumerateRunningWindows(hwnd);
        for (const auto& win : s_runningWindowsCache) {
            if (g_taskbarDynamicOrder.find(win.hwnd) == g_taskbarDynamicOrder.end()) {
                g_taskbarDynamicOrder[win.hwnd] = g_taskbarDynamicOrderCounter++;
            }
        }
        for (auto it = g_taskbarDynamicOrder.begin(); it != g_taskbarDynamicOrder.end(); ) {
            bool alive = false;
            for (const auto& win : s_runningWindowsCache) {
                if (win.hwnd == it->first) { alive = true; break; }
            }
            if (!alive) it = g_taskbarDynamicOrder.erase(it);
            else ++it;
        }
        std::sort(s_runningWindowsCache.begin(), s_runningWindowsCache.end(), [&](const RunningWindow& a, const RunningWindow& b) {
            int orderA = 0;
            int orderB = 0;
            auto itA = g_taskbarDynamicOrder.find(a.hwnd);
            auto itB = g_taskbarDynamicOrder.find(b.hwnd);
            if (itA != g_taskbarDynamicOrder.end()) orderA = itA->second;
            if (itB != g_taskbarDynamicOrder.end()) orderB = itB->second;
            return orderA < orderB;
        });
        // Fallback only for pinned apps the main enum missed.
        for (auto& pinned : taskbarPinned) {
            if (pinned.IconKind != TASKBAR_ICON_APP || pinned.AppPath.empty()) continue;
            HWND found = FindRunningHwndInList(pinned.AppPath, s_runningWindowsCache);
            if (!found) found = FindRunningHwnd(pinned.AppPath);
            if (found) s_pinnedFallbackHwnd[pinned.AppPath] = found;
            else s_pinnedFallbackHwnd.erase(pinned.AppPath);
        }
        s_netStatus = GetNetworkStatus();
        s_volumeLevel = GetMasterVolumeLevelScalar();
        s_imeOpen = GetImeOpenStatus(hwnd);
        s_imeChinese = IsChineseImeLayout();
        GetSystemPowerStatus(&s_power);
    }
    const std::vector<RunningWindow>& runningWindows = s_runningWindowsCache;
    HWND fgWindow = GetForegroundWindow();
    for (auto& pinned : taskbarPinned) {
        if (pinned.IconKind == TASKBAR_ICON_APP) {
            pinned.Running = false;
            pinned.Hwnd = nullptr;
            pinned.Pid = 0;
        }
    }
    std::vector<TaskbarEntry> taskbarDynamic;
    taskbarDynamic.reserve(runningWindows.size());
    for (const auto& win : runningWindows) {
        bool matched = false;
        for (auto& pinned : taskbarPinned) {
            if (pinned.IconKind == TASKBAR_ICON_APP && IsSamePath(win.path, pinned.AppPath)) {
                pinned.Running = true;
                if (pinned.Hwnd == nullptr || win.hwnd == fgWindow) {
                    pinned.Hwnd = win.hwnd;
                    pinned.Pid = win.pid;
                }
                matched = true;
                break;
            }
        }
        if (!matched) {
            TaskbarEntry dyn = {};
            char idBuffer[64];
            sprintf_s(idBuffer, "Taskbar.Run.%p", win.hwnd);
            dyn.Id = idBuffer;
            dyn.Label = "Running";
            dyn.AppPath = win.path;
            dyn.Accent = IM_COL32(120, 185, 255, 255);
            dyn.Action = TASKBAR_ACTION_NONE;
            dyn.IconKind = TASKBAR_ICON_APP;
            dyn.Icon = GetWindowIconTexture(device, win.hwnd, win.path);
            dyn.Pinned = false;
            dyn.Running = true;
            dyn.Hwnd = win.hwnd;
            dyn.Pid = win.pid;
            taskbarDynamic.push_back(dyn);
        }
    }
    for (auto& pinned : taskbarPinned) {
        if (pinned.IconKind == TASKBAR_ICON_APP && !pinned.Running) {
            auto fb = s_pinnedFallbackHwnd.find(pinned.AppPath);
            if (fb != s_pinnedFallbackHwnd.end() && IsWindow(fb->second)) {
                pinned.Running = true;
                pinned.Hwnd = fb->second;
            }
        }
    }

    std::vector<TaskbarEntry*> taskbarEntries;
    taskbarEntries.reserve(taskbarPinned.size() + taskbarDynamic.size());
    for (auto& pinned : taskbarPinned) taskbarEntries.push_back(&pinned);
    for (auto& dyn : taskbarDynamic) taskbarEntries.push_back(&dyn);

    const int taskbarAppCount = (int)taskbarEntries.size();
    float iconSize = 32.0f;
    float iconGap = 12.0f;
    float paddingX = 18.0f;
    float paddingY = 8.0f;
    float barHeight = iconSize + paddingY * 2.0f;
    float barWidth = viewport->WorkSize.x;
    float scale = 1.0f;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeText[16];
    char dateText[16];
    sprintf_s(timeText, "%02u:%02u", st.wHour, st.wMinute);
    sprintf_s(dateText, "%04u/%02u/%02u", st.wYear, st.wMonth, st.wDay);

    bool imeOpen = s_imeOpen;
    bool imeChinese = s_imeChinese;
    const char* imeLeft = imeChinese ? (imeOpen ? "中" : "英") : "";
    const char* imeRight = imeChinese ? "拼" : "ENG";
    bool netConnected = s_netStatus.connected;
    bool netWifi = s_netStatus.wifi || !s_netStatus.ethernet;
    float volumeLevel = s_volumeLevel;
    int batteryPercent = 100;
    bool batteryCharging = false;
    if (s_power.BatteryLifePercent != 255) {
        batteryPercent = (int)s_power.BatteryLifePercent;
    }
    batteryCharging = (s_power.ACLineStatus == 1);

    ImVec2 timeSize = ImGui::CalcTextSize(timeText);
    ImVec2 dateSize = ImGui::CalcTextSize(dateText);
    float timeBlockWidth = (timeSize.x > dateSize.x ? timeSize.x : dateSize.x);
    float timeBlockHeight = timeSize.y + dateSize.y;

    float trayIconBox = 16.0f;
    float trayGap = 10.0f;
    ImVec2 imeLeftSize = ImGui::CalcTextSize(imeLeft);
    ImVec2 imeRightSize = ImGui::CalcTextSize(imeRight);
    float imeGap = 10.0f;
    float imeBlockWidth = (imeLeftSize.x > 0.0f ? imeLeftSize.x + imeGap : 0.0f) + imeRightSize.x;
    ImVec2 chevronSize = ImGui::CalcTextSize("^");
    float rightAreaWidth = chevronSize.x + imeBlockWidth + timeBlockWidth + trayIconBox * 3.0f + trayGap * 5.0f;

    ImVec2 barPos = ImVec2(
        viewport->WorkPos.x,
        viewport->WorkPos.y + viewport->WorkSize.y - barHeight
    );
    g_taskbarRect.left = (LONG)barPos.x;
    g_taskbarRect.top = (LONG)barPos.y;
    g_taskbarRect.right = (LONG)(barPos.x + barWidth);
    g_taskbarRect.bottom = (LONG)(barPos.y + barHeight);
    g_taskbarRectValid = true;

    ImGui::SetNextWindowPos(barPos);
    ImGui::SetNextWindowSize(ImVec2(barWidth, barHeight));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(paddingX, paddingY));
    ImGui::Begin("CrossDim Taskbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetWindowPos();
    ImVec2 p1 = ImVec2(p0.x + barWidth, p0.y + barHeight);
    draw->AddRectFilled(p0, p1, IM_COL32(244, 246, 250, 235), 0.0f);
    draw->AddRectFilledMultiColor(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y + barHeight * 0.5f),
                                  IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 120),
                                  IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    draw->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y), IM_COL32(255, 255, 255, 120), 1.0f);
    draw->AddLine(ImVec2(p0.x, p1.y - 1.0f), ImVec2(p1.x, p1.y - 1.0f), IM_COL32(0, 0, 0, 18), 1.0f);

    auto launchTaskbarApp = [&](LPCWSTR appPath) {
        if (!appPath || appPath[0] == L'\0') return;
        WCHAR cmdBuffer[MAX_PATH];
        wcscpy_s(cmdBuffer, appPath);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            HWND currentFocus = GetForegroundWindow();
            g_pendingHijacks.push_back({ currentFocus, 0, pi.dwProcessId });
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            SetSystemTaskbarVisible(false);
        }
    };

    auto drawWindowsGlyph = [&](ImVec2 a, ImVec2 b, ImU32 color) {
        float w = b.x - a.x;
        float r = w * 0.08f;
        draw->AddRectFilled(a, b, color, r);
        float cx = (a.x + b.x) * 0.5f;
        float cy = (a.y + b.y) * 0.5f;
        ImU32 lineColor = IM_COL32(255, 255, 255, 230);
        draw->AddLine(ImVec2(cx, a.y), ImVec2(cx, b.y), lineColor, 1.6f);
        draw->AddLine(ImVec2(a.x, cy), ImVec2(b.x, cy), lineColor, 1.6f);
    };

    auto drawSearchGlyph = [&](ImVec2 center, float size, ImU32 color) {
        float r = size * 0.28f;
        ImVec2 c = ImVec2(center.x - size * 0.06f, center.y - size * 0.06f);
        draw->AddCircle(c, r, color, 16, 1.6f);
        ImVec2 handleStart = ImVec2(c.x + r * 0.7f, c.y + r * 0.7f);
        ImVec2 handleEnd = ImVec2(handleStart.x + r * 0.9f, handleStart.y + r * 0.9f);
        draw->AddLine(handleStart, handleEnd, color, 1.6f);
    };

    auto drawAppGlyph = [&](ImVec2 center, float size, ImU32 color) {
        float s = size * 0.34f;
        ImVec2 a = ImVec2(center.x - s, center.y - s);
        ImVec2 b = ImVec2(center.x + s, center.y + s);
        draw->AddRectFilled(a, b, color, 4.0f);
    };

    auto drawBatteryIcon = [&](ImVec2 pos, float w, float h, int percent, bool charging, ImU32 color) {
        float capW = w * 0.12f;
        ImVec2 bodyMin = pos;
        ImVec2 bodyMax = ImVec2(pos.x + w - capW - 1.0f, pos.y + h);
        draw->AddRect(bodyMin, bodyMax, color, 2.0f, 0, 1.2f);
        float fillW = (bodyMax.x - bodyMin.x - 4.0f) * (percent < 0 ? 0.0f : (percent > 100 ? 1.0f : (percent / 100.0f)));
        ImVec2 fillMin = ImVec2(bodyMin.x + 2.0f, bodyMin.y + 2.0f);
        ImVec2 fillMax = ImVec2(fillMin.x + fillW, bodyMax.y - 2.0f);
        draw->AddRectFilled(fillMin, fillMax, color, 1.0f);
        ImVec2 capMin = ImVec2(bodyMax.x + 1.0f, pos.y + h * 0.32f);
        ImVec2 capMax = ImVec2(bodyMax.x + 1.0f + capW, pos.y + h * 0.68f);
        draw->AddRectFilled(capMin, capMax, color, 1.0f);
        if (charging) {
            ImVec2 boltA = ImVec2(bodyMin.x + w * 0.45f, pos.y + h * 0.2f);
            ImVec2 boltB = ImVec2(bodyMin.x + w * 0.6f, pos.y + h * 0.45f);
            ImVec2 boltC = ImVec2(bodyMin.x + w * 0.5f, pos.y + h * 0.45f);
            ImVec2 boltD = ImVec2(bodyMin.x + w * 0.63f, pos.y + h * 0.8f);
            draw->AddTriangleFilled(boltA, boltB, boltC, IM_COL32(180, 255, 180, 230));
            draw->AddTriangleFilled(boltB, boltC, boltD, IM_COL32(180, 255, 180, 230));
        }
    };

    auto drawVolumeIcon = [&](ImVec2 center, float size, float level, ImU32 color) {
        float bodyW = size * 0.22f;
        float bodyH = size * 0.32f;
        ImVec2 bodyMin = ImVec2(center.x - size * 0.42f, center.y - bodyH * 0.5f);
        ImVec2 bodyMax = ImVec2(bodyMin.x + bodyW, bodyMin.y + bodyH);
        draw->AddRectFilled(bodyMin, bodyMax, color, 1.0f);
        ImVec2 triA = ImVec2(bodyMax.x, bodyMin.y - bodyH * 0.25f);
        ImVec2 triB = ImVec2(bodyMax.x + size * 0.32f, center.y);
        ImVec2 triC = ImVec2(bodyMax.x, bodyMax.y + bodyH * 0.25f);
        draw->AddTriangleFilled(triA, triB, triC, color);
        if (level > 0.2f) draw->AddCircle(center, size * 0.32f, color, 12, 1.2f);
        if (level > 0.6f) draw->AddCircle(center, size * 0.42f, color, 12, 1.2f);
    };

    auto drawNetBarsIcon = [&](ImVec2 center, float size, bool wifi, bool connected, ImU32 color) {
        float barW = size * 0.12f;
        float gap = size * 0.12f;
        float baseY = center.y + size * 0.22f;
        for (int i = 0; i < 3; ++i) {
            float h = size * (0.2f + 0.15f * i);
            float x = center.x - (barW * 1.5f + gap) + i * (barW + gap);
            draw->AddRectFilled(ImVec2(x, baseY - h), ImVec2(x + barW, baseY), color, 1.0f);
        }
        if (wifi) {
            draw->AddCircleFilled(ImVec2(center.x, baseY + barW * 0.5f), barW * 0.45f, color);
        }
        if (!connected) {
            draw->AddLine(ImVec2(center.x - size * 0.4f, center.y - size * 0.35f),
                          ImVec2(center.x + size * 0.4f, center.y + size * 0.35f),
                          color, 1.4f);
        }
    };

    float centerAreaLeft = p0.x + paddingX;
    float centerAreaRight = p1.x - paddingX - rightAreaWidth;
    float centerAreaWidth = centerAreaRight - centerAreaLeft;
    float iconRowWidth = taskbarAppCount * iconSize +
                         (taskbarAppCount > 1 ? (taskbarAppCount - 1) * iconGap : 0.0f);
    float iconScale = 1.0f;
    if (centerAreaWidth > 0.0f && iconRowWidth > centerAreaWidth) {
        iconScale = centerAreaWidth / iconRowWidth;
    }
    float drawIconSize = iconSize * iconScale;
    float drawIconGap = iconGap * iconScale;
    float drawRowWidth = taskbarAppCount * drawIconSize + (taskbarAppCount > 1 ? (taskbarAppCount - 1) * drawIconGap : 0.0f);

    for (auto& pinned : taskbarPinned) {
        if (pinned.Running && pinned.Hwnd) {
            pinned.Icon = GetWindowIconTexture(device, pinned.Hwnd, pinned.AppPath);
        }
    }
    if (centerAreaWidth < 0.0f) centerAreaWidth = 0.0f;

    ImVec2 cursor = ImVec2(
        centerAreaLeft + (centerAreaWidth - drawRowWidth) * 0.5f,
        p0.y + (barHeight - drawIconSize) * 0.5f
    );
    float rowStartX = cursor.x;
    static int dragPinnedIndex = -1;
    static int dragDynamicIndex = -1;
    static ImGuiID dragItemId = 0;
    static bool dragItemMoved = false;
    static ImVec2 dragMouseStart = ImVec2(0, 0);
    const int pinnedCount = (int)taskbarPinned.size();
    const int dynamicCount = (int)taskbarDynamic.size();
    for (int i = 0; i < taskbarAppCount; ++i) {
        ImVec2 iconPos = ImVec2(cursor.x + i * (drawIconSize + drawIconGap), cursor.y);
        ImGui::SetCursorScreenPos(iconPos);
        TaskbarEntry& entry = *taskbarEntries[i];
        ImGui::InvisibleButton(entry.Id.c_str(), ImVec2(drawIconSize, drawIconSize));

        ImGuiID itemId = ImGui::GetItemID();
        if (ImGui::IsItemActivated()) {
            dragItemId = itemId;
            dragMouseStart = ImGui::GetIO().MousePos;
            dragItemMoved = false;
        }
        if (dragItemId == itemId && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !dragItemMoved) {
            float dx = ImGui::GetIO().MousePos.x - dragMouseStart.x;
            float dy = ImGui::GetIO().MousePos.y - dragMouseStart.y;
            if (dx * dx + dy * dy > 6.0f) {
                dragItemMoved = true;
            }
        }

        bool hovered = ImGui::IsItemHovered();
        bool isStart = (entry.Action == TASKBAR_ACTION_START);
        bool isRunning = entry.Running;
        bool isForeground = (entry.Hwnd != nullptr && entry.Hwnd == fgWindow);
        bool isActive = isStart ? taskbarStartOpen : isForeground;
        bool wasDragged = (dragItemMoved && dragItemId == itemId);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !wasDragged) {
            if (isStart) {
                taskbarStartOpen = !taskbarStartOpen;
            } else {
                taskbarStartOpen = false;
                HWND clickHwnd = (entry.Running && entry.Hwnd) ? entry.Hwnd : FindRunningHwnd(entry.AppPath);
                if (clickHwnd) {
                    entry.Running = true;
                    entry.Hwnd = clickHwnd;
                    if (clickHwnd == fgWindow) {
                        ShowWindow(clickHwnd, SW_MINIMIZE);
                    } else {
                        ShowWindow(clickHwnd, SW_RESTORE);
                        DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
                        DWORD targetThread = GetWindowThreadProcessId(clickHwnd, nullptr);
                        AttachThreadInput(fgThread, targetThread, TRUE);
                        SetForegroundWindow(clickHwnd);
                        AttachThreadInput(fgThread, targetThread, FALSE);
                    }
                } else if (entry.Action == TASKBAR_ACTION_LAUNCH && !entry.AppPath.empty()) {
                    launchTaskbarApp(entry.AppPath.c_str());
                }
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Middle) && isRunning && entry.Hwnd) {
            PostMessage(entry.Hwnd, WM_CLOSE, 0, 0);
        }

        if (dragItemMoved && dragItemId == itemId && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float slot = (ImGui::GetIO().MousePos.x - rowStartX + (drawIconSize * 0.5f)) / (drawIconSize + drawIconGap);
            int target = (int)slot;
            if (entry.Pinned) {
                if (dragPinnedIndex == -1) dragPinnedIndex = i;
                if (target < 0) target = 0;
                if (target >= pinnedCount) target = pinnedCount - 1;
                if (target != dragPinnedIndex && target >= 0 && dragPinnedIndex >= 0 && dragPinnedIndex < pinnedCount) {
                    std::swap(taskbarPinned[dragPinnedIndex], taskbarPinned[target]);
                    dragPinnedIndex = target;
                }
            } else if (dynamicCount > 1) {
                int dynIndex = i - pinnedCount;
                if (dynIndex >= 0 && dynIndex < dynamicCount) {
                    if (dragDynamicIndex == -1) dragDynamicIndex = dynIndex;
                    int targetDyn = target - pinnedCount;
                    if (targetDyn < 0) targetDyn = 0;
                    if (targetDyn >= dynamicCount) targetDyn = dynamicCount - 1;
                    if (targetDyn != dragDynamicIndex && dragDynamicIndex >= 0 && dragDynamicIndex < dynamicCount) {
                        HWND a = taskbarDynamic[dragDynamicIndex].Hwnd;
                        HWND b = taskbarDynamic[targetDyn].Hwnd;
                        auto itA = g_taskbarDynamicOrder.find(a);
                        auto itB = g_taskbarDynamicOrder.find(b);
                        if (itA != g_taskbarDynamicOrder.end() && itB != g_taskbarDynamicOrder.end()) {
                            std::swap(itA->second, itB->second);
                        }
                        std::swap(taskbarDynamic[dragDynamicIndex], taskbarDynamic[targetDyn]);
                        dragDynamicIndex = targetDyn;
                    }
                }
            }
        }
        ImVec2 b0 = ImGui::GetItemRectMin();
        ImVec2 b1 = ImGui::GetItemRectMax();
        float iconRound = 6.0f * (drawIconSize / 28.0f);
        ImU32 base = (hovered || isActive) ? IM_COL32(255, 255, 255, 180) : IM_COL32(255, 255, 255, 0);
        if ((base >> 24) > 0) draw->AddRectFilled(b0, b1, base, iconRound);

        bool isAppIcon = (entry.IconKind == TASKBAR_ICON_APP);
        if (isAppIcon) {
            ImVec4 accent = ImGui::ColorConvertU32ToFloat4(entry.Accent);
            accent.w = hovered ? 0.08f : 0.04f;
            if (accent.w > 0.001f) {
                draw->AddRectFilled(b0, b1, ImGui::ColorConvertFloat4ToU32(accent), iconRound);
            }
        }

        ImVec2 center = ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
        ImVec2 glyphMin = ImVec2(b0.x + drawIconSize * 0.22f, b0.y + drawIconSize * 0.22f);
        ImVec2 glyphMax = ImVec2(b1.x - drawIconSize * 0.22f, b1.y - drawIconSize * 0.22f);
        if (entry.IconKind == TASKBAR_ICON_WINDOWS) {
            drawWindowsGlyph(glyphMin, glyphMax, entry.Accent);
        } else if (entry.IconKind == TASKBAR_ICON_SEARCH) {
            drawSearchGlyph(center, drawIconSize, IM_COL32(30, 40, 60, 210));
        } else {
            ID3D11ShaderResourceView* iconSrv = entry.Icon;
            if (iconSrv) {
                float imgSize = drawIconSize * 0.72f;
                ImVec2 imgPos = ImVec2(center.x - imgSize * 0.5f, center.y - imgSize * 0.5f);
                draw->AddImage((ImTextureID)iconSrv, imgPos, ImVec2(imgPos.x + imgSize, imgPos.y + imgSize));
            } else {
                ImVec4 glyphAccent = ImGui::ColorConvertU32ToFloat4(entry.Accent);
                glyphAccent.w = 0.85f;
                drawAppGlyph(center, drawIconSize, ImGui::ColorConvertFloat4ToU32(glyphAccent));
            }
        }

        if (isRunning) {
            ImVec2 dot = ImVec2((b0.x + b1.x) * 0.5f, b1.y + 3.0f);
            float dotRadius = isForeground ? 2.6f : 2.0f;
            ImU32 dotColor = isForeground ? entry.Accent : IM_COL32(120, 140, 190, 220);
            draw->AddCircleFilled(dot, dotRadius, dotColor);
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragPinnedIndex = -1;
        dragDynamicIndex = -1;
        dragItemId = 0;
        dragItemMoved = false;
        dragMouseStart = ImVec2(0, 0);
    }

    // Task View toggle button
    {
        float tvSize = 22.0f;
        ImGui::SetCursorScreenPos(ImVec2(p1.x - paddingX - rightAreaWidth - tvSize - trayGap, p0.y + (barHeight - tvSize) * 0.5f));
        ImGui::InvisibleButton("TaskView", ImVec2(tvSize, tvSize));
        ImU32 tvColor = ImGui::IsItemHovered() ? IM_COL32(0, 140, 255, 180) : g_showDesktopOverview ? IM_COL32(0, 140, 255, 120) : IM_COL32(40, 50, 70, 50);
        ImVec2 tvCenter = ImVec2(ImGui::GetItemRectMin().x + tvSize * 0.5f, ImGui::GetItemRectMin().y + tvSize * 0.5f);
        draw->AddRectFilled(ImVec2(tvCenter.x - tvSize * 0.5f, tvCenter.y - tvSize * 0.5f), ImVec2(tvCenter.x + tvSize * 0.5f, tvCenter.y + tvSize * 0.5f), tvColor, 4.0f);
        // Draw two overlapping rectangles to suggest "multi desktop"
        draw->AddRect(ImVec2(tvCenter.x - 6.0f, tvCenter.y - 5.0f), ImVec2(tvCenter.x + 2.0f, tvCenter.y + 5.0f), IM_COL32(200, 210, 220, 220), 3.0f, 0, 1.2f);
        draw->AddRect(ImVec2(tvCenter.x - 2.0f, tvCenter.y - 3.0f), ImVec2(tvCenter.x + 6.0f, tvCenter.y + 7.0f), IM_COL32(160, 180, 200, 200), 3.0f, 0, 1.2f);
        if (ImGui::IsItemClicked()) {
            g_showDesktopOverview = !g_showDesktopOverview;
        }
    }

    ImU32 trayColor = IM_COL32(20, 30, 50, 220);
    ImU32 trayMuted = IM_COL32(20, 30, 50, 150);
    ImU32 netColor = netConnected ? trayColor : IM_COL32(20, 30, 50, 110);
    float centerY = p0.y + barHeight * 0.5f;
    float timeX = p1.x - paddingX - timeSize.x;
    float dateX = p1.x - paddingX - dateSize.x;
    float timeY = centerY - timeBlockHeight * 0.5f;
    draw->AddText(ImVec2(timeX, timeY), trayColor, timeText);
    draw->AddText(ImVec2(dateX, timeY + timeSize.y), trayMuted, dateText);

    float cursorX = p1.x - paddingX - timeBlockWidth - trayGap;
    float batteryH = trayIconBox * 0.55f;
    cursorX -= trayIconBox;
    ImU32 batteryColor = batteryCharging ? IM_COL32(80, 200, 80, 240) : trayColor;
    drawBatteryIcon(ImVec2(cursorX, centerY - batteryH * 0.5f), trayIconBox, batteryH, batteryPercent, batteryCharging, batteryColor);

    cursorX -= trayGap + trayIconBox;
    drawVolumeIcon(ImVec2(cursorX + trayIconBox * 0.5f, centerY), trayIconBox, volumeLevel, trayColor);

    cursorX -= trayGap + trayIconBox;
    drawNetBarsIcon(ImVec2(cursorX + trayIconBox * 0.5f, centerY), trayIconBox, netWifi, netConnected, netColor);

    cursorX -= trayGap + imeBlockWidth;
    ImVec2 imeLeftPos = ImVec2(cursorX, centerY - imeLeftSize.y * 0.5f);
    ImVec2 imeRightPos = ImVec2(cursorX + imeLeftSize.x + imeGap, centerY - imeRightSize.y * 0.5f);

    bool imeLeftHover = false;
    if (imeChinese) {
        ImGui::SetCursorScreenPos(ImVec2(imeLeftPos.x - 3.0f, imeLeftPos.y - 2.0f));
        ImGui::InvisibleButton("Ime.Left", ImVec2(imeLeftSize.x + 6.0f, imeLeftSize.y + 4.0f));
        imeLeftHover = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_SHIFT;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_SHIFT;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(INPUT));
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(imeRightPos.x - 3.0f, imeRightPos.y - 2.0f));
    ImGui::InvisibleButton("Ime.Right", ImVec2(imeRightSize.x + 6.0f, imeRightSize.y + 4.0f));
    bool imeRightHover = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::OpenPopup("Ime.Menu");
    }

    ImU32 imeLeftColor = imeLeftHover ? IM_COL32(20, 30, 50, 240) : trayColor;
    ImU32 imeRightColor = imeRightHover ? IM_COL32(20, 30, 50, 240) : trayColor;
    draw->AddText(imeLeftPos, imeLeftColor, imeLeft);
    draw->AddText(imeRightPos, imeRightColor, imeRight);

    if (ImGui::BeginPopup("Ime.Menu")) {
        if (ImGui::MenuItem("Microsoft Pinyin")) {
            ActivateImeLayout(hwnd, L"00000804", true);
        }
        if (ImGui::MenuItem("ENG")) {
            ActivateImeLayout(hwnd, L"00000409", false);
        }
        ImGui::EndPopup();
    }

    cursorX -= trayGap + chevronSize.x;
    ImGui::SetCursorScreenPos(ImVec2(cursorX - 2.0f, centerY - trayIconBox * 0.5f));
    ImGui::InvisibleButton("Taskbar.TrayToggle", ImVec2(chevronSize.x + 4.0f, trayIconBox));
    bool chevronHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        taskbarTrayOpen = !taskbarTrayOpen;
    }
    ImU32 chevronColor = chevronHovered ? IM_COL32(20, 30, 50, 240) : IM_COL32(20, 30, 50, 200);
    draw->AddText(ImVec2(cursorX, centerY - chevronSize.y * 0.5f), chevronColor, taskbarTrayOpen ? "v" : "^");

    ImGui::End();
    ImGui::PopStyleVar(3);

    if (taskbarStartOpen) {
        EnsureStartMenuScanned();
        float panelWidth = 480.0f * scale;
        float panelHeight = 560.0f * scale;
        if (panelWidth > viewport->WorkSize.x - 32.0f) panelWidth = viewport->WorkSize.x - 32.0f;
        if (panelHeight > viewport->WorkSize.y - 120.0f) panelHeight = viewport->WorkSize.y - 120.0f;
        ImVec2 panelPos = ImVec2(
            viewport->WorkPos.x + (viewport->WorkSize.x - panelWidth) * 0.5f,
            barPos.y - panelHeight - 12.0f * scale
        );
        ImGui::SetNextWindowPos(panelPos);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * scale, 16.0f * scale));
        ImGui::Begin("CrossDim Start", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

        ImDrawList* panelDraw = ImGui::GetWindowDrawList();
        ImVec2 q0 = ImGui::GetWindowPos();
        ImVec2 q1 = ImVec2(q0.x + panelWidth, q0.y + panelHeight);
        float panelRound = 20.0f * scale;
        panelDraw->AddRectFilled(ImVec2(q0.x, q0.y + 2.0f * scale), ImVec2(q1.x, q1.y + 8.0f * scale), IM_COL32(0, 0, 0, 50), panelRound);
        panelDraw->AddRectFilled(q0, q1, IM_COL32(235, 238, 246, 200), panelRound);
        panelDraw->AddRectFilledMultiColor(ImVec2(q0.x, q0.y), ImVec2(q1.x, q0.y + panelHeight * 0.55f),
                                           IM_COL32(255, 255, 255, 100), IM_COL32(255, 255, 255, 100),
                                           IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        panelDraw->AddRect(q0, q1, IM_COL32(255, 255, 255, 160), panelRound, 0, 1.2f);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.18f, 0.24f, 1.0f));

        float searchHeight = 36.0f * scale;
        ImVec2 s0 = ImVec2(q0.x + 16.0f * scale, q0.y + 16.0f * scale);
        ImVec2 s1 = ImVec2(q1.x - 16.0f * scale, s0.y + searchHeight);
        panelDraw->AddRectFilled(s0, s1, IM_COL32(255, 255, 255, 220), searchHeight * 0.5f);
        panelDraw->AddRect(s0, s1, IM_COL32(0, 0, 0, 25), searchHeight * 0.5f, 0, 1.0f);

        float sCenterY = (s0.y + s1.y) * 0.5f;
        drawSearchGlyph(ImVec2(s0.x + searchHeight * 0.5f, sCenterY), searchHeight * 0.7f, IM_COL32(100, 110, 130, 200));

        ImGui::SetCursorScreenPos(ImVec2(s0.x + searchHeight + 2.0f, s0.y + 6.0f * scale));
        ImGui::PushItemWidth(s1.x - s0.x - searchHeight - 16.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.2f, 0.5f, 1.0f, 0.35f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::InputTextWithHint("##StartSearch", "Type to search apps...", g_searchFilter, sizeof(g_searchFilter));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();

        ImGui::SetCursorScreenPos(ImVec2(q0.x + 16.0f * scale, s1.y + 12.0f * scale));
        ImGui::Text("Pinned");

        const int tileCols = 4;
        const int tileRows = 2;
        float tileGap = 10.0f * scale;
        float tilePad = 4.0f * scale;
        ImVec2 gridStart = ImVec2(q0.x + 16.0f * scale, s1.y + 34.0f * scale);
        float gridAvailW = panelWidth - 32.0f * scale - tilePad * 2.0f;
        float tileW = (gridAvailW - tileGap * (tileCols - 1)) / tileCols;
        float tileH = 76.0f * scale;

        ImDrawList* startDraw = ImGui::GetWindowDrawList();
        int gridCol = 0;
        float gridX = gridStart.x + tilePad;
        float gridY = gridStart.y;
        int maxTiles = (int)taskbarPinned.size();
        if (maxTiles > tileCols * tileRows) maxTiles = tileCols * tileRows;

        for (int i = 0; i < maxTiles; ++i) {
            ImVec2 b0 = ImVec2(gridX, gridY);
            ImVec2 b1 = ImVec2(gridX + tileW, gridY + tileH);

            ImGui::PushID(i + 500);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton("PinnedTile", ImVec2(tileW, tileH));
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

            if (hovered) {
                startDraw->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 220), 10.0f * scale);
                startDraw->AddRect(b0, b1, IM_COL32(0, 120, 215, 80), 10.0f * scale, 0, 1.2f);
            } else {
                startDraw->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 140), 10.0f * scale);
                startDraw->AddRect(b0, b1, IM_COL32(0, 0, 0, 15), 10.0f * scale, 0, 1.0f);
            }

            ImVec2 iconCenter = ImVec2(b0.x + tileW * 0.5f, b0.y + tileH * 0.38f);
            ID3D11ShaderResourceView* iconSrv = taskbarPinned[i].Icon;
            if (iconSrv) {
                float imgSize = tileW * 0.55f;
                ImVec2 imgPos = ImVec2(iconCenter.x - imgSize * 0.5f, iconCenter.y - imgSize * 0.5f);
                startDraw->AddImage((ImTextureID)iconSrv, imgPos, ImVec2(imgPos.x + imgSize, imgPos.y + imgSize));
            } else {
                ImVec4 glyphAccent = ImGui::ColorConvertU32ToFloat4(taskbarPinned[i].Accent);
                glyphAccent.w = 0.85f;
                drawAppGlyph(iconCenter, tileW * 0.75f, ImGui::ColorConvertFloat4ToU32(glyphAccent));
            }

            const char* label = taskbarPinned[i].Label.c_str();
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            float labelX = b0.x + (tileW - labelSize.x) * 0.5f;
            float labelY = b1.y - labelSize.y - 6.0f * scale;
            startDraw->AddText(ImVec2(labelX, labelY), IM_COL32(35, 45, 65, 230), label);

            if (clicked) {
                taskbarStartOpen = false;
                if (!taskbarPinned[i].AppPath.empty()) {
                    launchTaskbarApp(taskbarPinned[i].AppPath.c_str());
                }
            }

            ImGui::PopID();
            gridCol++;
            if (gridCol >= tileCols) {
                gridCol = 0;
                gridX = gridStart.x + tilePad;
                gridY += tileH + tileGap;
            } else {
                gridX += tileW + tileGap;
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(gridStart.x, gridY + tileH + tileGap));
        ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
        ImGui::Text(g_searchFilter[0] ? "Search results" : "All apps");
        ImGui::Separator();
        ImGui::BeginChild("StartAllApps", ImVec2(-1.0f, -8.0f * scale), false, ImGuiWindowFlags_NoBackground);
        int shown = 0;
        std::vector<StartProgram> programsSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_startMenuMutex);
            programsSnapshot = g_startPrograms;
        }
        for (int pi = 0; pi < (int)programsSnapshot.size(); ++pi) {
            const auto& sp = programsSnapshot[pi];
            if (g_searchFilter[0] && !SearchMatch(g_searchFilter, sp.name, sp.lnkPath) &&
                !SearchMatch(g_searchFilter, sp.category, sp.targetPath))
                continue;
            if (!g_searchFilter[0] && shown >= 80) break;
            ImGui::PushID(pi + 1000);
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            float rowH = 32.0f * scale;
            ImGui::InvisibleButton("StartProg", ImVec2(ImGui::GetContentRegionAvail().x, rowH));
            bool hov = ImGui::IsItemHovered();
            bool clk = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            ImDrawList* rd = ImGui::GetWindowDrawList();
            ImVec2 rowMax = ImGui::GetItemRectMax();
            if (hov) rd->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 180), 8.0f * scale);
            ID3D11ShaderResourceView* ico = TextureLoader::PeekCachedIcon(sp.lnkPath);
            float ic = 22.0f * scale;
            ImVec2 ip = ImVec2(rowMin.x + 6.0f * scale, rowMin.y + (rowH - ic) * 0.5f);
            if (ico) rd->AddImage((ImTextureID)ico, ip, ImVec2(ip.x + ic, ip.y + ic));
            rd->AddText(ImVec2(ip.x + ic + 8.0f * scale, rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
                        IM_COL32(35, 45, 65, 230), sp.name.c_str());
            if (clk) {
                taskbarStartOpen = false;
                LaunchAppByPath(StartLaunchPath(sp).c_str());
            }
            ImGui::PopID();
            shown++;
        }
        if (shown == 0) ImGui::TextDisabled("No matching programs");
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    if (taskbarTrayOpen) {
        static std::vector<TrayIconEntry> s_trayIcons;
        static std::unordered_map<std::wstring, ID3D11ShaderResourceView*> s_trayIconCache;
        static int s_trayQueryFrame = 999;
        s_trayQueryFrame++;
        if (s_trayQueryFrame >= 60) {
            s_trayQueryFrame = 0;
            s_trayIcons = QueryTrayIcons();
            const std::vector<RunningWindow>& rw = s_runningWindowsCache;
            for (auto& icon : s_trayIcons) {
                if (!icon.exePath.empty()) continue;
                for (const auto& w : rw) {
                    if (!w.path.empty()) {
                        size_t p = w.path.rfind(L'\\');
                        std::wstring fname = (p != std::wstring::npos) ? w.path.substr(p + 1) : w.path;
                        size_t d = fname.rfind(L'.');
                        std::wstring nameOnly = (d != std::wstring::npos) ? fname.substr(0, d) : fname;
                        if (!nameOnly.empty()
                            && icon.tooltip.find(nameOnly) != std::wstring::npos) {
                            icon.exePath = w.path;
                            break;
                        }
                    }
                }
            }
        }
        int iconCount = (int)s_trayIcons.size();
        float rowHeight = 28.0f;
        float panelW = 220.0f;
        float panelH = 50.0f + iconCount * rowHeight;
        if (panelH < 100.0f) panelH = 100.0f;
        if (panelH > 400.0f) panelH = 400.0f;
        ImVec2 panelPos = ImVec2(p1.x - panelW - paddingX, barPos.y - panelH - 10.0f);
        ImGui::SetNextWindowPos(panelPos);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::Begin("CrossDim Tray", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImDrawList* panelDraw = ImGui::GetWindowDrawList();
        ImVec2 q0 = ImGui::GetWindowPos();
        ImVec2 q1 = ImVec2(q0.x + panelW, q0.y + panelH);
        panelDraw->AddRectFilled(q0, q1, IM_COL32(244, 246, 250, 235), 12.0f);
        panelDraw->AddRect(q0, q1, IM_COL32(255, 255, 255, 120), 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
        ImGui::Text(s_trayIcons.empty() ? "(no tray icons detected - Win11 XAML tray)" : "System Tray");
        ImGui::Separator();
        for (const auto& icon : s_trayIcons) {
            ID3D11ShaderResourceView* iconSrv = nullptr;
            if (!icon.exePath.empty()) {
                auto it = s_trayIconCache.find(icon.exePath);
                if (it != s_trayIconCache.end()) iconSrv = it->second;
                else {
                    iconSrv = GetTaskbarIcon(device, icon.exePath);
                    s_trayIconCache[icon.exePath] = iconSrv;
                }
            }
            float imgSize = 22.0f;
            if (iconSrv) ImGui::Image((ImTextureID)iconSrv, ImVec2(imgSize, imgSize));
            else ImGui::Dummy(ImVec2(imgSize, imgSize));
            ImGui::SameLine();
            char label[128];
            WideCharToMultiByte(CP_UTF8, 0, icon.tooltip.c_str(), -1, label, sizeof(label), nullptr, nullptr);
            ImGui::Text("%s", label);
        }
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}
