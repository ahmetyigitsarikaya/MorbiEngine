using System;

namespace Morbi.Engine;

public sealed class CharacterAnimationController : Script
{
    public static bool Enabled { get; set; } = false; // Yield to PlayerController & CameraController

    private Entity? _remyEntity;
    private Entity? _cameraEntity;
    private Animator? _animator;
    private bool _spacePreviouslyPressed;

    // Camera follow configuration
    private readonly Vec3 _cameraOffset = new(0.0f, 2.0f, -3.5f);
    private readonly Vec3 _lookTargetOffset = new(0.0f, 1.2f, 0.0f);
    private readonly float _followSmoothSpeed = 8.0f;

    public override void OnCreate()
    {
        if (!Enabled)
        {
            return;
        }

        NativeBridge.LogInfo("C#.CharacterAnimationController", "OnCreate() initialized! Binding Remy animator and camera follow.");

        // 1. Sahnedeki 'RemyCharacter' isimli Entity'yi bul
        _remyEntity = Scene.FindEntity("RemyCharacter");
        if (_remyEntity != null && _remyEntity.IsValid)
        {
            NativeBridge.LogInfo("C#.CharacterAnimationController", $"Found 'RemyCharacter' Entity (Index={_remyEntity.Index}).");

            // 2. Remy karakterinde Animator bileşeni var mı kontrol et; varsa Play() et
            _animator = _remyEntity.GetAnimator();
            if (_animator != null)
            {
                _animator.Play();
                NativeBridge.LogInfo("C#.CharacterAnimationController", "Remy Animator detected; initial Play() invoked.");
            }
        }
        else
        {
            NativeBridge.LogInfo("C#.CharacterAnimationController", "Warning: Entity 'RemyCharacter' not found in Scene.");
        }

        // 3. Sahnedeki 'MainCamera' (veya 'Camera') isimli kamera Entity'sini bul
        _cameraEntity = Scene.FindEntity("MainCamera");
        if (_cameraEntity != null && _cameraEntity.IsValid)
        {
            NativeBridge.LogInfo("C#.CharacterAnimationController", $"Found 'MainCamera' Entity (Index={_cameraEntity.Index}).");
        }
        else
        {
            NativeBridge.LogInfo("C#.CharacterAnimationController", "Warning: Camera Entity not found in Scene.");
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!Enabled)
        {
            return;
        }

        // 0. Lazy re-acquisition in case entities were spawned or restored dynamically
        if (_remyEntity == null || !_remyEntity.IsValid || !_remyEntity.IsAlive)
        {
            _remyEntity = Scene.FindEntity("RemyCharacter");
            _animator = _remyEntity?.GetAnimator();
        }
        if (_cameraEntity == null || !_cameraEntity.IsValid || !_cameraEntity.IsAlive)
        {
            _cameraEntity = Scene.FindEntity("MainCamera");
        }
        // 1. Klavye girişini motorun yerel Input yöneticisinden kontrol et:
        bool isSpaceDown = Input.IsKeyDown(KeyCode.Space) || Input.WasKeyPressedThisFrame(KeyCode.Space);

        if (isSpaceDown && !_spacePreviouslyPressed)
        {
            NativeBridge.LogInfo("C#.CharacterAnimationController", "Space key pressed! Triggering Jumping animation clip.");

            // Remy karakterinin Animator bileşenine eriş
            if (_animator != null)
            {
                // 'Assets/Animations/Jumping.fbx' klibini ata ve baştan oynat
                _animator.SetClip("Assets/Animations/Jumping.fbx");
                _animator.Play();
            }

            // HUD / UI & Audio bildirimleri
            NativeBridge.TriggerUIEvent("player.jump", 1);
            NativeBridge.SetUIProperty("player.action", 1.0f, "Jumping");
        }
        _spacePreviouslyPressed = isSpaceDown;

        // 2. Kamera Takip / Bağlantı Mantığı (Camera Follow):
        if (_remyEntity != null && _cameraEntity != null && _remyEntity.IsValid && _cameraEntity.IsValid)
        {
            Transform playerTr = _remyEntity.GetTransform();
            Transform cameraTr = _cameraEntity.GetTransform();

            // Karakterin dünya pozisyonunu al
            Vec3 charPos = playerTr.Position;

            // Kamerayı karakterin arkasına ve üstüne konumlandır
            float targetX = charPos.X + _cameraOffset.X;
            float targetY = charPos.Y + _cameraOffset.Y;
            float targetZ = charPos.Z + _cameraOffset.Z;

            // Yumuşak ve stabil kamera takip interpolasyonu (Lerp)
            float t = Math.Clamp(_followSmoothSpeed * deltaTime, 0.0f, 1.0f);
            Vec3 curCamPos = cameraTr.Position;
            cameraTr.Position = new Vec3(
                curCamPos.X + (targetX - curCamPos.X) * t,
                curCamPos.Y + (targetY - curCamPos.Y) * t,
                curCamPos.Z + (targetZ - curCamPos.Z) * t
            );

            // Kameranın her karede karaktere bakmasını sağla (LookAt)
            Vec3 lookTarget = new(
                charPos.X + _lookTargetOffset.X,
                charPos.Y + _lookTargetOffset.Y,
                charPos.Z + _lookTargetOffset.Z
            );
            cameraTr.LookAt(lookTarget);
        }
    }
}
