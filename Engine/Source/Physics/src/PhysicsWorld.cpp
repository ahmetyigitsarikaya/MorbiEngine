#include "PhysicsInternal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/RegisterTypes.h>

#include "Morbi/Core/Log.hpp"

namespace Morbi::Physics
{
    using namespace Detail;

    namespace
    {
        std::mutex g_joltGlobalMutex;
        u32 g_joltGlobalRefCount = 0u;

        void JoltTraceCallback(const char* inFmt, ...)
        {
            char buffer[1024]{};
            va_list args;
            va_start(args, inFmt);
            std::vsnprintf(buffer, sizeof(buffer), inFmt, args);
            va_end(args);
            Core::Log::Info("Jolt", buffer);
        }

#ifdef JPH_ENABLE_ASSERTS
        bool JoltAssertFailedCallback(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
        {
            Core::Log::Error("Jolt.Assert", std::string(inFile) + ":" + std::to_string(inLine) + " (" +
                inExpression + ") " + (inMessage ? inMessage : ""));
            return true;
        }
#endif

        void AcquireJoltGlobalRuntime()
        {
            std::scoped_lock lock(g_joltGlobalMutex);
            if (g_joltGlobalRefCount == 0u)
            {
                JPH::RegisterDefaultAllocator();
                JPH::Trace = JoltTraceCallback;
                JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailedCallback;)
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
            ++g_joltGlobalRefCount;
        }

        void ReleaseJoltGlobalRuntime()
        {
            std::scoped_lock lock(g_joltGlobalMutex);
            if (g_joltGlobalRefCount == 0u)
            {
                return;
            }
            --g_joltGlobalRefCount;
            if (g_joltGlobalRefCount == 0u)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        void ComputeEntityWorldTransform(
            const Scene::Scene& scene,
            Scene::EntityId entity,
            Math::Vec3& outPos,
            Math::Quat& outRot,
            Math::Vec3& outScale)
        {
            const auto* tr = scene.Transforms().TryGet(entity);
            if (!tr)
            {
                outPos = Math::Vec3::Zero();
                outRot = Math::Quat::Identity();
                outScale = Math::Vec3::One();
                return;
            }

            Scene::EntityId parent = scene.GetParent(entity);
            if (!parent.IsValid() || !scene.IsAlive(parent))
            {
                outPos = tr->Position;
                outRot = tr->Rotation;
                outScale = tr->Scale;
                return;
            }

            Scene::EntityId chain[64]{};
            u32 depth = 0u;
            Scene::EntityId curr = entity;
            while (curr.IsValid() && scene.IsAlive(curr) && depth < 64u)
            {
                chain[depth++] = curr;
                curr = scene.GetParent(curr);
            }

            outPos = Math::Vec3::Zero();
            outRot = Math::Quat::Identity();
            outScale = Math::Vec3::One();

            for (u32 i = depth; i-- > 0;)
            {
                const auto* nodeTr = scene.Transforms().TryGet(chain[i]);
                if (!nodeTr) continue;
                const Math::Vec3 scaledLocal{
                    nodeTr->Position.x * outScale.x,
                    nodeTr->Position.y * outScale.y,
                    nodeTr->Position.z * outScale.z
                };
                outPos = outPos + Math::RotateVector(outRot, scaledLocal);
                outRot = Math::NormalizeQuat(Math::MultiplyQuat(outRot, nodeTr->Rotation));
                outScale = {
                    outScale.x * nodeTr->Scale.x,
                    outScale.y * nodeTr->Scale.y,
                    outScale.z * nodeTr->Scale.z
                };
            }
        }

        void ConvertWorldToLocalTransform(
            const Scene::Scene& scene,
            Scene::EntityId entity,
            const Math::Vec3& worldPos,
            const Math::Quat& worldRot,
            Math::Vec3& outLocalPos,
            Math::Quat& outLocalRot)
        {
            const Scene::EntityId parent = scene.GetParent(entity);
            if (!parent.IsValid() || !scene.IsAlive(parent))
            {
                outLocalPos = worldPos;
                outLocalRot = worldRot;
                return;
            }

            Math::Vec3 pPos{};
            Math::Quat pRot{};
            Math::Vec3 pScale{};
            ComputeEntityWorldTransform(scene, parent, pPos, pRot, pScale);

            const Math::Quat normPRot = Math::NormalizeQuat(pRot);
            const Math::Quat invPRot{ -normPRot.x, -normPRot.y, -normPRot.z, normPRot.w };
            outLocalRot = Math::NormalizeQuat(Math::MultiplyQuat(invPRot, worldRot));

            const Math::Vec3 unscaled = Math::RotateVector(invPRot, worldPos - pPos);
            outLocalPos = {
                std::abs(pScale.x) > 1e-5f ? (unscaled.x / pScale.x) : unscaled.x,
                std::abs(pScale.y) > 1e-5f ? (unscaled.y / pScale.y) : unscaled.y,
                std::abs(pScale.z) > 1e-5f ? (unscaled.z / pScale.z) : unscaled.z
            };
        }

        [[nodiscard]] JPH::EMotionType ToJphMotionType(Scene::RigidBodyMotionType mt) noexcept
        {
            switch (mt)
            {
            case Scene::RigidBodyMotionType::Static:    return JPH::EMotionType::Static;
            case Scene::RigidBodyMotionType::Kinematic: return JPH::EMotionType::Kinematic;
            case Scene::RigidBodyMotionType::Dynamic:
            default:                                    return JPH::EMotionType::Dynamic;
            }
        }

        [[nodiscard]] JPH::ObjectLayer ToObjectLayer(Scene::RigidBodyMotionType mt) noexcept
        {
            return (mt == Scene::RigidBodyMotionType::Static)
                ? ObjectLayers::kNonMoving
                : ObjectLayers::kMoving;
        }

        [[nodiscard]] JPH::EAllowedDOFs ToAllowedDOFs(u8 lockedAxes) noexcept
        {
            if (lockedAxes == 0u)
            {
                return JPH::EAllowedDOFs::All;
            }
            JPH::EAllowedDOFs allowed = JPH::EAllowedDOFs::All;
            if ((lockedAxes & (1u << 0)) != 0u) allowed &= ~JPH::EAllowedDOFs::TranslationX;
            if ((lockedAxes & (1u << 1)) != 0u) allowed &= ~JPH::EAllowedDOFs::TranslationY;
            if ((lockedAxes & (1u << 2)) != 0u) allowed &= ~JPH::EAllowedDOFs::TranslationZ;
            if ((lockedAxes & (1u << 3)) != 0u) allowed &= ~JPH::EAllowedDOFs::RotationX;
            if ((lockedAxes & (1u << 4)) != 0u) allowed &= ~JPH::EAllowedDOFs::RotationY;
            if ((lockedAxes & (1u << 5)) != 0u) allowed &= ~JPH::EAllowedDOFs::RotationZ;

            if (allowed == JPH::EAllowedDOFs::None)
            {
                return JPH::EAllowedDOFs::TranslationY;
            }
            return allowed;
        }

        [[nodiscard]] PairKey MakePairKey(Scene::EntityId a, Scene::EntityId b) noexcept
        {
            const u64 pa = a.Pack();
            const u64 pb = b.Pack();
            return (pa <= pb) ? PairKey{ pa, pb } : PairKey{ pb, pa };
        }

        [[nodiscard]] Scene::ColliderShapeType GetEntityShape(const Scene::Scene& scene, Scene::EntityId entity) noexcept
        {
            const auto* rb = scene.RigidBodies().TryGet(entity);
            return rb ? rb->Shape : Scene::ColliderShapeType::Box;
        }

        JPH::RefConst<JPH::Shape> BuildSingleJoltShape(
            const ColliderShapeDesc& desc,
            const Math::Vec3& entityScale,
            bool useDensity,
            f32 density)
        {
            const f32 sx = std::max(std::abs(entityScale.x), 1e-3f);
            const f32 sy = std::max(std::abs(entityScale.y), 1e-3f);
            const f32 sz = std::max(std::abs(entityScale.z), 1e-3f);

            JPH::Ref<JPH::ConvexShapeSettings> baseSettings;
            switch (desc.Shape)
            {
            case Scene::ColliderShapeType::Sphere:
            {
                const f32 maxS = std::max({ sx, sy, sz });
                const f32 radius = std::max(desc.HalfExtents.x * maxS, 0.005f);
                baseSettings = new JPH::SphereShapeSettings(radius);
                break;
            }
            case Scene::ColliderShapeType::Capsule:
            {
                const f32 radius = std::max(desc.HalfExtents.x * std::max(sx, sz), 0.005f);
                const f32 halfHeight = std::max(desc.HalfExtents.y * sy, 0.001f);
                baseSettings = new JPH::CapsuleShapeSettings(halfHeight, radius);
                break;
            }
            case Scene::ColliderShapeType::Box:
            default:
            {
                const f32 hx = std::max(desc.HalfExtents.x * sx, 0.005f);
                const f32 hy = std::max(desc.HalfExtents.y * sy, 0.005f);
                const f32 hz = std::max(desc.HalfExtents.z * sz, 0.005f);
                const f32 minHalf = std::min({ hx, hy, hz });
                const f32 convexRadius = std::min(JPH::cDefaultConvexRadius, minHalf * 0.25f);
                baseSettings = new JPH::BoxShapeSettings(JPH::Vec3(hx, hy, hz), convexRadius);
                break;
            }
            }

            if (useDensity)
            {
                baseSettings->SetDensity(std::max(density, 0.01f));
            }

            const Math::Vec3 scaledOffset{
                desc.LocalOffset.x * sx,
                desc.LocalOffset.y * sy,
                desc.LocalOffset.z * sz
            };
            const bool hasOffset = !VecNearEqual(scaledOffset, Math::Vec3::Zero(), 1e-5f);
            const bool hasRot = !QuatNearEqual(desc.LocalRotation, Math::Quat::Identity(), 1e-5f);

            JPH::Shape::ShapeResult result;
            if (hasOffset || hasRot)
            {
                JPH::Ref<JPH::RotatedTranslatedShapeSettings> rtSettings =
                    new JPH::RotatedTranslatedShapeSettings(
                        ToJph(scaledOffset),
                        ToJph(desc.LocalRotation),
                        baseSettings.GetPtr());
                result = rtSettings->Create();
            }
            else
            {
                result = baseSettings->Create();
            }

            if (result.HasError())
            {
                Core::Log::Error("Physics", std::string("Jolt shape creation failed: ") + result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }

        JPH::RefConst<JPH::Shape> BuildEntityJoltShape(
            const Scene::RigidBodyComponent& rb,
            const Math::Vec3& entityScale,
            const std::vector<ColliderShapeDesc>* extraColliders)
        {
            const ColliderShapeDesc primary{
                .Shape = rb.Shape,
                .HalfExtents = rb.HalfExtents,
                .LocalOffset = rb.ColliderOffset,
                .LocalRotation = rb.ColliderRotation
            };

            const bool useDensity = (rb.UseDensity != 0u);
            if (!extraColliders || extraColliders->empty())
            {
                return BuildSingleJoltShape(primary, entityScale, useDensity, rb.Density);
            }

            JPH::Ref<JPH::StaticCompoundShapeSettings> compound = new JPH::StaticCompoundShapeSettings();
            const ColliderShapeDesc baseZeroOffset{
                .Shape = primary.Shape,
                .HalfExtents = primary.HalfExtents,
                .LocalOffset = Math::Vec3::Zero(),
                .LocalRotation = Math::Quat::Identity()
            };
            if (auto sub0 = BuildSingleJoltShape(baseZeroOffset, entityScale, useDensity, rb.Density))
            {
                const Math::Vec3 scaledOff0{
                    primary.LocalOffset.x * std::max(std::abs(entityScale.x), 1e-3f),
                    primary.LocalOffset.y * std::max(std::abs(entityScale.y), 1e-3f),
                    primary.LocalOffset.z * std::max(std::abs(entityScale.z), 1e-3f)
                };
                compound->AddShape(ToJph(scaledOff0), ToJph(primary.LocalRotation), sub0.GetPtr());
            }

            for (const auto& extra : *extraColliders)
            {
                const ColliderShapeDesc subDesc{
                    .Shape = extra.Shape,
                    .HalfExtents = extra.HalfExtents,
                    .LocalOffset = Math::Vec3::Zero(),
                    .LocalRotation = Math::Quat::Identity()
                };
                if (auto subShape = BuildSingleJoltShape(subDesc, entityScale, useDensity, rb.Density))
                {
                    const Math::Vec3 scaledOff{
                        extra.LocalOffset.x * std::max(std::abs(entityScale.x), 1e-3f),
                        extra.LocalOffset.y * std::max(std::abs(entityScale.y), 1e-3f),
                        extra.LocalOffset.z * std::max(std::abs(entityScale.z), 1e-3f)
                    };
                    compound->AddShape(ToJph(scaledOff), ToJph(extra.LocalRotation), subShape.GetPtr());
                }
            }

            JPH::Shape::ShapeResult res = compound->Create();
            if (res.HasError())
            {
                Core::Log::Error("Physics", std::string("Jolt compound shape creation failed: ") + res.GetError().c_str());
                return nullptr;
            }
            return res.Get();
        }

        void ComputeEffectiveCollisionGroup(
            const Scene::RigidBodyComponent& rb,
            const std::vector<ColliderShapeDesc>* extra,
            u32& outLayer,
            u32& outMask) noexcept
        {
            outLayer = rb.CollisionLayer;
            outMask = rb.CollisionMask;
            if (!extra)
            {
                return;
            }
            for (const auto& s : *extra)
            {
                outLayer |= s.CollisionLayer;
                outMask |= s.CollisionMask;
            }
        }

        void SyncContactSubshapeFilters(
            Detail::MorbiContactListener& listener,
            u64 packed,
            const Scene::RigidBodyComponent& rb,
            const std::vector<ColliderShapeDesc>* extra)
        {
            if (!extra || extra->empty())
            {
                listener.ClearEntitySubshapeFilters(packed);
                return;
            }
            const Detail::SubshapeFilterInfo primary{
                .CollisionLayer = rb.CollisionLayer,
                .CollisionMask = rb.CollisionMask,
                .IsTrigger = (rb.IsTrigger != 0u)
            };
            std::vector<Detail::SubshapeFilterInfo> filters;
            filters.reserve(extra->size());
            for (const auto& c : *extra)
            {
                filters.push_back(Detail::SubshapeFilterInfo{
                    .CollisionLayer = c.CollisionLayer,
                    .CollisionMask = c.CollisionMask,
                    .IsTrigger = c.IsTrigger
                });
            }
            listener.SetEntitySubshapeFilters(packed, primary, std::move(filters));
        }
    }

    void PhysicsWorld::Impl::EmitActiveExitsForEntityLocked(u64 packedEntity)
    {
        for (auto it = ActivePairs.begin(); it != ActivePairs.end();)
        {
            if (it->first.Low == packedEntity || it->first.High == packedEntity)
            {
                const auto& st = it->second;
                PendingExitCollisions.push_back(CollisionPairEvent{
                    .EntityA = st.EntityA,
                    .EntityB = st.EntityB,
                    .ContactPoint = st.LastContactPoint,
                    .ContactNormal = st.LastContactNormal,
                    .RelativeVelocity = st.LastRelativeVelocity,
                    .PenetrationDepth = 0.0f,
                    .ShapeA = st.ShapeA,
                    .ShapeB = st.ShapeB,
                    .EventType = ContactEventType::Exit,
                    .IsTrigger = st.IsTrigger
                });
                it = ActivePairs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void PhysicsWorld::Impl::DestroyBodyLocked(u64 packedEntity)
    {
        const Scene::EntityId eid = Scene::EntityId::Unpack(packedEntity);
        RemoveJointsForEntityLocked(eid);
        EmitActiveExitsForEntityLocked(packedEntity);
        ContactListener.ClearEntitySubshapeFilters(packedEntity);

        auto it = BodiesByEntity.find(packedEntity);
        if (it == BodiesByEntity.end())
        {
            return;
        }
        EntityByBodyId.erase(it->second.BodyId.GetIndexAndSequenceNumber());
        if (PhysicsSystem && !it->second.BodyId.IsInvalid())
        {
            JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();
            if (bi.IsAdded(it->second.BodyId))
            {
                bi.RemoveBody(it->second.BodyId);
            }
            bi.DestroyBody(it->second.BodyId);
        }
        BodiesByEntity.erase(it);
    }

    bool PhysicsWorld::Impl::CreateBodyForEntityLocked(
        Scene::Scene& scene,
        Scene::EntityId entity,
        Scene::RigidBodyComponent& rb,
        const Scene::TransformComponent& /*tr*/)
    {
        if (!PhysicsSystem || !entity.IsValid())
        {
            return false;
        }

        Math::Vec3 worldPos{};
        Math::Quat worldRot{};
        Math::Vec3 worldScale{};
        ComputeEntityWorldTransform(scene, entity, worldPos, worldRot, worldScale);

        const u64 packed = entity.Pack();
        DestroyBodyLocked(packed);

        const auto extraIt = CompoundColliders.find(packed);
        const std::vector<ColliderShapeDesc>* extraPtr =
            (extraIt != CompoundColliders.end()) ? &extraIt->second : nullptr;

        JPH::RefConst<JPH::Shape> shape = BuildEntityJoltShape(rb, worldScale, extraPtr);
        if (!shape)
        {
            return false;
        }

        const JPH::EMotionType jphMotion = ToJphMotionType(rb.MotionType);
        const JPH::ObjectLayer objLayer = ToObjectLayer(rb.MotionType);

        JPH::BodyCreationSettings bcs(
            shape.GetPtr(),
            ToJphR(worldPos),
            ToJph(worldRot),
            jphMotion,
            objLayer);

        bcs.mUserData = packed;
        bcs.mAllowDynamicOrKinematic = true;
        bcs.mIsSensor = (rb.IsTrigger != 0u);
        bcs.mMotionQuality = (rb.UseCCD != 0u && rb.MotionType == Scene::RigidBodyMotionType::Dynamic)
            ? JPH::EMotionQuality::LinearCast
            : JPH::EMotionQuality::Discrete;
        bcs.mFriction = std::max(0.0f, rb.Friction);
        bcs.mRestitution = std::clamp(rb.Restitution, 0.0f, 1.0f);
        bcs.mLinearDamping = std::max(0.0f, rb.LinearDamping);
        bcs.mAngularDamping = std::max(0.0f, rb.AngularDamping);
        bcs.mGravityFactor = (rb.UseGravity != 0u) ? rb.GravityFactor : 0.0f;
        bcs.mAllowedDOFs = ToAllowedDOFs(rb.LockedAxes);
        u32 effLayer = rb.CollisionLayer;
        u32 effMask = rb.CollisionMask;
        ComputeEffectiveCollisionGroup(rb, extraPtr, effLayer, effMask);
        bcs.mCollisionGroup = JPH::CollisionGroup(GroupFilter.GetPtr(), effLayer, effMask);
        SyncContactSubshapeFilters(ContactListener, packed, rb, extraPtr);

        if (rb.MotionType != Scene::RigidBodyMotionType::Static)
        {
            bcs.mLinearVelocity = ToJph(rb.LinearVelocity);
            bcs.mAngularVelocity = ToJph(rb.AngularVelocity);
        }

        if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic)
        {
            if (rb.UseDensity != 0u)
            {
                bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;
            }
            else
            {
                bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                bcs.mMassPropertiesOverride.mMass = std::max(rb.Mass, 0.001f);
            }
        }

        JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();
        const JPH::EActivation activation = (rb.MotionType != Scene::RigidBodyMotionType::Static && rb.IsAwake != 0u)
            ? JPH::EActivation::Activate
            : JPH::EActivation::DontActivate;

        const JPH::BodyID bodyId = bi.CreateAndAddBody(bcs, activation);
        if (bodyId.IsInvalid())
        {
            Core::Log::Error("Physics", "Failed to create Jolt body (pool exhausted?) for entity " + std::to_string(entity.Index));
            rb.JoltBodyId = 0xFFFFFFFFu;
            return false;
        }

        if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic && rb.UseDensity != 0u)
        {
            JPH::BodyLockRead lock(PhysicsSystem->GetBodyLockInterface(), bodyId);
            if (lock.Succeeded())
            {
                const auto* mp = lock.GetBody().GetMotionProperties();
                if (mp && mp->GetInverseMass() > 0.0f)
                {
                    rb.Mass = 1.0f / mp->GetInverseMass();
                }
            }
        }

        rb.JoltBodyId = bodyId.GetIndexAndSequenceNumber();
        rb.DirtyFlags = 0u;

        BodyShadowState shadow{};
        shadow.BodyId = bodyId;
        shadow.Entity = entity;
        shadow.LastPos = worldPos;
        shadow.LastRot = worldRot;
        shadow.LastScale = worldScale;
        shadow.LastLinearVel = rb.LinearVelocity;
        shadow.LastAngularVel = rb.AngularVelocity;
        shadow.Mass = rb.Mass;
        shadow.Density = rb.Density;
        shadow.HalfExtents = rb.HalfExtents;
        shadow.ColliderOffset = rb.ColliderOffset;
        shadow.ColliderRotation = rb.ColliderRotation;
        shadow.Restitution = rb.Restitution;
        shadow.Friction = rb.Friction;
        shadow.LinearDamping = rb.LinearDamping;
        shadow.AngularDamping = rb.AngularDamping;
        shadow.GravityFactor = rb.GravityFactor;
        shadow.CollisionLayer = rb.CollisionLayer;
        shadow.CollisionMask = rb.CollisionMask;
        shadow.MotionType = rb.MotionType;
        shadow.Shape = rb.Shape;
        shadow.UseGravity = rb.UseGravity;
        shadow.UseCCD = rb.UseCCD;
        shadow.IsTrigger = rb.IsTrigger;
        shadow.UseDensity = rb.UseDensity;
        shadow.LockedAxes = rb.LockedAxes;
        shadow.CompoundDirty = false;

        BodiesByEntity[packed] = shadow;
        EntityByBodyId[rb.JoltBodyId] = entity;
        return true;
    }

    void PhysicsWorld::Impl::SyncBodyChangesLocked(
        Scene::Scene& scene,
        Scene::EntityId entity,
        Scene::RigidBodyComponent& rb,
        const Scene::TransformComponent& tr,
        BodyShadowState& shadow)
    {
        JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();
        const JPH::BodyID bodyId = shadow.BodyId;

        Math::Vec3 worldPos{};
        Math::Quat worldRot{};
        Math::Vec3 worldScale{};
        ComputeEntityWorldTransform(scene, entity, worldPos, worldRot, worldScale);

        const bool motionChanged = (rb.MotionType != shadow.MotionType) ||
            ((rb.DirtyFlags & static_cast<u8>(Scene::RigidBodyDirtyFlags::MotionType)) != 0u);
        const bool shapeChanged = shadow.CompoundDirty ||
            (rb.Shape != shadow.Shape) ||
            !VecNearEqual(rb.HalfExtents, shadow.HalfExtents) ||
            !VecNearEqual(rb.ColliderOffset, shadow.ColliderOffset) ||
            !QuatNearEqual(rb.ColliderRotation, shadow.ColliderRotation) ||
            !VecNearEqual(worldScale, shadow.LastScale) ||
            ((rb.DirtyFlags & static_cast<u8>(Scene::RigidBodyDirtyFlags::Shape)) != 0u);
        const bool lockedAxesChanged = (rb.LockedAxes != shadow.LockedAxes);

        if (lockedAxesChanged)
        {
            CreateBodyForEntityLocked(scene, entity, rb, tr);
            return;
        }

        if (motionChanged)
        {
            const JPH::EMotionType newJphMotion = ToJphMotionType(rb.MotionType);
            const JPH::EActivation act = (rb.MotionType == Scene::RigidBodyMotionType::Static)
                ? JPH::EActivation::DontActivate
                : JPH::EActivation::Activate;
            bi.SetMotionType(bodyId, newJphMotion, act);
            bi.SetObjectLayer(bodyId, ToObjectLayer(rb.MotionType));
            if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic)
            {
                JPH::BodyLockWrite mLock(PhysicsSystem->GetBodyLockInterface(), bodyId);
                if (mLock.Succeeded())
                {
                    JPH::Body& b = mLock.GetBody();
                    if (auto* mp = b.GetMotionProperties())
                    {
                        mp->SetLinearDamping(std::max(0.0f, rb.LinearDamping));
                        mp->SetAngularDamping(std::max(0.0f, rb.AngularDamping));
                        JPH::MassProperties mpCalc = b.GetShape()->GetMassProperties();
                        if (rb.UseDensity == 0u)
                        {
                            mpCalc.ScaleToMass(std::max(rb.Mass, 0.001f));
                        }
                        else
                        {
                            const f32 ratio = std::max(rb.Density, 0.01f) / 1000.0f;
                            mpCalc.mMass = std::max(mpCalc.mMass * ratio, 0.001f);
                        }
                        mp->SetMassProperties(ToAllowedDOFs(rb.LockedAxes), mpCalc);
                    }
                }
            }
            shadow.MotionType = rb.MotionType;
        }

        if (shapeChanged)
        {
            const auto extraIt = CompoundColliders.find(entity.Pack());
            const std::vector<ColliderShapeDesc>* extraPtr =
                (extraIt != CompoundColliders.end()) ? &extraIt->second : nullptr;
            if (auto newShape = BuildEntityJoltShape(rb, worldScale, extraPtr))
            {
                const bool updateMass = (rb.MotionType == Scene::RigidBodyMotionType::Dynamic);
                bi.SetShape(bodyId, newShape.GetPtr(), updateMass, JPH::EActivation::Activate);
                if (updateMass && rb.UseDensity == 0u)
                {
                    JPH::BodyLockWrite lock(PhysicsSystem->GetBodyLockInterface(), bodyId);
                    if (lock.Succeeded())
                    {
                        JPH::Body& b = lock.GetBody();
                        if (auto* mp = b.GetMotionProperties())
                        {
                            JPH::MassProperties mpCalc = newShape->GetMassProperties();
                            mpCalc.ScaleToMass(std::max(rb.Mass, 0.001f));
                            mp->SetMassProperties(ToAllowedDOFs(rb.LockedAxes), mpCalc);
                        }
                    }
                }
            }
            u32 effLayer = rb.CollisionLayer;
            u32 effMask = rb.CollisionMask;
            ComputeEffectiveCollisionGroup(rb, extraPtr, effLayer, effMask);
            {
                JPH::BodyLockWrite bodyLock(PhysicsSystem->GetBodyLockInterface(), bodyId);
                if (bodyLock.Succeeded())
                {
                    bodyLock.GetBody().SetCollisionGroup(
                        JPH::CollisionGroup(GroupFilter.GetPtr(), effLayer, effMask));
                }
            }
            SyncContactSubshapeFilters(ContactListener, entity.Pack(), rb, extraPtr);
            shadow.Shape = rb.Shape;
            shadow.HalfExtents = rb.HalfExtents;
            shadow.ColliderOffset = rb.ColliderOffset;
            shadow.ColliderRotation = rb.ColliderRotation;
            shadow.LastScale = worldScale;
            shadow.CompoundDirty = false;
        }

        const bool propsChanged =
            std::abs(rb.Mass - shadow.Mass) > 1e-5f ||
            std::abs(rb.Density - shadow.Density) > 1e-4f ||
            rb.UseDensity != shadow.UseDensity ||
            std::abs(rb.Friction - shadow.Friction) > 1e-5f ||
            std::abs(rb.Restitution - shadow.Restitution) > 1e-5f ||
            std::abs(rb.LinearDamping - shadow.LinearDamping) > 1e-5f ||
            std::abs(rb.AngularDamping - shadow.AngularDamping) > 1e-5f ||
            std::abs(rb.GravityFactor - shadow.GravityFactor) > 1e-5f ||
            rb.UseGravity != shadow.UseGravity ||
            rb.UseCCD != shadow.UseCCD ||
            rb.IsTrigger != shadow.IsTrigger ||
            rb.CollisionLayer != shadow.CollisionLayer ||
            rb.CollisionMask != shadow.CollisionMask ||
            ((rb.DirtyFlags & static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties)) != 0u);

        if (propsChanged)
        {
            bi.SetFriction(bodyId, std::max(0.0f, rb.Friction));
            bi.SetRestitution(bodyId, std::clamp(rb.Restitution, 0.0f, 1.0f));
            bi.SetGravityFactor(bodyId, (rb.UseGravity != 0u) ? rb.GravityFactor : 0.0f);
            bi.SetMotionQuality(
                bodyId,
                (rb.UseCCD != 0u && rb.MotionType == Scene::RigidBodyMotionType::Dynamic)
                    ? JPH::EMotionQuality::LinearCast
                    : JPH::EMotionQuality::Discrete);

            const auto extraIt = CompoundColliders.find(entity.Pack());
            const std::vector<ColliderShapeDesc>* extraPtr =
                (extraIt != CompoundColliders.end()) ? &extraIt->second : nullptr;
            u32 effLayer = rb.CollisionLayer;
            u32 effMask = rb.CollisionMask;
            ComputeEffectiveCollisionGroup(rb, extraPtr, effLayer, effMask);
            SyncContactSubshapeFilters(ContactListener, entity.Pack(), rb, extraPtr);

            {
                JPH::BodyLockWrite lock(PhysicsSystem->GetBodyLockInterface(), bodyId);
                if (lock.Succeeded())
                {
                    JPH::Body& b = lock.GetBody();
                    b.SetIsSensor(rb.IsTrigger != 0u);
                    b.SetCollisionGroup(JPH::CollisionGroup(GroupFilter.GetPtr(), effLayer, effMask));
                    if (auto* mp = b.GetMotionProperties())
                    {
                        mp->SetLinearDamping(std::max(0.0f, rb.LinearDamping));
                        mp->SetAngularDamping(std::max(0.0f, rb.AngularDamping));

                        if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic &&
                            (std::abs(rb.Mass - shadow.Mass) > 1e-5f ||
                             std::abs(rb.Density - shadow.Density) > 1e-4f ||
                             rb.UseDensity != shadow.UseDensity))
                        {
                            JPH::MassProperties mpCalc = b.GetShape()->GetMassProperties();
                            if (rb.UseDensity == 0u)
                            {
                                mpCalc.ScaleToMass(std::max(rb.Mass, 0.001f));
                            }
                            else
                            {
                                const f32 ratio = std::max(rb.Density, 0.01f) / 1000.0f;
                                mpCalc.mMass = std::max(mpCalc.mMass * ratio, 0.001f);
                            }
                            mp->SetMassProperties(ToAllowedDOFs(rb.LockedAxes), mpCalc);
                        }
                    }
                }
            }

            if (rb.MotionType != Scene::RigidBodyMotionType::Static)
            {
                bi.ActivateBody(bodyId);
            }

            shadow.Mass = rb.Mass;
            shadow.Density = rb.Density;
            shadow.UseDensity = rb.UseDensity;
            shadow.Friction = rb.Friction;
            shadow.Restitution = rb.Restitution;
            shadow.LinearDamping = rb.LinearDamping;
            shadow.AngularDamping = rb.AngularDamping;
            shadow.GravityFactor = rb.GravityFactor;
            shadow.UseGravity = rb.UseGravity;
            shadow.UseCCD = rb.UseCCD;
            shadow.IsTrigger = rb.IsTrigger;
            shadow.CollisionLayer = rb.CollisionLayer;
            shadow.CollisionMask = rb.CollisionMask;
        }

        if (rb.MotionType != Scene::RigidBodyMotionType::Static)
        {
            if (!VecNearEqual(rb.LinearVelocity, shadow.LastLinearVel))
            {
                bi.SetLinearVelocity(bodyId, ToJph(rb.LinearVelocity));
                shadow.LastLinearVel = rb.LinearVelocity;
            }
            if (!VecNearEqual(rb.AngularVelocity, shadow.LastAngularVel))
            {
                bi.SetAngularVelocity(bodyId, ToJph(rb.AngularVelocity));
                shadow.LastAngularVel = rb.AngularVelocity;
            }
        }

        const bool explicitTeleport = (rb.DirtyFlags & static_cast<u8>(Scene::RigidBodyDirtyFlags::Transform)) != 0u;
        const bool trChanged = !VecNearEqual(worldPos, shadow.LastPos) || !QuatNearEqual(worldRot, shadow.LastRot);

        if (rb.MotionType == Scene::RigidBodyMotionType::Static)
        {
            if (trChanged || explicitTeleport)
            {
                bi.SetPositionAndRotation(bodyId, ToJphR(worldPos), ToJph(worldRot), JPH::EActivation::DontActivate);
                shadow.LastPos = worldPos;
                shadow.LastRot = worldRot;
            }
        }
        else if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic && explicitTeleport)
        {
            bi.SetPositionAndRotation(bodyId, ToJphR(worldPos), ToJph(worldRot), JPH::EActivation::Activate);
            shadow.LastPos = worldPos;
            shadow.LastRot = worldRot;
        }

        rb.DirtyFlags = 0u;
    }

    PhysicsWorld::PhysicsWorld(f32 fixedTimestep)
        : m_impl(std::make_unique<Impl>())
    {
        m_impl->FixedTimestep = std::max(fixedTimestep, 1.0f / 240.0f);
    }

    PhysicsWorld::~PhysicsWorld()
    {
        Shutdown();
    }

    bool PhysicsWorld::Initialize()
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (m_impl->Initialized)
        {
            return true;
        }

        AcquireJoltGlobalRuntime();
        m_impl->JoltRuntimeAcquired = true;

        m_impl->GroupFilter = new BitmaskGroupFilter();
        m_impl->TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32u * 1024u * 1024u);

        const int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
        const int workerThreads = std::clamp(hwThreads - 1, 1, 15);
        m_impl->JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            workerThreads);

        constexpr JPH::uint kMaxBodies = 16384u;
        constexpr JPH::uint kNumBodyMutexes = 0u;
        constexpr JPH::uint kMaxBodyPairs = 65536u;
        constexpr JPH::uint kMaxContactConstraints = 16384u;

        m_impl->PhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
        m_impl->PhysicsSystem->Init(
            kMaxBodies,
            kNumBodyMutexes,
            kMaxBodyPairs,
            kMaxContactConstraints,
            m_impl->BroadPhaseInterface,
            m_impl->ObjectVsBroadPhaseFilter,
            m_impl->ObjectLayerPairFilter);

        m_impl->PhysicsSystem->SetGravity(ToJph(m_impl->Gravity));
        m_impl->ContactListener.SetGroupFilter(m_impl->GroupFilter.GetPtr());
        m_impl->PhysicsSystem->SetContactListener(&m_impl->ContactListener);

        JPH::PhysicsSettings physSettings = m_impl->PhysicsSystem->GetPhysicsSettings();
        physSettings.mPenetrationSlop = 0.008f;
        physSettings.mNumVelocitySteps = 16u;
        physSettings.mNumPositionSteps = 4u;
        m_impl->PhysicsSystem->SetPhysicsSettings(physSettings);

        m_impl->BodiesByEntity.clear();
        m_impl->EntityByBodyId.clear();
        m_impl->CompoundColliders.clear();
        m_impl->ActivePairs.clear();
        m_impl->PendingExitCollisions.clear();
        m_impl->Collisions.clear();
        m_impl->Joints.clear();
        m_impl->FreeJointSlots.clear();
        m_impl->ActiveJointCount = 0u;
        m_impl->ContactListener.Clear();
        m_impl->Accumulator = 0.0f;
        m_impl->Stats = {};
        m_impl->Initialized = true;

        Core::Log::Info("Physics", "Initialized Jolt Physics v5.2.0 World (FixedTimestep=" +
            std::to_string(m_impl->FixedTimestep * 1000.0f) + " ms, WorkerThreads=" +
            std::to_string(workerThreads) + ", BroadPhaseLayers=2 [NonMoving, Moving])");
        return true;
    }

    void PhysicsWorld::Shutdown()
    {
        if (!m_impl)
        {
            return;
        }

        std::unique_lock lock(m_impl->WorldMutex);
        m_impl->DestroyAllJointsLocked();
        m_impl->DestroyAllCharactersLocked();

        if (m_impl->PhysicsSystem)
        {
            std::vector<u64> packedIds;
            packedIds.reserve(m_impl->BodiesByEntity.size());
            for (const auto& [packed, _] : m_impl->BodiesByEntity)
            {
                packedIds.push_back(packed);
            }
            for (u64 packed : packedIds)
            {
                m_impl->DestroyBodyLocked(packed);
            }
            m_impl->PhysicsSystem->SetContactListener(nullptr);
        }

        m_impl->BodiesByEntity.clear();
        m_impl->EntityByBodyId.clear();
        m_impl->CompoundColliders.clear();
        m_impl->ActivePairs.clear();
        m_impl->PendingExitCollisions.clear();
        m_impl->Collisions.clear();
        m_impl->ContactListener.Clear();
        m_impl->ContactListener.ClearAllSubshapeFilters();
        m_impl->ContactListener.SetGroupFilter(nullptr);

        m_impl->PhysicsSystem.reset();
        m_impl->JobSystem.reset();
        m_impl->TempAllocator.reset();
        m_impl->GroupFilter = nullptr;

        if (m_impl->JoltRuntimeAcquired)
        {
            ReleaseJoltGlobalRuntime();
            m_impl->JoltRuntimeAcquired = false;
        }

        m_impl->Initialized = false;
    }

    bool PhysicsWorld::IsInitialized() const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return m_impl->Initialized;
    }

    void PhysicsWorld::SetGravity(const Math::Vec3& gravity) noexcept
    {
        std::unique_lock lock(m_impl->WorldMutex);
        m_impl->Gravity = gravity;
        if (m_impl->PhysicsSystem)
        {
            m_impl->PhysicsSystem->SetGravity(ToJph(gravity));
        }
    }

    Math::Vec3 PhysicsWorld::GetGravity() const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return m_impl->Gravity;
    }

    void PhysicsWorld::SetFixedTimestep(f32 fixedTimestep) noexcept
    {
        std::unique_lock lock(m_impl->WorldMutex);
        m_impl->FixedTimestep = std::max(fixedTimestep, 1.0f / 240.0f);
    }

    f32 PhysicsWorld::GetFixedTimestep() const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return m_impl->FixedTimestep;
    }

    f32 PhysicsWorld::GetInterpolationAlpha() const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return (m_impl->FixedTimestep > 0.0f)
            ? std::clamp(m_impl->Accumulator / m_impl->FixedTimestep, 0.0f, 1.0f)
            : 0.0f;
    }

    void PhysicsWorld::SetLayerCollision(u32 layerIndexA, u32 layerIndexB, bool shouldCollide) noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (m_impl->GroupFilter)
        {
            m_impl->GroupFilter->SetLayerCollision(layerIndexA, layerIndexB, shouldCollide);
        }
    }

    bool PhysicsWorld::GetLayerCollision(u32 layerIndexA, u32 layerIndexB) const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return m_impl->GroupFilter
            ? m_impl->GroupFilter->GetLayerCollision(layerIndexA, layerIndexB)
            : false;
    }

    u32 PhysicsWorld::StepSimulation(
        f32 deltaTime,
        Scene::Scene& scene,
        Core::TaskScheduler* scheduler,
        const FixedStepCallback& preSubstepCallback)
    {
        (void)scheduler;
        const auto startClock = std::chrono::steady_clock::now();

        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem)
        {
            return 0u;
        }

        // 1. Prune stale bodies whose entity was destroyed or lost its RigidBodyComponent
        {
            std::vector<u64> staleEntities;
            for (const auto& [packed, shadow] : m_impl->BodiesByEntity)
            {
                if (!scene.IsAlive(shadow.Entity) || scene.RigidBodies().TryGet(shadow.Entity) == nullptr)
                {
                    staleEntities.push_back(packed);
                }
            }
            for (u64 packed : staleEntities)
            {
                m_impl->DestroyBodyLocked(packed);
            }
        }

        // 2. Register newly added RigidBodyComponents and sync runtime property/shape/kinematic edits
        auto rbSpan = scene.RigidBodies().RawComponents();
        auto rbEntities = scene.RigidBodies().RawEntities();
        u32 rbCount = static_cast<u32>(rbSpan.size());
        bool addedNewBodies = false;

        for (u32 i = 0; i < rbCount; ++i)
        {
            Scene::RigidBodyComponent& rb = rbSpan[i];
            const Scene::EntityId entity = rbEntities[i];
            const auto* tr = scene.Transforms().TryGet(entity);
            if (!tr || !scene.IsAlive(entity))
            {
                continue;
            }

            auto* shadow = m_impl->FindBodyLocked(entity);
            if (!shadow || rb.JoltBodyId != shadow->BodyId.GetIndexAndSequenceNumber())
            {
                if (m_impl->CreateBodyForEntityLocked(scene, entity, rb, *tr))
                {
                    addedNewBodies = true;
                }
            }
            else
            {
                m_impl->SyncBodyChangesLocked(scene, entity, rb, *tr, *shadow);
            }
        }

        if (addedNewBodies)
        {
            m_impl->PhysicsSystem->OptimizeBroadPhase();
        }
        m_impl->SyncAllCharactersPreStepLocked(scene);

        // 3. Prepare frame collision event aggregation (carry over any Exit events from DestroyBody calls)
        m_impl->Collisions.clear();
        if (!m_impl->PendingExitCollisions.empty())
        {
            m_impl->Collisions.insert(
                m_impl->Collisions.end(),
                std::make_move_iterator(m_impl->PendingExitCollisions.begin()),
                std::make_move_iterator(m_impl->PendingExitCollisions.end()));
            m_impl->PendingExitCollisions.clear();
        }
        for (auto& [_, activeState] : m_impl->ActivePairs)
        {
            activeState.EmittedStayThisFrame = false;
        }
        m_impl->ContactListener.Clear();

        m_impl->Accumulator += std::clamp(deltaTime, 0.0f, 0.25f);
        u32 stepsExecuted = 0u;
        constexpr u32 kMaxSubSteps = 4u;
        const f32 fixedDt = m_impl->FixedTimestep;

        const u32 plannedSteps = std::min(
            kMaxSubSteps,
            static_cast<u32>(std::floor(m_impl->Accumulator / fixedDt + 1e-6f)));

        auto& substepContacts = m_impl->SubstepContactsScratch;
        auto& substepRemoved = m_impl->SubstepRemovedScratch;
        auto& uniqueSubstepContacts = m_impl->UniqueSubstepContactsScratch;
        substepContacts.clear();
        substepRemoved.clear();
        uniqueSubstepContacts.clear();

        JPH::BodyInterface& bi = m_impl->PhysicsSystem->GetBodyInterface();

        while (m_impl->Accumulator >= fixedDt && stepsExecuted < kMaxSubSteps)
        {
            m_impl->Accumulator -= fixedDt;
            ++stepsExecuted;

            if (preSubstepCallback)
            {
                lock.unlock();
                preSubstepCallback(fixedDt);
                lock.lock();

                // Re-sync any kinematic/property/velocity/entity edits made during OnFixedUpdate
                rbSpan = scene.RigidBodies().RawComponents();
                rbEntities = scene.RigidBodies().RawEntities();
                rbCount = static_cast<u32>(rbSpan.size());
                for (u32 i = 0; i < rbCount; ++i)
                {
                    const Scene::EntityId ent = rbEntities[i];
                    const auto* tr = scene.Transforms().TryGet(ent);
                    if (!tr || !scene.IsAlive(ent)) continue;
                    auto* shadow = m_impl->FindBodyLocked(ent);
                    if (!shadow)
                    {
                        m_impl->CreateBodyForEntityLocked(scene, ent, rbSpan[i], *tr);
                    }
                    else
                    {
                        m_impl->SyncBodyChangesLocked(scene, ent, rbSpan[i], *tr, *shadow);
                    }
                }
                m_impl->SyncAllCharactersPreStepLocked(scene);
            }

            const u32 remainingSteps = (plannedSteps >= stepsExecuted) ? (plannedSteps - stepsExecuted + 1u) : 1u;
            m_impl->StepAllCharactersSubstepLocked(scene, fixedDt, remainingSteps);

            if (plannedSteps > 0u)
            {
                const f32 stepFraction = static_cast<f32>(stepsExecuted) / static_cast<f32>(plannedSteps);
                for (u32 i = 0; i < rbCount; ++i)
                {
                    const Scene::RigidBodyComponent& rb = rbSpan[i];
                    if (rb.MotionType != Scene::RigidBodyMotionType::Kinematic)
                    {
                        continue;
                    }
                    const auto* tr = scene.Transforms().TryGet(rbEntities[i]);
                    auto* shadow = m_impl->FindBodyLocked(rbEntities[i]);
                    if (!tr || !shadow)
                    {
                        continue;
                    }
                    Math::Vec3 targetWorldPos{};
                    Math::Quat targetWorldRot{};
                    Math::Vec3 targetWorldScale{};
                    ComputeEntityWorldTransform(scene, rbEntities[i], targetWorldPos, targetWorldRot, targetWorldScale);
                    const bool trMoved = !VecNearEqual(targetWorldPos, shadow->LastPos) ||
                                         !QuatNearEqual(targetWorldRot, shadow->LastRot);
                    if (trMoved)
                    {
                        const Math::Vec3 subPos = Math::Lerp(shadow->LastPos, targetWorldPos, stepFraction);
                        const Math::Quat subRot = Math::Slerp(shadow->LastRot, targetWorldRot, stepFraction);
                        bi.MoveKinematic(shadow->BodyId, ToJphR(subPos), ToJph(subRot), fixedDt);
                    }
                }
            }

            m_impl->PhysicsSystem->Update(
                fixedDt,
                1,
                m_impl->TempAllocator.get(),
                m_impl->JobSystem.get());

            m_impl->EvaluateBreakableJointsLocked(scene, fixedDt);

            // Process Enter / Stay / Exit transitions for this substep
            substepContacts.clear();
            substepRemoved.clear();
            m_impl->ContactListener.DrainSubstep(substepContacts, substepRemoved);

            uniqueSubstepContacts.clear();
            for (const auto& rec : substepContacts)
            {
                const PairKey key = MakePairKey(rec.EntityA, rec.EntityB);
                auto [it, inserted] = uniqueSubstepContacts.emplace(key, rec);
                if (!inserted && rec.PenetrationDepth > it->second.PenetrationDepth)
                {
                    it->second = rec;
                }
            }

            for (const auto& [key, rec] : uniqueSubstepContacts)
            {
                const auto shapeA = GetEntityShape(scene, rec.EntityA);
                const auto shapeB = GetEntityShape(scene, rec.EntityB);

                auto activeIt = m_impl->ActivePairs.find(key);
                if (activeIt == m_impl->ActivePairs.end())
                {
                    m_impl->Collisions.push_back(CollisionPairEvent{
                        .EntityA = rec.EntityA,
                        .EntityB = rec.EntityB,
                        .ContactPoint = rec.ContactPoint,
                        .ContactNormal = rec.ContactNormal,
                        .RelativeVelocity = rec.RelativeVelocity,
                        .PenetrationDepth = rec.PenetrationDepth,
                        .ShapeA = shapeA,
                        .ShapeB = shapeB,
                        .EventType = ContactEventType::Enter,
                        .IsTrigger = rec.IsTrigger
                    });
                    m_impl->ActivePairs.emplace(key, ActivePairState{
                        .EntityA = rec.EntityA,
                        .EntityB = rec.EntityB,
                        .BodyA = rec.BodyA,
                        .BodyB = rec.BodyB,
                        .LastContactPoint = rec.ContactPoint,
                        .LastContactNormal = rec.ContactNormal,
                        .LastRelativeVelocity = rec.RelativeVelocity,
                        .LastPenetrationDepth = rec.PenetrationDepth,
                        .ShapeA = shapeA,
                        .ShapeB = shapeB,
                        .IsTrigger = rec.IsTrigger,
                        .SleepingPaused = false,
                        .EmittedStayThisFrame = true
                    });
                }
                else
                {
                    auto& st = activeIt->second;
                    st.LastContactPoint = rec.ContactPoint;
                    st.LastContactNormal = rec.ContactNormal;
                    st.LastRelativeVelocity = rec.RelativeVelocity;
                    st.LastPenetrationDepth = rec.PenetrationDepth;
                    st.ShapeA = shapeA;
                    st.ShapeB = shapeB;
                    st.IsTrigger = rec.IsTrigger;
                    st.SleepingPaused = false;

                    if (!st.EmittedStayThisFrame)
                    {
                        m_impl->Collisions.push_back(CollisionPairEvent{
                            .EntityA = rec.EntityA,
                            .EntityB = rec.EntityB,
                            .ContactPoint = rec.ContactPoint,
                            .ContactNormal = rec.ContactNormal,
                            .RelativeVelocity = rec.RelativeVelocity,
                            .PenetrationDepth = rec.PenetrationDepth,
                            .ShapeA = shapeA,
                            .ShapeB = shapeB,
                            .EventType = ContactEventType::Stay,
                            .IsTrigger = rec.IsTrigger
                        });
                        st.EmittedStayThisFrame = true;
                    }
                }
            }

            for (const auto& rem : substepRemoved)
            {
                const auto it1 = m_impl->EntityByBodyId.find(rem.Body1.GetIndexAndSequenceNumber());
                const auto it2 = m_impl->EntityByBodyId.find(rem.Body2.GetIndexAndSequenceNumber());
                if (it1 == m_impl->EntityByBodyId.end() || it2 == m_impl->EntityByBodyId.end())
                {
                    continue;
                }
                const PairKey key = MakePairKey(it1->second, it2->second);
                if (uniqueSubstepContacts.find(key) != uniqueSubstepContacts.end())
                {
                    continue;
                }
                if (m_impl->PhysicsSystem->WereBodiesInContact(rem.Body1, rem.Body2))
                {
                    continue;
                }

                auto activeIt = m_impl->ActivePairs.find(key);
                if (activeIt == m_impl->ActivePairs.end())
                {
                    continue;
                }

                auto& st = activeIt->second;
                const bool bothAlive = scene.IsAlive(st.EntityA) && scene.IsAlive(st.EntityB) &&
                    bi.IsAdded(st.BodyA) && bi.IsAdded(st.BodyB);
                if (bothAlive)
                {
                    const auto* rbA = scene.RigidBodies().TryGet(st.EntityA);
                    const auto* rbB = scene.RigidBodies().TryGet(st.EntityB);
                    const bool movingActiveA = rbA && rbA->MotionType != Scene::RigidBodyMotionType::Static && bi.IsActive(st.BodyA);
                    const bool movingActiveB = rbB && rbB->MotionType != Scene::RigidBodyMotionType::Static && bi.IsActive(st.BodyB);
                    if (rbA && rbB && !movingActiveA && !movingActiveB)
                    {
                        // Bodies went to sleep while resting against each other; pause pair without emitting Exit
                        st.SleepingPaused = true;
                        continue;
                    }
                }

                m_impl->Collisions.push_back(CollisionPairEvent{
                    .EntityA = st.EntityA,
                    .EntityB = st.EntityB,
                    .ContactPoint = st.LastContactPoint,
                    .ContactNormal = st.LastContactNormal,
                    .RelativeVelocity = st.LastRelativeVelocity,
                    .PenetrationDepth = 0.0f,
                    .ShapeA = st.ShapeA,
                    .ShapeB = st.ShapeB,
                    .EventType = ContactEventType::Exit,
                    .IsTrigger = st.IsTrigger
                });
                m_impl->ActivePairs.erase(activeIt);
            }
        }

        // 4. Write Jolt simulation results back to SoA TransformComponent & RigidBodyComponent
        u32 dynamicCount = 0u;
        u32 kinematicCount = 0u;
        u32 staticCount = 0u;
        u32 awakeCount = 0u;
        u32 sleepingCount = 0u;

        const JPH::BodyLockInterfaceLocking& lockInterface = m_impl->PhysicsSystem->GetBodyLockInterface();
        for (u32 i = 0; i < rbCount; ++i)
        {
            Scene::RigidBodyComponent& rb = rbSpan[i];
            const Scene::EntityId entity = rbEntities[i];
            auto* shadow = m_impl->FindBodyLocked(entity);
            if (!shadow)
            {
                continue;
            }

            if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic) ++dynamicCount;
            else if (rb.MotionType == Scene::RigidBodyMotionType::Kinematic) ++kinematicCount;
            else ++staticCount;

            JPH::BodyLockRead bodyLock(lockInterface, shadow->BodyId);
            if (!bodyLock.Succeeded())
            {
                continue;
            }
            const JPH::Body& body = bodyLock.GetBody();
            const bool isActive = body.IsActive();
            rb.IsAwake = isActive ? 1u : 0u;
            if (isActive) ++awakeCount;
            else ++sleepingCount;

            if (rb.MotionType == Scene::RigidBodyMotionType::Dynamic)
            {
                const JPH::RVec3 pos = body.GetPosition();
                const JPH::Quat rot = body.GetRotation();
                const Math::Vec3 morbiPos{
                    static_cast<f32>(pos.GetX()),
                    static_cast<f32>(pos.GetY()),
                    static_cast<f32>(pos.GetZ())
                };
                const Math::Quat morbiRot = FromJph(rot);
                const Math::Vec3 linVel = FromJph(body.GetLinearVelocity());
                const Math::Vec3 angVel = FromJph(body.GetAngularVelocity());

                rb.LinearVelocity = linVel;
                rb.AngularVelocity = angVel;
                shadow->LastPos = morbiPos;
                shadow->LastRot = morbiRot;
                shadow->LastLinearVel = linVel;
                shadow->LastAngularVel = angVel;

                if (auto* tr = scene.Transforms().TryGet(entity))
                {
                    ConvertWorldToLocalTransform(scene, entity, morbiPos, morbiRot, tr->Position, tr->Rotation);
                }
            }
            else if (rb.MotionType == Scene::RigidBodyMotionType::Kinematic)
            {
                if (scene.Transforms().TryGet(entity))
                {
                    Math::Vec3 worldScale{};
                    ComputeEntityWorldTransform(scene, entity, shadow->LastPos, shadow->LastRot, worldScale);
                }
                rb.LinearVelocity = FromJph(body.GetLinearVelocity());
                rb.AngularVelocity = FromJph(body.GetAngularVelocity());
                shadow->LastLinearVel = rb.LinearVelocity;
                shadow->LastAngularVel = rb.AngularVelocity;
            }
        }

        u32 triggerPairCount = 0u;
        u32 solidPairCount = 0u;
        for (const auto& ev : m_impl->Collisions)
        {
            if (ev.IsTrigger) ++triggerPairCount;
            else ++solidPairCount;
        }

        const auto endClock = std::chrono::steady_clock::now();
        const f32 simMs = std::chrono::duration<f32, std::milli>(endClock - startClock).count();

        m_impl->Stats.ActiveBodyCount = awakeCount;
        m_impl->Stats.SleepingBodyCount = sleepingCount;
        m_impl->Stats.DynamicBodyCount = dynamicCount;
        m_impl->Stats.KinematicBodyCount = kinematicCount;
        m_impl->Stats.StaticBodyCount = staticCount;
        m_impl->Stats.TotalJoltBodies = m_impl->PhysicsSystem->GetNumBodies();
        m_impl->Stats.ActiveJointCount = m_impl->ActiveJointCount;
        m_impl->Stats.CharacterCount = static_cast<u32>(m_impl->CharactersByEntity.size());
        m_impl->Stats.FixedStepsExecutedLastFrame = stepsExecuted;
        m_impl->Stats.CollisionPairsLastStep = solidPairCount;
        m_impl->Stats.TriggerPairsLastStep = triggerPairCount;
        m_impl->Stats.QueryCountLastFrame = m_impl->QueriesThisFrame;
        m_impl->Stats.SimulationTimeMs = simMs;
        m_impl->QueriesThisFrame = 0u;

        return stepsExecuted;
    }

    void PhysicsWorld::ResetSimulation(Scene::Scene& scene)
    {
        RebuildFromScene(scene);
    }

    void PhysicsWorld::RebuildFromScene(Scene::Scene& scene)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem)
        {
            return;
        }

        m_impl->DestroyAllJointsLocked();
        m_impl->DestroyAllCharactersLocked();

        std::vector<u64> packedIds;
        packedIds.reserve(m_impl->BodiesByEntity.size());
        for (const auto& [packed, _] : m_impl->BodiesByEntity)
        {
            packedIds.push_back(packed);
        }
        for (u64 packed : packedIds)
        {
            m_impl->DestroyBodyLocked(packed);
        }
        m_impl->BodiesByEntity.clear();
        m_impl->EntityByBodyId.clear();
        m_impl->ActivePairs.clear();
        m_impl->PendingExitCollisions.clear();
        m_impl->Collisions.clear();
        m_impl->ContactListener.Clear();
        m_impl->ContactListener.ClearAllSubshapeFilters();
        m_impl->ContactListener.SetGroupFilter(m_impl->GroupFilter.GetPtr());
        m_impl->Accumulator = 0.0f;

        // Hydrate Scene-authored compound collider sub-shapes into PhysicsWorld
        for (const auto& [packedOwner, entries] : scene.CompoundCollidersMap())
        {
            const Scene::EntityId owner = Scene::EntityId::Unpack(packedOwner);
            if (!scene.IsAlive(owner))
            {
                continue;
            }
            std::vector<ColliderShapeDesc> converted;
            converted.reserve(entries.size());
            for (const auto& e : entries)
            {
                converted.push_back(ColliderShapeDesc{
                    .Shape = e.Shape,
                    .HalfExtents = e.HalfExtents,
                    .LocalOffset = e.LocalOffset,
                    .LocalRotation = e.LocalRotation,
                    .CollisionLayer = e.CollisionLayer,
                    .CollisionMask = e.CollisionMask,
                    .IsTrigger = (e.IsTrigger != 0u)
                });
            }
            if (!converted.empty())
            {
                m_impl->CompoundColliders[packedOwner] = std::move(converted);
            }
        }

        auto rbSpan = scene.RigidBodies().RawComponents();
        auto rbEntities = scene.RigidBodies().RawEntities();
        for (usize i = 0; i < rbSpan.size(); ++i)
        {
            rbSpan[i].JoltBodyId = 0xFFFFFFFFu;
            rbSpan[i].LinearVelocity = Math::Vec3::Zero();
            rbSpan[i].AngularVelocity = Math::Vec3::Zero();
            rbSpan[i].DirtyFlags = 0u;
            if (const auto* tr = scene.Transforms().TryGet(rbEntities[i]))
            {
                m_impl->CreateBodyForEntityLocked(scene, rbEntities[i], rbSpan[i], *tr);
            }
        }

        if (!rbSpan.empty())
        {
            m_impl->PhysicsSystem->OptimizeBroadPhase();
        }

        auto ccSpan = scene.CharacterControllers().RawComponents();
        auto ccEntities = scene.CharacterControllers().RawEntities();
        for (usize i = 0; i < ccSpan.size(); ++i)
        {
            ccSpan[i].Velocity = Math::Vec3::Zero();
            ccSpan[i].DesiredHorizontalVel = Math::Vec3::Zero();
            ccSpan[i].PendingDisplacement = Math::Vec3::Zero();
            ccSpan[i].PendingJumpSpeed = 0.0f;
            ccSpan[i].JumpRequested = 0u;
            ccSpan[i].IsGrounded = 0u;
            ccSpan[i].IsSliding = 0u;
            ccSpan[i].DirtyFlags = 0u;
            if (const auto* tr = scene.Transforms().TryGet(ccEntities[i]))
            {
                m_impl->CreateOrRebuildCharacterLocked(scene, ccEntities[i], ccSpan[i], *tr);
            }
        }
        if (!ccSpan.empty())
        {
            m_impl->PhysicsSystem->OptimizeBroadPhase();
        }

        m_impl->SyncSceneJointsLocked(scene);
    }

    bool PhysicsWorld::DestroyBody(Scene::EntityId entity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!entity.IsValid())
        {
            return false;
        }
        const u64 packed = entity.Pack();
        const bool existed = (m_impl->BodiesByEntity.find(packed) != m_impl->BodiesByEntity.end()) ||
            (m_impl->CharactersByEntity.find(packed) != m_impl->CharactersByEntity.end());
        m_impl->DestroyBodyLocked(packed);
        m_impl->DestroyCharacterLocked(packed);
        m_impl->CompoundColliders.erase(packed);
        m_impl->ContactListener.ClearEntitySubshapeFilters(packed);
        return existed;
    }

    void PhysicsWorld::ClearAllBodies(Scene::Scene* scene)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        m_impl->DestroyAllJointsLocked();
        m_impl->DestroyAllCharactersLocked();

        std::vector<u64> packedIds;
        packedIds.reserve(m_impl->BodiesByEntity.size());
        for (const auto& [packed, _] : m_impl->BodiesByEntity)
        {
            packedIds.push_back(packed);
        }
        for (u64 packed : packedIds)
        {
            m_impl->DestroyBodyLocked(packed);
        }
        m_impl->BodiesByEntity.clear();
        m_impl->EntityByBodyId.clear();
        m_impl->CompoundColliders.clear();
        m_impl->ActivePairs.clear();
        m_impl->PendingExitCollisions.clear();
        m_impl->Collisions.clear();
        m_impl->ContactListener.Clear();
        m_impl->ContactListener.ClearAllSubshapeFilters();
        m_impl->ContactListener.SetGroupFilter(m_impl->GroupFilter.GetPtr());
        m_impl->Accumulator = 0.0f;

        if (scene)
        {
            for (auto& rb : scene->RigidBodies().RawComponents())
            {
                rb.JoltBodyId = 0xFFFFFFFFu;
            }
        }
    }

    void PhysicsWorld::SetEntityCompoundColliders(Scene::EntityId entity, std::span<const ColliderShapeDesc> colliders)
    {
        if (!entity.IsValid())
        {
            return;
        }
        std::unique_lock lock(m_impl->WorldMutex);
        const u64 packed = entity.Pack();
        if (colliders.empty())
        {
            m_impl->CompoundColliders.erase(packed);
            m_impl->ContactListener.ClearEntitySubshapeFilters(packed);
        }
        else
        {
            m_impl->CompoundColliders[packed].assign(colliders.begin(), colliders.end());
        }
        if (auto it = m_impl->BodiesByEntity.find(packed); it != m_impl->BodiesByEntity.end())
        {
            it->second.CompoundDirty = true;
        }
    }

    void PhysicsWorld::ClearEntityCompoundColliders(Scene::EntityId entity)
    {
        if (!entity.IsValid())
        {
            return;
        }
        std::unique_lock lock(m_impl->WorldMutex);
        const u64 packed = entity.Pack();
        m_impl->CompoundColliders.erase(packed);
        m_impl->ContactListener.ClearEntitySubshapeFilters(packed);
        if (auto it = m_impl->BodiesByEntity.find(packed); it != m_impl->BodiesByEntity.end())
        {
            it->second.CompoundDirty = true;
        }
    }

    void PhysicsWorld::Impl::SyncEntityCompoundCollidersLocked(const Scene::Scene& scene, Scene::EntityId entity)
    {
        if (!entity.IsValid())
        {
            return;
        }
        const u64 packed = entity.Pack();
        const auto entries = scene.GetCompoundColliders(entity);
        if (entries.empty())
        {
            CompoundColliders.erase(packed);
            ContactListener.ClearEntitySubshapeFilters(packed);
        }
        else
        {
            auto& dst = CompoundColliders[packed];
            dst.clear();
            dst.reserve(entries.size());
            for (const auto& e : entries)
            {
                dst.push_back(ColliderShapeDesc{
                    .Shape = e.Shape,
                    .HalfExtents = e.HalfExtents,
                    .LocalOffset = e.LocalOffset,
                    .LocalRotation = e.LocalRotation,
                    .CollisionLayer = e.CollisionLayer,
                    .CollisionMask = e.CollisionMask,
                    .IsTrigger = (e.IsTrigger != 0u)
                });
            }
            if (const auto* rb = scene.RigidBodies().TryGet(entity))
            {
                SyncContactSubshapeFilters(ContactListener, packed, *rb, &dst);
            }
        }
        if (auto it = BodiesByEntity.find(packed); it != BodiesByEntity.end())
        {
            it->second.CompoundDirty = true;
        }
    }

    void PhysicsWorld::SyncEntityCompoundColliders(const Scene::Scene& scene, Scene::EntityId entity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        m_impl->SyncEntityCompoundCollidersLocked(scene, entity);
    }

    bool PhysicsWorld::AddForce(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& force, ForceMode mode)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!rb || !tr || rb->MotionType != Scene::RigidBodyMotionType::Dynamic)
        {
            return false;
        }
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (!m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr))
            {
                return false;
            }
            shadow = m_impl->FindBodyLocked(entity);
        }

        JPH::BodyInterface& bi = m_impl->PhysicsSystem->GetBodyInterface();
        const JPH::BodyID bodyId = shadow->BodyId;
        switch (mode)
        {
        case ForceMode::Force:
            bi.AddForce(bodyId, ToJph(force), JPH::EActivation::Activate);
            break;
        case ForceMode::Acceleration:
        {
            const f32 mass = std::max(rb->Mass, 0.001f);
            bi.AddForce(bodyId, ToJph(force * mass), JPH::EActivation::Activate);
            break;
        }
        case ForceMode::Impulse:
            bi.AddImpulse(bodyId, ToJph(force));
            break;
        case ForceMode::VelocityChange:
        {
            const f32 mass = std::max(rb->Mass, 0.001f);
            bi.AddImpulse(bodyId, ToJph(force * mass));
            break;
        }
        }

        const Math::Vec3 vel = FromJph(bi.GetLinearVelocity(bodyId));
        rb->LinearVelocity = vel;
        shadow->LastLinearVel = vel;
        return true;
    }

    bool PhysicsWorld::AddForceAtPosition(
        Scene::Scene& scene,
        Scene::EntityId entity,
        const Math::Vec3& force,
        const Math::Vec3& position,
        ForceMode mode)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!rb || !tr || rb->MotionType != Scene::RigidBodyMotionType::Dynamic)
        {
            return false;
        }
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (!m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr))
            {
                return false;
            }
            shadow = m_impl->FindBodyLocked(entity);
        }

        JPH::BodyInterface& bi = m_impl->PhysicsSystem->GetBodyInterface();
        const JPH::BodyID bodyId = shadow->BodyId;
        const f32 mass = std::max(rb->Mass, 0.001f);

        switch (mode)
        {
        case ForceMode::Force:
            bi.AddForce(bodyId, ToJph(force), ToJphR(position), JPH::EActivation::Activate);
            break;
        case ForceMode::Acceleration:
            bi.AddForce(bodyId, ToJph(force * mass), ToJphR(position), JPH::EActivation::Activate);
            break;
        case ForceMode::Impulse:
            bi.AddImpulse(bodyId, ToJph(force), ToJphR(position));
            break;
        case ForceMode::VelocityChange:
            bi.AddImpulse(bodyId, ToJph(force * mass), ToJphR(position));
            break;
        }

        const Math::Vec3 vel = FromJph(bi.GetLinearVelocity(bodyId));
        const Math::Vec3 angVel = FromJph(bi.GetAngularVelocity(bodyId));
        rb->LinearVelocity = vel;
        rb->AngularVelocity = angVel;
        shadow->LastLinearVel = vel;
        shadow->LastAngularVel = angVel;
        return true;
    }

    bool PhysicsWorld::AddLinearImpulse(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& impulse)
    {
        return AddForce(scene, entity, impulse, ForceMode::Impulse);
    }

    bool PhysicsWorld::AddTorque(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& torque, ForceMode mode)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!rb || !tr || rb->MotionType != Scene::RigidBodyMotionType::Dynamic)
        {
            return false;
        }
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (!m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr))
            {
                return false;
            }
            shadow = m_impl->FindBodyLocked(entity);
        }

        JPH::BodyInterface& bi = m_impl->PhysicsSystem->GetBodyInterface();
        const JPH::BodyID bodyId = shadow->BodyId;
        switch (mode)
        {
        case ForceMode::Force:
            bi.AddTorque(bodyId, ToJph(torque), JPH::EActivation::Activate);
            break;
        case ForceMode::Impulse:
            bi.AddAngularImpulse(bodyId, ToJph(torque));
            break;
        case ForceMode::VelocityChange:
        {
            const JPH::Vec3 curW = bi.GetAngularVelocity(bodyId);
            bi.SetAngularVelocity(bodyId, curW + ToJph(torque));
            bi.ActivateBody(bodyId);
            break;
        }
        case ForceMode::Acceleration:
        {
            JPH::Vec3 scaledTorque = ToJph(torque);
            {
                JPH::BodyLockRead bodyLock(m_impl->PhysicsSystem->GetBodyLockInterface(), bodyId);
                if (bodyLock.Succeeded())
                {
                    const JPH::Body& b = bodyLock.GetBody();
                    if (const auto* mp = b.GetMotionProperties())
                    {
                        const JPH::Vec3 invDiag = mp->GetInverseInertiaDiagonal();
                        const JPH::Quat inertiaRot = b.GetRotation() * mp->GetInertiaRotation();
                        const JPH::Vec3 localAlpha = inertiaRot.Conjugated() * ToJph(torque);
                        const JPH::Vec3 localTau(
                            invDiag.GetX() > 1e-6f ? (localAlpha.GetX() / invDiag.GetX()) : 0.0f,
                            invDiag.GetY() > 1e-6f ? (localAlpha.GetY() / invDiag.GetY()) : 0.0f,
                            invDiag.GetZ() > 1e-6f ? (localAlpha.GetZ() / invDiag.GetZ()) : 0.0f);
                        scaledTorque = inertiaRot * localTau;
                    }
                }
            }
            bi.AddTorque(bodyId, scaledTorque, JPH::EActivation::Activate);
            break;
        }
        }

        const Math::Vec3 angVel = FromJph(bi.GetAngularVelocity(bodyId));
        rb->AngularVelocity = angVel;
        shadow->LastAngularVel = angVel;
        return true;
    }

    bool PhysicsWorld::SetLinearVelocity(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& velocity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!rb || !tr || rb->MotionType == Scene::RigidBodyMotionType::Static)
        {
            return false;
        }
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (!m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr))
            {
                return false;
            }
            shadow = m_impl->FindBodyLocked(entity);
        }

        m_impl->PhysicsSystem->GetBodyInterface().SetLinearVelocity(shadow->BodyId, ToJph(velocity));
        rb->LinearVelocity = velocity;
        shadow->LastLinearVel = velocity;
        return true;
    }

    bool PhysicsWorld::GetLinearVelocity(const Scene::Scene& scene, Scene::EntityId entity, Math::Vec3& outVelocity) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        const auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (const auto* rb = scene.RigidBodies().TryGet(entity))
            {
                outVelocity = rb->LinearVelocity;
                return true;
            }
            return false;
        }
        outVelocity = FromJph(m_impl->PhysicsSystem->GetBodyInterface().GetLinearVelocity(shadow->BodyId));
        return true;
    }

    bool PhysicsWorld::SetAngularVelocity(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& angularVelocity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!rb || !tr || rb->MotionType == Scene::RigidBodyMotionType::Static)
        {
            return false;
        }
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (!m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr))
            {
                return false;
            }
            shadow = m_impl->FindBodyLocked(entity);
        }

        m_impl->PhysicsSystem->GetBodyInterface().SetAngularVelocity(shadow->BodyId, ToJph(angularVelocity));
        rb->AngularVelocity = angularVelocity;
        shadow->LastAngularVel = angularVelocity;
        return true;
    }

    bool PhysicsWorld::GetAngularVelocity(const Scene::Scene& scene, Scene::EntityId entity, Math::Vec3& outAngularVelocity) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        const auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            if (const auto* rb = scene.RigidBodies().TryGet(entity))
            {
                outAngularVelocity = rb->AngularVelocity;
                return true;
            }
            return false;
        }
        outAngularVelocity = FromJph(m_impl->PhysicsSystem->GetBodyInterface().GetAngularVelocity(shadow->BodyId));
        return true;
    }

    bool PhysicsWorld::WakeUp(Scene::Scene& scene, Scene::EntityId entity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!rb || !shadow)
        {
            return false;
        }
        m_impl->PhysicsSystem->GetBodyInterface().ActivateBody(shadow->BodyId);
        rb->IsAwake = 1u;
        return true;
    }

    bool PhysicsWorld::Sleep(Scene::Scene& scene, Scene::EntityId entity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!rb || !shadow)
        {
            return false;
        }
        m_impl->PhysicsSystem->GetBodyInterface().DeactivateBody(shadow->BodyId);
        rb->IsAwake = 0u;
        return true;
    }

    bool PhysicsWorld::IsSleeping(const Scene::Scene& scene, Scene::EntityId entity) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        const auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            return false;
        }
        return !m_impl->PhysicsSystem->GetBodyInterface().IsActive(shadow->BodyId);
    }

    bool PhysicsWorld::TeleportBody(
        Scene::Scene& scene,
        Scene::EntityId entity,
        const Math::Vec3& position,
        const Math::Quat& rotation)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* tr = scene.Transforms().TryGet(entity);
        auto* rb = scene.RigidBodies().TryGet(entity);
        if (!tr)
        {
            return false;
        }
        ConvertWorldToLocalTransform(scene, entity, position, rotation, tr->Position, tr->Rotation);

        if (!rb)
        {
            return true;
        }

        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow)
        {
            return m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr);
        }

        const JPH::EActivation act = (rb->MotionType != Scene::RigidBodyMotionType::Static)
            ? JPH::EActivation::Activate
            : JPH::EActivation::DontActivate;
        m_impl->PhysicsSystem->GetBodyInterface().SetPositionAndRotation(
            shadow->BodyId,
            ToJphR(position),
            ToJph(rotation),
            act);
        shadow->LastPos = position;
        shadow->LastRot = rotation;
        return true;
    }

    bool PhysicsWorld::SyncEntityBody(Scene::Scene& scene, Scene::EntityId entity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* rb = scene.RigidBodies().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!rb || !tr)
        {
            return false;
        }
        if (scene.CompoundCollidersMap().find(entity.Pack()) != scene.CompoundCollidersMap().end())
        {
            m_impl->SyncEntityCompoundCollidersLocked(scene, entity);
        }
        auto* shadow = m_impl->FindBodyLocked(entity);
        if (!shadow || rb->JoltBodyId != shadow->BodyId.GetIndexAndSequenceNumber())
        {
            return m_impl->CreateBodyForEntityLocked(scene, entity, *rb, *tr);
        }
        m_impl->SyncBodyChangesLocked(scene, entity, *rb, *tr, *shadow);
        return true;
    }

    u32 PhysicsWorld::GetBodyCount() const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return m_impl->PhysicsSystem ? m_impl->PhysicsSystem->GetNumBodies() : 0u;
    }

    PhysicsStats PhysicsWorld::GetStats() const noexcept
    {
        std::shared_lock lock(m_impl->WorldMutex);
        PhysicsStats out = m_impl->Stats;
        out.CharacterCount = static_cast<u32>(m_impl->CharactersByEntity.size());
        out.ActiveJointCount = m_impl->ActiveJointCount;
        out.TotalJoltBodies = m_impl->PhysicsSystem ? m_impl->PhysicsSystem->GetNumBodies() : 0u;
        return out;
    }

    std::vector<CollisionPairEvent> PhysicsWorld::GetRecentCollisions() const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        return m_impl->Collisions;
    }

    void PhysicsWorld::CopyRecentCollisions(std::vector<CollisionPairEvent>& outCollisions) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        outCollisions.assign(m_impl->Collisions.begin(), m_impl->Collisions.end());
    }
}
