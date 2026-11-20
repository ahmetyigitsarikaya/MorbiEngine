# MorbiEngine — Engineering Backlog

Centralized tracking of deferred tasks, discovered defects, technical debt, compatibility gaps, and future improvements outside the active work package.

---

## 1. Deferred Phase 1 RHI Tasks

| ID | Description | Reason | Affected Modules | Priority | Dependencies | Target Phase / WP | Status |
|---|---|---|---|---|---|---|---|
| `BL-RHI-001` | Persistent & in-memory PSO Cache (`VkPipelineCache`, `ID3D12PipelineLibrary1`, single-flight creation, deduplication) | Explicitly left outside Phase 1 scope (`roadmap_rgm.md` Phase 1 WP3) | `Morbi.RHI`, `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12` | High | Phase 2 WP4, WP5 | Phase 2 WP6 | DEFERRED |
| `BL-RHI-002` | Configurable Vertex Input Layout API (replace hardcoded 60B/92B stride checks in Vulkan & DX12) | Explicitly left outside Phase 1 scope (`roadmap_rgm.md` Phase 1 WP4) | `Morbi.RHI`, `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12` | High | Phase 2 WP3 | Phase 2 WP4 | DEFERRED |
| `BL-RHI-003` | Async GPU Upload queue & transfer synchronization | Explicitly left outside Phase 1 scope (`roadmap_rgm.md` Phase 1 WP5) | `Morbi.RHI`, `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12`, `Morbi.Asset` | Medium | Phase 1 RHI | Post-Phase 2 RHI Backlog | DEFERRED |

| `BL-RHI-004` | Dynamic non-bindless descriptor set pool allocation & `ICommandList::BindDescriptorSet` API for standalone sets beyond push constants & static samplers | Left out of WP3 scope cleanly via extension points in `PipelineLayout` | `Morbi.RHI`, `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12` | Medium | Phase 2 WP3 | Phase 2 WP4/WP5 or Phase 3 | PROPOSED |

---

## 2. Shader & Pipeline Infrastructure Gaps

| ID | Description | Reason | Affected Modules | Priority | Dependencies | Target Phase / WP | Status |
|---|---|---|---|---|---|---|---|
| `BL-SHD-001` | Cross-platform `dxcapi.h` & Linux `libdxcompiler.so` discovery/loading implementation | Implemented in `DxcRuntimeBridge.hpp`, `ShaderCompiler.cpp`, `Shader/CMakeLists.txt` | `Morbi.Shader`, `cmake/` | High | None | Phase 2 WP1 | RESOLVED (Windows verified; Linux verification tracked in `BL-SHD-007`) |
| `BL-SHD-002` | Package `spirv-val.exe` into build `Tools/dxc/` | Added to `Editor/CMakeLists.txt` and `Tests/ShaderTests/CMakeLists.txt` | `Editor/CMakeLists.txt`, `Morbi.Shader` | High | None | Phase 2 WP1 | RESOLVED |
| `BL-SHD-003` | Dedicated `Tests/ShaderTests/` CTest target | Added `MorbiShaderTests` (181/181 assertions) | `Tests/`, `CMakeLists.txt` | High | None | Phase 2 WP1 | RESOLVED |
| `BL-SHD-004` | Replace `std::regex` `ReflectSourceAndBinary` stub & hardcoded `PushConstantSizeBytes = 64` with genuine SPIR-V & DXIL binary reflection | Replaced in `ShaderCompiler::ReflectBytecode` (`SpirvReflector` + `DxilReflector`), `ShaderReflectionValidator`, and `ShaderCache` v3 | `Morbi.Shader` | High | Phase 2 WP1 | Phase 2 WP2 | RESOLVED |
| `BL-SHD-005` | Align `MeshPushConstants` & per-object skinning buffer (`b1, space0`) divergence between Vulkan (`binding 3` array) and DX12 (direct Root CBV) | Documented and modeled explicitly via `CrossBackendBindingPolicy::MorbiBindless()` + `PipelineLayoutMode::GlobalBindlessHybrid` & `PipelineLayoutBuilder` across Vulkan & DX12 | `Morbi.RHI`, `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12`, `Morbi.Shader` | High | Phase 2 WP2 | Phase 2 WP3 | RESOLVED |
| `BL-SHD-006` | Extract inline HLSL shader literals (`kUnifiedHlslShader`, `kScenePbrMeshHlslShader`, `kGameUIProceduralHlslShader`, `kGameUIOverlayHlslShader`) out of `EngineRuntime.cpp` into standalone `.hlsl` assets | Inline C++ strings block include-graph tracking and hot-reload | `Morbi.Runtime`, `Samples/Sandbox/Assets` | Medium | Phase 2 WP1, WP7 | Phase 2 WP7 / Phase 3 | OPEN |
| `BL-SHD-007` | Build and execute `Morbi.Shader` + `MorbiShaderTests` with `libdxcompiler.so` and `spirv-val` on Linux | Implemented cross-platform loader/bridge in WP1, but unverified on Windows host | `Morbi.Shader`, `Tests/ShaderTests` | High | Linux Toolchain | Cross-Platform Verification | OPEN |

---

## 3. IO, Physics, Render, Build & Environment Backlog

| ID | Description | Reason | Affected Modules | Priority | Dependencies | Target Phase / WP | Status |
|---|---|---|---|---|---|---|---|
| `BL-IO-001` | Implement Linux `inotify` backend for `IO::FileWatcher` | `FileWatcher.cpp` currently returns `false` on non-`_WIN32` platforms | `Morbi.IO` | Medium | None | Phase 2 WP7 (or prerequisite) | OPEN |
| `BL-PHY-001` | Investigate Debug-only crash in `MorbiCSharpPhysicsTests.exe` (`SEHException` in `Morbi.Engine.NativeBridge.SyncRigidBody` / `JointAndCompoundAuditDriver.OnStart()`) | Discovered during Debug CTest verification (`Logs/Crashes/Crash_Latest.log`); Release `MorbiCSharpPhysicsTests` passes | `Morbi.Physics`, `Morbi.Script`, `Managed/Morbi.Engine` | High | None | Physics / Script Maintenance | OPEN |
| `BL-RND-001` | Fix `RenderGraph::CreateTransientBuffer` GPU buffer leak & implement `m_transientBufferPool` | `RenderGraph.cpp:290-297` allocates buffers every frame without pooling or `DestroyBuffer` | `Morbi.Render` | Critical | Phase 1 RHI | Phase 3 (or targeted hotfix) | OPEN |
| `BL-RND-002` | Enable real `RenderGraph` placed-resource memory aliasing, DAG topological sort, and dead-pass culling | `RenderGraph.cpp:241` sets `AliasingPoolOffset = ~0ULL` | `Morbi.Render`, `Morbi.RHI.*` | Medium | Phase 1 RHI | Phase 3 | OPEN |
| `BL-BLD-001` | Configure and verify `windows-debug` preset (`build-debug/`) | Configured and built `build-debug/`; Debug Shader, RHI, Network, Physics, and Editor smoke tests verified | Build / CI | High | None | Phase 2 WP1 Verification | RESOLVED (except pre-existing `BL-PHY-001`) |
| `BL-ENV-001` | Install Vulkan SDK / `VK_LAYER_KHRONOS_validation` on dev machine for full Vulkan validation coverage | Currently `VK_LAYER_KHRONOS_validation` is missing on host (`MorbiRHIDescriptorTests` logs warning) | Dev Environment | Medium | External SDK | Ongoing | OPEN |
