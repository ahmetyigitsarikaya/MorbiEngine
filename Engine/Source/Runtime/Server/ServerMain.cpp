#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

#include "Morbi/Core/Diagnostics/CrashHandler.h"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Network/Security/CryptoContext.hpp"
#include "Morbi/Runtime/SimulationSession.hpp"

namespace
{
    std::atomic<bool> g_stopRequested{ false };

    void SignalHandler(int)
    {
        g_stopRequested.store(true, std::memory_order_release);
    }

#if defined(_WIN32)
    BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
    {
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT)
        {
            g_stopRequested.store(true, std::memory_order_release);
            return TRUE;
        }
        return FALSE;
    }
#endif

    std::filesystem::path GetExecutableDir(const char* argv0)
    {
#if defined(_WIN32)
        wchar_t buf[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            return std::filesystem::path(buf).parent_path();
        }
#endif
        if (argv0 != nullptr)
        {
            std::error_code ec;
            const auto p = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
            if (!ec && p.has_parent_path())
            {
                return p.parent_path();
            }
        }
        return std::filesystem::current_path();
    }

    bool ParseU32Arg(std::string_view text, Morbi::u32& outVal)
    {
        if (text.empty())
        {
            return false;
        }
        for (char c : text)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }
        try
        {
            const unsigned long parsed = std::stoul(std::string(text));
            outVal = static_cast<Morbi::u32>(parsed);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void PrintUsage()
    {
        std::cout
            << "MorbiServer - Headless Dedicated Server Runtime\n"
            << "Usage: MorbiServer [options]\n"
            << "  --project <path>             Optional .morbiproj descriptor\n"
            << "  --scene <path>               Optional initial .mscene.json\n"
            << "  --scripts <dir>              Optional C# Scripts directory\n"
            << "  --bind <ip>                  Bind IP address (default: 127.0.0.1)\n"
            << "  --port <port>                UDP listen port (default: 27015)\n"
            << "  --max-clients <count>        Max concurrent clients (default: 16)\n"
            << "  --tick-rate <hz>             Authoritative simulation tick rate (default: 60)\n"
            << "  --schema-hash <u32>          Protocol schema hash (default: 1)\n"
            << "  --max-ticks <count>          Optional finite tick limit (0 = unlimited)\n"
            << "  --require-token              Require signed Ed25519 connect tokens\n"
            << "  --issuer-pubkey <hex>        64-char hex Ed25519 token issuer public key\n"
            << "  --reconnect-window-ms <ms>   Session resumption window (default: 15000)\n"
            << "  --rep-rate <hz>              Snapshot replication cadence (default: 20)\n"
            << "  --interest-mode <mode>       all | distance (default: all)\n"
            << "  --interest-enter <m>         Distance interest enter radius (default: 50)\n"
            << "  --interest-leave <m>         Distance interest leave radius (default: 60)\n"
            << "  --world-id <u32>             Authoritative WorldInstanceId (default: 1)\n"
            << "  --no-auto-spawn              Disable auto-spawning player entities on connect\n"
            << "  --shutdown-after-disconnect  Exit cleanly after all clients disconnect\n"
            << "  --log-level <level>          trace | info | warn | error (default: info)\n"
            << "  --help                       Show this help message\n";
    }
}

int main(int argc, char** argv)
{
    using namespace Morbi;

    Core::Diagnostics::CrashHandler::Install();
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#if defined(_WIN32)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    Runtime::SimulationLaunchConfig cfg{};
    cfg.Profile = Runtime::RuntimeProfile::DedicatedServer;
    cfg.Capabilities = Runtime::RuntimeCapabilities::FromProfile(Runtime::RuntimeProfile::DedicatedServer);
    cfg.EngineBinDir = GetExecutableDir(argc > 0 ? argv[0] : nullptr);
    cfg.BindAddress = "127.0.0.1";
    cfg.Port = 27015;
    cfg.MaxClients = 16;
    cfg.TickRate = 60;
    cfg.SchemaHash = 1;

    Core::Log::SetMinLevel(Core::LogLevel::Info);
    Core::Log::SetImmediateFlush(true);

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg(argv[i]);
        auto nextVal = [&](std::string_view& out) -> bool {
            if (i + 1 >= argc || argv[i + 1] == nullptr)
            {
                std::cerr << "[MorbiServer] Missing value for argument: " << arg << "\n";
                return false;
            }
            out = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            return 0;
        }
        if (arg == "--shutdown-after-disconnect")
        {
            cfg.ShutdownAfterDisconnect = true;
            continue;
        }
        if (arg == "--require-token")
        {
            cfg.NetworkSecurity.RequireSignedConnectToken = true;
            continue;
        }
        if (arg == "--no-auto-spawn")
        {
            cfg.Replication.AutoSpawnPlayerEntities = false;
            continue;
        }

        std::string_view val;
        if (arg == "--project")
        {
            if (!nextVal(val)) return 1;
            cfg.ProjectFilePath = std::filesystem::path(val);
        }
        else if (arg == "--scene")
        {
            if (!nextVal(val)) return 1;
            cfg.ScenePath = std::filesystem::path(val);
        }
        else if (arg == "--scripts")
        {
            if (!nextVal(val)) return 1;
            cfg.ScriptsDir = std::filesystem::path(val);
        }
        else if (arg == "--bind")
        {
            if (!nextVal(val)) return 1;
            if (val.empty())
            {
                std::cerr << "[MorbiServer] Invalid empty --bind address.\n";
                return 1;
            }
            cfg.BindAddress = std::string(val);
        }
        else if (arg == "--port")
        {
            if (!nextVal(val)) return 1;
            u32 portVal = 0;
            if (!ParseU32Arg(val, portVal) || portVal == 0 || portVal > 65535)
            {
                std::cerr << "[MorbiServer] Invalid --port value: " << val << "\n";
                return 1;
            }
            cfg.Port = static_cast<u16>(portVal);
        }
        else if (arg == "--max-clients")
        {
            if (!nextVal(val)) return 1;
            u32 maxClients = 0;
            if (!ParseU32Arg(val, maxClients) || maxClients == 0 || maxClients > 4096)
            {
                std::cerr << "[MorbiServer] Invalid --max-clients value: " << val << "\n";
                return 1;
            }
            cfg.MaxClients = maxClients;
        }
        else if (arg == "--tick-rate")
        {
            if (!nextVal(val)) return 1;
            u32 tickRate = 0;
            if (!ParseU32Arg(val, tickRate) || tickRate == 0 || tickRate > 1000)
            {
                std::cerr << "[MorbiServer] Invalid --tick-rate value: " << val << "\n";
                return 1;
            }
            cfg.TickRate = tickRate;
        }
        else if (arg == "--schema-hash")
        {
            if (!nextVal(val)) return 1;
            u32 schemaHash = 0;
            if (!ParseU32Arg(val, schemaHash))
            {
                std::cerr << "[MorbiServer] Invalid --schema-hash value: " << val << "\n";
                return 1;
            }
            cfg.SchemaHash = schemaHash;
        }
        else if (arg == "--max-ticks")
        {
            if (!nextVal(val)) return 1;
            u32 maxTicks = 0;
            if (!ParseU32Arg(val, maxTicks))
            {
                std::cerr << "[MorbiServer] Invalid --max-ticks value: " << val << "\n";
                return 1;
            }
            cfg.MaxTicks = maxTicks;
        }
        else if (arg == "--issuer-pubkey")
        {
            if (!nextVal(val)) return 1;
            if (!Network::CryptoContext::HexDecode(val, cfg.NetworkSecurity.TokenIssuerPublicKey))
            {
                std::cerr << "[MorbiServer] Invalid --issuer-pubkey hex (expected 64 hex chars).\n";
                return 1;
            }
            cfg.NetworkSecurity.HasTokenIssuerPublicKey = true;
            cfg.NetworkSecurity.RequireSignedConnectToken = true;
        }
        else if (arg == "--reconnect-window-ms")
        {
            if (!nextVal(val)) return 1;
            u32 windowMs = 0;
            if (!ParseU32Arg(val, windowMs))
            {
                std::cerr << "[MorbiServer] Invalid --reconnect-window-ms value: " << val << "\n";
                return 1;
            }
            cfg.NetworkSecurity.ReconnectWindowMs = windowMs;
        }
        else if (arg == "--rep-rate")
        {
            if (!nextVal(val)) return 1;
            u32 repHz = 0;
            if (!ParseU32Arg(val, repHz) || repHz == 0 || repHz > 240)
            {
                std::cerr << "[MorbiServer] Invalid --rep-rate value: " << val << "\n";
                return 1;
            }
            cfg.Replication.ReplicationRateHz = repHz;
        }
        else if (arg == "--interest-mode")
        {
            if (!nextVal(val)) return 1;
            if (val == "all") cfg.Replication.Interest = Network::InterestMode::AllVisible;
            else if (val == "distance") cfg.Replication.Interest = Network::InterestMode::Distance;
            else
            {
                std::cerr << "[MorbiServer] Invalid --interest-mode (expected 'all' or 'distance').\n";
                return 1;
            }
        }
        else if (arg == "--interest-enter")
        {
            if (!nextVal(val)) return 1;
            try { cfg.Replication.InterestEnterRadius = std::stof(std::string(val)); }
            catch (...) { std::cerr << "[MorbiServer] Invalid --interest-enter.\n"; return 1; }
        }
        else if (arg == "--interest-leave")
        {
            if (!nextVal(val)) return 1;
            try { cfg.Replication.InterestLeaveRadius = std::stof(std::string(val)); }
            catch (...) { std::cerr << "[MorbiServer] Invalid --interest-leave.\n"; return 1; }
        }
        else if (arg == "--world-id")
        {
            if (!nextVal(val)) return 1;
            u32 worldId = 0;
            if (!ParseU32Arg(val, worldId) || worldId == 0)
            {
                std::cerr << "[MorbiServer] Invalid --world-id (must be >= 1).\n";
                return 1;
            }
            cfg.Replication.WorldId = worldId;
        }
        else if (arg == "--log-level")
        {
            if (!nextVal(val)) return 1;
            if (val == "trace") Core::Log::SetMinLevel(Core::LogLevel::Trace);
            else if (val == "info") Core::Log::SetMinLevel(Core::LogLevel::Info);
            else if (val == "warn") Core::Log::SetMinLevel(Core::LogLevel::Warn);
            else if (val == "error") Core::Log::SetMinLevel(Core::LogLevel::Error);
            else
            {
                std::cerr << "[MorbiServer] Invalid --log-level value: " << val << "\n";
                return 1;
            }
        }
        else
        {
            std::cerr << "[MorbiServer] Unknown argument: " << arg << "\n";
            PrintUsage();
            return 1;
        }
    }

    Runtime::SimulationSession session;
    const auto initRes = session.Initialize(cfg);
    if (!initRes)
    {
        Core::Log::Error("Server", "Initialization failed: " + initRes.Error());
        return 1;
    }

    const int loopExit = session.RunServerLoop(g_stopRequested);
    const auto stats = session.GetTickStats();
    session.Shutdown();

    Core::Log::Info("Server", "Stopped cleanly | Ticks=" + std::to_string(stats.TickCount) +
        " | SimTime=" + std::to_string(stats.SimulationTimeSeconds) + "s" +
        " | AvgTickMs=" + std::to_string(stats.AverageTickDurationMs) +
        " | Overruns=" + std::to_string(stats.OverrunCount));
    return loopExit;
}
