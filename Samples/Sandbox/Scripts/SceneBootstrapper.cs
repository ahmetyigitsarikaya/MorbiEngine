using System;

namespace Morbi.Engine;

/// <summary>
/// Bootstraps a clean, consistent scene setup for the Sandbox environment.
/// Ensures Floor, Player, and MainCamera entities are properly initialized, positioned, and oriented,
/// while pruning any lingering transient test entities.
/// </summary>
public sealed class SceneBootstrapper : Script
{
    private bool _bootstrapped;

    public override void OnCreate()
    {
        BootstrapScene();
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!_bootstrapped)
        {
            BootstrapScene();
        }
    }

    private void BootstrapScene()
    {
        if (_bootstrapped)
        {
            return;
        }

        NativeBridge.LogInfo("C#.SceneBootstrapper", "SceneBootstrapper starting environment initialization...");

        // 1. Setup Ground Floor Plane
        Entity? floor = Scene.FindEntity("Floor");
        if (floor != null && floor.IsValid)
        {
            Transform tr = floor.GetTransform();
            tr.Position = new Vec3(0.0f, -0.1f, 0.0f);
            tr.Rotation = Quat.Identity;
            tr.Scale = new Vec3(60.0f, 0.2f, 60.0f);
            NativeBridge.LogInfo("C#.SceneBootstrapper", $"[PASS] Floor entity '{floor.Name}' configured (Scale: 60x0.2x60).");
        }

        // 2. Setup Player Entity (Remy)
        Entity? player = Scene.FindEntity("Player");
        if (player != null && player.IsValid)
        {
            Transform tr = player.GetTransform();
            tr.Position = new Vec3(0.0f, 0.0f, 0.0f);
            tr.Rotation = Quat.Identity;
            tr.Scale = new Vec3(0.005f, 0.005f, 0.005f);
            NativeBridge.LogInfo("C#.SceneBootstrapper", $"[PASS] Player entity '{player.Name}' reset to origin (0, 0, 0) with scale 0.005 (Height ~1.89m).");
        }

        // 3. Setup MainCamera Entity
        Entity? camera = Scene.FindEntity("MainCamera");
        if (camera != null && camera.IsValid)
        {
            Transform tr = camera.GetTransform();
            tr.Position = new Vec3(0.0f, 2.0f, -3.5f);
            tr.LookAt(new Vec3(0.0f, 1.2f, 0.0f));
            NativeBridge.LogInfo("C#.SceneBootstrapper", $"[PASS] MainCamera entity '{camera.Name}' positioned at (0, 2.0, -3.5).");
        }

        // 4. Prune lingering transient test entities if present
        string[] staleEntities = new[]
        {
            "EmptyEntity",
            "CubeMesh_1",
            "Ch06_nonPBR",
            "Ch06_nonPBR_1",
            "Ch06_nonPBR_1_Copy",
            "Ch06_nonPBR_1_Copy_1"
        };

        foreach (string name in staleEntities)
        {
            EntityId id = NativeBridge.FindEntityByName(name);
            if (id.IsValid)
            {
                NativeBridge.DestroyEntity(id);
                NativeBridge.LogInfo("C#.SceneBootstrapper", $"Pruned transient entity: '{name}'");
            }
        }

        _bootstrapped = true;
        NativeBridge.LogInfo("C#.SceneBootstrapper", "Scene environment bootstrap complete.");
    }
}
