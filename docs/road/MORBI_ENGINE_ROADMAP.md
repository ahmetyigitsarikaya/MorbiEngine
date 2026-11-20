# MORBI ENGINE — CANONICAL MASTER ROADMAP

**Project:** Morbi Engine  
**Architecture:** C++20 Game Engine  
**Graphics APIs:** Vulkan 1.3 + DirectX 12  
**Platforms:** Windows + Linux  
**Quality Target:** Advanced / Production-Ready  
**Animation Scope:** Full Animation System  
**Original Source Input:** [roadmap_rgm.md](roadmap_rgm.md) (preserved as historical baseline)  
**Last Synchronized:** 2026-11-18

---

## 1. Overall Progress

| Phase | Name | Status | Phase Document |
|---|---|---|---|
| Phase 0 | Architecture Audit | COMPLETE | [PHASE-00-ARCHITECTURE-AUDIT.md](phases/PHASE-00-ARCHITECTURE-AUDIT.md) |
| Phase 1 | RHI & GPU Resource Foundation | COMPLETE | [PHASE-01-RHI-FOUNDATION.md](phases/PHASE-01-RHI-FOUNDATION.md) |
| Phase 2 | Shader & Pipeline Infrastructure | IN PROGRESS | [PHASE-02-SHADER-PIPELINE.md](phases/PHASE-02-SHADER-PIPELINE.md) |
| Phase 3 | Render Architecture | NOT STARTED | — |
| Phase 4 | Modern Rendering Pipeline | NOT STARTED | — |
| Phase 5 | Animation Asset Pipeline | NOT STARTED | — |
| Phase 6 | Advanced Animation Runtime | NOT STARTED | — |
| Phase 7 | Scene / Animation / Renderer Integration | NOT STARTED | — |
| Phase 8 | Amber & Ramy Game Test | NOT STARTED | — |
| Phase 9 | Performance & Stability | NOT STARTED | — |
| Phase 10 | Networking Integration Test | NOT STARTED | — |

> **Note:** Phases 3–10 were labeled `PENDING` in [roadmap_rgm.md](roadmap_rgm.md) and are normalized here to `NOT STARTED` per the canonical status vocabulary.

---

## 2. Phase 0 — Architecture Audit

**Status: COMPLETE**  
**Phase Document:** [PHASE-00-ARCHITECTURE-AUDIT.md](phases/PHASE-00-ARCHITECTURE-AUDIT.md)  
**Reference Report:** [SHADER_RENDER_ANIMATION_AUDIT.md](../SHADER_RENDER_ANIMATION_AUDIT.md)

The existing engine architecture was audited across RHI, Vulkan backend, DirectX 12 backend, Renderer, Shader infrastructure, Animation infrastructure, Physics, Networking, Asset pipeline, and Runtime/Editor.

---

## 3. Phase 1 — RHI & GPU Resource Foundation

**Status: COMPLETE**  
**Phase Document:** [PHASE-01-RHI-FOUNDATION.md](phases/PHASE-01-RHI-FOUNDATION.md)  
**Baseline Test Record:** [PHASE-01-BASELINE-VERIFICATION-2026-11-18.md](test-results/phase-01/PHASE-01-BASELINE-VERIFICATION-2026-11-18.md)

### Work Packages

| WP | Name | Status | Notes |
|---|---|---|---|
| WP1 | GPU Resource Lifetime | COMPLETE | Verified via `MorbiRHIDescriptorTests` (860/860) & `MorbiRHIGenerationalHandleHashTests` (507/507) |
| WP2 | Bindless Resource Reclamation | COMPLETE | Verified across Vulkan 1.3 and DX12 (4096-slot bindless reclamation) |
| WP3 | PSO Cache | DEFERRED | Marked `NOT IMPLEMENTED` in `roadmap_rgm.md`; planned in Phase 2 WP6 |
| WP4 | Vertex Layout API | DEFERRED | Marked `NOT IMPLEMENTED` in `roadmap_rgm.md`; planned in Phase 2 WP4 |
| WP5 | Async Upload | DEFERRED | Marked `NOT IMPLEMENTED` in `roadmap_rgm.md`; tracked in [BACKLOG.md](BACKLOG.md) (`BL-RHI-003`) |

### Final Six-Issue Closure (Verified Intact on 2026-11-18)

1. Vulkan shutdown native resource cleanup.
2. Vulkan swapchain destruction and deferred destruction ordering.
3. DX12 `ResizeSwapchain` RTV descriptor leak.
4. `VulkanMemoryAllocator` slab offset reuse and reclamation.
5. Synchronous `CreateTexture(initialData)` upload safety.
6. Duplicate `Destroy*` queue entries.

### Known Limitation

- Vulkan validation layers (`VK_LAYER_KHRONOS_validation`) are not installed on the current development machine. DX12 Debug Layer and DRED checks passed.
- Debug build directory (`build-debug/`) is not currently present on disk; Release baseline was re-verified on 2026-11-18.

---

## 4. Phase 2 — Shader & Pipeline Infrastructure

**Status: IN PROGRESS**  
**Phase Document:** [PHASE-02-SHADER-PIPELINE.md](phases/PHASE-02-SHADER-PIPELINE.md)

**Goal:** Establish a unified, robust, and production-quality shader and pipeline infrastructure for Vulkan 1.3 and DirectX 12.

### Work Package Dependency Order & Status

| WP | Name | Status | Dependencies | Plan / Report |
|---|---|---|---|---|
| WP1 | Shader Compilation Infrastructure | COMPLETE | Phase 1 | [PHASE-02-WP01-SHADER-COMPILATION.md](reports/phase-02/PHASE-02-WP01-SHADER-COMPILATION.md) |
| WP2 | Binary Reflection | COMPLETE | WP1 | [PHASE-02-WP02-BINARY-REFLECTION.md](reports/phase-02/PHASE-02-WP02-BINARY-REFLECTION.md) |
| WP3 | Unified Pipeline Layout | COMPLETE | WP2 | [PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md](reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md) |
| WP4 | Graphics Pipeline | NOT STARTED | WP3 | — |
| WP5 | Compute Pipeline | NOT STARTED | WP3 | — |
| WP6 | Pipeline Cache | NOT STARTED | WP4, WP5 | — |
| WP7 | Shader Hot Reload | NOT STARTED | WP1, WP4, WP5, WP6 | — |
| WP8 | Cooked Shader Asset | NOT STARTED | WP1, WP2, WP3 | — |

### WP1 — Shader Compilation Infrastructure (`COMPLETE`)

Completed and verified on 2026-11-18 ([Completion Report](reports/phase-02/PHASE-02-WP01-SHADER-COMPILATION.md) | [Test Record](test-results/phase-02/PHASE-02-WP01-VERIFICATION-2026-11-18.md)):
- In-process DXC integration (`IDxcCompiler3`, `IDxcUtils`, `IDxcVersionInfo`/`2`/`3`) with thread-safe COM worker pool and `LoadLibraryExW`/`dlopen` loading (Linux build/runtime `NOT VERIFIED` on Windows host; `BL-SHD-007`).
- Custom `MorbiDxcIncludeHandler` with canonical path resolution, transitive include dependency tracking, `#ifndef` diamond support, and cycle guard (`kMaxIncludeLoadsPerFile = 32`).
- Shader Model 6.0–6.6 (`SM6_0`–`SM6_6`), DXIL + Vulkan 1.3 SPIR-V (`-fspv-target-env=vulkan1.3`), packaged `spirv-val.exe --target-env vulkan1.3` validation.
- `ShaderCache` v2 (`'MSHC'` v2, CRC32 integrity check, compiler version + full request + transitive include hashing, dependency validation on memory & disk hits, corrupt recovery, atomic disk writes).
- Dedicated `MorbiShaderTests` CTest suite (181/181 assertions in Release & Debug).

### WP2 — Binary Reflection (`COMPLETE`)

Completed and verified on 2026-11-18 ([Completion Report](reports/phase-02/PHASE-02-WP02-BINARY-REFLECTION.md) | [Test Record](test-results/phase-02/PHASE-02-WP02-VERIFICATION-2026-11-18.md) | [ADR-0002](decisions/ADR-0002-spirv-reflect-dxil-reflection-and-shader-cache-v3.md)):
- SPIR-V binary reflection (`SpirvReflector`) via vendored Khronos `SPIRV-Reflect` (`vulkan-sdk-1.4.363.0`) with `SPV_REFLECT_MODULE_FLAG_NO_COPY` RAII guard and entry-point enumeration.
- DXIL binary reflection (`DxilReflector`) via `IDxcContainerReflection` + `ID3D12ShaderReflection` (Linux ABI bridge in `DxcRuntimeBridge.hpp` unverified on Linux host; `BL-SHD-007`).
- Unified `ShaderReflectionData` (descriptors, recursive constant buffer layouts, push constants, vertex/stage signatures, compute thread-group dimensions, backend provenance).
- `ShaderReflectionValidator` implementing `ValidateStageInterface` and policy-driven `ValidateCrossBackendCompatibility` (`StrictOneToOne` & `MorbiBindless`).
- `ShaderCache` v3 lossless serialization + v2 stale invalidation, verified alongside `MorbiShaderReflectionTests` (190/190 assertions in Release & Debug).

### WP3 — Unified Pipeline Layout (`COMPLETE`)

Completed and verified on 2026-11-18 ([Completion Report](reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md) | [Test Record](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) | [ADR-0003](decisions/ADR-0003-unified-pipeline-layout.md)):
- `PipelineLayoutHandle`, `PipelineLayoutDesc`, `PipelineLayoutValidator`, `IDevice::CreatePipelineLayout`/`DestroyPipelineLayout`, `ICommandList::TrackPipelineLayout`, and backward-compatible `PipelineLayout` on `GraphicsPipelineDesc` / `ComputePipelineDesc`.
- `Morbi.Shader::PipelineLayoutBuilder` (`BuildFromReflection`, `BuildFromDualBackendReflections`, cross-stage VS+PS & CS merging, `CrossBackendBindingPolicy`, `GlobalBindlessHybrid` Set 0 absorption) with zero `RHI -> Shader` dependency.
- Vulkan 1.3 (`VkDescriptorSetLayout`, `VkPipelineLayout`, `VkPhysicalDeviceLimits`/features, immutable samplers, push-constant stage mask/bytes) and DX12 (`ID3D12RootSignature`, CBV/SRV/UAV vs. Sampler table split, static samplers, `<= 64` DWORD check, root signature switching) with collision-safe canonical hash caching and GPU lifetime protection, verified in `MorbiPipelineLayoutTests` (`128/128` assertions).

### WP4 — Graphics Pipeline (`NOT STARTED`)

- Configurable vertex input layouts (resolves Phase 1 WP4 backlog), MRT, rasterization state, depth/stencil state, blend state, primitive topology, multisampling, configurable shader entry points, optional geometry and tessellation stages.
- Vulkan and DX12 implementations.

### WP5 — Compute Pipeline (`NOT STARTED`)

- Compute pipeline descriptor, configurable compute entry point, unified pipeline layout integration, thread group metadata, Vulkan & DX12 compute pipelines, resource binding validation, compute pipeline tests.

### WP6 — Pipeline Cache (`NOT STARTED`)

- In-memory PSO deduplication, single-flight pipeline creation, Vulkan `VkPipelineCache`, DX12 `ID3D12PipelineLibrary1` (resolves Phase 1 WP3 backlog), persistent pipeline cache, compatibility validation, corruption recovery, cache performance tests.

### WP7 — Shader Hot Reload (`NOT STARTED`)

- `FileWatcher` integration (note: `IO::FileWatcher` currently lacks Linux backend; tracked in `BACKLOG.md`), reverse include dependency tracking, shader recompilation, pipeline rebuilding, atomic pipeline replacement, last-known-good retention, deferred destruction of old pipelines, hot reload tests.

### WP8 — Cooked Shader Asset (`NOT STARTED`)

- `.mshdr` asset format, DXIL & SPIR-V storage, reflection metadata, include dependency metadata, compiler metadata, asset versioning, CRC/integrity validation, `MorbiPak` integration, runtime loading without DXC, backend-specific artifact selection.

### Phase 2 Mandatory Architecture Requirements

- Vulkan and DX12 must share a consistent resource binding model.
- DX12 register spaces and Vulkan descriptor sets must use an explicitly defined mapping.
- Do not assume `register space N = Vulkan set N` where the existing bindless architecture combines multiple register spaces into one descriptor set (currently `VulkanDevice` maps `t0,space0` -> `binding 0`, `t0,space1` -> `binding 1`, `s0,space0` -> `binding 2`, `b1,space0` -> `binding 3` all in Set 0, while `DX12Device` uses Root Constants `b0,space0`, Descriptor Table for `t0,space0` + `t0,space1`, and Direct Root CBV `b1,space0`).
- Vulkan target environment must match the engine's Vulkan 1.3 target.
- DXC loading must account for Windows and Linux platform differences.
- Shader cache keys must include compiler version, compile arguments, defines, profiles, and transitive include contents.
- Reflection must be derived from compiled binaries rather than source-code regex.
- Failed hot reload must never destroy the last working pipeline.

---

## 5. Phase 3 — Render Architecture (`NOT STARTED`)

**Goal:** Build a scalable rendering architecture supporting modern rendering techniques and both graphics APIs: Render Graph DAG/culling/aliasing, Render Pass abstraction, resource dependency tracking, GPU resource transitions & barrier management, Vulkan sync & DX12 resource state tracking, multi-threaded command recording, render submission scheduling, transient resource allocation (including fixing existing `RenderGraph` transient buffer leak), debugging/visualization, cross-backend validation.

---

## 6. Phase 4 — Modern Rendering Pipeline (`NOT STARTED`)

**Goal:** PBR material system, Forward+ rendering, Deferred rendering, Shadow mapping, Cascaded Shadow Maps, IBL, Environment lighting, HDR rendering, Tone mapping, Post-processing pipeline, GPU-driven rendering, Frustum culling, Occlusion culling, LOD, GPU resource batching, rendering performance optimization.

---

## 7. Phase 5 — Animation Asset Pipeline (`NOT STARTED`)

**Goal:** FBX import, GLTF/GLB animation import, Skeleton import, Bone hierarchy, Skinning data, Animation clip import, Animation metadata, Animation asset cooking, Root motion extraction, Animation compression, Asset validation, Editor integration.

---

## 8. Phase 6 — Advanced Animation Runtime (`NOT STARTED`)

**Goal:** Animation State Machine, Blend Trees, Animation Layers, Crossfade, Animation Events, Root Motion, Inverse Kinematics (IK), Animation Retargeting, Motion Blending, GPU Skinning, Animation Compression, Animation Update Scheduling, Multi-character animation optimization.

---

## 9. Phase 7 — Scene / Animation / Renderer Integration (`NOT STARTED`)

**Goal:** ECS Animator Component, Skeleton & Entity integration, Animation state management, Renderer integration, GPU animation buffer management, Animation update scheduling, Scene-level animation management, Multi-character rendering, Editor animation tools, Animation debugging tools.

---

## 10. Phase 8 — Amber & Ramy Game Test (`NOT STARTED`)

**Goal:** Validate the engine through a real two-player game scenario (Player 1: Amber, Player 2: Ramy) covering character/animation import, locomotion/combat animations, movement/rotation, state transitions, rendering, multiplayer synchronization, animation replication, and two-player scene testing.

---

## 11. Phase 9 — Performance & Stability (`NOT STARTED`)

**Goal:** CPU/GPU profiling, memory leak detection, GPU resource lifetime stress tests, large scene and high entity-count testing, multi-character animation stress tests, Vulkan & DX12 performance testing, frame pacing, GPU sync validation, long-running stability tests, Release build optimization.

---

## 12. Phase 10 — Networking Integration Test (`NOT STARTED`)

**Goal:** Two-player multiplayer test, server-authoritative simulation, character movement & animation state replication, transform interpolation, client-side prediction, network ownership, packet loss & latency simulation, dedicated server testing, Linux dedicated server validation, long-running multiplayer stability.

---

## 13. Outstanding RHI Backlog

| Task | Original Phase | Planned Handling | Backlog ID |
|---|---|---|---|
| PSO Cache | Phase 1 WP3 | Phase 2 WP6 | `BL-RHI-001` |
| Vertex Layout API | Phase 1 WP4 | Phase 2 WP4 | `BL-RHI-002` |
| Async Upload | Phase 1 WP5 | Separate RHI backlog task | `BL-RHI-003` |

---

## 14. Development Rules

1. Do not modify unrelated systems.
2. Preserve working Vulkan and DX12 implementations.
3. Keep Vulkan and DX12 feature parity as a primary requirement.
4. Use production-quality architecture rather than temporary workarounds.
5. Avoid unnecessary duplication between backends.
6. Add regression tests for every critical change.
7. Build both Release and Debug configurations where applicable.
8. Run Vulkan and DX12 tests.
9. Verify actual runtime behavior, not only compilation.
10. Report exact modified files and test results.
11. Do not mark a work package complete without evidence.
12. Do not start the next work package until the current package is reviewed and approved.
13. Keep a record of known limitations and deferred work.
14. Preserve Linux compatibility throughout development.
