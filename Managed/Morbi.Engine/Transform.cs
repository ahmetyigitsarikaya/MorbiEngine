using System;

namespace Morbi.Engine;

public sealed class Transform
{
    private readonly EntityId _entityId;

    internal Transform(EntityId entityId)
    {
        _entityId = entityId;
    }

    public EntityId EntityId => _entityId;

    public Vec3 Position
    {
        get
        {
            Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < transforms.Length) ? transforms[idx].Position : new Vec3(0f, 0f, 0f);
        }
        set
        {
            Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < transforms.Length)
            {
                transforms[idx].Position = value;
                NativeBridge.MarkPhysicsTransformDirty(_entityId);
            }
        }
    }

    public Quat Rotation
    {
        get
        {
            Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < transforms.Length) ? transforms[idx].Rotation : Quat.Identity;
        }
        set
        {
            Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < transforms.Length)
            {
                transforms[idx].Rotation = value;
                NativeBridge.MarkPhysicsTransformDirty(_entityId);
            }
        }
    }

    public Vec3 Scale
    {
        get
        {
            Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            return (idx >= 0 && idx < transforms.Length) ? transforms[idx].Scale : new Vec3(1f, 1f, 1f);
        }
        set
        {
            Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
            int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
            if (idx >= 0 && idx < transforms.Length)
            {
                transforms[idx].Scale = value;
            }
        }
    }

    public void SetPositionAndRotation(in Vec3 position, in Quat rotation)
    {
        Span<TransformComponent> transforms = NativeBridge.GetTransformSpan(out ReadOnlySpan<EntityId> entities);
        int idx = ComponentLookup.FindDenseIndex(_entityId, entities);
        if (idx >= 0 && idx < transforms.Length)
        {
            transforms[idx].Position = position;
            transforms[idx].Rotation = rotation;
            NativeBridge.MarkPhysicsTransformDirty(_entityId);
        }
    }

    public void LookAt(Vec3 targetPoint)
    {
        Vec3 curPos = Position;
        float dx = targetPoint.X - curPos.X;
        float dy = targetPoint.Y - curPos.Y;
        float dz = targetPoint.Z - curPos.Z;
        float horizDist = MathF.Sqrt(dx * dx + dz * dz);

        if (horizDist > 0.001f)
        {
            float pitch = -MathF.Atan2(dy, horizDist);
            float yaw = MathF.Atan2(dx, dz);

            float halfP = pitch * 0.5f;
            float halfY = yaw * 0.5f;

            Rotation = new Quat
            {
                X = MathF.Sin(halfP) * MathF.Cos(halfY),
                Y = MathF.Cos(halfP) * MathF.Sin(halfY),
                Z = -MathF.Sin(halfP) * MathF.Sin(halfY),
                W = MathF.Cos(halfP) * MathF.Cos(halfY)
            };
        }
    }
}
