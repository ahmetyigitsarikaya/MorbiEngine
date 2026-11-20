# Phase 2 WP1 — Shader Compilation Infrastructure Verification Record

**Date:** 2026-11-18  
**Machine / Hardware:** 12th Gen Intel(R) Core(TM) i5-12400F, NVIDIA GeForce RTX 4060 Ti (Driver `32.0.16.1656`)  
**Operating System:** Microsoft Windows 11 Home Single Language (`10.0.26200`, 64-bit)  
**Toolchain:** CMake `4.5.0-rc1`, Visual Studio 18 2026 (`MSVC 19.51.36260.0`), C++20, DXC `1.9 (5191-d355aa83) [1.9.2602.24]`

---

## Test Execution Matrix (Post-Final Edit)

| Test / Check | Config | Graphics API | Exact Command | Exit Code | Actual Result | Assertion Count |
|---|---|---|---|---|---|---|
| Shader Unit & Stress Tests (Release) | Release | DXIL + Vulkan 1.3 SPIR-V | `build\bin\Release\MorbiShaderTests.exe` | `0` | **PASSED** | **181/181** |
| Shader Unit & Stress Tests (Debug) | Debug | DXIL + Vulkan 1.3 SPIR-V | `build-debug\bin\Debug\MorbiShaderTests.exe` | `0` | **PASSED** | **181/181** |
| Release CTest Suite (6 targets) | Release | Vulkan 1.3 + DX12 | `ctest --test-dir build -C Release --output-on-failure` | `0` | **6/6 PASSED** (54.69s) | — |
| Debug CTest Suite (filtered, excluding `MorbiCSharpPhysicsTests`) | Debug | Vulkan 1.3 + DX12 | `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests --output-on-failure` | `0` | **5/5 PASSED** (58.74s) | — |
| Debug `MorbiCSharpPhysicsTests` (Direct) | Debug | N/A (Physics/CoreCLR) | `build-debug\bin\Debug\MorbiCSharpPhysicsTests.exe` | Pre-existing crash | **FAILED** (`SEHException` in `SyncRigidBody` / `JointAndCompoundAuditDriver.OnStart()`, unrelated to `Morbi.Shader`; see `BL-PHY-001`) | — |
| RHI Descriptor Tests (Release) | Release | Vulkan 1.3 + DX12 | `build\bin\Release\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** (Vulkan 430/430, DX12 430/430) |
| RHI Descriptor Tests (Debug) | Debug | Vulkan 1.3 + DX12 | `build-debug\bin\Debug\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** (Vulkan 430/430, DX12 430/430) |
| RHI Handle & Hash Tests (Release) | Release | Vulkan 1.3 + DX12 | `build\bin\Release\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** (Vulkan 240/240, DX12 239/239) |
| RHI Handle & Hash Tests (Debug) | Debug | Vulkan 1.3 + DX12 | `build-debug\bin\Debug\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** (Vulkan 240/240, DX12 239/239) |
| Editor Smoke Test — Vulkan 1.3 (Release) | Release | Vulkan 1.3 | `build\bin\Release\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all phases `[PASS]`, clean shutdown) | — |
| Editor Smoke Test — DX12 (Release) | Release | DirectX 12 | `build\bin\Release\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all phases `[PASS]`, clean shutdown) | — |
| Editor Smoke Test — Vulkan 1.3 (Debug) | Debug | Vulkan 1.3 | `build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all phases `[PASS]`, clean shutdown) | — |
| Editor Smoke Test — DX12 (Debug) | Debug | DirectX 12 | `build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all phases `[PASS]`, clean shutdown) | — |
| Linux Build & Runtime Tests | — | Vulkan 1.3 | — | — | **NOT VERIFIED** (Windows 11 host; no Linux toolchain/runner active) | — |

---

## Shader Model 6.0–6.6 & SPIR-V Validation Breakdown

Verified individually inside `MorbiShaderTests` (`Test_ShaderModels60To66AndVulkan13Validation`):
- **Models tested:** `SM6_0`, `SM6_1`, `SM6_2`, `SM6_3`, `SM6_4`, `SM6_5`, `SM6_6` (`vs_6_0`..`vs_6_6`, `ps_6_0`..`ps_6_6`, `cs_6_0`..`cs_6_6`).
- **Targets:** DXIL (`DXBC` magic verified) and Vulkan 1.3 SPIR-V (`-fspv-target-env=vulkan1.3`, `0x07230203` magic verified, validated via `spirv-val.exe --target-env vulkan1.3`).
- **All 7 Shader Models x 3 Stages x 2 Targets (42 compilations + 21 `spirv-val` validations):** PASSED in both Release and Debug.

## Environment Limitations

- `VK_LAYER_KHRONOS_validation` is not installed on this host (`MorbiRHIDescriptorTests` logs warning); DX12 Debug Layer and DRED (`AutoBreadcrumbs=FORCED_ON`, `PageFault=FORCED_ON`) are active and passed.
- Linux compilation and `libdxcompiler.so` runtime execution are **NOT VERIFIED** on this Windows host.
