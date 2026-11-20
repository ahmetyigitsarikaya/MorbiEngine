#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Psapi.h>

#include "Morbi/Audio/AudioWorld.hpp"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/StringId.hpp"
#include "Morbi/Physics/PhysicsWorld.hpp"
#include "Morbi/Scene/Scene.hpp"
#include "Morbi/Script/ScriptEngine.hpp"

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

    std::filesystem::path GetExecutableDir()
    {
        wchar_t buf[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return (len > 0 && len < MAX_PATH) ? std::filesystem::path(buf).parent_path() : std::filesystem::current_path();
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << text;
        return ofs.good();
    }

    Scene::EntityId CreateBox(
        Scene::Scene& scene,
        const std::string& name,
        const Math::Vec3& pos,
        const Math::Vec3& halfExtents,
        Scene::RigidBodyMotionType motionType,
        f32 mass = 1.0f,
        bool isTrigger = false,
        u32 layer = 1u)
    {
        Scene::EntityId id = scene.CreateEntity(name);
        if (auto* tr = scene.Transforms().TryGet(id))
        {
            tr->Position = pos;
        }
        Scene::RigidBodyComponent rb{};
        rb.MotionType = motionType;
        rb.Shape = Scene::ColliderShapeType::Box;
        rb.HalfExtents = halfExtents;
        rb.Mass = mass;
        rb.IsTrigger = isTrigger ? 1u : 0u;
        rb.CollisionLayer = layer;
        rb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        scene.RigidBodies().EmplaceOrReplace(id, rb);
        return id;
    }

    void AttachCSharpScript(Scene::Scene& scene, Scene::EntityId entity, const std::string& fullTypeName)
    {
        Scene::ScriptComponent sc{};
        sc.ScriptClassId = Core::StringId::FromString(fullTypeName).Value();
        sc.ManagedGCHandle = 0;
        sc.Enabled = 1u;
        scene.Scripts().EmplaceOrReplace(entity, sc);
    }

    void StepEngine(Physics::PhysicsWorld& physics, Scene::Scene& scene, Script::ScriptEngine& scripts, f32 seconds, f32 dt = 1.0f / 60.0f)
    {
        const int steps = static_cast<int>(std::ceil(seconds / dt));
        for (int i = 0; i < steps; ++i)
        {
            physics.StepSimulation(
                dt,
                scene,
                nullptr,
                [&](f32 fixedDt) {
                    scripts.FixedTickManagedScripts(fixedDt);
                });
            scripts.DispatchPhysicsEvents();
            scripts.TickManagedScripts(dt);
        }
    }

    int GetMarkerCount(const Scene::Scene& scene, const std::string& prefix)
    {
        int count = 0;
        for (const auto eid : scene.Transforms().RawEntities())
        {
            const std::string_view name = scene.GetEntityName(eid);
            if (name.rfind(prefix, 0) == 0)
            {
                ++count;
            }
        }
        return count;
    }

    void ClearTempDir(const std::filesystem::path& dir)
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void TestA_to_F_ManagedPhysicsIntegration(const std::filesystem::path& managedDir)
    {
        Scene::Scene scene;
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "PhysicsWorld Initialize");
        EXPECT_TRUE(audio.Initialize(true), "AudioWorld Initialize");

        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        auto initRes = scripts.Initialize(hostDesc, scene, physics, audio);
        EXPECT_TRUE(initRes.IsSuccess(), "ScriptEngine Initialize");
        if (!initRes.IsSuccess())
        {
            std::cerr << "ScriptEngine::Initialize error: " << initRes.Error() << "\n";
            return;
        }

        const std::filesystem::path workDir = GetExecutableDir() / "TempCSharpPhysicsTests";
        const std::filesystem::path scriptsDir = workDir / "Scripts1";
        const std::filesystem::path outDll = workDir / "CSharpPhysicsTests1.dll";
        ClearTempDir(workDir);

        const std::string csSource = R"CS(
using System;
using Morbi.Engine;

namespace PhysicsTests;

[EntityScript]
public sealed class BallProbeScript : Script
{
    public int FixedSteps;
    public int CollisionEnters;
    public int CollisionStays;
    public int CollisionExits;
    public int DestroyedCount;

    public override void OnStart()
    {
        if (RigidBody != null)
        {
            RigidBody.AddImpulse(new Vec3(4.0f, 0.0f, 0.0f));
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        FixedSteps++;
        RigidBody?.AddForce(new Vec3(1.0f, 0.0f, 0.0f), ForceMode.Force);
    }

    public override void OnCollisionEnter(Collision collision)
    {
        CollisionEnters++;
        Scene.CreateEntity($"ColEnter_{CollisionEnters}_{collision.Entity?.Name}");
    }

    public override void OnCollisionStay(Collision collision)
    {
        CollisionStays++;
        if (CollisionStays == 1)
        {
            Scene.CreateEntity("ColStay_1");
        }
    }

    public override void OnCollisionExit(Collision collision)
    {
        CollisionExits++;
        Scene.CreateEntity($"ColExit_{CollisionExits}");
    }

    public override void OnDestroy()
    {
        DestroyedCount++;
        Scene.CreateEntity("BallDestroyed");
    }
}

[EntityScript]
public sealed class TriggerZoneScript : Script
{
    public int Enters;
    public int Stays;
    public int Exits;

    public override void OnTriggerEnter(Collider other)
    {
        Enters++;
        Scene.CreateEntity($"TrigEnter_{Enters}_{other.Entity.Name}");
    }

    public override void OnTriggerStay(Collider other)
    {
        Stays++;
        if (Stays == 1)
        {
            Scene.CreateEntity("TrigStay_1");
        }
    }

    public override void OnTriggerExit(Collider other)
    {
        Exits++;
        Scene.CreateEntity($"TrigExit_{Exits}");
    }
}

[EntityScript]
public sealed class QueryAndSyncVerifier : Script
{
    public override void OnStart()
    {
        // Runtime RigidBody sync test
        Entity? syncBox = Scene.FindEntity("SyncBox");
        if (syncBox != null)
        {
            RigidBody? rb = syncBox.GetRigidBody();
            if (rb != null)
            {
                rb.Mass = 5.0f;
                rb.Friction = 0.25f;
                rb.Restitution = 0.75f;
                rb.UseGravity = false;
                rb.SetLinearVelocity(new Vec3(0.0f, 0.0f, 3.0f));
                Vec3 readBack = rb.LinearVelocity;
                if (MathF.Abs(readBack.Z - 3.0f) < 0.05f && MathF.Abs(rb.Mass - 5.0f) < 0.01f)
                {
                    Scene.CreateEntity("SyncVerified");
                }
            }
        }

        // Raycast + RaycastAll + SphereCast + BoxCast + OverlapSphere + LayerMatrix
        if (Physics.Raycast(new Vec3(-30.0f, 10.0f, 0.0f), Vec3.Down, out RaycastHit hit, 50.0f, CollisionLayers.AllMask, includeTriggers: false))
        {
            if (hit.Entity?.Name == "Floor" && MathF.Abs(hit.Point.Y) < 0.02f)
            {
                Scene.CreateEntity("RaycastFloorOK");
            }
        }

        RaycastHit[] allHits = Physics.RaycastAll(new Vec3(20.0f, 10.0f, 0.0f), Vec3.Down, 50.0f,
            new QueryFilter { IncludeTriggers = false });
        if (allHits.Length >= 2)
        {
            Scene.CreateEntity("RaycastAllOK");
        }

        if (Physics.SphereCast(new Vec3(20.0f, 10.0f, 0.0f), 0.4f, Vec3.Down, out RaycastHit sHit, 50.0f))
        {
            if (sHit.Entity?.Name == "StackTop" || sHit.Entity?.Name == "StackBottom")
            {
                Scene.CreateEntity("SphereCastOK");
            }
        }

        if (Physics.BoxCast(new Vec3(20.0f, 10.0f, 0.0f), new Vec3(0.3f, 0.3f, 0.3f), Quat.Identity, Vec3.Down, out RaycastHit bHit, 50.0f))
        {
            if (bHit.Entity != null)
            {
                Scene.CreateEntity("BoxCastOK");
            }
        }

        OverlapHit[] overlaps = Physics.OverlapSphere(new Vec3(20.0f, 1.5f, 0.0f), 2.0f);
        if (overlaps.Length >= 2)
        {
            Scene.CreateEntity("OverlapSphereOK");
        }

        Physics.SetLayerCollision(7, 8, false);
        if (!Physics.GetLayerCollision(7, 8))
        {
            Scene.CreateEntity("LayerMatrixOK");
        }
        Physics.SetLayerCollision(7, 8, true);
    }
}

[EntityScript]
public sealed class CharacterDriverScript : Script
{
    private bool _jumped;
    private float _elapsed;

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        CharacterController? cc = GetComponent<CharacterController>();
        if (cc == null) return;

        _elapsed += fixedDeltaTime;
        if (cc.IsGrounded && !_jumped)
        {
            Scene.CreateEntity("CharGroundedOK");
            cc.Jump(6.0f);
            _jumped = true;
        }
        cc.Move(new Vec3(3.0f * fixedDeltaTime, 0.0f, 0.0f));
        if (_elapsed > 1.6f && cc.IsGrounded && Transform != null && Transform.Position.X > -11.5f)
        {
            if (Scene.FindEntity("CharWalkAndJumpOK") == null)
            {
                Scene.CreateEntity("CharWalkAndJumpOK");
            }
        }
    }
}
)CS";

        EXPECT_TRUE(WriteTextFile(scriptsDir / "PhysicsIntegrationTests.cs", csSource), "Write C# test script");

        // Build scene BEFORE compiling/loading assembly
        CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 50.0f, 0.5f, 50.0f }, Scene::RigidBodyMotionType::Static);
        const Scene::EntityId ball = CreateBox(scene, "Ball", { 0.0f, 2.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 2.0f);
        AttachCSharpScript(scene, ball, "PhysicsTests.BallProbeScript");

        // Trigger zone floating at (10, 2, 0), and falling cube above it
        const Scene::EntityId triggerZone = CreateBox(scene, "TriggerZone", { 10.0f, 2.0f, 0.0f }, { 1.5f, 0.5f, 1.5f }, Scene::RigidBodyMotionType::Static, 1.0f, true);
        AttachCSharpScript(scene, triggerZone, "PhysicsTests.TriggerZoneScript");
        CreateBox(scene, "FallingCrate", { 10.0f, 4.0f, 0.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic, 1.0f);

        // SyncBox + Query stacks
        const Scene::EntityId syncBox = CreateBox(scene, "SyncBox", { -10.0f, 5.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f);
        CreateBox(scene, "StackBottom", { 20.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);
        CreateBox(scene, "StackTop", { 20.0f, 2.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Static);

        const Scene::EntityId verifier = scene.CreateEntity("Verifier");
        AttachCSharpScript(scene, verifier, "PhysicsTests.QueryAndSyncVerifier");

        const Scene::EntityId charHero = scene.CreateEntity("CharHero");
        if (auto* ctr = scene.Transforms().TryGet(charHero))
        {
            ctr->Position = { -15.0f, 1.0f, -15.0f };
        }
        Scene::CharacterControllerComponent charComp{};
        charComp.DirtyFlags = static_cast<u8>(Scene::CharacterControllerDirtyFlags::All);
        scene.CharacterControllers().EmplaceOrReplace(charHero, charComp);
        AttachCSharpScript(scene, charHero, "PhysicsTests.CharacterDriverScript");

        // Sync bodies and characters into Jolt before C# OnStart executes queries
        physics.RebuildFromScene(scene);

        auto compileRes = scripts.CompileProjectScripts(scriptsDir, outDll, true, false);
        EXPECT_TRUE(compileRes.Success, "CompileProjectScripts v1: " + compileRes.Diagnostics);

        // Step simulation for 2.0 seconds
        StepEngine(physics, scene, scripts, 2.0f);

        const auto* ballTr = scene.Transforms().TryGet(ball);
        EXPECT_TRUE(ballTr && ballTr->Position.x > 1.0f, "TestA: C# RigidBody AddImpulse/AddForce moved Ball along +X");
        const auto* syncTr = scene.Transforms().TryGet(syncBox);
        EXPECT_TRUE(syncTr && syncTr->Position.z > 3.0f && std::abs(syncTr->Position.y - 5.0f) < 0.05f,
            "TestE: C# SyncBox velocity Z=3 and UseGravity=false applied in Jolt");

        EXPECT_TRUE(scene.FindEntityByName("ColEnter_1_Floor").IsValid(), "TestB: C# OnCollisionEnter with Floor fired");
        EXPECT_TRUE(scene.FindEntityByName("ColStay_1").IsValid(), "TestB: C# OnCollisionStay fired");

        EXPECT_TRUE(scene.FindEntityByName("TrigEnter_1_FallingCrate").IsValid(), "TestC: C# OnTriggerEnter with FallingCrate fired");
        EXPECT_TRUE(scene.FindEntityByName("TrigStay_1").IsValid(), "TestC: C# OnTriggerStay fired");
        EXPECT_TRUE(scene.FindEntityByName("TrigExit_1").IsValid(), "TestC: C# OnTriggerExit fired after crate fell through trigger");

        EXPECT_TRUE(scene.FindEntityByName("SyncVerified").IsValid(), "TestE: C# SyncRigidBody property readback verified");
        EXPECT_TRUE(scene.FindEntityByName("RaycastFloorOK").IsValid(), "TestD: C# Physics.Raycast verified");
        EXPECT_TRUE(scene.FindEntityByName("RaycastAllOK").IsValid(), "TestD: C# Physics.RaycastAll verified");
        EXPECT_TRUE(scene.FindEntityByName("SphereCastOK").IsValid(), "TestD: C# Physics.SphereCast verified");
        EXPECT_TRUE(scene.FindEntityByName("BoxCastOK").IsValid(), "TestD: C# Physics.BoxCast verified");
        EXPECT_TRUE(scene.FindEntityByName("OverlapSphereOK").IsValid(), "TestD: C# Physics.OverlapSphere verified");
        EXPECT_TRUE(scene.FindEntityByName("LayerMatrixOK").IsValid(), "TestD: C# Physics.SetLayerCollision verified");
        EXPECT_TRUE(scene.FindEntityByName("CharGroundedOK").IsValid(), "TestH: C# CharacterController IsGrounded & Jump verified");
        EXPECT_TRUE(scene.FindEntityByName("CharWalkAndJumpOK").IsValid(), "TestH: C# CharacterController Move, apex & re-land verified");

        // Teleport Ball high above Floor to verify OnCollisionExit + OnDestroy on DestroyEntity
        physics.TeleportBody(scene, ball, { 0.0f, 15.0f, 0.0f }, Math::Quat::Identity());
        StepEngine(physics, scene, scripts, 0.1f);
        EXPECT_TRUE(scene.FindEntityByName("ColExit_1").IsValid(), "TestB: C# OnCollisionExit fired after separation");

        scene.DestroyEntity(ball);
        EXPECT_TRUE(scene.FindEntityByName("BallDestroyed").IsValid(), "TestF: C# OnDestroy called on DestroyEntity");
        StepEngine(physics, scene, scripts, 0.1f);

        // Test G: Hot-Reload with live physics world
        const std::filesystem::path scriptsDir2 = workDir / "Scripts2";
        const std::filesystem::path outDll2 = workDir / "CSharpPhysicsTests2.dll";
        const std::string csSourceV2 = R"CS(
using Morbi.Engine;

namespace PhysicsTests;

[EntityScript]
public sealed class HotReloadBodyScript : Script
{
    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        RigidBody?.AddImpulse(new Vec3(0.0f, 0.0f, 0.5f));
    }
}
)CS";
        EXPECT_TRUE(WriteTextFile(scriptsDir2 / "HotReloadTest.cs", csSourceV2), "Write C# v2 script");

        const Scene::EntityId hotBox = CreateBox(scene, "HotBox", { -20.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 1.0f);
        AttachCSharpScript(scene, hotBox, "PhysicsTests.HotReloadBodyScript");
        physics.RebuildFromScene(scene);

        const u32 genBefore = scripts.GetHotReloadGeneration();
        auto compileRes2 = scripts.CompileProjectScripts(scriptsDir2, outDll2, true, false);
        EXPECT_TRUE(compileRes2.Success, "TestG: Hot-reload CompileProjectScripts v2: " + compileRes2.Diagnostics);
        EXPECT_TRUE(scripts.GetHotReloadGeneration() > genBefore, "TestG: HotReloadGeneration incremented");

        StepEngine(physics, scene, scripts, 0.25f);
        const auto* hotTr = scene.Transforms().TryGet(hotBox);
        EXPECT_TRUE(hotTr && hotTr->Position.z > 0.5f, "TestG: Hot-reloaded C# script actively driving Jolt body");

        EXPECT_TRUE(GetMarkerCount(scene, "ColEnter_") >= 1, "Marker count sanity check");

        // Verify all 3 PhysicsShowcase example C# scripts compile cleanly via Roslyn
        const std::filesystem::path showcaseDir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
            "Samples" / "PhysicsShowcase" / "Scripts";
        if (std::filesystem::exists(showcaseDir))
        {
            const std::filesystem::path showcaseDll = workDir / "PhysicsShowcase.dll";
            auto showcaseRes = scripts.CompileProjectScripts(showcaseDir, showcaseDll, false, true);
            EXPECT_TRUE(showcaseRes.Success && showcaseRes.CompiledFileCount == 3u,
                "Showcase example scripts compile cleanly: " + showcaseRes.Diagnostics);
        }

        // Test I: C# Stress test - spawn 150 dynamic bodies from C#, apply impulses and query via C# over 1s
        const std::filesystem::path scriptsDirStress = workDir / "ScriptsStress";
        const std::filesystem::path outDllStress = workDir / "CSharpPhysicsStress.dll";
        const std::string csStressSource = R"CS(
using Morbi.Engine;

namespace PhysicsTests;

public sealed class CSharpStressDriver : Script
{
    private const int BodyCount = 150;
    private readonly Entity?[] _bodies = new Entity?[BodyCount];

    public override void OnStart()
    {
        for (int i = 0; i < BodyCount; i++)
        {
            Entity? e = Scene.CreateEntity($"StressCrate_{i}");
            if (e == null) continue;
            float x = (i % 10) * 1.5f - 7.5f;
            float z = (i / 10) * 1.5f - 10.0f;
            e.GetTransform().Position = new Vec3(x, 4.0f + (i % 3) * 1.2f, z);
            RigidBody? rb = e.AddComponent<RigidBody>();
            if (rb != null)
            {
                rb.MotionType = MotionType.Dynamic;
                rb.Shape = (i % 2 == 0) ? ColliderShape.Box : ColliderShape.Sphere;
                rb.HalfExtents = new Vec3(0.45f, 0.45f, 0.45f);
                rb.Mass = 1.5f;
                rb.AddImpulse(new Vec3(1.5f, 0.0f, 0.5f));
            }
            _bodies[i] = e;
        }
        Scene.CreateEntity("CSharpStressSpawned");
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        OverlapHit[] hits = Physics.OverlapSphere(new Vec3(0.0f, 1.0f, 0.0f), 12.0f,
            new QueryFilter { IgnoreStatic = true }, maxHits: 128);
        if (hits.Length > 0 && Scene.FindEntity("CSharpStressOverlapOK") == null)
        {
            Scene.CreateEntity("CSharpStressOverlapOK");
        }
    }
}
)CS";
        EXPECT_TRUE(WriteTextFile(scriptsDirStress / "StressDriver.cs", csStressSource), "Write C# stress script");
        auto stressCompile = scripts.CompileProjectScripts(scriptsDirStress, outDllStress, true, true);
        EXPECT_TRUE(stressCompile.Success, "Compile C# stress script: " + stressCompile.Diagnostics);
        StepEngine(physics, scene, scripts, 1.0f);
        EXPECT_TRUE(scene.FindEntityByName("CSharpStressSpawned").IsValid(), "TestI: C# spawned 150 RigidBodies");
        EXPECT_TRUE(scene.FindEntityByName("CSharpStressOverlapOK").IsValid(), "TestI: C# OverlapSphere queried dynamic bodies cleanly");
        EXPECT_TRUE(physics.GetStats().DynamicBodyCount >= 150u, "TestI: Jolt simulates >= 150 C#-created dynamic bodies");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        ClearTempDir(workDir);

        std::cout << "[PASS] TestA_to_F_and_G_CSharpPhysicsAndHotReload\n";
    }

    void TestJ_CompleteCSharpRigidBodyAndQueriesAudit(const std::filesystem::path& managedDir)
    {
        Scene::Scene scene;
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            scripts.NotifySceneCleared();
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "TestJ Physics init");
        EXPECT_TRUE(audio.Initialize(true), "TestJ Audio init");
        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        EXPECT_TRUE(scripts.Initialize(hostDesc, scene, physics, audio).IsSuccess(), "TestJ Script init");

        const std::filesystem::path workDir = GetExecutableDir() / "TempCSharpAuditJ";
        const std::filesystem::path scriptsDir = workDir / "Scripts";
        const std::filesystem::path outDll = workDir / "AuditJ.dll";
        ClearTempDir(workDir);

        const std::string csAuditJ = R"CS(
using System;
using Morbi.Engine;

namespace PhysicsAudit;

[EntityScript]
public sealed class AuditDriverJ : Script
{
    private Entity? _bForce;
    private Entity? _bAccel;
    private Entity? _bImpulse;
    private Entity? _bVelChg;
    private Entity? _bAtPos;
    private Entity? _bTorque;
    private Entity? _bTorqueImp;
    private Entity? _bTorqueVelChg;
    private Entity? _bSleepTest;
    private Entity? _bKinMover;
    private Entity? _bKinTarget;
    private Entity? _bLowDamp;
    private Entity? _bHighDamp;
    private float _elapsed;

    public override void OnStart()
    {
        Physics.Gravity = new Vec3(0.0f, -9.81f, 0.0f);

        // 1. ForceMode variants (gravity disabled, Mass = 2 kg each)
        _bForce   = MakeDyn("FM_Force",   new Vec3(-20, 2, 0), 2.0f, useGrav: false);
        _bAccel   = MakeDyn("FM_Accel",   new Vec3(-20, 2, 4), 2.0f, useGrav: false);
        _bImpulse = MakeDyn("FM_Impulse", new Vec3(-20, 2, 8), 2.0f, useGrav: false);
        _bVelChg  = MakeDyn("FM_VelChg",  new Vec3(-20, 2, 12), 2.0f, useGrav: false);

        _bImpulse?.GetRigidBody()?.AddForce(new Vec3(4.0f, 0.0f, 0.0f), ForceMode.Impulse);       // dv = 4/2 = 2 m/s
        _bVelChg?.GetRigidBody()?.AddForce(new Vec3(4.0f, 0.0f, 0.0f), ForceMode.VelocityChange); // dv = 4 m/s

        // 2. AddForceAtPosition (off-center impulse generates linear + angular spin)
        _bAtPos = MakeDyn("AtPosBox", new Vec3(0, 5, 20), 2.0f, useGrav: false);
        if (_bAtPos != null)
        {
            RigidBody? rb = _bAtPos.GetRigidBody();
            rb?.AddForceAtPosition(new Vec3(6.0f, 0.0f, 0.0f), new Vec3(0.0f, 5.45f, 20.45f), ForceMode.Impulse);
        }

        // 3. Torque + AddTorqueImpulse + VelocityChange
        _bTorque       = MakeDyn("T_Force",   new Vec3(5, 5, 20), 2.0f, useGrav: false);
        _bTorqueImp    = MakeDyn("T_Impulse", new Vec3(10, 5, 20), 2.0f, useGrav: false);
        _bTorqueVelChg = MakeDyn("T_VelChg",  new Vec3(15, 5, 20), 5.0f, useGrav: false);

        _bTorqueImp?.GetRigidBody()?.AddTorqueImpulse(new Vec3(0.0f, 4.0f, 0.0f));
        _bTorqueVelChg?.GetRigidBody()?.AddTorque(new Vec3(0.0f, 3.0f, 0.0f), ForceMode.VelocityChange);

        // 4. Sleep / WakeUp / Teleport
        _bSleepTest = MakeDyn("SleepTest", new Vec3(20, 5, 20), 1.0f, useGrav: false);
        if (_bSleepTest != null)
        {
            RigidBody? rb = _bSleepTest.GetRigidBody();
            if (rb != null)
            {
                rb.Sleep();
                bool isAsleep = rb.IsSleeping;
                rb.WakeUp();
                bool isAwakeAfter = !rb.IsSleeping;
                if (isAsleep && isAwakeAfter) Scene.CreateEntity("J_SleepWakeOK");
                rb.Teleport(new Vec3(22.0f, 6.0f, 22.0f), Quat.Identity);
                if (MathF.Abs(_bSleepTest.GetTransform().Position.X - 22.0f) < 0.02f)
                {
                    Scene.CreateEntity("J_TeleportOK");
                }
            }
        }

        // 5. Damping comparison
        _bLowDamp  = MakeDyn("LowDamp",  new Vec3(-10, 5, -20), 1.0f, useGrav: false);
        _bHighDamp = MakeDyn("HighDamp", new Vec3(-10, 5, -24), 1.0f, useGrav: false);
        if (_bLowDamp?.GetRigidBody() is RigidBody ld && _bHighDamp?.GetRigidBody() is RigidBody hd)
        {
            ld.LinearDamping = 0.0f;
            hd.LinearDamping = 6.0f;
            ld.SetLinearVelocity(new Vec3(10.0f, 0.0f, 0.0f));
            hd.SetLinearVelocity(new Vec3(10.0f, 0.0f, 0.0f));
        }

        // 6. Kinematic pusher -> Dynamic target
        _bKinMover = Scene.CreateEntity("KinMover");
        if (_bKinMover != null)
        {
            _bKinMover.GetTransform().Position = new Vec3(-30.0f, 1.0f, 0.0f);
            RigidBody? krb = _bKinMover.AddComponent<RigidBody>();
            if (krb != null)
            {
                krb.MotionType = MotionType.Kinematic;
                krb.HalfExtents = new Vec3(0.5f, 0.5f, 0.5f);
            }
        }
        _bKinTarget = MakeDyn("KinTarget", new Vec3(-28.7f, 1.0f, 0.0f), 1.0f, useGrav: false);

        // 7. Rotated & offset Capsule/Box targets for CapsuleCast, OverlapBox, OverlapCapsule & Collider components
        Entity? offsetCap = Scene.CreateEntity("OffsetCapsuleTarget");
        if (offsetCap != null)
        {
            offsetCap.GetTransform().Position = new Vec3(40.0f, 2.0f, 0.0f);
            CapsuleCollider? cc = offsetCap.AddComponent<CapsuleCollider>();
            if (cc != null)
            {
                cc.AttachedRigidBody.MotionType = MotionType.Static;
                cc.Radius = 0.5f;
                cc.HalfHeight = 0.8f;
                cc.Offset = new Vec3(2.0f, 0.0f, 0.0f); // Center now at (42, 2, 0)
            }
        }

        Entity? boxTarget = Scene.CreateEntity("BoxOverlapTarget");
        if (boxTarget != null)
        {
            boxTarget.GetTransform().Position = new Vec3(44.0f, 2.0f, 0.0f);
            BoxCollider? bc = boxTarget.AddComponent<BoxCollider>();
            if (bc != null)
            {
                bc.AttachedRigidBody.MotionType = MotionType.Static;
                bc.Size = new Vec3(1.2f, 1.2f, 1.2f);
            }
        }

        // Layer collision mask test
        Entity? layerA = MakeDyn("LayerMaskA", new Vec3(60, 3, 0), 1.0f, useGrav: false);
        Entity? layerB = MakeDyn("LayerMaskB", new Vec3(60, 3, 0), 1.0f, useGrav: false);
        if (layerA?.GetRigidBody() is RigidBody rba && layerB?.GetRigidBody() is RigidBody rbb)
        {
            rba.CollisionLayer = CollisionLayers.LayerBit(10);
            rba.CollisionMask = CollisionLayers.LayerBit(10);
            rbb.CollisionLayer = CollisionLayers.LayerBit(11);
            rbb.CollisionMask = CollisionLayers.LayerBit(11);
            Scene.CreateEntity("J_LayerFilterConfigured");
        }

        // Verify CapsuleCast, OverlapBox, OverlapCapsule
        if (Physics.CapsuleCast(new Vec3(37.0f, 2.0f, 0.0f), 0.35f, 0.5f, Quat.Identity, Vec3.Right, out RaycastHit capHit, 10.0f))
        {
            if (capHit.Entity?.Name == "OffsetCapsuleTarget" && capHit.Point.X > 40.5f)
            {
                Scene.CreateEntity("J_CapsuleCastOK");
            }
        }

        OverlapHit[] boxHits = Physics.OverlapBox(new Vec3(43.0f, 2.0f, 0.0f), new Vec3(1.8f, 1.5f, 1.5f), Quat.Identity);
        bool sawCap = false, sawBox = false;
        for (int i = 0; i < boxHits.Length; i++)
        {
            if (boxHits[i].Entity?.Name == "OffsetCapsuleTarget") sawCap = true;
            if (boxHits[i].Entity?.Name == "BoxOverlapTarget") sawBox = true;
        }
        if (sawCap && sawBox) Scene.CreateEntity("J_OverlapBoxOK");

        OverlapHit[] capHits = Physics.OverlapCapsule(new Vec3(43.0f, 2.0f, 0.0f), 1.6f, 0.5f, Quat.Identity);
        bool capSawBox = false;
        for (int i = 0; i < capHits.Length; i++)
        {
            if (capHits[i].Entity?.Name == "BoxOverlapTarget") capSawBox = true;
        }
        if (capSawBox) Scene.CreateEntity("J_OverlapCapsuleOK");
    }

    private static Entity? MakeDyn(string name, in Vec3 pos, float mass, bool useGrav)
    {
        Entity? e = Scene.CreateEntity(name);
        if (e == null) return null;
        e.GetTransform().Position = pos;
        RigidBody? rb = e.AddComponent<RigidBody>();
        if (rb != null)
        {
            rb.MotionType = MotionType.Dynamic;
            rb.Mass = mass;
            rb.UseGravity = useGrav;
            rb.LinearDamping = 0.0f;
            rb.AngularDamping = 0.0f;
            rb.HalfExtents = new Vec3(0.5f, 0.5f, 0.5f);
        }
        return e;
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _elapsed += fixedDeltaTime;
        _bForce?.GetRigidBody()?.AddForce(new Vec3(10.0f, 0.0f, 0.0f), ForceMode.Force);        // a = 10/2 = 5 m/s^2
        _bAccel?.GetRigidBody()?.AddForce(new Vec3(10.0f, 0.0f, 0.0f), ForceMode.Acceleration); // a = 10 m/s^2
        _bTorque?.GetRigidBody()?.AddTorque(new Vec3(0.0f, 5.0f, 0.0f), ForceMode.Force);

        if (_bKinMover != null)
        {
            Transform kt = _bKinMover.GetTransform();
            kt.Position = kt.Position + new Vec3(4.0f * fixedDeltaTime, 0.0f, 0.0f);
        }

        if (_elapsed >= 0.45f)
        {
            RigidBody? rbF = _bForce?.GetRigidBody();
            RigidBody? rbA = _bAccel?.GetRigidBody();
            RigidBody? rbI = _bImpulse?.GetRigidBody();
            RigidBody? rbV = _bVelChg?.GetRigidBody();
            if (rbF != null && rbA != null && rbI != null && rbV != null)
            {
                if (rbA.LinearVelocity.X > rbF.LinearVelocity.X * 1.7f &&
                    MathF.Abs(rbI.LinearVelocity.X - 2.0f) < 0.05f &&
                    MathF.Abs(rbV.LinearVelocity.X - 4.0f) < 0.05f)
                {
                    if (Scene.FindEntity("J_ForceModesOK") == null) Scene.CreateEntity("J_ForceModesOK");
                }
            }

            RigidBody? rbPos = _bAtPos?.GetRigidBody();
            if (rbPos != null && rbPos.LinearVelocity.X > 1.0f && rbPos.AngularVelocity.Length() > 0.5f)
            {
                if (Scene.FindEntity("J_ForceAtPosOK") == null) Scene.CreateEntity("J_ForceAtPosOK");
            }

            RigidBody? rbT  = _bTorque?.GetRigidBody();
            RigidBody? rbTi = _bTorqueImp?.GetRigidBody();
            RigidBody? rbTv = _bTorqueVelChg?.GetRigidBody();
            if (rbT != null && rbTi != null && rbTv != null &&
                rbT.AngularVelocity.Y > 0.5f &&
                rbTi.AngularVelocity.Y > 0.5f &&
                MathF.Abs(rbTv.AngularVelocity.Y - 3.0f) < 0.08f)
            {
                if (Scene.FindEntity("J_TorqueModesOK") == null) Scene.CreateEntity("J_TorqueModesOK");
            }

            RigidBody? ld = _bLowDamp?.GetRigidBody();
            RigidBody? hd = _bHighDamp?.GetRigidBody();
            if (ld != null && hd != null && ld.LinearVelocity.X > hd.LinearVelocity.X * 2.0f)
            {
                if (Scene.FindEntity("J_DampingOK") == null) Scene.CreateEntity("J_DampingOK");
            }

            if (_bKinTarget != null && _bKinTarget.GetTransform().Position.X > -28.0f)
            {
                if (Scene.FindEntity("J_KinematicPushOK") == null) Scene.CreateEntity("J_KinematicPushOK");
            }
        }
    }
}
)CS";
        EXPECT_TRUE(WriteTextFile(scriptsDir / "AuditJ.cs", csAuditJ), "Write AuditJ.cs");
        const Scene::EntityId driver = scene.CreateEntity("AuditDriverJ");
        AttachCSharpScript(scene, driver, "PhysicsAudit.AuditDriverJ");

        auto cRes = scripts.CompileProjectScripts(scriptsDir, outDll, true, false);
        EXPECT_TRUE(cRes.Success, "Compile AuditJ: " + cRes.Diagnostics);
        StepEngine(physics, scene, scripts, 0.55f);

        EXPECT_TRUE(scene.FindEntityByName("J_SleepWakeOK").IsValid(), "TestJ: Sleep/WakeUp/IsSleeping from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_TeleportOK").IsValid(), "TestJ: RigidBody.Teleport from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_CapsuleCastOK").IsValid(), "TestJ: Physics.CapsuleCast + OffsetCapsule from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_OverlapBoxOK").IsValid(), "TestJ: Physics.OverlapBox + BoxCollider from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_OverlapCapsuleOK").IsValid(), "TestJ: Physics.OverlapCapsule from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_ForceModesOK").IsValid(), "TestJ: All 4 ForceMode variants exact math from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_ForceAtPosOK").IsValid(), "TestJ: AddForceAtPosition linear+angular impulse from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_TorqueModesOK").IsValid(), "TestJ: AddTorque/AddTorqueImpulse/VelocityChange from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_DampingOK").IsValid(), "TestJ: LinearDamping comparison from C#");
        EXPECT_TRUE(scene.FindEntityByName("J_KinematicPushOK").IsValid(), "TestJ: Kinematic transform push from C#");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        ClearTempDir(workDir);
        std::cout << "[PASS] TestJ_CompleteCSharpRigidBodyAndQueriesAudit\n";
    }

    void TestK_CompleteCSharpCharacterAudit(const std::filesystem::path& managedDir)
    {
        Scene::Scene scene;
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            scripts.NotifySceneCleared();
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "TestK Physics init");
        EXPECT_TRUE(audio.Initialize(true), "TestK Audio init");
        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        EXPECT_TRUE(scripts.Initialize(hostDesc, scene, physics, audio).IsSuccess(), "TestK Script init");

        // World geometry in C++ setup
        CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 80.0f, 0.5f, 80.0f }, Scene::RigidBodyMotionType::Static);
        // Walkable 25-degree slope rising in -Z starting near Z=-5.2
        const Scene::EntityId gSlope = CreateBox(scene, "GentleSlope", { 0.0f, 1.35f, -9.0f }, { 3.0f, 0.25f, 4.0f }, Scene::RigidBodyMotionType::Static);
        scene.Transforms().TryGet(gSlope)->Rotation = Math::FromEulerDegrees({ 25.0f, 0.0f, 0.0f });
        // Steep 65-degree slope at X=0, Z=15
        const Scene::EntityId sSlope = CreateBox(scene, "SteepSlope", { 0.0f, 2.0f, 15.0f }, { 3.0f, 0.25f, 4.0f }, Scene::RigidBodyMotionType::Static);
        scene.Transforms().TryGet(sSlope)->Rotation = Math::FromEulerDegrees({ 65.0f, 0.0f, 0.0f });
        // Wall at X=8, Z=0
        CreateBox(scene, "Wall", { 8.0f, 1.5f, 0.0f }, { 0.5f, 1.5f, 3.0f }, Scene::RigidBodyMotionType::Static);
        // Stairs along -X starting X=-3
        CreateBox(scene, "Stair1", { -4.0f, 0.125f, 0.0f }, { 1.0f, 0.125f, 2.0f }, Scene::RigidBodyMotionType::Static);
        CreateBox(scene, "Stair2", { -6.0f, 0.375f, 0.0f }, { 1.0f, 0.125f, 2.0f }, Scene::RigidBodyMotionType::Static);
        // Dynamic crate at (0, 0.5, 4)
        CreateBox(scene, "DynCrate", { 0.0f, 0.5f, 4.0f }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic, 2.0f);
        // Kinematic platform at (25, 0.25, 0)
        CreateBox(scene, "Platform", { 25.0f, 0.25f, 0.0f }, { 2.0f, 0.25f, 2.0f }, Scene::RigidBodyMotionType::Kinematic);

        physics.RebuildFromScene(scene);

        const std::filesystem::path workDir = GetExecutableDir() / "TempCSharpAuditK";
        const std::filesystem::path scriptsDir = workDir / "Scripts";
        const std::filesystem::path outDll = workDir / "AuditK.dll";
        ClearTempDir(workDir);

        const std::string csAuditK = R"CS(
using System;
using Morbi.Engine;

namespace PhysicsAudit;

[EntityScript]
public sealed class AuditCharacterDriver : Script
{
    private Entity? _hero;
    private CharacterController? _cc;
    private Entity? _platform;
    private int _step;

    public override void OnStart()
    {
        _platform = Scene.FindEntity("Platform");
        _hero = Scene.CreateEntity("CSharpHero");
        if (_hero != null)
        {
            _hero.GetTransform().Position = new Vec3(0.0f, 1.2f, 0.0f);
            _cc = _hero.AddComponent<CharacterController>();
            if (_cc != null)
            {
                _cc.Radius = 0.35f;
                _cc.Height = 1.8f;
                _cc.StepHeight = 0.35f;
                _cc.MaxSlopeAngleDegrees = 45.0f;
                _cc.MaxPushStrength = 400.0f;
            }
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_hero == null || _cc == null) return;
        _step++;

        // Phase 1 (steps 1..40): Land -> check IsGrounded + Jump
        if (_step == 35 && _cc.IsGrounded)
        {
            Scene.CreateEntity("K_GroundedOK");
            _cc.Jump(6.5f);
        }
        if (_step == 50 && _hero.GetTransform().Position.Y > 0.7f && !_cc.IsGrounded)
        {
            Scene.CreateEntity("K_JumpOK");
        }

        // Phase 2 (steps 90..150): Walk toward Wall at X=8 -> verify wall collision stops penetration
        if (_step == 90) _cc.Teleport(new Vec3(6.0f, 0.05f, 0.0f));
        if (_step > 90 && _step <= 140)
        {
            _cc.Move(new Vec3(5.0f * fixedDeltaTime, 0.0f, 0.0f));
        }
        if (_step == 141 && _hero.GetTransform().Position.X < 7.5f)
        {
            Scene.CreateEntity("K_WallCollisionOK");
        }

        // Phase 3 (steps 145..225): Stair climbing along -X onto Stair2
        if (_step == 145) _cc.Teleport(new Vec3(-1.5f, 0.05f, 0.0f));
        if (_step > 145 && _step <= 205)
        {
            _cc.Move(new Vec3(-4.5f * fixedDeltaTime, 0.0f, 0.0f));
        }
        if (_step == 206 && _hero.GetTransform().Position.Y > 0.42f)
        {
            Scene.CreateEntity("K_StairClimbOK");
        }

        // Phase 4 (steps 210..260): Push Dynamic crate at (0, 0.5, 4)
        if (_step == 210) _cc.Teleport(new Vec3(0.0f, 0.05f, 2.4f));
        if (_step > 210 && _step <= 255)
        {
            _cc.Move(new Vec3(0.0f, 0.0f, 4.0f * fixedDeltaTime));
        }
        if (_step == 256)
        {
            Entity? crate = Scene.FindEntity("DynCrate");
            if (crate != null && crate.GetTransform().Position.Z > 4.6f)
            {
                Scene.CreateEntity("K_PushDynamicOK");
            }
        }

        // Phase 5 (steps 260..320): Moving platform ride-along
        if (_step == 260) _cc.Teleport(new Vec3(25.0f, 0.55f, 0.0f));
        if (_step > 265 && _step <= 320 && _platform != null)
        {
            Transform pt = _platform.GetTransform();
            pt.Position = pt.Position + new Vec3(3.0f * fixedDeltaTime, 0.0f, 0.0f);
        }
        if (_step == 321 && _hero.GetTransform().Position.X > 27.0f)
        {
            Scene.CreateEntity("K_MovingPlatformOK");
        }

        // Phase 6 (steps 325..385): Slope climb vs Steep slope slide
        if (_step == 325) _cc.Teleport(new Vec3(0.0f, 0.05f, -4.5f));
        if (_step > 325 && _step <= 370)
        {
            _cc.Move(new Vec3(0.0f, 0.0f, -4.5f * fixedDeltaTime));
        }
        if (_step == 371 && _hero.GetTransform().Position.Y > 0.8f)
        {
            Scene.CreateEntity("K_GentleSlopeOK");
        }
        if (_step == 374) _cc.Teleport(new Vec3(0.0f, 2.2f, 15.0f));
        if (_step >= 375 && _step <= 390 && _cc.IsSliding)
        {
            if (Scene.FindEntity("K_SteepSlideOK") == null) Scene.CreateEntity("K_SteepSlideOK");
        }

        // Phase 7 (step 392): Destroy Hero & Recreate new CharacterController
        if (_step == 392)
        {
            _hero.Destroy();
            Entity? hero2 = Scene.CreateEntity("CSharpHero2");
            if (hero2 != null)
            {
                hero2.GetTransform().Position = new Vec3(-20.0f, 1.0f, 0.0f);
                CharacterController? cc2 = hero2.AddComponent<CharacterController>();
                if (cc2 != null)
                {
                    _hero = hero2;
                    _cc = cc2;
                    Scene.CreateEntity("K_DestroyAndRecreateOK");
                }
            }
        }
        if (_step == 445 && _hero != null && _hero.Name == "CSharpHero2" && _cc != null && _cc.IsGrounded)
        {
            Scene.CreateEntity("K_RecreatedGroundedOK");
        }
    }
}
)CS";
        EXPECT_TRUE(WriteTextFile(scriptsDir / "AuditK.cs", csAuditK), "Write AuditK.cs");
        const Scene::EntityId driver = scene.CreateEntity("AuditCharacterDriver");
        AttachCSharpScript(scene, driver, "PhysicsAudit.AuditCharacterDriver");

        auto cRes = scripts.CompileProjectScripts(scriptsDir, outDll, true, false);
        EXPECT_TRUE(cRes.Success, "Compile AuditK: " + cRes.Diagnostics);
        StepEngine(physics, scene, scripts, 7.5f); // 450 steps at 60 Hz

        EXPECT_TRUE(scene.FindEntityByName("K_GroundedOK").IsValid(), "TestK: Character ground detection in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_JumpOK").IsValid(), "TestK: Character jump in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_WallCollisionOK").IsValid(), "TestK: Character wall collision in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_StairClimbOK").IsValid(), "TestK: Character step climbing in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_PushDynamicOK").IsValid(), "TestK: Character dynamic box push in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_MovingPlatformOK").IsValid(), "TestK: Character moving platform ride-along in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_GentleSlopeOK").IsValid(), "TestK: Character walkable slope climb in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_SteepSlideOK").IsValid(), "TestK: Character steep slope sliding in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_DestroyAndRecreateOK").IsValid(), "TestK: Character destroy & recreate in C#");
        EXPECT_TRUE(scene.FindEntityByName("K_RecreatedGroundedOK").IsValid(), "TestK: Recreated CharacterController lands cleanly in C#");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        ClearTempDir(workDir);
        std::cout << "[PASS] TestK_CompleteCSharpCharacterAudit\n";
    }

    void TestL_LifecycleShutdownReinitAndStaleHandles(const std::filesystem::path& managedDir)
    {
        Scene::Scene scene;
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            scripts.NotifySceneCleared();
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "TestL initial init");
        EXPECT_TRUE(audio.Initialize(true), "TestL audio init");

        // 1. Physics Shutdown -> Reinitialize cycle x3
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            CreateBox(scene, "Floor", { 0, -0.5f, 0 }, { 20, 0.5f, 20 }, Scene::RigidBodyMotionType::Static);
            const Scene::EntityId box = CreateBox(scene, "Box", { 0, 5.0f, 0 }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
            physics.RebuildFromScene(scene);
            for (int s = 0; s < 30; ++s) physics.StepSimulation(1.0f / 60.0f, scene);
            EXPECT_TRUE(scene.Transforms().TryGet(box)->Position.y < 4.5f, "TestL: Gravity active after reinit cycle " + std::to_string(cycle));
            scene.Clear();
            physics.Shutdown();
            EXPECT_TRUE(physics.Initialize(), "TestL: Physics reinitialize succeeded");
        }

        // 2. Repeated Play/Stop snapshots + Scene Switching + Stale Handle rejection
        CreateBox(scene, "BaseFloor", { 0, -0.5f, 0 }, { 30, 0.5f, 30 }, Scene::RigidBodyMotionType::Static);
        const Scene::EntityId prop = CreateBox(scene, "Prop", { 0, 4.0f, 0 }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
        const Scene::Scene::SceneSnapshot snap = scene.CreateSnapshot();

        for (int playStop = 0; playStop < 5; ++playStop)
        {
            physics.RebuildFromScene(scene);
            for (int s = 0; s < 30; ++s) physics.StepSimulation(1.0f / 60.0f, scene);
            EXPECT_TRUE(scene.Transforms().TryGet(prop)->Position.y < 3.5f, "TestL: PIE step fell");
            scene.RestoreSnapshot(snap);
            physics.RebuildFromScene(scene);
            EXPECT_TRUE(std::abs(scene.Transforms().TryGet(prop)->Position.y - 4.0f) < 1e-4f, "TestL: Stop restored exact Y=4");
        }

        // 3. Destroy entity & verify stale handle operations cleanly return false
        scene.DestroyEntity(prop);
        EXPECT_TRUE(!physics.AddLinearImpulse(scene, prop, { 1, 0, 0 }), "TestL: Stale entity AddLinearImpulse returns false");
        EXPECT_TRUE(!physics.SetLinearVelocity(scene, prop, { 1, 0, 0 }), "TestL: Stale entity SetLinearVelocity returns false");
        EXPECT_TRUE(!physics.SyncEntityBody(scene, prop), "TestL: Stale entity SyncEntityBody returns false");
        EXPECT_TRUE(!physics.DestroyBody(prop), "TestL: Duplicate DestroyBody on stale entity returns false");

        // 4. Repeated body creation & removal churn (500 cycles)
        for (int i = 0; i < 500; ++i)
        {
            const Scene::EntityId temp = CreateBox(scene, "ChurnBox", { 0, 2.0f, 0 }, { 0.5f, 0.5f, 0.5f }, Scene::RigidBodyMotionType::Dynamic);
            physics.SyncEntityBody(scene, temp);
            scene.DestroyEntity(temp);
        }
        EXPECT_TRUE(physics.GetBodyCount() == 1u, "TestL: Only BaseFloor remains after 500 churn cycles");

        // 5. Scene clear + ScriptEngine NotifySceneCleared verification
        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        EXPECT_TRUE(scripts.Initialize(hostDesc, scene, physics, audio).IsSuccess(), "TestL Script init");
        scene.Clear();
        EXPECT_TRUE(physics.GetBodyCount() == 0u, "TestL: Scene clear removed all bodies");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        std::cout << "[PASS] TestL_LifecycleShutdownReinitAndStaleHandles\n";
    }

    struct ProcessMemMB
    {
        f64 WorkingSet = 0.0;
        f64 PrivateCommit = 0.0;
    };

    ProcessMemMB SampleProcessMemMB()
    {
        ProcessMemMB m{};
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        {
            m.WorkingSet = static_cast<f64>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            m.PrivateCommit = static_cast<f64>(pmc.PrivateUsage) / (1024.0 * 1024.0);
        }
        return m;
    }

    std::filesystem::path FindPhysicsValidationFixtureDir()
    {
        const std::filesystem::path exeDir = GetExecutableDir();
        const std::filesystem::path c1 = exeDir / "Fixtures" / "PhysicsGameplayValidation";
        if (std::filesystem::exists(c1 / "Assets" / "Scenes" / "PhysicsGameplayValidation.mscene.json")) return c1;
        const std::filesystem::path c2 = exeDir.parent_path().parent_path().parent_path() / "Tests" / "PhysicsTests" / "Fixtures" / "PhysicsGameplayValidation";
        if (std::filesystem::exists(c2 / "Assets" / "Scenes" / "PhysicsGameplayValidation.mscene.json")) return c2;
        return c1;
    }

    void TestM_Phase1And2_CSharpJointsAndCompoundColliders(const std::filesystem::path& managedDir)
    {
        Scene::Scene scene;
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            scripts.NotifySceneCleared();
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "TestM Physics init");
        EXPECT_TRUE(audio.Initialize(true), "TestM Audio init");
        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        EXPECT_TRUE(scripts.Initialize(hostDesc, scene, physics, audio).IsSuccess(), "TestM Script init");

        CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 80.0f, 0.5f, 80.0f }, Scene::RigidBodyMotionType::Static);
        physics.RebuildFromScene(scene);

        const std::filesystem::path workDir = GetExecutableDir() / "TempCSharpAuditM";
        const std::filesystem::path scriptsDir = workDir / "Scripts";
        const std::filesystem::path outDll = workDir / "AuditM.dll";
        ClearTempDir(workDir);

        const std::string csAuditM = R"CS(
using System;
using Morbi.Engine;

namespace PhysicsAudit;

[EntityScript]
public sealed class JointAndCompoundAuditDriver : Script
{
    private JointHandle _fixedJoint;
    private JointHandle _distJoint;
    private JointHandle _hingeJoint;
    private JointHandle _sliderJoint;
    private JointHandle _breakableJoint;
    private JointHandle _ephemeralJoint;
    private JointHandle _entityBDestroyJoint;
    private Entity? _fixA;
    private Entity? _fixB;
    private Entity? _pendBob;
    private Entity? _hingeDoor;
    private Entity? _sliderCar;
    private Entity? _breakB;
    private Entity? _ephemeralEnt;
    private Entity? _linkKeepA;
    private Entity? _linkDropB;
    private Entity? _compoundShip;
    private CompoundCollider? _compoundComp;
    private Entity? _projPass;
    private Entity? _projHit;
    private int _step;

    public override void OnStart()
    {
        // 1. Fixed Joint
        _fixA = Scene.CreateEntity("FixA");
        _fixB = Scene.CreateEntity("FixB");
        if (_fixA != null && _fixB != null)
        {
            _fixA.GetTransform().Position = new Vec3(-20.0f, 1.5f, 0.0f);
            _fixB.GetTransform().Position = new Vec3(-18.8f, 1.5f, 0.0f);
            var rbA = _fixA.AddComponent<RigidBody>(); if (rbA != null) { rbA.UseGravity = false; rbA.Mass = 2.0f; }
            var rbB = _fixB.AddComponent<RigidBody>(); if (rbB != null) { rbB.UseGravity = false; rbB.Mass = 2.0f; }
            _fixedJoint = Physics.CreateFixedJoint(_fixA, _fixB);
            _fixA.GetRigidBody()?.AddImpulse(new Vec3(0.0f, 0.0f, 12.0f));
        }

        // 2. Distance Joint (Pendulum + Spring)
        var pendAnchor = Scene.CreateEntity("PendAnchor");
        _pendBob = Scene.CreateEntity("PendBob");
        if (pendAnchor != null && _pendBob != null)
        {
            pendAnchor.GetTransform().Position = new Vec3(-10.0f, 6.0f, 0.0f);
            var rbP = pendAnchor.AddComponent<RigidBody>(); if (rbP != null) rbP.MotionType = MotionType.Static;
            _pendBob.GetTransform().Position = new Vec3(-8.0f, 6.0f, 0.0f);
            var rbB = _pendBob.AddComponent<RigidBody>(); if (rbB != null) { rbB.Mass = 2.0f; rbB.HalfExtents = new Vec3(0.35f, 0.35f, 0.35f); }
            _distJoint = Physics.CreateDistanceJoint(_pendBob, pendAnchor, 0.5f, 2.05f, 5.0f, 0.5f);
        }

        // 3. Hinge Joint + runtime SetLimits/SetFriction edit
        var hingePost = Scene.CreateEntity("HingePost");
        _hingeDoor = Scene.CreateEntity("HingeDoor");
        if (hingePost != null && _hingeDoor != null)
        {
            hingePost.GetTransform().Position = new Vec3(-0.35f, 1.5f, 0.0f);
            var rbPost = hingePost.AddComponent<RigidBody>(); if (rbPost != null) { rbPost.MotionType = MotionType.Static; rbPost.HalfExtents = new Vec3(0.2f, 1.2f, 0.2f); }
            _hingeDoor.GetTransform().Position = new Vec3(1.1f, 1.5f, 0.0f);
            var rbD = _hingeDoor.AddComponent<RigidBody>();
            if (rbD != null) { rbD.UseGravity = false; rbD.Mass = 2.0f; rbD.HalfExtents = new Vec3(0.8f, 1.0f, 0.1f); }
            _hingeJoint = Physics.CreateHingeJoint(_hingeDoor, hingePost, new Vec3(0.25f, 1.5f, 0.0f), Vec3.Up, -1.0f, 1.0f, 0.1f);
            _hingeJoint.SetLimits(-1.8f, 1.8f);
            _hingeJoint.SetFriction(0.05f);
            _hingeDoor.GetRigidBody()?.AddTorqueImpulse(new Vec3(0.0f, 4.0f, 0.0f));
        }

        // 4. Slider Joint (along +X even under +X/+Y diagonal impulse)
        var slideAnchor = Scene.CreateEntity("SlideAnchor");
        _sliderCar = Scene.CreateEntity("SlideCar");
        if (slideAnchor != null && _sliderCar != null)
        {
            slideAnchor.GetTransform().Position = new Vec3(10.0f, 1.5f, 0.0f);
            var rbSA = slideAnchor.AddComponent<RigidBody>(); if (rbSA != null) rbSA.MotionType = MotionType.Static;
            _sliderCar.GetTransform().Position = new Vec3(11.0f, 1.5f, 0.0f);
            var rbSC = _sliderCar.AddComponent<RigidBody>();
            if (rbSC != null) { rbSC.UseGravity = false; rbSC.Mass = 2.0f; rbSC.HalfExtents = new Vec3(0.35f, 0.35f, 0.35f); }
            _sliderJoint = Physics.CreateSliderJoint(_sliderCar, slideAnchor, new Vec3(10.5f, 1.5f, 0.0f), Vec3.Right, -0.2f, 3.0f, 0.05f);
            _sliderCar.GetRigidBody()?.AddImpulse(new Vec3(10.0f, 8.0f, 5.0f));
        }

        // 5. Breakable Joint + Generational Invalidation
        var breakA = Scene.CreateEntity("BreakA");
        _breakB = Scene.CreateEntity("BreakB");
        if (breakA != null && _breakB != null)
        {
            breakA.GetTransform().Position = new Vec3(20.0f, 2.0f, 0.0f);
            var rbA = breakA.AddComponent<RigidBody>(); if (rbA != null) rbA.MotionType = MotionType.Static;
            _breakB.GetTransform().Position = new Vec3(21.0f, 2.0f, 0.0f);
            var rbB = _breakB.AddComponent<RigidBody>(); if (rbB != null) { rbB.Mass = 1.0f; rbB.HalfExtents = new Vec3(0.35f, 0.35f, 0.35f); }
            _breakableJoint = Physics.CreateFixedJoint(_breakB, breakA, breakForce: 150.0f);
        }

        // 6. Ephemeral EntityA Joint & EntityB Joint destruction verification
        _ephemeralEnt = Scene.CreateEntity("EphEnt");
        if (_ephemeralEnt != null)
        {
            _ephemeralEnt.GetTransform().Position = new Vec3(30.0f, 1.0f, 0.0f);
            var rbE = _ephemeralEnt.AddComponent<RigidBody>(); if (rbE != null) rbE.MotionType = MotionType.Dynamic;
            _ephemeralJoint = Physics.CreateFixedJoint(_ephemeralEnt, null);
        }

        _linkKeepA = Scene.CreateEntity("LinkKeepA");
        _linkDropB = Scene.CreateEntity("LinkDropB");
        if (_linkKeepA != null && _linkDropB != null)
        {
            _linkKeepA.GetTransform().Position = new Vec3(34.0f, 1.0f, 0.0f);
            _linkDropB.GetTransform().Position = new Vec3(35.5f, 1.0f, 0.0f);
            _linkKeepA.AddComponent<RigidBody>();
            _linkDropB.AddComponent<RigidBody>();
            _entityBDestroyJoint = Physics.CreateFixedJoint(_linkKeepA, _linkDropB);
        }

        // 7. Compound Collider with distinct sub-shape Layer/Mask filtering
        _compoundShip = Scene.CreateEntity("CompoundPlatform");
        if (_compoundShip != null)
        {
            _compoundShip.GetTransform().Position = new Vec3(0.0f, 2.0f, 12.0f);
            var rbC = _compoundShip.AddComponent<RigidBody>();
            if (rbC != null)
            {
                rbC.MotionType = MotionType.Static;
                rbC.HalfExtents = new Vec3(0.25f, 0.25f, 0.25f);
                rbC.CollisionLayer = CollisionLayers.LayerBit(0);
                rbC.CollisionMask = CollisionLayers.LayerBit(0); // center ignores projectiles (layer 4)
            }
            _compoundComp = _compoundShip.AddComponent<CompoundCollider>();
            if (_compoundComp != null)
            {
                // Sub-shape #1 at X=-2.0: Layer bit 2, Mask bit 2 -> ignores layer 4 projectile!
                _compoundComp.AddBox(
                    new Vec3(1.0f, 0.25f, 1.0f),
                    new Vec3(-2.0f, 0.0f, 0.0f),
                    Quat.Identity,
                    collisionLayer: CollisionLayers.LayerBit(2),
                    collisionMask: CollisionLayers.LayerBit(2));

                // Sub-shape #2 at X=+2.0: Layer bit 0, Mask ALL -> blocks layer 4 projectile!
                _compoundComp.AddSphere(
                    0.9f,
                    new Vec3(2.0f, 0.0f, 0.0f),
                    collisionLayer: CollisionLayers.LayerBit(0),
                    collisionMask: CollisionLayers.AllMask);

                // Sub-shape #3 (will be updated/removed cleanly via API check)
                _compoundComp.AddCapsule(0.3f, 0.4f, new Vec3(0.0f, 1.0f, 0.0f));
                var s0 = _compoundComp.GetColliders();
                if (s0.Length == 3)
                {
                    var mod = s0[2];
                    mod.LocalOffset = new Vec3(0.0f, 1.2f, 0.0f);
                    _compoundComp.UpdateCollider(2, mod);
                    _compoundComp.RemoveCollider(2);
                }
            }
        }

        _projPass = Scene.CreateEntity("ProjPassArm");
        if (_projPass != null)
        {
            _projPass.GetTransform().Position = new Vec3(-2.0f, 4.0f, 12.0f);
            var rbP1 = _projPass.AddComponent<RigidBody>();
            if (rbP1 != null)
            {
                rbP1.Shape = ColliderShape.Sphere;
                rbP1.HalfExtents = new Vec3(0.35f, 0.35f, 0.35f);
                rbP1.CollisionLayer = CollisionLayers.LayerBit(4);
                rbP1.CollisionMask = CollisionLayers.AllMask;
            }
        }

        _projHit = Scene.CreateEntity("ProjHitArm");
        if (_projHit != null)
        {
            _projHit.GetTransform().Position = new Vec3(2.0f, 4.0f, 12.0f);
            var rbP2 = _projHit.AddComponent<RigidBody>();
            if (rbP2 != null)
            {
                rbP2.Shape = ColliderShape.Sphere;
                rbP2.HalfExtents = new Vec3(0.35f, 0.35f, 0.35f);
                rbP2.CollisionLayer = CollisionLayers.LayerBit(4);
                rbP2.CollisionMask = CollisionLayers.LayerBit(0) | CollisionLayers.LayerBit(4);
            }
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _step++;

        if (_step == 2 && _breakableJoint.IsValid && _ephemeralJoint.IsValid && _entityBDestroyJoint.IsValid)
        {
            Scene.CreateEntity("M_JointsInitialValidOK");
            _ephemeralEnt?.Destroy();
            _linkDropB?.Destroy(); // Destroy connected EntityB while EntityA stays alive
        }
        if (_step == 4 && !_ephemeralJoint.IsValid && !_ephemeralJoint.Destroy() && !_ephemeralJoint.SetFriction(1.0f) &&
            !_entityBDestroyJoint.IsValid && (_linkKeepA?.IsAlive ?? false))
        {
            Scene.CreateEntity("M_StaleHandleRejectedOK");
        }

        if (_step == 20 && _breakableJoint.IsValid && _breakB != null)
        {
            // Huge impulse exceeds BreakForce = 150N -> snaps joint
            _breakB.GetRigidBody()?.AddImpulse(new Vec3(0.0f, -45.0f, 0.0f));
        }
        if (_step == 30 && !_breakableJoint.IsValid && !_breakableJoint.TryGetState(out _))
        {
            Scene.CreateEntity("M_BreakableJointSnappedOK");
        }

        if (_step == 45)
        {
            if (_fixA != null && _fixB != null)
            {
                float d = Vec3.Distance(_fixA.GetTransform().Position, _fixB.GetTransform().Position);
                if (_fixA.GetTransform().Position.Z > 1.5f && MathF.Abs(d - 1.2f) < 0.1f)
                    Scene.CreateEntity("M_FixedJointOK");
            }
            if (_pendBob != null && _distJoint.IsValid && _distJoint.TryGetState(out JointState djState) &&
                _pendBob.GetTransform().Position.Y < 5.5f && _pendBob.GetTransform().Position.Y > 3.5f && djState.AppliedForce > 0.1f)
            {
                Scene.CreateEntity("M_DistanceJointOK");
            }
            if (_hingeJoint.IsValid && _hingeJoint.TryGetState(out JointState hjState) &&
                _hingeJoint.TryGetDesc(out JointDesc hjDesc) &&
                MathF.Abs(hjState.PositionOrAngle) > 0.2f && MathF.Abs(hjDesc.MaxLimit - 1.8f) < 0.01f)
            {
                Scene.CreateEntity("M_HingeJointEditOK");
            }
            if (_sliderCar != null && _sliderJoint.IsValid &&
                _sliderCar.GetTransform().Position.X > 12.2f &&
                MathF.Abs(_sliderCar.GetTransform().Position.Y - 1.5f) < 0.1f &&
                MathF.Abs(_sliderCar.GetTransform().Position.Z) < 0.1f)
            {
                Scene.CreateEntity("M_SliderJointOK");
            }
        }
        if (_step == 65)
        {
            // At t = 1.08s: _projPass (Layer 4 vs Arm1 Mask Bit 2 -> ignored!) falls to floor Y~0.35 (<1.2),
            // while _projHit (Layer 4 vs Arm2 Mask All -> collides!) stays atop Arm2 at Y~3.25 (>2.6).
            if (_projPass != null && _projHit != null && _compoundComp != null &&
                _compoundComp.Count == 2 && _compoundComp.Rebuild() &&
                _projPass.GetTransform().Position.Y < 1.2f &&
                _projHit.GetTransform().Position.Y > 2.6f)
            {
                Scene.CreateEntity("M_CompoundLayerMaskOK");
            }
        }
    }
}
)CS";
        EXPECT_TRUE(WriteTextFile(scriptsDir / "AuditM.cs", csAuditM), "Write AuditM.cs");
        const Scene::EntityId driver = scene.CreateEntity("AuditMDriver");
        AttachCSharpScript(scene, driver, "PhysicsAudit.JointAndCompoundAuditDriver");

        auto cRes = scripts.CompileProjectScripts(scriptsDir, outDll, true, false);
        EXPECT_TRUE(cRes.Success, "Compile AuditM: " + cRes.Diagnostics);
        StepEngine(physics, scene, scripts, 1.25f); // 75 steps

        EXPECT_TRUE(scene.FindEntityByName("M_JointsInitialValidOK").IsValid(), "TestM: Joint handles initially valid");
        EXPECT_TRUE(scene.FindEntityByName("M_StaleHandleRejectedOK").IsValid(), "TestM: Destroyed entity invalidates joint handle & rejects stale ops");
        EXPECT_TRUE(scene.FindEntityByName("M_BreakableJointSnappedOK").IsValid(), "TestM: Breakable Fixed joint snapped under impulse & invalidated handle");
        EXPECT_TRUE(scene.FindEntityByName("M_FixedJointOK").IsValid(), "TestM: C# FixedJoint locked two dynamic bodies");
        EXPECT_TRUE(scene.FindEntityByName("M_DistanceJointOK").IsValid(), "TestM: C# DistanceJoint pendulum & state force");
        EXPECT_TRUE(scene.FindEntityByName("M_HingeJointEditOK").IsValid(), "TestM: C# HingeJoint + SetLimits/SetFriction live edit");
        EXPECT_TRUE(scene.FindEntityByName("M_SliderJointOK").IsValid(), "TestM: C# SliderJoint locked lateral/vertical axes");
        EXPECT_TRUE(scene.FindEntityByName("M_CompoundLayerMaskOK").IsValid(), "TestM: C# CompoundCollider Add/Update/Remove + subshape Layer/Mask filtering");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        ClearTempDir(workDir);
        std::cout << "[PASS] TestM_Phase1And2_CSharpJointsAndCompoundColliders\n";
    }

    void TestN_Phase4_RealGameplayValidationScene(const std::filesystem::path& managedDir)
    {
        const std::filesystem::path fixtureDir = FindPhysicsValidationFixtureDir();
        const std::filesystem::path scenePath = fixtureDir / "Assets" / "Scenes" / "PhysicsGameplayValidation.mscene.json";
        const std::filesystem::path scriptsDir = fixtureDir / "Scripts";
        EXPECT_TRUE(std::filesystem::exists(scenePath), "GameplayValidation scene exists: " + scenePath.string());
        EXPECT_TRUE(std::filesystem::exists(scriptsDir), "GameplayValidation scripts exist: " + scriptsDir.string());

        Scene::Scene scene;
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            scripts.NotifySceneCleared();
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "TestN Physics init");
        EXPECT_TRUE(audio.Initialize(true), "TestN Audio init");
        EXPECT_TRUE(scene.LoadFromJsonFile(scenePath), "TestN Load PhysicsGameplayValidation.mscene.json");

        // Verify JSON save -> reload preserves CompoundColliders & PhysicsJoints cleanly
        const std::filesystem::path workDir = GetExecutableDir() / "TempGameplayValidationN";
        ClearTempDir(workDir);
        const std::filesystem::path savedPath = workDir / "roundtrip.mscene.json";
        EXPECT_TRUE(scene.SaveToJsonFile(savedPath), "TestN Save roundtrip scene");
        EXPECT_TRUE(scene.LoadFromJsonFile(savedPath), "TestN Reload roundtrip scene");
        EXPECT_TRUE(scene.CompoundCollidersMap().size() >= 1u, "TestN CompoundColliders preserved across JSON save/load");
        EXPECT_TRUE(scene.PhysicsJointsMap().size() >= 4u, "TestN All 4 PhysicsJoints preserved across JSON save/load");

        physics.RebuildFromScene(scene);
        EXPECT_TRUE(physics.GetStats().ActiveJointCount == 4u, "TestN All 4 authored scene joints active in Jolt");

        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        EXPECT_TRUE(scripts.Initialize(hostDesc, scene, physics, audio).IsSuccess(), "TestN Script init");
        const std::filesystem::path outDll = workDir / "GameplayValidation.dll";
        auto cRes = scripts.CompileProjectScripts(scriptsDir, outDll, true, true);
        EXPECT_TRUE(cRes.Success, "TestN Compile PhysicsGameplayValidation.cs: " + cRes.Diagnostics);

        StepEngine(physics, scene, scripts, 6.0f); // 360 steps

        EXPECT_TRUE(scene.FindEntityByName("GV_RbHitCrate").IsValid(), "GameplayScene: RbPlayer collided with DynamicCrate");
        EXPECT_TRUE(scene.FindEntityByName("GV_RbHitBarrel").IsValid(), "GameplayScene: RbPlayer collided with DynamicBarrel");
        EXPECT_TRUE(scene.FindEntityByName("GV_RbHitDoor").IsValid(), "GameplayScene: RbPlayer hit HingeDoor");
        EXPECT_TRUE(scene.FindEntityByName("GV_HingeDoorSwungOK").IsValid(), "GameplayScene: HingeDoor joint swung open");
        EXPECT_TRUE(scene.FindEntityByName("GV_CharGrounded").IsValid(), "GameplayScene: CharPlayer grounded");
        EXPECT_TRUE(scene.FindEntityByName("GV_CharStairsOK").IsValid(), "GameplayScene: CharPlayer climbed stairs");
        EXPECT_TRUE(scene.FindEntityByName("GV_CharMovingPlatformOK").IsValid(), "GameplayScene: CharPlayer rode MovingPlatform");
        EXPECT_TRUE(scene.FindEntityByName("GV_RampClimbOK").IsValid(), "GameplayScene: RampClimber ascended slope");
        EXPECT_TRUE(scene.FindEntityByName("GV_GoalTriggerOK").IsValid(), "GameplayScene: GoalTrigger fired OnTriggerEnter");
        EXPECT_TRUE(scene.FindEntityByName("GV_SliderMechanismOK").IsValid(), "GameplayScene: SliderCarriage slid along Slider joint");
        EXPECT_TRUE(scene.FindEntityByName("GV_FixedJointCoupledOK").IsValid(), "GameplayScene: FixedPayload coupled via Fixed joint");
        EXPECT_TRUE(scene.FindEntityByName("GV_DistancePendulumOK").IsValid(), "GameplayScene: PendulumBob swung on Distance joint");
        EXPECT_TRUE(scene.FindEntityByName("GV_CompoundTotemOK").IsValid(), "GameplayScene: CompoundTotem stable multi-shape physics");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        ClearTempDir(workDir);
        std::cout << "[PASS] TestN_Phase4_RealGameplayValidationScene\n";
    }

    void TestO_Phase5_LongRunStabilityAndLeakAudit(const std::filesystem::path& managedDir)
    {
        Scene::Scene scene("LongRunStability");
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        Audio::AudioWorld audio;
        Script::ScriptEngine scripts;

        scene.SetEntityDestroyedCallback([&](Scene::EntityId eid) {
            scripts.NotifyEntityDestroyed(eid);
            physics.DestroyBody(eid);
        });
        scene.SetSceneClearedCallback([&]() {
            scripts.NotifySceneCleared();
            physics.ClearAllBodies(nullptr);
        });

        EXPECT_TRUE(physics.Initialize(), "TestO Physics init");
        EXPECT_TRUE(audio.Initialize(true), "TestO Audio init");
        Script::ScriptHostDesc hostDesc{};
        hostDesc.ManagedAssemblyDir = managedDir;
        EXPECT_TRUE(scripts.Initialize(hostDesc, scene, physics, audio).IsSuccess(), "TestO Script init");

        const std::filesystem::path workDir = GetExecutableDir() / "TempStabilityAuditO";
        const std::filesystem::path scriptsDir = workDir / "Scripts";
        const std::filesystem::path outDll = workDir / "StabilityO.dll";
        ClearTempDir(workDir);

        const std::string csStability = R"CS(
using System;
using Morbi.Engine;

namespace PhysicsStability;

[EntityScript]
public sealed class OrbitController : Script
{
    private CharacterController? _cc;
    private JointHandle _hinge;
    private Entity? _lever;
    private RigidBody? _crateRb;
    private int _tick;
    public int TriggerEvents { get; private set; }

    public override void OnStart()
    {
        _cc = Scene.FindEntity("StabHero")?.GetCharacterController();
        _lever = Scene.FindEntity("StabLever");
        _crateRb = Scene.FindEntity("StabCrate")?.GetRigidBody();
        if (_lever != null)
        {
            var joints = Physics.GetEntityJoints(_lever);
            if (joints.Length > 0) _hinge = joints[0];
            else _hinge = Physics.CreateHingeJoint(_lever, null, new Vec3(-2.0f, 1.5f, 0.0f), Vec3.Up, -1.5f, 1.5f, 0.05f);
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _tick++;
        float phase = _tick * fixedDeltaTime;
        if (_cc != null)
        {
            _cc.Move(new Vec3(MathF.Cos(phase) * 2.5f * fixedDeltaTime, 0.0f, MathF.Sin(phase) * 2.5f * fixedDeltaTime));
        }
        if ((_tick % 120) == 0 && _crateRb != null)
        {
            _crateRb.AddImpulse(new Vec3(0.0f, 3.0f, 0.0f));
        }
        if ((_tick % 180) == 0 && _lever != null && _hinge.IsValid)
        {
            _lever.GetRigidBody()?.AddTorqueImpulse(new Vec3(0.0f, 1.5f, 0.0f));
        }
    }

    public override void OnTriggerEnter(Collider other) => TriggerEvents++;
}
)CS";
        EXPECT_TRUE(WriteTextFile(scriptsDir / "StabilityO.cs", csStability), "Write StabilityO.cs");

        auto setupWorld = [&]() {
            scene.Clear();
            CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 40.0f, 0.5f, 40.0f }, Scene::RigidBodyMotionType::Static);
            const Scene::EntityId crate = CreateBox(scene, "StabCrate", { 3.0f, 0.6f, 0.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic, 1.5f, false);
            scene.RigidBodies().TryGet(crate)->Restitution = 0.3f;
            scene.AddCompoundCollider(crate, Scene::CompoundColliderEntry{
                .Shape = Scene::ColliderShapeType::Sphere,
                .HalfExtents = { 0.25f, 0.25f, 0.25f },
                .LocalOffset = { 0.0f, 0.5f, 0.0f }
            });
            const Scene::EntityId lever = CreateBox(scene, "StabLever", { -1.2f, 1.5f, 0.0f }, { 0.8f, 0.4f, 0.1f }, Scene::RigidBodyMotionType::Dynamic, 2.0f);
            scene.RigidBodies().TryGet(lever)->UseGravity = 0u;
            scene.AddEntityJoint(lever, Scene::PhysicsJointEntry{
                .Type = Scene::PhysicsJointType::Hinge,
                .ConnectedEntity = Scene::EntityId::Invalid(),
                .AnchorA = { -2.0f, 1.5f, 0.0f },
                .AnchorB = { -2.0f, 1.5f, 0.0f },
                .Axis = { 0.0f, 1.0f, 0.0f },
                .AutoDetectAnchors = true,
                .MinLimit = -1.5f,
                .MaxLimit = 1.5f,
                .MaxFriction = 0.05f
            });
            const Scene::EntityId hero = scene.CreateEntity("StabHero");
            scene.Transforms().TryGet(hero)->Position = { 0.0f, 0.5f, 0.0f };
            Scene::CharacterControllerComponent cc{};
            cc.Radius = 0.35f;
            cc.Height = 1.8f;
            scene.CharacterControllers().EmplaceOrReplace(hero, cc);
            const Scene::EntityId drv = scene.CreateEntity("StabDriver");
            AttachCSharpScript(scene, drv, "PhysicsStability.OrbitController");
            physics.RebuildFromScene(scene);
        };

        setupWorld();
        auto cRes = scripts.CompileProjectScripts(scriptsDir, outDll, true, true);
        EXPECT_TRUE(cRes.Success, "Compile StabilityO.cs: " + cRes.Diagnostics);

        // 1. 30-Minute Simulated Time (108,000 fixed 60 Hz steps) continuous C# + Jolt stability test
        constexpr int kThirtyMinSteps = 108000;
        const ProcessMemMB memStart = SampleProcessMemMB();
        const auto wallStart = std::chrono::steady_clock::now();
        f64 maxStepMs = 0.0;
        for (int s = 1; s <= kThirtyMinSteps; ++s)
        {
            const auto t0 = std::chrono::steady_clock::now();
            scripts.FixedTickManagedScripts(1.0f / 60.0f);
            physics.StepSimulation(1.0f / 60.0f, scene);
            scripts.DispatchPhysicsEvents();
            const auto t1 = std::chrono::steady_clock::now();
            maxStepMs = std::max(maxStepMs, std::chrono::duration<f64, std::milli>(t1 - t0).count());

            if (s % 36000 == 0)
            {
                const auto curStats = physics.GetStats();
                const auto curMem = SampleProcessMemMB();
                EXPECT_TRUE(curStats.ActiveJointCount == 1u, "Joint count steady during 30m sim");
                EXPECT_TRUE(curStats.CharacterCount == 1u, "Character count steady during 30m sim");
                const f64 elapsedS = std::chrono::duration<f64>(t1 - wallStart).count();
                std::cout << std::fixed << std::setprecision(2)
                          << "  [LONG-RUN] SimTime=" << (s / 3600) << "m (" << s << " steps)"
                          << " | Wall=" << elapsedS << "s | MaxStep=" << maxStepMs << " ms"
                          << " | WS=" << curMem.WorkingSet << " MB | Commit=" << curMem.PrivateCommit << " MB\n";
            }
        }
        const ProcessMemMB memEnd30m = SampleProcessMemMB();
        const f64 commitDriftMb = memEnd30m.PrivateCommit - memStart.PrivateCommit;
        EXPECT_TRUE(commitDriftMb < 64.0, "Memory commit drift over 108,000 steps bounded (<64 MB), got " + std::to_string(commitDriftMb));
        for (const auto& tr : scene.Transforms().RawComponents())
        {
            EXPECT_TRUE(std::isfinite(tr.Position.x) && std::isfinite(tr.Position.y) && std::isfinite(tr.Position.z) && tr.Position.y > -2.0f,
                "All entities finite & bounded after 30m simulated time");
        }

        // 2. Repeated PIE Start/Stop, Scene Clear/Reload, C# Hot-Reload, Entity+RB+CC+Joint churn
        const std::filesystem::path reloadScenePath = workDir / "stability_reload.mscene.json";
        EXPECT_TRUE(scene.SaveToJsonFile(reloadScenePath), "Save stability scene for reload cycles");
        const auto pieSnap = scene.CreateSnapshot();

        for (int cycle = 0; cycle < 15; ++cycle)
        {
            scene.RestoreSnapshot(pieSnap);
            physics.RebuildFromScene(scene);
            EXPECT_TRUE(scripts.LoadOrHotReloadAssembly(outDll).IsSuccess(), "PIE/HotReload cycle " + std::to_string(cycle));
            StepEngine(physics, scene, scripts, 0.25f);
        }

        for (int r = 0; r < 10; ++r)
        {
            EXPECT_TRUE(scene.LoadFromJsonFile(reloadScenePath), "Reload cycle");
            physics.RebuildFromScene(scene);
            EXPECT_TRUE(scripts.LoadOrHotReloadAssembly(outDll).IsSuccess(), "Reload hotreload");
            StepEngine(physics, scene, scripts, 0.15f);
        }

        // Dynamic Entity + RigidBody + CharacterController + CompoundCollider + Joint churn
        scene.Clear();
        EXPECT_TRUE(physics.GetStats().TotalJoltBodies == 0u, "TestO: Zero Jolt bodies after Clear");
        EXPECT_TRUE(physics.GetStats().ActiveJointCount == 0u, "TestO: Zero joints after Clear");
        EXPECT_TRUE(physics.GetStats().CharacterCount == 0u, "TestO: Zero characters after Clear");

        CreateBox(scene, "FloorChurn", { 0.0f, -0.5f, 0.0f }, { 50.0f, 0.5f, 50.0f }, Scene::RigidBodyMotionType::Static);
        physics.RebuildFromScene(scene);
        for (int c = 0; c < 300; ++c)
        {
            const Scene::EntityId a = CreateBox(scene, "CA", { 0.0f, 2.0f, 0.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic);
            const Scene::EntityId b = CreateBox(scene, "CB", { 1.2f, 2.0f, 0.0f }, { 0.4f, 0.4f, 0.4f }, Scene::RigidBodyMotionType::Dynamic);
            scene.AddCompoundCollider(a, Scene::CompoundColliderEntry{
                .Shape = Scene::ColliderShapeType::Sphere,
                .HalfExtents = { 0.3f, 0.3f, 0.3f },
                .LocalOffset = { 0.0f, 0.6f, 0.0f }
            });
            physics.SyncEntityCompoundColliders(scene, a);
            physics.SyncEntityBody(scene, a);
            physics.SyncEntityBody(scene, b);
            const auto jh = physics.CreateJoint(scene, Physics::JointDesc{
                .Type = Physics::JointType::Fixed,
                .EntityA = a,
                .EntityB = b
            });
            EXPECT_TRUE(physics.IsJointValid(jh), "Churn joint valid");

            const Scene::EntityId ch = scene.CreateEntity("CChar");
            scene.Transforms().TryGet(ch)->Position = { -3.0f, 1.0f, 0.0f };
            scene.CharacterControllers().EmplaceOrReplace(ch, Scene::CharacterControllerComponent{});
            physics.SyncCharacterController(scene, ch);

            physics.StepSimulation(1.0f / 60.0f, scene);

            scene.DestroyEntity(a);
            scene.DestroyEntity(b);
            scene.DestroyEntity(ch);
            EXPECT_TRUE(!physics.IsJointValid(jh), "Churn joint auto-invalidated on entity destroy");
            EXPECT_TRUE(!physics.DestroyJoint(jh), "Duplicate DestroyJoint on destroyed joint returns false");
        }

        scene.Clear();
        physics.StepSimulation(1.0f / 60.0f, scene);
        scripts.DispatchPhysicsEvents();
        EXPECT_TRUE(physics.GetStats().TotalJoltBodies == 0u, "TestO End: 0 leftover Jolt bodies");
        EXPECT_TRUE(physics.GetStats().ActiveJointCount == 0u, "TestO End: 0 leftover joints");
        EXPECT_TRUE(physics.GetStats().CharacterCount == 0u, "TestO End: 0 leftover characters");
        EXPECT_TRUE(physics.GetRecentCollisions().empty(), "TestO End: 0 phantom collision events after Clear");

        scripts.Shutdown();
        physics.Shutdown();
        audio.Shutdown();
        ClearTempDir(workDir);
        std::cout << "[PASS] TestO_Phase5_LongRunStabilityAndLeakAudit\n";
    }
}

int main()
{
    Core::Log::SetMinLevel(Core::LogLevel::Warn);
    Core::Log::Info("CSharpPhysicsTests", "Starting MorbiEngine C# <-> Jolt Physics Integration Tests...");
    const std::filesystem::path exeDir = GetExecutableDir();
    std::filesystem::path managedDir = exeDir / "Managed";
    if (!std::filesystem::exists(managedDir / "Morbi.Engine.dll"))
    {
        managedDir = exeDir.parent_path() / "Managed";
    }

    TestA_to_F_ManagedPhysicsIntegration(managedDir);
    TestJ_CompleteCSharpRigidBodyAndQueriesAudit(managedDir);
    TestK_CompleteCSharpCharacterAudit(managedDir);
    TestL_LifecycleShutdownReinitAndStaleHandles(managedDir);
    TestM_Phase1And2_CSharpJointsAndCompoundColliders(managedDir);
    TestN_Phase4_RealGameplayValidationScene(managedDir);
    TestO_Phase5_LongRunStabilityAndLeakAudit(managedDir);

    if (g_failures > 0)
    {
        std::cerr << "C# PHYSICS TESTS FAILED: " << g_failures << " failure(s).\n";
        return 1;
    }

    std::cout << "ALL C# PHYSICS INTEGRATION TESTS PASSED!\n";
    return 0;
}
