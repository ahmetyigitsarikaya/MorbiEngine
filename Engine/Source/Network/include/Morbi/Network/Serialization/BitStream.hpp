#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Network/NetworkConfig.hpp"

namespace Morbi::Network
{
    /// Bounded, explicit little-endian bit/byte stream writer.
    /// Enforces maximum message size and integer overflow checks on every write.
    class MORBI_NETWORK_API BitWriter
    {
    public:
        explicit BitWriter(usize maxBytes = ProtocolConstants::kMaxMessageBytes);

        void Reset() noexcept;
        [[nodiscard]] bool HasError() const noexcept { return m_error; }
        [[nodiscard]] usize GetBitCount() const noexcept { return m_bitPos; }
        [[nodiscard]] usize GetByteCount() const noexcept { return (m_bitPos + 7u) / 8u; }
        [[nodiscard]] usize GetMaxBytes() const noexcept { return m_maxBytes; }
        [[nodiscard]] std::span<const u8> GetData() const noexcept;

        bool WriteBits(u64 value, u8 bitCount) noexcept;
        bool WriteBool(bool value) noexcept;
        bool WriteU8(u8 value) noexcept;
        bool WriteU16(u16 value) noexcept;
        bool WriteU32(u32 value) noexcept;
        bool WriteU64(u64 value) noexcept;
        bool WriteI8(i8 value) noexcept;
        bool WriteI16(i16 value) noexcept;
        bool WriteI32(i32 value) noexcept;
        bool WriteI64(i64 value) noexcept;
        bool WriteF32(f32 value) noexcept;
        bool WriteF64(f64 value) noexcept;
        bool WriteQuantizedFloat(f32 value, f32 minVal, f32 maxVal, u8 bitCount) noexcept;
        bool WriteBytes(std::span<const u8> bytes) noexcept;
        bool WriteString(std::string_view text, usize maxLength = ProtocolConstants::kMaxStringBytes) noexcept;
        bool AlignToByte() noexcept;

    private:
        bool EnsureBitCapacity(usize extraBits) noexcept;

        std::vector<u8> m_buffer;
        usize m_bitPos = 0;
        usize m_maxBytes = ProtocolConstants::kMaxMessageBytes;
        bool m_error = false;
    };

    /// Bounded, explicit little-endian bit/byte stream reader.
    /// Rejects truncated streams, integer overflows, and excessive lengths without unbounded allocations.
    class MORBI_NETWORK_API BitReader
    {
    public:
        explicit BitReader(std::span<const u8> data) noexcept;

        [[nodiscard]] bool HasError() const noexcept { return m_error; }
        [[nodiscard]] usize GetBitPosition() const noexcept { return m_bitPos; }
        [[nodiscard]] usize GetTotalBits() const noexcept { return m_totalBits; }
        [[nodiscard]] usize GetRemainingBits() const noexcept
        {
            return (m_bitPos <= m_totalBits) ? (m_totalBits - m_bitPos) : 0u;
        }
        [[nodiscard]] usize GetRemainingBytes() const noexcept { return GetRemainingBits() / 8u; }

        bool ReadBits(u8 bitCount, u64& outValue) noexcept;
        bool ReadBool(bool& outValue) noexcept;
        bool ReadU8(u8& outValue) noexcept;
        bool ReadU16(u16& outValue) noexcept;
        bool ReadU32(u32& outValue) noexcept;
        bool ReadU64(u64& outValue) noexcept;
        bool ReadI8(i8& outValue) noexcept;
        bool ReadI16(i16& outValue) noexcept;
        bool ReadI32(i32& outValue) noexcept;
        bool ReadI64(i64& outValue) noexcept;
        bool ReadF32(f32& outValue) noexcept;
        bool ReadF64(f64& outValue) noexcept;
        bool ReadQuantizedFloat(f32 minVal, f32 maxVal, u8 bitCount, f32& outValue) noexcept;
        bool ReadBytes(std::span<u8> outBuffer) noexcept;
        bool ReadByteVector(std::vector<u8>& outBytes, usize maxAllowedBytes = ProtocolConstants::kMaxMessageBytes) noexcept;
        bool ReadString(std::string& outText, usize maxLength = ProtocolConstants::kMaxStringBytes) noexcept;
        bool AlignToByte() noexcept;

    private:
        bool CanReadBits(usize bitCount) const noexcept;

        std::span<const u8> m_data;
        usize m_totalBits = 0;
        usize m_bitPos = 0;
        bool m_error = false;
    };
}
