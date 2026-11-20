# Repository Audit — Morbi Engine

> **Audit Scope:** Core C++20 Architecture, Graphics & Rendering (RHI / Vulkan / DX12 / RenderGraph / Shader), Animation, Physics, Audio, Asset Pipeline, Networking, `.NET 10` Scripting, Editor & Runtime UI, and CMake Build System.

---

## 1. Core Architecture & Subsystems

- **16 Native C++20 Modules (`Engine/Source/*`):** Clear separation between headless simulation (`Morbi.Simulation` → `MorbiServer.exe`) and full interactive runtime (`Morbi.Runtime` → `MorbiEditor.exe` / `LauncherTemplate`).
- **ECS (`Morbi.Scene`):** Paged Sparse-Set SoA arenas (`kPageSize = 1024`) with 9 size-verified POD components (`static_assert`) and zero-copy `Span<T>` interop into `.NET 10`.
- **Networking (`Morbi.Network`):** Production-grade architecture using ENet v1.3.18 + libsodium 1.0.20 (X25519, Ed25519, XChaCha20-Poly1305 AEAD, replay protection, rate limiting, delta/keyframe replication, interest management, RPC router).
- **Scripting (`Morbi.Script` & `Managed/Morbi.Engine`):** `.NET 10` (`net10.0`) CoreCLR `hostfxr` host with embedded Roslyn (`Microsoft.CodeAnalysis.CSharp` `5.12.0`) compilation and `CollectibleScriptLoadContext` hot-reload.
- **Key Architectural Debt Identified:**
  1. `Engine/Source/Runtime/src/EngineRuntime.cpp` (5,298 LOC) is monolithic (contains inline HLSL shaders, smoke-test harness, rendering passes, editor command routing, and PIE logic).
  2. `SimulationSession.cpp` and `SceneReplicationBridge.cpp` are compiled twice (once into `Morbi.Simulation` static lib and again into `Morbi.Runtime` shared lib).
  3. `ScriptEngine.cpp` stores active pointers in a file-static `g_runtimeCtx`.
  4. `WebView2Compositor.cpp` creates DirectComposition visual objects that are never bound to an `IDCompositionTarget`; actual viewport compositing uses Win32 `SetWindowRgn` hole-punching and JSON WebMessages.

---

## 2. Graphics & Rendering Audit

- **Shader Infrastructure (`Morbi.Shader` — Implemented):** Most mature graphics subsystem. In-process DXC dual-target (DXIL + SPIR-V), V3 `.mshc` CRC32 disk cache with transitive `#include` hashing, `SPIRV-Reflect` + `IDxcContainerReflection`, cross-backend validator, and `PipelineLayoutBuilder`.
- **GPU Resource Lifetime (Implemented):** Strong deferred destruction model in both Vulkan and DX12 using `GenerationalPool`, `UnsubmittedRefCount`, and monotonic timeline semaphore / `ID3D12Fence` tracking (2 frames in flight).
- **Vulkan 1.3 (`Morbi.RHI.Vulkan` — Implemented):** Dynamic `vulkan-1.dll` dispatch, Vulkan 1.3 Dynamic Rendering, custom 32 MB slab sub-allocator, bindless Set 0 (`4096` buffers + `4096` textures).
  - **Bug Fixed During Mission:** `VulkanDevice::BeginFrame()` previously reset `InFlightFence` before `AcquireNextImageKHR`, which could deadlock if `VK_ERROR_OUT_OF_DATE_KHR` returned early. Fixed by moving `ResetFences` after `AcquireNextImageKHR` succeeds.
  - **Remaining Bugs/Gaps:** 4 BPP assumption in `CreateTexture` initial data upload; host-visible allocation for `GpuOnly` buffers with initial data; recording-time bindless Set 0 mutation in `BindConstantBuffer`.
- **DirectX 12 (`Morbi.RHI.DX12` — Implemented, Partial Parity):** Standard Windows SDK D3D12 (no Agility SDK), strong DRED crash forensics, bindless SRV heap, custom Root Signatures. Behind Vulkan on memory sub-allocation (`CreateCommittedResource` per resource) and debug markers (empty no-ops).
- **Render Graph (`Morbi.Render` — In Progress):** Linear pass execution, sequential barriers, name-based transient texture pooling. No DAG/topological sort/dead-pass culling; memory aliasing is stats-only (`AliasingPoolOffset` overwritten with `~0ULL`); transient buffers are unpooled.
- **Scene Rendering (`EngineRuntime.cpp` — Experimental):** Forward static + 100-bone skinned mesh rendering and wireframe physics debug work; lighting is hardcoded Blinn-Phong (`LightComponent` unused); `GBufferPass`, `LightingReadPass`, and `PostProcessWritePass` are dummy clears/no-ops; Transform Gizmo is a 2D screen-space SDF overlay.

---

## 3. Animation, Physics, Audio & Asset Audit

- **Animation (`Morbi.Asset` — Implemented):** Forward kinematics, O(1) channels, zero-alloc CrossFade, sockets, Mixamo-to-CC3 retargeting, 100-bone GPU skinning, C# `Animator` API. No state machines/blend trees/IK; skeletons/clips not cooked to `.mAsset`.
- **Physics (`Morbi.Physics` — Implemented):** Jolt Physics v5.2.0 (`Box`/`Sphere`/`Capsule`/`StaticCompoundShape`, queries, 4 joint types, `JPH::CharacterVirtual`, C# bindings). No convex-hull or triangle-mesh colliders yet.
- **Audio (`Morbi.Audio` — Implemented):** SoLoud (`MINIAUDIO` + `NULL`), 2D/3D spatial playback, procedural WAV. No streaming or mixing buses.
- **Asset Pipeline (`Morbi.Asset` / `Morbi.IO` — In Progress):** Assimp v6.0.5 + WIC/TGA importers, `.mAsset` v2 static mesh/texture cooking, `MorbiPak` chunked `.mpak` with SHA-256 and optional `ChaCha20Sha256`.

---

## 4. Build System & Repository Hygiene Audit

- **Fixed During Mission:** Removed duplicate MSVC flags in root `CMakeLists.txt`; guarded `RHI.DX12` with `if(WIN32)`; added `MORBI_ENABLE_AVX2`, `MORBI_BUILD_SAMPLES`, `MORBI_BUILD_FRONTEND`, `MORBI_DEPLOY_TO_SOURCE_TREE` (default `OFF`), and `MORBI_WEBVIEW2_LOADER_PATH`; replaced hardcoded `C:/Program Files/dotnet` path; added `VULKAN_SDK`/`PATH` fallback for DXC and `VK_NO_PROTOTYPES` on `find_package(Vulkan)`.
- **Remaining Build Coupling:** `SoLoud`, `Assimp`, `DXC`, and `WebView2Loader.dll` still rely on local paths or optional manual flags on clean clones (tracked in `ADDITIONAL_WORK_PACKAGES.md`).
