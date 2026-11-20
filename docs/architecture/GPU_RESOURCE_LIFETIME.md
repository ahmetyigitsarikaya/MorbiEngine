# GPU Resource Lifetime & Synchronization

Both `Morbi.RHI.Vulkan` and `Morbi.RHI.DX12` implement a deterministic deferred destruction and reference-tracking model designed to prevent use-after-free GPU faults and handle ABA bugs.

---

## 1. Generational Handles & Resource Pools

All GPU resources (`BufferHandle`, `TextureHandle`, `PipelineHandle`, `PipelineLayoutHandle`) are `Core::GenerationalHandle<Tag>` values backed by `Core::GenerationalPool<Record, Tag>`:

- Releasing a resource increments its slot generation before returning the slot to the free-list.
- `IDevice::IsResourceAlive(handle)` rejects stale or `PendingDestroy` handles immediately.
- Internal locking separates `m_resourceMutex` and `m_submitMutex` (never nested; GPU waits never hold mutexes).

---

## 2. Command-List Reference Tracking & Deferred Destruction

```mermaid
stateDiagram-v2
    [*] --> Live: CreateBuffer / CreateTexture / CreatePipeline
    Live --> Recorded: ICommandList::Track*() (++UnsubmittedRefCount)
    Recorded --> Live: Discard() (--UnsubmittedRefCount)
    Recorded --> InFlight: CommitCommandListSubmission()\n(LastUsed = ++timelineValue, --UnsubmittedRefCount)
    Live --> PendingDestroy: Destroy*() called
    InFlight --> PendingDestroy: Destroy*() called\n(CompletionValue = max(LastUsed, nextFrame))
    PendingDestroy --> Destroyed: ProcessDeferredDestructions(completedVal)\nwhen UnsubmittedRefCount == 0\nAND CompletionValue <= completedVal
    Destroyed --> [*]
```

### Synchronization Primitives (2 Frames in Flight)

| Mechanism | Vulkan 1.3 (`VulkanDevice.cpp`) | DirectX 12 (`DX12Device.cpp`) |
|---|---|---|
| Global monotonic timeline | `VK_SEMAPHORE_TYPE_TIMELINE` (`m_timelineSemaphore`) | `ID3D12Fence` (`m_fence`) + `m_nextFenceValue` |
| Per-frame slots (`kMaxFramesInFlight = 2`) | `ImageAvailableSemaphore`, `RenderFinishedSemaphore`, `InFlightFence`, `TargetTimelineValue` | `m_frameAllocators[2]`, `FrameFenceValues[2]` |
| PipelineLayout safety | Deferred until `UnsubmittedRefCount == 0` AND `PipelineRefCount == 0` AND timeline completed | Same (`PipelineRefCount` + `UnsubmittedRefCount` + fence value) |
| Destruction reclamation | Frees `VkBuffer`/`VkImage`, slab range, bindless slot, pool slot | Releases COM resource, RTV/DSV/bindless slot, pool slot |
