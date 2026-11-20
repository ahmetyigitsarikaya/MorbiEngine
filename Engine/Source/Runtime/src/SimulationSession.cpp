#include "Morbi/Runtime/SimulationSession.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Morbi/Audio/AudioWorld.hpp"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Task/TaskScheduler.hpp"
#include "Morbi/IO/PathService.hpp"
#include "Morbi/Network/Serialization/BitStream.hpp"
#include "Morbi/Physics/PhysicsWorld.hpp"
#include "Morbi/Scene/Scene.hpp"
#include "Morbi/Script/ScriptEngine.hpp"

namespace Morbi::Runtime
{
    namespace
    {
        std::filesystem::path FindRepoRoot(const std::filesystem::path& startDir)
        {
            std::error_code ec;
            for (std::filesystem::path probe = startDir; !probe.empty() && probe != probe.root_path(); probe = probe.parent_path())
            {
                if (std::filesystem::exists(probe / "CMakePresets.json", ec) && !ec)
                {
                    return probe;
                }
            }
            return startDir;
        }

        std::filesystem::path FindManagedDir(const std::filesystem::path& engineBinDir, const std::filesystem::path& repoRoot)
        {
            std::error_code ec;
            const std::vector<std::filesystem::path> candidates = {
                engineBinDir / "Managed",
                engineBinDir.parent_path() / "Managed",
                repoRoot / "build" / "bin" / "Managed"
            };
            for (const auto& cand : candidates)
            {
                if (std::filesystem::exists(cand / "Morbi.Engine.runtimeconfig.json", ec) && !ec)
                {
                    return cand;
                }
            }
            return engineBinDir / "Managed";
        }

        bool HasCSharpSourceFiles(const std::filesystem::path& scriptsDir)
        {
            std::error_code ec;
            if (scriptsDir.empty() || !std::filesystem::exists(scriptsDir, ec) || ec)
            {
                return false;
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir, ec))
            {
                if (!ec && entry.is_regular_file() && entry.path().extension() == ".cs")
                {
                    return true;
                }
            }
            return false;
        }
    }

    Core::Result<ProjectDescriptor, std::string> LoadProjectDescriptorFromFile(
        const std::filesystem::path& morbiprojPath)
    {
        std::error_code ec;
        const auto canonicalPath = std::filesystem::weakly_canonical(morbiprojPath, ec);
        if (ec || !std::filesystem::exists(canonicalPath, ec))
        {
            return Core::Result<ProjectDescriptor, std::string>::Failure(
                "Project file (.morbiproj) not found: " + morbiprojPath.string());
        }

        std::ifstream in(canonicalPath);
        if (!in.is_open())
        {
            return Core::Result<ProjectDescriptor, std::string>::Failure(
                "Failed to open project file: " + canonicalPath.string());
        }

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            return Core::Result<ProjectDescriptor, std::string>::Failure(
                "Malformed JSON in project file: " + canonicalPath.string());
        }

        auto getString = [&](const char* key, const std::string& fallback) -> std::string {
            if (auto it = root.find(key); it != root.end() && it->is_string())
            {
                return it->get<std::string>();
            }
            return fallback;
        };

        ProjectDescriptor desc{};
        desc.ProjectFilePath = canonicalPath;
        desc.ProjectRootDir = canonicalPath.parent_path();
        desc.ProjectName = getString("project_name", "MorbiSandbox");
        desc.ProjectVersion = getString("project_version", "1.0.0");
        desc.EngineVersion = getString("engine_version", "1.0.0");

        const std::string assetsRel = getString("assets_dir", "Assets");
        const std::string scriptsRel = getString("scripts_dir", "Scripts");
        desc.AssetsDir = desc.ProjectRootDir / assetsRel;
        desc.ScriptsDir = desc.ProjectRootDir / scriptsRel;
        desc.StartupScene = getString("startup_scene", "assets://DefaultScene.mscene.json");
        desc.ManagedAssembly = getString("managed_assembly", "Morbi.Engine.dll");
        desc.EntryScriptClass = getString("entry_script_class", "Morbi.Engine.PlayerController");
        desc.DefaultRHI = getString("default_rhi", "vulkan");
        desc.WindowTitle = desc.ProjectName;

        if (auto winIt = root.find("window"); winIt != root.end() && winIt->is_object())
        {
            if (auto t = winIt->find("title"); t != winIt->end() && t->is_string())
            {
                desc.WindowTitle = t->get<std::string>();
            }
            if (auto w = winIt->find("width"); w != winIt->end() && w->is_number_unsigned())
            {
                desc.WindowWidth = w->get<u32>();
            }
            if (auto h = winIt->find("height"); h != winIt->end() && h->is_number_unsigned())
            {
                desc.WindowHeight = h->get<u32>();
            }
            if (auto v = winIt->find("vsync"); v != winIt->end() && v->is_boolean())
            {
                desc.VSync = v->get<bool>();
            }
        }

        return Core::Result<ProjectDescriptor, std::string>::Success(std::move(desc));
    }

    struct WorldHost
    {
        Network::WorldInstanceId WorldId = 1;
        std::string Name = "ServerWorld";
        Scene::Scene WorldScene{ "ServerWorld" };
        Physics::PhysicsWorld Physics{ 1.0f / 60.0f };
        bool PhysicsInitialized = false;
        SceneReplicationBridge Replication;
        bool ReplicationInitialized = false;
    };

    struct SimulationSession::Impl
    {
        Core::TaskScheduler TaskScheduler;
        IO::PathService Vfs;
        std::map<Network::WorldInstanceId, std::unique_ptr<WorldHost>> Worlds;
        Network::WorldInstanceId DefaultWorldId = 1;
        Audio::AudioWorld Audio;
        Script::ScriptEngine Scripts;
        Network::ConnectionManager Network;
        AppMessageCallback OnAppMessage;

        [[nodiscard]] WorldHost* FindWorld(Network::WorldInstanceId worldId) noexcept
        {
            auto it = Worlds.find(worldId);
            return it != Worlds.end() ? it->second.get() : nullptr;
        }

        [[nodiscard]] const WorldHost* FindWorld(Network::WorldInstanceId worldId) const noexcept
        {
            auto it = Worlds.find(worldId);
            return it != Worlds.end() ? it->second.get() : nullptr;
        }

        [[nodiscard]] WorldHost* DefaultWorld() noexcept
        {
            return FindWorld(DefaultWorldId);
        }

        [[nodiscard]] const WorldHost* DefaultWorld() const noexcept
        {
            return FindWorld(DefaultWorldId);
        }

        [[nodiscard]] WorldHost* FindWorldForSession(Network::SessionId sessionId) noexcept
        {
            if (sessionId.IsValid())
            {
                Network::NetworkSession sess{};
                if (Network.GetSessionRegistry().TryGetSession(sessionId, sess))
                {
                    if (WorldHost* w = FindWorld(sess.WorldId))
                    {
                        return w;
                    }
                }
            }
            return DefaultWorld();
        }
    };

    SimulationSession::SimulationSession()
        : m_impl(std::make_unique<Impl>())
    {
    }

    SimulationSession::~SimulationSession()
    {
        Shutdown();
    }

    Core::Result<bool, std::string> SimulationSession::Initialize(const SimulationLaunchConfig& config)
    {
        if (m_initialized)
        {
            return Core::Result<bool, std::string>::Failure("SimulationSession is already initialized.");
        }

        if (config.TickRate == 0 || config.TickRate > 1000)
        {
            return Core::Result<bool, std::string>::Failure("Invalid TickRate (must be in range [1..1000]).");
        }
        if (config.Capabilities.Networking && config.Capabilities.ServerAuthority && config.Port == 0)
        {
            return Core::Result<bool, std::string>::Failure("Invalid server bind port (0).");
        }
        if (config.Capabilities.Networking && config.MaxClients == 0)
        {
            return Core::Result<bool, std::string>::Failure("MaxClients must be greater than 0.");
        }

        m_config = config;
        m_tickStats = ServerTickStats{};
        m_tickStats.TargetTickRate = config.TickRate;
        m_report = RuntimeVerificationReport{};
        m_report.Profile = config.Profile;
        m_accumulatorSeconds = 0.0;
        m_totalTickDurationMs = 0.0;
        m_hadClientConnection = false;

        // Ensure clean teardown if any step fails midway
        auto abortInit = [&](const std::string& reason) -> Core::Result<bool, std::string> {
            Shutdown();
            return Core::Result<bool, std::string>::Failure(reason);
        };

        const std::filesystem::path engineBinDir = config.EngineBinDir.empty()
            ? std::filesystem::current_path()
            : config.EngineBinDir;
        const std::filesystem::path repoRoot = FindRepoRoot(engineBinDir);

        ProjectDescriptor projectDesc{};
        if (!config.ProjectFilePath.empty())
        {
            auto projRes = LoadProjectDescriptorFromFile(config.ProjectFilePath);
            if (!projRes)
            {
                return abortInit(projRes.Error());
            }
            projectDesc = projRes.Value();
        }

        const std::filesystem::path projectRoot = !projectDesc.ProjectRootDir.empty()
            ? projectDesc.ProjectRootDir
            : repoRoot;
        const std::filesystem::path assetsRoot = !projectDesc.AssetsDir.empty()
            ? projectDesc.AssetsDir
            : (projectRoot / "Assets");
        const std::filesystem::path scriptsRoot = !config.ScriptsDir.empty()
            ? config.ScriptsDir
            : (!projectDesc.ScriptsDir.empty() ? projectDesc.ScriptsDir : (projectRoot / "Scripts"));
        const std::filesystem::path cacheRoot = engineBinDir / "cache";

        m_impl->Vfs.Mount("engine", std::filesystem::exists(repoRoot / "Engine") ? (repoRoot / "Engine") : engineBinDir);
        m_impl->Vfs.Mount("project", projectRoot);
        m_impl->Vfs.Mount("assets", assetsRoot);
        m_impl->Vfs.Mount("scripts", scriptsRoot);
        m_impl->Vfs.Mount("cache", cacheRoot);

        // 1. Default World ECS & Scene
        m_impl->Worlds.clear();
        m_impl->DefaultWorldId = (config.Replication.WorldId != Network::kInvalidWorldInstanceId)
            ? config.Replication.WorldId
            : 1u;

        auto defaultWorld = std::make_unique<WorldHost>();
        defaultWorld->WorldId = m_impl->DefaultWorldId;
        defaultWorld->Name = "ServerWorld";
        defaultWorld->WorldScene.SetName("ServerWorld");
        m_report.ECSInitialized = true;

        std::filesystem::path resolvedScenePath = config.ScenePath;
        if (resolvedScenePath.empty() && !projectDesc.StartupScene.empty())
        {
            if (auto resolved = m_impl->Vfs.Resolve(projectDesc.StartupScene))
            {
                resolvedScenePath = *resolved;
            }
        }

        if (!config.ScenePath.empty())
        {
            if (auto vfsScene = m_impl->Vfs.Resolve(config.ScenePath.string()))
            {
                resolvedScenePath = *vfsScene;
            }
            if (!std::filesystem::exists(resolvedScenePath))
            {
                return abortInit("Specified scene file not found: " + config.ScenePath.string());
            }
            if (!defaultWorld->WorldScene.LoadFromJsonFile(resolvedScenePath))
            {
                return abortInit("Failed to parse scene file: " + resolvedScenePath.string());
            }
        }
        else if (!resolvedScenePath.empty() && std::filesystem::exists(resolvedScenePath))
        {
            if (!defaultWorld->WorldScene.LoadFromJsonFile(resolvedScenePath))
            {
                return abortInit("Failed to parse project startup scene: " + resolvedScenePath.string());
            }
        }
        m_report.SceneInitialized = true;

        WorldHost* rawDefaultWorld = defaultWorld.get();
        defaultWorld->WorldScene.SetEntityDestroyedCallback([this, rawDefaultWorld](Scene::EntityId eid) {
            if (m_report.ScriptingInitialized)
            {
                m_impl->Scripts.NotifyEntityDestroyed(eid);
            }
            if (rawDefaultWorld->PhysicsInitialized)
            {
                rawDefaultWorld->Physics.DestroyBody(eid);
            }
            if (m_report.NetworkingInitialized && rawDefaultWorld->ReplicationInitialized)
            {
                rawDefaultWorld->Replication.OnSceneEntityDestroyed(eid, m_impl->Network.GetSessionRegistry());
            }
        });
        defaultWorld->WorldScene.SetSceneClearedCallback([this, rawDefaultWorld]() {
            if (m_report.ScriptingInitialized)
            {
                m_impl->Scripts.NotifySceneCleared();
            }
            if (rawDefaultWorld->PhysicsInitialized)
            {
                rawDefaultWorld->Physics.ClearAllBodies(nullptr);
            }
            if (m_report.NetworkingInitialized && rawDefaultWorld->ReplicationInitialized)
            {
                rawDefaultWorld->Replication.OnSceneCleared(m_impl->Network.GetSessionRegistry());
            }
        });

        // 2. Jolt Physics for Default World
        if (config.Capabilities.Physics)
        {
            defaultWorld->Physics.SetFixedTimestep(1.0f / static_cast<f32>(config.TickRate));
            if (!defaultWorld->Physics.Initialize())
            {
                return abortInit("PhysicsWorld::Initialize() failed.");
            }
            defaultWorld->Physics.RebuildFromScene(defaultWorld->WorldScene);
            defaultWorld->PhysicsInitialized = true;
            m_report.PhysicsInitialized = true;
        }
        m_impl->Worlds.emplace(m_impl->DefaultWorldId, std::move(defaultWorld));

        // 3. Audio (Disabled on DedicatedServer)
        if (config.Capabilities.Audio)
        {
            if (!m_impl->Audio.Initialize(false))
            {
                return abortInit("AudioWorld::Initialize() failed.");
            }
            m_report.AudioInitialized = true;
        }

        // 4. CoreCLR Scripting
        if (config.Capabilities.Scripting)
        {
            const std::filesystem::path managedDir = FindManagedDir(engineBinDir, repoRoot);
            m_impl->Scripts.BindVFS(&m_impl->Vfs);
            m_impl->Scripts.BindGameUI(nullptr);
            m_impl->Scripts.BindInput(nullptr);

            WorldHost* defWorld = m_impl->DefaultWorld();
            const auto scriptInit = m_impl->Scripts.Initialize(
                Script::ScriptHostDesc{
                    .ManagedAssemblyDir = managedDir,
                    .HostAssemblyName = "Morbi.Engine"
                },
                defWorld->WorldScene,
                defWorld->Physics,
                m_impl->Audio);
            if (!scriptInit)
            {
                return abortInit("ScriptEngine::Initialize() failed: " + scriptInit.Error());
            }
            m_report.ScriptingInitialized = true;

            if (HasCSharpSourceFiles(scriptsRoot))
            {
                const std::filesystem::path outDll = cacheRoot / "server_scripts" / "ServerGameScripts.dll";
                const auto compileRes = m_impl->Scripts.CompileProjectScripts(scriptsRoot, outDll, true, true);
                if (!compileRes.Success)
                {
                    return abortInit("Server C# script compilation failed:\n" + compileRes.Diagnostics);
                }
            }
        }

        // 5. Morbi.Network
        if (config.Capabilities.Networking)
        {
            Network::ConnectionConfig netCfg{};
            netCfg.Mode = ToNetworkMode(config.Profile);
            netCfg.Transport.BindEndpoint = Network::Endpoint{ config.BindAddress, config.Port };
            netCfg.Transport.MaxConnections = config.MaxClients;
            netCfg.SchemaHash = config.SchemaHash;
            netCfg.Security = config.NetworkSecurity;

            auto netInit = m_impl->Network.Initialize(netCfg);
            if (!netInit)
            {
                return abortInit("ConnectionManager::Initialize() failed: " + netInit.Error().Message);
            }

            WorldHost* defWorld = m_impl->DefaultWorld();
            ReplicationLaunchConfig defRepCfg = config.Replication;
            defRepCfg.WorldId = m_impl->DefaultWorldId;
            defWorld->Replication.Initialize(config.Capabilities.ServerAuthority, defRepCfg, m_impl->Network);
            defWorld->ReplicationInitialized = true;

            if (m_report.ScriptingInitialized)
            {
                auto wireWorldCallbacks = [this](SceneReplicationBridge& bridge) {
                    bridge.SetOnManagedRpc([this](
                        Network::WorldInstanceId worldId,
                        Network::NetworkId netId,
                        u32 behaviourHash,
                        u32 methodHash,
                        Network::PlayerId senderPlayerId,
                        bool isServerSide,
                        std::span<const u8> argsBytes) -> bool {
                        return m_impl->Scripts.DispatchManagedRpc(
                            worldId,
                            netId.Pack(),
                            behaviourHash,
                            methodHash,
                            senderPlayerId,
                            isServerSide,
                            argsBytes);
                    });
                    bridge.SetOnManagedPropertySync([this](
                        Network::WorldInstanceId worldId,
                        Network::NetworkId netId,
                        u32 behaviourHash,
                        std::span<const u8> propsBytes) -> bool {
                        return m_impl->Scripts.DispatchManagedPropertySync(
                            worldId,
                            netId.Pack(),
                            behaviourHash,
                            propsBytes);
                    });
                    bridge.SetOnNetworkSpawn([this](
                        Network::WorldInstanceId worldId,
                        Network::NetworkId netId,
                        Scene::EntityId sceneEntity,
                        Network::PlayerId ownerPlayerId,
                        bool isServer,
                        bool isClient,
                        bool isOwner) {
                        (void)m_impl->Scripts.NotifyNetworkSpawn(
                            worldId,
                            netId.Pack(),
                            sceneEntity.Pack(),
                            ownerPlayerId,
                            isServer,
                            isClient,
                            isOwner);
                    });
                    bridge.SetOnNetworkDespawn([this](
                        Network::WorldInstanceId worldId,
                        Network::NetworkId netId) {
                        (void)m_impl->Scripts.NotifyNetworkDespawn(worldId, netId.Pack());
                    });
                };

                wireWorldCallbacks(defWorld->Replication);

                Script::NetworkBridgeCallbacks bridgeCbs{};
                bridgeCbs.GetObjectState = [this](
                    u32 worldId,
                    u64 netIdPacked,
                    u64* outSceneEntityPacked,
                    u64* outOwnerPlayerId,
                    u8* outIsServer,
                    u8* outIsClient,
                    u8* outIsOwner) -> bool {
                    WorldHost* w = m_impl->FindWorld(worldId);
                    if (w == nullptr || !w->ReplicationInitialized)
                    {
                        return false;
                    }
                    const Network::NetworkId netId = Network::NetworkId::Unpack(netIdPacked);
                    const Scene::EntityId eid = w->Replication.ResolveSceneEntity(netId);
                    if (!eid.IsValid() || !w->WorldScene.IsAlive(eid))
                    {
                        return false;
                    }
                    if (m_config.Capabilities.ServerAuthority)
                    {
                        Network::ServerReplicatedEntity ent{};
                        if (!w->Replication.GetServerReplication().TryGetEntity(netId, ent))
                        {
                            return false;
                        }
                        if (outSceneEntityPacked) *outSceneEntityPacked = eid.Pack();
                        if (outOwnerPlayerId) *outOwnerPlayerId = ent.OwnerPlayer;
                        if (outIsServer) *outIsServer = 1;
                        if (outIsClient) *outIsClient = 0;
                        if (outIsOwner) *outIsOwner = (ent.OwnerPlayer == Network::kInvalidPlayerId) ? 1 : 0;
                        return true;
                    }
                    Network::ClientReplicatedEntity ent{};
                    if (!w->Replication.GetClientApplier().TryGetEntity(netId, ent))
                    {
                        return false;
                    }
                    if (outSceneEntityPacked) *outSceneEntityPacked = eid.Pack();
                    if (outOwnerPlayerId) *outOwnerPlayerId = ent.OwnerPlayer;
                    if (outIsServer) *outIsServer = 0;
                    if (outIsClient) *outIsClient = 1;
                    if (outIsOwner) *outIsOwner = (w->Replication.GetLocalOwnedNetId() == netId) ? 1 : 0;
                    return true;
                };
                bridgeCbs.ResolveEntityNetworkId = [this](
                    u32 worldId,
                    u64 sceneEntityPacked,
                    u64* outNetIdPacked) -> bool {
                    WorldHost* w = m_impl->FindWorld(worldId);
                    if (w == nullptr || !w->ReplicationInitialized || outNetIdPacked == nullptr)
                    {
                        return false;
                    }
                    const Scene::EntityId eid = Scene::EntityId::Unpack(sceneEntityPacked);
                    if (!eid.IsValid() || !w->WorldScene.IsAlive(eid))
                    {
                        return false;
                    }
                    const Network::NetworkId netId = w->Replication.ResolveNetworkId(eid);
                    if (!netId.IsValid())
                    {
                        return false;
                    }
                    *outNetIdPacked = netId.Pack();
                    return true;
                };
                bridgeCbs.SendManagedRpc = [this](
                    u32 worldId,
                    u64 netIdPacked,
                    u32 behaviourHash,
                    u32 methodHash,
                    u8 isServerRpc,
                    u8 flags,
                    std::span<const u8> argsPayload) -> bool {
                    WorldHost* w = m_impl->FindWorld(worldId);
                    if (w == nullptr || !w->ReplicationInitialized)
                    {
                        return false;
                    }
                    return w->Replication.SendManagedRpc(
                        worldId,
                        Network::NetworkId::Unpack(netIdPacked),
                        behaviourHash,
                        methodHash,
                        isServerRpc != 0,
                        flags,
                        argsPayload,
                        m_impl->Network);
                };
                bridgeCbs.SyncManagedProperties = [this](
                    u32 worldId,
                    u64 netIdPacked,
                    u32 behaviourHash,
                    std::span<const u8> propsPayload) -> bool {
                    WorldHost* w = m_impl->FindWorld(worldId);
                    if (w == nullptr || !w->ReplicationInitialized)
                    {
                        return false;
                    }
                    return w->Replication.SyncManagedProperties(
                        worldId,
                        Network::NetworkId::Unpack(netIdPacked),
                        behaviourHash,
                        propsPayload,
                        m_impl->Network);
                };
                m_impl->Scripts.BindNetworkBridge(std::move(bridgeCbs));
            }
            if (config.Capabilities.ServerAuthority && defRepCfg.Enabled)
            {
                if (!defWorld->Replication.RegisterStaticSceneEntities(defWorld->WorldScene, m_tickStats.TickCount))
                {
                    return abortInit("Failed to register static replicated scene entities (invalid or duplicate static_id).");
                }
            }

            m_impl->Network.SetOnConnected([this](const Network::ConnectionInfo& info, const std::string& reason) {
                m_hadClientConnection = true;
                Core::Log::Info("Server", "Client authenticated: handle=" + std::to_string(info.Handle.Value) +
                    " session=" + info.Session.ToHexString() +
                    " playerId=" + std::to_string(info.AuthenticatedPlayerId) +
                    " from " + info.RemoteEndpoint.Host + ":" + std::to_string(info.RemoteEndpoint.Port) +
                    " (Active=" + std::to_string(m_impl->Network.GetConnectionCount()) + ")");
                if (m_config.Capabilities.ServerAuthority)
                {
                    WorldHost* targetWorld = m_impl->FindWorldForSession(info.Session);
                    if (targetWorld != nullptr && targetWorld->ReplicationInitialized)
                    {
                        targetWorld->Replication.OnServerClientAuthenticated(
                            info,
                            reason == "Reconnected",
                            targetWorld->WorldScene,
                            m_impl->Network,
                            m_tickStats.TickCount);
                    }
                }
            });

            m_impl->Network.SetOnDisconnected([this](const Network::ConnectionInfo& info, const std::string& reason) {
                Core::Log::Info("Server", "Client disconnected: handle=" + std::to_string(info.Handle.Value) +
                    " (" + reason + ", Active=" + std::to_string(m_impl->Network.GetConnectionCount()) + ")");
            });

            m_impl->Network.SetOnSessionClosed([this](Network::SessionId sessionId, Network::ConnectionHandle) {
                if (m_config.Capabilities.ServerAuthority)
                {
                    WorldHost* targetWorld = m_impl->FindWorldForSession(sessionId);
                    if (targetWorld != nullptr && targetWorld->ReplicationInitialized)
                    {
                        targetWorld->Replication.OnServerSessionClosed(
                            sessionId,
                            targetWorld->WorldScene,
                            m_impl->Network);
                    }
                }
            });

            m_impl->Network.SetOnMessageReceived([this](const Network::NetworkMessageEvent& ev) {
                if (ev.Header.Category == Network::MessageCategory::Replication ||
                    ev.Header.Category == Network::MessageCategory::Rpc)
                {
                    WorldHost* targetWorld = m_impl->FindWorldForSession(ev.Session);
                    if (targetWorld != nullptr && targetWorld->ReplicationInitialized)
                    {
                        targetWorld->Replication.HandleNetworkMessage(ev, targetWorld->WorldScene, m_impl->Network);
                    }
                }
                else if (m_config.Capabilities.ServerAuthority && ev.Header.Category == Network::MessageCategory::Application)
                {
                    if (ev.Header.SubType == static_cast<u8>(ServerAppMessageSubType::PingEcho))
                    {
                        Network::BitReader r(ev.Payload);
                        std::string msg;
                        if (r.ReadString(msg))
                        {
                            Network::BitWriter reply(512);
                            if (reply.WriteString("PONG:" + msg))
                            {
                                (void)m_impl->Network.SendPacket(
                                    ev.Connection,
                                    Network::MessageCategory::Application,
                                    static_cast<u8>(ServerAppMessageSubType::PingEcho),
                                    reply.GetData(),
                                    Network::NetDelivery::ReliableOrdered,
                                    Network::NetChannel::GameplayEvents);
                            }
                        }
                    }
                    else if (ev.Header.SubType == static_cast<u8>(ServerAppMessageSubType::StatusQuery))
                    {
                        Network::BitWriter reply(256);
                        WorldHost* qWorld = m_impl->FindWorldForSession(ev.Session);
                        const u32 entityCount = qWorld ? qWorld->WorldScene.GetAliveEntityCount() : 0u;
                        if (reply.WriteU64(m_tickStats.TickCount) &&
                            reply.WriteU32(static_cast<u32>(m_impl->Network.GetConnectionCount())) &&
                            reply.WriteU32(entityCount) &&
                            reply.WriteF64(m_tickStats.SimulationTimeSeconds))
                        {
                            (void)m_impl->Network.SendPacket(
                                ev.Connection,
                                Network::MessageCategory::Application,
                                static_cast<u8>(ServerAppMessageSubType::StatusResponse),
                                reply.GetData(),
                                Network::NetDelivery::ReliableOrdered,
                                Network::NetChannel::GameplayEvents);
                        }
                    }
                }

                if (m_impl->OnAppMessage)
                {
                    m_impl->OnAppMessage(ev);
                }
            });

            m_report.NetworkingInitialized = true;
        }

        m_initialized = true;

        Core::Log::Info("Runtime", std::string("Profile: ") + ToString(m_report.Profile));
        Core::Log::Info("Runtime", std::string("Windowing: ") + (m_report.WindowingInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("Rendering: ") + (m_report.RenderingInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("RHI: ") + (m_report.RHIInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("Editor UI: ") + (m_report.EditorUIInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("Audio: ") + (m_report.AudioInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("Input: ") + (m_report.InputInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("ECS: ") + (m_report.ECSInitialized ? "Initialized" : "Disabled"));
        WorldHost* logWorld = m_impl->DefaultWorld();
        Core::Log::Info("Runtime", std::string("Scene: ") + (m_report.SceneInitialized ? "Initialized" : "Disabled") +
            " (Name='" + (logWorld ? logWorld->WorldScene.GetName() : std::string("None")) +
            "', Entities=" + std::to_string(logWorld ? logWorld->WorldScene.GetAliveEntityCount() : 0u) + ")");
        Core::Log::Info("Runtime", std::string("Physics: ") + (m_report.PhysicsInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("CoreCLR: ") + (m_report.ScriptingInitialized ? "Initialized" : "Disabled"));
        Core::Log::Info("Runtime", std::string("Networking: ") + (m_report.NetworkingInitialized ? "Initialized" : "Disabled"));

        if (m_report.NetworkingInitialized && config.Capabilities.ServerAuthority)
        {
            const std::string pubHex = Network::CryptoContext::HexEncode(m_impl->Network.GetServerIdentityPublicKey());
            Core::Log::Info("Server", "Listening on " + config.BindAddress + ":" + std::to_string(config.Port) +
                " | TickRate=" + std::to_string(config.TickRate) + "Hz | MaxClients=" + std::to_string(config.MaxClients) +
                " | Security=X25519+Ed25519+XChaCha20Poly1305 | TokenAuth=" +
                std::string((config.NetworkSecurity.RequireSignedConnectToken || config.NetworkSecurity.HasTokenIssuerPublicKey) ? "Required" : "Challenge") +
                " | ServerPubKey=" + pubHex);
        }

        return Core::Result<bool, std::string>::Success(true);
    }

    void SimulationSession::Shutdown()
    {
        // Reverse-dependency teardown, safe even when Initialize failed partway through.
        if (m_report.NetworkingInitialized)
        {
            for (auto& [worldId, worldPtr] : m_impl->Worlds)
            {
                (void)worldId;
                if (worldPtr && worldPtr->ReplicationInitialized)
                {
                    worldPtr->Replication.Shutdown(worldPtr->WorldScene, m_impl->Network);
                    worldPtr->ReplicationInitialized = false;
                }
            }
            const auto peers = m_impl->Network.GetConnections();
            for (const auto conn : peers)
            {
                m_impl->Network.Disconnect(conn, "ServerShutdown", false);
            }
            m_impl->Network.Poll();
            m_impl->Network.Shutdown();
            m_report.NetworkingInitialized = false;
        }

        for (auto& [worldId, worldPtr] : m_impl->Worlds)
        {
            (void)worldId;
            if (worldPtr)
            {
                worldPtr->WorldScene.SetEntityDestroyedCallback(nullptr);
                worldPtr->WorldScene.SetSceneClearedCallback(nullptr);
            }
        }

        if (m_report.ScriptingInitialized)
        {
            m_impl->Scripts.Shutdown();
            m_report.ScriptingInitialized = false;
        }

        for (auto& [worldId, worldPtr] : m_impl->Worlds)
        {
            (void)worldId;
            if (worldPtr && worldPtr->PhysicsInitialized)
            {
                worldPtr->Physics.Shutdown();
                worldPtr->PhysicsInitialized = false;
            }
        }
        m_report.PhysicsInitialized = false;

        if (m_report.AudioInitialized)
        {
            m_impl->Audio.Shutdown();
            m_report.AudioInitialized = false;
        }

        for (auto& [worldId, worldPtr] : m_impl->Worlds)
        {
            (void)worldId;
            if (worldPtr)
            {
                worldPtr->WorldScene.Clear();
            }
        }
        m_impl->Worlds.clear();
        m_report.ECSInitialized = false;
        m_report.SceneInitialized = false;
        m_initialized = false;
    }

    void SimulationSession::StepFixedTick()
    {
        if (!m_initialized)
        {
            return;
        }

        const auto tickStart = std::chrono::steady_clock::now();
        const f32 fixedDt = 1.0f / static_cast<f32>(m_config.TickRate);

        // 1. Poll incoming network events and process per-world inputs prior to Physics substep
        if (m_report.NetworkingInitialized)
        {
            m_impl->Network.Poll();
            if (m_config.Capabilities.ServerAuthority)
            {
                for (auto& [worldId, worldPtr] : m_impl->Worlds)
                {
                    (void)worldId;
                    if (worldPtr && worldPtr->ReplicationInitialized)
                    {
                        worldPtr->Replication.ProcessServerInputs(
                            fixedDt,
                            m_tickStats.TickCount,
                            worldPtr->WorldScene,
                            worldPtr->PhysicsInitialized ? &worldPtr->Physics : nullptr);
                    }
                }
            }
        }

        // 2-5. Authoritative physics, ECS, and managed scripting simulation per world (deterministic WorldId order)
        for (auto& [worldId, worldPtr] : m_impl->Worlds)
        {
            if (!worldPtr)
            {
                continue;
            }
            if (m_report.ScriptingInitialized)
            {
                m_impl->Scripts.SetActiveWorldContext(
                    worldId,
                    &worldPtr->WorldScene,
                    worldPtr->PhysicsInitialized ? &worldPtr->Physics : nullptr,
                    &m_impl->Audio);
            }
            const bool isDefaultWorld = (worldId == m_impl->DefaultWorldId);
            if (worldPtr->PhysicsInitialized)
            {
                worldPtr->Physics.StepSimulation(
                    fixedDt,
                    worldPtr->WorldScene,
                    &m_impl->TaskScheduler,
                    [this, isDefaultWorld](f32 dt) {
                        if (isDefaultWorld && m_report.ScriptingInitialized)
                        {
                            m_impl->Scripts.FixedTickManagedScripts(dt);
                        }
                    });
                if (isDefaultWorld && m_report.ScriptingInitialized)
                {
                    m_impl->Scripts.DispatchPhysicsEvents();
                }
            }
            else if (isDefaultWorld && m_report.ScriptingInitialized)
            {
                m_impl->Scripts.FixedTickManagedScripts(fixedDt);
            }

            if (isDefaultWorld && m_report.ScriptingInitialized)
            {
                m_impl->Scripts.TickManagedScripts(fixedDt);
            }

            worldPtr->WorldScene.UpdateScriptSystem(fixedDt, &m_impl->TaskScheduler);
            worldPtr->WorldScene.UpdateTransformHierarchySystem(&m_impl->TaskScheduler);

            if (m_report.NetworkingInitialized && m_config.Capabilities.ServerAuthority && worldPtr->ReplicationInitialized)
            {
                worldPtr->Replication.StepServerTick(
                    fixedDt,
                    m_tickStats.TickCount,
                    m_config.TickRate,
                    worldPtr->WorldScene,
                    worldPtr->PhysicsInitialized ? &worldPtr->Physics : nullptr,
                    m_impl->Network);
                if (m_report.ScriptingInitialized)
                {
                    (void)m_impl->Scripts.SyncManagedNetworkProperties(worldId);
                }
            }
        }

        if (m_report.ScriptingInitialized)
        {
            if (WorldHost* defWorld = m_impl->DefaultWorld())
            {
                m_impl->Scripts.SetActiveWorldContext(
                    m_impl->DefaultWorldId,
                    &defWorld->WorldScene,
                    defWorld->PhysicsInitialized ? &defWorld->Physics : nullptr,
                    &m_impl->Audio);
            }
        }

        // 6. Flush outgoing network packets
        if (m_report.NetworkingInitialized)
        {
            m_impl->Network.Poll();
        }

        const auto tickEnd = std::chrono::steady_clock::now();
        const f64 elapsedMs = std::chrono::duration<f64, std::milli>(tickEnd - tickStart).count();
        const f64 budgetMs = 1000.0 / static_cast<f64>(m_config.TickRate);

        ++m_tickStats.TickCount;
        m_tickStats.SimulationTimeSeconds += static_cast<f64>(fixedDt);
        m_tickStats.LastTickDurationMs = elapsedMs;
        m_totalTickDurationMs += elapsedMs;
        m_tickStats.AverageTickDurationMs = m_totalTickDurationMs / static_cast<f64>(m_tickStats.TickCount);
        if (elapsedMs > budgetMs)
        {
            ++m_tickStats.OverrunCount;
        }
    }

    u32 SimulationSession::StepSimulation(f32 deltaTime)
    {
        if (!m_initialized)
        {
            return 0;
        }

        constexpr f64 kMaxFrameDeltaSeconds = 0.25;
        constexpr u32 kMaxSubstepsPerCall = 4;

        const f64 clampedDelta = std::clamp(static_cast<f64>(deltaTime), 0.0, kMaxFrameDeltaSeconds);
        const f64 fixedDt = 1.0 / static_cast<f64>(m_config.TickRate);
        m_accumulatorSeconds += clampedDelta;

        u32 executed = 0;
        while (m_accumulatorSeconds >= fixedDt && executed < kMaxSubstepsPerCall)
        {
            StepFixedTick();
            m_accumulatorSeconds -= fixedDt;
            ++executed;
        }

        if (executed == kMaxSubstepsPerCall && m_accumulatorSeconds >= fixedDt)
        {
            // Prevent spiral of death if overloaded
            m_accumulatorSeconds = 0.0;
        }

        return executed;
    }

    int SimulationSession::RunServerLoop(const std::atomic<bool>& stopRequested)
    {
        if (!m_initialized)
        {
            return 1;
        }

        const auto fixedStepDur = std::chrono::duration<f64>(1.0 / static_cast<f64>(m_config.TickRate));
        auto nextTickTime = std::chrono::steady_clock::now();

        while (!stopRequested.load(std::memory_order_acquire))
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTickTime)
            {
                u32 substeps = 0;
                constexpr u32 kMaxCatchupTicks = 4;
                while (std::chrono::steady_clock::now() >= nextTickTime && substeps < kMaxCatchupTicks)
                {
                    StepFixedTick();
                    nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(fixedStepDur);
                    ++substeps;

                    if (m_config.MaxTicks > 0 && m_tickStats.TickCount >= m_config.MaxTicks)
                    {
                        Core::Log::Info("Server", "Reached MaxTicks (" + std::to_string(m_config.MaxTicks) + "). Shutting down.");
                        return 0;
                    }

                    if (m_config.ShutdownAfterDisconnect && m_hadClientConnection && GetActiveConnectionCount() == 0)
                    {
                        Core::Log::Info("Server", "All clients disconnected (--shutdown-after-disconnect). Shutting down.");
                        return 0;
                    }
                }

                // If server fell severely behind, reset target deadline rather than spinning catch-up forever
                if (substeps == kMaxCatchupTicks && std::chrono::steady_clock::now() > nextTickTime)
                {
                    nextTickTime = std::chrono::steady_clock::now() +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(fixedStepDur);
                }
            }
            else
            {
                const auto remaining = nextTickTime - now;
                if (remaining > std::chrono::milliseconds(2))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }

        Core::Log::Info("Server", "Shutdown signal received. Exiting server loop.");
        return 0;
    }

    Network::ConnectionManager& SimulationSession::GetNetworkManager() noexcept
    {
        return m_impl->Network;
    }

    const Network::ConnectionManager& SimulationSession::GetNetworkManager() const noexcept
    {
        return m_impl->Network;
    }

    SceneReplicationBridge& SimulationSession::GetReplicationBridge() noexcept
    {
        return m_impl->DefaultWorld()->Replication;
    }

    const SceneReplicationBridge& SimulationSession::GetReplicationBridge() const noexcept
    {
        return m_impl->DefaultWorld()->Replication;
    }

    Scene::Scene& SimulationSession::GetScene() noexcept
    {
        return m_impl->DefaultWorld()->WorldScene;
    }

    const Scene::Scene& SimulationSession::GetScene() const noexcept
    {
        return m_impl->DefaultWorld()->WorldScene;
    }

    Physics::PhysicsWorld& SimulationSession::GetPhysicsWorld() noexcept
    {
        return m_impl->DefaultWorld()->Physics;
    }

    const Physics::PhysicsWorld& SimulationSession::GetPhysicsWorld() const noexcept
    {
        return m_impl->DefaultWorld()->Physics;
    }

    Script::ScriptEngine& SimulationSession::GetScriptEngine() noexcept
    {
        return m_impl->Scripts;
    }

    Core::Result<bool, std::string> SimulationSession::CreateWorldInstance(const WorldInstanceDesc& desc)
    {
        if (!m_initialized)
        {
            return Core::Result<bool, std::string>::Failure("SimulationSession is not initialized.");
        }
        if (desc.WorldId == Network::kInvalidWorldInstanceId)
        {
            return Core::Result<bool, std::string>::Failure("Invalid WorldInstanceId (0).");
        }
        if (m_impl->Worlds.find(desc.WorldId) != m_impl->Worlds.end())
        {
            return Core::Result<bool, std::string>::Failure(
                "WorldInstanceId already exists: " + std::to_string(desc.WorldId));
        }

        auto world = std::make_unique<WorldHost>();
        world->WorldId = desc.WorldId;
        world->Name = desc.Name.empty() ? ("World_" + std::to_string(desc.WorldId)) : desc.Name;
        world->WorldScene.SetName(world->Name);

        if (!desc.ScenePath.empty())
        {
            std::filesystem::path resolved = desc.ScenePath;
            if (auto vfsPath = m_impl->Vfs.Resolve(desc.ScenePath.string()))
            {
                resolved = *vfsPath;
            }
            if (!std::filesystem::exists(resolved))
            {
                return Core::Result<bool, std::string>::Failure(
                    "World scene file not found: " + desc.ScenePath.string());
            }
            if (!world->WorldScene.LoadFromJsonFile(resolved))
            {
                return Core::Result<bool, std::string>::Failure(
                    "Failed to parse world scene file: " + resolved.string());
            }
        }

        WorldHost* rawWorld = world.get();
        world->WorldScene.SetEntityDestroyedCallback([this, rawWorld](Scene::EntityId eid) {
            if (rawWorld->PhysicsInitialized)
            {
                rawWorld->Physics.DestroyBody(eid);
            }
            if (m_report.NetworkingInitialized && rawWorld->ReplicationInitialized)
            {
                rawWorld->Replication.OnSceneEntityDestroyed(eid, m_impl->Network.GetSessionRegistry());
            }
        });
        world->WorldScene.SetSceneClearedCallback([this, rawWorld]() {
            if (rawWorld->PhysicsInitialized)
            {
                rawWorld->Physics.ClearAllBodies(nullptr);
            }
            if (m_report.NetworkingInitialized && rawWorld->ReplicationInitialized)
            {
                rawWorld->Replication.OnSceneCleared(m_impl->Network.GetSessionRegistry());
            }
        });

        if (m_config.Capabilities.Physics)
        {
            world->Physics.SetFixedTimestep(1.0f / static_cast<f32>(m_config.TickRate));
            if (!world->Physics.Initialize())
            {
                return Core::Result<bool, std::string>::Failure(
                    "PhysicsWorld::Initialize() failed for WorldInstanceId " + std::to_string(desc.WorldId));
            }
            world->Physics.RebuildFromScene(world->WorldScene);
            world->PhysicsInitialized = true;
        }

        if (m_report.NetworkingInitialized)
        {
            ReplicationLaunchConfig repCfg = desc.ReplicationOverride.has_value()
                ? *desc.ReplicationOverride
                : m_config.Replication;
            repCfg.WorldId = desc.WorldId;
            world->Replication.Initialize(m_config.Capabilities.ServerAuthority, repCfg, m_impl->Network);
            world->ReplicationInitialized = true;
            if (m_report.ScriptingInitialized)
            {
                world->Replication.SetOnManagedRpc([this](
                    Network::WorldInstanceId worldId,
                    Network::NetworkId netId,
                    u32 behaviourHash,
                    u32 methodHash,
                    Network::PlayerId senderPlayerId,
                    bool isServerSide,
                    std::span<const u8> argsBytes) -> bool {
                    return m_impl->Scripts.DispatchManagedRpc(
                        worldId,
                        netId.Pack(),
                        behaviourHash,
                        methodHash,
                        senderPlayerId,
                        isServerSide,
                        argsBytes);
                });
                world->Replication.SetOnManagedPropertySync([this](
                    Network::WorldInstanceId worldId,
                    Network::NetworkId netId,
                    u32 behaviourHash,
                    std::span<const u8> propsBytes) -> bool {
                    return m_impl->Scripts.DispatchManagedPropertySync(
                        worldId,
                        netId.Pack(),
                        behaviourHash,
                        propsBytes);
                });
                world->Replication.SetOnNetworkSpawn([this](
                    Network::WorldInstanceId worldId,
                    Network::NetworkId netId,
                    Scene::EntityId sceneEntity,
                    Network::PlayerId ownerPlayerId,
                    bool isServer,
                    bool isClient,
                    bool isOwner) {
                    (void)m_impl->Scripts.NotifyNetworkSpawn(
                        worldId,
                        netId.Pack(),
                        sceneEntity.Pack(),
                        ownerPlayerId,
                        isServer,
                        isClient,
                        isOwner);
                });
                world->Replication.SetOnNetworkDespawn([this](
                    Network::WorldInstanceId worldId,
                    Network::NetworkId netId) {
                    (void)m_impl->Scripts.NotifyNetworkDespawn(worldId, netId.Pack());
                });
            }
            if (m_config.Capabilities.ServerAuthority && repCfg.Enabled)
            {
                if (!world->Replication.RegisterStaticSceneEntities(world->WorldScene, m_tickStats.TickCount))
                {
                    world->Replication.Shutdown(world->WorldScene, m_impl->Network);
                    if (world->PhysicsInitialized)
                    {
                        world->Physics.Shutdown();
                    }
                    return Core::Result<bool, std::string>::Failure(
                        "Failed to register static replicated entities for WorldInstanceId " + std::to_string(desc.WorldId));
                }
            }
        }

        m_impl->Worlds.emplace(desc.WorldId, std::move(world));
        return Core::Result<bool, std::string>::Success(true);
    }

    bool SimulationSession::DestroyWorldInstance(Network::WorldInstanceId worldId)
    {
        if (!m_initialized || worldId == m_impl->DefaultWorldId)
        {
            return false;
        }
        auto it = m_impl->Worlds.find(worldId);
        if (it == m_impl->Worlds.end())
        {
            return false;
        }

        // Migrate any active sessions still in this world back to DefaultWorld cleanly
        if (m_report.NetworkingInitialized)
        {
            std::vector<Network::SessionId> sessionsInWorld;
            const auto conns = m_impl->Network.GetConnections();
            for (const auto conn : conns)
            {
                Network::ConnectionInfo ci{};
                if (m_impl->Network.TryGetConnectionInfo(conn, ci) && ci.Session.IsValid())
                {
                    Network::NetworkSession ns{};
                    if (m_impl->Network.GetSessionRegistry().TryGetSession(ci.Session, ns) &&
                        ns.WorldId == worldId)
                    {
                        sessionsInWorld.push_back(ci.Session);
                    }
                }
            }
            for (const auto& sid : sessionsInWorld)
            {
                (void)TransferSessionToWorld(sid, m_impl->DefaultWorldId);
            }
        }

        auto& world = it->second;
        if (m_report.ScriptingInitialized)
        {
            (void)m_impl->Scripts.NotifyWorldDestroyed(worldId);
        }
        if (world->ReplicationInitialized)
        {
            world->Replication.Shutdown(world->WorldScene, m_impl->Network);
            world->ReplicationInitialized = false;
        }
        world->WorldScene.SetEntityDestroyedCallback(nullptr);
        world->WorldScene.SetSceneClearedCallback(nullptr);
        if (world->PhysicsInitialized)
        {
            world->Physics.Shutdown();
            world->PhysicsInitialized = false;
        }
        world->WorldScene.Clear();
        m_impl->Worlds.erase(it);
        return true;
    }

    bool SimulationSession::TransferSessionToWorld(
        Network::SessionId sessionId,
        Network::WorldInstanceId targetWorldId)
    {
        if (!m_initialized || !m_report.NetworkingInitialized || !m_config.Capabilities.ServerAuthority ||
            !sessionId.IsValid())
        {
            return false;
        }

        WorldHost* targetWorld = m_impl->FindWorld(targetWorldId);
        if (targetWorld == nullptr || !targetWorld->ReplicationInitialized)
        {
            return false;
        }

        auto& sessions = m_impl->Network.GetSessionRegistry();
        Network::NetworkSession sess{};
        if (!sessions.TryGetSession(sessionId, sess) ||
            sess.LifecycleState != Network::SessionLifecycleState::Active)
        {
            return false;
        }

        if (sess.WorldId == targetWorldId)
        {
            return true;
        }

        if (WorldHost* oldWorld = m_impl->FindWorld(sess.WorldId))
        {
            if (oldWorld->ReplicationInitialized)
            {
                oldWorld->Replication.OnServerSessionClosed(sessionId, oldWorld->WorldScene, m_impl->Network);
                oldWorld->Replication.GetServerReplication().UnregisterConnection(sess.PrimaryConnection);
            }
        }

        if (!sessions.AssignSessionWorld(sessionId, targetWorldId))
        {
            return false;
        }

        Network::ConnectionInfo ci{};
        if (m_impl->Network.TryGetConnectionInfo(sess.PrimaryConnection, ci) &&
            ci.State == Network::ConnectionState::Connected)
        {
            targetWorld->Replication.OnServerClientAuthenticated(
                ci,
                false,
                targetWorld->WorldScene,
                m_impl->Network,
                m_tickStats.TickCount);
        }
        return true;
    }

    bool SimulationSession::HasWorldInstance(Network::WorldInstanceId worldId) const noexcept
    {
        return m_impl->FindWorld(worldId) != nullptr;
    }

    std::vector<Network::WorldInstanceId> SimulationSession::GetWorldIds() const
    {
        std::vector<Network::WorldInstanceId> ids;
        ids.reserve(m_impl->Worlds.size());
        for (const auto& [worldId, worldPtr] : m_impl->Worlds)
        {
            (void)worldPtr;
            ids.push_back(worldId);
        }
        return ids;
    }

    Network::WorldInstanceId SimulationSession::GetDefaultWorldId() const noexcept
    {
        return m_impl->DefaultWorldId;
    }

    Network::WorldInstanceId SimulationSession::GetSessionWorldId(Network::SessionId sessionId) const noexcept
    {
        if (!sessionId.IsValid() || !m_report.NetworkingInitialized)
        {
            return Network::kInvalidWorldInstanceId;
        }
        Network::NetworkSession sess{};
        if (m_impl->Network.GetSessionRegistry().TryGetSession(sessionId, sess))
        {
            return sess.WorldId;
        }
        return Network::kInvalidWorldInstanceId;
    }

    Scene::Scene* SimulationSession::TryGetWorldScene(Network::WorldInstanceId worldId) noexcept
    {
        WorldHost* w = m_impl->FindWorld(worldId);
        return w ? &w->WorldScene : nullptr;
    }

    Physics::PhysicsWorld* SimulationSession::TryGetWorldPhysics(Network::WorldInstanceId worldId) noexcept
    {
        WorldHost* w = m_impl->FindWorld(worldId);
        return (w && w->PhysicsInitialized) ? &w->Physics : nullptr;
    }

    SceneReplicationBridge* SimulationSession::TryGetWorldReplicationBridge(Network::WorldInstanceId worldId) noexcept
    {
        WorldHost* w = m_impl->FindWorld(worldId);
        return w ? &w->Replication : nullptr;
    }

    usize SimulationSession::GetActiveConnectionCount() const noexcept
    {
        return m_report.NetworkingInitialized ? m_impl->Network.GetConnectionCount() : 0u;
    }

    u32 SimulationSession::GetAliveEntityCount() const noexcept
    {
        const WorldHost* def = m_impl->DefaultWorld();
        return def ? def->WorldScene.GetAliveEntityCount() : 0u;
    }

    void SimulationSession::SetOnApplicationMessage(AppMessageCallback callback)
    {
        m_impl->OnAppMessage = std::move(callback);
    }
}
