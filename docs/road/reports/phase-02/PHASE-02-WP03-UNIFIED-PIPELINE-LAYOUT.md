# Phase 2 WP3 — Unified Pipeline Layout Completion Report

**Work Package:** Phase 2 WP3 — Unified Pipeline Layout  
**WP Status:** `COMPLETE`  
**Date:** 2026-11-18  
**Dependencies:** Phase 2 WP1 (`COMPLETE`), Phase 2 WP2 (`COMPLETE`)

---

## 1. WP Summary

Implemented a production-grade, backend-agnostic **Unified Pipeline Layout** system bridging `Morbi.Shader::ShaderReflectionData` → `Morbi.RHI::PipelineLayoutDesc` → native **Vulkan 1.3** (`VkDescriptorSetLayout` + `VkPipelineLayout`) and **DirectX 12** (`ID3D12RootSignature`) resources without exposing third-party/SDK headers in public RHI or Shader interfaces and without breaking any existing global bindless rendering pipelines.

Verified across `Null`, **Vulkan 1.3**, and **DirectX 12** backends in both **Release** and **Debug** builds via `MorbiPipelineLayoutTests` (`128/128` assertions including live GPU graphics draws, compute dispatches, and multi-layout command-list switching), alongside full regression verification (`MorbiShaderTests`, `MorbiShaderReflectionTests`, `MorbiRHIDescriptorTests`, `MorbiRHIGenerationalHandleHashTests`, CTest suites, and Vulkan 1.3 / DX12 Editor smoke tests).

---

## 2. Initial Repository State

Prior to WP3 ([PROP-0002](../../proposals/PROP-0002-WP3-UNIFIED-PIPELINE-LAYOUT.md) & [PHASE-02-WP03-EXECUTION-PLAN.md](PHASE-02-WP03-EXECUTION-PLAN.md)):
- Both `VulkanDevice` and `DX12Device` hardcoded a single device-wide pipeline layout (`m_pipelineLayout` + `m_bindlessLayout` on Vulkan; `m_rootSignature` on DX12).
- `GraphicsPipelineDesc` and `ComputePipelineDesc` had no `PipelineLayoutHandle` association.
- `VulkanCommandList::PushConstants` hardcoded `VK_SHADER_STAGE_ALL`, and `VulkanDevice::Initialize()` discarded `VkPhysicalDeviceProperties::limits` and enabled Vulkan 1.2/1.3 features without storing device limits or enabled feature bits.
- `DX12CommandList::SetPipeline` never switched `ID3D12RootSignature` or re-bound the global bindless descriptor table when changing PSOs, and `DX12CommandList::PushConstants` called both Graphics and Compute root slot `0` unconditionally.
- `Morbi.Shader` came before `Morbi.RHI` in root `CMakeLists.txt` and had no reflection-to-layout converter.

---

## 3. Architecture & Key Changes

Documented in [ADR-0003](../../decisions/ADR-0003-unified-pipeline-layout.md):

1. **RHI Pipeline Layout Model & Validator (`Morbi.RHI`):**
   - Added `PipelineLayoutTag`, `PipelineLayoutHandle`, and backward-compatible optional `PipelineLayoutHandle PipelineLayout = PipelineLayoutHandle::Invalid()` fields on `GraphicsPipelineDesc` and `ComputePipelineDesc` in [RHITypes.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/RHITypes.hpp).
   - Created [PipelineLayout.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp) and [PipelineLayout.cpp](../../../../Engine/Source/RHI/src/PipelineLayout.cpp) providing `PipelineLayoutMode` (`Standalone` / `GlobalBindlessHybrid`), `PipelineBindingLayout`, `DescriptorSetLayoutDesc`, `StaticSamplerDesc`, `PipelinePushConstantRange`, `PipelineConstantBufferLayout`, `PipelineLayoutDesc`, `PipelineLayoutLimits`, `PipelineLayoutStats`, and `PipelineLayoutValidator` (`Normalize`, `ComputeCanonicalHash`, `AreStructurallyEqual`, `Validate`).
   - Added `CreatePipelineLayout`, `DestroyPipelineLayout`, `IsResourceAlive(PipelineLayoutHandle)`, `GetDefaultBindlessPipelineLayout`, `GetPipelineLayoutLimits`, `GetPipelineLayoutStats` on `IDevice` ([IDevice.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/IDevice.hpp)), `TrackPipelineLayout` / `GetReferencedPipelineLayouts` on `ICommandList` ([ICommandList.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/ICommandList.hpp)), and full `NullDevice` + `NullCommandList` support in [RHIDevice.cpp](../../../../Engine/Source/RHI/src/RHIDevice.cpp).

2. **Reflection-to-Layout Builder (`Morbi.Shader`):**
   - Reordered `RHI` before `Shader` in root [CMakeLists.txt](../../../../CMakeLists.txt#L40-L41) and linked `Morbi::RHI` (`PUBLIC`) in [Shader/CMakeLists.txt](../../../../Engine/Source/Shader/CMakeLists.txt), preserving zero `RHI -> Shader` coupling.
   - Added [PipelineLayoutBuilder.hpp](../../../../Engine/Source/Shader/include/Morbi/Shader/PipelineLayoutBuilder.hpp) and [PipelineLayoutBuilder.cpp](../../../../Engine/Source/Shader/src/PipelineLayoutBuilder.cpp) supporting `BuildFromReflection`, `BuildFromDualBackendReflections`, cross-stage VS+PS & CS merging, `CrossBackendBindingPolicy` enforcement, DXIL Root Constant unwrapping, and `GlobalBindlessHybrid` reserved Set 0 absorption.

3. **Vulkan 1.3 Backend ([VulkanDevice.cpp](../../../../Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp)):**
   - Queries `VkPhysicalDeviceFeatures2` (`Vulkan12` + `Vulkan13`) and `VkPhysicalDeviceProperties2` (`limits` + `Vulkan12Properties`) in `Initialize()`, enabling only supported optional features and populating real `PipelineLayoutLimits`.
   - Implements collision-safe `m_pipelineLayoutCache` (`Hash -> vector<PipelineLayoutHandle>` + `AreStructurallyEqual`), reference-counted destruction (`CacheRefCount`), immutable `VkSampler` static samplers, dense set arrays with shared empty `VkDescriptorSetLayout` gap placeholders, conditional `m_bindlessSet` binding, and active layout `PushConstantStageMask` / max byte range clamping on `VulkanCommandList::PushConstants`.
   - Protects live layouts via `PipelineRefCount` + `UnsubmittedRefCount` and GPU timeline deferred destruction ordered cleanly before bindless/device teardown in `Shutdown()`.

4. **DirectX 12 Backend ([DX12Device.cpp](../../../../Engine/Source/RHI.DX12/src/DX12Device.cpp)):**
   - Queries `D3D12_FEATURE_DATA_D3D12_OPTIONS` resource binding tiers for `m_limits`.
   - Separates CBV/SRV/UAV descriptor ranges from Sampler descriptor ranges into distinct root descriptor tables, emits Root CBVs/SRVs/UAVs for `PreferRootDescriptorOnDx12`, maps static samplers to `D3D12_STATIC_SAMPLER_DESC`, enforces the `<= 64` DWORD Root Signature limit and `CombinedImageSampler` rejection, and preserves the global bindless contract (`b0` PushConstants + Bindless Table + `b1` Root CBV + `s0` Static Sampler) in `GlobalBindlessHybrid`.
   - Tracks `m_activeGraphicsRootSig` / `m_activeComputeRootSig` in `DX12CommandList::SetPipeline`, switching root signatures and re-binding `m_bindlessHeap` when needed, and dispatches `PushConstants` and `BindConstantBuffer` exclusively to the active pipeline bind point and resolved `RootConstantParamIndex`.

---

## 4. Bugs & Subtle Constraints Caught During Implementation / Testing

1. **DXC HLSL `ConstantBuffer<T>` vs `cbuffer` Top-Level Reflection Structure:** DXC's DXIL reflector exposes `ConstantBuffer<SceneUniform>` as 1 top-level struct variable whereas SPIR-V flattens `SceneUniform`'s fields into 2 top-level members; tests using cross-backend CB layout comparison should either use standard `cbuffer` blocks or unwrap struct variables symmetrically.
2. **HLSL Namespace Overlap in SPIR-V Set 1:** Declaring `register(b0, space1)` and `register(t0, space1)` without explicit `[[vk::binding(b, s)]]` maps both to Vulkan `(set=1, binding=0)`; cross-backend shaders using standalone sets must either assign distinct bindings via `[[vk::binding]]` or explicit `CrossBackendBindingPolicy` rules.
3. **`NullDevice::IsResourceAlive(PipelineLayoutHandle)` Residency Parity:** Fixed `NullDevice::IsResourceAlive` in `RHIDevice.cpp` (`rec != nullptr` instead of `!rec->PendingDestroy`) so it matches `VulkanDevice`, `DX12Device`, and Buffer/Pipeline deferred-destruction residency semantics.
4. **Environment & Tooling Notes:** Dedicated sub-agent invocation tools were unavailable in this session environment; all independent code audits were performed directly. `VK_LAYER_KHRONOS_validation` (`BL-ENV-001`) and a Linux toolchain/host (`BL-SHD-007`) remain absent on this Windows 11 machine.

---

## 5. Files Created / Modified

### Created
- [Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp)
- [Engine/Source/RHI/src/PipelineLayout.cpp](../../../../Engine/Source/RHI/src/PipelineLayout.cpp)
- [Engine/Source/Shader/include/Morbi/Shader/PipelineLayoutBuilder.hpp](../../../../Engine/Source/Shader/include/Morbi/Shader/PipelineLayoutBuilder.hpp)
- [Engine/Source/Shader/src/PipelineLayoutBuilder.cpp](../../../../Engine/Source/Shader/src/PipelineLayoutBuilder.cpp)
- [Tests/ShaderTests/PipelineLayoutTests.cpp](../../../../Tests/ShaderTests/PipelineLayoutTests.cpp)
- [docs/road/decisions/ADR-0003-unified-pipeline-layout.md](../../../../docs/road/decisions/ADR-0003-unified-pipeline-layout.md)
- [docs/road/test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md](../../../../docs/road/test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md)
- [docs/road/reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md](../../../../docs/road/reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md)

### Modified
- [CMakeLists.txt](../../../../CMakeLists.txt) (`RHI` ordered before `Shader`)
- [Engine/Source/RHI/CMakeLists.txt](../../../../Engine/Source/RHI/CMakeLists.txt)
- [Engine/Source/RHI/include/Morbi/RHI/RHITypes.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/RHITypes.hpp)
- [Engine/Source/RHI/include/Morbi/RHI/IDevice.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/IDevice.hpp)
- [Engine/Source/RHI/include/Morbi/RHI/ICommandList.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/ICommandList.hpp)
- [Engine/Source/RHI/src/RHIDevice.cpp](../../../../Engine/Source/RHI/src/RHIDevice.cpp)
- [Engine/Source/Shader/CMakeLists.txt](../../../../Engine/Source/Shader/CMakeLists.txt)
- [Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp](../../../../Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp)
- [Engine/Source/RHI.DX12/src/DX12Device.cpp](../../../../Engine/Source/RHI.DX12/src/DX12Device.cpp)
- [Tests/ShaderTests/CMakeLists.txt](../../../../Tests/ShaderTests/CMakeLists.txt)
- `docs/road/` status, roadmap, phase, changelog, and backlog files

---

## 6. Verification Reference

See full post-final-edit test matrix and environment caveats in [PHASE-02-WP03-VERIFICATION-2026-11-18.md](../../test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md).
