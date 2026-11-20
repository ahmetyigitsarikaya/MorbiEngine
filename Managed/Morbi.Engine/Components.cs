using System;
using System.Runtime.CompilerServices;

namespace Morbi.Engine;

public enum LightType : uint
{
    Directional = 0,
    Point = 1,
    Spot = 2
}

public enum CameraProjectionMode : uint
{
    Perspective = 0,
    Orthographic = 1
}

public enum MotionType : byte
{
    Static = 0,
    Kinematic = 1,
    Dynamic = 2
}

public enum ColliderShape : byte
{
    Box = 0,
    Sphere = 1,
    Capsule = 2
}

public enum ForceMode : byte
{
    Force = 0,
    Impulse = 1,
    VelocityChange = 2,
    Acceleration = 3
}

[Flags]
public enum AxisLocks : byte
{
    None = 0,
    PositionX = 1 << 0,
    PositionY = 1 << 1,
    PositionZ = 1 << 2,
    RotationX = 1 << 3,
    RotationY = 1 << 4,
    RotationZ = 1 << 5,
    AllPosition = PositionX | PositionY | PositionZ,
    AllRotation = RotationX | RotationY | RotationZ
}

public static class ComponentLookup
{
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int FindDenseIndex(EntityId entityId, ReadOnlySpan<EntityId> entities)
    {
        for (int i = 0; i < entities.Length; i++)
        {
            if (entities[i].Index == entityId.Index && entities[i].Generation == entityId.Generation)
            {
                return i;
            }
        }
        return -1;
    }
}

public sealed class Camera
{
    private readonly EntityId _entityId;

    internal Camera(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;

    public float FovDegrees
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].FovDegrees : 60.0f;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].FovDegrees = value;
        }
    }

    public float NearPlane
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].NearPlane : 0.1f;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].NearPlane = value;
        }
    }

    public float FarPlane
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].FarPlane : 500.0f;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].FarPlane = value;
        }
    }

    public float AspectRatio
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].AspectRatio : 16.0f / 9.0f;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].AspectRatio = value;
        }
    }

    public bool IsActive
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].IsActive != 0;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].IsActive = value ? 1u : 0u;
        }
    }

    public uint Priority
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Priority : 0u;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Priority = value;
        }
    }

    public CameraProjectionMode ProjectionMode
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? (CameraProjectionMode)span[idx].ProjectionMode : CameraProjectionMode.Perspective;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].ProjectionMode = (uint)value;
        }
    }

    public float OrthoSize
    {
        get
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].OrthoSize : 10.0f;
        }
        set
        {
            var span = NativeBridge.GetCameraSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].OrthoSize = value;
        }
    }
}

public sealed class Light
{
    private readonly EntityId _entityId;

    internal Light(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;

    public Vec3 Color
    {
        get
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Color : new Vec3(1f, 1f, 1f);
        }
        set
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Color = value;
        }
    }

    public float Intensity
    {
        get
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Intensity : 1.0f;
        }
        set
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Intensity = value;
        }
    }

    public float Range
    {
        get
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Range : 25.0f;
        }
        set
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Range = value;
        }
    }

    public LightType Type
    {
        get
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? (LightType)span[idx].Type : LightType.Point;
        }
        set
        {
            var span = NativeBridge.GetLightSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Type = (uint)value;
        }
    }
}

public sealed class MeshRenderer
{
    private readonly EntityId _entityId;

    internal MeshRenderer(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;

    public ulong MeshAssetId
    {
        get
        {
            var span = NativeBridge.GetMeshRendererSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].MeshAssetId : 0ul;
        }
        set
        {
            var span = NativeBridge.GetMeshRendererSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].MeshAssetId = value;
        }
    }

    public ulong MaterialAssetId
    {
        get
        {
            var span = NativeBridge.GetMeshRendererSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].MaterialAssetId : 0ul;
        }
        set
        {
            var span = NativeBridge.GetMeshRendererSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].MaterialAssetId = value;
        }
    }

    public bool Visible
    {
        get => GetFlagBit(0);
        set => SetFlagBit(0, value);
    }

    public bool CastShadows
    {
        get => GetFlagBit(1);
        set => SetFlagBit(1, value);
    }

    public bool ReceiveShadows
    {
        get => GetFlagBit(2);
        set => SetFlagBit(2, value);
    }

    private bool GetFlagBit(int bit)
    {
        var span = NativeBridge.GetMeshRendererSpan(out var entities);
        int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
        return (idx >= 0 && idx < span.Length) && (span[idx].Flags & (1u << bit)) != 0u;
    }

    private void SetFlagBit(int bit, bool enabled)
    {
        var span = NativeBridge.GetMeshRendererSpan(out var entities);
        int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
        if (idx >= 0 && idx < span.Length)
        {
            uint mask = 1u << bit;
            span[idx].Flags = enabled ? (span[idx].Flags | mask) : (span[idx].Flags & ~mask);
        }
    }
}

public sealed class RigidBody
{
    private readonly EntityId _entityId;

    internal RigidBody(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;

    public bool IsValid => _entityId.IsValid && NativeBridge.HasComponent(_entityId, ComponentPoolKind.RigidBody);

    public Vec3 LinearVelocity
    {
        get
        {
            if (NativeBridge.GetLinearVelocity(_entityId, out Vec3 liveVel))
            {
                return liveVel;
            }
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].LinearVelocity : default;
        }
        set => NativeBridge.SetLinearVelocity(_entityId, value);
    }

    public Vec3 AngularVelocity
    {
        get
        {
            if (NativeBridge.GetAngularVelocity(_entityId, out Vec3 liveAngVel))
            {
                return liveAngVel;
            }
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].AngularVelocity : default;
        }
        set => NativeBridge.SetAngularVelocity(_entityId, value);
    }

    public float Mass
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Mass : 1.0f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Mass = MathF.Max(0.001f, value);
                span[idx].UseDensity = 0;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public float Density
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Density : 1000.0f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Density = MathF.Max(0.01f, value);
                span[idx].UseDensity = 1;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public bool UseDensity
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].UseDensity != 0;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].UseDensity = value ? (byte)1 : (byte)0;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public Vec3 HalfExtents
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].HalfExtents : new Vec3(0.5f, 0.5f, 0.5f);
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].HalfExtents = value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Shape;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public Vec3 ColliderOffset
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].ColliderOffset : Vec3.Zero;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].ColliderOffset = value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Shape;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public Quat ColliderRotation
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].ColliderRotation : Quat.Identity;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].ColliderRotation = value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Shape;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public float Restitution
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Restitution : 0.2f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Restitution = Math.Clamp(value, 0.0f, 1.0f);
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public float Friction
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Friction : 0.6f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Friction = MathF.Max(0.0f, value);
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public float LinearDamping
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].LinearDamping : 0.05f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].LinearDamping = MathF.Max(0.0f, value);
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public float AngularDamping
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].AngularDamping : 0.05f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].AngularDamping = MathF.Max(0.0f, value);
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public float GravityFactor
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].GravityFactor : 1.0f;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].GravityFactor = value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public uint CollisionLayer
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].CollisionLayer : 0u;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].CollisionLayer = value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public uint CollisionMask
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].CollisionMask : 0xFFFFFFFFu;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].CollisionMask = value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public MotionType MotionType
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? (MotionType)span[idx].MotionType : MotionType.Dynamic;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].MotionType = (byte)value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.MotionType;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public ColliderShape Shape
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? (ColliderShape)span[idx].Shape : ColliderShape.Box;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Shape = (byte)value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Shape;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public bool UseGravity
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].UseGravity != 0;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].UseGravity = value ? (byte)1 : (byte)0;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public bool UseCCD
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].UseCCD != 0;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].UseCCD = value ? (byte)1 : (byte)0;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public bool IsTrigger
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].IsTrigger != 0;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].IsTrigger = value ? (byte)1 : (byte)0;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public AxisLocks LockedAxes
    {
        get
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? (AxisLocks)span[idx].LockedAxes : AxisLocks.None;
        }
        set
        {
            var span = NativeBridge.GetRigidBodySpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].LockedAxes = (byte)value;
                span[idx].DirtyFlags |= (byte)RigidBodyDirtyFlags.Properties;
                NativeBridge.SyncRigidBody(_entityId);
            }
        }
    }

    public bool IsSleeping => NativeBridge.IsBodySleeping(_entityId);

    public bool IsAwake => !IsSleeping;

    public bool AddForce(in Vec3 force, ForceMode mode = ForceMode.Force) =>
        NativeBridge.AddForce(_entityId, force, (byte)mode);

    public bool AddForceAtPosition(in Vec3 force, in Vec3 position, ForceMode mode = ForceMode.Force) =>
        NativeBridge.AddForceAtPosition(_entityId, force, position, (byte)mode);

    public bool AddImpulse(in Vec3 impulse) =>
        NativeBridge.AddLinearImpulse(_entityId, impulse);

    public bool AddTorque(in Vec3 torque, ForceMode mode = ForceMode.Force) =>
        NativeBridge.AddTorque(_entityId, torque, (byte)mode);

    public bool AddTorqueImpulse(in Vec3 impulse) =>
        NativeBridge.AddTorque(_entityId, impulse, (byte)ForceMode.Impulse);

    public bool SetLinearVelocity(in Vec3 velocity) =>
        NativeBridge.SetLinearVelocity(_entityId, velocity);

    public Vec3 GetLinearVelocity() => LinearVelocity;

    public bool SetAngularVelocity(in Vec3 angularVelocity) =>
        NativeBridge.SetAngularVelocity(_entityId, angularVelocity);

    public Vec3 GetAngularVelocity() => AngularVelocity;

    public bool WakeUp() => NativeBridge.WakeUpBody(_entityId);

    public bool Sleep() => NativeBridge.SleepBody(_entityId);

    public bool Teleport(in Vec3 position, in Quat rotation) =>
        NativeBridge.TeleportBody(_entityId, position, rotation);
}

public class Collider
{
    protected readonly EntityId _entityId;
    private readonly RigidBody _body;
    private Entity? _entity;

    internal Collider(EntityId entityId)
    {
        _entityId = entityId;
        _body = new RigidBody(entityId);
    }

    public EntityId EntityId => _entityId;
    public bool IsValid => _entityId.IsValid && NativeBridge.HasComponent(_entityId, ComponentPoolKind.RigidBody);
    public Entity Entity => _entity ??= new Entity(_entityId, NativeBridge.GetEntityName(_entityId));
    public RigidBody AttachedRigidBody => _body;

    public ColliderShape Shape
    {
        get => _body.Shape;
        set => _body.Shape = value;
    }

    public bool IsTrigger
    {
        get => _body.IsTrigger;
        set => _body.IsTrigger = value;
    }

    public Vec3 Offset
    {
        get => _body.ColliderOffset;
        set => _body.ColliderOffset = value;
    }

    public Quat Rotation
    {
        get => _body.ColliderRotation;
        set => _body.ColliderRotation = value;
    }

    public float Friction
    {
        get => _body.Friction;
        set => _body.Friction = value;
    }

    public float Restitution
    {
        get => _body.Restitution;
        set => _body.Restitution = value;
    }

    public uint CollisionLayer
    {
        get => _body.CollisionLayer;
        set => _body.CollisionLayer = value;
    }

    public uint CollisionMask
    {
        get => _body.CollisionMask;
        set => _body.CollisionMask = value;
    }
}

public sealed class BoxCollider : Collider
{
    internal BoxCollider(EntityId entityId) : base(entityId) { }

    public Vec3 HalfExtents
    {
        get => AttachedRigidBody.HalfExtents;
        set
        {
            AttachedRigidBody.Shape = ColliderShape.Box;
            AttachedRigidBody.HalfExtents = value;
        }
    }

    public Vec3 Size
    {
        get => AttachedRigidBody.HalfExtents * 2.0f;
        set
        {
            AttachedRigidBody.Shape = ColliderShape.Box;
            AttachedRigidBody.HalfExtents = value * 0.5f;
        }
    }
}

public sealed class SphereCollider : Collider
{
    internal SphereCollider(EntityId entityId) : base(entityId) { }

    public float Radius
    {
        get => AttachedRigidBody.HalfExtents.X;
        set
        {
            float r = MathF.Max(0.005f, value);
            AttachedRigidBody.Shape = ColliderShape.Sphere;
            AttachedRigidBody.HalfExtents = new Vec3(r, r, r);
        }
    }
}

public sealed class CapsuleCollider : Collider
{
    internal CapsuleCollider(EntityId entityId) : base(entityId) { }

    public float Radius
    {
        get => AttachedRigidBody.HalfExtents.X;
        set
        {
            float r = MathF.Max(0.005f, value);
            Vec3 cur = AttachedRigidBody.HalfExtents;
            AttachedRigidBody.Shape = ColliderShape.Capsule;
            AttachedRigidBody.HalfExtents = new Vec3(r, MathF.Max(0.005f, cur.Y), r);
        }
    }

    public float HalfHeight
    {
        get => AttachedRigidBody.HalfExtents.Y;
        set
        {
            float hh = MathF.Max(0.005f, value);
            Vec3 cur = AttachedRigidBody.HalfExtents;
            float r = MathF.Max(0.005f, cur.X);
            AttachedRigidBody.Shape = ColliderShape.Capsule;
            AttachedRigidBody.HalfExtents = new Vec3(r, hh, r);
        }
    }

    public float Height
    {
        get => (HalfHeight + Radius) * 2.0f;
        set
        {
            float r = Radius;
            HalfHeight = MathF.Max(0.005f, value * 0.5f - r);
        }
    }
}

public struct CompoundColliderShape
{
    public ColliderShape Shape { get; set; }
    public Vec3 HalfExtents { get; set; }
    public Vec3 LocalOffset { get; set; }
    public Quat LocalRotation { get; set; }
    public uint CollisionLayer { get; set; }
    public uint CollisionMask { get; set; }
    public bool IsTrigger { get; set; }

    public CompoundColliderShape()
    {
        Shape = ColliderShape.Box;
        HalfExtents = new Vec3(0.5f, 0.5f, 0.5f);
        LocalOffset = Vec3.Zero;
        LocalRotation = Quat.Identity;
        CollisionLayer = 1u;
        CollisionMask = CollisionLayers.AllMask;
        IsTrigger = false;
    }

    public static CompoundColliderShape Box(
        in Vec3 halfExtents,
        in Vec3 localOffset,
        in Quat? localRotation = null,
        uint collisionLayer = 1u,
        uint collisionMask = CollisionLayers.AllMask,
        bool isTrigger = false) => new()
        {
            Shape = ColliderShape.Box,
            HalfExtents = halfExtents,
            LocalOffset = localOffset,
            LocalRotation = localRotation ?? Quat.Identity,
            CollisionLayer = collisionLayer,
            CollisionMask = collisionMask,
            IsTrigger = isTrigger
        };

    public static CompoundColliderShape Sphere(
        float radius,
        in Vec3 localOffset,
        uint collisionLayer = 1u,
        uint collisionMask = CollisionLayers.AllMask,
        bool isTrigger = false) => new()
        {
            Shape = ColliderShape.Sphere,
            HalfExtents = new Vec3(MathF.Max(0.005f, radius), MathF.Max(0.005f, radius), MathF.Max(0.005f, radius)),
            LocalOffset = localOffset,
            LocalRotation = Quat.Identity,
            CollisionLayer = collisionLayer,
            CollisionMask = collisionMask,
            IsTrigger = isTrigger
        };

    public static CompoundColliderShape Capsule(
        float radius,
        float halfHeight,
        in Vec3 localOffset,
        in Quat? localRotation = null,
        uint collisionLayer = 1u,
        uint collisionMask = CollisionLayers.AllMask,
        bool isTrigger = false) => new()
        {
            Shape = ColliderShape.Capsule,
            HalfExtents = new Vec3(MathF.Max(0.005f, radius), MathF.Max(0.005f, halfHeight), MathF.Max(0.005f, radius)),
            LocalOffset = localOffset,
            LocalRotation = localRotation ?? Quat.Identity,
            CollisionLayer = collisionLayer,
            CollisionMask = collisionMask,
            IsTrigger = isTrigger
        };

    internal readonly NativeColliderShapeDesc ToNative() => new()
    {
        HalfExtents = HalfExtents,
        CollisionLayer = CollisionLayer,
        LocalOffset = LocalOffset,
        CollisionMask = CollisionMask,
        LocalRotation = LocalRotation,
        Shape = (byte)Shape,
        IsTrigger = IsTrigger ? (byte)1 : (byte)0
    };

    internal static CompoundColliderShape FromNative(in NativeColliderShapeDesc n) => new()
    {
        Shape = (ColliderShape)n.Shape,
        HalfExtents = n.HalfExtents,
        LocalOffset = n.LocalOffset,
        LocalRotation = n.LocalRotation,
        CollisionLayer = n.CollisionLayer,
        CollisionMask = n.CollisionMask,
        IsTrigger = n.IsTrigger != 0
    };
}

public sealed class CompoundCollider
{
    private readonly EntityId _entityId;

    internal CompoundCollider(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;
    public bool IsValid => _entityId.IsValid && NativeBridge.HasComponent(_entityId, ComponentPoolKind.RigidBody);
    public int Count => IsValid ? (int)NativeBridge.GetEntityCompoundColliderCount(_entityId) : 0;

    public CompoundColliderShape[] GetColliders()
    {
        int n = Count;
        if (n <= 0) return Array.Empty<CompoundColliderShape>();
        var list = new CompoundColliderShape[n];
        for (int i = 0; i < n; i++)
        {
            if (NativeBridge.GetEntityCompoundCollider(_entityId, (uint)i, out NativeColliderShapeDesc d))
            {
                list[i] = CompoundColliderShape.FromNative(d);
            }
        }
        return list;
    }

    public bool TryGetCollider(int index, out CompoundColliderShape shape)
    {
        shape = default;
        if (index < 0 || !IsValid) return false;
        if (NativeBridge.GetEntityCompoundCollider(_entityId, (uint)index, out NativeColliderShapeDesc d))
        {
            shape = CompoundColliderShape.FromNative(d);
            return true;
        }
        return false;
    }

    public bool SetColliders(ReadOnlySpan<CompoundColliderShape> shapes)
    {
        if (!_entityId.IsValid) return false;
        if (!NativeBridge.HasComponent(_entityId, ComponentPoolKind.RigidBody))
        {
            if (!NativeBridge.AddComponent(_entityId, ComponentPoolKind.RigidBody)) return false;
        }
        if (shapes.IsEmpty)
        {
            return NativeBridge.ClearEntityCompoundColliders(_entityId);
        }
        Span<NativeColliderShapeDesc> native = shapes.Length <= 32
            ? stackalloc NativeColliderShapeDesc[shapes.Length]
            : new NativeColliderShapeDesc[shapes.Length];
        for (int i = 0; i < shapes.Length; i++)
        {
            native[i] = shapes[i].ToNative();
        }
        return NativeBridge.SetEntityCompoundColliders(_entityId, native);
    }

    public bool AddCollider(in CompoundColliderShape shape)
    {
        if (!_entityId.IsValid) return false;
        CompoundColliderShape[] existing = GetColliders();
        var next = new CompoundColliderShape[existing.Length + 1];
        Array.Copy(existing, next, existing.Length);
        next[existing.Length] = shape;
        return SetColliders(next);
    }

    public bool AddBox(
        in Vec3 halfExtents,
        in Vec3 localOffset,
        in Quat? localRotation = null,
        uint collisionLayer = 1u,
        uint collisionMask = CollisionLayers.AllMask,
        bool isTrigger = false) =>
        AddCollider(CompoundColliderShape.Box(halfExtents, localOffset, localRotation, collisionLayer, collisionMask, isTrigger));

    public bool AddSphere(
        float radius,
        in Vec3 localOffset,
        uint collisionLayer = 1u,
        uint collisionMask = CollisionLayers.AllMask,
        bool isTrigger = false) =>
        AddCollider(CompoundColliderShape.Sphere(radius, localOffset, collisionLayer, collisionMask, isTrigger));

    public bool AddCapsule(
        float radius,
        float halfHeight,
        in Vec3 localOffset,
        in Quat? localRotation = null,
        uint collisionLayer = 1u,
        uint collisionMask = CollisionLayers.AllMask,
        bool isTrigger = false) =>
        AddCollider(CompoundColliderShape.Capsule(radius, halfHeight, localOffset, localRotation, collisionLayer, collisionMask, isTrigger));

    public bool UpdateCollider(int index, in CompoundColliderShape shape)
    {
        if (!IsValid || index < 0) return false;
        CompoundColliderShape[] existing = GetColliders();
        if (index >= existing.Length) return false;
        existing[index] = shape;
        return SetColliders(existing);
    }

    public bool RemoveCollider(int index)
    {
        if (!IsValid || index < 0) return false;
        CompoundColliderShape[] existing = GetColliders();
        if (index >= existing.Length) return false;
        if (existing.Length == 1)
        {
            return Clear();
        }
        var next = new CompoundColliderShape[existing.Length - 1];
        if (index > 0) Array.Copy(existing, 0, next, 0, index);
        if (index + 1 < existing.Length) Array.Copy(existing, index + 1, next, index, existing.Length - index - 1);
        return SetColliders(next);
    }

    public bool Clear() => _entityId.IsValid && NativeBridge.ClearEntityCompoundColliders(_entityId);

    public bool Rebuild() => IsValid && NativeBridge.SyncRigidBody(_entityId);
}

public sealed class AudioSource
{
    private readonly EntityId _entityId;

    internal AudioSource(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;

    public ulong SoundAssetId
    {
        get
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].SoundAssetId : 0ul;
        }
        set
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].SoundAssetId = value;
        }
    }

    public float Volume
    {
        get
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Volume : 1.0f;
        }
        set
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Volume = value;
        }
    }

    public float Pitch
    {
        get
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Pitch : 1.0f;
        }
        set
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Pitch = value;
        }
    }

    public bool Spatial3D
    {
        get
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].Spatial3D != 0;
        }
        set
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Spatial3D = value ? (byte)1 : (byte)0;
        }
    }

    public bool Looping
    {
        get
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) && span[idx].Looping != 0;
        }
        set
        {
            var span = NativeBridge.GetAudioSourceSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length) span[idx].Looping = value ? (byte)1 : (byte)0;
        }
    }

    public bool PlaySpatial3D(string soundKey, in Vec3 worldPosition) =>
        NativeBridge.PlaySpatialSound3D(soundKey, worldPosition, Volume, Pitch);
}

public enum CharacterGroundState : byte
{
    OnGround = 0,
    OnSteepGround = 1,
    NotSupported = 2,
    InAir = 3
}

public sealed class CharacterController
{
    private readonly EntityId _entityId;

    internal CharacterController(EntityId entityId) => _entityId = entityId;

    public EntityId EntityId => _entityId;
    public bool IsValid => _entityId.IsValid && NativeBridge.HasComponent(_entityId, ComponentPoolKind.CharacterController);

    public Vec3 Velocity
    {
        get => NativeBridge.GetCharacterVelocity(_entityId, out Vec3 vel) ? vel : Vec3.Zero;
        set => NativeBridge.SetCharacterVelocity(_entityId, value);
    }

    public Vec3 DesiredVelocity
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].DesiredHorizontalVel : Vec3.Zero;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].DesiredHorizontalVel = value;
            }
        }
    }

    public float Radius
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Radius : 0.35f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Radius = MathF.Max(0.05f, value);
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Shape;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public float Height
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Height : 1.8f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Height = MathF.Max(span[idx].Radius * 2.0f + 0.02f, value);
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Shape;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public float Mass
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].Mass : 70.0f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].Mass = MathF.Max(0.1f, value);
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Properties;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public float MaxPushStrength
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].MaxPushStrength : 250.0f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].MaxPushStrength = MathF.Max(0.0f, value);
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Properties;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public float MaxSlopeAngleDegrees
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].MaxSlopeAngleDegrees : 45.0f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].MaxSlopeAngleDegrees = Math.Clamp(value, 1.0f, 89.0f);
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Properties;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public float StepHeight
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].StepHeight : 0.35f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].StepHeight = MathF.Max(0.0f, value);
            }
        }
    }

    public float GravityFactor
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].GravityFactor : 1.0f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].GravityFactor = value;
            }
        }
    }

    public float AirControl
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].AirControl : 0.35f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].AirControl = Math.Clamp(value, 0.0f, 1.0f);
            }
        }
    }

    public float GroundFriction
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].GroundFriction : 12.0f;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].GroundFriction = MathF.Max(0.0f, value);
            }
        }
    }

    public uint CollisionLayer
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].CollisionLayer : 1u;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].CollisionLayer = value;
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Properties;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public uint CollisionMask
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].CollisionMask : 0xFFFFFFFFu;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].CollisionMask = value;
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Properties;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public Vec3 CenterOffset
    {
        get
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < span.Length) ? span[idx].CenterOffset : Vec3.Zero;
        }
        set
        {
            var span = NativeBridge.GetCharacterControllerSpan(out var entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < span.Length)
            {
                span[idx].CenterOffset = value;
                span[idx].DirtyFlags |= (byte)CharacterControllerDirtyFlags.Shape;
                NativeBridge.SyncCharacterController(_entityId);
            }
        }
    }

    public bool IsGrounded =>
        NativeBridge.GetCharacterState(_entityId, out NativeCharacterState st) && st.IsGrounded != 0;

    public bool IsSliding =>
        NativeBridge.GetCharacterState(_entityId, out NativeCharacterState st) && st.IsSliding != 0;

    public CharacterGroundState GroundState =>
        NativeBridge.GetCharacterState(_entityId, out NativeCharacterState st) ? (CharacterGroundState)st.GroundState : CharacterGroundState.InAir;

    public Vec3 GroundNormal =>
        NativeBridge.GetCharacterState(_entityId, out NativeCharacterState st) ? st.GroundNormal : Vec3.Up;

    public Vec3 GroundVelocity =>
        NativeBridge.GetCharacterState(_entityId, out NativeCharacterState st) ? st.GroundVelocity : Vec3.Zero;

    public Entity? GroundEntity
    {
        get
        {
            if (!NativeBridge.GetCharacterState(_entityId, out NativeCharacterState st)) return null;
            EntityId eid = EntityId.Unpack(st.GroundEntityPacked);
            return eid.IsValid ? new Entity(eid, NativeBridge.GetEntityName(eid)) : null;
        }
    }

    public bool Move(in Vec3 displacement) => NativeBridge.MoveCharacter(_entityId, displacement);

    public bool Jump(float jumpSpeed = 6.5f) => NativeBridge.JumpCharacter(_entityId, jumpSpeed);

    public bool SetVelocity(in Vec3 velocity) => NativeBridge.SetCharacterVelocity(_entityId, velocity);

    public Vec3 GetVelocity() => Velocity;

    public bool Teleport(in Vec3 position) => NativeBridge.TeleportCharacter(_entityId, position);

    public void SetMaxSlopeAngle(float degrees) => MaxSlopeAngleDegrees = degrees;
    public void SetStepHeight(float stepHeight) => StepHeight = stepHeight;
    public void SetGravityFactor(float factor) => GravityFactor = factor;
    public void SetGroundFriction(float friction) => GroundFriction = friction;
}
