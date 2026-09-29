#pragma once
#include <windows.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>
#include <DirectXMath.h>
#include "imgui.h"
#include "Engine/Camera.h"
#include "Engine/TextureLoader.h"
#include "Engine/JsonUtils.h"
#include "Shell/DesktopManager.h"
#include "Shell/VirtualDesktop.h"
#include "Shell/StartMenu.h"
#include "Shell/AIServer.h"

// Forward references to interaction state in main.cpp
extern int   g_grabbedAppIndex;
extern bool  g_isDragging;
extern int   g_dropTargetIndex;
extern bool  g_needRescan;
extern bool  g_shellMenuPending;
extern bool  g_shellMenuBackground;
extern std::vector<std::wstring> g_shellMenuPaths;
extern std::wstring g_shellMenuFolder;
extern POINT g_shellMenuPt;

void LaunchAppByPath(LPCWSTR appPath);

// Spatial folder navigation
struct FolderState {
    std::wstring folderPath;
    std::string displayName;
    std::vector<AppCube> cubes;
    DirectX::XMFLOAT3 cameraPos;
    DirectX::XMFLOAT3 cameraRot;
};
inline std::vector<FolderState> g_folderStack;
inline bool g_isInFolder = false;
inline bool g_isFolderMaximized = true;
inline RECT g_folderWindowRect = { 100, 80, 1300, 850 };
inline std::vector<AppCube> g_folderCubes;
inline Camera g_folderCamera;
inline bool g_isDraggingFolderWin = false;
inline ImVec2 g_folderDragOff;
inline bool g_folderMouseLocked = false;
inline float g_folderTransitionT = 0.0f;
inline DirectX::XMFLOAT3 g_folderFromPos, g_folderToPos;
inline DirectX::XMFLOAT3 g_folderFromRot, g_folderToRot;
inline std::vector<AppCube> g_desktopBackupCubes;
inline DirectX::XMFLOAT3 g_desktopBackupCamPos, g_desktopBackupCamRot;

inline bool IsPathDirectory(const std::wstring& path) {
    return FsIsDirectory(path);
}

inline std::wstring UserDesktopPath() {
    WCHAR desk[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk)))
        return desk;
    return {};
}

inline std::wstring CurrentShellFolder() {
    if (g_isInFolder && !g_folderStack.empty())
        return g_folderStack.back().folderPath;
    return UserDesktopPath();
}

inline std::vector<std::wstring> SelectedItemPaths() {
    std::vector<std::wstring> paths;
    for (const auto& c : g_myApps) {
        if (c.IsSelected && !c.AppPath.empty()) paths.push_back(c.AppPath);
    }
    return paths;
}

inline void LaunchCube(const AppCube& cube) {
    if (CubeIsDirectory(cube)) {
        return;
    }
    LaunchAppByPath(CubeLaunchPath(cube).c_str());
}

inline void RescanCurrentView() {
    g_grabbedAppIndex = -1;
    g_isDragging = false;
    g_dropTargetIndex = -1;
    if (g_isInFolder && !g_folderStack.empty()) {
        const std::wstring& folder = g_folderStack.back().folderPath;
        if (g_isFolderMaximized) {
            std::vector<AppCube> old = g_myApps;
            g_myApps.clear();
            ScanFolderForApps(folder, g_myApps);
            MergeCubeVisuals(old, g_myApps);
            LoadIconsForApps(g_myApps);
        } else {
            std::vector<AppCube> foldOld = g_folderCubes;
            g_folderCubes.clear();
            ScanFolderForApps(folder, g_folderCubes);
            MergeCubeVisuals(foldOld, g_folderCubes);
            LoadIconsForApps(g_folderCubes);
            std::vector<AppCube> deskOld = g_myApps;
            g_myApps.clear();
            ScanDesktopForApps(g_myApps);
            LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
            MergeCubeVisuals(deskOld, g_myApps);
            SeparateOverlappingCubes(g_myApps);
            LoadIconsForApps(g_myApps);
        }
    } else {
        std::vector<AppCube> old = g_myApps;
        g_myApps.clear();
        ScanDesktopForApps(g_myApps);
        LoadDesktopState(g_myApps, ActiveDesktopId(), &g_desktopRemovedPaths[ActiveDesktopId()]);
        MergeCubeVisuals(old, g_myApps);
        SeparateOverlappingCubes(g_myApps);
        LoadIconsForApps(g_myApps);
    }
}

inline void QueueShellItemMenu(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    g_shellMenuPaths = paths;
    g_shellMenuBackground = false;
    GetCursorPos(&g_shellMenuPt);
    g_shellMenuPending = true;
}

inline void QueueShellBackgroundMenu(const std::wstring& folder) {
    g_shellMenuPaths.clear();
    g_shellMenuFolder = folder;
    g_shellMenuBackground = true;
    GetCursorPos(&g_shellMenuPt);
    g_shellMenuPending = true;
}

inline void EnterFolder(const std::wstring& path, Camera& camera) {
    if (g_folderStack.empty()) {
        g_desktopBackupCubes = g_myApps;
        for (auto& c : g_desktopBackupCubes) c.IconTexture = nullptr;
        g_desktopBackupCamPos = camera.Position;
        g_desktopBackupCamRot = camera.Rotation;
    } else {
        // Save current folder state to its stack entry before entering subfolder
        if (g_isFolderMaximized)
            g_folderStack.back().cubes = g_myApps;
        else
            g_folderStack.back().cubes = g_folderCubes;
        for (auto& c : g_folderStack.back().cubes) c.IconTexture = nullptr;
    }

    g_myApps.clear();
    g_folderCubes.clear();

    std::vector<AppCube> newCubes;
    ScanFolderForApps(path, newCubes);

    FolderState fs;
    std::wstring fn = path; size_t sl = fn.rfind(L'\\');
    if (sl != std::wstring::npos) fn = fn.substr(sl + 1);
    fs.displayName = WcsToUtf8(fn); // breadcrumb is UTF-8; byte truncation garbles CJK
    fs.folderPath = path;
    fs.cubes = newCubes;
    for (auto& c : fs.cubes) c.IconTexture = nullptr;
    g_folderStack.push_back(fs);
    g_isInFolder = true;
    g_isFolderMaximized = false;

    g_folderCubes = newCubes;
    LoadIconsForApps(g_folderCubes);
    g_myApps = g_desktopBackupCubes;
    LoadIconsForApps(g_myApps);
    DirectX::XMFLOAT3 center = { 0.0f, 1.5f, 0.0f };
    g_folderCamera.Position = { center.x, center.y, center.z - 5.0f };
    g_folderCamera.Rotation = { 0.0f, 0.0f, 0.0f };
    // Lock cursor to portal center
    LONG cx = (g_folderWindowRect.left + g_folderWindowRect.right) / 2;
    LONG cy = (g_folderWindowRect.top + g_folderWindowRect.bottom) / 2;
    SetCursorPos(cx, cy);
    AIServer::EmitEvent("folder_enter", "{\"path\":\"" + Json::Escape(WcsToUtf8(path)) + "\",\"depth\":" + std::to_string(g_folderStack.size()) + "}");
}

inline void ExitFolderToParent(Camera& camera) {
    if (g_folderStack.empty()) return;
    g_folderCubes.clear();
    g_folderStack.pop_back();
    if (g_folderStack.empty()) {
        // Back to desktop
        g_myApps = g_desktopBackupCubes;
        LoadIconsForApps(g_myApps);
        g_isInFolder = false;
        g_isFolderMaximized = true;
        camera.Position = g_desktopBackupCamPos;
        camera.Rotation = g_desktopBackupCamRot;
    } else {
        // Back to parent folder: restore desktop, rescan parent for portal
        g_myApps = g_desktopBackupCubes;
        LoadIconsForApps(g_myApps);
        // Rescan parent folder for portal cubes
        std::vector<AppCube> pcubes;
        ScanFolderForApps(g_folderStack.back().folderPath, pcubes);
        LoadIconsForApps(pcubes);
        g_folderCubes = pcubes;
        g_folderStack.back().cubes = pcubes;
        for (auto& c : g_folderStack.back().cubes) c.IconTexture = nullptr;
    }
    AIServer::EmitEvent("folder_exit", "{\"depth\":" + std::to_string(g_folderStack.size()) + "}");
}

inline void PruneUnusedIconCache() {
    std::unordered_set<std::wstring> keep;
    for (const auto& a : g_myApps) {
        if (!a.AppPath.empty()) keep.insert(a.AppPath);
        if (!a.TargetPath.empty()) keep.insert(a.TargetPath);
    }
    for (const auto& c : g_folderCubes) {
        if (!c.AppPath.empty()) keep.insert(c.AppPath);
        if (!c.TargetPath.empty()) keep.insert(c.TargetPath);
    }
    for (const auto& b : g_desktopBackupCubes) {
        if (!b.AppPath.empty()) keep.insert(b.AppPath);
        if (!b.TargetPath.empty()) keep.insert(b.TargetPath);
    }
    {
        std::lock_guard<std::mutex> lock(g_startMenuMutex);
        for (const auto& p : g_startPrograms) {
            if (!p.lnkPath.empty()) keep.insert(p.lnkPath);
            if (!p.targetPath.empty()) keep.insert(p.targetPath);
        }
    }
    TextureLoader::PruneIconCache(keep);
}

inline void ExitFolderToRoot(Camera& camera) {
    if (!g_isInFolder) return;
    g_folderCubes.clear();
    g_folderStack.clear();
    g_myApps = g_desktopBackupCubes;
    LoadIconsForApps(g_myApps);
    g_isInFolder = false;
    g_isFolderMaximized = true;
    camera.Position = g_desktopBackupCamPos;
    camera.Rotation = g_desktopBackupCamRot;
    g_needRescan = true;
    PruneUnusedIconCache();
    AIServer::EmitEvent("folder_exit", "{\"depth\":0}");
}

inline void MakeFolderWindowed(Camera& mainCam) {
    if (!g_isInFolder || !g_isFolderMaximized) return;
    // Save maximized folder cubes to stack
    if (!g_folderStack.empty()) {
        g_folderStack.back().cubes = g_myApps;
        for (auto& c : g_folderStack.back().cubes) c.IconTexture = nullptr;
    }
    // Set up portal — copy then null before release to avoid dangling pointers
    g_folderCubes = g_myApps;
    for (auto& c : g_folderCubes) c.IconTexture = nullptr;
    g_myApps = g_desktopBackupCubes;
    LoadIconsForApps(g_myApps);
    LoadIconsForApps(g_folderCubes);
    g_isFolderMaximized = false;
    DirectX::XMFLOAT3 center = { 0.0f, 1.5f, 0.0f };
    g_folderCamera.Position = { center.x, center.y, center.z - 5.0f };
    g_folderCamera.Rotation = { 0.0f, 0.0f, 0.0f };
}

inline void MakeFolderMaximized(Camera& mainCam) {
    if (!g_isInFolder || g_isFolderMaximized) return;
    // Save desktop state (in case user modified it)
    g_desktopBackupCubes = g_myApps;
    for (auto& c : g_desktopBackupCubes) c.IconTexture = nullptr;
    g_desktopBackupCamPos = mainCam.Position;
    g_desktopBackupCamRot = mainCam.Rotation;
    g_myApps = g_folderCubes;
    LoadIconsForApps(g_myApps);
    g_folderCubes.clear();
    g_isFolderMaximized = true;
    DirectX::XMFLOAT3 center = { 0.0f, 1.5f, 0.0f };
    g_folderFromPos = mainCam.Position;
    g_folderFromRot = mainCam.Rotation;
    g_folderToPos = { center.x, center.y, center.z - 6.0f };
    g_folderToRot = { 0.0f, 0.0f, 0.0f };
    g_folderTransitionT = 0.0f;
}

inline void RenderFolderBreadcrumbs(Camera& camera) {
    if (!g_isInFolder || !g_isFolderMaximized) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    float barH = 38.0f;
    ImVec2 barPos = ImVec2(vp->WorkPos.x, vp->WorkPos.y);
    ImVec2 barSize = ImVec2(vp->WorkSize.x, barH);
    ImGui::SetNextWindowPos(barPos);
    ImGui::SetNextWindowSize(barSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
    ImGui::Begin("##FolderBar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* fbDraw = ImGui::GetWindowDrawList();
    ImVec2 fbP0 = ImGui::GetWindowPos();
    ImVec2 fbP1 = ImVec2(fbP0.x + barSize.x, fbP0.y + barH);
    fbDraw->AddRectFilled(fbP0, fbP1, IM_COL32(32, 36, 50, 235));
    fbDraw->AddRectFilled(fbP0, fbP1, IM_COL32(50, 60, 85, 60));
    fbDraw->AddLine(ImVec2(fbP0.x, fbP1.y - 1.0f), ImVec2(fbP1.x, fbP1.y - 1.0f), IM_COL32(255, 255, 255, 15), 1.0f);

    // Breadcrumb segments
    float bx = fbP0.x + 10.0f, by = fbP0.y + (barH - 20.0f) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(bx, by));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 190, 200, 220));
    if (ImGui::SmallButton("Desktop")) {
        if (g_isInFolder) ExitFolderToRoot(camera);
    }
    ImGui::SameLine();
    for (int i = 0; i < (int)g_folderStack.size(); ++i) {
        ImGui::Text(">");
        ImGui::SameLine();
        char segId[64];
        sprintf_s(segId, "##BC%d", i);
        ImGui::PushID(i);
        if (ImGui::SmallButton(g_folderStack[i].displayName.c_str())) {
            while ((int)g_folderStack.size() > i + 1)
                ExitFolderToParent(camera);
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::PopStyleColor();

    // Window chrome buttons (right side)
    float btnSize = 14.0f, btnGap = 8.0f, btnPad = 12.0f;
    float btnY = fbP0.y + (barH - btnSize) * 0.5f;
    float cx = fbP1.x - btnPad - btnSize;

    // Close button
    ImGui::SetCursorScreenPos(ImVec2(cx, btnY));
    ImGui::InvisibleButton("FBClose", ImVec2(btnSize, btnSize));
    bool clH = ImGui::IsItemHovered();
    fbDraw->AddCircleFilled(ImVec2(cx + btnSize * 0.5f, btnY + btnSize * 0.5f), btnSize * 0.48f,
                            clH ? IM_COL32(255, 95, 95, 230) : IM_COL32(255, 120, 120, 200));
    if (ImGui::IsItemClicked()) ExitFolderToRoot(camera);

    // Minimize button (windowed mode)
    cx -= btnGap + btnSize;
    ImGui::SetCursorScreenPos(ImVec2(cx, btnY));
    ImGui::InvisibleButton("FBMin", ImVec2(btnSize, btnSize));
    bool fMinH = ImGui::IsItemHovered();
    fbDraw->AddRectFilled(ImVec2(cx + 3.0f, btnY + btnSize * 0.5f), ImVec2(cx + btnSize - 3.0f, btnY + btnSize * 0.5f + 2.0f),
                          fMinH ? IM_COL32(110, 220, 140, 230) : IM_COL32(140, 235, 165, 200), 1.5f);
    if (ImGui::IsItemClicked() && g_isFolderMaximized) MakeFolderWindowed(camera);

    // Maximize button
    cx -= btnGap + btnSize;
    ImGui::SetCursorScreenPos(ImVec2(cx, btnY));
    ImGui::InvisibleButton("FBMax", ImVec2(btnSize, btnSize));
    fbDraw->AddRectFilled(ImVec2(cx + 2.0f, btnY + 2.0f), ImVec2(cx + btnSize - 2.0f, btnY + btnSize - 2.0f),
                          ImGui::IsItemHovered() ? IM_COL32(255, 210, 90, 230) : IM_COL32(255, 225, 140, 200), 3.0f);
    if (ImGui::IsItemClicked()) {
        if (g_isFolderMaximized) MakeFolderWindowed(camera);
        else MakeFolderMaximized(camera);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

inline void RenderFolderWindowChrome(Camera& camera) {
    if (!g_isInFolder || g_isFolderMaximized) return;

    RECT& wr = g_folderWindowRect;
    float fbarH = 30.0f, fbtnS = 14.0f, fbtnG = 8.0f, fbtnP = 10.0f;
    ImVec2 fbarMin = ImVec2((float)wr.left, (float)wr.top);
    ImVec2 fbarMax = ImVec2((float)wr.right, (float)wr.top + fbarH);
    float fctrlW = fbtnS * 3.0f + fbtnG * 2.0f;
    float fdragW = (fbarMax.x - fbarMin.x) - (fctrlW + fbtnP * 2.0f);
    if (fdragW < 20.0f) fdragW = 20.0f;
    ImDrawList* fDraw = ImGui::GetForegroundDrawList();
    // Title bar background
    fDraw->AddRectFilled(fbarMin, fbarMax, IM_COL32(32, 36, 50, 240), 6.0f);
    fDraw->AddRect(fbarMin, fbarMax, IM_COL32(80, 90, 120, 120), 6.0f);
    // Breadcrumb title
    std::string fTitle;
    for (int i = 0; i < (int)g_folderStack.size(); ++i) {
        if (i > 0) fTitle += " > ";
        fTitle += g_folderStack[i].displayName;
    }
    if (fTitle.empty()) fTitle = "Folder";
    fDraw->AddText(ImVec2(fbarMin.x + 10.0f, fbarMin.y + 8.0f), IM_COL32(200, 210, 220, 230), fTitle.c_str());

    // Drag area
    ImGui::SetCursorScreenPos(fbarMin);
    ImGui::PushID("FolderWin");
    ImGui::InvisibleButton("FWDrag", ImVec2(fdragW, fbarH));
    if (ImGui::IsItemActivated()) {
        g_isDraggingFolderWin = true;
        g_folderDragOff = ImVec2(ImGui::GetIO().MousePos.x - fbarMin.x, ImGui::GetIO().MousePos.y - fbarMin.y);
    }
    if (g_isDraggingFolderWin && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float nx = ImGui::GetIO().MousePos.x - g_folderDragOff.x;
        float ny = ImGui::GetIO().MousePos.y - g_folderDragOff.y;
        float ww = (float)(wr.right - wr.left), wh = (float)(wr.bottom - wr.top);
        wr.left = (LONG)nx; wr.top = (LONG)ny; wr.right = (LONG)(nx + ww); wr.bottom = (LONG)(ny + wh);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) g_isDraggingFolderWin = false;

    // Chrome buttons
    float fbtnY = fbarMin.y + (fbarH - fbtnS) * 0.5f;
    float fcx = fbarMax.x - fbtnP - fbtnS;
    // Close
    ImGui::SetCursorScreenPos(ImVec2(fcx, fbtnY));
    ImGui::InvisibleButton("FWClose", ImVec2(fbtnS, fbtnS));
    fDraw->AddCircleFilled(ImVec2(fcx + fbtnS * 0.5f, fbtnY + fbtnS * 0.5f), fbtnS * 0.48f,
                           ImGui::IsItemHovered() ? IM_COL32(255, 95, 95, 230) : IM_COL32(255, 120, 120, 200));
    if (ImGui::IsItemClicked()) ExitFolderToRoot(camera);
    // Maximize
    fcx -= fbtnG + fbtnS;
    ImGui::SetCursorScreenPos(ImVec2(fcx, fbtnY));
    ImGui::InvisibleButton("FWMax", ImVec2(fbtnS, fbtnS));
    fDraw->AddRectFilled(ImVec2(fcx + 2.0f, fbtnY + 2.0f), ImVec2(fcx + fbtnS - 2.0f, fbtnY + fbtnS - 2.0f),
                         ImGui::IsItemHovered() ? IM_COL32(255, 210, 90, 230) : IM_COL32(255, 225, 140, 200), 3.0f);
    if (ImGui::IsItemClicked()) MakeFolderMaximized(camera);
    // Minimize
    fcx -= fbtnG + fbtnS;
    ImGui::SetCursorScreenPos(ImVec2(fcx, fbtnY));
    ImGui::InvisibleButton("FWMin", ImVec2(fbtnS, fbtnS));
    fDraw->AddRectFilled(ImVec2(fcx + 3.0f, fbtnY + fbtnS * 0.5f), ImVec2(fcx + fbtnS - 3.0f, fbtnY + fbtnS * 0.5f + 2.0f),
                         ImGui::IsItemHovered() ? IM_COL32(110, 220, 140, 230) : IM_COL32(140, 235, 165, 200), 1.5f);
    // Border
    fDraw->AddRect(ImVec2((float)wr.left, (float)wr.top), ImVec2((float)wr.right, (float)wr.bottom),
                   IM_COL32(60, 70, 100, 180), 4.0f, 0, 1.5f);
    ImGui::PopID();
}

