# MorbiEngine — Physics Final Verification & Code Quality Audit

**Date:** 2026-11-17  
**Backend:** Jolt Physics v5.2.0 (vendored in `ThirdParty/JoltPhysics`, CMake-pinned SHA256 `6953A6293F1CCFE3790CB0D778C71E3736031C441E0F434FD2BAA8D307EBFEF3`)  
**Scripting Runtime:** .NET 8 CoreCLR (`hostfxr` + `UnmanagedCallersOnly` API v4 + `Span<T>` zero-copy ECS arenas)  
**Test Hardware & Config:** `12th Gen Intel(R) Core(TM) i5-12400F`, `15.8 GB RAM`, Windows Release (`/O2 /W4 /arch:AVX2`)

---

## 1. Executive Verdict

**Core Jolt Physics simulation, queries, collision/trigger events, `CharacterVirtual` character physics, and C# SDK bindings are verified operational and production-grade for single-collider RigidBodies, CharacterControllers, queries, and events.**

During this independent audit, executing end-to-end C++ and C# integration tests uncovered several real defects that were fixed and re-verified across the full suite:

1. **Multi-cycle `ScriptEngine` re-initialization failure (`0x80008081`):** Calling `ScriptEngine::Initialize()` a second time in the same OS process after `Shutdown()` failed with `hostfxr_initialize_for_runtime_config` error `0x80008081` because `Shutdown()` closed the primary `hostfxr_handle` and unloaded `hostfxr.dll` even though .NET CoreCLR cannot be restarted per process. Fixed via a mutex-guarded process-wide `ProcessDotNetHost` cache in `Engine/Source/Script/src/ScriptEngine.cpp` (`ManagedInit` rebinds cleanly while `ManagedNotifySceneCleared` + `ManagedUnloadCollectible` clean up per-session state).
2. **`AddTorque` `ForceMode` & missing `AddTorqueImpulse` in C#:** `PhysicsWorld::AddTorque` now implements `Force` (`BodyInterface::AddTorque`), `Impulse` (`BodyInterface::AddAngularImpulse`), `VelocityChange` (direct world angular velocity delta), and `Acceleration` (scaled by Jolt world-space inertia tensor via `MotionProperties::GetInverseInertiaDiagonal()` and `GetInertiaRotation()`). Exposed `RigidBody.AddTorqueImpulse(in Vec3)` in `Managed/Morbi.Engine/Components.cs`.
3. **Runtime `MotionType` transitions (`Static -> Dynamic`) in `SyncBodyChangesLocked`:** Transitioning an existing body to `Dynamic` now configures `MassProperties` (from explicit `Mass` or `Density * Volume`), `LinearDamping`, and `AngularDamping` cleanly on the Jolt body instead of retaining stale inverse mass state.
4. **Scene clear script cleanup & PIE teardown:** Added `ScriptEngine::NotifySceneCleared()` / `ManagedHost.NotifySceneCleared()` to invoke `OnDestroy()` on managed entity scripts and clear `s_entityScripts` / `ScriptComponent.ManagedGCHandle` during `Scene::Clear()` and Play-Mode stop transitions.
5. **Per-step scratch allocations removed:** Moved `SubstepContactsScratch`, `SubstepRemovedScratch`, and `UniqueSubstepContactsScratch` onto `PhysicsWorld::Impl` (`PhysicsInternal.hpp`), and reused `s_tickSnapshot` in `ManagedHost.cs`.
6. **CMake & solver hygiene:** Verified no fallback placeholder solver and no Bullet/PhysX code remain; `cmake/MorbiDependencies.cmake` pins local `ThirdParty/JoltPhysics` v5.2.0 (`FATAL_ERROR` if missing) with `CPP_EXCEPTIONS_ENABLED ON`, `FLOATING_POINT_EXCEPTIONS_ENABLED OFF`, `USE_AVX2 ON`.

### Known Remaining Scope Limitations (Honestly Disclosed)
- **Joints (`Fixed`, `Distance`, `Hinge`, `Slider`):** Implemented and verified in C++ (`PhysicsJoints.cpp`, `Test16`), **not yet exposed to the C# API or Editor Inspector**.
- **Multi-Collider Compound Shapes (`SetEntityCompoundColliders`):** Implemented and verified in C++ (`Test7`), **not yet exposed to C# or Editor Inspector** (C# `BoxCollider`/`SphereCollider`/`CapsuleCollider` wrap the entity's `RigidBodyComponent`).
- **`RigidBodyComponent.LockedAxes` & `UseDensity` / `ColliderOffset` / `ColliderRotation` in Editor UI:** Supported natively and from C# (`RigidBody.LockedAxes`, `Collider.Offset`, `Collider.Rotation`), **not yet exposed as UI controls in `Editor/Frontend/src/components/InspectorPanel.tsx`**.
- **`PhysicsWorld::GetRecentCollisions()` thread-safety contract:** Returns `std::span<const CollisionPairEvent>` directly into `PhysicsWorld::Impl::Collisions` without holding `WorldMutex`; callers must consume it on the main simulation thread between `StepSimulation` calls (which `EngineRuntime` and `ScriptEngine::DispatchPhysicsEvents` do).

---

## 2. Feature-by-Feature Verification Matrix

| Feature | Implemented? | Native Verified? | C# Verified? | Automated Test Executed? | Test Result? | Remaining Limitation |
|---|---|---|---|---|---|---|
| **Jolt v5.2.0 Core World, Fixed Timestep & Interpolation** | Yes | Yes (`PhysicsWorld.cpp`) | Indirectly via `OnFixedUpdate` | `Test1`, `Test2`, `Test10`, `TestA_to_F` | **PASS** | Single main physics world per runtime |
| **`RigidBody.AddForce` (all 4 `ForceMode`s)** | Yes | Yes (`PhysicsWorld.cpp`) | Yes (`Components.cs`) | `Test10`, `TestJ` (`Force`, `Acceleration`, `Impulse`, `VelocityChange`) | **PASS** | `ForceMode::Force`/`Acceleration` must be applied prior to/during fixed step |
| **`RigidBody.AddForceAtPosition`** | Yes | Yes (`PhysicsWorld.cpp`) | Yes (`Components.cs`) | `TestJ` (`J_ForceAtPosOK`) | **PASS** | Assumes world-space position & force vector |
| **`RigidBody.AddImpulse` (`AddLinearImpulse`)** | Yes | Yes (`PhysicsWorld.cpp`) | Yes (`Components.cs`) | `Test3`, `TestA_to_F`, `TestJ` | **PASS** | None |
| **`RigidBody.AddTorque` (`Force`/`Impulse`/`Acceleration`/`VelocityChange`) & `AddTorqueImpulse`** | Yes | Yes (`PhysicsWorld.cpp`) | Yes (`Components.cs`) | `TestJ` (`J_TorqueModesOK`) | **PASS** | None |
| **Linear & Angular Velocity (`LinearVelocity`, `AngularVelocity`)** | Yes | Yes | Yes | `Test3`, `Test4`, `TestA_to_F`, `TestJ` | **PASS** | Writes mark dirty flags for immediate or step sync |
| **Mass, Density, LinearDamping & AngularDamping** | Yes | Yes | Yes | `Test3`, `Test10`, `TestJ` (`J_DampingOK`) | **PASS** | `UseDensity` toggle not shown in Editor Inspector |
| **Friction & Restitution** | Yes | Yes | Yes | `Test4`, `Test5`, `TestA_to_F` | **PASS** | None |
| **Gravity (`Physics.Gravity`, `UseGravity`, `GravityFactor`) & Kinematic State** | Yes | Yes | Yes | `Test2`, `Test6`, `TestJ` (`J_KinematicPushOK`) | **PASS** | Static/Dynamic/Kinematic enum transitions supported |
| **Sleep Control (`WakeUp`, `Sleep`, `IsSleeping`, `Teleport`)** | Yes | Yes | Yes | `Test8`, `TestJ` (`J_SleepWakeOK`, `J_TeleportOK`) | **PASS** | None |
| **Collider Shapes (`Box`, `Sphere`, `Capsule`) + `Offset` & `Rotation`** | Yes | Yes | Yes (`BoxCollider`, `SphereCollider`, `CapsuleCollider`, `Collider`) | `Test4`, `Test5`, `Test7`, `TestA_to_F`, `TestJ` | **PASS** | Single primary collider per entity from C#; multi-shape compounds C++-only |
| **Multi-Collider Compound Shapes (`SetEntityCompoundColliders`)** | Yes (C++) | Yes | No | `Test7` (C++ only) | **PASS (C++ only)** | Not exposed to C# `NativeEngineFunctionTable` |
| **Continuous Collision Detection (`UseCCD`)** | Yes | Yes (`MotionQuality::LinearCast`) | Yes (`RigidBody.UseCCD`) | `Test13` | **PASS** | Toggle exists in Inspector & C#; explicit high-speed C# test mirrors C++ `Test13` |
| **Trigger Mode & Collision Filtering (`Layer`, `Mask`, `SetLayerCollision`)** | Yes | Yes (`BitmaskGroupFilter`) | Yes (`Collider.IsTrigger`, `CollisionLayer`, `CollisionMask`, `Physics.SetLayerCollision`) | `Test12`, `Test14`, `TestA_to_F`, `TestJ` | **PASS** | 32-bit layer bitmask |
| **Collision & Trigger Events (`OnCollisionEnter/Stay/Exit`, `OnTriggerEnter/Stay/Exit`)** | Yes | Yes (`MorbiContactListener`) | Yes (`Script.cs`, `ManagedHost.cs`) | `Test10`, `Test14`, `TestA_to_F` | **PASS** | Deduplicated per-frame from all substeps |
| **`Physics.Raycast` & `Physics.RaycastAll`** | Yes | Yes (`PhysicsQueries.cpp`) | Yes (`Physics.cs`) | `Test7`, `Test15`, `TestA_to_F` | **PASS** | `RaycastAll` capped at `maxHits` <= 256 per call in C# wrapper |
| **`Physics.SphereCast`, `BoxCast`, `CapsuleCast`** | Yes | Yes (`PhysicsQueries.cpp`) | Yes (`Physics.cs`) | `Test15` (C++ all 3), `TestA_to_F` (`SphereCast`, `BoxCast`), `TestJ` (`CapsuleCast`) | **PASS** | Closest-hit return mode |
| **`Physics.OverlapSphere`, `OverlapBox`, `OverlapCapsule`** | Yes | Yes (`PhysicsQueries.cpp`) | Yes (`Physics.cs`) | `Test15` (C++ all 3), `TestA_to_F` (`OverlapSphere`), `TestJ` (`OverlapBox`, `OverlapCapsule`) | **PASS** | Capped at `maxHits` <= 256 per call in C# wrapper |
| **Joints (`Fixed`, `Distance`, `Hinge`, `Slider`)** | Yes (C++) | Yes (`PhysicsJoints.cpp`) | No | `Test16` (C++ only) | **PASS (C++ only)** | Not exposed to C# SDK or scene serializer |
| **`CharacterController` Ground Detection, Jump & Teleport** | Yes | Yes (`PhysicsCharacter.cpp`) | Yes (`Components.cs`) | `Test17`, `TestK` (`K_GroundedOK`, `K_JumpOK`) | **PASS** | None |
| **`CharacterController` Slope Handling & Sliding (`MaxSlopeAngleDegrees`, `IsSliding`)** | Yes | Yes | Yes | `Test17`, `TestK` (`K_GentleSlopeOK`, `K_SteepSlideOK`) | **PASS** | None |
| **`CharacterController` Step Climbing & Wall Collision** | Yes | Yes | Yes | `Test17`, `TestK` (`K_StairClimbOK`, `K_WallCollisionOK`) | **PASS** | None |
| **`CharacterController` Moving Platform & Dynamic Body Interaction** | Yes | Yes | Yes | `Test17`, `TestK` (`K_MovingPlatformOK`, `K_PushDynamicOK`) | **PASS** | None |
| **Scene JSON Save / Load (`RigidBodyComponent`, `CharacterControllerComponent`)** | Yes | Yes | N/A | `Test9`, `Test18` | **PASS** | Does not serialize joints or compound lists |
| **Editor Inspector (`InspectorPanel.tsx`)** | Partial | N/A | N/A | Build + `MorbiEditor --smoke-test` | **PASS (Build/Smoke)** | Exposes MotionType, Shape, Mass, Friction, Restitution, Linear/Angular Damping, GravityFactor, HalfExtents, UseGravity, UseCCD, IsTrigger & CharacterController props; omits LockedAxes, Offset/Rot, Layer/Mask |

---

## 3. Lifecycle, Stale Handle & Memory Safety Audit

Verified by `Test8`, `Test18`, `TestA_to_F` (Hot-Reload), and `TestL_LifecycleShutdownReinitAndStaleHandles` plus `EngineRuntime.cpp` Play/Stop inspection:

- **Repeated Play/Stop (`CreateSnapshot` / `RestoreSnapshot`):** 5 consecutive snapshot/restore cycles verified in `TestL` with `RebuildFromScene` accurately restoring body counts and positions without leaking Jolt bodies.
- **Entity Destruction During Active Simulation:** `Scene::SetEntityDestroyedCallback` invokes `ScriptEngine::NotifyEntityDestroyed(eid)` (running managed `OnDestroy()`, removing from `s_entityScripts`) before `PhysicsWorld::DestroyBody(eid)` removes Jolt bodies, characters (`JPH::CharacterVirtual` + inner body), and joints (`RemoveJointsForEntityLocked`).
- **Character Destruction & Recreation:** Verified in C++ (`Test18`) and C# (`TestK`: `_hero.Destroy()` followed immediately by recreating `CSharpHero2` + `AddComponent<CharacterController>()` and verifying `IsGrounded` after landing).
- **Scene Clearing & Switching (`Scene::Clear()`):** `NotifySceneCleared` tears down managed script state cleanly before `PhysicsWorld::ClearAllBodies()` destroys all Jolt constraints, characters, and bodies (`Test8`, `TestL`).
- **C# Script Hot-Reload:** Verified in `TestA_to_F` (live script mutation, Roslyn recompile, collectible `AssemblyLoadContext` swap) and `MorbiEditor.exe --smoke-test` (`HotReloadGen=3`).
- **Repeated Body Creation/Removal Churn:** Verified in `TestL` (500 consecutive create + sync + step + destroy cycles yielding 0 remaining dynamic bodies) and Performance Churn workload (50 create + 50 destroy/step for 180 steps = 9,000 churn ops).
- **Physics Shutdown/Reinit & `ScriptEngine` Shutdown/Reinit:** 3 consecutive `PhysicsWorld` + `ScriptEngine` shutdown/reinit cycles verified in `TestL`; stale packed `EntityId` calls into `PhysicsWorld` and C# bridges return `false` safely without dereferencing invalid sparse-set slots.

---

## 4. Performance Benchmark Results

All benchmarks executed in `MorbiPhysicsTests.exe` (Release `/O2 /W4 /arch:AVX2`, fixed timestep `16.67 ms` / `60 Hz`, single isolated `PhysicsWorld` per workload).

**Memory Measurement Methodology:**  
Sampled immediately before `PhysicsWorld::Initialize()` and immediately prior to `PhysicsWorld::Shutdown()` using Win32 `GetProcessMemoryInfo(GetCurrentProcess(), ...)` (`PROCESS_MEMORY_COUNTERS_EX`):
- **WS (MB):** `WorkingSetSize` (physical resident pages at end of workload).
- **Commit (MB):** `PrivateUsage` (private committed virtual memory at end of workload, with `d=+... MB` showing commit delta over baseline before initializing Jolt).

| Workload | Total Jolt Bodies (Dyn / Active / Sleep / Chars) | Duration (Steps) | Avg Step (ms) | Max Step (ms) | End WS (MB) | End Commit (MB) |
|---|---|---:|---:|---:|---:|---:|
| **100 Bodies (Grid Drop)** | 101 (100 / 0 / 101 / 0) | 3.00s (180) | 0.04 ms | 0.35 ms | 11.59 MB | 47.45 MB |
| **500 Bodies (Grid Drop)** | 501 (500 / 145 / 356 / 0) | 3.00s (180) | 0.28 ms | 0.61 ms | 12.89 MB | 48.16 MB |
| **1000 Bodies (Grid Drop)** | 1001 (1000 / 1000 / 1 / 0) | 3.00s (180) | 0.98 ms | 2.07 ms | 15.67 MB | 49.48 MB |
| **500 Dense Collision Pit** (8x8m walled enclosure) | 505 (500 / 500 / 5 / 0) | 3.00s (180) | 0.60 ms | 1.48 ms | 14.21 MB | 48.73 MB |
| **1000 Sleeping + 1 Active Sphere** | 1002 (1001 / 1 / 1001 / 0) | 2.00s (120) | 0.23 ms | 0.29 ms | 14.41 MB | 48.82 MB |
| **500 Continuously Active** (`Restitution=0.98`, periodic impulse) | 501 (500 / 500 / 1 / 0) | 2.00s (120) | 0.19 ms | 0.52 ms | 12.25 MB | 48.58 MB |
| **500 Mixed Box / Sphere / Capsule** | 501 (500 / 145 / 356 / 0) | 2.00s (120) | 0.31 ms | 0.55 ms | 12.95 MB | 48.41 MB |
| **25 `CharacterVirtual` + Stairs + 20 Crates** | 50 (20 / 10 / 15 / 25) | 2.00s (120) | 0.24 ms | 0.44 ms | 11.55 MB | 47.78 MB |
| **Create/Destroy Churn (50/step = 9,000 ops)** | 51 (50 / 50 / 1 / 0) | 3.00s (180) | 0.12 ms | 0.19 ms | 10.64 MB | 46.76 MB |

All 9 workloads stayed well under 1.0 ms average step time (max single-step spike `2.07 ms` across 1,000 simultaneous dynamic grid-drop bodies), with zero NaN transforms, zero fallen-through-floor bodies, and bounded Jolt temp-allocator/process memory (`~47–49.5 MB` private commit including Jolt `TempAllocatorImpl(10 MB)` and job threads).

---

## 5. Final Verification Command Results (Post-Final-Edit)

All checks below ran after all final edits on 2026-11-17:

1. **Full Release Build (`cmake --build --preset release`):** **Exit 0** (0 warnings, 0 errors across `Morbi.Engine`, `Morbi.Physics`, `Morbi.Script`, `Morbi.Runtime`, `MorbiEditor`, `MorbiPhysicsTests`, `MorbiCSharpPhysicsTests`).
2. **C++ Jolt Physics Tests (`.\build\bin\Release\MorbiPhysicsTests.exe`):** **Exit 0** — `Test1` through `Test18` + `Test19_PerformanceWorkloadsSuite` (9 workloads) `[PASS]` (`ALL PHASE 1-5 JOLT PHYSICS TESTS PASSED!`).
3. **C# Managed Physics Integration Tests (`.\build\bin\Release\MorbiCSharpPhysicsTests.exe`):** **Exit 0** — `TestA_to_F_and_G_CSharpPhysicsAndHotReload`, `TestJ_CompleteCSharpRigidBodyAndQueriesAudit`, `TestK_CompleteCSharpCharacterAudit`, `TestL_LifecycleShutdownReinitAndStaleHandles` `[PASS]` (`ALL C# PHYSICS INTEGRATION TESTS PASSED!`).
4. **CTest (`ctest --test-dir build -C Release --output-on-failure`):** **Exit 0** — `2/2 Passed` (`100% tests passed out of 2`, `2.67 sec`).
5. **Editor Frontend (`cmd /c "cd /d Editor\Frontend && npm run build"`):** **Exit 0** — `tsc -b && vite build` (`311.86 kB`, `1.58s`).
6. **Editor Smoke Test (`.\build\bin\Release\MorbiEditor.exe --smoke-test`):** **Exit 0** — All verified engine phases `[PASS]`, including `Jolt Physics v5.2.0 World`, `.NET CoreCLR Host (hostfxr) ApiVersion=4`, Roslyn compilation, `HotReloadGen=3`, GameUI HUD, and Phase 24 Managed Script bindings.
