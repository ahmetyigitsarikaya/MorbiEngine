#include "PhysicsInternal.hpp"

#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include <algorithm>
#include <cmath>

namespace Morbi::Physics
{
    using namespace Detail;

    namespace
    {
        JPH::Vec3 SafeNormalizedAxis(const Math::Vec3& axis, const Math::Vec3& fallback) noexcept
        {
            const f32 lenSq = Math::LengthSquared(axis);
            if (lenSq <= 1e-8f)
            {
                return ToJph(fallback);
            }
            return ToJph(axis * (1.0f / std::sqrt(lenSq)));
        }

        JPH::Vec3 ComputePerpendicularAxis(JPH::Vec3Arg axis) noexcept
        {
            const JPH::Vec3 ref = (std::abs(axis.GetY()) < 0.9f)
                ? JPH::Vec3::sAxisY()
                : JPH::Vec3::sAxisX();
            return axis.Cross(ref).Normalized();
        }

        JPH::Ref<JPH::TwoBodyConstraint> BuildConstraintForBodies(
            const JointDesc& desc,
            JPH::Body& bodyA,
            JPH::Body& bodyB)
        {
            switch (desc.Type)
            {
            case JointType::Fixed:
            {
                JPH::FixedConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::WorldSpace;
                s.mAutoDetectPoint = desc.AutoDetectAnchors;
                s.mPoint1 = ToJphR(desc.AnchorA);
                s.mPoint2 = ToJphR(desc.AnchorB);
                return s.Create(bodyA, bodyB);
            }
            case JointType::Distance:
            {
                JPH::DistanceConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::WorldSpace;
                s.mPoint1 = desc.AutoDetectAnchors ? bodyA.GetPosition() : ToJphR(desc.AnchorA);
                s.mPoint2 = desc.AutoDetectAnchors ? bodyB.GetPosition() : ToJphR(desc.AnchorB);
                s.mMinDistance = desc.MinDistance;
                s.mMaxDistance = desc.MaxDistance;
                if (desc.SpringFrequencyHz > 0.0f)
                {
                    s.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
                    s.mLimitsSpringSettings.mFrequency = desc.SpringFrequencyHz;
                    s.mLimitsSpringSettings.mDamping = std::max(0.0f, desc.SpringDampingRatio);
                }
                return s.Create(bodyA, bodyB);
            }
            case JointType::Hinge:
            {
                const JPH::Vec3 axis = SafeNormalizedAxis(desc.Axis, Math::Vec3::Up());
                const JPH::Vec3 normal = ComputePerpendicularAxis(axis);
                JPH::HingeConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::WorldSpace;
                s.mPoint1 = ToJphR(desc.AnchorA);
                s.mPoint2 = desc.AutoDetectAnchors ? ToJphR(desc.AnchorA) : ToJphR(desc.AnchorB);
                s.mHingeAxis1 = axis;
                s.mHingeAxis2 = axis;
                s.mNormalAxis1 = normal;
                s.mNormalAxis2 = normal;
                s.mLimitsMin = std::clamp(desc.MinLimit, -JPH::JPH_PI, 0.0f);
                s.mLimitsMax = std::clamp(desc.MaxLimit, 0.0f, JPH::JPH_PI);
                s.mMaxFrictionTorque = std::max(0.0f, desc.MaxFriction);
                if (desc.SpringFrequencyHz > 0.0f)
                {
                    s.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
                    s.mLimitsSpringSettings.mFrequency = desc.SpringFrequencyHz;
                    s.mLimitsSpringSettings.mDamping = std::max(0.0f, desc.SpringDampingRatio);
                }
                return s.Create(bodyA, bodyB);
            }
            case JointType::Slider:
            {
                // Jolt SliderConstraint::GetCurrentPosition() measures (Body2 - Body1) . mSliderAxis1.
                // Negate mSliderAxis so positive limits [MinLimit, MaxLimit] move EntityA (Body1) along +desc.Axis relative to EntityB.
                const JPH::Vec3 axis = -SafeNormalizedAxis(desc.Axis, Math::Vec3::Right());
                const JPH::Vec3 normal = ComputePerpendicularAxis(axis);
                JPH::SliderConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::WorldSpace;
                s.mAutoDetectPoint = desc.AutoDetectAnchors;
                s.mPoint1 = ToJphR(desc.AnchorA);
                s.mPoint2 = ToJphR(desc.AnchorB);
                s.mSliderAxis1 = axis;
                s.mSliderAxis2 = axis;
                s.mNormalAxis1 = normal;
                s.mNormalAxis2 = normal;
                s.mLimitsMin = std::min(desc.MinLimit, 0.0f);
                s.mLimitsMax = std::max(desc.MaxLimit, 0.0f);
                s.mMaxFrictionForce = std::max(0.0f, desc.MaxFriction);
                if (desc.SpringFrequencyHz > 0.0f)
                {
                    s.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
                    s.mLimitsSpringSettings.mFrequency = desc.SpringFrequencyHz;
                    s.mLimitsSpringSettings.mDamping = std::max(0.0f, desc.SpringDampingRatio);
                }
                return s.Create(bodyA, bodyB);
            }
            default:
                return nullptr;
            }
        }

        JPH::Ref<JPH::TwoBodyConstraint> CreateConstraintPair(
            JPH::PhysicsSystem& physicsSystem,
            BodyShadowState& shadowA,
            BodyShadowState* shadowB,
            const JointDesc& desc)
        {
            const JPH::BodyLockInterfaceLocking& lockInterface = physicsSystem.GetBodyLockInterface();
            if (shadowB)
            {
                const JPH::BodyID ids[2] = { shadowA.BodyId, shadowB->BodyId };
                JPH::BodyLockMultiWrite multiLock(lockInterface, ids, 2);
                JPH::Body* bA = multiLock.GetBody(0);
                JPH::Body* bB = multiLock.GetBody(1);
                if (!bA || !bB)
                {
                    return nullptr;
                }
                return BuildConstraintForBodies(desc, *bA, *bB);
            }

            JPH::BodyLockWrite lockA(lockInterface, shadowA.BodyId);
            if (!lockA.Succeeded())
            {
                return nullptr;
            }
            return BuildConstraintForBodies(desc, lockA.GetBody(), JPH::Body::sFixedToWorld);
        }

        void ComputeConstraintForces(
            JointType type,
            const JPH::TwoBodyConstraint* baseConstraint,
            f32 invDt,
            f32& outForce,
            f32& outTorque) noexcept
        {
            outForce = 0.0f;
            outTorque = 0.0f;
            if (!baseConstraint || !baseConstraint->GetEnabled())
            {
                return;
            }

            switch (type)
            {
            case JointType::Fixed:
            {
                const auto* fc = static_cast<const JPH::FixedConstraint*>(baseConstraint);
                outForce = fc->GetTotalLambdaPosition().Length() * invDt;
                outTorque = fc->GetTotalLambdaRotation().Length() * invDt;
                break;
            }
            case JointType::Distance:
            {
                const auto* dc = static_cast<const JPH::DistanceConstraint*>(baseConstraint);
                outForce = std::abs(dc->GetTotalLambdaPosition()) * invDt;
                break;
            }
            case JointType::Hinge:
            {
                const auto* hc = static_cast<const JPH::HingeConstraint*>(baseConstraint);
                outForce = hc->GetTotalLambdaPosition().Length() * invDt;
                const f32 rotLen = hc->GetTotalLambdaRotation().Length();
                const f32 lim = hc->GetTotalLambdaRotationLimits();
                outTorque = std::sqrt(rotLen * rotLen + lim * lim) * invDt;
                break;
            }
            case JointType::Slider:
            {
                const auto* sc = static_cast<const JPH::SliderConstraint*>(baseConstraint);
                const f32 posLen = sc->GetTotalLambdaPosition().Length();
                const f32 lim = sc->GetTotalLambdaPositionLimits();
                outForce = std::sqrt(posLen * posLen + lim * lim) * invDt;
                outTorque = sc->GetTotalLambdaRotation().Length() * invDt;
                break;
            }
            default:
                break;
            }
        }

        JointDesc FromSceneJointEntry(Scene::EntityId owner, const Scene::PhysicsJointEntry& e) noexcept
        {
            JointDesc d{};
            d.Type = static_cast<JointType>(e.Type);
            d.EntityA = owner;
            d.EntityB = e.ConnectedEntity;
            d.AnchorA = e.AnchorA;
            d.AnchorB = e.AnchorB;
            d.Axis = e.Axis;
            d.AutoDetectAnchors = e.AutoDetectAnchors;
            d.MinDistance = e.MinDistance;
            d.MaxDistance = e.MaxDistance;
            d.SpringFrequencyHz = e.SpringFrequencyHz;
            d.SpringDampingRatio = e.SpringDampingRatio;
            d.MinLimit = e.MinLimit;
            d.MaxLimit = e.MaxLimit;
            d.MaxFriction = e.MaxFriction;
            d.BreakForce = e.BreakForce;
            d.BreakTorque = e.BreakTorque;
            d.Enabled = e.Enabled;
            return d;
        }
    }

    void PhysicsWorld::Impl::RemoveJointsForEntityLocked(Scene::EntityId entity)
    {
        if (!entity.IsValid())
        {
            return;
        }
        for (u32 idx = 0u; idx < static_cast<u32>(Joints.size()); ++idx)
        {
            auto& rec = Joints[idx];
            if (!rec.Alive)
            {
                continue;
            }
            if (rec.EntityA == entity || rec.EntityB == entity)
            {
                if (PhysicsSystem && rec.Constraint)
                {
                    PhysicsSystem->RemoveConstraint(rec.Constraint.GetPtr());
                }
                rec.Constraint = nullptr;
                rec.Alive = false;
                rec.SceneOwnerEntity = Scene::EntityId::Invalid();
                rec.SceneJointIndex = 0xFFFFFFFFu;
                ++rec.Generation;
                FreeJointSlots.push_back(idx);
                if (ActiveJointCount > 0u)
                {
                    --ActiveJointCount;
                }
            }
        }
    }

    void PhysicsWorld::Impl::DestroyAllJointsLocked()
    {
        for (auto& rec : Joints)
        {
            if (rec.Alive && PhysicsSystem && rec.Constraint)
            {
                PhysicsSystem->RemoveConstraint(rec.Constraint.GetPtr());
            }
            rec.Constraint = nullptr;
            rec.Alive = false;
            rec.SceneOwnerEntity = Scene::EntityId::Invalid();
            rec.SceneJointIndex = 0xFFFFFFFFu;
        }
        Joints.clear();
        FreeJointSlots.clear();
        ActiveJointCount = 0u;
    }

    JointHandle PhysicsWorld::Impl::CreateJointLocked(
        Scene::Scene& scene,
        const JointDesc& desc,
        Scene::EntityId sceneOwner,
        u32 sceneJointIndex)
    {
        if (!Initialized || !PhysicsSystem || !scene.IsAlive(desc.EntityA))
        {
            return JointHandle::Invalid();
        }

        auto* rbA = scene.RigidBodies().TryGet(desc.EntityA);
        const auto* trA = scene.Transforms().TryGet(desc.EntityA);
        if (!rbA || !trA)
        {
            return JointHandle::Invalid();
        }
        auto* shadowA = FindBodyLocked(desc.EntityA);
        if (!shadowA)
        {
            if (!CreateBodyForEntityLocked(scene, desc.EntityA, *rbA, *trA))
            {
                return JointHandle::Invalid();
            }
            shadowA = FindBodyLocked(desc.EntityA);
        }

        const bool hasEntityB = desc.EntityB.IsValid();
        BodyShadowState* shadowB = nullptr;
        if (hasEntityB)
        {
            if (!scene.IsAlive(desc.EntityB) || desc.EntityA == desc.EntityB)
            {
                return JointHandle::Invalid();
            }
            auto* rbB = scene.RigidBodies().TryGet(desc.EntityB);
            const auto* trB = scene.Transforms().TryGet(desc.EntityB);
            if (!rbB || !trB)
            {
                return JointHandle::Invalid();
            }
            shadowB = FindBodyLocked(desc.EntityB);
            if (!shadowB)
            {
                if (!CreateBodyForEntityLocked(scene, desc.EntityB, *rbB, *trB))
                {
                    return JointHandle::Invalid();
                }
                shadowB = FindBodyLocked(desc.EntityB);
            }
        }

        JPH::Ref<JPH::TwoBodyConstraint> constraint = CreateConstraintPair(*PhysicsSystem, *shadowA, shadowB, desc);
        if (!constraint)
        {
            return JointHandle::Invalid();
        }

        constraint->SetEnabled(desc.Enabled);
        PhysicsSystem->AddConstraint(constraint.GetPtr());

        // Ensure connected bodies wake up so constraint immediately takes effect
        JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();
        if (shadowA) bi.ActivateBody(shadowA->BodyId);
        if (shadowB) bi.ActivateBody(shadowB->BodyId);

        u32 slot = 0u;
        if (!FreeJointSlots.empty())
        {
            slot = FreeJointSlots.back();
            FreeJointSlots.pop_back();
        }
        else
        {
            slot = static_cast<u32>(Joints.size());
            Joints.push_back(JointRecord{});
        }

        auto& rec = Joints[slot];
        rec.Alive = true;
        rec.Type = desc.Type;
        rec.EntityA = desc.EntityA;
        rec.EntityB = desc.EntityB;
        rec.Desc = desc;
        rec.LastAppliedForce = 0.0f;
        rec.LastAppliedTorque = 0.0f;
        rec.SceneOwnerEntity = sceneOwner;
        rec.SceneJointIndex = sceneJointIndex;
        rec.Constraint = constraint;
        ++ActiveJointCount;

        return JointHandle{ slot, rec.Generation };
    }

    bool PhysicsWorld::Impl::UpdateJointLocked(Scene::Scene& scene, JointHandle handle, const JointDesc& desc)
    {
        if (!Initialized || !PhysicsSystem || !handle.IsValid() || handle.Index >= Joints.size())
        {
            return false;
        }
        auto& rec = Joints[handle.Index];
        if (!rec.Alive || rec.Generation != handle.Generation || !rec.Constraint)
        {
            return false;
        }

        const bool topologyChanged =
            rec.Type != desc.Type ||
            rec.EntityA != desc.EntityA ||
            rec.EntityB != desc.EntityB ||
            rec.Desc.AutoDetectAnchors != desc.AutoDetectAnchors ||
            !VecNearEqual(rec.Desc.AnchorA, desc.AnchorA, 1e-4f) ||
            !VecNearEqual(rec.Desc.AnchorB, desc.AnchorB, 1e-4f) ||
            !VecNearEqual(rec.Desc.Axis, desc.Axis, 1e-4f);

        auto* shadowA = FindBodyLocked(desc.EntityA);
        auto* shadowB = desc.EntityB.IsValid() ? FindBodyLocked(desc.EntityB) : nullptr;
        if (!shadowA || !scene.IsAlive(desc.EntityA))
        {
            return false;
        }
        if (desc.EntityB.IsValid() && (!shadowB || !scene.IsAlive(desc.EntityB) || desc.EntityA == desc.EntityB))
        {
            return false;
        }

        if (topologyChanged)
        {
            JPH::Ref<JPH::TwoBodyConstraint> nextConstraint = CreateConstraintPair(*PhysicsSystem, *shadowA, shadowB, desc);
            if (!nextConstraint)
            {
                return false;
            }
            PhysicsSystem->RemoveConstraint(rec.Constraint.GetPtr());
            nextConstraint->SetEnabled(desc.Enabled);
            PhysicsSystem->AddConstraint(nextConstraint.GetPtr());
            rec.Type = desc.Type;
            rec.EntityA = desc.EntityA;
            rec.EntityB = desc.EntityB;
            rec.Constraint = nextConstraint;
        }
        else
        {
            JPH::SpringSettings springSettings{};
            if (desc.SpringFrequencyHz > 0.0f)
            {
                springSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
                springSettings.mFrequency = desc.SpringFrequencyHz;
                springSettings.mDamping = std::max(0.0f, desc.SpringDampingRatio);
            }
            else
            {
                springSettings.mFrequency = 0.0f;
                springSettings.mDamping = 0.0f;
            }

            switch (desc.Type)
            {
            case JointType::Distance:
            {
                auto* dc = static_cast<JPH::DistanceConstraint*>(rec.Constraint.GetPtr());
                const f32 minD = desc.MinDistance;
                const f32 maxD = std::max(desc.MinDistance, desc.MaxDistance);
                if (minD >= 0.0f && maxD >= minD)
                {
                    dc->SetDistance(minD, maxD);
                }
                dc->SetLimitsSpringSettings(springSettings);
                break;
            }
            case JointType::Hinge:
            {
                auto* hc = static_cast<JPH::HingeConstraint*>(rec.Constraint.GetPtr());
                hc->SetLimits(
                    std::clamp(desc.MinLimit, -JPH::JPH_PI, 0.0f),
                    std::clamp(desc.MaxLimit, 0.0f, JPH::JPH_PI));
                hc->SetMaxFrictionTorque(std::max(0.0f, desc.MaxFriction));
                hc->SetLimitsSpringSettings(springSettings);
                break;
            }
            case JointType::Slider:
            {
                auto* sc = static_cast<JPH::SliderConstraint*>(rec.Constraint.GetPtr());
                sc->SetLimits(
                    std::min(desc.MinLimit, 0.0f),
                    std::max(desc.MaxLimit, 0.0f));
                sc->SetMaxFrictionForce(std::max(0.0f, desc.MaxFriction));
                sc->SetLimitsSpringSettings(springSettings);
                break;
            }
            case JointType::Fixed:
            default:
                break;
            }
            rec.Constraint->SetEnabled(desc.Enabled);
        }

        rec.Desc = desc;
        JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();
        bi.ActivateBody(shadowA->BodyId);
        if (shadowB)
        {
            bi.ActivateBody(shadowB->BodyId);
        }
        return true;
    }

    void PhysicsWorld::Impl::EvaluateBreakableJointsLocked(Scene::Scene& scene, f32 fixedDt)
    {
        if (!Initialized || !PhysicsSystem || Joints.empty() || fixedDt <= 1e-6f)
        {
            return;
        }
        const f32 invDt = 1.0f / fixedDt;
        JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();

        for (u32 idx = 0u; idx < static_cast<u32>(Joints.size()); ++idx)
        {
            auto& rec = Joints[idx];
            if (!rec.Alive || !rec.Constraint)
            {
                continue;
            }

            f32 force = 0.0f;
            f32 torque = 0.0f;
            ComputeConstraintForces(rec.Type, rec.Constraint.GetPtr(), invDt, force, torque);
            rec.LastAppliedForce = force;
            rec.LastAppliedTorque = torque;

            const bool breakByForce = (rec.Desc.BreakForce > 0.0f) && (force >= rec.Desc.BreakForce);
            const bool breakByTorque = (rec.Desc.BreakTorque > 0.0f) && (torque >= rec.Desc.BreakTorque);
            if (breakByForce || breakByTorque)
            {
                if (auto* sA = FindBodyLocked(rec.EntityA)) bi.ActivateBody(sA->BodyId);
                if (rec.EntityB.IsValid())
                {
                    if (auto* sB = FindBodyLocked(rec.EntityB)) bi.ActivateBody(sB->BodyId);
                }

                if (rec.SceneOwnerEntity.IsValid() && rec.SceneJointIndex != 0xFFFFFFFFu)
                {
                    if (auto* vec = scene.TryGetMutableEntityJoints(rec.SceneOwnerEntity))
                    {
                        if (rec.SceneJointIndex < static_cast<u32>(vec->size()))
                        {
                            (*vec)[rec.SceneJointIndex].Broken = true;
                            (*vec)[rec.SceneJointIndex].RuntimeJointHandle = 0ull;
                        }
                    }
                }

                PhysicsSystem->RemoveConstraint(rec.Constraint.GetPtr());
                rec.Constraint = nullptr;
                rec.Alive = false;
                rec.SceneOwnerEntity = Scene::EntityId::Invalid();
                rec.SceneJointIndex = 0xFFFFFFFFu;
                ++rec.Generation;
                FreeJointSlots.push_back(idx);
                if (ActiveJointCount > 0u)
                {
                    --ActiveJointCount;
                }
            }
        }
    }

    void PhysicsWorld::Impl::SyncSceneJointsLocked(Scene::Scene& scene)
    {
        if (!Initialized || !PhysicsSystem)
        {
            return;
        }

        // 1. Destroy previously synced scene-owned joints whose entry is gone or disabled/broken
        for (u32 idx = 0u; idx < static_cast<u32>(Joints.size()); ++idx)
        {
            auto& rec = Joints[idx];
            if (!rec.Alive || !rec.SceneOwnerEntity.IsValid())
            {
                continue;
            }
            const auto span = scene.GetEntityJoints(rec.SceneOwnerEntity);
            const bool keep = rec.SceneJointIndex < static_cast<u32>(span.size()) &&
                              !span[rec.SceneJointIndex].Broken &&
                              span[rec.SceneJointIndex].RuntimeJointHandle == JointHandle{ idx, rec.Generation }.Pack();
            if (!keep)
            {
                if (rec.Constraint)
                {
                    PhysicsSystem->RemoveConstraint(rec.Constraint.GetPtr());
                }
                rec.Constraint = nullptr;
                rec.Alive = false;
                rec.SceneOwnerEntity = Scene::EntityId::Invalid();
                rec.SceneJointIndex = 0xFFFFFFFFu;
                ++rec.Generation;
                FreeJointSlots.push_back(idx);
                if (ActiveJointCount > 0u)
                {
                    --ActiveJointCount;
                }
            }
        }

        // 2. Ensure every authored PhysicsJointEntry on Scene has a live or updated runtime constraint
        for (auto& [packedOwner, entries] : scene.PhysicsJointsMap())
        {
            const Scene::EntityId owner = Scene::EntityId::Unpack(packedOwner);
            if (!scene.IsAlive(owner))
            {
                continue;
            }
            for (u32 jIdx = 0u; jIdx < static_cast<u32>(entries.size()); ++jIdx)
            {
                auto& entry = entries[jIdx];
                if (entry.Broken)
                {
                    entry.RuntimeJointHandle = 0ull;
                    continue;
                }
                const JointDesc desired = FromSceneJointEntry(owner, entry);
                const JointHandle existing = JointHandle::Unpack(entry.RuntimeJointHandle);
                const bool existingLive = existing.IsValid() &&
                    existing.Index < Joints.size() &&
                    Joints[existing.Index].Alive &&
                    Joints[existing.Index].Generation == existing.Generation;
                if (existingLive)
                {
                    UpdateJointLocked(scene, existing, desired);
                    Joints[existing.Index].SceneOwnerEntity = owner;
                    Joints[existing.Index].SceneJointIndex = jIdx;
                }
                else
                {
                    const JointHandle created = CreateJointLocked(scene, desired, owner, jIdx);
                    entry.RuntimeJointHandle = created.IsValid() ? created.Pack() : 0ull;
                }
            }
        }
    }

    JointHandle PhysicsWorld::CreateJoint(Scene::Scene& scene, const JointDesc& desc)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        return m_impl->CreateJointLocked(scene, desc);
    }

    bool PhysicsWorld::UpdateJoint(Scene::Scene& scene, JointHandle handle, const JointDesc& desc)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        return m_impl->UpdateJointLocked(scene, handle, desc);
    }

    bool PhysicsWorld::GetJointDesc(JointHandle handle, JointDesc& outDesc) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!handle.IsValid() || handle.Index >= m_impl->Joints.size())
        {
            return false;
        }
        const auto& rec = m_impl->Joints[handle.Index];
        if (!rec.Alive || rec.Generation != handle.Generation)
        {
            return false;
        }
        outDesc = rec.Desc;
        return true;
    }

    bool PhysicsWorld::GetJointState(
        JointHandle handle,
        f32& outCurrentPositionOrAngle,
        f32& outAppliedForce,
        f32& outAppliedTorque) const
    {
        outCurrentPositionOrAngle = 0.0f;
        outAppliedForce = 0.0f;
        outAppliedTorque = 0.0f;

        std::shared_lock lock(m_impl->WorldMutex);
        if (!handle.IsValid() || handle.Index >= m_impl->Joints.size())
        {
            return false;
        }
        const auto& rec = m_impl->Joints[handle.Index];
        if (!rec.Alive || rec.Generation != handle.Generation || !rec.Constraint)
        {
            return false;
        }

        outAppliedForce = rec.LastAppliedForce;
        outAppliedTorque = rec.LastAppliedTorque;

        switch (rec.Type)
        {
        case JointType::Hinge:
            outCurrentPositionOrAngle = static_cast<const JPH::HingeConstraint*>(rec.Constraint.GetPtr())->GetCurrentAngle();
            break;
        case JointType::Slider:
            outCurrentPositionOrAngle = static_cast<const JPH::SliderConstraint*>(rec.Constraint.GetPtr())->GetCurrentPosition();
            break;
        case JointType::Distance:
            outCurrentPositionOrAngle = static_cast<const JPH::DistanceConstraint*>(rec.Constraint.GetPtr())->GetMaxDistance();
            break;
        case JointType::Fixed:
        default:
            outCurrentPositionOrAngle = 0.0f;
            break;
        }
        return true;
    }

    void PhysicsWorld::SyncSceneJoints(Scene::Scene& scene)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        m_impl->SyncSceneJointsLocked(scene);
    }

    bool PhysicsWorld::DestroyJoint(JointHandle handle)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!handle.IsValid() || handle.Index >= m_impl->Joints.size())
        {
            return false;
        }
        auto& rec = m_impl->Joints[handle.Index];
        if (!rec.Alive || rec.Generation != handle.Generation)
        {
            return false;
        }

        if (m_impl->PhysicsSystem && rec.Constraint)
        {
            m_impl->PhysicsSystem->RemoveConstraint(rec.Constraint.GetPtr());
        }
        rec.Constraint = nullptr;
        rec.Alive = false;
        rec.SceneOwnerEntity = Scene::EntityId::Invalid();
        rec.SceneJointIndex = 0xFFFFFFFFu;
        ++rec.Generation;
        m_impl->FreeJointSlots.push_back(handle.Index);
        if (m_impl->ActiveJointCount > 0u)
        {
            --m_impl->ActiveJointCount;
        }
        return true;
    }

    bool PhysicsWorld::IsJointValid(JointHandle handle) const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return handle.IsValid() &&
            handle.Index < m_impl->Joints.size() &&
            m_impl->Joints[handle.Index].Alive &&
            m_impl->Joints[handle.Index].Generation == handle.Generation;
    }
}
