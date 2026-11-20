#include "Morbi/Network/Replication/NetworkIdPool.hpp"

#include <algorithm>

namespace Morbi::Network
{
    NetworkIdPool::NetworkIdPool(usize maxEntities)
        : m_maxEntities((std::max)(maxEntities, static_cast<usize>(1)))
    {
    }

    void NetworkIdPool::Clear() noexcept
    {
        m_slots.clear();
        m_freeIndices.clear();
        m_aliveCount = 0;
    }

    NetworkId NetworkIdPool::Allocate()
    {
        u32 idx = 0;
        if (!m_freeIndices.empty())
        {
            idx = m_freeIndices.back();
            m_freeIndices.pop_back();
            m_slots[idx].Alive = true;
            ++m_aliveCount;
            return NetworkId{ idx + 1u, m_slots[idx].Generation };
        }

        if (m_slots.size() >= m_maxEntities)
        {
            return kInvalidNetworkId;
        }

        idx = static_cast<u32>(m_slots.size());
        m_slots.push_back(Slot{ .Generation = 1u, .Alive = true });
        ++m_aliveCount;
        return NetworkId{ idx + 1u, 1u };
    }

    bool NetworkIdPool::Release(NetworkId id) noexcept
    {
        if (!IsAlive(id))
        {
            return false;
        }
        const usize slotIdx = static_cast<usize>(id.Index - 1u);
        Slot& slot = m_slots[slotIdx];
        slot.Alive = false;
        ++slot.Generation;
        if (slot.Generation == 0)
        {
            slot.Generation = 1;
        }
        m_freeIndices.push_back(static_cast<u32>(slotIdx));
        if (m_aliveCount > 0)
        {
            --m_aliveCount;
        }
        return true;
    }

    bool NetworkIdPool::IsAlive(NetworkId id) const noexcept
    {
        if (!id.IsValid() || id.Index == 0)
        {
            return false;
        }
        const usize slotIdx = static_cast<usize>(id.Index - 1u);
        if (slotIdx >= m_slots.size())
        {
            return false;
        }
        const Slot& slot = m_slots[slotIdx];
        return slot.Alive && slot.Generation == id.Generation;
    }
}
