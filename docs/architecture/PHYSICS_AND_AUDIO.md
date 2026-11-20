# Physics & Audio (`Morbi.Physics` · `Morbi.Audio`)

---

## 1. Physics (`Morbi.Physics`) — Status: **Implemented**

Morbi Engine uses **Jolt Physics v5.2.0** (vendored in `ThirdParty/JoltPhysics` with pinned SHA-256 `FetchContent` fallback). All Jolt types are encapsulated behind `PhysicsWorld::Impl`.

> For full coordinate system rules, C# examples, and tuning guides, see [`docs/PHYSICS_GUIDE.md`](../PHYSICS_GUIDE.md).

### Verified Capabilities
- **World Lifecycle (`PhysicsWorld.cpp`, 2,116 LOC):** Thread-safe ref-counted global Jolt runtime, fixed-timestep accumulator (`StepSimulation`, default 1/60s) with `preSubstepCallback` and `GetInterpolationAlpha()`, scene rebuild/reset, and bidirectional ECS sync (`TransformComponent`, `RigidBodyComponent`, `CharacterControllerComponent`).
- **Shapes & Colliders:** `Box`, `Sphere`, `Capsule`, and multi-shape `JPH::StaticCompoundShape` via `CompoundColliderEntry`.
- **Collision Filtering & Events:** 2 BroadPhase layers (`NonMoving`, `Moving`) + custom 32-bit `BitmaskGroupFilter` collision matrix. `ContactListener` records `Enter`/`Stay`/`Exit` and trigger events (`GetRecentCollisions`).
- **Spatial Queries (`PhysicsQueries.cpp`):** `Raycast`, `RaycastAll`, `SphereCast`, `BoxCast`, `CapsuleCast`, `OverlapSphere`, `OverlapBox`, `OverlapCapsule` with `QueryFilter`.
- **Joints (`PhysicsJoints.cpp`):** `Fixed`, `Distance`, `Hinge`, and `Slider` constraints with generational `JointHandle`, springs, limits, and break thresholds.
- **Character Controller (`PhysicsCharacter.cpp`):** Backed by `JPH::CharacterVirtual` with ground state reporting, jumping, sliding, velocity control, and teleportation.
- **Known Gaps:** No `ConvexHullShape` or triangle-mesh (`MeshShape`) colliders yet.

---

## 2. Audio (`Morbi.Audio`) — Status: **Implemented**

Morbi Engine uses **SoLoud** (compiled statically with `WITH_MINIAUDIO` and `WITH_NULL` backends; falls back cleanly to a no-op stub if `MORBI_ENABLE_SOLOUD=0`).

### Verified Capabilities
- **Device Management (`AudioWorld.cpp`):** `Initialize(headlessNoSoundDevice)` selects Miniaudio or NullDriver (with automatic hardware fallback).
- **Spatial & 2D Playback:** `PreloadSound`, `SetListenerTransform`, `PlayOneShot2D`, `PlayOneShot3D` (inverse-distance attenuation, min 1.0 m / max 50.0 m), and `GenerateProceduralToneWav` (44.1 kHz 16-bit mono PCM).
- **ECS Integration:** `AudioWorld::Update(dt, scene)` drives `AudioSourceComponent` (`PlayOnStart`, looping, live 3D emitter positions) and exposes playback hooks to C# scripts.
- **Known Gaps:** No streaming (`WavStream`) for long music tracks, no mixing buses/groups, and no per-voice stop/pause/fade handle API.
