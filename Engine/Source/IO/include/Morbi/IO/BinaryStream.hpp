#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Types.hpp"

namespace Morbi::IO
{
    class MORBI_API BinaryReader
    {
    public:
        explicit BinaryReader(std::span<const std::byte> bytes) noexcept
            : m_bytes(bytes)
        {
        }

        template <typename TValue>
        [[nodiscard]] std::optional<TValue> Read() noexcept
        {
            if (m_offset + sizeof(TValue) > m_bytes.size())
            {
                return std::nullopt;
            }

            TValue value{};
            std::memcpy(&value, m_bytes.data() + m_offset, sizeof(TValue));
            m_offset += sizeof(TValue);
            return value;
        }

        [[nodiscard]] std::optional<std::vector<std::byte>> ReadBytes(usize byteCount);
        [[nodiscard]] std::optional<std::string> ReadString();
        [[nodiscard]] bool IsAtEnd() const noexcept { return m_offset >= m_bytes.size(); }
        [[nodiscard]] usize Offset() const noexcept { return m_offset; }

    private:
        std::span<const std::byte> m_bytes;
        usize m_offset = 0;
    };

    class MORBI_API BinaryWriter
    {
    public:
        template <typename TValue>
        void Write(const TValue& value)
        {
            const auto* begin = reinterpret_cast<const std::byte*>(&value);
            m_bytes.insert(m_bytes.end(), begin, begin + sizeof(TValue));
        }

        void WriteBytes(std::span<const std::byte> bytes);
        void WriteString(std::string_view value);

        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return m_bytes; }
        [[nodiscard]] usize Size() const noexcept { return m_bytes.size(); }
        void Clear() noexcept { m_bytes.clear(); }

    private:
        std::vector<std::byte> m_bytes;
    };
}
