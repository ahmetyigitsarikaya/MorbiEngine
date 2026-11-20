# Phase 2 WP1 — Shader Compilation Infrastructure: Source Inspection & Execution Plan

**Work Package:** Phase 2 WP1 — Shader Compilation Infrastructure  
**Status:** `IN PROGRESS` (Planning & Verification Complete — **Awaiting Approval Before Implementation**)  
**Date:** 2026-11-18  
**Dependencies:** Phase 1 (`COMPLETE`)

---

## 1. Purpose & Methodology

Per Section 7 and Section 13 of the Master Roadmap Execution Directive, this document captures:
1. Direct source-code inspection of the existing `Morbi.Shader` module, RHI backend assumptions, CMake toolchain, and runtime consumers.
2. Independent multi-pass analysis across Architecture, Vulkan 1.3, DirectX 12, Shader/DXC, QA/Testing, and Code Audit perspectives. *(Note: Dedicated sub-agent spawning tools were unavailable in this execution environment; all passes were conducted directly by inspecting source files and verifying behavior against builds and tests.)*
3. A surgical implementation plan for Phase 2 WP1 ready for review and approval.

---

## 2. Step 1 — Source Inspection Findings

### Inspected Files
- [CMakeLists.txt](../../../../CMakeLists.txt)
- [CMakePresets.json](../../../../CMakePresets.json)
- [cmake/MorbiCompilerOptions.cmake](../../../../cmake/MorbiCompilerOptions.cmake)
- [cmake/MorbiDependencies.cmake](../../../../cmake/MorbiDependencies.cmake)
- [Editor/CMakeLists.txt](../../../../Editor/CMakeLists.txt)
- [Engine/Source/Shader/CMakeLists.txt](../../../../Engine/Source/Shader/CMakeLists.txt)
- [Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp](../../../../Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp)
- [Engine/Source/Shader/include/Morbi/Shader/ShaderCache.hpp](../../../../Engine/Source/Shader/include/Morbi/Shader/ShaderCache.hpp)
- [Engine/Source/Shader/src/ShaderCompiler.cpp](../../../../Engine/Source/Shader/src/ShaderCompiler.cpp)
- [Engine/Source/Shader/src/ShaderCache.cpp](../../../../Engine/Source/Shader/src/ShaderCache.cpp)
- [Engine/Source/Runtime/src/EngineRuntime.cpp](../../../../Engine/Source/Runtime/src/EngineRuntime.cpp) (lines 140–380, 1154–1211, 1835–1850)
- [Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp](../../../../Engine/Source/RHI.Vulkan/src/VulkanDevice.cpp) (lines 650–775, 1975–2200, 2621–2744)
- [Engine/Source/RHI.DX12/src/DX12Device.cpp](../../../../Engine/Source/RHI.DX12/src/DX12Device.cpp) (lines 1487–1600, 1883–1956, 2130–2165)

### Current Implementation Gap Matrix

| WP1 Requirement | Current Repository Reality | Gap Severity |
|---|---|---|
| In-process DXC (`IDxcCompiler3` & `IDxcUtils`) | `ShaderCompiler.cpp:272-373` writes temporary `.hlsl` to `%TEMP%/MorbiShaderBuild`, spawns `dxc.exe` via `CreateProcessW`, reads output file, deletes temp files. | High |
| Thread-safe COM lifetime & cross-platform loading | No COM usage at all; `#if defined(_WIN32)` around `RunProcessCapture` and `GetModuleFileNameW` leaves Linux completely unimplemented. | High |
| Compiler version tracking | Not queried or stored anywhere. `dxcompiler.dll` on disk is `1.9(5191-d355aa83)(1.9.2602.24)`. | High |
| Custom HLSL include handler & dependency graph | Relies on `dxc.exe` `-I` arguments only; no `IDxcIncludeHandler`, no canonical path resolution, no transitive include graph, no cycle detection. | High |
| Shader Model 6.0–6.6 | `StageToTargetProfile()` hardcodes `vs_6_0`, `ps_6_0`, `cs_6_0`. | Medium |
| Vulkan 1.3 SPIR-V & validation | Hardcodes `-spirv -fspv-target-env=vulkan1.2` while `VulkanDevice.cpp:764` creates `VK_API_VERSION_1_3`. `spirv-val.exe` exists in `legacy/.../win64/` but is not packaged or invoked. | High |
| Deterministic request hashing | `ComputeRequestHash()` (`ShaderCompiler.cpp:163-178`) hashes only `SourceCode`, `EntryPoint`, `Stage`, `TargetFormat`, `EnableOptimization`, and `Defines`. It omits `VirtualFilePath`, `GenerateDebugInfo`, `IncludeDirs`, shader model, compiler version, and transitive include contents. | High |
| `ShaderCache` v2 | `ShaderCache.cpp:12-13` is version `1` (`'MSHC'`). No compiler version, no dependency manifest, no disk-hit dependency re-validation, no payload CRC/checksum. | High |
| Shader compilation tests | `Tests/ShaderTests/` does not exist; only `EngineRuntime.cpp:1154-1211` exercises compilation in smoke tests. | High |

### Available Tooling & Headers Observed on Disk
- **DXC Binaries:** `legacy/FlueperEngine/Legacy/FlueEngine/tools/ShaderCompiler/win64/` contains `dxcompiler.dll` (`1.9.2602.24`), `dxil.dll`, `dxc.exe`, `spirv-val.exe`, `spirv-dis.exe`, `spirv-opt.exe`. `Editor/CMakeLists.txt:99-113` already copies `dxc.exe`, `dxcompiler.dll`, `dxil.dll` to `<TargetDir>/Tools/dxc/`.
- **DXC Header (`dxcapi.h`):** Available in Windows SDK `10.0.26100.0\um\dxcapi.h` and `ThirdParty/Tools/zig/lib/libc/include/any-windows-any/dxcapi.h`. For Linux builds, `dxcapi.h` + WinAdapter compatibility or clean minimal COM interface definitions can be provided via `cmake/MorbiDependencies.cmake` / `Engine/Source/Shader/`.

---

## 3. Step 2 — Multi-Pass Architecture Verification

### Pass A — Architecture & Module Boundary Review
- `Morbi.Shader` links `Morbi::Core` and `Morbi::IO` (`Engine/Source/Shader/CMakeLists.txt`). Keep `Morbi.Shader` decoupled from RHI backends (`RHI.Vulkan` / `RHI.DX12`).
- Existing callers (`EngineRuntime.cpp:1162-1209`, `EngineRuntime.cpp:1838-1847`) use:
  - `ShaderCompiler(dxcDir)`
  - `ShaderCache(shaderCacheDir, dxcCompiler)`
  - `ShaderCompileRequest` aggregate initialization (`SourceCode`, `VirtualFilePath`, `EntryPoint`, `Stage`, `TargetFormat`, optional `EnableOptimization`, `GenerateDebugInfo`, `Defines`, `IncludeDirs`)
  - `shaderCache.GetOrCompile(req)`, `shaderCache.GetStats()`
  - `CompiledShaderArtifact` (`Stage`, `TargetFormat`, `ContentHash`, `Bytecode`, `Reflection`, `CompilerMessages`)
- **Backward Compatibility Constraint:** Adding new fields to `ShaderCompileRequest` (e.g., `ShaderModel`, `ValidateSpirV`) and `CompiledShaderArtifact` (e.g., `IncludeDependencies`, `CompilerVersion`) must preserve aggregate-initialization compatibility (default member initializers at the end of structs) so existing call sites in `EngineRuntime.cpp` compile cleanly without unrelated refactoring.
- **Note on `ReflectSourceAndBinary`:** WP2 owns real binary reflection. During WP1, keep `ReflectSourceAndBinary` intact (do not attempt to sneak WP2 into WP1), while ensuring `CompiledShaderArtifact` continues to populate `Reflection` cleanly for existing `EngineRuntime.cpp` consumers.

### Pass B — Vulkan 1.3 Specialist Review
- `VulkanDevice.cpp:764` uses `VK_API_VERSION_1_3`. Compiling SPIR-V with `-fspv-target-env=vulkan1.3` aligns SPIR-V target capabilities with the runtime device.
- **Binding Model Preservation:** `VulkanDevice.cpp:2621-2744` binds a single Descriptor Set 0 combining:
  - `binding 0` -> `STORAGE_BUFFER[4096]` (`t0, space0`)
  - `binding 1` -> `SAMPLED_IMAGE[4096]` (`t0, space1`)
  - `binding 2` -> `SAMPLER[1]` (`s0, space0`)
  - `binding 3` -> `UNIFORM_BUFFER[256]` (`b1, space0`)
  - `VkPushConstantRange` 128 bytes (`VK_SHADER_STAGE_ALL`)
- `ShaderCompiler` must continue passing `-D MORBI_VULKAN=1` for `SPIRV` and must **not** alter `[[vk::binding(...)]]` / `-fvk-*-shift` semantics in a way that breaks `EngineRuntime.cpp` shaders.
- **SPIR-V Validation:** Running `spirv-val` (with `--target-env vulkan1.3`) on generated SPIR-V bytecode catches invalid SPIR-V before `vkCreateShaderModule`. Note: if `spirv-val` fails, compilation must return `Failure` with the validator diagnostic.

### Pass C — DirectX 12 Specialist Review
- `DX12Device.cpp:1883-1956` binds Root Constants (`b0, space0`), Bindless Descriptor Table (`t0, space0` + `t0, space1`), Direct Root CBV (`b1, space0`), and Static Sampler (`s0, space0`).
- `dxcompiler.dll` requires `dxil.dll` in the same directory (or loadable path) to sign DXIL containers; without signing (`dxil.dll` missing), D3D12 `CreateGraphicsPipelineState` / `CreateComputePipelineState` will reject unsigned DXIL in release mode! In-process `DxcCreateInstance` must load `dxcompiler.dll` from `m_dxcDirectory` (and ensure `dxil.dll` is co-located so `dxcompiler.dll` finds it automatically, verified on `Windows` with `SetDllDirectoryW` / `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`).

### Pass D — Shader / DXC Specialist Review
- **In-Process DXC Lifecycle:**
  - Load `dxcompiler.dll` (`LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS` on Windows; `dlopen("libdxcompiler.so", RTLD_NOW | RTLD_LOCAL)` on Linux) and resolve `DxcCreateInstance`.
  - Keep a reference-counted shared library holder (`DxcLibraryModule`) inside `ShaderCompiler` so copies of `ShaderCompiler` (note: `ShaderCache` takes `ShaderCompiler` by value in `ShaderCache(path, ShaderCompiler)`!) do not prematurely `FreeLibrary`/`dlclose` while `IDxcCompiler3`/`IDxcUtils` COM objects are alive!
  - **Thread-Safety:** Rather than sharing mutable COM state unsafely across threads, either create thread-local `IDxcUtils` + `IDxcCompiler3` pairs keyed to the `DxcLibraryModule` or guard factory creation and create lightweight per-compile `IDxcIncludeHandler` + `IDxcResult`. Verify DXC thread safety under concurrent `TaskScheduler` / `std::thread` stress tests.
  - **Version Tracking:** Query `IDxcVersionInfo` (`GetVersion(&major, &minor)`) and `IDxcVersionInfo2` (`GetCommitInfo(&commitCount, &commitHash)`) on `IDxcCompiler3` to produce a deterministic `CompilerVersionString` and `CompilerVersionHash`.
- **Include System (`MorbiDxcIncludeHandler`):**
  - Implements `IDxcIncludeHandler` (`LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource)`).
  - Resolves `#include` relative to: (1) parent directory of the including file (if known) or `VirtualFilePath` parent, then (2) `request.IncludeDirs` in order.
  - Normalizes to canonical path (`std::filesystem::weakly_canonical` / `lexically_normal`) and rejects path traversal / missing files with clear error tracking.
  - **Cycle Detection:** Tracks active include recursion or tracks visited paths + `#pragma once`? Wait: standard C/HLSL `#include` without include guards can legitimately include a file twice if guarded by `#ifndef` (or once with `#pragma once`), whereas an unguarded recursive cycle (`A -> B -> A` infinitely) would hang/overflow. DXC's preprocessor itself handles `#pragma once` and `#ifndef` guards and calls `LoadSource` only when actually `#include`-ing; however, tracking active include depth / detecting infinite self-inclusion (e.g., max include depth limit + tracking resolved include dependencies cleanly) protects against cyclic includes without breaking valid guarded multiple-includes! We should verify exact DXC `IDxcIncludeHandler::LoadSource` call behavior in tests (both `#pragma once` and true cycle `a.hlsl -> b.hlsl -> a.hlsl`).
  - Records every resolved include `(CanonicalPath, FileSize, ContentHash)` deterministically in `CompiledShaderArtifact::IncludeDependencies`.
- **Shader Model 6.0–6.6:**
  - Add `enum class ShaderModel : u8 { SM6_0 = 0, SM6_1, SM6_2, SM6_3, SM6_4, SM6_5, SM6_6 };` (default `SM6_0` for compatibility, or configurable per request).
  - Map `(ShaderStage, ShaderModel)` -> `"vs_6_0" .. "vs_6_6"`, `"ps_6_0" .. "ps_6_6"`, `"cs_6_0" .. "cs_6_6"`.

### Pass E — QA & Code Audit Review
- **Cache v2 Validation & Corruption Resistance:**
  - `ShaderCache` v2 binary format (`'MSHC'`, version `2`) must store:
    - Magic + Version + RequestHash + CompilerVersionHash + PayloadCRC32 (or 64-bit FNV/CRC integrity tag covering the serialized payload).
    - Recorded transitive include dependencies (`CanonicalPath` + `ContentHash`).
  - On `TryLoadFromDisk`:
    1. Reject truncated files, bad magic/version, or checksum mismatch (treat as cache miss + overwrite/quarantine corrupt file).
    2. Verify `CompilerVersionHash` matches active `ShaderCompiler` version.
    3. Re-compute content hash of every recorded transitive include dependency from disk; if any included file is missing or modified, **invalidate** the cache hit and recompile.
  - Note: What if the caller passed `CompileFromFile` vs inline `SourceCode` with `#include`? `CompileFromFile` sets `SourceFilePath` / `VirtualFilePath` and `IncludeDirs`; inline `SourceCode` hashes `SourceCode` directly and any `#include`d files via `IncludeDirs`.
- **Testing Plan (Dedicated `Tests/ShaderTests/MorbiShaderTests` CTest Target):**
  1. In-process DXC initialization & compiler version query (`major.minor` non-zero).
  2. Dual-target compilation (DXIL + Vulkan 1.3 SPIR-V) across SM 6.0 and SM 6.6.
  3. SPIR-V validation pass on valid shader & failure detection on invalid SPIR-V/HLSL.
  4. Custom include handler: nested includes (`main -> A -> B`), `#pragma once`, include search paths, missing include diagnostic, include cycle detection.
  5. Cache v2 tests: memory hit, disk hit, cache invalidation when transitive `#include` file changes on disk, cache invalidation when define / optimization / debug info / shader model / compiler version changes, corrupt/truncated `.mshc` recovery.
  6. Thread-safety stress test: concurrent multi-threaded compilation of multiple shaders across threads without COM crashes, leaks, or corrupt outputs.
  7. Repeated init/shutdown stress test of `ShaderCompiler` and `ShaderCache`.
  8. Full regression check: Release & Debug builds, CTest (`Physics`, `Network`, `RHI`, `Shader`), Vulkan 1.3 and DX12 Editor smoke tests.

---

## 4. Step 3 — Concrete Implementation Plan (Awaiting Approval)

### Files Expected to Modify
1. `Engine/Source/Shader/CMakeLists.txt` — Configure `dxcapi.h` include path discovery (Windows SDK / cross-platform), link platform dynamic loader (`dl` on Linux), and expose `MORBI_DEFAULT_DXC_DIR` / `MORBI_SPIRV_VAL_PATH` if needed.
2. `Engine/Source/Shader/include/Morbi/Shader/ShaderCompiler.hpp` — Add `ShaderModel` (`SM6_0`..`SM6_6`), `ShaderIncludeDependency`, `CompilerVersion` metadata, `ValidateSpirV` option, thread-safe shared DXC runtime handle, and updated `ComputeRequestHash` / APIs while preserving backward-compatible aggregate initialization.
3. `Engine/Source/Shader/src/ShaderCompiler.cpp` — Replace `CreateProcessW` + temp files with in-process `DxcCreateInstance` (`IDxcCompiler3`, `IDxcUtils`, `IDxcVersionInfo`), custom `IDxcIncludeHandler` with canonical path resolution & transitive include tracking & cycle guard, SM 6.0–6.6 profiles, `-fspv-target-env=vulkan1.3`, `spirv-val` integration, and Windows/Linux dynamic loading.
4. `Engine/Source/Shader/include/Morbi/Shader/ShaderCache.hpp` & `src/ShaderCache.cpp` — Upgrade to `ShaderCache` v2: integrity checksum, compiler version check, transitive include dependency serialization & disk-hit re-validation, explicit invalidation/corruption recovery, and cache key completeness.
5. `Editor/CMakeLists.txt` & `CMakeLists.txt` — Package `spirv-val.exe` alongside `dxc.exe`/`dxcompiler.dll`/`dxil.dll` in `Tools/dxc/` (and register `Tests/ShaderTests` under `MORBI_BUILD_TESTS`).

### New Files Expected to Create
1. `Tests/ShaderTests/CMakeLists.txt`
2. `Tests/ShaderTests/ShaderCompilerTests.cpp`

### Public API & Lifetime Implications
- `ShaderCompiler` becomes cheaply copyable via `std::shared_ptr<DxcModuleState>` (keeping `dxcompiler.dll`/`libdxcompiler.so` loaded until all `ShaderCompiler` and `ShaderCache` instances are destroyed, fixing the implicit hazard if `FreeLibrary` ran before COM release).
- All COM pointers use a deterministic RAII `DxcComPtr<T>` wrapper (works identically on Windows and Linux without ATL/WRL dependencies).
- Existing callers (`EngineRuntime.cpp`) require zero breaking changes; `ShaderCache` disk format bumps from v1 to v2 so old v1 `.mshc` files cleanly miss and recompile as v2.

### Risks & Mitigations
- **Risk 1:** `dxcompiler.dll` version `1.9.2602.24` flag behavior with `-fspv-target-env=vulkan1.3` and `spirv-val.exe --target-env vulkan1.3`. **Mitigation:** Verify directly with `dxcompiler.dll` & `spirv-val.exe` during implementation and unit tests.
- **Risk 2:** `dxil.dll` loading when `dxcompiler.dll` is loaded from `Tools/dxc/` while CWD is elsewhere. **Mitigation:** Use `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` on Windows so `dxcompiler.dll` resolves `dxil.dll` from its own directory.
- **Risk 3:** `EngineRuntime.cpp` smoke test regression if shader bytecode or cache layout changes. **Mitigation:** Re-run both Vulkan 1.3 and DX12 Editor smoke tests and all CTest suites after implementation, in both Release and Debug configurations.

---

## 5. Approval Gate

This plan is ready for review. **No source code has been modified yet.**  
Upon approval, implementation will proceed strictly within Phase 2 WP1 scope.
