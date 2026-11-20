#pragma once

#include <memory>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Replication/ReplicationContracts.hpp"

namespace Morbi::Network
{
    enum class InterestMode : u8
    {
        AllVisible = 0,
        Distance = 1
    };

    struct InterestViewerContext
    {
        SessionId ViewerSession = kInvalidSessionId;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        f32 ViewPosX = 0.0f;
        f32 ViewPosY = 0.0f;
        f32 ViewPosZ = 0.0f;
        bool HasViewPosition = false;
    };

    struct InterestTargetView
    {
        NetworkId NetId = kInvalidNetworkId;
        SessionId OwnerSession = kInvalidSessionId;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        f32 PosX = 0.0f;
        f32 PosY = 0.0f;
        f32 PosZ = 0.0f;
        ReplicationScope Scope = ReplicationScope::AllRelevant;
        bool PreviouslyVisible = false;
    };

    /// Extensible relevancy boundary (allows spatial grid / hierarchical policies in later phases).
    class IInterestPolicy
    {
    public:
        virtual ~IInterestPolicy() = default;
        [[nodiscard]] virtual bool IsRelevant(
            const InterestViewerContext& viewer,
            const InterestTargetView& target) const noexcept = 0;
    };

    class AllVisibleInterestPolicy final : public IInterestPolicy
    {
    public:
        [[nodiscard]] bool IsRelevant(
            const InterestViewerContext& viewer,
            const InterestTargetView& target) const noexcept override;
    };

    class DistanceInterestPolicy final : public IInterestPolicy
    {
    public:
        DistanceInterestPolicy(f32 enterRadius = 50.0f, f32 leaveRadius = 60.0f) noexcept;

        void SetThresholds(f32 enterRadius, f32 leaveRadius) noexcept;
        [[nodiscard]] f32 GetEnterRadius() const noexcept { return m_enterRadius; }
        [[nodiscard]] f32 GetLeaveRadius() const noexcept { return m_leaveRadius; }

        [[nodiscard]] bool IsRelevant(
            const InterestViewerContext& viewer,
            const InterestTargetView& target) const noexcept override;

    private:
        f32 m_enterRadius = 50.0f;
        f32 m_leaveRadius = 60.0f;
    };
}
