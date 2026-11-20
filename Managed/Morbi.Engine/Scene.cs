using System;

namespace Morbi.Engine;

public static class Scene
{
    public static Entity? FindEntity(string name)
    {
        if (string.IsNullOrWhiteSpace(name)) return null;

        EntityId id = NativeBridge.FindEntityByName(name);
        if (id.IsValid)
        {
            return new Entity(id, name);
        }

        // Aliases for Player
        if (string.Equals(name, "Player", StringComparison.OrdinalIgnoreCase))
        {
            EntityId remyId = NativeBridge.FindEntityByName("RemyCharacter");
            if (!remyId.IsValid) remyId = NativeBridge.FindEntityByName("Remy");
            if (remyId.IsValid) return new Entity(remyId, "Player");
        }
        else if (string.Equals(name, "RemyCharacter", StringComparison.OrdinalIgnoreCase))
        {
            EntityId pId = NativeBridge.FindEntityByName("Player");
            if (pId.IsValid) return new Entity(pId, "RemyCharacter");
        }

        // Fallback: check Camera alias
        if (string.Equals(name, "MainCamera", StringComparison.OrdinalIgnoreCase))
        {
            EntityId camId = NativeBridge.FindEntityByName("Camera");
            if (camId.IsValid) return new Entity(camId, "Camera");
        }

        // Aliases for Floor / Ground
        if (string.Equals(name, "Floor", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(name, "Environment", StringComparison.OrdinalIgnoreCase))
        {
            EntityId floorId = NativeBridge.FindEntityByName("Floor");
            if (!floorId.IsValid) floorId = NativeBridge.FindEntityByName("CubeMesh");
            if (!floorId.IsValid) floorId = NativeBridge.FindEntityByName("StaticGroundFloor");
            if (floorId.IsValid) return new Entity(floorId, "Floor");
        }

        return null;
    }

    public static uint AliveEntityCount => NativeBridge.GetAliveEntityCount();

    public static bool LoadScene(string filePath) => NativeBridge.LoadScene(filePath);

    public static bool SaveScene(string filePath) => NativeBridge.SaveScene(filePath);

    public static void Clear() => NativeBridge.ClearScene();

    public static Entity? CreateEntity(string name = "Entity")
    {
        EntityId id = NativeBridge.CreateEntity(name);
        if (!id.IsValid) return null;
        return new Entity(id, name);
    }

    public static bool DestroyEntity(Entity entity)
    {
        if (entity == null || !entity.IsValid) return false;
        return NativeBridge.DestroyEntity(entity.Id);
    }
}
