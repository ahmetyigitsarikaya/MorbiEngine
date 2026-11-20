# Cleanup Report — Morbi Engine

> **Executed:** 2026-11-20  
> **Safety Policy:** Every removal was verified against CMake targets, C++ `#include`s, `.csproj` globs, and frontend build scripts. No essential engine source files, sample assets, or user projects were deleted.

---

## 1. Files & Directories Removed

| Path | Category | Reason for Removal |
|---|---|---|
| `MorbiEditor.exe`, `MorbiEditor.pdb` | Generated binary | Post-build copies polluting repository root; gated behind `MORBI_DEPLOY_TO_SOURCE_TREE=OFF` |
| `Editor/MorbiEditor.exe`, `Editor/MorbiEditor.pdb` | Generated binary | Post-build copies polluting `Editor/` |
| `Logs/` | Runtime crash artifacts | Contained `Crash_*.dmp` minidumps and `Crash_Latest.log` (with local paths); recreated automatically on crash |
| `cache/` | Runtime script cache | Contained generated `ServerGameScripts.dll`/`.pdb`; recreated automatically at runtime |
| `scratch/` | Temporary dev scripts | Ad-hoc Python debug scripts, PNG screenshots, `__pycache__`, and 7.1 MB `zig_launcher_test.pdb` |
| `Samples/Sandbox/Assets/Animations/MediaCreationTool_22H2.exe` | Stray binary (19.5 MB) | Unrelated Windows installer accidentally placed in sample animations directory |
| `docs/audit_test_output.txt`, `final_test_output.txt`, `network_test_output.txt`, `smoke_test_output.txt`, `build_output.txt`, `physics_test_output.txt`, `physics_test_output2.txt` | Raw terminal logs | 700+ KB of raw console dumps containing local `C:\Users\<user>` paths |
| `docs/KALINAN_YER_VE_ONERILER.txt` | Obsolete note | Superseded by `docs/road/STATUS.md` and `docs/road/MORBI_ENGINE_ROADMAP.md` |
| `Managed/Morbi.Engine/{PlayerController,CameraController,CharacterAnimationController,SceneBootstrapper}.cs` | Dead source files | Explicitly excluded via `<Compile Remove>` in `Morbi.Engine.csproj`; active versions live in `Samples/Sandbox/Scripts/` |
| `Editor/Frontend/strip-module.js` | Duplicate build script | Duplicated the inline `strip-module-tags` Vite plugin already active in `Editor/Frontend/vite.config.ts` |

---

## 2. Personal Path Sanitization

Replaced machine-specific `file:///c:/Users/<user>/Desktop/MorbiEngine/` links with portable relative paths in:
- `AGENTS.md`
- `docs/PHYSICS_GAMEPLAY_VALIDATION.md`
- `docs/road/decisions/ADR-0003-unified-pipeline-layout.md`
- `docs/road/reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md`

---

## 3. Preserved Locally & Excluded via `.gitignore`

To protect separate user workspaces, raw staging assets, and local build caches while keeping the GitHub repository clean, the following directories were **kept intact on disk** and **excluded in `.gitignore`**:

- `/build/`, `/build-debug/` (7.6 GB generated CMake build trees)
- `/legacy/` (8.6 GB legacy `FlueperEngine` tree — still used locally until `SoLoud`/`Assimp`/`DXC`/`WebView2` are fully migrated)
- `/hub_and_web_syss/` (10.0 GB separate Tauri/Go/React ecosystem containing live `api/.env` secrets)
- `/testgameassets/` (6.1 GB raw unreferenced 3D asset packs)
- `/NYCMultiplayerTest/` (1.1 GB separate standalone game project)
- `/ECC/` (217 MB nested Git clone of Everything-Claude-Code)
- `/ThirdParty/Tools/` (large portable CMake/Zig/.NET binaries)
- `.agents/ecc-install-state.json` (776 machine-specific local paths)
- `Editor/Frontend/node_modules/`, `Editor/Frontend/dist/`, `Managed/**/bin/`, `Managed/**/obj/`

---

## 4. Safe Source & Build Improvements Applied

- **`Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp`:** Fixed `BeginFrame()` fence deadlock on `VK_ERROR_OUT_OF_DATE_KHR` by moving `ResetFences` after `AcquireNextImageKHR` succeeds.
- **`CMakeLists.txt`:** Removed duplicate global MSVC flags; guarded `RHI.DX12` with `if(WIN32)`; added `MORBI_BUILD_SAMPLES` option.
- **`cmake/MorbiCompilerOptions.cmake`:** Added `MORBI_ENABLE_AVX2` option.
- **`cmake/MorbiDependencies.cmake`:** Fixed hardcoded `C:/Program Files/dotnet` path; added `VULKAN_SDK`/`PATH` discovery for `MORBI_DXC_DIR`; ensured `find_package(Vulkan)` defines `VK_NO_PROTOTYPES`.
- **`Editor/CMakeLists.txt`:** Added `MORBI_BUILD_FRONTEND`, `MORBI_DEPLOY_TO_SOURCE_TREE` (default `OFF`), `find_program(MORBI_NPM_EXECUTABLE)`, and `MORBI_WEBVIEW2_LOADER_PATH`.
- **`Managed/Morbi.Engine/Morbi.Engine.csproj` & `Editor/Frontend/package.json`:** Removed obsolete `<Compile Remove>` entries and redundant `postbuild` script.
