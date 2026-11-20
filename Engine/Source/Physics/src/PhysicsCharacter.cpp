#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

namespace Morbi::Physics
{
    using namespace Detail;

    namespace
    {
        class CharacterBodyFilter final : public JPH::BodyFilter
        {
        public:
            CharacterBodyFilter(
                JPH::BodyID selfInnerBodyId,
                const BitmaskGroupFilter* groupFilter,
                u32 collisionLayer,
                u32 collisionMask) noexcept
                : m_selfInnerBodyId(selfInnerBodyId)
                , m_groupFilter(groupFilter)
                , m_collisionLayer(collisionLayer)
                , m_collisionMask(collisionMask)
            {
            }

            bool ShouldCollide(const JPH::BodyID& inBodyID) const override
            {
                return inBodyID != m_selfInnerBodyId;
            }

            bool ShouldCollideLocked(const JPH::Body& inBody) const override
            {
                if (inBody.GetID() == m_selfInnerBodyId || inBody.IsSensor())
                {
                    return false;
                }
                if (!m_groupFilter)
                {
                    return true;
                }
                const JPH::CollisionGroup selfGroup(m_groupFilter, m_collisionLayer, m_collisionMask);
                return m_groupFilter->CanCollide(selfGroup, inBody.GetCollisionGroup());
            }

        private:
            JPH::BodyID m_selfInnerBodyId{};
            const BitmaskGroupFilter* m_groupFilter = nullptr;
            u32 m_collisionLayer = 1u;
            u32 m_collisionMask = 0xFFFFFFFFu;
        };

        [[nodiscard]] JPH::RefConst<JPH::Shape> BuildCharacterCapsuleShape(
            f32 radius,
            f32 height,
            const Math::Vec3& centerOffset)
        {
            const f32 safeRadius = std::max(0.05f, radius);
            const f32 safeHalfHeight = std::max(0.01f, (height - 2.0f * safeRadius) * 0.5f);

            JPH::Ref<JPH::CapsuleShapeSettings> capsuleSettings =
                new JPH::CapsuleShapeSettings(safeHalfHeight, safeRadius);
            const Math::Vec3 feetOffset{
                centerOffset.x,
                centerOffset.y + safeHalfHeight + safeRadius,
                centerOffset.z
            };
            JPH::Ref<JPH::RotatedTranslatedShapeSettings> rtSettings =
                new JPH::RotatedTranslatedShapeSettings(
                    ToJph(feetOffset),
                    JPH::Quat::sIdentity(),
                    capsuleSettings.GetPtr());

            JPH::Shape::ShapeResult result = rtSettings->Create();
            if (result.HasError())
            {
                Core::Log::Error("Physics", std::string("CharacterVirtual capsule creation failed: ") + result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }

        [[nodiscard]] CharacterGroundState FromJphGroundState(JPH::CharacterBase::EGroundState gs) noexcept
        {
            switch (gs)
            {
            case JPH::CharacterBase::EGroundState::OnGround:      return CharacterGroundState::OnGround;
            case JPH::CharacterBase::EGroundState::OnSteepGround: return CharacterGroundState::OnSteepGround;
            case JPH::CharacterBase::EGroundState::NotSupported:  return CharacterGroundState::NotSupported;
            case JPH::CharacterBase::EGroundState::InAir:
            default:                                              return CharacterGroundState::InAir;
            }
        }
    }

    void PhysicsWorld::Impl::DestroyCharacterLocked(u64 packedEntity)
    {
        auto it = CharactersByEntity.find(packedEntity);
        if (it == CharactersByEntity.end())
        {
            return;
        }
        if (it->second.Character)
        {
            it->second.Character->SetListener(nullptr);
            it->second.Character->SetCharacterVsCharacterCollision(nullptr);
            CharacterVsCharacterCollision.Remove(it->second.Character.GetPtr());
        }
        if (!it->second.InnerBodyId.IsInvalid())
        {
            EntityByBodyId.erase(it->second.InnerBodyId.GetIndexAndSequenceNumber());
        }
        CharactersByEntity.erase(it);
    }

    void PhysicsWorld::Impl::DestroyAllCharactersLocked()
    {
        for (auto& [packed, shadow] : CharactersByEntity)
        {
            (void)packed;
            if (shadow.Character)
            {
                shadow.Character->SetListener(nullptr);
                shadow.Character->SetCharacterVsCharacterCollision(nullptr);
                CharacterVsCharacterCollision.Remove(shadow.Character.GetPtr());
            }
            if (!shadow.InnerBodyId.IsInvalid())
            {
                EntityByBodyId.erase(shadow.InnerBodyId.GetIndexAndSequenceNumber());
            }
        }
        CharactersByEntity.clear();
    }

    bool PhysicsWorld::Impl::CreateOrRebuildCharacterLocked(
        Scene::Scene& scene,
        Scene::EntityId entity,
        Scene::CharacterControllerComponent& cc,
        const Scene::TransformComponent& tr)
    {
        if (!PhysicsSystem || !entity.IsValid() || !scene.IsAlive(entity))
        {
            return false;
        }

        const u64 packed = entity.Pack();
        DestroyCharacterLocked(packed);

        const f32 safeRadius = std::max(0.05f, cc.Radius);
        const f32 safeHeight = std::max(cc.Height, safeRadius * 2.0f + 0.02f);
        JPH::RefConst<JPH::Shape> shape = BuildCharacterCapsuleShape(safeRadius, safeHeight, cc.CenterOffset);
        if (!shape)
        {
            return false;
        }

        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mUp = JPH::Vec3::sAxisY();
        settings->mShape = shape;
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -(cc.CenterOffset.y + safeRadius));
        settings->mMaxSlopeAngle = JPH::DegreesToRadians(std::clamp(cc.MaxSlopeAngleDegrees, 1.0f, 89.0f));
        settings->mMass = std::max(0.1f, cc.Mass);
        settings->mMaxStrength = std::max(0.0f, cc.MaxPushStrength);
        settings->mEnhancedInternalEdgeRemoval = true;
        settings->mInnerBodyShape = shape;
        settings->mInnerBodyLayer = ObjectLayers::kMoving;

        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            settings.GetPtr(),
            ToJphR(tr.Position),
            ToJph(tr.Rotation),
            packed,
            PhysicsSystem.get());

        character->SetLinearVelocity(ToJph(cc.Velocity));
        character->SetCharacterVsCharacterCollision(&CharacterVsCharacterCollision);
        CharacterVsCharacterCollision.Add(character.GetPtr());

        const JPH::BodyID innerId = character->GetInnerBodyID();
        if (!innerId.IsInvalid())
        {
            JPH::BodyInterface& bi = PhysicsSystem->GetBodyInterface();
            bi.SetUserData(innerId, packed);
            JPH::BodyLockWrite innerLock(PhysicsSystem->GetBodyLockInterface(), innerId);
            if (innerLock.Succeeded())
            {
                innerLock.GetBody().SetCollisionGroup(JPH::CollisionGroup(GroupFilter.GetPtr(), cc.CollisionLayer, cc.CollisionMask));
            }
            EntityByBodyId[innerId.GetIndexAndSequenceNumber()] = entity;
        }

        auto listener = std::make_unique<MorbiCharacterContactListener>(
            PhysicsSystem.get(),
            GroupFilter.GetPtr(),
            cc.CollisionLayer,
            cc.CollisionMask,
            innerId);
        character->SetListener(listener.get());

        cc.Radius = safeRadius;
        cc.Height = safeHeight;
        cc.DirtyFlags = 0u;

        CharacterShadowState shadow{};
        shadow.Character = character;
        shadow.Listener = std::move(listener);
        shadow.Entity = entity;
        shadow.InnerBodyId = innerId;
        shadow.LastPos = tr.Position;
        shadow.CenterOffset = cc.CenterOffset;
        shadow.Radius = safeRadius;
        shadow.Height = safeHeight;
        shadow.Mass = settings->mMass;
        shadow.MaxPushStrength = settings->mMaxStrength;
        shadow.MaxSlopeAngleDegrees = cc.MaxSlopeAngleDegrees;
        shadow.CollisionLayer = cc.CollisionLayer;
        shadow.CollisionMask = cc.CollisionMask;

        CharactersByEntity[packed] = std::move(shadow);
        return true;
    }

    void PhysicsWorld::Impl::SyncAllCharactersPreStepLocked(Scene::Scene& scene)
    {
        auto ccSpan = scene.CharacterControllers().RawComponents();
        auto ccEntities = scene.CharacterControllers().RawEntities();
        for (usize i = 0; i < ccSpan.size(); ++i)
        {
            Scene::CharacterControllerComponent& cc = ccSpan[i];
            const Scene::EntityId entity = ccEntities[i];
            const auto* tr = scene.Transforms().TryGet(entity);
            if (!tr || !scene.IsAlive(entity))
            {
                continue;
            }

            auto it = CharactersByEntity.find(entity.Pack());
            if (it == CharactersByEntity.end() || !it->second.Character)
            {
                CreateOrRebuildCharacterLocked(scene, entity, cc, *tr);
                continue;
            }

            CharacterShadowState& shadow = it->second;
            const bool shapeChanged = (cc.DirtyFlags & static_cast<u8>(Scene::CharacterControllerDirtyFlags::Shape)) != 0u ||
                std::abs(cc.Radius - shadow.Radius) > 1e-4f ||
                std::abs(cc.Height - shadow.Height) > 1e-4f ||
                !VecNearEqual(cc.CenterOffset, shadow.CenterOffset, 1e-4f);

            if (shapeChanged)
            {
                CreateOrRebuildCharacterLocked(scene, entity, cc, *tr);
                continue;
            }

            JPH::CharacterVirtual* ch = shadow.Character.GetPtr();
            const bool transformDirty = (cc.DirtyFlags & static_cast<u8>(Scene::CharacterControllerDirtyFlags::Transform)) != 0u ||
                !VecNearEqual(tr->Position, shadow.LastPos, 1e-3f);
            if (transformDirty)
            {
                ch->SetPosition(ToJphR(tr->Position));
                ch->SetRotation(ToJph(tr->Rotation));
                shadow.LastPos = tr->Position;
            }

            if (std::abs(cc.Mass - shadow.Mass) > 1e-4f)
            {
                shadow.Mass = std::max(0.1f, cc.Mass);
                ch->SetMass(shadow.Mass);
            }
            if (std::abs(cc.MaxPushStrength - shadow.MaxPushStrength) > 1e-4f)
            {
                shadow.MaxPushStrength = std::max(0.0f, cc.MaxPushStrength);
                ch->SetMaxStrength(shadow.MaxPushStrength);
            }
            if (std::abs(cc.MaxSlopeAngleDegrees - shadow.MaxSlopeAngleDegrees) > 1e-4f)
            {
                shadow.MaxSlopeAngleDegrees = std::clamp(cc.MaxSlopeAngleDegrees, 1.0f, 89.0f);
                ch->SetMaxSlopeAngle(JPH::DegreesToRadians(shadow.MaxSlopeAngleDegrees));
            }
            if (cc.CollisionLayer != shadow.CollisionLayer || cc.CollisionMask != shadow.CollisionMask)
            {
                shadow.CollisionLayer = cc.CollisionLayer;
                shadow.CollisionMask = cc.CollisionMask;
                if (!shadow.InnerBodyId.IsInvalid())
                {
                    JPH::BodyLockWrite innerLock(PhysicsSystem->GetBodyLockInterface(), shadow.InnerBodyId);
                    if (innerLock.Succeeded())
                    {
                        innerLock.GetBody().SetCollisionGroup(
                            JPH::CollisionGroup(GroupFilter.GetPtr(), cc.CollisionLayer, cc.CollisionMask));
                    }
                }
                if (shadow.Listener)
                {
                    shadow.Listener->UpdateFilter(cc.CollisionLayer, cc.CollisionMask, shadow.InnerBodyId);
                }
            }
            cc.DirtyFlags = 0u;
        }
    }

    void PhysicsWorld::Impl::StepAllCharactersSubstepLocked(Scene::Scene& scene, f32 fixedDt, u32 remainingSubsteps)
    {
        auto ccSpan = scene.CharacterControllers().RawComponents();
        auto ccEntities = scene.CharacterControllers().RawEntities();
        if (ccSpan.empty() || !PhysicsSystem || !TempAllocator)
        {
            return;
        }

        const auto& bpFilter = PhysicsSystem->GetDefaultBroadPhaseLayerFilter(ObjectLayers::kMoving);
        const auto& objFilter = PhysicsSystem->GetDefaultLayerFilter(ObjectLayers::kMoving);
        const u32 safeRemaining = std::max(1u, remainingSubsteps);

        for (usize i = 0; i < ccSpan.size(); ++i)
        {
            Scene::CharacterControllerComponent& cc = ccSpan[i];
            const Scene::EntityId entity = ccEntities[i];
            auto* tr = scene.Transforms().TryGet(entity);
            if (!tr || !scene.IsAlive(entity))
            {
                continue;
            }

            auto it = CharactersByEntity.find(entity.Pack());
            if (it == CharactersByEntity.end() || !it->second.Character)
            {
                if (!CreateOrRebuildCharacterLocked(scene, entity, cc, *tr))
                {
                    continue;
                }
                it = CharactersByEntity.find(entity.Pack());
            }

            CharacterShadowState& shadow = it->second;
            JPH::CharacterVirtual* ch = shadow.Character.GetPtr();

            ch->UpdateGroundVelocity();

            const JPH::CharacterBase::EGroundState groundState = ch->GetGroundState();
            const JPH::Vec3 groundVel = ch->GetGroundVelocity();
            const JPH::Vec3 curVel = ch->GetLinearVelocity();
            const f32 gravY = Gravity.y * cc.GravityFactor;

            JPH::Vec3 targetVel = JPH::Vec3::sZero();
            const bool movingUp = (curVel.GetY() - groundVel.GetY()) > 0.15f;
            if (groundState == JPH::CharacterBase::EGroundState::OnGround && !movingUp)
            {
                targetVel = groundVel;
            }
            else
            {
                targetVel = JPH::Vec3(curVel.GetX(), curVel.GetY() + gravY * fixedDt, curVel.GetZ());
            }

            const bool hasPendingDisp =
                std::abs(cc.PendingDisplacement.x) > 1e-6f ||
                std::abs(cc.PendingDisplacement.y) > 1e-6f ||
                std::abs(cc.PendingDisplacement.z) > 1e-6f;

            if (hasPendingDisp)
            {
                const Math::Vec3 stepDisp = cc.PendingDisplacement * (1.0f / static_cast<f32>(safeRemaining));
                cc.PendingDisplacement = cc.PendingDisplacement - stepDisp;
                const Math::Vec3 dispVel = stepDisp * (1.0f / fixedDt);
                targetVel.SetX(groundVel.GetX() + dispVel.x);
                targetVel.SetZ(groundVel.GetZ() + dispVel.z);
                if (std::abs(dispVel.y) > 1e-5f)
                {
                    targetVel.SetY(dispVel.y);
                }
            }
            else
            {
                const f32 control = (groundState == JPH::CharacterBase::EGroundState::OnGround)
                    ? 1.0f
                    : std::clamp(cc.AirControl, 0.0f, 1.0f);
                const f32 desiredX = groundVel.GetX() + cc.DesiredHorizontalVel.x;
                const f32 desiredZ = groundVel.GetZ() + cc.DesiredHorizontalVel.z;
                targetVel.SetX(curVel.GetX() + (desiredX - curVel.GetX()) * control);
                targetVel.SetZ(curVel.GetZ() + (desiredZ - curVel.GetZ()) * control);
            }

            bool jumpTriggered = false;
            if (cc.JumpRequested != 0u && cc.PendingJumpSpeed > 0.0f)
            {
                if (groundState == JPH::CharacterBase::EGroundState::OnGround || ch->IsSupported())
                {
                    targetVel.SetY(groundVel.GetY() + cc.PendingJumpSpeed);
                    jumpTriggered = true;
                }
                cc.JumpRequested = 0u;
                cc.PendingJumpSpeed = 0.0f;
            }

            if (cc.GravityFactor != 0.0f && groundState == JPH::CharacterBase::EGroundState::OnGround && !jumpTriggered && !movingUp)
            {
                // Maintain mild downward ground contact on slopes without overriding upward jumps
                targetVel.SetY(targetVel.GetY() + gravY * fixedDt);
            }

            ch->SetLinearVelocity(targetVel);

            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            const bool ascending = jumpTriggered || (targetVel.GetY() > groundVel.GetY() + 0.05f);
            updateSettings.mStickToFloorStepDown = ascending
                ? JPH::Vec3::sZero()
                : JPH::Vec3(0.0f, -std::max(0.0f, cc.StepHeight), 0.0f);
            updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(0.0f, cc.StepHeight), 0.0f);

            const CharacterBodyFilter bodyFilter(shadow.InnerBodyId, GroupFilter.GetPtr(), cc.CollisionLayer, cc.CollisionMask);
            ch->ExtendedUpdate(
                fixedDt,
                JPH::Vec3(0.0f, gravY, 0.0f),
                updateSettings,
                bpFilter,
                objFilter,
                bodyFilter,
                JPH::ShapeFilter(),
                *TempAllocator);

            const JPH::RVec3 newPos = ch->GetPosition();
            const Math::Vec3 morbiPos{
                static_cast<f32>(newPos.GetX()),
                static_cast<f32>(newPos.GetY()),
                static_cast<f32>(newPos.GetZ())
            };
            tr->Position = morbiPos;
            shadow.LastPos = morbiPos;
            cc.Velocity = FromJph(ch->GetLinearVelocity());

            const auto newGround = ch->GetGroundState();
            cc.IsGrounded = (newGround == JPH::CharacterBase::EGroundState::OnGround) ? 1u : 0u;
            cc.IsSliding = (newGround == JPH::CharacterBase::EGroundState::OnSteepGround) ? 1u : 0u;
            cc.GroundNormal = ch->IsSupported() ? FromJph(ch->GetGroundNormal()) : Math::Vec3::Up();
        }
    }

    bool PhysicsWorld::SyncCharacterController(Scene::Scene& scene, Scene::EntityId entity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* cc = scene.CharacterControllers().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!cc || !tr)
        {
            return false;
        }
        const bool ok = m_impl->CreateOrRebuildCharacterLocked(scene, entity, *cc, *tr);
        if (ok)
        {
            m_impl->PhysicsSystem->OptimizeBroadPhase();
        }
        return ok;
    }

    bool PhysicsWorld::DestroyCharacter(Scene::EntityId entity)
    {
        if (!entity.IsValid())
        {
            return false;
        }
        std::unique_lock lock(m_impl->WorldMutex);
        const u64 packed = entity.Pack();
        const bool existed = (m_impl->CharactersByEntity.find(packed) != m_impl->CharactersByEntity.end());
        m_impl->DestroyCharacterLocked(packed);
        return existed;
    }

    bool PhysicsWorld::MoveCharacter(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& displacement)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* cc = scene.CharacterControllers().TryGet(entity);
        if (!cc)
        {
            return false;
        }
        cc->PendingDisplacement = cc->PendingDisplacement + displacement;
        return true;
    }

    bool PhysicsWorld::JumpCharacter(Scene::Scene& scene, Scene::EntityId entity, f32 jumpSpeed)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !scene.IsAlive(entity) || jumpSpeed <= 0.0f)
        {
            return false;
        }
        auto* cc = scene.CharacterControllers().TryGet(entity);
        if (!cc)
        {
            return false;
        }
        cc->PendingJumpSpeed = jumpSpeed;
        cc->JumpRequested = 1u;
        return true;
    }

    bool PhysicsWorld::SetCharacterVelocity(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& velocity)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* cc = scene.CharacterControllers().TryGet(entity);
        const auto* tr = scene.Transforms().TryGet(entity);
        if (!cc || !tr)
        {
            return false;
        }
        cc->Velocity = velocity;
        cc->DesiredHorizontalVel = { velocity.x, 0.0f, velocity.z };
        if (auto it = m_impl->CharactersByEntity.find(entity.Pack()); it != m_impl->CharactersByEntity.end() && it->second.Character)
        {
            it->second.Character->SetLinearVelocity(ToJph(velocity));
        }
        else if (m_impl->CreateOrRebuildCharacterLocked(scene, entity, *cc, *tr))
        {
            m_impl->PhysicsSystem->OptimizeBroadPhase();
        }
        return true;
    }

    bool PhysicsWorld::GetCharacterVelocity(const Scene::Scene& scene, Scene::EntityId entity, Math::Vec3& outVelocity) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !scene.IsAlive(entity))
        {
            return false;
        }
        if (auto it = m_impl->CharactersByEntity.find(entity.Pack()); it != m_impl->CharactersByEntity.end() && it->second.Character)
        {
            outVelocity = FromJph(it->second.Character->GetLinearVelocity());
            return true;
        }
        if (const auto* cc = scene.CharacterControllers().TryGet(entity))
        {
            outVelocity = cc->Velocity;
            return true;
        }
        return false;
    }

    bool PhysicsWorld::GetCharacterState(const Scene::Scene& scene, Scene::EntityId entity, CharacterControllerState& outState) const
    {
        std::shared_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !scene.IsAlive(entity))
        {
            return false;
        }
        if (auto it = m_impl->CharactersByEntity.find(entity.Pack()); it != m_impl->CharactersByEntity.end() && it->second.Character)
        {
            const JPH::CharacterVirtual* ch = it->second.Character.GetPtr();
            outState.GroundState = FromJphGroundState(ch->GetGroundState());
            outState.IsGrounded = (outState.GroundState == CharacterGroundState::OnGround);
            outState.IsSliding = (outState.GroundState == CharacterGroundState::OnSteepGround);
            outState.Velocity = FromJph(ch->GetLinearVelocity());
            outState.GroundNormal = ch->IsSupported() ? FromJph(ch->GetGroundNormal()) : Math::Vec3::Up();
            outState.GroundVelocity = FromJph(ch->GetGroundVelocity());
            const JPH::BodyID gBody = ch->GetGroundBodyID();
            if (!gBody.IsInvalid())
            {
                if (auto ebIt = m_impl->EntityByBodyId.find(gBody.GetIndexAndSequenceNumber()); ebIt != m_impl->EntityByBodyId.end())
                {
                    outState.GroundEntity = ebIt->second;
                }
            }
            return true;
        }
        if (const auto* cc = scene.CharacterControllers().TryGet(entity))
        {
            outState = CharacterControllerState{};
            outState.IsGrounded = (cc->IsGrounded != 0u);
            outState.IsSliding = (cc->IsSliding != 0u);
            outState.GroundState = outState.IsGrounded
                ? CharacterGroundState::OnGround
                : (outState.IsSliding ? CharacterGroundState::OnSteepGround : CharacterGroundState::InAir);
            outState.Velocity = cc->Velocity;
            outState.GroundNormal = cc->GroundNormal;
            return true;
        }
        return false;
    }

    bool PhysicsWorld::TeleportCharacter(Scene::Scene& scene, Scene::EntityId entity, const Math::Vec3& position)
    {
        std::unique_lock lock(m_impl->WorldMutex);
        if (!m_impl->Initialized || !m_impl->PhysicsSystem || !scene.IsAlive(entity))
        {
            return false;
        }
        auto* cc = scene.CharacterControllers().TryGet(entity);
        auto* tr = scene.Transforms().TryGet(entity);
        if (!cc || !tr)
        {
            return false;
        }

        tr->Position = position;
        cc->PendingDisplacement = Math::Vec3::Zero();
        cc->DirtyFlags &= static_cast<u8>(~static_cast<u8>(Scene::CharacterControllerDirtyFlags::Transform));

        auto it = m_impl->CharactersByEntity.find(entity.Pack());
        if (it == m_impl->CharactersByEntity.end() || !it->second.Character)
        {
            return m_impl->CreateOrRebuildCharacterLocked(scene, entity, *cc, *tr);
        }

        JPH::CharacterVirtual* ch = it->second.Character.GetPtr();
        ch->SetPosition(ToJphR(position));
        it->second.LastPos = position;

        const CharacterBodyFilter bodyFilter(it->second.InnerBodyId, m_impl->GroupFilter.GetPtr(), cc->CollisionLayer, cc->CollisionMask);
        ch->RefreshContacts(
            m_impl->PhysicsSystem->GetDefaultBroadPhaseLayerFilter(ObjectLayers::kMoving),
            m_impl->PhysicsSystem->GetDefaultLayerFilter(ObjectLayers::kMoving),
            bodyFilter,
            JPH::ShapeFilter(),
            *m_impl->TempAllocator);
        return true;
    }
}
