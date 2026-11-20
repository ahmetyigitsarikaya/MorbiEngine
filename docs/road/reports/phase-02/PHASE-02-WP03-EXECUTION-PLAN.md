# Phase 2 WP3 — Unified Pipeline Layout: Execution Plan

**Work Package:** Phase 2 WP3 — Unified Pipeline Layout  
**Status:** `NOT STARTED` (Audit & Architecture Design Complete — **Awaiting Implementation Approval**)  
**Date:** 2026-11-18  
**Dependencies:** Phase 2 WP1 (`COMPLETE`), Phase 2 WP2 (`COMPLETE`)  
**Companion Architecture Proposal:** [PROP-0002-WP3-UNIFIED-PIPELINE-LAYOUT.md](../../proposals/PROP-0002-WP3-UNIFIED-PIPELINE-LAYOUT.md)

---

## 1. Incremental Implementation Breakdown (`WP3.1`–`WP3.7`)

### WP3.1 — Backend-Independent RHI Pipeline Layout Model (`Morbi.RHI`)
- Add `Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp` and `Engine/Source/RHI/src/PipelineLayout.cpp` (register in `Engine/Source/RHI/CMakeLists.txt`).
- Types:
  - `PipelineLayoutTag`, `PipelineLayoutHandle` (exportable via `RHITypes.hpp` / `PipelineLayout.hpp`).
  - `ShaderVisibility`, `DescriptorType`, `PipelineLayoutMode`, `StaticSamplerFilter`, `StaticSamplerAddressMode`.
  - `PipelineBindingLayout`, `DescriptorSetLayoutDesc`, `StaticSamplerDesc`, `PipelinePushConstantRange`, `PipelineConstantBufferLayout`, `PipelineLayoutDesc`.
  - `PipelineLayoutLimits`, `PipelineLayoutCostBreakdown` (DX12 DWORDs + Vulkan per-stage descriptor counts), `PipelineLayoutValidationResult`, `PipelineLayoutValidator`.
  - Canonical normalization & deterministic 64-bit hash (`ComputePipelineLayoutHash(const PipelineLayoutDesc&)`).
- **Gate:** `Morbi.RHI` compiles cleanly at `/W4` (0 warnings, 0 errors); pure unit tests verify canonical hashing & validator rules.

### WP3.2 — Reflection-to-Layout Builder (`Morbi.Shader` -> `Morbi.RHI`)
- Link `Morbi::RHI` in `Engine/Source/Shader/CMakeLists.txt` (`Morbi.RHI` does not depend on `Morbi.Shader` — acyclic).
- Add `Engine/Source/Shader/include/Morbi/Shader/PipelineLayoutBuilder.hpp` & `src/PipelineLayoutBuilder.cpp`.
- Implement:
  - `PipelineLayoutBuilder::BuildFromReflection(std::span<const ShaderReflectionData> stages, const CrossBackendBindingPolicy& policy, PipelineLayoutMode mode, std::string_view debugName)`.
  - Cross-stage merging (e.g., `VSMain` + `PSMain`, or multi-shader shared layout): merges identical bindings (`Visibility |= stageVis`), merges overlapping/identical push-constant ranges, detects conflicting resource types/counts/layouts across stages.
  - Emits explicit `PipelineLayoutDesc` preserving DX12 register/space provenance and `PreferRootDescriptorOnDx12` hints from `CrossBackendBindingPolicy`.
- **Gate:** Release & Debug build + unit tests merging VS+PS & CS reflections (including real Morbi PBR shader + standalone multi-binding shaders).

### WP3.3 — `IDevice` & `ICommandList` Contract Extension (`Morbi.RHI` & `NullDevice`)
- Update [RHITypes.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/RHITypes.hpp):
  - Add `PipelineLayoutHandle PipelineLayout = PipelineLayoutHandle::Invalid();` to `GraphicsPipelineDesc` and `ComputePipelineDesc` (backward-compatible default = engine default bindless layout).
- Update [IDevice.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/IDevice.hpp):
  - `CreatePipelineLayout(const PipelineLayoutDesc& desc)`
  - `DestroyPipelineLayout(PipelineLayoutHandle handle)` (uses deferred destruction matching Buffer/Texture/Pipeline timeline/fence safety!)
  - `IsResourceAlive(PipelineLayoutHandle handle)`
  - `GetDefaultBindlessPipelineLayout() const noexcept`
  - `GetPipelineLayoutLimits() const noexcept`
  - `GetPipelineLayoutStats() const noexcept` (created count, cached/deduplicated hits, destroyed count)
- Update [ICommandList.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/ICommandList.hpp) & [RHIDevice.cpp](../../../../Engine/Source/RHI/src/RHIDevice.cpp):
  - Track `PipelineLayoutHandle` lifetime (`TrackPipelineLayout` / indirect tracking through bound pipelines so destroying a `PipelineLayoutHandle` before GPU completion is fence/timeline-safe!).
  - Implement `NullDevice` layout validation, hash-cache deduplication, and handle lifecycle cleanly.

### WP3.4 — Vulkan 1.3 Pipeline Layout Generator & Cache (`VulkanDevice.cpp`)
- Retain `VkPhysicalDeviceProperties::limits` (`m_deviceLimits`) and descriptor indexing limits during `VulkanDevice::Initialize()`.
- Implement `m_pipelineLayouts` `GenerationalPool<VulkanPipelineLayoutRecord, PipelineLayoutTag>` + hash map `m_pipelineLayoutCache` (`u64 canonicalHash -> PipelineLayoutHandle`, ref-counted/deduplicated so duplicate layouts reuse the same `VkPipelineLayout`!).
- Implement `CreatePipelineLayout` / `DestroyPipelineLayoutInternalLocked`:
  - Validate against `m_deviceLimits`.
  - `GlobalBindlessHybrid`: reuse `m_bindlessLayout` as Set `0`; allocate custom `VkDescriptorSetLayout`s (plus immutable `VkSampler` handles for `StaticSamplerDesc`s) for Sets `>= 1`.
  - `Standalone`: allocate all `VkDescriptorSetLayout`s (Set `0..N`) from `DescriptorSetLayoutDesc`.
  - Build `VkPushConstantRange` array with combined `VkShaderStageFlags` (`VK_SHADER_STAGE_VERTEX_BIT`, `FRAGMENT_BIT`, `COMPUTE_BIT`).
  - Create `VkPipelineLayout` via `m_devDisp.CreatePipelineLayout` (and store `VulkanPipelineLayoutRecord` with `OwnedSetLayouts`, `OwnedSamplers`, `PushConstantStageMask`, `IncludesGlobalBindlessSet`, `RefCount`).
- Update `CreateGraphicsPipeline` & `CreateComputePipeline` to resolve `desc.PipelineLayout` (fallback `m_defaultBindlessPipelineLayout`), store `PipelineLayoutHandle` + `VkPipelineLayout` + push-constant stage mask on `VulkanPipelineRecord`, and keep default Set 0 behavior intact.
- Update `VulkanCommandList::SetPipeline` & `PushConstants` & `Begin()`: bind `rec->VkLayout`, bind `m_bindlessSet` ONLY when `rec->IncludesGlobalBindlessSet`, and pass `rec->PushConstantStageMask` to `CmdPushConstants` instead of blindly hardcoding `m_pipelineLayout` / `VK_SHADER_STAGE_ALL`.

### WP3.5 — DirectX 12 Root Signature Generator & Cache (`DX12Device.cpp`)
- Implement `m_pipelineLayouts` `GenerationalPool<DX12PipelineLayoutRecord, PipelineLayoutTag>` + `m_pipelineLayoutCache` (`u64 canonicalHash -> PipelineLayoutHandle`, ref-counted deduplication).
- Implement `CreatePipelineLayout` / `DestroyPipelineLayoutInternalLocked`:
  - Run `PipelineLayoutValidator` checking `<= 64` DWORD root signature cost.
  - Translate `PipelinePushConstantRange` → `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS` (record `RootConstantParamIndex`, `PushConstantBindPoint`, `PushConstantRegisterSpace`).
  - Translate `StaticSamplerDesc` → `D3D12_STATIC_SAMPLER_DESC` (`0` DWORD cost).
  - Translate bindings per `DescriptorSetLayoutDesc`:
    - `PreferRootDescriptorOnDx12 && Count == 1` → `D3D12_ROOT_PARAMETER_TYPE_CBV` (or `SRV`/`UAV`).
    - All remaining CBV/SRV/UAV bindings in the set → 1 `DESCRIPTOR_TABLE` (`1` DWORD); all dynamic `Sampler` bindings in the set → 1 separate Sampler `DESCRIPTOR_TABLE` (`1` DWORD).
  - In `GlobalBindlessHybrid`: keep Slot `0` (`32-bit constants`), Slot `1` (`m_bindlessHeap` SRV table), Slot `2` (`b1, space0` Root CBV), Static Sampler `s0`, and append custom Sets `>= 1`.
  - Serialize via `D3D12SerializeRootSignature` and create via `m_device->CreateRootSignature`. Log `errBlob` clearly on failure.
- Update `CreateGraphicsPipeline` & `CreateComputePipeline` to attach `rec->RootSignature`, store `PipelineLayoutHandle` + `IsCompute` + `RootConstantParamIndex` + `UsesGlobalBindlessHeap` on `DX12PipelineRecord`.
- Update `DX12CommandList::SetPipeline()` & `PushConstants()`:
  - Switch `SetGraphicsRootSignature` / `SetComputeRootSignature` when active root signature changes (re-binding `m_bindlessHeap` table when `UsesGlobalBindlessHeap`), and route `PushConstants` ONLY to the active pipeline's bind point (`Graphics` or `Compute`) at its resolved `RootConstantParamIndex`!
  - Fix D3D12 Debug Layer compatibility across mixed root signature switches.

### WP3.6 — Runtime Consumer Integration & Smoke Verification
- Verify `EngineRuntime.cpp` existing 4 shaders (`kUnifiedHlslShader`, `kScenePbrMeshHlslShader`, `kGameUIProceduralHlslShader`, `kGameUIOverlayHlslShader`) + `GenerationalHandleHashTests.cpp` compute shader continue working identically, PLUS validate reflection-built layouts on those real shaders.
- Confirm Phase 1 RHI tests (`860/860`, `507/507`) & Shader/Reflection tests (`181/181`, `190/190`) remain green.

### WP3.7 — Comprehensive WP3 Tests, Audit & Documentation
- Add dedicated test suite `MorbiPipelineLayoutTests` (`Tests/ShaderTests/PipelineLayoutTests.cpp` or `Tests/RHITests/`) covering all 12+ required scenarios (Section 2 below).

---

## 2. Planned Test Matrix (`MorbiPipelineLayoutTests`)

| # | Scenario | Backends | Assertions |
|---|---|---|---|
| 1 | Same shader (VS+PS and CS) Vulkan `VkPipelineLayout` + DX12 `ID3D12RootSignature` creation from `ShaderReflectionData` | Vulkan 1.3 + DX12 + Null | Valid handles, clean PSO creation |
| 2 | Descriptor binding mapping (`t`, `u`, `b`, `s` ↔ Set/Binding) | Vulkan 1.3 + DX12 | Exact set/binding, sampler split table in DX12 |
| 3 | Descriptor arrays (`Texture2D[8]`) & unbounded bindless arrays (`[]`) | Vulkan 1.3 + DX12 | Hybrid & Standalone modes validated cleanly |
| 4 | Push constants ↔ Root constants parity (`32B`, `64B`, `128B`) + runtime `PushConstants` dispatch without D3D12/Vulkan validation errors | Vulkan 1.3 + DX12 | Exact visibility, byte size, slot resolution |
| 5 | Constant buffer layout compatibility (`PreferRootDescriptorOnDx12` vs descriptor table CBV) | Vulkan 1.3 + DX12 | Metadata + DWORD calculation verified |
| 6 | Shader stage visibility (`Vertex`-only, `Pixel`-only, `Vertex|Pixel`, `Compute`) | Vulkan 1.3 + DX12 | Exact `VkShaderStageFlags` & `D3D12_SHADER_VISIBILITY` |
| 7 | Root signature `64`-DWORD hardware limit overflow rejection (e.g. `32` root const + `17` Root CBVs = `66` DWORDs) | Validator + DX12 | Rejected cleanly BEFORE native call |
| 8 | Vulkan descriptor set limits (`maxBoundDescriptorSets`, `>128B` push constants, per-stage limits) | Validator + Vulkan 1.3 | Rejected cleanly against device limits |
| 9 | Unsupported resource types (`CombinedImageSampler` on DX12, overlapping push constant ranges) | Validator + DX12 | Rejected with diagnostic code |
| 10 | Conflicting / incompatible VS+PS stage bindings & layouts rejected | Builder + Validator | Failure `Result` with clear error |
| 11 | Pipeline layout cache deduplication (`CreatePipelineLayout` on identical normalized `PipelineLayoutDesc` returns cached/deduped layout) + deferred timeline/fence safe destruction | Vulkan 1.3 + DX12 + Null | Cache hit stats, ref-count, zero UAF |
| 12 | Multiple pipelines/shaders sharing one unified `PipelineLayoutHandle` across draw/dispatch & switching between custom and default bindless layouts | Vulkan 1.3 + DX12 | Clean execution, DX12 Debug Layer + DRED clean |

---

## 3. Files Planned for Creation / Modification (WP3 Only)

### Planned New Files
- `Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp`
- `Engine/Source/RHI/src/PipelineLayout.cpp`
- `Engine/Source/Shader/include/Morbi/Shader/PipelineLayoutBuilder.hpp`
- `Engine/Source/Shader/src/PipelineLayoutBuilder.cpp`
- `Tests/RHITests/PipelineLayoutTests.cpp` (or `Tests/ShaderTests/PipelineLayoutTests.cpp`)
- `docs/road/decisions/ADR-0003-unified-pipeline-layout.md`, `docs/road/test-results/phase-02/PHASE-02-WP03-VERIFICATION-*.md`, `docs/road/reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md`

### Planned Modified Files
- `Engine/Source/RHI/CMakeLists.txt`, `Engine/Source/RHI/include/Morbi/RHI/RHITypes.hpp`, `Engine/Source/RHI/include/Morbi/RHI/IDevice.hpp`, `Engine/Source/RHI/include/Morbi/RHI/ICommandList.hpp`, `Engine/Source/RHI/src/RHIDevice.cpp`
- `Engine/Source/Shader/CMakeLists.txt`
- `Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp`
- `Engine/Source/RHI.DX12/src/DX12Device.cpp`
- `Tests/.../CMakeLists.txt`

---

## 4. Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| DX12 `SetGraphicsRootSignature` invalidates root arguments on pipeline change | Incorrect descriptors or GPU hang | Track active root signature per command list; re-bind `m_bindlessHeap` table immediately after root signature change |
| `VulkanCommandList::PushConstants` `stageFlags` mismatch | Vulkan validation failure | Store unioned `PushConstantStageMask` per `VulkanPipelineLayoutRecord` and pass it dynamically to `vkCmdPushConstants` |
| Premature `DestroyPipelineLayout` while command list is in flight | Dangling native layout handles | Reference-count + defer destruction on `VulkanDevice` timeline / `DX12Device` fence identically to Buffer/Texture/Pipeline |
| Premature breaking of `kScenePbrMeshHlslShader` / runtime shaders | Smoke test failure | Default `PipelineLayout = Invalid()` routes to `GetDefaultBindlessPipelineLayout()`, plus `GlobalBindlessHybrid` explicitly preserves Set 0 / `m_bindlessHeap` |
| Vulkan validation layers absent on Windows dev host (`BL-ENV-001`) | Validation errors missed on VK | Strict pre-creation `PipelineLayoutValidator` + DX12 Debug Layer + DRED + Vulkan return code & limit checks |
