using System;

namespace Morbi.Engine;

/// <summary>
/// Third-person smooth follow camera controller that tracks the player entity.
/// Continuously positions itself behind/above the character and points towards the upper body.
/// </summary>
public sealed class CameraController : Script
{
    public static bool Enabled { get; set; } = true;

    /// <summary>
    /// Current horizontal orbit yaw angle in radians (used by PlayerController for camera-relative movement).
    /// </summary>
    public static float CurrentYaw { get; private set; } = 0.0f;

    private Entity? _cameraEntity;
    private Entity? _playerEntity;

    // 3D Mouse Orbit & Zoom Configuration (X: Yaw, Y: Pitch, Z: Scroll Distance)
    private float _yaw = 0.0f;
    private float _pitch = 0.30f; // ~17.2 degrees elevation (matches initial (0, 2.0, -3.5) offset)
    private float _distance = 3.68f;
    private const float kMinDistance = 1.2f;
    private const float kMaxDistance = 12.0f;
    private const float kMinPitch = -0.35f; // ~-20 deg
    private const float kMaxPitch = 1.35f;  // ~+77 deg
    private const float kMouseSensitivity = 0.0045f;
    private const float kScrollZoomSpeed = 0.45f;

    private readonly Vec3 _lookTargetOffset = new(0.0f, 1.2f, 0.0f);
    private readonly float _smoothSpeed = 10.0f;
    private float _rebindCooldown = 0.0f;
    private const float kRebindInterval = 0.5f;

    public override void OnCreate()
    {
        _yaw = 0.0f;
        _pitch = 0.30f;
        _distance = 3.68f;
        CurrentYaw = 0.0f;
        _rebindCooldown = 0.0f;

        _cameraEntity = Scene.FindEntity("MainCamera") ?? Scene.FindEntity("Camera");
        _playerEntity = Scene.FindEntity("Player") ?? Scene.FindEntity("RemyCharacter");

        if (_cameraEntity != null && _cameraEntity.IsValid)
        {
            NativeBridge.LogInfo("C#.Camera", $"Camera Entity bound: '{_cameraEntity.Name}' (Index={_cameraEntity.Index})");
        }

        if (_playerEntity != null && _playerEntity.IsValid)
        {
            NativeBridge.LogInfo("C#.Camera", $"Target Player Entity bound: '{_playerEntity.Name}' (Index={_playerEntity.Index})");
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!Enabled)
        {
            return;
        }

        // Lazy re-acquisition in case entities were created or loaded asynchronously
        bool needCam = _cameraEntity == null || !_cameraEntity.IsValid || !_cameraEntity.IsAlive;
        bool needPlayer = _playerEntity == null || !_playerEntity.IsValid || !_playerEntity.IsAlive;
        if (needCam || needPlayer)
        {
            _rebindCooldown -= deltaTime;
            if (_rebindCooldown <= 0.0f)
            {
                _rebindCooldown = kRebindInterval;
                if (needCam)
                {
                    _cameraEntity = Scene.FindEntity("MainCamera") ?? Scene.FindEntity("Camera");
                }
                if (needPlayer)
                {
                    _playerEntity = Scene.FindEntity("Player") ?? Scene.FindEntity("RemyCharacter");
                }
            }
        }

        if (_cameraEntity == null || !_cameraEntity.IsValid || _playerEntity == null || !_playerEntity.IsValid)
        {
            return;
        }

        Transform cameraTr = _cameraEntity.GetTransform();
        Transform playerTr = _playerEntity.GetTransform();

        // 1. Mouse Capture Toggle: Middle Mouse Button or Tab toggles full Mouse Lock; Escape releases it
        if (Input.MiddleMousePressed || Input.WasKeyPressedThisFrame(KeyCode.Tab))
        {
            Input.ToggleMouseCapture();
        }
        else if (Input.WasKeyPressedThisFrame(KeyCode.Escape) && Input.MouseCaptured)
        {
            Input.ReleaseMouse();
        }

        // 2. Read 3D Mouse Delta (X: Left/Right, Y: Up/Down, Z: Scroll Wheel)
        Vec3 mouseDelta = Input.MouseDelta3D;

        // Orbit camera when mouse is captured OR when holding Right mouse button (Left click is reserved for combat/punch)
        bool isOrbiting = Input.MouseCaptured || Input.RightMouseDown;
        if (isOrbiting)
        {
            _yaw += mouseDelta.X * kMouseSensitivity;
            _pitch = Math.Clamp(_pitch + mouseDelta.Y * kMouseSensitivity, kMinPitch, kMaxPitch);
        }

        // Side mouse buttons (X1 / Back & X2 / Forward) quick-adjust zoom or reset camera angle
        if (Input.MouseButton4Pressed)
        {
            _yaw = 0.0f;
            _pitch = 0.30f;
        }
        if (Input.MouseButton5Pressed)
        {
            _distance = 3.68f;
        }

        // Z axis (Scroll wheel) zooms camera in/out
        if (MathF.Abs(mouseDelta.Z) > 0.0001f)
        {
            _distance = Math.Clamp(_distance - mouseDelta.Z * kScrollZoomSpeed, kMinDistance, kMaxDistance);
        }

        CurrentYaw = _yaw;

        Vec3 playerPos = playerTr.Position;
        Vec3 curCamPos = cameraTr.Position;

        Vec3 lookTarget = new(
            playerPos.X + _lookTargetOffset.X,
            playerPos.Y + _lookTargetOffset.Y,
            playerPos.Z + _lookTargetOffset.Z
        );

        // Compute spherical orbit offset around lookTarget
        float cosPitch = MathF.Cos(_pitch);
        float sinPitch = MathF.Sin(_pitch);
        float sinYaw = MathF.Sin(_yaw);
        float cosYaw = MathF.Cos(_yaw);

        Vec3 orbitOffset = new(
            -sinYaw * cosPitch * _distance,
            sinPitch * _distance,
            -cosYaw * cosPitch * _distance
        );

        Vec3 desiredPos = new(
            lookTarget.X + orbitOffset.X,
            lookTarget.Y + orbitOffset.Y,
            lookTarget.Z + orbitOffset.Z
        );

        // Smooth Lerp tracking
        float t = Math.Clamp(_smoothSpeed * deltaTime, 0.0f, 1.0f);
        cameraTr.Position = new Vec3(
            curCamPos.X + (desiredPos.X - curCamPos.X) * t,
            curCamPos.Y + (desiredPos.Y - curCamPos.Y) * t,
            curCamPos.Z + (desiredPos.Z - curCamPos.Z) * t
        );

        // Continuous LookAt target (Upper body / head of character)
        cameraTr.LookAt(lookTarget);
    }
}
