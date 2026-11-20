# MorbiEngine — Engineering Changelog

All notable inspections, architectural decisions, implementations, bug discoveries, fixes, and test results are recorded chronologically (reverse chronological order).

---

## [2026-11-18] — Phase 2 WP3 Complete: Unified Pipeline Layout

**Phase / WP:** Phase 2 WP3 (`COMPLETE`)

### Implemented
- **RHI Pipeline Layout Layer (`Morbi.RHI`):** Added `PipelineLayout.hpp/.cpp` (`PipelineLayoutHandle`, `PipelineLayoutMode::Standalone` & `GlobalBindlessHybrid`, `DescriptorSetLayoutDesc`, `StaticSamplerDesc`, `PipelinePushConstantRange`, `PipelineConstantBufferLayout`, `PipelineLayoutLimits`, `PipelineLayoutStats`, `PipelineLayoutValidator` with canonical `Normalize`, `ComputeCanonicalHash`, structural `AreStructurallyEqual`, and backend validation), optional `PipelineLayout` handle on `GraphicsPipelineDesc` & `ComputePipelineDesc`, `IDevice` layout APIs, `ICommandList::TrackPipelineLayout`, and `NullDevice` + `NullCommandList` implementation (`RHIDevice.cpp`).
- **Shader-to-RHI Builder (`Morbi.Shader`):** Swapped `RHI` before `Shader` in root `CMakeLists.txt` and linked `Morbi::RHI` (`PUBLIC`) in `Shader/CMakeLists.txt` with zero circular dependency; implemented `PipelineLayoutBuilder` (`BuildFromReflection`, `BuildFromDualBackendReflections`, cross-stage VS+PS/CS merging, `CrossBackendBindingPolicy`, `GlobalBindlessHybrid` Set 0 absorption).
- **Vulkan 1.3 Backend (`VulkanDevice.cpp`):** Queried physical Vk1.2/1.3 features & limits (`VkPhysicalDeviceFeatures2`, `VkPhysicalDeviceProperties2`), implemented immutable static `VkSampler`s, dense set layout arrays with shared empty placeholder sets, collision-safe canonical hash cache (`CacheRefCount`), deferred GPU timeline destruction protected by `PipelineRefCount` + `UnsubmittedRefCount`, conditional `m_bindlessSet` binding, and active layout push-constant stage mask/byte range resolution.
- **DirectX 12 Backend (`DX12Device.cpp`):** Queried D3D12 resource binding tier limits, implemented `ID3D12RootSignature` creation separating CBV/SRV/UAV vs. Sampler tables, Root CBV/SRV/UAV descriptors, `D3D12_STATIC_SAMPLER_DESC`, 64-DWORD enforcement, collision-safe cache & deferred fence destruction, and `DX12CommandList` root signature switching, bindless table re-binding, and active bind-point `PushConstants` / `BindConstantBuffer`.
- **Tests & Documentation:** Added `MorbiPipelineLayoutTests` (`128/128` assertions covering Null, Vulkan 1.3, and DX12 incl. live GPU execution) and documented `ADR-0003`, `PHASE-02-WP03-VERIFICATION-2026-11-18.md`, and `PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md`.

### Tests Executed (Post-Final Edit)
- `cmake --build --preset release` & `debug`: **PASSED** (exit `0`).
- Release & Debug `MorbiPipelineLayoutTests.exe`: **128/128 PASSED**; `MorbiShaderTests.exe`: **181/181 PASSED**; `MorbiShaderReflectionTests.exe`: **190/190 PASSED**.
- Release & Debug `MorbiRHIDescriptorTests.exe` (**860/860**), `MorbiRHIGenerationalHandleHashTests.exe` (**507/507**): **PASSED**.
- Release `ctest --test-dir build -C Release`: **8/8 PASSED** (`49.89 sec`); Debug `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests`: **7/7 PASSED** (`52.72 sec`; excludes pre-existing `BL-PHY-001`).
- Release & Debug `MorbiEditor.exe --smoke-test --api vulkan` & `--api dx12`: **PASSED** (exit `0`).
- Linux build/runtime: **NOT VERIFIED** (`BL-SHD-007`).

---

## [2026-11-18] — WP2 Smoke Test Parity Verification & Phase 2 WP3 Architecture Audit

**Phase / WP:** Phase 2 WP2 Audit & Phase 2 WP3 (`NOT STARTED` — Audit & Design Only, Awaiting Implementation Approval)

### WP2 Smoke Test Reflection Parity Verification
- Audited `kUnifiedHlslShader` (`FullscreenComposite.hlsl`, `EngineRuntime.cpp:87-109, 1154-1208`), `ShaderCompiler.cpp:1417-1428` (`MORBI_VULKAN=1` vs `MORBI_DX12=1`), and `dxc.exe -dumpbin` on `kUnifiedHlslShader` (`PSMain`).
- Confirmed Vulkan (`Bindings=0, PushConstBytes=64`) vs DX12 (`Bindings=1, PushConstBytes=0`) represent identical `FramePushConstants` (`64` bytes across 6 fields; `gGlobalBuffer` stripped by dead-resource elimination) under differing API conventions (`StorageClassPushConstant` vs `D3D_SIT_CBUFFER` `b0,space0`), validated compatible via `CrossBackendBindingPolicy::MorbiBindless()`. Documented in `PHASE-02-WP02-VERIFICATION-2026-11-18.md`.

### WP3 Architecture Audit & Design
- Inspected `Morbi.RHI`, `VulkanDevice.cpp`, `DX12Device.cpp`, `RenderGraph`, `EngineRuntime.cpp`, `GameUIRenderer.cpp`, `MorbiDependencies` / `CMakeLists.txt` module DAG, and `Tests/RHITests/`.
- Identified critical backend hazards to resolve cleanly in WP3: `VulkanCommandList` hardcoded `VK_SHADER_STAGE_ALL` on `CmdPushConstants`, discarded `VkPhysicalDeviceProperties::limits`, `DX12CommandList::SetPipeline` missing root-signature switch & descriptor-table re-binding, `DX12CommandList::PushConstants` calling both Graphics & Compute slot `0` unconditionally, and D3D12 sampler vs CBV/SRV/UAV table separation.
- **Created:** `docs/road/proposals/PROP-0002-WP3-UNIFIED-PIPELINE-LAYOUT.md` and `docs/road/reports/phase-02/PHASE-02-WP03-EXECUTION-PLAN.md`.
- **Source Code Modified:** None (Audit, Research & Design only).

---

## [2026-11-18] — Phase 2 WP2 Complete: Binary Shader Reflection & Validation

**Phase / WP:** Phase 2 WP2 (`COMPLETE`)

### Implemented
- **WP2.1 — SPIRV-Reflect Vendoring:** Added Khronos `SPIRV-Reflect` (`vulkan-sdk-1.4.363.0`, Apache-2.0) under `ThirdParty/SPIRV-Reflect/` (`MorbiSPIRVReflect` / `SPIRVReflect::SPIRVReflect`, private to `Morbi.Shader`); updated `docs/THIRD_PARTY_NOTICES.md`.
- **WP2.2 — Unified Reflection Model:** Created `ShaderReflection.hpp` (`ShaderReflectionData`, `ReflectedBinding`, `ReflectedConstantBuffer`, `ReflectedPushConstantRange`, `ReflectedShaderMember`, `ReflectedStageParameter`, `CrossBackendBindingPolicy`, `ShaderReflectionValidator`) preserving per-backend provenance without leaking third-party/SDK headers.
- **WP2.3 — SPIR-V Reflector:** Added `SpirvReflector.hpp/.cpp` with RAII `ScopedSpvReflectModule` (`SPV_REFLECT_MODULE_FLAG_NO_COPY` + alignment guard), entry-point-scoped descriptors (incl. unbounded runtime arrays & RO/RW storage buffers), nested CB/PushConstant member trees, interface inputs/outputs, and compute local sizes.
- **WP2.4 — DXIL Reflector:** Added `DxilReflector.hpp/.cpp` using pooled `IDxcUtils` and `IDxcContainerReflection` (`DXC_PART_DXIL` / `DXC_PART_REFLECTION_DATA` -> `ID3D12ShaderReflection`), Windows `<d3d12shader.h>` and Linux ABI declarations in `DxcRuntimeBridge.hpp`, `D3D_CT_CBUFFER` filtering, and `ShaderCompiler::Compile()` / `ReflectBytecode()` integration.
- **WP2.5 & WP2.6 — Validation:** Added `ShaderReflectionValidator.cpp` implementing `ValidateStageInterface` (VS→PS location/semantic matching, scalar types, component masks) and explicit policy-driven `ValidateCrossBackendCompatibility` (`StrictOneToOne()` vs. `MorbiBindless()`).
- **WP2.7 — ShaderCache v3:** Upgraded `ShaderCache` to `'MSHC'` v3 with lossless reflection serialization, bounded recursion depth (`kMaxSerializedMemberDepth = 16`), IEEE CRC32 integrity verification, v1/v2 stale invalidation, and atomic disk writes.
- **WP2.8 — Test Suite & Documentation:** Added `MorbiShaderReflectionTests` (`190/190` assertions covering all 15 scenarios including real `kScenePbrMeshHlslShader` parity) and documented `ADR-0002` + WP2 verification/completion records.

### Bugs Discovered & Fixed
- Fixed `SpvReflectInterfaceVariable::component == ~0u` (`0xFFFFFFFFu`) undecorated sentinel causing SPIR-V `ComponentMask` bitshift to `0x8` instead of `0x7` / `0xF`.
- Fixed nested struct member byte size parity (`spirv-reflect` 16B alignment padding vs. `D3D12_SHADER_VARIABLE_DESC::Size` unpadded extent) while keeping `PaddedSizeBytes`.
- Added zero-offset single-struct unwrapping when validating `[[vk::push_constant]] Struct var;` against DX12 `cbuffer RootConstantsBuffer { Struct var; }`.

### Tests Executed (Post-Final Edit)
- `cmake --build --preset release` & `debug`: **PASSED** (exit `0`).
- Release & Debug `MorbiShaderTests.exe`: **181/181 PASSED**; `MorbiShaderReflectionTests.exe`: **190/190 PASSED**.
- Release `ctest --test-dir build -C Release`: **7/7 PASSED** (59.21s); Debug `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests`: **6/6 PASSED** (62.29s; excludes pre-existing `BL-PHY-001`).
- Release & Debug `MorbiRHIDescriptorTests.exe` (**860/860**), `MorbiRHIGenerationalHandleHashTests.exe` (**507/507**): **PASSED**.
- Release & Debug `MorbiEditor.exe --smoke-test --api vulkan` & `--api dx12`: **PASSED** (exit `0`).
- Linux build/runtime: **NOT VERIFIED** (Windows 11 host; `BL-SHD-007`).

---

## [2026-11-18] — Phase 2 WP2 (Step 1–3): Source Inspection, Technical Research & Architecture Design

**Phase / WP:** Phase 2 WP2 (`NOT STARTED` — Design & Research Only, Awaiting Implementation Approval)

### Inspected & Probed
- Inspected all `ShaderReflectionData` / `ReflectedBinding` / `ReflectSourceAndBinary` usages (`ShaderCompiler.hpp/.cpp`, `ShaderCache.cpp`, `EngineRuntime.cpp`).
- Inspected RHI pipeline & binding contracts in `VulkanDevice.cpp`, `DX12Device.cpp`, `RHITypes.hpp`, `ICommandList.hpp`, and `StaticMeshVertex` (`ModelImporter.hpp`).
- Inspected Windows SDK `10.0.26100.0\um\d3d12shader.h` and `d3dcommon.h`, plus `ThirdParty/Tools/zig/.../d3d12shader.h` licensing.
- Empirically probed DXC (`1.9.2602.24`) and `spirv-dis` / `spirv-val` on `kScenePbrMeshHlslShader`:
  - Confirmed default DXIL containers embed 7 parts (`SFI0`, `ISG1`, `OSG1`, `PSV0`, `STAT` 1500B, `HASH`, `DXIL`).
  - Confirmed `-fspv-reflect` emits `OpExtension "SPV_GOOGLE_hlsl_functionality1"`, whereas `VulkanDevice.cpp` enables only `VK_KHR_SWAPCHAIN_EXTENSION_NAME`; verified standard SPIR-V (without `-fspv-reflect`) already preserves `OpEntryPoint`, `Location`, `DescriptorSet`/`Binding`, runtime arrays, CB/PushConstant member offsets/strides, `LocalSize`, and unstripped `OpName` semantics.
  - Documented genuine backend divergences: Vulkan `[[vk::push_constant]]` vs DX12 `b0,space0` Root Constants (`D3D_SIT_CBUFFER`), and Vulkan `Set 0, Binding 3` unbounded UBO array vs DX12 `b1,space0` Root CBV.

### Documentation Created / Updated
- Created: `docs/road/proposals/PROP-0001-WP2-REFLECTION-ARCHITECTURE.md` (SPIRV-Reflect vs SPIRV-Cross & DXC `IDxcContainerReflection` evaluation).
- Created: `docs/road/reports/phase-02/PHASE-02-WP02-EXECUTION-PLAN.md` (12-section WP2 architecture, unified `ShaderReflectionData` design draft, integration plans, 15-case test matrix, risks, and file plan).
- Updated: `docs/road/proposals/README.md`, `docs/road/phases/PHASE-02-SHADER-PIPELINE.md`, `docs/road/MORBI_ENGINE_ROADMAP.md`, `docs/road/STATUS.md`.
- **Source Code Modified:** None (Step 1–3 research & design only).

---

## [2026-11-18] — Phase 2 WP1 Complete: Shader Compilation Infrastructure

**Phase / WP:** Phase 2 WP1 (`COMPLETE`)

### Implemented
- In-process DXC runtime (`DxcModuleContext`, `DxcComPtr<T>`, `DxcRuntimeBridge.hpp`) with Windows `LoadLibraryExW` (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`) and Linux `dlopen` + ABI bridge, thread-safe COM worker pooling, and `IDxcVersionInfo`/`2`/`3` version tracking (`DXC 1.9 (5191-d355aa83) [1.9.2602.24]`).
- Custom `MorbiDxcIncludeHandler` (`IDxcIncludeHandler`) with canonical path resolution, transitive include dependency tracking, `#ifndef` diamond support, nested relative subdirectory resolution, and include cycle guard (`kMaxIncludeLoadsPerFile = 32`).
- Shader Model 6.0–6.6 support (`ShaderModel::SM6_0`..`SM6_6`), Vulkan 1.3 SPIR-V (`-fspv-target-env=vulkan1.3`), packaged `spirv-val.exe`, and `ValidateSpirVBytecode` (`spirv-val --target-env vulkan1.3`).
- `ShaderCache` v2 (`'MSHC'` v2): 40-byte header with payload IEEE CRC32 checksum, compiler version + full request parameter + transitive include hashing, live dependency validation on both memory and disk hits, corrupt/truncated recovery, `Invalidate`/`ClearAll`, and atomic writes (`*.tmp.<id>` + `MoveFileExW`/`rename`) under `m_diskMutex`.
- Added `Tests/ShaderTests/` (`MorbiShaderTests`, 181 assertions).

### Files Created / Modified
- Created: `Engine/Source/Shader/src/DxcRuntimeBridge.hpp`, `Tests/ShaderTests/CMakeLists.txt`, `Tests/ShaderTests/ShaderCompilerTests.cpp`, `docs/road/decisions/ADR-0001-in-process-dxc-and-shader-cache-v2.md`, `docs/road/reports/phase-02/PHASE-02-WP01-SHADER-COMPILATION.md`, `docs/road/test-results/phase-02/PHASE-02-WP01-VERIFICATION-2026-11-18.md`.
- Modified: `ShaderCompiler.hpp`, `ShaderCompiler.cpp`, `ShaderCache.hpp`, `ShaderCache.cpp`, `Engine/Source/Shader/CMakeLists.txt`, `Editor/CMakeLists.txt`, `CMakeLists.txt`.

### Bugs Discovered & Fixed
- Fixed `DxcRefCountType` (`ULONG` on Windows vs `uint32_t` on POSIX) for `IUnknown::AddRef`/`Release`.
- Fixed `ShaderCompiler` explicit-directory constructor fallback so an invalid explicit path reports `IsDXCAvailable() == false` instead of silently falling back.
- Fixed `ShaderCache::GetOrCompile` double-incrementing `Invalidations` when both memory and disk held the same stale entry (`invalidatedInMemory` + `m_diskMutex`).
- **Pre-existing unrelated issue discovered:** Debug `MorbiCSharpPhysicsTests.exe` crashes with `SEHException` in `Morbi.Engine.NativeBridge.SyncRigidBody` (`JointAndCompoundAuditDriver.OnStart()`, `Logs/Crashes/Crash_Latest.log`). Logged as `BL-PHY-001`.

### Tests Executed (Post-Final Edit)
- Release & Debug `MorbiShaderTests.exe`: **181/181 PASSED** (exit `0`).
- Release `ctest --test-dir build -C Release`: **6/6 PASSED** (54.69s, exit `0`).
- Debug `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests`: **5/5 PASSED** (58.74s, exit `0`).
- Release & Debug `MorbiRHIDescriptorTests.exe` (**860/860**), `MorbiRHIGenerationalHandleHashTests.exe` (**507/507**): **PASSED** (exit `0`).
- Release & Debug `MorbiEditor.exe --smoke-test --api vulkan` and `--api dx12`: **PASSED** (exit `0`).
- Linux build/runtime: **NOT VERIFIED** (Windows 11 host; `BL-SHD-007`).

---

## [2026-11-18] — Repository & Roadmap Synchronization + Phase 2 WP1 Inspection & Plan

**Phase / WP:** Phase 0/1 Verification & Phase 2 WP1 (`IN PROGRESS` — Planning)

### Inspected
- Master roadmap input: `docs/road/roadmap_rgm.md`
- Build configuration: `CMakeLists.txt`, `CMakePresets.json`, `cmake/MorbiCompilerOptions.cmake`, `cmake/MorbiDependencies.cmake`, `Editor/CMakeLists.txt`
- Shader subsystem: `Engine/Source/Shader/CMakeLists.txt`, `ShaderCompiler.hpp/.cpp`, `ShaderCache.hpp/.cpp`
- RHI subsystem: `IDevice.hpp`, `ICommandList.hpp`, `RHITypes.hpp`, `VulkanDevice.cpp`, `VulkanDebug.cpp`, `DX12Device.cpp`
- Render & IO subsystems: `RenderGraph.hpp/.cpp`, `FileWatcher.hpp/.cpp`, `PathService.hpp`, `CookedAssetSerializer.hpp`
- Runtime shader usage: `Engine/Source/Runtime/src/EngineRuntime.cpp`
- Prior audit: `docs/SHADER_RENDER_ANIMATION_AUDIT.md`

### Key Findings & Discrepancies
1. **WP1 is only at prototype stage:** `ShaderCompiler` spawns `dxc.exe` out-of-process via `CreateProcessW` using temporary files (Windows-only, hardcoded `vs_6_0`/`ps_6_0`/`cs_6_0`, `-fspv-target-env=vulkan1.2`). There is no in-process `IDxcCompiler3`/`IDxcUtils`, no custom `IDxcIncludeHandler`, no transitive include dependency graph, no compiler version tracking, no `spirv-val` validation, and no `Tests/ShaderTests/`.
2. **`ShaderCache` is v1:** Missing compiler version, target shader model, `VirtualFilePath`, `GenerateDebugInfo`, `IncludeDirs`, transitive include content hashes, disk dependency re-validation, and integrity checksums.
3. **WP2 is a regex stub:** `ReflectSourceAndBinary` parses HLSL source text with `std::regex`, ignores compiled bytecode, and hardcodes `PushConstantSizeBytes = 64` even though both `VulkanDevice` and `DX12Device` expose 128-byte push/root constants.
4. **WP3–WP8 are NOT STARTED:** Both RHI backends hardcode a single global bindless layout (`VulkanDevice` Set 0 bindings 0..3 + 128B push constants; `DX12Device` Root Params 0..2 + static sampler `s0,space0`) and hardcode vertex strides `60` / `92` and entry points `"VSMain"` / `"PSMain"` / `"CSMain"` in Vulkan.
5. **Tools & SDK Discovery:** `dxcompiler.dll` (`1.9(5191-d355aa83)(1.9.2602.24)`), `dxil.dll`, `dxc.exe`, and `spirv-val.exe` exist under `legacy/FlueperEngine/Legacy/FlueEngine/tools/ShaderCompiler/win64/`. `dxcapi.h` is available in Windows SDK `10.0.26100.0\um\dxcapi.h` (and Zig toolchain libc headers). `spirv-val.exe` is not currently copied to `build/bin/Release/Tools/dxc/`.
6. **Build Baseline:** Release build (`build/`) is present and healthy; Debug build (`build-debug/`) is not currently present on disk.

### Tests Executed (Baseline Verification)
- `ctest --test-dir build -C Release --output-on-failure`: **5/5 PASSED** (exit code `0`, 47.45s)
- `build\bin\Release\MorbiRHIDescriptorTests.exe`: **860/860 assertions PASSED** (Vulkan 430/430, DX12 430/430; exit code `0`; `VK_LAYER_KHRONOS_validation` unavailable on host; DX12 Debug Layer + DRED active)
- `build\bin\Release\MorbiRHIGenerationalHandleHashTests.exe`: **507/507 assertions PASSED** (Vulkan 240/240, DX12 239/239; exit code `0`)
- `build\bin\Release\MorbiEditor.exe --smoke-test --api vulkan`: **PASSED** (exit code `0`)
- `build\bin\Release\MorbiEditor.exe --smoke-test --api dx12`: **PASSED** (exit code `0`)

### Documentation Created
- Established canonical `docs/road/` hierarchy (`README.md`, `MORBI_ENGINE_ROADMAP.md`, `STATUS.md`, `CHANGELOG.md`, `BACKLOG.md`, `decisions/README.md`, `proposals/README.md`, `phases/PHASE-00..02`, `test-results/phase-01/`, `reports/phase-02/PHASE-02-WP01-EXECUTION-PLAN.md`).

### Next Step
- Awaiting user approval of [PHASE-02-WP01-EXECUTION-PLAN.md](reports/phase-02/PHASE-02-WP01-EXECUTION-PLAN.md) before modifying any engine source files.
