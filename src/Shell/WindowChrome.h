#pragma once
#include <windows.h>
#include <vector>
#include "imgui.h"
#include "Shell/WindowManager.h"

extern RECT g_taskbarRect;
extern bool g_taskbarRectValid;

enum SnapZone {
    SNAP_NONE = 0,
    SNAP_LEFT_HALF,
    SNAP_RIGHT_HALF,
    SNAP_MAXIMIZE,
    SNAP_TOP_LEFT,
    SNAP_TOP_RIGHT,
    SNAP_BOTTOM_LEFT,
    SNAP_BOTTOM_RIGHT
};

inline SnapZone DetectSnapZone(int mx, int my, const RECT& area) {
    const int thresh = 20;
    bool nearL = (mx - area.left) < thresh;
    bool nearR = (area.right - mx) < thresh;
    bool nearT = (my - area.top) < thresh;
    if (nearT && nearL) return SNAP_TOP_LEFT;
    if (nearT && nearR) return SNAP_TOP_RIGHT;
    if (nearT) return SNAP_MAXIMIZE;
    if (nearL) return SNAP_LEFT_HALF;
    if (nearR) return SNAP_RIGHT_HALF;
    return SNAP_NONE;
}

inline RECT GetSnapRect(SnapZone zone, const RECT& area) {
    RECT r = area;
    switch (zone) {
    case SNAP_LEFT_HALF:  r.right = area.left + (area.right - area.left) / 2; break;
    case SNAP_RIGHT_HALF: r.left  = area.left + (area.right - area.left) / 2; break;
    case SNAP_MAXIMIZE:   break;
    case SNAP_TOP_LEFT:   r.right = area.left + (area.right - area.left) / 2;
                          r.bottom = area.top + (area.bottom - area.top) / 2; break;
    case SNAP_TOP_RIGHT:  r.left  = area.left + (area.right - area.left) / 2;
                          r.bottom = area.top + (area.bottom - area.top) / 2; break;
    case SNAP_BOTTOM_LEFT:r.right = area.left + (area.right - area.left) / 2;
                          r.top   = area.top + (area.bottom - area.top) / 2; break;
    case SNAP_BOTTOM_RIGHT:r.left = area.left + (area.right - area.left) / 2;
                          r.top  = area.top + (area.bottom - area.top) / 2; break;
    default: break;
    }
    return r;
}

inline RECT GetDefaultSnapWorkArea() {
    RECT workArea;
    workArea.left = 0;
    workArea.top = 0;
    workArea.right = GetSystemMetrics(SM_CXSCREEN);
    workArea.bottom = g_taskbarRectValid ? g_taskbarRect.top : GetSystemMetrics(SM_CYSCREEN);
    return workArea;
}

inline void SnapHijackedWindow(HWND target, int snapAction, const RECT& workArea) {
    if (!target || !IsWindow(target)) return;

    if (snapAction == 3) { // DOWN: restore if maximized, else minimize
        if (IsZoomed(target)) {
            ShowWindow(target, SW_RESTORE);
        } else {
            ShowWindow(target, SW_MINIMIZE);
        }
        return;
    }
    RECT r;
    if (snapAction == 2) { // UP: Maximize to workArea
        r = workArea;
    } else if (snapAction == 0) { // LEFT: Left half
        r = workArea;
        r.right = workArea.left + (workArea.right - workArea.left) / 2;
    } else if (snapAction == 1) { // RIGHT: Right half
        r = workArea;
        r.left = workArea.left + (workArea.right - workArea.left) / 2;
    } else {
        return;
    }
    if (IsZoomed(target)) ShowWindow(target, SW_RESTORE);
    SetWindowPos(target, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_SHOWWINDOW);
}

inline void SnapHijackedWindow(HWND target, int snapAction) {
    SnapHijackedWindow(target, snapAction, GetDefaultSnapWorkArea());
}

inline void RenderWindowChrome() {
    if (g_hijackedWindows.empty()) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    RECT workArea;
    workArea.left = (LONG)viewport->WorkPos.x;
    workArea.top = (LONG)viewport->WorkPos.y;
    workArea.right = (LONG)(viewport->WorkPos.x + viewport->WorkSize.x);
    workArea.bottom = g_taskbarRectValid ? g_taskbarRect.top : (LONG)(viewport->WorkPos.y + viewport->WorkSize.y);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("CrossDim WindowChrome", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* chromeDraw = ImGui::GetWindowDrawList();
    static HWND dragHwnd = nullptr;
    static ImVec2 dragOffset = ImVec2(0.0f, 0.0f);
    static SnapZone dragSnapZone = SNAP_NONE;
    std::vector<HijackedWindow> alive;
    alive.reserve(g_hijackedWindows.size());

    for (const auto& win : g_hijackedWindows) {
        if (!IsWindow(win.hwnd)) continue;
        if (!IsWindowVisible(win.hwnd)) {
            alive.push_back(win);
            continue;
        }
        RECT r; GetWindowRect(win.hwnd, &r);
        float barHeight = 30.0f;
        float btnSize = 14.0f;
        float btnGap = 8.0f;
        float btnPad = 10.0f;
        ImVec2 barMin = ImVec2((float)r.left, (float)r.top);
        ImVec2 barMax = ImVec2((float)r.right, (float)r.top + barHeight);

        chromeDraw->AddRectFilled(barMin, barMax, IM_COL32(250, 250, 252, 235), 6.0f);
        chromeDraw->AddRect(barMin, barMax, IM_COL32(255, 255, 255, 120), 6.0f);

        float ctrlWidth = btnSize * 3.0f + btnGap * 2.0f;
        float dragWidth = (barMax.x - barMin.x) - (ctrlWidth + btnPad * 2.0f);
        if (dragWidth < 20.0f) dragWidth = 20.0f;

        ImGui::PushID(win.hwnd);
        ImGui::SetCursorScreenPos(barMin);
        ImGui::InvisibleButton("WinDrag", ImVec2(dragWidth, barHeight));
        if (ImGui::IsItemActivated()) {
            dragHwnd = win.hwnd;
            dragSnapZone = SNAP_NONE;
            dragOffset = ImVec2(ImGui::GetIO().MousePos.x - barMin.x, ImGui::GetIO().MousePos.y - barMin.y);
            LONG style = GetWindowLong(win.hwnd, GWL_STYLE);
            if (style & WS_THICKFRAME) {
                SetWindowLong(win.hwnd, GWL_STYLE, style & ~WS_THICKFRAME);
                SetWindowPos(win.hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }
        if (dragHwnd == win.hwnd && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            int nx = (int)(ImGui::GetIO().MousePos.x - dragOffset.x);
            int ny = (int)(ImGui::GetIO().MousePos.y - dragOffset.y);
            SetWindowPos(win.hwnd, HWND_TOP, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            dragSnapZone = DetectSnapZone((int)ImGui::GetIO().MousePos.x, (int)ImGui::GetIO().MousePos.y, workArea);
        }
        if (dragHwnd == win.hwnd && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (dragSnapZone != SNAP_NONE) {
                RECT snapR = GetSnapRect(dragSnapZone, workArea);
                SetWindowPos(win.hwnd, HWND_TOP, snapR.left, snapR.top,
                             snapR.right - snapR.left, snapR.bottom - snapR.top,
                             SWP_NOZORDER);
            }
            LONG style = GetWindowLong(win.hwnd, GWL_STYLE);
            if (!(style & WS_THICKFRAME) && (win.originalStyle & WS_THICKFRAME)) {
                SetWindowLong(win.hwnd, GWL_STYLE, style | WS_THICKFRAME);
                SetWindowPos(win.hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            dragHwnd = nullptr;
            dragSnapZone = SNAP_NONE;
        }

        float btnY = barMin.y + (barHeight - btnSize) * 0.5f;
        float x = barMax.x - btnPad - btnSize;

        ImGui::SetCursorScreenPos(ImVec2(x, btnY));
        ImGui::InvisibleButton("WinClose", ImVec2(btnSize, btnSize));
        bool closeHover = ImGui::IsItemHovered();
        ImU32 closeColor = closeHover ? IM_COL32(255, 95, 95, 230) : IM_COL32(255, 120, 120, 210);
        chromeDraw->AddCircleFilled(ImVec2(x + btnSize * 0.5f, btnY + btnSize * 0.5f), btnSize * 0.48f, closeColor);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            PostMessage(win.hwnd, WM_CLOSE, 0, 0);
        }

        x -= btnGap + btnSize;
        ImGui::SetCursorScreenPos(ImVec2(x, btnY));
        ImGui::InvisibleButton("WinMax", ImVec2(btnSize, btnSize));
        bool maxHover = ImGui::IsItemHovered();
        ImU32 maxColor = maxHover ? IM_COL32(255, 210, 90, 230) : IM_COL32(255, 225, 140, 210);
        chromeDraw->AddRectFilled(ImVec2(x + 2.0f, btnY + 2.0f), ImVec2(x + btnSize - 2.0f, btnY + btnSize - 2.0f), maxColor, 3.0f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (IsZoomed(win.hwnd)) ShowWindow(win.hwnd, SW_RESTORE);
            else ShowWindow(win.hwnd, SW_MAXIMIZE);
        }

        x -= btnGap + btnSize;
        ImGui::SetCursorScreenPos(ImVec2(x, btnY));
        ImGui::InvisibleButton("WinMin", ImVec2(btnSize, btnSize));
        bool minHover = ImGui::IsItemHovered();
        ImU32 minColor = minHover ? IM_COL32(110, 220, 140, 230) : IM_COL32(140, 235, 165, 210);
        chromeDraw->AddRectFilled(ImVec2(x + 3.0f, btnY + btnSize * 0.5f), ImVec2(x + btnSize - 3.0f, btnY + btnSize * 0.5f + 2.0f), minColor, 1.5f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ShowWindow(win.hwnd, SW_MINIMIZE);
        }

        ImGui::PopID();
        alive.push_back(win);
    }

    if (dragHwnd != nullptr && dragSnapZone != SNAP_NONE) {
        RECT snapR = GetSnapRect(dragSnapZone, workArea);
        ImVec2 p0 = ImVec2((float)snapR.left, (float)snapR.top);
        ImVec2 p1 = ImVec2((float)snapR.right, (float)snapR.bottom);
        chromeDraw->AddRectFilled(p0, p1, IM_COL32(0, 140, 255, 60), 6.0f);
        chromeDraw->AddRect(p0, p1, IM_COL32(0, 160, 255, 180), 6.0f, 0, 2.0f);
    }

    g_hijackedWindows.swap(alive);
    ImGui::End();
    ImGui::PopStyleVar(3);
}
