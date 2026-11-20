#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Morbi/Core/Types.hpp"
#include "Morbi/Network/NetworkConfig.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Replication/InterestManagement.hpp"

namespace Morbi::Runtime
{
    enum class RuntimeProfile : u8
    {
        Editor = 0,
        GameClient = 1,
        ListenServer = 2,
        DedicatedServer = 3
    };

    [[nodiscard]] constexpr const char* ToString(RuntimeProfile profile) noexcept
    {
        switch (profile)
        {
        case RuntimeProfile::Editor:          return "Editor";
        case RuntimeProfile::GameClient:      return "GameClient";
        case RuntimeProfile::ListenServer:    return "ListenServer";
        case RuntimeProfile::DedicatedServer: return "DedicatedServer";
        default:                              return "Unknown";
        }
    }

    struct RuntimeCapabilities
    {
        bool Rendering = true;
        bool Windowing = true;
        bool Audio = true;
        bool Input = true;
        bool EditorUI = false;
        bool Physics = true;
        bool Scripting = true;
        bool Networking = true;
        bool ServerAuthority = false;
        bool ClientConnection = false;

        [[nodiscard]] static constexpr RuntimeCapabilities FromProfile(RuntimeProfile profile) noexcept
        {
            switch (profile)
            {
            case RuntimeProfile::Editor:
                return RuntimeCapabilities{
                    .Rendering = true,
                    .Windowing = true,
                    .Audio = true,
                    .Input = true,
                    .EditorUI = true,
                    .Physics = true,
                    .Scripting = true,
                    .Networking = true,
                    .ServerAuthority = false,
                    .ClientConnection = false
                };
            case RuntimeProfile::GameClient:
                return RuntimeCapabilities{
                    .Rendering = true,
                    .Windowing = true,
                    .Audio = true,
                    .Input = true,
                    .EditorUI = false,
                    .Physics = true,
                    .Scripting = true,
                    .Networking = true,
                    .ServerAuthority = false,
                    .ClientConnection = true
                };
            case RuntimeProfile::ListenServer:
                return RuntimeCapabilities{
                    .Rendering = true,
                    .Windowing = true,
                    .Audio = true,
                    .Input = true,
                    .EditorUI = false,
                    .Physics = true,
                    .Scripting = true,
                    .Networking = true,
                    .ServerAuthority = true,
                    .ClientConnection = true
                };
            case RuntimeProfile::DedicatedServer:
                return RuntimeCapabilities{
                    .Rendering = false,
                    .Windowing = false,
                    .Audio = false,
                    .Input = false,
                    .EditorUI = false,
                    .Physics = true,
                    .Scripting = true,
                    .Networking = true,
                    .ServerAuthority = true,
                    .ClientConnection = false
                };
            default:
                return RuntimeCapabilities{};
            }
        }
    };

    [[nodiscard]] constexpr Network::NetworkMode ToNetworkMode(RuntimeProfile profile) noexcept
    {
        switch (profile)
        {
        case RuntimeProfile::Editor:          return Network::NetworkMode::EditorPIE;
        case RuntimeProfile::GameClient:      return Network::NetworkMode::Client;
        case RuntimeProfile::ListenServer:    return Network::NetworkMode::ListenServer;
        case RuntimeProfile::DedicatedServer: return Network::NetworkMode::DedicatedServer;
        default:                              return Network::NetworkMode::Standalone;
        }
    }

    struct ProjectDescriptor
    {
        std::string ProjectName = "MorbiSandbox";
        std::string ProjectVersion = "1.0.0";
        std::string EngineVersion = "1.0.0";
        std::filesystem::path ProjectFilePath;
        std::filesystem::path ProjectRootDir;
        std::filesystem::path AssetsDir;
        std::filesystem::path ScriptsDir;
        std::string StartupScene = "assets://DefaultScene.mscene.json";
        std::string ManagedAssembly = "Morbi.Engine.dll";
        std::string EntryScriptClass = "Morbi.Engine.PlayerController";
        std::string DefaultRHI = "vulkan";
        std::string WindowTitle = "MorbiEngine";
        u32 WindowWidth = 1280;
        u32 WindowHeight = 720;
        bool VSync = true;
    };

    struct ServerTickStats
    {
        u32 TargetTickRate = 60;
        u64 TickCount = 0;
        f64 SimulationTimeSeconds = 0.0;
        f64 LastTickDurationMs = 0.0;
        f64 AverageTickDurationMs = 0.0;
        u64 OverrunCount = 0;
    };

    struct RuntimeVerificationReport
    {
        RuntimeProfile Profile = RuntimeProfile::DedicatedServer;
        bool WindowingInitialized = false;
        bool RenderingInitialized = false;
        bool RHIInitialized = false;
        bool EditorUIInitialized = false;
        bool AudioInitialized = false;
        bool InputInitialized = false;
        bool ECSInitialized = false;
        bool SceneInitialized = false;
        bool PhysicsInitialized = false;
        bool ScriptingInitialized = false;
        bool NetworkingInitialized = false;
    };

    struct ReplicationLaunchConfig
    {
        bool Enabled = true;
        bool AutoSpawnPlayerEntities = true;
        bool UsePhysicsCharacterForPlayers = false;
        u32 ReplicationRateHz = 20;
        Network::InterestMode Interest = Network::InterestMode::AllVisible;
        f32 InterestEnterRadius = 50.0f;
        f32 InterestLeaveRadius = 60.0f;
        f32 PlayerMoveSpeed = 6.0f;
        f32 PlayerJumpSpeed = 6.5f;
        Network::WorldInstanceId WorldId = 1;
    };

    struct SimulationLaunchConfig
    {
        RuntimeProfile Profile = RuntimeProfile::DedicatedServer;
        RuntimeCapabilities Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        std::filesystem::path ProjectFilePath;
        std::filesystem::path ScenePath;
        std::filesystem::path ScriptsDir;
        std::filesystem::path EngineBinDir;
        std::string BindAddress = "127.0.0.1";
        u16 Port = 27015;
        u32 MaxClients = 16;
        u32 TickRate = 60;
        u32 SchemaHash = 1;
        u64 MaxTicks = 0;
        bool ShutdownAfterDisconnect = false;
        Network::Endpoint ConnectEndpoint{};
        Network::SecurityConfig NetworkSecurity{};
        ReplicationLaunchConfig Replication{};
    };
}
