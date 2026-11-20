# ADR-0001: In-Process DXC Lifetime, Cross-Platform Bridge & ShaderCache v2

- **Decision ID:** ADR-0001
- **Title:** In-Process DXC COM Worker Pooling, Cross-Platform Dynamic Loading, and ShaderCache v2 Integrity/Dependency Model
- **Date:** 2026-11-18
- **Approval Status:** Accepted (Approved as part of Phase 2 WP1)
- **Affected Modules:** `Morbi.Shader`, `Editor/CMakeLists.txt`, `Tests/ShaderTests`

---

## Context

Prior to Phase 2 WP1, `Morbi::Shader::ShaderCompiler` executed `dxc.exe` out-of-process via Windows-only `CreateProcessW` with temporary `.hlsl` files, hardcoded `vs_6_0`/`ps_6_0`/`cs_6_0` and `-fspv-target-env=vulkan1.2`, lacked an `IDxcIncludeHandler`, and used `ShaderCache` v1 without compiler version tracking, transitive include invalidation, checksums, or atomic disk writes.

## Problem

1. Out-of-process `dxc.exe` spawning adds process creation overhead, temp-file I/O, no include dependency graph, and breaks Linux portability.
2. Sharing raw COM objects across threads or unloading `dxcompiler.dll` while `ShaderCompiler` copies or worker threads hold COM interfaces risks use-after-free crashes.
3. Linux builds cannot rely on Windows SDK `dxcapi.h` / `WinAdapter.h` being preinstalled.
4. `ShaderCache` v1 can serve stale bytecode when transitive `#include` files change or return corrupt data on truncated/damaged `.mshc` files.

## Options Considered

1. **Thread-local COM pointers (`thread_local DxcComPtr<IDxcCompiler3>`)**: Rejected due to teardown ordering hazards if `dxcompiler.dll` unloads before worker threads exit.
2. **Global mutex around a single `IDxcCompiler3` instance**: Rejected because it serializes shader compilation across threads.
3. **`std::shared_ptr<DxcModuleContext>` owning a mutex-guarded pool of `WorkerContext { IDxcUtils, IDxcCompiler3 }`**: Selected. Each compilation checks out a dedicated COM pair with O(1) pool pop, compiles lock-free in parallel, returns the pair via RAII `ScopedWorkerGuard`, and `~DxcModuleContext()` releases all COM interfaces strictly before `FreeLibrary` / `dlclose`.

## Decision

1. **Dynamic Loading & Lifetime:** Load `dxcompiler.dll` (`LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`) on Windows and `libdxcompiler.so` (`dlopen`) on Linux inside `DxcModuleContext`, held via `std::shared_ptr` across `ShaderCompiler` and `ShaderCache` copies.
2. **Cross-Platform Header Bridge:** Use system/SDK `<dxcapi.h>` on Windows (and `<dxc/dxcapi.h>` on Linux when `WinAdapter.h` is present), with an ABI-compatible COM interface bridge in `src/DxcRuntimeBridge.hpp` when Linux SDK headers are absent.
3. **Vulkan 1.3 & Shader Models 6.0–6.6:** Target `-fspv-target-env=vulkan1.3`, validate SPIR-V via `spirv-val --target-env vulkan1.3`, and support `SM6_0` through `SM6_6`.
4. **Custom `IDxcIncludeHandler` & `ShaderCache` v2:** Track canonicalized transitive `#include` dependencies `(CanonicalPath, ContentHash, ByteSize)` with recursion guard (`kMaxIncludeLoadsPerFile = 32`), hash all compile arguments + compiler version (`IDxcVersionInfo`/`2`/`3`), validate CRC32 and live include hashes on both memory and disk cache hits, and write `.mshc` v2 files atomically (`*.tmp.<id>` + `MoveFileExW` / `rename`) guarded by a dedicated `m_diskMutex` independent of `m_mutex` and compilation.

## Consequences

- **Positive:** Parallel shader compilation scales across threads without COM races or DLL-unload crashes; stale transitive includes and corrupt cache files are automatically invalidated and recovered; Linux compilation no longer blocks on Windows-specific headers/APIs.
- **Negative / Trade-off:** `spirv-val` currently executes as a packaged CLI tool (`spirv-val.exe` / `spirv-val`) when available rather than an in-process static SPIRV-Tools library; if `spirv-val` is absent on a deployment target, compilation logs a warning and skips external validation.
