#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Morbi/Physics/PhysicsWorld.hpp"

namespace Morbi::Physics::Detail
{
    [[nodiscard]] inline JPH::Vec3 ToJph(const Math::Vec3& v) noexcept
    {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    [[nodiscard]] inline JPH::RVec3 ToJphR(const Math::Vec3& v) noexcept
    {
        return JPH::RVec3(v.x, v.y, v.z);
    }

    [[nodiscard]] inline Math::Vec3 FromJph(JPH::Vec3Arg v) noexcept
    {
        return { v.GetX(), v.GetY(), v.GetZ() };
    }

    [[nodiscard]] inline JPH::Quat ToJph(const Math::Quat& q) noexcept
    {
        const f32 lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (lenSq <= 1e-8f)
        {
            return JPH::Quat::sIdentity();
        }
        return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
    }

    [[nodiscard]] inline Math::Quat FromJph(JPH::QuatArg q) noexcept
    {
        return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
    }

    [[nodiscard]] inline bool VecNearEqual(const Math::Vec3& a, const Math::Vec3& b, f32 eps = 1e-5f) noexcept
    {
        return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps;
    }

    [[nodiscard]] inline bool QuatNearEqual(const Math::Quat& a, const Math::Quat& b, f32 eps = 1e-5f) noexcept
    {
        const f32 dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return dot >= (1.0f - eps);
    }

    class MorbiBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
    {
    public:
        MorbiBroadPhaseLayerInterface()
        {
            m_objectToBroadPhase[ObjectLayers::kNonMoving] = JPH::BroadPhaseLayer(BroadPhaseLayers::kNonMoving);
            m_objectToBroadPhase[ObjectLayers::kMoving] = JPH::BroadPhaseLayer(BroadPhaseLayers::kMoving);
        }

        JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kNumLayers; }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            return (inLayer < ObjectLayers::kNumLayers)
                ? m_objectToBroadPhase[inLayer]
                : JPH::BroadPhaseLayer(BroadPhaseLayers::kMoving);
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
            {
            case BroadPhaseLayers::kNonMoving: return "NonMoving";
            case BroadPhaseLayers::kMoving:    return "Moving";
            default:                           return "Unknown";
            }
        }
#endif

    private:
        JPH::BroadPhaseLayer m_objectToBroadPhase[ObjectLayers::kNumLayers]{};
    };

    class MorbiObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            if (inLayer1 == ObjectLayers::kNonMoving)
            {
                return inLayer2 == JPH::BroadPhaseLayer(BroadPhaseLayers::kMoving);
            }
            return true;
        }
    };

    class MorbiObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            if (inObject1 == ObjectLayers::kNonMoving)
            {
                return inObject2 == ObjectLayers::kMoving;
            }
            return true;
        }
    };

    class BitmaskGroupFilter final : public JPH::GroupFilter
    {
    public:
        BitmaskGroupFilter()
        {
            for (auto& mask : m_layerMatrix)
            {
                mask.store(0xFFFFFFFFu, std::memory_order_relaxed);
            }
        }

        void SetLayerCollision(u32 layerIndexA, u32 layerIndexB, bool shouldCollide) noexcept
        {
            if (layerIndexA >= 32u || layerIndexB >= 32u)
            {
                return;
            }
            const u32 bitB = (1u << layerIndexB);
            const u32 bitA = (1u << layerIndexA);
            auto update = [](std::atomic<u32>& target, u32 bit, bool enable) {
                u32 cur = target.load(std::memory_order_relaxed);
                u32 next = 0u;
                do
                {
                    next = enable ? (cur | bit) : (cur & ~bit);
                } while (!target.compare_exchange_weak(cur, next, std::memory_order_relaxed));
            };
            update(m_layerMatrix[layerIndexA], bitB, shouldCollide);
            update(m_layerMatrix[layerIndexB], bitA, shouldCollide);
        }

        [[nodiscard]] bool GetLayerCollision(u32 layerIndexA, u32 layerIndexB) const noexcept
        {
            if (layerIndexA >= 32u || layerIndexB >= 32u)
            {
                return false;
            }
            return (m_layerMatrix[layerIndexA].load(std::memory_order_relaxed) & (1u << layerIndexB)) != 0u;
        }

        bool CanCollide(const JPH::CollisionGroup& inGroup1, const JPH::CollisionGroup& inGroup2) const override
        {
            const u32 layer1 = inGroup1.GetGroupID();
            const u32 mask1 = inGroup1.GetSubGroupID();
            const u32 layer2 = inGroup2.GetGroupID();
            const u32 mask2 = inGroup2.GetSubGroupID();
            if (((layer1 & mask2) == 0u) || ((layer2 & mask1) == 0u))
            {
                return false;
            }

            // Global layer matrix check for set bits in layer1 vs layer2
            u32 bits = layer1;
            while (bits != 0u)
            {
                unsigned long idx = 0;
                // Portable bit scan
                u32 tz = 0u;
                while (((bits >> tz) & 1u) == 0u) { ++tz; }
                idx = tz;
                if ((m_layerMatrix[idx].load(std::memory_order_relaxed) & layer2) != 0u)
                {
                    return true;
                }
                bits &= ~(1u << idx);
            }
            return false;
        }

    private:
        std::array<std::atomic<u32>, 32> m_layerMatrix{};
    };

    class QueryBodyFilter final : public JPH::BodyFilter
    {
    public:
        explicit QueryBodyFilter(const QueryFilter& filter) noexcept
            : m_filter(filter)
            , m_ignorePacked(filter.IgnoreEntity.Pack())
        {
        }

        bool ShouldCollideLocked(const JPH::Body& inBody) const override
        {
            if (!m_filter.IncludeTriggers && inBody.IsSensor())
            {
                return false;
            }
            if (m_filter.IgnoreEntity.IsValid() && inBody.GetUserData() == m_ignorePacked)
            {
                return false;
            }
            const JPH::EMotionType mt = inBody.GetMotionType();
            if (m_filter.IgnoreStatic && mt == JPH::EMotionType::Static) return false;
            if (m_filter.IgnoreKinematic && mt == JPH::EMotionType::Kinematic) return false;
            if (m_filter.IgnoreDynamic && mt == JPH::EMotionType::Dynamic) return false;

            const u32 bodyLayer = inBody.GetCollisionGroup().GetGroupID();
            return (bodyLayer & m_filter.LayerMask) != 0u;
        }

    private:
        QueryFilter m_filter;
        u64 m_ignorePacked = 0;
    };

    struct PairKey
    {
        u64 Low = 0;
        u64 High = 0;

        bool operator==(const PairKey& other) const noexcept = default;
    };

    struct PairKeyHash
    {
        usize operator()(const PairKey& k) const noexcept
        {
            const usize h1 = std::hash<u64>{}(k.Low);
            const usize h2 = std::hash<u64>{}(k.High);
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        }
    };

    struct ActivePairState
    {
        Scene::EntityId EntityA = Scene::EntityId::Invalid();
        Scene::EntityId EntityB = Scene::EntityId::Invalid();
        JPH::BodyID BodyA{};
        JPH::BodyID BodyB{};
        Math::Vec3 LastContactPoint = Math::Vec3::Zero();
        Math::Vec3 LastContactNormal = Math::Vec3::Up();
        Math::Vec3 LastRelativeVelocity = Math::Vec3::Zero();
        f32 LastPenetrationDepth = 0.0f;
        Scene::ColliderShapeType ShapeA = Scene::ColliderShapeType::Box;
        Scene::ColliderShapeType ShapeB = Scene::ColliderShapeType::Box;
        bool IsTrigger = false;
        bool SleepingPaused = false;
        bool EmittedStayThisFrame = false;
    };

    struct SubstepContactRecord
    {
        Scene::EntityId EntityA = Scene::EntityId::Invalid();
        Scene::EntityId EntityB = Scene::EntityId::Invalid();
        JPH::BodyID BodyA{};
        JPH::BodyID BodyB{};
        Math::Vec3 ContactPoint = Math::Vec3::Zero();
        Math::Vec3 ContactNormal = Math::Vec3::Up();
        Math::Vec3 RelativeVelocity = Math::Vec3::Zero();
        f32 PenetrationDepth = 0.0f;
        bool IsTrigger = false;
    };

    struct RemovedSubstepPair
    {
        JPH::BodyID Body1{};
        JPH::BodyID Body2{};
    };

    struct SubshapeFilterInfo
    {
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
        bool IsTrigger = false;
    };

    struct EntitySubshapeFilters
    {
        SubshapeFilterInfo Primary{};
        std::vector<SubshapeFilterInfo> Extra;
    };

    class MorbiContactListener final : public JPH::ContactListener
    {
    public:
        void SetGroupFilter(const BitmaskGroupFilter* filter) noexcept
        {
            m_groupFilter = filter;
        }

        void SetEntitySubshapeFilters(
            u64 packedEntity,
            SubshapeFilterInfo primaryFilter,
            std::vector<SubshapeFilterInfo> extraFilters)
        {
            std::unique_lock lock(m_filterMutex);
            if (extraFilters.empty())
            {
                m_subshapeFilters.erase(packedEntity);
            }
            else
            {
                m_subshapeFilters[packedEntity] = EntitySubshapeFilters{
                    .Primary = primaryFilter,
                    .Extra = std::move(extraFilters)
                };
            }
        }

        void ClearEntitySubshapeFilters(u64 packedEntity)
        {
            std::unique_lock lock(m_filterMutex);
            m_subshapeFilters.erase(packedEntity);
        }

        void ClearAllSubshapeFilters()
        {
            std::unique_lock lock(m_filterMutex);
            m_subshapeFilters.clear();
        }

        void Clear()
        {
            std::scoped_lock lock(m_mutex);
            m_substepContacts.clear();
            m_substepRemoved.clear();
        }

        void DrainSubstep(
            std::vector<SubstepContactRecord>& outContacts,
            std::vector<RemovedSubstepPair>& outRemoved)
        {
            std::scoped_lock lock(m_mutex);
            outContacts.swap(m_substepContacts);
            outRemoved.swap(m_substepRemoved);
        }

        JPH::ValidateResult OnContactValidate(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            JPH::RVec3Arg /*inBaseOffset*/,
            const JPH::CollideShapeResult& inCollisionResult) override
        {
            std::shared_lock lock(m_filterMutex);
            if (m_subshapeFilters.empty())
            {
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            const u64 p1 = inBody1.GetUserData();
            const u64 p2 = inBody2.GetUserData();
            const auto it1 = m_subshapeFilters.find(p1);
            const auto it2 = m_subshapeFilters.find(p2);
            if (it1 == m_subshapeFilters.end() && it2 == m_subshapeFilters.end())
            {
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            const SubshapeFilterInfo f1 = ResolveSubshapeFilterLocked(inBody1, inCollisionResult.mSubShapeID1, it1 != m_subshapeFilters.end() ? &it1->second : nullptr);
            const SubshapeFilterInfo f2 = ResolveSubshapeFilterLocked(inBody2, inCollisionResult.mSubShapeID2, it2 != m_subshapeFilters.end() ? &it2->second : nullptr);

            const bool matchMask = ((f1.CollisionLayer & f2.CollisionMask) != 0u) && ((f2.CollisionLayer & f1.CollisionMask) != 0u);
            if (!matchMask)
            {
                return JPH::ValidateResult::RejectContact;
            }
            if (m_groupFilter)
            {
                const JPH::CollisionGroup g1(m_groupFilter, f1.CollisionLayer, f1.CollisionMask);
                const JPH::CollisionGroup g2(m_groupFilter, f2.CollisionLayer, f2.CollisionMask);
                if (!m_groupFilter->CanCollide(g1, g2))
                {
                    return JPH::ValidateResult::RejectContact;
                }
            }
            return JPH::ValidateResult::AcceptContact;
        }

        void OnContactAdded(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override
        {
            RecordContact(inBody1, inBody2, inManifold, ioSettings);
        }

        void OnContactPersisted(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override
        {
            RecordContact(inBody1, inBody2, inManifold, ioSettings);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
        {
            std::scoped_lock lock(m_mutex);
            m_substepRemoved.push_back(RemovedSubstepPair{
                .Body1 = inSubShapePair.GetBody1ID(),
                .Body2 = inSubShapePair.GetBody2ID()
            });
        }

    private:
        static SubshapeFilterInfo ResolveSubshapeFilterLocked(
            const JPH::Body& body,
            JPH::SubShapeID subShapeId,
            const EntitySubshapeFilters* entry) noexcept
        {
            const JPH::CollisionGroup& cg = body.GetCollisionGroup();
            SubshapeFilterInfo base{
                .CollisionLayer = cg.GetGroupID(),
                .CollisionMask = cg.GetSubGroupID(),
                .IsTrigger = body.IsSensor()
            };
            if (!entry || entry->Extra.empty())
            {
                return base;
            }
            const JPH::Shape* rootShape = body.GetShape();
            if (!rootShape || rootShape->GetType() != JPH::EShapeType::Compound)
            {
                return entry->Primary;
            }
            const auto* compound = static_cast<const JPH::CompoundShape*>(rootShape);
            JPH::SubShapeID remainder;
            const JPH::uint idx = compound->GetSubShapeIndexFromID(subShapeId, remainder);
            // Index 0 = primary RigidBody collider; indices 1..N = entry->Extra[idx - 1]
            if (idx == 0u)
            {
                return entry->Primary;
            }
            if ((idx - 1u) < static_cast<JPH::uint>(entry->Extra.size()))
            {
                const SubshapeFilterInfo& sub = entry->Extra[idx - 1u];
                return SubshapeFilterInfo{
                    .CollisionLayer = sub.CollisionLayer != 0u ? sub.CollisionLayer : entry->Primary.CollisionLayer,
                    .CollisionMask = sub.CollisionMask != 0u ? sub.CollisionMask : entry->Primary.CollisionMask,
                    .IsTrigger = sub.IsTrigger || entry->Primary.IsTrigger
                };
            }
            return entry->Primary;
        }

        void RecordContact(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings)
        {
            const Scene::EntityId ent1 = Scene::EntityId::Unpack(inBody1.GetUserData());
            const Scene::EntityId ent2 = Scene::EntityId::Unpack(inBody2.GetUserData());
            if (!ent1.IsValid() || !ent2.IsValid())
            {
                return;
            }

            bool subTrigger = false;
            {
                std::shared_lock lock(m_filterMutex);
                if (!m_subshapeFilters.empty())
                {
                    const auto it1 = m_subshapeFilters.find(ent1.Pack());
                    const auto it2 = m_subshapeFilters.find(ent2.Pack());
                    const SubshapeFilterInfo f1 = ResolveSubshapeFilterLocked(inBody1, inManifold.mSubShapeID1, it1 != m_subshapeFilters.end() ? &it1->second : nullptr);
                    const SubshapeFilterInfo f2 = ResolveSubshapeFilterLocked(inBody2, inManifold.mSubShapeID2, it2 != m_subshapeFilters.end() ? &it2->second : nullptr);
                    subTrigger = f1.IsTrigger || f2.IsTrigger;
                }
            }
            if (subTrigger)
            {
                ioSettings.mIsSensor = true;
            }

            Math::Vec3 contactPoint = FromJph(inBody1.GetPosition());
            if (!inManifold.mRelativeContactPointsOn1.empty())
            {
                const JPH::RVec3 cp = inManifold.GetWorldSpaceContactPointOn1(0);
                contactPoint = {
                    static_cast<f32>(cp.GetX()),
                    static_cast<f32>(cp.GetY()),
                    static_cast<f32>(cp.GetZ())
                };
            }

            const Math::Vec3 v1 = inBody1.IsStatic() ? Math::Vec3::Zero() : FromJph(inBody1.GetLinearVelocity());
            const Math::Vec3 v2 = inBody2.IsStatic() ? Math::Vec3::Zero() : FromJph(inBody2.GetLinearVelocity());
            const bool isTrigger = subTrigger || ioSettings.mIsSensor || inBody1.IsSensor() || inBody2.IsSensor();

            // Canonicalize order so PairKey (Low, High) always matches EntityA <= EntityB
            SubstepContactRecord rec{};
            if (ent1.Pack() <= ent2.Pack())
            {
                rec.EntityA = ent1;
                rec.EntityB = ent2;
                rec.BodyA = inBody1.GetID();
                rec.BodyB = inBody2.GetID();
                rec.ContactPoint = contactPoint;
                rec.ContactNormal = FromJph(inManifold.mWorldSpaceNormal);
                rec.RelativeVelocity = v1 - v2;
            }
            else
            {
                rec.EntityA = ent2;
                rec.EntityB = ent1;
                rec.BodyA = inBody2.GetID();
                rec.BodyB = inBody1.GetID();
                rec.ContactPoint = contactPoint;
                rec.ContactNormal = -FromJph(inManifold.mWorldSpaceNormal);
                rec.RelativeVelocity = v2 - v1;
            }
            rec.PenetrationDepth = std::max(0.0f, inManifold.mPenetrationDepth);
            rec.IsTrigger = isTrigger;

            std::scoped_lock lock(m_mutex);
            m_substepContacts.push_back(rec);
        }

        const BitmaskGroupFilter* m_groupFilter = nullptr;
        mutable std::shared_mutex m_filterMutex;
        std::unordered_map<u64, EntitySubshapeFilters> m_subshapeFilters;
        std::mutex m_mutex;
        std::vector<SubstepContactRecord> m_substepContacts;
        std::vector<RemovedSubstepPair> m_substepRemoved;
    };

    struct BodyShadowState
    {
        JPH::BodyID BodyId{};
        Scene::EntityId Entity = Scene::EntityId::Invalid();
        Math::Vec3 LastPos = Math::Vec3::Zero();
        Math::Quat LastRot = Math::Quat::Identity();
        Math::Vec3 LastScale = Math::Vec3::One();
        Math::Vec3 LastLinearVel = Math::Vec3::Zero();
        Math::Vec3 LastAngularVel = Math::Vec3::Zero();
        f32 Mass = 1.0f;
        f32 Density = 1000.0f;
        Math::Vec3 HalfExtents{ 0.5f, 0.5f, 0.5f };
        Math::Vec3 ColliderOffset = Math::Vec3::Zero();
        Math::Quat ColliderRotation = Math::Quat::Identity();
        f32 Restitution = 0.35f;
        f32 Friction = 0.5f;
        f32 LinearDamping = 0.05f;
        f32 AngularDamping = 0.05f;
        f32 GravityFactor = 1.0f;
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
        Scene::RigidBodyMotionType MotionType = Scene::RigidBodyMotionType::Dynamic;
        Scene::ColliderShapeType Shape = Scene::ColliderShapeType::Box;
        u8 UseGravity = 1;
        u8 UseCCD = 0;
        u8 IsTrigger = 0;
        u8 UseDensity = 0;
        u8 LockedAxes = 0;
        bool CompoundDirty = false;
    };

    struct JointRecord
    {
        u32 Generation = 1u;
        bool Alive = false;
        JointType Type = JointType::Fixed;
        Scene::EntityId EntityA = Scene::EntityId::Invalid();
        Scene::EntityId EntityB = Scene::EntityId::Invalid();
        JointDesc Desc{};
        f32 LastAppliedForce = 0.0f;
        f32 LastAppliedTorque = 0.0f;
        Scene::EntityId SceneOwnerEntity = Scene::EntityId::Invalid();
        u32 SceneJointIndex = 0xFFFFFFFFu;
        JPH::Ref<JPH::TwoBodyConstraint> Constraint;
    };

    class MorbiCharacterContactListener final : public JPH::CharacterContactListener
    {
    public:
        MorbiCharacterContactListener(
            const JPH::PhysicsSystem* physicsSystem,
            const BitmaskGroupFilter* groupFilter,
            u32 collisionLayer,
            u32 collisionMask,
            JPH::BodyID innerBodyId) noexcept
            : m_physicsSystem(physicsSystem)
            , m_groupFilter(groupFilter)
            , m_collisionLayer(collisionLayer)
            , m_collisionMask(collisionMask)
            , m_innerBodyId(innerBodyId)
        {
        }

        void UpdateFilter(u32 collisionLayer, u32 collisionMask, JPH::BodyID innerBodyId) noexcept
        {
            m_collisionLayer = collisionLayer;
            m_collisionMask = collisionMask;
            m_innerBodyId = innerBodyId;
        }

        bool OnContactValidate(
            const JPH::CharacterVirtual* /*inCharacter*/,
            const JPH::BodyID& inBodyID2,
            const JPH::SubShapeID& /*inSubShapeID2*/) override
        {
            if (inBodyID2 == m_innerBodyId || !m_physicsSystem)
            {
                return false;
            }
            JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterfaceNoLock(), inBodyID2);
            if (!lock.Succeeded())
            {
                return false;
            }
            const JPH::Body& body2 = lock.GetBody();
            if (body2.IsSensor())
            {
                return false;
            }
            if (!m_groupFilter)
            {
                return true;
            }
            const JPH::CollisionGroup selfGroup(m_groupFilter, m_collisionLayer, m_collisionMask);
            return m_groupFilter->CanCollide(selfGroup, body2.GetCollisionGroup());
        }

        void OnContactAdded(
            const JPH::CharacterVirtual* /*inCharacter*/,
            const JPH::BodyID& /*inBodyID2*/,
            const JPH::SubShapeID& /*inSubShapeID2*/,
            JPH::RVec3Arg /*inContactPosition*/,
            JPH::Vec3Arg /*inContactNormal*/,
            JPH::CharacterContactSettings& ioSettings) override
        {
            ioSettings.mCanPushCharacter = true;
            ioSettings.mCanReceiveImpulses = true;
        }

    private:
        const JPH::PhysicsSystem* m_physicsSystem = nullptr;
        const BitmaskGroupFilter* m_groupFilter = nullptr;
        u32 m_collisionLayer = 1u;
        u32 m_collisionMask = 0xFFFFFFFFu;
        JPH::BodyID m_innerBodyId{};
    };

    struct CharacterShadowState
    {
        JPH::Ref<JPH::CharacterVirtual> Character;
        std::unique_ptr<MorbiCharacterContactListener> Listener;
        Scene::EntityId Entity = Scene::EntityId::Invalid();
        JPH::BodyID InnerBodyId{};
        Math::Vec3 LastPos = Math::Vec3::Zero();
        Math::Vec3 CenterOffset = Math::Vec3::Zero();
        f32 Radius = 0.35f;
        f32 Height = 1.8f;
        f32 Mass = 70.0f;
        f32 MaxPushStrength = 250.0f;
        f32 MaxSlopeAngleDegrees = 45.0f;
        u32 CollisionLayer = 1u;
        u32 CollisionMask = 0xFFFFFFFFu;
    };
}

namespace Morbi::Physics
{
    struct PhysicsWorld::Impl
    {
        f32 FixedTimestep = 1.0f / 60.0f;
        f32 Accumulator = 0.0f;
        Math::Vec3 Gravity{ 0.0f, -9.81f, 0.0f };

        bool JoltRuntimeAcquired = false;
        bool Initialized = false;

        Detail::MorbiBroadPhaseLayerInterface BroadPhaseInterface{};
        Detail::MorbiObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseFilter{};
        Detail::MorbiObjectLayerPairFilter ObjectLayerPairFilter{};
        JPH::Ref<Detail::BitmaskGroupFilter> GroupFilter;
        Detail::MorbiContactListener ContactListener{};

        std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> JobSystem;
        std::unique_ptr<JPH::PhysicsSystem> PhysicsSystem;

        mutable std::shared_mutex WorldMutex;
        std::unordered_map<u64, Detail::BodyShadowState> BodiesByEntity;
        std::unordered_map<u32, Scene::EntityId> EntityByBodyId;
        std::unordered_map<u64, std::vector<ColliderShapeDesc>> CompoundColliders;

        std::unordered_map<Detail::PairKey, Detail::ActivePairState, Detail::PairKeyHash> ActivePairs;
        std::vector<CollisionPairEvent> PendingExitCollisions;
        std::vector<CollisionPairEvent> Collisions;

        std::vector<Detail::JointRecord> Joints;
        std::vector<u32> FreeJointSlots;
        u32 ActiveJointCount = 0u;

        std::unordered_map<u64, Detail::CharacterShadowState> CharactersByEntity;
        JPH::CharacterVsCharacterCollisionSimple CharacterVsCharacterCollision;

        std::vector<Detail::SubstepContactRecord> SubstepContactsScratch;
        std::vector<Detail::RemovedSubstepPair> SubstepRemovedScratch;
        std::unordered_map<Detail::PairKey, Detail::SubstepContactRecord, Detail::PairKeyHash> UniqueSubstepContactsScratch;

        PhysicsStats Stats{};
        mutable u32 QueriesThisFrame = 0u;

        Detail::BodyShadowState* FindBodyLocked(Scene::EntityId entity)
        {
            auto it = BodiesByEntity.find(entity.Pack());
            return (it != BodiesByEntity.end()) ? &it->second : nullptr;
        }

        const Detail::BodyShadowState* FindBodyLocked(Scene::EntityId entity) const
        {
            auto it = BodiesByEntity.find(entity.Pack());
            return (it != BodiesByEntity.end()) ? &it->second : nullptr;
        }

        void RemoveJointsForEntityLocked(Scene::EntityId entity);
        void DestroyAllJointsLocked();
        JointHandle CreateJointLocked(Scene::Scene& scene, const JointDesc& desc, Scene::EntityId sceneOwner = Scene::EntityId::Invalid(), u32 sceneJointIndex = 0xFFFFFFFFu);
        bool UpdateJointLocked(Scene::Scene& scene, JointHandle handle, const JointDesc& desc);
        void EvaluateBreakableJointsLocked(Scene::Scene& scene, f32 fixedDt);
        void SyncSceneJointsLocked(Scene::Scene& scene);
        void SyncEntityCompoundCollidersLocked(const Scene::Scene& scene, Scene::EntityId entity);
        void EmitActiveExitsForEntityLocked(u64 packedEntity);
        void DestroyBodyLocked(u64 packedEntity);
        bool CreateBodyForEntityLocked(
            Scene::Scene& scene,
            Scene::EntityId entity,
            Scene::RigidBodyComponent& rb,
            const Scene::TransformComponent& tr);
        void SyncBodyChangesLocked(
            Scene::Scene& scene,
            Scene::EntityId entity,
            Scene::RigidBodyComponent& rb,
            const Scene::TransformComponent& tr,
            Detail::BodyShadowState& shadow);

        void DestroyCharacterLocked(u64 packedEntity);
        void DestroyAllCharactersLocked();
        bool CreateOrRebuildCharacterLocked(
            Scene::Scene& scene,
            Scene::EntityId entity,
            Scene::CharacterControllerComponent& cc,
            const Scene::TransformComponent& tr);
        void SyncAllCharactersPreStepLocked(Scene::Scene& scene);
        void StepAllCharactersSubstepLocked(Scene::Scene& scene, f32 fixedDt, u32 remainingSubsteps);
    };
}
