#pragma once

#include <functional>
#include <string_view>

#include "Morbi/Core/Types.hpp"

namespace Morbi::Core
{
    class StringId
    {
    public:
        static constexpr u64 kOffsetBasis = 14695981039346656037ULL;
        static constexpr u64 kPrime = 1099511628211ULL;

        constexpr StringId() noexcept = default;

        explicit constexpr StringId(u64 value) noexcept
            : m_value(value)
        {
        }

        [[nodiscard]] static constexpr StringId FromString(std::string_view str) noexcept
        {
            u64 hash = kOffsetBasis;
            for (char c : str)
            {
                hash ^= static_cast<u64>(static_cast<unsigned char>(c));
                hash *= kPrime;
            }
            return StringId(hash);
        }

        [[nodiscard]] constexpr u64 Value() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_value != 0;
        }

        friend constexpr bool operator==(StringId lhs, StringId rhs) noexcept = default;
        friend constexpr auto operator<=>(StringId lhs, StringId rhs) noexcept = default;

    private:
        u64 m_value = 0;
    };

    namespace Literals
    {
        [[nodiscard]] constexpr StringId operator""_sid(const char* str, usize len) noexcept
        {
            return StringId::FromString(std::string_view(str, len));
        }
    }
}

template <>
struct std::hash<Morbi::Core::StringId>
{
    [[nodiscard]] constexpr std::size_t operator()(Morbi::Core::StringId id) const noexcept
    {
        return static_cast<std::size_t>(id.Value());
    }
};
