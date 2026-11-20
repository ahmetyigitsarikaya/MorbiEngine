# Dependency & Licensing Audit — Morbi Engine

> **Audit Date:** 2026-11-20

---

## 1. Project Licensing Assessment (Maintainer Action Required)

- **Root `LICENSE` File:** **Not present.** No license was arbitrarily chosen during this mission.
- **Implication:** Until the repository owner selects and commits a root `LICENSE` (e.g., MIT or Apache-2.0), all rights to original Morbi Engine source code remain reserved by default.
- **License Compatibility Note:** All active native, managed, and frontend dependencies use permissive licenses (`MIT`, `Apache-2.0`, `BSD-3-Clause`, `ISC`, `zlib`, `NCSA`). However:
  - Redistributing the optional portable `.NET` runtime (`ThirdParty/Tools/dotnet/`) is governed by **Microsoft Software License Terms (.NET Library)**, which is incompatible with copyleft ("Excluded Licenses" such as GPL).
  - `legacy/FlueperEngine/` contains GPL-licensed reference source (`Blender`) and NVIDIA Omniverse/PhysX binaries; it is excluded via `.gitignore` and must never be published in the Morbi Engine repository.

---

## 2. Verified Dependency Inventory

| Dependency | Version | Delivery Mechanism | License | Attribution Required? |
|---|---|---|---|---|
| **Jolt Physics** | `v5.2.0` | Vendored `ThirdParty/JoltPhysics` + pinned SHA-256 `FetchContent` | MIT | Yes (included in `THIRD_PARTY_NOTICES.md`) |
| **Khronos SPIRV-Reflect** | `vulkan-sdk-1.4.363.0` | Vendored `ThirdParty/SPIRV-Reflect` | Apache-2.0 | Yes (included) |
| **ENet** | `v1.3.18` | Pinned SHA-256 `FetchContent` | MIT | Yes (included) |
| **libsodium** | `1.0.20` (`9b2848df`) | Pinned Git `FetchContent` | ISC | Yes (included) |
| **nlohmann/json** | `3.11.3` / `3.12.0` | Local or `FetchContent` | MIT | Yes (included) |
| **SDL3** | `3.2.0` | Local, `find_package`, or `FetchContent` | zlib | Source notice (included) |
| **Vulkan Headers** | `1.4.356` / SDK | Local or `find_package(Vulkan)` | Apache-2.0 OR MIT | Yes (included) |
| **DXC & `spirv-val`** | Runtime binary | `VULKAN_SDK` / `PATH` / `-DMORBI_DXC_DIR` | NCSA / Apache-2.0 | Yes (included) |
| **SoLoud** | `202002` | Optional local source | zlib/libpng | Source notice (included) |
| **Assimp** (+ zlib) | `6.0.5` | Optional local static lib | BSD-3-Clause + zlib | Yes — binary notice required (included) |
| **WebView2Loader** | `1.0.4022.49` | Optional runtime DLL | BSD-3-Clause | Yes — binary notice required (included) |
| **Roslyn (`Microsoft.CodeAnalysis.CSharp`)** | `5.12.0` | NuGet (`Morbi.Engine.csproj`) | MIT | Yes (included) |
| **React / React-DOM** | `18.3.1` | npm (bundled into `MorbiEditor.exe` via `vite-plugin-singlefile`) | MIT | Yes (included) |

## 3. Actions Completed

- Expanded [`docs/THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) from 3 entries to all 14 verified dependency groups.
- Created [`docs/development/DEPENDENCIES.md`](../development/DEPENDENCIES.md) summarizing delivery mechanisms and licensing.
- Excluded `/ThirdParty/Tools/` (large portable CMake/Zig/.NET binaries) and `/legacy/` from Git via `.gitignore`.
- Documented third-party sample asset license review (`Samples/Sandbox/Assets/` FBX/Mixamo models) as a follow-up item in `ADDITIONAL_WORK_PACKAGES.md`.
