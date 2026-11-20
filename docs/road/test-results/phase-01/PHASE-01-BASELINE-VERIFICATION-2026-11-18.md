# Phase 1 Baseline Verification Record

**Date:** 2026-11-18  
**Machine / Hardware:** 12th Gen Intel(R) Core(TM) i5-12400F, NVIDIA GeForce RTX 4060 Ti (Driver `32.0.16.1656`)  
**Operating System:** Microsoft Windows 11 Home Single Language (`10.0.26200`, 64-bit)  
**Build Configuration:** Release (`build/`, CMake `4.5.0-rc1`, Visual Studio 18 2026 generator, C++20)  
**Graphics APIs Tested:** Vulkan 1.3 (`vulkan-1.dll`), DirectX 12 (`d3d12.dll`)

---

## Test Summary

| Test Name | Graphics API | Exact Command | Exit Code | Actual Result | Assertion Count |
|---|---|---|---|---|---|
| Release CTest Suite (5 targets) | Vulkan 1.3 + DX12 | `ctest --test-dir build -C Release --output-on-failure` | `0` | **5/5 PASSED** (47.45 sec) | — |
| RHI Descriptor Tests | Vulkan 1.3 + DX12 | `build\bin\Release\MorbiRHIDescriptorTests.exe` | `0` | **PASSED** | **860/860** (Vulkan: 430/430, DX12: 430/430) |
| RHI Generational Handle & Hash Tests | Vulkan 1.3 + DX12 | `build\bin\Release\MorbiRHIGenerationalHandleHashTests.exe` | `0` | **PASSED** | **507/507** (Vulkan: 240/240, DX12: 239/239, plus handle/hash unit checks) |
| Vulkan 1.3 Editor Smoke Test | Vulkan 1.3 | `build\bin\Release\MorbiEditor.exe --smoke-test --api vulkan` | `0` | **PASSED** (all engine phases `[PASS]`, clean shutdown) | — |
| DirectX 12 Editor Smoke Test | DirectX 12 | `build\bin\Release\MorbiEditor.exe --smoke-test --api dx12` | `0` | **PASSED** (all engine phases `[PASS]`, clean shutdown) | — |
| Debug Build & Debug Tests | — | — | — | **NOT RUN** (`build-debug/` directory does not exist on disk) | — |
| Linux Build & Tests | — | — | — | **NOT RUN** (Windows host; no Linux toolchain active in session) | — |

---

## CTest Breakdown (`ctest --test-dir build -C Release`)

1. `MorbiPhysicsTests` — Passed (4.49 sec)
2. `MorbiCSharpPhysicsTests` — Passed (8.66 sec)
3. `MorbiNetworkTests` — Passed (16.15 sec)
4. `MorbiRHIDescriptorTests` — Passed (7.93 sec)
5. `MorbiRHIGenerationalHandleHashTests` — Passed (10.20 sec)

---

## GPU Validation & Environment Notes

- **Vulkan 1.3:** `VK_LAYER_KHRONOS_validation not found on this machine (install Vulkan SDK for full validation).` `VK_EXT_debug_utils` messenger and object naming active.
- **DirectX 12:** In `MorbiRHIDescriptorTests` and `MorbiRHIGenerationalHandleHashTests`, `D3D12 Debug Layer` and `DRED` (`AutoBreadcrumbs=FORCED_ON`, `PageFault=FORCED_ON`, `ID3D12InfoQueue1` callback) were active and passed without errors.
- **DXC Baseline Observed in Editor Smoke Test:** `dxcompiler.dll` version `1.9(5191-d355aa83)(1.9.2602.24)`. Smoke test compiled/cached `FullscreenComposite.hlsl` -> DXIL (3024 bytes) & SPIR-V (784 bytes), `Cache Stats (Compiles=0, DiskHits=3, MemHits=1)`.
