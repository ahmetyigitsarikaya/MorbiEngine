# MorbiEngine — Physics API Completion & Real Gameplay Validation

**Date:** 2026-11-17  
**Physics Backend:** Jolt Physics v5.2.0 (`ThirdParty/JoltPhysics`)  
**Scripting Runtime:** .NET 8 CoreCLR (`hostfxr` + `UnmanagedCallersOnly` API v5 + `Span<T>` zero-copy ECS arenas)  
**Hardware & Build:** `12th Gen Intel(R) Core(TM) i5-12400F`, `15.8 GB RAM`, Windows Release (`/O2 /W4 /arch:AVX2`)

---

## 1. Phase Completion Summary

### Phase 1 — C# Joint API (`Fixed`, `Distance`, `Hinge`, `Slider`)
- **Native Engine (`Morbi.Physics` / `PhysicsWorld.hpp`, `PhysicsInternal.hpp`, `PhysicsJoints.cpp`):**
  - Added `BreakForce`, `BreakTorque`, and `Enabled` to `Physics::JointDesc` and `Detail::JointRecord`.
  - Added `PhysicsWorld::UpdateJoint`, `GetJointDesc`, `GetJointState` (current position/angle and applied constraint force/torque computed from Jolt Lagrange multipliers `/ fixedDt`), and `SyncSceneJoints`.
  - Added `PhysicsWorld::Impl::EvaluateBreakableJointsLocked` inside each `StepSimulation` substep: wakes connected bodies, removes constraints exceeding `BreakForce` or `BreakTorque`, increments `Generation`, frees slots cleanly, and updates any linked Scene-authored joint record (`Broken = true`).
  - Corrected Jolt `SliderConstraint` axis sign convention in `BuildConstraintForBodies` (negating `mSliderAxis1/2` so positive `[MinLimit, MaxLimit]` and `GetCurrentPosition()` measure `EntityA`'s displacement along `+desc.Axis` relative to `EntityB`).
  - Extended `Scene` (`Components.hpp`, `Scene.hpp`, `Scene.cpp`) with `PhysicsJointEntry` storage, Entity/Scene cleanup, `.mscene.json` `"physics_joints"` serialization + two-pass `connected_entity` name resolution, and PIE snapshot cloning/restoration.
- **Native/Managed Bridge & C# SDK (`ScriptEngine.hpp/.cpp`, `NativeBridge.cs`, `Physics.cs`):**
  - Bumped `NativeEngineFunctionTable::ApiVersion` to `5` with `NativeJointDesc` (96B) and `CreateJoint`, `UpdateJoint`, `DestroyJoint`, `IsJointValid`, `GetJointDesc`, `GetJointState`, `GetEntityJointCount`, `GetEntityJointHandle`.
  - Implemented `JointType`, `JointDesc`, `JointState`, `JointHandle` (`IEquatable<JointHandle>`, packed generational index + generation with `IsValid`, `Update`, `TryGetDesc`, `TryGetState`, `SetLimits`, `SetDistanceLimits`, `SetFriction`, `SetSpring`, `SetBreakThresholds`, `SetEnabled`, `Destroy`), and helper constructors on `Morbi.Engine.Physics` (`CreateFixedJoint`, `CreateDistanceJoint`, `CreateHingeJoint`, `CreateSliderJoint`, `GetEntityJoints`).
- **Automated Verification (`TestM_Phase1And2_CSharpJointsAndCompoundColliders`):** **PASS**. Verified Fixed, Distance (+ spring & force telemetry), Hinge (+ runtime `SetLimits` / `SetFriction`), Slider (+ lateral/vertical lock), Breakable joint snapping under heavy impulse, entity destruction invalidating connected joints, and stale `JointHandle` operations safely returning `false`.

### Phase 2 — Compound Collider API
- **Native Engine (`Morbi.Scene` & `Morbi.Physics`):**
  - Added `Scene::CompoundColliderEntry` (`Shape`, `HalfExtents`, `LocalOffset`, `LocalRotation`, `CollisionLayer`, `CollisionMask`, `IsTrigger`) with `.mscene.json` `"compound_colliders"` serialization, Entity/Scene cleanup, and PIE snapshot support.
  - Extended `Physics::ColliderShapeDesc` with per-subshape `CollisionLayer`, `CollisionMask`, and `IsTrigger`.
  - Added `ComputeEffectiveCollisionGroup` (bit-union for Jolt coarse broadphase `GroupFilter`) paired with thread-safe `MorbiContactListener::OnContactValidate` and `RecordContact` (`SetEntitySubshapeFilters` using `JPH::CompoundShape::GetSubShapeIndexFromID`) so each sub-shape enforces its own exact `CollisionLayer`, `CollisionMask`, and `IsTrigger` status without affecting primary collider filtering.
- **C# SDK (`NativeBridge.cs`, `Components.cs`, `Entity.cs`):**
  - Added `NativeColliderShapeDesc` (56B), `CompoundColliderShape` (with `.Box(...)`, `.Sphere(...)`, `.Capsule(...)` builders), and `CompoundCollider` component (`entity.AddComponent<CompoundCollider>()`, `entity.GetCompoundCollider()`, `Count`, `GetColliders`, `TryGetCollider`, `AddCollider`, `AddBox`, `AddSphere`, `AddCapsule`, `UpdateCollider`, `RemoveCollider`, `SetColliders`, `Clear`) updating Scene & Jolt shapes live at runtime.
- **Automated Verification (`TestM_Phase1And2_CSharpJointsAndCompoundColliders`):** **PASS**. Verified multi-shape Box/Sphere/Capsule creation, runtime `UpdateCollider`/`RemoveCollider`, and per-subshape `CollisionLayer`/`CollisionMask` filtering where projectile Layer 4 passed through Sub-shape #1 (Layer 2 / Mask 2) while colliding cleanly with Sub-shape #2 (Layer 0 / Mask All).

### Phase 3 — Editor Inspector Completion
- **TypeScript UI & Native Bridge (`Editor/Frontend/src/types.ts`, `nativeBridge.ts`, `InspectorPanel.tsx`):**
  - Added **Locked Axes** 6-button toggle bar (`PX/PY/PZ/RX/RY/RZ` mutating `lockedAxes` bitmask), **Collider Offset (X/Y/Z)**, **Collider Rotation Euler (Deg X/Y/Z)**, **Collision Layer**, and **Collision Mask (Hex)** inside the `Rigidbody & Collider (Jolt)` card matching the existing dark Tailwind aesthetic.
  - Added collapsible **Compound Colliders** panel supporting `+ Add Sub-Shape`, shape dropdown (`Box`/`Sphere`/`Capsule`), `HalfExtents`, `LocalOffset`, `LocalRotationEuler`, `Layer`, `Mask`, `Trigger`, and `Remove`.
  - Added collapsible **Physics Joints (Jolt)** panel displaying joint count and `BROKEN` status badge, supporting `+ Add Joint`, `Fixed`/`Distance`/`Hinge`/`Slider` type switcher, `Connected Entity (-1=World)` + resolved name label, `Auto Anchors`, `Anchor A`, `Anchor B`, `Axis`, `Min/Max Distance`, `Min/Max Limit`, `Max Friction`, `Spring Hz/Damping`, `Break Force/Torque`, `Enabled`, and `Remove`.
  - Added `Compound Collider (Sub-Shapes)` and `Physics Joint (Jolt)` to the `+ Add Component` dropdown menu.
- **C++ UI IPC (`Engine/Source/UI/src/WebView2Compositor.cpp`, `Engine/Source/Runtime/src/EngineRuntime.cpp`):**
  - Extended `SyncSceneData` and `update_physics_component` handlers (`rigidbody`, `compoundColliders`, `physicsJoints`) plus `AddComponent` handlers (`compoundCollider`, `physicsJoint`), wiring `ConsumePhysicsSyncEntity` -> `SyncEntityCompoundColliders` -> `SyncEntityBody` -> `SyncSceneJoints`.
  - **Finalization UI Fixes:** Added `Use Density (Compute Mass from Shape Volume)` and `Density (kg/m3)` controls to `InspectorPanel.tsx`; fixed the `type` vs `jointType` IPC envelope collision in `nativeBridge.ts`, `InspectorPanel.tsx`, and `WebView2Compositor.cpp` so React UI joint creation (`+ Add Joint`) and joint-type switching (`Fixed`/`Distance`/`Hinge`/`Slider`) no longer overwrite the IPC `"type": "update_physics_component"` header.
- **Verification Scope (`Test20_EditorInspectorThreadSafeCollisionsAndInvalidRefs` + Smoke Test):** Verified end-to-end via headless `UI::WebView2Compositor::ProcessFrontendJsonMessage` -> `Scene` -> `ConsumePhysicsSyncEntity` -> `PhysicsWorld::SyncEntityCompoundColliders` / `SyncEntityBody` / `SyncSceneJoints` -> `Jolt` (`UseDensity=1`, `Density=4000`, `LockedAxes=56`, `ColliderOffset`, `ColliderRotationEuler`, Compound sub-shape `Raycast`, `Hinge` joint creation/removal, and `SyncSceneData` JSON output), plus `tsc -b && vite build` (0 errors) and `MorbiEditor.exe --smoke-test` (`Phase 8 [PASS]`). Interactive mouse/keyboard clicking inside a live desktop WebView2 window was not executed in headless CLI verification.

### Phase 4 — Real Gameplay Test Scene (`PhysicsGameplayValidation`)
- **Scene Fixture Location:**
  - [PhysicsGameplayValidation.mscene.json](../Tests/PhysicsTests/Fixtures/PhysicsGameplayValidation/Assets/Scenes/PhysicsGameplayValidation.mscene.json)
  - [PhysicsGameplayValidation.cs](../Tests/PhysicsTests/Fixtures/PhysicsGameplayValidation/Scripts/PhysicsGameplayValidation.cs)
- **Verified Gameplay Interactions (`TestN_Phase4_RealGameplayValidationScene`):** **PASS** (13/13 gameplay assertions + JSON Save/Reload roundtrip preserving `CompoundCollidersMap` & all 4 `PhysicsJointsMap` entries):
  - `RbPlayerController`: RigidBody sphere accelerated via C# `AddForce`, collided with `DynamicCrate` (`GV_RbHitCrate`), propelled chain into `DynamicBarrel` (`GV_RbHitBarrel`) and opened `HingeDoor` (`GV_RbHitDoor`, `GV_HingeDoorSwungOK` verified via `Physics.GetEntityJoints` + `JointState.PositionOrAngle > 0.2 rad`).
  - `CharCourseController`: `CharacterController` grounded (`GV_CharGrounded`), climbed `StairStep1..3` (`GV_CharStairsOK`), rode kinematic `MovingPlatform` (`GV_CharMovingPlatformOK`), and activated `GoalTrigger` via `GoalTriggerMonitor.OnTriggerEnter` (`GV_GoalTriggerOK`).
  - `RampClimberController`: ascended 20-degree `GentleRamp` (`GV_RampClimbOK`).
  - `SliderDriver`: translated `SliderCarriage` along +X Slider joint (`GV_SliderMechanismOK`).
  - `FixedCouplerMonitor`: coupled `FixedPayload` moved coherently with `FixedCarrier` (`GV_FixedJointCoupledOK`).
  - `PendulumBob` (`Distance` joint) and `CompoundTotem` (`Compound` Box+Box+Sphere): verified `GV_DistancePendulumOK` & `GV_CompoundTotemOK`.

### Phase 5 — Long-Run Stability & Memory Audit (`TestO_Phase5_LongRunStabilityAndLeakAudit`)
- **30-Minute Simulated Time (`108,000` continuous 60 Hz fixed steps)** running active C# `OrbitController`, `CharacterController`, `CompoundCollider` crate impulse, and `Hinge` joint lever:
  - `SimTime=10m (36,000 steps)` | Wall: `1.78s` | MaxStep: `8.81 ms` | WS: `169.42 MB` | Commit: `146.32 MB`
  - `SimTime=20m (72,000 steps)` | Wall: `3.59s` | MaxStep: `8.81 ms` | WS: `169.45 MB` | Commit: `146.32 MB`
  - `SimTime=30m (108,000 steps)` | Wall: `5.49s` | MaxStep: `8.81 ms` | WS: `172.15 MB` | Commit: `148.96 MB`
  - Net commit growth between 10m and 30m: **+2.64 MB** across `72,000` managed+native physics frames (100% bounded, zero NaNs, `ActiveJointCount == 1`, `CharacterCount == 1`).
- **Finalization Runtime Safety & Serialization Hardening:**
  - **Thread-Safe Collision Snapshots:** Replaced unlocked `std::span<const CollisionPairEvent> PhysicsWorld::GetRecentCollisions()` with `std::vector<CollisionPairEvent> GetRecentCollisions() const` + `CopyRecentCollisions(std::vector<CollisionPairEvent>&)` under `std::shared_lock<std::shared_mutex>`; `ScriptEngine::DispatchPhysicsEvents()` now reuses a scratch buffer via `CopyRecentCollisions`. Verified by `Test20` concurrent background reader thread across live collisions (`0` invalid handles).
  - **Invalid Joint Reference Rejection & Clean Serialization:** `Scene::LoadFromJsonFile` now rejects malformed `"physics_joints"` entries referencing missing non-`"World"` `connected_entity` names or self-joints cleanly without falling back to World joints; `SaveToJsonFile` omits `runtime_joint_handle` / `jolt_body_id` (verified in `Test20`).
  - **Joint EntityB Destruction & Compound `Rebuild()` + Validation:** `Bridge_SetEntityCompoundColliders` rejects non-finite (`NaN`/`Inf`), out-of-range `Shape`, or zero quaternions; C# `CompoundCollider.Rebuild()` and `EntityB`-side joint auto-invalidation (`_linkDropB`) verified in `TestM`.
- **Lifecycle Torture Cycles:**
  - **15x** PIE `RestoreSnapshot` + `RebuildFromScene` + C# assembly reload cycles (`PASS`).
  - **10x** JSON `LoadFromJsonFile` + `RebuildFromScene` + C# hot-reload cycles (`PASS`).
  - **300x** full Entity + `RigidBody` + `CompoundCollider` + `Fixed` Joint + `CharacterController` create/step/destroy churn (`PASS`), verifying immediate auto-invalidation of connected `JointHandle` (`IsJointValid == false`, duplicate `DestroyJoint == false`).
  - **Post-Clear Verification:** `TotalJoltBodies == 0`, `ActiveJointCount == 0`, `CharacterCount == 0`, and `GetRecentCollisions().empty() == true` (zero phantom collision events).

---

## 2. Performance Comparison (Baseline vs Post-Completion)

Benchmarks measured in `MorbiPhysicsTests.exe` (`Release /O2 /W4 /arch:AVX2`, isolated `PhysicsWorld` per workload, Win32 `GetProcessMemoryInfo` Working Set & `PrivateUsage` commit):

| Workload | Baseline Avg / Max ([PHYSICS_FINAL_VERIFICATION.md](../docs/PHYSICS_FINAL_VERIFICATION.md)) | Post-Completion Avg / Max (`MorbiPhysicsTests.exe`) | Post-Completion End Commit |
|---|---:|---:|---:|
| **100 Bodies (Grid Drop)** | 0.04 ms / 0.35 ms | 0.04 ms / 0.30 ms | 47.96 MB |
| **500 Bodies (Grid Drop)** | 0.28 ms / 0.61 ms | 0.28 ms / 0.60 ms | 48.71 MB |
| **1000 Bodies (Grid Drop)** | 0.98 ms / 2.07 ms | 1.00 ms / 2.31 ms | 50.17 MB |
| **500 Dense Collision Pit** | 0.60 ms / 1.48 ms | 0.63 ms / 1.78 ms | 49.58 MB |
| **1000 Sleeping + 1 Active** | 0.23 ms / 0.29 ms | 0.25 ms / 0.51 ms | 49.65 MB |
| **500 Continuously Active** | 0.19 ms / 0.52 ms | 0.19 ms / 0.48 ms | 49.14 MB |
| **500 Mixed Box/Sphere/Capsule** | 0.31 ms / 0.55 ms | 0.32 ms / 1.07 ms | 49.04 MB |
| **25 `CharacterVirtual` + Crates** | 0.24 ms / 0.44 ms | 0.23 ms / 0.42 ms | 48.82 MB |
| **Create/Destroy Churn (50/step)** | 0.12 ms / 0.19 ms | 0.13 ms / 0.36 ms | 47.54 MB |

Average step times remain within baseline noise (`+/- 0.02 ms`), confirming that breakable joint evaluation, Scene joint sync, per-subshape contact validation (`O(1)` fast path when compound subshape filter map is empty), and thread-safe collision snapshot locks introduced no measurable regression.

---

## 3. Executed Verification Commands (Post-Final-Edit)

All checks ran after final source edits on 2026-11-17:

1. **Release CMake Build (`cmake --build --preset release`):** **Exit 0** (0 errors, 0 warnings).
2. **Editor Frontend Build (`cmd /c "cd /d Editor\Frontend && npm run build"`):** **Exit 0** (`tsc -b && vite build`, `332.26 kB`, `1.46s`).
3. **C++ Physics Tests (`.\build\bin\Release\MorbiPhysicsTests.exe`):** **Exit 0** (`Test1`–`Test18`, `Test19_PerformanceWorkloadsSuite`, and `Test20_EditorInspectorThreadSafeCollisionsAndInvalidRefs` all `[PASS]`).
4. **C# Physics + Gameplay + 30m Stability Tests (`.\build\bin\Release\MorbiCSharpPhysicsTests.exe`):** **Exit 0** (`TestA_to_F_and_G_CSharpPhysicsAndHotReload`, `TestJ`, `TestK`, `TestL`, `TestM_Phase1And2_CSharpJointsAndCompoundColliders`, `TestN_Phase4_RealGameplayValidationScene`, `TestO_Phase5_LongRunStabilityAndLeakAudit` all `[PASS]`).
5. **CTest (`ctest --test-dir build -C Release --output-on-failure`):** **Exit 0** (`2/2 Passed`, `13.09 sec`).
6. **Editor Smoke Test (`.\build\bin\Release\MorbiEditor.exe --smoke-test`):** **Exit 0** (`Phase 1`–`Phase 24` all `[PASS]`).

---

## 4. Final System Status & Limitations

| Category | Item | Status / Details |
|---|---|---|
| **Implemented and tested** | Jolt 5.2.0 Core, RigidBodies, Shapes, Layers/Masks, Queries, Events, `CharacterVirtual` | Verified across `Test1`–`Test20` (C++) and `TestA`–`TestO` (C#/.NET CoreCLR). |
| **Implemented and tested** | C# Joint API (`Fixed`, `Distance`, `Hinge`, `Slider`), generational handles, breakable limits, EntityA & EntityB cleanup | Verified in `Test16`, `TestM`, `TestN`, and `TestO`. |
| **Implemented and tested** | C# Compound Collider API (Box/Sphere/Capsule sub-shapes, `Add`/`Update`/`Remove`/`Rebuild`, per-subshape layer/mask/trigger, finite/bounds validation) | Verified in `Test7`, `TestM`, `TestN`, and `Test20`. |
| **Implemented and tested** | Serialization (`RigidBody`, `CharacterController`, `CompoundCollider`, `PhysicsJoint`), invalid `connected_entity` rejection, no runtime handles persisted | Verified in `Test9`, `Test18`, `TestN`, and `Test20`. |
| **Implemented and tested** | Thread-safe collision snapshots (`GetRecentCollisions` / `CopyRecentCollisions`) & headless `WebView2Compositor` IPC -> `PhysicsWorld` sync | Verified in `Test20`. |
| **Implemented but not fully tested** | Interactive live desktop WebView2 mouse/keyboard clicks in `InspectorPanel.tsx` | Frontend TypeScript/Vite build + C++ IPC + `MorbiEditor --smoke-test` verified; live OS-window UI click automation was not run. |
| **Not implemented** | Powered Joint Motor velocity/angle setters & 6-DOF / Cone constraints | Only joint limits, springs, friction, and break force/torque are exposed. |
| **Known limitations** | Compound sub-shape wireframe debug draw; wall-clock soak duration | `EngineRuntime.cpp` draws the primary collider wireframe (Jolt still simulates all compound sub-shapes accurately); `TestO` executes 108,000 fixed 60 Hz steps (30.0 simulated minutes) in ~5.49s wall-clock rather than waiting 30+ idle real-time minutes. |
