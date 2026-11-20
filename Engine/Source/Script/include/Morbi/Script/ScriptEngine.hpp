#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "Morbi/Audio/AudioWorld.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Physics/PhysicsWorld.hpp"
#include "Morbi/Scene/Scene.hpp"

namespace Morbi::IO
{
    class PathService;
}

namespace Morbi::Platform
{
    class InputState;
}

namespace Morbi::UI
{
    class GameUIRenderer;
}

namespace Morbi::Script
{
    struct NativeQueryFilter
    {
        u64 IgnoreEntityPacked = 0;
        u32 LayerMask = 0xFFFFFFFFu;
        u8 IncludeTriggers = 0;
        u8 IgnoreStatic = 0;
        u8 IgnoreKinematic = 0;
        u8 IgnoreDynamic = 0;
    };
    static_assert(sizeof(NativeQueryFilter) == 16);
    static_assert(std::is_standard_layout_v<NativeQueryFilter> && std::is_trivially_copyable_v<NativeQueryFilter>);

    struct NativeRaycastHit
    {
        u64 HitEntityPacked = 0;
        Math::Vec3 Point{};
        Math::Vec3 Normal{};
        f32 Distance = 0.0f;
        u8 Shape = 0;
        u8 Reserved0 = 0;
        u8 Reserved1 = 0;
        u8 Reserved2 = 0;
    };
    static_assert(sizeof(NativeRaycastHit) == 40);
    static_assert(std::is_standard_layout_v<NativeRaycastHit> && std::is_trivially_copyable_v<NativeRaycastHit>);

    struct NativeOverlapHit
    {
        u64 EntityPacked = 0;
        Math::Vec3 ContactPoint{};
        Math::Vec3 PenetrationAxis{};
        f32 PenetrationDepth = 0.0f;
        u8 Shape = 0;
        u8 Reserved0 = 0;
        u8 Reserved1 = 0;
        u8 Reserved2 = 0;
    };
    static_assert(sizeof(NativeOverlapHit) == 40);
    static_assert(std::is_standard_layout_v<NativeOverlapHit> && std::is_trivially_copyable_v<NativeOverlapHit>);

    struct NativeCollisionEvent
    {
        u64 EntityAPacked = 0;
        u64 EntityBPacked = 0;
        Math::Vec3 ContactPoint{};
        Math::Vec3 ContactNormal{};
        Math::Vec3 RelativeVelocity{};
        f32 PenetrationDepth = 0.0f;
        u8 ShapeA = 0;
        u8 ShapeB = 0;
        u8 EventType = 0; // 0 = Enter, 1 = Stay, 2 = Exit
        u8 IsTrigger = 0;
        u32 Reserved = 0;
    };
    static_assert(sizeof(NativeCollisionEvent) == 64);
    static_assert(std::is_standard_layout_v<NativeCollisionEvent> && std::is_trivially_copyable_v<NativeCollisionEvent>);

    struct NativeCharacterState
    {
        u64 GroundEntityPacked = 0;
        Math::Vec3 Velocity{};
        f32 Speed = 0.0f;
        Math::Vec3 GroundNormal{ 0.0f, 1.0f, 0.0f };
        u8 GroundState = 3; // 0=OnGround, 1=OnSteepGround, 2=NotSupported, 3=InAir
        u8 IsGrounded = 0;
        u8 IsSliding = 0;
        u8 Reserved = 0;
        Math::Vec3 GroundVelocity{};
        u32 ReservedPad = 0;
    };
    static_assert(sizeof(NativeCharacterState) == 56);
    static_assert(std::is_standard_layout_v<NativeCharacterState> && std::is_trivially_copyable_v<NativeCharacterState>);

    struct NativeJointDesc
    {
        u64 EntityAPacked = 0ull;
        u64 EntityBPacked = 0ull;
        Math::Vec3 AnchorA{};
        f32 MinDistance = -1.0f;
        Math::Vec3 AnchorB{};
        f32 MaxDistance = -1.0f;
        Math::Vec3 Axis{ 0.0f, 1.0f, 0.0f };
        f32 SpringFrequencyHz = 0.0f;
        f32 SpringDampingRatio = 0.0f;
        f32 MinLimit = -Math::kPi;
        f32 MaxLimit = Math::kPi;
        f32 MaxFriction = 0.0f;
        f32 BreakForce = 0.0f;
        f32 BreakTorque = 0.0f;
        u8 Type = 0u;
        u8 AutoDetectAnchors = 1u;
        u8 Enabled = 1u;
        u8 Reserved0 = 0u;
        u32 Reserved1 = 0u;
    };
    static_assert(sizeof(NativeJointDesc) == 96);
    static_assert(std::is_standard_layout_v<NativeJointDesc> && std::is_trivially_copyable_v<NativeJointDesc>);

    struct NativeColliderShapeDesc
    {
        Math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
        u32 CollisionLayer = 1u;
        Math::Vec3 LocalOffset{};
        u32 CollisionMask = 0xFFFFFFFFu;
        Math::Quat LocalRotation = Math::Quat::Identity();
        u8 Shape = 0u;
        u8 IsTrigger = 0u;
        u8 Reserved0 = 0u;
        u8 Reserved1 = 0u;
        u32 Reserved2 = 0u;
    };
    static_assert(sizeof(NativeColliderShapeDesc) == 56);
    static_assert(std::is_standard_layout_v<NativeColliderShapeDesc> && std::is_trivially_copyable_v<NativeColliderShapeDesc>);

    // Function pointer table passed from C++ to C# during initialization.
    // C# invokes these via zero-overhead `delegate* unmanaged[Cdecl]<...>` pointers!
    struct NativeEngineFunctionTable
    {
        u32 StructSize = sizeof(NativeEngineFunctionTable);
        u32 ApiVersion = 7; // Phase 8: Multi-World C# Gameplay Networking API

        // Core & Logging
        void (__cdecl* LogInfo)(const char* category, const char* message) = nullptr;
        void (__cdecl* GetComponentArenaView)(u8 poolKind, Scene::NativeComponentArenaView* outView) = nullptr;

        // Physics
        bool (__cdecl* AddLinearImpulse)(u64 packedEntityId, const Math::Vec3* impulse) = nullptr;
        bool (__cdecl* SetLinearVelocity)(u64 packedEntityId, const Math::Vec3* velocity) = nullptr;
        bool (__cdecl* Raycast)(const Math::Vec3* origin, const Math::Vec3* direction, f32 maxDistance, Math::Vec3* outHitPoint) = nullptr;

        // Audio & Engine
        bool (__cdecl* PlaySpatialSound3D)(const char* soundKey, const Math::Vec3* position, f32 volume, f32 pitch) = nullptr;
        void (__cdecl* SetVSync)(bool enabled) = nullptr;

        // UI Data-Binding
        void (__cdecl* SetGameUIProperty)(const char* propertyKey, f32 value, const char* stringValue) = nullptr;
        void (__cdecl* TriggerGameUIEvent)(const char* eventName, i32 eventPayload) = nullptr;

        // Scene Management (Phase 24)
        u64 (__cdecl* FindEntityByName)(const char* name) = nullptr;
        u64 (__cdecl* CreateEntity)(const char* name) = nullptr;
        bool (__cdecl* DestroyEntity)(u64 packedEntityId) = nullptr;
        bool (__cdecl* IsEntityAlive)(u64 packedEntityId) = nullptr;
        bool (__cdecl* SetEntityParent)(u64 childPackedId, u64 parentPackedId) = nullptr;

        // Animation / AnimatorComponent (Phase 24)
        bool (__cdecl* PlayAnimation)(u64 packedEntityId) = nullptr;
        bool (__cdecl* PauseAnimation)(u64 packedEntityId) = nullptr;
        bool (__cdecl* StopAnimation)(u64 packedEntityId) = nullptr;
        bool (__cdecl* SetAnimationClip)(u64 packedEntityId, const char* clipPath) = nullptr;
        bool (__cdecl* SetAnimationSpeed)(u64 packedEntityId, f32 speed) = nullptr;
        bool (__cdecl* IsAnimationPlaying)(u64 packedEntityId) = nullptr;

        // Input Management (Phase 24)
        bool (__cdecl* IsKeyDown)(u32 scancode) = nullptr;
        bool (__cdecl* WasKeyPressed)(u32 scancode) = nullptr;
        bool (__cdecl* WasKeyReleased)(u32 scancode) = nullptr;
        bool (__cdecl* IsMouseButtonDown)(u8 button) = nullptr;
        bool (__cdecl* WasMouseButtonPressed)(u8 button) = nullptr;
        void (__cdecl* GetMousePosition)(f32* outX, f32* outY) = nullptr;
        void (__cdecl* GetMouseDelta)(f32* outDx, f32* outDy) = nullptr;
        void (__cdecl* GetMouseWheelDelta)(f32* outWheelX, f32* outWheelY) = nullptr;

        // Extended Mouse Capture & 3D Axis Input (X, Y, Z / Scroll & All Mouse Buttons)
        bool (__cdecl* WasMouseButtonReleased)(u8 button) = nullptr;
        void (__cdecl* GetMouseDelta3D)(f32* outDx, f32* outDy, f32* outDz) = nullptr;
        void (__cdecl* GetMousePosition3D)(f32* outX, f32* outY, f32* outZ) = nullptr;
        void (__cdecl* SetMouseCaptured)(bool captured) = nullptr;
        bool (__cdecl* IsMouseCaptured)() = nullptr;
        void (__cdecl* SetCursorVisible)(bool visible) = nullptr;
        bool (__cdecl* IsCursorVisible)() = nullptr;

        // Complete C# Entity, Component & Scene Bindings (v3)
        u32 (__cdecl* GetEntityName)(u64 packedEntityId, char* outBuffer, u32 bufferSize) = nullptr;
        bool (__cdecl* SetEntityName)(u64 packedEntityId, const char* name) = nullptr;
        u64 (__cdecl* GetEntityParent)(u64 packedEntityId) = nullptr;
        u32 (__cdecl* GetAliveEntityCount)() = nullptr;
        bool (__cdecl* HasComponent)(u64 packedEntityId, u8 poolKind) = nullptr;
        bool (__cdecl* AddComponent)(u64 packedEntityId, u8 poolKind) = nullptr;
        bool (__cdecl* RemoveComponent)(u64 packedEntityId, u8 poolKind) = nullptr;
        bool (__cdecl* LoadScene)(const char* filePath) = nullptr;
        bool (__cdecl* SaveScene)(const char* filePath) = nullptr;
        void (__cdecl* ClearScene)() = nullptr;

        // Animation CrossFade & Pose Blending
        bool (__cdecl* CrossFadeAnimationClip)(u64 packedEntityId, const char* clipPath, f32 blendDurationSeconds) = nullptr;
        bool (__cdecl* IsAnimationBlending)(u64 packedEntityId) = nullptr;

        // Virtual File System (PathService) Resolution
        u32 (__cdecl* ResolveVirtualPath)(const char* virtualPath, char* outBuffer, u32 bufferSize) = nullptr;

        // Production Jolt Physics Bridge (v4)
        bool (__cdecl* SyncRigidBody)(u64 packedEntityId) = nullptr;
        void (__cdecl* MarkPhysicsTransformDirty)(u64 packedEntityId) = nullptr;
        bool (__cdecl* AddForce)(u64 packedEntityId, const Math::Vec3* force, u8 mode) = nullptr;
        bool (__cdecl* AddForceAtPosition)(u64 packedEntityId, const Math::Vec3* force, const Math::Vec3* position, u8 mode) = nullptr;
        bool (__cdecl* AddTorque)(u64 packedEntityId, const Math::Vec3* torque, u8 mode) = nullptr;
        bool (__cdecl* GetLinearVelocity)(u64 packedEntityId, Math::Vec3* outVelocity) = nullptr;
        bool (__cdecl* SetAngularVelocity)(u64 packedEntityId, const Math::Vec3* angularVelocity) = nullptr;
        bool (__cdecl* GetAngularVelocity)(u64 packedEntityId, Math::Vec3* outAngularVelocity) = nullptr;
        bool (__cdecl* WakeUpBody)(u64 packedEntityId) = nullptr;
        bool (__cdecl* SleepBody)(u64 packedEntityId) = nullptr;
        bool (__cdecl* IsBodySleeping)(u64 packedEntityId) = nullptr;
        bool (__cdecl* TeleportBody)(u64 packedEntityId, const Math::Vec3* position, const Math::Quat* rotation) = nullptr;
        void (__cdecl* SetGravity)(const Math::Vec3* gravity) = nullptr;
        void (__cdecl* GetGravity)(Math::Vec3* outGravity) = nullptr;
        void (__cdecl* SetLayerCollision)(u32 layerA, u32 layerB, bool shouldCollide) = nullptr;
        bool (__cdecl* GetLayerCollision)(u32 layerA, u32 layerB) = nullptr;
        bool (__cdecl* RaycastEx)(const Math::Vec3* origin, const Math::Vec3* direction, f32 maxDistance, const NativeQueryFilter* filter, NativeRaycastHit* outHit) = nullptr;
        u32 (__cdecl* RaycastAllEx)(const Math::Vec3* origin, const Math::Vec3* direction, f32 maxDistance, const NativeQueryFilter* filter, NativeRaycastHit* outHits, u32 maxHits) = nullptr;
        bool (__cdecl* SphereCastEx)(const Math::Vec3* origin, f32 radius, const Math::Vec3* direction, f32 maxDistance, const NativeQueryFilter* filter, NativeRaycastHit* outHit) = nullptr;
        bool (__cdecl* BoxCastEx)(const Math::Vec3* origin, const Math::Vec3* halfExtents, const Math::Quat* orientation, const Math::Vec3* direction, f32 maxDistance, const NativeQueryFilter* filter, NativeRaycastHit* outHit) = nullptr;
        bool (__cdecl* CapsuleCastEx)(const Math::Vec3* origin, f32 radius, f32 halfHeight, const Math::Quat* orientation, const Math::Vec3* direction, f32 maxDistance, const NativeQueryFilter* filter, NativeRaycastHit* outHit) = nullptr;
        u32 (__cdecl* OverlapSphereEx)(const Math::Vec3* center, f32 radius, const NativeQueryFilter* filter, NativeOverlapHit* outHits, u32 maxHits) = nullptr;
        u32 (__cdecl* OverlapBoxEx)(const Math::Vec3* center, const Math::Vec3* halfExtents, const Math::Quat* orientation, const NativeQueryFilter* filter, NativeOverlapHit* outHits, u32 maxHits) = nullptr;
        u32 (__cdecl* OverlapCapsuleEx)(const Math::Vec3* center, f32 radius, f32 halfHeight, const Math::Quat* orientation, const NativeQueryFilter* filter, NativeOverlapHit* outHits, u32 maxHits) = nullptr;

        // Character Physics Bridge (Phase 5)
        bool (__cdecl* SyncCharacterController)(u64 packedEntityId) = nullptr;
        bool (__cdecl* MoveCharacter)(u64 packedEntityId, const Math::Vec3* displacement) = nullptr;
        bool (__cdecl* JumpCharacter)(u64 packedEntityId, f32 jumpSpeed) = nullptr;
        bool (__cdecl* SetCharacterVelocity)(u64 packedEntityId, const Math::Vec3* velocity) = nullptr;
        bool (__cdecl* GetCharacterVelocity)(u64 packedEntityId, Math::Vec3* outVelocity) = nullptr;
        bool (__cdecl* GetCharacterState)(u64 packedEntityId, NativeCharacterState* outState) = nullptr;
        bool (__cdecl* TeleportCharacter)(u64 packedEntityId, const Math::Vec3* position) = nullptr;

        // Joints & Compound Collider Bridge (ApiVersion 5)
        u64  (__cdecl* CreateJoint)(const NativeJointDesc* desc) = nullptr;
        bool (__cdecl* UpdateJoint)(u64 packedHandle, const NativeJointDesc* desc) = nullptr;
        bool (__cdecl* DestroyJoint)(u64 packedHandle) = nullptr;
        bool (__cdecl* IsJointValid)(u64 packedHandle) = nullptr;
        bool (__cdecl* GetJointDesc)(u64 packedHandle, NativeJointDesc* outDesc) = nullptr;
        bool (__cdecl* GetJointState)(u64 packedHandle, f32* outPositionOrAngle, f32* outForce, f32* outTorque) = nullptr;
        u32  (__cdecl* GetEntityJointCount)(u64 packedEntityId) = nullptr;
        u64  (__cdecl* GetEntityJointHandle)(u64 packedEntityId, u32 index) = nullptr;
        bool (__cdecl* SetEntityCompoundColliders)(u64 packedEntityId, const NativeColliderShapeDesc* shapes, u32 count) = nullptr;
        u32  (__cdecl* GetEntityCompoundColliderCount)(u64 packedEntityId) = nullptr;
        bool (__cdecl* GetEntityCompoundCollider)(u64 packedEntityId, u32 index, NativeColliderShapeDesc* outShape) = nullptr;
        bool (__cdecl* ClearEntityCompoundColliders)(u64 packedEntityId) = nullptr;

        // Diagnostics & Unhandled Managed Exception Bridge (ApiVersion 6)
        void (__cdecl* LogFatalException)(const char* exceptionType, const char* message, const char* stackTrace) = nullptr;
        u8   (__cdecl* IsDebugMode)() = nullptr;

        // Multi-World & Gameplay Networking Bridge (ApiVersion 7 / Phase 8)
        u32  (__cdecl* GetActiveWorldId)() = nullptr;
        u8   (__cdecl* NetGetObjectState)(
            u32 worldId,
            u64 netIdPacked,
            u64* outSceneEntityPacked,
            u64* outOwnerPlayerId,
            u8* outIsServer,
            u8* outIsClient,
            u8* outIsOwner) = nullptr;
        u8   (__cdecl* NetResolveEntityNetworkId)(
            u32 worldId,
            u64 sceneEntityPacked,
            u64* outNetIdPacked) = nullptr;
        u8   (__cdecl* NetSendManagedRpc)(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            u32 methodHash,
            u8 isServerRpc,
            u8 flags,
            const u8* argsBytes,
            u32 argsLen) = nullptr;
        u8   (__cdecl* NetSyncManagedProperties)(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            const u8* propsBytes,
            u32 propsLen) = nullptr;
    };

    struct NetworkBridgeCallbacks
    {
        std::function<bool(u32 worldId, u64 netIdPacked, u64* outSceneEntityPacked, u64* outOwnerPlayerId, u8* outIsServer, u8* outIsClient, u8* outIsOwner)> GetObjectState;
        std::function<bool(u32 worldId, u64 sceneEntityPacked, u64* outNetIdPacked)> ResolveEntityNetworkId;
        std::function<bool(u32 worldId, u64 netIdPacked, u32 behaviourHash, u32 methodHash, u8 isServerRpc, u8 flags, std::span<const u8> argsPayload)> SendManagedRpc;
        std::function<bool(u32 worldId, u64 netIdPacked, u32 behaviourHash, std::span<const u8> propsPayload)> SyncManagedProperties;
    };

    struct ScriptHostDesc
    {
        std::filesystem::path ManagedAssemblyDir;
        std::string HostAssemblyName = "Morbi.Engine";
    };

    struct ScriptCompileResult
    {
        bool Success = false;
        u32 CompiledFileCount = 0;
        std::string Diagnostics;
    };

    class MORBI_API ScriptEngine
    {
    public:
        ScriptEngine();
        ~ScriptEngine();

        ScriptEngine(const ScriptEngine&) = delete;
        ScriptEngine& operator=(const ScriptEngine&) = delete;

        [[nodiscard]] Core::Result<bool, std::string> Initialize(
            const ScriptHostDesc& desc,
            Scene::Scene& activeScene,
            Physics::PhysicsWorld& physicsWorld,
            Audio::AudioWorld& audioWorld);

        void Shutdown();

        // Binds GameUIRenderer for zero-lag C# data-binding and HUD events
        void BindGameUI(UI::GameUIRenderer* renderer);

        // Binds Platform::InputState for native C# SDL3 input queries (IsKeyDown, MousePosition, etc.)
        void BindInput(Platform::InputState* input);

        // Binds EngineRuntime scene loader so C# Scene.LoadScene/Clear stays synced with GPU/Physics/UI
        using SceneHandlerCallback = std::function<bool(std::string_view scenePath, bool clearOnly)>;
        void BindSceneHandler(SceneHandlerCallback callback);

        // Binds Virtual File System (PathService) for VFS URI & relative asset resolution
        void BindVFS(IO::PathService* vfs);

        // Multi-World & Networking Interop (Phase 8)
        void SetActiveWorldContext(
            u32 worldId,
            Scene::Scene* activeScene,
            Physics::PhysicsWorld* physicsWorld,
            Audio::AudioWorld* audioWorld);
        [[nodiscard]] u32 GetActiveWorldId() const noexcept;
        void BindNetworkBridge(NetworkBridgeCallbacks callbacks);

        bool NotifyNetworkSpawn(
            u32 worldId,
            u64 netIdPacked,
            u64 sceneEntityPacked,
            u64 ownerPlayerId,
            bool isServer,
            bool isClient,
            bool isOwner);
        bool NotifyNetworkDespawn(u32 worldId, u64 netIdPacked);
        bool NotifyNetworkOwnershipChanged(
            u32 worldId,
            u64 netIdPacked,
            u64 previousOwnerPlayerId,
            u64 newOwnerPlayerId,
            bool isOwner);
        bool NotifyNetworkWorldChanged(u32 previousWorldId, u32 newWorldId, u64 netIdPacked);
        bool NotifyWorldDestroyed(u32 worldId);
        bool DispatchManagedPropertySync(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            std::span<const u8> propsBytes);
        bool DispatchManagedRpc(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            u32 methodHash,
            u64 senderPlayerId,
            bool isServerSide,
            std::span<const u8> argsBytes);
        bool SyncManagedNetworkProperties(u32 worldId);

        // Loads or hot-reloads a gameplay assembly inside a Collectible AssemblyLoadContext (isCollectible: true)
        [[nodiscard]] Core::Result<bool, std::string> LoadOrHotReloadAssembly(const std::filesystem::path& assemblyPath);

        // Compiles raw project Scripts/*.cs via embedded Roslyn (in-process) into outputDllPath and optionally hot-reloads
        [[nodiscard]] ScriptCompileResult CompileProjectScripts(
            const std::filesystem::path& scriptsDir,
            const std::filesystem::path& outputDllPath,
            bool loadAfterCompile = true,
            bool releaseOptimize = false);

        // Unloads the active Collectible AssemblyLoadContext and forces GC cleanup
        bool UnloadActiveScriptContext();

        // Executes C# OnUpdate(deltaTime) across all managed scripts, operating zero-copy on Span<TransformComponent>
        bool TickManagedScripts(f32 deltaTime);

        // Executes C# OnFixedUpdate(fixedDeltaTime) right before each physics substep
        bool FixedTickManagedScripts(f32 fixedDeltaTime);

        // Dispatches Collision/Trigger Enter/Stay/Exit callbacks from the latest physics step to entity-bound scripts
        bool DispatchPhysicsEvents();

        // Notifies managed host when a native ECS entity is destroyed so attached scripts run OnDestroy() cleanly
        bool NotifyEntityDestroyed(Scene::EntityId entity);

        // Notifies managed host when scene is cleared / reset so entity-bound scripts run OnDestroy() and avoid stale ID collisions
        bool NotifySceneCleared();

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] u32 GetHotReloadGeneration() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
