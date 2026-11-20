#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Task/TaskScheduler.hpp"
#include "Morbi/Scene/SparseSet.hpp"

namespace Morbi::Scene
{
    enum class ComponentPoolKind : u8
    {
        Transform = 0,
        Hierarchy = 1,
        MeshRenderer = 2,
        Light = 3,
        Script = 4,
        RigidBody = 5,
        AudioSource = 6,
        Camera = 7,
        Animator = 8,
        CharacterController = 9
    };

    class MORBI_API Scene
    {
    public:
        explicit Scene(std::string name = "UntitledScene");

        [[nodiscard]] const std::string& GetName() const noexcept { return m_name; }
        void SetName(std::string name) { m_name = std::move(name); }

        // Entity Lifecycle
        [[nodiscard]] EntityId CreateEntity(std::string_view name = "Entity");
        void DestroyEntity(EntityId entity);
        using EntityDestroyedCallback = std::function<void(EntityId)>;
        void SetEntityDestroyedCallback(EntityDestroyedCallback callback) noexcept { m_entityDestroyedCallback = std::move(callback); }
        using SceneClearedCallback = std::function<void()>;
        void SetSceneClearedCallback(SceneClearedCallback callback) noexcept { m_sceneClearedCallback = std::move(callback); }
        [[nodiscard]] bool IsAlive(EntityId entity) const noexcept;
        [[nodiscard]] std::string_view GetEntityName(EntityId entity) const noexcept;
        void SetEntityName(EntityId entity, std::string_view name);
        [[nodiscard]] EntityId FindEntityByName(std::string_view name) const noexcept;
        [[nodiscard]] u32 GetAliveEntityCount() const noexcept { return m_aliveCount; }

        // Hierarchy Management
        void SetParent(EntityId child, EntityId parent);
        [[nodiscard]] EntityId GetParent(EntityId entity) const noexcept;
        [[nodiscard]] std::vector<EntityId> GetChildren(EntityId parent) const;

        // SoA Sparse-Set Pools Access
        [[nodiscard]] SparseSet<TransformComponent>& Transforms() noexcept { return m_transforms; }
        [[nodiscard]] const SparseSet<TransformComponent>& Transforms() const noexcept { return m_transforms; }

        [[nodiscard]] SparseSet<HierarchyComponent>& Hierarchies() noexcept { return m_hierarchies; }
        [[nodiscard]] const SparseSet<HierarchyComponent>& Hierarchies() const noexcept { return m_hierarchies; }

        [[nodiscard]] SparseSet<MeshRendererComponent>& MeshRenderers() noexcept { return m_meshRenderers; }
        [[nodiscard]] const SparseSet<MeshRendererComponent>& MeshRenderers() const noexcept { return m_meshRenderers; }

        [[nodiscard]] SparseSet<LightComponent>& Lights() noexcept { return m_lights; }
        [[nodiscard]] const SparseSet<LightComponent>& Lights() const noexcept { return m_lights; }

        [[nodiscard]] SparseSet<ScriptComponent>& Scripts() noexcept { return m_scripts; }
        [[nodiscard]] const SparseSet<ScriptComponent>& Scripts() const noexcept { return m_scripts; }

        [[nodiscard]] SparseSet<RigidBodyComponent>& RigidBodies() noexcept { return m_rigidBodies; }
        [[nodiscard]] const SparseSet<RigidBodyComponent>& RigidBodies() const noexcept { return m_rigidBodies; }

        [[nodiscard]] SparseSet<AudioSourceComponent>& AudioSources() noexcept { return m_audioSources; }
        [[nodiscard]] const SparseSet<AudioSourceComponent>& AudioSources() const noexcept { return m_audioSources; }

        [[nodiscard]] SparseSet<CameraComponent>& Cameras() noexcept { return m_cameras; }
        [[nodiscard]] const SparseSet<CameraComponent>& Cameras() const noexcept { return m_cameras; }

        [[nodiscard]] SparseSet<AnimatorComponent>& Animators() noexcept { return m_animators; }
        [[nodiscard]] const SparseSet<AnimatorComponent>& Animators() const noexcept { return m_animators; }

        [[nodiscard]] SparseSet<CharacterControllerComponent>& CharacterControllers() noexcept { return m_characterControllers; }
        [[nodiscard]] const SparseSet<CharacterControllerComponent>& CharacterControllers() const noexcept { return m_characterControllers; }

        [[nodiscard]] SparseSet<NetworkIdentityComponent>& NetworkIdentities() noexcept { return m_networkIdentities; }
        [[nodiscard]] const SparseSet<NetworkIdentityComponent>& NetworkIdentities() const noexcept { return m_networkIdentities; }

        // Compound Collider Sub-Shapes per Entity
        void SetCompoundColliders(EntityId entity, std::span<const CompoundColliderEntry> shapes);
        bool AddCompoundCollider(EntityId entity, const CompoundColliderEntry& shape);
        bool RemoveCompoundColliderAt(EntityId entity, u32 index);
        void ClearCompoundColliders(EntityId entity);
        [[nodiscard]] std::span<const CompoundColliderEntry> GetCompoundColliders(EntityId entity) const noexcept;
        [[nodiscard]] const std::unordered_map<u64, std::vector<CompoundColliderEntry>>& CompoundCollidersMap() const noexcept { return m_compoundColliders; }

        // Authored Physics Joints per Entity
        void SetEntityJoints(EntityId entity, std::span<const PhysicsJointEntry> joints);
        bool AddEntityJoint(EntityId entity, const PhysicsJointEntry& joint);
        bool UpdateEntityJointAt(EntityId entity, u32 index, const PhysicsJointEntry& joint);
        bool RemoveEntityJointAt(EntityId entity, u32 index);
        void ClearEntityJoints(EntityId entity);
        [[nodiscard]] std::span<const PhysicsJointEntry> GetEntityJoints(EntityId entity) const noexcept;
        [[nodiscard]] std::vector<PhysicsJointEntry>* TryGetMutableEntityJoints(EntityId entity) noexcept;
        [[nodiscard]] const std::unordered_map<u64, std::vector<PhysicsJointEntry>>& PhysicsJointsMap() const noexcept { return m_physicsJoints; }
        [[nodiscard]] std::unordered_map<u64, std::vector<PhysicsJointEntry>>& PhysicsJointsMap() noexcept { return m_physicsJoints; }

        [[nodiscard]] std::span<const Math::Mat4> WorldMatrices() const noexcept { return m_worldMatrices; }

        // Zero-Copy Native Arena Access for C# (.NET 8 Span<T>)
        [[nodiscard]] NativeComponentArenaView GetNativeArenaView(ComponentPoolKind kind) noexcept;

        // High-Performance Data-Oriented Systems Update
        void UpdateScriptSystem(f32 deltaTime, Core::TaskScheduler* scheduler = nullptr);
        void UpdateAnimationSystem(f32 deltaTime, Core::TaskScheduler* scheduler = nullptr);
        void UpdateTransformHierarchySystem(Core::TaskScheduler* scheduler = nullptr);

        // JSON Scene Serialization
        bool SaveToJsonFile(const std::filesystem::path& filePath) const;
        bool LoadFromJsonFile(const std::filesystem::path& filePath);
        void Clear();

        // Scene Snapshot & PIE (Play In Editor) Rollback
        struct EntityRecord
        {
            std::string Name;
            u32 Generation = 1;
            bool Alive = false;
        };

        struct SceneSnapshot
        {
            std::string Name;
            std::vector<EntityRecord> EntityRecords;
            std::vector<u32> FreeIndices;
            u32 AliveCount = 0;

            SparseSet<TransformComponent> Transforms;
            SparseSet<HierarchyComponent> Hierarchies;
            SparseSet<MeshRendererComponent> MeshRenderers;
            SparseSet<LightComponent> Lights;
            SparseSet<ScriptComponent> Scripts;
            SparseSet<RigidBodyComponent> RigidBodies;
            SparseSet<AudioSourceComponent> AudioSources;
            SparseSet<CameraComponent> Cameras;
            SparseSet<AnimatorComponent> Animators;
            SparseSet<CharacterControllerComponent> CharacterControllers;
            SparseSet<NetworkIdentityComponent> NetworkIdentities;
            std::unordered_map<u64, std::vector<CompoundColliderEntry>> CompoundColliders;
            std::unordered_map<u64, std::vector<PhysicsJointEntry>> PhysicsJoints;
            std::vector<Math::Mat4> WorldMatrices;
        };

        [[nodiscard]] SceneSnapshot CreateSnapshot() const;
        void RestoreSnapshot(const SceneSnapshot& snapshot);

    private:
        std::string m_name;
        std::vector<EntityRecord> m_entityRecords;
        std::vector<u32> m_freeIndices;
        u32 m_aliveCount = 0;

        SparseSet<TransformComponent> m_transforms;
        SparseSet<HierarchyComponent> m_hierarchies;
        SparseSet<MeshRendererComponent> m_meshRenderers;
        SparseSet<LightComponent> m_lights;
        SparseSet<ScriptComponent> m_scripts;
        SparseSet<RigidBodyComponent> m_rigidBodies;
        SparseSet<AudioSourceComponent> m_audioSources;
        SparseSet<CameraComponent> m_cameras;
        SparseSet<AnimatorComponent> m_animators;
        SparseSet<CharacterControllerComponent> m_characterControllers;
        SparseSet<NetworkIdentityComponent> m_networkIdentities;
        std::unordered_map<u64, std::vector<CompoundColliderEntry>> m_compoundColliders;
        std::unordered_map<u64, std::vector<PhysicsJointEntry>> m_physicsJoints;

        // Contiguous SoA output buffer aligned with m_transforms.RawComponents()
        std::vector<Math::Mat4> m_worldMatrices;

        EntityDestroyedCallback m_entityDestroyedCallback{};
        SceneClearedCallback m_sceneClearedCallback{};
    };
}

// ============================================================================
// C# (.NET 8 UnmanagedCallersOnly / P/Invoke) Zero-Copy Scene & Arena Exports
// ============================================================================
#if defined(_WIN32)
    #define MORBI_SCENE_C_API extern "C" __declspec(dllexport)
#else
    #define MORBI_SCENE_C_API extern "C" __attribute__((visibility("default")))
#endif

MORBI_SCENE_C_API Morbi::u64 Morbi_Scene_CreateEntity(Morbi::Scene::Scene* scene, const char* name);
MORBI_SCENE_C_API void Morbi_Scene_DestroyEntity(Morbi::Scene::Scene* scene, Morbi::u64 packedEntityId);
MORBI_SCENE_C_API void Morbi_Scene_GetNativeArenaView(
    Morbi::Scene::Scene* scene,
    Morbi::u8 poolKind,
    Morbi::Scene::NativeComponentArenaView* outView);
