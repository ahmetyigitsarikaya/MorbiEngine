# MorbiEngine Physics Architecture & Developer Guide

MorbiEngine's physics subsystem is powered natively in C++ by **Jolt Physics v5.2.0** (strictly encapsulated behind `Morbi::Physics::PhysicsWorld`) and integrated directly with MorbiEngine's Sparse-Set SoA ECS, .NET CoreCLR C# scripting runtime (`Morbi.Engine`), JSON scene serialization, and Editor UI.

---

## 1. Units, Coordinate System & Transform Ownership

### Coordinate System & Units
* **Handedness & Axes:** Left-handed Y-up (`Up = (0, 1, 0)`, `Forward = (0, 0, 1)`, `Right = (1, 0, 0)`).
* **SI Units:**
  * Distance / Position: **meters** (`1 world unit = 1 m`)
  * Mass: **kilograms** (`kg`)
  * Linear Velocity: **meters / second** (`m/s`)
  * Angular Velocity: **radians / second** (`rad/s`)
  * Force: **newtons** (`N`)
  * Impulse: **newton-seconds** (`N*s`)
  * Default Gravity: `(0, -9.81, 0) m/s^2`

### Transform Ownership & Simulation Order
* **Dynamic RigidBodies:** Owned by Jolt Physics during simulation. Each fixed substep synchronizes Jolt's world position/orientation and linear/angular velocity back to `TransformComponent` and `RigidBodyComponent` (respecting `HierarchyComponent` parent/child world transforms). Setting `Transform.Position` / `Rotation` from C# marks `RigidBodyDirtyFlags.Transform`, or call `RigidBody.Teleport(pos, rot)` for explicit non-kinematic teleports.
* **Kinematic RigidBodies:** Owned by ECS / Gameplay scripts. Each substep inspects changes in `TransformComponent` (with linear interpolation across multi-substep frames) and calls Jolt `BodyInterface::MoveKinematic`, computing real surface velocity so dynamic bodies respond accurately and character controllers ride moving platforms cleanly.
* **Static RigidBodies:** Owned by the scene; rebuilt/moved only on scene load or explicit `RigidBodyDirtyFlags.Transform` updates.
* **CharacterController (`JPH::CharacterVirtual`):** Updates during fixed substeps before `PhysicsSystem::Update`, pushing dynamic bodies and writing the resolved foot position back to `TransformComponent::Position`.

### Frame Update Loop (`EngineRuntime.cpp`)
1. Drain Editor Inspector / Binary IPC edits (`SyncEntityBody`, `SyncCharacterController`).
2. `PhysicsWorld::StepSimulation(dt, scene, &taskScheduler, preSubstepCallback)`:
   * Syncs dirty ECS physics components and `CharacterController` components.
   * For each `fixedTimestep` (`1/60s`, clamped to max `4` substeps per frame):
     1. Calls `ScriptEngine::FixedTickManagedScripts(fixedDt)` -> C# `OnFixedUpdate(fixedDt)`.
     2. Re-syncs any script edits and advances kinematic targets.
     3. Advances `CharacterVirtual::ExtendedUpdate`.
     4. Steps `JPH::PhysicsSystem::Update` across worker threads.
     5. Accumulates substep contacts into deduplicated `Enter` / `Stay` / `Exit` transitions (paused cleanly when bodies fall asleep without emitting spurious `Exit`).
   * Writes updated poses & velocities back to ECS and refreshes `PhysicsStats`.
3. `ScriptEngine::DispatchPhysicsEvents()` -> C# `OnCollisionEnter/Stay/Exit` and `OnTriggerEnter/Stay/Exit`.
4. `ScriptEngine::TickManagedScripts(dt)` -> C# `OnUpdate(dt)`.

---

## 2. RigidBody & Collider Components (C++ & C#)

### RigidBodyComponent (116 bytes, `Morbi::Scene::RigidBodyComponent`)
Supported `RigidBodyMotionType` values:
* `Static` (`0`) — immovable geometry (floors, walls, ramps).
* `Kinematic` (`1`) — script/animation-driven movers (platforms, doors, elevators) that push dynamic bodies.
* `Dynamic` (`2`) — fully simulated rigid bodies with Jolt mass & inertia tensor computation.

Supported `ColliderShapeType` values (with per-collider `ColliderOffset`, `ColliderRotation`, and entity scale support):
* `Box` — `HalfExtents = (hx, hy, hz)`
* `Sphere` — `HalfExtents.x = radius`
* `Capsule` — `HalfExtents.x = radius`, `HalfExtents.y = cylinderHalfHeight` (total height = `2 * halfHeight + 2 * radius`)

Key properties: `Mass`, `Density` / `UseDensity`, `Friction`, `Restitution`, `LinearDamping`, `AngularDamping`, `GravityFactor`, `UseGravity`, `UseCCD` (Jolt `EMotionQuality::LinearCast`), `IsTrigger` (sensor volume), `LockedAxes` (`AxisLocks` bitmask for Position X/Y/Z and Rotation X/Y/Z), `CollisionLayer` (32-bit bitmask), `CollisionMask` (32-bit bitmask).

---

## 3. Collision Filtering & Layers

MorbiEngine combines:
* **Per-Body Layer & Mask (`CollisionLayer` and `CollisionMask`):** Both are 32-bit bitmasks (`1u << layerIndex`). Two bodies collide if `((layerA & maskB) != 0) && ((layerB & maskA) != 0)` AND the global layer matrix allows collision between their layer bits.
* **Global Layer Collision Matrix:** Configured via `PhysicsWorld::SetLayerCollision(layerIndexA, layerIndexB, shouldCollide)` (C++) or `Physics.SetLayerCollision(layerA, layerB, shouldCollide)` (C#) for layer indices `0..31`.

Default `CollisionLayers` constants in C#:
* `Default = 0`, `Environment = 1`, `DynamicProps = 2`, `Player = 3`, `Enemy = 4`, `Trigger = 5`, `Projectile = 6`, `AllMask = 0xFFFFFFFFu`, helper `CollisionLayers.LayerBit(int layerIndex)`.

---

## 4. C# Physics Scripting API (`Morbi.Engine`)

All setters on `RigidBody`, `Collider` (`BoxCollider`, `SphereCollider`, `CapsuleCollider`), and `CharacterController` mutate the native ECS component span and immediately call `NativeBridge.SyncRigidBody` / `NativeBridge.SyncCharacterController`.

### Script Lifecycle & Physics Callbacks
Inherit from `Morbi.Engine.Script` (and apply `[EntityScript]` for entity-attached behavior scripts):
* `OnStart()` / `OnCreate()`
* `OnFixedUpdate(float fixedDeltaTime)` — executes right before each Jolt substep (`60 Hz`).
* `OnUpdate(float deltaTime)` — executes once per rendered frame after physics & event dispatch.
* `OnDestroy()` — invoked automatically on `DestroyEntity`, component removal, or hot-reload unload.
* `OnCollisionEnter(Collision col)`, `OnCollisionStay(Collision col)`, `OnCollisionExit(Collision col)`
* `OnTriggerEnter(Collider other)`, `OnTriggerStay(Collider other)`, `OnTriggerExit(Collider other)`

### RigidBody Forces & Velocities
```csharp
rb.AddForce(new Vec3(0, 0, 20), ForceMode.Force);
rb.AddForce(new Vec3(0, 5, 0), ForceMode.Impulse);
rb.AddForceAtPosition(impulse, hit.Point, ForceMode.Impulse);
rb.AddTorque(new Vec3(0, 10, 0), ForceMode.Acceleration);
rb.LinearVelocity = new Vec3(4, 0, 0);
rb.Teleport(new Vec3(0, 10, 0), Quat.Identity);
```

### Spatial Queries (`Morbi.Engine.Physics`)
* `Physics.Raycast(origin, direction, out RaycastHit hit, maxDistance, filter)`
* `Physics.RaycastAll(origin, direction, maxDistance, filter)` (distance-sorted)
* `Physics.SphereCast(origin, radius, direction, out hit, maxDistance, filter)`
* `Physics.BoxCast(origin, halfExtents, orientation, direction, out hit, maxDistance, filter)`
* `Physics.CapsuleCast(origin, radius, halfHeight, orientation, direction, out hit, maxDistance, filter)`
* `Physics.OverlapSphere(center, radius, filter)`
* `Physics.OverlapBox(center, halfExtents, orientation, filter)`
* `Physics.OverlapCapsule(center, radius, halfHeight, orientation, filter)`

---

## 5. Character Controller (`JPH::CharacterVirtual`)

Attach `CharacterControllerComponent` in C++ / Scene JSON (`"character_controller"`) or via `entity.AddComponent<CharacterController>()` in C# or Inspector -> `Add Component -> Character Controller (Jolt Virtual)`.

* **Bottom-at-Feet Convention:** Capsule bottom rests at `Transform.Position + CenterOffset`, compatible with skeletal character rigs whose root origin is between the feet.
* **Features:** Automatic ground & steep-slope detection (`IsGrounded`, `IsSliding`, `GroundState`, `GroundNormal`, `GroundEntity`), configurable `MaxSlopeAngleDegrees`, `StepHeight` stair-climbing, moving/rotating kinematic platform ride-along (`GroundVelocity`), dynamic rigid-body pushing (`Mass`, `MaxPushStrength`), and an internal broadphase proxy body so `Raycast` and `Trigger` volumes detect characters.

---

## 6. Joints / Constraints (C++ `PhysicsWorld`)

`PhysicsWorld::CreateJoint(scene, JointDesc)` returns a generational `JointHandle` (`DestroyJoint`, `IsJointValid`):
* `JointType::Fixed` — welds two bodies (or one body to world).
* `JointType::Distance` — maintains min/max distance with optional `SpringFrequencyHz` / `SpringDampingRatio`.
* `JointType::Hinge` — single-axis rotation around `Axis` with `[MinLimit, MaxLimit]` radians and friction torque.
* `JointType::Slider` — single-axis translation along `Axis` with `[MinLimit, MaxLimit]` meters and friction force.

---

## 7. Editor Integration & Debug Tools

* **Inspector:** Displays & live-edits Motion Type, Collider Shape, Mass, Friction, Restitution, Linear/Angular Damping, Gravity Factor, CCD, Trigger flag, HalfExtents, and `CharacterController` properties (`Radius`, `Height`, `StepHeight`, `MaxSlopeAngleDegrees`, `MaxPushStrength`, `AirControl`, `Grounded`/`Sliding` state).
* **Viewport Wireframe Colliders:** Click **Colliders** in the viewport mini toolbar (or type `physdebug` in the Editor Console) to toggle GPU wireframe rendering of all RigidBody shapes and CharacterController capsules.
* **StatusBar & Console:** StatusBar displays live Jolt active/total body counts, character count, and simulation time (`ms`). Type `stat physics` in the Editor Console for full solver & query telemetry.

---

## 8. Working C# Example Scripts

See `Samples/PhysicsShowcase/Scripts/`:
1. `PhysicsPlayerRigidBodyController.cs` — Force-based movement, raycast ground detection, CCD, axis locking, and impulse jumping.
2. `PhysicsCharacterControllerSample.cs` — `CharacterVirtual` movement, sprint, slope/stair handling, and jumping.
3. `PhysicsInteractionController.cs` — Raycast impulse shots (`AddForceAtPosition`), `OverlapSphere` radial shockwaves, and `OnTriggerEnter` / `OnTriggerExit` tracking.
