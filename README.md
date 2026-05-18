# CrossDim

**Work in Progress (WIP)**

CrossDim is a lightweight 3D engine/framework developed in C++, utilizing **DirectX 11** for rendering and **Dear ImGui** for the user interface. It features separate modes for 3D exploration and 2D workbench interfaces.

## Current Features
- **DirectX 11 Rendering Backend**: Core initialization and render pipeline.
- **Engine Components**: FPS-style Camera, Model Loading (OBJ), Skybox, and basic primitives.
- **Bloom Post-Processing**: Real-time bloom iterations.
- **ImGui Integration**: Supports UI overlay rendering alongside the 3D pipeline natively.
- **State Management**: Switching between 3D Exploration mode and 2D Workbench mode.

## Building
Currently, the project is configured for MSVC (`cl.exe`). A build task is provided out-of-the-box for Visual Studio Code.
You must have Visual Studio Build Tools installed with C++ development support.

1. Open this folder in VS Code.
2. Press `Ctrl + Shift + B` (Build Task) to compile the engine via the pre-configured `tasks.json`.
3. The executable will be generated in `/build/CrossDim.exe`.

## License and Attribution
- **Source Code**: *Specify your desired open source license here (e.g., MIT, GPL) once public.*
- **Dear ImGui**: Included via `vendor/imgui/`. It is licensed under the [MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE.txt) and is free for commercial use.
- **Assets Disclaimer**: 
  Some textures and 3D models located in the `assets/` directory are generated via online AI image-to-3D platforms. Please refer to the specific website's Terms of Service regarding commercial usage. For production or public forks, please replace these with your own assets or ensure you hold the proper commercial generation rights from the AI tools used.

*Note: This repository is currently in its early stages of development.*
