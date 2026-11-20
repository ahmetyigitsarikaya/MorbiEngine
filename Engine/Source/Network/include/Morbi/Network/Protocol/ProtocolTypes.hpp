#pragma once

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkConfig.hpp"
#include "Morbi/Network/Serialization/BitStream.hpp"

namespace Morbi::Network
{
    enum class MessageCategory : u8
    {
        Control = 0,
        Application = 1,
        Replication = 2,
        Rpc = 3
    };

    enum class ControlMessageKind : u8
    {
        ClientHello = 1,
        ServerChallenge = 2,
        ClientAuth = 3,
        ServerAuthAccept = 4,
        VersionReject = 5,
        AuthReject = 6,
        DisconnectNotice = 7
    };

    enum class ReplicationMessageSubType : u8
    {
        ServerSnapshot = 1,
        ClientSnapshotAck = 2,
        ClientRequestResync = 3,
        ClientInputCommand = 4,
        ServerKeyframeChunk = 5,
        ManagedPropertySync = 6
    };

    enum class RpcMessageSubType : u8
    {
        RpcInvoke = 1
    };

    namespace WireEnvelopeFlags
    {
        inline constexpr u8 kNone = 0;
        inline constexpr u8 kEncrypted = 1u << 0;
        inline constexpr u8 kHandshake = 1u << 1;
    }

    struct WireEnvelopeHeader
    {
        u32 Magic = ProtocolConstants::kMagic;
        u8 Flags = WireEnvelopeFlags::kNone;
        u16 ProtocolVersion = ProtocolConstants::kCurrentVersion;
        u64 PacketSequence = 0;
    };

    struct MessageHeader
    {
        u32 Magic = ProtocolConstants::kMagic;
        u16 ProtocolVersion = ProtocolConstants::kCurrentVersion;
        u32 SchemaHash = 1;
        MessageCategory Category = MessageCategory::Application;
        u8 SubType = 0;
        u32 Sequence = 0;
        u32 PayloadBytes = 0;
    };

    class MORBI_NETWORK_API ProtocolCodec
    {
    public:
        static constexpr usize kEncodedWireEnvelopeBytes = 15;
        static constexpr usize kEncodedHeaderBytes = 20;

        static bool WriteWireEnvelope(BitWriter& writer, const WireEnvelopeHeader& envelope);
        static bool ReadWireEnvelope(BitReader& reader, WireEnvelopeHeader& outEnvelope);

        static bool WriteHeader(BitWriter& writer, const MessageHeader& header);
        static bool ReadHeader(BitReader& reader, MessageHeader& outHeader);
        static bool ValidateHeader(const MessageHeader& header, u16 expectedVersion, u32 expectedSchemaHash) noexcept;
    };
}
