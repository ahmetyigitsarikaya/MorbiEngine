#pragma once

#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Replication/ReplicationContracts.hpp"

namespace Morbi::Network
{
    /// Server-owned allocator for stable generational NetworkIds.
    /// Completely independent of Scene ECS EntityId indices/generations.
    class MORBI_NETWORK_API NetworkIdPool final
    {
    public:
        explicit NetworkIdPool(usize maxEntities = ReplicationConstants::kMaxReplicatedEntities);

        void Clear() noexcept;

        [[nodiscard]] NetworkId Allocate();
        bool Release(NetworkId id) noexcept;
        [[nodiscard]] bool IsAlive(NetworkId id) const noexcept;

        [[nodiscard]] usize GetAliveCount() const noexcept { return m_aliveCount; }
        [[nodiscard]] usize GetCapacity() const noexcept { return m_maxEntities; }

    private:
        struct Slot
        {
            u32 Generation = 1;
            bool Alive = false;
        };

        std::vector<Slot> m_slots;
        std::vector<u32> m_freeIndices;
        usize m_maxEntities = ReplicationConstants::kMaxReplicatedEntities;
        usize m_aliveCount = 0;
    };
}
