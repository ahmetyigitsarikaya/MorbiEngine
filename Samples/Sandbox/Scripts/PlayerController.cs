using System;

namespace Morbi.Engine;

public enum PlayerState
{
    Idle = 0,
    Walk = 1,
    Run = 2,
    Jump = 3,
    Punch = 4,
    WalkBackward = 5,
    StrafeLeft = 6,
    StrafeRight = 7,
    CrouchIdle = 8,
    CrouchWalkForward = 9,
    CrouchWalkBack = 10,
    CrouchWalkLeft = 11,
    CrouchWalkRight = 12
}

public sealed class PlayerController : Script
{
    private Entity? _playerEntity;
    private Animator? _animator;
    private PlayerState _currentState = (PlayerState)(-1);
    private bool _isCrouching;

    // Movement & Tuning parameters
    private float _walkSpeed = 2.8f;
    private float _runSpeed = 6.2f;
    private float _crouchSpeed = 1.65f;
    private float _rotationSmoothSpeed = 10.0f;

    // Jump & Punch State handling
    private float _jumpTimer = 0.0f;
    private const float kJumpDuration = 1.25f; // Jump clip duration lock
    private float _punchTimer = 0.0f;
    private const float kPunchDuration = 0.95f;

    // Animation File Paths indexed by (int)PlayerState
    private static readonly string[] s_clipPaths = new[]
    {
        "Assets/Animations/Idle.fbx",                // 0: Idle
        "Assets/Animations/Walking.fbx",             // 1: Walk
        "Assets/Animations/Running.fbx",             // 2: Run
        "Assets/Animations/Jumping.fbx",             // 3: Jump
        "Assets/Animations/Punching.fbx",            // 4: Punch
        "Assets/Animations/Walking Backwards.fbx",   // 5: WalkBackward
        "Assets/Animations/Right Strafe Walking.fbx",// 6: StrafeLeft (Mixamo FBX +X maps to LH Right, so Left/Right are swapped)
        "Assets/Animations/Left Strafe Walking.fbx", // 7: StrafeRight
        "Assets/Animations/Male Crouch Pose.fbx",    // 8: CrouchIdle
        "Assets/Animations/Crouch Walk Forward.fbx", // 9: CrouchWalkForward
        "Assets/Animations/Crouch Walk Back.fbx",    // 10: CrouchWalkBack
        "Assets/Animations/Crouch Walk Right.fbx",   // 11: CrouchWalkLeft
        "Assets/Animations/Crouch Walk Left.fbx"     // 12: CrouchWalkRight
    };

    private static readonly string[] s_stateNames = new[]
    {
        "Idle", "Walk", "Run", "Jump", "Punch",
        "WalkBack", "StrafeL", "StrafeR",
        "Crouch", "CrouchFwd", "CrouchBack", "CrouchL", "CrouchR"
    };

    private readonly string?[] _resolvedClips = new string?[s_clipPaths.Length];
    private float _rebindCooldown = 0.0f;
    private const float kRebindInterval = 0.5f;

    // Zero-allocation UI state caching
    private float _lastReportedSpeed = -1.0f;
    private string _lastSpeedString = "SPEED: 0.0 m/s";

    public static PlayerState LocalPlayerState { get; private set; } = PlayerState.Idle;
    public static bool IsLocalCrouching { get; private set; }
    public PlayerState CurrentState => _currentState;

    public override void OnCreate()
    {
        _rebindCooldown = 0.0f;
        // Find Player Entity (supports 'Player', 'RemyCharacter', 'Remy')
        _playerEntity = Scene.FindEntity("Player") ?? Scene.FindEntity("RemyCharacter");
        if (_playerEntity != null && _playerEntity.IsValid)
        {
            _animator = _playerEntity.GetAnimator();
            NativeBridge.LogInfo("C#.PlayerController", $"Player Entity bound: '{_playerEntity.Name}' (Index={_playerEntity.Index})");

            // Initial state: Idle
            SetState(PlayerState.Idle);
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        // 0. Lazy re-acquisition in case entities were spawned or restored dynamically
        if (_playerEntity == null || !_playerEntity.IsValid || !_playerEntity.IsAlive)
        {
            _rebindCooldown -= deltaTime;
            if (_rebindCooldown <= 0.0f)
            {
                _rebindCooldown = kRebindInterval;
                Array.Clear(_resolvedClips);
                _playerEntity = Scene.FindEntity("Player") ?? Scene.FindEntity("RemyCharacter");
                _animator = _playerEntity?.GetAnimator();
                if (_playerEntity != null && _animator != null)
                {
                    var targetState = (_currentState != (PlayerState)(-1)) ? _currentState : PlayerState.Idle;
                    _currentState = (PlayerState)(-1);
                    SetState(targetState);
                }
            }
        }

        if (_playerEntity == null || !_playerEntity.IsValid)
        {
            return;
        }

        Transform transform = _playerEntity.GetTransform();
        Vec3 currentPos = transform.Position;

        // 1. Read Inputs via NativeBridge Input class
        float inputX = 0.0f;
        float inputZ = 0.0f;

        if (Input.IsKeyDown(KeyCode.W) || Input.IsKeyDown(KeyCode.Up)) inputZ += 1.0f;
        if (Input.IsKeyDown(KeyCode.S) || Input.IsKeyDown(KeyCode.Down)) inputZ -= 1.0f;
        if (Input.IsKeyDown(KeyCode.D) || Input.IsKeyDown(KeyCode.Right)) inputX += 1.0f;
        if (Input.IsKeyDown(KeyCode.A) || Input.IsKeyDown(KeyCode.Left)) inputX -= 1.0f;

        bool isShiftDown = Input.IsKeyDown(KeyCode.LShift) || Input.IsKeyDown(KeyCode.RShift);
        bool isSpaceDown = Input.WasKeyPressedThisFrame(KeyCode.Space);
        bool isPunchPressed = Input.LeftMousePressed;

        if (Input.WasKeyPressedThisFrame(KeyCode.C))
        {
            _isCrouching = !_isCrouching;
        }
        bool crouchActive = (_isCrouching || Input.IsKeyDown(KeyCode.LCtrl)) && !isShiftDown;
        IsLocalCrouching = crouchActive;

        // Normalize movement vector
        float moveLenSq = inputX * inputX + inputZ * inputZ;
        bool isMoving = moveLenSq > 0.0001f;

        float camYaw = CameraController.CurrentYaw;
        Vec3 moveDir = new(0f, 0f, 0f);
        if (isMoving)
        {
            float invLen = 1.0f / MathF.Sqrt(moveLenSq);
            float normX = inputX * invLen;
            float normZ = inputZ * invLen;

            float sinY = MathF.Sin(camYaw);
            float cosY = MathF.Cos(camYaw);

            moveDir = new Vec3(
                normX * cosY + normZ * sinY,
                0f,
                normZ * cosY - normX * sinY
            );
        }

        PlayerState desiredLocomotion = ResolveLocomotionState(isMoving, isShiftDown, crouchActive, inputX, inputZ);

        // 2. State Machine Logic
        if (_currentState == PlayerState.Jump)
        {
            _jumpTimer -= deltaTime;
            if (_jumpTimer <= 0.0f)
            {
                SetState(desiredLocomotion);
            }
        }
        else if (_currentState == PlayerState.Punch)
        {
            _punchTimer -= deltaTime;
            if (isSpaceDown)
            {
                _isCrouching = false;
                IsLocalCrouching = false;
                SetState(PlayerState.Jump);
                _jumpTimer = kJumpDuration;
            }
            else if (isPunchPressed && _punchTimer <= kPunchDuration * 0.5f)
            {
                _currentState = (PlayerState)(-1);
                SetState(PlayerState.Punch);
                _punchTimer = kPunchDuration;
            }
            else if (_punchTimer <= 0.0f)
            {
                SetState(desiredLocomotion);
            }
        }
        else
        {
            if (isPunchPressed)
            {
                _isCrouching = false;
                IsLocalCrouching = false;
                SetState(PlayerState.Punch);
                _punchTimer = kPunchDuration;
            }
            else if (isSpaceDown)
            {
                _isCrouching = false;
                IsLocalCrouching = false;
                SetState(PlayerState.Jump);
                _jumpTimer = kJumpDuration;
            }
            else
            {
                SetState(desiredLocomotion);
            }
        }

        // 3. Kinematic Movement Translation
        float currentSpeed = 0.0f;
        if (_currentState == PlayerState.Run)
        {
            currentSpeed = _runSpeed;
        }
        else if (_currentState == PlayerState.Jump)
        {
            currentSpeed = isShiftDown ? _runSpeed * 0.85f : _walkSpeed * 0.85f;
        }
        else if (_currentState == PlayerState.Punch)
        {
            currentSpeed = _walkSpeed * 0.35f;
        }
        else if (crouchActive && isMoving)
        {
            currentSpeed = _crouchSpeed;
        }
        else if (isMoving)
        {
            currentSpeed = _walkSpeed;
        }

        if (isMoving)
        {
            float dx = moveDir.X * currentSpeed * deltaTime;
            float dz = moveDir.Z * currentSpeed * deltaTime;

            Vec3 newPos = new(currentPos.X + dx, currentPos.Y, currentPos.Z + dz);
            transform.Position = newPos;

            // 4. Smooth Rotation: face moveDir when sprinting/forward; face camera forward when strafing or walking backward
            bool useCameraFacing = _currentState == PlayerState.WalkBackward
                || _currentState == PlayerState.StrafeLeft
                || _currentState == PlayerState.StrafeRight
                || _currentState == PlayerState.CrouchWalkBack
                || _currentState == PlayerState.CrouchWalkLeft
                || _currentState == PlayerState.CrouchWalkRight;

            float targetYaw = useCameraFacing ? camYaw : MathF.Atan2(moveDir.X, moveDir.Z);
            float halfYaw = targetYaw * 0.5f;

            Quat targetRot = new()
            {
                X = 0f,
                Y = MathF.Sin(halfYaw),
                Z = 0f,
                W = MathF.Cos(halfYaw)
            };

            Quat curRot = transform.Rotation;
            float rotT = Math.Clamp(_rotationSmoothSpeed * deltaTime, 0.0f, 1.0f);

            float dot = curRot.X * targetRot.X + curRot.Y * targetRot.Y + curRot.Z * targetRot.Z + curRot.W * targetRot.W;
            if (dot < 0.0f)
            {
                targetRot.X = -targetRot.X;
                targetRot.Y = -targetRot.Y;
                targetRot.Z = -targetRot.Z;
                targetRot.W = -targetRot.W;
            }

            Quat blendedRot = new()
            {
                X = curRot.X + (targetRot.X - curRot.X) * rotT,
                Y = curRot.Y + (targetRot.Y - curRot.Y) * rotT,
                Z = curRot.Z + (targetRot.Z - curRot.Z) * rotT,
                W = curRot.W + (targetRot.W - curRot.W) * rotT
            };

            float rotLen = MathF.Sqrt(blendedRot.X * blendedRot.X + blendedRot.Y * blendedRot.Y + blendedRot.Z * blendedRot.Z + blendedRot.W * blendedRot.W);
            if (rotLen > 0.0001f)
            {
                float invRotLen = 1.0f / rotLen;
                blendedRot.X *= invRotLen;
                blendedRot.Y *= invRotLen;
                blendedRot.Z *= invRotLen;
                blendedRot.W *= invRotLen;
            }

            transform.Rotation = blendedRot;
        }

        // 5. GameUI HUD Data-Binding Update (zero-allocation state lookup & cached speed string)
        int stateIdx = (int)_currentState;
        string stateStr = (stateIdx >= 0 && stateIdx < s_stateNames.Length) ? s_stateNames[stateIdx] : "Unknown";
        NativeBridge.SetUIProperty("player.state", (float)_currentState, stateStr);

        if (MathF.Abs(currentSpeed - _lastReportedSpeed) > 0.05f)
        {
            _lastReportedSpeed = currentSpeed;
            _lastSpeedString = $"SPEED: {currentSpeed:F1} m/s";
        }
        NativeBridge.SetUIProperty("player.speed", currentSpeed, _lastSpeedString);
    }

    private static PlayerState ResolveLocomotionState(
        bool isMoving,
        bool isShiftDown,
        bool isCrouching,
        float inputX,
        float inputZ)
    {
        if (!isMoving)
        {
            return isCrouching ? PlayerState.CrouchIdle : PlayerState.Idle;
        }

        if (isCrouching)
        {
            if (inputZ > 0.1f) return PlayerState.CrouchWalkForward;
            if (inputZ < -0.1f) return PlayerState.CrouchWalkBack;
            return inputX < 0.0f ? PlayerState.CrouchWalkLeft : PlayerState.CrouchWalkRight;
        }

        if (isShiftDown)
        {
            return PlayerState.Run;
        }

        if (inputZ > 0.1f) return PlayerState.Walk;
        if (inputZ < -0.1f) return PlayerState.WalkBackward;
        return inputX < 0.0f ? PlayerState.StrafeLeft : PlayerState.StrafeRight;
    }

    private void SetState(PlayerState newState)
    {
        if (_currentState == newState)
        {
            return; // Guard: NEVER reset animation if already in this state!
        }

        PlayerState prevState = _currentState;
        _currentState = newState;
        LocalPlayerState = newState;
        if (_animator == null) return;

        int idx = (int)_currentState;
        if (idx < 0 || idx >= s_clipPaths.Length) idx = 0;

        string clipPath = s_clipPaths[idx];
        string resolvedPath = _resolvedClips[idx] ??= ResolveAssetPath(clipPath);

        NativeBridge.LogInfo("C#.PlayerController", $"State Transition -> [{_currentState}] | Clip: '{clipPath}' (Resolved: '{resolvedPath}')");
        if (prevState == (PlayerState)(-1))
        {
            _animator.SetClip(resolvedPath);
            _animator.Play();
        }
        else
        {
            float blendDuration = (_currentState == PlayerState.Punch || _currentState == PlayerState.Jump)
                ? 0.08f
                : 0.16f;
            _animator.CrossFade(resolvedPath, blendDuration);
        }

        if (_currentState == PlayerState.Jump)
        {
            NativeBridge.TriggerUIEvent("player.jump", 1);
        }
    }

    private static string ResolveAssetPath(string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath)) return relativePath;

        string vfsResolved = NativeBridge.ResolveVirtualPath(relativePath);
        if (!string.IsNullOrEmpty(vfsResolved) && System.IO.File.Exists(vfsResolved))
        {
            return vfsResolved;
        }

        if (System.IO.File.Exists(relativePath))
        {
            return System.IO.Path.GetFullPath(relativePath);
        }

        string exeRelative = System.IO.Path.Combine(AppContext.BaseDirectory, relativePath);
        if (System.IO.File.Exists(exeRelative))
        {
            return System.IO.Path.GetFullPath(exeRelative);
        }

        return relativePath;
    }
}
