# ADR-0002: Binary Shader Reflection (`SPIRV-Reflect` + DXC DXIL Reflection), CrossBackendBindingPolicy, and `ShaderCache` v3

**Status:** Accepted  
**Date:** 2026-11-18  
**Phase / WP:** Phase 2 WP2 — Binary Reflection

---

## Context

Prior to WP2, `ShaderCompiler::ReflectSourceAndBinary` parsed HLSL source text with `std::regex`, ignored compiled bytecode completely, hardcoded `PushConstantSizeBytes = 64`, failed to inspect constant buffer member layouts, vertex/stage signatures, compute thread-group sizes, descriptor dimensions/read-only state, and could not detect Vulkan vs. DirectX 12 cross-backend incompatibilities or VS→PS stage interface mismatches.

At the same time, MorbiEngine already depends on genuinely divergent Vulkan 1.3 and DX12 resource binding models:
- Vulkan (`VulkanDevice.cpp`) combines `t0,space0`, `t0,space1`, `s0,space0`, and unbounded `b1,space0` into Descriptor Set 0 (`bindings 0..3`) alongside 128-byte `VkPushConstantRange` (`[[vk::push_constant]]`).
- DX12 (`DX12Device.cpp`) binds Root Constants at `(b0, space0)`, a SRV Descriptor Table `(t0, space0 + space1)`, Static Sampler `(s0, space0)`, and a direct Root CBV at `(b1, space0)`.

## Decision

1. **Dual-Backend Binary Reflectors with Public API Isolation:**
   - **SPIR-V (Vulkan 1.3):** Vendor Khronos `SPIRV-Reflect` (`vulkan-sdk-1.4.363.0`, Apache-2.0, `ThirdParty/SPIRV-Reflect/`) as a private static library (`SPIRVReflect::SPIRVReflect`) linked solely into `Morbi.Shader`. Use `SPV_REFLECT_MODULE_FLAG_NO_COPY` behind an RAII `ScopedSpvReflectModule` wrapper that guarantees 4-byte word alignment (copying only unaligned caller sub-spans) and keeps backing storage valid for the module's lifetime.
   - **DXIL (DX12 SM 6.0–6.6):** Use DXC `IDxcContainerReflection` (`FindFirstPartKind(DXC_PART_DXIL)` with fallback `DXC_PART_REFLECTION_DATA`) and `ID3D12ShaderReflection` via pooled `IDxcUtils` (`DxcModuleContext`), native `<d3d12shader.h>` on Windows, and ABI-compatible declarations in `DxcRuntimeBridge.hpp` for Linux.
   - Neither `spirv_reflect.h` nor `d3d12shader.h`/`dxcapi.h` types appear in any public `Morbi.Shader` header (`ShaderReflection.hpp`, `ShaderCompiler.hpp`).

2. **Honest Unified Data Model & Provenance Preservation:**
   - `ShaderReflectionData` represents unified stage, entry point, compute thread group sizes, bindings, constant buffer layouts (recursive `ReflectedShaderMember`), push constant ranges, and stage input/output parameters (`ReflectedStageParameter`).
   - Backend-specific coordinates are stored explicitly as `std::optional<u32>` (`DescriptorSet`/`Binding` and `Location` for SPIR-V; `DxilRegister`/`DxilSpace` and `Register`/`SemanticName` for DXIL) and are never fabricated when absent.
   - Standard `-spirv -fspv-target-env=vulkan1.3` compilation (without `-fspv-reflect`) is retained so SPIR-V output never depends on `SPV_GOOGLE_hlsl_functionality1` which `VulkanDevice` does not enable.

3. **Explicit `CrossBackendBindingPolicy` Validation (`ShaderReflectionValidator`):**
   - `CrossBackendBindingPolicy::StrictOneToOne()` rejects Push Constants vs. Root Constant mismatches, UBO descriptor arrays vs. single Root CBVs, and unmapped descriptor set/space rules.
   - `CrossBackendBindingPolicy::MorbiBindless()` documents the exact MorbiEngine bindings (`CrossBackendBindingRule` keyed by `(DxilRegisterClass, DxilSpace, DxilRegister)` to prevent `t`/`s`/`b` slot collision, and `CrossBackendRootConstantRule` checking 128-byte limit, stage visibility, byte size, and full member-by-member offset/type/shape/array/matrix layout, safely unwrapping single-struct HLSL `cbuffer RootConstantsBuffer { StructType var; }` wrappers).
   - Unbounded texture arrays vs. single textures remain rejected even under `MorbiBindless()`.
   - `ValidateStageInterface` checks VS→PS varying availability, scalar types, and component bitmasks/counts across both SPIR-V (`Location`) and DXIL (`SemanticName` + `SemanticIndex`).

4. **`ShaderCache` v3 Serialization (`kShaderCacheVersion = 3u`):**
   - Bumped `.mshc` cache version from `v2` to `v3`, adding full lossless serialization of `ShaderReflectionData` (`Bindings`, `ConstantBuffers`, recursive `ReflectedShaderMember`, `PushConstants`, `StageInputs`, `StageOutputs`) with depth (`kMaxSerializedMemberDepth = 16`) and bounds guards alongside CRC32 checksums and atomic temp-rename writes. Older `v1`/`v2` files on disk are cleanly treated as stale (`StaleDependency`), deleted, recompiled, and upgraded to `v3`.

## Consequences

- `ShaderCompiler::Compile()` executes real binary reflection on every compiled SPIR-V and DXIL shader; reflection failure fails compilation rather than returning fabricated data.
- Pipeline layout generation in **WP3** and graphics/compute PSO validation in **WP4/WP5** can consume `ShaderReflectionData` directly without touching raw SPIR-V/DXIL parsers.
- Windows runtime & cross-backend parity tests are fully verified; Linux compilation and `libdxcompiler.so` runtime reflection remain unverified on the Windows host (`BL-SHD-007`).
