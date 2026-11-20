#nullable enable
using System;
using Morbi.Engine;

namespace GameplayValidation;

[EntityScript]
public sealed class RbPlayerController : Script
{
    private RigidBody? _rb;
    private bool _hitCrate;
    private bool _hitBarrel;

    public override void OnStart()
    {
        _rb = Entity.GetRigidBody();
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_rb == null) return;
        if (_rb.LinearVelocity.Z < 7.5f)
        {
            _rb.AddForce(new Vec3(0.0f, 0.0f, 180.0f), ForceMode.Force);
        }
    }

    public override void OnCollisionEnter(Collision collision)
    {
        string? hitName = collision.Entity?.Name;
        if (hitName == "DynamicCrate" && !_hitCrate)
        {
            _hitCrate = true;
            if (Scene.FindEntity("GV_RbHitCrate") == null) Scene.CreateEntity("GV_RbHitCrate");
        }
        else if (hitName == "DynamicBarrel" && !_hitBarrel)
        {
            _hitBarrel = true;
            if (Scene.FindEntity("GV_RbHitBarrel") == null) Scene.CreateEntity("GV_RbHitBarrel");
        }
        else if (hitName == "HingeDoor")
        {
            if (Scene.FindEntity("GV_RbHitDoor") == null) Scene.CreateEntity("GV_RbHitDoor");
        }
    }
}

[EntityScript]
public sealed class DynamicBarrelMonitor : Script
{
    private bool _hit;

    public override void OnCollisionEnter(Collision collision)
    {
        string? n = collision.Entity?.Name;
        if (!_hit && (n == "RbPlayer" || n == "DynamicCrate"))
        {
            _hit = true;
            if (Scene.FindEntity("GV_RbHitBarrel") == null) Scene.CreateEntity("GV_RbHitBarrel");
        }
    }
}

[EntityScript]
public sealed class HingeDoorMonitor : Script
{
    private bool _hit;

    public override void OnCollisionEnter(Collision collision)
    {
        string? n = collision.Entity?.Name;
        if (!_hit && n != "GroundFloor" && n != "DoorFrame")
        {
            _hit = true;
            if (Scene.FindEntity("GV_RbHitDoor") == null) Scene.CreateEntity("GV_RbHitDoor");
        }
    }
}

[EntityScript]
public sealed class MovingPlatformDriver : Script
{
    private int _step;

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _step++;
        if (_step >= 120 && _step <= 280)
        {
            Transform tr = Entity.GetTransform();
            tr.Position = tr.Position + new Vec3(0.0f, 0.0f, 2.8f * fixedDeltaTime);
        }
    }
}

[EntityScript]
public sealed class CharCourseController : Script
{
    private CharacterController? _cc;
    private int _step;
    private bool _climbedStairs;
    private bool _rodePlatform;

    public override void OnStart()
    {
        _cc = Entity.GetCharacterController();
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_cc == null) return;
        _step++;

        if (_step == 36 && _cc.IsGrounded)
        {
            if (Scene.FindEntity("GV_CharGrounded") == null) Scene.CreateEntity("GV_CharGrounded");
        }

        // Walk forward up stairs & onto MovingPlatform at Z ~ 3.2
        if (_step >= 37 && _step <= 122)
        {
            _cc.Move(new Vec3(0.0f, 0.0f, 7.6f * fixedDeltaTime));
        }

        if (!_climbedStairs && _step >= 100 && Entity.GetTransform().Position.Y > 0.65f)
        {
            _climbedStairs = true;
            if (Scene.FindEntity("GV_CharStairsOK") == null) Scene.CreateEntity("GV_CharStairsOK");
        }

        // Stand on moving platform (steps 120..280) and ride to Z > 7.0
        if (!_rodePlatform && _step >= 270 && Entity.GetTransform().Position.Z > 6.5f)
        {
            _rodePlatform = true;
            if (Scene.FindEntity("GV_CharMovingPlatformOK") == null) Scene.CreateEntity("GV_CharMovingPlatformOK");
        }

        // Step off moving platform into GoalTrigger at Z = 9.5
        if (_step >= 285 && _step <= 345)
        {
            _cc.Move(new Vec3(0.0f, 0.0f, 4.5f * fixedDeltaTime));
        }
    }
}

[EntityScript]
public sealed class RampClimberController : Script
{
    private CharacterController? _cc;
    private int _step;

    public override void OnStart()
    {
        _cc = Entity.GetCharacterController();
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_cc == null) return;
        _step++;
        if (_step >= 30 && _step <= 110)
        {
            _cc.Move(new Vec3(0.0f, 0.0f, 4.5f * fixedDeltaTime));
        }
        if (_step == 112 && Entity.GetTransform().Position.Y > 0.85f)
        {
            if (Scene.FindEntity("GV_RampClimbOK") == null) Scene.CreateEntity("GV_RampClimbOK");
        }
    }
}

[EntityScript]
public sealed class GoalTriggerMonitor : Script
{
    private bool _entered;

    public override void OnTriggerEnter(Collider other)
    {
        if (_entered) return;
        _entered = true;
        if (Scene.FindEntity("GV_GoalTriggerOK") == null)
        {
            Entity? m = Scene.CreateEntity("GV_GoalTriggerOK");
            if (m != null) m.GetTransform().Position = new Vec3(other.EntityId.Index, 0f, 0f);
        }
    }
}

[EntityScript]
public sealed class SliderDriver : Script
{
    private RigidBody? _rb;
    private int _step;

    public override void OnStart()
    {
        _rb = Entity.GetRigidBody();
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _step++;
        if (_rb != null && _step < 120)
        {
            _rb.AddForce(new Vec3(24.0f, 0.0f, 0.0f), ForceMode.Force);
        }
    }
}

[EntityScript]
public sealed class FixedCouplerMonitor : Script
{
    private RigidBody? _rb;
    private Entity? _carrier;
    private int _step;

    public override void OnStart()
    {
        _rb = Entity.GetRigidBody();
        _carrier = Scene.FindEntity("FixedCarrier");
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _step++;
        if (_step == 10 && _rb != null)
        {
            _rb.AddImpulse(new Vec3(0.0f, 0.0f, 18.0f));
        }
        if (_step == 90 && _carrier != null)
        {
            float dist = Vec3.Distance(Entity.GetTransform().Position, _carrier.GetTransform().Position);
            if (_carrier.GetTransform().Position.Z > 3.5f && MathF.Abs(dist - 1.0f) < 0.25f)
            {
                if (Scene.FindEntity("GV_FixedJointCoupledOK") == null) Scene.CreateEntity("GV_FixedJointCoupledOK");
            }
        }
    }
}

[EntityScript]
public sealed class ValidationCoordinator : Script
{
    private int _step;

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _step++;
        if (_step == 200)
        {
            Entity? door = Scene.FindEntity("HingeDoor");
            if (door != null)
            {
                JointHandle[] joints = Physics.GetEntityJoints(door);
                if (joints.Length > 0 && joints[0].TryGetState(out JointState js) && MathF.Abs(js.PositionOrAngle) > 0.2f)
                {
                    if (Scene.FindEntity("GV_HingeDoorSwungOK") == null) Scene.CreateEntity("GV_HingeDoorSwungOK");
                }
            }

            Entity? slider = Scene.FindEntity("SliderCarriage");
            if (slider != null && slider.GetTransform().Position.X > 12.2f && MathF.Abs(slider.GetTransform().Position.Y - 1.0f) < 0.1f)
            {
                if (Scene.FindEntity("GV_SliderMechanismOK") == null) Scene.CreateEntity("GV_SliderMechanismOK");
            }

            Entity? pendulum = Scene.FindEntity("PendulumBob");
            if (pendulum != null && pendulum.GetTransform().Position.Y < 4.5f)
            {
                if (Scene.FindEntity("GV_DistancePendulumOK") == null) Scene.CreateEntity("GV_DistancePendulumOK");
            }

            Entity? totem = Scene.FindEntity("CompoundTotem");
            CompoundCollider? cc = totem?.GetCompoundCollider();
            if (totem != null && cc != null && cc.Count == 2 && totem.GetTransform().Position.Y > 0.4f)
            {
                if (Scene.FindEntity("GV_CompoundTotemOK") == null) Scene.CreateEntity("GV_CompoundTotemOK");
            }
        }
    }
}
