# ADR-0003: Unified Pipeline Layout Model, Reflection Builder, and Backend Resource Lifetime Integration

**Status:** Accepted  
**Date:** 2026-11-18  
**Phase / WP:** Phase 2 WP3 — Unified Pipeline Layout

---

## Context

Prior to WP3, both `VulkanDevice` and `DX12Device` hardcoded a single device-wide pipeline layout (`m_pipelineLayout` + `m_bindlessLayout` on Vulkan; `m_rootSignature` on DX12), `GraphicsPipelineDesc` and `ComputePipelineDesc` had no `PipelineLayoutHandle` field, `VulkanCommandList::PushConstants` hardcoded `VK_SHADER_STAGE_ALL`, `VulkanDevice` discarded `VkPhysicalDeviceProperties::limits` and enabled Vk1.2/1.3 features without storing the enabled subset, `DX12CommandList::SetPipeline` never switched `ID3D12RootSignature` or re-bound descriptor tables, and `DX12CommandList::PushConstants` blindly called both Graphics and Compute root constant slot `0`.

At the same time:
- Existing engine runtime rendering and tests rely on MorbiEngine's global bindless architecture (Vulkan Set 0 combining `t0/space0`, `t0/space1`, `s0/space0`, `b1/space0` + 128B push constants; DX12 `35`-DWORD Root Signature with 32 DWORD Root Constants `b0/space0`, Bindless SRV Table, Direct Root CBV `b1/space0`, and Static Sampler `s0/space0`).
- `Morbi.RHI` cannot depend on `Morbi.Shader`, `DXC`, or `SPIRV-Reflect`, nor expose Vulkan or DirectX 12 SDK types in public RHI headers.

---

## Decision

1. **Dependency Direction (`Morbi.Shader -> Morbi.RHI`, never `RHI -> Shader`):**
   - Swapped subdirectory order in root [CMakeLists.txt](../../../CMakeLists.txt#L40-L41) (`RHI` before `Shader`) and linked `Morbi::RHI` (`PUBLIC`) in [Shader/CMakeLists.txt](../../../Engine/Source/Shader/CMakeLists.txt). `Morbi.RHI` remains strictly dependent on `Morbi::Core` and `Morbi::Platform` only.

2. **Backend-Agnostic RHI Pipeline Layout Model ([PipelineLayout.hpp](../../../Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp), [PipelineLayout.cpp](../../../Engine/Source/RHI/src/PipelineLayout.cpp)):**
   - Added `PipelineLayoutHandle` (generational pool handle via `PipelineLayoutTag`), `PipelineLayoutMode` (`Standalone`, `GlobalBindlessHybrid`), `PipelineBindingLayout`, `DescriptorSetLayoutDesc`, `StaticSamplerDesc`, `PipelinePushConstantRange`, `PipelineConstantBufferLayout`, `PipelineLayoutDesc`, `PipelineLayoutLimits`, `PipelineLayoutStats`, and `PipelineLayoutValidator`.
   - `PipelineLayoutValidator` implements canonical `Normalize`, `ComputeCanonicalHash` (FNV-1a), structural `AreStructurallyEqual`, and backend-aware `Validate` (enforcing DX12 64-DWORD root signature cap, rejecting `CombinedImageSampler` on DX12/portable targets, checking Vulkan per-stage/set limits, enforcing enabled bindless features, and preventing duplicate/overlapping bindings or DXIL register collisions).
   - Added optional `PipelineLayoutHandle PipelineLayout = PipelineLayoutHandle::Invalid()` to `GraphicsPipelineDesc` and `ComputePipelineDesc`, preserving backward compatibility by resolving `Invalid()` to each device's default bindless layout.

3. **Shader Reflection Converter ([PipelineLayoutBuilder.hpp](../../../Engine/Source/Shader/include/Morbi/Shader/PipelineLayoutBuilder.hpp), [PipelineLayoutBuilder.cpp](../../../Engine/Source/Shader/src/PipelineLayoutBuilder.cpp)):**
   - Implemented `BuildFromReflection` and `BuildFromDualBackendReflections`, enforcing cross-stage VS+PS interface/binding/push-constant merging, `CrossBackendBindingPolicy` explicit register-space-to-descriptor-set mapping, DXIL Root Constant unwrapping into `PipelinePushConstantRange`, and `GlobalBindlessHybrid` Set 0 slot validation/absorption.

4. **Collision-Safe Canonical Hash Cache & Lifetime Reference Counting (`NullDevice`, `VulkanDevice`, `DX12Device`):**
   - Device pipeline layout caches map `CanonicalHash -> std::vector<PipelineLayoutHandle>` and compare `PipelineLayoutValidator::AreStructurallyEqual` on every candidate—never equating 64-bit hash equality with structural equality.
   - Cache hits increment `CacheRefCount`; `DestroyPipelineLayout` only enqueues deferred destruction once `CacheRefCount` drops to zero.
   - `CreateGraphicsPipeline` / `CreateComputePipeline` reject stale/`PendingDestroy` `PipelineLayoutHandle`s and increment `PipelineRefCount` on the layout (decrementing on PSO destroy and propagating `LastUsedTimelineValue`/`LastUsedFenceValue`).
   - `ICommandList::TrackPipeline` also tracks `TrackPipelineLayout` (`UnsubmittedRefCount`), so deferred destruction waits until `PipelineRefCount == 0 && UnsubmittedRefCount == 0 && LastUsedTimeline <= completedTimeline`.
   - `IsResourceAlive(PipelineLayoutHandle)` consistently reflects underlying pool/native residency until final deferred reclamation.

5. **Vulkan 1.3 & DirectX 12 Backend Implementations ([VulkanDevice.cpp](../../../Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp), [DX12Device.cpp](../../../Engine/Source/RHI.DX12/src/DX12Device.cpp)):**
   - **Vulkan 1.3:** Queries `VkPhysicalDeviceFeatures2` (`Vulkan12`/`13`) + `VkPhysicalDeviceProperties2` (`limits` + `Vulkan12Properties`), creates immutable `VkSampler`s for static samplers, fills skipped set indices with a shared empty `VkDescriptorSetLayout`, updates `VulkanCommandList::SetPipeline` to bind the pipeline's `VkPipelineLayout` and conditionally bind `m_bindlessSet`, and uses the layout's actual `PushConstantStageMask` + byte cap in `PushConstants`.
   - **DX12:** Splits CBV/SRV/UAV ranges and Sampler ranges per set into separate `D3D12_DESCRIPTOR_RANGE` tables (+1 DWORD each), emits Root CBVs/SRVs/UAVs for `PreferRootDescriptorOnDx12` (+2 DWORDs), emits `D3D12_STATIC_SAMPLER_DESC` (0 DWORDs), enforces `<= 64` DWORDs pre-serialization, and updates `DX12CommandList::SetPipeline` / `PushConstants` to switch Graphics/Compute root signatures, re-bind the global bindless table when switching, and push to the active bind point's `RootConstantParamIndex`.

6. **Deferred Out of WP3 Scope (Explicitly Left Untouched or Proposed Separately):**
   - Dynamic non-bindless descriptor set allocation (`AllocateDescriptorSet` / `BindDescriptorSet` API) is deferred beyond WP3 and recorded as a follow-up backlog proposal (`BL-RHI-004`).
   - Configurable vertex input layout (`BL-RHI-002` → WP4), PSO cache (`BL-RHI-001` → WP6), and inline HLSL extraction (`BL-SHD-006` → WP7/Phase 3) remain deferred per roadmap.

---

## Consequences

- Pipelines on both Vulkan 1.3 and DirectX 12 can now use either custom reflected/standalone layouts or `GlobalBindlessHybrid` without breaking any Phase 1 or Phase 2 runtime code.
- Hardware/feature limits, hash-collision safety, and GPU timeline/fence lifetime safety are enforced uniformly across `Null`, `Vulkan 1.3`, and `DirectX 12`.
