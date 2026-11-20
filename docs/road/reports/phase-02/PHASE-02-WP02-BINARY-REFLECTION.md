# Phase 2 WP2 — Binary Reflection Completion Report

**Work Package:** Phase 2 WP2 — Binary Reflection  
**WP Status:** `COMPLETE`  
**Date:** 2026-11-18  
**Dependencies:** Phase 2 WP1 (`COMPLETE`)

---

## 1. WP Summary

Replaced the legacy source-regex `ReflectSourceAndBinary` stub in `Morbi.Shader` with genuine binary shader reflection for both **Vulkan 1.3 SPIR-V** (via vendored Khronos `SPIRV-Reflect` `vulkan-sdk-1.4.363.0`) and **DirectX 12 DXIL** (via DXC `IDxcContainerReflection` + `ID3D12ShaderReflection`), unified into a backend-agnostic `ShaderReflectionData` model that preserves per-backend provenance without guessing missing values, added stage-interface and policy-driven cross-backend validators (`ValidateStageInterface` and `ValidateCrossBackendCompatibility`), upgraded `ShaderCache` to v3 (`'MSHC'` v3) with lossless reflection serialization and v2 stale invalidation, and verified all 15 reflection/validation scenarios in `MorbiShaderReflectionTests` (`190/190` assertions) alongside full Release/Debug builds, CTest suites, RHI tests, and Vulkan 1.3 / DX12 Editor smoke tests.

---

## 2. Initial Repository State

Prior to WP2 (`PHASE-02-WP02-EXECUTION-PLAN.md` & `PROP-0001`):
- `ShaderCompiler::Compile()` called `ReflectSourceAndBinary(request, bytecode)`, which ignored `bytecode`, matched HLSL text with `std::regex`, hardcoded `PushConstantSizeBytes = 64`, and returned no constant buffer member layouts, vertex input signatures, stage outputs, push constant members, or resource dimensions/read-only state.
- `ShaderCache` v2 serialized only coarse `ReflectedBinding` fields (`Name`, `Type`, `RegisterSlot`, `RegisterSpace`, `Count`).
- No VS→PS stage interface validation or Vulkan vs. DX12 cross-backend binding/layout validator existed.

---

## 3. Architecture & Key Decisions

Documented in [ADR-0002](../../decisions/ADR-0002-spirv-reflect-dxil-reflection-and-shader-cache-v3.md):
1. **SPIR-V Reflection (`SpirvReflector`):** Links `SPIRVReflect::SPIRVReflect` (`ThirdParty/SPIRV-Reflect/`, `vulkan-sdk-1.4.363.0`, Apache-2.0) privately inside `Morbi.Shader` without exposing `spirv_reflect.h` in public headers. Uses `SPV_REFLECT_MODULE_FLAG_NO_COPY` behind an RAII `ScopedSpvReflectModule` wrapper with alignment safety for unaligned caller spans and entry-point-scoped enumeration.
2. **DXIL Reflection (`DxilReflector`):** Uses pooled `IDxcUtils` (`CreateBlobFromPinned`) and per-call `IDxcContainerReflection` (`FindFirstPartKind(DXC_PART_DXIL)` with fallback `DXC_PART_REFLECTION_DATA` -> `ID3D12ShaderReflection`), with native `<d3d12shader.h>` on Windows and ABI-compatible declarations in `DxcRuntimeBridge.hpp` for Linux. Filters `ConstantBuffers` strictly by `D3D_CT_CBUFFER` so internal `D3D_CT_RESOURCE_BIND_INFO` descriptors for `StructuredBuffer` elements are never misclassified as cbuffers.
3. **Public Header Provenance Isolation:** Preserves `DescriptorSet`/`Binding`/`Location` (SPIR-V) and `DxilRegister`/`DxilSpace`/`Register`/`SemanticName` (DXIL) as `std::optional<u32>` without synthesizing unavailable values.
4. **Explicit `CrossBackendBindingPolicy`:** `StrictOneToOne()` rejects Push Constants vs. DX12 Root Constants and Vulkan UBO descriptor arrays vs. single DX12 Root CBVs. `MorbiBindless()` explicitly encodes MorbiEngine's Descriptor Set 0 bindings (keyed with `DxilRegisterClass` to separate `t`/`s`/`b` namespaces) and Root Constant rule (`b0, space0`, max `128` bytes), validating stage visibility, byte size, matrix dimensions/strides, array counts/strides, and member-by-member offsets/types.
5. **`ShaderCache` v3:** Bumped `kShaderCacheVersion = 3u` with lossless serialization of full `ShaderReflectionData`, recursion depth guard (`kMaxSerializedMemberDepth = 16`), IEEE CRC32 integrity verification, v1/v2 stale invalidation, and atomic temp-rename writes.

---

## 4. Bugs Discovered & Fixed During WP2

1. **Undecorated `SpvReflectInterfaceVariable::component == ~0u` (`0xFFFFFFFFu`):** `SpirvReflector.cpp` initially clamped `std::min(var.component, 3u)`, shifting component bitmasks to `0x8` (`((1 << n) - 1) << 3 & 0xF`) instead of `0x7` / `0xF`. Fixed to `(var.component <= 3u) ? var.component : 0u`, immediately caught by `Test_StageInterfaceValidation`.
2. **Nested Struct `SizeBytes` Rounding Difference Between SPIRV-Reflect and DXIL:** `spirv-reflect` rounds nested struct member `var.size` to 16-byte alignment (`32B` for `SubMaterial`), whereas `D3D12_SHADER_VARIABLE_DESC::Size` reports unpadded extent (`24B`). Fixed `SpirvReflector::ConvertBlockMember` to compute unpadded `SizeBytes` from `max(child.OffsetBytes + child.SizeBytes)` while preserving `PaddedSizeBytes`.
3. **HLSL Wrapper Struct Unwrap for Push Constants vs. Root Constants:** `[[vk::push_constant]] MeshPushConstants PushConstants;` emits top-level block members in SPIR-V, whereas `cbuffer RootConstantsBuffer : register(b0, space0) { MeshPushConstants PushConstants; };` wraps them inside a single top-level struct variable `PushConstants` at offset `0` in DXIL. Added symmetric zero-offset single-struct unwrapping in `ShaderReflectionValidator`.

---

## 5. Files Created / Modified

### Created
- `ThirdParty/SPIRV-Reflect/` (`spirv_reflect.h`, `spirv_reflect.c`, `include/spirv/unified1/spirv.h`, `LICENSE`)
- `Engine/Source/Shader/include/Morbi/Shader/ShaderReflection.hpp`
- `Engine/Source/Shader/src/SpirvReflector.hpp`
- `Engine/Source/Shader/src/SpirvReflector.cpp`
- `Engine/Source/Shader/src/DxilReflector.hpp`
- `Engine/Source/Shader/src/DxilReflector.cpp`
- `Engine/Source/Shader/src/ShaderReflectionValidator.cpp`
- `Tests/ShaderTests/ShaderReflectionTests.cpp`
- `docs/road/decisions/ADR-0002-spirv-reflect-dxil-reflection-and-shader-cache-v3.md`
- `docs/road/test-results/phase-02/PHASE-02-WP02-VERIFICATION-2026-11-18.md`
- `docs/road/reports/phase-02/PHASE-02-WP02-BINARY-REFLECTION.md`

### Modified
- `cmake/MorbiDependencies.cmake` (`MorbiSPIRVReflect` / `SPIRVReflect::SPIRVReflect`)
- `docs/THIRD_PARTY_NOTICES.md` (`SPIRV-Reflect` Apache-2.0 notice)
- `Engine/Source/Shader/CMakeLists.txt`
- `Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp`
- `Engine/Source/Shader/src/DxcRuntimeBridge.hpp`
- `Engine/Source/Shader/src/ShaderCompiler.cpp`
- `Engine/Source/Shader/src/ShaderCache.cpp`
- `Tests/ShaderTests/CMakeLists.txt`
- `docs/road/MORBI_ENGINE_ROADMAP.md`, `docs/road/phases/PHASE-02-SHADER-PIPELINE.md`, `docs/road/STATUS.md`, `docs/road/BACKLOG.md`, `docs/road/CHANGELOG.md`, `docs/road/decisions/README.md`, `docs/road/proposals/README.md`, `docs/road/proposals/PROP-0001-WP2-REFLECTION-ARCHITECTURE.md`, `docs/road/reports/phase-02/PHASE-02-WP02-EXECUTION-PLAN.md`

---

## 6. Verification Summary (Post-Final Edit)

- `cmake --build --preset release`: **PASSED** (exit `0`, 0 errors, 0 warnings on `Morbi.Shader`/`MorbiSPIRVReflect`/`Tests/ShaderTests`)
- `cmake --build --preset debug`: **PASSED** (exit `0`, 0 errors, 0 warnings on `Morbi.Shader`/`MorbiSPIRVReflect`/`Tests/ShaderTests`)
- `MorbiShaderTests.exe` (Release & Debug): **181/181 PASSED**
- `MorbiShaderReflectionTests.exe` (Release & Debug): **190/190 PASSED**
- `ctest --test-dir build -C Release --output-on-failure`: **7/7 PASSED** (`59.21 sec`)
- `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests --output-on-failure`: **6/6 PASSED** (`62.29 sec`; filtered excluding pre-existing `BL-PHY-001`)
- `MorbiRHIDescriptorTests.exe` (Release & Debug): **860/860 PASSED**
- `MorbiRHIGenerationalHandleHashTests.exe` (Release & Debug): **507/507 PASSED**
- `MorbiEditor.exe --smoke-test --api vulkan` & `--api dx12` (Release & Debug): **PASSED** (exit `0`)
- Linux build & `libdxcompiler.so` runtime: **NOT VERIFIED** (Windows 11 host; `BL-SHD-007`)

Full verification matrix and smoke-test reflection parity analysis (`Vulkan Bindings=0, PushConstBytes=64` vs. `DX12 Bindings=1, PushConstBytes=0` confirmed equivalent via `FramePushConstants` `64B` under `CrossBackendBindingPolicy::MorbiBindless()`): [PHASE-02-WP02-VERIFICATION-2026-11-18.md](../../test-results/phase-02/PHASE-02-WP02-VERIFICATION-2026-11-18.md).

---

## 7. Next Work Package & Approval Gate

- **WP2 STATUS:** `COMPLETE`
- **Next WP:** **Phase 2 WP3 — Unified Pipeline Layout** (`PipelineLayoutHandle`, `PipelineLayoutDesc`, explicit Vulkan Descriptor Set Layout + Push Constant range generation, DX12 Root Signature generation, binding compatibility & layout lifetime).
- **Awaiting User Approval:** Implementation of WP3 will not begin until explicitly approved.
