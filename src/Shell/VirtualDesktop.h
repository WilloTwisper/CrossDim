#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <d3d11.h>
#include "imgui.h"
#include "Shell/DesktopManager.h"
#include "Shell/WindowManager.h"
#include "Shell/AIServer.h"

// Virtual desktop state
inline int g_activeDesktopIndex = 0;
inline std::vector<int> g_desktopSlots;
inline int g_nextDesktopId = 1;
// Per-desktop-id tombstones: paths the user removed; kept out of future scans
inline std::unordered_map<int, std::vector<std::wstring>> g_desktopRemovedPaths;

inline int DesktopIdAt(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= (int)g_desktopSlots.size()) return 0;
    return g_desktopSlots[slotIdx];
}
inline int ActiveDesktopId() { return DesktopIdAt(g_activeDesktopIndex); }

inline int g_targetDesktopIndex = -1;
inline float g_desktopTransition = 0.0f;
inline const float g_desktopTransitionSpeed = 4.0f;
inline bool g_showDesktopOverview = false;
inline std::vector<std::vector<HijackedWindow>> g_desktopWindows;

inline int GetDesktopSlotCount() { return (int)g_desktopSlots.size(); }

inline void SaveVirtualDesktopsConfig() {
    HANDLE hFile = CreateFileW(L"desktops.cfg", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written;
    DWORD magic = 0x4B534556; // "VESK"
    DWORD version = 1;
    DWORD slotCount = (DWORD)g_desktopSlots.size();
    DWORD activeIdx = (DWORD)g_activeDesktopIndex;
    DWORD nextId = (DWORD)g_nextDesktopId;
    WriteFile(hFile, &magic, sizeof(magic), &written, nullptr);
    WriteFile(hFile, &version, sizeof(version), &written, nullptr);
    WriteFile(hFile, &slotCount, sizeof(slotCount), &written, nullptr);
    WriteFile(hFile, &activeIdx, sizeof(activeIdx), &written, nullptr);
    WriteFile(hFile, &nextId, sizeof(nextId), &written, nullptr);
    if (slotCount > 0) {
        WriteFile(hFile, g_desktopSlots.data(), (DWORD)(slotCount * sizeof(int)), &written, nullptr);
    }
    CloseHandle(hFile);
}

inline bool LoadVirtualDesktopsConfig() {
    HANDLE hFile = CreateFileW(L"desktops.cfg", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    DWORD magic = 0, version = 0, slotCount = 0, activeIdx = 0, nextId = 1;
    if (!ReadFile(hFile, &magic, sizeof(magic), &read, nullptr) || magic != 0x4B534556) {
        CloseHandle(hFile);
        return false;
    }
    if (!ReadFile(hFile, &version, sizeof(version), &read, nullptr) || version != 1) {
        CloseHandle(hFile);
        return false;
    }
    ReadFile(hFile, &slotCount, sizeof(slotCount), &read, nullptr);
    ReadFile(hFile, &activeIdx, sizeof(activeIdx), &read, nullptr);
    ReadFile(hFile, &nextId, sizeof(nextId), &read, nullptr);
    if (slotCount > 0 && slotCount <= 16) {
        std::vector<int> slots(slotCount);
        if (ReadFile(hFile, slots.data(), (DWORD)(slotCount * sizeof(int)), &read, nullptr)) {
            g_desktopSlots = slots;
            g_desktopWindows.assign(slotCount, {});
            g_nextDesktopId = (int)nextId;
            if (activeIdx < slotCount) g_activeDesktopIndex = (int)activeIdx;
            else g_activeDesktopIndex = 0;
            CloseHandle(hFile);
            return true;
        }
    }
    CloseHandle(hFile);
    return false;
}

inline void SwitchToDesktop(int slotIdx) {
    if (slotIdx == g_activeDesktopIndex || slotIdx < 0 || slotIdx >= (int)g_desktopSlots.size()) return;
    if (g_targetDesktopIndex >= 0) return;
    SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
    // Hide all windows on current desktop, capturing minimized state
    for (auto& w : g_hijackedWindows) {
        w.wasMinimized = IsIconic(w.hwnd) != FALSE;
        ShowWindow(w.hwnd, SW_HIDE);
    }
    g_desktopWindows[g_activeDesktopIndex] = g_hijackedWindows;
    g_targetDesktopIndex = slotIdx;
    g_desktopTransition = 0.0f;
}

inline void CompleteDesktopSwitch() {
    g_myApps.clear();
    g_hijackedWindows = g_desktopWindows[g_targetDesktopIndex];
    // Show windows on new desktop respecting minimized state
    for (const auto& w : g_hijackedWindows) {
        if (IsWindow(w.hwnd)) {
            if (w.wasMinimized) ShowWindow(w.hwnd, SW_MINIMIZE);
            else ShowWindow(w.hwnd, SW_SHOW);
        }
    }
    ScanDesktopForApps(g_myApps);
    LoadDesktopState(g_myApps, DesktopIdAt(g_targetDesktopIndex), &g_desktopRemovedPaths[DesktopIdAt(g_targetDesktopIndex)]);
    SeparateOverlappingCubes(g_myApps);
    LoadIconsForApps(g_myApps);
    g_activeDesktopIndex = g_targetDesktopIndex;
    g_targetDesktopIndex = -1;
    g_desktopTransition = 1.0f;
    SaveVirtualDesktopsConfig();
    AIServer::EmitEvent("desktop_switch", "{\"index\":" + std::to_string(g_activeDesktopIndex) + "}");
}

inline void CreateNewDesktop() {
    int newId = g_nextDesktopId++;
    g_desktopSlots.push_back(newId);
    SaveDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
    // Hide current desktop's windows and save to cache
    for (auto& w : g_hijackedWindows) {
        w.wasMinimized = IsIconic(w.hwnd) != FALSE;
        ShowWindow(w.hwnd, SW_HIDE);
    }
    g_desktopWindows[g_activeDesktopIndex] = g_hijackedWindows;
    g_activeDesktopIndex = (int)g_desktopSlots.size() - 1;
    g_hijackedWindows.clear();
    // Initialize new desktop's window cache
    g_desktopWindows.push_back({});
    g_myApps.clear();
    ScanDesktopForApps(g_myApps);
    LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
    SeparateOverlappingCubes(g_myApps);
    LoadIconsForApps(g_myApps);
    SaveVirtualDesktopsConfig();
    AIServer::EmitEvent("desktop_create", "{\"index\":" + std::to_string(g_activeDesktopIndex) + "}");
}

inline void CloseDesktop(int slotIdx) {
    if (g_desktopSlots.size() <= 1) return;
    if (g_targetDesktopIndex >= 0) return;
    int closingId = g_desktopSlots[slotIdx];
    wchar_t fname[64];
    if (closingId == 0) wcscpy_s(fname, L"desktop.cddesk");
    else swprintf_s(fname, L"desktop_%d.cddesk", closingId);
    DeleteFileW(fname);
    g_desktopRemovedPaths.erase(closingId);
    int removeSlot = slotIdx;
    if (slotIdx == g_activeDesktopIndex) {
        g_myApps.clear();
        std::vector<HijackedWindow> closingWindows;
        for (const auto& w : g_hijackedWindows) {
            if (IsWindow(w.hwnd)) closingWindows.push_back(w);
        }
        g_desktopSlots.erase(g_desktopSlots.begin() + removeSlot);
        g_desktopWindows.erase(g_desktopWindows.begin() + removeSlot);
        if (g_activeDesktopIndex >= (int)g_desktopSlots.size())
            g_activeDesktopIndex = (int)g_desktopSlots.size() - 1;
        g_hijackedWindows = g_desktopWindows[g_activeDesktopIndex];
        for (const auto& w : closingWindows) {
            bool alreadyIn = false;
            for (const auto& existing : g_hijackedWindows) {
                if (existing.hwnd == w.hwnd) { alreadyIn = true; break; }
            }
            if (!alreadyIn) {
                g_hijackedWindows.push_back(w);
            }
        }
        g_desktopWindows[g_activeDesktopIndex] = g_hijackedWindows;
        for (const auto& w : g_hijackedWindows) {
            if (IsWindow(w.hwnd)) {
                if (w.wasMinimized) ShowWindow(w.hwnd, SW_MINIMIZE);
                else ShowWindow(w.hwnd, SW_SHOW);
            }
        }
        ScanDesktopForApps(g_myApps);
        LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
        SeparateOverlappingCubes(g_myApps);
        LoadIconsForApps(g_myApps);
    } else {
        // Migrate hidden windows from the closed desktop to the active desktop to prevent window/process leaks
        if (removeSlot < (int)g_desktopWindows.size()) {
            for (const auto& w : g_desktopWindows[removeSlot]) {
                if (IsWindow(w.hwnd)) {
                    ShowWindow(w.hwnd, SW_SHOW);
                    g_hijackedWindows.push_back(w);
                }
            }
        }
        g_desktopSlots.erase(g_desktopSlots.begin() + removeSlot);
        g_desktopWindows.erase(g_desktopWindows.begin() + removeSlot);
        if (g_activeDesktopIndex > slotIdx) g_activeDesktopIndex--;
        if (g_activeDesktopIndex < (int)g_desktopWindows.size()) {
            g_desktopWindows[g_activeDesktopIndex] = g_hijackedWindows;
        }
    }
    SaveVirtualDesktopsConfig();
    AIServer::EmitEvent("desktop_close", "{\"slot\":" + std::to_string(slotIdx) + ",\"count\":" + std::to_string(g_desktopSlots.size()) + "}");
}

inline void RenderDesktopOverview(ID3D11Device* device) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* ovDraw = ImGui::GetForegroundDrawList();
    ImVec2 ovP = viewport->WorkPos;
    ImVec2 ovS = viewport->WorkSize;
    ImVec2 ovEnd = ImVec2(ovP.x + ovS.x, ovP.y + ovS.y);

    // Bright quartz acrylic background
    ovDraw->AddRectFilled(ovP, ovEnd, IM_COL32(235, 240, 248, 220));
    ovDraw->AddRectFilled(ovP, ovEnd, IM_COL32(250, 252, 255, 120));
    for (int ring = 0; ring < 14; ++ring) {
        float t = ring / 14.0f;
        float inset = t * 150.0f;
        float alpha = (int)(30.0f * (1.0f - t));
        ovDraw->AddRect(ImVec2(ovP.x + inset, ovP.y + inset),
                        ImVec2(ovEnd.x - inset, ovEnd.y - inset),
                        IM_COL32(200, 210, 230, alpha), 60.0f, 0, 2.5f);
    }

    ImGui::SetNextWindowPos(ovP);
    ImGui::SetNextWindowSize(ovS);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("TaskViewOverlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Sync cache
    g_desktopWindows[g_activeDesktopIndex] = g_hijackedWindows;

    // --- WINDOW CARDS ---
    float cardW = 240.0f, cardH = 155.0f, cardGapX = 24.0f, cardGapY = 32.0f;
    int cols = (int)((ovS.x - 100.0f + cardGapX) / (cardW + cardGapX));
    if (cols < 3) cols = 3;
    float totalCardW = cols * cardW + (cols - 1) * cardGapX;
    float cardStartX = ovP.x + (ovS.x - totalCardW) * 0.5f;
    float cardAreaTop = ovP.y + 60.0f;

    int totalWindows = 0;
    static std::vector<std::pair<HWND, std::wstring>> tvCards;
    tvCards.clear();
    for (const auto& w : g_hijackedWindows) {
        if (!IsWindow(w.hwnd)) continue;
        DWORD pid = 0; GetWindowThreadProcessId(w.hwnd, &pid);
        std::wstring exePath = GetProcessPath(pid);
        tvCards.push_back({ w.hwnd, exePath });
        totalWindows++;
    }

    ovDraw->AddText(ImVec2(cardStartX, ovP.y + 24.0f), IM_COL32(60, 70, 100, 200),
                    totalWindows > 0 ? "Active windows" : "");

    for (int i = 0; i < totalWindows; ++i) {
        HWND wh = tvCards[i].first;
        int col = i % cols, row = i / cols;
        float cx = cardStartX + col * (cardW + cardGapX);
        float cy = cardAreaTop + row * (cardH + cardGapY);
        ImVec2 c0 = ImVec2(cx, cy), c1 = ImVec2(cx + cardW, cy + cardH);

        bool hovered = ImGui::IsMouseHoveringRect(c0, c1);
        ImU32 cardBg = hovered ? IM_COL32(220, 226, 240, 230) : IM_COL32(240, 244, 250, 220);
        ovDraw->AddRectFilled(ImVec2(cx + 2, cy + 3), ImVec2(c1.x + 2, c1.y + 3), IM_COL32(180, 185, 200, 50), 10.0f);
        ovDraw->AddRectFilled(c0, c1, cardBg, 10.0f);
        ovDraw->AddRect(c0, c1, hovered ? IM_COL32(150, 170, 210, 160) : IM_COL32(180, 190, 210, 60), 10.0f, 0, 1.5f);

        // App icon
        ID3D11ShaderResourceView* iconSrv = GetWindowIconTexture(device, wh, tvCards[i].second);
        if (iconSrv) {
            float iconSz = 32.0f;
            ovDraw->AddImage((ImTextureID)iconSrv, ImVec2(cx + 14, cy + 14),
                             ImVec2(cx + 14 + iconSz, cy + 14 + iconSz));
        } else {
            ovDraw->AddCircleFilled(ImVec2(cx + 30, cy + 30), 14.0f, IM_COL32(0, 120, 220, 180));
        }

        // Window title
        WCHAR wtitle[256]; GetWindowTextW(wh, wtitle, 256);
        char ctitle[256]; WideCharToMultiByte(CP_UTF8, 0, wtitle, -1, ctitle, 256, nullptr, nullptr);
        if (ctitle[0] == 0) strcpy_s(ctitle, "(untitled)");
        ovDraw->AddText(ImVec2(cx + 14, cy + cardH - 30.0f), IM_COL32(30, 40, 70, 230), ctitle);

        // Click
        ImGui::SetCursorScreenPos(c0);
        char wid[32]; sprintf_s(wid, "TVC_%p", wh);
        ImGui::InvisibleButton(wid, ImVec2(cardW, cardH));
        if (ImGui::IsItemClicked()) {
            ShowWindow(wh, SW_SHOW); SetForegroundWindow(wh);
            g_showDesktopOverview = false;
        }
    }

    // --- BOTTOM DESKTOP BAR ---
    float barH = 120.0f;
    float barY = ovEnd.y - barH;
    float barX = ovP.x + 60.0f;
    float barW2 = ovS.x - 120.0f;
    ImVec2 b0 = ImVec2(barX, barY), b1 = ImVec2(barX + barW2, ovEnd.y - 8.0f);
    ovDraw->AddRectFilled(b0, b1, IM_COL32(230, 236, 245, 245), 14.0f);
    ovDraw->AddRectFilled(b0, b1, IM_COL32(245, 248, 252, 100), 14.0f);
    ovDraw->AddRect(b0, b1, IM_COL32(190, 200, 220, 40), 14.0f, 0, 1.0f);
    ovDraw->AddText(ImVec2(barX + 20, barY + 12), IM_COL32(80, 90, 120, 200), "Desktops");

    float dsW = 140.0f, dsH = 70.0f, dsG = 12.0f;
    float dsY = barY + 36.0f;
    float addW = 40.0f;
    int slotCount = GetDesktopSlotCount();
    for (int i = 0; i < slotCount; ++i) {
        float dx = barX + 20.0f + i * (dsW + dsG);
        ImVec2 d0 = ImVec2(dx, dsY), d1 = ImVec2(dx + dsW, dsY + dsH);
        bool active = (i == g_activeDesktopIndex && g_targetDesktopIndex < 0);
        bool dH = ImGui::IsMouseHoveringRect(d0, d1);
        ImU32 dFill = active ? IM_COL32(60, 140, 240, 200) : dH ? IM_COL32(210, 218, 235, 200) : IM_COL32(225, 232, 245, 150);
        ovDraw->AddRectFilled(d0, d1, dFill, 6.0f);
        ovDraw->AddRect(d0, d1, active ? IM_COL32(40, 130, 240, 200) : IM_COL32(180, 195, 220, 60), 6.0f, 0, 1.0f);

        int miniW = (int)g_desktopWindows[i].size(); if (miniW > 7) miniW = 7;
        if (miniW > 0) {
            float mw = 8.0f, mg = 3.0f, mTot = miniW * mw + (miniW - 1) * mg;
            float mx = dx + (dsW - mTot) * 0.5f, my = dsY + dsH - 16.0f;
            for (int m = 0; m < miniW; ++m)
                ovDraw->AddRectFilled(ImVec2(mx + m * (mw + mg), my), ImVec2(mx + m * (mw + mg) + mw, my + 4.0f),
                                      active ? IM_COL32(60, 120, 200, 160) : IM_COL32(140, 155, 180, 80), 2.0f);
        }
        char dlbl[16]; sprintf_s(dlbl, "Desktop %d", i + 1);
        ovDraw->AddText(ImVec2(dx + 8, dsY + 5), IM_COL32(40, 55, 90, 220), dlbl);
        ImGui::SetCursorScreenPos(d0);
        char did[16]; sprintf_s(did, "TVD%d", i);
        ImGui::InvisibleButton(did, ImVec2(dsW, dsH));
        if (ImGui::IsItemClicked()) { SwitchToDesktop(i); g_showDesktopOverview = false; }

        if (slotCount > 1 && !active) {
            float cx = dx + dsW - 13, cy = dsY + 2;
            ImGui::SetCursorScreenPos(ImVec2(cx, cy));
            char cid[16]; sprintf_s(cid, "TVC%d", i);
            ImGui::InvisibleButton(cid, ImVec2(11, 11));
            bool clH = ImGui::IsItemHovered();
            ovDraw->AddCircleFilled(ImVec2(cx + 5.5f, cy + 5.5f), 4.5f, clH ? IM_COL32(210, 55, 55, 200) : IM_COL32(90, 35, 35, 100));
            ovDraw->AddLine(ImVec2(cx + 2.5f, cy + 2.5f), ImVec2(cx + 8.5f, cy + 8.5f), IM_COL32(255, 180, 180, 160), 1.0f);
            ovDraw->AddLine(ImVec2(cx + 8.5f, cy + 2.5f), ImVec2(cx + 2.5f, cy + 8.5f), IM_COL32(255, 180, 180, 160), 1.0f);
            if (ImGui::IsItemClicked()) CloseDesktop(i);
        }
    }
    // + button
    float nx = barX + 20.0f + slotCount * (dsW + dsG);
    ImVec2 n0 = ImVec2(nx, dsY), n1 = ImVec2(nx + addW, dsY + dsH);
    bool nH = ImGui::IsMouseHoveringRect(n0, n1);
    ovDraw->AddRectFilled(n0, n1, nH ? IM_COL32(210, 218, 235, 160) : IM_COL32(225, 232, 245, 90), 6.0f);
    ovDraw->AddRect(n0, n1, IM_COL32(180, 195, 220, 40), 6.0f, 0, 1.0f);
    ovDraw->AddLine(ImVec2(nx + addW * 0.5f, dsY + dsH * 0.28f), ImVec2(nx + addW * 0.5f, dsY + dsH * 0.72f), nH ? IM_COL32(60, 80, 120, 200) : IM_COL32(120, 140, 170, 100), 2.2f);
    ovDraw->AddLine(ImVec2(nx + addW * 0.28f, dsY + dsH * 0.5f), ImVec2(nx + addW * 0.72f, dsY + dsH * 0.5f), nH ? IM_COL32(60, 80, 120, 200) : IM_COL32(120, 140, 170, 100), 2.2f);
    ImGui::SetCursorScreenPos(n0);
    ImGui::InvisibleButton("TVAdd", ImVec2(addW, dsH));
    if (ImGui::IsItemClicked()) { CreateNewDesktop(); g_showDesktopOverview = false; }

    ImGui::End();
    ImGui::PopStyleVar(3);

    if ((ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        || ImGui::IsKeyPressed(ImGuiKey_Escape))
        g_showDesktopOverview = false;
}
