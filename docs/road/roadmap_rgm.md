
# MORBI ENGINE — MASTER ROADMAP

**Project:** Morbi Engine  
**Architecture:** C++20 Game Engine  
**Graphics APIs:** Vulkan 1.3 + DirectX 12  
**Platforms:** Windows + Linux  
**Quality Target:** Advanced / Production-Ready  
**Animation Scope:** Full Animation System

---

# 1. Overall Progress

| Phase | Name | Status |
|---|---|---|
| Phase 0 | Architecture Audit | COMPLETE |
| Phase 1 | RHI & GPU Resource Foundation | COMPLETE |
| Phase 2 | Shader & Pipeline Infrastructure | IN PROGRESS |
| Phase 3 | Render Architecture | PENDING |
| Phase 4 | Modern Rendering Pipeline | PENDING |
| Phase 5 | Animation Asset Pipeline | PENDING |
| Phase 6 | Advanced Animation Runtime | PENDING |
| Phase 7 | Scene / Animation / Renderer Integration | PENDING |
| Phase 8 | Amber & Ramy Game Test | PENDING |
| Phase 9 | Performance & Stability | PENDING |
| Phase 10 | Networking Integration Test | PENDING |

---

# 2. Phase 0 — Architecture Audit

**Status: COMPLETE**

The existing engine architecture was audited to establish the current state of:

- RHI
- Vulkan backend
- DirectX 12 backend
- Renderer
- Shader infrastructure
- Animation infrastructure
- Physics
- Networking
- Asset pipeline
- Runtime and Editor

The audit established the architectural foundation for subsequent phases.

---

# 3. Phase 1 — RHI & GPU Resource Foundation

**Status: COMPLETE**

## Work Packages

| WP | Name | Status |
|---|---|---|
| WP1 | GPU Resource Lifetime | COMPLETE |
| WP2 | Bindless Resource Reclamation | COMPLETE |
| WP3 | PSO Cache | NOT IMPLEMENTED |
| WP4 | Vertex Layout API | NOT IMPLEMENTED |
| WP5 | Async Upload | NOT IMPLEMENTED |

WP3, WP4 and WP5 were intentionally left outside the completed Phase 1 scope. Their required work is tracked in later phases or the RHI backlog.

## Final Six-Issue Closure

The following issues were fixed:

1. Vulkan shutdown native resource cleanup.
2. Vulkan swapchain destruction and deferred destruction ordering.
3. DX12 ResizeSwapchain RTV descriptor leak.
4. VulkanMemoryAllocator slab offset reuse and reclamation.
5. Synchronous CreateTexture(initialData) upload safety.
6. Duplicate Destroy queue entries.

## Verification Results

| Test | Result |
|---|---|
| Release CTest | 5/5 passed |
| Release GenerationalHandleHashTests | 507/507 assertions |
| Debug GenerationalHandleHashTests | 507/507 assertions |
| Release RHIDescriptorTests | 860/860 assertions |
| Debug RHIDescriptorTests | 860/860 assertions |
| Vulkan 1.3 Editor Smoke Test | PASSED |
| DirectX 12 Editor Smoke Test | PASSED |
| Release Build | PASSED |
| Debug RHI and Test Build | PASSED |

Hardware used for verification:

- NVIDIA GeForce RTX 4060 Ti
- Vulkan 1.3
- DirectX 12

### Known Limitation

Vulkan validation layers were unavailable on the development machine. DX12 Debug Layer and DRED checks passed.

**Phase 1 is officially closed.**

---

# 4. Phase 2 — Shader & Pipeline Infrastructure

**Status: IN PROGRESS**

**Goal:** Establish a unified, robust and production-quality shader and pipeline infrastructure for Vulkan and DirectX 12.

## WP1 — Shader Compilation Infrastructure

- In-process DXC integration.
- IDxcCompiler3 and IDxcUtils.
- Custom HLSL include handler.
- Include dependency graph.
- Deterministic shader request hashing.
- Compiler version tracking.
- Shader model 6.0–6.6 support.
- DXIL compilation.
- SPIR-V compilation.
- SPIR-V validation.
- ShaderCache v2.
- Cache invalidation and corruption handling.
- Shader compilation tests.

## WP2 — Binary Reflection

- SPIR-V reflection.
- DXIL reflection.
- Unified ShaderReflectionData.
- Descriptor binding reflection.
- Constant buffer layout reflection.
- Vertex input reflection.
- Push constant reflection.
- Compute thread group size reflection.
- Cross-stage validation.
- Cross-backend layout compatibility validation.

## WP3 — Unified Pipeline Layout

- PipelineLayoutHandle.
- PipelineLayoutDesc.
- Descriptor set layouts.
- Descriptor binding definitions.
- Push constant ranges.
- Vulkan pipeline layout implementation.
- DX12 root signature implementation.
- Resource binding compatibility.
- Pipeline layout lifetime management.

## WP4 — Graphics Pipeline

- Configurable vertex input layouts.
- Multiple render targets (MRT).
- Rasterization state.
- Depth and stencil state.
- Blend state.
- Primitive topology.
- Multisampling.
- Configurable shader entry points.
- Optional geometry and tessellation stages.
- Vulkan implementation.
- DX12 implementation.

## WP5 — Compute Pipeline

- Compute pipeline descriptor.
- Configurable compute entry point.
- Unified pipeline layout integration.
- Thread group metadata.
- Vulkan compute pipeline.
- DX12 compute pipeline.
- Resource binding validation.
- Compute pipeline tests.

## WP6 — Pipeline Cache

- In-memory PSO deduplication.
- Single-flight pipeline creation.
- Vulkan VkPipelineCache.
- DX12 ID3D12PipelineLibrary1.
- Persistent pipeline cache.
- Cache compatibility validation.
- Corruption recovery.
- Cache performance tests.

## WP7 — Shader Hot Reload

- FileWatcher integration.
- Reverse include dependency tracking.
- Shader recompilation.
- Pipeline rebuilding.
- Atomic pipeline replacement.
- Last-known-good pipeline retention.
- Deferred destruction of old pipelines.
- Hot reload tests.

## WP8 — Cooked Shader Asset

- `.mshdr` asset format.
- DXIL storage.
- SPIR-V storage.
- Reflection metadata.
- Include dependency metadata.
- Compiler metadata.
- Asset versioning.
- CRC/integrity validation.
- MorbiPak integration.
- Runtime loading without DXC.
- Backend-specific artifact selection.

## Important Architecture Requirements

- Vulkan and DX12 must share a consistent resource binding model.
- DX12 register spaces and Vulkan descriptor sets must use an explicitly defined mapping.
- Do not assume a direct `register space N = Vulkan set N` mapping where the existing bindless architecture combines multiple register spaces into one descriptor set.
- Vulkan target environment must match the engine's Vulkan 1.3 target.
- DXC loading must account for Windows and Linux platform differences.
- Shader cache keys must include compiler version, compile arguments, defines, profiles and transitive include contents.
- Reflection must be derived from compiled binaries rather than source-code regex.
- Failed hot reload must never destroy the last working pipeline.

---

# 5. Phase 3 — Render Architecture

**Status: PENDING**

**Goal:** Build a scalable rendering architecture that supports modern rendering techniques and both graphics APIs.

- Render Graph.
- Render Pass abstraction.
- Resource dependency tracking.
- Frame Graph.
- GPU resource transitions.
- Barrier management.
- Vulkan synchronization.
- DX12 resource state tracking.
- Multi-threaded command recording.
- Render submission scheduling.
- Transient resource allocation.
- Render Graph debugging and visualization.
- Cross-backend validation.

---

# 6. Phase 4 — Modern Rendering Pipeline

**Status: PENDING**

**Goal:** Implement a modern, scalable rendering pipeline.

- PBR material system.
- Forward+ rendering.
- Deferred rendering.
- Shadow mapping.
- Cascaded Shadow Maps.
- Image-Based Lighting.
- Environment lighting.
- HDR rendering.
- Tone mapping.
- Post-processing pipeline.
- GPU-driven rendering.
- Frustum culling.
- Occlusion culling.
- Level of Detail (LOD).
- GPU resource batching.
- Rendering performance optimization.

---

# 7. Phase 5 — Animation Asset Pipeline

**Status: PENDING**

**Goal:** Build a complete animation import and asset processing pipeline.

- FBX import.
- GLTF/GLB animation import.
- Skeleton import.
- Bone hierarchy.
- Skinning data.
- Animation clip import.
- Animation metadata.
- Animation asset cooking.
- Root motion extraction.
- Animation compression.
- Asset validation.
- Editor integration.

---

# 8. Phase 6 — Advanced Animation Runtime

**Status: PENDING**

**Goal:** Implement a complete animation runtime suitable for advanced games.

- Animation State Machine.
- Blend Trees.
- Animation Layers.
- Crossfade.
- Animation Events.
- Root Motion.
- Inverse Kinematics (IK).
- Animation Retargeting.
- Motion Blending.
- GPU Skinning.
- Animation Compression.
- Animation Update Scheduling.
- Multi-character animation optimization.

---

# 9. Phase 7 — Scene / Animation / Renderer Integration

**Status: PENDING**

**Goal:** Integrate animation, ECS, scene management and rendering into one coherent runtime.

- ECS Animator Component.
- Skeleton and Entity integration.
- Animation state management.
- Renderer integration.
- GPU animation buffer management.
- Animation update scheduling.
- Scene-level animation management.
- Multi-character rendering.
- Editor animation tools.
- Animation debugging tools.

---

# 10. Phase 8 — Amber & Ramy Game Test

**Status: PENDING**

**Goal:** Validate the engine through a real two-player game scenario.

## Test Characters

- Player 1: Amber.
- Player 2: Ramy.

## Test Scope

- Import both character models.
- Import their animation sets.
- Use compatible animation sets where possible.
- Idle animation.
- Walking animation.
- Running animation.
- Jump animation.
- Attack animation.
- Character movement.
- Character rotation.
- Animation state transitions.
- Character rendering.
- Multiplayer synchronization.
- Animation replication.
- Two-player scene testing.

The goal is to validate the complete asset → animation → ECS → renderer → networking pipeline.

---

# 11. Phase 9 — Performance & Stability

**Status: PENDING**

**Goal:** Validate performance, reliability and stability.

- CPU profiling.
- GPU profiling.
- Memory leak detection.
- GPU resource lifetime stress tests.
- Large scene testing.
- High entity-count testing.
- Multi-character animation stress tests.
- Vulkan performance testing.
- DX12 performance testing.
- Frame pacing.
- GPU synchronization validation.
- Long-running stability tests.
- Release build optimization.

---

# 12. Phase 10 — Networking Integration Test

**Status: PENDING**

**Goal:** Validate the engine's networking architecture through real multiplayer gameplay.

- Two-player multiplayer test.
- Server-authoritative simulation.
- Character movement replication.
- Animation state replication.
- Transform interpolation.
- Client-side prediction.
- Network ownership.
- Packet loss testing.
- Latency simulation.
- Dedicated server testing.
- Linux dedicated server validation.
- Long-running multiplayer stability.

---

# 13. Outstanding RHI Backlog

These tasks must not be forgotten:

| Task | Original Phase | Planned Handling |
|---|---|---|
| PSO Cache | Phase 1 WP3 | Phase 2 WP6 |
| Vertex Layout API | Phase 1 WP4 | Phase 2 WP4 |
| Async Upload | Phase 1 WP5 | Separate RHI backlog task |

The Phase 2 implementations should explicitly verify whether these original requirements are fully covered.

---

# 14. Development Rules

All phases must follow these rules:

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

---

# 15. Current Development Position

**Completed:**
- Phase 0 — Architecture Audit
- Phase 1 — RHI & GPU Resource Foundation

**Current:**
- Phase 2 — Shader & Pipeline Infrastructure

**Next Work Package:**
- Phase 2 WP1 — Shader Compilation Infrastructure

**Development Strategy:**

Complete Phase 2 in dependency order, validate both graphics APIs, then proceed to Render Architecture and Modern Rendering Pipeline.

The ultimate goal is a general-purpose, advanced-quality game engine with a complete rendering, animation, physics and networking foundation.
