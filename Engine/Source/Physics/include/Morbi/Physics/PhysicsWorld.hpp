#pragma once

#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Memory/GenerationalHandle.hpp"
#include "Morbi/Core/Task/TaskScheduler.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Scene/Scene.hpp"

namespace Morbi::Physics
{
    namespace BroadPhaseLayers
    {
        static constexpr u8 kNonMoving = 0;
        static constexpr u8 kMoving = 1;
        static constexpr u32 kNumLayers = 2;
    }

    namespace ObjectLayers
    {
        static constexpr u16 kNonMoving = 0;
        static constexpr u16 kMoving = 1;
        static constexpr u32 kNumLayers = 2;
    }

    namespace CollisionLayers
    {
        static constexpr u32 kDefault     = 1u << 0;
        static constexpr u32 kEnvironment = 1u << 1;
        static constexpr u32 kDynamic     = 1u << 2;
        static constexpr u32 kKinematic   = 1u << 3;
        static constexpr u32 kPlayer      = 1u << 4;
        static constexpr u32 kTrigger     = 1u << 5;
        static constexpr u32 kAll         = 0xFFFFFFFFu;
    }

    enum class ForceMode : u8
    {
        Force = 0,          // Continuous force (N), scaled by dt and 1/mass
        Acceleration = 1,   // Continuous acceleration (m/s^2), scaled by dt, ignores mass
        Impulse = 2,        // Instantaneous momentum change (N*s), scaled by 1/mass
        VelocityChange = 3  // Instantaneous velocity change (m/s), ignores mass
    };

    enum class ContactEventType : u8
    {
        Enter = 0,
        Stay  = 1,
        Exit  = 2
    };

    enum class JointType : u8
    {
        Fixed    = 0,
        Distance = 1,
        Hinge    = 2,
        Slider   = 3
    };

    struct JointTag {};
    using JointHandle = Core::GenerationalHandle<JointTag>;

    struct ColliderShapeDesc
    {
        Scene::ColliderShapeType Shape = Scene::ColliderShapeType::Box;
        Math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
        Math::Vec3 LocalOffset{ 0.0f, 0.0f, 0.0f };
        Math::Quat LocalRotation = Math::Quat::Identity();
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
        bool IsTrigger = false;
    };

    struct QueryFilter
    {
        u32 LayerMask = 0xFFFFFFFFu;
        bool IncludeTriggers = false;
        Scene::EntityId IgnoreEntity = Scene::EntityId::Invalid();
        bool IgnoreStatic = false;
        bool IgnoreKinematic = false;
        bool IgnoreDynamic = false;
    };

    struct RaycastHit
    {
        Scene::EntityId HitEntity = Scene::EntityId::Invalid();
        Math::Vec3 Point = Math::Vec3::Zero();
        Math::Vec3 Normal = Math::Vec3::Up();
        f32 Distance = 0.0f;
        u32 JoltBodyId = 0xFFFFFFFFu;
        Scene::ColliderShapeType Shape = Scene::ColliderShapeType::Box;
    };

    struct OverlapHit
    {
        Scene::EntityId Entity = Scene::EntityId::Invalid();
        Math::Vec3 ContactPoint = Math::Vec3::Zero();
        Math::Vec3 PenetrationAxis = Math::Vec3::Up();
        f32 PenetrationDepth = 0.0f;
        u32 JoltBodyId = 0xFFFFFFFFu;
        Scene::ColliderShapeType Shape = Scene::ColliderShapeType::Box;
    };

    struct CollisionPairEvent
    {
        Scene::EntityId EntityA = Scene::EntityId::Invalid();
        Scene::EntityId EntityB = Scene::EntityId::Invalid();
        Math::Vec3 ContactPoint = Math::Vec3::Zero();
        Math::Vec3 ContactNormal = Math::Vec3::Up();
        Math::Vec3 RelativeVelocity = Math::Vec3::Zero();
        f32 PenetrationDepth = 0.0f;
        Scene::ColliderShapeType ShapeA = Scene::ColliderShapeType::Box;
        Scene::ColliderShapeType ShapeB = Scene::ColliderShapeType::Box;
        ContactEventType EventType = ContactEventType::Enter;
        bool IsTrigger = false;
    };

    struct JointDesc
    {
        JointType Type = JointType::Fixed;
        Scene::EntityId EntityA = Scene::EntityId::Invalid();
        Scene::EntityId EntityB = Scene::EntityId::Invalid(); // Invalid = attach EntityA to world
        Math::Vec3 AnchorA = Math::Vec3::Zero();              // World space anchor for body A
        Math::Vec3 AnchorB = Math::Vec3::Zero();              // World space anchor for body B (if Fixed/Distance/Hinge/Slider)
        Math::Vec3 Axis = Math::Vec3::Up();                   // Hinge rotation axis or Slider translation axis (world space)
        bool AutoDetectAnchors = true;                        // If true for Fixed, binds bodies at current relative transform
        f32 MinDistance = -1.0f;                              // Distance joint min (-1 = current distance)
        f32 MaxDistance = -1.0f;                              // Distance joint max (-1 = current distance)
        f32 SpringFrequencyHz = 0.0f;                         // > 0 enables soft spring limit
        f32 SpringDampingRatio = 0.0f;
        f32 MinLimit = -Math::kPi;                            // Hinge angle min (rad) or Slider pos min (m)
        f32 MaxLimit = Math::kPi;                             // Hinge angle max (rad) or Slider pos max (m)
        f32 MaxFriction = 0.0f;                               // Hinge friction torque (N*m) or Slider friction force (N)
        f32 BreakForce = 0.0f;                                // <= 0 = unbreakable; breaks if solver force >= BreakForce (N)
        f32 BreakTorque = 0.0f;                               // <= 0 = unbreakable; breaks if solver torque >= BreakTorque (N*m)
        bool Enabled = true;
    };

    struct PhysicsStats
    {
        u32 ActiveBodyCount = 0;
        u32 SleepingBodyCount = 0;
        u32 DynamicBodyCount = 0;
        u32 KinematicBodyCount = 0;
        u32 StaticBodyCount = 0;
        u32 TotalJoltBodies = 0;
        u32 ActiveJointCount = 0;
        u32 CharacterCount = 0;
        u32 FixedStepsExecutedLastFrame = 0;
        u32 CollisionPairsLastStep = 0;
        u32 TriggerPairsLastStep = 0;
        u32 QueryCountLastFrame = 0;
        f32 SimulationTimeMs = 0.0f;
    };

    enum class CharacterGroundState : u8
    {
        OnGround = 0,
        OnSteepGround = 1,
        NotSupported = 2,
        InAir = 3
    };

    struct CharacterControllerState
    {
        CharacterGroundState GroundState = CharacterGroundState::InAir;
        bool IsGrounded = false;
        bool IsSliding = false;
        Math::Vec3 Velocity = Math::Vec3::Zero();
        Math::Vec3 GroundNormal = Math::Vec3::Up();
        Math::Vec3 GroundVelocity = Math::Vec3::Zero();
        Scene::EntityId GroundEntity = Scene::EntityId::Invalid();
    };

    // Pure C++ Jolt Physics 5.2.0 System with Fixed-Timestep accumulator & SoA TransformComponent sync.
    // All Jolt Physics types and headers remain strictly encapsulated inside Morbi.Physics.
    class MORBI_API PhysicsWorld
    {
    public:
        explicit PhysicsWorld(f32 fixedTimestep = 1.0f / 60.0f);
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        bool Initialize();
        void Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        void SetGravity(const Math::Vec3& gravity) noexcept;
        [[nodiscard]] Math::Vec3 GetGravity() const noexcept;

        void SetFixedTimestep(f32 fixedTimestep) noexcept;
        [[nodiscard]] f32 GetFixedTimestep() const noexcept;
        [[nodiscard]] f32 GetInterpolationAlpha() const noexcept;

        // Global collision matrix for bit-indexed layers [0..31]
        void SetLayerCollision(u32 layerIndexA, u32 layerIndexB, bool shouldCollide) noexcept;
        [[nodiscard]] bool GetLayerCollision(u32 layerIndexA, u32 layerIndexB) const noexcept;

        // Synchronizes RigidBodyComponents into Jolt BodyInterface, steps simulation
        // at fixed intervals, and writes updated transforms back to SoA TransformComponent array.
        using FixedStepCallback = std::function<void(f32 fixedDeltaTime)>;
        u32 StepSimulation(
            f32 deltaTime,
            Scene::Scene& scene,
            Core::TaskScheduler* scheduler = nullptr,
            const FixedStepCallback& preSubstepCallback = {});

        // Immediately synchronizes a single entity's RigidBodyComponent properties/shape/transform to Jolt
        bool SyncEntityBody(Scene::Scene& scene, Scene::EntityId entity);

        // Destroys all Jolt bodies and rebuilds from the current scene state, clearing velocities and accumulator.
        void ResetSimulation(Scene::Scene& scene);
        void RebuildFromScene(Scene::Scene& scene);

        // Explicit body lifecycle management
        bool DestroyBody(Scene::EntityId entity);
        void ClearAllBodies(Scene::Scene* scene = nullptr);

        // Optional multi-collider / compound shape support for an entity
        void SetEntityCompoundColliders(Scene::EntityId entity, std::span<const ColliderShapeDesc> colliders);
        void ClearEntityCompoundColliders(Scene::EntityId entity);
        void SyncEntityCompoundColliders(const Scene::Scene& scene, Scene::EntityId entity);

        // Forces, impulses, torques, velocities, and body state
        bool AddForce(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& force, ForceMode mode = ForceMode::Force);
        bool AddForceAtPosition(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& force, const Math::Vec3& position, ForceMode mode = ForceMode::Force);
        bool AddLinearImpulse(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& impulse);
        bool AddTorque(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& torque, ForceMode mode = ForceMode::Force);

        bool SetLinearVelocity(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& velocity);
        [[nodiscard]] bool GetLinearVelocity(const Scene::Scene& scene, Scene::EntityId entity, Math::Vec3& outVelocity) const;
        bool SetAngularVelocity(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& angularVelocity);
        [[nodiscard]] bool GetAngularVelocity(const Scene::Scene& scene, Scene::EntityId entity, Math::Vec3& outAngularVelocity) const;

        bool WakeUp(Scene::Scene& scene, Scene::EntityId entity);
        bool Sleep(Scene::Scene& scene, Scene::EntityId entity);
        [[nodiscard]] bool IsSleeping(const Scene::Scene& scene, Scene::EntityId entity) const;
        bool TeleportBody(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& position, const Math::Quat& rotation);

        // Physics Queries (Raycast, ShapeCast, Overlap)
        [[nodiscard]] bool Raycast(
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            const Math::Vec3& direction,
            f32 maxDistance,
            RaycastHit& outHit,
            u32 layerMask = 0xFFFFFFFFu,
            bool includeTriggers = false) const;

        [[nodiscard]] bool Raycast(
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            const Math::Vec3& direction,
            f32 maxDistance,
            RaycastHit& outHit,
            const QueryFilter& filter) const;

        u32 RaycastAll(
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            const Math::Vec3& direction,
            f32 maxDistance,
            std::vector<RaycastHit>& outHits,
            const QueryFilter& filter = {}) const;

        [[nodiscard]] bool SphereCast(
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            f32 radius,
            const Math::Vec3& direction,
            f32 maxDistance,
            RaycastHit& outHit,
            const QueryFilter& filter = {}) const;

        [[nodiscard]] bool BoxCast(
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            const Math::Vec3& halfExtents,
            const Math::Quat& orientation,
            const Math::Vec3& direction,
            f32 maxDistance,
            RaycastHit& outHit,
            const QueryFilter& filter = {}) const;

        [[nodiscard]] bool CapsuleCast(
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            f32 radius,
            f32 halfHeight,
            const Math::Quat& orientation,
            const Math::Vec3& direction,
            f32 maxDistance,
            RaycastHit& outHit,
            const QueryFilter& filter = {}) const;

        u32 OverlapSphere(
            const Scene::Scene& scene,
            const Math::Vec3& center,
            f32 radius,
            std::vector<OverlapHit>& outHits,
            const QueryFilter& filter = {}) const;

        u32 OverlapBox(
            const Scene::Scene& scene,
            const Math::Vec3& center,
            const Math::Vec3& halfExtents,
            const Math::Quat& orientation,
            std::vector<OverlapHit>& outHits,
            const QueryFilter& filter = {}) const;

        u32 OverlapCapsule(
            const Scene::Scene& scene,
            const Math::Vec3& center,
            f32 radius,
            f32 halfHeight,
            const Math::Quat& orientation,
            std::vector<OverlapHit>& outHits,
            const QueryFilter& filter = {}) const;

        // Constraints / Joints (Fixed, Distance, Hinge, Slider)
        [[nodiscard]] JointHandle CreateJoint(Scene::Scene& scene, const JointDesc& desc);
        bool UpdateJoint(Scene::Scene& scene, JointHandle handle, const JointDesc& desc);
        [[nodiscard]] bool GetJointDesc(JointHandle handle, JointDesc& outDesc) const;
        [[nodiscard]] bool GetJointState(
            JointHandle handle,
            f32& outCurrentPositionOrAngle,
            f32& outAppliedForce,
            f32& outAppliedTorque) const;
        void SyncSceneJoints(Scene::Scene& scene);
        bool DestroyJoint(JointHandle handle);
        [[nodiscard]] bool IsJointValid(JointHandle handle) const noexcept;

        // Character Physics (JPH::CharacterVirtual)
        bool SyncCharacterController(Scene::Scene& scene, Scene::EntityId entity);
        bool DestroyCharacter(Scene::EntityId entity);
        bool MoveCharacter(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& displacement);
        bool JumpCharacter(Scene::Scene& scene, Scene::EntityId entity, f32 jumpSpeed);
        bool SetCharacterVelocity(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& velocity);
        [[nodiscard]] bool GetCharacterVelocity(const Scene::Scene& scene, Scene::EntityId entity, Math::Vec3& outVelocity) const;
        [[nodiscard]] bool GetCharacterState(const Scene::Scene& scene, Scene::EntityId entity, CharacterControllerState& outState) const;
        bool TeleportCharacter(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& position);

        [[nodiscard]] u32 GetBodyCount() const noexcept;
        [[nodiscard]] PhysicsStats GetStats() const noexcept;
        // Thread-safe snapshot of collision/trigger events recorded during the latest simulation step.
        [[nodiscard]] std::vector<CollisionPairEvent> GetRecentCollisions() const;
        void CopyRecentCollisions(std::vector<CollisionPairEvent>& outCollisions) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
