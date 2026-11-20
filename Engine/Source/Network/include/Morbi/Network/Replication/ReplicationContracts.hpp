#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Serialization/BitStream.hpp"

namespace Morbi::Network
{
    namespace ReplicationConstants
    {
        inline constexpr u32 kTransformTypeHash = 0x5452414Eu; // 'TRAN'
        inline constexpr u32 kMovementTypeHash = 0x4D4F5645u;  // 'MOVE'
        inline constexpr u32 kPhysicsTypeHash = 0x50485953u;   // 'PHYS'
        inline constexpr u16 kTransformSchemaVersion = 1;
        inline constexpr u16 kMovementSchemaVersion = 1;
        inline constexpr u16 kPhysicsSchemaVersion = 1;
        inline constexpr u16 kSnapshotWireVersion = 1;

        inline constexpr usize kMaxReplicatedEntities = 16384;
        inline constexpr usize kMaxEntityNameBytes = 64;
        inline constexpr usize kMaxSnapshotEntriesPerPacket = 256;
        inline constexpr usize kMaxKeyframeEntries = 2048;
        inline constexpr usize kDefaultKeyframeChunkPayloadBytes = 1024;
        inline constexpr usize kMaxKeyframeChunkPayloadBytes = 4096;
        inline constexpr usize kMaxKeyframeTotalBytes = 512 * 1024;
        inline constexpr u16 kMaxKeyframeChunks = 512;
        inline constexpr u64 kDefaultKeyframeTransferTimeoutMs = 5000;
        inline constexpr usize kMaxPendingSnapshotsPerPeer = 32;
        inline constexpr f32 kMaxWorldCoordAbs = 100000.0f;
        inline constexpr f32 kMaxScale = 1000.0f;
        inline constexpr f32 kMinScale = 0.001f;
        inline constexpr f32 kMaxSpeedAbs = 500.0f;
        inline constexpr f32 kMaxAngularSpeedAbs = 200.0f;
        inline constexpr u16 kManagedWireVersion = 1;
        inline constexpr usize kMaxManagedPropertiesPerSync = 32;
        inline constexpr usize kMaxManagedRpcArgs = 8;
        inline constexpr usize kMaxManagedStringBytes = 128;
    }

    enum class NetworkAuthority : u8
    {
        None = 0,
        Server = 1,
        ClientPredicted = 2
    };

    enum class ReplicationScope : u8
    {
        AllRelevant = 0,
        OwnerOnly = 1,
        InitialOnly = 2
    };

    struct ReplicationObjectHeader
    {
        NetworkId Id = kInvalidNetworkId;
        u32 TypeHash = 0;
        ConnectionHandle OwnerConnection = kInvalidConnectionHandle;
        NetworkAuthority Authority = NetworkAuthority::Server;
        bool Dormant = false;
    };

    /// Explicit serialization contract for replicated components.
    /// Never exposes raw ECS memory layouts onto the wire.
    class INetworkSerializable
    {
    public:
        virtual ~INetworkSerializable() = default;
        [[nodiscard]] virtual u32 GetTypeHash() const noexcept = 0;
        [[nodiscard]] virtual u16 GetSchemaVersion() const noexcept = 0;
        virtual bool NetSerialize(BitWriter& writer) const = 0;
        virtual bool NetDeserialize(BitReader& reader, u16 schemaVersion) = 0;
    };

    struct ReplicatedComponentDescriptor
    {
        u32 ComponentTypeHash = 0;
        std::string_view DebugName;
        u16 SchemaVersion = 1;
        ReplicationScope Scope = ReplicationScope::AllRelevant;
        f32 BasePriority = 1.0f;
    };

    struct ReplicatedTransformData final : public INetworkSerializable
    {
        f32 PosX = 0.0f;
        f32 PosY = 0.0f;
        f32 PosZ = 0.0f;
        f32 RotX = 0.0f;
        f32 RotY = 0.0f;
        f32 RotZ = 0.0f;
        f32 RotW = 1.0f;
        f32 ScaleX = 1.0f;
        f32 ScaleY = 1.0f;
        f32 ScaleZ = 1.0f;

        [[nodiscard]] u32 GetTypeHash() const noexcept override { return ReplicationConstants::kTransformTypeHash; }
        [[nodiscard]] u16 GetSchemaVersion() const noexcept override { return ReplicationConstants::kTransformSchemaVersion; }
        bool NetSerialize(BitWriter& writer) const override;
        bool NetDeserialize(BitReader& reader, u16 schemaVersion) override;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool ApproximatelyEquals(const ReplicatedTransformData& other, f32 eps = 1e-4f) const noexcept;
    };

    struct ReplicatedMovementData final : public INetworkSerializable
    {
        f32 VelX = 0.0f;
        f32 VelY = 0.0f;
        f32 VelZ = 0.0f;
        f32 YawRadians = 0.0f;
        u32 LastProcessedInputSeq = 0;
        u8 MovementFlags = 0;

        [[nodiscard]] u32 GetTypeHash() const noexcept override { return ReplicationConstants::kMovementTypeHash; }
        [[nodiscard]] u16 GetSchemaVersion() const noexcept override { return ReplicationConstants::kMovementSchemaVersion; }
        bool NetSerialize(BitWriter& writer) const override;
        bool NetDeserialize(BitReader& reader, u16 schemaVersion) override;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool ApproximatelyEquals(const ReplicatedMovementData& other, f32 eps = 1e-4f) const noexcept;
    };

    namespace ReplicatedPhysicsFlags
    {
        inline constexpr u8 kNone = 0;
        inline constexpr u8 kSleeping = 1u << 0;
        inline constexpr u8 kGrounded = 1u << 1;
        inline constexpr u8 kSliding = 1u << 2;
        inline constexpr u8 kRigidBody = 1u << 3;
        inline constexpr u8 kCharacter = 1u << 4;
    }

    struct ReplicatedPhysicsStateData final : public INetworkSerializable
    {
        f32 LinearVelX = 0.0f;
        f32 LinearVelY = 0.0f;
        f32 LinearVelZ = 0.0f;
        f32 AngularVelX = 0.0f;
        f32 AngularVelY = 0.0f;
        f32 AngularVelZ = 0.0f;
        u8 PhysicsFlags = ReplicatedPhysicsFlags::kNone;

        [[nodiscard]] u32 GetTypeHash() const noexcept override { return ReplicationConstants::kPhysicsTypeHash; }
        [[nodiscard]] u16 GetSchemaVersion() const noexcept override { return ReplicationConstants::kPhysicsSchemaVersion; }
        bool NetSerialize(BitWriter& writer) const override;
        bool NetDeserialize(BitReader& reader, u16 schemaVersion) override;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool ApproximatelyEquals(const ReplicatedPhysicsStateData& other, f32 eps = 1e-3f) const noexcept;
    };

    namespace SnapshotComponentFlags
    {
        inline constexpr u8 kNone = 0;
        inline constexpr u8 kTransform = 1u << 0;
        inline constexpr u8 kMovement = 1u << 1;
        inline constexpr u8 kPhysics = 1u << 2;
    }

    enum class ReplicationOpKind : u8
    {
        Spawn = 1,
        Update = 2,
        Despawn = 3
    };

    struct SnapshotEntityEntry
    {
        ReplicationOpKind Op = ReplicationOpKind::Spawn;
        NetworkId NetId = kInvalidNetworkId;
        u32 ArchetypeHash = 0;
        PlayerId OwnerPlayer = kInvalidPlayerId;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        NetworkAuthority Authority = NetworkAuthority::Server;
        std::string EntityName;
        u8 ComponentMask = SnapshotComponentFlags::kNone;
        ReplicatedTransformData Transform{};
        ReplicatedMovementData Movement{};
        ReplicatedPhysicsStateData Physics{};
    };

    struct SnapshotPacket
    {
        u16 WireVersion = ReplicationConstants::kSnapshotWireVersion;
        u32 SnapshotSequence = 0;
        u32 BaselineSequence = 0;
        bool IsKeyframe = false;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        u64 ServerTick = 0;
        std::vector<SnapshotEntityEntry> Entries;
        u32 BaselineEpoch = 1;
    };

    struct KeyframeChunkPacket
    {
        u16 WireVersion = ReplicationConstants::kSnapshotWireVersion;
        u32 TransferId = 0;
        u32 SnapshotSequence = 0;
        u32 BaselineEpoch = 1;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        u64 ServerTick = 0;
        u16 ChunkIndex = 0;
        u16 TotalChunks = 0;
        u32 TotalBytes = 0;
        u32 PayloadDigest32 = 0;
        std::vector<u8> ChunkData;
    };

    struct SnapshotAckPacket
    {
        u32 SnapshotSequence = 0;
        u32 BaselineSequence = 0;
        bool AppliedSuccessfully = true;
        u32 BaselineEpoch = 1;
    };

    enum class ResyncReason : u8
    {
        BaselineMismatch = 1,
        DecodeFailure = 2,
        ExplicitRequest = 3
    };

    struct ResyncRequestPacket
    {
        u32 LastAppliedSequence = 0;
        ResyncReason Reason = ResyncReason::BaselineMismatch;
        u32 BaselineEpoch = 1;
    };

    enum class ManagedValueType : u8
    {
        Int32 = 1,
        Float32 = 2,
        Boolean = 3,
        Vec3 = 4,
        String = 5,
        UInt64 = 6
    };

    struct ManagedValue
    {
        ManagedValueType Type = ManagedValueType::Int32;
        i32 IntVal = 0;
        f32 FloatVal = 0.0f;
        f32 VecX = 0.0f;
        f32 VecY = 0.0f;
        f32 VecZ = 0.0f;
        u64 UInt64Val = 0;
        bool BoolVal = false;
        std::string StringVal;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool Equals(const ManagedValue& other) const noexcept;
    };

    struct ManagedPropertyEntry
    {
        u32 PropertyId = 0;
        ManagedValue Value{};
    };

    struct ManagedPropertySyncPacket
    {
        u16 WireVersion = ReplicationConstants::kManagedWireVersion;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        NetworkId TargetNetId = kInvalidNetworkId;
        u32 BehaviourTypeHash = 0;
        std::vector<ManagedPropertyEntry> Properties;
    };

    struct ManagedRpcPacket
    {
        u16 WireVersion = ReplicationConstants::kManagedWireVersion;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        NetworkId TargetNetId = kInvalidNetworkId;
        u32 BehaviourTypeHash = 0;
        u32 MethodHash = 0;
        bool RequireOwnership = true;
        bool TargetOwnerOnly = false;
        std::vector<ManagedValue> Args;
    };

    class MORBI_NETWORK_API ReplicationCodec final
    {
    public:
        [[nodiscard]] static u32 ComputeDigest32(std::span<const u8> data) noexcept;

        static bool WriteSnapshotPacket(BitWriter& writer, const SnapshotPacket& packet);
        static bool ReadSnapshotPacket(BitReader& reader, SnapshotPacket& outPacket);

        static bool WriteKeyframeChunk(BitWriter& writer, const KeyframeChunkPacket& chunk);
        static bool ReadKeyframeChunk(BitReader& reader, KeyframeChunkPacket& outChunk);

        static bool WriteSnapshotAck(BitWriter& writer, const SnapshotAckPacket& ack);
        static bool ReadSnapshotAck(BitReader& reader, SnapshotAckPacket& outAck);

        static bool WriteResyncRequest(BitWriter& writer, const ResyncRequestPacket& req);
        static bool ReadResyncRequest(BitReader& reader, ResyncRequestPacket& outReq);

        static bool WriteManagedValue(BitWriter& writer, const ManagedValue& val);
        static bool ReadManagedValue(BitReader& reader, ManagedValue& outVal);

        static bool WriteManagedPropertySync(BitWriter& writer, const ManagedPropertySyncPacket& pkt);
        static bool ReadManagedPropertySync(BitReader& reader, ManagedPropertySyncPacket& outPkt);

        static bool WriteManagedRpc(BitWriter& writer, const ManagedRpcPacket& pkt);
        static bool ReadManagedRpc(BitReader& reader, ManagedRpcPacket& outPkt);
    };
}
