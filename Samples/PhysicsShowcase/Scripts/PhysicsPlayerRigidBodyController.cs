using System;
using Morbi.Engine;

namespace Samples.PhysicsShowcase;

/// <summary>
/// Example A: Physics-driven RigidBody player controller using fixed-step forces,
/// ground raycast detection, and impulse jumping.
/// </summary>
[EntityScript]
public sealed class PhysicsPlayerRigidBodyController : Script
{
    public float MoveAcceleration { get; set; } = 28.0f;
    public float MaxGroundSpeed { get; set; } = 8.0f;
    public float JumpImpulse { get; set; } = 6.5f;
    public float GroundCheckDistance { get; set; } = 0.65f;

    private bool _grounded;
    private bool _jumpQueued;

    public bool IsGrounded => _grounded;

    public override void OnStart()
    {
        if (RigidBody == null) return;
        RigidBody.MotionType = MotionType.Dynamic;
        RigidBody.LockedAxes = AxisLocks.AllRotation;
        RigidBody.LinearDamping = 1.5f;
        RigidBody.Friction = 0.4f;
        RigidBody.UseCCD = true;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (Input.WasKeyPressedThisFrame(KeyCode.Space) && _grounded)
        {
            _jumpQueued = true;
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (Entity == null || Transform == null || RigidBody == null) return;

        Vec3 origin = Transform.Position + new Vec3(0.0f, 0.05f, 0.0f);
        var filter = new QueryFilter
        {
            IgnoreEntity = Entity,
            IncludeTriggers = false
        };
        _grounded = Physics.Raycast(origin, Vec3.Down, out _, GroundCheckDistance, filter);

        Vec3 moveDir = Vec3.Zero;
        if (Input.IsKeyDown(KeyCode.W)) moveDir = moveDir + Vec3.Forward;
        if (Input.IsKeyDown(KeyCode.S)) moveDir = moveDir - Vec3.Forward;
        if (Input.IsKeyDown(KeyCode.D)) moveDir = moveDir + Vec3.Right;
        if (Input.IsKeyDown(KeyCode.A)) moveDir = moveDir - Vec3.Right;

        if (moveDir.LengthSquared() > 1e-4f)
        {
            moveDir = moveDir.Normalized;
            Vec3 currentVel = RigidBody.LinearVelocity;
            float horizSpeed = MathF.Sqrt(currentVel.X * currentVel.X + currentVel.Z * currentVel.Z);
            if (horizSpeed < MaxGroundSpeed)
            {
                RigidBody.AddForce(moveDir * MoveAcceleration, ForceMode.Acceleration);
            }
        }

        if (_jumpQueued && _grounded)
        {
            Vec3 impulse = Vec3.Up * (JumpImpulse * RigidBody.Mass);
            RigidBody.AddImpulse(impulse);
        }
        _jumpQueued = false;
    }

    public override void OnCollisionEnter(Collision collision)
    {
        if (collision.Entity != null && collision.RelativeVelocity.Length() > 4.0f)
        {
            NativeBridge.LogInfo("PhysicsSample",
                $"RigidBody impact with '{collision.Entity.Name}' (speed={collision.RelativeVelocity.Length():F2} m/s).");
        }
    }
}
