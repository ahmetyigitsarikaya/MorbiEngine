#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

#include "Morbi/Core/Types.hpp"

namespace Morbi::Core
{
    class LinearAllocator
    {
    public:
        explicit LinearAllocator(usize capacityBytes)
            : m_capacity(capacityBytes)
        {
#if defined(_MSC_VER)
            m_buffer = static_cast<std::byte*>(_aligned_malloc(capacityBytes, 64));
#else
            m_buffer = static_cast<std::byte*>(std::aligned_alloc(64, capacityBytes));
#endif
        }

        ~LinearAllocator()
        {
            if (m_buffer != nullptr)
            {
#if defined(_MSC_VER)
                _aligned_free(m_buffer);
#else
                std::free(m_buffer);
#endif
                m_buffer = nullptr;
            }
        }

        LinearAllocator(const LinearAllocator&) = delete;
        LinearAllocator& operator=(const LinearAllocator&) = delete;

        LinearAllocator(LinearAllocator&& other) noexcept
            : m_buffer(other.m_buffer)
            , m_capacity(other.m_capacity)
            , m_offset(other.m_offset)
        {
            other.m_buffer = nullptr;
            other.m_capacity = 0;
            other.m_offset = 0;
        }

        LinearAllocator& operator=(LinearAllocator&& other) noexcept
        {
            if (this != &other)
            {
                if (m_buffer != nullptr)
                {
#if defined(_MSC_VER)
                    _aligned_free(m_buffer);
#else
                    std::free(m_buffer);
#endif
                }
                m_buffer = other.m_buffer;
                m_capacity = other.m_capacity;
                m_offset = other.m_offset;
                other.m_buffer = nullptr;
                other.m_capacity = 0;
                other.m_offset = 0;
            }
            return *this;
        }

        [[nodiscard]] void* Allocate(usize sizeBytes, usize alignment = alignof(std::max_align_t)) noexcept
        {
            if (sizeBytes == 0 || m_buffer == nullptr)
            {
                return nullptr;
            }

            const usize currentAddress = reinterpret_cast<usize>(m_buffer) + m_offset;
            const usize alignedAddress = (currentAddress + (alignment - 1u)) & ~(alignment - 1u);
            const usize padding = alignedAddress - currentAddress;

            if (m_offset + padding + sizeBytes > m_capacity)
            {
                return nullptr;
            }

            m_offset += padding;
            void* result = m_buffer + m_offset;
            m_offset += sizeBytes;
            return result;
        }

        template <typename T, typename... TArgs>
        [[nodiscard]] T* Emplace(TArgs&&... args)
        {
            void* memory = Allocate(sizeof(T), alignof(T));
            if (memory == nullptr)
            {
                return nullptr;
            }
            return std::construct_at(static_cast<T*>(memory), std::forward<TArgs>(args)...);
        }

        void Reset() noexcept
        {
            m_offset = 0;
        }

        [[nodiscard]] usize UsedBytes() const noexcept { return m_offset; }
        [[nodiscard]] usize CapacityBytes() const noexcept { return m_capacity; }
        [[nodiscard]] usize RemainingBytes() const noexcept { return m_capacity - m_offset; }

    private:
        std::byte* m_buffer = nullptr;
        usize m_capacity = 0;
        usize m_offset = 0;
    };
}
