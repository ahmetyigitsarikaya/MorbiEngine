# Phase 2 — Shader & Pipeline Infrastructure

**Status:** IN PROGRESS  
**Dependencies:** Phase 1

---

## Objective

Establish a unified, robust, and production-quality shader compilation, binary reflection, pipeline layout, graphics/compute pipeline, PSO cache, hot-reload, and cooked shader asset infrastructure for **Vulkan 1.3** and **DirectX 12** on **Windows** and **Linux**.

## Work Packages & Dependency Order

1. **WP1 — Shader Compilation Infrastructure** (`COMPLETE`) — Plan: [PHASE-02-WP01-EXECUTION-PLAN.md](../reports/phase-02/PHASE-02-WP01-EXECUTION-PLAN.md) | Completion Report: [PHASE-02-WP01-SHADER-COMPILATION.md](../reports/phase-02/PHASE-02-WP01-SHADER-COMPILATION.md)
2. **WP2 — Binary Reflection** (`COMPLETE`) — Completion Report: [PHASE-02-WP02-BINARY-REFLECTION.md](../reports/phase-02/PHASE-02-WP02-BINARY-REFLECTION.md) | Verification Record: [PHASE-02-WP02-VERIFICATION-2026-11-18.md](../test-results/phase-02/PHASE-02-WP02-VERIFICATION-2026-11-18.md)
3. **WP3 — Unified Pipeline Layout** (`COMPLETE`) — Completion Report: [PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md](../reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md) | Verification Record: [PHASE-02-WP03-VERIFICATION-2026-11-18.md](../test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) | [ADR-0003](../decisions/ADR-0003-unified-pipeline-layout.md)
4. **WP4 — Graphics Pipeline** (`NOT STARTED`, depends on WP3) — covers Phase 1 WP4 (`BL-RHI-002`)
5. **WP5 — Compute Pipeline** (`NOT STARTED`, depends on WP3)
6. **WP6 — Pipeline Cache** (`NOT STARTED`, depends on WP4, WP5) — covers Phase 1 WP3 (`BL-RHI-001`)
7. **WP7 — Shader Hot Reload** (`NOT STARTED`, depends on WP1, WP4, WP5, WP6)
8. **WP8 — Cooked Shader Asset** (`NOT STARTED`, depends on WP1, WP2, WP3)

## Current State & Discrepancies (Updated 2026-11-18 After WP2)

| Area | Roadmap Expectation | Current Repository Reality |
|---|---|---|
| DXC Execution | In-process `IDxcCompiler3` / `IDxcUtils`, Windows + Linux | **DONE (WP1):** In-process `IDxcCompiler3`/`IDxcUtils` + COM worker pool (`DxcModuleContext`), `LoadLibraryExW` (Windows) & `dlopen` + ABI bridge (Linux; Linux build/runtime `NOT VERIFIED`) |
| Include System | Custom `IDxcIncludeHandler`, canonical paths, transitive dependency graph, cycle detection | **DONE (WP1):** `MorbiDxcIncludeHandler` with canonical resolution, transitive dependency graph, `#ifndef` diamond support, and cycle guard (`kMaxIncludeLoadsPerFile=32`) |
| Shader Profiles & Targets | SM 6.0–6.6, DXIL + Vulkan 1.3 SPIR-V + SPIR-V validation | **DONE (WP1):** `ShaderModel::SM6_0`–`SM6_6`, `-fspv-target-env=vulkan1.3`, packaged `spirv-val.exe --target-env vulkan1.3` |
| Shader Cache | v3 with compiler version, args, defines, profiles, transitive includes, full reflection, corruption handling | **DONE (WP2):** `ShaderCache` v3 (`'MSHC'` v3), CRC32 checksum, lossless `ShaderReflectionData` serialization, v2 stale invalidation, corrupt recovery, atomic writes |
| Binary Reflection | SPIR-V & DXIL binary reflection + stage & cross-backend validation | **DONE (WP2):** `SpirvReflector` (`SPIRV-Reflect`) + `DxilReflector` (`IDxcContainerReflection`/`ID3D12ShaderReflection`), unified `ShaderReflectionData`, `ShaderReflectionValidator` (`ValidateStageInterface` + `ValidateCrossBackendCompatibility`) |
| Pipeline Layout & PSOs | Unified layout, configurable vertex layouts, MRT, states, cache | **WP3 DONE:** Unified `PipelineLayout` + `PipelineLayoutBuilder` + Vulkan 1.3 (`VkDescriptorSetLayout`/`VkPipelineLayout`) & DX12 (`ID3D12RootSignature`) + collision-safe cache & GPU timeline/fence lifetime safety (`GlobalBindlessHybrid` + `Standalone`); hardcoded 60B/92B vertex strides & `"VSMain"`/`"PSMain"`/`"CSMain"` entry points remain for WP4/WP5 (`BL-RHI-002`) |
| Tests | Automated shader & pipeline test suites | **WP1–WP3 DONE:** `MorbiShaderTests` (181/181) + `MorbiShaderReflectionTests` (190/190) + `MorbiPipelineLayoutTests` (128/128) passing in Release & Debug |

## Mandatory Architectural Constraints

- **Vulkan 1.3 Target:** `VulkanDevice.cpp:764` initializes `VK_API_VERSION_1_3`; SPIR-V compilation must align with Vulkan 1.3.
- **Explicit Register-Space / Descriptor-Set Mapping:**
  - `VulkanDevice.cpp:2621-2744` combines multiple HLSL register spaces into **Descriptor Set 0** (`binding 0 = t0,space0 STORAGE_BUFFER[4096]`, `binding 1 = t0,space1 SAMPLED_IMAGE[4096]`, `binding 2 = s0,space0 SAMPLER[1]`, `binding 3 = b1,space0 UNIFORM_BUFFER[256]`) + 128B `VkPushConstantRange`.
  - `DX12Device.cpp:1883-1956` uses Root Param 0 (`b0,space0` 32 DWORD Root Constants), Root Param 1 (Descriptor Table `t0,space0` + `t0,space1`), Root Param 2 (Direct Root CBV `b1,space0`), and Static Sampler (`s0,space0`).
  - Implementations must **never** assume `register space N = Vulkan set N`.
- **Protect Phase 1 Baseline:** All existing RHI tests (`860/860`, `507/507`) and Vulkan/DX12 Editor smoke tests must continue to pass.

## Acceptance Criteria (Phase-Level)

- Every WP1–WP8 verified on both Vulkan 1.3 and DirectX 12 with Release & Debug builds, automated unit/integration/stress tests, independent code audit, and complete `docs/road/` records.
