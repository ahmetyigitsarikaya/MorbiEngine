#include "Morbi/Network/Serialization/BitStream.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace Morbi::Network
{
    namespace
    {
        [[nodiscard]] bool SafeAdd(usize a, usize b, usize& outSum) noexcept
        {
            if (a > (std::numeric_limits<usize>::max)() - b)
            {
                return false;
            }
            outSum = a + b;
            return true;
        }
    }

    BitWriter::BitWriter(usize maxBytes)
        : m_maxBytes((std::min)(maxBytes, ProtocolConstants::kMaxMessageBytes))
    {
        if (m_maxBytes == 0)
        {
            m_maxBytes = 1;
        }
        m_buffer.reserve((std::min)(m_maxBytes, usize{ 256 }));
    }

    void BitWriter::Reset() noexcept
    {
        m_buffer.clear();
        m_bitPos = 0;
        m_error = false;
    }

    std::span<const u8> BitWriter::GetData() const noexcept
    {
        return std::span<const u8>(m_buffer.data(), GetByteCount());
    }

    bool BitWriter::EnsureBitCapacity(usize extraBits) noexcept
    {
        if (m_error)
        {
            return false;
        }

        usize newBitPos = 0;
        if (!SafeAdd(m_bitPos, extraBits, newBitPos))
        {
            m_error = true;
            return false;
        }

        if (m_maxBytes > ((std::numeric_limits<usize>::max)() / 8u))
        {
            m_error = true;
            return false;
        }
        const usize maxBits = m_maxBytes * 8u;
        if (newBitPos > maxBits)
        {
            m_error = true;
            return false;
        }

        const usize requiredBytes = (newBitPos + 7u) / 8u;
        if (requiredBytes > m_buffer.size())
        {
            m_buffer.resize(requiredBytes, 0u);
        }
        return true;
    }

    bool BitWriter::WriteBits(u64 value, u8 bitCount) noexcept
    {
        if (bitCount == 0 || bitCount > 64)
        {
            m_error = true;
            return false;
        }
        if (!EnsureBitCapacity(bitCount))
        {
            return false;
        }

        for (u8 i = 0; i < bitCount; ++i)
        {
            const usize bitIndex = m_bitPos + i;
            const usize byteIdx = bitIndex >> 3u;
            const u8 bitInByte = static_cast<u8>(bitIndex & 7u);
            if (((value >> i) & 1ull) != 0ull)
            {
                m_buffer[byteIdx] |= static_cast<u8>(1u << bitInByte);
            }
            else
            {
                m_buffer[byteIdx] &= static_cast<u8>(~(1u << bitInByte));
            }
        }
        m_bitPos += bitCount;
        return true;
    }

    bool BitWriter::WriteBool(bool value) noexcept
    {
        return WriteBits(value ? 1ull : 0ull, 1);
    }

    bool BitWriter::WriteU8(u8 value) noexcept
    {
        return WriteBits(value, 8);
    }

    bool BitWriter::WriteU16(u16 value) noexcept
    {
        return WriteBits(value, 16);
    }

    bool BitWriter::WriteU32(u32 value) noexcept
    {
        return WriteBits(value, 32);
    }

    bool BitWriter::WriteU64(u64 value) noexcept
    {
        return WriteBits(value, 64);
    }

    bool BitWriter::WriteI8(i8 value) noexcept
    {
        return WriteU8(static_cast<u8>(value));
    }

    bool BitWriter::WriteI16(i16 value) noexcept
    {
        return WriteU16(static_cast<u16>(value));
    }

    bool BitWriter::WriteI32(i32 value) noexcept
    {
        return WriteU32(static_cast<u32>(value));
    }

    bool BitWriter::WriteI64(i64 value) noexcept
    {
        return WriteU64(static_cast<u64>(value));
    }

    bool BitWriter::WriteF32(f32 value) noexcept
    {
        return WriteU32(std::bit_cast<u32>(value));
    }

    bool BitWriter::WriteF64(f64 value) noexcept
    {
        return WriteU64(std::bit_cast<u64>(value));
    }

    bool BitWriter::WriteQuantizedFloat(f32 value, f32 minVal, f32 maxVal, u8 bitCount) noexcept
    {
        if (bitCount == 0 || bitCount > 32 || !(maxVal > minVal) || !std::isfinite(value))
        {
            m_error = true;
            return false;
        }
        const f64 clamped = std::clamp(static_cast<f64>(value), static_cast<f64>(minVal), static_cast<f64>(maxVal));
        const f64 normalized = (clamped - static_cast<f64>(minVal)) / (static_cast<f64>(maxVal) - static_cast<f64>(minVal));
        const u64 maxQuant = (bitCount == 32) ? 0xFFFFFFFFull : ((1ull << bitCount) - 1ull);
        const u64 quantized = static_cast<u64>(std::llround(normalized * static_cast<f64>(maxQuant)));
        return WriteBits((std::min)(quantized, maxQuant), bitCount);
    }

    bool BitWriter::AlignToByte() noexcept
    {
        if (m_error)
        {
            return false;
        }
        const usize rem = m_bitPos & 7u;
        if (rem == 0)
        {
            return true;
        }
        return EnsureBitCapacity(8u - rem) && ([&]() noexcept {
            m_bitPos += (8u - rem);
            return true;
        })();
    }

    bool BitWriter::WriteBytes(std::span<const u8> bytes) noexcept
    {
        if (!AlignToByte())
        {
            return false;
        }
        if (bytes.size() > (std::numeric_limits<usize>::max)() / 8u)
        {
            m_error = true;
            return false;
        }
        if (!EnsureBitCapacity(bytes.size() * 8u))
        {
            return false;
        }
        if (!bytes.empty())
        {
            const usize byteOffset = m_bitPos >> 3u;
            std::memcpy(m_buffer.data() + byteOffset, bytes.data(), bytes.size());
            m_bitPos += bytes.size() * 8u;
        }
        return true;
    }

    bool BitWriter::WriteString(std::string_view text, usize maxLength) noexcept
    {
        const usize effectiveMax = (std::min)(maxLength, ProtocolConstants::kMaxStringBytes);
        if (text.size() > effectiveMax || text.size() > (std::numeric_limits<u32>::max)())
        {
            m_error = true;
            return false;
        }
        if (!WriteU16(static_cast<u16>(text.size())))
        {
            return false;
        }
        return WriteBytes(std::span<const u8>(
            reinterpret_cast<const u8*>(text.data()),
            text.size()));
    }

    // ------------------------------------------------------------------------
    // BitReader
    // ------------------------------------------------------------------------

    BitReader::BitReader(std::span<const u8> data) noexcept
        : m_data(data)
    {
        if (data.size() > (std::numeric_limits<usize>::max)() / 8u)
        {
            m_totalBits = 0;
            m_error = true;
        }
        else
        {
            m_totalBits = data.size() * 8u;
        }
    }

    bool BitReader::CanReadBits(usize bitCount) const noexcept
    {
        if (m_error)
        {
            return false;
        }
        usize endBit = 0;
        if (!SafeAdd(m_bitPos, bitCount, endBit))
        {
            return false;
        }
        return endBit <= m_totalBits;
    }

    bool BitReader::ReadBits(u8 bitCount, u64& outValue) noexcept
    {
        outValue = 0;
        if (bitCount == 0 || bitCount > 64 || !CanReadBits(bitCount))
        {
            m_error = true;
            return false;
        }

        u64 result = 0;
        for (u8 i = 0; i < bitCount; ++i)
        {
            const usize bitIndex = m_bitPos + i;
            const usize byteIdx = bitIndex >> 3u;
            const u8 bitInByte = static_cast<u8>(bitIndex & 7u);
            if ((m_data[byteIdx] & (1u << bitInByte)) != 0u)
            {
                result |= (1ull << i);
            }
        }
        m_bitPos += bitCount;
        outValue = result;
        return true;
    }

    bool BitReader::ReadBool(bool& outValue) noexcept
    {
        u64 raw = 0;
        if (!ReadBits(1, raw))
        {
            outValue = false;
            return false;
        }
        outValue = (raw != 0ull);
        return true;
    }

    bool BitReader::ReadU8(u8& outValue) noexcept
    {
        u64 raw = 0;
        if (!ReadBits(8, raw))
        {
            outValue = 0;
            return false;
        }
        outValue = static_cast<u8>(raw);
        return true;
    }

    bool BitReader::ReadU16(u16& outValue) noexcept
    {
        u64 raw = 0;
        if (!ReadBits(16, raw))
        {
            outValue = 0;
            return false;
        }
        outValue = static_cast<u16>(raw);
        return true;
    }

    bool BitReader::ReadU32(u32& outValue) noexcept
    {
        u64 raw = 0;
        if (!ReadBits(32, raw))
        {
            outValue = 0;
            return false;
        }
        outValue = static_cast<u32>(raw);
        return true;
    }

    bool BitReader::ReadU64(u64& outValue) noexcept
    {
        return ReadBits(64, outValue);
    }

    bool BitReader::ReadI8(i8& outValue) noexcept
    {
        u8 raw = 0;
        const bool ok = ReadU8(raw);
        outValue = static_cast<i8>(raw);
        return ok;
    }

    bool BitReader::ReadI16(i16& outValue) noexcept
    {
        u16 raw = 0;
        const bool ok = ReadU16(raw);
        outValue = static_cast<i16>(raw);
        return ok;
    }

    bool BitReader::ReadI32(i32& outValue) noexcept
    {
        u32 raw = 0;
        const bool ok = ReadU32(raw);
        outValue = static_cast<i32>(raw);
        return ok;
    }

    bool BitReader::ReadI64(i64& outValue) noexcept
    {
        u64 raw = 0;
        const bool ok = ReadU64(raw);
        outValue = static_cast<i64>(raw);
        return ok;
    }

    bool BitReader::ReadF32(f32& outValue) noexcept
    {
        u32 raw = 0;
        if (!ReadU32(raw))
        {
            outValue = 0.0f;
            return false;
        }
        outValue = std::bit_cast<f32>(raw);
        return true;
    }

    bool BitReader::ReadF64(f64& outValue) noexcept
    {
        u64 raw = 0;
        if (!ReadU64(raw))
        {
            outValue = 0.0;
            return false;
        }
        outValue = std::bit_cast<f64>(raw);
        return true;
    }

    bool BitReader::ReadQuantizedFloat(f32 minVal, f32 maxVal, u8 bitCount, f32& outValue) noexcept
    {
        outValue = minVal;
        if (bitCount == 0 || bitCount > 32 || !(maxVal > minVal))
        {
            m_error = true;
            return false;
        }
        u64 raw = 0;
        if (!ReadBits(bitCount, raw))
        {
            return false;
        }
        const u64 maxQuant = (bitCount == 32) ? 0xFFFFFFFFull : ((1ull << bitCount) - 1ull);
        const f64 ratio = static_cast<f64>(raw) / static_cast<f64>(maxQuant);
        outValue = static_cast<f32>(static_cast<f64>(minVal) + ratio * (static_cast<f64>(maxVal) - static_cast<f64>(minVal)));
        return true;
    }

    bool BitReader::AlignToByte() noexcept
    {
        if (m_error)
        {
            return false;
        }
        const usize rem = m_bitPos & 7u;
        if (rem == 0)
        {
            return true;
        }
        const usize pad = 8u - rem;
        if (!CanReadBits(pad))
        {
            m_error = true;
            return false;
        }
        m_bitPos += pad;
        return true;
    }

    bool BitReader::ReadBytes(std::span<u8> outBuffer) noexcept
    {
        if (!AlignToByte())
        {
            return false;
        }
        if (outBuffer.size() > (std::numeric_limits<usize>::max)() / 8u)
        {
            m_error = true;
            return false;
        }
        const usize neededBits = outBuffer.size() * 8u;
        if (!CanReadBits(neededBits))
        {
            m_error = true;
            return false;
        }
        if (!outBuffer.empty())
        {
            const usize byteOffset = m_bitPos >> 3u;
            std::memcpy(outBuffer.data(), m_data.data() + byteOffset, outBuffer.size());
            m_bitPos += neededBits;
        }
        return true;
    }

    bool BitReader::ReadByteVector(std::vector<u8>& outBytes, usize maxAllowedBytes) noexcept
    {
        outBytes.clear();
        u32 length = 0;
        if (!ReadU32(length))
        {
            return false;
        }
        const usize effectiveMax = (std::min)(maxAllowedBytes, ProtocolConstants::kMaxMessageBytes);
        if (length > effectiveMax || !AlignToByte() || !CanReadBits(static_cast<usize>(length) * 8u))
        {
            m_error = true;
            return false;
        }
        outBytes.resize(length);
        return ReadBytes(std::span<u8>(outBytes.data(), outBytes.size()));
    }

    bool BitReader::ReadString(std::string& outText, usize maxLength) noexcept
    {
        outText.clear();
        u16 length = 0;
        if (!ReadU16(length))
        {
            return false;
        }
        const usize effectiveMax = (std::min)(maxLength, ProtocolConstants::kMaxStringBytes);
        if (static_cast<usize>(length) > effectiveMax || !AlignToByte() || !CanReadBits(static_cast<usize>(length) * 8u))
        {
            m_error = true;
            return false;
        }
        outText.resize(length);
        if (length > 0)
        {
            return ReadBytes(std::span<u8>(reinterpret_cast<u8*>(outText.data()), length));
        }
        return true;
    }
}
