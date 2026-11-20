#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Morbi/Core/Result.hpp"
#include "Morbi/Network/Connection/ConnectionManager.hpp"
#include "Morbi/Runtime/RuntimeProfile.hpp"
#include "Morbi/Runtime/SceneReplicationBridge.hpp"

namespace Morbi::Script
{
    class ScriptEngine;
}

namespace Morbi::Runtime
{
    enum class ServerAppMessageSubType : u8
    {
        PingEcho = 1,
        StatusQuery = 2,
        StatusResponse = 3
    };

    struct WorldInstanceDesc
    {
        Network::WorldInstanceId WorldId = 1;
        std::string Name = "World";
        std::filesystem::path ScenePath;
        std::optional<ReplicationLaunchConfig> ReplicationOverride;
    };

    [[nodiscard]] Core::Result<ProjectDescriptor, std::string> LoadProjectDescriptorFromFile(
        const std::filesystem::path& morbiprojPath);

    /// Shared headless gameplay & networking runtime host.
    /// Owns ECS, Scene, Jolt Physics, CoreCLR Scripting, and Morbi.Network ConnectionManager
    /// without initializing any graphical, windowing, or editor presentation systems.
    class SimulationSession final
    {
    public:
        SimulationSession();
        ~SimulationSession();

        SimulationSession(const SimulationSession&) = delete;
        SimulationSession& operator=(const SimulationSession&) = delete;

        [[nodiscard]] Core::Result<bool, std::string> Initialize(const SimulationLaunchConfig& config);
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

        /// Advances accumulated delta time via bounded fixed-timestep simulation ticks.
        /// Returns the number of fixed ticks executed.
        u32 StepSimulation(f32 deltaTime);

        /// Executes one authoritative fixed-timestep tick.
        void StepFixedTick();

        /// Runs the dedicated server fixed-timestep loop until `stopRequested` is set,
        /// `MaxTicks` is reached, or `ShutdownAfterDisconnect` triggers cleanly.
        int RunServerLoop(const std::atomic<bool>& stopRequested);

        [[nodiscard]] const RuntimeCapabilities& GetCapabilities() const noexcept { return m_config.Capabilities; }
        [[nodiscard]] const ServerTickStats& GetTickStats() const noexcept { return m_tickStats; }
        [[nodiscard]] const RuntimeVerificationReport& GetVerificationReport() const noexcept { return m_report; }
        [[nodiscard]] Network::ConnectionManager& GetNetworkManager() noexcept;
        [[nodiscard]] const Network::ConnectionManager& GetNetworkManager() const noexcept;
        [[nodiscard]] SceneReplicationBridge& GetReplicationBridge() noexcept;
        [[nodiscard]] const SceneReplicationBridge& GetReplicationBridge() const noexcept;
        [[nodiscard]] Scene::Scene& GetScene() noexcept;
        [[nodiscard]] const Scene::Scene& GetScene() const noexcept;
        [[nodiscard]] Physics::PhysicsWorld& GetPhysicsWorld() noexcept;
        [[nodiscard]] const Physics::PhysicsWorld& GetPhysicsWorld() const noexcept;
        [[nodiscard]] Script::ScriptEngine& GetScriptEngine() noexcept;

        // Multi-World Instance Hosting (Phase 7)
        [[nodiscard]] Core::Result<bool, std::string> CreateWorldInstance(const WorldInstanceDesc& desc);
        bool DestroyWorldInstance(Network::WorldInstanceId worldId);
        bool TransferSessionToWorld(Network::SessionId sessionId, Network::WorldInstanceId targetWorldId);
        [[nodiscard]] bool HasWorldInstance(Network::WorldInstanceId worldId) const noexcept;
        [[nodiscard]] std::vector<Network::WorldInstanceId> GetWorldIds() const;
        [[nodiscard]] Network::WorldInstanceId GetDefaultWorldId() const noexcept;
        [[nodiscard]] Network::WorldInstanceId GetSessionWorldId(Network::SessionId sessionId) const noexcept;
        [[nodiscard]] Scene::Scene* TryGetWorldScene(Network::WorldInstanceId worldId) noexcept;
        [[nodiscard]] Physics::PhysicsWorld* TryGetWorldPhysics(Network::WorldInstanceId worldId) noexcept;
        [[nodiscard]] SceneReplicationBridge* TryGetWorldReplicationBridge(Network::WorldInstanceId worldId) noexcept;
        [[nodiscard]] usize GetActiveConnectionCount() const noexcept;
        [[nodiscard]] u32 GetAliveEntityCount() const noexcept;
        [[nodiscard]] bool HasHadClientConnection() const noexcept { return m_hadClientConnection; }

        using AppMessageCallback = std::function<void(const Network::NetworkMessageEvent&)>;
        void SetOnApplicationMessage(AppMessageCallback callback);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        SimulationLaunchConfig m_config{};
        ServerTickStats m_tickStats{};
        RuntimeVerificationReport m_report{};
        f64 m_accumulatorSeconds = 0.0;
        f64 m_totalTickDurationMs = 0.0;
        bool m_initialized = false;
        bool m_hadClientConnection = false;
    };
}
