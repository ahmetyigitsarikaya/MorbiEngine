# Phase 1 — RHI & GPU Resource Foundation

**Status:** COMPLETE  
**Dependencies:** Phase 0

---

## Objective

Establish deterministic GPU resource lifetime management, generational handles, deferred destruction, and bindless descriptor reclamation across Vulkan 1.3 and DirectX 12.

## Work Packages

| WP | Name | Status |
|---|---|---|
| WP1 | GPU Resource Lifetime | COMPLETE |
| WP2 | Bindless Resource Reclamation | COMPLETE |
| WP3 | PSO Cache | DEFERRED (Handled in Phase 2 WP6; `BL-RHI-001`) |
| WP4 | Vertex Layout API | DEFERRED (Handled in Phase 2 WP4; `BL-RHI-002`) |
| WP5 | Async Upload | DEFERRED (Separate RHI backlog; `BL-RHI-003`) |

## Completed Work

1. Generational resource handles (`BufferHandle`, `TextureHandle`, `SamplerHandle`, `PipelineHandle`, `ShaderHandle`) and `GenerationalPool`.
2. Frame/timeline-tracked deferred destruction across `VulkanDevice` and `DX12Device`.
3. 4096-slot bindless descriptor allocation and reclamation (`t0,space0` buffers, `t0,space1` textures, `b1,space0` uniform buffers on Vulkan).
4. Final Six-Issue Closure:
   - Vulkan shutdown native resource cleanup.
   - Vulkan swapchain destruction and deferred destruction ordering.
   - DX12 `ResizeSwapchain` RTV descriptor leak.
   - `VulkanMemoryAllocator` slab offset reuse and reclamation.
   - Synchronous `CreateTexture(initialData)` upload safety.
   - Duplicate `Destroy*` queue entries.

## Incomplete / Deferred Work

- **WP3 — PSO Cache** -> Planned in Phase 2 WP6 (`BL-RHI-001`)
- **WP4 — Vertex Layout API** -> Planned in Phase 2 WP4 (`BL-RHI-002`)
- **WP5 — Async Upload** -> Tracked in [BACKLOG.md](../BACKLOG.md) (`BL-RHI-003`)

## Verification & Test References

- Baseline re-verified on 2026-11-18: [PHASE-01-BASELINE-VERIFICATION-2026-11-18.md](../test-results/phase-01/PHASE-01-BASELINE-VERIFICATION-2026-11-18.md)
  - Release CTest: 5/5 passed
  - `MorbiRHIDescriptorTests`: 860/860 assertions passed (Vulkan 430/430, DX12 430/430)
  - `MorbiRHIGenerationalHandleHashTests`: 507/507 assertions passed (Vulkan 240/240, DX12 239/239)
  - `MorbiEditor.exe --smoke-test --api vulkan`: PASSED
  - `MorbiEditor.exe --smoke-test --api dx12`: PASSED

## Known Limitations

- `VK_LAYER_KHRONOS_validation` is not installed on the development machine. DX12 Debug Layer and DRED checks passed.
- Debug build directory (`build-debug/`) is not currently built on disk.
