using Morbi.Engine;

namespace Samples.PhysicsShowcase;

/// <summary>
/// Example B: Kinematic CharacterVirtual controller with slope handling, stair climbing,
/// moving platform ride-along, jumping, and dynamic rigid body pushing.
/// </summary>
[EntityScript]
public sealed class PhysicsCharacterControllerSample : Script
{
    public float WalkSpeed { get; set; } = 5.5f;
    public float SprintSpeed { get; set; } = 9.0f;
    public float JumpSpeed { get; set; } = 6.8f;

    private CharacterController? _controller;
    private bool _jumpRequested;

    public override void OnStart()
    {
        _controller = GetComponent<CharacterController>();
        if (_controller != null)
        {
            _controller.SetMaxSlopeAngle(48.0f);
            _controller.SetStepHeight(0.38f);
            _controller.MaxPushStrength = 300.0f;
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (Input.WasKeyPressedThisFrame(KeyCode.Space))
        {
            _jumpRequested = true;
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_controller == null) return;

        Vec3 wishDir = Vec3.Zero;
        if (Input.IsKeyDown(KeyCode.W)) wishDir = wishDir + Vec3.Forward;
        if (Input.IsKeyDown(KeyCode.S)) wishDir = wishDir - Vec3.Forward;
        if (Input.IsKeyDown(KeyCode.D)) wishDir = wishDir + Vec3.Right;
        if (Input.IsKeyDown(KeyCode.A)) wishDir = wishDir - Vec3.Right;

        if (wishDir.LengthSquared() > 1e-4f)
        {
            wishDir = wishDir.Normalized;
        }

        float speed = Input.IsKeyDown(KeyCode.LShift) ? SprintSpeed : WalkSpeed;
        _controller.Move(wishDir * (speed * fixedDeltaTime));

        if (_jumpRequested && _controller.IsGrounded && !_controller.IsSliding)
        {
            _controller.Jump(JumpSpeed);
        }
        _jumpRequested = false;
    }
}
