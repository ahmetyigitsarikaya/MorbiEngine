#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <Psapi.h>
#endif

#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Physics/PhysicsWorld.hpp"
#include "Morbi/Scene/Scene.hpp"
#include "Morbi/UI/WebView2Compositor.hpp"

using namespace Morbi;

namespace
{
    int g_failures = 0;

    void Check(bool cond, const std::string& expr, const std::string& msg)
    {
        if (!cond)
        {
            std::cerr << "[FAIL] " << expr << " | " << msg << "\n";
            ++g_failures;
        }
    }

    #define EXPECT_TRUE(cond, msg) Check((cond), #cond, (msg))

    void StepWorld(Physics::PhysicsWorld& world, Scene::Scene& scene, f32 seconds, f32 dt = 1.0f / 60.0f)
    {
        const int steps = static_cast<int>(std::ceil(seconds / dt));
        for (int i = 0; i < steps; ++i)
        {
            world.StepSimulation(dt, scene);
        }
    }

    Scene::EntityId CreateBoxEntity(
        Scene::Scene& scene,
        const std::string& name,
        const Math::Vec3& pos,
        const Math::Vec3& halfExtents,
        Scene::RigidBodyMotionType motionType,
        f32 mass = 1.0f,
        f32 restitution = 0.0f,
        f32 friction = 0.5f)
    {
        const Scene::EntityId eid = scene.CreateEntity(name);
        auto* tr = scene.Transforms().TryGet(eid);
        tr->Position = pos;

        Scene::RigidBodyComponent rb{};
        rb.MotionType = motionType;
        rb.Shape = Scene::ColliderShapeType::Box;
        rb.HalfExtents = halfExtents;
        rb.Mass = mass;
        rb.Restitution = restitution;
        rb.Friction = friction;
        scene.RigidBodies().EmplaceOrReplace(eid, rb);
        return eid;
    }

    void Test1_JoltInitAndShutdown()
    {
        Physics::PhysicsWorld world;
        EXPECT_TRUE(world.Initialize(), "Initialize should succeed");
        EXPECT_TRUE(world.IsInitialized(), "World should report initialized");
        EXPECT_TRUE(world.GetBodyCount() == 0u, "Fresh world should have 0 Jolt bodies");

        Scene::Scene scene("InitTest");
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f }, Scene::RigidBodyMotionType::Static);
        world.StepSimulation(1.0f / 60.0f, scene);
        EXPECT_TRUE(world.GetBodyCount() == 1u, "Jolt PhysicsSystem should have 1 body after step");

        world.Shutdown();
        EXPECT_TRUE(!world.IsInitialized(), "World should report uninitialized after Shutdown");
        EXPECT_TRUE(world.Initialize(), "Re-initializing PhysicsWorld should succeed");
        world.Shutdown();
        std::cout << "[PASS] Test1_JoltInitAndShutdown\n";
    }

    void Test2_BoxDropAndRest()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("BoxDrop");

        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.6f);
        const auto box = CreateBoxEntity(scene, "Box", { 0.0f, 5.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.6f);

        StepWorld(world, scene, 3.0f);

        const auto* tr = scene.Transforms().TryGet(box);
        const auto* rb = scene.RigidBodies().TryGet(box);
        EXPECT_TRUE(tr != nullptr && rb != nullptr, "Box components exist");
        EXPECT_TRUE(std::abs(tr->Position.y - 0.5f) < 0.05f, "Box should rest on floor surface at y=0.5m, got " + std::to_string(tr->Position.y));
        EXPECT_TRUE(world.IsSleeping(scene, box), "Resting box should enter sleep state");
        world.Shutdown();
        std::cout << "[PASS] Test2_BoxDropAndRest\n";
    }

    void Test3_HorizontalMomentumAndMassRatio()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        world.SetGravity({ 0.0f, 0.0f, 0.0f });
        Scene::Scene scene("HorizontalCollision");

        const auto heavy = CreateBoxEntity(scene, "Heavy", { -3.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 10.0f, 1.0f, 0.0f);
        const auto light = CreateBoxEntity(scene, "Light", { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 1.0f, 0.0f);

        // Disable linear damping so momentum is conserved
        scene.RigidBodies().TryGet(heavy)->LinearDamping = 0.0f;
        scene.RigidBodies().TryGet(light)->LinearDamping = 0.0f;
        world.SetLinearVelocity(scene, heavy, { 5.0f, 0.0f, 0.0f });

        StepWorld(world, scene, 1.5f);

        Math::Vec3 vHeavy{};
        Math::Vec3 vLight{};
        EXPECT_TRUE(world.GetLinearVelocity(scene, heavy, vHeavy), "GetLinearVelocity heavy should succeed");
        EXPECT_TRUE(world.GetLinearVelocity(scene, light, vLight), "GetLinearVelocity light should succeed");

        const f32 pBefore = 10.0f * 5.0f;
        const f32 pAfter = 10.0f * vHeavy.x + 1.0f * vLight.x;
        EXPECT_TRUE(vLight.x > vHeavy.x, "Light box should bounce forward faster than heavy box");
        EXPECT_TRUE(vLight.x > 6.0f, "Light box should acquire high positive X velocity, got " + std::to_string(vLight.x));
        EXPECT_TRUE(std::abs(pAfter - pBefore) < 0.25f, "Linear momentum should be conserved: before=" +
            std::to_string(pBefore) + " after=" + std::to_string(pAfter));
        world.Shutdown();
        std::cout << "[PASS] Test3_HorizontalMomentumAndMassRatio\n";
    }

    void Test4_SphereRestitutionAndCapsuleRotation()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("SphereAndCapsule");

        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 30.0f, 0.5f, 30.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.5f);

        const auto sphereHigh = scene.CreateEntity("SphereHigh");
        scene.Transforms().TryGet(sphereHigh)->Position = { -3.0f, 5.0f, 0.0f };
        {
            Scene::RigidBodyComponent rb{};
            rb.Shape = Scene::ColliderShapeType::Sphere;
            rb.HalfExtents = { 0.5f, 0.5f, 0.5f };
            rb.Restitution = 0.85f;
            rb.Friction = 0.1f;
            rb.LinearDamping = 0.0f;
            scene.RigidBodies().EmplaceOrReplace(sphereHigh, rb);
        }

        const auto sphereZero = scene.CreateEntity("SphereZero");
        scene.Transforms().TryGet(sphereZero)->Position = { 3.0f, 5.0f, 0.0f };
        {
            Scene::RigidBodyComponent rb{};
            rb.Shape = Scene::ColliderShapeType::Sphere;
            rb.HalfExtents = { 0.5f, 0.5f, 0.5f };
            rb.Restitution = 0.0f;
            rb.Friction = 0.5f;
            scene.RigidBodies().EmplaceOrReplace(sphereZero, rb);
        }

        const auto capsule = scene.CreateEntity("TiltedCapsule");
        scene.Transforms().TryGet(capsule)->Position = { 0.0f, 2.0f, 0.0f };
        scene.Transforms().TryGet(capsule)->Rotation = Math::FromEulerDegrees({ 0.0f, 0.0f, 35.0f });
        {
            Scene::RigidBodyComponent rb{};
            rb.Shape = Scene::ColliderShapeType::Capsule;
            rb.HalfExtents = { 0.4f, 0.6f, 0.4f };
            rb.Restitution = 0.1f;
            rb.Friction = 0.6f;
            scene.RigidBodies().EmplaceOrReplace(capsule, rb);
        }

        f32 maxBounceYAfterHit = 0.0f;
        bool sphereHighHitOnce = false;
        f32 sphereZeroMaxAfterHit = 0.0f;
        bool sphereZeroHitOnce = false;

        for (int i = 0; i < 180; ++i)
        {
            world.StepSimulation(1.0f / 60.0f, scene);
            const f32 hy = scene.Transforms().TryGet(sphereHigh)->Position.y;
            const f32 zy = scene.Transforms().TryGet(sphereZero)->Position.y;
            if (hy < 0.65f) sphereHighHitOnce = true;
            if (sphereHighHitOnce) maxBounceYAfterHit = std::max(maxBounceYAfterHit, hy);
            if (zy < 0.65f) sphereZeroHitOnce = true;
            if (sphereZeroHitOnce) sphereZeroMaxAfterHit = std::max(sphereZeroMaxAfterHit, zy);
        }

        const auto* capTr = scene.Transforms().TryGet(capsule);
        EXPECT_TRUE(maxBounceYAfterHit > 2.5f, "High-restitution sphere should bounce significantly, maxY=" + std::to_string(maxBounceYAfterHit));
        EXPECT_TRUE(sphereZeroMaxAfterHit < 0.65f, "Zero-restitution sphere should not bounce, maxY=" + std::to_string(sphereZeroMaxAfterHit));
        EXPECT_TRUE(capTr->Position.y >= 0.35f && capTr->Position.y < 1.2f, "Tilted capsule should settle onto floor without sinking");
        world.Shutdown();
        std::cout << "[PASS] Test4_SphereRestitutionAndCapsuleRotation\n";
    }

    void Test5_SlopeFrictionAndOBB()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("SlopeFriction");

        // 30-degree inclined ramp: tan(30 deg) ~= 0.577
        const auto ramp = CreateBoxEntity(scene, "Ramp", { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.25f, 10.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.9f);
        scene.Transforms().TryGet(ramp)->Rotation = Math::FromEulerDegrees({ 0.0f, 0.0f, -30.0f });

        const auto lowFric = CreateBoxEntity(scene, "LowFriction", { 0.0f, 2.0f, -2.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.05f);
        scene.Transforms().TryGet(lowFric)->Rotation = Math::FromEulerDegrees({ 0.0f, 0.0f, -30.0f });

        const auto highFric = CreateBoxEntity(scene, "HighFriction", { 0.0f, 2.0f, 2.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.95f);
        scene.Transforms().TryGet(highFric)->Rotation = Math::FromEulerDegrees({ 0.0f, 0.0f, -30.0f });

        StepWorld(world, scene, 1.5f);

        const f32 lowX = scene.Transforms().TryGet(lowFric)->Position.x;
        const f32 highX = scene.Transforms().TryGet(highFric)->Position.x;
        EXPECT_TRUE(lowX > 2.0f, "Low-friction box should slide down 30-deg slope (+X), x=" + std::to_string(lowX));
        EXPECT_TRUE(highX < 0.75f, "High-friction box should stay near top of 30-deg slope, x=" + std::to_string(highX));
        world.Shutdown();
        std::cout << "[PASS] Test5_SlopeFrictionAndOBB\n";
    }

    void Test6_KinematicPushesDynamic()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("KinematicPush");

        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.2f);
        const auto pusher = CreateBoxEntity(scene, "KinematicPusher", { -2.5f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Kinematic, 1.0f, 0.0f, 0.2f);
        const auto dynBox = CreateBoxEntity(scene, "DynamicBox", { 0.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 2.0f, 0.0f, 0.2f);

        const f32 dt = 1.0f / 60.0f;
        for (int i = 0; i < 90; ++i)
        {
            scene.Transforms().TryGet(pusher)->Position.x += 4.0f * dt;
            world.StepSimulation(dt, scene);
        }

        const f32 dynX = scene.Transforms().TryGet(dynBox)->Position.x;
        EXPECT_TRUE(dynX > 2.0f, "Kinematic body must push Dynamic box along +X, got x=" + std::to_string(dynX));
        world.Shutdown();
        std::cout << "[PASS] Test6_KinematicPushesDynamic\n";
    }

    void Test7_OffsetCompoundAndRaycast()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("CompoundAndRaycast");

        const auto floor = CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f }, Scene::RigidBodyMotionType::Static);
        const auto sphere = scene.CreateEntity("SphereTarget");
        scene.Transforms().TryGet(sphere)->Position = { 0.0f, 2.0f, 0.0f };
        {
            Scene::RigidBodyComponent rb{};
            rb.MotionType = Scene::RigidBodyMotionType::Static;
            rb.Shape = Scene::ColliderShapeType::Sphere;
            rb.HalfExtents = { 1.0f, 1.0f, 1.0f };
            rb.ColliderOffset = { 2.0f, 0.0f, 0.0f };
            scene.RigidBodies().EmplaceOrReplace(sphere, rb);
        }

        world.StepSimulation(1.0f / 60.0f, scene);

        Physics::RaycastHit zeroHit{};
        EXPECT_TRUE(!world.Raycast(scene, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 10.0f, zeroHit),
            "Zero-direction raycast must safely return false");

        Physics::RaycastHit sphereHit{};
        const bool hitOffsetSphere = world.Raycast(scene, { 2.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 10.0f, sphereHit);
        EXPECT_TRUE(hitOffsetSphere && sphereHit.HitEntity == sphere, "Raycast should hit offset sphere at x=2");
        EXPECT_TRUE(std::abs(sphereHit.Point.y - 3.0f) < 0.05f, "Hit point should be top of offset sphere y=3.0m");

        // Compound shape test
        const auto compound = CreateBoxEntity(scene, "Compound", { -4.0f, 2.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
        std::vector<Physics::ColliderShapeDesc> extras{
            Physics::ColliderShapeDesc{
                .Shape = Scene::ColliderShapeType::Sphere,
                .HalfExtents = { 0.4f, 0.4f, 0.4f },
                .LocalOffset = { 1.0f, 0.0f, 0.0f }
            }
        };
        world.SetEntityCompoundColliders(compound, extras);
        StepWorld(world, scene, 1.0f);
        EXPECT_TRUE(scene.Transforms().TryGet(compound)->Position.y > 0.0f, "Compound body should rest above floor");
        (void)floor;
        world.Shutdown();
        std::cout << "[PASS] Test7_OffsetCompoundAndRaycast\n";
    }

    void Test8_EntityDestructionAndSceneLifecycle()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("LifecycleTest");

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            world.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            world.ClearAllBodies(nullptr);
        });

        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, Scene::RigidBodyMotionType::Static);
        std::vector<Scene::EntityId> boxes;
        for (int i = 0; i < 50; ++i)
        {
            boxes.push_back(CreateBoxEntity(
                scene,
                "Box_" + std::to_string(i),
                { static_cast<f32>(i % 10) * 2.0f, 3.0f + static_cast<f32>(i / 10) * 2.0f, 0.0f },
                { 0.4f, 0.4f, 0.4f },
                Scene::RigidBodyMotionType::Dynamic));
        }

        world.StepSimulation(1.0f / 60.0f, scene);
        EXPECT_TRUE(world.GetBodyCount() == 51u, "Should have 51 Jolt bodies");

        // Destroy 25 entities
        for (int i = 0; i < 25; ++i)
        {
            scene.DestroyEntity(boxes[i]);
        }
        EXPECT_TRUE(world.GetBodyCount() == 26u, "Immediate DestroyBody callback should leave 26 bodies");

        // PIE snapshot & restore cycle
        const auto snapshot = scene.CreateSnapshot();
        const f32 initialY = scene.Transforms().TryGet(boxes[25])->Position.y;
        StepWorld(world, scene, 1.0f);
        const f32 fallenY = scene.Transforms().TryGet(boxes[25])->Position.y;
        EXPECT_TRUE(fallenY < initialY - 0.5f, "Box should fall during PIE");

        scene.RestoreSnapshot(snapshot);
        world.RebuildFromScene(scene);
        EXPECT_TRUE(world.GetBodyCount() == 26u, "Restored PIE snapshot should rebuild exact 26 bodies");
        EXPECT_TRUE(std::abs(scene.Transforms().TryGet(boxes[25])->Position.y - initialY) < 1e-4f,
            "Restored box should return to initial Y position");

        // Scene Clear
        scene.Clear();
        EXPECT_TRUE(world.GetBodyCount() == 0u, "Scene::Clear should clear all Jolt bodies");
        world.Shutdown();
        std::cout << "[PASS] Test8_EntityDestructionAndSceneLifecycle\n";
    }

    void Test9_SceneSerializationRoundTrip()
    {
        const std::filesystem::path tempScene = std::filesystem::temp_directory_path() / "morbi_physics_roundtrip.mscene.json";
        {
            Scene::Scene scene("SerializeScene");
            const auto eid = scene.CreateEntity("TestBody");
            scene.Transforms().TryGet(eid)->Position = { 1.5f, 4.0f, -2.5f };

            Scene::RigidBodyComponent rb{};
            rb.MotionType = Scene::RigidBodyMotionType::Kinematic;
            rb.Shape = Scene::ColliderShapeType::Capsule;
            rb.Mass = 12.5f;
            rb.Density = 800.0f;
            rb.UseDensity = 1;
            rb.Friction = 0.77f;
            rb.Restitution = 0.42f;
            rb.LinearDamping = 0.12f;
            rb.AngularDamping = 0.23f;
            rb.UseGravity = 0;
            rb.GravityFactor = 0.5f;
            rb.UseCCD = 1;
            rb.IsTrigger = 1;
            rb.CollisionLayer = 4u;
            rb.CollisionMask = 0x0Fu;
            rb.LockedAxes = 0b001010u;
            rb.HalfExtents = { 0.45f, 0.9f, 0.45f };
            rb.ColliderOffset = { 0.1f, 0.2f, 0.3f };
            rb.ColliderRotation = Math::FromEulerDegrees({ 15.0f, 30.0f, 0.0f });
            scene.RigidBodies().EmplaceOrReplace(eid, rb);

            EXPECT_TRUE(scene.SaveToJsonFile(tempScene), "SaveToJsonFile should succeed");
        }

        {
            Scene::Scene loaded("Empty");
            EXPECT_TRUE(loaded.LoadFromJsonFile(tempScene), "LoadFromJsonFile should succeed");
            const auto eid = loaded.FindEntityByName("TestBody");
            EXPECT_TRUE(eid.IsValid(), "Loaded entity should exist");
            const auto* rb = loaded.RigidBodies().TryGet(eid);
            EXPECT_TRUE(rb != nullptr, "Loaded entity must have RigidBodyComponent");
            if (rb)
            {
                EXPECT_TRUE(rb->MotionType == Scene::RigidBodyMotionType::Kinematic, "MotionType preserved");
                EXPECT_TRUE(rb->Shape == Scene::ColliderShapeType::Capsule, "Shape preserved");
                EXPECT_TRUE(std::abs(rb->Mass - 12.5f) < 1e-4f, "Mass preserved");
                EXPECT_TRUE(std::abs(rb->Friction - 0.77f) < 1e-4f, "Friction preserved");
                EXPECT_TRUE(std::abs(rb->Restitution - 0.42f) < 1e-4f, "Restitution preserved");
                EXPECT_TRUE(std::abs(rb->LinearDamping - 0.12f) < 1e-4f, "LinearDamping preserved");
                EXPECT_TRUE(std::abs(rb->AngularDamping - 0.23f) < 1e-4f, "AngularDamping preserved");
                EXPECT_TRUE(rb->UseGravity == 0 && rb->UseCCD == 1 && rb->IsTrigger == 1, "Flags preserved");
                EXPECT_TRUE(rb->CollisionLayer == 4u && rb->CollisionMask == 0x0Fu, "Collision layer/mask preserved");
                EXPECT_TRUE(std::abs(rb->ColliderOffset.y - 0.2f) < 1e-4f, "ColliderOffset preserved");
            }
        }
        std::error_code ec;
        std::filesystem::remove(tempScene, ec);
        std::cout << "[PASS] Test9_SceneSerializationRoundTrip\n";
    }

    void Test10_RuntimeComponentSyncAndMultiSubstepEvents()
    {
        Physics::PhysicsWorld world(1.0f / 60.0f);
        world.Initialize();
        Scene::Scene scene("RuntimeSyncAndSubsteps");

        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.5f);
        const auto box = CreateBoxEntity(scene, "RuntimeBox", { 0.0f, 0.52f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.5f);

        // 1. Multi-substep event aggregation: dt = 0.035s runs 2 substeps (2 * 1/60 = 0.0333s)
        // Put box slightly above floor with downward velocity so it hits on substep 1
        world.SetLinearVelocity(scene, box, { 0.0f, -5.0f, 0.0f });
        const u32 steps = world.StepSimulation(0.035f, scene);
        EXPECT_TRUE(steps == 2u, "Expected 2 substeps for dt=0.035s");
        EXPECT_TRUE(!world.GetRecentCollisions().empty(), "Collision events across multiple substeps must not be lost");

        // 2. Runtime property sync: disable gravity and apply upward velocity, verify no downward fall
        auto* rb = scene.RigidBodies().TryGet(box);
        rb->UseGravity = 0;
        rb->LinearDamping = 0.0f;
        world.SetLinearVelocity(scene, box, { 0.0f, 2.0f, 0.0f });
        StepWorld(world, scene, 0.5f);
        Math::Vec3 vel{};
        EXPECT_TRUE(world.GetLinearVelocity(scene, box, vel), "GetLinearVelocity");
        EXPECT_TRUE(std::abs(vel.y - 2.0f) < 0.05f, "Runtime UseGravity=0 should keep vy=2.0 m/s, got " + std::to_string(vel.y));

        // 3. Runtime shape resize: enlarge HalfExtents and verify raycast hits new bounds
        rb->MotionType = Scene::RigidBodyMotionType::Static;
        rb->HalfExtents = { 3.0f, 0.5f, 3.0f };
        world.StepSimulation(1.0f / 60.0f, scene);
        Physics::RaycastHit hit{};
        const bool hitExpanded = world.Raycast(scene, { 2.5f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 20.0f, hit);
        EXPECT_TRUE(hitExpanded && hit.HitEntity == box, "Raycast should hit expanded runtime shape at x=2.5m");

        // 4. Parent-child hierarchy sync
        const auto parent = scene.CreateEntity("Parent");
        scene.Transforms().TryGet(parent)->Position = { 10.0f, 0.0f, 0.0f };
        const auto child = CreateBoxEntity(scene, "Child", { 2.0f, 5.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        scene.SetParent(child, parent);
        world.StepSimulation(1.0f / 60.0f, scene);
        Physics::RaycastHit childHit{};
        EXPECT_TRUE(world.Raycast(scene, { 12.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 10.0f, childHit) && childHit.HitEntity == child,
            "Parented static rigid body should exist at world position x=12.0m");

        world.Shutdown();
        std::cout << "[PASS] Test10_RuntimeComponentSyncAndMultiSubstepEvents\n";
    }

    void Test11_30BoxStackingStability()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("Stack30");

        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 25.0f, 0.5f, 25.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.8f);
        std::vector<Scene::EntityId> stack;
        for (int i = 0; i < 30; ++i)
        {
            stack.push_back(CreateBoxEntity(
                scene,
                "StackBox_" + std::to_string(i),
                { 0.0f, 0.5f + static_cast<f32>(i) * 1.0f, 0.0f },
                { 0.5f, 0.5f, 0.5f },
                Scene::RigidBodyMotionType::Dynamic,
                1.0f,
                0.0f,
                0.8f));
        }

        // Also verify a 10-box stack enters sleep cleanly
        std::vector<Scene::EntityId> stack10;
        for (int i = 0; i < 10; ++i)
        {
            stack10.push_back(CreateBoxEntity(
                scene,
                "Stack10_" + std::to_string(i),
                { 8.0f, 0.5f + static_cast<f32>(i) * 1.0f, 0.0f },
                { 0.5f, 0.5f, 0.5f },
                Scene::RigidBodyMotionType::Dynamic,
                1.0f,
                0.0f,
                0.8f));
        }

        StepWorld(world, scene, 5.0f);

        const f32 topY = scene.Transforms().TryGet(stack.back())->Position.y;
        const f32 expectedTopY = 29.5f;
        Math::Vec3 topVel{};
        EXPECT_TRUE(world.GetLinearVelocity(scene, stack.back(), topVel), "GetLinearVelocity top");
        EXPECT_TRUE(std::abs(topY - expectedTopY) < 0.30f,
            "30-box stack top should remain within 1cm/layer of y=29.5m, got " + std::to_string(topY));
        EXPECT_TRUE(std::abs(topVel.y) < 0.05f, "30-box stack should not jitter/explode, topVy=" + std::to_string(topVel.y));
        EXPECT_TRUE(world.IsSleeping(scene, stack10.front()), "10-box stack should settle completely to sleep");
        world.Shutdown();
        std::cout << "[PASS] Test11_30BoxStackingStability\n";
    }

    void Test12_CollisionFilteringAndLayerMatrix()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        world.SetGravity({ 0.0f, 0.0f, 0.0f });
        Scene::Scene scene("Filtering");

        // Layer 1 (bit 0) vs Layer 2 (bit 1): disable collision via SetLayerCollision(0, 1, false)
        world.SetLayerCollision(0u, 1u, false);
        EXPECT_TRUE(!world.GetLayerCollision(0u, 1u), "Layer 0 and 1 collision should be disabled");

        const auto b1 = CreateBoxEntity(scene, "Layer0", { -2.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
        const auto b2 = CreateBoxEntity(scene, "Layer1", { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        scene.RigidBodies().TryGet(b1)->CollisionLayer = 1u << 0;
        scene.RigidBodies().TryGet(b2)->CollisionLayer = 1u << 1;

        world.SetLinearVelocity(scene, b1, { 6.0f, 0.0f, 0.0f });
        StepWorld(world, scene, 1.0f);

        EXPECT_TRUE(scene.Transforms().TryGet(b1)->Position.x > 2.0f,
            "Disabled layer collision should allow body to pass right through");
        world.Shutdown();
        std::cout << "[PASS] Test12_CollisionFilteringAndLayerMatrix\n";
    }

    void Test13_ContinuousCollisionDetection()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        world.SetGravity({ 0.0f, 0.0f, 0.0f });
        Scene::Scene scene("CCDTest");

        // Thin 0.05m wall at x = 2.0m
        CreateBoxEntity(scene, "ThinWall", { 2.0f, 0.0f, 0.0f }, { 0.025f, 5.0f, 5.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.5f);

        const auto bullet = CreateBoxEntity(scene, "FastBullet", { 0.0f, 0.0f, 0.0f }, { 0.1f, 0.1f, 0.1f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.5f);
        scene.RigidBodies().TryGet(bullet)->UseCCD = 1u;
        scene.RigidBodies().TryGet(bullet)->LinearDamping = 0.0f;
        world.SetLinearVelocity(scene, bullet, { 200.0f, 0.0f, 0.0f });

        bool sawCollision = false;
        for (int i = 0; i < 10; ++i)
        {
            world.StepSimulation(1.0f / 60.0f, scene);
            if (!world.GetRecentCollisions().empty())
            {
                sawCollision = true;
            }
        }

        const f32 finalX = scene.Transforms().TryGet(bullet)->Position.x;
        EXPECT_TRUE(finalX <= 2.0f, "CCD bullet must not tunnel past thin wall at x=2.0m, got x=" + std::to_string(finalX));
        EXPECT_TRUE(sawCollision, "CCD bullet must generate a collision event");
        world.Shutdown();
        std::cout << "[PASS] Test13_ContinuousCollisionDetection\n";
    }

    void Test14_CollisionAndTriggerEnterStayExitEvents()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        world.SetGravity({ 0.0f, 0.0f, 0.0f });
        Scene::Scene scene("EventsTest");

        const auto trigger = CreateBoxEntity(scene, "TriggerZone", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, Scene::RigidBodyMotionType::Static);
        scene.RigidBodies().TryGet(trigger)->IsTrigger = 1u;

        const auto mover = CreateBoxEntity(scene, "Mover", { -3.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
        scene.RigidBodies().TryGet(mover)->LinearDamping = 0.0f;
        world.SetLinearVelocity(scene, mover, { 4.0f, 0.0f, 0.0f });

        int enterCount = 0;
        int stayCount = 0;
        int exitCount = 0;

        for (int i = 0; i < 120; ++i)
        {
            world.StepSimulation(1.0f / 60.0f, scene);
            for (const auto& ev : world.GetRecentCollisions())
            {
                if (!ev.IsTrigger) continue;
                if (ev.EventType == Physics::ContactEventType::Enter) ++enterCount;
                else if (ev.EventType == Physics::ContactEventType::Stay) ++stayCount;
                else if (ev.EventType == Physics::ContactEventType::Exit) ++exitCount;
            }
        }

        EXPECT_TRUE(enterCount == 1, "Trigger Enter should fire exactly once, got " + std::to_string(enterCount));
        EXPECT_TRUE(stayCount > 0, "Trigger Stay should fire while overlapping, got " + std::to_string(stayCount));
        EXPECT_TRUE(exitCount == 1, "Trigger Exit should fire exactly once, got " + std::to_string(exitCount));

        // Verify sleeping does NOT fire spurious Exit
        world.SetGravity({ 0.0f, -9.81f, 0.0f });
        CreateBoxEntity(scene, "Floor", { 0.0f, -10.5f, 0.0f }, { 10.0f, 0.5f, 10.0f }, Scene::RigidBodyMotionType::Static);
        const auto sleeper = CreateBoxEntity(scene, "Sleeper", { 0.0f, -9.45f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
        int solidExitOnSleep = 0;
        for (int i = 0; i < 120; ++i)
        {
            world.StepSimulation(1.0f / 60.0f, scene);
            for (const auto& ev : world.GetRecentCollisions())
            {
                if (!ev.IsTrigger && ev.EventType == Physics::ContactEventType::Exit &&
                    (ev.EntityA == sleeper || ev.EntityB == sleeper))
                {
                    ++solidExitOnSleep;
                }
            }
        }
        EXPECT_TRUE(world.IsSleeping(scene, sleeper), "Sleeper should be asleep");
        EXPECT_TRUE(solidExitOnSleep == 0, "Falling asleep on floor must not emit spurious Exit event");
        world.Shutdown();
        std::cout << "[PASS] Test14_CollisionAndTriggerEnterStayExitEvents\n";
    }

    void Test15_ShapeCastAndOverlapQueries()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("QueriesTest");

        const auto e1 = CreateBoxEntity(scene, "Box1", { 0.0f, 0.0f, 5.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        const auto e2 = CreateBoxEntity(scene, "Box2", { 0.0f, 0.0f, 10.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        const auto e3Trig = CreateBoxEntity(scene, "Trig", { 0.0f, 0.0f, 3.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        scene.RigidBodies().TryGet(e3Trig)->IsTrigger = 1u;

        world.StepSimulation(1.0f / 60.0f, scene);

        std::vector<Physics::RaycastHit> allHits;
        Physics::QueryFilter filter{};
        filter.IncludeTriggers = false;
        const u32 count = world.RaycastAll(scene, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 20.0f, allHits, filter);
        EXPECT_TRUE(count == 2u && allHits[0].HitEntity == e1 && allHits[1].HitEntity == e2,
            "RaycastAll should skip trigger and return Box1 then Box2");

        Physics::RaycastHit castHit{};
        EXPECT_TRUE(world.SphereCast(scene, { 0.0f, 0.0f, 0.0f }, 0.3f, { 0.0f, 0.0f, 1.0f }, 20.0f, castHit, filter) && castHit.HitEntity == e1,
            "SphereCast should hit Box1");
        EXPECT_TRUE(world.BoxCast(scene, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f }, Math::Quat::Identity(), { 0.0f, 0.0f, 1.0f }, 20.0f, castHit, filter) && castHit.HitEntity == e1,
            "BoxCast should hit Box1");
        EXPECT_TRUE(world.CapsuleCast(scene, { 0.0f, 0.0f, 0.0f }, 0.2f, 0.3f, Math::Quat::Identity(), { 0.0f, 0.0f, 1.0f }, 20.0f, castHit, filter) && castHit.HitEntity == e1,
            "CapsuleCast should hit Box1");

        std::vector<Physics::OverlapHit> overlaps;
        const u32 ovSphere = world.OverlapSphere(scene, { 0.0f, 0.0f, 5.0f }, 1.0f, overlaps, filter);
        EXPECT_TRUE(ovSphere == 1u && overlaps[0].Entity == e1, "OverlapSphere should find Box1");
        const u32 ovBox = world.OverlapBox(scene, { 0.0f, 0.0f, 10.0f }, { 0.6f, 0.6f, 0.6f }, Math::Quat::Identity(), overlaps, filter);
        EXPECT_TRUE(ovBox == 1u && overlaps[0].Entity == e2, "OverlapBox should find Box2");

        filter.IncludeTriggers = true;
        const u32 ovCap = world.OverlapCapsule(scene, { 0.0f, 0.0f, 3.0f }, 0.5f, 0.5f, Math::Quat::Identity(), overlaps, filter);
        EXPECT_TRUE(ovCap == 1u && overlaps[0].Entity == e3Trig, "OverlapCapsule with triggers should find Trig");

        world.Shutdown();
        std::cout << "[PASS] Test15_ShapeCastAndOverlapQueries\n";
    }

    void Test16_JointsFixedDistanceHingeSlider()
    {
        Physics::PhysicsWorld world;
        world.Initialize();
        Scene::Scene scene("JointsTest");

        const auto anchor = CreateBoxEntity(scene, "Anchor", { 0.0f, 10.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        const auto bob = CreateBoxEntity(scene, "DistanceBob", { 2.0f, 10.0f, 0.0f }, { 0.3f, 0.3f, 0.3f }, Scene::RigidBodyMotionType::Dynamic);
        const auto hinged = CreateBoxEntity(scene, "Hinged", { -3.0f, 10.0f, 0.0f }, { 0.5f, 0.2f, 0.2f }, Scene::RigidBodyMotionType::Dynamic);
        const auto slider = CreateBoxEntity(scene, "Slider", { 5.0f, 10.0f, 0.0f }, { 0.3f, 0.3f, 0.3f }, Scene::RigidBodyMotionType::Dynamic);
        const auto fixed = CreateBoxEntity(scene, "Fixed", { 8.0f, 10.0f, 0.0f }, { 0.3f, 0.3f, 0.3f }, Scene::RigidBodyMotionType::Dynamic);

        const auto jDist = world.CreateJoint(scene, Physics::JointDesc{
            .Type = Physics::JointType::Distance,
            .EntityA = anchor,
            .EntityB = bob,
            .AutoDetectAnchors = true,
            .MinDistance = 2.0f,
            .MaxDistance = 2.0f
        });
        const auto jHinge = world.CreateJoint(scene, Physics::JointDesc{
            .Type = Physics::JointType::Hinge,
            .EntityA = hinged,
            .EntityB = Scene::EntityId::Invalid(),
            .AnchorA = { -3.5f, 10.0f, 0.0f },
            .Axis = { 0.0f, 0.0f, 1.0f },
            .MinLimit = -Math::kPi * 0.5f,
            .MaxLimit = Math::kPi * 0.5f
        });
        const auto jSlider = world.CreateJoint(scene, Physics::JointDesc{
            .Type = Physics::JointType::Slider,
            .EntityA = slider,
            .EntityB = Scene::EntityId::Invalid(),
            .AnchorA = { 5.0f, 10.0f, 0.0f },
            .Axis = { 1.0f, 0.0f, 0.0f },
            .AutoDetectAnchors = true,
            .MinLimit = -2.0f,
            .MaxLimit = 2.0f
        });
        const auto jFixed = world.CreateJoint(scene, Physics::JointDesc{
            .Type = Physics::JointType::Fixed,
            .EntityA = fixed,
            .EntityB = Scene::EntityId::Invalid(),
            .AutoDetectAnchors = true
        });

        EXPECT_TRUE(world.IsJointValid(jDist) && world.IsJointValid(jHinge) &&
            world.IsJointValid(jSlider) && world.IsJointValid(jFixed), "All 4 joints should be valid");

        StepWorld(world, scene, 1.0f);

        const Math::Vec3 bobPos = scene.Transforms().TryGet(bob)->Position;
        const f32 distFromAnchor = Math::Length(bobPos - Math::Vec3{ 0.0f, 10.0f, 0.0f });
        EXPECT_TRUE(std::abs(distFromAnchor - 2.0f) < 0.15f, "Distance joint should constrain bob to ~2.0m radius, got " + std::to_string(distFromAnchor));
        EXPECT_TRUE(std::abs(scene.Transforms().TryGet(fixed)->Position.y - 10.0f) < 0.05f, "Fixed joint should hold body at y=10m");
        EXPECT_TRUE(std::abs(scene.Transforms().TryGet(slider)->Position.y - 10.0f) < 0.05f, "Slider along X should prevent falling in Y");

        EXPECT_TRUE(world.DestroyJoint(jFixed), "DestroyJoint should succeed");
        EXPECT_TRUE(!world.IsJointValid(jFixed), "Destroyed joint should be invalid");
        world.Shutdown();
        std::cout << "[PASS] Test16_JointsFixedDistanceHingeSlider\n";
    }

    Scene::EntityId CreateCharacterEntity(
        Scene::Scene& scene,
        const std::string& name,
        const Math::Vec3& pos,
        f32 radius = 0.35f,
        f32 height = 1.8f)
    {
        Scene::EntityId id = scene.CreateEntity(name);
        if (auto* tr = scene.Transforms().TryGet(id))
        {
            tr->Position = pos;
        }
        Scene::CharacterControllerComponent cc{};
        cc.Radius = radius;
        cc.Height = height;
        cc.StepHeight = 0.35f;
        cc.MaxSlopeAngleDegrees = 45.0f;
        cc.Mass = 75.0f;
        cc.MaxPushStrength = 400.0f;
        cc.DirtyFlags = static_cast<u8>(Scene::CharacterControllerDirtyFlags::All);
        scene.CharacterControllers().EmplaceOrReplace(id, cc);
        return id;
    }

    void Test17_CharacterVirtualMovementStairsSlopesAndPlatforms()
    {
        Physics::PhysicsWorld world;
        EXPECT_TRUE(world.Initialize(), "Initialize world");

        Scene::Scene scene;
        // Main floor: top at Y = 0
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 80.0f, 0.5f, 80.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.8f);

        // Stair steps along +X starting at X=3: each step +0.25m high
        CreateBoxEntity(scene, "Step1", { 4.0f, 0.125f, 0.0f }, { 1.0f, 0.125f, 2.0f }, Scene::RigidBodyMotionType::Static);
        CreateBoxEntity(scene, "Step2", { 6.0f, 0.375f, 0.0f }, { 1.0f, 0.125f, 2.0f }, Scene::RigidBodyMotionType::Static);

        // Steep ramp (60 degrees) at Z = 10 to test slope rejection/sliding
        const Scene::EntityId steepWall = CreateBoxEntity(scene, "SteepSlope", { 0.0f, 2.0f, 10.0f }, { 3.0f, 0.25f, 4.0f }, Scene::RigidBodyMotionType::Static);
        if (auto* stTr = scene.Transforms().TryGet(steepWall))
        {
            stTr->Rotation = Math::FromEulerDegrees({ 60.0f, 0.0f, 0.0f });
        }

        // Gentle ramp (25 degrees) at Z = -10 to test walkable slope
        const Scene::EntityId gentleSlope = CreateBoxEntity(scene, "GentleSlope", { 0.0f, 0.6f, -10.0f }, { 3.0f, 0.25f, 4.0f }, Scene::RigidBodyMotionType::Static);
        if (auto* gnTr = scene.Transforms().TryGet(gentleSlope))
        {
            gnTr->Rotation = Math::FromEulerDegrees({ -25.0f, 0.0f, 0.0f });
        }

        // Dynamic crate at X = 0, Z = 4
        const Scene::EntityId pushCrate = CreateBoxEntity(scene, "PushCrate", { 0.0f, 0.5f, 4.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 2.0f, 0.0f, 0.3f);

        // Kinematic moving platform at X = 25, Y = 0.25 (top Y = 0.5)
        const Scene::EntityId platform = CreateBoxEntity(scene, "MovingPlatform", { 25.0f, 0.25f, 0.0f }, { 2.0f, 0.25f, 2.0f }, Scene::RigidBodyMotionType::Kinematic);

        // Character spawns at (0, 2.0, 0) -> falls under gravity onto floor
        const Scene::EntityId player = CreateCharacterEntity(scene, "Player", { 0.0f, 2.0f, 0.0f });
        world.RebuildFromScene(scene);

        Physics::CharacterControllerState initSt{};
        EXPECT_TRUE(world.GetCharacterState(scene, player, initSt), "GetCharacterState valid");
        EXPECT_TRUE(!initSt.IsGrounded, "Character starts in air at Y=2");

        // Land on floor
        StepWorld(world, scene, 1.0f);
        Physics::CharacterControllerState landedSt{};
        EXPECT_TRUE(world.GetCharacterState(scene, player, landedSt), "GetCharacterState after fall");
        EXPECT_TRUE(landedSt.IsGrounded, "Character should land on floor");
        EXPECT_TRUE(std::abs(scene.Transforms().TryGet(player)->Position.y) < 0.08f, "Character feet should rest at Y~0");

        // Test Jump + Apex + Re-land
        EXPECT_TRUE(world.JumpCharacter(scene, player, 6.5f), "JumpCharacter");
        f32 maxY = 0.0f;
        for (int i = 0; i < 90; ++i)
        {
            world.StepSimulation(1.0f / 60.0f, scene);
            maxY = std::max(maxY, scene.Transforms().TryGet(player)->Position.y);
        }
        EXPECT_TRUE(maxY > 1.3f, "Jump should reach apex > 1.3m, got " + std::to_string(maxY));
        Physics::CharacterControllerState postJumpSt{};
        EXPECT_TRUE(world.GetCharacterState(scene, player, postJumpSt), "GetCharacterState post jump");
        EXPECT_TRUE(postJumpSt.IsGrounded, "Character should re-land cleanly after jump");

        // Walk up stairs (+X from X=0 to X=5.95m onto top of Step2)
        for (int i = 0; i < 85; ++i)
        {
            world.MoveCharacter(scene, player, { 0.07f, 0.0f, 0.0f });
            world.StepSimulation(1.0f / 60.0f, scene);
        }
        const f32 stairY = scene.Transforms().TryGet(player)->Position.y;
        EXPECT_TRUE(stairY > 0.42f, "Character should step up both stairs to Y~0.5m, got " + std::to_string(stairY));

        // Teleport behind crate and push it along +Z
        EXPECT_TRUE(world.TeleportCharacter(scene, player, { 0.0f, 0.02f, 2.5f }), "TeleportCharacter near crate");
        const f32 crateZBefore = scene.Transforms().TryGet(pushCrate)->Position.z;
        for (int i = 0; i < 45; ++i)
        {
            world.MoveCharacter(scene, player, { 0.0f, 0.0f, 0.06f });
            world.StepSimulation(1.0f / 60.0f, scene);
        }
        const f32 crateZAfter = scene.Transforms().TryGet(pushCrate)->Position.z;
        EXPECT_TRUE(crateZAfter > crateZBefore + 0.5f, "CharacterVirtual should push dynamic crate forward");

        // Moving platform ride-along test
        world.TeleportCharacter(scene, player, { 25.0f, 0.55f, 0.0f });
        StepWorld(world, scene, 0.25f);
        const f32 charXBeforePlat = scene.Transforms().TryGet(player)->Position.x;
        for (int i = 0; i < 60; ++i)
        {
            if (auto* platTr = scene.Transforms().TryGet(platform))
            {
                platTr->Position.x += 0.05f;
            }
            world.StepSimulation(1.0f / 60.0f, scene);
        }
        const f32 charXAfterPlat = scene.Transforms().TryGet(player)->Position.x;
        EXPECT_TRUE(charXAfterPlat > charXBeforePlat + 2.0f,
            "Character standing on moving kinematic platform should ride along (+X > 2.0m), moved " +
            std::to_string(charXAfterPlat - charXBeforePlat));

        // Verify character raycastable via inner body!
        Physics::RaycastHit charHit{};
        const Math::Vec3 pNow = scene.Transforms().TryGet(player)->Position;
        EXPECT_TRUE(world.Raycast(scene, { pNow.x - 3.0f, pNow.y + 0.9f, pNow.z }, { 1.0f, 0.0f, 0.0f }, 6.0f, charHit),
            "Raycast should detect CharacterVirtual via inner body");
        EXPECT_TRUE(charHit.HitEntity == player, "Raycast hit entity should be Player");

        world.Shutdown();
        std::cout << "[PASS] Test17_CharacterVirtualMovementStairsSlopesAndPlatforms\n";
    }

    void Test18_CharacterControllerSerializationAndCleanup()
    {
        Physics::PhysicsWorld world;
        EXPECT_TRUE(world.Initialize(), "Initialize world");

        Scene::Scene scene;
        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) { world.DestroyBody(eid); });
        scene.SetSceneClearedCallback([&]() { world.ClearAllBodies(nullptr); });

        const Scene::EntityId ch = CreateCharacterEntity(scene, "SerializedHero", { 3.0f, 1.0f, -2.0f }, 0.4f, 1.9f);
        auto* cc = scene.CharacterControllers().TryGet(ch);
        cc->StepHeight = 0.45f;
        cc->MaxSlopeAngleDegrees = 52.0f;
        cc->MaxPushStrength = 350.0f;
        world.RebuildFromScene(scene);
        EXPECT_TRUE(world.GetStats().CharacterCount == 1u, "Should have 1 active CharacterVirtual");

        const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "morbi_char_test.mscene.json";
        EXPECT_TRUE(scene.SaveToJsonFile(tempPath), "Save scene with character_controller");

        scene.Clear();
        EXPECT_TRUE(world.GetStats().CharacterCount == 0u, "ClearAllBodies should clear characters");

        Scene::Scene loaded;
        EXPECT_TRUE(loaded.LoadFromJsonFile(tempPath), "Load scene with character_controller");
        const Scene::EntityId loadedHero = loaded.FindEntityByName("SerializedHero");
        EXPECT_TRUE(loadedHero.IsValid(), "Loaded Hero exists");
        const auto* loadedCC = loaded.CharacterControllers().TryGet(loadedHero);
        EXPECT_TRUE(loadedCC != nullptr, "Loaded Hero has CharacterControllerComponent");
        if (loadedCC)
        {
            EXPECT_TRUE(std::abs(loadedCC->Radius - 0.4f) < 1e-4f && std::abs(loadedCC->Height - 1.9f) < 1e-4f &&
                std::abs(loadedCC->StepHeight - 0.45f) < 1e-4f && std::abs(loadedCC->MaxSlopeAngleDegrees - 52.0f) < 1e-3f,
                "Loaded CharacterController fields match");
        }

        world.RebuildFromScene(loaded);
        EXPECT_TRUE(world.GetStats().CharacterCount == 1u, "RebuildFromScene recreates character");
        loaded.DestroyEntity(loadedHero);
        world.DestroyCharacter(loadedHero);
        EXPECT_TRUE(world.GetStats().CharacterCount == 0u, "DestroyCharacter removes character cleanly");

        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        world.Shutdown();
        std::cout << "[PASS] Test18_CharacterControllerSerializationAndCleanup\n";
    }

    struct MemorySample
    {
        f64 WorkingSetMB = 0.0;
        f64 PrivateCommitMB = 0.0;
    };

    MemorySample GetProcessMemoryMB()
    {
        MemorySample sample{};
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        {
            sample.WorkingSetMB = static_cast<f64>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            sample.PrivateCommitMB = static_cast<f64>(pmc.PrivateUsage) / (1024.0 * 1024.0);
        }
#endif
        return sample;
    }

    void PrintBenchResult(
        const char* label,
        const Physics::PhysicsStats& stats,
        f64 simSeconds,
        int steps,
        f64 totalStepMs,
        f64 maxStepMs,
        const MemorySample& memBefore,
        const MemorySample& memAfter)
    {
        const f64 avgStepMs = (steps > 0) ? (totalStepMs / static_cast<f64>(steps)) : 0.0;
        const f64 deltaCommit = memAfter.PrivateCommitMB - memBefore.PrivateCommitMB;
        std::cout << std::fixed << std::setprecision(2)
                  << "  [BENCH] " << label
                  << " | Bodies=" << stats.TotalJoltBodies
                  << " (Dyn=" << stats.DynamicBodyCount
                  << ", Active=" << stats.ActiveBodyCount
                  << ", Sleep=" << stats.SleepingBodyCount
                  << ", Chars=" << stats.CharacterCount << ")"
                  << " | Dur=" << simSeconds << "s (" << steps << " steps)"
                  << " | Avg=" << avgStepMs << " ms | Max=" << maxStepMs << " ms"
                  << " | WS=" << memAfter.WorkingSetMB << " MB"
                  << " | Commit=" << memAfter.PrivateCommitMB << " MB (d=" << std::showpos << deltaCommit << std::noshowpos << " MB)\n";
    }

    u32 CountNonFiniteOrFallen(const Scene::Scene& scene, f32 minY = -2.0f)
    {
        u32 bad = 0;
        for (const auto& tr : scene.Transforms().RawComponents())
        {
            if (!std::isfinite(tr.Position.x) || !std::isfinite(tr.Position.y) || !std::isfinite(tr.Position.z) || tr.Position.y < minY)
            {
                ++bad;
            }
        }
        return bad;
    }

    void RunGridDropBenchmark(const char* label, u32 dynamicCount, f32 simSeconds)
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Benchmark world init");

        Scene::Scene scene(label);
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 100.0f, 0.5f, 100.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.7f);

        const u32 gridX = 10;
        const u32 gridZ = 10;
        for (u32 i = 0; i < dynamicCount; ++i)
        {
            const u32 gx = i % gridX;
            const u32 gz = (i / gridX) % gridZ;
            const u32 gy = i / (gridX * gridZ);
            const f32 x = (static_cast<f32>(gx) - 4.5f) * 1.35f;
            const f32 y = 1.2f + static_cast<f32>(gy) * 1.25f;
            const f32 z = (static_cast<f32>(gz) - 4.5f) * 1.35f;
            const Scene::EntityId id = CreateBoxEntity(scene, "Body_" + std::to_string(i), { x, y, z }, { 0.45f, 0.45f, 0.45f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.6f);
            if (i % 3 == 1)
            {
                scene.RigidBodies().TryGet(id)->Shape = Scene::ColliderShapeType::Sphere;
            }
        }

        world.RebuildFromScene(scene);

        const int steps = static_cast<int>(std::round(simSeconds * 60.0f));
        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            const auto t0 = std::chrono::steady_clock::now();
            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.TotalJoltBodies == dynamicCount + 1u, std::string(label) + ": all bodies in Jolt");
        EXPECT_TRUE(CountNonFiniteOrFallen(scene) == 0u, std::string(label) + ": zero NaN or fallen bodies");
        PrintBenchResult(label, stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void RunDenseCollisionPitBenchmark()
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Dense pit init");

        Scene::Scene scene("DenseCollisionPit");
        // Enclosed 8m x 8m pit (floor + 4 walls)
        CreateBoxEntity(scene, "PitFloor", { 0.0f, -0.5f, 0.0f }, { 5.0f, 0.5f, 5.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.1f, 0.6f);
        CreateBoxEntity(scene, "WallN", { 0.0f, 4.0f, -4.5f }, { 5.0f, 4.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        CreateBoxEntity(scene, "WallS", { 0.0f, 4.0f, 4.5f }, { 5.0f, 4.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        CreateBoxEntity(scene, "WallE", { 4.5f, 4.0f, 0.0f }, { 0.5f, 4.5f, 5.0f }, Scene::RigidBodyMotionType::Static);
        CreateBoxEntity(scene, "WallW", { -4.5f, 4.0f, 0.0f }, { 0.5f, 4.5f, 5.0f }, Scene::RigidBodyMotionType::Static);

        constexpr u32 kCount = 500;
        for (u32 i = 0; i < kCount; ++i)
        {
            const u32 gx = i % 7;
            const u32 gz = (i / 7) % 7;
            const u32 gy = i / 49;
            const f32 x = (static_cast<f32>(gx) - 3.0f) * 1.02f;
            const f32 y = 0.6f + static_cast<f32>(gy) * 0.92f;
            const f32 z = (static_cast<f32>(gz) - 3.0f) * 1.02f;
            const Scene::EntityId id = CreateBoxEntity(scene, "PitBody_" + std::to_string(i), { x, y, z }, { 0.42f, 0.42f, 0.42f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.15f, 0.5f);
            if (i % 2 == 1)
            {
                scene.RigidBodies().TryGet(id)->Shape = Scene::ColliderShapeType::Sphere;
            }
        }
        world.RebuildFromScene(scene);

        constexpr f32 simSeconds = 3.0f;
        const int steps = 180;
        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            const auto t0 = std::chrono::steady_clock::now();
            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.DynamicBodyCount == kCount, "Dense pit dynamic count");
        EXPECT_TRUE(CountNonFiniteOrFallen(scene) == 0u, "Dense pit zero NaN/fallen");
        PrintBenchResult("500 Dense Collision Pit", stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void RunManySleepingBenchmark()
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Sleeping bench init");

        Scene::Scene scene("SleepingBench");
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 120.0f, 0.5f, 120.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.0f, 0.8f);

        constexpr u32 kSleepingCount = 1000;
        std::vector<Scene::EntityId> sleepingIds;
        sleepingIds.reserve(kSleepingCount);
        for (u32 i = 0; i < kSleepingCount; ++i)
        {
            const u32 gx = i % 32;
            const u32 gz = i / 32;
            const f32 x = (static_cast<f32>(gx) - 16.0f) * 2.0f;
            const f32 z = (static_cast<f32>(gz) - 16.0f) * 2.0f;
            const Scene::EntityId id = CreateBoxEntity(scene, "Sleep_" + std::to_string(i), { x, 0.45f, z }, { 0.45f, 0.45f, 0.45f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.0f, 0.8f);
            if (auto* rb = scene.RigidBodies().TryGet(id))
            {
                rb->LinearDamping = 0.25f;
                rb->AngularDamping = 0.25f;
            }
            sleepingIds.push_back(id);
        }
        world.RebuildFromScene(scene);
        // Settle & sleep warmup (outside measured window)
        StepWorld(world, scene, 3.0f);
        for (const auto eid : sleepingIds)
        {
            world.Sleep(scene, eid);
        }
        // Add 1 active rolling sphere well clear of sleeping boxes
        const Scene::EntityId activeOrb = CreateBoxEntity(scene, "ActiveOrb", { -50.0f, 0.5f, -50.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.2f, 0.1f);
        scene.RigidBodies().TryGet(activeOrb)->Shape = Scene::ColliderShapeType::Sphere;
        world.StepSimulation(1.0f / 60.0f, scene);
        world.SetLinearVelocity(scene, activeOrb, { 12.0f, 0.0f, 0.0f });

        constexpr f32 simSeconds = 2.0f;
        const int steps = 120;
        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            const auto t0 = std::chrono::steady_clock::now();
            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.SleepingBodyCount >= 950u, "Sleeping workload should have >=950 sleeping bodies");
        PrintBenchResult("1000 Sleeping + 1 Active", stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void RunContinuouslyActiveBenchmark()
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Continuous active init");

        Scene::Scene scene("ActiveBench");
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 100.0f, 0.5f, 100.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.98f, 0.2f);

        constexpr u32 kCount = 500;
        std::vector<Scene::EntityId> bodies;
        bodies.reserve(kCount);
        for (u32 i = 0; i < kCount; ++i)
        {
            const u32 gx = i % 25;
            const u32 gz = i / 25;
            const f32 x = (static_cast<f32>(gx) - 12.0f) * 2.0f;
            const f32 z = (static_cast<f32>(gz) - 10.0f) * 2.0f;
            const Scene::EntityId id = CreateBoxEntity(scene, "Active_" + std::to_string(i), { x, 2.0f + static_cast<f32>(i % 4), z }, { 0.45f, 0.45f, 0.45f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.98f, 0.1f);
            auto* rb = scene.RigidBodies().TryGet(id);
            rb->Shape = Scene::ColliderShapeType::Sphere;
            rb->LinearDamping = 0.0f;
            rb->AngularDamping = 0.0f;
            bodies.push_back(id);
        }
        world.RebuildFromScene(scene);

        constexpr f32 simSeconds = 2.0f;
        const int steps = 120;
        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            if (s % 45 == 0)
            {
                for (u32 i = 0; i < kCount; ++i)
                {
                    world.AddLinearImpulse(scene, bodies[i], { 0.0f, 4.5f, 0.0f });
                }
            }
            const auto t0 = std::chrono::steady_clock::now();
            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.ActiveBodyCount == kCount, "All 500 bodies remain active");
        EXPECT_TRUE(CountNonFiniteOrFallen(scene) == 0u, "Continuous active zero NaN/fallen");
        PrintBenchResult("500 Continuously Active", stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void RunMixedShapesBenchmark()
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Mixed shapes init");

        Scene::Scene scene("MixedShapes");
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 100.0f, 0.5f, 100.0f }, Scene::RigidBodyMotionType::Static);

        constexpr u32 kCount = 500;
        for (u32 i = 0; i < kCount; ++i)
        {
            const u32 gx = i % 10;
            const u32 gz = (i / 10) % 10;
            const u32 gy = i / 100;
            const f32 x = (static_cast<f32>(gx) - 4.5f) * 1.5f;
            const f32 y = 1.5f + static_cast<f32>(gy) * 1.6f;
            const f32 z = (static_cast<f32>(gz) - 4.5f) * 1.5f;
            const Scene::EntityId id = CreateBoxEntity(scene, "Mix_" + std::to_string(i), { x, y, z }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic);
            auto* rb = scene.RigidBodies().TryGet(id);
            if (i % 3 == 1)
            {
                rb->Shape = Scene::ColliderShapeType::Sphere;
                rb->HalfExtents.x = 0.4f;
            }
            else if (i % 3 == 2)
            {
                rb->Shape = Scene::ColliderShapeType::Capsule;
                rb->HalfExtents = { 0.3f, 0.6f, 0.3f };
            }
        }
        world.RebuildFromScene(scene);

        constexpr f32 simSeconds = 2.0f;
        const int steps = 120;
        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            const auto t0 = std::chrono::steady_clock::now();
            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.DynamicBodyCount == kCount, "Mixed shapes count");
        EXPECT_TRUE(CountNonFiniteOrFallen(scene) == 0u, "Mixed shapes zero NaN/fallen");
        PrintBenchResult("500 Mixed Box/Sphere/Capsule", stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void RunMultipleCharactersBenchmark()
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Multi character init");

        Scene::Scene scene("MultiCharBench");
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 80.0f, 0.5f, 80.0f }, Scene::RigidBodyMotionType::Static);
        // Stairs & dynamic crates for characters to interact with
        for (int i = 0; i < 4; ++i)
        {
            CreateBoxEntity(scene, "Step_" + std::to_string(i), { 4.0f + static_cast<f32>(i) * 0.8f, 0.125f * static_cast<f32>(i + 1), 0.0f }, { 0.4f, 0.125f, 20.0f }, Scene::RigidBodyMotionType::Static);
        }
        for (int i = 0; i < 20; ++i)
        {
            CreateBoxEntity(scene, "Crate_" + std::to_string(i), { 2.0f, 0.5f, (static_cast<f32>(i) - 10.0f) * 2.0f }, { 0.45f, 0.45f, 0.45f }, Scene::RigidBodyMotionType::Dynamic, 5.0f);
        }

        constexpr u32 kCharCount = 25;
        std::vector<Scene::EntityId> chars;
        chars.reserve(kCharCount);
        for (u32 i = 0; i < kCharCount; ++i)
        {
            const Scene::EntityId cid = scene.CreateEntity("Char_" + std::to_string(i));
            scene.Transforms().TryGet(cid)->Position = { -2.0f - static_cast<f32>(i % 5) * 2.0f, 1.2f, (static_cast<f32>(i) - 12.0f) * 1.5f };
            Scene::CharacterControllerComponent cc{};
            cc.Radius = 0.35f;
            cc.Height = 1.8f;
            cc.StepHeight = 0.35f;
            scene.CharacterControllers().EmplaceOrReplace(cid, cc);
            chars.push_back(cid);
        }
        world.RebuildFromScene(scene);

        constexpr f32 simSeconds = 2.0f;
        const int steps = 120;
        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            for (const auto cid : chars)
            {
                world.MoveCharacter(scene, cid, { 4.5f / 60.0f, 0.0f, 0.0f });
            }
            const auto t0 = std::chrono::steady_clock::now();
            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.CharacterCount == kCharCount, "25 CharacterVirtual instances registered");
        EXPECT_TRUE(CountNonFiniteOrFallen(scene) == 0u, "Characters zero NaN/fallen");
        PrintBenchResult("25 CharacterVirtual + Crates", stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void RunFrequentCreateDestroyChurnBenchmark()
    {
        const MemorySample memBefore = GetProcessMemoryMB();
        Physics::PhysicsWorld world(1.0f / 60.0f);
        EXPECT_TRUE(world.Initialize(), "Churn bench init");

        Scene::Scene scene("ChurnBench");
        CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 60.0f, 0.5f, 60.0f }, Scene::RigidBodyMotionType::Static);
        world.StepSimulation(1.0f / 60.0f, scene);

        constexpr u32 kBatchSize = 50;
        constexpr int steps = 180;
        constexpr f32 simSeconds = 3.0f;
        std::vector<Scene::EntityId> activeBatch;
        activeBatch.reserve(kBatchSize);

        f64 totalStepMs = 0.0;
        f64 maxStepMs = 0.0;
        for (int s = 0; s < steps; ++s)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (const auto oldId : activeBatch)
            {
                world.DestroyBody(oldId);
                scene.DestroyEntity(oldId);
            }
            activeBatch.clear();

            for (u32 i = 0; i < kBatchSize; ++i)
            {
                const f32 x = (static_cast<f32>(i % 10) - 4.5f) * 2.0f;
                const f32 z = (static_cast<f32>(i / 10) - 2.0f) * 2.0f;
                const Scene::EntityId eid = CreateBoxEntity(scene, "Churn_" + std::to_string(s) + "_" + std::to_string(i), { x, 3.0f, z }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic);
                world.SyncEntityBody(scene, eid);
                activeBatch.push_back(eid);
            }

            world.StepSimulation(1.0f / 60.0f, scene);
            const auto t1 = std::chrono::steady_clock::now();
            const f64 ms = std::chrono::duration<f64, std::milli>(t1 - t0).count();
            totalStepMs += ms;
            maxStepMs = std::max(maxStepMs, ms);
        }

        const Physics::PhysicsStats stats = world.GetStats();
        const MemorySample memAfter = GetProcessMemoryMB();
        EXPECT_TRUE(stats.DynamicBodyCount == kBatchSize, "Churn end dynamic body count exact");
        EXPECT_TRUE(CountNonFiniteOrFallen(scene) == 0u, "Churn zero NaN/fallen");
        PrintBenchResult("Create/Destroy Churn (50/step)", stats, simSeconds, steps, totalStepMs, maxStepMs, memBefore, memAfter);
        world.Shutdown();
    }

    void Test19_PerformanceWorkloadsSuite()
    {
        RunGridDropBenchmark("100 Bodies (Grid Drop)", 100u, 3.0f);
        RunGridDropBenchmark("500 Bodies (Grid Drop)", 500u, 3.0f);
        RunGridDropBenchmark("1000 Bodies (Grid Drop)", 1000u, 3.0f);
        RunDenseCollisionPitBenchmark();
        RunManySleepingBenchmark();
        RunContinuouslyActiveBenchmark();
        RunMixedShapesBenchmark();
        RunMultipleCharactersBenchmark();
        RunFrequentCreateDestroyChurnBenchmark();
        std::cout << "[PASS] Test19_PerformanceWorkloadsSuite\n";
    }

    void Test20_EditorInspectorThreadSafeCollisionsAndInvalidRefs()
    {
        // 1. Concurrent thread-safe GetRecentCollisions() & CopyRecentCollisions() test
        {
            Physics::PhysicsWorld world(1.0f / 60.0f);
            EXPECT_TRUE(world.Initialize(), "Test20 concurrent physics init");
            Scene::Scene scene("ConcurrentCollisions");
            CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, Scene::RigidBodyMotionType::Static, 1.0f, 0.8f);
            for (int i = 0; i < 8; ++i)
            {
                const auto b = CreateBoxEntity(scene, "Drop_" + std::to_string(i), { static_cast<f32>(i - 4) * 1.2f, 0.38f, 0.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic, 1.0f, 0.9f);
                scene.RigidBodies().TryGet(b)->LinearDamping = 0.0f;
                scene.RigidBodies().TryGet(b)->GravityFactor = 1.0f;
            }
            world.RebuildFromScene(scene);

            std::atomic<bool> readerStarted{ false };
            std::atomic<bool> stopReader{ false };
            std::atomic<u32> readerObservedEvents{ 0u };
            std::atomic<u32> invalidEventCount{ 0u };
            std::thread reader([&]() {
                std::vector<Physics::CollisionPairEvent> scratch;
                readerStarted.store(true, std::memory_order_release);
                while (!stopReader.load(std::memory_order_acquire))
                {
                    const auto snap = world.GetRecentCollisions();
                    world.CopyRecentCollisions(scratch);
                    if (snap.size() == scratch.size())
                    {
                        for (const auto& ev : scratch)
                        {
                            if (!ev.EntityA.IsValid() || !ev.EntityB.IsValid())
                            {
                                ++invalidEventCount;
                            }
                            readerObservedEvents.fetch_add(1u, std::memory_order_relaxed);
                        }
                    }
                    std::this_thread::yield();
                }
            });

            while (!readerStarted.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int s = 0; s < 240; ++s)
            {
                world.StepSimulation(1.0f / 60.0f, scene);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            stopReader.store(true, std::memory_order_release);
            reader.join();

            EXPECT_TRUE(invalidEventCount.load() == 0u, "Concurrent GetRecentCollisions snapshots had 0 invalid handles");
            EXPECT_TRUE(readerObservedEvents.load() > 0u, "Concurrent reader safely observed live collision snapshots");
            world.Shutdown();
        }

        // 2. Real Editor Inspector JSON IPC patch -> Scene -> PhysicsWorld sync
        {
            Physics::PhysicsWorld world(1.0f / 60.0f);
            EXPECT_TRUE(world.Initialize(), "Test20 Editor IPC physics init");
            Scene::Scene scene("EditorInspectorSync");
            const auto floor = CreateBoxEntity(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 40.0f, 0.5f, 40.0f }, Scene::RigidBodyMotionType::Static);
            const auto box = CreateBoxEntity(scene, "InspectedBox", { 0.0f, 2.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f);
            const auto anchor = CreateBoxEntity(scene, "InspectedAnchor", { -1.2f, 2.0f, 0.0f }, { 0.2f, 0.2f, 0.2f }, Scene::RigidBodyMotionType::Static);
            world.RebuildFromScene(scene);

            UI::WebView2Compositor compositor;

            auto flushPhysicsSync = [&]() {
                Scene::EntityId syncEnt = Scene::EntityId::Invalid();
                while (compositor.ConsumePhysicsSyncEntity(syncEnt))
                {
                    world.SyncEntityCompoundColliders(scene, syncEnt);
                    world.SyncEntityBody(scene, syncEnt);
                }
                world.SyncSceneJoints(scene);
                compositor.SyncSceneData(scene, "EditorInspectorSync");
            };

            // Edit RigidBody: enable UseDensity=true, Density=4000, LockedAxes=56 (Rot X/Y/Z), ColliderOffset=[0,1,0], ColliderRotationEuler=[0,45,0]
            const std::string rbPatch =
                "{\"type\":\"update_physics_component\",\"entityId\":" + std::to_string(box.Index) +
                ",\"componentKind\":\"rigidbody\",\"useDensity\":true,\"density\":4000.0,\"lockedAxes\":56"
                ",\"colliderOffset\":[0.0,1.0,0.0],\"colliderRotationEuler\":[0.0,45.0,0.0]"
                ",\"collisionLayer\":2,\"collisionMask\":6}";
            EXPECT_TRUE(compositor.ProcessFrontendJsonMessage(rbPatch, scene), "Process RigidBody Inspector JSON patch");
            flushPhysicsSync();

            const auto* rb = scene.RigidBodies().TryGet(box);
            EXPECT_TRUE(rb != nullptr && rb->UseDensity == 1u && std::abs(rb->Density - 4000.0f) < 1e-3f && rb->LockedAxes == 56u,
                "Inspector JSON patch updated UseDensity, Density, and LockedAxes on Scene");

            // Add Compound sub-shape via Editor JSON patch at LocalOffset=[2.0, 0.0, 0.0]
            const std::string ccAddPatch =
                "{\"type\":\"update_physics_component\",\"entityId\":" + std::to_string(box.Index) +
                ",\"componentKind\":\"compoundColliders\",\"action\":\"add\",\"shape\":\"Box\",\"halfExtents\":[0.4,0.4,0.4]"
                ",\"localOffset\":[2.0,0.0,0.0],\"localRotationEuler\":[0.0,0.0,0.0],\"collisionLayer\":1,\"collisionMask\":4294967295,\"isTrigger\":false}";
            EXPECT_TRUE(compositor.ProcessFrontendJsonMessage(ccAddPatch, scene), "Process CompoundCollider add JSON patch");
            flushPhysicsSync();

            Physics::RaycastHit ccHit{};
            EXPECT_TRUE(world.Raycast(scene, { 2.0f, 8.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 15.0f, ccHit) && ccHit.HitEntity == box,
                "Jolt Raycast hits Editor-added Compound sub-shape at offset X=2.0");

            // Update and then verify SyncSceneData contains all Inspector fields
            const std::string jtAddPatch =
                "{\"type\":\"update_physics_component\",\"entityId\":" + std::to_string(box.Index) +
                ",\"componentKind\":\"physicsJoints\",\"action\":\"add\",\"jointType\":\"Hinge\",\"connectedEntityId\":" + std::to_string(anchor.Index) +
                ",\"anchorA\":[-0.8,2.0,0.0],\"axis\":[0.0,1.0,0.0],\"autoDetectAnchors\":true,\"minLimit\":-1.5,\"maxLimit\":1.5,\"enabled\":true}";
            EXPECT_TRUE(compositor.ProcessFrontendJsonMessage(jtAddPatch, scene), "Process PhysicsJoint add JSON patch");
            flushPhysicsSync();
            EXPECT_TRUE(world.GetStats().ActiveJointCount == 1u, "Editor-added Hinge joint active in Jolt");

            const std::string& cachedSync = compositor.GetCachedSceneSyncJson();
            EXPECT_TRUE(cachedSync.find("\"useDensity\":true") != std::string::npos &&
                        cachedSync.find("\"lockedAxes\":56") != std::string::npos &&
                        cachedSync.find("\"colliderRotationEuler\"") != std::string::npos &&
                        cachedSync.find("\"compoundColliders\"") != std::string::npos &&
                        cachedSync.find("\"physicsJoints\"") != std::string::npos,
                "SyncSceneData emits complete RigidBody, CompoundColliders & PhysicsJoints payload for UI");

            // Verify LockedAxes=56 blocks angular rotation under off-center impulse & UseDensity keeps heavy mass
            world.AddForceAtPosition(scene, box, { 0.0f, 0.0f, 40.0f }, { 2.0f, 2.0f, 0.0f }, Physics::ForceMode::Impulse);
            world.StepSimulation(1.0f / 60.0f, scene);
            Math::Vec3 angVel{};
            EXPECT_TRUE(world.GetAngularVelocity(scene, box, angVel) && Math::Length(angVel) < 1e-3f,
                "LockedAxes=56 (Rot X/Y/Z locked) prevented angular rotation under off-center impulse");
            EXPECT_TRUE(compositor.ProcessFrontendJsonMessage(
                "{\"type\":\"update_physics_component\",\"entityId\":" + std::to_string(box.Index) +
                ",\"componentKind\":\"physicsJoints\",\"action\":\"remove\",\"index\":0}", scene), "Remove joint via IPC");
            flushPhysicsSync();
            EXPECT_TRUE(world.GetStats().ActiveJointCount == 0u, "Removed Inspector joint cleaned from Jolt");
            (void)floor;
            world.Shutdown();
        }

        // 3. Serialization: no runtime handles written, invalid joint connected_entity rejected cleanly
        {
            Scene::Scene src("SerializationSafety");
            const auto a = CreateBoxEntity(src, "BoxA", { 0, 2, 0 }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
            const auto b = CreateBoxEntity(src, "BoxB", { 2, 2, 0 }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
            src.AddCompoundCollider(a, Scene::CompoundColliderEntry{
                .Shape = Scene::ColliderShapeType::Sphere,
                .HalfExtents = { 0.3f, 0.3f, 0.3f },
                .LocalOffset = { 0.5f, 0.0f, 0.0f }
            });
            src.AddEntityJoint(a, Scene::PhysicsJointEntry{
                .Type = Scene::PhysicsJointType::Fixed,
                .ConnectedEntity = b,
                .AutoDetectAnchors = true,
                .RuntimeJointHandle = 0x123456789ull
            });

            const std::filesystem::path tmpFile = std::filesystem::temp_directory_path() / "morbi_test20_safety.mscene.json";
            EXPECT_TRUE(src.SaveToJsonFile(tmpFile), "Save test20 serialization scene");

            std::ifstream in(tmpFile);
            const std::string savedStr((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            EXPECT_TRUE(savedStr.find("runtime_joint_handle") == std::string::npos &&
                        savedStr.find("jolt_body_id") == std::string::npos,
                "Serialized scene must not persist runtime handles or Jolt body IDs");

            // Inject a malformed joint referencing a missing entity and reload
            const size_t pos = savedStr.find("\"BoxB\"");
            EXPECT_TRUE(pos != std::string::npos, "Found valid BoxB joint target in JSON");
            std::string corrupted = savedStr;
            corrupted.replace(pos, 6, "\"MissingGhostEntity\"");
            {
                std::ofstream out(tmpFile);
                out << corrupted;
            }

            Scene::Scene loaded;
            EXPECT_TRUE(loaded.LoadFromJsonFile(tmpFile), "Load scene with invalid connected_entity reference");
            const auto loadedA = loaded.FindEntityByName("BoxA");
            EXPECT_TRUE(loadedA.IsValid(), "BoxA exists");
            EXPECT_TRUE(loaded.GetCompoundColliders(loadedA).size() == 1u, "BoxA compound collider preserved");
            EXPECT_TRUE(loaded.GetEntityJoints(loadedA).empty(), "Invalid connected_entity joint reference must be rejected safely, not converted to World joint");

            std::error_code ec;
            std::filesystem::remove(tmpFile, ec);
        }

        std::cout << "[PASS] Test20_EditorInspectorThreadSafeCollisionsAndInvalidRefs\n";
    }
}

int main()
{
    Core::Log::SetMinLevel(Core::LogLevel::Warn);
    Test1_JoltInitAndShutdown();
    Test2_BoxDropAndRest();
    Test3_HorizontalMomentumAndMassRatio();
    Test4_SphereRestitutionAndCapsuleRotation();
    Test5_SlopeFrictionAndOBB();
    Test6_KinematicPushesDynamic();
    Test7_OffsetCompoundAndRaycast();
    Test8_EntityDestructionAndSceneLifecycle();
    Test9_SceneSerializationRoundTrip();
    Test10_RuntimeComponentSyncAndMultiSubstepEvents();
    Test11_30BoxStackingStability();
    Test12_CollisionFilteringAndLayerMatrix();
    Test13_ContinuousCollisionDetection();
    Test14_CollisionAndTriggerEnterStayExitEvents();
    Test15_ShapeCastAndOverlapQueries();
    Test16_JointsFixedDistanceHingeSlider();
    Test17_CharacterVirtualMovementStairsSlopesAndPlatforms();
    Test18_CharacterControllerSerializationAndCleanup();
    Test19_PerformanceWorkloadsSuite();
    Test20_EditorInspectorThreadSafeCollisionsAndInvalidRefs();

    if (g_failures > 0)
    {
        std::cerr << "PhysicsTests FAILED with " << g_failures << " failure(s).\n";
        return 1;
    }
    std::cout << "ALL PHASE 1-5 JOLT PHYSICS TESTS PASSED!\n";
    return 0;
}
