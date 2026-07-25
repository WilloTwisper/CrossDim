# CrossDim 🌌
**The World Outside Windows.**

![Language](https://img.shields.io/badge/Language-C++17-blue.svg)
![Graphics](https://img.shields.io/badge/Graphics-DirectX%2011-lightgreen.svg)
![UI](https://img.shields.io/badge/UI-Dear%20ImGui-red.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6.svg)

[English](#english) | [中文说明](#中文说明)

---

<h2 id="english">🌐 English</h2>

> *CrossDim is a next-generation Spatial OS Shell built from scratch. Currently a Work in Progress (WIP).*

### 📖 Philosophy: "Old Face, New Experience"
CrossDim is not just a 3D wallpaper or a fancy launcher. It is an ambitious **Native C++ Spatial Desktop Environment** designed to break the boundaries of the traditional 2D desktop. 

We pay homage to classic Windows designs but elevate them to a 3D spatial computing environment. We believe in **"Old face, new experience"**—classic interactions (like dragging a selection box, double-clicking, or taskbar management) are elegantly translated into 3D holographic mechanisms without sacrificing productivity or user intuition.

### ✨ Core Features
- **DirectX 11 Native Rendering Engine**: Built from scratch without bloated game engines (like Unity/Unreal). Ensures zero-overhead Win32 hooking, minimal RAM footprint, and ultimate OS-level control.
- **Window Hijacking & Cloaking**: CrossDim intercepts native Windows applications (e.g., Task Manager, Explorer), strips away their legacy borders using DWM APIs, applies layered acrylic translucency, and embeds them into your 3D workspace.
- **Custom ImGui Taskbar & System Tray**: 
  - Completely hides the native Windows taskbar.
  - Dynamically enumerates running background apps and extracts their native `HICON`s to DX11 textures.
  - Custom-built system tray pulling real-time OS states via COM interfaces: Master Volume, Wi-Fi/Ethernet status, IME (Input Method), and Battery.
- **Holographic Spatial Interactions**: 
  - FPS-style Raycast targeting and app dragging.
  - A unique mathematically-driven **3D Volumetric Curved Selection Box** (using Inverse View-Projection Matrix) that wraps perfectly around spherically arranged icons with zero distortion.
- **Retina-Grade Floating Labels**: 3D coordinates are dynamically projected back to 2D screen space to render razor-sharp, distortion-free text labels.
- **High-Performance Asset Pipeline**: Includes a custom C-style multi-threaded `.obj/.mtl` loader capable of asynchronously loading 1M+ polygon models instantly. Features custom Half-Lambert shading, Fresnel Rim Lighting, and Pivot-based model transformations.

### 🚀 Roadmap
- [ ] **Physics-Based Drag & Drop**: Spring-lerp physics for moving individual or bulk-selected 3D application nodes.
- [ ] **Holographic Context Menus**: 3D projected ImGui right-click menus for applications.
- [ ] **Real-time App Streaming (DXGI)**: Capturing active native window contents and streaming them onto 3D planes within the environment (True Spatial Computing).
- [ ] **Spatial Folders**: Tesseract-like 3D folder navigation arrays.

### 🛠️ Building & Compilation
The project is heavily optimized for **MSVC (cl.exe)** with extreme optimization flags (`/O2`). A build task is provided out-of-the-box for Visual Studio Code. You must have **Visual Studio Build Tools** installed with C++ desktop development support.

1. Open this repository folder in VS Code.
2. Press `Ctrl + Shift + B` (Run Build Task) to compile the engine via the pre-configured `tasks.json`.
3. The executable will be generated in `\build\CrossDim.exe`.
4. *(Required)* Run the executable as **Administrator** to successfully hijack high-privilege windows (like Task Manager).

---

<h2 id="中文说明">🇨🇳 中文说明</h2>

> *CrossDim 是一个从零构建的次世代空间操作系统外壳（Spatial OS Shell）。目前处于开发阶段 (WIP)。*

### 📖 核心理念："老面孔，新体验"
CrossDim 绝不仅仅是一款 3D 游戏或动态壁纸，而是一个极具野心的 **原生 C++ 空间桌面环境**，旨在打破传统 2D 桌面的次元壁。

我们致敬经典的 Windows 设计，并将其升维至 3D 空间计算环境。我们坚信 **“老面孔，新体验”**——传统的经典交互（如拉框多选、双击打开、底部任务栏管理）必须以最符合人类直觉的方式，平滑迁移为 3D 全息机制，绝不为了炫技而牺牲生产力与操作效率。

### ✨ 核心特性
- **原生 DX11 渲染架构**: 拒绝臃肿的商业游戏引擎（如 Unity/Godot），纯血 C++ 构建。实现了极低开销的系统级 Hook、极小的内存占用与 100% 的操作系统控制权。
- **原生窗口劫持与重塑**: 异步捕获 Windows 原生程序（如任务管理器、文件管理器），利用 DWM API 强制剥离其原生边框，注入亚克力半透明材质，并将其无缝嵌入 3D 空间。
- **全局接管的空间任务栏**: 
  - 彻底隐藏 Windows 原生任务栏。
  - 动态扫描后台运行程序，提取底层 `HICON` 并转换为 DX11 材质。
  - 徒手接入系统底层 COM 接口，实时重绘系统托盘：主音量、Wi-Fi/以太网状态、中英输入法状态 (IME) 及电池电量。
- **全息空间交互体系**: 
  - 第一人称准星射线检测 (Raycast) 与拖拽。
  - 独创 **3D 全息曲面划选框**：基于逆视图矩阵与世界坐标系推导，拉出一个带有物理厚度和折射光感的曲面玻璃体，完美无畸变地框选环绕阵列。
- **视网膜级全息浮动标签**: 提取 3D 空间坐标，降维投影至 2D 屏幕空间，利用 ImGui 渲染无透视畸变、极度清晰的中文字体。
- **高性能 3D 资产管线**: 手写极速异步 `.obj/.mtl` 解析器（纯 C-style 读取），瞬间加载百万面高模。内置基于真实几何中心的 Pivot 旋转体系、Half-Lambert 柔和包裹光照及菲涅尔边缘泛光 (Fresnel Rim Light)。

### 🚀 未来路线图
- [ ] **物理阻尼拖拽系统**: 引入弹簧插值算法 (Spring Physics Lerp)，实现单体或批量选中 3D 图标时的物理平滑跟随与碰撞。
- [ ] **全息右键菜单**: 在 3D 空间中目标旁弹出 ImGui 渲染的 3D 全息投影操作面板。
- [ ] **DXGI 实时画面映射**: 获取原生 2D 窗口的实时画面流，并作为纹理贴在 3D 空间的 Plane 上，实现真正的空间计算 (Spatial Computing)。
- [ ] **空间文件夹**: 类似超立方体 (Tesseract) 的 3D 子阵列空间导航。

### 🛠️ 编译指南
本项目针对 **MSVC (cl.exe)** 进行了极致的性能优化配置（开启 `/O2` 涡轮加速）。已内置 Visual Studio Code 的一键编译任务。请确保已安装带有 C++ 桌面开发支持的 **Visual Studio Build Tools**。

1. 在 VS Code 中打开本项目文件夹。
2. 按下 `Ctrl + Shift + B`（运行生成任务），通过预配置的 `tasks.json` 编译引擎。
3. 编译后的可执行文件将生成于 `\build\CrossDim.exe`。
4. *(注意)* 请务必 **以管理员身份运行** 该程序，以突破系统 UIPI 隔离，成功劫持并扒皮任务管理器等高权限窗口。

---

### 📄 License & Attribution / 协议与声明
- **Source Code**: [此处可填写你选择的开源协议，如 MIT License]
- **Dear ImGui**: UI 系统基于 `vendor/imgui/` 构建。遵循 MIT 协议，可免费用于商业用途。
- **免责声明**: `assets/` 目录中的部分 3D 模型与 PBR 贴图仅供测试使用。在分发或投入生产环境时，请替换为您自己的资产，或确保您拥有 AI 生成工具对应的商业使用授权。