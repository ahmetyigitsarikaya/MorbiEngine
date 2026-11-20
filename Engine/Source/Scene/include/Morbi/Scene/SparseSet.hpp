#pragma once

#include <span>
#include <vector>

#include "Morbi/Core/Assert.hpp"
#include "Morbi/Scene/Components.hpp"

namespace Morbi::Scene
{
    // Cache-friendly L1/L2 Sparse-Set with tightly packed contiguous dense arrays (SoA ready)
    template <typename TComponent>
    class SparseSet
    {
    public:
        static constexpr u32 kInvalidDenseIndex = 0xFFFFFFFFu;
        static constexpr u32 kPageSize = 1024u;

        void Reserve(u32 capacity)
        {
            m_denseComponents.reserve(capacity);
            m_denseEntities.reserve(capacity);
        }

        void Clear() noexcept
        {
            m_denseComponents.clear();
            m_denseEntities.clear();
            m_sparsePages.clear();
        }

        [[nodiscard]] bool Contains(EntityId entity) const noexcept
        {
            if (!entity.IsValid())
            {
                return false;
            }
            const u32 pageIdx = entity.Index / kPageSize;
            const u32 offset = entity.Index % kPageSize;
            if (pageIdx >= m_sparsePages.size() || m_sparsePages[pageIdx].empty())
            {
                return false;
            }
            const u32 denseIdx = m_sparsePages[pageIdx][offset];
            return denseIdx < m_denseEntities.size() && m_denseEntities[denseIdx] == entity;
        }

        TComponent& EmplaceOrReplace(EntityId entity, const TComponent& component = TComponent{})
        {
            MORBI_ASSERT(entity.IsValid(), "Cannot emplace component on invalid EntityId!");
            const u32 pageIdx = entity.Index / kPageSize;
            const u32 offset = entity.Index % kPageSize;

            if (pageIdx >= m_sparsePages.size())
            {
                m_sparsePages.resize(pageIdx + 1);
            }
            if (m_sparsePages[pageIdx].empty())
            {
                m_sparsePages[pageIdx].assign(kPageSize, kInvalidDenseIndex);
            }

            const u32 existingDense = m_sparsePages[pageIdx][offset];
            if (existingDense < m_denseEntities.size() && m_denseEntities[existingDense] == entity)
            {
                m_denseComponents[existingDense] = component;
                return m_denseComponents[existingDense];
            }

            const u32 newDenseIdx = static_cast<u32>(m_denseComponents.size());
            m_sparsePages[pageIdx][offset] = newDenseIdx;
            m_denseEntities.push_back(entity);
            m_denseComponents.push_back(component);
            return m_denseComponents.back();
        }

        TComponent& EmplaceOrReplace(EntityId entity, TComponent&& component)
        {
            MORBI_ASSERT(entity.IsValid(), "Cannot emplace component on invalid EntityId!");
            const u32 pageIdx = entity.Index / kPageSize;
            const u32 offset = entity.Index % kPageSize;

            if (pageIdx >= m_sparsePages.size())
            {
                m_sparsePages.resize(pageIdx + 1);
            }
            if (m_sparsePages[pageIdx].empty())
            {
                m_sparsePages[pageIdx].assign(kPageSize, kInvalidDenseIndex);
            }

            const u32 existingDense = m_sparsePages[pageIdx][offset];
            if (existingDense < m_denseEntities.size() && m_denseEntities[existingDense] == entity)
            {
                m_denseComponents[existingDense] = std::move(component);
                return m_denseComponents[existingDense];
            }

            const u32 newDenseIdx = static_cast<u32>(m_denseComponents.size());
            m_sparsePages[pageIdx][offset] = newDenseIdx;
            m_denseEntities.push_back(entity);
            m_denseComponents.push_back(std::move(component));
            return m_denseComponents.back();
        }

        bool Remove(EntityId entity) noexcept
        {
            if (!Contains(entity))
            {
                return false;
            }

            const u32 pageIdx = entity.Index / kPageSize;
            const u32 offset = entity.Index % kPageSize;
            const u32 removedDenseIdx = m_sparsePages[pageIdx][offset];
            const u32 lastDenseIdx = static_cast<u32>(m_denseComponents.size() - 1);

            if (removedDenseIdx != lastDenseIdx)
            {
                const EntityId movedEntity = m_denseEntities[lastDenseIdx];
                m_denseComponents[removedDenseIdx] = std::move(m_denseComponents[lastDenseIdx]);
                m_denseEntities[removedDenseIdx] = movedEntity;
                m_sparsePages[movedEntity.Index / kPageSize][movedEntity.Index % kPageSize] = removedDenseIdx;
            }

            m_sparsePages[pageIdx][offset] = kInvalidDenseIndex;
            m_denseComponents.pop_back();
            m_denseEntities.pop_back();
            return true;
        }

        [[nodiscard]] TComponent* TryGet(EntityId entity) noexcept
        {
            if (!Contains(entity))
            {
                return nullptr;
            }
            return &m_denseComponents[m_sparsePages[entity.Index / kPageSize][entity.Index % kPageSize]];
        }

        [[nodiscard]] const TComponent* TryGet(EntityId entity) const noexcept
        {
            if (!Contains(entity))
            {
                return nullptr;
            }
            return &m_denseComponents[m_sparsePages[entity.Index / kPageSize][entity.Index % kPageSize]];
        }

        [[nodiscard]] u32 Count() const noexcept { return static_cast<u32>(m_denseComponents.size()); }
        [[nodiscard]] std::span<TComponent> RawComponents() noexcept { return m_denseComponents; }
        [[nodiscard]] std::span<const TComponent> RawComponents() const noexcept { return m_denseComponents; }
        [[nodiscard]] std::span<const EntityId> RawEntities() const noexcept { return m_denseEntities; }

        // Zero-Copy Arena View for C# Span<T> Interop
        [[nodiscard]] NativeComponentArenaView GetArenaView() noexcept
        {
            return NativeComponentArenaView{
                .DenseComponentsPtr = m_denseComponents.empty() ? nullptr : m_denseComponents.data(),
                .DenseEntitiesPtr = m_denseEntities.empty() ? nullptr : m_denseEntities.data(),
                .Count = static_cast<u32>(m_denseComponents.size()),
                .ComponentStrideBytes = static_cast<u32>(sizeof(TComponent))
            };
        }

    private:
        std::vector<std::vector<u32>> m_sparsePages;
        std::vector<EntityId> m_denseEntities;
        std::vector<TComponent> m_denseComponents;
    };
}
