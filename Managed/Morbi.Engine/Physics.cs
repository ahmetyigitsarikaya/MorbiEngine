#nullable enable
using System;

namespace Morbi.Engine;

public static class CollisionLayers
{
    public const int Default = 0;
    public const int Environment = 1;
    public const int DynamicProps = 2;
    public const int Player = 3;
    public const int Enemy = 4;
    public const int Trigger = 5;
    public const int Projectile = 6;
    public const uint AllMask = 0xFFFFFFFFu;

    public static uint LayerBit(int layerIndex) =>
        (layerIndex >= 0 && layerIndex < 32) ? (1u << layerIndex) : 0u;
}

public enum JointType : byte
{
    Fixed = 0,
    Distance = 1,
    Hinge = 2,
    Slider = 3
}

public struct JointDesc
{
    public JointType Type { get; set; }
    public EntityId EntityA { get; set; }
    public EntityId EntityB { get; set; }
    public Vec3 AnchorA { get; set; }
    public Vec3 AnchorB { get; set; }
    public Vec3 Axis { get; set; }
    public bool AutoDetectAnchors { get; set; }
    public float MinDistance { get; set; }
    public float MaxDistance { get; set; }
    public float SpringFrequencyHz { get; set; }
    public float SpringDampingRatio { get; set; }
    public float MinLimit { get; set; }
    public float MaxLimit { get; set; }
    public float MaxFriction { get; set; }
    public float BreakForce { get; set; }
    public float BreakTorque { get; set; }
    public bool Enabled { get; set; }

    public JointDesc()
    {
        Type = JointType.Fixed;
        EntityA = EntityId.Invalid;
        EntityB = EntityId.Invalid;
        AnchorA = Vec3.Zero;
        AnchorB = Vec3.Zero;
        Axis = Vec3.Up;
        AutoDetectAnchors = true;
        MinDistance = -1.0f;
        MaxDistance = -1.0f;
        SpringFrequencyHz = 0.0f;
        SpringDampingRatio = 0.0f;
        MinLimit = -MathF.PI;
        MaxLimit = MathF.PI;
        MaxFriction = 0.0f;
        BreakForce = 0.0f;
        BreakTorque = 0.0f;
        Enabled = true;
    }

    internal readonly NativeJointDesc ToNative() => new()
    {
        EntityAPacked = EntityA.Pack(),
        EntityBPacked = EntityB.Pack(),
        AnchorA = AnchorA,
        MinDistance = MinDistance,
        AnchorB = AnchorB,
        MaxDistance = MaxDistance,
        Axis = Axis,
        SpringFrequencyHz = SpringFrequencyHz,
        SpringDampingRatio = SpringDampingRatio,
        MinLimit = MinLimit,
        MaxLimit = MaxLimit,
        MaxFriction = MaxFriction,
        BreakForce = BreakForce,
        BreakTorque = BreakTorque,
        Type = (byte)Type,
        AutoDetectAnchors = AutoDetectAnchors ? (byte)1 : (byte)0,
        Enabled = Enabled ? (byte)1 : (byte)0
    };

    internal static JointDesc FromNative(in NativeJointDesc n) => new()
    {
        Type = (JointType)n.Type,
        EntityA = EntityId.Unpack(n.EntityAPacked),
        EntityB = EntityId.Unpack(n.EntityBPacked),
        AnchorA = n.AnchorA,
        AnchorB = n.AnchorB,
        Axis = n.Axis,
        AutoDetectAnchors = n.AutoDetectAnchors != 0,
        MinDistance = n.MinDistance,
        MaxDistance = n.MaxDistance,
        SpringFrequencyHz = n.SpringFrequencyHz,
        SpringDampingRatio = n.SpringDampingRatio,
        MinLimit = n.MinLimit,
        MaxLimit = n.MaxLimit,
        MaxFriction = n.MaxFriction,
        BreakForce = n.BreakForce,
        BreakTorque = n.BreakTorque,
        Enabled = n.Enabled != 0
    };
}

public readonly struct JointState
{
    public float PositionOrAngle { get; }
    public float AppliedForce { get; }
    public float AppliedTorque { get; }

    public JointState(float positionOrAngle, float appliedForce, float appliedTorque)
    {
        PositionOrAngle = positionOrAngle;
        AppliedForce = appliedForce;
        AppliedTorque = appliedTorque;
    }
}

public readonly struct JointHandle : IEquatable<JointHandle>
{
    public static readonly JointHandle Invalid = new(0ul);

    public ulong Packed { get; }
    public uint Index => (uint)(Packed & 0xFFFFFFFFul);
    public uint Generation => (uint)(Packed >> 32);

    public JointHandle(ulong packed) => Packed = packed;

    public bool IsValid => Generation != 0u && NativeBridge.IsJointValid(Packed);

    public bool Update(in JointDesc desc) => IsValid && NativeBridge.UpdateJoint(Packed, desc.ToNative());

    public bool TryGetDesc(out JointDesc desc)
    {
        desc = default;
        if (Generation == 0u || !NativeBridge.GetJointDesc(Packed, out NativeJointDesc native))
        {
            return false;
        }
        desc = JointDesc.FromNative(native);
        return true;
    }

    public bool TryGetState(out JointState state)
    {
        state = default;
        if (Generation == 0u || !NativeBridge.GetJointState(Packed, out float pos, out float frc, out float trq))
        {
            return false;
        }
        state = new JointState(pos, frc, trq);
        return true;
    }

    public bool SetLimits(float minLimit, float maxLimit)
    {
        if (!TryGetDesc(out JointDesc d)) return false;
        d.MinLimit = minLimit;
        d.MaxLimit = maxLimit;
        return NativeBridge.UpdateJoint(Packed, d.ToNative());
    }

    public bool SetDistanceLimits(float minDistance, float maxDistance)
    {
        if (!TryGetDesc(out JointDesc d)) return false;
        d.MinDistance = minDistance;
        d.MaxDistance = maxDistance;
        return NativeBridge.UpdateJoint(Packed, d.ToNative());
    }

    public bool SetFriction(float maxFriction)
    {
        if (!TryGetDesc(out JointDesc d)) return false;
        d.MaxFriction = maxFriction;
        return NativeBridge.UpdateJoint(Packed, d.ToNative());
    }

    public bool SetSpring(float frequencyHz, float dampingRatio)
    {
        if (!TryGetDesc(out JointDesc d)) return false;
        d.SpringFrequencyHz = frequencyHz;
        d.SpringDampingRatio = dampingRatio;
        return NativeBridge.UpdateJoint(Packed, d.ToNative());
    }

    public bool SetBreakThresholds(float breakForce, float breakTorque)
    {
        if (!TryGetDesc(out JointDesc d)) return false;
        d.BreakForce = breakForce;
        d.BreakTorque = breakTorque;
        return NativeBridge.UpdateJoint(Packed, d.ToNative());
    }

    public bool SetEnabled(bool enabled)
    {
        if (!TryGetDesc(out JointDesc d)) return false;
        d.Enabled = enabled;
        return NativeBridge.UpdateJoint(Packed, d.ToNative());
    }

    public bool Destroy() => Generation != 0u && NativeBridge.DestroyJoint(Packed);

    public bool Equals(JointHandle other) => Packed == other.Packed;
    public override bool Equals(object? obj) => obj is JointHandle other && Equals(other);
    public override int GetHashCode() => Packed.GetHashCode();
    public static bool operator ==(JointHandle left, JointHandle right) => left.Packed == right.Packed;
    public static bool operator !=(JointHandle left, JointHandle right) => left.Packed != right.Packed;
}

public sealed class QueryFilter
{
    public uint LayerMask { get; set; } = CollisionLayers.AllMask;
    public bool IncludeTriggers { get; set; }
    public Entity? IgnoreEntity { get; set; }
    public EntityId IgnoreEntityId { get; set; } = EntityId.Invalid;
    public bool IgnoreStatic { get; set; }
    public bool IgnoreKinematic { get; set; }
    public bool IgnoreDynamic { get; set; }

    public static QueryFilter Default { get; } = new();

    internal NativeQueryFilter ToNative()
    {
        EntityId ignored = IgnoreEntity != null ? IgnoreEntity.Id : IgnoreEntityId;
        return new NativeQueryFilter
        {
            IgnoreEntityPacked = ignored.Pack(),
            LayerMask = LayerMask,
            IncludeTriggers = IncludeTriggers ? (byte)1 : (byte)0,
            IgnoreStatic = IgnoreStatic ? (byte)1 : (byte)0,
            IgnoreKinematic = IgnoreKinematic ? (byte)1 : (byte)0,
            IgnoreDynamic = IgnoreDynamic ? (byte)1 : (byte)0
        };
    }
}

public readonly struct RaycastHit
{
    public EntityId EntityId { get; }
    public Vec3 Point { get; }
    public Vec3 Normal { get; }
    public float Distance { get; }
    public ColliderShape Shape { get; }

    public Entity? Entity => EntityId.IsValid ? new Entity(EntityId, NativeBridge.GetEntityName(EntityId)) : null;

    internal RaycastHit(in NativeRaycastHit native)
    {
        EntityId = EntityId.Unpack(native.HitEntityPacked);
        Point = native.Point;
        Normal = native.Normal;
        Distance = native.Distance;
        Shape = (ColliderShape)native.Shape;
    }
}

public readonly struct OverlapHit
{
    public EntityId EntityId { get; }
    public Vec3 ContactPoint { get; }
    public Vec3 PenetrationAxis { get; }
    public float PenetrationDepth { get; }
    public ColliderShape Shape { get; }

    public Entity? Entity => EntityId.IsValid ? new Entity(EntityId, NativeBridge.GetEntityName(EntityId)) : null;

    internal OverlapHit(in NativeOverlapHit native)
    {
        EntityId = EntityId.Unpack(native.EntityPacked);
        ContactPoint = native.ContactPoint;
        PenetrationAxis = native.PenetrationAxis;
        PenetrationDepth = native.PenetrationDepth;
        Shape = (ColliderShape)native.Shape;
    }
}

public readonly struct ContactPoint
{
    public Vec3 Point { get; }
    public Vec3 Normal { get; }
    public float Separation { get; }

    public ContactPoint(in Vec3 point, in Vec3 normal, float separation)
    {
        Point = point;
        Normal = normal;
        Separation = separation;
    }
}

public sealed class Collision
{
    private Entity? _entity;
    private Collider? _collider;
    private RigidBody? _rigidBody;

    public EntityId EntityId { get; }
    public Vec3 RelativeVelocity { get; }
    public ContactPoint PrimaryContact { get; }
    public ColliderShape SelfShape { get; }
    public ColliderShape OtherShape { get; }
    public bool IsTrigger { get; }

    public Entity? Entity => EntityId.IsValid ? (_entity ??= new Entity(EntityId, NativeBridge.GetEntityName(EntityId))) : null;
    public Collider? Collider => EntityId.IsValid ? (_collider ??= new Collider(EntityId)) : null;
    public RigidBody? RigidBody => EntityId.IsValid ? (_rigidBody ??= new RigidBody(EntityId)) : null;

    internal Collision(
        EntityId otherEntityId,
        in Vec3 contactPoint,
        in Vec3 contactNormal,
        in Vec3 relativeVelocity,
        float penetrationDepth,
        ColliderShape selfShape,
        ColliderShape otherShape,
        bool isTrigger)
    {
        EntityId = otherEntityId;
        PrimaryContact = new ContactPoint(contactPoint, contactNormal, -penetrationDepth);
        RelativeVelocity = relativeVelocity;
        SelfShape = selfShape;
        OtherShape = otherShape;
        IsTrigger = isTrigger;
    }
}

public static class Physics
{
    public static Vec3 Gravity
    {
        get => NativeBridge.GetGravity();
        set => NativeBridge.SetGravity(value);
    }

    public static void SetLayerCollision(int layerA, int layerB, bool shouldCollide)
    {
        if (layerA < 0 || layerA >= 32 || layerB < 0 || layerB >= 32) return;
        NativeBridge.SetLayerCollision((uint)layerA, (uint)layerB, shouldCollide);
    }

    public static bool GetLayerCollision(int layerA, int layerB)
    {
        if (layerA < 0 || layerA >= 32 || layerB < 0 || layerB >= 32) return false;
        return NativeBridge.GetLayerCollision((uint)layerA, (uint)layerB);
    }

    public static bool Raycast(
        in Vec3 origin,
        in Vec3 direction,
        out RaycastHit hit,
        float maxDistance = 1000.0f,
        QueryFilter? filter = null)
    {
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        if (NativeBridge.RaycastEx(origin, direction, maxDistance, nativeFilter, out NativeRaycastHit nativeHit))
        {
            hit = new RaycastHit(nativeHit);
            return true;
        }
        hit = default;
        return false;
    }

    public static bool Raycast(
        in Vec3 origin,
        in Vec3 direction,
        out RaycastHit hit,
        float maxDistance,
        uint layerMask,
        bool includeTriggers = false)
    {
        NativeQueryFilter nativeFilter = new()
        {
            LayerMask = layerMask,
            IncludeTriggers = includeTriggers ? (byte)1 : (byte)0
        };
        if (NativeBridge.RaycastEx(origin, direction, maxDistance, nativeFilter, out NativeRaycastHit nativeHit))
        {
            hit = new RaycastHit(nativeHit);
            return true;
        }
        hit = default;
        return false;
    }

    public static RaycastHit[] RaycastAll(
        in Vec3 origin,
        in Vec3 direction,
        float maxDistance = 1000.0f,
        QueryFilter? filter = null,
        int maxHits = 64)
    {
        int cap = Math.Clamp(maxHits, 1, 256);
        Span<NativeRaycastHit> buffer = cap <= 64 ? stackalloc NativeRaycastHit[cap] : new NativeRaycastHit[cap];
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        uint count = NativeBridge.RaycastAllEx(origin, direction, maxDistance, nativeFilter, buffer);
        if (count == 0u) return Array.Empty<RaycastHit>();

        var result = new RaycastHit[count];
        for (int i = 0; i < result.Length; i++)
        {
            result[i] = new RaycastHit(buffer[i]);
        }
        return result;
    }

    public static bool SphereCast(
        in Vec3 origin,
        float radius,
        in Vec3 direction,
        out RaycastHit hit,
        float maxDistance = 1000.0f,
        QueryFilter? filter = null)
    {
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        if (NativeBridge.SphereCastEx(origin, radius, direction, maxDistance, nativeFilter, out NativeRaycastHit nativeHit))
        {
            hit = new RaycastHit(nativeHit);
            return true;
        }
        hit = default;
        return false;
    }

    public static bool BoxCast(
        in Vec3 origin,
        in Vec3 halfExtents,
        in Quat orientation,
        in Vec3 direction,
        out RaycastHit hit,
        float maxDistance = 1000.0f,
        QueryFilter? filter = null)
    {
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        if (NativeBridge.BoxCastEx(origin, halfExtents, orientation, direction, maxDistance, nativeFilter, out NativeRaycastHit nativeHit))
        {
            hit = new RaycastHit(nativeHit);
            return true;
        }
        hit = default;
        return false;
    }

    public static bool CapsuleCast(
        in Vec3 origin,
        float radius,
        float halfHeight,
        in Quat orientation,
        in Vec3 direction,
        out RaycastHit hit,
        float maxDistance = 1000.0f,
        QueryFilter? filter = null)
    {
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        if (NativeBridge.CapsuleCastEx(origin, radius, halfHeight, orientation, direction, maxDistance, nativeFilter, out NativeRaycastHit nativeHit))
        {
            hit = new RaycastHit(nativeHit);
            return true;
        }
        hit = default;
        return false;
    }

    public static OverlapHit[] OverlapSphere(
        in Vec3 center,
        float radius,
        QueryFilter? filter = null,
        int maxHits = 64)
    {
        int cap = Math.Clamp(maxHits, 1, 256);
        Span<NativeOverlapHit> buffer = cap <= 64 ? stackalloc NativeOverlapHit[cap] : new NativeOverlapHit[cap];
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        uint count = NativeBridge.OverlapSphereEx(center, radius, nativeFilter, buffer);
        if (count == 0u) return Array.Empty<OverlapHit>();

        var result = new OverlapHit[count];
        for (int i = 0; i < result.Length; i++)
        {
            result[i] = new OverlapHit(buffer[i]);
        }
        return result;
    }

    public static OverlapHit[] OverlapBox(
        in Vec3 center,
        in Vec3 halfExtents,
        in Quat orientation,
        QueryFilter? filter = null,
        int maxHits = 64)
    {
        int cap = Math.Clamp(maxHits, 1, 256);
        Span<NativeOverlapHit> buffer = cap <= 64 ? stackalloc NativeOverlapHit[cap] : new NativeOverlapHit[cap];
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        uint count = NativeBridge.OverlapBoxEx(center, halfExtents, orientation, nativeFilter, buffer);
        if (count == 0u) return Array.Empty<OverlapHit>();

        var result = new OverlapHit[count];
        for (int i = 0; i < result.Length; i++)
        {
            result[i] = new OverlapHit(buffer[i]);
        }
        return result;
    }

    public static OverlapHit[] OverlapCapsule(
        in Vec3 center,
        float radius,
        float halfHeight,
        in Quat orientation,
        QueryFilter? filter = null,
        int maxHits = 64)
    {
        int cap = Math.Clamp(maxHits, 1, 256);
        Span<NativeOverlapHit> buffer = cap <= 64 ? stackalloc NativeOverlapHit[cap] : new NativeOverlapHit[cap];
        NativeQueryFilter nativeFilter = (filter ?? QueryFilter.Default).ToNative();
        uint count = NativeBridge.OverlapCapsuleEx(center, radius, halfHeight, orientation, nativeFilter, buffer);
        if (count == 0u) return Array.Empty<OverlapHit>();

        var result = new OverlapHit[count];
        for (int i = 0; i < result.Length; i++)
        {
            result[i] = new OverlapHit(buffer[i]);
        }
        return result;
    }

    public static JointHandle CreateJoint(in JointDesc desc)
    {
        ulong packed = NativeBridge.CreateJoint(desc.ToNative());
        return packed != 0ul ? new JointHandle(packed) : JointHandle.Invalid;
    }

    public static JointHandle CreateFixedJoint(
        Entity bodyA,
        Entity? bodyB = null,
        float breakForce = 0.0f,
        float breakTorque = 0.0f)
    {
        if (bodyA == null || !bodyA.IsValid) return JointHandle.Invalid;
        JointDesc d = new()
        {
            Type = JointType.Fixed,
            EntityA = bodyA.Id,
            EntityB = bodyB != null ? bodyB.Id : EntityId.Invalid,
            AutoDetectAnchors = true,
            BreakForce = breakForce,
            BreakTorque = breakTorque
        };
        return CreateJoint(in d);
    }

    public static JointHandle CreateDistanceJoint(
        Entity bodyA,
        Entity? bodyB,
        float minDistance,
        float maxDistance,
        float springFrequencyHz = 0.0f,
        float springDampingRatio = 0.0f,
        float breakForce = 0.0f)
    {
        if (bodyA == null || !bodyA.IsValid) return JointHandle.Invalid;
        JointDesc d = new()
        {
            Type = JointType.Distance,
            EntityA = bodyA.Id,
            EntityB = bodyB != null ? bodyB.Id : EntityId.Invalid,
            AutoDetectAnchors = true,
            MinDistance = minDistance,
            MaxDistance = maxDistance,
            SpringFrequencyHz = springFrequencyHz,
            SpringDampingRatio = springDampingRatio,
            BreakForce = breakForce
        };
        return CreateJoint(in d);
    }

    public static JointHandle CreateHingeJoint(
        Entity bodyA,
        Entity? bodyB,
        in Vec3 worldAnchor,
        in Vec3 worldAxis,
        float minAngleRadians = -MathF.PI,
        float maxAngleRadians = MathF.PI,
        float maxFrictionTorque = 0.0f,
        float breakForce = 0.0f,
        float breakTorque = 0.0f)
    {
        if (bodyA == null || !bodyA.IsValid) return JointHandle.Invalid;
        JointDesc d = new()
        {
            Type = JointType.Hinge,
            EntityA = bodyA.Id,
            EntityB = bodyB != null ? bodyB.Id : EntityId.Invalid,
            AnchorA = worldAnchor,
            AnchorB = worldAnchor,
            Axis = worldAxis,
            AutoDetectAnchors = true,
            MinLimit = minAngleRadians,
            MaxLimit = maxAngleRadians,
            MaxFriction = maxFrictionTorque,
            BreakForce = breakForce,
            BreakTorque = breakTorque
        };
        return CreateJoint(in d);
    }

    public static JointHandle CreateSliderJoint(
        Entity bodyA,
        Entity? bodyB,
        in Vec3 worldAnchor,
        in Vec3 worldAxis,
        float minPosition,
        float maxPosition,
        float maxFrictionForce = 0.0f,
        float breakForce = 0.0f,
        float breakTorque = 0.0f)
    {
        if (bodyA == null || !bodyA.IsValid) return JointHandle.Invalid;
        JointDesc d = new()
        {
            Type = JointType.Slider,
            EntityA = bodyA.Id,
            EntityB = bodyB != null ? bodyB.Id : EntityId.Invalid,
            AnchorA = worldAnchor,
            AnchorB = worldAnchor,
            Axis = worldAxis,
            AutoDetectAnchors = true,
            MinLimit = minPosition,
            MaxLimit = maxPosition,
            MaxFriction = maxFrictionForce,
            BreakForce = breakForce,
            BreakTorque = breakTorque
        };
        return CreateJoint(in d);
    }

    public static bool UpdateJoint(JointHandle handle, in JointDesc desc) => handle.Update(in desc);
    public static bool DestroyJoint(JointHandle handle) => handle.Destroy();
    public static bool IsJointValid(JointHandle handle) => handle.IsValid;
    public static bool GetJointDesc(JointHandle handle, out JointDesc desc) => handle.TryGetDesc(out desc);
    public static bool GetJointState(JointHandle handle, out JointState state) => handle.TryGetState(out state);

    public static JointHandle[] GetEntityJoints(Entity entity)
    {
        if (entity == null || !entity.IsValid) return Array.Empty<JointHandle>();
        uint count = NativeBridge.GetEntityJointCount(entity.Id);
        if (count == 0u) return Array.Empty<JointHandle>();
        var list = new JointHandle[count];
        for (uint i = 0; i < count; i++)
        {
            list[i] = new JointHandle(NativeBridge.GetEntityJointHandle(entity.Id, i));
        }
        return list;
    }
}
