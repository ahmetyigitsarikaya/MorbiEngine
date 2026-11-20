#pragma once

#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "Morbi/Network/Input/InputCommandPipeline.hpp"
#include "Morbi/Network/Network.hpp"
#include "Morbi/Network/Replication/ReplicationManager.hpp"
#include "Morbi/Network/RPC/RpcRouter.hpp"
#include "Morbi/Runtime/RuntimeProfile.hpp"
#include "Morbi/Scene/Scene.hpp"

namespace Morbi::Physics
{
    class PhysicsWorld;
}

namespace Morbi::Runtime
{
    namespace BuiltinRpcIds
    {
        inline constexpr u32 kRequestDespawnSelf = 101;
        inline constexpr u32 kPlayerEmoteAction = 102;
        inline constexpr u32 kServerOnlyAdminReset = 900;
        inline constexpr u32 kManagedServerRpc = 5001;
        inline constexpr u32 kManagedClientRpc = 5002;
    }

    struct ClientPredictionConfig
    {
        bool EnablePrediction = true;
        bool EnableRemoteInterpolation = true;
        f64 InterpolationDelaySec = 0.08;
        f32 ReconciliationIgnoreThreshold = 0.01f;
        f32 ReconciliationSnapThreshold = 1.5f;
        f32 VisualSmoothingRate = 18.0f;
        usize InputHistoryCapacity = Network::ClientInputHistory::kDefaultCapacity;
    };

    struct ClientPredictionStats
    {
        u64 PredictedInputsSubmitted = 0;
        u64 ReconciliationsPerformed = 0;
        u64 InputsReplayed = 0;
        u64 CorrectionsApplied = 0;
        u64 LargeSnaps = 0;
        f32 MaxCorrectionDistance = 0.0f;
    };

    /// Explicit Scene-side adapter translating between ECS Scene::Scene entities/components
    /// and Morbi.Network replication contracts without leaking raw ECS memory layouts.
    class SceneReplicationBridge final
    {
    public:
        static bool SceneToReplicatedTransform(
            const Scene::TransformComponent& src,
            Network::ReplicatedTransformData& outDst) noexcept;

        static bool ReplicatedToSceneTransform(
            const Network::ReplicatedTransformData& src,
            Scene::TransformComponent& outDst) noexcept;

        void Initialize(
            bool isServer,
            const ReplicationLaunchConfig& config,
            Network::ConnectionManager& net);

        void Shutdown(Scene::Scene& scene, Network::ConnectionManager& net);

        // Server-side entity lifecycle & ownership mapping
        [[nodiscard]] Network::NetworkId SpawnReplicatedEntity(
            Scene::Scene& scene,
            Network::SessionRegistry& sessions,
            const Network::ReplicatedEntitySpawnDesc& desc,
            u64 serverTick);

        bool DespawnReplicatedEntity(
            Scene::Scene& scene,
            Network::SessionRegistry& sessions,
            Network::NetworkId netId);

        void OnSceneEntityDestroyed(
            Scene::EntityId eid,
            Network::SessionRegistry& sessions);

        void OnSceneCleared(Network::SessionRegistry& sessions);

        /// Registers explicitly opted-in static scene entities (`NetworkIdentityComponent` with `Replicated != 0`)
        /// deterministically by `StaticNetId`. Rejects `StaticNetId == 0` or duplicates without partial registration.
        bool RegisterStaticSceneEntities(Scene::Scene& scene, u64 serverTick = 0);

        void OnServerClientAuthenticated(
            const Network::ConnectionInfo& info,
            bool isReconnect,
            Scene::Scene& scene,
            Network::ConnectionManager& net,
            u64 serverTick);

        void OnServerSessionClosed(
            Network::SessionId sessionId,
            Scene::Scene& scene,
            Network::ConnectionManager& net);

        /// Drains and applies authoritative client input commands into Scene/Physics prior to PhysicsWorld::StepSimulation.
        void ProcessServerInputs(
            f32 fixedDt,
            u64 serverTick,
            Scene::Scene& scene,
            Physics::PhysicsWorld* physics);

        void StepServerTick(
            f32 fixedDt,
            u64 serverTick,
            u32 simulationTickRateHz,
            Scene::Scene& scene,
            Physics::PhysicsWorld* physics,
            Network::ConnectionManager& net);

        // Message ingress (Server & Client)
        void HandleNetworkMessage(
            const Network::NetworkMessageEvent& ev,
            Scene::Scene& scene,
            Network::ConnectionManager& net);

        // Client-side helper to submit an input command cleanly
        bool ClientSendInput(
            Network::ConnectionManager& net,
            Network::ConnectionHandle serverConnection,
            const Network::PlayerInputCommand& cmd);

        // Client-side prediction, history buffering, reconciliation, and remote interpolation
        void ConfigurePrediction(const ClientPredictionConfig& config) noexcept;
        [[nodiscard]] const ClientPredictionConfig& GetPredictionConfig() const noexcept { return m_predictionConfig; }
        void SetLocalPlayerId(Network::PlayerId playerId) noexcept { m_localPlayerId = playerId; }
        [[nodiscard]] Network::PlayerId GetLocalPlayerId() const noexcept { return m_localPlayerId; }
        [[nodiscard]] Network::NetworkId GetLocalOwnedNetworkId() const noexcept { return m_localOwnedNetId; }

        void PrepareClientForReconnect() noexcept;

        bool ClientPredictAndSendInput(
            f32 fixedDt,
            const Network::PlayerInputCommand& partialCmd,
            Scene::Scene& scene,
            Physics::PhysicsWorld* clientPhysics,
            Network::ConnectionManager& net,
            Network::ConnectionHandle serverConnection);

        void TickClientPredictionAndInterpolation(
            f32 frameDt,
            Scene::Scene& scene,
            Network::ConnectionManager& net,
            Network::ConnectionHandle serverConnection,
            Physics::PhysicsWorld* clientPhysics = nullptr);

        void TickClientMaintenance(
            Network::ConnectionManager& net,
            Network::ConnectionHandle serverConnection);

        [[nodiscard]] const Network::ClientInputHistory& GetClientInputHistory() const noexcept { return m_clientInputHistory; }
        [[nodiscard]] const Network::RemoteInterpolationBuffer& GetRemoteInterpolation() const noexcept { return m_remoteInterp; }
        [[nodiscard]] const ClientPredictionStats& GetPredictionStats() const noexcept { return m_predictionStats; }
        [[nodiscard]] Math::Vec3 GetVisualCorrectionOffset() const noexcept { return m_visualCorrectionOffset; }

        // Phase 8: Managed C# Gameplay Networking Bridge Hooks
        using ManagedRpcCallback = std::function<bool(
            Network::WorldInstanceId worldId,
            Network::NetworkId netId,
            u32 behaviourHash,
            u32 methodHash,
            Network::PlayerId senderPlayerId,
            bool isServerSide,
            std::span<const u8> argsBytes)>;
        using ManagedPropertySyncCallback = std::function<bool(
            Network::WorldInstanceId worldId,
            Network::NetworkId netId,
            u32 behaviourHash,
            std::span<const u8> propsBytes)>;
        using NetworkSpawnLifecycleCallback = std::function<void(
            Network::WorldInstanceId worldId,
            Network::NetworkId netId,
            Scene::EntityId sceneEntity,
            Network::PlayerId ownerPlayerId,
            bool isServer,
            bool isClient,
            bool isOwner)>;
        using NetworkDespawnLifecycleCallback = std::function<void(
            Network::WorldInstanceId worldId,
            Network::NetworkId netId)>;

        void SetOnManagedRpc(ManagedRpcCallback cb) noexcept { m_onManagedRpc = std::move(cb); }
        void SetOnManagedPropertySync(ManagedPropertySyncCallback cb) noexcept { m_onManagedPropertySync = std::move(cb); }
        void SetOnNetworkSpawn(NetworkSpawnLifecycleCallback cb) noexcept { m_onNetworkSpawn = std::move(cb); }
        void SetOnNetworkDespawn(NetworkDespawnLifecycleCallback cb) noexcept { m_onNetworkDespawn = std::move(cb); }

        bool SendManagedRpc(
            Network::WorldInstanceId worldId,
            Network::NetworkId targetNetId,
            u32 behaviourHash,
            u32 methodHash,
            bool isServerRpc,
            u8 flags,
            std::span<const u8> argsBytes,
            Network::ConnectionManager& net,
            Network::ConnectionHandle clientTargetServerConn = Network::kInvalidConnectionHandle);

        bool SyncManagedProperties(
            Network::WorldInstanceId worldId,
            Network::NetworkId targetNetId,
            u32 behaviourHash,
            std::span<const u8> propsBytes,
            Network::ConnectionManager& net);

        [[nodiscard]] Scene::EntityId ResolveSceneEntity(Network::NetworkId netId) const noexcept;
        [[nodiscard]] Network::NetworkId ResolveNetworkId(Scene::EntityId eid) const noexcept;
        [[nodiscard]] Network::NetworkId ResolveStaticNetworkId(u64 staticNetId) const noexcept;

        [[nodiscard]] Network::ReplicationManager& GetServerReplication() noexcept { return m_serverRep; }
        [[nodiscard]] const Network::ReplicationManager& GetServerReplication() const noexcept { return m_serverRep; }
        [[nodiscard]] Network::ClientReplicationApplier& GetClientApplier() noexcept { return m_clientApplier; }
        [[nodiscard]] const Network::ClientReplicationApplier& GetClientApplier() const noexcept { return m_clientApplier; }
        [[nodiscard]] Network::ServerInputPipeline& GetInputPipeline() noexcept { return m_inputPipeline; }
        [[nodiscard]] const Network::ServerInputPipeline& GetInputPipeline() const noexcept { return m_inputPipeline; }
        [[nodiscard]] Network::RpcRouter& GetRpcRouter() noexcept { return m_rpcRouter; }
        [[nodiscard]] const Network::RpcRouter& GetRpcRouter() const noexcept { return m_rpcRouter; }
        [[nodiscard]] const ReplicationLaunchConfig& GetConfig() const noexcept { return m_config; }
        [[nodiscard]] u64 GetLastPlayerActionRpcCount() const noexcept { return m_playerActionRpcCount; }
        [[nodiscard]] Network::NetworkId GetLocalOwnedNetId() const noexcept { return m_localOwnedNetId; }

    private:
        void ApplyServerSnapshotOnClient(
            const Network::NetworkMessageEvent& ev,
            Scene::Scene& scene,
            Network::ConnectionManager& net);

        void ApplyServerKeyframeChunkOnClient(
            const Network::NetworkMessageEvent& ev,
            Scene::Scene& scene,
            Network::ConnectionManager& net,
            u64 nowMs);

        void CommitClientApplierOutcome(
            bool applied,
            const std::vector<Network::ClientReplicationEvent>& events,
            const std::optional<Network::SnapshotAckPacket>& ackOpt,
            const std::optional<Network::ResyncRequestPacket>& resyncOpt,
            Network::ConnectionHandle serverConn,
            Scene::Scene& scene,
            Network::ConnectionManager& net);

        void SendClientResyncRequest(
            Network::ConnectionHandle serverConn,
            Network::ConnectionManager& net,
            const Network::ResyncRequestPacket& req);

        void RegisterBuiltinRpcs(Scene::Scene& scene, Network::ConnectionManager& net);

        bool m_initialized = false;
        bool m_isServer = false;
        ReplicationLaunchConfig m_config{};
        u32 m_spawnCounter = 0;
        u64 m_playerActionRpcCount = 0;
        u64 m_lastInputProcessedTick = static_cast<u64>(-1);

        Network::ReplicationManager m_serverRep;
        Network::ClientReplicationApplier m_clientApplier;
        Network::ServerInputPipeline m_inputPipeline;
        Network::RpcRouter m_rpcRouter;

        std::unordered_map<u64, Scene::EntityId> m_netToScene;
        std::unordered_map<u64, Network::NetworkId> m_sceneToNet;
        std::unordered_map<u64, Network::NetworkId> m_staticToNet;
        std::unordered_map<u64, u64> m_netToStatic;
        std::vector<Network::PlayerInputCommand> m_scratchInputs;

        // Client prediction & reconciliation state
        ClientPredictionConfig m_predictionConfig{};
        ClientPredictionStats m_predictionStats{};
        Network::PlayerId m_localPlayerId = Network::kInvalidPlayerId;
        Network::NetworkId m_localOwnedNetId = Network::kInvalidNetworkId;
        Network::ClientInputHistory m_clientInputHistory;
        Network::RemoteInterpolationBuffer m_remoteInterp;
        std::vector<Network::ClientInputHistoryEntry> m_scratchHistory;
        Physics::PhysicsWorld* m_clientPhysics = nullptr;
        Math::Vec3 m_logicalPredictedPos = Math::Vec3::Zero();
        Math::Vec3 m_visualCorrectionOffset = Math::Vec3::Zero();
        u32 m_nextClientInputSeq = 1;
        u32 m_clientPredictionTick = 1;
        f64 m_clientClockSec = 0.0;
        u64 m_lastClientSnapshotTick = 0;
        Network::WorldInstanceId m_activeClientWorldId = Network::kInvalidWorldInstanceId;
        bool m_clientInterpolationDriven = false;

        // Phase 8: Managed callbacks & property cache
        ManagedRpcCallback m_onManagedRpc;
        ManagedPropertySyncCallback m_onManagedPropertySync;
        NetworkSpawnLifecycleCallback m_onNetworkSpawn;
        NetworkDespawnLifecycleCallback m_onNetworkDespawn;
        std::unordered_map<u64, std::unordered_map<u32, std::vector<u8>>> m_serverManagedPropCache;
        std::vector<Network::ConnectionHandle> m_pendingPropReplayConns;
    };
}
