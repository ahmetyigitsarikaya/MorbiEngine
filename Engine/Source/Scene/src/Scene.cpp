#include "Morbi/Scene/Scene.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

#include "Morbi/Core/Log.hpp"

namespace Morbi::Scene
{
    Scene::Scene(std::string name)
        : m_name(std::move(name))
    {
        m_transforms.Reserve(1024);
        m_hierarchies.Reserve(1024);
        m_meshRenderers.Reserve(512);
        m_lights.Reserve(64);
        m_scripts.Reserve(512);
    }

    EntityId Scene::CreateEntity(std::string_view name)
    {
        u32 idx = 0;
        if (!m_freeIndices.empty())
        {
            idx = m_freeIndices.back();
            m_freeIndices.pop_back();
            m_entityRecords[idx].Name = std::string(name);
            m_entityRecords[idx].Alive = true;
        }
        else
        {
            idx = static_cast<u32>(m_entityRecords.size());
            m_entityRecords.push_back(EntityRecord{
                .Name = std::string(name),
                .Generation = 1,
                .Alive = true
            });
        }

        ++m_aliveCount;
        const EntityId entity{ idx, m_entityRecords[idx].Generation };
        m_transforms.EmplaceOrReplace(entity, TransformComponent{});
        m_hierarchies.EmplaceOrReplace(entity, HierarchyComponent{});
        return entity;
    }

    void Scene::DestroyEntity(EntityId entity)
    {
        if (!IsAlive(entity))
        {
            return;
        }

        // Destroy children recursively
        const std::vector<EntityId> children = GetChildren(entity);
        for (EntityId child : children)
        {
            DestroyEntity(child);
        }

        // Unlink from parent if attached
        SetParent(entity, EntityId::Invalid());

        if (m_entityDestroyedCallback)
        {
            m_entityDestroyedCallback(entity);
        }

        m_transforms.Remove(entity);
        m_hierarchies.Remove(entity);
        m_meshRenderers.Remove(entity);
        m_lights.Remove(entity);
        m_scripts.Remove(entity);
        m_rigidBodies.Remove(entity);
        m_audioSources.Remove(entity);
        m_cameras.Remove(entity);
        m_animators.Remove(entity);
        m_characterControllers.Remove(entity);
        m_networkIdentities.Remove(entity);

        const u64 packedDestroyed = entity.Pack();
        m_compoundColliders.erase(packedDestroyed);
        m_physicsJoints.erase(packedDestroyed);
        for (auto it = m_physicsJoints.begin(); it != m_physicsJoints.end();)
        {
            auto& vec = it->second;
            std::erase_if(vec, [entity](const PhysicsJointEntry& j) { return j.ConnectedEntity == entity; });
            if (vec.empty())
            {
                it = m_physicsJoints.erase(it);
            }
            else
            {
                ++it;
            }
        }

        auto& rec = m_entityRecords[entity.Index];
        rec.Alive = false;
        ++rec.Generation;
        m_freeIndices.push_back(entity.Index);
        if (m_aliveCount > 0)
        {
            --m_aliveCount;
        }
    }

    bool Scene::IsAlive(EntityId entity) const noexcept
    {
        return entity.IsValid() &&
               entity.Index < m_entityRecords.size() &&
               m_entityRecords[entity.Index].Alive &&
               m_entityRecords[entity.Index].Generation == entity.Generation;
    }

    std::string_view Scene::GetEntityName(EntityId entity) const noexcept
    {
        if (!IsAlive(entity))
        {
            return {};
        }
        return m_entityRecords[entity.Index].Name;
    }

    void Scene::SetEntityName(EntityId entity, std::string_view name)
    {
        if (IsAlive(entity))
        {
            m_entityRecords[entity.Index].Name = std::string(name);
        }
    }

    EntityId Scene::FindEntityByName(std::string_view name) const noexcept
    {
        for (u32 idx = 0; idx < static_cast<u32>(m_entityRecords.size()); ++idx)
        {
            const auto& rec = m_entityRecords[idx];
            if (rec.Alive && rec.Name == name)
            {
                return EntityId{ idx, rec.Generation };
            }
        }
        return EntityId::Invalid();
    }

    void Scene::SetParent(EntityId child, EntityId newParent)
    {
        if (!IsAlive(child) || child == newParent)
        {
            return;
        }

        auto* childHier = m_hierarchies.TryGet(child);
        if (!childHier)
        {
            childHier = &m_hierarchies.EmplaceOrReplace(child, HierarchyComponent{});
        }

        // 1. Detach from old parent's doubly-linked child list
        if (childHier->Parent.IsValid() && IsAlive(childHier->Parent))
        {
            if (auto* oldParentHier = m_hierarchies.TryGet(childHier->Parent))
            {
                if (oldParentHier->FirstChild == child)
                {
                    oldParentHier->FirstChild = childHier->NextSibling;
                }
            }
        }
        if (childHier->PrevSibling.IsValid())
        {
            if (auto* prevHier = m_hierarchies.TryGet(childHier->PrevSibling))
            {
                prevHier->NextSibling = childHier->NextSibling;
            }
        }
        if (childHier->NextSibling.IsValid())
        {
            if (auto* nextHier = m_hierarchies.TryGet(childHier->NextSibling))
            {
                nextHier->PrevSibling = childHier->PrevSibling;
            }
        }

        childHier->Parent = EntityId::Invalid();
        childHier->PrevSibling = EntityId::Invalid();
        childHier->NextSibling = EntityId::Invalid();

        // 2. Attach to new parent
        if (newParent.IsValid() && IsAlive(newParent))
        {
            auto* parentHier = m_hierarchies.TryGet(newParent);
            if (!parentHier)
            {
                parentHier = &m_hierarchies.EmplaceOrReplace(newParent, HierarchyComponent{});
            }
            childHier = m_hierarchies.TryGet(child);

            childHier->Parent = newParent;
            childHier->NextSibling = parentHier->FirstChild;
            if (parentHier->FirstChild.IsValid())
            {
                if (auto* oldFirst = m_hierarchies.TryGet(parentHier->FirstChild))
                {
                    oldFirst->PrevSibling = child;
                }
            }
            parentHier->FirstChild = child;
        }
    }

    EntityId Scene::GetParent(EntityId entity) const noexcept
    {
        if (const auto* h = m_hierarchies.TryGet(entity))
        {
            return h->Parent;
        }
        return EntityId::Invalid();
    }

    std::vector<EntityId> Scene::GetChildren(EntityId parent) const
    {
        std::vector<EntityId> result;
        const auto* h = m_hierarchies.TryGet(parent);
        if (!h)
        {
            return result;
        }

        EntityId curr = h->FirstChild;
        while (curr.IsValid())
        {
            result.push_back(curr);
            const auto* currHier = m_hierarchies.TryGet(curr);
            curr = currHier ? currHier->NextSibling : EntityId::Invalid();
        }
        return result;
    }

    NativeComponentArenaView Scene::GetNativeArenaView(ComponentPoolKind kind) noexcept
    {
        switch (kind)
        {
        case ComponentPoolKind::Transform:    return m_transforms.GetArenaView();
        case ComponentPoolKind::Hierarchy:    return m_hierarchies.GetArenaView();
        case ComponentPoolKind::MeshRenderer: return m_meshRenderers.GetArenaView();
        case ComponentPoolKind::Light:        return m_lights.GetArenaView();
        case ComponentPoolKind::Script:       return m_scripts.GetArenaView();
        case ComponentPoolKind::RigidBody:    return m_rigidBodies.GetArenaView();
        case ComponentPoolKind::AudioSource:  return m_audioSources.GetArenaView();
        case ComponentPoolKind::Camera:              return m_cameras.GetArenaView();
        case ComponentPoolKind::Animator:            return {};
        case ComponentPoolKind::CharacterController: return m_characterControllers.GetArenaView();
        }
        return {};
    }

    void Scene::UpdateScriptSystem(f32 deltaTime, Core::TaskScheduler* scheduler)
    {
        auto scripts = m_scripts.RawComponents();
        auto scriptEntities = m_scripts.RawEntities();
        const u32 count = static_cast<u32>(scripts.size());
        if (count == 0)
        {
            return;
        }

        auto updateRange = [&](u32 start, u32 end) {
            for (u32 i = start; i < end; ++i)
            {
                const ScriptComponent& sc = scripts[i];
                if (!sc.Enabled)
                {
                    continue;
                }
                if (TransformComponent* tr = m_transforms.TryGet(scriptEntities[i]))
                {
                    const Math::Quat deltaRot = Math::FromEulerDegrees({ 0.0f, sc.AngularSpeed * deltaTime * 57.2957795f, 0.0f });
                    tr->Rotation = Math::NormalizeQuat(Math::MultiplyQuat(tr->Rotation, deltaRot));
                }
            }
        };

        if (scheduler && count >= 64)
        {
            scheduler->ParallelFor(count, 64, updateRange);
        }
        else
        {
            updateRange(0, count);
        }
    }

    void Scene::UpdateAnimationSystem(f32 deltaTime, Core::TaskScheduler* scheduler)
    {
        auto animators = m_animators.RawComponents();
        const u32 count = static_cast<u32>(animators.size());
        if (count == 0)
        {
            return;
        }

        auto updateRange = [&](u32 start, u32 end) {
            for (u32 i = start; i < end; ++i)
            {
                AnimatorComponent& anim = animators[i];
                if (!anim.IsPlaying)
                {
                    continue;
                }

                anim.Evaluator.SetPlaybackSpeed(anim.PlaybackSpeed);
                anim.Evaluator.UpdateAnimation(deltaTime);
                anim.Evaluator.FillGPUBuffer(anim.GpuBoneBuffer);
                anim.NeedsGpuUpload = true;
            }
        };

        if (scheduler && count >= 16)
        {
            scheduler->ParallelFor(count, 16, updateRange);
        }
        else
        {
            updateRange(0, count);
        }
    }

    void Scene::UpdateTransformHierarchySystem(Core::TaskScheduler* scheduler)
    {
        auto transforms = m_transforms.RawComponents();
        auto entities = m_transforms.RawEntities();
        const u32 count = static_cast<u32>(transforms.size());
        m_worldMatrices.resize(count);

        if (count == 0)
        {
            return;
        }

        // Step 1: Compute local TRS matrices in parallel using SIMD Mat4
        auto computeLocalRange = [&](u32 start, u32 end) {
            for (u32 i = start; i < end; ++i)
            {
                m_worldMatrices[i] = Math::TRS(transforms[i]);
            }
        };

        if (scheduler && count >= 64)
        {
            scheduler->ParallelFor(count, 64, computeLocalRange);
        }
        else
        {
            computeLocalRange(0, count);
        }

        // Step 2: Propagate parent world transforms to children
        for (u32 i = 0; i < count; ++i)
        {
            const EntityId entity = entities[i];
            const auto* hier = m_hierarchies.TryGet(entity);
            if (!hier || !hier->Parent.IsValid())
            {
                continue;
            }

            // Walk ancestor chain
            Math::Mat4 world = m_worldMatrices[i];
            EntityId p = hier->Parent;
            while (p.IsValid())
            {
                if (const auto* pTr = m_transforms.TryGet(p))
                {
                    world = Math::MultiplyMat4(Math::TRS(*pTr), world);
                }
                const auto* pHier = m_hierarchies.TryGet(p);
                p = pHier ? pHier->Parent : EntityId::Invalid();
            }
            m_worldMatrices[i] = world;
        }
    }

    bool Scene::SaveToJsonFile(const std::filesystem::path& filePath) const
    {
        nlohmann::json root;
        root["scene_name"] = m_name;
        root["entities"] = nlohmann::json::array();

        const auto entities = m_transforms.RawEntities();
        const auto transforms = m_transforms.RawComponents();

        for (usize i = 0; i < entities.size(); ++i)
        {
            const EntityId eid = entities[i];
            const TransformComponent& tr = transforms[i];

            nlohmann::json ej;
            ej["index"] = eid.Index;
            ej["generation"] = eid.Generation;
            ej["name"] = GetEntityName(eid);
            ej["transform"] = {
                { "position", { tr.Position.x, tr.Position.y, tr.Position.z } },
                { "rotation", { tr.Rotation.x, tr.Rotation.y, tr.Rotation.z, tr.Rotation.w } },
                { "scale",    { tr.Scale.x, tr.Scale.y, tr.Scale.z } }
            };

            if (const auto* hier = m_hierarchies.TryGet(eid); hier && hier->Parent.IsValid())
            {
                ej["parent_index"] = hier->Parent.Index;
                ej["parent_generation"] = hier->Parent.Generation;
            }

            if (const auto* mr = m_meshRenderers.TryGet(eid))
            {
                ej["mesh_renderer"] = {
                    { "mesh_asset_id", mr->MeshAssetId },
                    { "material_asset_id", mr->MaterialAssetId },
                    { "submesh_index", mr->SubmeshIndex },
                    { "flags", mr->Flags }
                };
            }

            if (const auto* light = m_lights.TryGet(eid))
            {
                ej["light"] = {
                    { "color", { light->Color.x, light->Color.y, light->Color.z } },
                    { "intensity", light->Intensity },
                    { "range", light->Range },
                    { "type", static_cast<u32>(light->Type) }
                };
            }

            if (const auto* sc = m_scripts.TryGet(eid))
            {
                ej["script"] = {
                    { "class_id", sc->ScriptClassId },
                    { "angular_speed", sc->AngularSpeed },
                    { "enabled", sc->Enabled }
                };
            }

            if (const auto* rb = m_rigidBodies.TryGet(eid))
            {
                const char* motionStr = "Dynamic";
                if (rb->MotionType == RigidBodyMotionType::Static) motionStr = "Static";
                else if (rb->MotionType == RigidBodyMotionType::Kinematic) motionStr = "Kinematic";

                const char* shapeStr = "Box";
                if (rb->Shape == ColliderShapeType::Sphere) shapeStr = "Sphere";
                else if (rb->Shape == ColliderShapeType::Capsule) shapeStr = "Capsule";

                ej["rigid_body"] = {
                    { "motion_type", motionStr },
                    { "shape", shapeStr },
                    { "mass", rb->Mass },
                    { "density", rb->Density },
                    { "use_density", rb->UseDensity },
                    { "restitution", rb->Restitution },
                    { "friction", rb->Friction },
                    { "linear_damping", rb->LinearDamping },
                    { "angular_damping", rb->AngularDamping },
                    { "use_gravity", rb->UseGravity },
                    { "gravity_factor", rb->GravityFactor },
                    { "use_ccd", rb->UseCCD },
                    { "is_trigger", rb->IsTrigger },
                    { "collision_layer", rb->CollisionLayer },
                    { "collision_mask", rb->CollisionMask },
                    { "locked_axes", rb->LockedAxes },
                    { "half_extents", { rb->HalfExtents.x, rb->HalfExtents.y, rb->HalfExtents.z } },
                    { "collider_offset", { rb->ColliderOffset.x, rb->ColliderOffset.y, rb->ColliderOffset.z } },
                    { "collider_rotation", { rb->ColliderRotation.x, rb->ColliderRotation.y, rb->ColliderRotation.z, rb->ColliderRotation.w } }
                };
            }

            if (const auto* cc = m_characterControllers.TryGet(eid))
            {
                ej["character_controller"] = {
                    { "radius", cc->Radius },
                    { "height", cc->Height },
                    { "mass", cc->Mass },
                    { "max_push_strength", cc->MaxPushStrength },
                    { "max_slope_angle_degrees", cc->MaxSlopeAngleDegrees },
                    { "step_height", cc->StepHeight },
                    { "gravity_factor", cc->GravityFactor },
                    { "air_control", cc->AirControl },
                    { "ground_friction", cc->GroundFriction },
                    { "collision_layer", cc->CollisionLayer },
                    { "collision_mask", cc->CollisionMask },
                    { "center_offset", { cc->CenterOffset.x, cc->CenterOffset.y, cc->CenterOffset.z } }
                };
            }

            if (const auto* cam = m_cameras.TryGet(eid))
            {
                ej["camera"] = {
                    { "fov", cam->FovDegrees },
                    { "near", cam->NearPlane },
                    { "far", cam->FarPlane },
                    { "aspect", cam->AspectRatio },
                    { "is_active", cam->IsActive }
                };
            }

            if (const auto ccSpan = GetCompoundColliders(eid); !ccSpan.empty())
            {
                nlohmann::json ccArr = nlohmann::json::array();
                for (const auto& entry : ccSpan)
                {
                    const char* subShapeStr = (entry.Shape == ColliderShapeType::Sphere) ? "Sphere" :
                                              (entry.Shape == ColliderShapeType::Capsule ? "Capsule" : "Box");
                    ccArr.push_back({
                        { "shape", subShapeStr },
                        { "half_extents", { entry.HalfExtents.x, entry.HalfExtents.y, entry.HalfExtents.z } },
                        { "local_offset", { entry.LocalOffset.x, entry.LocalOffset.y, entry.LocalOffset.z } },
                        { "local_rotation", { entry.LocalRotation.x, entry.LocalRotation.y, entry.LocalRotation.z, entry.LocalRotation.w } },
                        { "collision_layer", entry.CollisionLayer },
                        { "collision_mask", entry.CollisionMask },
                        { "is_trigger", entry.IsTrigger }
                    });
                }
                ej["compound_colliders"] = std::move(ccArr);
            }

            if (const auto jointSpan = GetEntityJoints(eid); !jointSpan.empty())
            {
                nlohmann::json jArr = nlohmann::json::array();
                for (const auto& jt : jointSpan)
                {
                    const char* jTypeStr = "Fixed";
                    switch (jt.Type)
                    {
                    case PhysicsJointType::Distance: jTypeStr = "Distance"; break;
                    case PhysicsJointType::Hinge:    jTypeStr = "Hinge"; break;
                    case PhysicsJointType::Slider:   jTypeStr = "Slider"; break;
                    case PhysicsJointType::Fixed:
                    default:                         jTypeStr = "Fixed"; break;
                    }
                    const std::string connName = (jt.ConnectedEntity.IsValid() && IsAlive(jt.ConnectedEntity))
                        ? std::string(GetEntityName(jt.ConnectedEntity))
                        : std::string();
                    jArr.push_back({
                        { "type", jTypeStr },
                        { "connected_entity", connName },
                        { "anchor_a", { jt.AnchorA.x, jt.AnchorA.y, jt.AnchorA.z } },
                        { "anchor_b", { jt.AnchorB.x, jt.AnchorB.y, jt.AnchorB.z } },
                        { "axis", { jt.Axis.x, jt.Axis.y, jt.Axis.z } },
                        { "auto_detect_anchors", jt.AutoDetectAnchors },
                        { "min_distance", jt.MinDistance },
                        { "max_distance", jt.MaxDistance },
                        { "spring_frequency_hz", jt.SpringFrequencyHz },
                        { "spring_damping_ratio", jt.SpringDampingRatio },
                        { "min_limit", jt.MinLimit },
                        { "max_limit", jt.MaxLimit },
                        { "max_friction", jt.MaxFriction },
                        { "break_force", jt.BreakForce },
                        { "break_torque", jt.BreakTorque },
                        { "enabled", jt.Enabled }
                    });
                }
                ej["physics_joints"] = std::move(jArr);
            }

            if (const auto* net = m_networkIdentities.TryGet(eid))
            {
                const char* authStr = "Server";
                if (net->Authority == 0) authStr = "None";
                else if (net->Authority == 2) authStr = "ClientPredicted";

                const char* scopeStr = "AllRelevant";
                if (net->Scope == 1) scopeStr = "OwnerOnly";
                else if (net->Scope == 2) scopeStr = "InitialOnly";

                ej["network"] = {
                    { "replicated", net->Replicated != 0 },
                    { "static_id", net->StaticNetId },
                    { "authority", authStr },
                    { "scope", scopeStr }
                };
            }

            root["entities"].push_back(std::move(ej));
        }

        if (filePath.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }

        std::ofstream out(filePath);
        if (!out)
        {
            return false;
        }
        out << root.dump(2);
        return out.good();
    }

    bool Scene::LoadFromJsonFile(const std::filesystem::path& filePath)
    {
        MORBI_BREADCRUMB("Scene JSON Parse");
        MORBI_TRACE("Scene", "Parsing Scene JSON: '" + filePath.string() + "'");
        std::ifstream in(filePath);
        if (!in)
        {
            MORBI_TRACE("Scene", "Failed to open Scene JSON file: '" + filePath.string() + "'");
            return false;
        }

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.contains("entities"))
        {
            MORBI_TRACE("Scene", "Invalid Scene JSON or missing 'entities': '" + filePath.string() + "'");
            return false;
        }

        Clear();
        if (root.contains("scene_name") && root["scene_name"].is_string())
        {
            m_name = root["scene_name"].get<std::string>();
        }

        struct PendingParentLink
        {
            EntityId Child{};
            std::string ParentName;
        };
        struct PendingJointLink
        {
            EntityId Owner{};
            PhysicsJointEntry Joint{};
            std::string ConnectedEntityName;
        };
        std::vector<PendingParentLink> pendingParents;
        std::vector<PendingJointLink> pendingJoints;

        for (const auto& ej : root["entities"])
        {
            const std::string name = ej.value("name", "Entity");
            const EntityId eid = CreateEntity(name);

            TransformComponent tr{};
            if (ej.contains("transform") && ej["transform"].is_object())
            {
                const auto& tj = ej["transform"];
                if (tj.contains("position") && tj["position"].size() == 3)
                {
                    tr.Position = { tj["position"][0].get<f32>(), tj["position"][1].get<f32>(), tj["position"][2].get<f32>() };
                }
                if (tj.contains("rotation") && tj["rotation"].size() == 4)
                {
                    tr.Rotation = { tj["rotation"][0].get<f32>(), tj["rotation"][1].get<f32>(), tj["rotation"][2].get<f32>(), tj["rotation"][3].get<f32>() };
                }
                else if (tj.contains("rotation_euler") && tj["rotation_euler"].size() == 3)
                {
                    tr.Rotation = Math::FromEulerDegrees({
                        tj["rotation_euler"][0].get<f32>(),
                        tj["rotation_euler"][1].get<f32>(),
                        tj["rotation_euler"][2].get<f32>()
                    });
                }
                if (tj.contains("scale") && tj["scale"].size() == 3)
                {
                    tr.Scale = { tj["scale"][0].get<f32>(), tj["scale"][1].get<f32>(), tj["scale"][2].get<f32>() };
                }
                m_transforms.EmplaceOrReplace(eid, tr);
            }
            else if (ej.contains("position") && ej["position"].size() == 3)
            {
                tr.Position = { ej["position"][0].get<f32>(), ej["position"][1].get<f32>(), ej["position"][2].get<f32>() };
                if (ej.contains("scale") && ej["scale"].size() == 3)
                {
                    tr.Scale = { ej["scale"][0].get<f32>(), ej["scale"][1].get<f32>(), ej["scale"][2].get<f32>() };
                }
                m_transforms.EmplaceOrReplace(eid, tr);
            }

            if (ej.contains("parent") && ej["parent"].is_string())
            {
                pendingParents.push_back(PendingParentLink{
                    .Child = eid,
                    .ParentName = ej["parent"].get<std::string>()
                });
            }

            if (ej.contains("mesh_renderer") && ej["mesh_renderer"].is_object())
            {
                const auto& mj = ej["mesh_renderer"];
                u64 meshId = mj.value("mesh_asset_id", 0ULL);
                if (meshId == 0 && mj.contains("mesh_path") && mj["mesh_path"].is_string())
                {
                    meshId = Core::StringId::FromString(mj["mesh_path"].get<std::string>()).Value();
                }

                u64 matId = mj.value("material_asset_id", 0ULL);
                if (matId == 0 && mj.contains("albedo_texture") && mj["albedo_texture"].is_string())
                {
                    matId = Core::StringId::FromString(mj["albedo_texture"].get<std::string>()).Value();
                }
                else if (matId == 0 && mj.contains("material") && mj["material"].is_object())
                {
                    const auto& matObj = mj["material"];
                    if (matObj.contains("albedo_texture") && matObj["albedo_texture"].is_string())
                    {
                        matId = Core::StringId::FromString(matObj["albedo_texture"].get<std::string>()).Value();
                    }
                }

                m_meshRenderers.EmplaceOrReplace(eid, MeshRendererComponent{
                    .MeshAssetId = meshId,
                    .MaterialAssetId = matId,
                    .SubmeshIndex = mj.value("submesh_index", 0xFFFFFFFFu),
                    .Flags = mj.value("flags", 1u)
                });
            }

            if (ej.contains("light") && ej["light"].is_object())
            {
                const auto& lj = ej["light"];
                LightComponent lc{};
                if (lj.contains("color") && lj["color"].size() == 3)
                {
                    lc.Color = { lj["color"][0].get<f32>(), lj["color"][1].get<f32>(), lj["color"][2].get<f32>() };
                }
                lc.Intensity = lj.value("intensity", 5.0f);
                lc.Range = lj.value("range", 25.0f);
                lc.Type = static_cast<LightType>(lj.value("type", 0u));
                m_lights.EmplaceOrReplace(eid, lc);
            }

            if (ej.contains("script"))
            {
                if (ej["script"].is_string())
                {
                    const std::string clsName = ej["script"].get<std::string>();
                    m_scripts.EmplaceOrReplace(eid, ScriptComponent{
                        .ScriptClassId = Core::StringId::FromString(clsName).Value(),
                        .AngularSpeed = 0.0f,
                        .Enabled = 1u
                    });
                }
                else if (ej["script"].is_object())
                {
                    const auto& sj = ej["script"];
                    u64 clsId = sj.value("class_id", 0ULL);
                    if (clsId == 0 && sj.contains("class_name") && sj["class_name"].is_string())
                    {
                        clsId = Core::StringId::FromString(sj["class_name"].get<std::string>()).Value();
                    }
                    m_scripts.EmplaceOrReplace(eid, ScriptComponent{
                        .ScriptClassId = clsId,
                        .AngularSpeed = sj.value("angular_speed", 1.5f),
                        .Enabled = sj.value("enabled", 1u)
                    });
                }
            }

            if (ej.contains("rigid_body") && ej["rigid_body"].is_object())
            {
                const auto& rj = ej["rigid_body"];
                RigidBodyComponent rb{};
                rb.Mass = rj.value("mass", 1.0f);
                rb.Density = rj.value("density", 1000.0f);
                rb.UseDensity = static_cast<u8>(rj.value("use_density", 0u));
                rb.Restitution = rj.value("restitution", 0.35f);
                rb.Friction = rj.value("friction", 0.5f);
                rb.LinearDamping = rj.value("linear_damping", 0.05f);
                rb.AngularDamping = rj.value("angular_damping", 0.05f);
                rb.UseGravity = static_cast<u8>(rj.value("use_gravity", 1u));
                rb.GravityFactor = rj.value("gravity_factor", 1.0f);
                rb.UseCCD = static_cast<u8>(rj.value("use_ccd", 0u));
                rb.IsTrigger = static_cast<u8>(rj.value("is_trigger", 0u));
                rb.CollisionLayer = rj.value("collision_layer", 1u);
                rb.CollisionMask = rj.value("collision_mask", 0xFFFFFFFFu);
                rb.LockedAxes = static_cast<u8>(rj.value("locked_axes", 0u));
                if (rj.contains("half_extents") && rj["half_extents"].size() == 3)
                {
                    rb.HalfExtents = {
                        rj["half_extents"][0].get<f32>(),
                        rj["half_extents"][1].get<f32>(),
                        rj["half_extents"][2].get<f32>()
                    };
                }
                if (rj.contains("collider_offset") && rj["collider_offset"].size() == 3)
                {
                    rb.ColliderOffset = {
                        rj["collider_offset"][0].get<f32>(),
                        rj["collider_offset"][1].get<f32>(),
                        rj["collider_offset"][2].get<f32>()
                    };
                }
                if (rj.contains("collider_rotation") && rj["collider_rotation"].size() == 4)
                {
                    rb.ColliderRotation = {
                        rj["collider_rotation"][0].get<f32>(),
                        rj["collider_rotation"][1].get<f32>(),
                        rj["collider_rotation"][2].get<f32>(),
                        rj["collider_rotation"][3].get<f32>()
                    };
                }
                const std::string motionStr = rj.value("motion_type", "Dynamic");
                if (motionStr == "Static") rb.MotionType = RigidBodyMotionType::Static;
                else if (motionStr == "Kinematic") rb.MotionType = RigidBodyMotionType::Kinematic;
                else rb.MotionType = RigidBodyMotionType::Dynamic;

                const std::string shapeStr = rj.value("shape", "Box");
                if (shapeStr == "Sphere") rb.Shape = ColliderShapeType::Sphere;
                else if (shapeStr == "Capsule") rb.Shape = ColliderShapeType::Capsule;
                else rb.Shape = ColliderShapeType::Box;

                m_rigidBodies.EmplaceOrReplace(eid, rb);
            }

            if (ej.contains("audio_source") && ej["audio_source"].is_object())
            {
                const auto& aj = ej["audio_source"];
                u64 soundId = aj.value("sound_asset_id", 0ULL);
                if (soundId == 0 && aj.contains("sound_path") && aj["sound_path"].is_string())
                {
                    soundId = Core::StringId::FromString(aj["sound_path"].get<std::string>()).Value();
                }
                m_audioSources.EmplaceOrReplace(eid, AudioSourceComponent{
                    .SoundAssetId = soundId,
                    .Volume = aj.value("volume", 1.0f),
                    .Pitch = aj.value("pitch", 1.0f),
                    .MinDistance = aj.value("min_distance", 1.0f),
                    .MaxDistance = aj.value("max_distance", 50.0f),
                    .Spatial3D = static_cast<u8>(aj.value("spatial_3d", 1u)),
                    .Looping = static_cast<u8>(aj.value("loop", 0u))
                });
            }

            if (ej.contains("character_controller") && ej["character_controller"].is_object())
            {
                const auto& ccj = ej["character_controller"];
                CharacterControllerComponent cc{};
                cc.Radius = ccj.value("radius", 0.35f);
                cc.Height = ccj.value("height", 1.8f);
                cc.Mass = ccj.value("mass", 70.0f);
                cc.MaxPushStrength = ccj.value("max_push_strength", 250.0f);
                cc.MaxSlopeAngleDegrees = ccj.value("max_slope_angle_degrees", 45.0f);
                cc.StepHeight = ccj.value("step_height", 0.35f);
                cc.GravityFactor = ccj.value("gravity_factor", 1.0f);
                cc.AirControl = ccj.value("air_control", 0.35f);
                cc.GroundFriction = ccj.value("ground_friction", 12.0f);
                cc.CollisionLayer = ccj.value("collision_layer", 1u);
                cc.CollisionMask = ccj.value("collision_mask", 0xFFFFFFFFu);
                if (ccj.contains("center_offset") && ccj["center_offset"].is_array() && ccj["center_offset"].size() >= 3)
                {
                    cc.CenterOffset = {
                        ccj["center_offset"][0].get<f32>(),
                        ccj["center_offset"][1].get<f32>(),
                        ccj["center_offset"][2].get<f32>()
                    };
                }
                cc.DirtyFlags = static_cast<u8>(CharacterControllerDirtyFlags::All);
                m_characterControllers.EmplaceOrReplace(eid, cc);
            }

            if (ej.contains("camera") && ej["camera"].is_object())
            {
                const auto& cj = ej["camera"];
                CameraComponent cc{};
                cc.FovDegrees = cj.value("fov", 60.0f);
                cc.NearPlane = cj.value("near", 0.1f);
                cc.FarPlane = cj.value("far", 1000.0f);
                cc.AspectRatio = cj.value("aspect", 16.0f / 9.0f);
                cc.IsActive = cj.value("is_active", 1u);
                m_cameras.EmplaceOrReplace(eid, cc);
            }

            if (ej.contains("compound_colliders") && ej["compound_colliders"].is_array())
            {
                std::vector<CompoundColliderEntry> entries;
                for (const auto& cj : ej["compound_colliders"])
                {
                    if (!cj.is_object()) continue;
                    CompoundColliderEntry entry{};
                    const std::string sh = cj.value("shape", "Box");
                    if (sh == "Sphere") entry.Shape = ColliderShapeType::Sphere;
                    else if (sh == "Capsule") entry.Shape = ColliderShapeType::Capsule;
                    else entry.Shape = ColliderShapeType::Box;

                    if (cj.contains("half_extents") && cj["half_extents"].size() >= 3)
                    {
                        entry.HalfExtents = {
                            cj["half_extents"][0].get<f32>(),
                            cj["half_extents"][1].get<f32>(),
                            cj["half_extents"][2].get<f32>()
                        };
                    }
                    if (cj.contains("local_offset") && cj["local_offset"].size() >= 3)
                    {
                        entry.LocalOffset = {
                            cj["local_offset"][0].get<f32>(),
                            cj["local_offset"][1].get<f32>(),
                            cj["local_offset"][2].get<f32>()
                        };
                    }
                    if (cj.contains("local_rotation") && cj["local_rotation"].size() >= 4)
                    {
                        entry.LocalRotation = {
                            cj["local_rotation"][0].get<f32>(),
                            cj["local_rotation"][1].get<f32>(),
                            cj["local_rotation"][2].get<f32>(),
                            cj["local_rotation"][3].get<f32>()
                        };
                    }
                    entry.CollisionLayer = cj.value("collision_layer", 1u);
                    entry.CollisionMask = cj.value("collision_mask", 0xFFFFFFFFu);
                    entry.IsTrigger = static_cast<u8>(cj.value("is_trigger", 0u));
                    entries.push_back(entry);
                }
                if (!entries.empty())
                {
                    m_compoundColliders[eid.Pack()] = std::move(entries);
                }
            }

            if (ej.contains("physics_joints") && ej["physics_joints"].is_array())
            {
                for (const auto& jObj : ej["physics_joints"])
                {
                    if (!jObj.is_object()) continue;
                    PendingJointLink pj{};
                    pj.Owner = eid;
                    pj.ConnectedEntityName = jObj.value("connected_entity", "");
                    const std::string jtStr = jObj.value("type", "Fixed");
                    if (jtStr == "Distance") pj.Joint.Type = PhysicsJointType::Distance;
                    else if (jtStr == "Hinge") pj.Joint.Type = PhysicsJointType::Hinge;
                    else if (jtStr == "Slider") pj.Joint.Type = PhysicsJointType::Slider;
                    else pj.Joint.Type = PhysicsJointType::Fixed;

                    if (jObj.contains("anchor_a") && jObj["anchor_a"].size() >= 3)
                    {
                        pj.Joint.AnchorA = { jObj["anchor_a"][0].get<f32>(), jObj["anchor_a"][1].get<f32>(), jObj["anchor_a"][2].get<f32>() };
                    }
                    if (jObj.contains("anchor_b") && jObj["anchor_b"].size() >= 3)
                    {
                        pj.Joint.AnchorB = { jObj["anchor_b"][0].get<f32>(), jObj["anchor_b"][1].get<f32>(), jObj["anchor_b"][2].get<f32>() };
                    }
                    if (jObj.contains("axis") && jObj["axis"].size() >= 3)
                    {
                        pj.Joint.Axis = { jObj["axis"][0].get<f32>(), jObj["axis"][1].get<f32>(), jObj["axis"][2].get<f32>() };
                    }
                    pj.Joint.AutoDetectAnchors = jObj.value("auto_detect_anchors", true);
                    pj.Joint.MinDistance = jObj.value("min_distance", -1.0f);
                    pj.Joint.MaxDistance = jObj.value("max_distance", -1.0f);
                    pj.Joint.SpringFrequencyHz = jObj.value("spring_frequency_hz", 0.0f);
                    pj.Joint.SpringDampingRatio = jObj.value("spring_damping_ratio", 0.0f);
                    pj.Joint.MinLimit = jObj.value("min_limit", -Math::kPi);
                    pj.Joint.MaxLimit = jObj.value("max_limit", Math::kPi);
                    pj.Joint.MaxFriction = jObj.value("max_friction", 0.0f);
                    pj.Joint.BreakForce = jObj.value("break_force", 0.0f);
                    pj.Joint.BreakTorque = jObj.value("break_torque", 0.0f);
                    pj.Joint.Enabled = jObj.value("enabled", true);
                    pendingJoints.push_back(std::move(pj));
                }
            }

            if (ej.contains("network") && ej["network"].is_object())
            {
                const auto& nj = ej["network"];
                NetworkIdentityComponent net{};
                net.Replicated = nj.value("replicated", false) ? 1u : 0u;
                net.StaticNetId = nj.value("static_id", 0ULL);

                const std::string authStr = nj.value("authority", "Server");
                if (authStr == "None") net.Authority = 0;
                else if (authStr == "ClientPredicted") net.Authority = 2;
                else net.Authority = 1;

                const std::string scopeStr = nj.value("scope", "AllRelevant");
                if (scopeStr == "OwnerOnly") net.Scope = 1;
                else if (scopeStr == "InitialOnly") net.Scope = 2;
                else net.Scope = 0;

                m_networkIdentities.EmplaceOrReplace(eid, net);
            }
        }

        for (const auto& link : pendingParents)
        {
            const EntityId parentId = FindEntityByName(link.ParentName);
            if (parentId.IsValid())
            {
                SetParent(link.Child, parentId);
            }
        }

        for (auto& pj : pendingJoints)
        {
            if (!pj.ConnectedEntityName.empty() && pj.ConnectedEntityName != "World")
            {
                const EntityId resolved = FindEntityByName(pj.ConnectedEntityName);
                if (!resolved.IsValid() || resolved == pj.Owner)
                {
                    Core::Log::Warn("Scene", "Skipping physics joint on entity '" +
                        std::string(GetEntityName(pj.Owner)) + "' with invalid connected_entity reference '" +
                        pj.ConnectedEntityName + "'.");
                    continue;
                }
                pj.Joint.ConnectedEntity = resolved;
            }
            AddEntityJoint(pj.Owner, pj.Joint);
        }

        return true;
    }

    void Scene::Clear()
    {
        if (m_sceneClearedCallback)
        {
            m_sceneClearedCallback();
        }
        m_entityRecords.clear();
        m_freeIndices.clear();
        m_aliveCount = 0;
        m_transforms.Clear();
        m_hierarchies.Clear();
        m_meshRenderers.Clear();
        m_lights.Clear();
        m_scripts.Clear();
        m_rigidBodies.Clear();
        m_audioSources.Clear();
        m_cameras.Clear();
        m_animators.Clear();
        m_characterControllers.Clear();
        m_networkIdentities.Clear();
        m_compoundColliders.clear();
        m_physicsJoints.clear();
        m_worldMatrices.clear();
    }

    void Scene::SetCompoundColliders(EntityId entity, std::span<const CompoundColliderEntry> shapes)
    {
        if (!IsAlive(entity))
        {
            return;
        }
        if (shapes.empty())
        {
            m_compoundColliders.erase(entity.Pack());
            return;
        }
        m_compoundColliders[entity.Pack()].assign(shapes.begin(), shapes.end());
    }

    bool Scene::AddCompoundCollider(EntityId entity, const CompoundColliderEntry& shape)
    {
        if (!IsAlive(entity))
        {
            return false;
        }
        m_compoundColliders[entity.Pack()].push_back(shape);
        return true;
    }

    bool Scene::RemoveCompoundColliderAt(EntityId entity, u32 index)
    {
        if (!IsAlive(entity))
        {
            return false;
        }
        const auto it = m_compoundColliders.find(entity.Pack());
        if (it == m_compoundColliders.end() || index >= static_cast<u32>(it->second.size()))
        {
            return false;
        }
        it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
        if (it->second.empty())
        {
            m_compoundColliders.erase(it);
        }
        return true;
    }

    void Scene::ClearCompoundColliders(EntityId entity)
    {
        m_compoundColliders.erase(entity.Pack());
    }

    std::span<const CompoundColliderEntry> Scene::GetCompoundColliders(EntityId entity) const noexcept
    {
        if (!IsAlive(entity))
        {
            return {};
        }
        const auto it = m_compoundColliders.find(entity.Pack());
        if (it == m_compoundColliders.end())
        {
            return {};
        }
        return it->second;
    }

    void Scene::SetEntityJoints(EntityId entity, std::span<const PhysicsJointEntry> joints)
    {
        if (!IsAlive(entity))
        {
            return;
        }
        if (joints.empty())
        {
            m_physicsJoints.erase(entity.Pack());
            return;
        }
        m_physicsJoints[entity.Pack()].assign(joints.begin(), joints.end());
    }

    bool Scene::AddEntityJoint(EntityId entity, const PhysicsJointEntry& joint)
    {
        if (!IsAlive(entity))
        {
            return false;
        }
        m_physicsJoints[entity.Pack()].push_back(joint);
        return true;
    }

    bool Scene::UpdateEntityJointAt(EntityId entity, u32 index, const PhysicsJointEntry& joint)
    {
        if (!IsAlive(entity))
        {
            return false;
        }
        const auto it = m_physicsJoints.find(entity.Pack());
        if (it == m_physicsJoints.end() || index >= static_cast<u32>(it->second.size()))
        {
            return false;
        }
        it->second[index] = joint;
        return true;
    }

    bool Scene::RemoveEntityJointAt(EntityId entity, u32 index)
    {
        if (!IsAlive(entity))
        {
            return false;
        }
        const auto it = m_physicsJoints.find(entity.Pack());
        if (it == m_physicsJoints.end() || index >= static_cast<u32>(it->second.size()))
        {
            return false;
        }
        it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
        if (it->second.empty())
        {
            m_physicsJoints.erase(it);
        }
        return true;
    }

    void Scene::ClearEntityJoints(EntityId entity)
    {
        m_physicsJoints.erase(entity.Pack());
    }

    std::span<const PhysicsJointEntry> Scene::GetEntityJoints(EntityId entity) const noexcept
    {
        if (!IsAlive(entity))
        {
            return {};
        }
        const auto it = m_physicsJoints.find(entity.Pack());
        if (it == m_physicsJoints.end())
        {
            return {};
        }
        return it->second;
    }

    std::vector<PhysicsJointEntry>* Scene::TryGetMutableEntityJoints(EntityId entity) noexcept
    {
        if (!IsAlive(entity))
        {
            return nullptr;
        }
        const auto it = m_physicsJoints.find(entity.Pack());
        return (it != m_physicsJoints.end()) ? &it->second : nullptr;
    }

    Scene::SceneSnapshot Scene::CreateSnapshot() const
    {
        SceneSnapshot snap;
        snap.Name = m_name;
        snap.AliveCount = m_aliveCount;
        snap.EntityRecords = m_entityRecords;
        snap.FreeIndices = m_freeIndices;
        snap.Transforms = m_transforms;
        snap.Hierarchies = m_hierarchies;
        snap.MeshRenderers = m_meshRenderers;
        snap.Lights = m_lights;
        snap.Scripts = m_scripts;
        snap.RigidBodies = m_rigidBodies;
        snap.AudioSources = m_audioSources;
        snap.Cameras = m_cameras;
        snap.Animators = m_animators;
        snap.CharacterControllers = m_characterControllers;
        snap.NetworkIdentities = m_networkIdentities;
        snap.CompoundColliders = m_compoundColliders;
        snap.PhysicsJoints = m_physicsJoints;
        snap.WorldMatrices = m_worldMatrices;
        return snap;
    }

    void Scene::RestoreSnapshot(const Scene::SceneSnapshot& snapshot)
    {
        m_name = snapshot.Name;
        m_aliveCount = snapshot.AliveCount;
        m_entityRecords = snapshot.EntityRecords;
        m_freeIndices = snapshot.FreeIndices;
        m_transforms = snapshot.Transforms;
        m_hierarchies = snapshot.Hierarchies;
        m_meshRenderers = snapshot.MeshRenderers;
        m_lights = snapshot.Lights;
        m_scripts = snapshot.Scripts;
        m_rigidBodies = snapshot.RigidBodies;
        m_audioSources = snapshot.AudioSources;
        m_cameras = snapshot.Cameras;
        m_animators = snapshot.Animators;
        m_characterControllers = snapshot.CharacterControllers;
        m_networkIdentities = snapshot.NetworkIdentities;
        m_compoundColliders = snapshot.CompoundColliders;
        m_physicsJoints = snapshot.PhysicsJoints;
        for (auto& [owner, joints] : m_physicsJoints)
        {
            for (auto& jt : joints)
            {
                jt.RuntimeJointHandle = 0ull;
                jt.Broken = false;
            }
        }
        m_worldMatrices = snapshot.WorldMatrices;
    }
}

MORBI_SCENE_C_API Morbi::u64 Morbi_Scene_CreateEntity(Morbi::Scene::Scene* scene, const char* name)
{
    if (!scene)
    {
        return Morbi::Scene::EntityId::Invalid().Pack();
    }
    return scene->CreateEntity(name ? name : "ManagedEntity").Pack();
}

MORBI_SCENE_C_API void Morbi_Scene_DestroyEntity(Morbi::Scene::Scene* scene, Morbi::u64 packedEntityId)
{
    if (scene)
    {
        scene->DestroyEntity(Morbi::Scene::EntityId::Unpack(packedEntityId));
    }
}

MORBI_SCENE_C_API void Morbi_Scene_GetNativeArenaView(
    Morbi::Scene::Scene* scene,
    Morbi::u8 poolKind,
    Morbi::Scene::NativeComponentArenaView* outView)
{
    if (scene && outView)
    {
        *outView = scene->GetNativeArenaView(static_cast<Morbi::Scene::ComponentPoolKind>(poolKind));
    }
}
