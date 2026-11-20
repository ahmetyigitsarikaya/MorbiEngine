# Validation Report — Morbi Engine

> **Validated On:** 2026-11-20 (Windows x64, MSVC / Visual Studio 2022, .NET 10 SDK, Node.js / Vite 5.4)  
> **Scope Notice:** Per mission rules, zero new tests or test infrastructure were created or added. Only existing project builds, frontend builds, SVG XML checks, Markdown link checks, and Git hygiene checks were executed.

---

## 1. Commands Executed & Verified Results

| Check | Command | Result | Details |
|---|---|---|---|
| **Editor Frontend Build** | `cmd /c "cd /d Editor\Frontend && npm run build"` | **PASS** (exit 0) | `tsc -b && vite build` succeeded in 2.68s; produced `dist/index.html` (330.79 kB, gzip 82.39 kB) via `vite-plugin-singlefile` without redundant `strip-module.js`. |
| **CMake Reconfigure & Full Release Build** | `cmake --build --preset release` | **PASS** (exit 0) | Reconfigured cleanly in 3.6s picking up all CMake changes. Built `.NET 10` `Morbi.Engine.dll` (0 warnings, 0 errors), all 16 `Morbi.*` C++20 targets (including modified `VulkanDevice.cpp`), `MorbiEditor.exe`, and `MorbiServer.exe` with 0 errors and 0 MorbiEngine warnings. Confirmed `MorbiEditor.exe`/`.pdb` were **not** copied into the repository root (`MORBI_DEPLOY_TO_SOURCE_TREE=OFF`). |
| **SVG Well-Formedness Check** | PowerShell `[xml]` parse over `docs/assets/**/*.svg` | **PASS** | `morbi-engine-banner.svg` and `morbi-architecture.svg` both parse cleanly as valid XML with zero external dependencies. |
| **Personal Path Sanitization Check** | `Select-String -Path "AGENTS.md","docs\**\*.md" -Pattern "Users[/\\]<user>"` | **PASS** | 0 occurrences remaining in `AGENTS.md` and `docs/**/*.md`. |
| **Markdown Relative Link & Image Check** | PowerShell link checker over root `.md` and `docs/**/*.md` | **PASS** (re-verified after report creation) | All relative links and image `src` paths resolve cleanly on disk. |
| **Git Hygiene Check** | `git status --short --branch` | **PASS** | `build/`, `build-debug/`, `legacy/`, `hub_and_web_syss/`, `testgameassets/`, `NYCMultiplayerTest/`, `ECC/`, `ThirdParty/Tools/`, `node_modules/`, `Managed/**/obj/`, and `.agents/ecc-install-state.json` are cleanly ignored; zero commits or destructive Git operations performed. |

---

## 2. Skipped Operations & Environmental Limitations

- **Debug Preset Build (`cmake --build --preset debug`):** Not re-run after Release build succeeded to avoid unnecessary rebuild time; `CMakePresets.json` `windows-debug` preset was preserved intact.
- **Linux / macOS Builds:** Not executed (Windows x64 host only). `RHI.DX12` is now guarded by `if(WIN32)`, but Win32 APIs in `Morbi.UI`, `Morbi.IO`, `Morbi.Asset`, and `Morbi.RHI.Vulkan` still require POSIX porting before Linux/macOS builds can succeed.
- **GitHub Actions CI Execution (`.github/workflows/ci.yml`):** Workflow file created and structurally verified locally (`npm run build` and `dotnet build` both succeeded locally), but live GitHub Actions execution has not occurred because pushing to GitHub is outside mission authorization.
- **Clean-Clone Build Without `legacy/`:** Local validation ran with the existing workspace's `legacy/` directory still present on disk (excluded via `.gitignore`). Clean-clone builds without `legacy/` will disable optional `SoLoud` and `Assimp` unless provided via system/CMake options, and require `VULKAN_SDK` / `-DMORBI_DXC_DIR` for DXC and `-DMORBI_WEBVIEW2_LOADER_PATH` for WebView2Loader.dll.

---

## 3. Known Unresolved Issues

- Two benign MSVC compiler command-line warnings (`D9025: '/W3' overridden by '/w'`) originate from third-party `FetchContent` targets `enet` and `sodium` (zero warnings in MorbiEngine targets).
- Remaining graphics, rendering, architectural, and licensing follow-ups are documented transparently in [`ADDITIONAL_WORK_PACKAGES.md`](ADDITIONAL_WORK_PACKAGES.md).
