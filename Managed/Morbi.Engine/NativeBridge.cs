using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Morbi.Engine;

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct Vec3
{
    public float X;
    public float Y;
    public float Z;

    public Vec3(float x, float y, float z) { X = x; Y = y; Z = z; }

    public static Vec3 Zero => new(0f, 0f, 0f);
    public static Vec3 One => new(1f, 1f, 1f);
    public static Vec3 Up => new(0f, 1f, 0f);
    public static Vec3 Down => new(0f, -1f, 0f);
    public static Vec3 Forward => new(0f, 0f, 1f);
    public static Vec3 Right => new(1f, 0f, 0f);

    public float LengthSquared() => X * X + Y * Y + Z * Z;
    public float Length() => MathF.Sqrt(LengthSquared());

    public Vec3 Normalized
    {
        get
        {
            float len = Length();
            return len > 1e-6f ? new Vec3(X / len, Y / len, Z / len) : Zero;
        }
    }

    public static float Dot(in Vec3 a, in Vec3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static Vec3 Cross(in Vec3 a, in Vec3 b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X);

    public static float Distance(in Vec3 a, in Vec3 b) => (a - b).Length();

    public static Vec3 operator +(in Vec3 a, in Vec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vec3 operator -(in Vec3 a, in Vec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vec3 operator -(in Vec3 v) => new(-v.X, -v.Y, -v.Z);
    public static Vec3 operator *(in Vec3 v, float s) => new(v.X * s, v.Y * s, v.Z * s);
    public static Vec3 operator *(float s, in Vec3 v) => new(v.X * s, v.Y * s, v.Z * s);
    public static Vec3 operator /(in Vec3 v, float s) => new(v.X / s, v.Y / s, v.Z / s);
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct Quat
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public static Quat Identity => new() { X = 0f, Y = 0f, Z = 0f, W = 1f };
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public readonly record struct EntityId(uint Index, uint Generation)
{
    public ulong Pack() => ((ulong)Generation << 32) | Index;

    public static EntityId Unpack(ulong packed)
    {
        return new EntityId((uint)(packed & 0xFFFFFFFFu), (uint)(packed >> 32));
    }

    public static EntityId Invalid => new(0u, 0u);
    public bool IsValid => Generation != 0u;
}

// Exact 40-byte layout matching C++ Morbi::Scene::TransformComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct TransformComponent
{
    public Vec3 Position;
    public Quat Rotation;
    public Vec3 Scale;
}

// Exact 116-byte layout matching C++ Morbi::Scene::RigidBodyComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct RigidBodyComponent
{
    public Vec3 LinearVelocity;
    public float Mass;
    public Vec3 AngularVelocity;
    public float Density;
    public Vec3 HalfExtents;
    public float Restitution;
    public Vec3 ColliderOffset;
    public float Friction;
    public Quat ColliderRotation;
    public float LinearDamping;
    public float AngularDamping;
    public float GravityFactor;
    public uint JoltBodyId;
    public uint CollisionLayer;
    public uint CollisionMask;
    public byte MotionType;
    public byte Shape;
    public byte IsAwake;
    public byte UseGravity;
    public byte UseCCD;
    public byte IsTrigger;
    public byte UseDensity;
    public byte LockedAxes;
    public byte DirtyFlags;
    public byte Reserved0;
    public byte Reserved1;
    public byte Reserved2;
}

// Exact 32-byte layout matching C++ Morbi::Scene::HierarchyComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct HierarchyComponent
{
    public EntityId Parent;
    public EntityId FirstChild;
    public EntityId NextSibling;
    public EntityId PrevSibling;
}

// Exact 32-byte layout matching C++ Morbi::Scene::CameraComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct CameraComponent
{
    public float FovDegrees;
    public float NearPlane;
    public float FarPlane;
    public float AspectRatio;
    public uint IsActive;
    public uint Priority;
    public uint ProjectionMode;
    public float OrthoSize;
}

// Exact 32-byte layout matching C++ Morbi::Scene::MeshRendererComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct MeshRendererComponent
{
    public ulong MeshAssetId;
    public ulong MaterialAssetId;
    public uint SubmeshIndex;
    public uint BindlessVertexBufferIndex;
    public uint BindlessIndexBufferIndex;
    public uint Flags;
}

// Exact 32-byte layout matching C++ Morbi::Scene::LightComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct LightComponent
{
    public Vec3 Color;
    public float Intensity;
    public float Range;
    public float SpotInnerConeRadians;
    public float SpotOuterConeRadians;
    public uint Type;
}

// Exact 32-byte layout matching C++ Morbi::Scene::ScriptComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct ScriptComponent
{
    public ulong ScriptClassId;
    public ulong ManagedGCHandle;
    public float TickInterval;
    public float AngularSpeed;
    public uint Enabled;
    public uint Reserved;
}

// Exact 32-byte layout matching C++ Morbi::Scene::AudioSourceComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct AudioSourceComponent
{
    public ulong SoundAssetId;
    public float Volume;
    public float Pitch;
    public float MinDistance;
    public float MaxDistance;
    public uint ActiveVoiceHandle;
    public byte Spatial3D;
    public byte Looping;
    public byte PlayOnStart;
    public byte Reserved;
}

[Flags]
public enum CharacterControllerDirtyFlags : byte
{
    None = 0,
    Shape = 1 << 0,
    Properties = 1 << 1,
    Transform = 1 << 2,
    All = 0xFF
}

// Exact 112-byte layout matching C++ Morbi::Scene::CharacterControllerComponent
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct CharacterControllerComponent
{
    public Vec3 Velocity;
    public float Radius;
    public Vec3 DesiredHorizontalVel;
    public float Height;
    public Vec3 GroundNormal;
    public float Mass;
    public Vec3 CenterOffset;
    public float MaxPushStrength;
    public Vec3 PendingDisplacement;
    public float PendingJumpSpeed;
    public float MaxSlopeAngleDegrees;
    public float StepHeight;
    public float GravityFactor;
    public float AirControl;
    public float GroundFriction;
    public uint CollisionLayer;
    public uint CollisionMask;
    public byte IsGrounded;
    public byte IsSliding;
    public byte JumpRequested;
    public byte DirtyFlags;
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeCharacterState
{
    public ulong GroundEntityPacked;
    public Vec3 Velocity;
    public float Speed;
    public Vec3 GroundNormal;
    public byte GroundState;
    public byte IsGrounded;
    public byte IsSliding;
    public byte Reserved;
    public Vec3 GroundVelocity;
    public uint ReservedPad;
}

[Flags]
public enum RigidBodyDirtyFlags : byte
{
    None = 0,
    Properties = 1 << 0,
    Shape = 1 << 1,
    MotionType = 1 << 2,
    Transform = 1 << 3,
    All = 0xFF
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeQueryFilter
{
    public ulong IgnoreEntityPacked;
    public uint LayerMask;
    public byte IncludeTriggers;
    public byte IgnoreStatic;
    public byte IgnoreKinematic;
    public byte IgnoreDynamic;
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeRaycastHit
{
    public ulong HitEntityPacked;
    public Vec3 Point;
    public Vec3 Normal;
    public float Distance;
    public byte Shape;
    public byte Reserved0;
    public byte Reserved1;
    public byte Reserved2;
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeOverlapHit
{
    public ulong EntityPacked;
    public Vec3 ContactPoint;
    public Vec3 PenetrationAxis;
    public float PenetrationDepth;
    public byte Shape;
    public byte Reserved0;
    public byte Reserved1;
    public byte Reserved2;
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeCollisionEvent
{
    public ulong EntityAPacked;
    public ulong EntityBPacked;
    public Vec3 ContactPoint;
    public Vec3 ContactNormal;
    public Vec3 RelativeVelocity;
    public float PenetrationDepth;
    public byte ShapeA;
    public byte ShapeB;
    public byte EventType;
    public byte IsTrigger;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeJointDesc
{
    public ulong EntityAPacked;
    public ulong EntityBPacked;
    public Vec3 AnchorA;
    public float MinDistance;
    public Vec3 AnchorB;
    public float MaxDistance;
    public Vec3 Axis;
    public float SpringFrequencyHz;
    public float SpringDampingRatio;
    public float MinLimit;
    public float MaxLimit;
    public float MaxFriction;
    public float BreakForce;
    public float BreakTorque;
    public byte Type;
    public byte AutoDetectAnchors;
    public byte Enabled;
    public byte Reserved0;
    public uint Reserved1;
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct NativeColliderShapeDesc
{
    public Vec3 HalfExtents;
    public uint CollisionLayer;
    public Vec3 LocalOffset;
    public uint CollisionMask;
    public Quat LocalRotation;
    public byte Shape;
    public byte IsTrigger;
    public byte Reserved0;
    public byte Reserved1;
    public uint Reserved2;
}

public enum ComponentPoolKind : byte
{
    Transform = 0,
    Hierarchy = 1,
    MeshRenderer = 2,
    Light = 3,
    Script = 4,
    RigidBody = 5,
    AudioSource = 6,
    Camera = 7,
    Animator = 8,
    CharacterController = 9
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeComponentArenaView
{
    public void* DenseComponentsPtr;
    public EntityId* DenseEntitiesPtr;
    public uint Count;
    public uint ComponentStrideBytes;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeEngineFunctionTable
{
    public uint StructSize;
    public uint ApiVersion;

    // Core & Log
    public delegate* unmanaged[Cdecl]<byte*, byte*, void> LogInfo;
    public delegate* unmanaged[Cdecl]<byte, NativeComponentArenaView*, void> GetComponentArenaView;

    // Physics
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> AddLinearImpulse;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> SetLinearVelocity;
    public delegate* unmanaged[Cdecl]<Vec3*, Vec3*, float, Vec3*, byte> Raycast;

    // Audio & Engine
    public delegate* unmanaged[Cdecl]<byte*, Vec3*, float, float, byte> PlaySpatialSound3D;
    public delegate* unmanaged[Cdecl]<byte, void> SetVSync;

    // UI Data-Binding
    public delegate* unmanaged[Cdecl]<byte*, float, byte*, void> SetGameUIProperty;
    public delegate* unmanaged[Cdecl]<byte*, int, void> TriggerGameUIEvent;

    // Scene Management (Phase 24)
    public delegate* unmanaged[Cdecl]<byte*, ulong> FindEntityByName;
    public delegate* unmanaged[Cdecl]<byte*, ulong> CreateEntity;
    public delegate* unmanaged[Cdecl]<ulong, byte> DestroyEntity;
    public delegate* unmanaged[Cdecl]<ulong, byte> IsEntityAlive;
    public delegate* unmanaged[Cdecl]<ulong, ulong, byte> SetEntityParent;

    // Animation / AnimatorComponent (Phase 24)
    public delegate* unmanaged[Cdecl]<ulong, byte> PlayAnimation;
    public delegate* unmanaged[Cdecl]<ulong, byte> PauseAnimation;
    public delegate* unmanaged[Cdecl]<ulong, byte> StopAnimation;
    public delegate* unmanaged[Cdecl]<ulong, byte*, byte> SetAnimationClip;
    public delegate* unmanaged[Cdecl]<ulong, float, byte> SetAnimationSpeed;
    public delegate* unmanaged[Cdecl]<ulong, byte> IsAnimationPlaying;

    // Input Management (Phase 24)
    public delegate* unmanaged[Cdecl]<uint, byte> IsKeyDown;
    public delegate* unmanaged[Cdecl]<uint, byte> WasKeyPressed;
    public delegate* unmanaged[Cdecl]<uint, byte> WasKeyReleased;
    public delegate* unmanaged[Cdecl]<byte, byte> IsMouseButtonDown;
    public delegate* unmanaged[Cdecl]<byte, byte> WasMouseButtonPressed;
    public delegate* unmanaged[Cdecl]<float*, float*, void> GetMousePosition;
    public delegate* unmanaged[Cdecl]<float*, float*, void> GetMouseDelta;
    public delegate* unmanaged[Cdecl]<float*, float*, void> GetMouseWheelDelta;

    // Extended Mouse Capture & 3D Axis Input (X, Y, Z / Scroll & All Mouse Buttons)
    public delegate* unmanaged[Cdecl]<byte, byte> WasMouseButtonReleased;
    public delegate* unmanaged[Cdecl]<float*, float*, float*, void> GetMouseDelta3D;
    public delegate* unmanaged[Cdecl]<float*, float*, float*, void> GetMousePosition3D;
    public delegate* unmanaged[Cdecl]<byte, void> SetMouseCaptured;
    public delegate* unmanaged[Cdecl]<byte> IsMouseCaptured;
    public delegate* unmanaged[Cdecl]<byte, void> SetCursorVisible;
    public delegate* unmanaged[Cdecl]<byte> IsCursorVisible;

    // Complete C# Entity, Component & Scene Bindings (v3)
    public delegate* unmanaged[Cdecl]<ulong, byte*, uint, uint> GetEntityName;
    public delegate* unmanaged[Cdecl]<ulong, byte*, byte> SetEntityName;
    public delegate* unmanaged[Cdecl]<ulong, ulong> GetEntityParent;
    public delegate* unmanaged[Cdecl]<uint> GetAliveEntityCount;
    public delegate* unmanaged[Cdecl]<ulong, byte, byte> HasComponent;
    public delegate* unmanaged[Cdecl]<ulong, byte, byte> AddComponent;
    public delegate* unmanaged[Cdecl]<ulong, byte, byte> RemoveComponent;
    public delegate* unmanaged[Cdecl]<byte*, byte> LoadScene;
    public delegate* unmanaged[Cdecl]<byte*, byte> SaveScene;
    public delegate* unmanaged[Cdecl]<void> ClearScene;

    // Animation CrossFade & Pose Blending
    public delegate* unmanaged[Cdecl]<ulong, byte*, float, byte> CrossFadeAnimationClip;
    public delegate* unmanaged[Cdecl]<ulong, byte> IsAnimationBlending;

    // Virtual File System (PathService) Resolution
    public delegate* unmanaged[Cdecl]<byte*, byte*, uint, uint> ResolveVirtualPath;

    // Production Jolt Physics Bridge (v4)
    public delegate* unmanaged[Cdecl]<ulong, byte> SyncRigidBody;
    public delegate* unmanaged[Cdecl]<ulong, void> MarkPhysicsTransformDirty;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte, byte> AddForce;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, Vec3*, byte, byte> AddForceAtPosition;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte, byte> AddTorque;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> GetLinearVelocity;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> SetAngularVelocity;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> GetAngularVelocity;
    public delegate* unmanaged[Cdecl]<ulong, byte> WakeUpBody;
    public delegate* unmanaged[Cdecl]<ulong, byte> SleepBody;
    public delegate* unmanaged[Cdecl]<ulong, byte> IsBodySleeping;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, Quat*, byte> TeleportBody;
    public delegate* unmanaged[Cdecl]<Vec3*, void> SetGravity;
    public delegate* unmanaged[Cdecl]<Vec3*, void> GetGravity;
    public delegate* unmanaged[Cdecl]<uint, uint, byte, void> SetLayerCollision;
    public delegate* unmanaged[Cdecl]<uint, uint, byte> GetLayerCollision;
    public delegate* unmanaged[Cdecl]<Vec3*, Vec3*, float, NativeQueryFilter*, NativeRaycastHit*, byte> RaycastEx;
    public delegate* unmanaged[Cdecl]<Vec3*, Vec3*, float, NativeQueryFilter*, NativeRaycastHit*, uint, uint> RaycastAllEx;
    public delegate* unmanaged[Cdecl]<Vec3*, float, Vec3*, float, NativeQueryFilter*, NativeRaycastHit*, byte> SphereCastEx;
    public delegate* unmanaged[Cdecl]<Vec3*, Vec3*, Quat*, Vec3*, float, NativeQueryFilter*, NativeRaycastHit*, byte> BoxCastEx;
    public delegate* unmanaged[Cdecl]<Vec3*, float, float, Quat*, Vec3*, float, NativeQueryFilter*, NativeRaycastHit*, byte> CapsuleCastEx;
    public delegate* unmanaged[Cdecl]<Vec3*, float, NativeQueryFilter*, NativeOverlapHit*, uint, uint> OverlapSphereEx;
    public delegate* unmanaged[Cdecl]<Vec3*, Vec3*, Quat*, NativeQueryFilter*, NativeOverlapHit*, uint, uint> OverlapBoxEx;
    public delegate* unmanaged[Cdecl]<Vec3*, float, float, Quat*, NativeQueryFilter*, NativeOverlapHit*, uint, uint> OverlapCapsuleEx;

    // Character Physics Bridge (Phase 5)
    public delegate* unmanaged[Cdecl]<ulong, byte> SyncCharacterController;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> MoveCharacter;
    public delegate* unmanaged[Cdecl]<ulong, float, byte> JumpCharacter;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> SetCharacterVelocity;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> GetCharacterVelocity;
    public delegate* unmanaged[Cdecl]<ulong, NativeCharacterState*, byte> GetCharacterState;
    public delegate* unmanaged[Cdecl]<ulong, Vec3*, byte> TeleportCharacter;

    // Joints & Compound Collider Bridge (ApiVersion 5)
    public delegate* unmanaged[Cdecl]<NativeJointDesc*, ulong> CreateJoint;
    public delegate* unmanaged[Cdecl]<ulong, NativeJointDesc*, byte> UpdateJoint;
    public delegate* unmanaged[Cdecl]<ulong, byte> DestroyJoint;
    public delegate* unmanaged[Cdecl]<ulong, byte> IsJointValid;
    public delegate* unmanaged[Cdecl]<ulong, NativeJointDesc*, byte> GetJointDesc;
    public delegate* unmanaged[Cdecl]<ulong, float*, float*, float*, byte> GetJointState;
    public delegate* unmanaged[Cdecl]<ulong, uint> GetEntityJointCount;
    public delegate* unmanaged[Cdecl]<ulong, uint, ulong> GetEntityJointHandle;
    public delegate* unmanaged[Cdecl]<ulong, NativeColliderShapeDesc*, uint, byte> SetEntityCompoundColliders;
    public delegate* unmanaged[Cdecl]<ulong, uint> GetEntityCompoundColliderCount;
    public delegate* unmanaged[Cdecl]<ulong, uint, NativeColliderShapeDesc*, byte> GetEntityCompoundCollider;
    public delegate* unmanaged[Cdecl]<ulong, byte> ClearEntityCompoundColliders;

    // Diagnostics & Unhandled Managed Exception Bridge (ApiVersion 6)
    public delegate* unmanaged[Cdecl]<byte*, byte*, byte*, void> LogFatalException;
    public delegate* unmanaged[Cdecl]<byte> IsDebugMode;

    // Multi-World & Gameplay Networking Bridge (ApiVersion 7 / Phase 8)
    public delegate* unmanaged[Cdecl]<uint> GetActiveWorldId;
    public delegate* unmanaged[Cdecl]<uint, ulong, ulong*, ulong*, byte*, byte*, byte*, byte> NetGetObjectState;
    public delegate* unmanaged[Cdecl]<uint, ulong, ulong*, byte> NetResolveEntityNetworkId;
    public delegate* unmanaged[Cdecl]<uint, ulong, uint, uint, byte, byte, byte*, uint, byte> NetSendManagedRpc;
    public delegate* unmanaged[Cdecl]<uint, ulong, uint, byte*, uint, byte> NetSyncManagedProperties;
}

public static unsafe class NativeBridge
{
    private static NativeEngineFunctionTable s_table;
    private static bool s_initialized;

    internal static void Bind(NativeEngineFunctionTable* table)
    {
        s_table = *table;
        s_initialized = true;
    }

    public static void LogInfo(string category, string message)
    {
        if (!s_initialized || s_table.LogInfo == null || string.IsNullOrEmpty(category) || string.IsNullOrEmpty(message)) return;

        int catByteLen = Encoding.UTF8.GetByteCount(category);
        Span<byte> catUtf8 = catByteLen <= 512 ? stackalloc byte[catByteLen + 1] : new byte[catByteLen + 1];
        Encoding.UTF8.GetBytes(category, catUtf8);
        catUtf8[catByteLen] = 0;

        int msgByteLen = Encoding.UTF8.GetByteCount(message);
        Span<byte> msgUtf8 = msgByteLen <= 512 ? stackalloc byte[msgByteLen + 1] : new byte[msgByteLen + 1];
        Encoding.UTF8.GetBytes(message, msgUtf8);
        msgUtf8[msgByteLen] = 0;

        fixed (byte* pCat = catUtf8)
        fixed (byte* pMsg = msgUtf8)
        {
            s_table.LogInfo(pCat, pMsg);
        }
    }

    public static bool IsDebugMode => s_initialized && s_table.IsDebugMode != null && s_table.IsDebugMode() != 0;

    public static void ReportFatalException(Exception exception, string? context = null)
    {
        if (exception == null) return;

        string typeName = exception.GetType().FullName ?? exception.GetType().Name;
        string msg = string.IsNullOrEmpty(context)
            ? exception.Message
            : $"[{context}] {exception.Message}";
        string stack = exception.ToString();

        if (!s_initialized || s_table.LogFatalException == null)
        {
            LogInfo("C#.Exception", $"{typeName}: {msg}\n{stack}");
            return;
        }

        int typeLen = Encoding.UTF8.GetByteCount(typeName);
        Span<byte> typeUtf8 = typeLen <= 256 ? stackalloc byte[typeLen + 1] : new byte[typeLen + 1];
        Encoding.UTF8.GetBytes(typeName, typeUtf8);
        typeUtf8[typeLen] = 0;

        int msgLen = Encoding.UTF8.GetByteCount(msg);
        Span<byte> msgUtf8 = msgLen <= 512 ? stackalloc byte[msgLen + 1] : new byte[msgLen + 1];
        Encoding.UTF8.GetBytes(msg, msgUtf8);
        msgUtf8[msgLen] = 0;

        int stackLen = Encoding.UTF8.GetByteCount(stack);
        Span<byte> stackUtf8 = stackLen <= 2048 ? stackalloc byte[stackLen + 1] : new byte[stackLen + 1];
        Encoding.UTF8.GetBytes(stack, stackUtf8);
        stackUtf8[stackLen] = 0;

        fixed (byte* pType = typeUtf8)
        fixed (byte* pMsg = msgUtf8)
        fixed (byte* pStack = stackUtf8)
        {
            s_table.LogFatalException(pType, pMsg, pStack);
        }
    }

    private static Span<T> GetComponentSpan<T>(ComponentPoolKind kind, out ReadOnlySpan<EntityId> entities)
        where T : unmanaged
    {
        if (!s_initialized || s_table.GetComponentArenaView == null)
        {
            entities = ReadOnlySpan<EntityId>.Empty;
            return Span<T>.Empty;
        }

        NativeComponentArenaView view = default;
        s_table.GetComponentArenaView((byte)kind, &view);
        if (view.DenseComponentsPtr == null || view.DenseEntitiesPtr == null || view.Count == 0 ||
            view.Count > int.MaxValue ||
            (view.ComponentStrideBytes != 0 && view.ComponentStrideBytes != (uint)sizeof(T)))
        {
            entities = ReadOnlySpan<EntityId>.Empty;
            return Span<T>.Empty;
        }
        entities = new ReadOnlySpan<EntityId>(view.DenseEntitiesPtr, (int)view.Count);
        return new Span<T>(view.DenseComponentsPtr, (int)view.Count);
    }

    private static bool CallWithUtf8String(delegate* unmanaged[Cdecl]<byte*, byte> fn, string value)
    {
        if (!s_initialized || fn == null || string.IsNullOrEmpty(value)) return false;
        int byteLen = Encoding.UTF8.GetByteCount(value);
        Span<byte> utf8 = byteLen <= 512 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(value, utf8);
        utf8[byteLen] = 0;
        fixed (byte* pVal = utf8)
        {
            return fn(pVal) != 0;
        }
    }

    // Zero-Copy Spans directly over C++ SoA SparseSet memory!
    public static Span<TransformComponent> GetTransformSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<TransformComponent>(ComponentPoolKind.Transform, out entities);

    public static Span<HierarchyComponent> GetHierarchySpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<HierarchyComponent>(ComponentPoolKind.Hierarchy, out entities);

    public static Span<CameraComponent> GetCameraSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<CameraComponent>(ComponentPoolKind.Camera, out entities);

    public static Span<MeshRendererComponent> GetMeshRendererSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<MeshRendererComponent>(ComponentPoolKind.MeshRenderer, out entities);

    public static Span<LightComponent> GetLightSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<LightComponent>(ComponentPoolKind.Light, out entities);

    public static Span<ScriptComponent> GetScriptSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<ScriptComponent>(ComponentPoolKind.Script, out entities);

    public static Span<RigidBodyComponent> GetRigidBodySpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<RigidBodyComponent>(ComponentPoolKind.RigidBody, out entities);

    public static Span<AudioSourceComponent> GetAudioSourceSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<AudioSourceComponent>(ComponentPoolKind.AudioSource, out entities);

    public static Span<CharacterControllerComponent> GetCharacterControllerSpan(out ReadOnlySpan<EntityId> entities) =>
        GetComponentSpan<CharacterControllerComponent>(ComponentPoolKind.CharacterController, out entities);

    public static bool AddLinearImpulse(EntityId entity, in Vec3 impulse)
    {
        if (!s_initialized || s_table.AddLinearImpulse == null || !entity.IsValid) return false;
        Vec3 copy = impulse;
        return s_table.AddLinearImpulse(entity.Pack(), &copy) != 0;
    }

    public static bool Raycast(in Vec3 origin, in Vec3 direction, float maxDistance, out Vec3 hitPoint)
    {
        hitPoint = default;
        if (!s_initialized || s_table.Raycast == null) return false;
        Vec3 origCopy = origin;
        Vec3 dirCopy = direction;
        Vec3 hit = default;
        bool ok = s_table.Raycast(&origCopy, &dirCopy, maxDistance, &hit) != 0;
        hitPoint = hit;
        return ok;
    }

    public static bool PlaySpatialSound3D(string soundKey, in Vec3 position, float volume = 1.0f, float pitch = 1.0f)
    {
        if (!s_initialized || s_table.PlaySpatialSound3D == null || string.IsNullOrEmpty(soundKey)) return false;
        int keyByteLen = Encoding.UTF8.GetByteCount(soundKey);
        Span<byte> keyUtf8 = keyByteLen <= 256 ? stackalloc byte[keyByteLen + 1] : new byte[keyByteLen + 1];
        Encoding.UTF8.GetBytes(soundKey, keyUtf8);
        keyUtf8[keyByteLen] = 0;
        Vec3 posCopy = position;
        fixed (byte* pKey = keyUtf8)
        {
            return s_table.PlaySpatialSound3D(pKey, &posCopy, volume, pitch) != 0;
        }
    }

    public static void SetUIProperty(string propertyKey, float value, string? stringValue = null)
    {
        if (!s_initialized || s_table.SetGameUIProperty == null || string.IsNullOrEmpty(propertyKey)) return;

        int keyByteLen = Encoding.UTF8.GetByteCount(propertyKey);
        Span<byte> keyUtf8 = keyByteLen <= 512 ? stackalloc byte[keyByteLen + 1] : new byte[keyByteLen + 1];
        Encoding.UTF8.GetBytes(propertyKey, keyUtf8);
        keyUtf8[keyByteLen] = 0;

        fixed (byte* pKey = keyUtf8)
        {
            if (stringValue != null)
            {
                int strByteLen = Encoding.UTF8.GetByteCount(stringValue);
                Span<byte> strUtf8 = strByteLen <= 512 ? stackalloc byte[strByteLen + 1] : new byte[strByteLen + 1];
                Encoding.UTF8.GetBytes(stringValue, strUtf8);
                strUtf8[strByteLen] = 0;
                fixed (byte* pStr = strUtf8)
                {
                    s_table.SetGameUIProperty(pKey, value, pStr);
                }
            }
            else
            {
                s_table.SetGameUIProperty(pKey, value, null);
            }
        }
    }

    public static void TriggerUIEvent(string eventName, int eventPayload = 0)
    {
        if (!s_initialized || s_table.TriggerGameUIEvent == null || string.IsNullOrEmpty(eventName)) return;

        int nameByteLen = Encoding.UTF8.GetByteCount(eventName);
        Span<byte> nameUtf8 = nameByteLen <= 512 ? stackalloc byte[nameByteLen + 1] : new byte[nameByteLen + 1];
        Encoding.UTF8.GetBytes(eventName, nameUtf8);
        nameUtf8[nameByteLen] = 0;

        fixed (byte* pName = nameUtf8)
        {
            s_table.TriggerGameUIEvent(pName, eventPayload);
        }
    }

    // =========================================================================
    // Phase 24: Extended Native Bridge Functions
    // =========================================================================

    public static bool SetLinearVelocity(EntityId entity, in Vec3 velocity)
    {
        if (!s_initialized || s_table.SetLinearVelocity == null || !entity.IsValid) return false;
        Vec3 copy = velocity;
        return s_table.SetLinearVelocity(entity.Pack(), &copy) != 0;
    }

    public static EntityId FindEntityByName(string name)
    {
        if (!s_initialized || s_table.FindEntityByName == null || string.IsNullOrEmpty(name))
            return EntityId.Invalid;

        int byteLen = Encoding.UTF8.GetByteCount(name);
        Span<byte> nameUtf8 = byteLen <= 256 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(name, nameUtf8);
        nameUtf8[byteLen] = 0;

        fixed (byte* pName = nameUtf8)
        {
            ulong packed = s_table.FindEntityByName(pName);
            return EntityId.Unpack(packed);
        }
    }

    public static EntityId CreateEntity(string name = "Entity")
    {
        if (!s_initialized || s_table.CreateEntity == null) return EntityId.Invalid;

        name ??= "Entity";
        int byteLen = Encoding.UTF8.GetByteCount(name);
        Span<byte> nameUtf8 = byteLen <= 256 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(name, nameUtf8);
        nameUtf8[byteLen] = 0;

        fixed (byte* pName = nameUtf8)
        {
            ulong packed = s_table.CreateEntity(pName);
            return EntityId.Unpack(packed);
        }
    }

    public static bool DestroyEntity(EntityId entity)
    {
        if (!s_initialized || s_table.DestroyEntity == null || !entity.IsValid) return false;
        return s_table.DestroyEntity(entity.Pack()) != 0;
    }

    public static bool IsEntityAlive(EntityId entity)
    {
        if (!s_initialized || s_table.IsEntityAlive == null || !entity.IsValid) return false;
        return s_table.IsEntityAlive(entity.Pack()) != 0;
    }

    public static bool SetEntityParent(EntityId child, EntityId parent)
    {
        if (!s_initialized || s_table.SetEntityParent == null || !child.IsValid) return false;
        return s_table.SetEntityParent(child.Pack(), parent.Pack()) != 0;
    }

    public static bool PlayAnimation(EntityId entity)
    {
        if (!s_initialized || s_table.PlayAnimation == null || !entity.IsValid) return false;
        return s_table.PlayAnimation(entity.Pack()) != 0;
    }

    public static bool PauseAnimation(EntityId entity)
    {
        if (!s_initialized || s_table.PauseAnimation == null || !entity.IsValid) return false;
        return s_table.PauseAnimation(entity.Pack()) != 0;
    }

    public static bool StopAnimation(EntityId entity)
    {
        if (!s_initialized || s_table.StopAnimation == null || !entity.IsValid) return false;
        return s_table.StopAnimation(entity.Pack()) != 0;
    }

    public static bool SetAnimationClip(EntityId entity, string clipPath)
    {
        if (!s_initialized || s_table.SetAnimationClip == null || !entity.IsValid || string.IsNullOrEmpty(clipPath))
            return false;

        int byteLen = Encoding.UTF8.GetByteCount(clipPath);
        Span<byte> clipUtf8 = byteLen <= 512 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(clipPath, clipUtf8);
        clipUtf8[byteLen] = 0;

        fixed (byte* pClip = clipUtf8)
        {
            return s_table.SetAnimationClip(entity.Pack(), pClip) != 0;
        }
    }

    public static bool SetAnimationSpeed(EntityId entity, float speed)
    {
        if (!s_initialized || s_table.SetAnimationSpeed == null || !entity.IsValid) return false;
        return s_table.SetAnimationSpeed(entity.Pack(), speed) != 0;
    }

    public static bool IsAnimationPlaying(EntityId entity)
    {
        if (!s_initialized || s_table.IsAnimationPlaying == null || !entity.IsValid) return false;
        return s_table.IsAnimationPlaying(entity.Pack()) != 0;
    }

    public static bool CrossFadeAnimationClip(EntityId entity, string clipPath, float blendDurationSeconds = 0.15f)
    {
        if (!s_initialized || s_table.CrossFadeAnimationClip == null || !entity.IsValid || string.IsNullOrEmpty(clipPath))
            return false;

        int byteLen = Encoding.UTF8.GetByteCount(clipPath);
        Span<byte> clipUtf8 = byteLen <= 512 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(clipPath, clipUtf8);
        clipUtf8[byteLen] = 0;

        fixed (byte* pClip = clipUtf8)
        {
            return s_table.CrossFadeAnimationClip(entity.Pack(), pClip, blendDurationSeconds) != 0;
        }
    }

    public static bool IsAnimationBlending(EntityId entity)
    {
        if (!s_initialized || s_table.IsAnimationBlending == null || !entity.IsValid) return false;
        return s_table.IsAnimationBlending(entity.Pack()) != 0;
    }

    public static string ResolveVirtualPath(string virtualOrRelativePath)
    {
        if (!s_initialized || s_table.ResolveVirtualPath == null || string.IsNullOrWhiteSpace(virtualOrRelativePath))
            return virtualOrRelativePath;

        int byteLen = Encoding.UTF8.GetByteCount(virtualOrRelativePath);
        Span<byte> inUtf8 = byteLen <= 512 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(virtualOrRelativePath, inUtf8);
        inUtf8[byteLen] = 0;

        Span<byte> outBuf = stackalloc byte[1024];
        fixed (byte* pIn = inUtf8)
        fixed (byte* pOut = outBuf)
        {
            uint len = s_table.ResolveVirtualPath(pIn, pOut, (uint)outBuf.Length);
            if (len > 0 && len < outBuf.Length)
            {
                return Encoding.UTF8.GetString(outBuf.Slice(0, (int)len));
            }
        }
        return virtualOrRelativePath;
    }

    public static bool IsKeyDown(uint scancode)
    {
        if (!s_initialized || s_table.IsKeyDown == null) return false;
        return s_table.IsKeyDown(scancode) != 0;
    }

    public static bool WasKeyPressed(uint scancode)
    {
        if (!s_initialized || s_table.WasKeyPressed == null) return false;
        return s_table.WasKeyPressed(scancode) != 0;
    }

    public static bool WasKeyReleased(uint scancode)
    {
        if (!s_initialized || s_table.WasKeyReleased == null) return false;
        return s_table.WasKeyReleased(scancode) != 0;
    }

    public static bool IsMouseButtonDown(byte button)
    {
        if (!s_initialized || s_table.IsMouseButtonDown == null) return false;
        return s_table.IsMouseButtonDown(button) != 0;
    }

    public static bool WasMouseButtonPressed(byte button)
    {
        if (!s_initialized || s_table.WasMouseButtonPressed == null) return false;
        return s_table.WasMouseButtonPressed(button) != 0;
    }

    public static void GetMousePosition(out float x, out float y)
    {
        x = 0.0f;
        y = 0.0f;
        if (!s_initialized || s_table.GetMousePosition == null) return;
        float outX = 0f, outY = 0f;
        s_table.GetMousePosition(&outX, &outY);
        x = outX;
        y = outY;
    }

    public static void GetMouseDelta(out float dx, out float dy)
    {
        dx = 0.0f;
        dy = 0.0f;
        if (!s_initialized || s_table.GetMouseDelta == null) return;
        float outDx = 0f, outDy = 0f;
        s_table.GetMouseDelta(&outDx, &outDy);
        dx = outDx;
        dy = outDy;
    }

    public static void GetMouseWheelDelta(out float wheelX, out float wheelY)
    {
        wheelX = 0.0f;
        wheelY = 0.0f;
        if (!s_initialized || s_table.GetMouseWheelDelta == null) return;
        float outWX = 0f, outWY = 0f;
        s_table.GetMouseWheelDelta(&outWX, &outWY);
        wheelX = outWX;
        wheelY = outWY;
    }

    public static bool WasMouseButtonReleased(byte button)
    {
        if (!s_initialized || s_table.WasMouseButtonReleased == null) return false;
        return s_table.WasMouseButtonReleased(button) != 0;
    }

    public static void GetMouseDelta3D(out float dx, out float dy, out float dz)
    {
        dx = 0.0f;
        dy = 0.0f;
        dz = 0.0f;
        if (!s_initialized || s_table.GetMouseDelta3D == null) return;
        float outDx = 0f, outDy = 0f, outDz = 0f;
        s_table.GetMouseDelta3D(&outDx, &outDy, &outDz);
        dx = outDx;
        dy = outDy;
        dz = outDz;
    }

    public static void GetMousePosition3D(out float x, out float y, out float z)
    {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        if (!s_initialized || s_table.GetMousePosition3D == null) return;
        float outX = 0f, outY = 0f, outZ = 0f;
        s_table.GetMousePosition3D(&outX, &outY, &outZ);
        x = outX;
        y = outY;
        z = outZ;
    }

    public static void SetMouseCaptured(bool captured)
    {
        if (!s_initialized || s_table.SetMouseCaptured == null) return;
        s_table.SetMouseCaptured(captured ? (byte)1 : (byte)0);
    }

    public static bool IsMouseCaptured()
    {
        if (!s_initialized || s_table.IsMouseCaptured == null) return false;
        return s_table.IsMouseCaptured() != 0;
    }

    public static void SetCursorVisible(bool visible)
    {
        if (!s_initialized || s_table.SetCursorVisible == null) return;
        s_table.SetCursorVisible(visible ? (byte)1 : (byte)0);
    }

    public static bool IsCursorVisible()
    {
        if (!s_initialized || s_table.IsCursorVisible == null) return true;
        return s_table.IsCursorVisible() != 0;
    }

    // =========================================================================
    // Complete C# Entity, Component & Scene Bindings (v3)
    // =========================================================================

    public static string GetEntityName(EntityId entity)
    {
        if (!s_initialized || s_table.GetEntityName == null || !entity.IsValid) return string.Empty;
        uint requiredLen = s_table.GetEntityName(entity.Pack(), null, 0);
        if (requiredLen == 0) return string.Empty;

        int bufSize = (int)requiredLen + 1;
        Span<byte> utf8 = bufSize <= 512 ? stackalloc byte[bufSize] : new byte[bufSize];
        fixed (byte* pBuf = utf8)
        {
            uint written = s_table.GetEntityName(entity.Pack(), pBuf, (uint)bufSize);
            return written == 0 ? string.Empty : Encoding.UTF8.GetString(pBuf, (int)written);
        }
    }

    public static bool SetEntityName(EntityId entity, string name)
    {
        if (!s_initialized || s_table.SetEntityName == null || !entity.IsValid || name == null) return false;
        int byteLen = Encoding.UTF8.GetByteCount(name);
        Span<byte> nameUtf8 = byteLen <= 256 ? stackalloc byte[byteLen + 1] : new byte[byteLen + 1];
        Encoding.UTF8.GetBytes(name, nameUtf8);
        nameUtf8[byteLen] = 0;
        fixed (byte* pName = nameUtf8)
        {
            return s_table.SetEntityName(entity.Pack(), pName) != 0;
        }
    }

    public static EntityId GetEntityParent(EntityId entity)
    {
        if (!s_initialized || s_table.GetEntityParent == null || !entity.IsValid) return EntityId.Invalid;
        return EntityId.Unpack(s_table.GetEntityParent(entity.Pack()));
    }

    public static uint GetAliveEntityCount()
    {
        if (!s_initialized || s_table.GetAliveEntityCount == null) return 0u;
        return s_table.GetAliveEntityCount();
    }

    public static bool HasComponent(EntityId entity, ComponentPoolKind kind)
    {
        if (!s_initialized || s_table.HasComponent == null || !entity.IsValid) return false;
        return s_table.HasComponent(entity.Pack(), (byte)kind) != 0;
    }

    public static bool AddComponent(EntityId entity, ComponentPoolKind kind)
    {
        if (!s_initialized || s_table.AddComponent == null || !entity.IsValid) return false;
        return s_table.AddComponent(entity.Pack(), (byte)kind) != 0;
    }

    public static bool RemoveComponent(EntityId entity, ComponentPoolKind kind)
    {
        if (!s_initialized || s_table.RemoveComponent == null || !entity.IsValid) return false;
        return s_table.RemoveComponent(entity.Pack(), (byte)kind) != 0;
    }

    public static bool LoadScene(string filePath) =>
        CallWithUtf8String(s_table.LoadScene, filePath);

    public static bool SaveScene(string filePath) =>
        CallWithUtf8String(s_table.SaveScene, filePath);

    public static void ClearScene()
    {
        if (!s_initialized || s_table.ClearScene == null) return;
        s_table.ClearScene();
    }

    // =========================================================================
    // Production Jolt Physics Bridge (v4)
    // =========================================================================

    public static bool SyncRigidBody(EntityId entity)
    {
        if (!s_initialized || s_table.SyncRigidBody == null || !entity.IsValid) return false;
        return s_table.SyncRigidBody(entity.Pack()) != 0;
    }

    public static void MarkPhysicsTransformDirty(EntityId entity)
    {
        if (!s_initialized || s_table.MarkPhysicsTransformDirty == null || !entity.IsValid) return;
        s_table.MarkPhysicsTransformDirty(entity.Pack());
    }

    public static bool AddForce(EntityId entity, in Vec3 force, byte mode = 0)
    {
        if (!s_initialized || s_table.AddForce == null || !entity.IsValid) return false;
        Vec3 f = force;
        return s_table.AddForce(entity.Pack(), &f, mode) != 0;
    }

    public static bool AddForceAtPosition(EntityId entity, in Vec3 force, in Vec3 position, byte mode = 0)
    {
        if (!s_initialized || s_table.AddForceAtPosition == null || !entity.IsValid) return false;
        Vec3 f = force;
        Vec3 p = position;
        return s_table.AddForceAtPosition(entity.Pack(), &f, &p, mode) != 0;
    }

    public static bool AddTorque(EntityId entity, in Vec3 torque, byte mode = 0)
    {
        if (!s_initialized || s_table.AddTorque == null || !entity.IsValid) return false;
        Vec3 t = torque;
        return s_table.AddTorque(entity.Pack(), &t, mode) != 0;
    }

    public static bool GetLinearVelocity(EntityId entity, out Vec3 velocity)
    {
        velocity = default;
        if (!s_initialized || s_table.GetLinearVelocity == null || !entity.IsValid) return false;
        Vec3 v = default;
        bool ok = s_table.GetLinearVelocity(entity.Pack(), &v) != 0;
        velocity = v;
        return ok;
    }

    public static bool SetAngularVelocity(EntityId entity, in Vec3 angularVelocity)
    {
        if (!s_initialized || s_table.SetAngularVelocity == null || !entity.IsValid) return false;
        Vec3 w = angularVelocity;
        return s_table.SetAngularVelocity(entity.Pack(), &w) != 0;
    }

    public static bool GetAngularVelocity(EntityId entity, out Vec3 angularVelocity)
    {
        angularVelocity = default;
        if (!s_initialized || s_table.GetAngularVelocity == null || !entity.IsValid) return false;
        Vec3 w = default;
        bool ok = s_table.GetAngularVelocity(entity.Pack(), &w) != 0;
        angularVelocity = w;
        return ok;
    }

    public static bool WakeUpBody(EntityId entity)
    {
        if (!s_initialized || s_table.WakeUpBody == null || !entity.IsValid) return false;
        return s_table.WakeUpBody(entity.Pack()) != 0;
    }

    public static bool SleepBody(EntityId entity)
    {
        if (!s_initialized || s_table.SleepBody == null || !entity.IsValid) return false;
        return s_table.SleepBody(entity.Pack()) != 0;
    }

    public static bool IsBodySleeping(EntityId entity)
    {
        if (!s_initialized || s_table.IsBodySleeping == null || !entity.IsValid) return false;
        return s_table.IsBodySleeping(entity.Pack()) != 0;
    }

    public static bool TeleportBody(EntityId entity, in Vec3 position, in Quat rotation)
    {
        if (!s_initialized || s_table.TeleportBody == null || !entity.IsValid) return false;
        Vec3 p = position;
        Quat r = rotation;
        return s_table.TeleportBody(entity.Pack(), &p, &r) != 0;
    }

    public static void SetGravity(in Vec3 gravity)
    {
        if (!s_initialized || s_table.SetGravity == null) return;
        Vec3 g = gravity;
        s_table.SetGravity(&g);
    }

    public static Vec3 GetGravity()
    {
        if (!s_initialized || s_table.GetGravity == null) return new Vec3(0f, -9.81f, 0f);
        Vec3 g = default;
        s_table.GetGravity(&g);
        return g;
    }

    public static void SetLayerCollision(uint layerA, uint layerB, bool shouldCollide)
    {
        if (!s_initialized || s_table.SetLayerCollision == null) return;
        s_table.SetLayerCollision(layerA, layerB, shouldCollide ? (byte)1 : (byte)0);
    }

    public static bool GetLayerCollision(uint layerA, uint layerB)
    {
        if (!s_initialized || s_table.GetLayerCollision == null) return true;
        return s_table.GetLayerCollision(layerA, layerB) != 0;
    }

    public static bool RaycastEx(in Vec3 origin, in Vec3 direction, float maxDistance, in NativeQueryFilter filter, out NativeRaycastHit hit)
    {
        hit = default;
        if (!s_initialized || s_table.RaycastEx == null) return false;
        Vec3 o = origin;
        Vec3 d = direction;
        NativeQueryFilter f = filter;
        NativeRaycastHit h = default;
        bool ok = s_table.RaycastEx(&o, &d, maxDistance, &f, &h) != 0;
        hit = h;
        return ok;
    }

    public static uint RaycastAllEx(in Vec3 origin, in Vec3 direction, float maxDistance, in NativeQueryFilter filter, Span<NativeRaycastHit> outHits)
    {
        if (!s_initialized || s_table.RaycastAllEx == null || outHits.IsEmpty) return 0u;
        Vec3 o = origin;
        Vec3 d = direction;
        NativeQueryFilter f = filter;
        fixed (NativeRaycastHit* pHits = outHits)
        {
            return s_table.RaycastAllEx(&o, &d, maxDistance, &f, pHits, (uint)outHits.Length);
        }
    }

    public static bool SphereCastEx(in Vec3 origin, float radius, in Vec3 direction, float maxDistance, in NativeQueryFilter filter, out NativeRaycastHit hit)
    {
        hit = default;
        if (!s_initialized || s_table.SphereCastEx == null) return false;
        Vec3 o = origin;
        Vec3 d = direction;
        NativeQueryFilter f = filter;
        NativeRaycastHit h = default;
        bool ok = s_table.SphereCastEx(&o, radius, &d, maxDistance, &f, &h) != 0;
        hit = h;
        return ok;
    }

    public static bool BoxCastEx(in Vec3 origin, in Vec3 halfExtents, in Quat orientation, in Vec3 direction, float maxDistance, in NativeQueryFilter filter, out NativeRaycastHit hit)
    {
        hit = default;
        if (!s_initialized || s_table.BoxCastEx == null) return false;
        Vec3 o = origin;
        Vec3 he = halfExtents;
        Quat q = orientation;
        Vec3 d = direction;
        NativeQueryFilter f = filter;
        NativeRaycastHit h = default;
        bool ok = s_table.BoxCastEx(&o, &he, &q, &d, maxDistance, &f, &h) != 0;
        hit = h;
        return ok;
    }

    public static bool CapsuleCastEx(in Vec3 origin, float radius, float halfHeight, in Quat orientation, in Vec3 direction, float maxDistance, in NativeQueryFilter filter, out NativeRaycastHit hit)
    {
        hit = default;
        if (!s_initialized || s_table.CapsuleCastEx == null) return false;
        Vec3 o = origin;
        Quat q = orientation;
        Vec3 d = direction;
        NativeQueryFilter f = filter;
        NativeRaycastHit h = default;
        bool ok = s_table.CapsuleCastEx(&o, radius, halfHeight, &q, &d, maxDistance, &f, &h) != 0;
        hit = h;
        return ok;
    }

    public static uint OverlapSphereEx(in Vec3 center, float radius, in NativeQueryFilter filter, Span<NativeOverlapHit> outHits)
    {
        if (!s_initialized || s_table.OverlapSphereEx == null || outHits.IsEmpty) return 0u;
        Vec3 c = center;
        NativeQueryFilter f = filter;
        fixed (NativeOverlapHit* pHits = outHits)
        {
            return s_table.OverlapSphereEx(&c, radius, &f, pHits, (uint)outHits.Length);
        }
    }

    public static uint OverlapBoxEx(in Vec3 center, in Vec3 halfExtents, in Quat orientation, in NativeQueryFilter filter, Span<NativeOverlapHit> outHits)
    {
        if (!s_initialized || s_table.OverlapBoxEx == null || outHits.IsEmpty) return 0u;
        Vec3 c = center;
        Vec3 he = halfExtents;
        Quat q = orientation;
        NativeQueryFilter f = filter;
        fixed (NativeOverlapHit* pHits = outHits)
        {
            return s_table.OverlapBoxEx(&c, &he, &q, &f, pHits, (uint)outHits.Length);
        }
    }

    public static uint OverlapCapsuleEx(in Vec3 center, float radius, float halfHeight, in Quat orientation, in NativeQueryFilter filter, Span<NativeOverlapHit> outHits)
    {
        if (!s_initialized || s_table.OverlapCapsuleEx == null || outHits.IsEmpty) return 0u;
        Vec3 c = center;
        Quat q = orientation;
        NativeQueryFilter f = filter;
        fixed (NativeOverlapHit* pHits = outHits)
        {
            return s_table.OverlapCapsuleEx(&c, radius, halfHeight, &q, &f, pHits, (uint)outHits.Length);
        }
    }

    // =========================================================================
    // Character Physics Bridge (Phase 5)
    // =========================================================================

    public static bool SyncCharacterController(EntityId entity)
    {
        if (!s_initialized || s_table.SyncCharacterController == null || !entity.IsValid) return false;
        return s_table.SyncCharacterController(entity.Pack()) != 0;
    }

    public static bool MoveCharacter(EntityId entity, in Vec3 displacement)
    {
        if (!s_initialized || s_table.MoveCharacter == null || !entity.IsValid) return false;
        Vec3 d = displacement;
        return s_table.MoveCharacter(entity.Pack(), &d) != 0;
    }

    public static bool JumpCharacter(EntityId entity, float jumpSpeed)
    {
        if (!s_initialized || s_table.JumpCharacter == null || !entity.IsValid) return false;
        return s_table.JumpCharacter(entity.Pack(), jumpSpeed) != 0;
    }

    public static bool SetCharacterVelocity(EntityId entity, in Vec3 velocity)
    {
        if (!s_initialized || s_table.SetCharacterVelocity == null || !entity.IsValid) return false;
        Vec3 v = velocity;
        return s_table.SetCharacterVelocity(entity.Pack(), &v) != 0;
    }

    public static bool GetCharacterVelocity(EntityId entity, out Vec3 velocity)
    {
        velocity = default;
        if (!s_initialized || s_table.GetCharacterVelocity == null || !entity.IsValid) return false;
        Vec3 v = default;
        bool ok = s_table.GetCharacterVelocity(entity.Pack(), &v) != 0;
        velocity = v;
        return ok;
    }

    public static bool GetCharacterState(EntityId entity, out NativeCharacterState state)
    {
        state = default;
        if (!s_initialized || s_table.GetCharacterState == null || !entity.IsValid) return false;
        NativeCharacterState st = default;
        bool ok = s_table.GetCharacterState(entity.Pack(), &st) != 0;
        state = st;
        return ok;
    }

    public static bool TeleportCharacter(EntityId entity, in Vec3 position)
    {
        if (!s_initialized || s_table.TeleportCharacter == null || !entity.IsValid) return false;
        Vec3 p = position;
        return s_table.TeleportCharacter(entity.Pack(), &p) != 0;
    }

    // =========================================================================
    // Joints & Compound Collider Bridge (ApiVersion 5)
    // =========================================================================

    public static ulong CreateJoint(in NativeJointDesc desc)
    {
        if (!s_initialized || s_table.CreateJoint == null) return 0ul;
        NativeJointDesc d = desc;
        return s_table.CreateJoint(&d);
    }

    public static bool UpdateJoint(ulong packedHandle, in NativeJointDesc desc)
    {
        if (!s_initialized || s_table.UpdateJoint == null || packedHandle == 0ul) return false;
        NativeJointDesc d = desc;
        return s_table.UpdateJoint(packedHandle, &d) != 0;
    }

    public static bool DestroyJoint(ulong packedHandle)
    {
        if (!s_initialized || s_table.DestroyJoint == null || packedHandle == 0ul) return false;
        return s_table.DestroyJoint(packedHandle) != 0;
    }

    public static bool IsJointValid(ulong packedHandle)
    {
        if (!s_initialized || s_table.IsJointValid == null || packedHandle == 0ul) return false;
        return s_table.IsJointValid(packedHandle) != 0;
    }

    public static bool GetJointDesc(ulong packedHandle, out NativeJointDesc desc)
    {
        desc = default;
        if (!s_initialized || s_table.GetJointDesc == null || packedHandle == 0ul) return false;
        NativeJointDesc d = default;
        bool ok = s_table.GetJointDesc(packedHandle, &d) != 0;
        desc = d;
        return ok;
    }

    public static bool GetJointState(ulong packedHandle, out float positionOrAngle, out float appliedForce, out float appliedTorque)
    {
        positionOrAngle = 0f;
        appliedForce = 0f;
        appliedTorque = 0f;
        if (!s_initialized || s_table.GetJointState == null || packedHandle == 0ul) return false;
        float pos = 0f, frc = 0f, trq = 0f;
        bool ok = s_table.GetJointState(packedHandle, &pos, &frc, &trq) != 0;
        positionOrAngle = pos;
        appliedForce = frc;
        appliedTorque = trq;
        return ok;
    }

    public static uint GetEntityJointCount(EntityId entity)
    {
        if (!s_initialized || s_table.GetEntityJointCount == null || !entity.IsValid) return 0u;
        return s_table.GetEntityJointCount(entity.Pack());
    }

    public static ulong GetEntityJointHandle(EntityId entity, uint index)
    {
        if (!s_initialized || s_table.GetEntityJointHandle == null || !entity.IsValid) return 0ul;
        return s_table.GetEntityJointHandle(entity.Pack(), index);
    }

    public static bool SetEntityCompoundColliders(EntityId entity, ReadOnlySpan<NativeColliderShapeDesc> shapes)
    {
        if (!s_initialized || s_table.SetEntityCompoundColliders == null || !entity.IsValid) return false;
        if (shapes.IsEmpty)
        {
            return s_table.SetEntityCompoundColliders(entity.Pack(), null, 0u) != 0;
        }
        fixed (NativeColliderShapeDesc* ptr = shapes)
        {
            return s_table.SetEntityCompoundColliders(entity.Pack(), ptr, (uint)shapes.Length) != 0;
        }
    }

    public static uint GetEntityCompoundColliderCount(EntityId entity)
    {
        if (!s_initialized || s_table.GetEntityCompoundColliderCount == null || !entity.IsValid) return 0u;
        return s_table.GetEntityCompoundColliderCount(entity.Pack());
    }

    public static bool GetEntityCompoundCollider(EntityId entity, uint index, out NativeColliderShapeDesc shape)
    {
        shape = default;
        if (!s_initialized || s_table.GetEntityCompoundCollider == null || !entity.IsValid) return false;
        NativeColliderShapeDesc s = default;
        bool ok = s_table.GetEntityCompoundCollider(entity.Pack(), index, &s) != 0;
        shape = s;
        return ok;
    }

    public static bool ClearEntityCompoundColliders(EntityId entity)
    {
        if (!s_initialized || s_table.ClearEntityCompoundColliders == null || !entity.IsValid) return false;
        return s_table.ClearEntityCompoundColliders(entity.Pack()) != 0;
    }

    public static uint GetActiveWorldId()
    {
        if (!s_initialized || s_table.GetActiveWorldId == null) return 1u;
        return s_table.GetActiveWorldId();
    }

    public static bool NetGetObjectState(
        uint worldId,
        ulong netIdPacked,
        out EntityId sceneEntity,
        out ulong ownerPlayerId,
        out bool isServer,
        out bool isClient,
        out bool isOwner)
    {
        sceneEntity = EntityId.Invalid;
        ownerPlayerId = 0ul;
        isServer = false;
        isClient = false;
        isOwner = false;
        if (!s_initialized || s_table.NetGetObjectState == null || worldId == 0u || netIdPacked == 0ul)
        {
            return false;
        }
        ulong entPacked = 0ul;
        ulong owner = 0ul;
        byte srv = 0, cli = 0, own = 0;
        if (s_table.NetGetObjectState(worldId, netIdPacked, &entPacked, &owner, &srv, &cli, &own) == 0)
        {
            return false;
        }
        sceneEntity = EntityId.Unpack(entPacked);
        ownerPlayerId = owner;
        isServer = srv != 0;
        isClient = cli != 0;
        isOwner = own != 0;
        return true;
    }

    public static bool NetResolveEntityNetworkId(uint worldId, EntityId sceneEntity, out ulong netIdPacked)
    {
        netIdPacked = 0ul;
        if (!s_initialized || s_table.NetResolveEntityNetworkId == null || worldId == 0u || !sceneEntity.IsValid)
        {
            return false;
        }
        ulong packed = 0ul;
        if (s_table.NetResolveEntityNetworkId(worldId, sceneEntity.Pack(), &packed) == 0)
        {
            return false;
        }
        netIdPacked = packed;
        return packed != 0ul;
    }

    public static bool NetSendManagedRpc(
        uint worldId,
        ulong netIdPacked,
        uint behaviourHash,
        uint methodHash,
        bool isServerRpc,
        byte flags,
        ReadOnlySpan<byte> argsPayload)
    {
        if (!s_initialized || s_table.NetSendManagedRpc == null || worldId == 0u || netIdPacked == 0ul)
        {
            return false;
        }
        fixed (byte* ptr = argsPayload)
        {
            return s_table.NetSendManagedRpc(
                worldId,
                netIdPacked,
                behaviourHash,
                methodHash,
                isServerRpc ? (byte)1 : (byte)0,
                flags,
                ptr,
                (uint)argsPayload.Length) != 0;
        }
    }

    public static bool NetSyncManagedProperties(
        uint worldId,
        ulong netIdPacked,
        uint behaviourHash,
        ReadOnlySpan<byte> propsPayload)
    {
        if (!s_initialized || s_table.NetSyncManagedProperties == null || worldId == 0u || netIdPacked == 0ul || propsPayload.IsEmpty)
        {
            return false;
        }
        fixed (byte* ptr = propsPayload)
        {
            return s_table.NetSyncManagedProperties(
                worldId,
                netIdPacked,
                behaviourHash,
                ptr,
                (uint)propsPayload.Length) != 0;
        }
    }
}
