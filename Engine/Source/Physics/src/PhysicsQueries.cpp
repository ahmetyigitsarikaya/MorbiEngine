#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

namespace Morbi::Physics
{
    using namespace Detail;

    namespace
    {
        bool IsFiniteVec3(const Math::Vec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool IsFiniteQuat(const Math::Quat& q) noexcept
        {
            return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
        }

        bool PopulateShapeCastHit(
            const JPH::PhysicsSystem& physicsSystem,
            const Scene::Scene& scene,
            const Math::Vec3& origin,
            const Math::Vec3& dirNorm,
            f32 maxDistance,
            const JPH::ShapeCastResult& hit,
            RaycastHit& outHit)
        {
            JPH::BodyLockRead bodyLock(physicsSystem.GetBodyLockInterface(), hit.mBodyID2);
            if (!bodyLock.Succeeded())
            {
                return false;
            }
            const JPH::Body& body = bodyLock.GetBody();
            const Scene::EntityId hitEntity = Scene::EntityId::Unpack(body.GetUserData());
            if (!scene.IsAlive(hitEntity))
            {
                return false;
            }

            outHit.HitEntity = hitEntity;
            outHit.Distance = hit.mFraction * maxDistance;
            outHit.Point = origin + FromJph(hit.mContactPointOn2);

            const f32 axisLenSq = hit.mPenetrationAxis.LengthSq();
            if (axisLenSq > 1e-8f)
            {
                outHit.Normal = FromJph(-hit.mPenetrationAxis.Normalized());
            }
            else
            {
                outHit.Normal = -dirNorm;
            }

            outHit.JoltBodyId = hit.mBodyID2.GetIndexAndSequenceNumber();
            if (const auto* rb = scene.RigidBodies().TryGet(hitEntity))
            {
                outHit.Shape = rb->Shape;
            }
            return true;
        }

        bool CastShapeCommon(
            const JPH::PhysicsSystem& physicsSystem,
            const Scene::Scene& scene,
            const JPH::Shape* shape,
            const Math::Vec3& origin,
            const Math::Quat& orientation,
            const Math::Vec3& direction,
            f32 maxDistance,
            const QueryFilter& filter,
            RaycastHit& outHit)
        {
            if (!shape || maxDistance <= 0.0f || !std::isfinite(maxDistance) ||
                !IsFiniteVec3(origin) || !IsFiniteVec3(direction) || !IsFiniteQuat(orientation))
            {
                return false;
            }

            const f32 lenSq = Math::LengthSquared(direction);
            if (lenSq <= 1e-8f)
            {
                return false;
            }

            const Math::Vec3 dirNorm = direction * (1.0f / std::sqrt(lenSq));
            const JPH::RMat44 startTransform = JPH::RMat44::sRotationTranslation(ToJph(orientation), ToJphR(origin));
            const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
                shape,
                JPH::Vec3::sReplicate(1.0f),
                startTransform,
                ToJph(dirNorm * maxDistance));

            JPH::ShapeCastSettings settings;
            settings.mUseShrunkenShapeAndConvexRadius = true;
            settings.mReturnDeepestPoint = true;

            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            const QueryBodyFilter bodyFilter(filter);
            physicsSystem.GetNarrowPhaseQuery().CastShape(
                shapeCast,
                settings,
                ToJphR(origin),
                collector,
                JPH::BroadPhaseLayerFilter(),
                JPH::ObjectLayerFilter(),
                bodyFilter);

            if (!collector.HadHit())
            {
                return false;
            }

            return PopulateShapeCastHit(physicsSystem, scene, origin, dirNorm, maxDistance, collector.mHit, outHit);
        }

        u32 OverlapShapeCommon(
            const JPH::PhysicsSystem& physicsSystem,
            const Scene::Scene& scene,
            const JPH::Shape* shape,
            const Math::Vec3& center,
            const Math::Quat& orientation,
            const QueryFilter& filter,
            std::vector<OverlapHit>& outHits)
        {
            outHits.clear();
            if (!shape || !IsFiniteVec3(center) || !IsFiniteQuat(orientation))
            {
                return 0u;
            }

            const JPH::RMat44 worldTransform = JPH::RMat44::sRotationTranslation(ToJph(orientation), ToJphR(center));
            const JPH::RMat44 comTransform = worldTransform.PreTranslated(shape->GetCenterOfMass());

            JPH::CollideShapeSettings settings;
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            const QueryBodyFilter bodyFilter(filter);
            physicsSystem.GetNarrowPhaseQuery().CollideShape(
                shape,
                JPH::Vec3::sReplicate(1.0f),
                comTransform,
                settings,
                ToJphR(center),
                collector,
                JPH::BroadPhaseLayerFilter(),
                JPH::ObjectLayerFilter(),
                bodyFilter);

            std::unordered_map<u64, OverlapHit> deduplicated;
            for (const auto& res : collector.mHits)
            {
                JPH::BodyLockRead bodyLock(physicsSystem.GetBodyLockInterface(), res.mBodyID2);
                if (!bodyLock.Succeeded())
                {
                    continue;
                }
                const JPH::Body& body = bodyLock.GetBody();
                const Scene::EntityId ent = Scene::EntityId::Unpack(body.GetUserData());
                if (!scene.IsAlive(ent))
                {
                    continue;
                }

                const Math::Vec3 axis = (res.mPenetrationAxis.LengthSq() > 1e-8f)
                    ? FromJph(res.mPenetrationAxis.Normalized())
                    : Math::Vec3::Up();

                OverlapHit hit{};
                hit.Entity = ent;
                hit.ContactPoint = center + FromJph(res.mContactPointOn2);
                hit.PenetrationAxis = axis;
                hit.PenetrationDepth = res.mPenetrationDepth;
                hit.JoltBodyId = res.mBodyID2.GetIndexAndSequenceNumber();
                if (const auto* rb = scene.RigidBodies().TryGet(ent))
                {
                    hit.Shape = rb->Shape;
                }

                auto [it, inserted] = deduplicated.emplace(ent.Pack(), hit);
                if (!inserted && hit.PenetrationDepth > it->second.PenetrationDepth)
                {
                    it->second = hit;
                }
            }

            outHits.reserve(deduplicated.size());
            for (auto& [_, h] : deduplicated)
            {
                outHits.push_back(h);
            }
            return static_cast<u32>(outHits.size());
        }
    }

    bool PhysicsWorld::Raycast(
        const Scene::Scene& scene,
        const Math::Vec3& origin,
        const Math::Vec3& direction,
        f32 maxDistance,
        RaycastHit& outHit,
        u32 layerMask,
        bool includeTriggers) const
    {
        QueryFilter filter{};
        filter.LayerMask = layerMask;
        filter.IncludeTriggers = includeTriggers;
        return Raycast(scene, origin, direction, maxDistance, outHit, filter);
    }

    bool PhysicsWorld::Raycast(
        const Scene::Scene& scene,
        const Math::Vec3& origin,
        const Math::Vec3& direction,
        f32 maxDistance,
        RaycastHit& outHit,
        const QueryFilter& filter) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || maxDistance <= 0.0f ||
            !std::isfinite(maxDistance) || !IsFiniteVec3(origin) || !IsFiniteVec3(direction))
        {
            return false;
        }

        const f32 lenSq = Math::LengthSquared(direction);
        if (lenSq <= 1e-8f)
        {
            return false;
        }

        ++m_impl->QueriesThisFrame;
        const Math::Vec3 dirNorm = direction * (1.0f / std::sqrt(lenSq));
        const JPH::RRayCast ray(ToJphR(origin), ToJph(dirNorm * maxDistance));

        JPH::RayCastResult hitResult;
        const QueryBodyFilter bodyFilter(filter);
        const bool hit = m_impl->PhysicsSystem->GetNarrowPhaseQuery().CastRay(
            ray,
            hitResult,
            JPH::BroadPhaseLayerFilter(),
            JPH::ObjectLayerFilter(),
            bodyFilter);

        if (!hit)
        {
            return false;
        }

        const JPH::RVec3 hitPos = ray.GetPointOnRay(hitResult.mFraction);
        JPH::BodyLockRead bodyLock(m_impl->PhysicsSystem->GetBodyLockInterface(), hitResult.mBodyID);
        if (!bodyLock.Succeeded())
        {
            return false;
        }

        const JPH::Body& body = bodyLock.GetBody();
        const Scene::EntityId hitEntity = Scene::EntityId::Unpack(body.GetUserData());
        if (!scene.IsAlive(hitEntity))
        {
            return false;
        }

        outHit.HitEntity = hitEntity;
        outHit.Distance = hitResult.mFraction * maxDistance;
        outHit.Point = {
            static_cast<f32>(hitPos.GetX()),
            static_cast<f32>(hitPos.GetY()),
            static_cast<f32>(hitPos.GetZ())
        };
        outHit.Normal = FromJph(body.GetWorldSpaceSurfaceNormal(hitResult.mSubShapeID2, hitPos));
        outHit.JoltBodyId = hitResult.mBodyID.GetIndexAndSequenceNumber();
        if (const auto* rb = scene.RigidBodies().TryGet(hitEntity))
        {
            outHit.Shape = rb->Shape;
        }
        return true;
    }

    u32 PhysicsWorld::RaycastAll(
        const Scene::Scene& scene,
        const Math::Vec3& origin,
        const Math::Vec3& direction,
        f32 maxDistance,
        std::vector<RaycastHit>& outHits,
        const QueryFilter& filter) const
    {
        outHits.clear();
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || maxDistance <= 0.0f ||
            !std::isfinite(maxDistance) || !IsFiniteVec3(origin) || !IsFiniteVec3(direction))
        {
            return 0u;
        }

        const f32 lenSq = Math::LengthSquared(direction);
        if (lenSq <= 1e-8f)
        {
            return 0u;
        }

        ++m_impl->QueriesThisFrame;
        const Math::Vec3 dirNorm = direction * (1.0f / std::sqrt(lenSq));
        const JPH::RRayCast ray(ToJphR(origin), ToJph(dirNorm * maxDistance));

        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        const QueryBodyFilter bodyFilter(filter);
        m_impl->PhysicsSystem->GetNarrowPhaseQuery().CastRay(
            ray,
            JPH::RayCastSettings(),
            collector,
            JPH::BroadPhaseLayerFilter(),
            JPH::ObjectLayerFilter(),
            bodyFilter);

        collector.Sort();

        outHits.reserve(collector.mHits.size());
        for (const auto& res : collector.mHits)
        {
            JPH::BodyLockRead bodyLock(m_impl->PhysicsSystem->GetBodyLockInterface(), res.mBodyID);
            if (!bodyLock.Succeeded())
            {
                continue;
            }
            const JPH::Body& body = bodyLock.GetBody();
            const Scene::EntityId hitEntity = Scene::EntityId::Unpack(body.GetUserData());
            if (!scene.IsAlive(hitEntity))
            {
                continue;
            }

            const JPH::RVec3 hitPos = ray.GetPointOnRay(res.mFraction);
            RaycastHit hit{};
            hit.HitEntity = hitEntity;
            hit.Distance = res.mFraction * maxDistance;
            hit.Point = {
                static_cast<f32>(hitPos.GetX()),
                static_cast<f32>(hitPos.GetY()),
                static_cast<f32>(hitPos.GetZ())
            };
            hit.Normal = FromJph(body.GetWorldSpaceSurfaceNormal(res.mSubShapeID2, hitPos));
            hit.JoltBodyId = res.mBodyID.GetIndexAndSequenceNumber();
            if (const auto* rb = scene.RigidBodies().TryGet(hitEntity))
            {
                hit.Shape = rb->Shape;
            }
            outHits.push_back(hit);
        }

        return static_cast<u32>(outHits.size());
    }

    bool PhysicsWorld::SphereCast(
        const Scene::Scene& scene,
        const Math::Vec3& origin,
        f32 radius,
        const Math::Vec3& direction,
        f32 maxDistance,
        RaycastHit& outHit,
        const QueryFilter& filter) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || radius <= 0.0f || !std::isfinite(radius))
        {
            return false;
        }
        ++m_impl->QueriesThisFrame;
        const JPH::Ref<JPH::SphereShape> shape = new JPH::SphereShape(std::max(radius, 0.005f));
        return CastShapeCommon(*m_impl->PhysicsSystem, scene, shape.GetPtr(), origin, Math::Quat::Identity(), direction, maxDistance, filter, outHit);
    }

    bool PhysicsWorld::BoxCast(
        const Scene::Scene& scene,
        const Math::Vec3& origin,
        const Math::Vec3& halfExtents,
        const Math::Quat& orientation,
        const Math::Vec3& direction,
        f32 maxDistance,
        RaycastHit& outHit,
        const QueryFilter& filter) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem ||
            halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
        {
            return false;
        }
        ++m_impl->QueriesThisFrame;
        const f32 hx = std::max(halfExtents.x, 0.005f);
        const f32 hy = std::max(halfExtents.y, 0.005f);
        const f32 hz = std::max(halfExtents.z, 0.005f);
        const f32 convexR = std::min(JPH::cDefaultConvexRadius, std::min({ hx, hy, hz }) * 0.25f);
        const JPH::Ref<JPH::BoxShape> shape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz), convexR);
        return CastShapeCommon(*m_impl->PhysicsSystem, scene, shape.GetPtr(), origin, orientation, direction, maxDistance, filter, outHit);
    }

    bool PhysicsWorld::CapsuleCast(
        const Scene::Scene& scene,
        const Math::Vec3& origin,
        f32 radius,
        f32 halfHeight,
        const Math::Quat& orientation,
        const Math::Vec3& direction,
        f32 maxDistance,
        RaycastHit& outHit,
        const QueryFilter& filter) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || radius <= 0.0f || halfHeight < 0.0f)
        {
            return false;
        }
        ++m_impl->QueriesThisFrame;
        const JPH::Ref<JPH::CapsuleShape> shape = new JPH::CapsuleShape(std::max(halfHeight, 0.001f), std::max(radius, 0.005f));
        return CastShapeCommon(*m_impl->PhysicsSystem, scene, shape.GetPtr(), origin, orientation, direction, maxDistance, filter, outHit);
    }

    u32 PhysicsWorld::OverlapSphere(
        const Scene::Scene& scene,
        const Math::Vec3& center,
        f32 radius,
        std::vector<OverlapHit>& outHits,
        const QueryFilter& filter) const
    {
        outHits.clear();
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || radius <= 0.0f || !std::isfinite(radius))
        {
            return 0u;
        }
        ++m_impl->QueriesThisFrame;
        const JPH::Ref<JPH::SphereShape> shape = new JPH::SphereShape(std::max(radius, 0.005f));
        return OverlapShapeCommon(*m_impl->PhysicsSystem, scene, shape.GetPtr(), center, Math::Quat::Identity(), filter, outHits);
    }

    u32 PhysicsWorld::OverlapBox(
        const Scene::Scene& scene,
        const Math::Vec3& center,
        const Math::Vec3& halfExtents,
        const Math::Quat& orientation,
        std::vector<OverlapHit>& outHits,
        const QueryFilter& filter) const
    {
        outHits.clear();
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem ||
            halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
        {
            return 0u;
        }
        ++m_impl->QueriesThisFrame;
        const f32 hx = std::max(halfExtents.x, 0.005f);
        const f32 hy = std::max(halfExtents.y, 0.005f);
        const f32 hz = std::max(halfExtents.z, 0.005f);
        const f32 convexR = std::min(JPH::cDefaultConvexRadius, std::min({ hx, hy, hz }) * 0.25f);
        const JPH::Ref<JPH::BoxShape> shape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz), convexR);
        return OverlapShapeCommon(*m_impl->PhysicsSystem, scene, shape.GetPtr(), center, orientation, filter, outHits);
    }

    u32 PhysicsWorld::OverlapCapsule(
        const Scene::Scene& scene,
        const Math::Vec3& center,
        f32 radius,
        f32 halfHeight,
        const Math::Quat& orientation,
        std::vector<OverlapHit>& outHits,
        const QueryFilter& filter) const
    {
        outHits.clear();
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || radius <= 0.0f || halfHeight < 0.0f)
        {
            return 0u;
        }
        ++m_impl->QueriesThisFrame;
        const JPH::Ref<JPH::CapsuleShape> shape = new JPH::CapsuleShape(std::max(halfHeight, 0.001f), std::max(radius, 0.005f));
        return OverlapShapeCommon(*m_impl->PhysicsSystem, scene, shape.GetPtr(), center, orientation, filter, outHits);
    }
}
