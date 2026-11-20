# Engine Core, ECS, Networking & Scripting

This document covers the foundational non-graphics modules of Morbi Engine: `Morbi.Core`, `Morbi.IO`, `Morbi.Platform`, `Morbi.Scene`, `Morbi.Network`, `Morbi.Script`, and `Managed/Morbi.Engine`.

---

## 1. Foundation Modules

### `Morbi.Core` — Status: **Implemented**
- **Types & Error Handling:** `Types.hpp`, `Assert.hpp`, `Result<TValue, TError>` (`Result.hpp`), compile-time/runtime 64-bit FNV-1a `StringId` (`StringId.hpp`).
- **Math (`Math/Math.hpp`):** `Vec2`, `Vec3`, `Vec4`, `Quat`, SSE2-accelerated row-major `Mat4`, and 40-byte tightly packed `Transform` (`static_assert(sizeof(Transform) == 40)`).
- **Concurrency (`Task/TaskScheduler.hpp`):** Work-stealing thread pool with `TaskCounter`, `ParallelFor`, and `Wait`/`WaitIdle`.
- **Memory & Handles (`Memory/`):** `GenerationalHandle<Tag>`, 64-byte aligned `LinearAllocator`, and `GenerationalPool<T>` (`PoolAllocator.hpp`).
- **Diagnostics & Settings:** `Log` (leveled + file + callback), `EngineSettings::Global()` (thread-safe JSON config with dirty listeners and C ABI), `CrashHandler` (Win32 SEH/VEH minidumps + `std::terminate`), `BreadcrumbTracker`, and `DebugConfig`.

### `Morbi.IO` — Status: **Implemented**
- **`PathService`:** VFS scheme mounting (`assets://`, etc.), path normalization, and chunked `.mpak` streaming-to-cache (`ResolveOrStreamToCache`).
- **`MorbiPak`:** Single and chunked (256 MB default) `.mpak` archives with SHA-256 integrity hashes and optional `ChaCha20Sha256` payload encryption (`PakCompression` currently supports `None`).
- **`FileWatcher`:** Background Win32 `ReadDirectoryChangesW` directory monitor with polled event queue.
- **`BinaryReader` / `BinaryWriter`:** Binary serialization primitives used by `.mAsset` and `.mpak`.

### `Morbi.Platform` — Status: **Implemented (Windows/SDL3)**
- **`Window`:** PIMPL wrapper over SDL3 (`SDL3.dll`), exposing DPI scale, Win32 `HWND` (`NativeWindowHandle`), mouse capture, and settings application.
- **`InputState`:** 512-key scancode current/previous state, 5 mouse buttons, cursor position/delta, and scroll wheel.

---

## 2. Scene & Sparse-Set SoA ECS (`Morbi.Scene`) — Status: **Implemented**

`Morbi.Scene` implements a **paged Sparse-Set Structure-of-Arrays (SoA)** ECS (`SparseSet.hpp`, `kPageSize = 1024`) with tightly packed dense arrays (`m_denseEntities`, `m_denseComponents`). Each dense pool exposes `GetArenaView()` so `.NET` scripts can read and mutate contiguous component memory directly via `Span<T>` without marshaling copies.

- **Entity Identifiers:** `EntityId = Core::GenerationalHandle<EntityTag>`.
- **Blittable POD Components (size-verified via `static_assert`):**
  - `TransformComponent` (40 B), `HierarchyComponent` (32 B), `CameraComponent` (32 B), `MeshRendererComponent` (32 B), `LightComponent` (32 B), `ScriptComponent` (32 B), `RigidBodyComponent` (116 B), `AudioSourceComponent` (32 B), `CharacterControllerComponent` (112 B).
- **Non-POD Components:** `AnimatorComponent`, `NetworkIdentityComponent`, plus auxiliary `CompoundColliderEntry` and `PhysicsJointEntry` tables.
- **Systems & Serialization:** Parallelized `UpdateScriptSystem`, `UpdateAnimationSystem`, and `UpdateTransformHierarchySystem`; JSON scene save/load; full `SceneSnapshot` / `RestoreSnapshot` for Play-In-Editor (PIE) state rollback.

---

## 3. Networking Stack (`Morbi.Network`) — Status: **Implemented**

`Morbi.Network` is a dedicated multiplayer architecture built on **ENet v1.3.18** (reliable/unreliable UDP) and **libsodium 1.0.20**:

- **Transport (`Transport/`):** `ITransportBackend`, `EnetTransportBackend`, and `NetworkSimulator` (configurable artificial latency, jitter, and packet loss).
- **Security (`Security/`):** `CryptoContext` (X25519 key exchange, Ed25519 signed connect tokens, XChaCha20-Poly1305 AEAD framing, `ReconnectTicketManager` with key rotation), `ReplayProtection`, and `ConnectionRateLimiter`.
- **Connections & Sessions:** `ConnectionManager` (authenticated handshake, AEAD framing, reconnect tickets) and `SessionRegistry` (multi-world session routing).
- **Replication (`Replication/`):** `ReplicationManager` (server-side delta/keyframe replication, chunked keyframes, explicit ACK/resync), `ClientReplicationApplier`, `RemoteInterpolationBuffer`, `InterestManagement` (`AllVisible` and distance hysteresis), and `NetworkIdPool`.
- **Gameplay Messaging:** `InputCommandPipeline`, `RpcRouter` (direction, authority, and rate-limit validation), and `BitStream` bit-level serialization.

---

## 4. .NET 10 Scripting (`Morbi.Script` & `Managed/Morbi.Engine`) — Status: **Implemented**

- **CoreCLR Host (`ScriptEngine.cpp`):** Loads `hostfxr` via `nethost.lib` / `Tools/dotnet` (process-wide cached `ProcessDotNetHost`), binds 17 `[UnmanagedCallersOnly]` entry points on `Morbi.Engine.ManagedHost`, and passes a `NativeEngineFunctionTable` (ApiVersion 7, ~90 `__cdecl` function pointers covering ECS arenas, Scene, Input, Animation, Jolt Physics, Audio, GameUI, VFS, Diagnostics, and Multi-World Networking).
- **In-Process Compilation & Hot-Reload (`Managed/Morbi.Engine`):**
  - Targets **`net10.0`** with `Microsoft.CodeAnalysis.CSharp` `5.12.0` (Roslyn) in `ScriptCompiler.cs` to compile project `.cs` scripts into `GameScripts.dll` + Portable PDB on demand.
  - `CollectibleScriptLoadContext` (`ManagedHost.cs`) loads assemblies from `MemoryStream` to avoid Windows file locks and enables runtime hot-reload unloading.
  - `NativeBridge.cs` uses C# `delegate* unmanaged[Cdecl]` function pointers and `Span<T>` zero-copy views over native ECS pools.
- **Architectural Caveat:** `ScriptEngine.cpp` currently stores active native pointers in a file-static `g_runtimeCtx` (multi-world execution switches via `SetActiveWorldContext`).
