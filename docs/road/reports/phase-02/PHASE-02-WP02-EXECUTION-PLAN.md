# Phase 2 WP2 — Binary Reflection: Research & Execution Plan

**Work Package:** Phase 2 WP2 — Binary Reflection  
**Status:** `COMPLETE` (Implemented incrementally WP2.1–WP2.8; see [PHASE-02-WP02-BINARY-REFLECTION.md](PHASE-02-WP02-BINARY-REFLECTION.md))  
**Date:** 2026-11-18  
**Dependencies:** Phase 2 WP1 (`COMPLETE`)  
**Companion Architecture Proposal:** [PROP-0001-WP2-REFLECTION-ARCHITECTURE.md](../../proposals/PROP-0001-WP2-REFLECTION-ARCHITECTURE.md)

---

## 1. Current Architecture Audit

### Inspected Source Files & Toolchain Artifacts
- **Shader Subsystem:**
  - [ShaderCompiler.hpp](../../../../Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp)
  - [ShaderCompiler.cpp](../../../../Engine/Source/Shader/src/ShaderCompiler.cpp) (lines 1201–1285, 1494–1513)
  - [DxcRuntimeBridge.hpp](../../../../Engine/Source/Shader/src/DxcRuntimeBridge.hpp)
  - [ShaderCache.hpp](../../../../Engine/Source/Shader/include/Morbi/Shader/ShaderCache.hpp) & [ShaderCache.cpp](../../../../Engine/Source/Shader/src/ShaderCache.cpp)
- **RHI & Backends:**
  - [RHITypes.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/RHITypes.hpp) (`GraphicsPipelineDesc`, `ComputePipelineDesc`, `PushConstantsDesc`)
  - [ICommandList.hpp](../../../../Engine/Source/RHI/include/Morbi/RHI/ICommandList.hpp) (`BindConstantBuffer`, `PushConstants`)
  - [RHIDevice.cpp](../../../../Engine/Source/RHI/src/RHIDevice.cpp)
  - [VulkanDevice.cpp](../../../../Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp) (lines 652–683, 879–889, 1553–1609, 1875–1904, 1975–2199, 2621–2744, 3258–3336)
  - [DX12Device.cpp](../../../../Engine/Source/RHI.DX12/src/DX12Device.cpp) (lines 359–370, 1487–1599, 1883–1956, 2146–2161, 2356–2408)
- **Asset & Runtime Consumers:**
  - [ModelImporter.hpp](../../../../Engine/Source/Asset/include/Morbi/Asset/ModelImporter.hpp) (`StaticMeshVertex` 92-byte layout)
  - [EngineRuntime.cpp](../../../../Engine/Source/Runtime/src/EngineRuntime.cpp) (lines 87–415, 978–1028, 1154–1211, 2245–2368, 4920–5000)
  - [GenerationalHandleHashTests.cpp](../../../../Tests/RHITests/GenerationalHandleHashTests.cpp) (lines 108–136)
- **SDK / Compiler Headers & Empirical Probes:**
  - Windows SDK `10.0.26100.0\um\dxcapi.h`, `d3d12shader.h`, `d3dcommon.h`
  - `legacy/FlueperEngine/Legacy/FlueEngine/tools/ShaderCompiler/win64/dxc.exe` (`dxcompiler.dll` `1.9.2602.24`), `spirv-dis.exe`, `spirv-val.exe`

### Current Reflection Consumers
1. `ShaderCompiler::Compile()` (`ShaderCompiler.cpp:1508`) calls `ReflectSourceAndBinary(request, bytecode)` and stores the result in `CompiledShaderArtifact::Reflection`.
2. `ShaderCache` (`ShaderCache.cpp:183–285`) serializes/deserializes `ShaderReflectionData` in `.mshc` v2 files.
3. `EngineRuntime.cpp:1205–1206` logs `outPS.Reflection.Bindings.size()` and `outPS.Reflection.PushConstantSizeBytes` in `VerifyPhase2ShaderPipeline()`.

### Current RHI Pipeline & Binding Mechanics
- **Vulkan 1.3 (`VulkanDevice.cpp`):**
  - Single global `VkPipelineLayout` (`m_pipelineLayout`) with **Descriptor Set 0** (`binding 0`: `STORAGE_BUFFER[4096]`, `binding 1`: `SAMPLED_IMAGE[4096]`, `binding 2`: `SAMPLER[1]`, `binding 3`: `UNIFORM_BUFFER[256]`) + 128-byte `VkPushConstantRange` (`VK_SHADER_STAGE_ALL`).
  - `CreateGraphicsPipeline` hardcodes vertex input layouts for `VertexStrideBytes == 60` (5 attrs) and `92` (7 attrs), and hardcodes entry points `"VSMain"` / `"PSMain"` (`"CSMain"` for compute).
  - `BindConstantBuffer` updates `binding 3` element (`rec->Bindless.Index`) for `UNIFORM_BUFFER` slots.
- **DirectX 12 (`DX12Device.cpp`):**
  - Single global `ID3D12RootSignature` (`m_rootSignature`):
    - Root Param 0: 32 DWORDs (128B) Root Constants (`b0, space0`, `SHADER_VISIBILITY_ALL`)
    - Root Param 1: Descriptor Table (`t0, space0` `[0..4095]` + `t0, space1` `[4096..8191]`)
    - Root Param 2: Direct Root CBV (`b1, space0`)
    - Static Sampler: `s0, space0`
  - `CreateGraphicsPipeline` hardcodes `5`-element (`60B`) and `7`-element (`92B`) `D3D12_INPUT_ELEMENT_DESC` arrays.
  - `BindConstantBuffer(2, ...)` calls `SetGraphicsRootConstantBufferView` / `SetComputeRootConstantBufferView`.

---

## 2. Existing Reflection Limitations

`ShaderCompiler::ReflectSourceAndBinary` (`ShaderCompiler.cpp:1201–1285`) has critical defects:
1. **Ignores Bytecode:** Ends with `(void)bytecode;` and runs `std::regex` over unpreprocessed `request.SourceCode`.
2. **Broken Under Preprocessor / Includes:** Fails to evaluate `#ifdef MORBI_VULKAN`, `#ifdef MORBI_DX12`, shader macros (`request.Defines`), or `#include`d resources/cbuffers; also matches commented-out or dead code.
3. **Incorrect Hardcoded Push Constant Size:** `PushConstantSizeBytes = 64` hardcoded whenever `"push_constant"` or `"PushConstants"` substring appears, whereas `MeshDrawPushConstants` and both backends use **128 bytes** (and `GameUIOverlay` uses 32 bytes).
4. **No Constant Buffer Layout Reflection:** Zero member names, offsets, sizes, array elements, matrix strides/majorness, or total CB sizes.
5. **No Vertex Input / Stage I/O Reflection:** Zero input/output signature locations, semantics, formats, or component counts.
6. **No Descriptor Set / Binding vs. Register / Space Distinction:** Regex captures only `: register(btus...)`, ignoring `[[vk::binding(b, s)]]` and Vulkan descriptor sets.
7. **No Cross-Stage or Cross-Backend Validation:** Cannot detect VS->PS interface mismatches or SPIR-V vs. DXIL layout divergences.

---

## 3. Library / API Comparison

Detailed comparison is documented in [PROP-0001-WP2-REFLECTION-ARCHITECTURE.md](../../proposals/PROP-0001-WP2-REFLECTION-ARCHITECTURE.md):
- **SPIR-V:** **Khronos `SPIRV-Reflect`** (Apache-2.0, 2 files, zero external deps, no exceptions, full Vulkan 1.3 & `SPV_EXT_descriptor_indexing` support) selected over `SPIRV-Cross` (heavy transpiler AST, exceptions, redundant CMake footprint).
- **DXIL:** **DXC `IDxcContainerReflection` + `ID3D12ShaderReflection`** (zero new binary dependencies — already in `dxcompiler.dll` / `libdxcompiler.so`; empirically verified `STAT` 1500B + `DXIL` parts emitted by default in `dxcompiler.dll` `1.9.2602.24`).
- **Critical Vulkan 1.3 Extension Finding:** Probed `dxc.exe -fspv-reflect` and confirmed it injects `OpExtension "SPV_GOOGLE_hlsl_functionality1"`. Because `VulkanDevice.cpp` enables only `VK_KHR_SWAPCHAIN_EXTENSION_NAME`, runtime SPIR-V must **not** include `-fspv-reflect`; `SPIRV-Reflect` extracts bindings, CB layouts, push constants, locations, formats, `LocalSize`, and unstripped `OpName` semantics directly from clean Vulkan 1.3 SPIR-V.

---

## 4. Recommended Architecture

```text
                       CompiledShaderArtifact (DXIL or SPIR-V)
                                        │
                 ┌──────────────────────┴──────────────────────┐
                 ▼                                             ▼
     [DxilShaderReflector]                           [SpirvShaderReflector]
   (IDxcContainerReflection +                        (Khronos SPIRV-Reflect)
    ID3D12ShaderReflection)                                    │
                 │                                             │
                 └──────────────────────┬──────────────────────┘
                                        ▼
                       Unified ShaderReflectionData
            (Bindings, ConstantBuffers, PushConstants, Stage I/O,
             Compute LocalSize, Explicit Optional Set/Register IDs)
                                        │
                 ┌──────────────────────┴──────────────────────┐
                 ▼                                             ▼
    ValidateStageInterface()                  ValidateCrossBackendCompatibility()
     (VS -> PS / HS -> DS)                    (SPIR-V <-> DXIL + BindingPolicy)
```

### Architectural Invariants
1. **Strict Separation:** `SpirvShaderReflector` and `DxilShaderReflector` live in private implementation files (`Engine/Source/Shader/src/`); no SPIRV-Reflect or D3D12 headers leak into public headers (`ShaderCompiler.hpp` or `Morbi.Render` / `Morbi.RHI`).
2. **Zero Fabrication:** SPIR-V populates `DescriptorSet` & `Binding` (leaving `RegisterSlot`/`RegisterSpace` as `std::nullopt`); DXIL populates `RegisterSlot` & `RegisterSpace` (leaving `DescriptorSet`/`Binding` as `std::nullopt`). Missing semantic names (e.g., stripped SPIR-V) stay empty rather than guessed.
3. **Thread Safety:** `DxilShaderReflector` uses `DxcModuleContext::CreateContainerReflection()` (`CLSID_DxcContainerReflection` per call or pooled alongside `WorkerContext`) so reflection is thread-safe and respects COM/DLL unload ordering. `SPIRV-Reflect` creates/destroys `SpvReflectShaderModule` on stack per call (100% thread-safe).
4. **Failure Reporting:** Return `Core::Result<ShaderReflectionData, std::string>` from binary reflection APIs (`ReflectBytecode(...)`) so corrupt/unsupported binaries produce explicit errors; `Compile()` fails cleanly if binary reflection fails on freshly compiled bytecode, while keeping `ReflectSourceAndBinary` working (or deprecating its regex path) cleanly.

---

## 5. Unified `ShaderReflectionData` Design Draft

```cpp
namespace Morbi::Shader
{
    inline constexpr u32 kUnboundedDescriptorCount = 0xFFFFFFFFu;

    enum class ReflectedResourceKind : u8
    {
        Unknown = 0,
        UniformBuffer,
        StorageBuffer,
        RWStorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
        AccelerationStructure,
        PushConstants
    };

    enum class ReflectedResourceDimension : u8
    {
        Unknown = 0,
        Buffer,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D,
        TextureCube,
        TextureCubeArray
    };

    enum class ReflectedScalarType : u8
    {
        Unknown = 0,
        Bool,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float16,
        Float32,
        Float64,
        Struct
    };

    enum class ReflectedVariableClass : u8
    {
        Unknown = 0,
        Scalar,
        Vector,
        MatrixRows,
        MatrixColumns,
        Struct
    };

    enum class ReflectedShaderVisibility : u32
    {
        None    = 0,
        Vertex  = 1u << 0,
        Pixel   = 1u << 1,
        Compute = 1u << 2,
        All     = Vertex | Pixel | Compute
    };

    struct ReflectedShaderMember
    {
        std::string Name;
        u32 OffsetBytes = 0;
        u32 SizeBytes = 0;
        u32 PaddedSizeBytes = 0;
        ReflectedVariableClass Class = ReflectedVariableClass::Unknown;
        ReflectedScalarType ScalarType = ReflectedScalarType::Unknown;
        u32 Rows = 1;
        u32 Columns = 1;
        u32 ArrayElementCount = 0; // 0 = non-array
        u32 ArrayStrideBytes = 0;
        u32 MatrixStrideBytes = 0;
        bool IsRowMajor = false;
        std::vector<ReflectedShaderMember> Members;
    };

    struct ReflectedConstantBuffer
    {
        std::string Name;
        u32 SizeBytes = 0;
        std::optional<u32> DescriptorSet;
        std::optional<u32> Binding;
        std::optional<u32> RegisterSlot;
        std::optional<u32> RegisterSpace;
        std::vector<ReflectedShaderMember> Members;
    };

    struct ReflectedPushConstantRange
    {
        std::string Name;
        u32 OffsetBytes = 0;
        u32 SizeBytes = 0;
        ReflectedShaderVisibility Visibility = ReflectedShaderVisibility::None;
        std::vector<ReflectedShaderMember> Members;
    };

    struct ReflectedStageParameter
    {
        std::string Name;
        std::string SemanticName;
        u32 SemanticIndex = 0;
        std::optional<u32> Location;
        std::optional<u32> Register;
        ReflectedScalarType ComponentType = ReflectedScalarType::Unknown;
        u32 ComponentCount = 0; // 1..4
        u8 ComponentMask = 0;   // bitmask 0x1..0xF
        bool IsSystemValue = false;
        std::string SystemValueName;
    };

    struct ReflectedBinding
    {
        std::string Name;
        ResourceBindingType Type = ResourceBindingType::ShaderResource; // Coarse compatibility field
        ReflectedResourceKind Kind = ReflectedResourceKind::Unknown;
        ReflectedResourceDimension Dimension = ReflectedResourceDimension::Unknown;
        u32 RegisterSlot = 0;  // Compatibility alias: DXIL register or Vulkan binding when set
        u32 RegisterSpace = 0; // Compatibility alias: DXIL space or Vulkan descriptor set when set
        u32 Count = 1;         // kUnboundedDescriptorCount = unbounded bindless array
        bool IsUnboundedArray = false;
        bool IsReadOnly = true;
        bool IsComparisonSampler = false;
        std::optional<u32> DescriptorSet;
        std::optional<u32> Binding;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;
        std::optional<u32> ConstantBufferIndex;
        ReflectedShaderVisibility Visibility = ReflectedShaderVisibility::None;
    };

    struct ShaderReflectionData
    {
        ShaderTargetFormat TargetFormat = ShaderTargetFormat::DXIL;
        ShaderStage Stage = ShaderStage::Vertex;
        std::string EntryPoint;
        bool IsValid = false;
        u32 PushConstantSizeBytes = 0;
        u32 ThreadGroupSizeX = 1;
        u32 ThreadGroupSizeY = 1;
        u32 ThreadGroupSizeZ = 1;
        bool UsesBindlessHeap = false;
        std::vector<ReflectedBinding> Bindings;
        std::vector<ReflectedConstantBuffer> ConstantBuffers;
        std::vector<ReflectedPushConstantRange> PushConstants;
        std::vector<ReflectedStageParameter> StageInputs;
        std::vector<ReflectedStageParameter> StageOutputs;
    };
}
```

> **Note on `ReflectedBinding::RegisterSlot`/`RegisterSpace` Compatibility:** Existing consumers (`EngineRuntime.cpp`) read `Bindings.size()` and `PushConstantSizeBytes`. Keeping `std::optional<u32> DescriptorSet/Binding` and `std::optional<u32> DxilRegister/DxilSpace` separate guarantees downstream WP3 (`Unified Pipeline Layout`) and cross-backend validation never confuse Vulkan `(set, binding)` with DX12 `(register, space)`.

---

## 6. Vulkan (SPIR-V) Integration Plan

1. **Module Creation:** `spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY, bytecode.size(), bytecode.data(), &module)`. Always free via RAII scope guard `spvReflectDestroyShaderModule(&module)`.
2. **Stage & Entry Point:** Read `entry_point->shader_stage` and `entry_point->name`; verify against requested stage/entry point.
3. **Descriptor Bindings:** Enumerate `spvReflectEnumerateDescriptorSets` / `spvReflectEnumerateDescriptorBindings`:
   - Map `SpvReflectDescriptorType`:
     - `UNIFORM_BUFFER` / `UNIFORM_BUFFER_DYNAMIC` -> `UniformBuffer`
     - `STORAGE_BUFFER` / `STORAGE_BUFFER_DYNAMIC` -> `StorageBuffer` / `RWStorageBuffer` (inspect `SPV_REFLECT_RESOURCE_FLAG_UAV` / `NonWritable` decoration!)
     - `SAMPLED_IMAGE` -> `SampledTexture`
     - `STORAGE_IMAGE` -> `StorageTexture`
     - `SAMPLER` -> `Sampler`
     - `COMBINED_IMAGE_SAMPLER` -> `CombinedImageSampler`
     - `ACCELERATION_STRUCTURE_KHR` -> `AccelerationStructure`
     - Any unsupported descriptor type (e.g. `INPUT_ATTACHMENT` if unmapped) -> return explicit unsupported-resource diagnostic or map cleanly!
   - Arrays & Bindless: Check `binding->array.dims_count > 0`; if `dims[0] == SPV_REFLECT_ARRAY_DIM_RUNTIME` (`0`), set `IsUnboundedArray = true`, `Count = kUnboundedDescriptorCount`, `UsesBindlessHeap = true`.
   - Record `DescriptorSet = binding->set`, `Binding = binding->binding` (`DxilRegister` & `DxilSpace` remain `std::nullopt`).
   - For `UNIFORM_BUFFER`, recursively walk `binding->block` (`SpvReflectBlockVariable`) to populate `ReflectedConstantBuffer` (`padded_size`, `members`, `offset`, `size`, `array`, `matrix` stride/majorness).
4. **Push Constants:** Enumerate `spvReflectEnumeratePushConstantBlocks`:
   - Populate `PushConstants` (`offset`, `size`, `visibility`, recursive `Members`).
   - Compute `PushConstantSizeBytes` as max `(offset + size)` across push constant blocks (or `0` if none).
5. **Stage I/O (Vertex Inputs & Outputs):** Enumerate `spvReflectEnumerateInputVariables` & `spvReflectEnumerateOutputVariables`:
   - Filter/mark `SPV_REFLECT_BUILT_IN_*` (`BuiltInPosition`, `BuiltInVertexIndex`, etc.) with `IsSystemValue = true`.
   - For non-built-in variables, record `Location = var->location`, `Format`/`ComponentType`/`ComponentCount` from `var->format` / `var->type_description`, and parse semantic from `var->semantic` or DXC `OpName` (`"in.var.POSITION"` / `"out.var.TEXCOORD0"` -> `SemanticName = "POSITION"` / `"TEXCOORD"`, `SemanticIndex = 0`) when present; leave empty if absent.
6. **Compute Thread Group Size:** When `Stage == Compute`, read `entry_point->local_size.{x,y,z}`.

---

## 7. DX12 (DXIL) Integration Plan

1. **Container Loading:** Acquire `IDxcContainerReflection` via `DxcModuleContext::CreateContainerReflection()` (`CLSID_DxcContainerReflection`).
2. **Wrap Bytecode & Find DXIL Part:**
   - Create `IDxcBlobEncoding` via `IDxcUtils::CreateBlobFromPinned(bytecode.data(), size, 0, ...)`.
   - Call `containerRefl->Load(blob)`.
   - Call `FindFirstPartKind(DXC_PART_DXIL, &partIdx)` (fallback `DXC_PART_REFLECTION_DATA` if needed).
   - Call `GetPartReflection(partIdx, IID_ID3D12ShaderReflection, &shaderRefl)`.
3. **Shader & Stage Metadata:** `shaderRefl->GetDesc(&shaderDesc)`:
   - Validate `D3D12_SHVER_GET_TYPE(shaderDesc.Version)` matches expected stage.
   - Read `shaderRefl->GetThreadGroupSize(&x, &y, &z)` for Compute.
4. **Resource Bindings:** Loop `0 .. shaderDesc.BoundResources - 1` calling `GetResourceBindingDesc(i, &bindDesc)`:
   - Record `DxilRegister = bindDesc.BindPoint`, `DxilSpace = bindDesc.Space` (`DescriptorSet` & `Binding` remain `std::nullopt`).
   - Map `bindDesc.Type` (`D3D_SIT_CBUFFER`, `D3D_SIT_TBUFFER`, `D3D_SIT_TEXTURE`, `D3D_SIT_SAMPLER`, `D3D_SIT_UAV_RWTYPED`, `D3D_SIT_STRUCTURED`, `D3D_SIT_UAV_RWSTRUCTURED`, `D3D_SIT_BYTEADDRESS`, `D3D_SIT_UAV_RWBYTEADDRESS`, `D3D_SIT_UAV_APPEND_STRUCTURED`, `D3D_SIT_UAV_CONSUME_STRUCTURED`, `D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER`, `D3D_SIT_RTACCELERATIONSTRUCTURE`) and `bindDesc.Dimension`.
   - Note: `bindDesc.BindCount == 0` (or `UINT_MAX`) in DXIL denotes an unbounded bindless array (`[]`) -> set `IsUnboundedArray = true`, `Count = kUnboundedDescriptorCount`, `UsesBindlessHeap = true`.
   - Unsupported resource type (e.g. unknown/unhandled `D3D_SIT_*`) fails explicitly rather than silently misclassifying.
5. **Constant Buffers & Recursive Types:** Loop `0 .. shaderDesc.ConstantBuffers - 1`:
   - `GetConstantBufferByIndex(i)` -> `GetDesc(&cbDesc)`. Skip/categorize non-`D3D_CT_CBUFFER` (e.g. `D3D_CT_RESOURCE_BIND_INFO` if present for StructuredBuffers) cleanly!
   - Link to matching `ReflectedBinding` by `Name` / bind point.
   - Loop `0 .. cbDesc.Variables - 1`: `GetVariableByIndex(v)` -> `GetDesc(&varDesc)` (`StartOffset`, `Size`) and recursively traverse `var->GetType()` (`D3D12_SHADER_TYPE_DESC`: `Class`, `Type`, `Rows`, `Columns`, `Elements`, `Members`, `Offset`, `GetMemberTypeName`, `GetMemberTypeByIndex`).
   - Note: Calculate `ArrayStrideBytes` / `MatrixStrideBytes` accurately (HLSL cbuffer packing: 16-byte register alignment for arrays/structs; `D3D_SVC_MATRIX_ROWS` vs `D3D_SVC_MATRIX_COLUMNS`).
6. **Stage Signatures:** Loop `InputParameters` / `OutputParameters` via `GetInputParameterDesc` / `GetOutputParameterDesc`:
   - Record `SemanticName`, `SemanticIndex`, `Register = sig.Register`, `ComponentType` (`D3D_REGISTER_COMPONENT_*`), `ComponentMask = sig.Mask`, `ComponentCount = popcount(sig.Mask)`, `IsSystemValue = (sig.SystemValueType != D3D_NAME_UNDEFINED)`.

---

## 8. Cross-Backend & Cross-Stage Validation Plan

### 8.1 Cross-Stage Interface Validation (`ValidateStageInterface`)
- Validates `producer` (e.g. VS) -> `consumer` (e.g. PS):
  - **SPIR-V Mode:** Matches non-built-in `StageOutputs` vs `StageInputs` by `Location`. Verifies `ComponentType` equality and `producer.ComponentCount >= consumer.ComponentCount` (or mask coverage).
  - **DXIL Mode:** Matches non-SV `StageOutputs` vs `StageInputs` by `(SemanticName, SemanticIndex)` and `Register`, verifying `ComponentType` and `(producer.ComponentMask & consumer.ComponentMask) == consumer.ComponentMask`.
  - **Cross-Stage System Values:** Verifies required system-value compatibility if applicable (e.g. VS emits `SV_POSITION` before PS).

### 8.2 Cross-Backend Validation (`ValidateCrossBackendCompatibility`)
Takes `(spirvRefl, dxilRefl, CrossBackendBindingPolicy)`:
1. **Stage & EntryPoint Check:** Must match identically.
2. **Compute Thread Group Size Check:** `ThreadGroupSizeX/Y/Z` must match identically.
3. **Vertex Input Interface Check:** Compares non-system-value `StageInputs` count, `ComponentType`, `ComponentCount`, and `SemanticName`/`SemanticIndex` (when available).
4. **Binding & Layout Reconciliation (Policy-Driven):**
   - `CrossBackendBindingPolicy` defines explicit rules (never silent assumptions):
     - **Root/Push Constant Contract:** e.g. DXIL `ConstantBuffer` at `(b0, space0)` maps to SPIR-V `PushConstants` (`SizeBytes` and member `OffsetBytes` / `SizeBytes` / scalar-vector layout must match!).
     - **Bindless Exception Contract:** Allows configured MorbiEngine divergences (e.g. Vulkan `Set 0, Binding 3` unbounded CB array <-> DX12 `(b1, space0)` Root CBV) while validating inner `ConstantBuffer` member layout (`BoneMatrices[100]`).
     - **Standard Resource Mapping:** Matches remaining bindings via explicit `(DxilSpace, DxilRegister) -> (VkSet, VkBinding)` table (e.g. MorbiEngine's Set 0 mapping: `t0,space0 -> set 0, binding 0`, `t0,space1 -> set 0, binding 1`, `s0,space0 -> set 0, binding 2`) and checks `Kind`, `Dimension`, `IsUnboundedArray` / `Count`, and `ConstantBuffer` `SizeBytes` + member layout!
   - Note on `row_major float4x4` (`BoneMatrices[100]`): DXC compiles HLSL `row_major` into DXIL `D3D_SVC_MATRIX_ROWS` (`IsRowMajor = true`) and SPIR-V `ColMajor` (`IsRowMajor = false`, transposed load). The cross-backend validator must compare matrix dimensions `(Rows x Columns)` and byte size `(64B * 100)`, and explicitly flag majorness representation differences as an informational/expected note or enforce consistent byte size & stride rather than falsely failing valid `row_major` shaders!

---

## 9. Test Matrix (WP2 Planned Tests in `Tests/ShaderTests/`)

| # | Test Scenario | Targets | Expected Verification |
|---|---|---|---|
| 1 | Vertex Shader Reflection | DXIL + SPIR-V | Input semantics/locations/formats/component counts, outputs, bindings |
| 2 | Pixel Shader Reflection | DXIL + SPIR-V | Stage inputs matching VS, `SV_TARGET` outputs, textures/samplers |
| 3 | Compute Shader Reflection | DXIL + SPIR-V | `ThreadGroupSizeX/Y/Z` (`[numthreads(8,4,2)]`), UAV/SRV buffers |
| 4 | Descriptor Array (Fixed & Unbounded `[]`) | DXIL + SPIR-V | `IsUnboundedArray == true`, `Count == kUnboundedDescriptorCount`, `UsesBindlessHeap == true`, fixed array counts |
| 5 | Multiple Descriptor Sets / Register Spaces | DXIL + SPIR-V | Distinct `Set`/`Binding` on SPIR-V vs distinct `DxilRegister`/`DxilSpace` on DXIL (no fabricated cross-fields) |
| 6 | Constant Buffer Member Offsets, Sizes, Arrays & Matrices | DXIL + SPIR-V | Exact total CB size, member names, offsets, sizes, nested structs, arrays, `float4x4` strides |
| 7 | Push Constant Reflection | SPIR-V + DXIL Root CB | SPIR-V `PushConstants` (`128B` + members); DXIL `b0, space0` `ConstantBuffer` (`128B` + members) |
| 8 | Vertex Input Reflection (`StaticMeshVertex` 7-attribute layout) | DXIL + SPIR-V | Exact 7 attributes (`POSITION` float3, `NORMAL` float3, `TEXCOORD0` float2, `TANGENT` float4, `BINORMAL` float3, `BLENDINDICES` int4, `BLENDWEIGHT` float4) |
| 9 | Compute Thread Group Size | DXIL + SPIR-V | Exact X/Y/Z verification across multiple `[numthreads(...)]` values |
| 10 | Multiple Stage Interface Compatibility | DXIL + SPIR-V | Valid VS->PS passes; missing/incompatible VS->PS attribute fails `ValidateStageInterface` |
| 11 | Vulkan / DX12 Layout Compatibility (Morbi PBR Shader) | DXIL + SPIR-V | Real `kScenePbrMeshHlslShader` passes `ValidateCrossBackendCompatibility` with Morbi policy |
| 12 | Invalid / Corrupt / Truncated Binary | DXIL + SPIR-V | Non-SPIRV, truncated DXIL, non-shader bytes fail gracefully with clear `Result::Failure` diagnostics |
| 13 | Unsupported / Unmapped Resource Type | DXIL + SPIR-V | Unsupported resource or stripped/invalid reflection part returns clean error rather than silent guess |
| 14 | Cross-Backend Mismatch Detection | DXIL + SPIR-V | CB member size/offset mismatch, push-constant size mismatch, resource kind mismatch, and `numthreads` mismatch all fail `ValidateCrossBackendCompatibility` |
| 15 | Cache v3 Serialization Round-Trip & Thread Safety | DXIL + SPIR-V | Full `ShaderReflectionData` survives `.mshc` v3 save/load identically; parallel reflection safe across threads |

---

## 10. Dependencies & Risks

| Item | Type | Risk & Mitigation |
|---|---|---|
| `SPIRV-Reflect` integration | Dependency | Vendor 2 Apache-2.0 files under `ThirdParty/SPIRV-Reflect/` (offline-safe like `ThirdParty/JoltPhysics`) OR pinned `FetchContent`. Recommend vendoring if offline determinism is preferred. |
| Linux `ID3D12ShaderReflection` | Cross-Platform | Windows uses `<d3d12shader.h>` from Windows SDK; Linux requires clean ABI-compatible `ID3D12ShaderReflection*` declarations in `DxcRuntimeBridge.hpp` (no Wine/LGPL headers). Linux build/runtime remains `NOT VERIFIED` until Linux toolchain runs. |
| SPIR-V stripped `OpName` | Format Nuance | Standard runtime SPIR-V keeps `OpName` unless stripped; WP2 documents that semantic strings are optional on stripped SPIR-V (never fabricated), and WP8 stores full reflection in `.mshdr` before stripping. |
| `dxil.dll` / `STAT` availability | DXIL Nuance | If a DXIL binary was compiled with `-Qstrip_reflect`, `'STAT'` is absent (`IDxcContainerReflection::GetPartReflection` on `DXC_PART_DXIL` vs `STAT`). WP2 handles missing reflection cleanly via `Result::Failure`. |
| `.mshc` Cache Format Bump | Compatibility | Bump `ShaderCache` v2 -> v3 so old v2 files without full reflection trees cleanly miss and recompile as v3. |

---

## 11. Exact Files Expected to Change (During WP2 Implementation)

### New Files
- `ThirdParty/SPIRV-Reflect/` (`spirv_reflect.h`, `spirv_reflect.c`, `spirv.h`, `LICENSE`) *(or CMake FetchContent entry)*
- `Engine/Source/Shader/include/Morbi/Shader/ShaderReflection.hpp` *(or inside `ShaderCompiler.hpp` + included cleanly)*
- `Engine/Source/Shader/src/SpirvReflector.hpp` / `.cpp`
- `Engine/Source/Shader/src/DxilReflector.hpp` / `.cpp`
- `Engine/Source/Shader/src/ShaderReflectionValidator.cpp`
- `Tests/ShaderTests/ShaderReflectionTests.cpp` *(added to `MorbiShaderTests` or second test executable in `Tests/ShaderTests/CMakeLists.txt`)*

### Modified Files
- `cmake/MorbiDependencies.cmake` (configure `SPIRV-Reflect` target with `/W0`/`-w` warning suppression)
- `Engine/Source/Shader/CMakeLists.txt` (link `SPIRV-Reflect` privately, compile new `.cpp` files)
- `Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp` (expanded `ShaderReflectionData` + `ReflectBytecode` + validator APIs)
- `Engine/Source/Shader/src/DxcRuntimeBridge.hpp` (add `CLSID_DxcContainerReflection`, `IDxcContainerReflection`, and Linux ABI declarations for `ID3D12ShaderReflection`)
- `Engine/Source/Shader/src/ShaderCompiler.cpp` (dispatch `ReflectBytecode` to `SpirvReflector` / `DxilReflector`; keep `ReflectSourceAndBinary` delegating to binary reflection when valid bytecode is supplied)
- `Engine/Source/Shader/src/ShaderCache.cpp` (bump `kShaderCacheVersion = 3u`, serialize/deserialize full reflection model)
- `Tests/ShaderTests/CMakeLists.txt` (include reflection tests)

*(No changes required to `EngineRuntime.cpp`, `VulkanDevice.cpp`, `DX12Device.cpp`, or Phase 1 RHI files.)*

---

## 12. WP2 Implementation Steps (Pending Approval)

1. Integrate `SPIRV-Reflect` cleanly in `ThirdParty/SPIRV-Reflect/` + `cmake/MorbiDependencies.cmake` (with `/W0`/`-w`).
2. Add `IDxcContainerReflection` & `ID3D12ShaderReflection` support to `DxcRuntimeBridge.hpp` and `DxcModuleContext`.
3. Define expanded unified `ShaderReflectionData` structures and `ShaderReflectionValidator` APIs while preserving backward compatibility for `EngineRuntime.cpp`.
4. Implement `DxilReflector` (`IDxcContainerReflection` + `ID3D12ShaderReflection`) and `SpirvReflector` (`SPIRV-Reflect`).
5. Implement `ValidateStageInterface` and `ValidateCrossBackendCompatibility` with explicit `CrossBackendBindingPolicy`.
6. Upgrade `ShaderCache` serialization to v3 (`kShaderCacheVersion = 3u`).
7. Implement all 15 test categories in `Tests/ShaderTests/`, run Release & Debug builds, CTest suites, and Vulkan 1.3 / DX12 Editor smoke tests.
8. Perform independent Code Audit, update `docs/road/`, and deliver the WP2 Completion Report.
