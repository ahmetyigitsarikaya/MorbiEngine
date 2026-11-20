# PROP-0001: Phase 2 WP2 — Binary Reflection Architecture & Dependency Evaluation

**Status:** `APPROVED / IMPLEMENTED` (Adopted in Phase 2 WP2; see [ADR-0002](../decisions/ADR-0002-spirv-reflect-dxil-reflection-and-shader-cache-v3.md))  
**Target Phase / WP:** Phase 2 WP2 — Binary Reflection  
**Date:** 2026-11-18  
**Dependencies:** Phase 2 WP1 (`COMPLETE`)

---

## 1. Purpose

Evaluate binary shader reflection technologies for **SPIR-V (Vulkan 1.3)** and **DXIL (DirectX 12 SM 6.0–6.6)**, select the optimal dependencies for MorbiEngine's C++20 / Windows + Linux architecture, and propose the decoupled reflection architecture prior to WP2 implementation.

---

## 2. Empirical Baseline Observed in Repository

Direct inspection and tool probes against MorbiEngine's actual DXC (`1.9.2602.24`) and RHI backends established:

1. **DXIL Container Parts Emitted by Default:**
   Compiling HLSL with MorbiEngine's `dxcompiler.dll` produces a 7-part DXBC/DXIL container (`SFI0`, `ISG1`, `OSG1`, `PSV0`, `STAT`, `HASH`, `DXIL`). The `STAT` (`DXC_PART_REFLECTION_DATA`) and `DXIL` parts are present by default when `-Qstrip_reflect` is not passed.
2. **SPIR-V Decorations & `SPV_GOOGLE_*` Extension Hazard:**
   - Passing `-fspv-reflect` to DXC causes DXC to emit `OpExtension "SPV_GOOGLE_hlsl_functionality1"` and `OpDecorateString ... UserSemantic "..."`.
   - However, [VulkanDevice.cpp](../../Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp#L879-L880) enables only `VK_KHR_SWAPCHAIN_EXTENSION_NAME` on `VkDevice`. Passing runtime SPIR-V containing `SPV_GOOGLE_hlsl_functionality1` or `SPV_GOOGLE_user_type` to `vkCreateShaderModule` without enabling the corresponding `VK_GOOGLE_*` device extensions violates the Vulkan specification and risks driver/validation failure.
   - Disassembly of MorbiEngine's actual `kScenePbrMeshHlslShader` compiled **without** `-fspv-reflect` (`-spirv -fspv-target-env=vulkan1.3`) confirms that standard DXC SPIR-V already emits:
     - `OpEntryPoint` + stage + entry point name
     - `OpDecorate ... Location N` and `OpName %in_var_<SEMANTIC>` (when debug names are not stripped)
     - `OpDecorate ... DescriptorSet` & `Binding`, including unbounded `OpTypeRuntimeArray` (`g_BindlessTextures[]`, `g_BoneBuffers[]`)
     - `OpTypeStruct` + `OpMemberDecorate Offset` + `MatrixStride` + `ColMajor`/`RowMajor` + `ArrayStride` for both `PushConstant` and `UniformBuffer` (`cbuffer`)
     - `OpExecutionMode ... LocalSize` for compute shaders
3. **Genuine Vulkan vs. DX12 Shader Divergences in MorbiEngine:**
   - **Push Constants vs. Root Constants:** Vulkan uses `[[vk::push_constant]]` (`StorageClassPushConstant`, 128 bytes); DX12 uses `cbuffer RootConstantsBuffer : register(b0, space0)` bound via `SetGraphicsRoot32BitConstants(0, ...)` in [DX12Device.cpp](../../Engine/Source/RHI.DX12/src/DX12Device.cpp#L360-L369). DXIL binary reflection natively reports `b0, space0` as `D3D_SIT_CBUFFER`, NOT a push constant.
   - **Per-Object Skinning UBO (`b1, space0`):** Vulkan binds `g_BoneBuffers[]` at `Set 0, Binding 3` (unbounded UBO array); DX12 binds `PerObjectBuffer` directly via Root CBV at `RootParam 2` (`b1, space0`).
   - **Register Space vs. Descriptor Set:** Vulkan's `VulkanDevice` packs `t0,space0` (binding 0), `t0,space1` (binding 1), `s0,space0` (binding 2), and `b1,space0` (binding 3) all into **Descriptor Set 0**.

---

## 3. Library / API Comparison

### 3.1 SPIR-V Reflection Options

| Criterion | **Khronos SPIRV-Reflect** (Recommended) | **Khronos SPIRV-Cross** | Custom Binary SPIR-V Parser |
|---|---|---|---|
| **License** | Apache-2.0 | Apache-2.0 | Internal |
| **Maintenance** | Active (KhronosGroup; LunarG Vulkan SDK component) | Active (KhronosGroup; primarily a cross-compiler) | High ongoing maintenance burden |
| **Dependencies** | Zero external deps (2 source files: `spirv_reflect.h`, `spirv_reflect.c` + bundled `spirv.h`) | Heavy C++ core (`spirv_cross`, `spirv_parser`, `spirv_cfg`, `spirv_reflect`, ~25k+ LOC); uses C++ exceptions unless disabled | None |
| **CMake Integration** | Trivial static target in `ThirdParty/SPIRV-Reflect` (matching `ThirdParty/JoltPhysics` pattern) or pinned `FetchContent`; compiles in <1s with `/W0`/`-w` | Multi-target CMake config; must disable GLSL/HLSL/MSL/CLI backends and exceptions; heavier compile cost | None |
| **Windows & Linux** | Pure C99/C++20 portable across MSVC, GCC, Clang | Portable across MSVC, GCC, Clang | Portable |
| **Vulkan 1.3 Compatibility** | Full SPIR-V 1.0–1.6, `SPV_EXT_descriptor_indexing` unbounded arrays, push constants, UBO/SSBO member offsets/strides/matrices, `LocalSize`, stage I/O locations & formats | Full SPIR-V 1.0–1.6, but designed around AST decompilation rather than zero-overhead reflection structs | Risk of missing SPIR-V opcodes/extensions |
| **Suitability for MorbiEngine** | **Best Fit:** Minimal footprint, zero runtime exceptions, clean C struct output mapped directly to `ShaderReflectionData`, aligns with `MorbiDependencies.cmake` | **Overkill:** Pulls in cross-compilation IR/AST machinery we do not need (DXC already produces HLSL -> SPIR-V & DXIL) | **Rejected:** Reinvents Khronos-maintained parser |

### 3.2 DXIL Reflection Options

| Criterion | **DXC `IDxcContainerReflection` + `ID3D12ShaderReflection`** (Recommended) | **`dxc.exe -Fre` / `-dumpbin` Text Parsing** | Custom LLVM Bitcode / DXIL Parser |
|---|---|---|---|
| **License** | University of Illinois / NCSA (`dxcompiler.dll`) + MIT (`d3d12shader.h` in Windows SDK) | N/A | N/A |
| **Maintenance** | Official Microsoft DirectXShaderCompiler API | Unstable text/binary sidecar format | Fragile against DXIL metadata changes |
| **Dependencies** | **Zero new dependencies** — already inside `dxcompiler.dll` (`1.9.2602.24`) / `libdxcompiler.so` loaded by `DxcModuleContext` in WP1 | Requires spawning `dxc.exe` or extra compile pass | Requires full LLVM 3.7 bitcode reader |
| **CMake Integration** | Windows: `<d3d12shader.h>` from Windows SDK (already detected); Linux: ABI-compatible COM interface declarations in `DxcRuntimeBridge.hpp` (no Wine/LGPL headers) | N/A | Massive LLVM dependency |
| **Windows & Linux** | `IDxcContainerReflection` & `ID3D12ShaderReflection` are implemented inside both `dxcompiler.dll` (Windows) and `libdxcompiler.so` (Linux) | Process-based | Complex |
| **DXIL SM 6.0–6.6 Compatibility** | Full `D3D12_SHADER_DESC`, `D3D12_SHADER_INPUT_BIND_DESC` (`BindPoint`, `Space`, `BindCount`), `ID3D12ShaderReflectionConstantBuffer`, recursive `ID3D12ShaderReflectionType`, `D3D12_SIGNATURE_PARAMETER_DESC`, `GetThreadGroupSize` | Incomplete or text-scraped | High defect risk |
| **Suitability for MorbiEngine** | **Best Fit:** Uses the exact in-process `DxcModuleContext` established in WP1 with zero extra binaries | **Rejected:** Regresses toward out-of-process CLI patterns removed in WP1 | **Rejected:** Massive unnecessary complexity |

---

## 4. Recommended Architecture Summary

1. **Pure Backend-Specific Reflectors (Private to `Morbi.Shader`):**
   - `SpirvShaderReflector`: Uses `SPIRV-Reflect` on `std::span<const u8>` SPIR-V bytecode (zero `-fspv-reflect` extension injection).
   - `DxilShaderReflector`: Uses `DxcModuleContext::CreateContainerReflection()` (`CLSID_DxcContainerReflection`) -> `IDxcContainerReflection::Load` -> `FindFirstPartKind(DXC_PART_DXIL, ...)` (fallback `DXC_PART_REFLECTION_DATA`) -> `GetPartReflection(..., ID3D12ShaderReflection)`.
2. **Public Backend-Independent Model (`ShaderReflectionData`):**
   - Exposes unified resource kinds, constant buffer member trees, push-constant ranges, stage inputs/outputs, compute thread group sizes, and **optional** backend coordinates (`DescriptorSet`/`Binding` for SPIR-V; `RegisterSlot`/`RegisterSpace` for DXIL) without fabricating absent fields.
3. **Explicit Cross-Stage & Cross-Backend Validation (`ShaderReflectionValidator`):**
   - `ValidateStageInterface(producer, consumer)`: Validates stage I/O compatibility (`Location` / `(SemanticName, SemanticIndex)` / component type & mask coverage).
   - `ValidateCrossBackendCompatibility(spirvRefl, dxilRefl, policy)`: Compares SPIR-V and DXIL reflections using an explicit `CrossBackendBindingPolicy` (reconciling MorbiEngine's Vulkan PushConstants <-> DX12 `b0, space0` Root Constants and Vulkan `b1` array <-> DX12 `b1` Root CBV without falsifying raw reflection).
4. **Preserve Existing Callers & Cache Compatibility:**
   - Keep `ShaderReflectionData::Stage`, `EntryPoint`, `PushConstantSizeBytes`, `ThreadGroupSizeX/Y/Z`, `UsesBindlessHeap`, and `Bindings` compatible with [EngineRuntime.cpp](../../Engine/Source/Runtime/src/EngineRuntime.cpp).
   - Bump `ShaderCache` binary format version `2 -> 3` in WP2 so serialized `.mshc` files store the full `ShaderReflectionData` tree (CB members, push constant ranges, stage I/O) and cleanly recompile stale v2 entries.

---

## 5. Dependency Integration Note (For Approval)

Integrating `SPIRV-Reflect` requires choosing between:
- **Option A (Recommended):** Vendor the 2 Apache-2.0 source files (`spirv_reflect.h`, `spirv_reflect.c` + `spirv.h` from Khronos `SPIRV-Reflect` pinned release/commit) under `ThirdParty/SPIRV-Reflect/` (mirrors `ThirdParty/JoltPhysics` and works 100% offline/hermetically).
- **Option B:** `FetchContent_Declare` pinned to a Khronos `SPIRV-Reflect` commit hash in `cmake/MorbiDependencies.cmake` (mirrors `enet` / `libsodium`, requires internet on clean configure).

This choice is highlighted in [PHASE-02-WP02-EXECUTION-PLAN.md](../reports/phase-02/PHASE-02-WP02-EXECUTION-PLAN.md) for approval before WP2 implementation begins.
