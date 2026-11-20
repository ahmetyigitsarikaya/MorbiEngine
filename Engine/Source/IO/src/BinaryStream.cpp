#include "Morbi/IO/BinaryStream.hpp"

namespace Morbi::IO
{
    std::optional<std::vector<std::byte>> BinaryReader::ReadBytes(usize byteCount)
    {
        if (m_offset + byteCount > m_bytes.size())
        {
            return std::nullopt;
        }

        std::vector<std::byte> result(
            m_bytes.begin() + static_cast<isize>(m_offset),
            m_bytes.begin() + static_cast<isize>(m_offset + byteCount));
        m_offset += byteCount;
        return result;
    }

    std::optional<std::string> BinaryReader::ReadString()
    {
        const auto length = Read<u32>();
        if (!length.has_value())
        {
            return std::nullopt;
        }

        if (m_offset + *length > m_bytes.size())
        {
            return std::nullopt;
        }

        std::string text(reinterpret_cast<const char*>(m_bytes.data() + m_offset), *length);
        m_offset += *length;
        return text;
    }

    void BinaryWriter::WriteBytes(std::span<const std::byte> bytes)
    {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    void BinaryWriter::WriteString(std::string_view value)
    {
        const u32 length = static_cast<u32>(value.size());
        Write(length);
        const auto* begin = reinterpret_cast<const std::byte*>(value.data());
        m_bytes.insert(m_bytes.end(), begin, begin + value.size());
    }
}
