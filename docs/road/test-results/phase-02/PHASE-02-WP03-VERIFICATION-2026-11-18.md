# Phase 2 WP3 — Unified Pipeline Layout Verification Record

**Date:** 2026-11-18  
**Machine / Hardware:** 12th Gen Intel(R) Core(TM) i5-12400F, NVIDIA GeForce RTX 4060 Ti  
**Operating System:** Microsoft Windows 11 (`10.0.26200`, 64-bit)  
**Toolchain:** CMake (`Release` + `Debug`), MSVC C++20 (`/W4`), DXC `1.9 (5191-d355aa83) [1.9.2602.24]`, Khronos `SPIRV-Reflect` (`vulkan-sdk-1.4.363.0`)

---

## Test Execution Matrix (Post-Final Edit)

| Test / Check | Config | Graphics API | Exact Command | Exit Code | Actual Result | Assertion / Target Count |
|---|---|---|---|---|---|---|
| Full Engine & Tests Build (Release) | Release | Vulkan 1.3 + DX12 | `cmake --build --preset release` | `0` | **PASSED** (0 errors, 0 warnings in modified WP3 targets) | Full build |
| Full Engine & Tests Build (Debug) | Debug | Vulkan 1.3 + DX12 | `cmake --build --preset debug` | `0` | **PASSED** (0 errors, 0 warnings in modified WP3 targets) | Full build |
| Unified Pipeline Layout Suite (Release) | Release | Null + Vulkan 1.3 + DX12 | `.\build\bin\Release\MorbiPipelineLayoutTests.exe` | `0` | **PASSED** | **128/128** |
| Unified Pipeline Layout Suite (Debug) | Debug | Null + Vulkan 1.3 + DX12 | `.\build-debug\bin\Debug\MorbiPipelineLayoutTests.exe` | `0` | **PASSED** | **128/128** |
| Shader Compilation Suite (Release) | Release | DXIL + Vulkan 1.3 SPIR-V | `.\build\bin\Release\MorbiShaderTests.exe` | `0` | **PASSED** | **181/181** |
| Shader Compilation Suite (Debug) | Debug | DXIL + Vulkan 1.3 SPIR-V | `.\build-debug\bin\Debug\MorbiShaderTests.exe` | `0` | **PASSED** | **181/181** |
| Binary Reflection Suite (Release) | Release | DXIL + Vulkan 1.3 SPIR-V | `.\build\bin\Release\MorbiShaderReflectionTests.exe` | `0` | **PASSED** | **190/190** |
| Binary Reflection Suite (Debug) | Debug | DXIL + Vulkan 1.3 SPIR-V | `.\build-debug\bin\Debug\MorbiShaderReflectionTests.exe` | `0` | **PASSED** | **190/190** |
| RHI Descriptor Tests (Release) | Release | Vulkan 1.3 + DX12 | `.\build\bin\Release\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** |
| RHI Descriptor Tests (Debug) | Debug | Vulkan 1.3 + DX12 | `.\build-debug\bin\Debug\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** |
| RHI Handle & Hash Tests (Release) | Release | Vulkan 1.3 + DX12 | `.\build\bin\Release\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** |
| RHI Handle & Hash Tests (Debug) | Debug | Vulkan 1.3 + DX12 | `.\build-debug\bin\Debug\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** |
| Release CTest Suite (All Targets) | Release | Vulkan 1.3 + DX12 | `ctest --test-dir build -C Release --output-on-failure` | `0` | **8/8 PASSED** (`49.89 sec`) | All 8 CTest targets (`100%`) |
| Debug CTest Suite (Filtered, excl. `MorbiCSharpPhysicsTests`) | Debug | Vulkan 1.3 + DX12 | `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests --output-on-failure` | `0` | **7/7 PASSED** (`52.72 sec`) | 7/8 CTest targets (excludes pre-existing `BL-PHY-001`) |
| Editor Smoke Test — Vulkan 1.3 (Release) | Release | Vulkan 1.3 | `.\build\bin\Release\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all phases `[PASS]`) | Exit `0` |
| Editor Smoke Test — DX12 (Release) | Release | DirectX 12 | `.\build\bin\Release\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all phases `[PASS]`) | Exit `0` |
| Editor Smoke Test — Vulkan 1.3 (Debug) | Debug | Vulkan 1.3 | `.\build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all phases `[PASS]`) | Exit `0` |
| Editor Smoke Test — DX12 (Debug) | Debug | DirectX 12 | `.\build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all phases `[PASS]`) | Exit `0` |
| Linux Build & Runtime Tests | — | Vulkan 1.3 | — | — | **NOT VERIFIED** (Windows 11 host; no Linux toolchain/runner active; tracked in `BL-SHD-007`) | — |

---

## WP3 Test Coverage Summary (`MorbiPipelineLayoutTests`, 128/128 Assertions)

1. **Structural Normalization, Canonical Hash & Collision Guards:** Canonical normalization invariant under set/binding reordering & `DebugName` changes, `AreStructurallyEqual` mismatch checks across type/count/visibility (`Null`, `Vulkan`, `DX12`).
2. **Reflection → `PipelineLayoutBuilder` Parity:** Dual-backend VS+PS push/root constants (`BuildFromDualBackendReflections`), explicit non-identity `CrossBackendBindingPolicy` rules across CBV/SRV/UAV/Sampler + fixed `[4]` texture array cardinality, unmapped rule rejection when `FallbackToIdentitySetSpaceMapping = false`, `GlobalBindlessHybrid` real PBR VS+PS shader (Set 0 absorption + Root Constants + `BoneMatrices`), VS→PS interface mismatch rejection, and cross-stage conflicting binding type rejection.
3. **Cache Deduplication & Reference-Counted Destruction:** Cache hit increments `CacheHitCount` and returns same `PipelineLayoutHandle`; first `DestroyPipelineLayout` leaves handle alive for second owner (`CacheRefCount`); structurally unequal descriptor with distinct binding creates separate handle.
4. **DX12 Root Signature Limits & Formats:** Rejects `> 64 DWORD` Root Signature (`66 DWORDs`) and `CombinedImageSampler`; accepts exact `64 DWORD` Root Signature.
5. **Vulkan Limits, Feature Flags & Set Gaps:** Rejects `> MaxBoundDescriptorSets` (`33 > 32`), `> MaxPerStageSampledImages`, `> MaxPushConstantsBytes` (`260 > 128`), and simulated disabled `UpdateAfterBind`/`RuntimeDescriptorArray` features; cleanly handles non-contiguous set indices (Set 2 with omitted Set 0/1) via shared empty `VkDescriptorSetLayout`.
6. **Stale Layout Rejection & Deferred GPU Lifetime:** Rejects `CreateGraphicsPipeline` with destroyed/`PendingDestroy` `PipelineLayoutHandle`; preserves `IsResourceAlive(layout)` while referenced by live PSO (`PipelineRefCount`) OR unsubmitted `ICommandList` (`UnsubmittedRefCount`) across frame pumps until both release and GPU timeline/fence completes (`Null`, `Vulkan 1.3`, `DX12`).
7. **Live Multi-Layout Switching & GPU Execution (Vulkan 1.3 & DX12):** Records two distinct custom graphics PSOs (`32B` push constants → `64B` push constants) inside an offscreen `RGBA8_UNorm` `BeginRendering`/`EndRendering` pass + a custom compute PSO (`Dispatch(1, 1, 1)` + push constants) in one command list, submits, and synchronizes cleanly with active DX12 Debug Layer + DRED (0 errors).

---

## Known Environment Notes

- `VK_LAYER_KHRONOS_validation` is absent on this machine (`BL-ENV-001`); DX12 Debug Layer (`ID3D12InfoQueue1`) + DRED are active with zero errors.
- `MorbiCSharpPhysicsTests` crashes in Debug only due to pre-existing `BL-PHY-001` (`SEHException` in `SyncRigidBody`); Release `MorbiCSharpPhysicsTests` passed in CTest (`8/8`).
- Dedicated sub-agent tools were unavailable in this environment; code audit was performed directly across all WP3 source files.
- Linux compilation and `libdxcompiler.so` runtime verification remain **NOT VERIFIED** (`BL-SHD-007`).
