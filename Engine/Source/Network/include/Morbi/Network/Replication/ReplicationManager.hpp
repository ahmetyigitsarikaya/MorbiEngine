#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Replication/InterestManagement.hpp"
#include "Morbi/Network/Replication/NetworkIdPool.hpp"
#include "Morbi/Network/Replication/ReplicationContracts.hpp"

namespace Morbi::Network
{
    struct ReplicatedEntitySpawnDesc
    {
        u32 ArchetypeHash = 1;
        SessionId OwnerSession = kInvalidSessionId;
        PlayerId OwnerPlayer = kInvalidPlayerId;
        WorldInstanceId WorldId = 1;
        NetworkAuthority Authority = NetworkAuthority::Server;
        ReplicationScope Scope = ReplicationScope::AllRelevant;
        std::string EntityName = "NetEntity";
        u8 ComponentMask = SnapshotComponentFlags::kTransform | SnapshotComponentFlags::kMovement;
        ReplicatedTransformData InitialTransform{};
        ReplicatedMovementData InitialMovement{};
        ReplicatedPhysicsStateData InitialPhysics{};
    };

    struct ServerReplicatedEntity
    {
        NetworkId NetId = kInvalidNetworkId;
        u32 ArchetypeHash = 1;
        SessionId OwnerSession = kInvalidSessionId;
        PlayerId OwnerPlayer = kInvalidPlayerId;
        WorldInstanceId WorldId = 1;
        NetworkAuthority Authority = NetworkAuthority::Server;
        ReplicationScope Scope = ReplicationScope::AllRelevant;
        std::string EntityName;
        u8 ComponentMask = SnapshotComponentFlags::kTransform | SnapshotComponentFlags::kMovement;
        ReplicatedTransformData Transform{};
        ReplicatedMovementData Movement{};
        ReplicatedPhysicsStateData Physics{};
        u32 TransformRevision = 1;
        u32 MovementRevision = 1;
        u32 PhysicsRevision = 1;
        u32 OwnerRevision = 1;
        u64 SpawnTick = 0;
    };

    struct ReplicationStats
    {
        u64 BytesSent = 0;
        u64 KeyframesSent = 0;
        u64 DeltasSent = 0;
        u64 SnapshotAcksReceived = 0;
        u64 StaleAcksIgnored = 0;
        u64 DuplicateAcksIgnored = 0;
        u64 InvalidAcksRejected = 0;
        u64 BaselineRecoveries = 0;
        u64 ResyncRequestsReceived = 0;
        u64 SpawnOpsSent = 0;
        u64 UpdateOpsSent = 0;
        u64 DespawnOpsSent = 0;
        u64 OwnershipTransfers = 0;
        u64 ChunkedKeyframesSent = 0;
        u64 KeyframeChunksSent = 0;
        u64 LastSnapshotBytes = 0;
        u64 LastReplicationDurationUs = 0;
        u32 ActiveReplicatedEntities = 0;
    };

    struct ClientReplicationStats
    {
        u64 SnapshotsApplied = 0;
        u64 StaleSnapshotsDropped = 0;
        u64 DuplicateSnapshotsDropped = 0;
        u64 OutOfSyncDeltasRejected = 0;
        u64 ResyncRequestsEmitted = 0;
        u64 ChunkedKeyframesCompleted = 0;
        u64 ChunkedKeyframesFailed = 0;
        u64 ChunkedKeyframesTimedOut = 0;
        u64 DuplicateChunksIgnored = 0;
    };

    struct ClientReplicatedEntity
    {
        NetworkId NetId = kInvalidNetworkId;
        u32 ArchetypeHash = 1;
        PlayerId OwnerPlayer = kInvalidPlayerId;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        NetworkAuthority Authority = NetworkAuthority::Server;
        std::string EntityName;
        u8 ComponentMask = SnapshotComponentFlags::kNone;
        ReplicatedTransformData Transform{};
        ReplicatedMovementData Movement{};
        ReplicatedPhysicsStateData Physics{};
    };

    struct ClientReplicationEvent
    {
        ReplicationOpKind Kind = ReplicationOpKind::Spawn;
        NetworkId NetId = kInvalidNetworkId;
    };

    /// Server-side authoritative replication manager with per-connection baselines,
    /// explicit application-level ACK verification, delta encoding, and interest management.
    class MORBI_NETWORK_API ReplicationManager final
    {
    public:
        ReplicationManager();

        void Reset();
        void SetInterestPolicy(std::shared_ptr<const IInterestPolicy> policy);
        [[nodiscard]] const std::shared_ptr<const IInterestPolicy>& GetInterestPolicy() const noexcept { return m_interestPolicy; }

        [[nodiscard]] NetworkId SpawnEntity(const ReplicatedEntitySpawnDesc& desc, u64 serverTick = 0);
        bool DespawnEntity(NetworkId netId);
        void DespawnEntitiesBySession(SessionId ownerSession);
        void DespawnEntitiesByWorld(WorldInstanceId worldId);

        bool UpdateEntityTransform(NetworkId netId, const ReplicatedTransformData& transform);
        bool UpdateEntityMovement(NetworkId netId, const ReplicatedMovementData& movement);
        bool UpdateEntityPhysics(NetworkId netId, const ReplicatedPhysicsStateData& physicsState);
        bool TransferEntityOwnership(NetworkId netId, SessionId newOwnerSession, PlayerId newOwnerPlayer);

        [[nodiscard]] bool TryGetEntity(NetworkId netId, ServerReplicatedEntity& outEntity) const;
        [[nodiscard]] std::vector<NetworkId> GetEntitiesForSession(SessionId ownerSession) const;
        [[nodiscard]] usize GetEntityCount() const noexcept { return m_entities.size(); }

        // Per-connection replication registration & view updates
        void RegisterConnection(ConnectionHandle connection, SessionId session, WorldInstanceId worldId);
        void UnregisterConnection(ConnectionHandle connection);
        void UpdateConnectionSession(ConnectionHandle connection, SessionId session, WorldInstanceId worldId);
        void SetConnectionViewPosition(ConnectionHandle connection, f32 x, f32 y, f32 z);
        void RequestFullResyncForConnection(ConnectionHandle connection);

        void SetKeyframeChunkThresholdBytes(usize thresholdBytes) noexcept;
        [[nodiscard]] usize GetKeyframeChunkThresholdBytes() const noexcept { return m_keyframeChunkThresholdBytes; }

        /// Builds the next serialized snapshot packet if work is needed for `connection`.
        /// Returns empty optional if the connection's confirmed state is already up to date
        /// or is awaiting confirmation of a pending keyframe.
        [[nodiscard]] std::optional<SnapshotPacket> BuildSnapshotForConnection(
            ConnectionHandle connection,
            u64 serverTick,
            bool forceResendKeyframe = false);

        /// Serializes `packet` with bounded BitWriter and updates telemetry counters.
        bool SerializeSnapshot(const SnapshotPacket& packet, std::vector<u8>& outBytes);

        /// Serializes a snapshot either as a single wire snapshot (`outSingleSnapshotBytes`)
        /// or splits large keyframes into bounded `KeyframeChunkPacket`s (`outKeyframeChunks`).
        bool PrepareOutgoingSnapshot(
            const SnapshotPacket& packet,
            std::optional<std::vector<u8>>& outSingleSnapshotBytes,
            std::vector<KeyframeChunkPacket>& outKeyframeChunks);

        /// Splits an already-serialized or oversized keyframe into bounded `KeyframeChunkPacket`s.
        bool BuildKeyframeChunks(
            const SnapshotPacket& packet,
            std::vector<KeyframeChunkPacket>& outChunks,
            usize maxChunkPayloadBytes = ReplicationConstants::kDefaultKeyframeChunkPayloadBytes);

        /// Processes explicit client application-level snapshot ACK.
        bool HandleSnapshotAck(ConnectionHandle connection, const SnapshotAckPacket& ack);

        /// Processes explicit client resync request and invalidates baseline cleanly.
        bool HandleResyncRequest(ConnectionHandle connection, const ResyncRequestPacket& req);

        [[nodiscard]] bool IsBaselineConfirmed(ConnectionHandle connection) const noexcept;
        [[nodiscard]] u32 GetConfirmedBaselineSequence(ConnectionHandle connection) const noexcept;
        [[nodiscard]] u32 GetLastReceivedAckSequence(ConnectionHandle connection) const noexcept;
        [[nodiscard]] u32 GetBaselineEpoch(ConnectionHandle connection) const noexcept;
        [[nodiscard]] usize GetPendingSnapshotCount(ConnectionHandle connection) const noexcept;
        [[nodiscard]] bool IsEntityVisibleToConnection(ConnectionHandle connection, NetworkId netId) const noexcept;
        [[nodiscard]] const ReplicationStats& GetStats() const noexcept { return m_stats; }
        void RecordReplicationDurationUs(u64 durationUs) noexcept { m_stats.LastReplicationDurationUs = durationUs; }

    private:
        struct BaselineEntityRecord
        {
            u32 TransformRevision = 0;
            u32 MovementRevision = 0;
            u32 PhysicsRevision = 0;
            u32 OwnerRevision = 0;
            u8 ComponentMask = SnapshotComponentFlags::kNone;
        };

        struct PendingSnapshotRecord
        {
            u32 SnapshotSequence = 0;
            u32 BaselineSequence = 0;
            u32 BaselineEpoch = 1;
            bool IsKeyframe = false;
            std::unordered_map<u64, BaselineEntityRecord> TargetBaseline;
        };

        struct ConnectionReplicationState
        {
            ConnectionHandle Handle = kInvalidConnectionHandle;
            InterestViewerContext Viewer{};
            u32 NextSnapshotSequence = 1;
            u32 ConfirmedBaselineSequence = 0;
            u32 LastReceivedAckSequence = 0;
            u32 BaselineEpoch = 1;
            bool NeedsFullResync = true;
            u64 LastKeyframeSentTick = 0;
            std::unordered_map<u64, BaselineEntityRecord> ConfirmedEntities;
            std::deque<PendingSnapshotRecord> PendingSnapshots;
        };

        void InvalidateBaseline(ConnectionReplicationState& state);

        NetworkIdPool m_idPool;
        std::unordered_map<u64, ServerReplicatedEntity> m_entities;
        std::unordered_map<u32, ConnectionReplicationState> m_connections;
        std::shared_ptr<const IInterestPolicy> m_interestPolicy;
        ReplicationStats m_stats{};
        usize m_keyframeChunkThresholdBytes = ReplicationConstants::kDefaultKeyframeChunkPayloadBytes;
        u32 m_nextBaselineEpoch = 1;
        u32 m_nextTransferId = 1;
    };

    /// Client-side strict snapshot & delta applier.
    /// Rejects stale snapshots, refuses out-of-sync deltas by emitting ResyncRequestPacket,
    /// and performs atomic bounded chunked-keyframe reassembly.
    class MORBI_NETWORK_API ClientReplicationApplier final
    {
    public:
        void Reset() noexcept;
        void PrepareForReconnect() noexcept;
        void CancelPendingKeyframeTransfer() noexcept;

        bool ApplySnapshotPacket(
            const SnapshotPacket& packet,
            std::vector<ClientReplicationEvent>& outEvents,
            std::optional<SnapshotAckPacket>& outAck,
            std::optional<ResyncRequestPacket>& outResync);

        bool ProcessKeyframeChunk(
            const KeyframeChunkPacket& chunk,
            u64 nowMs,
            std::vector<ClientReplicationEvent>& outEvents,
            std::optional<SnapshotAckPacket>& outAck,
            std::optional<ResyncRequestPacket>& outResync);

        bool TickKeyframeTimeouts(
            u64 nowMs,
            std::optional<ResyncRequestPacket>& outResync,
            u64 timeoutMs = ReplicationConstants::kDefaultKeyframeTransferTimeoutMs);

        [[nodiscard]] bool TryGetEntity(NetworkId netId, ClientReplicatedEntity& outEntity) const;
        [[nodiscard]] const std::unordered_map<u64, ClientReplicatedEntity>& GetEntities() const noexcept { return m_entities; }
        [[nodiscard]] usize GetEntityCount() const noexcept { return m_entities.size(); }
        [[nodiscard]] u32 GetLastReceivedSequence() const noexcept { return m_lastReceivedSequence; }
        [[nodiscard]] u32 GetLastAppliedSequence() const noexcept { return m_lastAppliedSequence; }
        [[nodiscard]] u32 GetLastAcknowledgedSequence() const noexcept { return m_lastAcknowledgedSequence; }
        [[nodiscard]] u32 GetActiveBaselineEpoch() const noexcept { return m_activeBaselineEpoch; }
        [[nodiscard]] WorldInstanceId GetActiveWorldId() const noexcept { return m_activeWorldId; }
        [[nodiscard]] bool IsKeyframeTransferInProgress() const noexcept { return m_activeTransfer.Active; }
        [[nodiscard]] const ClientReplicationStats& GetStats() const noexcept { return m_stats; }

    private:
        struct ActiveKeyframeTransfer
        {
            bool Active = false;
            u32 TransferId = 0;
            u32 SnapshotSequence = 0;
            u32 BaselineEpoch = 0;
            WorldInstanceId WorldId = kInvalidWorldInstanceId;
            u64 ServerTick = 0;
            u16 TotalChunks = 0;
            u32 TotalBytes = 0;
            u32 ExpectedDigest32 = 0;
            u64 StartedAtMs = 0;
            u64 LastChunkAtMs = 0;
            u16 ReceivedChunkCount = 0;
            usize AccumulatedBytes = 0;
            std::vector<bool> ChunkReceived;
            std::vector<std::vector<u8>> Chunks;
        };

        std::unordered_map<u64, ClientReplicatedEntity> m_entities;
        u32 m_lastReceivedSequence = 0;
        u32 m_lastAppliedSequence = 0;
        u32 m_lastAcknowledgedSequence = 0;
        u32 m_activeBaselineEpoch = 0;
        u32 m_lastCompletedTransferId = 0;
        WorldInstanceId m_activeWorldId = kInvalidWorldInstanceId;
        ActiveKeyframeTransfer m_activeTransfer{};
        ClientReplicationStats m_stats{};
    };

    struct RemoteSnapshotSample
    {
        u64 ServerTick = 0;
        f64 ReceiveTimeSec = 0.0;
        ReplicatedTransformData Transform{};
    };

    /// Bounded timestamped snapshot interpolation buffer for non-owned remote entities.
    class MORBI_NETWORK_API RemoteInterpolationBuffer final
    {
    public:
        static constexpr usize kMaxSamplesPerEntity = 32;

        void SetInterpolationDelaySec(f64 delaySec) noexcept;
        [[nodiscard]] f64 GetInterpolationDelaySec() const noexcept { return m_interpolationDelaySec; }

        void Clear() noexcept;
        void RemoveEntity(NetworkId netId) noexcept;
        void PushSample(NetworkId netId, u64 serverTick, f64 receiveTimeSec, const ReplicatedTransformData& transform);

        [[nodiscard]] bool SampleEntity(
            NetworkId netId,
            f64 renderTimeSec,
            ReplicatedTransformData& outTransform) const noexcept;

        [[nodiscard]] usize GetSampleCount(NetworkId netId) const noexcept;

    private:
        f64 m_interpolationDelaySec = 0.08;
        std::unordered_map<u64, std::deque<RemoteSnapshotSample>> m_samples;
    };
}
