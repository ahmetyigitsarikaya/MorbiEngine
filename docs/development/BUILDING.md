# Building Morbi Engine

> **Platform Status:** Windows 10/11 x64 with **MSVC (Visual Studio 2022 / C++20)** is currently the only build-verified platform. Linux and macOS builds are **not** yet verified.

---

## 1. Prerequisites (Windows x64)

| Tool / SDK | Requirement | Purpose |
|---|---|---|
| **Visual Studio 2022** (or Build Tools) | MSVC v143+ with C++20 & Windows 10/11 SDK | Native C++20 engine, DirectX 12 (`d3d12`/`dxgi`), WIC, Win32 |
| **CMake** | `3.24+` | Configure & build presets (`CMakePresets.json`) |
| **Vulkan SDK** (or DXC binaries) | Vulkan 1.3 headers + `dxc.exe`, `dxcompiler.dll`, `dxil.dll` | Vulkan RHI headers & in-process DXC shader compilation |
| **.NET 10 SDK** | `net10.0` (with `Microsoft.NETCore.App.Host.win-x64`) | `Managed/Morbi.Engine` Roslyn compiler & `nethost` CoreCLR hosting |
| **Node.js & npm** | Node.js 18+ / 20+ | Building `Editor/Frontend` (React 18 + TypeScript + Vite) |
| **Git** | Any recent version | `FetchContent` for `libsodium` and source dependencies |

---

## 2. Step-by-Step Build Instructions

### Step 1 — Build the Editor Frontend

```powershell
cd Editor\Frontend
npm install
npm run build
cd ..\..
```

This produces `Editor/Frontend/dist/index.html` (single-file bundle), which `MorbiEditor` embeds via Win32 `RCDATA` (`Editor/src/EmbeddedFrontend.rc`).

### Step 2 — Configure with CMake Presets

```powershell
cmake --preset windows-release
```

For a Debug configuration:

```powershell
cmake --preset windows-debug
```

#### Useful CMake Options

| Option | Default | Description |
|---|---|---|
| `MORBI_ENABLE_AVX2` | `ON` | Enable `/arch:AVX2` (MSVC) / `-mavx2 -mfma` (GCC/Clang) and Jolt AVX2 |
| `MORBI_BUILD_FRONTEND` | `ON` | Invoke `npm run build` automatically during CMake builds when `node_modules` exists |
| `MORBI_BUILD_SAMPLES` | `OFF` | Include `Samples/Sandbox` (`MorbiSandbox`) in the build |
| `MORBI_DEPLOY_TO_SOURCE_TREE` | `OFF` | Copy `MorbiEditor.exe`/`.pdb` into the repository root and `Editor/` |
| `MORBI_DXC_DIR` | Auto-detected | Directory containing `dxc.exe`, `dxcompiler.dll`, `dxil.dll` (checks `VULKAN_SDK` / `PATH`) |
| `MORBI_WEBVIEW2_LOADER_PATH` | Optional | Path to `WebView2Loader.dll` for Editor WebView2 hosting |

### Step 3 — Build Engine, Managed Bridge, Server & Editor

```powershell
cmake --build --preset release
```

Outputs are placed in `build/bin/Release/` (including `MorbiEditor.exe`, `MorbiServer.exe`, `Morbi.*.dll`, and `Managed/Morbi.Engine.dll`).

### Step 4 — Running

- **Editor (default Sandbox project):**
  ```powershell
  .\build\bin\Release\MorbiEditor.exe
  ```
- **Dedicated Server (headless):**
  ```powershell
  .\build\bin\Release\MorbiServer.exe
  ```
- **Graphics Debug / Validation Flags:**
  - `--debug` enables verbose diagnostic tracing.
  - `--gpu-gbv` enables GPU-Based Validation (D3D12 GBV / Vulkan GPU-assisted + sync validation).

---

## 3. Known Clean-Clone Dependency Caveats

On a clean clone without local legacy directories:
- `JoltPhysics v5.2.0` (vendored in `ThirdParty/JoltPhysics`), `SPIRV-Reflect` (vendored in `ThirdParty/SPIRV-Reflect`), `ENet v1.3.18`, `libsodium 1.0.20`, `nlohmann_json`, and `SDL3` resolve automatically.
- **DXC** (`dxc.exe`, `dxcompiler.dll`, `dxil.dll`) is discovered via `VULKAN_SDK` / `PATH` or `-DMORBI_DXC_DIR=<path>`.
- **SoLoud** (audio) and **Assimp** (FBX/glTF model import) are optional at configure time if not provided locally (`MORBI_ENABLE_SOLOUD=0` disables audio; without Assimp, `ModelImporter` falls back to built-in `.obj`/minimal `.gltf` parsers).
- **WebView2Loader.dll** can be supplied via `-DMORBI_WEBVIEW2_LOADER_PATH=<path>` for the Editor UI.

Migrating SoLoud, Assimp, DXC, and WebView2Loader to fully self-contained CMake packages is tracked in [`docs/github-readiness/ADDITIONAL_WORK_PACKAGES.md`](../github-readiness/ADDITIONAL_WORK_PACKAGES.md).
