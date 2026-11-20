#pragma once

#include <deque>
#include <optional>
#include <utility>
#include <vector>

#include "Morbi/Core/Memory/GenerationalHandle.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Core
{
    template <typename TValue, typename TTag = TValue>
    class GenerationalPool
    {
    public:
        using HandleType = GenerationalHandle<TTag>;

        explicit GenerationalPool(usize initialCapacity = 64)
        {
            m_freeIndices.reserve(initialCapacity);
        }

        template <typename... TArgs>
        [[nodiscard]] HandleType Emplace(TArgs&&... args)
        {
            u32 index = 0;
            if (!m_freeIndices.empty())
            {
                index = m_freeIndices.back();
                m_freeIndices.pop_back();
                Slot& slot = m_entries[index];
                slot.Value.emplace(std::forward<TArgs>(args)...);
                ++m_activeCount;
                return HandleType{ index, slot.Generation };
            }

            index = static_cast<u32>(m_entries.size());
            Slot slot{};
            slot.Generation = 1u;
            slot.Value.emplace(std::forward<TArgs>(args)...);
            m_entries.push_back(std::move(slot));
            ++m_activeCount;
            return HandleType{ index, 1u };
        }

        [[nodiscard]] TValue* Get(HandleType handle) noexcept
        {
            if (!IsValid(handle))
            {
                return nullptr;
            }
            return &(*m_entries[handle.Index].Value);
        }

        [[nodiscard]] const TValue* Get(HandleType handle) const noexcept
        {
            if (!IsValid(handle))
            {
                return nullptr;
            }
            return &(*m_entries[handle.Index].Value);
        }

        bool Remove(HandleType handle) noexcept
        {
            if (!IsValid(handle))
            {
                return false;
            }

            Slot& slot = m_entries[handle.Index];
            slot.Value.reset();
            ++slot.Generation;
            if (slot.Generation == 0u)
            {
                slot.Generation = 1u;
            }
            m_freeIndices.push_back(handle.Index);
            --m_activeCount;
            return true;
        }

        [[nodiscard]] bool IsValid(HandleType handle) const noexcept
        {
            return handle.IsValid() &&
                handle.Index < m_entries.size() &&
                m_entries[handle.Index].Generation == handle.Generation &&
                m_entries[handle.Index].Value.has_value();
        }

        void Clear() noexcept
        {
            m_entries.clear();
            m_freeIndices.clear();
            m_activeCount = 0;
        }

        [[nodiscard]] usize ActiveCount() const noexcept
        {
            return m_activeCount;
        }

        template <typename TFunc>
        void ForEach(TFunc&& fn)
        {
            for (u32 i = 0; i < static_cast<u32>(m_entries.size()); ++i)
            {
                if (m_entries[i].Value.has_value())
                {
                    fn(HandleType{ i, m_entries[i].Generation }, *m_entries[i].Value);
                }
            }
        }

    private:
        struct Slot
        {
            u32 Generation = 1u;
            std::optional<TValue> Value;
        };

        std::deque<Slot> m_entries;
        std::vector<u32> m_freeIndices;
        usize m_activeCount = 0;
    };
}
