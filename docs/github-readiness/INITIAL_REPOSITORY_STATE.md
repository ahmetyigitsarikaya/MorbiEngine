# Initial Repository State — Morbi Engine

> **Recorded:** 2026-11-20 (prior to any cleanup, CMake updates, or documentation modifications)  
> **Workspace:** `MorbiEngine` (Windows x64)

## 1. Git Status Summary

- **Branch:** `master` (`## No commits yet on master`)
- **Commit History:** 0 commits
- **Remotes:** None configured
- **Staged Changes:** None
- **Unstaged Modifications:** None (all workspace entries are untracked)
- **Root Ignore / Config Files:** Missing `.gitignore`, `.gitattributes`, `.editorconfig`, `README.md`, and `LICENSE`.

## 2. Initial Top-Level Inventory

| Entry | Type | File Count | Size (MB) | Initial Role / Observation |
|---|---:|---:|---:|---|
| `.agents/` | Directory | 388 | 2.03 | Agent rules, skills, and `ecc-install-state.json` (contains 776 local developer paths) |
| `AGENTS.md` | File | 1 | 0.01 | Project agent guidelines (contains 5 local `file:///c:/Users/...` links and outdated `.NET 8` / `Agility SDK` claims) |
| `CMakeLists.txt` | File | 1 | <0.01 | Root CMake configuration (16 engine modules, Managed build, Editor, Tests) |
| `CMakePresets.json` | File | 1 | <0.01 | `windows-release` (`build/`) and `windows-debug` (`build-debug/`) presets |
| `cmake/` | Directory | 3 | 0.02 | `MorbiCompilerOptions.cmake`, `MorbiDependencies.cmake`, `SyncDir.ps1` |
| `Engine/` | Directory | 158 | 2.42 | Core C++20 engine source across 16 modules (`Engine/Source/*`) |
| `Managed/` | Directory | 58 | 1.17 | `.NET 10` (`net10.0`) C# scripting bridge (`Morbi.Engine`) + `bin/`/`obj/` + 4 excluded `.cs` files |
| `Editor/` | Directory | 3,729 | 78.42 | Thin native host (`MorbiEditor`) + React 18/TS/Vite frontend (`node_modules/` and `dist/` present) |
| `Samples/` | Directory | 65 | 178.75 | `Sandbox` default project + `PhysicsShowcase` scripts; contains stray `MediaCreationTool_22H2.exe` (19.5 MB) |
| `Tests/` | Directory | 14 | 0.60 | Existing CTest suites (`PhysicsTests`, `NetworkTests`, `RHITests`, `ShaderTests`) |
| `ThirdParty/` | Directory | 24,468 | 466.32 | Vendored `JoltPhysics` v5.2.0, `SPIRV-Reflect`, and portable `Tools/` (`cmake`, `dotnet`, `zig`) |
| `docs/` | Directory | 42 | 1.05 | `docs/road/` (26 files), technical guides, plus 7 raw `.txt` terminal logs & old checkpoint note |
| `build/` | Directory | 29,736 | 5,657.52 | Generated Release CMake build tree |
| `build-debug/` | Directory | 26,497 | 1,994.05 | Generated Debug CMake build tree |
| `Logs/` | Directory | 3 | 8.94 | Runtime crash minidumps (`Crash_*.dmp`) and `Crash_Latest.log` |
| `cache/` | Directory | 2 | 0.01 | Runtime-generated `server_scripts/ServerGameScripts.dll` & `.pdb` |
| `scratch/` | Directory | 20 | 8.40 | Ad-hoc Python UI inspection scripts, PNG screenshots, `__pycache__`, and `zig_launcher_test.pdb` |
| `MorbiEditor.exe` | File | 1 | 1.53 | Post-build copy polluted into repository root |
| `MorbiEditor.pdb` | File | 1 | 5.34 | Post-build debug symbols polluted into repository root |
| `legacy/` | Directory | 55,991 | 8,593.76 | Legacy `FlueperEngine` tree; still referenced by `cmake/MorbiDependencies.cmake` & `Editor/CMakeLists.txt` for 7 third-party libs |
| `hub_and_web_syss/` | Directory | 25,916 | 9,980.62 | Separate web/launcher ecosystem (`hub` Tauri/Rust, `api` Go, `admin-ui` React); contains live `api/.env` credentials |
| `testgameassets/` | Directory | 692 | 6,081.13 | Unreferenced raw 3D asset packs (`.rar`, `.zip`, FBX, textures) |
| `NYCMultiplayerTest/` | Directory | 393 | 1,088.31 | Separate unreferenced MorbiEngine sample game project |
| `ECC/` | Directory | 24,699 | 217.59 | Cloned Everything-Claude-Code repository (nested `.git`) |

## 3. Important User-Created & Active Assets Identified

- **Engine Source & Managed Bridge:** `Engine/Source/` (16 modules), `Managed/Morbi.Engine/`, `Editor/src/`, `Editor/Frontend/src/`.
- **Active Sample Project:** `Samples/Sandbox/` (`game.morbiproj`, scenes, C# scripts, models, animations, textures) and `Samples/PhysicsShowcase/`.
- **Engineering Governance & Docs:** `docs/road/` (ADRs, roadmap, status, proposals, phase reports), `docs/PHYSICS_GUIDE.md`, `docs/THIRD_PARTY_NOTICES.md`, `docs/SHADER_RENDER_ANIMATION_AUDIT.md`, `docs/MORBIENGINE_OVERVIEW_AND_WORKFLOWS.md`, `docs/MORBIENGINE_DETAILED_FEATURES.md`.
- **Vendored Dependencies:** `ThirdParty/JoltPhysics`, `ThirdParty/SPIRV-Reflect`, `ThirdParty/Tools/`.
- **External/Separate Projects on Disk (Preserve Locally, Exclude from Git):** `legacy/`, `hub_and_web_syss/`, `NYCMultiplayerTest/`, `testgameassets/`, `ECC/`.

## 4. Initial Build Process Observed

1. **Editor Frontend:** `cd Editor/Frontend && npm run build` (bundles React 18 + TS + Tailwind into single-file `dist/index.html`, embedded into `MorbiEditor.exe` via `Editor/src/EmbeddedFrontend.rc`).
2. **Managed C# Runtime:** Invoked automatically by CMake target `MorbiManagedBuild` (`dotnet build Managed/Morbi.Engine/Morbi.Engine.csproj --configuration Release`).
3. **Native C++20 Engine & Editor:** `cmake --preset windows-release` / `cmake --build --preset release` (MSVC x64, C++20, `/W4`).
