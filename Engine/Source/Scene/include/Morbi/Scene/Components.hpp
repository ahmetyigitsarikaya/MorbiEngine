#pragma once

#include <memory>
#include <type_traits>

#include "Morbi/Asset/Animation.hpp"
#include "Morbi/Asset/AnimationEvaluator.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Memory/GenerationalHandle.hpp"
#include "Morbi/Core/StringId.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/RHITypes.hpp"

namespace Morbi::Scene
{
    struct EntityTag {};
    using EntityId = Core::GenerationalHandle<EntityTag>;

    // ============================================================================
    // Blittable POD Components for Contiguous SoA & Zero-Copy C# Span<T> Interop
    // ============================================================================

    // 1. TransformComponent (Exact 40-byte tightly packed layout matching C# TransformComponent)
    using TransformComponent = Math::Transform;
    static_assert(sizeof(TransformComponent) == 40, "TransformComponent must remain 40 bytes for C# Span<T> interop!");
    static_assert(std::is_standard_layout_v<TransformComponent> && std::is_trivially_copyable_v<TransformComponent>);

    // 2. HierarchyComponent (Scene graph parent/child/sibling links)
    struct HierarchyComponent
    {
        EntityId Parent = EntityId::Invalid();
        EntityId FirstChild = EntityId::Invalid();
        EntityId NextSibling = EntityId::Invalid();
        EntityId PrevSibling = EntityId::Invalid();
    };
    static_assert(sizeof(HierarchyComponent) == 32);
    static_assert(std::is_standard_layout_v<HierarchyComponent> && std::is_trivially_copyable_v<HierarchyComponent>);

    // Reserved well-known procedural asset IDs (JS Safe Integer compatible <= 2^53 - 1)
    inline constexpr u64 kProceduralCubeMeshId   = 0x1000C0BE01ull; // 68732108289
    inline constexpr u64 kProceduralCameraMeshId = 0x1000CA3001ull; // 68732112897

    // 8. CameraComponent (32-byte contiguous projection & active camera state for SoA + C# Span<T>)
    struct CameraComponent
    {
        f32 FovDegrees = 60.0f;
        f32 NearPlane = 0.1f;
        f32 FarPlane = 1000.0f;
        f32 AspectRatio = 16.0f / 9.0f;
        u32 IsActive = 1u;
        u32 Priority = 0u;
        u32 ProjectionMode = 0u; // 0 = Perspective, 1 = Orthographic
        f32 OrthoSize = 10.0f;
    };
    static_assert(sizeof(CameraComponent) == 32);
    static_assert(std::is_standard_layout_v<CameraComponent> && std::is_trivially_copyable_v<CameraComponent>);

    // 3. MeshRendererComponent (Bindless Mesh & Material IDs + GPU visibility flags)
    struct MeshRendererComponent
    {
        u64 MeshAssetId = 0;
        u64 MaterialAssetId = 0;
        u32 SubmeshIndex = 0;
        u32 BindlessVertexBufferIndex = 0xFFFFFFFFu;
        u32 BindlessIndexBufferIndex = 0xFFFFFFFFu;
        u32 Flags = 1u; // bit 0: Visible, bit 1: CastShadows, bit 2: ReceiveShadows
    };
    static_assert(sizeof(MeshRendererComponent) == 32);
    static_assert(std::is_standard_layout_v<MeshRendererComponent> && std::is_trivially_copyable_v<MeshRendererComponent>);

    enum class LightType : u32
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    // 4. LightComponent (32-byte contiguous lighting data)
    struct LightComponent
    {
        Math::Vec3 Color{ 1.0f, 0.98f, 0.92f };
        f32 Intensity = 5.0f;
        f32 Range = 25.0f;
        f32 SpotInnerConeRadians = 0.35f;
        f32 SpotOuterConeRadians = 0.55f;
        LightType Type = LightType::Directional;
    };
    static_assert(sizeof(LightComponent) == 32);
    static_assert(std::is_standard_layout_v<LightComponent> && std::is_trivially_copyable_v<LightComponent>);

    // 5. ScriptComponent (.NET Managed Class StringId + GCHandle slot + state flags)
    struct ScriptComponent
    {
        u64 ScriptClassId = 0;      // 64-bit FNV-1a StringId of C# class name (e.g. "Sandbox.RotatorController"_sid)
        u64 ManagedGCHandle = 0;    // Pin/Weak GCHandle or managed instance index
        f32 TickInterval = 0.0f;    // 0.0f = every frame
        f32 AngularSpeed = 1.5f;    // Custom fast user parameter slot
        u32 Enabled = 1u;
        u32 Reserved = 0u;
    };
    static_assert(sizeof(ScriptComponent) == 32);
    static_assert(std::is_standard_layout_v<ScriptComponent> && std::is_trivially_copyable_v<ScriptComponent>);

    enum class RigidBodyMotionType : u8
    {
        Static = 0,
        Kinematic = 1,
        Dynamic = 2
    };

    enum class ColliderShapeType : u8
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2
    };

    enum class RigidBodyDirtyFlags : u8
    {
        None       = 0,
        Properties = 1 << 0,
        Shape      = 1 << 1,
        MotionType = 1 << 2,
        Transform  = 1 << 3,
        All        = 0xFF
    };

    // 6. RigidBodyComponent (116-byte contiguous Jolt Physics body & collider data for SoA + C# Span<T>)
    struct RigidBodyComponent
    {
        Math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
        f32 Mass = 1.0f;
        Math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };
        f32 Density = 1000.0f;
        Math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f }; // Box: half-extents; Sphere: radius in x; Capsule: radius in x, cylinder half-height in y
        f32 Restitution = 0.35f;
        Math::Vec3 ColliderOffset{ 0.0f, 0.0f, 0.0f };
        f32 Friction = 0.5f;
        Math::Quat ColliderRotation = Math::Quat::Identity();
        f32 LinearDamping = 0.05f;
        f32 AngularDamping = 0.05f;
        f32 GravityFactor = 1.0f;
        u32 JoltBodyId = 0xFFFFFFFFu;
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
        RigidBodyMotionType MotionType = RigidBodyMotionType::Dynamic;
        ColliderShapeType Shape = ColliderShapeType::Box;
        u8 IsAwake = 1;
        u8 UseGravity = 1;
        u8 UseCCD = 0;       // 0 = Discrete, 1 = LinearCast CCD
        u8 IsTrigger = 0;    // 0 = Solid collider, 1 = Sensor/Trigger
        u8 UseDensity = 0;   // 0 = Explicit Mass, 1 = Calculate Mass from Density * Volume
        u8 LockedAxes = 0;   // bit 0..2: LockPos X/Y/Z, bit 3..5: LockRot X/Y/Z
        u8 DirtyFlags = 0;   // RigidBodyDirtyFlags bitmask for runtime/editor sync
        u8 Reserved0 = 0;
        u8 Reserved1 = 0;
        u8 Reserved2 = 0;
    };
    static_assert(sizeof(RigidBodyComponent) == 116);
    static_assert(std::is_standard_layout_v<RigidBodyComponent> && std::is_trivially_copyable_v<RigidBodyComponent>);

    // 7. AudioSourceComponent (32-byte contiguous 3D Spatial Audio state)
    struct AudioSourceComponent
    {
        u64 SoundAssetId = 0; // 64-bit StringId of audio virtual path
        f32 Volume = 1.0f;
        f32 Pitch = 1.0f;
        f32 MinDistance = 1.0f;
        f32 MaxDistance = 50.0f;
        u32 ActiveVoiceHandle = 0;
        u8 Spatial3D = 1;
        u8 Looping = 0;
        u8 PlayOnStart = 0;
        u8 Reserved = 0;
    };
    static_assert(sizeof(AudioSourceComponent) == 32);
    static_assert(std::is_standard_layout_v<AudioSourceComponent> && std::is_trivially_copyable_v<AudioSourceComponent>);

    enum class CharacterControllerDirtyFlags : u8
    {
        None = 0,
        Shape = 1 << 0,
        Properties = 1 << 1,
        Transform = 1 << 2,
        All = 0xFF
    };

    // 10. CharacterControllerComponent (112-byte blittable POD aligned for C# Span<T>)
    struct CharacterControllerComponent
    {
        Math::Vec3 Velocity{ 0.0f, 0.0f, 0.0f };
        f32 Radius = 0.35f;
        Math::Vec3 DesiredHorizontalVel{ 0.0f, 0.0f, 0.0f };
        f32 Height = 1.8f;
        Math::Vec3 GroundNormal{ 0.0f, 1.0f, 0.0f };
        f32 Mass = 70.0f;
        Math::Vec3 CenterOffset{ 0.0f, 0.0f, 0.0f };
        f32 MaxPushStrength = 250.0f;
        Math::Vec3 PendingDisplacement{ 0.0f, 0.0f, 0.0f };
        f32 PendingJumpSpeed = 0.0f;
        f32 MaxSlopeAngleDegrees = 45.0f;
        f32 StepHeight = 0.35f;
        f32 GravityFactor = 1.0f;
        f32 AirControl = 0.35f;
        f32 GroundFriction = 12.0f;
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
        u8 IsGrounded = 0u;
        u8 IsSliding = 0u;
        u8 JumpRequested = 0u;
        u8 DirtyFlags = 0u;
    };
    static_assert(sizeof(CharacterControllerComponent) == 112);
    static_assert(std::is_standard_layout_v<CharacterControllerComponent> && std::is_trivially_copyable_v<CharacterControllerComponent>);

    // 9. AnimatorComponent (Skeletal animation playback, evaluator state, and GPU bone palette buffer)
    struct AnimatorComponent
    {
        std::shared_ptr<Asset::AnimationClip> CurrentClip = nullptr;
        std::shared_ptr<Asset::AnimationClip> PreviousClip = nullptr;
        Asset::AnimationEvaluator Evaluator{};
        Asset::PerObjectBuffer GpuBoneBuffer{};
        RHI::BufferHandle GpuBuffer = RHI::BufferHandle::Invalid();
        bool IsPlaying = true;
        f32 PlaybackSpeed = 1.0f;
        bool NeedsGpuUpload = true;

        void Play() noexcept
        {
            IsPlaying = true;
            Evaluator.Play();
        }

        void Pause() noexcept
        {
            IsPlaying = false;
            Evaluator.Pause();
        }

        void Stop() noexcept
        {
            IsPlaying = false;
            Evaluator.Stop();
            Evaluator.FillGPUBuffer(GpuBoneBuffer);
            NeedsGpuUpload = true;
        }
    };

    // ============================================================================
    // Compound Collider Sub-Shapes & Authored Physics Joint Descriptors (Scene)
    // ============================================================================
    struct CompoundColliderEntry
    {
        ColliderShapeType Shape = ColliderShapeType::Box;
        Math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
        Math::Vec3 LocalOffset{ 0.0f, 0.0f, 0.0f };
        Math::Quat LocalRotation = Math::Quat::Identity();
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
        u8 IsTrigger = 0u;
    };

    enum class PhysicsJointType : u8
    {
        Fixed = 0,
        Distance = 1,
        Hinge = 2,
        Slider = 3
    };

    struct PhysicsJointEntry
    {
        PhysicsJointType Type = PhysicsJointType::Fixed;
        EntityId ConnectedEntity = EntityId::Invalid(); // Invalid = attach to world
        Math::Vec3 AnchorA{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 AnchorB{ 0.0f, 0.0f, 0.0f };
        Math::Vec3 Axis{ 0.0f, 1.0f, 0.0f };
        bool AutoDetectAnchors = true;
        f32 MinDistance = -1.0f;
        f32 MaxDistance = -1.0f;
        f32 SpringFrequencyHz = 0.0f;
        f32 SpringDampingRatio = 0.0f;
        f32 MinLimit = -Math::kPi;
        f32 MaxLimit = Math::kPi;
        f32 MaxFriction = 0.0f;
        f32 BreakForce = 0.0f;   // <= 0 means unbreakable
        f32 BreakTorque = 0.0f;  // <= 0 means unbreakable
        bool Enabled = true;
        bool Broken = false;     // Runtime-only feedback when broken by impulse threshold
        u64 RuntimeJointHandle = 0ull; // Runtime packed JointHandle while active in PhysicsWorld
    };

    // ============================================================================
    // Network Identity Component (Opt-In Static Scene Replication Metadata)
    // ============================================================================
    struct NetworkIdentityComponent
    {
        u64 StaticNetId = 0; // Explicit scene-authored stable identity (> 0 when Replicated != 0)
        u8 Replicated = 0;   // 0 = local-only, 1 = opt-in replicated
        u8 Authority = 1;    // 0 = None, 1 = Server, 2 = ClientPredicted
        u8 Scope = 0;        // 0 = AllRelevant, 1 = OwnerOnly, 2 = InitialOnly
        u8 Reserved = 0;
    };

    // ============================================================================
    // NativeComponentArenaView: Zero-Copy Descriptor Exposed Directly to C# Span<T>
    // ============================================================================
    struct NativeComponentArenaView
    {
        void* DenseComponentsPtr = nullptr;      // Contiguous TComponent[Count] -> C# new Span<T>(DenseComponentsPtr, Count)
        const EntityId* DenseEntitiesPtr = nullptr; // Contiguous EntityId[Count] -> C# new ReadOnlySpan<EntityId>(DenseEntitiesPtr, Count)
        u32 Count = 0;
        u32 ComponentStrideBytes = 0;
    };
}
