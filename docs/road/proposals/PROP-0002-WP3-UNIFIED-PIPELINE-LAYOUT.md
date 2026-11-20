# PROP-0002: Phase 2 WP3 — Unified Pipeline Layout Architecture

**Status:** `IMPLEMENTED` (Phase 2 WP3 Complete — Accepted in [ADR-0003](../decisions/ADR-0003-unified-pipeline-layout.md))  
**Target Phase / WP:** Phase 2 WP3 — Unified Pipeline Layout  
**Date:** 2026-11-18  
**Dependencies:** Phase 2 WP1 (`COMPLETE`), Phase 2 WP2 (`COMPLETE`)

---

## 1. Objective & Architectural Scope

Design a backend-independent **Unified Pipeline Layout** abstraction converting `Shader::ShaderReflectionData` (single compute shader or merged VS+PS graphics shaders) into RHI pipeline layout descriptors validated against **Vulkan 1.3** (`VkDescriptorSetLayout` + `VkPipelineLayout`) and **DirectX 12** (`D3D12_ROOT_SIGNATURE_DESC`), without breaking MorbiEngine's existing global bindless runtime shaders.

### Strict Scope Boundaries
- **In Scope (WP3):** Pipeline layout types & handles (`Morbi.RHI`), `ShaderReflectionData -> PipelineLayoutDesc` builder & cross-stage merger (`Morbi.Shader`), layout validation (`PipelineLayoutValidator` checking DX12 64-DWORD budget, Vulkan limits, overlap/collision, unsupported resource kinds), backend `CreatePipelineLayout`/`DestroyPipelineLayout` + canonical hash caching (`VulkanDevice`, `DX12Device`), safe PSO/CommandList integration (`GraphicsPipelineDesc::PipelineLayout`, `ComputePipelineDesc::PipelineLayout`, dynamic root/push-constant stage resolution & root signature switching).
- **Out of Scope (Deferred to WP4–WP7):**
  - Configurable `VertexInputLayoutDesc` (60B/92B stride replacement) → **WP4** (`BL-RHI-002`).
  - Dynamic pipeline raster/blend/depth states & entry-point names (`pName`) → **WP4 & WP5**.
  - Persistent `VkPipelineCache` / `ID3D12PipelineLibrary1` → **WP6** (`BL-RHI-001`).
  - Inline HLSL extraction (`EngineRuntime.cpp`) → **WP7 / Phase 3** (`BL-SHD-006`).

---

## 2. Repository Source Audit Findings

### 2.1 Existing Vulkan 1.3 Layout (`Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp`)
- Single device-wide `m_bindlessLayout` + `m_pipelineLayout` (`lines 2621–2744`):
  - **Descriptor Set 0 (`UPDATE_AFTER_BIND_POOL` + `PARTIALLY_BOUND`):**
    - `binding 0`: `STORAGE_BUFFER[4096]` (`t0, space0`)
    - `binding 1`: `SAMPLED_IMAGE[4096]` (`t0, space1`)
    - `binding 2`: `SAMPLER[1]` (`s0, space0`, immutable/pre-written linear sampler)
    - `binding 3`: `UNIFORM_BUFFER[256]` (`b1, space0`)
  - **Push Constants:** 1 range `[0, 128)` (`VK_SHADER_STAGE_ALL`).
- **Identified Hazards When Adding Per-Pipeline Layouts:**
  1. `VulkanCommandList::SetPipeline()` (`lines 3277–3280`) unconditionally binds `m_pipelineLayout` and `m_bindlessSet` even if a pipeline has no Set 0 or uses a custom layout. In WP3, `SetPipeline` must read the pipeline's bound `PipelineLayoutHandle` and only bind `m_bindlessSet` when that layout includes the global bindless set (or is the default bindless layout).
  2. `VulkanCommandList::PushConstants()` (`lines 656–662`) hardcodes `m_pipelineLayout` and `VK_SHADER_STAGE_ALL`. Vulkan spec requires `vkCmdPushConstants` `stageFlags` to include the `stageFlags` of all ranges overlapping `[offset, offset + size)`. Custom layouts with `VERTEX_BIT`-only or `VERTEX_BIT | FRAGMENT_BIT` push constant ranges require resolving the active `VkPipelineLayout` + combined push constant `VkShaderStageFlags` from the active pipeline/layout!
  3. `VkPhysicalDeviceProperties::limits` (`chosenProps` at `line 836`) is currently discarded after copying `deviceName`. WP3 must retain `m_deviceLimits` (and query `VkPhysicalDeviceDescriptorIndexingProperties` via Vulkan 1.2/1.3 properties) to validate real device bounds.

### 2.2 Existing DirectX 12 Layout (`Engine/Source/RHI.DX12/src/DX12Device.cpp`)
- Single device-wide `m_rootSignature` (`lines 1883–1956`) + shader-visible CBV/SRV/UAV heap `m_bindlessHeap` (`8448` descriptors = `4096 + 4096 + 256`):
  - **Slot 0 (`32` DWORDs):** `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS` (`b0, space0`, 32 values).
  - **Slot 1 (`1` DWORD):** `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE` (`t0..t4095, space0` + `t0..t4095, space1`).
  - **Slot 2 (`2` DWORDs):** `D3D12_ROOT_PARAMETER_TYPE_CBV` (`b1, space0`).
  - **Static Sampler (`0` DWORDs):** `s0, space0` (`D3D12_FILTER_MIN_MAG_MIP_LINEAR`, wrap).
  - **Total Current Root Signature Cost:** `32 + 1 + 2 + 0 = 35 DWORDs` (`<= 64` DWORD limit).
- **Identified Hazards When Adding Per-Pipeline Layouts:**
  1. `DX12CommandList::Begin()` (`lines 2151–2159`) binds `m_rootSignature` once; `DX12CommandList::SetPipeline()` (`lines 2356–2365`) calls `SetPipelineState` without calling `SetGraphicsRootSignature` / `SetComputeRootSignature`. Changing to a PSO whose root signature differs from the active root signature is invalid in D3D12 without re-setting the root signature (which also invalidates active root parameters!). `DX12CommandList` must track `m_activeGraphicsRootSig` / `m_activeComputeRootSig` + `m_activeLayout` and re-bind root signature + default bindless table (if hybrid/default) when switching between distinct root signatures.
  2. `DX12CommandList::PushConstants()` (`lines 366–369`) blindly calls BOTH `SetGraphicsRoot32BitConstants(0, ...)` and `SetComputeRoot32BitConstants(0, ...)`. For custom root signatures, root constants may live at a resolved `RootParamIndex` (or only on Graphics vs. Compute). `DX12CommandList` must dispatch only to the active pipeline bind point (Graphics or Compute) using the active layout's `RootConstantParamIndex`.
  3. DX12 separates `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` and `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER`: **samplers cannot appear inside a CBV/SRV/UAV descriptor table**. Any descriptor set containing both textures/buffers and dynamic samplers must generate separate DX12 root descriptor tables (`CBV/SRV/UAV` table vs. `Sampler` table), whereas static/immutable samplers map to `D3D12_STATIC_SAMPLER_DESC` (`0 DWORD` cost).

### 2.3 Module Dependency Graph (`CMakeLists.txt`)
- `Morbi.Core` → `Morbi.IO`, `Morbi.Platform`, `Morbi.Shader`
- `Morbi.RHI` → depends ONLY on `Morbi::Core`, `Morbi::Platform` (**zero dependency on `Morbi.Shader`**)
- `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12` → depend on `Morbi::RHI`
- `Morbi.Shader` does not currently depend on `Morbi::RHI`.

**Design Decision:** Keep `Morbi.RHI` completely free of `Morbi.Shader` / `DXC` / `SPIRV-Reflect` and free of `<vulkan/vulkan.h>` / `<d3d12.h>`!
- Put core layout types, validation logic (`PipelineLayoutValidator`), and `IDevice` / `ICommandList` interfaces in **`Engine/Source/RHI/include/Morbi/RHI/PipelineLayout.hpp`** (implemented in `Morbi.RHI`).
- Put the reflection converter `Shader::PipelineLayoutBuilder` (`BuildFromReflection(...)`) in **`Morbi.Shader`** linking **`Morbi::RHI` `PRIVATE` or `PUBLIC`** (no cycle: `Morbi.RHI` sits parallel/below and never links `Morbi.Shader`!). Wait — linking `Morbi::RHI` `PUBLIC` to `Morbi.Shader` cleanly exposes `Core::Result<RHI::PipelineLayoutDesc, std::string> PipelineLayoutBuilder::BuildFromReflection(...)` to `EngineRuntime`, `Render`, and tests without inflating `Morbi.RHI` dependencies.

---

## 3. Unified Data Model (`Morbi::RHI::PipelineLayout.hpp`)

```cpp
struct PipelineLayoutTag {};
using PipelineLayoutHandle = Core::GenerationalHandle<PipelineLayoutTag>;

enum class ShaderVisibility : u32
{
    None        = 0,
    Vertex      = 1u << 0,
    Pixel       = 1u << 1,
    Compute     = 1u << 2,
    AllGraphics = Vertex | Pixel,
    All         = Vertex | Pixel | Compute
};

enum class DescriptorType : u8
{
    UniformBuffer = 0,
    StorageBuffer,
    RWStorageBuffer,
    SampledTexture,
    StorageTexture,
    Sampler,
    CombinedImageSampler,   // Validated carefully: rejected on DX12 unless explicitly handled
    AccelerationStructure
};

enum class PipelineLayoutMode : u8
{
    Standalone = 0,         // Derived strictly from declared DescriptorSets & PushConstants
    GlobalBindlessHybrid    // Reserves/mirrors Set 0 / Root Params 0..2 + StaticSampler s0 for Morbi Bindless Heap
};
```

### Core Descriptors
1. **`PipelineBindingLayout`:**
   - `Name` (`std::string`), `Type` (`DescriptorType`)
   - `Set` (`u32`), `Binding` (`u32`) *(Vulkan coordinate; always explicit)*
   - `DxilRegister` (`std::optional<u32>`), `DxilSpace` (`std::optional<u32>`) *(DX12 coordinate; defaults to `Binding`/`Set` if not overridden by explicit rule)*
   - `Count` (`u32`, `0xFFFFFFFFu` for unbounded), `IsUnboundedArray` (`bool`), `IsReadOnly` (`bool`), `IsComparisonSampler` (`bool`)
   - `PreferRootDescriptorOnDx12` (`bool`, e.g., `true` for per-object single CBV `b1, space0` matching existing DX12 Root CBV without forcing table allocation!)
   - `Visibility` (`ShaderVisibility`)
2. **`DescriptorSetLayoutDesc`:**
   - `SetIndex` (`u32`)
   - `AllowUpdateAfterBind` (`bool`)
   - `Bindings` (`std::vector<PipelineBindingLayout>`)
3. **`StaticSamplerDesc`:**
   - `Name`, `Set`, `Binding`, `DxilRegister`, `DxilSpace`, `Visibility`, `Filter`, `AddressMode`, `Comparison` — maps to `VkSampler` (`pImmutableSamplers`) on Vulkan and `D3D12_STATIC_SAMPLER_DESC` (`0 DWORD` cost) on DX12!
4. **`PipelinePushConstantRange`:**
   - `Name`, `OffsetBytes` (4B aligned), `SizeBytes` (4B aligned, sum <= 128B), `Visibility`, `DxilRegister` (`std::optional<u32>`, default `0`), `DxilSpace` (`std::optional<u32>`, default `0`)
5. **`PipelineConstantBufferLayout`:**
   - Metadata tracking reflected CBVs (`Name`, `Set`, `Binding`, `DxilRegister`, `DxilSpace`, `SizeBytes`, `MemberCount`, `Visibility`, `UseRootDescriptor`) for downstream WP4/WP5 material/pass validation.
6. **`PipelineLayoutDesc`:**
   - `Mode` (`Standalone` vs `GlobalBindlessHybrid`)
   - `Sets` (`std::vector<DescriptorSetLayoutDesc>`)
   - `PushConstants` (`std::vector<PipelinePushConstantRange>`)
   - `ConstantBuffers` (`std::vector<PipelineConstantBufferLayout>`)
   - `StaticSamplers` (`std::vector<StaticSamplerDesc>`)
   - `DebugName` (`std::string`)
   - `ComputeCanonicalHash()` (`u64` deterministic FNV-1a over normalized sets, bindings, ranges, samplers, and mode)

---

## 4. Vulkan 1.3 vs. DirectX 12 Mapping Matrix

| Concept | Unified RHI (`PipelineLayoutDesc`) | Vulkan 1.3 Backend | DirectX 12 Backend |
|---|---|---|---|
| Push / Root Constants | `PipelinePushConstantRange` (`Offset=0`, `Size<=128B`) | `VkPushConstantRange` (`offset`, `size`, `stageFlags`) | `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS` (`b{reg}, space{space}`, `(Size+3)/4` DWORDs) |
| Single Fast CBV (`PreferRootDescriptorOnDx12=true`) | `DescriptorType::UniformBuffer`, `Count=1` | `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` at `(Set, Binding)` | `D3D12_ROOT_PARAMETER_TYPE_CBV` (`b{reg}, space{space}`, **2 DWORDs**) |
| CBV/SRV/UAV Descriptor Table / Array | `DescriptorType::{UniformBuffer, StorageBuffer, RWStorageBuffer, SampledTexture, StorageTexture}` | `VkDescriptorSetLayoutBinding` at `(Set, Binding)` (`PARTIALLY_BOUND` / `UPDATE_AFTER_BIND` if `AllowUpdateAfterBind`) | Grouped per `(Set, Visibility?)` into `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE` (**1 DWORD** per table) of `D3D12_DESCRIPTOR_RANGE` (`CBV`/`SRV`/`UAV`) |
| Dynamic Sampler Array / Binding | `DescriptorType::Sampler` | `VK_DESCRIPTOR_TYPE_SAMPLER` in `VkDescriptorSetLayoutBinding` | Split into separate Sampler `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE` (**1 DWORD**) (`D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER`) |
| Static / Immutable Sampler | `StaticSamplerDesc` | `VK_DESCRIPTOR_TYPE_SAMPLER` with `pImmutableSamplers = &vkSampler` | `D3D12_STATIC_SAMPLER_DESC` (**0 DWORDs**) |
| Combined Image+Sampler | `DescriptorType::CombinedImageSampler` | Native `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` | **Unsupported directly** — `PipelineLayoutValidator` fails DX12 generation unless split into `SampledTexture` + `Sampler` (`CombinedImageSampler` caught cleanly) |
| Global Bindless Hybrid Mode | `PipelineLayoutMode::GlobalBindlessHybrid` | Set 0 pinned to `m_bindlessLayout` / `m_bindlessSet`; Sets `1..K` custom | Root Params `0..2` + StaticSampler `s0` pinned to Morbi Bindless Root Params + `m_bindlessHeap`; additional sets/tables follow |

---

## 5. Limit & Cost Model Analysis (`PipelineLayoutValidator`)

### 5.1 DirectX 12 Root Signature Cost Budget (Max = `64` DWORDs)
1. **32-bit Root Constants:** `ceil(SizeBytes / 4)` DWORDs (max `32` DWORDs for `128B` portable ceiling).
2. **Root CBV / SRV / UAV (`PreferRootDescriptorOnDx12`):** `2` DWORDs per descriptor.
3. **Descriptor Table:** `1` DWORD per table (CBV/SRV/UAV contiguous ranges share 1 table per set; Sampler ranges require a separate `1` DWORD table per set!).
4. **Static Sampler:** `0` DWORDs.
- Validator computes `EstimatedDx12Dwords`; if `> Limits.MaxRootSignatureDwords` (`64`), `Validate()` fails BEFORE calling `D3D12SerializeRootSignature`.

### 5.2 Vulkan 1.3 Hardware Limits
- `Sets.size() <= Limits.MaxBoundDescriptorSets` (portability baseline warning if `> 4`; hard error if `> deviceLimits.maxBoundDescriptorSets`).
- Total Push Constant span `max(Offset + Size) <= std::min(128u, Limits.MaxPushConstantsBytes)` (portability baseline `128B`; hard error if exceeding `deviceLimits.maxPushConstantsSize` or `128B` cross-backend cap).
- Per-stage descriptor counts (excluding `UPDATE_AFTER_BIND` sets checked against `VkPhysicalDeviceDescriptorIndexingProperties` vs non-bindless sets checked against `VkPhysicalDeviceLimits::maxPerStageDescriptor*` and `maxPerStageResources`).
- Alignment & Collision rules:
  - Duplicate `(Set, Binding)` or duplicate `(DxilClass, DxilSpace, DxilRegister)` rejected.
  - Overlapping or unaligned (`% 4 != 0`) Push Constant ranges rejected.
  - Unbounded arrays in DX12 descriptor tables validated cleanly (at most one unbounded range at end of a table, or `AllowUpdateAfterBind` required on Vulkan + `VK_EXT_descriptor_indexing` feature active).

---

## 6. Why We Preserve Hybrid Global Bindless Coexistence Rather Than Rewriting Everything

Replacing `VulkanDevice::m_bindlessLayout` and `DX12Device::m_rootSignature` wholesale in WP3 would break:
1. All 4 runtime shaders (`kUnifiedHlslShader`, `kScenePbrMeshHlslShader`, `kGameUIProceduralHlslShader`, `kGameUIOverlayHlslShader`) which assume `BindlessResourceIndex` maps directly into `Set 0` (Vulkan) and `m_bindlessHeap` (DX12).
2. `MorbiRHIDescriptorTests` and `MorbiRHIGenerationalHandleHashTests` (1,367 RHI assertions).

Instead:
- **`PipelineLayoutMode::GlobalBindlessHybrid`** keeps Set 0 (`m_bindlessSet` / `m_bindlessHeap`) zero-overhead and binary-compatible with existing shaders while letting pipelines declare exact push-constant stage visibility / byte sizes and optional Set 1+ pass/material tables.
- **`PipelineLayoutMode::Standalone`** generates completely self-contained `VkDescriptorSetLayout[]` / `VkPipelineLayout` and `ID3D12RootSignature*` purely from `ShaderReflectionData` — validating our unified layout compiler end-to-end across both Vulkan 1.3 and DX12 without altering a single working runtime shader!

---

## 7. Follow-Up Note on Standalone Descriptor Set Binding (`WP4 / WP5` Interface)

WP3 handles **`ShaderReflectionData` → `PipelineLayoutHandle` → `VkPipelineLayout` / `ID3D12RootSignature*` → `CreateGraphicsPipeline` / `CreateComputePipeline` + `PushConstants` + Root/Bindless `BindConstantBuffer`**.  
Explicitly allocating and binding custom *per-pass / per-material non-bindless* `VkDescriptorSet`s or DX12 descriptor table slices (`BindDescriptorSet` / `ResourceSet`) requires runtime descriptor-set management and frame-ring descriptor allocation, which cleanly pairs with **WP4 (`Graphics Pipeline`)** and **WP5 (`Compute Pipeline`)** (or can be introduced there cleanly using WP3's `DescriptorSetLayoutDesc`). No hidden partial hack will be added in WP3.
