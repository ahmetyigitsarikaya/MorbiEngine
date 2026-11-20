#pragma once

#include <functional>

#include "Morbi/Core/Types.hpp"

namespace Morbi::Core
{
    template <typename TTag>
    struct GenerationalHandle
    {
        u32 Index = 0;
        u32 Generation = 0;

        [[nodiscard]] static constexpr GenerationalHandle Invalid() noexcept
        {
            return { 0u, 0u };
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Generation != 0u;
        }

        [[nodiscard]] constexpr u64 Pack() const noexcept
        {
            return (static_cast<u64>(Generation) << 32u) | static_cast<u64>(Index);
        }

        [[nodiscard]] static constexpr GenerationalHandle Unpack(u64 packed) noexcept
        {
            return {
                static_cast<u32>(packed & 0xFFFFFFFFull),
                static_cast<u32>((packed >> 32u) & 0xFFFFFFFFull)
            };
        }

        [[nodiscard]] constexpr bool operator==(const GenerationalHandle& other) const noexcept = default;
    };
}

// C++ standard-library specialization for std::hash
namespace std
{
    template <typename TTag>
    struct hash<Morbi::Core::GenerationalHandle<TTag>>
    {
        constexpr size_t operator()(const Morbi::Core::GenerationalHandle<TTag>& handle) const noexcept
        {
            return static_cast<size_t>(handle.Pack());
        }
    };
}
