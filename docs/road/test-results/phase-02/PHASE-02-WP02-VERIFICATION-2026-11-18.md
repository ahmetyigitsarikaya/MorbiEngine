# Phase 2 WP2 — Binary Reflection Verification Record

**Date:** 2026-11-18  
**Machine / Hardware:** 12th Gen Intel(R) Core(TM) i5-12400F, NVIDIA GeForce RTX 4060 Ti (Driver `32.0.16.1656`)  
**Operating System:** Microsoft Windows 11 Home Single Language (`10.0.26200`, 64-bit)  
**Toolchain:** CMake `4.5.0-rc1`, Visual Studio 18 2026 (`MSVC 19.51.36260.0`), C++20, DXC `1.9 (5191-d355aa83) [1.9.2602.24]`, Khronos `SPIRV-Reflect` (`vulkan-sdk-1.4.363.0`)

---

## Test Execution Matrix (Post-Final Edit)

| Test / Check | Config | Graphics API | Exact Command | Exit Code | Actual Result | Assertion / Target Count |
|---|---|---|---|---|---|---|
| Full Engine & Tests Build (Release) | Release | Vulkan 1.3 + DX12 | `cmake --build --preset release` | `0` | **PASSED** (0 errors, 0 warnings in `Morbi.Shader` / `MorbiSPIRVReflect` / `Tests/ShaderTests`) | Full build |
| Full Engine & Tests Build (Debug) | Debug | Vulkan 1.3 + DX12 | `cmake --build --preset debug` | `0` | **PASSED** (0 errors, 0 warnings in `Morbi.Shader` / `MorbiSPIRVReflect` / `Tests/ShaderTests`; pre-existing `C4324` in untouched `Animation.hpp`/`AnimationEvaluator.hpp`) | Full build |
| Shader Compilation Suite (Release) | Release | DXIL + Vulkan 1.3 SPIR-V | `.\build\bin\Release\MorbiShaderTests.exe` | `0` | **PASSED** | **181/181** |
| Shader Compilation Suite (Debug) | Debug | DXIL + Vulkan 1.3 SPIR-V | `.\build-debug\bin\Debug\MorbiShaderTests.exe` | `0` | **PASSED** | **181/181** |
| Binary Reflection & Validation Suite (Release) | Release | DXIL + Vulkan 1.3 SPIR-V | `.\build\bin\Release\MorbiShaderReflectionTests.exe` | `0` | **PASSED** | **190/190** |
| Binary Reflection & Validation Suite (Debug) | Debug | DXIL + Vulkan 1.3 SPIR-V | `.\build-debug\bin\Debug\MorbiShaderReflectionTests.exe` | `0` | **PASSED** | **190/190** |
| Release CTest Suite (All Targets) | Release | Vulkan 1.3 + DX12 | `ctest --test-dir build -C Release --output-on-failure` | `0` | **7/7 PASSED** (`59.21 sec`) | All 7 CTest targets |
| Debug CTest Suite (Filtered, excl. `MorbiCSharpPhysicsTests`) | Debug | Vulkan 1.3 + DX12 | `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests --output-on-failure` | `0` | **6/6 PASSED** (`62.29 sec`) | 6/7 CTest targets (filtered; `MorbiCSharpPhysicsTests` fails due to pre-existing `BL-PHY-001`) |
| RHI Descriptor Tests (Release) | Release | Vulkan 1.3 + DX12 | `.\build\bin\Release\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** (Vulkan 430/430, DX12 430/430) |
| RHI Descriptor Tests (Debug) | Debug | Vulkan 1.3 + DX12 | `.\build-debug\bin\Debug\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** (Vulkan 430/430, DX12 430/430) |
| RHI Handle & Hash Tests (Release) | Release | Vulkan 1.3 + DX12 | `.\build\bin\Release\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** (Vulkan 240/240, DX12 239/239) |
| RHI Handle & Hash Tests (Debug) | Debug | Vulkan 1.3 + DX12 | `.\build-debug\bin\Debug\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** (Vulkan 240/240, DX12 239/239) |
| Editor Smoke Test — Vulkan 1.3 (Release) | Release | Vulkan 1.3 | `.\build\bin\Release\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all phases `[PASS]`; live log: `Bindings=0, PushConstBytes=64`) | Exit `0` |
| Editor Smoke Test — DX12 (Release) | Release | DirectX 12 | `.\build\bin\Release\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all phases `[PASS]`; live log: `Bindings=1, PushConstBytes=0`) | Exit `0` |
| Editor Smoke Test — Vulkan 1.3 (Debug) | Debug | Vulkan 1.3 | `.\build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all phases `[PASS]`) | Exit `0` |
| Editor Smoke Test — DX12 (Debug) | Debug | DirectX 12 | `.\build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all phases `[PASS]`) | Exit `0` |
| Linux Build & Runtime Tests | — | Vulkan 1.3 | — | — | **NOT VERIFIED** (Windows 11 host; no Linux toolchain/runner active; tracked in `BL-SHD-007`) | — |

---

## WP2 Test Scenario Coverage (`MorbiShaderReflectionTests`, 190/190 Assertions)

1. **Test 1/2/5 (`Test_ConstantBuffersResourcesAndCompute`):** Nested struct (`SubMaterial`), `float4x4`, array `ExtraWeights[3]`, scalar/vector member offsets & byte sizes in both SPIR-V and DXIL (`SceneGlobals = 160B`, `PrimaryMat = 24B` unpadded content); `Texture2D`, `Texture2DArray`, `StructuredBuffer`, `RWTexture2D`, `RWStructuredBuffer`, `SamplerState`, `SamplerComparisonState`; compute `[numthreads(8, 8, 4)]`; and `StrictOneToOne` cross-backend validation passing.
2. **Test 3/4/9 (`Test_MorbiPbrShaderAndVertexInputs`):** Real `kScenePbrMeshHlslShader` PBR mesh VS+PS (`SPIR-V` + `MORBI_VULKAN` vs `DXIL`); 7 vertex inputs including `int4 BLENDINDICES`; 128-byte Push Constants vs DX12 `RootConstantsBuffer`; `UsesBindlessHeap`; VS→PS stage interface validation on both backends; `StrictOneToOne` rejection vs `MorbiBindless` acceptance.
3. **Test 6/7 (`Test_StageInterfaceValidation`):** Missing Pixel input (`STAGE_MISSING_OUTPUT`), component mask/width mismatch (`STAGE_COMPONENT_MASK_MISMATCH`), scalar type mismatch (`STAGE_TYPE_MISMATCH`), and invalid stage order (`STAGE_ORDER_INVALID`) across both SPIR-V and DXIL.
4. **Test 10 (`Test_CrossBackendNegativeValidation`):** Thread group mismatch, CB offset/type mismatch, descriptor array count `[4]` vs `[8]`, Vulkan UBO array vs single DX12 CBV under `StrictOneToOne`, Vulkan unbounded `Texture2D[]` vs single DX12 `Texture2D` rejected even under `MorbiBindless`, and PushConstant >128B / layout mismatches.
5. **Test 11/12 (`Test_ErrorPathsNoCopyAndLifetime`):** Empty/truncated/corrupt SPIR-V & DXIL, cross-format rejection, stage/entry mismatches, unaligned `SPV_REFLECT_MODULE_FLAG_NO_COPY` sub-span handling, and caller buffer destruction safety.
6. **Test 13/14 (`Test_ShaderCacheV3AndParallelReflection`):** Bit-exact `ShaderReflectionData` round-trip in `ShaderCache` v3, automatic invalidation & migration of v2 headers, and 6-thread parallel compile + binary reflection stress.
7. **Test 15 (`Test_DxcContainerReflectionLifetimeStress`):** Repeated `ShaderCompiler` construction/destruction and DXIL container reflection lifetime cycles.

---

## Environment & Smoke Test Reflection Analysis (Post-WP2 Audit)

### Smoke Test Difference: Vulkan (`Bindings=0, PushConstBytes=64`) vs. DX12 (`Bindings=1, PushConstBytes=0`)

Verified empirically against `EngineRuntime.cpp` (`lines 87–193, 1154–1209`), `ShaderCompiler.cpp` (`lines 1417–1428`), and `dxc.exe -dumpbin` on `kUnifiedHlslShader` (`PSMain`):

1. **Same Underlying Shader Struct (`FramePushConstants` = 64 bytes):** Both targets compile `kUnifiedHlslShader` (`FramePushConstants`: `AccentColor` @0, `BindlessBufferSlot` @16, `FrameIndex` @20, `InvResolution` @24, `CameraPosAndAspect` @32, `CameraTargetAndFov` @48 = `64 bytes`). `ShaderCompiler::Compile()` automatically injects `-D MORBI_VULKAN=1` (SPIR-V) or `-D MORBI_DX12=1` (DXIL).
2. **Why `gGlobalBuffer` Does Not Appear:** `StructuredBuffer<float4> gGlobalBuffer : register(t0, space0);` is unused in `PSMain`, so DXC dead-resource elimination strips it from both SPIR-V and DXIL binaries.
3. **Why Vulkan Reports `Bindings=0, PushConstBytes=64`:** With `MORBI_VULKAN=1`, DXC compiles `[[vk::push_constant]] FramePushConstants PushConstants;` into SPIR-V `PushConstant` storage class (not a Descriptor Set binding). `SpirvReflector` emits `Bindings.size() == 0`, `PushConstants.size() == 1`, and `PushConstantSizeBytes == 64`.
4. **Why DX12 Reports `Bindings=1, PushConstBytes=0`:** With `MORBI_DX12=1`, DXC compiles `cbuffer RootConstantsBuffer : register(b0, space0) { FramePushConstants PushConstants; };`. DXIL has no separate push-constant metadata (`ID3D12ShaderReflection` reports it as `D3D_SIT_CBUFFER` at `cb0, space0`, Size=`64`). Per WP2's no-guessing rule, `DxilReflector` leaves `PushConstantSizeBytes = 0` and puts `RootConstantsBuffer` in `Bindings` (`Bindings.size() == 1`).
5. **Cross-Backend Equivalence Verified:** Under `CrossBackendBindingPolicy::StrictOneToOne()`, the mismatch is explicitly flagged; under `CrossBackendBindingPolicy::MorbiBindless()` (`RootConstantRule { DxilSpace=0, DxilRegister=0, MaxSizeBytes=128 }`), `PushConstants` (`64B`, 6 fields) and `RootConstantsBuffer` (`64B`, single wrapper struct `PushConstants` containing the same 6 fields at identical offsets) validate as **100% compatible**. No bug exists; the log difference reflects expected backend API semantics.

## Known Environment Notes

- `VK_LAYER_KHRONOS_validation` is not installed on this machine (`BL-ENV-001`); DX12 Debug Layer + DRED are active in RHI tests.
- `MorbiCSharpPhysicsTests` in Debug fails due to pre-existing `SEHException` in `Morbi.Engine.NativeBridge.SyncRigidBody` (`BL-PHY-001`, unrelated to `Morbi.Shader`; Release `MorbiCSharpPhysicsTests` passes in CTest).
- Linux build and `libdxcompiler.so` runtime execution remain **NOT VERIFIED** on this Windows host (`BL-SHD-007`).
