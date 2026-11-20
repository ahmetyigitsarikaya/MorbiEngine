#include "Morbi/Network/Replication/ReplicationContracts.hpp"

#include <algorithm>
#include <cmath>

namespace Morbi::Network
{
    namespace
    {
        [[nodiscard]] bool IsFiniteBounded(f32 v, f32 maxAbs) noexcept
        {
            return std::isfinite(v) && std::abs(v) <= maxAbs;
        }
    }

    bool ReplicatedTransformData::IsValid() const noexcept
    {
        if (!IsFiniteBounded(PosX, ReplicationConstants::kMaxWorldCoordAbs) ||
            !IsFiniteBounded(PosY, ReplicationConstants::kMaxWorldCoordAbs) ||
            !IsFiniteBounded(PosZ, ReplicationConstants::kMaxWorldCoordAbs))
        {
            return false;
        }

        if (!std::isfinite(RotX) || !std::isfinite(RotY) || !std::isfinite(RotZ) || !std::isfinite(RotW))
        {
            return false;
        }
        const f32 quatLenSq = RotX * RotX + RotY * RotY + RotZ * RotZ + RotW * RotW;
        if (quatLenSq < 0.25f || quatLenSq > 2.25f)
        {
            return false;
        }

        auto validScale = [](f32 s) noexcept {
            return std::isfinite(s) && s >= ReplicationConstants::kMinScale && s <= ReplicationConstants::kMaxScale;
        };
        return validScale(ScaleX) && validScale(ScaleY) && validScale(ScaleZ);
    }

    bool ReplicatedTransformData::ApproximatelyEquals(const ReplicatedTransformData& other, f32 eps) const noexcept
    {
        return std::abs(PosX - other.PosX) <= eps &&
            std::abs(PosY - other.PosY) <= eps &&
            std::abs(PosZ - other.PosZ) <= eps &&
            std::abs(RotX - other.RotX) <= eps &&
            std::abs(RotY - other.RotY) <= eps &&
            std::abs(RotZ - other.RotZ) <= eps &&
            std::abs(RotW - other.RotW) <= eps &&
            std::abs(ScaleX - other.ScaleX) <= eps &&
            std::abs(ScaleY - other.ScaleY) <= eps &&
            std::abs(ScaleZ - other.ScaleZ) <= eps;
    }

    bool ReplicatedTransformData::NetSerialize(BitWriter& writer) const
    {
        if (!IsValid())
        {
            return false;
        }
        return writer.WriteF32(PosX) &&
            writer.WriteF32(PosY) &&
            writer.WriteF32(PosZ) &&
            writer.WriteF32(RotX) &&
            writer.WriteF32(RotY) &&
            writer.WriteF32(RotZ) &&
            writer.WriteF32(RotW) &&
            writer.WriteF32(ScaleX) &&
            writer.WriteF32(ScaleY) &&
            writer.WriteF32(ScaleZ);
    }

    bool ReplicatedTransformData::NetDeserialize(BitReader& reader, u16 schemaVersion)
    {
        if (schemaVersion != ReplicationConstants::kTransformSchemaVersion)
        {
            return false;
        }
        if (!reader.ReadF32(PosX) ||
            !reader.ReadF32(PosY) ||
            !reader.ReadF32(PosZ) ||
            !reader.ReadF32(RotX) ||
            !reader.ReadF32(RotY) ||
            !reader.ReadF32(RotZ) ||
            !reader.ReadF32(RotW) ||
            !reader.ReadF32(ScaleX) ||
            !reader.ReadF32(ScaleY) ||
            !reader.ReadF32(ScaleZ))
        {
            return false;
        }
        if (!IsValid())
        {
            return false;
        }
        const f32 len = std::sqrt(RotX * RotX + RotY * RotY + RotZ * RotZ + RotW * RotW);
        if (len > 1e-6f)
        {
            const f32 inv = 1.0f / len;
            RotX *= inv;
            RotY *= inv;
            RotZ *= inv;
            RotW *= inv;
        }
        return true;
    }

    bool ReplicatedMovementData::IsValid() const noexcept
    {
        return IsFiniteBounded(VelX, ReplicationConstants::kMaxSpeedAbs) &&
            IsFiniteBounded(VelY, ReplicationConstants::kMaxSpeedAbs) &&
            IsFiniteBounded(VelZ, ReplicationConstants::kMaxSpeedAbs) &&
            IsFiniteBounded(YawRadians, 100.0f);
    }

    bool ReplicatedMovementData::ApproximatelyEquals(const ReplicatedMovementData& other, f32 eps) const noexcept
    {
        return LastProcessedInputSeq == other.LastProcessedInputSeq &&
            MovementFlags == other.MovementFlags &&
            std::abs(VelX - other.VelX) <= eps &&
            std::abs(VelY - other.VelY) <= eps &&
            std::abs(VelZ - other.VelZ) <= eps &&
            std::abs(YawRadians - other.YawRadians) <= eps;
    }

    bool ReplicatedMovementData::NetSerialize(BitWriter& writer) const
    {
        if (!IsValid())
        {
            return false;
        }
        return writer.WriteF32(VelX) &&
            writer.WriteF32(VelY) &&
            writer.WriteF32(VelZ) &&
            writer.WriteF32(YawRadians) &&
            writer.WriteU32(LastProcessedInputSeq) &&
            writer.WriteU8(MovementFlags);
    }

    bool ReplicatedMovementData::NetDeserialize(BitReader& reader, u16 schemaVersion)
    {
        if (schemaVersion != ReplicationConstants::kMovementSchemaVersion)
        {
            return false;
        }
        if (!reader.ReadF32(VelX) ||
            !reader.ReadF32(VelY) ||
            !reader.ReadF32(VelZ) ||
            !reader.ReadF32(YawRadians) ||
            !reader.ReadU32(LastProcessedInputSeq) ||
            !reader.ReadU8(MovementFlags))
        {
            return false;
        }
        return IsValid();
    }

    bool ReplicatedPhysicsStateData::IsValid() const noexcept
    {
        return std::isfinite(LinearVelX) && std::isfinite(LinearVelY) && std::isfinite(LinearVelZ) &&
            std::isfinite(AngularVelX) && std::isfinite(AngularVelY) && std::isfinite(AngularVelZ) &&
            std::fabs(LinearVelX) <= ReplicationConstants::kMaxSpeedAbs &&
            std::fabs(LinearVelY) <= ReplicationConstants::kMaxSpeedAbs &&
            std::fabs(LinearVelZ) <= ReplicationConstants::kMaxSpeedAbs &&
            std::fabs(AngularVelX) <= ReplicationConstants::kMaxAngularSpeedAbs &&
            std::fabs(AngularVelY) <= ReplicationConstants::kMaxAngularSpeedAbs &&
            std::fabs(AngularVelZ) <= ReplicationConstants::kMaxAngularSpeedAbs;
    }

    bool ReplicatedPhysicsStateData::ApproximatelyEquals(const ReplicatedPhysicsStateData& other, f32 eps) const noexcept
    {
        return std::fabs(LinearVelX - other.LinearVelX) <= eps &&
            std::fabs(LinearVelY - other.LinearVelY) <= eps &&
            std::fabs(LinearVelZ - other.LinearVelZ) <= eps &&
            std::fabs(AngularVelX - other.AngularVelX) <= eps &&
            std::fabs(AngularVelY - other.AngularVelY) <= eps &&
            std::fabs(AngularVelZ - other.AngularVelZ) <= eps &&
            PhysicsFlags == other.PhysicsFlags;
    }

    bool ReplicatedPhysicsStateData::NetSerialize(BitWriter& writer) const
    {
        if (!IsValid())
        {
            return false;
        }
        return writer.WriteF32(LinearVelX) &&
            writer.WriteF32(LinearVelY) &&
            writer.WriteF32(LinearVelZ) &&
            writer.WriteF32(AngularVelX) &&
            writer.WriteF32(AngularVelY) &&
            writer.WriteF32(AngularVelZ) &&
            writer.WriteU8(PhysicsFlags);
    }

    bool ReplicatedPhysicsStateData::NetDeserialize(BitReader& reader, u16 schemaVersion)
    {
        if (schemaVersion != ReplicationConstants::kPhysicsSchemaVersion)
        {
            return false;
        }
        if (!reader.ReadF32(LinearVelX) ||
            !reader.ReadF32(LinearVelY) ||
            !reader.ReadF32(LinearVelZ) ||
            !reader.ReadF32(AngularVelX) ||
            !reader.ReadF32(AngularVelY) ||
            !reader.ReadF32(AngularVelZ) ||
            !reader.ReadU8(PhysicsFlags))
        {
            return false;
        }
        return IsValid();
    }

    u32 ReplicationCodec::ComputeDigest32(std::span<const u8> data) noexcept
    {
        u32 crc = 0xFFFFFFFFu;
        for (const u8 byte : data)
        {
            crc ^= static_cast<u32>(byte);
            for (int bit = 0; bit < 8; ++bit)
            {
                const u32 mask = 0u - (crc & 1u);
                crc = (crc >> 1u) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }

    bool ReplicationCodec::WriteSnapshotPacket(BitWriter& writer, const SnapshotPacket& packet)
    {
        const usize maxEntries = packet.IsKeyframe
            ? ReplicationConstants::kMaxKeyframeEntries
            : ReplicationConstants::kMaxSnapshotEntriesPerPacket;
        if (packet.SnapshotSequence == 0 ||
            packet.BaselineEpoch == 0 ||
            packet.Entries.size() > maxEntries)
        {
            return false;
        }
        if (packet.IsKeyframe && packet.BaselineSequence != 0)
        {
            return false;
        }
        if (!packet.IsKeyframe && packet.BaselineSequence == 0)
        {
            return false;
        }

        if (!writer.WriteU16(packet.WireVersion) ||
            !writer.WriteU32(packet.SnapshotSequence) ||
            !writer.WriteU32(packet.BaselineSequence) ||
            !writer.WriteU8(packet.IsKeyframe ? 1u : 0u) ||
            !writer.WriteU32(packet.WorldId) ||
            !writer.WriteU64(packet.ServerTick) ||
            !writer.WriteU16(static_cast<u16>(packet.Entries.size())) ||
            !writer.WriteU32(packet.BaselineEpoch))
        {
            return false;
        }

        for (const auto& entry : packet.Entries)
        {
            if (!entry.NetId.IsValid() || entry.EntityName.size() > ReplicationConstants::kMaxEntityNameBytes)
            {
                return false;
            }
            if (!writer.WriteU8(static_cast<u8>(entry.Op)) ||
                !writer.WriteU32(entry.NetId.Index) ||
                !writer.WriteU32(entry.NetId.Generation))
            {
                return false;
            }
            if (entry.Op == ReplicationOpKind::Despawn)
            {
                continue;
            }

            if (entry.Op == ReplicationOpKind::Spawn)
            {
                if (!writer.WriteU32(entry.ArchetypeHash) ||
                    !writer.WriteU64(entry.OwnerPlayer) ||
                    !writer.WriteU32(entry.WorldId) ||
                    !writer.WriteU8(static_cast<u8>(entry.Authority)) ||
                    !writer.WriteString(entry.EntityName, ReplicationConstants::kMaxEntityNameBytes))
                {
                    return false;
                }
            }

            if (!writer.WriteU8(entry.ComponentMask))
            {
                return false;
            }
            if ((entry.ComponentMask & SnapshotComponentFlags::kTransform) != 0)
            {
                if (!writer.WriteU16(ReplicationConstants::kTransformSchemaVersion) ||
                    !entry.Transform.NetSerialize(writer))
                {
                    return false;
                }
            }
            if ((entry.ComponentMask & SnapshotComponentFlags::kMovement) != 0)
            {
                if (!writer.WriteU16(ReplicationConstants::kMovementSchemaVersion) ||
                    !entry.Movement.NetSerialize(writer))
                {
                    return false;
                }
            }
            if ((entry.ComponentMask & SnapshotComponentFlags::kPhysics) != 0)
            {
                if (!writer.WriteU16(ReplicationConstants::kPhysicsSchemaVersion) ||
                    !entry.Physics.NetSerialize(writer))
                {
                    return false;
                }
            }
        }
        return !writer.HasError();
    }

    bool ReplicationCodec::ReadSnapshotPacket(BitReader& reader, SnapshotPacket& outPacket)
    {
        outPacket = SnapshotPacket{};
        u8 keyframeByte = 0;
        u16 entryCount = 0;

        if (!reader.ReadU16(outPacket.WireVersion) ||
            outPacket.WireVersion != ReplicationConstants::kSnapshotWireVersion ||
            !reader.ReadU32(outPacket.SnapshotSequence) ||
            outPacket.SnapshotSequence == 0 ||
            !reader.ReadU32(outPacket.BaselineSequence) ||
            !reader.ReadU8(keyframeByte) ||
            keyframeByte > 1 ||
            !reader.ReadU32(outPacket.WorldId) ||
            !reader.ReadU64(outPacket.ServerTick) ||
            !reader.ReadU16(entryCount))
        {
            return false;
        }

        outPacket.IsKeyframe = (keyframeByte != 0);
        if (outPacket.IsKeyframe && outPacket.BaselineSequence != 0)
        {
            return false;
        }
        if (!outPacket.IsKeyframe && outPacket.BaselineSequence == 0)
        {
            return false;
        }
        const usize maxEntries = outPacket.IsKeyframe
            ? ReplicationConstants::kMaxKeyframeEntries
            : ReplicationConstants::kMaxSnapshotEntriesPerPacket;
        if (entryCount > maxEntries)
        {
            return false;
        }
        if (!reader.ReadU32(outPacket.BaselineEpoch) || outPacket.BaselineEpoch == 0)
        {
            return false;
        }

        outPacket.Entries.reserve(entryCount);
        for (u16 i = 0; i < entryCount; ++i)
        {
            SnapshotEntityEntry entry{};
            u8 opRaw = 0;
            if (!reader.ReadU8(opRaw) ||
                opRaw < static_cast<u8>(ReplicationOpKind::Spawn) ||
                opRaw > static_cast<u8>(ReplicationOpKind::Despawn) ||
                !reader.ReadU32(entry.NetId.Index) ||
                !reader.ReadU32(entry.NetId.Generation) ||
                !entry.NetId.IsValid())
            {
                return false;
            }
            entry.Op = static_cast<ReplicationOpKind>(opRaw);

            if (entry.Op == ReplicationOpKind::Despawn)
            {
                outPacket.Entries.push_back(std::move(entry));
                continue;
            }

            if (entry.Op == ReplicationOpKind::Spawn)
            {
                u8 authRaw = 0;
                if (!reader.ReadU32(entry.ArchetypeHash) ||
                    !reader.ReadU64(entry.OwnerPlayer) ||
                    !reader.ReadU32(entry.WorldId) ||
                    !reader.ReadU8(authRaw) ||
                    authRaw > static_cast<u8>(NetworkAuthority::ClientPredicted) ||
                    !reader.ReadString(entry.EntityName, ReplicationConstants::kMaxEntityNameBytes))
                {
                    return false;
                }
                entry.Authority = static_cast<NetworkAuthority>(authRaw);
            }

            if (!reader.ReadU8(entry.ComponentMask))
            {
                return false;
            }
            if ((entry.ComponentMask & ~(SnapshotComponentFlags::kTransform |
                                          SnapshotComponentFlags::kMovement |
                                          SnapshotComponentFlags::kPhysics)) != 0)
            {
                return false;
            }

            if ((entry.ComponentMask & SnapshotComponentFlags::kTransform) != 0)
            {
                u16 schemaVer = 0;
                if (!reader.ReadU16(schemaVer) || !entry.Transform.NetDeserialize(reader, schemaVer))
                {
                    return false;
                }
            }
            if ((entry.ComponentMask & SnapshotComponentFlags::kMovement) != 0)
            {
                u16 schemaVer = 0;
                if (!reader.ReadU16(schemaVer) || !entry.Movement.NetDeserialize(reader, schemaVer))
                {
                    return false;
                }
            }
            if ((entry.ComponentMask & SnapshotComponentFlags::kPhysics) != 0)
            {
                u16 schemaVer = 0;
                if (!reader.ReadU16(schemaVer) || !entry.Physics.NetDeserialize(reader, schemaVer))
                {
                    return false;
                }
            }

            outPacket.Entries.push_back(std::move(entry));
        }

        return !reader.HasError() && reader.GetRemainingBits() == 0;
    }

    bool ReplicationCodec::WriteKeyframeChunk(BitWriter& writer, const KeyframeChunkPacket& chunk)
    {
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
            return false;
        }

        const u16 chunkLen = static_cast<u16>(chunk.ChunkData.size());
        return writer.WriteU16(chunk.WireVersion) &&
            writer.WriteU32(chunk.TransferId) &&
            writer.WriteU32(chunk.SnapshotSequence) &&
            writer.WriteU32(chunk.BaselineEpoch) &&
            writer.WriteU32(chunk.WorldId) &&
            writer.WriteU64(chunk.ServerTick) &&
            writer.WriteU16(chunk.ChunkIndex) &&
            writer.WriteU16(chunk.TotalChunks) &&
            writer.WriteU32(chunk.TotalBytes) &&
            writer.WriteU32(chunk.PayloadDigest32) &&
            writer.WriteU16(chunkLen) &&
            writer.WriteBytes(chunk.ChunkData) &&
            !writer.HasError();
    }

    bool ReplicationCodec::ReadKeyframeChunk(BitReader& reader, KeyframeChunkPacket& outChunk)
    {
        outChunk = KeyframeChunkPacket{};
        u16 chunkLen = 0;
        if (!reader.ReadU16(outChunk.WireVersion) ||
            outChunk.WireVersion != ReplicationConstants::kSnapshotWireVersion ||
            !reader.ReadU32(outChunk.TransferId) ||
            outChunk.TransferId == 0 ||
            !reader.ReadU32(outChunk.SnapshotSequence) ||
            outChunk.SnapshotSequence == 0 ||
            !reader.ReadU32(outChunk.BaselineEpoch) ||
            outChunk.BaselineEpoch == 0 ||
            !reader.ReadU32(outChunk.WorldId) ||
            !reader.ReadU64(outChunk.ServerTick) ||
            !reader.ReadU16(outChunk.ChunkIndex) ||
            !reader.ReadU16(outChunk.TotalChunks) ||
            outChunk.TotalChunks == 0 ||
            outChunk.TotalChunks > ReplicationConstants::kMaxKeyframeChunks ||
            outChunk.ChunkIndex >= outChunk.TotalChunks ||
            !reader.ReadU32(outChunk.TotalBytes) ||
            outChunk.TotalBytes == 0 ||
            outChunk.TotalBytes > ReplicationConstants::kMaxKeyframeTotalBytes ||
            !reader.ReadU32(outChunk.PayloadDigest32) ||
            !reader.ReadU16(chunkLen) ||
            chunkLen == 0 ||
            chunkLen > ReplicationConstants::kMaxKeyframeChunkPayloadBytes ||
            chunkLen > outChunk.TotalBytes)
        {
            return false;
        }

        outChunk.ChunkData.resize(chunkLen);
        if (!reader.ReadBytes(outChunk.ChunkData) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            outChunk.ChunkData.clear();
            return false;
        }
        return true;
    }

    bool ReplicationCodec::WriteSnapshotAck(BitWriter& writer, const SnapshotAckPacket& ack)
    {
        if (ack.SnapshotSequence == 0 || ack.BaselineEpoch == 0)
        {
            return false;
        }
        return writer.WriteU32(ack.SnapshotSequence) &&
            writer.WriteU32(ack.BaselineSequence) &&
            writer.WriteU8(ack.AppliedSuccessfully ? 1u : 0u) &&
            writer.WriteU32(ack.BaselineEpoch);
    }

    bool ReplicationCodec::ReadSnapshotAck(BitReader& reader, SnapshotAckPacket& outAck)
    {
        outAck = SnapshotAckPacket{};
        u8 appliedByte = 0;
        if (!reader.ReadU32(outAck.SnapshotSequence) ||
            outAck.SnapshotSequence == 0 ||
            !reader.ReadU32(outAck.BaselineSequence) ||
            !reader.ReadU8(appliedByte) ||
            appliedByte > 1 ||
            !reader.ReadU32(outAck.BaselineEpoch) ||
            outAck.BaselineEpoch == 0 ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            return false;
        }
        outAck.AppliedSuccessfully = (appliedByte != 0);
        return true;
    }

    bool ReplicationCodec::WriteResyncRequest(BitWriter& writer, const ResyncRequestPacket& req)
    {
        return writer.WriteU32(req.LastAppliedSequence) &&
            writer.WriteU8(static_cast<u8>(req.Reason)) &&
            writer.WriteU32(req.BaselineEpoch);
    }

    bool ReplicationCodec::ReadResyncRequest(BitReader& reader, ResyncRequestPacket& outReq)
    {
        outReq = ResyncRequestPacket{};
        u8 reasonRaw = 0;
        if (!reader.ReadU32(outReq.LastAppliedSequence) ||
            !reader.ReadU8(reasonRaw) ||
            reasonRaw < static_cast<u8>(ResyncReason::BaselineMismatch) ||
            reasonRaw > static_cast<u8>(ResyncReason::ExplicitRequest) ||
            !reader.ReadU32(outReq.BaselineEpoch) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            return false;
        }
        outReq.Reason = static_cast<ResyncReason>(reasonRaw);
        return true;
    }

    bool ManagedValue::IsValid() const noexcept
    {
        switch (Type)
        {
        case ManagedValueType::Int32:
        case ManagedValueType::Boolean:
        case ManagedValueType::UInt64:
            return true;
        case ManagedValueType::Float32:
            return std::isfinite(FloatVal);
        case ManagedValueType::Vec3:
            return std::isfinite(VecX) && std::isfinite(VecY) && std::isfinite(VecZ) &&
                std::fabs(VecX) <= ReplicationConstants::kMaxWorldCoordAbs &&
                std::fabs(VecY) <= ReplicationConstants::kMaxWorldCoordAbs &&
                std::fabs(VecZ) <= ReplicationConstants::kMaxWorldCoordAbs;
        case ManagedValueType::String:
            return StringVal.size() <= ReplicationConstants::kMaxManagedStringBytes;
        default:
            return false;
        }
    }

    bool ManagedValue::Equals(const ManagedValue& other) const noexcept
    {
        if (Type != other.Type)
        {
            return false;
        }
        switch (Type)
        {
        case ManagedValueType::Int32:   return IntVal == other.IntVal;
        case ManagedValueType::Float32: return std::fabs(FloatVal - other.FloatVal) <= 1e-5f;
        case ManagedValueType::Boolean: return BoolVal == other.BoolVal;
        case ManagedValueType::Vec3:
            return std::fabs(VecX - other.VecX) <= 1e-4f &&
                std::fabs(VecY - other.VecY) <= 1e-4f &&
                std::fabs(VecZ - other.VecZ) <= 1e-4f;
        case ManagedValueType::String:  return StringVal == other.StringVal;
        case ManagedValueType::UInt64:  return UInt64Val == other.UInt64Val;
        default:                        return false;
        }
    }

    bool ReplicationCodec::WriteManagedValue(BitWriter& writer, const ManagedValue& val)
    {
        if (!val.IsValid() || !writer.WriteU8(static_cast<u8>(val.Type)))
        {
            return false;
        }
        switch (val.Type)
        {
        case ManagedValueType::Int32:   return writer.WriteI32(val.IntVal);
        case ManagedValueType::Float32: return writer.WriteF32(val.FloatVal);
        case ManagedValueType::Boolean: return writer.WriteU8(val.BoolVal ? 1u : 0u);
        case ManagedValueType::Vec3:
            return writer.WriteF32(val.VecX) && writer.WriteF32(val.VecY) && writer.WriteF32(val.VecZ);
        case ManagedValueType::String:
            return writer.WriteString(val.StringVal, ReplicationConstants::kMaxManagedStringBytes);
        case ManagedValueType::UInt64:  return writer.WriteU64(val.UInt64Val);
        default:                        return false;
        }
    }

    bool ReplicationCodec::ReadManagedValue(BitReader& reader, ManagedValue& outVal)
    {
        outVal = ManagedValue{};
        u8 typeRaw = 0;
        if (!reader.ReadU8(typeRaw) ||
            typeRaw < static_cast<u8>(ManagedValueType::Int32) ||
            typeRaw > static_cast<u8>(ManagedValueType::UInt64))
        {
            return false;
        }
        outVal.Type = static_cast<ManagedValueType>(typeRaw);
        switch (outVal.Type)
        {
        case ManagedValueType::Int32:
            if (!reader.ReadI32(outVal.IntVal)) return false;
            break;
        case ManagedValueType::Float32:
            if (!reader.ReadF32(outVal.FloatVal)) return false;
            break;
        case ManagedValueType::Boolean:
        {
            u8 b = 0;
            if (!reader.ReadU8(b) || b > 1) return false;
            outVal.BoolVal = (b != 0);
            break;
        }
        case ManagedValueType::Vec3:
            if (!reader.ReadF32(outVal.VecX) || !reader.ReadF32(outVal.VecY) || !reader.ReadF32(outVal.VecZ)) return false;
            break;
        case ManagedValueType::String:
            if (!reader.ReadString(outVal.StringVal, ReplicationConstants::kMaxManagedStringBytes)) return false;
            break;
        case ManagedValueType::UInt64:
            if (!reader.ReadU64(outVal.UInt64Val)) return false;
            break;
        default:
            return false;
        }
        return outVal.IsValid() && !reader.HasError();
    }

    bool ReplicationCodec::WriteManagedPropertySync(BitWriter& writer, const ManagedPropertySyncPacket& pkt)
    {
        if (pkt.WireVersion != ReplicationConstants::kManagedWireVersion ||
            pkt.WorldId == kInvalidWorldInstanceId ||
            !pkt.TargetNetId.IsValid() ||
            pkt.BehaviourTypeHash == 0 ||
            pkt.Properties.empty() ||
            pkt.Properties.size() > ReplicationConstants::kMaxManagedPropertiesPerSync)
        {
            return false;
        }
        if (!writer.WriteU16(pkt.WireVersion) ||
            !writer.WriteU32(pkt.WorldId) ||
            !writer.WriteU32(pkt.TargetNetId.Index) ||
            !writer.WriteU32(pkt.TargetNetId.Generation) ||
            !writer.WriteU32(pkt.BehaviourTypeHash) ||
            !writer.WriteU16(static_cast<u16>(pkt.Properties.size())))
        {
            return false;
        }
        for (const auto& prop : pkt.Properties)
        {
            if (prop.PropertyId == 0 ||
                !writer.WriteU32(prop.PropertyId) ||
                !WriteManagedValue(writer, prop.Value))
            {
                return false;
            }
        }
        return !writer.HasError();
    }

    bool ReplicationCodec::ReadManagedPropertySync(BitReader& reader, ManagedPropertySyncPacket& outPkt)
    {
        outPkt = ManagedPropertySyncPacket{};
        u16 count = 0;
        if (!reader.ReadU16(outPkt.WireVersion) ||
            outPkt.WireVersion != ReplicationConstants::kManagedWireVersion ||
            !reader.ReadU32(outPkt.WorldId) ||
            outPkt.WorldId == kInvalidWorldInstanceId ||
            !reader.ReadU32(outPkt.TargetNetId.Index) ||
            !reader.ReadU32(outPkt.TargetNetId.Generation) ||
            !outPkt.TargetNetId.IsValid() ||
            !reader.ReadU32(outPkt.BehaviourTypeHash) ||
            outPkt.BehaviourTypeHash == 0 ||
            !reader.ReadU16(count) ||
            count == 0 ||
            count > ReplicationConstants::kMaxManagedPropertiesPerSync)
        {
            return false;
        }
        outPkt.Properties.reserve(count);
        for (u16 i = 0; i < count; ++i)
        {
            ManagedPropertyEntry entry{};
            if (!reader.ReadU32(entry.PropertyId) ||
                entry.PropertyId == 0 ||
                !ReadManagedValue(reader, entry.Value))
            {
                return false;
            }
            outPkt.Properties.push_back(std::move(entry));
        }
        return !reader.HasError() && reader.GetRemainingBits() == 0;
    }

    bool ReplicationCodec::WriteManagedRpc(BitWriter& writer, const ManagedRpcPacket& pkt)
    {
        if (pkt.WireVersion != ReplicationConstants::kManagedWireVersion ||
            pkt.WorldId == kInvalidWorldInstanceId ||
            !pkt.TargetNetId.IsValid() ||
            pkt.BehaviourTypeHash == 0 ||
            pkt.MethodHash == 0 ||
            pkt.Args.size() > ReplicationConstants::kMaxManagedRpcArgs)
        {
            return false;
        }
        const u8 flags = static_cast<u8>((pkt.RequireOwnership ? 1u : 0u) | (pkt.TargetOwnerOnly ? 2u : 0u));
        if (!writer.WriteU16(pkt.WireVersion) ||
            !writer.WriteU32(pkt.WorldId) ||
            !writer.WriteU32(pkt.TargetNetId.Index) ||
            !writer.WriteU32(pkt.TargetNetId.Generation) ||
            !writer.WriteU32(pkt.BehaviourTypeHash) ||
            !writer.WriteU32(pkt.MethodHash) ||
            !writer.WriteU8(flags) ||
            !writer.WriteU8(static_cast<u8>(pkt.Args.size())))
        {
            return false;
        }
        for (const auto& arg : pkt.Args)
        {
            if (!WriteManagedValue(writer, arg))
            {
                return false;
            }
        }
        return !writer.HasError();
    }

    bool ReplicationCodec::ReadManagedRpc(BitReader& reader, ManagedRpcPacket& outPkt)
    {
        outPkt = ManagedRpcPacket{};
        u8 flags = 0;
        u8 argCount = 0;
        if (!reader.ReadU16(outPkt.WireVersion) ||
            outPkt.WireVersion != ReplicationConstants::kManagedWireVersion ||
            !reader.ReadU32(outPkt.WorldId) ||
            outPkt.WorldId == kInvalidWorldInstanceId ||
            !reader.ReadU32(outPkt.TargetNetId.Index) ||
            !reader.ReadU32(outPkt.TargetNetId.Generation) ||
            !outPkt.TargetNetId.IsValid() ||
            !reader.ReadU32(outPkt.BehaviourTypeHash) ||
            outPkt.BehaviourTypeHash == 0 ||
            !reader.ReadU32(outPkt.MethodHash) ||
            outPkt.MethodHash == 0 ||
            !reader.ReadU8(flags) ||
            !reader.ReadU8(argCount) ||
            argCount > ReplicationConstants::kMaxManagedRpcArgs)
        {
            return false;
        }
        outPkt.RequireOwnership = (flags & 1u) != 0u;
        outPkt.TargetOwnerOnly = (flags & 2u) != 0u;
        outPkt.Args.reserve(argCount);
        for (u8 i = 0; i < argCount; ++i)
        {
            ManagedValue val{};
            if (!ReadManagedValue(reader, val))
            {
                return false;
            }
            outPkt.Args.push_back(std::move(val));
        }
        return !reader.HasError() && reader.GetRemainingBits() == 0;
    }
}
