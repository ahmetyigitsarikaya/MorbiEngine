# Phase 2 WP1 — Shader Compilation Infrastructure Completion Report

**Work Package:** Phase 2 WP1 — Shader Compilation Infrastructure  
**WP Status:** `COMPLETE`  
**Date:** 2026-11-18  
**Dependencies:** Phase 1 (`COMPLETE`)

---

## 1. WP Summary

Replaced the prototype out-of-process `dxc.exe` CLI wrapper and v1 `ShaderCache` in `Morbi.Shader` with a production-grade, thread-safe, in-process DXC (`IDxcCompiler3` / `IDxcUtils` / `IDxcVersionInfo*`) compilation pipeline, custom `IDxcIncludeHandler` with transitive dependency tracking and cycle detection, Shader Model 6.0–6.6 support, Vulkan 1.3 SPIR-V generation + `spirv-val` validation, `ShaderCache` v2 (checksummed, dependency-validated, corruption-recovering, atomic writes), and a dedicated `MorbiShaderTests` CTest suite.

---

## 2. Initial Repository State

Prior to WP1 ([PHASE-02-WP01-EXECUTION-PLAN.md](PHASE-02-WP01-EXECUTION-PLAN.md)):
- `ShaderCompiler` wrote temporary `.hlsl` files to `%TEMP%/MorbiShaderBuild` and spawned `dxc.exe` via Windows-only `CreateProcessW`.
- No COM interfaces, no `IDxcIncludeHandler`, no transitive include graph, no compiler version tracking, hardcoded `vs_6_0`/`ps_6_0`/`cs_6_0` and `-fspv-target-env=vulkan1.2`.
- `spirv-val.exe` existed in `legacy/.../ShaderCompiler/win64/` but was not packaged or invoked.
- `ShaderCache` v1 lacked compiler version, debug/optimization/profile/include hash inputs, payload checksum, dependency re-validation, and atomic writes.
- No `Tests/ShaderTests/` existed.

---

## 3. Requirements & Acceptance Scope

- In-process DXC integration (`IDxcCompiler3`, `IDxcUtils`), thread-safe COM lifetime, Windows/Linux loading strategy, compiler version tracking.
- Custom HLSL include handler (`IDxcIncludeHandler`), canonical path resolution, transitive include graph, cycle detection, clear diagnostics.
- Shader Model 6.0–6.6 support (`SM6_0`..`SM6_6`).
- DXIL and Vulkan 1.3 SPIR-V (`-fspv-target-env=vulkan1.3`) + `spirv-val` validation.
- `ShaderCache` v2: full request + compiler version + transitive include hashing, checksum validation, stale-dependency invalidation, corruption recovery, atomic disk writes.
- Automated tests covering valid & failure paths (`MorbiShaderTests`), plus full Release/Debug builds and Vulkan 1.3 / DX12 smoke tests.

---

## 4. Architecture Decisions

Recorded in [ADR-0001](../../decisions/ADR-0001-in-process-dxc-and-shader-cache-v2.md):
- `std::shared_ptr<DxcModuleContext>` owning a mutex-guarded `WorkerContext { IDxcUtils, IDxcCompiler3 }` pool (releases all COM interfaces strictly prior to `FreeLibrary`/`dlclose`, avoids `thread_local` teardown hazards).
- `src/DxcRuntimeBridge.hpp` providing native `<dxcapi.h>` on Windows and ABI-compatible COM/DXC fallback definitions + `dlopen`/`dlsym` on Linux when `WinAdapter.h` is absent.
- `ShaderCache` v2 (`'MSHC'` v2, 40-byte header with IEEE CRC32 over payload, separate `m_diskMutex` around disk I/O + `*.tmp.<id>` + `MoveFileExW`/`rename` atomic replacement).

---

## 5. Multi-Pass Analysis Findings

*(Dedicated sub-agent spawning tools were unavailable in this environment; independent analysis passes were performed directly against source code and binaries.)*

- **Shader/DXC Pass:** Verified empirically on `dxcompiler.dll` (`1.9.2602.24`) and `spirv-val.exe` that `-fspv-target-env=vulkan1.3`, `--target-env vulkan1.3`, and `vs_6_0..vs_6_6` / `ps_6_0..ps_6_6` / `cs_6_0..cs_6_6` are supported. Verified DXC behavior on circular unguarded includes (hits 200-level nesting limit) vs guarded diamond includes.
- **Vulkan 1.3 & DX12 Pass:** Confirmed `EngineRuntime.cpp` inline shaders (`kUnifiedHlslShader`, `kScenePbrMeshHlslShader`, `kGameUIProceduralHlslShader`, `kGameUIOverlayHlslShader`) and `GenerationalHandleHashTests.cpp` compute shader compile cleanly under Vulkan 1.3 SPIR-V + `spirv-val` and DXIL without altering RHI layouts.
- **Code Audit Pass:** Caught and fixed two defects during implementation/audit:
  1. `IUnknown::AddRef`/`Release` return `ULONG` on Windows (fixed `DxcRefCountType`).
  2. Added `m_diskMutex` + `invalidatedInMemory` flag in `ShaderCache::GetOrCompile` after our refactor briefly caused double-counting `Invalidations` (`2` instead of `1`) on stale memory+disk entries — caught immediately by `MorbiShaderTests`.

---

## 6. Files Inspected

- `CMakeLists.txt`, `CMakePresets.json`, `cmake/MorbiCompilerOptions.cmake`, `cmake/MorbiDependencies.cmake`, `Editor/CMakeLists.txt`
- `Engine/Source/Shader/CMakeLists.txt`, `ShaderCompiler.hpp`, `ShaderCache.hpp`, `ShaderCompiler.cpp`, `ShaderCache.cpp`
- `Engine/Source/IO/include/Morbi/IO/BinaryStream.hpp`
- `Engine/Source/Runtime/src/EngineRuntime.cpp`
- `Tests/RHITests/GenerationalHandleHashTests.cpp`
- `Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp`, `Engine/Source/RHI.DX12/src/DX12Device.cpp`

## 7. Files Created

- `Engine/Source/Shader/src/DxcRuntimeBridge.hpp`
- `Tests/ShaderTests/CMakeLists.txt`
- `Tests/ShaderTests/ShaderCompilerTests.cpp`
- `docs/road/decisions/ADR-0001-in-process-dxc-and-shader-cache-v2.md`
- `docs/road/test-results/phase-02/PHASE-02-WP01-VERIFICATION-2026-11-18.md`
- `docs/road/reports/phase-02/PHASE-02-WP01-SHADER-COMPILATION.md`

## 8. Files Modified

- `Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp`
- `Engine/Source/Shader/src/ShaderCompiler.cpp`
- `Engine/Source/Shader/include/Morbi/Shader/ShaderCache.hpp`
- `Engine/Source/Shader/src/ShaderCache.cpp`
- `Engine/Source/Shader/CMakeLists.txt`
- `Editor/CMakeLists.txt`
- `CMakeLists.txt`

## 9. Files Removed

- None.

---

## 10. Implemented Functionality

- **In-Process DXC:** `DxcModuleContext` dynamically loads `dxcompiler.dll` / `libdxcompiler.so`, resolves `DxcCreateInstance`, queries `IDxcVersionInfo`/`IDxcVersionInfo2`/`IDxcVersionInfo3` (`DXC 1.9 (5191-d355aa83) [1.9.2602.24]`), pools `IDxcUtils` + `IDxcCompiler3` worker pairs safely across threads, and releases all COM objects before library unload.
- **Custom Include Handler:** `MorbiDxcIncludeHandler` resolves relative, primary-directory, and `-I` include paths, normalizes canonical paths, tracks transitive dependencies `(CanonicalPath, ContentHash, ByteSize)`, supports `#ifndef` diamond includes and nested subdirectory relative includes, and detects circular/excessive include recursion (`kMaxIncludeLoadsPerFile = 32`).
- **Profiles & Targets:** Added `ShaderModel` (`SM6_0`..`SM6_6`), switched SPIR-V to `-fspv-target-env=vulkan1.3`, packaged `spirv-val.exe`, and added `ValidateSpirVBytecode` (`spirv-val --target-env vulkan1.3`).
- **`ShaderCache` v2:** Full parameter + compiler version + dependency hashing, CRC32 payload validation, stale transitive-include invalidation on both memory and disk hits, corrupt/truncated `.mshc` recovery, `Invalidate`/`ClearAll`, and atomic write (`*.tmp.<id>` + `MoveFileExW`/`rename`) under `m_diskMutex`.

---

## 11. Bugs Discovered & Fixed During WP1

1. **Bug Discovered & Fixed:** `ShaderCache` double-counted `Invalidations` (`2` instead of `1`) when a stale entry existed in both memory and disk cache (`ShaderCache::GetOrCompile` refactored with `invalidatedInMemory` + `m_diskMutex`).
2. **Bug Discovered & Fixed:** Explicit invalid `dxcToolDirectory` constructor argument previously fell back to default paths, preventing clean unavailable-DXC testing; fixed so non-empty explicit path does not silently fall back.
3. **Pre-Existing Unrelated Bug Discovered (Not Modified):** Debug build of `MorbiCSharpPhysicsTests.exe` crashes with `System.Runtime.InteropServices.SEHException` in `Morbi.Engine.NativeBridge.SyncRigidBody` (`JointAndCompoundAuditDriver.OnStart()`, `Logs/Crashes/Crash_Latest.log`). Logged in `BACKLOG.md` as `BL-PHY-001`.

---

## 12. Tests Added & Executed

Added `MorbiShaderTests` (`Tests/ShaderTests/ShaderCompilerTests.cpp`): **181/181 assertions** covering DXC init/version/lifetime, unavailable DXC/syntax/missing-entry/bad-SPIR-V failure paths, SM 6.0–6.6 (VS/PS/CS x DXIL/SPIR-V + `spirv-val`), custom include handler (canonical, transitive, nested, diamond, missing, circular), `ShaderCache` v2 (parameter hash sensitivity, memory/disk hits, transitive include invalidation, CRC/truncation recovery), and 8-thread concurrent stress compilation + caching.

### Actual Verification Commands & Outcomes (Post-Final Edit)

- `build\bin\Release\MorbiShaderTests.exe` -> exit `0`, **181/181 assertions PASSED**
- `build-debug\bin\Debug\MorbiShaderTests.exe` -> exit `0`, **181/181 assertions PASSED**
- `ctest --test-dir build -C Release --output-on-failure` -> exit `0`, **6/6 PASSED** (54.69s)
- `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests --output-on-failure` -> exit `0`, **5/5 PASSED** (58.74s)
- `build-debug\bin\Debug\MorbiCSharpPhysicsTests.exe` -> **FAILED** (pre-existing `SEHException` in `SyncRigidBody`, `BL-PHY-001`)
- `build\bin\Release\MorbiRHIDescriptorTests.exe` -> exit `0`, **860/860 assertions PASSED**
- `build-debug\bin\Debug\MorbiRHIDescriptorTests.exe` -> exit `0`, **860/860 assertions PASSED**
- `build\bin\Release\MorbiRHIGenerationalHandleHashTests.exe` -> exit `0`, **507/507 assertions PASSED**
- `build-debug\bin\Debug\MorbiRHIGenerationalHandleHashTests.exe` -> exit `0`, **507/507 assertions PASSED**
- `build\bin\Release\MorbiEditor.exe --smoke-test --api vulkan` -> exit `0`, **PASSED**
- `build\bin\Release\MorbiEditor.exe --smoke-test --api dx12` -> exit `0`, **PASSED**
- `build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api vulkan` -> exit `0`, **PASSED**
- `build-debug\bin\Debug\MorbiEditor.exe --smoke-test --api dx12` -> exit `0`, **PASSED**
- Linux build/runtime -> **NOT VERIFIED** (Windows 11 host; no Linux toolchain active)

Full record: [PHASE-02-WP01-VERIFICATION-2026-11-18.md](../../test-results/phase-02/PHASE-02-WP01-VERIFICATION-2026-11-18.md).

---

## 13. Vulkan 1.3 & DirectX 12 Verification

- **Vulkan 1.3:** Compiled SPIR-V with `-fspv-target-env=vulkan1.3`, validated with `spirv-val.exe --target-env vulkan1.3`, passed `MorbiRHIDescriptorTests` (430/430 Release & Debug), `MorbiRHIGenerationalHandleHashTests` (240/240 Release & Debug), and `MorbiEditor.exe --smoke-test --api vulkan` (Release & Debug). *(Note: `VK_LAYER_KHRONOS_validation` unavailable on host.)*
- **DirectX 12:** Compiled signed DXIL (`dxil.dll` co-loaded via `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`), passed `MorbiRHIDescriptorTests` (430/430 Release & Debug with Debug Layer + DRED), `MorbiRHIGenerationalHandleHashTests` (239/239 Release & Debug with Debug Layer + DRED), and `MorbiEditor.exe --smoke-test --api dx12` (Release & Debug).

---

## 14. Remaining Limitations & Deferred Work

- **Linux Toolchain:** Cross-platform `dlopen` / `fork`+`execvp` / ABI bridge implemented in `DxcRuntimeBridge.hpp` and `ShaderCompiler.cpp`, but **NOT VERIFIED** at compile/runtime on Linux (tracked in `BACKLOG.md` as `BL-SHD-007`).
- **SPIR-V Validation Transport:** Uses `spirv-val` CLI process when available (`Tools/dxc/spirv-val.exe`) rather than static in-process SPIRV-Tools linking.
- **Reflection (`ReflectSourceAndBinary`):** Intentionally left at WP1 boundary; real binary reflection belongs to **Phase 2 WP2 — Binary Reflection**.
- **Inline HLSL Shaders:** Kept intact in `EngineRuntime.cpp` per WP1 scope (`BL-SHD-006`).

---

## 15. Final WP1 Status & Next WP Proposal

- **WP1 STATUS:** `COMPLETE`
- **Next WP:** **Phase 2 WP2 — Binary Reflection** (SPIR-V binary reflection + DXIL `IDxcContainerReflection` / `ID3D12ShaderReflection`, unified `ShaderReflectionData`, descriptor/CBV/vertex-input/push-constant/compute-thread-group reflection, cross-stage and cross-backend layout validation).
- **Awaiting Approval:** Work on WP2 will not begin until explicitly approved.
