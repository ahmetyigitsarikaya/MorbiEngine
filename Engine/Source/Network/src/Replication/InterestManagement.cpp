#include "Morbi/Network/Replication/InterestManagement.hpp"

#include <algorithm>

namespace Morbi::Network
{
    namespace
    {
        [[nodiscard]] bool PassesWorldAndScope(
            const InterestViewerContext& viewer,
            const InterestTargetView& target) noexcept
        {
            if (viewer.WorldId == kInvalidWorldInstanceId ||
                target.WorldId == kInvalidWorldInstanceId ||
                viewer.WorldId != target.WorldId)
            {
                return false;
            }
            if (target.Scope == ReplicationScope::OwnerOnly &&
                target.OwnerSession != viewer.ViewerSession)
            {
                return false;
            }
            return true;
        }
    }

    bool AllVisibleInterestPolicy::IsRelevant(
        const InterestViewerContext& viewer,
        const InterestTargetView& target) const noexcept
    {
        return PassesWorldAndScope(viewer, target);
    }

    DistanceInterestPolicy::DistanceInterestPolicy(f32 enterRadius, f32 leaveRadius) noexcept
    {
        SetThresholds(enterRadius, leaveRadius);
    }

    void DistanceInterestPolicy::SetThresholds(f32 enterRadius, f32 leaveRadius) noexcept
    {
        m_enterRadius = (std::max)(enterRadius, 0.1f);
        m_leaveRadius = (std::max)(leaveRadius, m_enterRadius);
    }

    bool DistanceInterestPolicy::IsRelevant(
        const InterestViewerContext& viewer,
        const InterestTargetView& target) const noexcept
    {
        if (!PassesWorldAndScope(viewer, target))
        {
            return false;
        }

        // Owning session always keeps relevancy for its own controlled entities
        if (target.OwnerSession.IsValid() && target.OwnerSession == viewer.ViewerSession)
        {
            return true;
        }

        if (!viewer.HasViewPosition)
        {
            return true;
        }

        const f32 dx = target.PosX - viewer.ViewPosX;
        const f32 dy = target.PosY - viewer.ViewPosY;
        const f32 dz = target.PosZ - viewer.ViewPosZ;
        const f32 distSq = dx * dx + dy * dy + dz * dz;
        const f32 radius = target.PreviouslyVisible ? m_leaveRadius : m_enterRadius;
        return distSq <= (radius * radius);
    }
}
