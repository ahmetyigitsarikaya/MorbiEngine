#include "Morbi/Network/Replication/ReplicationManager.hpp"
#include "Morbi/Network/Protocol/ProtocolTypes.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Morbi::Network
{
    ReplicationManager::ReplicationManager()
        : m_interestPolicy(std::make_shared<AllVisibleInterestPolicy>())
    {
    }

    void ReplicationManager::Reset()
    {
        m_idPool.Clear();
        m_entities.clear();
        m_connections.clear();
        m_stats = ReplicationStats{};
        ++m_nextBaselineEpoch;
    }

    void ReplicationManager::SetInterestPolicy(std::shared_ptr<const IInterestPolicy> policy)
    {
        m_interestPolicy = policy ? std::move(policy) : std::make_shared<AllVisibleInterestPolicy>();
    }

    NetworkId ReplicationManager::SpawnEntity(const ReplicatedEntitySpawnDesc& desc, u64 serverTick)
    {
        if (desc.WorldId == kInvalidWorldInstanceId ||
            desc.EntityName.size() > ReplicationConstants::kMaxEntityNameBytes ||
            !desc.InitialTransform.IsValid() ||
            !desc.InitialMovement.IsValid() ||
            !desc.InitialPhysics.IsValid())
        {
            return kInvalidNetworkId;
        }

        const NetworkId netId = m_idPool.Allocate();
        if (!netId.IsValid())
        {
            return kInvalidNetworkId;
        }

        ServerReplicatedEntity entity{};
        entity.NetId = netId;
        entity.ArchetypeHash = desc.ArchetypeHash;
        entity.OwnerSession = desc.OwnerSession;
        entity.OwnerPlayer = desc.OwnerPlayer;
        entity.WorldId = desc.WorldId;
        entity.Authority = desc.Authority;
        entity.Scope = desc.Scope;
        entity.EntityName = desc.EntityName.empty() ? "NetEntity" : desc.EntityName;
        entity.ComponentMask = desc.ComponentMask;
        entity.Transform = desc.InitialTransform;
        entity.Movement = desc.InitialMovement;
        entity.Physics = desc.InitialPhysics;
        entity.TransformRevision = 1;
        entity.MovementRevision = 1;
        entity.PhysicsRevision = 1;
        entity.OwnerRevision = 1;
        entity.SpawnTick = serverTick;

        m_entities.emplace(netId.Pack(), std::move(entity));
        m_stats.ActiveReplicatedEntities = static_cast<u32>(m_entities.size());
        return netId;
    }

    bool ReplicationManager::DespawnEntity(NetworkId netId)
    {
        if (!m_idPool.IsAlive(netId))
        {
            return false;
        }
        m_entities.erase(netId.Pack());
        (void)m_idPool.Release(netId);
        m_stats.ActiveReplicatedEntities = static_cast<u32>(m_entities.size());
        return true;
    }

    void ReplicationManager::DespawnEntitiesBySession(SessionId ownerSession)
    {
        if (!ownerSession.IsValid())
        {
            return;
        }
        std::vector<NetworkId> toRemove = GetEntitiesForSession(ownerSession);
        for (const NetworkId id : toRemove)
        {
            (void)DespawnEntity(id);
        }
    }

    void ReplicationManager::DespawnEntitiesByWorld(WorldInstanceId worldId)
    {
        std::vector<NetworkId> toRemove;
        for (const auto& [packed, ent] : m_entities)
        {
            (void)packed;
            if (ent.WorldId == worldId)
            {
                toRemove.push_back(ent.NetId);
            }
        }
        for (const NetworkId id : toRemove)
        {
            (void)DespawnEntity(id);
        }
    }

    bool ReplicationManager::UpdateEntityTransform(NetworkId netId, const ReplicatedTransformData& transform)
    {
        if (!transform.IsValid())
        {
            return false;
        }
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end() || !m_idPool.IsAlive(netId))
        {
            return false;
        }
        if (it->second.Transform.ApproximatelyEquals(transform))
        {
            return true;
        }
        it->second.Transform = transform;
        ++it->second.TransformRevision;
        return true;
    }

    bool ReplicationManager::UpdateEntityMovement(NetworkId netId, const ReplicatedMovementData& movement)
    {
        if (!movement.IsValid())
        {
            return false;
        }
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end() || !m_idPool.IsAlive(netId))
        {
            return false;
        }
        if (it->second.Movement.ApproximatelyEquals(movement))
        {
            return true;
        }
        it->second.Movement = movement;
        ++it->second.MovementRevision;
        return true;
    }

    bool ReplicationManager::UpdateEntityPhysics(NetworkId netId, const ReplicatedPhysicsStateData& physicsState)
    {
        if (!physicsState.IsValid())
        {
            return false;
        }
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end() || !m_idPool.IsAlive(netId))
        {
            return false;
        }
        if ((it->second.ComponentMask & SnapshotComponentFlags::kPhysics) != 0 &&
            it->second.Physics.ApproximatelyEquals(physicsState))
        {
            return true;
        }
        it->second.ComponentMask |= SnapshotComponentFlags::kPhysics;
        it->second.Physics = physicsState;
        ++it->second.PhysicsRevision;
        return true;
    }

    bool ReplicationManager::TransferEntityOwnership(NetworkId netId, SessionId newOwnerSession, PlayerId newOwnerPlayer)
    {
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end() || !m_idPool.IsAlive(netId))
        {
            return false;
        }
        if (it->second.OwnerSession == newOwnerSession && it->second.OwnerPlayer == newOwnerPlayer)
        {
            return true;
        }
        it->second.OwnerSession = newOwnerSession;
        it->second.OwnerPlayer = newOwnerPlayer;
        ++it->second.OwnerRevision;
        ++m_stats.OwnershipTransfers;
        return true;
    }

    bool ReplicationManager::TryGetEntity(NetworkId netId, ServerReplicatedEntity& outEntity) const
    {
        if (!m_idPool.IsAlive(netId))
        {
            return false;
        }
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end())
        {
            return false;
        }
        outEntity = it->second;
        return true;
    }

    std::vector<NetworkId> ReplicationManager::GetEntitiesForSession(SessionId ownerSession) const
    {
        std::vector<NetworkId> out;
        if (!ownerSession.IsValid())
        {
            return out;
        }
        for (const auto& [packed, ent] : m_entities)
        {
            (void)packed;
            if (ent.OwnerSession == ownerSession)
            {
                out.push_back(ent.NetId);
            }
        }
        return out;
    }

    void ReplicationManager::RegisterConnection(ConnectionHandle connection, SessionId session, WorldInstanceId worldId)
    {
        if (!connection.IsValid())
        {
            return;
        }
        ConnectionReplicationState state{};
        state.Handle = connection;
        state.Viewer.ViewerSession = session;
        state.Viewer.WorldId = worldId;
        state.BaselineEpoch = m_nextBaselineEpoch;
        state.NeedsFullResync = true;
        m_connections.insert_or_assign(connection.Value, std::move(state));
    }

    void ReplicationManager::UnregisterConnection(ConnectionHandle connection)
    {
        m_connections.erase(connection.Value);
    }

    void ReplicationManager::UpdateConnectionSession(ConnectionHandle connection, SessionId session, WorldInstanceId worldId)
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            RegisterConnection(connection, session, worldId);
            return;
        }
        it->second.Viewer.ViewerSession = session;
        if (it->second.Viewer.WorldId != worldId)
        {
            it->second.Viewer.WorldId = worldId;
            InvalidateBaseline(it->second);
        }
    }

    void ReplicationManager::SetConnectionViewPosition(ConnectionHandle connection, f32 x, f32 y, f32 z)
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return;
        }
        it->second.Viewer.ViewPosX = x;
        it->second.Viewer.ViewPosY = y;
        it->second.Viewer.ViewPosZ = z;
        it->second.Viewer.HasViewPosition = true;
    }

    void ReplicationManager::RequestFullResyncForConnection(ConnectionHandle connection)
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return;
        }
        InvalidateBaseline(it->second);
    }

    void ReplicationManager::SetKeyframeChunkThresholdBytes(usize thresholdBytes) noexcept
    {
        if (thresholdBytes == 0)
        {
            m_keyframeChunkThresholdBytes = ReplicationConstants::kDefaultKeyframeChunkPayloadBytes;
            return;
        }
        m_keyframeChunkThresholdBytes = (std::min)(thresholdBytes, ProtocolConstants::kMaxMessageBytes);
    }

    void ReplicationManager::InvalidateBaseline(ConnectionReplicationState& state)
    {
        state.ConfirmedBaselineSequence = 0;
        state.NeedsFullResync = true;
        state.ConfirmedEntities.clear();
        state.PendingSnapshots.clear();
        if (state.NextSnapshotSequence > 1)
        {
            state.BaselineEpoch = ++m_nextBaselineEpoch;
        }
    }

    std::optional<SnapshotPacket> ReplicationManager::BuildSnapshotForConnection(
        ConnectionHandle connection,
        u64 serverTick,
        bool forceResendKeyframe)
    {
        auto connIt = m_connections.find(connection.Value);
        if (connIt == m_connections.end())
        {
            return std::nullopt;
        }

        ConnectionReplicationState& state = connIt->second;
        const auto& policy = *m_interestPolicy;

        // If pending snapshot queue reached bound without confirmation, reset cleanly for keyframe recovery
        if (state.PendingSnapshots.size() >= ReplicationConstants::kMaxPendingSnapshotsPerPeer)
        {
            InvalidateBaseline(state);
            ++m_stats.BaselineRecoveries;
        }

        // Deterministic entity iteration order (ascending packed NetworkId)
        std::vector<u64> sortedEntityIds;
        sortedEntityIds.reserve(m_entities.size());
        for (const auto& [packed, ent] : m_entities)
        {
            (void)ent;
            sortedEntityIds.push_back(packed);
        }
        std::sort(sortedEntityIds.begin(), sortedEntityIds.end());

        const bool mustSendKeyframe = (state.ConfirmedBaselineSequence == 0) || state.NeedsFullResync;
        if (mustSendKeyframe)
        {
            // Avoid flooding redundant full keyframes every tick while awaiting initial ACK
            if (!forceResendKeyframe &&
                !state.PendingSnapshots.empty() &&
                state.LastKeyframeSentTick != 0 &&
                serverTick >= state.LastKeyframeSentTick &&
                (serverTick - state.LastKeyframeSentTick) < 30 &&
                state.PendingSnapshots.back().IsKeyframe &&
                state.PendingSnapshots.back().BaselineEpoch == state.BaselineEpoch)
            {
                // Still send immediately if an entity spawned/despawned/changed since that pending keyframe
                const auto& prevMap = state.PendingSnapshots.back().TargetBaseline;
                bool membershipOrStateSame = true;
                usize expectedVisibleCount = 0;
                for (const u64 packed : sortedEntityIds)
                {
                    const auto& ent = m_entities.at(packed);
                    InterestTargetView tv{
                        .NetId = ent.NetId,
                        .OwnerSession = ent.OwnerSession,
                        .WorldId = ent.WorldId,
                        .PosX = ent.Transform.PosX,
                        .PosY = ent.Transform.PosY,
                        .PosZ = ent.Transform.PosZ,
                        .Scope = ent.Scope,
                        .PreviouslyVisible = (prevMap.find(packed) != prevMap.end())
                    };
                    if (policy.IsRelevant(state.Viewer, tv))
                    {
                        ++expectedVisibleCount;
                        auto prevIt = prevMap.find(packed);
                        if (prevIt == prevMap.end() ||
                            prevIt->second.TransformRevision != ent.TransformRevision ||
                            prevIt->second.MovementRevision != ent.MovementRevision ||
                            prevIt->second.PhysicsRevision != ent.PhysicsRevision ||
                            prevIt->second.OwnerRevision != ent.OwnerRevision)
                        {
                            membershipOrStateSame = false;
                            break;
                        }
                    }
                }
                if (membershipOrStateSame && expectedVisibleCount == prevMap.size())
                {
                    return std::nullopt;
                }
            }

            SnapshotPacket pkt{};
            pkt.SnapshotSequence = state.NextSnapshotSequence++;
            pkt.BaselineSequence = 0;
            pkt.IsKeyframe = true;
            pkt.WorldId = state.Viewer.WorldId;
            pkt.ServerTick = serverTick;
            pkt.BaselineEpoch = state.BaselineEpoch;

            std::unordered_map<u64, BaselineEntityRecord> nextBaseline;
            for (const u64 packed : sortedEntityIds)
            {
                const auto& ent = m_entities.at(packed);
                if (pkt.Entries.size() >= ReplicationConstants::kMaxKeyframeEntries)
                {
                    break;
                }
                const bool wasVis = (state.ConfirmedEntities.find(packed) != state.ConfirmedEntities.end());
                InterestTargetView tv{
                    .NetId = ent.NetId,
                    .OwnerSession = ent.OwnerSession,
                    .WorldId = ent.WorldId,
                    .PosX = ent.Transform.PosX,
                    .PosY = ent.Transform.PosY,
                    .PosZ = ent.Transform.PosZ,
                    .Scope = ent.Scope,
                    .PreviouslyVisible = wasVis
                };
                if (!policy.IsRelevant(state.Viewer, tv))
                {
                    continue;
                }

                SnapshotEntityEntry entry{};
                entry.Op = ReplicationOpKind::Spawn;
                entry.NetId = ent.NetId;
                entry.ArchetypeHash = ent.ArchetypeHash;
                entry.OwnerPlayer = ent.OwnerPlayer;
                entry.WorldId = ent.WorldId;
                entry.Authority = ent.Authority;
                entry.EntityName = ent.EntityName;
                entry.ComponentMask = ent.ComponentMask;
                entry.Transform = ent.Transform;
                entry.Movement = ent.Movement;
                entry.Physics = ent.Physics;
                pkt.Entries.push_back(std::move(entry));

                nextBaseline.emplace(packed, BaselineEntityRecord{
                    .TransformRevision = ent.TransformRevision,
                    .MovementRevision = ent.MovementRevision,
                    .PhysicsRevision = ent.PhysicsRevision,
                    .OwnerRevision = ent.OwnerRevision,
                    .ComponentMask = ent.ComponentMask
                });
            }

            state.PendingSnapshots.push_back(PendingSnapshotRecord{
                .SnapshotSequence = pkt.SnapshotSequence,
                .BaselineSequence = 0,
                .BaselineEpoch = pkt.BaselineEpoch,
                .IsKeyframe = true,
                .TargetBaseline = std::move(nextBaseline)
            });
            state.LastKeyframeSentTick = serverTick;
            ++m_stats.KeyframesSent;
            m_stats.SpawnOpsSent += pkt.Entries.size();
            return pkt;
        }

        // Delta Snapshot STRICTLY against ConfirmedBaselineSequence & ConfirmedEntities!
        SnapshotPacket delta{};
        delta.SnapshotSequence = 0; // assigned only if >= 1 op emitted
        delta.BaselineSequence = state.ConfirmedBaselineSequence;
        delta.IsKeyframe = false;
        delta.WorldId = state.Viewer.WorldId;
        delta.ServerTick = serverTick;
        delta.BaselineEpoch = state.BaselineEpoch;

        std::unordered_map<u64, BaselineEntityRecord> nextBaseline = state.ConfirmedEntities;
        u64 spawnsAdded = 0;
        u64 updatesAdded = 0;
        u64 despawnsAdded = 0;

        // 1. Entities that vanished from authoritative table or left interest relevance -> Despawn
        std::vector<u64> sortedBaselineIds;
        sortedBaselineIds.reserve(state.ConfirmedEntities.size());
        for (const auto& [packed, baseRec] : state.ConfirmedEntities)
        {
            (void)baseRec;
            sortedBaselineIds.push_back(packed);
        }
        std::sort(sortedBaselineIds.begin(), sortedBaselineIds.end());

        for (const u64 packed : sortedBaselineIds)
        {
            if (delta.Entries.size() >= ReplicationConstants::kMaxSnapshotEntriesPerPacket)
            {
                break;
            }
            auto entIt = m_entities.find(packed);
            bool stillRelevant = false;
            if (entIt != m_entities.end())
            {
                const auto& ent = entIt->second;
                InterestTargetView tv{
                    .NetId = ent.NetId,
                    .OwnerSession = ent.OwnerSession,
                    .WorldId = ent.WorldId,
                    .PosX = ent.Transform.PosX,
                    .PosY = ent.Transform.PosY,
                    .PosZ = ent.Transform.PosZ,
                    .Scope = ent.Scope,
                    .PreviouslyVisible = true
                };
                stillRelevant = policy.IsRelevant(state.Viewer, tv);
            }

            if (!stillRelevant)
            {
                SnapshotEntityEntry entry{};
                entry.Op = ReplicationOpKind::Despawn;
                entry.NetId = NetworkId::Unpack(packed);
                delta.Entries.push_back(std::move(entry));
                nextBaseline.erase(packed);
                ++despawnsAdded;
            }
        }

        // 2. Newly relevant (Spawn), ownership-changed (Spawn/Re-metadata), or changed component state (Update)
        for (const u64 packed : sortedEntityIds)
        {
            const auto& ent = m_entities.at(packed);
            if (delta.Entries.size() >= ReplicationConstants::kMaxSnapshotEntriesPerPacket)
            {
                break;
            }
            auto baseIt = state.ConfirmedEntities.find(packed);
            const bool previouslyConfirmed = (baseIt != state.ConfirmedEntities.end());
            InterestTargetView tv{
                .NetId = ent.NetId,
                .OwnerSession = ent.OwnerSession,
                .WorldId = ent.WorldId,
                .PosX = ent.Transform.PosX,
                .PosY = ent.Transform.PosY,
                .PosZ = ent.Transform.PosZ,
                .Scope = ent.Scope,
                .PreviouslyVisible = previouslyConfirmed
            };
            if (!policy.IsRelevant(state.Viewer, tv))
            {
                continue;
            }

            const bool ownerChanged = previouslyConfirmed && (ent.OwnerRevision != baseIt->second.OwnerRevision);
            if (!previouslyConfirmed || ownerChanged)
            {
                SnapshotEntityEntry entry{};
                entry.Op = ReplicationOpKind::Spawn;
                entry.NetId = ent.NetId;
                entry.ArchetypeHash = ent.ArchetypeHash;
                entry.OwnerPlayer = ent.OwnerPlayer;
                entry.WorldId = ent.WorldId;
                entry.Authority = ent.Authority;
                entry.EntityName = ent.EntityName;
                entry.ComponentMask = ent.ComponentMask;
                entry.Transform = ent.Transform;
                entry.Movement = ent.Movement;
                entry.Physics = ent.Physics;
                delta.Entries.push_back(std::move(entry));
                nextBaseline[packed] = BaselineEntityRecord{
                    .TransformRevision = ent.TransformRevision,
                    .MovementRevision = ent.MovementRevision,
                    .PhysicsRevision = ent.PhysicsRevision,
                    .OwnerRevision = ent.OwnerRevision,
                    .ComponentMask = ent.ComponentMask
                };
                if (previouslyConfirmed)
                {
                    ++updatesAdded;
                }
                else
                {
                    ++spawnsAdded;
                }
                continue;
            }

            u8 changedMask = SnapshotComponentFlags::kNone;
            if ((ent.ComponentMask & SnapshotComponentFlags::kTransform) != 0 &&
                ent.TransformRevision != baseIt->second.TransformRevision)
            {
                changedMask |= SnapshotComponentFlags::kTransform;
            }
            if ((ent.ComponentMask & SnapshotComponentFlags::kMovement) != 0 &&
                ent.MovementRevision != baseIt->second.MovementRevision)
            {
                changedMask |= SnapshotComponentFlags::kMovement;
            }
            if ((ent.ComponentMask & SnapshotComponentFlags::kPhysics) != 0 &&
                ent.PhysicsRevision != baseIt->second.PhysicsRevision)
            {
                changedMask |= SnapshotComponentFlags::kPhysics;
            }

            if (changedMask != SnapshotComponentFlags::kNone)
            {
                SnapshotEntityEntry entry{};
                entry.Op = ReplicationOpKind::Update;
                entry.NetId = ent.NetId;
                entry.ComponentMask = changedMask;
                entry.Transform = ent.Transform;
                entry.Movement = ent.Movement;
                entry.Physics = ent.Physics;
                delta.Entries.push_back(std::move(entry));
                nextBaseline[packed] = BaselineEntityRecord{
                    .TransformRevision = ent.TransformRevision,
                    .MovementRevision = ent.MovementRevision,
                    .PhysicsRevision = ent.PhysicsRevision,
                    .OwnerRevision = ent.OwnerRevision,
                    .ComponentMask = ent.ComponentMask
                };
                ++updatesAdded;
            }
        }

        if (delta.Entries.empty())
        {
            return std::nullopt;
        }

        delta.SnapshotSequence = state.NextSnapshotSequence++;
        state.PendingSnapshots.push_back(PendingSnapshotRecord{
            .SnapshotSequence = delta.SnapshotSequence,
            .BaselineSequence = delta.BaselineSequence,
            .BaselineEpoch = delta.BaselineEpoch,
            .IsKeyframe = false,
            .TargetBaseline = std::move(nextBaseline)
        });
        ++m_stats.DeltasSent;
        m_stats.SpawnOpsSent += spawnsAdded;
        m_stats.UpdateOpsSent += updatesAdded;
        m_stats.DespawnOpsSent += despawnsAdded;
        return delta;
    }

    bool ReplicationManager::SerializeSnapshot(const SnapshotPacket& packet, std::vector<u8>& outBytes)
    {
        const usize maxBytes = packet.IsKeyframe
            ? ReplicationConstants::kMaxKeyframeTotalBytes
            : (ProtocolConstants::kMaxMessageBytes - ProtocolCodec::kEncodedHeaderBytes);
        BitWriter writer(maxBytes);
        if (!ReplicationCodec::WriteSnapshotPacket(writer, packet))
        {
            return false;
        }
        const auto span = writer.GetData();
        outBytes.assign(span.begin(), span.end());
        m_stats.BytesSent += outBytes.size();
        m_stats.LastSnapshotBytes = outBytes.size();
        return true;
    }

    bool ReplicationManager::BuildKeyframeChunks(
        const SnapshotPacket& packet,
        std::vector<KeyframeChunkPacket>& outChunks,
        usize maxChunkPayloadBytes)
    {
        outChunks.clear();
        if (!packet.IsKeyframe ||
            maxChunkPayloadBytes == 0 ||
            maxChunkPayloadBytes > ReplicationConstants::kMaxKeyframeChunkPayloadBytes)
        {
            return false;
        }

        BitWriter writer(ReplicationConstants::kMaxKeyframeTotalBytes);
        if (!ReplicationCodec::WriteSnapshotPacket(writer, packet))
        {
            return false;
        }
        const auto span = writer.GetData();
        if (span.empty() || span.size() > ReplicationConstants::kMaxKeyframeTotalBytes)
        {
            return false;
        }

        const usize totalBytes = span.size();
        const usize chunkCount = (totalBytes + maxChunkPayloadBytes - 1) / maxChunkPayloadBytes;
        if (chunkCount == 0 || chunkCount > ReplicationConstants::kMaxKeyframeChunks)
        {
            return false;
        }

        const u32 transferId = m_nextTransferId++;
        const u32 digest32 = ReplicationCodec::ComputeDigest32(span);

        outChunks.reserve(chunkCount);
        for (usize idx = 0; idx < chunkCount; ++idx)
        {
            const usize offset = idx * maxChunkPayloadBytes;
            const usize chunkBytes = (std::min)(maxChunkPayloadBytes, totalBytes - offset);

            KeyframeChunkPacket chunk{};
            chunk.WireVersion = packet.WireVersion;
            chunk.TransferId = transferId;
            chunk.SnapshotSequence = packet.SnapshotSequence;
            chunk.BaselineEpoch = packet.BaselineEpoch;
            chunk.WorldId = packet.WorldId;
            chunk.ServerTick = packet.ServerTick;
            chunk.ChunkIndex = static_cast<u16>(idx);
            chunk.TotalChunks = static_cast<u16>(chunkCount);
            chunk.TotalBytes = static_cast<u32>(totalBytes);
            chunk.PayloadDigest32 = digest32;
            chunk.ChunkData.assign(span.begin() + static_cast<std::ptrdiff_t>(offset),
                span.begin() + static_cast<std::ptrdiff_t>(offset + chunkBytes));
            outChunks.push_back(std::move(chunk));
        }

        ++m_stats.ChunkedKeyframesSent;
        m_stats.KeyframeChunksSent += outChunks.size();
        return true;
    }

    bool ReplicationManager::PrepareOutgoingSnapshot(
        const SnapshotPacket& packet,
        std::optional<std::vector<u8>>& outSingleSnapshotBytes,
        std::vector<KeyframeChunkPacket>& outKeyframeChunks)
    {
        outSingleSnapshotBytes.reset();
        outKeyframeChunks.clear();

        std::vector<u8> serialized;
        const usize maxBytes = packet.IsKeyframe
            ? ReplicationConstants::kMaxKeyframeTotalBytes
            : (ProtocolConstants::kMaxMessageBytes - ProtocolCodec::kEncodedHeaderBytes);
        BitWriter writer(maxBytes);
        if (!ReplicationCodec::WriteSnapshotPacket(writer, packet))
        {
            return false;
        }
        const auto span = writer.GetData();
        serialized.assign(span.begin(), span.end());

        const usize wireMaxSingle = ProtocolConstants::kMaxMessageBytes - ProtocolCodec::kEncodedHeaderBytes;
        const usize singleThreshold = (std::min)(m_keyframeChunkThresholdBytes, wireMaxSingle);
        const bool mustChunk = packet.IsKeyframe &&
            (serialized.size() > singleThreshold ||
             packet.Entries.size() > ReplicationConstants::kMaxSnapshotEntriesPerPacket);

        if (!mustChunk)
        {
            if (serialized.size() > wireMaxSingle)
            {
                return false;
            }
            m_stats.BytesSent += serialized.size();
            m_stats.LastSnapshotBytes = serialized.size();
            outSingleSnapshotBytes = std::move(serialized);
            return true;
        }

        const usize chunkPayload = (std::min)(
            (std::max<usize>)(256, singleThreshold),
            ReplicationConstants::kMaxKeyframeChunkPayloadBytes);
        const usize chunkCount = (serialized.size() + chunkPayload - 1) / chunkPayload;
        if (chunkCount == 0 || chunkCount > ReplicationConstants::kMaxKeyframeChunks)
        {
            return false;
        }

        const u32 transferId = m_nextTransferId++;
        const u32 digest32 = ReplicationCodec::ComputeDigest32(serialized);
        outKeyframeChunks.reserve(chunkCount);
        for (usize idx = 0; idx < chunkCount; ++idx)
        {
            const usize offset = idx * chunkPayload;
            const usize len = (std::min)(chunkPayload, serialized.size() - offset);

            KeyframeChunkPacket chunk{};
            chunk.WireVersion = packet.WireVersion;
            chunk.TransferId = transferId;
            chunk.SnapshotSequence = packet.SnapshotSequence;
            chunk.BaselineEpoch = packet.BaselineEpoch;
            chunk.WorldId = packet.WorldId;
            chunk.ServerTick = packet.ServerTick;
            chunk.ChunkIndex = static_cast<u16>(idx);
            chunk.TotalChunks = static_cast<u16>(chunkCount);
            chunk.TotalBytes = static_cast<u32>(serialized.size());
            chunk.PayloadDigest32 = digest32;
            chunk.ChunkData.assign(
                serialized.begin() + static_cast<std::ptrdiff_t>(offset),
                serialized.begin() + static_cast<std::ptrdiff_t>(offset + len));
            outKeyframeChunks.push_back(std::move(chunk));
        }

        m_stats.BytesSent += serialized.size();
        m_stats.LastSnapshotBytes = serialized.size();
        ++m_stats.ChunkedKeyframesSent;
        m_stats.KeyframeChunksSent += outKeyframeChunks.size();
        return true;
    }

    bool ReplicationManager::HandleSnapshotAck(ConnectionHandle connection, const SnapshotAckPacket& ack)
    {
        auto connIt = m_connections.find(connection.Value);
        if (connIt == m_connections.end())
        {
            return false;
        }
        ConnectionReplicationState& state = connIt->second;

        // Ignore ACKs from superseded baseline epochs without disrupting active baseline
        if (ack.BaselineEpoch != 0 && ack.BaselineEpoch != state.BaselineEpoch)
        {
            ++m_stats.StaleAcksIgnored;
            return false;
        }

        if (!ack.AppliedSuccessfully)
        {
            ++m_stats.InvalidAcksRejected;
            InvalidateBaseline(state);
            ++m_stats.BaselineRecoveries;
            return false;
        }

        // Future / fabricated sequence number cannot have been sent yet -> invalidate cleanly
        if (ack.SnapshotSequence == 0 || ack.SnapshotSequence >= state.NextSnapshotSequence)
        {
            ++m_stats.InvalidAcksRejected;
            InvalidateBaseline(state);
            ++m_stats.BaselineRecoveries;
            return false;
        }

        // Idempotent duplicate ACK for already confirmed baseline -> ignore safely
        if (ack.SnapshotSequence == state.ConfirmedBaselineSequence &&
            ack.SnapshotSequence != 0)
        {
            ++m_stats.DuplicateAcksIgnored;
            return false;
        }

        // Stale older ACK delivered late / out-of-order after newer baseline already confirmed
        if (ack.SnapshotSequence < state.ConfirmedBaselineSequence)
        {
            ++m_stats.StaleAcksIgnored;
            return false;
        }

        auto matchIt = std::find_if(
            state.PendingSnapshots.begin(),
            state.PendingSnapshots.end(),
            [&](const PendingSnapshotRecord& rec) {
                return rec.SnapshotSequence == ack.SnapshotSequence &&
                       rec.BaselineSequence == ack.BaselineSequence &&
                       rec.BaselineEpoch == ack.BaselineEpoch;
            });

        if (matchIt == state.PendingSnapshots.end())
        {
            // Mismatched baseline or evicted pending record for an unconfirmed future sequence -> recover
            ++m_stats.InvalidAcksRejected;
            InvalidateBaseline(state);
            ++m_stats.BaselineRecoveries;
            return false;
        }

        // Advance baseline strictly to the confirmed snapshot
        state.ConfirmedBaselineSequence = matchIt->SnapshotSequence;
        state.LastReceivedAckSequence = ack.SnapshotSequence;
        state.ConfirmedEntities = std::move(matchIt->TargetBaseline);
        state.NeedsFullResync = false;
        ++m_stats.SnapshotAcksReceived;

        while (!state.PendingSnapshots.empty() &&
               state.PendingSnapshots.front().SnapshotSequence <= state.ConfirmedBaselineSequence)
        {
            state.PendingSnapshots.pop_front();
        }
        return true;
    }

    bool ReplicationManager::HandleResyncRequest(ConnectionHandle connection, const ResyncRequestPacket& req)
    {
        auto connIt = m_connections.find(connection.Value);
        if (connIt == m_connections.end())
        {
            return false;
        }
        // Ignore stale resync requests belonging to a previous epoch if client already fell behind epoch transition
        if (req.BaselineEpoch != 0 &&
            req.BaselineEpoch < connIt->second.BaselineEpoch &&
            !connIt->second.PendingSnapshots.empty() &&
            connIt->second.PendingSnapshots.back().IsKeyframe)
        {
            return false;
        }
        InvalidateBaseline(connIt->second);
        ++m_stats.ResyncRequestsReceived;
        ++m_stats.BaselineRecoveries;
        return true;
    }

    bool ReplicationManager::IsBaselineConfirmed(ConnectionHandle connection) const noexcept
    {
        auto it = m_connections.find(connection.Value);
        return it != m_connections.end() && it->second.ConfirmedBaselineSequence != 0 && !it->second.NeedsFullResync;
    }

    u32 ReplicationManager::GetConfirmedBaselineSequence(ConnectionHandle connection) const noexcept
    {
        auto it = m_connections.find(connection.Value);
        return it != m_connections.end() ? it->second.ConfirmedBaselineSequence : 0;
    }

    u32 ReplicationManager::GetLastReceivedAckSequence(ConnectionHandle connection) const noexcept
    {
        auto it = m_connections.find(connection.Value);
        return it != m_connections.end() ? it->second.LastReceivedAckSequence : 0;
    }

    u32 ReplicationManager::GetBaselineEpoch(ConnectionHandle connection) const noexcept
    {
        auto it = m_connections.find(connection.Value);
        return it != m_connections.end() ? it->second.BaselineEpoch : 0;
    }

    usize ReplicationManager::GetPendingSnapshotCount(ConnectionHandle connection) const noexcept
    {
        auto it = m_connections.find(connection.Value);
        return it != m_connections.end() ? it->second.PendingSnapshots.size() : 0u;
    }

    bool ReplicationManager::IsEntityVisibleToConnection(ConnectionHandle connection, NetworkId netId) const noexcept
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return false;
        }
        return it->second.ConfirmedEntities.find(netId.Pack()) != it->second.ConfirmedEntities.end();
    }

    // ------------------------------------------------------------------------
    // ClientReplicationApplier
    // ------------------------------------------------------------------------

    void ClientReplicationApplier::Reset() noexcept
    {
        m_entities.clear();
        m_lastReceivedSequence = 0;
        m_lastAppliedSequence = 0;
        m_lastAcknowledgedSequence = 0;
        m_activeBaselineEpoch = 0;
        m_lastCompletedTransferId = 0;
        m_activeWorldId = kInvalidWorldInstanceId;
        CancelPendingKeyframeTransfer();
        m_stats = ClientReplicationStats{};
    }

    void ClientReplicationApplier::PrepareForReconnect() noexcept
    {
        // Reset sequence/epoch state so next server Keyframe reconciles cleanly while preserving entity diffing
        m_lastReceivedSequence = 0;
        m_lastAppliedSequence = 0;
        m_lastAcknowledgedSequence = 0;
        m_activeBaselineEpoch = 0;
        CancelPendingKeyframeTransfer();
    }

    void ClientReplicationApplier::CancelPendingKeyframeTransfer() noexcept
    {
        m_activeTransfer = ActiveKeyframeTransfer{};
    }

    bool ClientReplicationApplier::ApplySnapshotPacket(
        const SnapshotPacket& packet,
        std::vector<ClientReplicationEvent>& outEvents,
        std::optional<SnapshotAckPacket>& outAck,
        std::optional<ResyncRequestPacket>& outResync)
    {
        outEvents.clear();
        outAck.reset();
        outResync.reset();

        if (packet.SnapshotSequence == 0 || packet.BaselineEpoch == 0)
        {
            return false;
        }

        const bool isWorldTransition =
            (m_activeWorldId != kInvalidWorldInstanceId &&
             packet.WorldId != kInvalidWorldInstanceId &&
             packet.WorldId != m_activeWorldId);

        if (isWorldTransition)
        {
            if (!packet.IsKeyframe)
            {
                ++m_stats.OutOfSyncDeltasRejected;
                ++m_stats.ResyncRequestsEmitted;
                outResync = ResyncRequestPacket{
                    .LastAppliedSequence = 0,
                    .Reason = ResyncReason::BaselineMismatch,
                    .BaselineEpoch = packet.BaselineEpoch
                };
                return false;
            }
            m_lastReceivedSequence = packet.SnapshotSequence;
        }
        else
        {
            // Reject packets from an older epoch
            if (m_activeBaselineEpoch != 0 && packet.BaselineEpoch < m_activeBaselineEpoch)
            {
                ++m_stats.StaleSnapshotsDropped;
                return false;
            }

            // Within same epoch, reject duplicate or stale sequence
            if (m_activeBaselineEpoch != 0 && packet.BaselineEpoch == m_activeBaselineEpoch)
            {
                if (packet.SnapshotSequence == m_lastAppliedSequence)
                {
                    ++m_stats.DuplicateSnapshotsDropped;
                    return false;
                }
                if (packet.SnapshotSequence < m_lastAppliedSequence)
                {
                    ++m_stats.StaleSnapshotsDropped;
                    return false;
                }
            }
            else if (!packet.IsKeyframe)
            {
                // Never accept a delta from an uninitialized/new epoch without first receiving a Keyframe
                ++m_stats.OutOfSyncDeltasRejected;
                ++m_stats.ResyncRequestsEmitted;
                outResync = ResyncRequestPacket{
                    .LastAppliedSequence = m_lastAppliedSequence,
                    .Reason = ResyncReason::BaselineMismatch,
                    .BaselineEpoch = packet.BaselineEpoch
                };
                return false;
            }

            if (packet.SnapshotSequence > m_lastReceivedSequence || packet.BaselineEpoch > m_activeBaselineEpoch)
            {
                m_lastReceivedSequence = packet.SnapshotSequence;
            }

            // Strict delta baseline check: never apply a delta against an unconfirmed or mismatched baseline!
            if (!packet.IsKeyframe && packet.BaselineSequence != m_lastAppliedSequence)
            {
                ++m_stats.OutOfSyncDeltasRejected;
                ++m_stats.ResyncRequestsEmitted;
                outResync = ResyncRequestPacket{
                    .LastAppliedSequence = m_lastAppliedSequence,
                    .Reason = ResyncReason::BaselineMismatch,
                    .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : packet.BaselineEpoch
                };
                return false;
            }
        }

        // Pre-validate delta operations before mutating local replicated state
        if (!packet.IsKeyframe)
        {
            for (const auto& entry : packet.Entries)
            {
                const u64 packed = entry.NetId.Pack();
                if (entry.Op == ReplicationOpKind::Update && m_entities.find(packed) == m_entities.end())
                {
                    ++m_stats.OutOfSyncDeltasRejected;
                    ++m_stats.ResyncRequestsEmitted;
                    outResync = ResyncRequestPacket{
                        .LastAppliedSequence = m_lastAppliedSequence,
                        .Reason = ResyncReason::BaselineMismatch,
                        .BaselineEpoch = m_activeBaselineEpoch
                    };
                    return false;
                }
            }
        }

        if (packet.IsKeyframe)
        {
            CancelPendingKeyframeTransfer();

            std::unordered_set<u64> keyframeIds;
            keyframeIds.reserve(packet.Entries.size());
            for (const auto& entry : packet.Entries)
            {
                keyframeIds.insert(entry.NetId.Pack());
            }

            // Reconcile any locally stale entity no longer in authoritative keyframe
            for (auto it = m_entities.begin(); it != m_entities.end();)
            {
                if (keyframeIds.find(it->first) == keyframeIds.end())
                {
                    outEvents.push_back({ ReplicationOpKind::Despawn, it->second.NetId });
                    it = m_entities.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            for (const auto& entry : packet.Entries)
            {
                if (entry.Op != ReplicationOpKind::Spawn)
                {
                    continue;
                }
                const u64 packed = entry.NetId.Pack();
                const bool existed = (m_entities.find(packed) != m_entities.end());
                ClientReplicatedEntity& rec = m_entities[packed];
                rec.NetId = entry.NetId;
                rec.ArchetypeHash = entry.ArchetypeHash;
                rec.OwnerPlayer = entry.OwnerPlayer;
                rec.WorldId = entry.WorldId;
                rec.Authority = entry.Authority;
                rec.EntityName = entry.EntityName;
                rec.ComponentMask = entry.ComponentMask;
                if ((entry.ComponentMask & SnapshotComponentFlags::kTransform) != 0)
                {
                    rec.Transform = entry.Transform;
                }
                if ((entry.ComponentMask & SnapshotComponentFlags::kMovement) != 0)
                {
                    rec.Movement = entry.Movement;
                }
                if ((entry.ComponentMask & SnapshotComponentFlags::kPhysics) != 0)
                {
                    rec.Physics = entry.Physics;
                }
                outEvents.push_back({ existed ? ReplicationOpKind::Update : ReplicationOpKind::Spawn, rec.NetId });
            }
        }
        else
        {
            for (const auto& entry : packet.Entries)
            {
                const u64 packed = entry.NetId.Pack();
                switch (entry.Op)
                {
                case ReplicationOpKind::Spawn:
                {
                    const bool existed = (m_entities.find(packed) != m_entities.end());
                    ClientReplicatedEntity& rec = m_entities[packed];
                    rec.NetId = entry.NetId;
                    rec.ArchetypeHash = entry.ArchetypeHash;
                    rec.OwnerPlayer = entry.OwnerPlayer;
                    rec.WorldId = entry.WorldId;
                    rec.Authority = entry.Authority;
                    rec.EntityName = entry.EntityName;
                    rec.ComponentMask = entry.ComponentMask;
                    if ((entry.ComponentMask & SnapshotComponentFlags::kTransform) != 0)
                    {
                        rec.Transform = entry.Transform;
                    }
                    if ((entry.ComponentMask & SnapshotComponentFlags::kMovement) != 0)
                    {
                        rec.Movement = entry.Movement;
                    }
                    if ((entry.ComponentMask & SnapshotComponentFlags::kPhysics) != 0)
                    {
                        rec.Physics = entry.Physics;
                    }
                    outEvents.push_back({ existed ? ReplicationOpKind::Update : ReplicationOpKind::Spawn, rec.NetId });
                    break;
                }
                case ReplicationOpKind::Update:
                {
                    auto it = m_entities.find(packed);
                    if (it != m_entities.end())
                    {
                        if ((entry.ComponentMask & SnapshotComponentFlags::kTransform) != 0)
                        {
                            it->second.Transform = entry.Transform;
                        }
                        if ((entry.ComponentMask & SnapshotComponentFlags::kMovement) != 0)
                        {
                            it->second.Movement = entry.Movement;
                        }
                        if ((entry.ComponentMask & SnapshotComponentFlags::kPhysics) != 0)
                        {
                            it->second.Physics = entry.Physics;
                        }
                        it->second.ComponentMask |= entry.ComponentMask;
                        outEvents.push_back({ ReplicationOpKind::Update, it->second.NetId });
                    }
                    break;
                }
                case ReplicationOpKind::Despawn:
                {
                    if (m_entities.erase(packed) > 0)
                    {
                        outEvents.push_back({ ReplicationOpKind::Despawn, entry.NetId });
                    }
                    break;
                }
                }
            }
        }

        m_activeWorldId = packet.WorldId;
        m_activeBaselineEpoch = packet.BaselineEpoch;
        m_lastAppliedSequence = packet.SnapshotSequence;
        m_lastAcknowledgedSequence = packet.SnapshotSequence;
        ++m_stats.SnapshotsApplied;
        outAck = SnapshotAckPacket{
            .SnapshotSequence = packet.SnapshotSequence,
            .BaselineSequence = packet.BaselineSequence,
            .AppliedSuccessfully = true,
            .BaselineEpoch = packet.BaselineEpoch
        };
        return true;
    }

    bool ClientReplicationApplier::ProcessKeyframeChunk(
        const KeyframeChunkPacket& chunk,
        u64 nowMs,
        std::vector<ClientReplicationEvent>& outEvents,
        std::optional<SnapshotAckPacket>& outAck,
        std::optional<ResyncRequestPacket>& outResync)
    {
        outEvents.clear();
        outAck.reset();
        outResync.reset();

        if (chunk.WireVersion != ReplicationConstants::kSnapshotWireVersion ||
            chunk.TransferId == 0 ||
            chunk.SnapshotSequence == 0 ||
            chunk.BaselineEpoch == 0 ||
            chunk.TotalChunks == 0 ||
            chunk.TotalChunks > ReplicationConstants::kMaxKeyframeChunks ||
            chunk.ChunkIndex >= chunk.TotalChunks ||
            chunk.TotalBytes == 0 ||
            chunk.TotalBytes > ReplicationConstants::kMaxKeyframeTotalBytes ||
            chunk.ChunkData.empty() ||
            chunk.ChunkData.size() > ReplicationConstants::kMaxKeyframeChunkPayloadBytes ||
            chunk.ChunkData.size() > chunk.TotalBytes)
        {
            CancelPendingKeyframeTransfer();
            ++m_stats.ChunkedKeyframesFailed;
            ++m_stats.ResyncRequestsEmitted;
            outResync = ResyncRequestPacket{
                .LastAppliedSequence = m_lastAppliedSequence,
                .Reason = ResyncReason::DecodeFailure,
                .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : 1u
            };
            return false;
        }

        const bool isWorldTransition =
            (m_activeWorldId != kInvalidWorldInstanceId &&
             chunk.WorldId != kInvalidWorldInstanceId &&
             chunk.WorldId != m_activeWorldId);

        // Ignore chunks belonging to older epochs or already-applied snapshots/transfers within the same world
        if (!isWorldTransition && m_activeBaselineEpoch != 0 && chunk.BaselineEpoch < m_activeBaselineEpoch)
        {
            ++m_stats.StaleSnapshotsDropped;
            return false;
        }
        if (!isWorldTransition &&
            m_activeBaselineEpoch != 0 &&
            chunk.BaselineEpoch == m_activeBaselineEpoch &&
            chunk.SnapshotSequence <= m_lastAppliedSequence)
        {
            if (chunk.TransferId == m_lastCompletedTransferId)
            {
                ++m_stats.DuplicateChunksIgnored;
            }
            else
            {
                ++m_stats.StaleSnapshotsDropped;
            }
            return false;
        }

        if (m_activeTransfer.Active && chunk.TransferId != m_activeTransfer.TransferId)
        {
            // Ignore chunks from an older superseded transfer
            const bool isOlderEpoch = (chunk.BaselineEpoch < m_activeTransfer.BaselineEpoch);
            const bool isSameEpochOlderSeq =
                (chunk.BaselineEpoch == m_activeTransfer.BaselineEpoch &&
                 chunk.SnapshotSequence < m_activeTransfer.SnapshotSequence);
            const bool isOlderTransferSameSeq =
                (chunk.BaselineEpoch == m_activeTransfer.BaselineEpoch &&
                 chunk.SnapshotSequence == m_activeTransfer.SnapshotSequence &&
                 chunk.TransferId < m_activeTransfer.TransferId);
            if (isOlderEpoch || isSameEpochOlderSeq || isOlderTransferSameSeq)
            {
                ++m_stats.StaleSnapshotsDropped;
                return false;
            }

            // Newer transfer supersedes old incomplete transfer cleanly
            CancelPendingKeyframeTransfer();
        }

        if (!m_activeTransfer.Active)
        {
            m_activeTransfer.Active = true;
            m_activeTransfer.TransferId = chunk.TransferId;
            m_activeTransfer.SnapshotSequence = chunk.SnapshotSequence;
            m_activeTransfer.BaselineEpoch = chunk.BaselineEpoch;
            m_activeTransfer.WorldId = chunk.WorldId;
            m_activeTransfer.ServerTick = chunk.ServerTick;
            m_activeTransfer.TotalChunks = chunk.TotalChunks;
            m_activeTransfer.TotalBytes = chunk.TotalBytes;
            m_activeTransfer.ExpectedDigest32 = chunk.PayloadDigest32;
            m_activeTransfer.StartedAtMs = nowMs;
            m_activeTransfer.LastChunkAtMs = nowMs;
            m_activeTransfer.ReceivedChunkCount = 0;
            m_activeTransfer.AccumulatedBytes = 0;
            m_activeTransfer.ChunkReceived.assign(chunk.TotalChunks, false);
            m_activeTransfer.Chunks.resize(chunk.TotalChunks);
        }
        else
        {
            // Verify metadata matches active transfer identically
            if (chunk.SnapshotSequence != m_activeTransfer.SnapshotSequence ||
                chunk.BaselineEpoch != m_activeTransfer.BaselineEpoch ||
                chunk.WorldId != m_activeTransfer.WorldId ||
                chunk.TotalChunks != m_activeTransfer.TotalChunks ||
                chunk.TotalBytes != m_activeTransfer.TotalBytes ||
                chunk.PayloadDigest32 != m_activeTransfer.ExpectedDigest32)
            {
                CancelPendingKeyframeTransfer();
                ++m_stats.ChunkedKeyframesFailed;
                ++m_stats.ResyncRequestsEmitted;
                outResync = ResyncRequestPacket{
                    .LastAppliedSequence = m_lastAppliedSequence,
                    .Reason = ResyncReason::DecodeFailure,
                    .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : chunk.BaselineEpoch
                };
                return false;
            }
        }

        m_activeTransfer.LastChunkAtMs = nowMs;
        if (m_activeTransfer.ChunkReceived[chunk.ChunkIndex])
        {
            ++m_stats.DuplicateChunksIgnored;
            return false;
        }

        if (m_activeTransfer.AccumulatedBytes + chunk.ChunkData.size() > m_activeTransfer.TotalBytes)
        {
            CancelPendingKeyframeTransfer();
            ++m_stats.ChunkedKeyframesFailed;
            ++m_stats.ResyncRequestsEmitted;
            outResync = ResyncRequestPacket{
                .LastAppliedSequence = m_lastAppliedSequence,
                .Reason = ResyncReason::DecodeFailure,
                .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : chunk.BaselineEpoch
            };
            return false;
        }

        m_activeTransfer.ChunkReceived[chunk.ChunkIndex] = true;
        m_activeTransfer.Chunks[chunk.ChunkIndex] = chunk.ChunkData;
        m_activeTransfer.AccumulatedBytes += chunk.ChunkData.size();
        ++m_activeTransfer.ReceivedChunkCount;

        if (chunk.SnapshotSequence > m_lastReceivedSequence || chunk.BaselineEpoch > m_activeBaselineEpoch)
        {
            m_lastReceivedSequence = chunk.SnapshotSequence;
        }

        if (m_activeTransfer.ReceivedChunkCount < m_activeTransfer.TotalChunks)
        {
            // Still awaiting remaining chunks; keep client world state untouched
            return false;
        }

        // All chunks received: assemble, verify exact total size + digest + decode atomically
        if (m_activeTransfer.AccumulatedBytes != m_activeTransfer.TotalBytes)
        {
            CancelPendingKeyframeTransfer();
            ++m_stats.ChunkedKeyframesFailed;
            ++m_stats.ResyncRequestsEmitted;
            outResync = ResyncRequestPacket{
                .LastAppliedSequence = m_lastAppliedSequence,
                .Reason = ResyncReason::DecodeFailure,
                .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : chunk.BaselineEpoch
            };
            return false;
        }

        std::vector<u8> assembled;
        assembled.reserve(m_activeTransfer.TotalBytes);
        for (const auto& part : m_activeTransfer.Chunks)
        {
            assembled.insert(assembled.end(), part.begin(), part.end());
        }

        const u32 actualDigest = ReplicationCodec::ComputeDigest32(assembled);
        const u32 expectedSeq = m_activeTransfer.SnapshotSequence;
        const u32 expectedEpoch = m_activeTransfer.BaselineEpoch;
        const u32 completedTransferId = m_activeTransfer.TransferId;
        const u32 expectedDigest = m_activeTransfer.ExpectedDigest32;
        CancelPendingKeyframeTransfer();

        if (actualDigest != expectedDigest)
        {
            ++m_stats.ChunkedKeyframesFailed;
            ++m_stats.ResyncRequestsEmitted;
            outResync = ResyncRequestPacket{
                .LastAppliedSequence = m_lastAppliedSequence,
                .Reason = ResyncReason::DecodeFailure,
                .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : expectedEpoch
            };
            return false;
        }

        SnapshotPacket decoded{};
        BitReader reader(assembled);
        if (!ReplicationCodec::ReadSnapshotPacket(reader, decoded) ||
            !decoded.IsKeyframe ||
            decoded.SnapshotSequence != expectedSeq ||
            decoded.BaselineEpoch != expectedEpoch)
        {
            ++m_stats.ChunkedKeyframesFailed;
            ++m_stats.ResyncRequestsEmitted;
            outResync = ResyncRequestPacket{
                .LastAppliedSequence = m_lastAppliedSequence,
                .Reason = ResyncReason::DecodeFailure,
                .BaselineEpoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : expectedEpoch
            };
            return false;
        }

        if (!ApplySnapshotPacket(decoded, outEvents, outAck, outResync))
        {
            return false;
        }

        m_lastCompletedTransferId = completedTransferId;
        ++m_stats.ChunkedKeyframesCompleted;
        return true;
    }

    bool ClientReplicationApplier::TickKeyframeTimeouts(
        u64 nowMs,
        std::optional<ResyncRequestPacket>& outResync,
        u64 timeoutMs)
    {
        outResync.reset();
        if (!m_activeTransfer.Active || timeoutMs == 0)
        {
            return false;
        }
        if (nowMs >= m_activeTransfer.LastChunkAtMs &&
            (nowMs - m_activeTransfer.LastChunkAtMs) >= timeoutMs)
        {
            const u32 epoch = m_activeBaselineEpoch != 0 ? m_activeBaselineEpoch : m_activeTransfer.BaselineEpoch;
            CancelPendingKeyframeTransfer();
            ++m_stats.ChunkedKeyframesTimedOut;
            ++m_stats.ResyncRequestsEmitted;
            outResync = ResyncRequestPacket{
                .LastAppliedSequence = m_lastAppliedSequence,
                .Reason = ResyncReason::DecodeFailure,
                .BaselineEpoch = epoch
            };
            return true;
        }
        return false;
    }

    bool ClientReplicationApplier::TryGetEntity(NetworkId netId, ClientReplicatedEntity& outEntity) const
    {
        if (!netId.IsValid())
        {
            return false;
        }
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end())
        {
            return false;
        }
        outEntity = it->second;
        return true;
    }

    // ------------------------------------------------------------------------
    // RemoteInterpolationBuffer
    // ------------------------------------------------------------------------

    void RemoteInterpolationBuffer::SetInterpolationDelaySec(f64 delaySec) noexcept
    {
        if (std::isfinite(delaySec))
        {
            m_interpolationDelaySec = std::clamp(delaySec, 0.0, 0.5);
        }
    }

    void RemoteInterpolationBuffer::Clear() noexcept
    {
        m_samples.clear();
    }

    void RemoteInterpolationBuffer::RemoveEntity(NetworkId netId) noexcept
    {
        if (netId.IsValid())
        {
            m_samples.erase(netId.Pack());
        }
    }

    void RemoteInterpolationBuffer::PushSample(
        NetworkId netId,
        u64 serverTick,
        f64 receiveTimeSec,
        const ReplicatedTransformData& transform)
    {
        if (!netId.IsValid() || !transform.IsValid() || !std::isfinite(receiveTimeSec))
        {
            return;
        }
        auto& queue = m_samples[netId.Pack()];
        if (!queue.empty())
        {
            const auto& back = queue.back();
            if (receiveTimeSec <= back.ReceiveTimeSec)
            {
                // Keep monotonic timeline; update latest if same tick/time
                if (serverTick >= back.ServerTick)
                {
                    queue.back().ServerTick = serverTick;
                    queue.back().Transform = transform;
                }
                return;
            }
        }
        if (queue.size() >= kMaxSamplesPerEntity)
        {
            queue.pop_front();
        }
        queue.push_back(RemoteSnapshotSample{
            .ServerTick = serverTick,
            .ReceiveTimeSec = receiveTimeSec,
            .Transform = transform
        });
    }

    bool RemoteInterpolationBuffer::SampleEntity(
        NetworkId netId,
        f64 renderTimeSec,
        ReplicatedTransformData& outTransform) const noexcept
    {
        if (!netId.IsValid() || !std::isfinite(renderTimeSec))
        {
            return false;
        }
        auto it = m_samples.find(netId.Pack());
        if (it == m_samples.end() || it->second.empty())
        {
            return false;
        }

        const auto& queue = it->second;
        if (queue.size() == 1)
        {
            outTransform = queue.front().Transform;
            return true;
        }

        const f64 targetTime = renderTimeSec - m_interpolationDelaySec;
        if (targetTime <= queue.front().ReceiveTimeSec)
        {
            outTransform = queue.front().Transform;
            return true;
        }
        if (targetTime >= queue.back().ReceiveTimeSec)
        {
            outTransform = queue.back().Transform;
            return true;
        }

        for (usize i = 0; i + 1 < queue.size(); ++i)
        {
            const auto& a = queue[i];
            const auto& b = queue[i + 1];
            if (targetTime >= a.ReceiveTimeSec && targetTime <= b.ReceiveTimeSec)
            {
                const f64 span = b.ReceiveTimeSec - a.ReceiveTimeSec;
                const f32 alpha = (span > 1e-7)
                    ? static_cast<f32>(std::clamp((targetTime - a.ReceiveTimeSec) / span, 0.0, 1.0))
                    : 1.0f;

                const auto& ta = a.Transform;
                const auto& tb = b.Transform;

                outTransform.PosX = ta.PosX + (tb.PosX - ta.PosX) * alpha;
                outTransform.PosY = ta.PosY + (tb.PosY - ta.PosY) * alpha;
                outTransform.PosZ = ta.PosZ + (tb.PosZ - ta.PosZ) * alpha;

                outTransform.ScaleX = ta.ScaleX + (tb.ScaleX - ta.ScaleX) * alpha;
                outTransform.ScaleY = ta.ScaleY + (tb.ScaleY - ta.ScaleY) * alpha;
                outTransform.ScaleZ = ta.ScaleZ + (tb.ScaleZ - ta.ScaleZ) * alpha;

                // Shortest-path normalized quaternion Nlerp
                const f32 dot = ta.RotX * tb.RotX + ta.RotY * tb.RotY + ta.RotZ * tb.RotZ + ta.RotW * tb.RotW;
                const f32 sign = (dot < 0.0f) ? -1.0f : 1.0f;
                f32 rx = ta.RotX + (tb.RotX * sign - ta.RotX) * alpha;
                f32 ry = ta.RotY + (tb.RotY * sign - ta.RotY) * alpha;
                f32 rz = ta.RotZ + (tb.RotZ * sign - ta.RotZ) * alpha;
                f32 rw = ta.RotW + (tb.RotW * sign - ta.RotW) * alpha;
                const f32 lenSq = rx * rx + ry * ry + rz * rz + rw * rw;
                if (lenSq > 1e-8f)
                {
                    const f32 inv = 1.0f / std::sqrt(lenSq);
                    rx *= inv;
                    ry *= inv;
                    rz *= inv;
                    rw *= inv;
                }
                else
                {
                    rx = 0.0f;
                    ry = 0.0f;
                    rz = 0.0f;
                    rw = 1.0f;
                }
                outTransform.RotX = rx;
                outTransform.RotY = ry;
                outTransform.RotZ = rz;
                outTransform.RotW = rw;
                return true;
            }
        }

        outTransform = queue.back().Transform;
        return true;
    }

    usize RemoteInterpolationBuffer::GetSampleCount(NetworkId netId) const noexcept
    {
        if (!netId.IsValid())
        {
            return 0u;
        }
        auto it = m_samples.find(netId.Pack());
        return it != m_samples.end() ? it->second.size() : 0u;
    }
}
