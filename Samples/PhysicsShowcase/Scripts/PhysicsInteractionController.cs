using System;
using Morbi.Engine;

namespace Samples.PhysicsShowcase;

/// <summary>
/// Example C: Physics interaction sample combining Raycast impulse shots,
/// OverlapSphere radial shockwaves, and OnTriggerEnter/Exit zone detection.
/// </summary>
[EntityScript]
public sealed class PhysicsInteractionController : Script
{
    public float RayDistance { get; set; } = 50.0f;
    public float ImpulseStrength { get; set; } = 18.0f;
    public float BlastRadius { get; set; } = 6.0f;
    public float BlastImpulse { get; set; } = 24.0f;

    public int BodiesInsideTrigger { get; private set; }

    public override void OnUpdate(float deltaTime)
    {
        if (Transform == null) return;

        if (Input.WasMouseButtonPressed(0))
        {
            FireRaycastImpulse(Transform.Position + new Vec3(0.0f, 1.5f, 0.0f), Vec3.Forward);
        }

        if (Input.WasKeyPressedThisFrame(KeyCode.E))
        {
            TriggerRadialShockwave(Transform.Position);
        }
    }

    public bool FireRaycastImpulse(in Vec3 origin, in Vec3 direction)
    {
        var filter = new QueryFilter
        {
            IgnoreEntity = Entity,
            IncludeTriggers = false,
            LayerMask = CollisionLayers.AllMask
        };

        if (!Physics.Raycast(origin, direction, out RaycastHit hit, RayDistance, filter))
        {
            return false;
        }

        RigidBody? rb = hit.Entity?.GetRigidBody();
        if (rb != null && rb.MotionType == MotionType.Dynamic)
        {
            Vec3 impulse = direction.Normalized * ImpulseStrength;
            rb.AddForceAtPosition(impulse, hit.Point, ForceMode.Impulse);
            return true;
        }
        return false;
    }

    public int TriggerRadialShockwave(in Vec3 center)
    {
        OverlapHit[] hits = Physics.OverlapSphere(center, BlastRadius,
            new QueryFilter { IgnoreEntity = Entity, IgnoreStatic = true, IncludeTriggers = false });

        int affected = 0;
        for (int i = 0; i < hits.Length; i++)
        {
            Entity? target = hits[i].Entity;
            RigidBody? rb = target?.GetRigidBody();
            if (target == null || rb == null || rb.MotionType != MotionType.Dynamic) continue;

            Vec3 delta = target.GetTransform().Position - center;
            float dist = MathF.Max(0.25f, delta.Length());
            float falloff = Math.Clamp(1.0f - (dist / BlastRadius), 0.1f, 1.0f);
            Vec3 pushDir = (delta.Normalized + new Vec3(0.0f, 0.45f, 0.0f)).Normalized;
            rb.AddImpulse(pushDir * (BlastImpulse * falloff));
            affected++;
        }
        return affected;
    }

    public override void OnTriggerEnter(Collider other)
    {
        BodiesInsideTrigger++;
        NativeBridge.LogInfo("PhysicsSample", $"Trigger entered by '{other.Entity.Name}' (inside={BodiesInsideTrigger}).");
    }

    public override void OnTriggerExit(Collider other)
    {
        BodiesInsideTrigger = Math.Max(0, BodiesInsideTrigger - 1);
        NativeBridge.LogInfo("PhysicsSample", $"Trigger exited by '{other.Entity.Name}' (inside={BodiesInsideTrigger}).");
    }
}
