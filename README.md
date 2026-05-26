# CrossDim 🌌
**The World Outside Windows.**

![C++](https://img.shields.io/badge/Language-C++17-blue.svg)
![DirectX](https://img.shields.io/badge/Graphics-DirectX%2011-lightgreen.svg)
![ImGui](https://img.shields.io/badge/UI-Dear%20ImGui-red.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6.svg)

> *CrossDim is a next-generation Spatial OS Shell built from scratch. Currently a Work in Progress (WIP).*

## 📖 Philosophy: "Old Face, New Experience"
CrossDim is not just a 3D wallpaper or a fancy launcher. It is an ambitious **Native C++ Spatial Desktop Environment** designed to break the boundaries of the traditional 2D desktop. 

We pay homage to classic Windows designs but elevate them to a 3D spatial computing environment. We believe in **"Old face, new experience"**—classic interactions (like dragging a selection box or taskbar management) are elegantly translated into 3D holographic mechanisms without sacrificing productivity or user intuition.

## ✨ Core Features

- **DirectX 11 Native Rendering Engine**: Built from scratch without bloated game engines. Ensures zero-overhead Win32 hooking, minimal RAM footprint, and ultimate OS-level control.
- **Window Hijacking & Cloaking**: CrossDim intercepts native Windows applications (e.g., Task Manager, Explorer), strips away their legacy borders using DWM APIs, applies layered acrylic translucency, and embeds them into your 3D workspace.
- **Custom ImGui Taskbar & System Tray**: 
  - Completely hides the native Windows taskbar.
  - Dynamically enumerates running background apps and extracts their native `HICON`s to DX11 textures.
  - Custom-built system tray pulling real-time OS states via COM interfaces: Master Volume, Wi-Fi/Ethernet status, IME (Input Method), and Battery.
- **Holographic Spatial Interactions**: 
  - FPS-style Raycast targeting and app dragging.
  - A unique mathematically-driven **3D Volumetric Curved Selection Box** that wraps perfectly around spherically arranged icons.
- **Retina-Grade Floating Labels**: 3D coordinates are dynamically projected back to 2D screen space to render razor-sharp, distortion-free text labels using ImGui.
- **High-Performance Asset Pipeline**: Includes a custom C-style multi-threaded `.obj/.mtl` loader capable of asynchronously loading 1M+ polygon models instantly. Features custom Half-Lambert shading and Fresnel Rim Lighting.

## 🚀 Roadmap

- [ ] **Physics-Based Drag & Drop**: Spring-lerp physics for moving individual or bulk-selected 3D application nodes.
- [ ] **Cross-Dimensional Pinning**: Drag an app from the 3D space directly onto the 2D taskbar to pin it.
- [ ] **Holographic Context Menus**: 3D projected right-click menus for applications.
- [ ] **Real-time App Streaming (DXGI)**: Capturing active native window contents and streaming them onto 3D planes within the environment (True Spatial Computing).
- [ ] **Spatial Folders**: Tesseract-like 3D folder navigation arrays.

## 🛠️ Building & Compilation

The project is heavily optimized for **MSVC (cl.exe)** with extreme optimization flags (`/O2`). A build task is provided out-of-the-box for Visual Studio Code. You must have **Visual Studio Build Tools** installed with C++ desktop development support.

1. Open this repository folder in VS Code.
2. Press `Ctrl + Shift + B` (Run Build Task) to compile the engine via the pre-configured `tasks.json`.
3. The executable will be generated in `\build\CrossDim.exe`.
4. *(Required)* Run the executable as **Administrator** to successfully hijack high-privilege windows (like Task Manager).

## 📄 License and Attribution
- **Source Code**: [Specify your open-source license here, e.g., MIT]
- **Dear ImGui**: Included via `vendor/imgui/`. MIT Licensed.
- **Disclaimer**: Certain 3D models and textures in the `assets/` directory are for testing purposes. Please ensure you hold the proper commercial rights for your own assets when deploying.