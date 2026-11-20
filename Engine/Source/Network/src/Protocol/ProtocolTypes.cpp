#include "Morbi/Network/Protocol/ProtocolTypes.hpp"

#include <cstdio>

namespace Morbi::Network
{
    std::string SessionId::ToHexString() const
    {
        char buf[33]{};
        std::snprintf(buf, sizeof(buf), "%016llx%016llx",
            static_cast<unsigned long long>(High),
            static_cast<unsigned long long>(Low));
        return std::string(buf);
    }

    bool ProtocolCodec::WriteWireEnvelope(BitWriter& writer, const WireEnvelopeHeader& envelope)
    {
        return writer.WriteU32(envelope.Magic) &&
            writer.WriteU8(envelope.Flags) &&
            writer.WriteU16(envelope.ProtocolVersion) &&
            writer.WriteU64(envelope.PacketSequence);
    }

    bool ProtocolCodec::ReadWireEnvelope(BitReader& reader, WireEnvelopeHeader& outEnvelope)
    {
        const bool ok =
            reader.ReadU32(outEnvelope.Magic) &&
            reader.ReadU8(outEnvelope.Flags) &&
            reader.ReadU16(outEnvelope.ProtocolVersion) &&
            reader.ReadU64(outEnvelope.PacketSequence);
        if (!ok)
        {
            return false;
        }
        return outEnvelope.Magic == ProtocolConstants::kMagic;
    }

    bool ProtocolCodec::WriteHeader(BitWriter& writer, const MessageHeader& header)
    {
        if (header.PayloadBytes > ProtocolConstants::kMaxMessageBytes)
        {
            return false;
        }
        return writer.WriteU32(header.Magic) &&
            writer.WriteU16(header.ProtocolVersion) &&
            writer.WriteU32(header.SchemaHash) &&
            writer.WriteU8(static_cast<u8>(header.Category)) &&
            writer.WriteU8(header.SubType) &&
            writer.WriteU32(header.Sequence) &&
            writer.WriteU32(header.PayloadBytes);
    }

    bool ProtocolCodec::ReadHeader(BitReader& reader, MessageHeader& outHeader)
    {
        u8 categoryRaw = 0;
        const bool ok =
            reader.ReadU32(outHeader.Magic) &&
            reader.ReadU16(outHeader.ProtocolVersion) &&
            reader.ReadU32(outHeader.SchemaHash) &&
            reader.ReadU8(categoryRaw) &&
            reader.ReadU8(outHeader.SubType) &&
            reader.ReadU32(outHeader.Sequence) &&
            reader.ReadU32(outHeader.PayloadBytes);
        if (!ok)
        {
            return false;
        }
        if (categoryRaw > static_cast<u8>(MessageCategory::Rpc))
        {
            return false;
        }
        outHeader.Category = static_cast<MessageCategory>(categoryRaw);
        return outHeader.Magic == ProtocolConstants::kMagic &&
            outHeader.PayloadBytes <= ProtocolConstants::kMaxMessageBytes &&
            static_cast<usize>(outHeader.PayloadBytes) == reader.GetRemainingBytes();
    }

    bool ProtocolCodec::ValidateHeader(const MessageHeader& header, u16 expectedVersion, u32 expectedSchemaHash) noexcept
    {
        return header.Magic == ProtocolConstants::kMagic &&
            header.ProtocolVersion == expectedVersion &&
            header.SchemaHash == expectedSchemaHash &&
            header.PayloadBytes <= ProtocolConstants::kMaxMessageBytes;
    }
}
