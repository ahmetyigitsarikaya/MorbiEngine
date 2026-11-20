#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#else
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "Morbi/Core/Log.hpp"
#include "Morbi/Network/Network.hpp"
#include "Morbi/Physics/PhysicsWorld.hpp"
#include "Morbi/Runtime/SceneReplicationBridge.hpp"
#include "Morbi/Runtime/SimulationSession.hpp"
#include "Morbi/Script/ScriptEngine.hpp"

using namespace Morbi;
using namespace Morbi::Network;
using namespace Morbi::Runtime;

namespace
{
    int g_failures = 0;

    void Check(bool cond, const char* expr, const std::string& msg)
    {
        if (!cond)
        {
            std::cerr << "[FAIL] " << expr << " | " << msg << "\n";
            ++g_failures;
        }
    }

    #define EXPECT_TRUE(cond, msg) Check((cond), #cond, (msg))

    std::filesystem::path GetCurrentExePath()
    {
#if defined(_WIN32)
        wchar_t buf[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            return std::filesystem::path(buf);
        }
#endif
        return std::filesystem::current_path();
    }

    std::filesystem::path FindRepoSampleScene()
    {
        std::error_code ec;
        const auto exeDir = GetCurrentExePath().parent_path();
        for (std::filesystem::path probe = exeDir; !probe.empty() && probe != probe.root_path(); probe = probe.parent_path())
        {
            const auto candidate = probe / "Samples" / "Sandbox" / "Assets" / "Scenes" / "ThirdPersonSandbox.mscene.json";
            if (std::filesystem::exists(candidate, ec) && !ec)
            {
                return candidate;
            }
        }
        return {};
    }

    void PumpPair(ConnectionManager& a, ConnectionManager& b, u32 iterations = 100, u32 sleepMs = 2)
    {
        for (u32 i = 0; i < iterations; ++i)
        {
            a.Poll();
            b.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }

    template <typename Pred>
    bool PumpUntil(ConnectionManager& a, ConnectionManager& b, Pred&& pred, u32 timeoutMs = 3000)
    {
        const auto start = std::chrono::steady_clock::now();
        while (true)
        {
            a.Poll();
            b.Poll();
            if (pred())
            {
                return true;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMs)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // ------------------------------------------------------------------------
    // 1. Serialization & Protocol Tests
    // ------------------------------------------------------------------------

    void Test_BitStreamRoundTripAndQuantization()
    {
        BitWriter writer(1024);
        EXPECT_TRUE(writer.WriteBool(true), "WriteBool true");
        EXPECT_TRUE(writer.WriteBool(false), "WriteBool false");
        EXPECT_TRUE(writer.WriteBits(0x2Au, 6), "WriteBits 6-bit value");
        EXPECT_TRUE(writer.WriteU8(0xABu), "WriteU8");
        EXPECT_TRUE(writer.WriteU16(0xCAFEu), "WriteU16");
        EXPECT_TRUE(writer.WriteU32(0xDEADBEEFu), "WriteU32");
        EXPECT_TRUE(writer.WriteU64(0x0123456789ABCDEFull), "WriteU64");
        EXPECT_TRUE(writer.WriteI32(-1234567), "WriteI32");
        EXPECT_TRUE(writer.WriteF32(3.1415926f), "WriteF32");
        EXPECT_TRUE(writer.WriteF64(-98765.4321), "WriteF64");
        EXPECT_TRUE(writer.WriteQuantizedFloat(12.34f, -100.0f, 100.0f, 16), "WriteQuantizedFloat");
        EXPECT_TRUE(writer.WriteString("MorbiEngine_NetCore"), "WriteString");
        EXPECT_TRUE(!writer.HasError(), "Writer should have no errors");

        BitReader reader(writer.GetData());
        bool b1 = false, b2 = true;
        u64 bits6 = 0;
        u8 v8 = 0;
        u16 v16 = 0;
        u32 v32 = 0;
        u64 v64 = 0;
        i32 vi32 = 0;
        f32 vf32 = 0.0f;
        f64 vf64 = 0.0;
        f32 qf = 0.0f;
        std::string text;

        EXPECT_TRUE(reader.ReadBool(b1) && b1, "ReadBool true");
        EXPECT_TRUE(reader.ReadBool(b2) && !b2, "ReadBool false");
        EXPECT_TRUE(reader.ReadBits(6, bits6) && bits6 == 0x2Au, "ReadBits 6-bit value");
        EXPECT_TRUE(reader.ReadU8(v8) && v8 == 0xABu, "ReadU8");
        EXPECT_TRUE(reader.ReadU16(v16) && v16 == 0xCAFEu, "ReadU16");
        EXPECT_TRUE(reader.ReadU32(v32) && v32 == 0xDEADBEEFu, "ReadU32");
        EXPECT_TRUE(reader.ReadU64(v64) && v64 == 0x0123456789ABCDEFull, "ReadU64");
        EXPECT_TRUE(reader.ReadI32(vi32) && vi32 == -1234567, "ReadI32");
        EXPECT_TRUE(reader.ReadF32(vf32) && std::abs(vf32 - 3.1415926f) < 1e-6f, "ReadF32");
        EXPECT_TRUE(reader.ReadF64(vf64) && std::abs(vf64 - (-98765.4321)) < 1e-9, "ReadF64");
        EXPECT_TRUE(reader.ReadQuantizedFloat(-100.0f, 100.0f, 16, qf) && std::abs(qf - 12.34f) < 0.01f, "ReadQuantizedFloat precision");
        EXPECT_TRUE(reader.ReadString(text) && text == "MorbiEngine_NetCore", "ReadString");
        EXPECT_TRUE(!reader.HasError(), "Reader should have no errors");
        std::cout << "[PASS] Test_BitStreamRoundTripAndQuantization\n";
    }

    void Test_BitStreamBoundsAndAdversarialInputs()
    {
        BitWriter bounded(16);
        std::vector<u8> exact16(16, 0x5Au);
        EXPECT_TRUE(bounded.WriteBytes(exact16), "Exact maxBytes write succeeds");
        EXPECT_TRUE(!bounded.WriteU8(1), "Write past maxBytes must fail");
        EXPECT_TRUE(bounded.HasError(), "HasError set on overflow write");

        const u8 smallBuf[2] = { 0x11, 0x22 };
        BitReader truncReader(smallBuf);
        u32 out32 = 0;
        EXPECT_TRUE(!truncReader.ReadU32(out32), "Truncated U32 read fails");
        EXPECT_TRUE(truncReader.HasError(), "HasError set on truncated read");

        BitWriter maliciousWriter(64);
        maliciousWriter.WriteU16(3000);
        maliciousWriter.WriteU16(0x4141);
        BitReader maliciousReader(maliciousWriter.GetData());
        std::string maliciousStr;
        EXPECT_TRUE(!maliciousReader.ReadString(maliciousStr), "Invalid string length exceeding buffer must be rejected");
        EXPECT_TRUE(maliciousReader.HasError(), "Reader flags invalid string length");
        EXPECT_TRUE(maliciousStr.empty(), "Rejected string read leaves output empty");

        BitWriter strWriter(128);
        strWriter.WriteString("0123456789");
        BitReader strReader(strWriter.GetData());
        std::string shortStr;
        EXPECT_TRUE(!strReader.ReadString(shortStr, 5), "String exceeding maxLength must be rejected");

        BitWriter nanWriter(64);
        EXPECT_TRUE(!nanWriter.WriteQuantizedFloat(std::numeric_limits<f32>::quiet_NaN(), 0.0f, 1.0f, 8), "NaN quantized write rejected");

        std::cout << "[PASS] Test_BitStreamBoundsAndAdversarialInputs\n";
    }

    void Test_ProtocolHeaderValidation()
    {
        BitWriter w(256);
        MessageHeader hdr{};
        hdr.Magic = ProtocolConstants::kMagic;
        hdr.ProtocolVersion = ProtocolConstants::kCurrentVersion;
        hdr.SchemaHash = 0x12345678u;
        hdr.Category = MessageCategory::Application;
        hdr.SubType = 7;
        hdr.Sequence = 42;
        hdr.PayloadBytes = 4;

        EXPECT_TRUE(ProtocolCodec::WriteHeader(w, hdr), "WriteHeader succeeds");
        EXPECT_TRUE(w.WriteU32(0xAABBCCDDu), "Write 4-byte payload");

        BitReader r(w.GetData());
        MessageHeader parsed{};
        EXPECT_TRUE(ProtocolCodec::ReadHeader(r, parsed), "ReadHeader succeeds");
        EXPECT_TRUE(ProtocolCodec::ValidateHeader(parsed, ProtocolConstants::kCurrentVersion, 0x12345678u), "ValidateHeader matches");
        EXPECT_TRUE(!ProtocolCodec::ValidateHeader(parsed, ProtocolConstants::kCurrentVersion + 1, 0x12345678u), "Version mismatch rejected");

        std::vector<u8> badBytes(w.GetData().begin(), w.GetData().end());
        badBytes[0] ^= 0xFFu;
        BitReader badReader(badBytes);
        MessageHeader badParsed{};
        EXPECT_TRUE(!ProtocolCodec::ReadHeader(badReader, badParsed), "Corrupted magic rejected");

        std::vector<u8> shortPayload(w.GetData().begin(), w.GetData().end() - 1);
        BitReader shortReader(shortPayload);
        MessageHeader shortParsed{};
        EXPECT_TRUE(!ProtocolCodec::ReadHeader(shortReader, shortParsed), "Header claiming more bytes than remain rejected");

        std::cout << "[PASS] Test_ProtocolHeaderValidation\n";
    }

    // ------------------------------------------------------------------------
    // 2. Deterministic Network Simulator Tests
    // ------------------------------------------------------------------------

    void Test_NetworkSimulatorFaultInjection()
    {
        NetworkSimulator sim;
        const u8 data[4] = { 1, 2, 3, 4 };
        std::vector<SimulatedPacket> ready;

        NetSimConfig lossCfg{};
        lossCfg.Enabled = true;
        lossCfg.Seed = 42;
        lossCfg.PacketLossPercent = 100.0f;
        sim.Configure(lossCfg);
        for (int i = 0; i < 10; ++i)
        {
            sim.Submit(ConnectionHandle{ 1 }, NetChannel::Snapshot, NetDelivery::Unreliable, data, 100);
        }
        sim.DrainReady(10000, ready);
        EXPECT_TRUE(ready.empty(), "100% packet loss drops all packets");

        NetSimConfig latCfg{};
        latCfg.Enabled = true;
        latCfg.Seed = 42;
        latCfg.BaseLatencyMs = 50.0f;
        latCfg.DuplicationPercent = 100.0f;
        sim.Configure(latCfg);
        sim.Submit(ConnectionHandle{ 1 }, NetChannel::GameplayEvents, NetDelivery::ReliableOrdered, data, 1000);
        sim.DrainReady(1040, ready);
        EXPECT_TRUE(ready.empty(), "Packet not released before latency expires");
        sim.DrainReady(1060, ready);
        EXPECT_TRUE(ready.size() == 2u, "100% duplication produces 2 packets after latency");

        NetSimConfig reorderCfg{};
        reorderCfg.Enabled = true;
        reorderCfg.Seed = 99;
        reorderCfg.BaseLatencyMs = 10.0f;
        reorderCfg.ReorderPercent = 50.0f;
        sim.Configure(reorderCfg);
        for (u8 i = 0; i < 10; ++i)
        {
            const u8 seqByte[1] = { i };
            sim.Submit(ConnectionHandle{ 1 }, NetChannel::Snapshot, NetDelivery::Unreliable, seqByte, 2000 + i);
        }
        std::vector<u8> observedOrder;
        for (u64 t = 2000; t <= 2200; t += 2)
        {
            sim.DrainReady(t, ready);
            for (const auto& p : ready)
            {
                observedOrder.push_back(p.Payload[0]);
            }
        }
        EXPECT_TRUE(observedOrder.size() == 10u, "All 10 reordered packets eventually drained");
        bool outOfOrderObserved = false;
        for (usize i = 1; i < observedOrder.size(); ++i)
        {
            if (observedOrder[i] < observedOrder[i - 1])
            {
                outOfOrderObserved = true;
                break;
            }
        }
        EXPECT_TRUE(outOfOrderObserved, "Reordering caused at least one packet to arrive out of order");

        std::cout << "[PASS] Test_NetworkSimulatorFaultInjection\n";
    }

    // ------------------------------------------------------------------------
    // 3. ENet ConnectionManager Loopback Tests
    // ------------------------------------------------------------------------

    void Test_ConnectionLifecycleAndMessageExchange()
    {
        const u16 port = 19701;

        ConnectionManager server;
        ConnectionManager client;

        ConnectionConfig serverCfg{};
        serverCfg.Mode = NetworkMode::DedicatedServer;
        serverCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        serverCfg.Transport.MaxConnections = 16;
        serverCfg.SchemaHash = 0x55AA1234u;

        ConnectionConfig clientCfg{};
        clientCfg.Mode = NetworkMode::Client;
        clientCfg.Transport.MaxConnections = 1;
        clientCfg.SchemaHash = 0x55AA1234u;

        EXPECT_TRUE(server.Initialize(serverCfg).IsSuccess(), "Server initialize succeeds");
        EXPECT_TRUE(client.Initialize(clientCfg).IsSuccess(), "Client initialize succeeds");

        bool serverConnected = false;
        bool clientConnected = false;
        bool clientDisconnected = false;
        ConnectionHandle serverClientHandle = kInvalidConnectionHandle;
        std::string serverReceivedText;
        std::string clientReceivedText;

        server.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            serverConnected = true;
            serverClientHandle = info.Handle;
        });
        client.SetOnConnected([&](const ConnectionInfo&, const std::string&) {
            clientConnected = true;
        });
        client.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) {
            clientDisconnected = true;
        });

        server.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            BitReader r(ev.Payload);
            (void)r.ReadString(serverReceivedText);
            BitWriter reply(128);
            (void)reply.WriteString("ACK:" + serverReceivedText);
            (void)server.SendPacket(ev.Connection, MessageCategory::Application, 2, reply.GetData(), NetDelivery::ReliableOrdered, NetChannel::GameplayEvents);
        });

        client.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            if (ev.Header.Category != MessageCategory::Application)
            {
                return;
            }
            BitReader r(ev.Payload);
            (void)r.ReadString(clientReceivedText);
        });

        auto connRes = client.Connect(Endpoint{ "127.0.0.1", port });
        EXPECT_TRUE(connRes.IsSuccess(), "Client Connect initiated");
        const ConnectionHandle clientHandle = connRes.Value();

        EXPECT_TRUE(PumpUntil(server, client, [&]() { return serverConnected && clientConnected; }),
            "Handshake completed on both server and client");

        BitWriter msgWriter(128);
        msgWriter.WriteString("HelloMorbiNetwork");
        const auto sendRes = client.SendPacket(
            clientHandle,
            MessageCategory::Application,
            1,
            msgWriter.GetData(),
            NetDelivery::ReliableOrdered,
            NetChannel::GameplayEvents);
        EXPECT_TRUE(sendRes.Succeeded(), "Client SendPacket succeeded");

        EXPECT_TRUE(PumpUntil(server, client, [&]() {
            return serverReceivedText == "HelloMorbiNetwork" && clientReceivedText == "ACK:HelloMorbiNetwork";
        }), "Server and client exchanged real ENet UDP messages");

        ConnectionInfo statsInfo{};
        EXPECT_TRUE(server.TryGetConnectionInfo(serverClientHandle, statsInfo) && statsInfo.State == ConnectionState::Connected,
            "Server reports connected info");

        client.Disconnect(clientHandle, "TestComplete");
        EXPECT_TRUE(PumpUntil(server, client, [&]() { return clientDisconnected; }),
            "Client disconnect event observed");

        client.Shutdown();
        server.Shutdown();
        std::cout << "[PASS] Test_ConnectionLifecycleAndMessageExchange\n";
    }

    void Test_ProtocolMismatchAndMultiClient()
    {
        const u16 port = 19702;

        ConnectionManager server;
        ConnectionConfig serverCfg{};
        serverCfg.Mode = NetworkMode::DedicatedServer;
        serverCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        serverCfg.Transport.MaxConnections = 8;
        serverCfg.SchemaHash = 0x11111111u;
        EXPECT_TRUE(server.Initialize(serverCfg).IsSuccess(), "MultiClient server initialized");

        ConnectionManager badClient;
        ConnectionConfig badCfg{};
        badCfg.Mode = NetworkMode::Client;
        badCfg.SchemaHash = 0x99999999u;
        EXPECT_TRUE(badClient.Initialize(badCfg).IsSuccess(), "BadClient initialized");

        bool badConnected = false;
        bool badRejected = false;
        badClient.SetOnConnected([&](const ConnectionInfo&, const std::string&) { badConnected = true; });
        badClient.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { badRejected = true; });
        (void)badClient.Connect(Endpoint{ "127.0.0.1", port });

        EXPECT_TRUE(PumpUntil(server, badClient, [&]() { return badRejected; }),
            "Mismatched schema client rejected by server");
        EXPECT_TRUE(!badConnected, "Mismatched schema client never transitions to Connected");
        badClient.Shutdown();

        ConnectionManager c1, c2, c3;
        ConnectionConfig goodCfg{};
        goodCfg.Mode = NetworkMode::Client;
        goodCfg.SchemaHash = 0x11111111u;
        EXPECT_TRUE(c1.Initialize(goodCfg).IsSuccess(), "Client 1 init");
        EXPECT_TRUE(c2.Initialize(goodCfg).IsSuccess(), "Client 2 init");
        EXPECT_TRUE(c3.Initialize(goodCfg).IsSuccess(), "Client 3 init");

        int connectedCount = 0;
        auto onConn = [&](const ConnectionInfo&, const std::string&) { ++connectedCount; };
        c1.SetOnConnected(onConn);
        c2.SetOnConnected(onConn);
        c3.SetOnConnected(onConn);

        (void)c1.Connect(Endpoint{ "127.0.0.1", port });
        (void)c2.Connect(Endpoint{ "127.0.0.1", port });
        (void)c3.Connect(Endpoint{ "127.0.0.1", port });

        const auto start = std::chrono::steady_clock::now();
        while (connectedCount < 3)
        {
            server.Poll();
            c1.Poll();
            c2.Poll();
            c3.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > 3000)
            {
                break;
            }
        }
        EXPECT_TRUE(connectedCount == 3 && server.GetConnectionCount() == 3u, "All 3 concurrent clients connected");

        c1.Shutdown();
        c2.Shutdown();
        c3.Shutdown();
        PumpPair(server, c1, 20, 2);
        server.Shutdown();
        std::cout << "[PASS] Test_ProtocolMismatchAndMultiClient\n";
    }

    // ------------------------------------------------------------------------
    // 4. RuntimeProfile, Capabilities & Headless SimulationSession Tests
    // ------------------------------------------------------------------------

    void Test_RuntimeProfilesAndCapabilities()
    {
        const auto dedCaps = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        EXPECT_TRUE(!dedCaps.Rendering && !dedCaps.Windowing && !dedCaps.Audio && !dedCaps.Input && !dedCaps.EditorUI,
            "DedicatedServer disables all presentation capabilities");
        EXPECT_TRUE(dedCaps.Physics && dedCaps.Scripting && dedCaps.Networking && dedCaps.ServerAuthority && !dedCaps.ClientConnection,
            "DedicatedServer enables Physics, Scripting, Networking, ServerAuthority");
        EXPECT_TRUE(ToNetworkMode(RuntimeProfile::DedicatedServer) == NetworkMode::DedicatedServer, "DedicatedServer NetworkMode maps");

        const auto cliCaps = RuntimeCapabilities::FromProfile(RuntimeProfile::GameClient);
        EXPECT_TRUE(cliCaps.Rendering && cliCaps.Windowing && cliCaps.Audio && cliCaps.Input && !cliCaps.EditorUI,
            "GameClient enables presentation and disables EditorUI");
        EXPECT_TRUE(cliCaps.ClientConnection && !cliCaps.ServerAuthority, "GameClient capability roles");
        EXPECT_TRUE(ToNetworkMode(RuntimeProfile::GameClient) == NetworkMode::Client, "GameClient NetworkMode maps");

        const auto listenCaps = RuntimeCapabilities::FromProfile(RuntimeProfile::ListenServer);
        EXPECT_TRUE(listenCaps.Rendering && listenCaps.Windowing && listenCaps.ServerAuthority && listenCaps.ClientConnection,
            "ListenServer enables presentation + server authority");
        EXPECT_TRUE(ToNetworkMode(RuntimeProfile::ListenServer) == NetworkMode::ListenServer, "ListenServer NetworkMode maps");

        std::cout << "[PASS] Test_RuntimeProfilesAndCapabilities\n";
    }

    void Test_HeadlessSimulationSessionAndRecreation()
    {
        const auto exeDir = GetCurrentExePath().parent_path();
        const auto sampleScene = FindRepoSampleScene();

        for (int cycle = 0; cycle < 2; ++cycle)
        {
            SimulationLaunchConfig cfg{};
            cfg.Profile = RuntimeProfile::DedicatedServer;
            cfg.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
            cfg.EngineBinDir = exeDir;
            cfg.ScenePath = sampleScene;
            cfg.BindAddress = "127.0.0.1";
            cfg.Port = static_cast<u16>(27100 + cycle);
            cfg.MaxClients = 8;
            cfg.TickRate = 60;

            SimulationSession session;
            const auto initRes = session.Initialize(cfg);
            EXPECT_TRUE(initRes.IsSuccess(), "SimulationSession Initialize cycle " + std::to_string(cycle) + ": " +
                (initRes ? "OK" : initRes.Error()));

            const auto& rep = session.GetVerificationReport();
            EXPECT_TRUE(!rep.WindowingInitialized && !rep.RenderingInitialized && !rep.RHIInitialized && !rep.EditorUIInitialized && !rep.AudioInitialized,
                "Headless SimulationSession never initializes graphical/audio systems");
            EXPECT_TRUE(rep.ECSInitialized && rep.SceneInitialized && rep.PhysicsInitialized && rep.ScriptingInitialized && rep.NetworkingInitialized,
                "Headless SimulationSession initializes ECS, Scene, Physics, CoreCLR, and Networking");
            if (!sampleScene.empty())
            {
                EXPECT_TRUE(session.GetAliveEntityCount() > 0u, "Sample scene loaded into headless ECS");
            }

            // Fixed-timestep accumulation + overload clamp verification
            const u32 ticks1 = session.StepSimulation(0.05f); // 3 ticks at 60Hz (dt=16.666ms)
            EXPECT_TRUE(ticks1 >= 2u && ticks1 <= 3u, "StepSimulation executes fixed ticks for 50ms");
            const u32 ticksOverload = session.StepSimulation(5.0f); // Must clamp to max 4 substeps
            EXPECT_TRUE(ticksOverload == 4u, "Overload step clamped to max 4 substeps");

            const auto stats = session.GetTickStats();
            EXPECT_TRUE(stats.TickCount == static_cast<u64>(ticks1 + ticksOverload), "TickCount matches executed steps");
            EXPECT_TRUE(stats.SimulationTimeSeconds > 0.09, "SimulationTimeSeconds advances deterministically");

            session.Shutdown();
            EXPECT_TRUE(!session.IsInitialized(), "SimulationSession Shutdown resets initialized state");
        }

        // Verify partial initialization failure cleans up safely
        SimulationLaunchConfig badCfg{};
        badCfg.Profile = RuntimeProfile::DedicatedServer;
        badCfg.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        badCfg.EngineBinDir = exeDir;
        badCfg.ScenePath = exeDir / "NonExistentScene_XYZ.mscene.json";
        badCfg.Port = 27109;
        SimulationSession failSession;
        EXPECT_TRUE(!failSession.Initialize(badCfg).IsSuccess(), "Missing scene fails cleanly");
        EXPECT_TRUE(!failSession.IsInitialized(), "Failed init leaves session uninitialized");

        std::cout << "[PASS] Test_HeadlessSimulationSessionAndRecreation\n";
    }

    // ------------------------------------------------------------------------
    // 5. Real MorbiServer.exe Separate-Process Tests
    // ------------------------------------------------------------------------

    int RunServerClientProcess(u16 port, u32 clientId, u32 holdMs)
    {
        ConnectionManager client;
        ConnectionConfig cfg{};
        cfg.Mode = NetworkMode::Client;
        cfg.Transport.MaxConnections = 1;
        cfg.SchemaHash = 1;
        if (!client.Initialize(cfg))
        {
            std::cerr << "[ServerClientProc " << clientId << "] Initialize failed\n";
            return 30;
        }

        bool connected = false;
        bool gotEcho = false;
        bool gotStatus = false;
        bool disconnected = false;
        bool disconnectSent = false;
        ConnectionHandle handle = kInvalidConnectionHandle;
        std::chrono::steady_clock::time_point readySince{};

        const std::string expectedMsg = "HELLO_MORBISERVER_" + std::to_string(clientId);

        client.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            connected = true;
            handle = info.Handle;

            BitWriter pingW(128);
            (void)pingW.WriteString(expectedMsg);
            (void)client.SendPacket(
                handle,
                MessageCategory::Application,
                static_cast<u8>(ServerAppMessageSubType::PingEcho),
                pingW.GetData(),
                NetDelivery::ReliableOrdered,
                NetChannel::GameplayEvents);

            BitWriter statW(32);
            (void)statW.WriteU8(1);
            (void)client.SendPacket(
                handle,
                MessageCategory::Application,
                static_cast<u8>(ServerAppMessageSubType::StatusQuery),
                statW.GetData(),
                NetDelivery::ReliableOrdered,
                NetChannel::GameplayEvents);
        });

        client.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            if (ev.Header.Category != MessageCategory::Application)
            {
                return;
            }
            BitReader r(ev.Payload);
            if (ev.Header.SubType == static_cast<u8>(ServerAppMessageSubType::PingEcho))
            {
                std::string reply;
                if (r.ReadString(reply) && reply == ("PONG:" + expectedMsg))
                {
                    gotEcho = true;
                }
            }
            else if (ev.Header.SubType == static_cast<u8>(ServerAppMessageSubType::StatusResponse))
            {
                u64 tickCount = 0;
                u32 activeConns = 0;
                u32 aliveEntities = 0;
                f64 simTime = 0.0;
                if (r.ReadU64(tickCount) &&
                    r.ReadU32(activeConns) &&
                    r.ReadU32(aliveEntities) &&
                    r.ReadF64(simTime))
                {
                    if (activeConns >= 1 && simTime >= 0.0)
                    {
                        gotStatus = true;
                    }
                }
            }

            if (gotEcho && gotStatus && readySince == std::chrono::steady_clock::time_point{})
            {
                readySince = std::chrono::steady_clock::now();
            }
        });

        client.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) {
            disconnected = true;
        });

        if (!client.Connect(Endpoint{ "127.0.0.1", port }))
        {
            std::cerr << "[ServerClientProc " << clientId << "] Connect failed\n";
            return 31;
        }

        const auto start = std::chrono::steady_clock::now();
        while (!disconnected)
        {
            client.Poll();
            if (gotEcho && gotStatus && !disconnectSent)
            {
                const auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - readySince).count();
                if (heldMs >= static_cast<long long>(holdMs))
                {
                    disconnectSent = true;
                    client.Disconnect(handle, "ClientDone");
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > 12000)
            {
                std::cerr << "[ServerClientProc " << clientId << "] Timed out (connected=" << connected
                          << ", gotEcho=" << gotEcho << ", gotStatus=" << gotStatus << ")\n";
                client.Shutdown();
                return 32;
            }
        }

        client.Shutdown();
        return (gotEcho && gotStatus) ? 0 : 33;
    }

#if defined(_WIN32)
    struct ChildProcessHandle
    {
        HANDLE Process = nullptr;
        HANDLE ReadPipe = nullptr;
    };

    ChildProcessHandle LaunchProcessWithPipe(const std::filesystem::path& exePath, const std::wstring& args)
    {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        {
            return {};
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;

        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + exePath.wstring() + L"\" " + args;
        const BOOL ok = CreateProcessW(
            nullptr,
            cmd.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            exePath.parent_path().c_str(),
            &si,
            &pi);

        CloseHandle(writePipe);
        if (!ok)
        {
            CloseHandle(readPipe);
            return {};
        }
        CloseHandle(pi.hThread);
        return ChildProcessHandle{ pi.hProcess, readPipe };
    }

    bool WaitAndCollectProcess(ChildProcessHandle& proc, DWORD timeoutMs, DWORD& outExitCode, std::string& outOutput)
    {
        outOutput.clear();
        if (proc.Process == nullptr)
        {
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        char buf[1024];
        while (true)
        {
            DWORD avail = 0;
            while (proc.ReadPipe != nullptr && PeekNamedPipe(proc.ReadPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            {
                DWORD readLen = 0;
                if (ReadFile(proc.ReadPipe, buf, sizeof(buf), &readLen, nullptr) && readLen > 0)
                {
                    outOutput.append(buf, readLen);
                }
                else
                {
                    break;
                }
            }

            const DWORD waitRes = WaitForSingleObject(proc.Process, 15);
            if (waitRes == WAIT_OBJECT_0)
            {
                // Drain remaining pipe output
                while (proc.ReadPipe != nullptr)
                {
                    DWORD availAfter = 0;
                    if (!PeekNamedPipe(proc.ReadPipe, nullptr, 0, nullptr, &availAfter, nullptr) || availAfter == 0)
                    {
                        break;
                    }
                    DWORD readLen = 0;
                    if (!ReadFile(proc.ReadPipe, buf, sizeof(buf), &readLen, nullptr) || readLen == 0)
                    {
                        break;
                    }
                    outOutput.append(buf, readLen);
                }
                GetExitCodeProcess(proc.Process, &outExitCode);
                CloseHandle(proc.Process);
                if (proc.ReadPipe != nullptr) CloseHandle(proc.ReadPipe);
                proc.Process = nullptr;
                proc.ReadPipe = nullptr;
                return true;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= static_cast<long long>(timeoutMs))
            {
                TerminateProcess(proc.Process, 99);
                WaitForSingleObject(proc.Process, 2000);
                CloseHandle(proc.Process);
                if (proc.ReadPipe != nullptr) CloseHandle(proc.ReadPipe);
                proc.Process = nullptr;
                proc.ReadPipe = nullptr;
                outExitCode = 99;
                return false;
            }
        }
    }

    bool WaitUntilPipeContains(ChildProcessHandle& proc, std::string_view token, DWORD timeoutMs, std::string& accumulatedOutput)
    {
        const auto start = std::chrono::steady_clock::now();
        char buf[1024];
        while (true)
        {
            DWORD avail = 0;
            while (proc.ReadPipe != nullptr && PeekNamedPipe(proc.ReadPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            {
                DWORD readLen = 0;
                if (ReadFile(proc.ReadPipe, buf, sizeof(buf), &readLen, nullptr) && readLen > 0)
                {
                    accumulatedOutput.append(buf, readLen);
                }
                else
                {
                    break;
                }
            }
            if (accumulatedOutput.find(token) != std::string::npos)
            {
                return true;
            }
            if (WaitForSingleObject(proc.Process, 10) == WAIT_OBJECT_0)
            {
                return accumulatedOutput.find(token) != std::string::npos;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= static_cast<long long>(timeoutMs))
            {
                return false;
            }
        }
    }
#endif

    void Test_MorbiServerStartupAndHeadlessVerification()
    {
#if defined(_WIN32)
        const auto exePath = GetCurrentExePath();
        const auto serverExe = exePath.parent_path() / "MorbiServer.exe";
        EXPECT_TRUE(std::filesystem::exists(serverExe), "MorbiServer.exe exists at " + serverExe.string());

        auto proc = LaunchProcessWithPipe(
            serverExe,
            L"--bind 127.0.0.1 --port 27115 --max-ticks 12 --tick-rate 60");
        EXPECT_TRUE(proc.Process != nullptr, "Launched MorbiServer.exe");

        DWORD exitCode = 999;
        std::string output;
        const bool finished = WaitAndCollectProcess(proc, 12000, exitCode, output);
        EXPECT_TRUE(finished && exitCode == 0, "MorbiServer.exe exited cleanly with 0 (exit=" + std::to_string(exitCode) + ")");
        EXPECT_TRUE(output.find("Profile: DedicatedServer") != std::string::npos, "Logged DedicatedServer profile");
        EXPECT_TRUE(output.find("Windowing: Disabled") != std::string::npos, "Logged Windowing: Disabled");
        EXPECT_TRUE(output.find("Rendering: Disabled") != std::string::npos, "Logged Rendering: Disabled");
        EXPECT_TRUE(output.find("RHI: Disabled") != std::string::npos, "Logged RHI: Disabled");
        EXPECT_TRUE(output.find("Editor UI: Disabled") != std::string::npos, "Logged Editor UI: Disabled");
        EXPECT_TRUE(output.find("Audio: Disabled") != std::string::npos, "Logged Audio: Disabled");
        EXPECT_TRUE(output.find("ECS: Initialized") != std::string::npos, "Logged ECS: Initialized");
        EXPECT_TRUE(output.find("Scene: Initialized") != std::string::npos, "Logged Scene: Initialized");
        EXPECT_TRUE(output.find("Physics: Initialized") != std::string::npos, "Logged Physics: Initialized");
        EXPECT_TRUE(output.find("CoreCLR: Initialized") != std::string::npos, "Logged CoreCLR: Initialized");
        EXPECT_TRUE(output.find("Networking: Initialized") != std::string::npos, "Logged Networking: Initialized");
        EXPECT_TRUE(output.find("Listening on 127.0.0.1:27115") != std::string::npos, "Logged UDP listen endpoint");
        std::cout << "[PASS] Test_MorbiServerStartupAndHeadlessVerification\n";
#endif
    }

    void Test_MorbiServerRealClientAndFourClientIntegration()
    {
#if defined(_WIN32)
        const auto exePath = GetCurrentExePath();
        const auto serverExe = exePath.parent_path() / "MorbiServer.exe";
        const auto sampleScene = FindRepoSampleScene();

        // 5A: Single real client process -> MorbiServer.exe with scene loaded
        {
            std::wstring srvArgs = L"--bind 127.0.0.1 --port 27116 --max-clients 8 --tick-rate 60 --shutdown-after-disconnect";
            if (!sampleScene.empty())
            {
                srvArgs += L" --scene \"" + sampleScene.wstring() + L"\"";
            }

            auto srvProc = LaunchProcessWithPipe(serverExe, srvArgs);
            EXPECT_TRUE(srvProc.Process != nullptr, "Launched MorbiServer.exe for 1-client integration");

            std::string srvOut;
            const bool listening = WaitUntilPipeContains(srvProc, "Listening on 127.0.0.1:27116", 10000, srvOut);
            EXPECT_TRUE(listening, "MorbiServer reached listening state");

            auto cliProc = LaunchProcessWithPipe(exePath, L"--role=server-client --port=27116 --id=1 --hold-ms=50");
            EXPECT_TRUE(cliProc.Process != nullptr, "Launched separate client process");

            DWORD cliExit = 999;
            std::string cliOut;
            const bool cliOk = WaitAndCollectProcess(cliProc, 12000, cliExit, cliOut);
            EXPECT_TRUE(cliOk && cliExit == 0, "Client process exited with 0 (exit=" + std::to_string(cliExit) + ") " + cliOut);

            DWORD srvExit = 999;
            std::string srvTail;
            const bool srvOk = WaitAndCollectProcess(srvProc, 10000, srvExit, srvTail);
            srvOut += srvTail;
            EXPECT_TRUE(srvOk && srvExit == 0, "MorbiServer exited cleanly after disconnect (exit=" + std::to_string(srvExit) + ")");
            EXPECT_TRUE(srvOut.find("Active=1") != std::string::npos, "Server observed connection count Active=1");
            EXPECT_TRUE(srvOut.find("Active=0") != std::string::npos, "Server observed disconnection Active=0");
        }

        // 5B: Four concurrent client processes -> MorbiServer.exe
        {
            auto srvProc = LaunchProcessWithPipe(
                serverExe,
                L"--bind 127.0.0.1 --port 27117 --max-clients 8 --tick-rate 60 --shutdown-after-disconnect");
            EXPECT_TRUE(srvProc.Process != nullptr, "Launched MorbiServer.exe for 4-client integration");

            std::string srvOut;
            const bool listening = WaitUntilPipeContains(srvProc, "Listening on 127.0.0.1:27117", 10000, srvOut);
            EXPECT_TRUE(listening, "MorbiServer 4-client port listening");

            std::vector<ChildProcessHandle> clients(4);
            for (u32 i = 0; i < 4; ++i)
            {
                const std::wstring args = L"--role=server-client --port=27117 --id=" + std::to_wstring(i + 1) + L" --hold-ms=400";
                clients[i] = LaunchProcessWithPipe(exePath, args);
                EXPECT_TRUE(clients[i].Process != nullptr, "Launched concurrent client #" + std::to_string(i + 1));
            }

            for (u32 i = 0; i < 4; ++i)
            {
                DWORD cliExit = 999;
                std::string cliOut;
                const bool ok = WaitAndCollectProcess(clients[i], 15000, cliExit, cliOut);
                EXPECT_TRUE(ok && cliExit == 0, "Concurrent client #" + std::to_string(i + 1) + " exited 0 (exit=" + std::to_string(cliExit) + ")");
            }

            DWORD srvExit = 999;
            std::string srvTail;
            const bool srvOk = WaitAndCollectProcess(srvProc, 12000, srvExit, srvTail);
            srvOut += srvTail;
            EXPECT_TRUE(srvOk && srvExit == 0, "MorbiServer exited cleanly after 4 clients disconnected (exit=" + std::to_string(srvExit) + ")");
            EXPECT_TRUE(srvOut.find("Active=4") != std::string::npos, "Server observed 4 simultaneous clients (Active=4)");
            EXPECT_TRUE(srvOut.find("Active=0") != std::string::npos, "Server observed all 4 clients disconnected (Active=0)");
        }

        std::cout << "[PASS] Test_MorbiServerRealClientAndFourClientIntegration\n";
#endif
    }

    // ------------------------------------------------------------------------
    // 6. Legacy Low-Level Separate-Process ENet Integration Test
    // ------------------------------------------------------------------------

    int RunRawServerProcess(u16 port)
    {
        ConnectionManager server;
        ConnectionConfig cfg{};
        cfg.Mode = NetworkMode::DedicatedServer;
        cfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        cfg.Transport.MaxConnections = 8;
        cfg.SchemaHash = 0xCAFEBABEu;
        if (!server.Initialize(cfg))
        {
            return 10;
        }

        bool done = false;
        server.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            BitReader r(ev.Payload);
            std::string msg;
            if (r.ReadString(msg) && msg == "PING_FROM_CLIENT_PROC")
            {
                BitWriter reply(128);
                (void)reply.WriteString("PONG_FROM_SERVER_PROC");
                (void)server.SendPacket(
                    ev.Connection,
                    MessageCategory::Application,
                    10,
                    reply.GetData(),
                    NetDelivery::ReliableOrdered,
                    NetChannel::GameplayEvents);
            }
        });
        server.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) {
            done = true;
        });

        const auto start = std::chrono::steady_clock::now();
        while (!done)
        {
            server.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > 6000)
            {
                server.Shutdown();
                return 11;
            }
        }
        server.Shutdown();
        return 0;
    }

    int RunRawClientProcess(u16 port)
    {
        ConnectionManager client;
        ConnectionConfig cfg{};
        cfg.Mode = NetworkMode::Client;
        cfg.Transport.MaxConnections = 1;
        cfg.SchemaHash = 0xCAFEBABEu;
        if (!client.Initialize(cfg))
        {
            return 20;
        }

        bool gotPong = false;
        bool disconnected = false;
        ConnectionHandle handle = kInvalidConnectionHandle;

        client.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            handle = info.Handle;
            BitWriter w(128);
            (void)w.WriteString("PING_FROM_CLIENT_PROC");
            (void)client.SendPacket(
                handle,
                MessageCategory::Application,
                10,
                w.GetData(),
                NetDelivery::ReliableOrdered,
                NetChannel::GameplayEvents);
        });
        client.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            if (ev.Header.Category != MessageCategory::Application)
            {
                return;
            }
            BitReader r(ev.Payload);
            std::string reply;
            if (r.ReadString(reply) && reply == "PONG_FROM_SERVER_PROC")
            {
                gotPong = true;
                client.Disconnect(handle, "IntegrationSuccess");
            }
        });
        client.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) {
            disconnected = true;
        });

        if (!client.Connect(Endpoint{ "127.0.0.1", port }))
        {
            return 21;
        }

        const auto start = std::chrono::steady_clock::now();
        while (!disconnected)
        {
            client.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > 6000)
            {
                client.Shutdown();
                return 22;
            }
        }

        client.Shutdown();
        return gotPong ? 0 : 23;
    }

    void Test_SeparateProcessRawTransportIntegration()
    {
#if defined(_WIN32)
        const u16 port = 19703;
        const auto exePath = GetCurrentExePath();
        auto srvProc = LaunchProcessWithPipe(exePath, L"--role=server --port=" + std::to_wstring(port));
        EXPECT_TRUE(srvProc.Process != nullptr, "Launched raw server process");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        auto cliProc = LaunchProcessWithPipe(exePath, L"--role=client --port=" + std::to_wstring(port));
        EXPECT_TRUE(cliProc.Process != nullptr, "Launched raw client process");

        DWORD cliExit = 999, srvExit = 999;
        std::string cliOut, srvOut;
        const bool cliOk = WaitAndCollectProcess(cliProc, 8000, cliExit, cliOut);
        const bool srvOk = WaitAndCollectProcess(srvProc, 8000, srvExit, srvOut);
        EXPECT_TRUE(cliOk && cliExit == 0, "Raw client process exited 0");
        EXPECT_TRUE(srvOk && srvExit == 0, "Raw server process exited 0");
        std::cout << "[PASS] Test_SeparateProcessRawTransportIntegration\n";
#endif
    }

    // ------------------------------------------------------------------------
    // 7. Phase 3 Security, Cryptography, Rate Limiting & Session Tests
    // ------------------------------------------------------------------------

    class AdversarialTransportDecorator final : public ITransportBackend
    {
    public:
        std::unique_ptr<ITransportBackend> Inner;
        std::function<void(ConnectionHandle, std::span<const u8>, NetDelivery, NetChannel, bool& outSuppress)> OnBeforeSend;

        explicit AdversarialTransportDecorator(std::unique_ptr<ITransportBackend> inner = nullptr)
            : Inner(inner ? std::move(inner) : CreateEnetTransportBackend())
        {
        }

        NetResult<bool> Initialize(const TransportConfig& config, bool isServer) override
        {
            return Inner->Initialize(config, isServer);
        }
        void Shutdown() override { Inner->Shutdown(); }
        bool IsInitialized() const noexcept override { return Inner->IsInitialized(); }
        NetResult<ConnectionHandle> Connect(const Endpoint& remote) override { return Inner->Connect(remote); }
        void Disconnect(ConnectionHandle connection, u32 reasonCode, bool immediate) override
        {
            Inner->Disconnect(connection, reasonCode, immediate);
        }
        SendResult Send(ConnectionHandle connection, std::span<const u8> payload, NetDelivery delivery, NetChannel channel) override
        {
            bool suppress = false;
            if (OnBeforeSend)
            {
                OnBeforeSend(connection, payload, delivery, channel, suppress);
            }
            if (suppress)
            {
                return { SendStatus::Queued, {} };
            }
            return Inner->Send(connection, payload, delivery, channel);
        }
        SendResult InjectRaw(ConnectionHandle connection, std::span<const u8> rawWire, NetDelivery delivery, NetChannel channel)
        {
            return Inner->Send(connection, rawWire, delivery, channel);
        }
        void Poll(ITransportEventSink& sink) override { Inner->Poll(sink); }
        void SetSimulationConfig(const NetSimConfig& sim) override { Inner->SetSimulationConfig(sim); }
        bool TryGetConnectionStats(ConnectionHandle connection, NetworkStats& outStats) const override
        {
            return Inner->TryGetConnectionStats(connection, outStats);
        }
        bool TryGetRemoteEndpoint(ConnectionHandle connection, Endpoint& outEndpoint) const override
        {
            return Inner->TryGetRemoteEndpoint(connection, outEndpoint);
        }
    };

    std::vector<u8> MakeEncodedToken(
        const Ed25519KeyPair& issuerKeys,
        PlayerId playerId,
        std::string_view playerName,
        i64 expiresDeltaMs,
        std::span<const u8> boundServerPubKey = {})
    {
        const u64 nowUnix = CryptoContext::GetUnixNowMs();
        SignedConnectToken unsignedTok{};
        unsignedTok.PlayerIdValue = playerId;
        unsignedTok.PlayerName = std::string(playerName);
        unsignedTok.IssuedAtUnixMs = nowUnix - 1000;
        if (expiresDeltaMs >= 0)
        {
            unsignedTok.ExpiresAtUnixMs = nowUnix + static_cast<u64>(expiresDeltaMs);
        }
        else
        {
            unsignedTok.ExpiresAtUnixMs = unsignedTok.IssuedAtUnixMs + 1; // already in the past relative to nowUnix
        }
        unsignedTok.Roles = 3u;
        if (boundServerPubKey.size() == kEd25519PublicKeyBytes)
        {
            unsignedTok.HasBoundServerPublicKey = true;
            std::memcpy(unsignedTok.BoundServerPublicKey.data(), boundServerPubKey.data(), kEd25519PublicKeyBytes);
        }

        SignedConnectToken signedTok{};
        std::vector<u8> encoded;
        if (CryptoContext::CreateSignedConnectToken(unsignedTok, issuerKeys.SecretKey, signedTok))
        {
            (void)CryptoContext::EncodeSignedConnectToken(signedTok, encoded);
        }
        return encoded;
    }

    int RunTokenAuthClientProcess(u16 port, const std::string& tokenHex)
    {
        ConnectionManager client;
        ConnectionConfig cfg{};
        cfg.Mode = NetworkMode::Client;
        cfg.Transport.MaxConnections = 1;
        cfg.SchemaHash = 1;
        if (!client.Initialize(cfg))
        {
            return 40;
        }

        ConnectAuthOptions auth{};
        auth.PlayerDisplayName = "TokenProcPlayer";
        if (!tokenHex.empty())
        {
            auth.EncodedConnectToken.resize(tokenHex.size() / 2);
            if (!CryptoContext::HexDecode(tokenHex, auth.EncodedConnectToken))
            {
                return 41;
            }
        }

        bool gotEcho = false;
        bool disconnected = false;
        std::string disconnectReason;
        ConnectionHandle handle = kInvalidConnectionHandle;

        client.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            handle = info.Handle;
            BitWriter pingW(128);
            (void)pingW.WriteString("TOKEN_AUTH_PING");
            (void)client.SendPacket(
                handle,
                MessageCategory::Application,
                static_cast<u8>(ServerAppMessageSubType::PingEcho),
                pingW.GetData(),
                NetDelivery::ReliableOrdered,
                NetChannel::GameplayEvents);
        });

        client.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            if (ev.Header.Category != MessageCategory::Application)
            {
                return;
            }
            BitReader r(ev.Payload);
            std::string reply;
            if (ev.Header.SubType == static_cast<u8>(ServerAppMessageSubType::PingEcho) &&
                r.ReadString(reply) &&
                reply == "PONG:TOKEN_AUTH_PING")
            {
                gotEcho = true;
                client.Disconnect(handle, "TokenClientDone");
            }
        });

        client.SetOnDisconnected([&](const ConnectionInfo&, const std::string& reason) {
            disconnected = true;
            disconnectReason = reason;
        });

        if (!client.Connect(Endpoint{ "127.0.0.1", port }, auth))
        {
            return 42;
        }

        const auto start = std::chrono::steady_clock::now();
        while (!disconnected)
        {
            client.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > 8000)
            {
                client.Shutdown();
                return 43;
            }
        }
        client.Shutdown();
        return gotEcho ? 0 : 44;
    }

    void Test_AuthenticatedHandshakeSessionModelAndPinning()
    {
        Ed25519KeyPair issuerKeys{};
        Ed25519KeyPair serverKeys{};
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(issuerKeys), "Generate token issuer keypair");
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(serverKeys), "Generate server Ed25519 keypair");

        const u16 port = 19710;
        ConnectionManager server;
        ConnectionConfig serverCfg{};
        serverCfg.Mode = NetworkMode::DedicatedServer;
        serverCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        serverCfg.Transport.MaxConnections = 8;
        serverCfg.SchemaHash = 0x77771111u;
        serverCfg.Security.HasServerIdentityKeyPair = true;
        serverCfg.Security.ServerIdentityPublicKey = serverKeys.PublicKey;
        serverCfg.Security.ServerIdentitySecretKey = serverKeys.SecretKey.Bytes;
        serverCfg.Security.RequireSignedConnectToken = true;
        serverCfg.Security.HasTokenIssuerPublicKey = true;
        serverCfg.Security.TokenIssuerPublicKey = issuerKeys.PublicKey;
        EXPECT_TRUE(server.Initialize(serverCfg).IsSuccess(), "Authenticated server initialized");

        ConnectionManager client;
        ConnectionConfig clientCfg{};
        clientCfg.Mode = NetworkMode::Client;
        clientCfg.SchemaHash = 0x77771111u;
        clientCfg.Security.RequirePinnedServerPublicKey = true;
        clientCfg.Security.ExpectedServerPublicKey = serverKeys.PublicKey;
        EXPECT_TRUE(client.Initialize(clientCfg).IsSuccess(), "Pinned client initialized");

        ConnectionInfo srvConnInfo{};
        ConnectionInfo cliConnInfo{};
        bool srvConnected = false;
        bool cliConnected = false;
        std::string srvReceived;
        server.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            srvConnInfo = info;
            srvConnected = true;
        });
        client.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            cliConnInfo = info;
            cliConnected = true;
        });
        server.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            BitReader r(ev.Payload);
            (void)r.ReadString(srvReceived);
        });

        ConnectAuthOptions authOpts{};
        authOpts.PlayerDisplayName = "HeroOne";
        authOpts.EncodedConnectToken = MakeEncodedToken(issuerKeys, 90001ull, "HeroOne", 60000, serverKeys.PublicKey);
        EXPECT_TRUE(!authOpts.EncodedConnectToken.empty(), "Created bound signed connect token");

        auto connRes = client.Connect(Endpoint{ "127.0.0.1", port }, authOpts);
        EXPECT_TRUE(connRes.IsSuccess(), "Connect initiated with token");
        EXPECT_TRUE(PumpUntil(server, client, [&]() { return srvConnected && cliConnected; }),
            "Authenticated & server-pinned handshake completed");

        EXPECT_TRUE(srvConnInfo.Session.IsValid() && srvConnInfo.Session == cliConnInfo.Session,
            "128-bit CSPRNG SessionId matches across server and client");
        EXPECT_TRUE(srvConnInfo.IsEncrypted && srvConnInfo.IsAuthenticated &&
                    cliConnInfo.IsEncrypted && cliConnInfo.IsAuthenticated,
            "ConnectionInfo reports encrypted & authenticated");
        EXPECT_TRUE(srvConnInfo.AuthenticatedPlayerId == 90001ull && cliConnInfo.AuthenticatedPlayerId == 90001ull,
            "PlayerIdentity ID propagated cleanly");

        // Verify separated SessionRegistry architecture: multiple players + multiple entities per session
        auto& registry = server.GetSessionRegistry();
        EXPECT_TRUE(registry.AttachPlayer(srvConnInfo.Session, PlayerIdentity{
            .Id = 90002ull,
            .DisplayName = "SplitScreenCoopPartner",
            .Roles = 1,
            .VerifiedByToken = false,
            .AuthenticatedAtMs = 1000,
            .BoundSession = srvConnInfo.Session
        }), "Attached second logical PlayerIdentity to same NetworkSession");
        EXPECT_TRUE(registry.AssignSessionWorld(srvConnInfo.Session, 42u, 0xABCD1234u), "Assigned dungeon WorldInstanceId");
        EXPECT_TRUE(registry.AssignSessionParty(srvConnInfo.Session, 555ull), "Assigned future PartyId");
        EXPECT_TRUE(registry.BindEntity(NetworkEntityBinding{
            .NetId = NetworkId{ 10, 1 },
            .OwnerSession = srvConnInfo.Session,
            .OwnerPlayer = 90001ull,
            .WorldId = 42u,
            .SceneEntityIndex = 10u,
            .SceneEntityGeneration = 1u,
            .Authority = NetworkAuthority::Server
        }), "Bound first network entity to session+player");
        EXPECT_TRUE(registry.BindEntity(NetworkEntityBinding{
            .NetId = NetworkId{ 11, 1 },
            .OwnerSession = srvConnInfo.Session,
            .OwnerPlayer = 90002ull,
            .WorldId = 42u,
            .SceneEntityIndex = 11u,
            .SceneEntityGeneration = 1u,
            .Authority = NetworkAuthority::ClientPredicted
        }), "Bound second network entity to co-op player");

        NetworkSession regSess{};
        EXPECT_TRUE(registry.TryGetSessionByConnection(srvConnInfo.Handle, regSess) &&
                    regSess.Players.size() == 2u &&
                    regSess.OwnedEntities.size() == 2u &&
                    regSess.WorldId == 42u &&
                    regSess.PartyId == 555ull,
            "SessionRegistry tracks N players and N entities on 1 connection without equating 1:1:1");

        BitWriter w(64);
        (void)w.WriteString("EncryptedSecretPayload");
        EXPECT_TRUE(client.SendPacket(cliConnInfo.Handle, MessageCategory::Application, 1, w.GetData()).Succeeded(),
            "Sent encrypted application packet");
        EXPECT_TRUE(PumpUntil(server, client, [&]() { return srvReceived == "EncryptedSecretPayload"; }),
            "Server decrypted and delivered authentic message");
        EXPECT_TRUE(server.GetSecurityCounters().SuccessfulHandshakes == 1u, "Security counters recorded 1 successful handshake");

        client.Shutdown();
        server.Shutdown();
        std::cout << "[PASS] Test_AuthenticatedHandshakeSessionModelAndPinning\n";
    }

    void Test_InvalidAuthTokenReplayAndWrongServerPin()
    {
        Ed25519KeyPair issuerKeys{};
        Ed25519KeyPair wrongIssuerKeys{};
        Ed25519KeyPair serverKeys{};
        Ed25519KeyPair wrongServerKeys{};
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(issuerKeys), "Issuer keypair");
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(wrongIssuerKeys), "Wrong issuer keypair");
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(serverKeys), "Server keypair");
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(wrongServerKeys), "Wrong server keypair");

        const u16 port = 19711;
        ConnectionManager server;
        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        srvCfg.Transport.MaxConnections = 16;
        srvCfg.Security.HasServerIdentityKeyPair = true;
        srvCfg.Security.ServerIdentityPublicKey = serverKeys.PublicKey;
        srvCfg.Security.ServerIdentitySecretKey = serverKeys.SecretKey.Bytes;
        srvCfg.Security.RequireSignedConnectToken = true;
        srvCfg.Security.HasTokenIssuerPublicKey = true;
        srvCfg.Security.TokenIssuerPublicKey = issuerKeys.PublicKey;
        srvCfg.Security.RateLimit.Enabled = false; // test auth logic cleanly without IP flood trigger
        EXPECT_TRUE(server.Initialize(srvCfg).IsSuccess(), "Auth rejection test server init");

        auto tryAuth = [&](const ConnectAuthOptions& opts, bool pinWrongServer, const char* label) {
            ConnectionManager cli;
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            if (pinWrongServer)
            {
                cliCfg.Security.RequirePinnedServerPublicKey = true;
                cliCfg.Security.ExpectedServerPublicKey = wrongServerKeys.PublicKey;
            }
            EXPECT_TRUE(cli.Initialize(cliCfg).IsSuccess(), std::string(label) + " init");
            bool connected = false;
            bool disconnected = false;
            cli.SetOnConnected([&](const ConnectionInfo&, const std::string&) { connected = true; });
            cli.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { disconnected = true; });
            (void)cli.Connect(Endpoint{ "127.0.0.1", port }, opts);
            EXPECT_TRUE(PumpUntil(server, cli, [&]() { return disconnected; }), std::string(label) + " disconnected");
            EXPECT_TRUE(!connected, std::string(label) + " never became Connected");
            cli.Shutdown();
        };

        // Case A: missing token
        tryAuth(ConnectAuthOptions{}, false, "Missing token");

        // Case B: token signed by untrusted issuer
        ConnectAuthOptions badIssuerOpts{};
        badIssuerOpts.EncodedConnectToken = MakeEncodedToken(wrongIssuerKeys, 1001ull, "Untrusted", 60000);
        tryAuth(badIssuerOpts, false, "Untrusted token issuer");

        // Case C: expired token
        ConnectAuthOptions expiredOpts{};
        expiredOpts.EncodedConnectToken = MakeEncodedToken(issuerKeys, 1002ull, "ExpiredUser", -5000);
        tryAuth(expiredOpts, false, "Expired token");

        // Case D: token tampered signature byte
        ConnectAuthOptions tamperedTokOpts{};
        tamperedTokOpts.EncodedConnectToken = MakeEncodedToken(issuerKeys, 1003ull, "TamperUser", 60000);
        tamperedTokOpts.EncodedConnectToken.back() ^= 0x5Au;
        tryAuth(tamperedTokOpts, false, "Tampered token signature");

        // Case E: wrong server public key pin
        ConnectAuthOptions validTokOpts{};
        validTokOpts.EncodedConnectToken = MakeEncodedToken(issuerKeys, 1004ull, "ValidOnce", 60000);
        tryAuth(validTokOpts, true, "Wrong server public key pin");

        // Case F: spend valid token once, then replay the exact same token (must reject second use!)
        {
            ConnectionManager goodCli;
            ConnectionConfig goodCfg{};
            goodCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(goodCli.Initialize(goodCfg).IsSuccess(), "Valid token first use init");
            bool connected = false;
            goodCli.SetOnConnected([&](const ConnectionInfo&, const std::string&) { connected = true; });
            (void)goodCli.Connect(Endpoint{ "127.0.0.1", port }, validTokOpts);
            EXPECT_TRUE(PumpUntil(server, goodCli, [&]() { return connected; }), "First token use succeeds");
            goodCli.Shutdown();
            PumpPair(server, goodCli, 15, 2);

            tryAuth(validTokOpts, false, "Replayed ConnectToken");
        }

        EXPECT_TRUE(server.GetSecurityCounters().AuthFailures >= 5u, "Server recorded auth failures");
        EXPECT_TRUE(server.GetSecurityCounters().TokenReplayDrops >= 1u, "Server recorded spent ConnectToken replay drop");
        server.Shutdown();
        std::cout << "[PASS] Test_InvalidAuthTokenReplayAndWrongServerPin\n";
    }

    void Test_PacketReplayAndTamperingProtection()
    {
        const u16 port = 19712;
        ConnectionManager server;
        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        srvCfg.SchemaHash = 0x42424242u;
        srvCfg.Security.RateLimit.MaxSecurityStrikes = 4; // allow 1 replay + 1 tamper before strike cutoff tests
        EXPECT_TRUE(server.Initialize(srvCfg).IsSuccess(), "Replay/tamper server init");

        auto rawDecorator = std::make_unique<AdversarialTransportDecorator>();
        auto* decoratorPtr = rawDecorator.get();
        std::vector<std::vector<u8>> capturedAppPackets;
        bool captureEncryptedApp = false;
        decoratorPtr->OnBeforeSend = [&](ConnectionHandle, std::span<const u8> payload, NetDelivery, NetChannel, bool&) {
            if (captureEncryptedApp)
            {
                capturedAppPackets.emplace_back(payload.begin(), payload.end());
            }
        };

        ConnectionManager client(std::move(rawDecorator));
        ConnectionConfig cliCfg{};
        cliCfg.Mode = NetworkMode::Client;
        cliCfg.SchemaHash = 0x42424242u;
        EXPECT_TRUE(client.Initialize(cliCfg).IsSuccess(), "Replay/tamper client init");

        bool srvConnected = false;
        bool cliConnected = false;
        ConnectionHandle cliHandle = kInvalidConnectionHandle;
        std::vector<std::string> serverMessages;

        server.SetOnConnected([&](const ConnectionInfo&, const std::string&) { srvConnected = true; });
        client.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            cliConnected = true;
            cliHandle = info.Handle;
        });
        server.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            BitReader r(ev.Payload);
            std::string s;
            if (r.ReadString(s))
            {
                serverMessages.push_back(s);
            }
        });

        (void)client.Connect(Endpoint{ "127.0.0.1", port });
        EXPECT_TRUE(PumpUntil(server, client, [&]() { return srvConnected && cliConnected; }), "Handshake complete for replay/tamper test");

        // Send legitimate encrypted packet #1 while capturing raw wire bytes
        captureEncryptedApp = true;
        BitWriter m1(64);
        (void)m1.WriteString("AuthenticMessage_1");
        EXPECT_TRUE(client.SendPacket(cliHandle, MessageCategory::Application, 1, m1.GetData()).Succeeded(), "Send authentic #1");
        captureEncryptedApp = false;

        EXPECT_TRUE(PumpUntil(server, client, [&]() { return serverMessages.size() == 1u; }), "Server delivered authentic #1");
        EXPECT_TRUE(capturedAppPackets.size() == 1u, "Captured real encrypted wire bytes");

        // 1) Replay exact captured wire bytes -> must be dropped by ReplayWindow without delivering again
        (void)decoratorPtr->InjectRaw(cliHandle, capturedAppPackets[0], NetDelivery::ReliableOrdered, NetChannel::GameplayEvents);
        PumpPair(server, client, 25, 2);
        EXPECT_TRUE(serverMessages.size() == 1u, "Replayed packet did NOT redeliver application message");
        EXPECT_TRUE(server.GetSecurityCounters().ReplayDrops >= 1u, "Server recorded ReplayDrop");

        // 2) Tamper AAD / PacketSequence with huge fake seq (0xFFFFFFFFFFFFull) + flipped ciphertext byte:
        //    Verify AEAD rejects AND ReplayWindow is NOT poisoned (subsequent authentic packet #2 still works!)
        {
            std::vector<u8> poisoned = capturedAppPackets[0];
            // WireEnvelope: Magic(4) + Flags(1) + Ver(2) + PacketSeq(8 at offset 7..14)
            poisoned[7] = 0xFFu;
            poisoned[8] = 0xFFu;
            poisoned[9] = 0xFFu;
            poisoned.back() ^= 0x33u;
            (void)decoratorPtr->InjectRaw(cliHandle, poisoned, NetDelivery::ReliableOrdered, NetChannel::GameplayEvents);
            PumpPair(server, client, 25, 2);
            EXPECT_TRUE(serverMessages.size() == 1u, "Tampered packet rejected before delivery");
            EXPECT_TRUE(server.GetSecurityCounters().TamperDrops >= 1u, "Server recorded TamperDrop");
        }

        // Send authentic packet #2 -> proves ReplayWindow was NOT shifted by unauthenticated huge sequence!
        BitWriter m2(64);
        (void)m2.WriteString("AuthenticMessage_2");
        EXPECT_TRUE(client.SendPacket(cliHandle, MessageCategory::Application, 1, m2.GetData()).Succeeded(), "Send authentic #2");
        EXPECT_TRUE(PumpUntil(server, client, [&]() { return serverMessages.size() == 2u; }),
            "Authentic packet #2 succeeded (ReplayWindow unpoisoned by forged seq)");

        // 3) Repeated AEAD tampering exceeds MaxSecurityStrikes -> automatic server disconnect
        {
            std::vector<u8> tampered = capturedAppPackets[0];
            tampered.back() ^= 0xFFu;
            for (int i = 0; i < 3; ++i)
            {
                (void)decoratorPtr->InjectRaw(cliHandle, tampered, NetDelivery::ReliableOrdered, NetChannel::GameplayEvents);
            }
            bool cliDisconnected = false;
            client.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { cliDisconnected = true; });
            EXPECT_TRUE(PumpUntil(server, client, [&]() { return cliDisconnected || server.GetConnectionCount() == 0; }),
                "Repeated tampering triggered automatic server disconnect");
            EXPECT_TRUE(serverMessages.size() == 2u, "Tampered packets never delivered");
        }

        client.Shutdown();
        server.Shutdown();
        std::cout << "[PASS] Test_PacketReplayAndTamperingProtection\n";
    }

    void Test_DuplicateHandshakeMalformedPayloadsAndTimeouts()
    {
        // A. Handshake timeout + duplicate handshake + malformed packet rejection
        const u16 port = 19713;
        ConnectionManager server;
        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        srvCfg.HandshakeTimeoutMs = 150;
        srvCfg.Security.RateLimit.Enabled = false;
        EXPECT_TRUE(server.Initialize(srvCfg).IsSuccess(), "Timeout/malformed server init");

        // Client 1: Stalls handshake (suppress ClientHello) -> triggers server HandshakeTimeout
        {
            auto stallDec = std::make_unique<AdversarialTransportDecorator>();
            stallDec->OnBeforeSend = [&](ConnectionHandle, std::span<const u8>, NetDelivery, NetChannel, bool& suppress) {
                suppress = true;
            };
            ConnectionManager stalledCli(std::move(stallDec));
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            cliCfg.HandshakeTimeoutMs = 2000;
            EXPECT_TRUE(stalledCli.Initialize(cliCfg).IsSuccess(), "Stalled client init");
            bool stalledDisconn = false;
            stalledCli.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { stalledDisconn = true; });
            (void)stalledCli.Connect(Endpoint{ "127.0.0.1", port });
            EXPECT_TRUE(PumpUntil(server, stalledCli, [&]() { return stalledDisconn && server.GetConnectionCount() == 0; }, 3000),
                "Server reclaims stalled unauthenticated connection on HandshakeTimeout");
            EXPECT_TRUE(server.GetSecurityCounters().TimeoutDrops >= 1u, "TimeoutDrops incremented");
            stalledCli.Shutdown();
        }

        // Client 2: Normal handshake, then inject duplicate plaintext ClientHello + malformed wire packet -> immediate revoke
        {
            auto dupDec = std::make_unique<AdversarialTransportDecorator>();
            auto* dupPtr = dupDec.get();
            std::vector<u8> capturedHello;
            dupPtr->OnBeforeSend = [&](ConnectionHandle, std::span<const u8> payload, NetDelivery, NetChannel, bool&) {
                if (capturedHello.empty())
                {
                    capturedHello.assign(payload.begin(), payload.end());
                }
            };
            ConnectionManager cli(std::move(dupDec));
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(cli.Initialize(cliCfg).IsSuccess(), "Duplicate-handshake client init");
            bool connected = false;
            bool disconnected = false;
            ConnectionHandle handle = kInvalidConnectionHandle;
            cli.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
                connected = true;
                handle = info.Handle;
            });
            cli.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { disconnected = true; });
            (void)cli.Connect(Endpoint{ "127.0.0.1", port });
            EXPECT_TRUE(PumpUntil(server, cli, [&]() { return connected; }), "Client connected before injecting malformed/duplicate");

            // Send truncated 3-byte packet first
            const u8 tiny[3] = { 0x4D, 0x52, 0x00 };
            (void)dupPtr->InjectRaw(handle, tiny, NetDelivery::ReliableOrdered, NetChannel::Control);
            PumpPair(server, cli, 10, 2);
            EXPECT_TRUE(server.GetSecurityCounters().MalformedDrops >= 1u, "Malformed short packet dropped");

            // Now send duplicate plaintext ClientHello on an already Connected session -> must revoke & disconnect!
            (void)dupPtr->InjectRaw(handle, capturedHello, NetDelivery::ReliableOrdered, NetChannel::Control);
            EXPECT_TRUE(PumpUntil(server, cli, [&]() { return disconnected || server.GetConnectionCount() == 0; }),
                "Duplicate ClientHello on active session forced immediate disconnect");
            EXPECT_TRUE(server.GetSecurityCounters().DuplicateHandshakeDrops >= 1u, "DuplicateHandshakeDrops incremented");
            cli.Shutdown();
        }

        // B. Direct BitReader unbounded-allocation protection check (0xFFFFFFFF string length on 4-byte packet)
        {
            BitWriter malicious(16);
            (void)malicious.WriteU32(0xFFFFFFFFu);
            BitReader r(malicious.GetData());
            std::vector<u8> out;
            EXPECT_TRUE(!r.ReadByteVector(out) && out.empty(), "Adversarial huge byte vector length rejected without huge allocation");
        }

        server.Shutdown();
        std::cout << "[PASS] Test_DuplicateHandshakeMalformedPayloadsAndTimeouts\n";
    }

    void Test_InvalidProtocolVersionNegotiation()
    {
        const u16 port = 19714;
        ConnectionManager server;
        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        srvCfg.MinProtocolVersion = 2;
        srvCfg.MaxProtocolVersion = 3;
        EXPECT_TRUE(server.Initialize(srvCfg).IsSuccess(), "Version negotiation server init");

        // Incompatible client version (99..99)
        {
            ConnectionManager oldCli;
            ConnectionConfig badCfg{};
            badCfg.Mode = NetworkMode::Client;
            badCfg.MinProtocolVersion = 99;
            badCfg.MaxProtocolVersion = 99;
            EXPECT_TRUE(oldCli.Initialize(badCfg).IsSuccess(), "Incompatible-version client init");
            bool connected = false;
            bool rejected = false;
            oldCli.SetOnConnected([&](const ConnectionInfo&, const std::string&) { connected = true; });
            oldCli.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { rejected = true; });
            (void)oldCli.Connect(Endpoint{ "127.0.0.1", port });
            EXPECT_TRUE(PumpUntil(server, oldCli, [&]() { return rejected; }), "Incompatible protocol version rejected");
            EXPECT_TRUE(!connected, "Incompatible version client never connected");
            EXPECT_TRUE(server.GetSecurityCounters().ProtocolMismatchDrops >= 1u, "ProtocolMismatchDrops incremented");
            oldCli.Shutdown();
        }

        // Overlapping range (client 1..2 vs server 2..3 -> negotiates 2)
        {
            ConnectionManager compatCli;
            ConnectionConfig goodCfg{};
            goodCfg.Mode = NetworkMode::Client;
            goodCfg.MinProtocolVersion = 1;
            goodCfg.MaxProtocolVersion = 2;
            EXPECT_TRUE(compatCli.Initialize(goodCfg).IsSuccess(), "Range-compatible client init");
            ConnectionInfo connInfo{};
            bool connected = false;
            compatCli.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
                connInfo = info;
                connected = true;
            });
            (void)compatCli.Connect(Endpoint{ "127.0.0.1", port });
            EXPECT_TRUE(PumpUntil(server, compatCli, [&]() { return connected; }), "Overlapping protocol version range negotiated");
            EXPECT_TRUE(connInfo.NegotiatedProtocolVersion == 2u, "Negotiated highest common version (v2)");
            compatCli.Shutdown();
        }

        server.Shutdown();
        std::cout << "[PASS] Test_InvalidProtocolVersionNegotiation\n";
    }

    void Test_ReconnectLifecycleAndExpiredSession()
    {
        const u16 port = 19715;
        ConnectionManager server;
        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        srvCfg.Security.ReconnectWindowMs = 600;
        srvCfg.Security.RateLimit.Enabled = false;
        EXPECT_TRUE(server.Initialize(srvCfg).IsSuccess(), "Reconnect server init");

        SessionId originalSession{};
        PlayerId originalPlayer = kInvalidPlayerId;
        std::vector<u8> reconnectTicket;

        // 1) Initial connect + bind entity in SessionRegistry
        {
            ConnectionManager cli1;
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(cli1.Initialize(cliCfg).IsSuccess(), "Initial client init");
            bool connected = false;
            bool disconnected = false;
            ConnectionHandle handle = kInvalidConnectionHandle;
            cli1.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
                connected = true;
                handle = info.Handle;
                originalSession = info.Session;
                originalPlayer = info.AuthenticatedPlayerId;
            });
            cli1.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { disconnected = true; });
            (void)cli1.Connect(Endpoint{ "127.0.0.1", port });
            EXPECT_TRUE(PumpUntil(server, cli1, [&]() { return connected; }), "Initial session connected");
            EXPECT_TRUE(cli1.TryGetLastReconnectTicket(handle, reconnectTicket) && !reconnectTicket.empty(),
                "Client received sealed ReconnectTicket");
            EXPECT_TRUE(server.GetSessionRegistry().BindEntity(NetworkEntityBinding{
                .NetId = NetworkId{ 77, 1 },
                .OwnerSession = originalSession,
                .OwnerPlayer = originalPlayer,
                .WorldId = 1,
                .SceneEntityIndex = 77,
                .SceneEntityGeneration = 1,
                .Authority = NetworkAuthority::ClientPredicted
            }), "Bound entity to initial session");

            cli1.Disconnect(handle, "TemporaryDrop", true);
            EXPECT_TRUE(PumpUntil(server, cli1, [&]() { return disconnected && server.GetConnectionCount() == 0; }),
                "Initial client disconnected gracefully");
            EXPECT_TRUE(server.GetSessionRegistry().GetSuspendedSessionCount() == 1u,
                "Server placed dropped session in SuspendedReconnectable state");
            cli1.Shutdown();
        }

        // 2) Reconnect within ReconnectWindowMs -> must preserve SessionId, PlayerId, Entity binding, and rotate ticket!
        std::vector<u8> rotatedTicket;
        {
            ConnectionManager cli2;
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(cli2.Initialize(cliCfg).IsSuccess(), "Reconnect client init");
            ConnectionInfo resumedInfo{};
            bool reconnected = false;
            bool disconnected = false;
            cli2.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
                resumedInfo = info;
                reconnected = true;
            });
            cli2.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { disconnected = true; });
            (void)cli2.Reconnect(Endpoint{ "127.0.0.1", port }, originalSession, reconnectTicket);
            EXPECT_TRUE(PumpUntil(server, cli2, [&]() { return reconnected; }), "Session reconnected within window");
            EXPECT_TRUE(resumedInfo.Session == originalSession && resumedInfo.AuthenticatedPlayerId == originalPlayer,
                "Resumed session preserved logical SessionId & PlayerId");
            EXPECT_TRUE(server.GetSecurityCounters().SuccessfulReconnects == 1u, "SuccessfulReconnects incremented");

            NetworkEntityBinding bound{};
            EXPECT_TRUE(server.GetSessionRegistry().TryGetEntityBinding(NetworkId{ 77, 1 }, bound) &&
                        bound.OwnerSession == originalSession,
                "Owned network entity preserved across reconnect");
            EXPECT_TRUE(cli2.TryGetLastReconnectTicket(resumedInfo.Handle, rotatedTicket) && rotatedTicket != reconnectTicket,
                "Server issued fresh rotated ReconnectTicket");

            // Drop again using graceful allowReconnect=true, then attempt REPLAY of OLD ticket (should fail!)
            cli2.Disconnect(resumedInfo.Handle, "DropBeforeReplayCheck", true);
            EXPECT_TRUE(PumpUntil(server, cli2, [&]() { return disconnected && server.GetConnectionCount() == 0; }),
                "Disconnected after reconnect");
            cli2.Shutdown();
        }

        // Replay old consumed ticket -> rejected!
        {
            ConnectionManager replayCli;
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(replayCli.Initialize(cliCfg).IsSuccess(), "Replay-ticket client init");
            bool connected = false;
            bool rejected = false;
            replayCli.SetOnConnected([&](const ConnectionInfo&, const std::string&) { connected = true; });
            replayCli.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { rejected = true; });
            (void)replayCli.Reconnect(Endpoint{ "127.0.0.1", port }, originalSession, reconnectTicket);
            EXPECT_TRUE(PumpUntil(server, replayCli, [&]() { return rejected; }), "Old spent reconnect ticket rejected");
            EXPECT_TRUE(!connected, "Old reconnect ticket did not authenticate");
            replayCli.Shutdown();
        }

        // Wait past ReconnectWindowMs (600ms) -> rotatedTicket is now expired and pruned!
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        server.Poll();
        EXPECT_TRUE(server.GetSessionRegistry().GetSuspendedSessionCount() == 0u &&
                    server.GetSessionRegistry().GetEntityBindingCount() == 0u,
            "Expired session and its entity bindings pruned cleanly");

        {
            ConnectionManager expiredCli;
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(expiredCli.Initialize(cliCfg).IsSuccess(), "Expired-session client init");
            bool connected = false;
            bool rejected = false;
            expiredCli.SetOnConnected([&](const ConnectionInfo&, const std::string&) { connected = true; });
            expiredCli.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { rejected = true; });
            (void)expiredCli.Reconnect(Endpoint{ "127.0.0.1", port }, originalSession, rotatedTicket);
            EXPECT_TRUE(PumpUntil(server, expiredCli, [&]() { return rejected; }), "Expired session reconnect rejected");
            EXPECT_TRUE(!connected, "Expired session client never connected");
            expiredCli.Shutdown();
        }

        server.Shutdown();
        std::cout << "[PASS] Test_ReconnectLifecycleAndExpiredSession\n";
    }

    void Test_ConnectionFloodRateLimiting()
    {
        const u16 port = 19716;
        ConnectionManager server;
        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", port };
        srvCfg.Transport.MaxConnections = 32;
        srvCfg.Security.RateLimit.Enabled = true;
        srvCfg.Security.RateLimit.MaxConnectAttemptsPerIpWindow = 3;
        srvCfg.Security.RateLimit.ConnectRateWindowMs = 5000;
        srvCfg.Security.RateLimit.MaxPendingHandshakesPerIp = 2;
        srvCfg.Security.RateLimit.IpCooldownMs = 3000;
        EXPECT_TRUE(server.Initialize(srvCfg).IsSuccess(), "Flood-limited server init");

        std::vector<std::unique_ptr<ConnectionManager>> floodClients;
        int connectedCount = 0;
        int disconnectedCount = 0;
        for (int i = 0; i < 6; ++i)
        {
            auto cli = std::make_unique<ConnectionManager>();
            ConnectionConfig cliCfg{};
            cliCfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(cli->Initialize(cliCfg).IsSuccess(), "Flood client init");
            cli->SetOnConnected([&](const ConnectionInfo&, const std::string&) { ++connectedCount; });
            cli->SetOnDisconnected([&](const ConnectionInfo&, const std::string&) { ++disconnectedCount; });
            (void)cli->Connect(Endpoint{ "127.0.0.1", port });
            floodClients.push_back(std::move(cli));
        }

        const auto start = std::chrono::steady_clock::now();
        while (server.GetSecurityCounters().RateLimitDrops == 0 &&
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000)
        {
            server.Poll();
            for (auto& cli : floodClients)
            {
                cli->Poll();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // Pump a bit more so disconnected events propagate cleanly
        for (int iter = 0; iter < 50; ++iter)
        {
            server.Poll();
            for (auto& cli : floodClients)
            {
                cli->Poll();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        EXPECT_TRUE(server.GetSecurityCounters().RateLimitDrops >= 3u, "Server rate-limited flood connections");
        EXPECT_TRUE(connectedCount <= 3, "At most MaxConnectAttemptsPerIpWindow clients allowed");

        for (auto& cli : floodClients)
        {
            cli->Shutdown();
        }
        server.Shutdown();
        std::cout << "[PASS] Test_ConnectionFloodRateLimiting\n";
    }

    void Test_MorbiServerTokenAuthSeparateProcess()
    {
#if defined(_WIN32)
        const auto exePath = GetCurrentExePath();
        const auto serverExe = exePath.parent_path() / "MorbiServer.exe";

        Ed25519KeyPair issuerKeys{};
        Ed25519KeyPair wrongIssuerKeys{};
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(issuerKeys), "Token test issuer keypair");
        EXPECT_TRUE(CryptoContext::GenerateEd25519KeyPair(wrongIssuerKeys), "Token test wrong keypair");
        const std::string issuerHex = CryptoContext::HexEncode(issuerKeys.PublicKey);

        const std::wstring srvArgs =
            L"--bind 127.0.0.1 --port 27120 --max-clients 4 --tick-rate 60 --require-token --issuer-pubkey " +
            std::wstring(issuerHex.begin(), issuerHex.end()) + L" --shutdown-after-disconnect";
        auto srvProc = LaunchProcessWithPipe(serverExe, srvArgs);
        EXPECT_TRUE(srvProc.Process != nullptr, "Launched MorbiServer.exe with --require-token");

        std::string srvOut;
        EXPECT_TRUE(WaitUntilPipeContains(srvProc, "Listening on 127.0.0.1:27120", 10000, srvOut),
            "Token-enforcing MorbiServer reached listening state");
        EXPECT_TRUE(srvOut.find("TokenAuth=Required") != std::string::npos, "MorbiServer reported TokenAuth=Required");

        // 1) Launch invalid (wrong issuer) client process -> must be rejected (exit != 0)
        const auto badTok = MakeEncodedToken(wrongIssuerKeys, 501ull, "Intruder", 60000);
        const std::string badHex = CryptoContext::HexEncode(badTok);
        auto badProc = LaunchProcessWithPipe(
            exePath,
            L"--role=token-client --port=27120 --token=" + std::wstring(badHex.begin(), badHex.end()));
        DWORD badExit = 999;
        std::string badOut;
        EXPECT_TRUE(WaitAndCollectProcess(badProc, 10000, badExit, badOut) && badExit != 0,
            "Invalid-token separate client process rejected by MorbiServer");

        // 2) Launch valid issuer-signed token client process -> must authenticate, echo, and cause server clean exit!
        const auto goodTok = MakeEncodedToken(issuerKeys, 502ull, "LegitPlayer", 60000);
        const std::string goodHex = CryptoContext::HexEncode(goodTok);
        auto goodProc = LaunchProcessWithPipe(
            exePath,
            L"--role=token-client --port=27120 --token=" + std::wstring(goodHex.begin(), goodHex.end()));
        DWORD goodExit = 999;
        std::string goodOut;
        EXPECT_TRUE(WaitAndCollectProcess(goodProc, 12000, goodExit, goodOut) && goodExit == 0,
            "Valid-token separate client process authenticated cleanly against MorbiServer (exit=" + std::to_string(goodExit) + ")");

        DWORD srvExit = 999;
        std::string srvTail;
        const bool srvOk = WaitAndCollectProcess(srvProc, 10000, srvExit, srvTail);
        srvOut += srvTail;
        EXPECT_TRUE(srvOk && srvExit == 0, "Token-enforcing MorbiServer shut down cleanly after disconnect");
        EXPECT_TRUE(srvOut.find("playerId=502") != std::string::npos, "MorbiServer authenticated exact token playerId=502");
        std::cout << "[PASS] Test_MorbiServerTokenAuthSeparateProcess\n";
#endif
    }

    // ------------------------------------------------------------------------
    // 8. Phase 4 Authoritative Replication, Input, RPC & 2-Client Multiplayer Tests
    // ------------------------------------------------------------------------

    void Test_NetworkIdAllocationAndReuse()
    {
        NetworkIdPool pool(4);
        const NetworkId id1 = pool.Allocate();
        const NetworkId id2 = pool.Allocate();
        EXPECT_TRUE(id1.IsValid() && id1.Index == 1 && id1.Generation == 1, "First NetworkId valid (index=1, gen=1)");
        EXPECT_TRUE(id2.IsValid() && id2.Index == 2 && id2.Generation == 1, "Second NetworkId valid (index=2, gen=1)");
        EXPECT_TRUE(pool.IsAlive(id1) && pool.IsAlive(id2), "Allocated IDs are alive");

        EXPECT_TRUE(pool.Release(id1), "Released id1");
        EXPECT_TRUE(!pool.IsAlive(id1), "Stale id1 rejected");
        EXPECT_TRUE(!pool.Release(id1), "Double release rejected");

        const NetworkId id1Reused = pool.Allocate();
        EXPECT_TRUE(id1Reused.Index == id1.Index && id1Reused.Generation == 2, "Reused slot bumped generation to 2");
        EXPECT_TRUE(!pool.IsAlive(id1) && pool.IsAlive(id1Reused), "Old generation remains invalid while new generation is alive");

        const NetworkId id3 = pool.Allocate();
        const NetworkId id4 = pool.Allocate();
        const NetworkId overflow = pool.Allocate();
        EXPECT_TRUE(id3.IsValid() && id4.IsValid() && !overflow.IsValid(), "Capacity limit enforced cleanly");

        pool.Clear();
        EXPECT_TRUE(pool.GetAliveCount() == 0 && !pool.IsAlive(id1Reused), "Clear invalidates all handles");
        std::cout << "[PASS] Test_NetworkIdAllocationAndReuse\n";
    }

    void Test_ComponentSerializationAndMalformedPayloads()
    {
        ReplicatedTransformData tf{};
        tf.PosX = 12.5f;
        tf.PosY = -3.0f;
        tf.PosZ = 44.25f;
        tf.RotY = 0.70710678f;
        tf.RotW = 0.70710678f;
        tf.ScaleX = 1.5f;

        BitWriter w(256);
        EXPECT_TRUE(tf.NetSerialize(w), "Valid transform serializes");

        BitReader r(w.GetData());
        ReplicatedTransformData parsed{};
        EXPECT_TRUE(parsed.NetDeserialize(r, ReplicationConstants::kTransformSchemaVersion), "Valid transform deserializes");
        EXPECT_TRUE(tf.ApproximatelyEquals(parsed), "Round-tripped transform matches");

        // Version mismatch & NaN & out-of-range checks
        BitReader rWrongVer(w.GetData());
        ReplicatedTransformData badVer{};
        EXPECT_TRUE(!badVer.NetDeserialize(rWrongVer, ReplicationConstants::kTransformSchemaVersion + 1), "Wrong schema version rejected");

        ReplicatedTransformData nanTf = tf;
        nanTf.PosX = std::numeric_limits<f32>::quiet_NaN();
        EXPECT_TRUE(!nanTf.IsValid(), "NaN position rejected");

        ReplicatedTransformData badScale = tf;
        badScale.ScaleY = -1.0f;
        EXPECT_TRUE(!badScale.IsValid(), "Negative scale rejected");

        ReplicatedTransformData zeroQuat = tf;
        zeroQuat.RotX = zeroQuat.RotY = zeroQuat.RotZ = zeroQuat.RotW = 0.0f;
        EXPECT_TRUE(!zeroQuat.IsValid(), "Zero quaternion rejected");

        // SnapshotPacket malformed & bounds checks
        SnapshotPacket badPkt{};
        badPkt.IsKeyframe = false;
        badPkt.SnapshotSequence = 5;
        badPkt.BaselineSequence = 0; // invalid: delta cannot have baseline 0
        BitWriter bw(256);
        EXPECT_TRUE(!ReplicationCodec::WriteSnapshotPacket(bw, badPkt), "Delta with baseline 0 rejected");

        // Truncated & huge entry count wire packet check
        BitWriter rawBad(64);
        (void)rawBad.WriteU16(ReplicationConstants::kSnapshotWireVersion);
        (void)rawBad.WriteU32(1); // seq
        (void)rawBad.WriteU32(0); // baseline
        (void)rawBad.WriteU8(1);  // keyframe
        (void)rawBad.WriteU32(1); // world
        (void)rawBad.WriteU64(10); // tick
        (void)rawBad.WriteU16(9999); // exceeds kMaxSnapshotEntriesPerPacket
        BitReader badSnapReader(rawBad.GetData());
        SnapshotPacket parsedBad{};
        EXPECT_TRUE(!ReplicationCodec::ReadSnapshotPacket(badSnapReader, parsedBad), "Excessive snapshot entry count rejected");

        std::cout << "[PASS] Test_ComponentSerializationAndMalformedPayloads\n";
    }

    void Test_SnapshotDeltaAckBaselineAndRecovery()
    {
        ReplicationManager srv;
        ClientReplicationApplier cli;
        const ConnectionHandle h{ 10 };
        const SessionId sess{ 0xAA01ull, 0xBB01ull };
        srv.RegisterConnection(h, sess, 1);

        ReplicatedEntitySpawnDesc desc{};
        desc.OwnerSession = sess;
        desc.OwnerPlayer = 500;
        desc.WorldId = 1;
        desc.EntityName = "Hero";
        desc.InitialTransform.PosX = 1.0f;
        const NetworkId netId = srv.SpawnEntity(desc, 1);
        EXPECT_TRUE(netId.IsValid(), "Spawned entity in ReplicationManager");

        // 1. Keyframe 1 emitted; before ACK, baseline must NOT advance
        auto kfOpt = srv.BuildSnapshotForConnection(h, 1);
        EXPECT_TRUE(kfOpt.has_value() && kfOpt->IsKeyframe && kfOpt->BaselineSequence == 0, "Initial Full Keyframe emitted");
        EXPECT_TRUE(!srv.IsBaselineConfirmed(h) && srv.GetConfirmedBaselineSequence(h) == 0, "Baseline remains unconfirmed before ACK");

        // Serialize + Deserialize + Apply on client
        std::vector<u8> wire;
        EXPECT_TRUE(srv.SerializeSnapshot(*kfOpt, wire), "Serialized keyframe");
        BitReader kfReader(wire);
        SnapshotPacket kfDecoded{};
        EXPECT_TRUE(ReplicationCodec::ReadSnapshotPacket(kfReader, kfDecoded), "Decoded keyframe");

        std::vector<ClientReplicationEvent> evs;
        std::optional<SnapshotAckPacket> ackOpt;
        std::optional<ResyncRequestPacket> resyncOpt;
        EXPECT_TRUE(cli.ApplySnapshotPacket(kfDecoded, evs, ackOpt, resyncOpt) && ackOpt.has_value() && !resyncOpt.has_value(),
            "Client applied keyframe & emitted ACK");
        EXPECT_TRUE(cli.GetEntityCount() == 1u && evs.size() == 1u && evs[0].Kind == ReplicationOpKind::Spawn,
            "Client created replicated entity");

        // Confirm ACK on server
        EXPECT_TRUE(srv.HandleSnapshotAck(h, *ackOpt) && srv.GetConfirmedBaselineSequence(h) == kfDecoded.SnapshotSequence,
            "Server advanced confirmed baseline on explicit ACK");

        // 2. Unchanged entity -> no delta emitted!
        EXPECT_TRUE(!srv.BuildSnapshotForConnection(h, 2).has_value(), "Unchanged entity skips redundant delta");

        // 3. Transform-only mutation -> delta sends Transform bit ONLY (no Movement bit)
        ReplicatedTransformData movedTf = desc.InitialTransform;
        movedTf.PosX = 9.25f;
        EXPECT_TRUE(srv.UpdateEntityTransform(netId, movedTf), "Updated transform");
        auto d1Opt = srv.BuildSnapshotForConnection(h, 3);
        EXPECT_TRUE(d1Opt.has_value() && !d1Opt->IsKeyframe && d1Opt->BaselineSequence == 1 && d1Opt->Entries.size() == 1u,
            "Delta generated strictly against confirmed baseline 1");
        EXPECT_TRUE(d1Opt->Entries[0].Op == ReplicationOpKind::Update &&
                    d1Opt->Entries[0].ComponentMask == SnapshotComponentFlags::kTransform,
            "Delta serializes only changed Transform component");

        // 4. Unconfirmed baseline test: another move BEFORE ACK still bases Delta on baseline 1, NOT unconfirmed 2!
        movedTf.PosX = 14.0f;
        EXPECT_TRUE(srv.UpdateEntityTransform(netId, movedTf), "Updated transform again before ACK");
        auto d2Opt = srv.BuildSnapshotForConnection(h, 4);
        EXPECT_TRUE(d2Opt.has_value() && !d2Opt->IsKeyframe && d2Opt->BaselineSequence == 1,
            "Unacknowledged snapshot #2 NEVER used as baseline for #3");

        // 5. Client baseline mismatch -> rejects delta and requests full keyframe resync!
        SnapshotPacket corruptedBase = *d2Opt;
        corruptedBase.BaselineSequence = 999; // invalid baseline vs client's seq 1
        EXPECT_TRUE(!cli.ApplySnapshotPacket(corruptedBase, evs, ackOpt, resyncOpt) && resyncOpt.has_value() && !ackOpt.has_value(),
            "Client rejected out-of-sync delta & produced ResyncRequest");

        ClientReplicatedEntity cliEnt{};
        EXPECT_TRUE(cli.TryGetEntity(netId, cliEnt) && std::abs(cliEnt.Transform.PosX - 1.0f) < 1e-4f,
            "Client state uncorrupted by rejected delta");

        EXPECT_TRUE(srv.HandleResyncRequest(h, *resyncOpt) && !srv.IsBaselineConfirmed(h),
            "Server invalidated baseline on ResyncRequest");
        auto resyncKf = srv.BuildSnapshotForConnection(h, 5, true);
        EXPECT_TRUE(resyncKf.has_value() && resyncKf->IsKeyframe && resyncKf->BaselineSequence == 0,
            "Server generated Full Keyframe recovery");
        EXPECT_TRUE(cli.ApplySnapshotPacket(*resyncKf, evs, ackOpt, resyncOpt) && ackOpt.has_value(),
            "Client recovered via Full Keyframe");
        EXPECT_TRUE(cli.TryGetEntity(netId, cliEnt) && std::abs(cliEnt.Transform.PosX - 14.0f) < 1e-4f,
            "Client synchronized to latest authoritative state");
        EXPECT_TRUE(srv.GetStats().BaselineRecoveries >= 1u, "BaselineRecoveries metric incremented");

        std::cout << "[PASS] Test_SnapshotDeltaAckBaselineAndRecovery\n";
    }

    void Test_InterestManagementAndWorldIsolation()
    {
        ReplicationManager srv;
        srv.SetInterestPolicy(std::make_shared<DistanceInterestPolicy>(10.0f, 15.0f));

        const ConnectionHandle hViewer{ 1 };
        const SessionId viewerSess{ 101, 1 };
        const SessionId otherSess{ 202, 2 };
        const SessionId world2Sess{ 303, 3 };

        srv.RegisterConnection(hViewer, viewerSess, 1);
        srv.SetConnectionViewPosition(hViewer, 0.0f, 0.0f, 0.0f);

        // Far entity at X=12 (> enter 10) -> initially NOT visible
        ReplicatedEntitySpawnDesc farDesc{};
        farDesc.OwnerSession = otherSess;
        farDesc.WorldId = 1;
        farDesc.EntityName = "FarNPC";
        farDesc.InitialTransform.PosX = 12.0f;
        const NetworkId farId = srv.SpawnEntity(farDesc, 1);

        // Owner entity at X=500 -> ALWAYS visible to owner!
        ReplicatedEntitySpawnDesc ownDesc{};
        ownDesc.OwnerSession = viewerSess;
        ownDesc.WorldId = 1;
        ownDesc.EntityName = "OwnHero";
        ownDesc.InitialTransform.PosX = 500.0f;
        const NetworkId ownId = srv.SpawnEntity(ownDesc, 1);

        // World 2 entity at origin -> NEVER visible to World 1 viewer!
        ReplicatedEntitySpawnDesc w2Desc{};
        w2Desc.OwnerSession = world2Sess;
        w2Desc.WorldId = 2;
        w2Desc.EntityName = "DungeonBossWorld2";
        const NetworkId w2Id = srv.SpawnEntity(w2Desc, 1);
        (void)w2Id;

        auto k1 = srv.BuildSnapshotForConnection(hViewer, 1);
        EXPECT_TRUE(k1.has_value() && k1->Entries.size() == 1u && k1->Entries[0].NetId == ownId,
            "Viewer sees only own entity (far X=12 & World2 excluded)");
        (void)srv.HandleSnapshotAck(hViewer, SnapshotAckPacket{ k1->SnapshotSequence, 0, true });

        // Move FarNPC inside enter radius (X=8) -> Spawn emitted!
        farDesc.InitialTransform.PosX = 8.0f;
        (void)srv.UpdateEntityTransform(farId, farDesc.InitialTransform);
        auto dEnter = srv.BuildSnapshotForConnection(hViewer, 2);
        EXPECT_TRUE(dEnter.has_value() && dEnter->Entries.size() == 1u &&
                    dEnter->Entries[0].Op == ReplicationOpKind::Spawn &&
                    dEnter->Entries[0].NetId == farId,
            "Entity entering distance radius triggers Spawn");
        (void)srv.HandleSnapshotAck(hViewer, SnapshotAckPacket{ dEnter->SnapshotSequence, dEnter->BaselineSequence, true });

        // Move FarNPC to X=13 (between enter=10 and leave=15) -> hysteresis keeps visible (Update, NOT Despawn)
        farDesc.InitialTransform.PosX = 13.0f;
        (void)srv.UpdateEntityTransform(farId, farDesc.InitialTransform);
        auto dHyst = srv.BuildSnapshotForConnection(hViewer, 3);
        EXPECT_TRUE(dHyst.has_value() && dHyst->Entries.size() == 1u &&
                    dHyst->Entries[0].Op == ReplicationOpKind::Update,
            "Hysteresis retains visibility inside leave radius");
        (void)srv.HandleSnapshotAck(hViewer, SnapshotAckPacket{ dHyst->SnapshotSequence, dHyst->BaselineSequence, true });

        // Move FarNPC outside leave radius (X=18) -> Despawn emitted!
        farDesc.InitialTransform.PosX = 18.0f;
        (void)srv.UpdateEntityTransform(farId, farDesc.InitialTransform);
        auto dLeave = srv.BuildSnapshotForConnection(hViewer, 4);
        EXPECT_TRUE(dLeave.has_value() && dLeave->Entries.size() == 1u &&
                    dLeave->Entries[0].Op == ReplicationOpKind::Despawn &&
                    dLeave->Entries[0].NetId == farId,
            "Entity leaving distance radius triggers Despawn");

        std::cout << "[PASS] Test_InterestManagementAndWorldIsolation\n";
    }

    void Test_ServerInputValidationAndOwnership()
    {
        SessionRegistry sessions;
        ServerInputPipeline inputPipe(InputValidationConfig{
            .MaxBufferedPerEntity = 4,
            .MaxFutureSequenceLead = 10,
            .MaxCommandsPerSecond = 20
        });

        const ConnectionHandle hA{ 1 }, hB{ 2 }, hW2{ 3 };
        const SessionId sA{ 1, 1 }, sB{ 2, 2 }, sW2{ 3, 3 };
        (void)sessions.CreateSession(sA, hA, 100, 1);
        (void)sessions.CreateSession(sB, hB, 100, 1);
        (void)sessions.CreateSession(sW2, hW2, 100, 2);

        const NetworkId eA{ 10, 1 };
        const NetworkId eW2{ 20, 1 };
        (void)sessions.BindEntity(NetworkEntityBinding{ eA, sA, 100, 1, 0, 1, NetworkAuthority::ClientPredicted });
        (void)sessions.BindEntity(NetworkEntityBinding{ eW2, sW2, 200, 2, 1, 1, NetworkAuthority::ClientPredicted });

        // Valid commands from owner
        PlayerInputCommand validCmd{ eA, 1, 10, 0.5f, -0.5f, 1.2f, InputActionFlags::kJump };
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1000) == InputSubmitStatus::Accepted, "Valid owner input accepted");
        validCmd.Sequence = 2;
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1005) == InputSubmitStatus::Accepted, "Sequential input accepted");

        // Stale & duplicate
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1010) == InputSubmitStatus::StaleOrDuplicate, "Duplicate input sequence rejected");
        validCmd.Sequence = 1;
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1012) == InputSubmitStatus::StaleOrDuplicate, "Stale input sequence rejected");

        // Excessively future sequence
        validCmd.Sequence = 500;
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1015) == InputSubmitStatus::TooFarInFuture, "Future-dated sequence rejected");

        // Invalid float / out-of-range values
        PlayerInputCommand nanCmd{ eA, 3, 12, std::numeric_limits<f32>::infinity(), 0.0f, 0.0f, 0 };
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, nanCmd, 1020) == InputSubmitStatus::Malformed, "Infinite axis rejected");
        PlayerInputCommand speedHackCmd{ eA, 3, 12, 99.0f, 0.0f, 0.0f, 0 };
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, speedHackCmd, 1021) == InputSubmitStatus::Malformed, "Out-of-range MoveX rejected");

        // Ownership violation (B tries to move A's entity)
        validCmd.Sequence = 3;
        EXPECT_TRUE(inputPipe.SubmitCommand(hB, sessions, 1, validCmd, 1025) == InputSubmitStatus::Unauthorized, "Non-owner input rejected");

        // World mismatch violation (W2 in World 2 submits to World 1 pipeline)
        PlayerInputCommand w2Cmd{ eW2, 1, 1, 1.0f, 0.0f, 0.0f, 0 };
        EXPECT_TRUE(inputPipe.SubmitCommand(hW2, sessions, 1, w2Cmd, 1030) == InputSubmitStatus::Unauthorized, "Cross-world input rejected");

        // Buffer limit enforcement (already 2 queued, max=4 -> 3,4 accept, 5 BufferFull)
        validCmd.Sequence = 3;
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1031) == InputSubmitStatus::Accepted, "Queue slot 3 accepted");
        validCmd.Sequence = 4;
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1032) == InputSubmitStatus::Accepted, "Queue slot 4 accepted");
        validCmd.Sequence = 5;
        EXPECT_TRUE(inputPipe.SubmitCommand(hA, sessions, 1, validCmd, 1033) == InputSubmitStatus::BufferFull, "Buffer limit enforced");

        std::vector<PlayerInputCommand> drained;
        EXPECT_TRUE(inputPipe.DrainCommands(eA, drained, 8) == 4u, "Drained 4 validated commands");
        std::cout << "[PASS] Test_ServerInputValidationAndOwnership\n";
    }

    void Test_RpcAuthorizationAndWorldMembership()
    {
        SessionRegistry sessions;
        RpcRouter router(true);

        const ConnectionHandle hOwner{ 1 }, hOther{ 2 }, hWrongWorld{ 3 };
        const SessionId sOwner{ 11, 1 }, sOther{ 22, 2 }, sWrongWorld{ 33, 3 };
        (void)sessions.CreateSession(sOwner, hOwner, 100, 1);
        (void)sessions.CreateSession(sOther, hOther, 100, 1);
        (void)sessions.CreateSession(sWrongWorld, hWrongWorld, 100, 2);

        const NetworkId ownerEnt{ 1, 1 };
        (void)sessions.BindEntity(NetworkEntityBinding{ ownerEnt, sOwner, 1, 1, 0, 1, NetworkAuthority::ClientPredicted });

        u32 ownerCalls = 0;
        u32 serverOnlyCalls = 0;
        EXPECT_TRUE(router.RegisterRpc(RpcDescriptor{
            .RpcId = 10, .Name = "OwnerCast", .Direction = RpcDirection::ClientToServer,
            .Authority = RpcAuthorityRule::OwnerOnly, .MaxPayloadBytes = 16, .RateLimitPerSec = 2
        }, [&](const RpcInvocationContext&) { ++ownerCalls; }), "Registered OwnerOnly RPC");

        EXPECT_TRUE(router.RegisterRpc(RpcDescriptor{
            .RpcId = 20, .Name = "AdminBan", .Direction = RpcDirection::ClientToServer,
            .Authority = RpcAuthorityRule::ServerOnly, .MaxPayloadBytes = 16, .RateLimitPerSec = 10
        }, [&](const RpcInvocationContext&) { ++serverOnlyCalls; }), "Registered ServerOnly RPC");

        EXPECT_TRUE(router.RegisterRpc(RpcDescriptor{
            .RpcId = 30, .Name = "ClientBanner", .Direction = RpcDirection::ServerToClient,
            .Authority = RpcAuthorityRule::ServerOnly, .MaxPayloadBytes = 32, .RateLimitPerSec = 10
        }, [&](const RpcInvocationContext&) {}), "Registered ServerToClient RPC");

        auto makeEnvBytes = [](u32 rpcId, NetworkId target, WorldInstanceId worldId, std::vector<u8> payload) {
            BitWriter w(256);
            RpcInvocationEnvelope env{ rpcId, target, worldId, std::move(payload) };
            (void)RpcRouter::WriteInvocation(w, env);
            return std::vector<u8>(w.GetData().begin(), w.GetData().end());
        };

        // 1. Authorized owner invocation
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(10, ownerEnt, 1, { 1, 2 }), 1000) == RpcValidationResult::Authorized,
            "Owner RPC authorized");
        // 2. Non-owner denied
        EXPECT_TRUE(router.ProcessIncomingRpc(hOther, sessions, 1, makeEnvBytes(10, ownerEnt, 1, { 1, 2 }), 1000) == RpcValidationResult::AuthorityDenied,
            "Non-owner RPC denied");
        // 3. ServerOnly denied from client
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(20, ownerEnt, 1, { 1 }), 1000) == RpcValidationResult::AuthorityDenied,
            "ServerOnly RPC denied from client");
        EXPECT_TRUE(serverOnlyCalls == 0, "ServerOnly handler never executed");
        // 4. Direction mismatch denied
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(30, ownerEnt, 1, { 1 }), 1000) == RpcValidationResult::InvalidDirection,
            "ServerToClient RPC rejected on server");
        // 5. Unknown RPC denied
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(999, ownerEnt, 1, {}), 1000) == RpcValidationResult::UnknownRpc,
            "Unknown RPC ID rejected");
        // 6. Oversized payload denied
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(10, ownerEnt, 1, std::vector<u8>(32, 0)), 1000) == RpcValidationResult::PayloadTooLarge,
            "Oversized RPC payload rejected");
        // 7. World mismatch denied
        EXPECT_TRUE(router.ProcessIncomingRpc(hWrongWorld, sessions, 1, makeEnvBytes(10, ownerEnt, 2, { 1 }), 1000) == RpcValidationResult::WorldMismatch,
            "Cross-world RPC denied");
        // 8. Unauthenticated connection denied
        EXPECT_TRUE(router.ProcessIncomingRpc(ConnectionHandle{ 999 }, sessions, 1, makeEnvBytes(10, ownerEnt, 1, { 1 }), 1000) == RpcValidationResult::UnauthenticatedSession,
            "Unauthenticated connection RPC denied");
        // 9. Per-RPC rate limit (limit=2/s, call #2 succeeds, #3 RateLimited)
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(10, ownerEnt, 1, { 3 }), 1005) == RpcValidationResult::Authorized,
            "Second RPC inside limit authorized");
        EXPECT_TRUE(router.ProcessIncomingRpc(hOwner, sessions, 1, makeEnvBytes(10, ownerEnt, 1, { 4 }), 1010) == RpcValidationResult::RateLimited,
            "Third RPC exceeding RateLimitPerSec rejected");
        EXPECT_TRUE(ownerCalls == 2u, "Authorized handler count matches exact 2");

        std::cout << "[PASS] Test_RpcAuthorizationAndWorldMembership\n";
    }

    int RunMultiplayerClientProcess(u16 port, const std::string& playerRole, const std::string& sessionHex, const std::string& ticketHex)
    {
        ConnectionManager client;
        ConnectionConfig cfg{};
        cfg.Mode = NetworkMode::Client;
        cfg.Transport.MaxConnections = 1;
        cfg.SchemaHash = 1;
        if (!client.Initialize(cfg))
        {
            return 50;
        }

        Scene::Scene localScene("MpClientScene");
        SceneReplicationBridge bridge;
        bridge.Initialize(false, ReplicationLaunchConfig{}, client);

        ConnectAuthOptions authOpts{};
        if (playerRole == "playerA-reconnect")
        {
            if (sessionHex.size() != 32 || ticketHex.empty() || (ticketHex.size() % 2) != 0)
            {
                return 51;
            }
            std::array<u8, 16> rawSess{};
            if (!CryptoContext::HexDecode(sessionHex, rawSess))
            {
                return 52;
            }
            auto readU64LE = [](const u8* p) -> u64 {
                u64 v = 0;
                for (int i = 0; i < 8; ++i) v |= (static_cast<u64>(p[i]) << (i * 8));
                return v;
            };
            // Parse big-endian hex from ToHexString ("%016llx%016llx") cleanly
            authOpts.ResumeSessionId.High = std::stoull(sessionHex.substr(0, 16), nullptr, 16);
            authOpts.ResumeSessionId.Low = std::stoull(sessionHex.substr(16, 16), nullptr, 16);
            (void)readU64LE;
            authOpts.IsReconnect = true;
            authOpts.ReconnectTicket.resize(ticketHex.size() / 2);
            if (!CryptoContext::HexDecode(ticketHex, authOpts.ReconnectTicket))
            {
                return 53;
            }
            authOpts.PlayerDisplayName = "PlayerA";
        }
        else
        {
            authOpts.PlayerDisplayName = (playerRole == "playerA") ? "PlayerA" : "PlayerB";
        }

        ConnectionHandle srvConn = kInvalidConnectionHandle;
        SessionId assignedSession{};
        PlayerId assignedPlayer = kInvalidPlayerId;
        bool connected = false;
        bool disconnected = false;

        client.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            srvConn = info.Handle;
            assignedSession = info.Session;
            assignedPlayer = info.AuthenticatedPlayerId;
            connected = true;

            if (playerRole == "playerA")
            {
                std::vector<u8> ticket;
                if (client.TryGetLastReconnectTicket(info.Handle, ticket))
                {
                    std::cout << "MP_SESSION=" << info.Session.ToHexString() << "\n";
                    std::cout << "MP_TICKET=" << CryptoContext::HexEncode(ticket) << "\n";
                    std::cout.flush();
                }
            }
        });

        client.SetOnDisconnected([&](const ConnectionInfo&, const std::string&) {
            disconnected = true;
        });

        client.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            bridge.HandleNetworkMessage(ev, localScene, client);
        });

        if (!client.Connect(Endpoint{ "127.0.0.1", port }, authOpts))
        {
            return 54;
        }

        const auto pumpFor = [&](u32 ms, auto&& stopPred) -> bool {
            const auto start = std::chrono::steady_clock::now();
            while (!disconnected)
            {
                client.Poll();
                if (stopPred())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count() >= static_cast<long long>(ms))
                {
                    return false;
                }
            }
            return stopPred();
        };

        if (!pumpFor(5000, [&]() { return connected; }))
        {
            client.Shutdown();
            return 55;
        }

        auto findByPrefix = [&](std::string_view name, ClientReplicatedEntity& outEnt) -> bool {
            for (const auto& [packed, ent] : bridge.GetClientApplier().GetEntities())
            {
                (void)packed;
                if (ent.EntityName == name)
                {
                    outEnt = ent;
                    return true;
                }
            }
            return false;
        };

        if (playerRole == "playerA")
        {
            // Wait until both PlayerA and PlayerB appear in local Scene
            ClientReplicatedEntity entA{}, entB{};
            if (!pumpFor(6000, [&]() {
                    return findByPrefix("PlayerA", entA) && findByPrefix("PlayerB", entB) &&
                           localScene.GetAliveEntityCount() >= 2u;
                }))
            {
                client.Shutdown();
                return 56;
            }
            const f32 initialAx = entA.Transform.PosX;

            // Attempt forbidden ServerOnlyAdminReset RPC (server must reject cleanly without disconnecting/acting)
            BitWriter badRpcW(128);
            (void)RpcRouter::WriteInvocation(badRpcW, RpcInvocationEnvelope{
                BuiltinRpcIds::kServerOnlyAdminReset, entA.NetId, 1, { 1 }
            });
            (void)client.SendPacket(srvConn, MessageCategory::Rpc, static_cast<u8>(RpcMessageSubType::RpcInvoke), badRpcW.GetData());

            // Send valid OwnerOnly PlayerEmoteAction RPC
            BitWriter goodRpcW(128);
            (void)RpcRouter::WriteInvocation(goodRpcW, RpcInvocationEnvelope{
                BuiltinRpcIds::kPlayerEmoteAction, entA.NetId, 1, { 7 }
            });
            (void)client.SendPacket(srvConn, MessageCategory::Rpc, static_cast<u8>(RpcMessageSubType::RpcInvoke), goodRpcW.GetData());

            // Drive PlayerA along +X via 25 authoritative input commands
            for (u32 seq = 1; seq <= 25; ++seq)
            {
                (void)bridge.ClientSendInput(client, srvConn, PlayerInputCommand{
                    .TargetEntity = entA.NetId,
                    .Sequence = seq,
                    .ClientTick = seq,
                    .MoveX = 1.0f,
                    .MoveZ = 0.0f,
                    .YawRadians = 1.57f,
                    .ActionFlags = InputActionFlags::kSprint
                });
                client.Poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }

            // Wait until both PlayerA moved in X AND PlayerB moved in Z!
            if (!pumpFor(6000, [&]() {
                    ClientReplicatedEntity curA{}, curB{};
                    if (!findByPrefix("PlayerA", curA) || !findByPrefix("PlayerB", curB))
                    {
                        return false;
                    }
                    const Scene::EntityId sidB = bridge.ResolveSceneEntity(curB.NetId);
                    const auto* tfB = localScene.Transforms().TryGet(sidB);
                    return (curA.Transform.PosX > initialAx + 1.0f) && (tfB != nullptr && tfB->Position.z > 1.0f);
                }))
            {
                client.Shutdown();
                return 57;
            }

            std::cout << "MP_PLAYERA_MOVED=OK\n";
            std::cout.flush();

            // Suspend cleanly with allowReconnect=true so next process tests reconnect + full snapshot recovery
            client.Disconnect(srvConn, "SuspendForReconnect", true);
            (void)pumpFor(3000, [&]() { return disconnected; });
            client.Shutdown();
            return 0;
        }

        if (playerRole == "playerA-reconnect")
        {
            // Expect full keyframe with BOTH PlayerA (moved X) and PlayerB (moved Z > 1.0) in localScene
            ClientReplicatedEntity entA{}, entB{};
            if (!pumpFor(6000, [&]() {
                    if (!findByPrefix("PlayerA", entA) || !findByPrefix("PlayerB", entB))
                    {
                        return false;
                    }
                    return entB.Transform.PosZ > 1.0f && localScene.GetAliveEntityCount() == 2u;
                }))
            {
                client.Shutdown();
                return 58;
            }

            std::cout << "MP_RECONNECT_RECOVERED=OK\n";
            std::cout.flush();

            // Now leave permanently (allowReconnect=false) so server closes Session A and despawns PlayerA!
            client.Disconnect(srvConn, "LeaveWorldPermanent", false);
            (void)pumpFor(3000, [&]() { return disconnected; });
            client.Shutdown();
            return 0;
        }

        // playerRole == "playerB"
        {
            // Wait until both entities are replicated & PlayerA has moved significantly (+X)
            ClientReplicatedEntity entA{}, entB{};
            if (!pumpFor(8000, [&]() {
                    return findByPrefix("PlayerA", entA) && findByPrefix("PlayerB", entB) &&
                           localScene.GetAliveEntityCount() >= 2u;
                }))
            {
                client.Shutdown();
                return 59;
            }
            const f32 initialAx = entA.Transform.PosX;
            const f32 initialBz = entB.Transform.PosZ;

            if (!pumpFor(8000, [&]() {
                    ClientReplicatedEntity curA{};
                    if (!findByPrefix("PlayerA", curA)) return false;
                    const Scene::EntityId sidA = bridge.ResolveSceneEntity(curA.NetId);
                    const auto* tfA = localScene.Transforms().TryGet(sidA);
                    return tfA != nullptr && tfA->Position.x > initialAx + 1.0f;
                }))
            {
                client.Shutdown();
                return 60;
            }
            std::cout << "MP_PLAYERB_SAW_PLAYERA=OK\n";
            std::cout.flush();

            // Drive PlayerB along +Z via 25 authoritative input commands
            for (u32 seq = 1; seq <= 25; ++seq)
            {
                (void)bridge.ClientSendInput(client, srvConn, PlayerInputCommand{
                    .TargetEntity = entB.NetId,
                    .Sequence = seq,
                    .ClientTick = seq,
                    .MoveX = 0.0f,
                    .MoveZ = 1.0f,
                    .YawRadians = 0.0f,
                    .ActionFlags = InputActionFlags::kNone
                });
                client.Poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }

            if (!pumpFor(6000, [&]() {
                    ClientReplicatedEntity curB{};
                    return findByPrefix("PlayerB", curB) && (curB.Transform.PosZ > initialBz + 1.0f);
                }))
            {
                client.Shutdown();
                return 61;
            }

            // Wait until PlayerA permanently leaves and despawns from PlayerB's replicated Scene!
            if (!pumpFor(10000, [&]() {
                    ClientReplicatedEntity tmpA{}, tmpB{};
                    return !findByPrefix("PlayerA", tmpA) && findByPrefix("PlayerB", tmpB) &&
                           localScene.GetAliveEntityCount() == 1u;
                }))
            {
                client.Shutdown();
                return 62;
            }
            std::cout << "MP_PLAYERB_SAW_DESPAWN=OK\n";
            std::cout.flush();

            client.Disconnect(srvConn, "PlayerBDone", false);
            (void)pumpFor(3000, [&]() { return disconnected; });
            client.Shutdown();
            return 0;
        }
    }

    std::string ExtractStdoutValue(const std::string& text, std::string_view key)
    {
        const auto pos = text.find(key);
        if (pos == std::string::npos)
        {
            return {};
        }
        const auto valStart = pos + key.size();
        const auto endPos = text.find_first_of("\r\n", valStart);
        return text.substr(valStart, endPos == std::string::npos ? std::string::npos : (endPos - valStart));
    }

    void Test_TwoClientMultiplayerAcceptanceSeparateProcesses()
    {
#if defined(_WIN32)
        const auto exePath = GetCurrentExePath();
        const auto serverExe = exePath.parent_path() / "MorbiServer.exe";
        const auto sampleScene = FindRepoSampleScene();

        std::wstring srvArgs = L"--bind 127.0.0.1 --port 27130 --max-clients 8 --tick-rate 60 --rep-rate 30 --reconnect-window-ms 5000 --shutdown-after-disconnect";
        if (!sampleScene.empty())
        {
            srvArgs += L" --scene \"" + sampleScene.wstring() + L"\"";
        }

        auto srvProc = LaunchProcessWithPipe(serverExe, srvArgs);
        EXPECT_TRUE(srvProc.Process != nullptr, "Launched MorbiServer.exe for 2-client multiplayer acceptance");

        std::string srvOut;
        EXPECT_TRUE(WaitUntilPipeContains(srvProc, "Listening on 127.0.0.1:27130", 10000, srvOut),
            "Multiplayer MorbiServer reached listening state");

        auto cliAProc = LaunchProcessWithPipe(exePath, L"--role=mp-client --port=27130 --mp-mode=playerA");
        auto cliBProc = LaunchProcessWithPipe(exePath, L"--role=mp-client --port=27130 --mp-mode=playerB");
        EXPECT_TRUE(cliAProc.Process != nullptr && cliBProc.Process != nullptr, "Launched independent PlayerA and PlayerB client processes");

        DWORD cliAExit = 999;
        std::string cliAOut;
        EXPECT_TRUE(WaitAndCollectProcess(cliAProc, 15000, cliAExit, cliAOut) && cliAExit == 0,
            "PlayerA initial phase exited 0 (exit=" + std::to_string(cliAExit) + ")");
        EXPECT_TRUE(cliAOut.find("MP_PLAYERA_MOVED=OK") != std::string::npos, "PlayerA moved & saw PlayerB movement");

        const std::string savedSession = ExtractStdoutValue(cliAOut, "MP_SESSION=");
        const std::string savedTicket = ExtractStdoutValue(cliAOut, "MP_TICKET=");
        EXPECT_TRUE(savedSession.size() == 32 && !savedTicket.empty(), "Captured PlayerA session & sealed reconnect ticket");

        // Launch separate reconnect process using saved SessionId + ReconnectTicket
        const std::wstring recArgs = L"--role=mp-client --port=27130 --mp-mode=playerA-reconnect --session=" +
            std::wstring(savedSession.begin(), savedSession.end()) + L" --ticket=" +
            std::wstring(savedTicket.begin(), savedTicket.end());
        auto recProc = LaunchProcessWithPipe(exePath, recArgs);
        EXPECT_TRUE(recProc.Process != nullptr, "Launched PlayerA reconnect OS process");

        DWORD recExit = 999;
        std::string recOut;
        EXPECT_TRUE(WaitAndCollectProcess(recProc, 12000, recExit, recOut) && recExit == 0,
            "PlayerA reconnect process exited 0 (exit=" + std::to_string(recExit) + ")");
        EXPECT_TRUE(recOut.find("MP_RECONNECT_RECOVERED=OK") != std::string::npos,
            "PlayerA recovered full snapshot after reconnect");

        DWORD cliBExit = 999;
        std::string cliBOut;
        EXPECT_TRUE(WaitAndCollectProcess(cliBProc, 15000, cliBExit, cliBOut) && cliBExit == 0,
            "PlayerB process exited 0 (exit=" + std::to_string(cliBExit) + ")");
        EXPECT_TRUE(cliBOut.find("MP_PLAYERB_SAW_PLAYERA=OK") != std::string::npos,
            "PlayerB observed PlayerA's authoritative movement");
        EXPECT_TRUE(cliBOut.find("MP_PLAYERB_SAW_DESPAWN=OK") != std::string::npos,
            "PlayerB observed PlayerA's despawn after session closed");

        DWORD srvExit = 999;
        std::string srvTail;
        const bool srvOk = WaitAndCollectProcess(srvProc, 10000, srvExit, srvTail);
        srvOut += srvTail;
        EXPECT_TRUE(srvOk && srvExit == 0, "Multiplayer MorbiServer shut down cleanly (exit=" + std::to_string(srvExit) + ")");
        EXPECT_TRUE(srvOut.find("Despawning entity") != std::string::npos, "Server logged authoritative entity despawn");
        std::cout << "[PASS] Test_TwoClientMultiplayerAcceptanceSeparateProcesses\n";
#endif
    }

    void Test_AckBaselineEpochHardeningAndOutOfOrder()
    {
        ReplicationManager srv;
        ClientReplicationApplier cli;
        const ConnectionHandle h{ 42 };
        const SessionId sess{ 0xAA10ull, 0xBB20ull };
        srv.RegisterConnection(h, sess, 1);

        ReplicatedEntitySpawnDesc desc{};
        desc.OwnerSession = sess;
        desc.OwnerPlayer = 900;
        desc.WorldId = 1;
        desc.EntityName = "HardenedEntity";
        desc.InitialTransform.PosX = 1.0f;
        const NetworkId id = srv.SpawnEntity(desc, 1);
        EXPECT_TRUE(id.IsValid(), "Spawned entity for ACK hardening test");

        // Initial keyframe seq 1, epoch 1
        auto kf1 = srv.BuildSnapshotForConnection(h, 1);
        EXPECT_TRUE(kf1.has_value() && kf1->IsKeyframe && kf1->SnapshotSequence == 1 && kf1->BaselineEpoch == 1,
            "Initial keyframe seq=1 epoch=1 emitted");

        std::vector<ClientReplicationEvent> evs;
        std::optional<SnapshotAckPacket> ackOpt;
        std::optional<ResyncRequestPacket> resyncOpt;
        EXPECT_TRUE(cli.ApplySnapshotPacket(*kf1, evs, ackOpt, resyncOpt) && ackOpt.has_value(),
            "Client applied keyframe seq=1");
        EXPECT_TRUE(cli.GetLastReceivedSequence() == 1 &&
                    cli.GetLastAppliedSequence() == 1 &&
                    cli.GetLastAcknowledgedSequence() == 1 &&
                    cli.GetActiveBaselineEpoch() == 1,
            "Client sequence & epoch telemetry matches initial keyframe");
        EXPECT_TRUE(srv.HandleSnapshotAck(h, *ackOpt) && srv.GetConfirmedBaselineSequence(h) == 1,
            "Server confirmed baseline seq=1");

        // Duplicate ACK for seq 1 must be ignored idempotently without invalidating baseline
        EXPECT_TRUE(!srv.HandleSnapshotAck(h, *ackOpt), "Duplicate ACK rejected as no-op");
        EXPECT_TRUE(srv.IsBaselineConfirmed(h) && srv.GetStats().DuplicateAcksIgnored == 1 &&
                    srv.GetStats().BaselineRecoveries == 0,
            "Duplicate ACK did not reset baseline");

        // Duplicate & stale snapshots rejected on client
        EXPECT_TRUE(!cli.ApplySnapshotPacket(*kf1, evs, ackOpt, resyncOpt) &&
                    cli.GetStats().DuplicateSnapshotsDropped == 1,
            "Duplicate keyframe rejected cleanly on client");

        // Send two deltas before any confirmation (seq 2 and seq 3 both against baseline 1)
        desc.InitialTransform.PosX = 5.0f;
        EXPECT_TRUE(srv.UpdateEntityTransform(id, desc.InitialTransform), "Transform updated #1");
        auto d2 = srv.BuildSnapshotForConnection(h, 2);
        EXPECT_TRUE(d2.has_value() && !d2->IsKeyframe && d2->SnapshotSequence == 2 && d2->BaselineSequence == 1,
            "Delta seq=2 built against confirmed baseline=1");

        desc.InitialTransform.PosX = 9.0f;
        EXPECT_TRUE(srv.UpdateEntityTransform(id, desc.InitialTransform), "Transform updated #2");
        auto d3 = srv.BuildSnapshotForConnection(h, 3);
        EXPECT_TRUE(d3.has_value() && !d3->IsKeyframe && d3->SnapshotSequence == 3 && d3->BaselineSequence == 1,
            "Delta seq=3 also built against confirmed baseline=1");

        // Out-of-order ACK for d3 arrives first (ACK for d2 was lost)
        EXPECT_TRUE(srv.HandleSnapshotAck(h, SnapshotAckPacket{ d3->SnapshotSequence, d3->BaselineSequence, true, d3->BaselineEpoch }),
            "Out-of-order ACK for newer seq=3 accepted");
        EXPECT_TRUE(srv.GetConfirmedBaselineSequence(h) == 3 && srv.GetPendingSnapshotCount(h) == 0,
            "Confirmed baseline jumped cleanly to 3 and drained pending window");

        // Delayed old ACK for d2 arrives after d3 already confirmed -> must be safely ignored as stale!
        EXPECT_TRUE(!srv.HandleSnapshotAck(h, SnapshotAckPacket{ d2->SnapshotSequence, d2->BaselineSequence, true, d2->BaselineEpoch }),
            "Late ACK for superseded seq=2 safely ignored");
        EXPECT_TRUE(srv.IsBaselineConfirmed(h) && srv.GetConfirmedBaselineSequence(h) == 3 &&
                    srv.GetStats().StaleAcksIgnored == 1 && srv.GetStats().BaselineRecoveries == 0,
            "Stale older ACK did NOT invalidate newer confirmed baseline");

        // Client receives d3 directly (skipping d2) -> valid because d3.BaselineSequence == 1 == cli.LastApplied
        EXPECT_TRUE(cli.ApplySnapshotPacket(*d3, evs, ackOpt, resyncOpt) &&
                    cli.GetLastAppliedSequence() == 3,
            "Client applied delta d3 matching baseline=1 cleanly");
        // Late stale d2 arrives on client -> rejected without resync corruption
        EXPECT_TRUE(!cli.ApplySnapshotPacket(*d2, evs, ackOpt, resyncOpt) &&
                    cli.GetStats().StaleSnapshotsDropped == 1 && !resyncOpt.has_value(),
            "Client dropped stale older delta d2 without mutating applied state");

        // Future fabricated ACK on server triggers recovery & increments BaselineEpoch
        EXPECT_TRUE(!srv.HandleSnapshotAck(h, SnapshotAckPacket{ 999, 3, true, 1 }),
            "Future fabricated ACK rejected");
        EXPECT_TRUE(!srv.IsBaselineConfirmed(h) && srv.GetStats().InvalidAcksRejected == 1 &&
                    srv.GetStats().BaselineRecoveries == 1 && srv.GetBaselineEpoch(h) == 2,
            "Fabricated ACK invalidated baseline and bumped epoch to 2");

        // Old-epoch ACK arriving after epoch transition is ignored cleanly without incrementing recoveries
        EXPECT_TRUE(!srv.HandleSnapshotAck(h, SnapshotAckPacket{ 3, 1, true, 1 }),
            "Old-epoch ACK ignored cleanly");
        EXPECT_TRUE(srv.GetStats().BaselineRecoveries == 1,
            "Old-epoch ACK did not loop baseline recovery");

        // Server emits recovery keyframe with BaselineEpoch == 2; client transitions epoch without PrepareForReconnect
        auto kfRecovery = srv.BuildSnapshotForConnection(h, 4, true);
        EXPECT_TRUE(kfRecovery.has_value() && kfRecovery->IsKeyframe && kfRecovery->BaselineEpoch == 2,
            "Recovery keyframe emitted with epoch=2");
        EXPECT_TRUE(cli.ApplySnapshotPacket(*kfRecovery, evs, ackOpt, resyncOpt) &&
                    ackOpt.has_value() && cli.GetActiveBaselineEpoch() == 2 &&
                    srv.HandleSnapshotAck(h, *ackOpt),
            "Client and server converged cleanly on new BaselineEpoch=2");

        std::cout << "[PASS] Test_AckBaselineEpochHardeningAndOutOfOrder\n";
    }

    void Test_DeterministicNetworkSimulatorBurstAndRealConnection()
    {
        // 1. Verify deterministic burst loss + reordering + duplication across independent simulator instances
        NetSimConfig simCfg{};
        simCfg.Enabled = true;
        simCfg.Seed = 424242u;
        simCfg.BaseLatencyMs = 25.0f;
        simCfg.JitterMs = 8.0f;
        simCfg.PacketLossPercent = 10.0f;
        simCfg.BurstLossChancePercent = 20.0f;
        simCfg.BurstLossMinPackets = 2u;
        simCfg.BurstLossMaxPackets = 4u;
        simCfg.DuplicationPercent = 15.0f;
        simCfg.ReorderPercent = 25.0f;

        NetworkSimulator simA;
        NetworkSimulator simB;
        simA.Configure(simCfg);
        simB.Configure(simCfg);

        const std::vector<u8> dummyPayload = { 0x11, 0x22, 0x33, 0x44 };
        for (u64 i = 0; i < 60; ++i)
        {
            simA.Submit(ConnectionHandle{ 1 }, NetChannel::Snapshot, NetDelivery::UnreliableSequenced, dummyPayload, i * 10);
            simB.Submit(ConnectionHandle{ 1 }, NetChannel::Snapshot, NetDelivery::UnreliableSequenced, dummyPayload, i * 10);
        }

        std::vector<SimulatedPacket> readyA;
        std::vector<SimulatedPacket> readyB;
        simA.DrainReady(100000ull, readyA);
        simB.DrainReady(100000ull, readyB);

        EXPECT_TRUE(readyA.size() == readyB.size() && !readyA.empty(),
            "Deterministic NetSim produced identical packet counts for identical seeds");
        EXPECT_TRUE(simA.GetStats().PacketsDroppedBurst > 0 &&
                    simA.GetStats().PacketsReordered > 0 &&
                    simA.GetStats().PacketsDuplicated > 0,
            "Burst loss, reordering, and duplication telemetry triggered");
        bool exactMatch = (readyA.size() == readyB.size());
        for (usize i = 0; i < readyA.size() && exactMatch; ++i)
        {
            exactMatch = (readyA[i].ReleaseTimeMs == readyB[i].ReleaseTimeMs);
        }
        EXPECT_TRUE(exactMatch, "Deterministic NetSim schedule matched down to ReleaseTimeMs");

        // 2. Real ConnectionManager client/server replication convergence under adverse NetSim
        ConnectionManager serverNet;
        ConnectionManager clientNet;

        ConnectionConfig srvCfg{};
        srvCfg.Mode = NetworkMode::DedicatedServer;
        srvCfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", 27141 };
        srvCfg.Transport.MaxConnections = 4;
        srvCfg.Security.RequireSignedConnectToken = false;
        srvCfg.Transport.Simulation.Enabled = true;
        srvCfg.Transport.Simulation.Seed = 9001u;
        srvCfg.Transport.Simulation.BaseLatencyMs = 15.0f;
        srvCfg.Transport.Simulation.JitterMs = 5.0f;
        srvCfg.Transport.Simulation.PacketLossPercent = 10.0f;
        srvCfg.Transport.Simulation.BurstLossChancePercent = 5.0f;
        srvCfg.Transport.Simulation.DuplicationPercent = 10.0f;
        srvCfg.Transport.Simulation.ReorderPercent = 15.0f;

        ConnectionConfig cliCfg{};
        cliCfg.Mode = NetworkMode::Client;
        cliCfg.Security.RequireSignedConnectToken = false;
        cliCfg.Transport.Simulation.Enabled = true;
        cliCfg.Transport.Simulation.Seed = 7777u; // distinct client seed
        cliCfg.Transport.Simulation.BaseLatencyMs = 10.0f;
        cliCfg.Transport.Simulation.PacketLossPercent = 5.0f;

        EXPECT_TRUE(serverNet.Initialize(srvCfg).IsSuccess() && clientNet.Initialize(cliCfg).IsSuccess(),
            "Initialized real ConnectionManagers with independent fault simulators");

        Scene::Scene serverScene("FaultSimSrv");
        Scene::Scene clientScene("FaultSimCli");
        SceneReplicationBridge serverBridge;
        SceneReplicationBridge clientBridge;

        ReplicationLaunchConfig repCfg{};
        repCfg.Enabled = true;
        repCfg.AutoSpawnPlayerEntities = false;
        repCfg.ReplicationRateHz = 20;

        serverBridge.Initialize(true, repCfg, serverNet);
        clientBridge.Initialize(false, repCfg, clientNet);

        ConnectionHandle cliOnSrv = kInvalidConnectionHandle;
        serverNet.SetOnConnected([&](const ConnectionInfo& info, const std::string& reason) {
            cliOnSrv = info.Handle;
            serverBridge.OnServerClientAuthenticated(info, reason == "Reconnected", serverScene, serverNet, 0);
        });
        serverNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            serverBridge.HandleNetworkMessage(ev, serverScene, serverNet);
        });

        ConnectionHandle srvOnCli = kInvalidConnectionHandle;
        clientNet.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            srvOnCli = info.Handle;
        });
        clientNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            clientBridge.HandleNetworkMessage(ev, clientScene, clientNet);
        });

        auto connRes = clientNet.Connect(Endpoint{ "127.0.0.1", 27141 });
        EXPECT_TRUE(connRes.IsSuccess(), "Client initiated connection under NetSim");

        // Wait for auth under simulated latency/loss
        for (int i = 0; i < 200; ++i)
        {
            serverNet.Poll();
            clientNet.Poll();
            if (cliOnSrv.IsValid() && srvOnCli.IsValid())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(cliOnSrv.IsValid() && srvOnCli.IsValid(), "Authenticated real session under simulated loss/latency");

        ReplicatedEntitySpawnDesc box{};
        box.WorldId = 1;
        box.EntityName = "SimBox";
        box.InitialTransform.PosY = 7.5f;
        const NetworkId boxNetId = serverBridge.SpawnReplicatedEntity(serverScene, serverNet.GetSessionRegistry(), box, 1);
        EXPECT_TRUE(boxNetId.IsValid(), "Spawned authoritative entity under NetSim");

        bool converged = false;
        for (u64 tick = 1; tick <= 120; ++tick)
        {
            if (tick == 30)
            {
                box.InitialTransform.PosX = 42.0f;
                const Scene::EntityId boxEid = serverBridge.ResolveSceneEntity(boxNetId);
                if (auto* tf = serverScene.Transforms().TryGet(boxEid))
                {
                    tf->Position.x = 42.0f;
                }
            }
            serverBridge.StepServerTick(1.0f / 60.0f, tick, 60, serverScene, nullptr, serverNet);
            serverNet.Poll();
            clientNet.Poll();
            clientBridge.TickClientMaintenance(clientNet, srvOnCli);

            ClientReplicatedEntity ce{};
            if (clientBridge.GetClientApplier().TryGetEntity(boxNetId, ce) &&
                std::fabs(ce.Transform.PosX - 42.0f) < 0.01f &&
                serverBridge.GetServerReplication().IsBaselineConfirmed(cliOnSrv))
            {
                converged = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        EXPECT_TRUE(converged, "Client converged cleanly under simulated loss, jitter, burst loss, duplication, and reordering");

        serverBridge.Shutdown(serverScene, serverNet);
        clientBridge.Shutdown(clientScene, clientNet);
        clientNet.Shutdown();
        serverNet.Shutdown();
        std::cout << "[PASS] Test_DeterministicNetworkSimulatorBurstAndRealConnection\n";
    }

    void Test_ChunkedKeyframeTransportAndAtomicFailureHandling()
    {
        // 1. Spawn > kMaxSnapshotEntriesPerPacket (300 entities) and split into bounded chunks
        ReplicationManager srv;
        srv.SetKeyframeChunkThresholdBytes(512);
        const ConnectionHandle h{ 77 };
        srv.RegisterConnection(h, SessionId{ 0xCE01ull, 0xCE02ull }, 1);

        std::vector<NetworkId> spawnedIds;
        spawnedIds.reserve(300);
        for (int i = 0; i < 300; ++i)
        {
            ReplicatedEntitySpawnDesc d{};
            d.WorldId = 1;
            d.EntityName = "BulkEntity_" + std::to_string(i);
            d.InitialTransform.PosX = static_cast<f32>(i);
            const NetworkId id = srv.SpawnEntity(d, 1);
            EXPECT_TRUE(id.IsValid(), "Spawned bulk entity for chunked keyframe");
            spawnedIds.push_back(id);
        }

        auto kfOpt = srv.BuildSnapshotForConnection(h, 1);
        EXPECT_TRUE(kfOpt.has_value() && kfOpt->Entries.size() == 300,
            "Keyframe included all 300 entities (> legacy 256 cap)");

        std::optional<std::vector<u8>> singleBytes;
        std::vector<KeyframeChunkPacket> chunks;
        EXPECT_TRUE(srv.PrepareOutgoingSnapshot(*kfOpt, singleBytes, chunks) &&
                    !singleBytes.has_value() && chunks.size() >= 6,
            "300-entity keyframe automatically split into multiple bounded KeyframeChunks");

        ClientReplicationApplier cli;
        std::vector<ClientReplicationEvent> evs;
        std::optional<SnapshotAckPacket> ackOpt;
        std::optional<ResyncRequestPacket> resyncOpt;

        // Wire-roundtrip & feed chunks out-of-order + duplicate chunk 0!
        std::vector<KeyframeChunkPacket> wireChunks;
        wireChunks.reserve(chunks.size());
        for (const auto& c : chunks)
        {
            BitWriter cw(ReplicationConstants::kMaxKeyframeChunkPayloadBytes + 128);
            EXPECT_TRUE(ReplicationCodec::WriteKeyframeChunk(cw, c), "Encoded KeyframeChunk");
            EXPECT_TRUE(cw.GetData().size() <= ReplicationConstants::kMaxKeyframeChunkPayloadBytes + 128,
                "Chunk wire message strictly bounded");
            BitReader cr(cw.GetData());
            KeyframeChunkPacket decoded{};
            EXPECT_TRUE(ReplicationCodec::ReadKeyframeChunk(cr, decoded), "Decoded KeyframeChunk");
            wireChunks.push_back(std::move(decoded));
        }

        // Feed reverse order except first chunk last
        for (usize i = wireChunks.size() - 1; i > 0; --i)
        {
            EXPECT_TRUE(!cli.ProcessKeyframeChunk(wireChunks[i], 1000 + i, evs, ackOpt, resyncOpt),
                "Partial chunk does not apply incomplete keyframe");
            EXPECT_TRUE(cli.GetEntityCount() == 0 && cli.IsKeyframeTransferInProgress(),
                "Client world stays untouched while awaiting remaining chunks");
        }
        // Duplicate chunk
        EXPECT_TRUE(!cli.ProcessKeyframeChunk(wireChunks[1], 1500, evs, ackOpt, resyncOpt) &&
                    cli.GetStats().DuplicateChunksIgnored == 1 && !resyncOpt.has_value(),
            "Duplicate chunk ignored idempotently");
        // Final chunk arrives -> atomic apply
        EXPECT_TRUE(cli.ProcessKeyframeChunk(wireChunks[0], 1600, evs, ackOpt, resyncOpt) &&
                    ackOpt.has_value() && cli.GetEntityCount() == 300 &&
                    !cli.IsKeyframeTransferInProgress() && cli.GetStats().ChunkedKeyframesCompleted == 1,
            "All 300 entities applied atomically once final chunk arrived");
        EXPECT_TRUE(srv.HandleSnapshotAck(h, *ackOpt), "Server confirmed chunked keyframe ACK");

        // 2. Test corrupted digest -> atomic rejection preserving live 300-entity world!
        srv.RequestFullResyncForConnection(h);
        auto kfResync = srv.BuildSnapshotForConnection(h, 20, true);
        EXPECT_TRUE(kfResync.has_value(), "Built second keyframe for corruption test");
        std::optional<std::vector<u8>> dummySingle;
        std::vector<KeyframeChunkPacket> badChunks;
        EXPECT_TRUE(srv.PrepareOutgoingSnapshot(*kfResync, dummySingle, badChunks) && badChunks.size() >= 6,
            "Prepared chunks for corruption test");
        badChunks.back().ChunkData[0] ^= 0xFFu; // flip byte in final chunk

        bool badApplied = false;
        std::optional<ResyncRequestPacket> corruptResync;
        for (const auto& c : badChunks)
        {
            if (cli.ProcessKeyframeChunk(c, 2000, evs, ackOpt, resyncOpt))
            {
                badApplied = true;
            }
            if (resyncOpt.has_value())
            {
                corruptResync = resyncOpt;
            }
        }
        EXPECT_TRUE(!badApplied && corruptResync.has_value() &&
                    cli.GetEntityCount() == 300 && cli.GetStats().ChunkedKeyframesFailed == 1,
            "Corrupted chunked keyframe rejected cleanly without mutating existing world");

        // 3. Test missing chunk timeout -> cancels transfer and emits resync
        auto kfTimeout = srv.BuildSnapshotForConnection(h, 60, true);
        EXPECT_TRUE(kfTimeout.has_value(), "Built keyframe for timeout test");
        std::vector<KeyframeChunkPacket> partialChunks;
        EXPECT_TRUE(srv.PrepareOutgoingSnapshot(*kfTimeout, dummySingle, partialChunks) && partialChunks.size() >= 2,
            "Prepared chunks for timeout test");
        EXPECT_TRUE(!cli.ProcessKeyframeChunk(partialChunks[0], 5000, evs, ackOpt, resyncOpt) &&
                    cli.IsKeyframeTransferInProgress(),
            "Incomplete chunk transfer active");
        EXPECT_TRUE(cli.TickKeyframeTimeouts(11000, resyncOpt) && resyncOpt.has_value() &&
                    !cli.IsKeyframeTransferInProgress() && cli.GetStats().ChunkedKeyframesTimedOut == 1,
            "Incomplete chunked transfer timed out cleanly and requested resync");

        // 4. Oversized chunk allocation check
        BitWriter rawBadChunk(128);
        (void)rawBadChunk.WriteU16(ReplicationConstants::kSnapshotWireVersion);
        (void)rawBadChunk.WriteU32(99);
        (void)rawBadChunk.WriteU32(1);
        (void)rawBadChunk.WriteU32(1);
        (void)rawBadChunk.WriteU32(1);
        (void)rawBadChunk.WriteU64(1);
        (void)rawBadChunk.WriteU16(0);
        (void)rawBadChunk.WriteU16(2);
        (void)rawBadChunk.WriteU32(static_cast<u32>(ReplicationConstants::kMaxKeyframeTotalBytes + 1024)); // oversized!
        (void)rawBadChunk.WriteU32(0x12345678u);
        (void)rawBadChunk.WriteU16(4);
        (void)rawBadChunk.WriteU32(0xDEADBEEFu);
        BitReader badChunkReader(rawBadChunk.GetData());
        KeyframeChunkPacket rejectedChunk{};
        EXPECT_TRUE(!ReplicationCodec::ReadKeyframeChunk(badChunkReader, rejectedChunk),
            "Oversized KeyframeChunk TotalBytes rejected without allocation");

        // 5. Real ENet + libsodium chunked keyframe transfer via SceneReplicationBridge
        ConnectionManager srvNet, cliNet;
        ConnectionConfig sc{};
        sc.Mode = NetworkMode::DedicatedServer;
        sc.Transport.BindEndpoint = Endpoint{ "127.0.0.1", 27142 };
        sc.Security.RequireSignedConnectToken = false;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        cc.Security.RequireSignedConnectToken = false;
        EXPECT_TRUE(srvNet.Initialize(sc).IsSuccess() && cliNet.Initialize(cc).IsSuccess(),
            "Initialized transport for live chunked keyframe E2E test");

        Scene::Scene sScene("ChunkSrv"), cScene("ChunkCli");
        SceneReplicationBridge sBridge, cBridge;
        ReplicationLaunchConfig rc{};
        rc.Enabled = true;
        rc.AutoSpawnPlayerEntities = false;
        sBridge.Initialize(true, rc, srvNet);
        sBridge.GetServerReplication().SetKeyframeChunkThresholdBytes(256); // force multi-chunk over real transport!
        cBridge.Initialize(false, rc, cliNet);

        ConnectionHandle cOnS = kInvalidConnectionHandle;
        srvNet.SetOnConnected([&](const ConnectionInfo& info, const std::string& reason) {
            cOnS = info.Handle;
            sBridge.OnServerClientAuthenticated(info, reason == "Reconnected", sScene, srvNet, 0);
        });
        srvNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            sBridge.HandleNetworkMessage(ev, sScene, srvNet);
        });
        cliNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            cBridge.HandleNetworkMessage(ev, cScene, cliNet);
        });

        for (int i = 0; i < 20; ++i)
        {
            ReplicatedEntitySpawnDesc d{};
            d.WorldId = 1;
            d.EntityName = "NetChunkedProp_" + std::to_string(i);
            d.InitialTransform.PosX = static_cast<f32>(i) * 2.0f;
            EXPECT_TRUE(sBridge.SpawnReplicatedEntity(sScene, srvNet.GetSessionRegistry(), d, 0).IsValid(),
                "Spawned entity for live chunked keyframe transfer");
        }

        EXPECT_TRUE(cliNet.Connect(Endpoint{ "127.0.0.1", 27142 }).IsSuccess(), "Connected for chunked E2E");
        bool liveChunkOk = false;
        for (u64 tick = 1; tick <= 80; ++tick)
        {
            sBridge.StepServerTick(1.0f / 60.0f, tick, 60, sScene, nullptr, srvNet);
            srvNet.Poll();
            cliNet.Poll();
            if (cOnS.IsValid() &&
                cBridge.GetClientApplier().GetEntityCount() == 20 &&
                cScene.GetAliveEntityCount() == 20 &&
                sBridge.GetServerReplication().GetStats().ChunkedKeyframesSent >= 1 &&
                sBridge.GetServerReplication().IsBaselineConfirmed(cOnS))
            {
                liveChunkOk = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(liveChunkOk, "Live ENet+libsodium transport delivered chunked keyframe and converged cleanly");

        sBridge.Shutdown(sScene, srvNet);
        cBridge.Shutdown(cScene, cliNet);
        cliNet.Shutdown();
        srvNet.Shutdown();
        std::cout << "[PASS] Test_ChunkedKeyframeTransportAndAtomicFailureHandling\n";
    }

    void Test_StaticSceneOptInReplicationAndReloadLifecycle()
    {
        // 1. Existing editor scene without opt-in flags must NOT automatically replicate entities
        const std::filesystem::path sandboxScene = std::filesystem::path("Samples/Sandbox/Assets/Scenes/ThirdPersonSandbox.mscene.json");
        if (std::filesystem::exists(sandboxScene))
        {
            Scene::Scene sandbox("SandboxCheck");
            EXPECT_TRUE(sandbox.LoadFromJsonFile(sandboxScene), "Loaded ThirdPersonSandbox.mscene.json");
            EXPECT_TRUE(sandbox.GetAliveEntityCount() > 0, "Sandbox scene contains local entities");

            ConnectionManager dummyNet;
            ConnectionConfig cfg{};
            cfg.Mode = NetworkMode::DedicatedServer;
            cfg.Transport.BindEndpoint = Endpoint{ "127.0.0.1", 27143 };
            cfg.Security.RequireSignedConnectToken = false;
            EXPECT_TRUE(dummyNet.Initialize(cfg).IsSuccess(), "Initialized server net for static scene opt-in check");

            SceneReplicationBridge bridge;
            ReplicationLaunchConfig rep{};
            rep.Enabled = true;
            rep.AutoSpawnPlayerEntities = false;
            bridge.Initialize(true, rep, dummyNet);
            EXPECT_TRUE(bridge.RegisterStaticSceneEntities(sandbox, 0), "Registered sandbox static identities (expected 0 opted in)");
            EXPECT_TRUE(bridge.GetServerReplication().GetEntityCount() == 0,
                "Normal unflagged scene entities did NOT replicate automatically");
            bridge.Shutdown(sandbox, dummyNet);
            dummyNet.Shutdown();
        }

        // 2. Duplicate static_id detection must fail cleanly without partial registration
        const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();
        const std::filesystem::path dupScenePath = tmpDir / "morbi_test_dup_static.mscene.json";
        const std::filesystem::path validSceneA = tmpDir / "morbi_test_static_a.mscene.json";
        const std::filesystem::path validSceneB = tmpDir / "morbi_test_static_b.mscene.json";

        {
            std::ofstream out(dupScenePath);
            out << R"({
                "scene_name": "DupTest",
                "entities": [
                    { "name": "PropA", "transform": { "position": [1,0,0] }, "network": { "replicated": true, "static_id": 5001 } },
                    { "name": "PropB", "transform": { "position": [2,0,0] }, "network": { "replicated": true, "static_id": 5001 } }
                ]
            })";
        }
        {
            std::ofstream out(validSceneA);
            out << R"({
                "scene_name": "ValidSceneA",
                "entities": [
                    { "name": "LocalCamera", "transform": { "position": [0,5,-10] } },
                    { "name": "StaticDoor", "transform": { "position": [10,0,0] }, "network": { "replicated": true, "static_id": 1002 } },
                    { "name": "StaticCrate", "transform": { "position": [4,1,0] }, "network": { "replicated": true, "static_id": 1001 } },
                    { "name": "DisabledProp", "transform": { "position": [7,0,0] }, "network": { "replicated": false, "static_id": 1003 } }
                ]
            })";
        }
        {
            std::ofstream out(validSceneB);
            out << R"({
                "scene_name": "ValidSceneB",
                "entities": [
                    { "name": "ReloadedTower", "transform": { "position": [25,0,0] }, "network": { "replicated": true, "static_id": 2001 } }
                ]
            })";
        }

        ConnectionManager srvNet, cliNet;
        ConnectionConfig sc{};
        sc.Mode = NetworkMode::DedicatedServer;
        sc.Transport.BindEndpoint = Endpoint{ "127.0.0.1", 27144 };
        sc.Security.RequireSignedConnectToken = false;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        cc.Security.RequireSignedConnectToken = false;
        EXPECT_TRUE(srvNet.Initialize(sc).IsSuccess() && cliNet.Initialize(cc).IsSuccess(),
            "Initialized nets for static scene replication lifecycle");

        Scene::Scene sScene("StaticSrv"), cScene("StaticCli");
        SceneReplicationBridge sBridge, cBridge;
        ReplicationLaunchConfig repCfg{};
        repCfg.Enabled = true;
        repCfg.AutoSpawnPlayerEntities = false;
        sBridge.Initialize(true, repCfg, srvNet);
        cBridge.Initialize(false, repCfg, cliNet);

        EXPECT_TRUE(sScene.LoadFromJsonFile(dupScenePath), "Loaded duplicate static_id test JSON");
        EXPECT_TRUE(!sBridge.RegisterStaticSceneEntities(sScene, 0) &&
                    sBridge.GetServerReplication().GetEntityCount() == 0,
            "Duplicate static_id rejected cleanly with zero partial spawns");

        // Load validSceneA: only 2 opted-in static entities (1001, 1002) should replicate; LocalCamera & DisabledProp stay local
        EXPECT_TRUE(sScene.LoadFromJsonFile(validSceneA) && sBridge.RegisterStaticSceneEntities(sScene, 0),
            "Registered opted-in static entities from validSceneA");
        EXPECT_TRUE(sScene.GetAliveEntityCount() == 4 && sBridge.GetServerReplication().GetEntityCount() == 2,
            "Server scene has 4 entities while exactly 2 opted-in static entities replicate");
        const NetworkId netCrate = sBridge.ResolveStaticNetworkId(1001);
        const NetworkId netDoor = sBridge.ResolveStaticNetworkId(1002);
        EXPECT_TRUE(netCrate.IsValid() && netDoor.IsValid() && !sBridge.ResolveStaticNetworkId(1003).IsValid(),
            "StaticNetId lookup resolved opted-in entities via NetworkId abstraction");

        // Ensure ECS entity index is NOT blindly identical to StaticNetId or NetworkId
        EXPECT_TRUE(netCrate.Index != 1001u && netDoor.Index != 1002u,
            "NetworkId allocated cleanly via NetworkIdPool without leaking StaticNetId or ECS index");

        ConnectionHandle cOnS = kInvalidConnectionHandle;
        srvNet.SetOnConnected([&](const ConnectionInfo& info, const std::string& reason) {
            cOnS = info.Handle;
            sBridge.OnServerClientAuthenticated(info, reason == "Reconnected", sScene, srvNet, 0);
        });
        srvNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            sBridge.HandleNetworkMessage(ev, sScene, srvNet);
        });
        cliNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            cBridge.HandleNetworkMessage(ev, cScene, cliNet);
        });

        EXPECT_TRUE(cliNet.Connect(Endpoint{ "127.0.0.1", 27144 }).IsSuccess(), "Connected client for static scene test");
        bool initialStaticSynced = false;
        for (u64 t = 1; t <= 60; ++t)
        {
            sBridge.StepServerTick(1.0f / 60.0f, t, 60, sScene, nullptr, srvNet);
            srvNet.Poll();
            cliNet.Poll();
            ClientReplicatedEntity ceCrate{};
            if (cOnS.IsValid() &&
                cBridge.GetClientApplier().GetEntityCount() == 2 &&
                cBridge.GetClientApplier().TryGetEntity(netCrate, ceCrate) &&
                std::fabs(ceCrate.Transform.PosX - 4.0f) < 0.01f &&
                sBridge.GetServerReplication().IsBaselineConfirmed(cOnS))
            {
                initialStaticSynced = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(initialStaticSynced, "Client received only the 2 opted-in static scene entities with accurate transforms");

        // Reload scene B: old static entities must despawn cleanly and new static entity 2001 must spawn on client
        sBridge.OnSceneCleared(srvNet.GetSessionRegistry());
        EXPECT_TRUE(sScene.LoadFromJsonFile(validSceneB) && sBridge.RegisterStaticSceneEntities(sScene, 61),
            "Reloaded scene B and registered updated static entities");
        const NetworkId netTower = sBridge.ResolveStaticNetworkId(2001);
        EXPECT_TRUE(netTower.IsValid() && !sBridge.ResolveStaticNetworkId(1001).IsValid(),
            "Reload replaced old static net bindings with new scene static entity");

        bool reloadConverged = false;
        for (u64 t = 62; t <= 120; ++t)
        {
            sBridge.StepServerTick(1.0f / 60.0f, t, 60, sScene, nullptr, srvNet);
            srvNet.Poll();
            cliNet.Poll();
            ClientReplicatedEntity ceTower{};
            if (cBridge.GetClientApplier().GetEntityCount() == 1 &&
                cBridge.GetClientApplier().TryGetEntity(netTower, ceTower) &&
                std::fabs(ceTower.Transform.PosX - 25.0f) < 0.01f)
            {
                reloadConverged = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(reloadConverged, "Scene reload cleanly despawned old static entities and spawned reloaded entity");

        sBridge.Shutdown(sScene, srvNet);
        cBridge.Shutdown(cScene, cliNet);
        cliNet.Shutdown();
        srvNet.Shutdown();
        std::error_code ec;
        std::filesystem::remove(dupScenePath, ec);
        std::filesystem::remove(validSceneA, ec);
        std::filesystem::remove(validSceneB, ec);
        std::cout << "[PASS] Test_StaticSceneOptInReplicationAndReloadLifecycle\n";
    }

    void Test_SpawnDespawnEdgeCasesOwnershipTransferAndReconnectRecovery()
    {
        ReplicationManager srv;
        ClientReplicationApplier cliA, cliB;

        const ConnectionHandle hA{ 1 }, hB{ 2 };
        const SessionId sA{ 0x1001ull, 0x01ull }, sB{ 0x2002ull, 0x02ull };
        srv.RegisterConnection(hA, sA, 1);
        srv.RegisterConnection(hB, sB, 1);

        // Initial keyframe handshake with 0 entities
        auto kA1 = srv.BuildSnapshotForConnection(hA, 1);
        auto kB1 = srv.BuildSnapshotForConnection(hB, 1);
        std::vector<ClientReplicationEvent> evs;
        std::optional<SnapshotAckPacket> ack;
        std::optional<ResyncRequestPacket> resync;
        EXPECT_TRUE(cliA.ApplySnapshotPacket(*kA1, evs, ack, resync) && srv.HandleSnapshotAck(hA, *ack), "CliA baseline confirmed");
        EXPECT_TRUE(cliB.ApplySnapshotPacket(*kB1, evs, ack, resync) && srv.HandleSnapshotAck(hB, *ack), "CliB baseline confirmed");

        // 1. Immediate spawn + despawn before next snapshot -> zero ghost entities ever arrive on client
        ReplicatedEntitySpawnDesc transientDesc{};
        transientDesc.OwnerSession = sA;
        transientDesc.OwnerPlayer = 10;
        transientDesc.WorldId = 1;
        transientDesc.EntityName = "TransientProjectile";
        const NetworkId id1 = srv.SpawnEntity(transientDesc, 2);
        EXPECT_TRUE(id1.IsValid() && srv.DespawnEntity(id1), "Spawned and immediately despawned transient entity");
        EXPECT_TRUE(!srv.BuildSnapshotForConnection(hA, 3).has_value(),
            "Immediate spawn+despawn emitted zero ghost ops");

        // 2. Spawn persistent entity and verify generational reuse check
        transientDesc.EntityName = "RelicItem";
        transientDesc.OwnerPlayer = 10;
        transientDesc.OwnerSession = sA;
        transientDesc.Scope = ReplicationScope::AllRelevant;
        const NetworkId id2 = srv.SpawnEntity(transientDesc, 4);
        EXPECT_TRUE(id2.IsValid() && id2.Index == id1.Index && id2.Generation > id1.Generation,
            "Recycled slot incremented NetworkId Generation strictly");
        ServerReplicatedEntity staleCheck{};
        EXPECT_TRUE(!srv.TryGetEntity(id1, staleCheck), "Stale NetworkId generation lookup rejected");

        // Also spawn an OwnerOnly entity for sA
        ReplicatedEntitySpawnDesc secretDesc{};
        secretDesc.OwnerSession = sA;
        secretDesc.OwnerPlayer = 10;
        secretDesc.WorldId = 1;
        secretDesc.Scope = ReplicationScope::OwnerOnly;
        secretDesc.EntityName = "SecretInventory";
        const NetworkId secretId = srv.SpawnEntity(secretDesc, 4);

        auto dA2 = srv.BuildSnapshotForConnection(hA, 5);
        auto dB2 = srv.BuildSnapshotForConnection(hB, 5);
        EXPECT_TRUE(dA2.has_value() && dB2.has_value(), "Deltas built for both clients");
        EXPECT_TRUE(cliA.ApplySnapshotPacket(*dA2, evs, ack, resync) && srv.HandleSnapshotAck(hA, *ack), "CliA received relic + secret");
        EXPECT_TRUE(cliB.ApplySnapshotPacket(*dB2, evs, ack, resync) && srv.HandleSnapshotAck(hB, *ack), "CliB received relic only");
        EXPECT_TRUE(cliA.GetEntityCount() == 2 && cliB.GetEntityCount() == 1,
            "OwnerOnly entity isolated strictly to owner A");

        // 3. Ownership transfer: transfer both RelicItem and SecretInventory from sA/10 -> sB/20
        EXPECT_TRUE(srv.TransferEntityOwnership(id2, sB, 20) &&
                    srv.TransferEntityOwnership(secretId, sB, 20),
            "Transferred ownership of both entities cleanly from PlayerA to PlayerB");

        auto dA3 = srv.BuildSnapshotForConnection(hA, 6);
        auto dB3 = srv.BuildSnapshotForConnection(hB, 6);
        EXPECT_TRUE(dA3.has_value() && dB3.has_value(), "Ownership deltas emitted");
        EXPECT_TRUE(cliA.ApplySnapshotPacket(*dA3, evs, ack, resync) && srv.HandleSnapshotAck(hA, *ack), "CliA processed handoff");
        EXPECT_TRUE(cliB.ApplySnapshotPacket(*dB3, evs, ack, resync) && srv.HandleSnapshotAck(hB, *ack), "CliB processed handoff");

        ClientReplicatedEntity relB{}, secB{};
        EXPECT_TRUE(cliA.GetEntityCount() == 1 && cliB.GetEntityCount() == 2 &&
                    cliB.TryGetEntity(id2, relB) && relB.OwnerPlayer == 20 &&
                    cliB.TryGetEntity(secretId, secB) && secB.OwnerPlayer == 20,
            "Ownership handoff spawned secret entity on new owner B, despawned from A, and updated OwnerPlayer");

        std::cout << "[PASS] Test_SpawnDespawnEdgeCasesOwnershipTransferAndReconnectRecovery\n";
    }

    void Test_PhysicsRigidBodyAuthoritativeReplicationAndSleep()
    {
        // 1. ReplicatedPhysicsStateData serialization & malformed bounds rejection
        ReplicatedPhysicsStateData phys{};
        phys.LinearVelX = 3.5f;
        phys.LinearVelY = -9.8f;
        phys.LinearVelZ = 1.25f;
        phys.AngularVelY = 2.0f;
        phys.PhysicsFlags = ReplicatedPhysicsFlags::kRigidBody;
        BitWriter pw(64);
        EXPECT_TRUE(phys.NetSerialize(pw), "Serialized valid ReplicatedPhysicsStateData");
        BitReader pr(pw.GetData());
        ReplicatedPhysicsStateData decoded{};
        EXPECT_TRUE(decoded.NetDeserialize(pr, ReplicationConstants::kPhysicsSchemaVersion) &&
                    decoded.ApproximatelyEquals(phys),
            "Decoded ReplicatedPhysicsStateData round-tripped cleanly");

        ReplicatedPhysicsStateData invalidPhys{};
        invalidPhys.LinearVelX = ReplicationConstants::kMaxSpeedAbs + 50.0f;
        EXPECT_TRUE(!invalidPhys.IsValid(), "Excessive physics linear velocity rejected");

        // 2. Real Jolt PhysicsWorld + ConnectionManager + SceneReplicationBridge gravity, collision, sleep, wake
        Physics::PhysicsWorld physics(1.0f / 60.0f);
        EXPECT_TRUE(physics.Initialize(), "Initialized Jolt PhysicsWorld");

        Scene::Scene srvScene("PhysSrv"), cliScene("PhysCli");
        // Static floor at y = -0.5, thickness 1.0 (top surface at y = 0.0)
        const Scene::EntityId floorEid = srvScene.CreateEntity("Floor");
        if (auto* tf = srvScene.Transforms().TryGet(floorEid))
        {
            tf->Position = { 0.0f, -0.5f, 0.0f };
        }
        Scene::RigidBodyComponent floorRb{};
        floorRb.MotionType = Scene::RigidBodyMotionType::Static;
        floorRb.Shape = Scene::ColliderShapeType::Box;
        floorRb.HalfExtents = { 25.0f, 0.5f, 25.0f };
        floorRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        srvScene.RigidBodies().EmplaceOrReplace(floorEid, floorRb);

        ConnectionManager srvNet, cliNet;
        ConnectionConfig sc{};
        sc.Mode = NetworkMode::DedicatedServer;
        sc.Transport.BindEndpoint = Endpoint{ "127.0.0.1", 27151 };
        sc.Security.RequireSignedConnectToken = false;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        cc.Security.RequireSignedConnectToken = false;
        EXPECT_TRUE(srvNet.Initialize(sc).IsSuccess() && cliNet.Initialize(cc).IsSuccess(),
            "Initialized transport for physics rigid body test");

        SceneReplicationBridge srvBridge, cliBridge;
        ReplicationLaunchConfig repCfg{};
        repCfg.Enabled = true;
        repCfg.AutoSpawnPlayerEntities = false;
        repCfg.ReplicationRateHz = 60;
        srvBridge.Initialize(true, repCfg, srvNet);
        cliBridge.Initialize(false, repCfg, cliNet);

        ConnectionHandle cOnS = kInvalidConnectionHandle;
        srvNet.SetOnConnected([&](const ConnectionInfo& info, const std::string& reason) {
            cOnS = info.Handle;
            srvBridge.OnServerClientAuthenticated(info, reason == "Reconnected", srvScene, srvNet, 0);
        });
        srvNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            srvBridge.HandleNetworkMessage(ev, srvScene, srvNet);
        });
        cliNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            cliBridge.HandleNetworkMessage(ev, cliScene, cliNet);
        });

        // Spawn dynamic rigid body cube above floor at y = 4.0
        ReplicatedEntitySpawnDesc cubeDesc{};
        cubeDesc.WorldId = 1;
        cubeDesc.EntityName = "FallingCrate";
        cubeDesc.ComponentMask = SnapshotComponentFlags::kTransform | SnapshotComponentFlags::kPhysics;
        cubeDesc.InitialTransform.PosY = 4.0f;
        cubeDesc.InitialPhysics.PhysicsFlags = ReplicatedPhysicsFlags::kRigidBody;
        const NetworkId cubeNetId = srvBridge.SpawnReplicatedEntity(srvScene, srvNet.GetSessionRegistry(), cubeDesc, 0);
        const Scene::EntityId cubeEid = srvBridge.ResolveSceneEntity(cubeNetId);
        EXPECT_TRUE(cubeNetId.IsValid() && srvScene.IsAlive(cubeEid), "Spawned dynamic crate entity");

        Scene::RigidBodyComponent cubeRb{};
        cubeRb.MotionType = Scene::RigidBodyMotionType::Dynamic;
        cubeRb.Shape = Scene::ColliderShapeType::Box;
        cubeRb.HalfExtents = { 0.5f, 0.5f, 0.5f };
        cubeRb.Mass = 5.0f;
        cubeRb.Restitution = 0.05f;
        cubeRb.Friction = 0.8f;
        cubeRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        srvScene.RigidBodies().EmplaceOrReplace(cubeEid, cubeRb);
        physics.RebuildFromScene(srvScene);

        EXPECT_TRUE(cliNet.Connect(Endpoint{ "127.0.0.1", 27151 }).IsSuccess(), "Connected client for physics rigid body test");

        bool sawFallingVel = false;
        bool settledAndSlept = false;
        for (u64 tick = 1; tick <= 240; ++tick)
        {
            physics.StepSimulation(1.0f / 60.0f, srvScene);
            srvBridge.StepServerTick(1.0f / 60.0f, tick, 60, srvScene, &physics, srvNet);
            srvNet.Poll();
            cliNet.Poll();

            ClientReplicatedEntity ce{};
            if (cliBridge.GetClientApplier().TryGetEntity(cubeNetId, ce))
            {
                if (ce.Physics.LinearVelY < -1.0f && ce.Transform.PosY < 3.95f)
                {
                    sawFallingVel = true;
                }
                if (sawFallingVel &&
                    (ce.Physics.PhysicsFlags & ReplicatedPhysicsFlags::kSleeping) != 0 &&
                    ce.Transform.PosY > 0.3f && ce.Transform.PosY < 0.7f &&
                    srvBridge.GetServerReplication().GetPendingSnapshotCount(cOnS) == 0)
                {
                    settledAndSlept = true;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(sawFallingVel, "Client observed authoritative falling gravity & negative Y velocity");
        EXPECT_TRUE(settledAndSlept, "Dynamic rigid body collided with floor, settled at y~0.5, and replicated sleep flag");

        // Verify sleeping body emits zero redundant delta snapshots
        const u64 deltasBeforeSleepIdle = srvBridge.GetServerReplication().GetStats().DeltasSent;
        for (u64 tick = 241; tick <= 255; ++tick)
        {
            physics.StepSimulation(1.0f / 60.0f, srvScene);
            srvBridge.StepServerTick(1.0f / 60.0f, tick, 60, srvScene, &physics, srvNet);
            srvNet.Poll();
            cliNet.Poll();
        }
        EXPECT_TRUE(srvBridge.GetServerReplication().GetStats().DeltasSent == deltasBeforeSleepIdle,
            "Sleeping rigid body emitted zero redundant delta packets");

        // Wake up via authoritative server impulse and verify client observes horizontal displacement + spoof overwrite
        const Scene::EntityId cliCubeEid = cliBridge.ResolveSceneEntity(cubeNetId);
        if (auto* cliTf = cliScene.Transforms().TryGet(cliCubeEid))
        {
            cliTf->Position.x = 999.0f; // simulated invalid client local perturbation
        }
        EXPECT_TRUE(physics.AddLinearImpulse(srvScene, cubeEid, Math::Vec3{ 35.0f, 2.0f, 0.0f }),
            "Applied server-authoritative linear impulse");
        bool wokeAndMoved = false;
        for (u64 tick = 256; tick <= 310; ++tick)
        {
            physics.StepSimulation(1.0f / 60.0f, srvScene);
            srvBridge.StepServerTick(1.0f / 60.0f, tick, 60, srvScene, &physics, srvNet);
            srvNet.Poll();
            cliNet.Poll();

            ClientReplicatedEntity ce{};
            if (cliBridge.GetClientApplier().TryGetEntity(cubeNetId, ce) &&
                (ce.Physics.PhysicsFlags & ReplicatedPhysicsFlags::kSleeping) == 0 &&
                ce.Transform.PosX > 0.5f && ce.Transform.PosX < 20.0f)
            {
                if (const auto* cliTf = cliScene.Transforms().TryGet(cliCubeEid))
                {
                    if (cliTf->Position.x < 20.0f)
                    {
                        wokeAndMoved = true;
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(wokeAndMoved, "Authoritative server impulse woke body, replicated motion, and overwrote client spoof");

        srvBridge.Shutdown(srvScene, srvNet);
        cliBridge.Shutdown(cliScene, cliNet);
        cliNet.Shutdown();
        srvNet.Shutdown();
        physics.Shutdown();
        std::cout << "[PASS] Test_PhysicsRigidBodyAuthoritativeReplicationAndSleep\n";
    }

    void Test_PhysicsCharacterVirtualInputCollisionAndPush()
    {
        // Authoritative CharacterVirtual player input -> ground movement, jump, wall block, and dynamic crate push
        SimulationLaunchConfig srvLaunch{};
        srvLaunch.Profile = RuntimeProfile::DedicatedServer;
        srvLaunch.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvLaunch.Capabilities.Scripting = false;
        srvLaunch.BindAddress = "127.0.0.1";
        srvLaunch.Port = 27152;
        srvLaunch.TickRate = 60;
        srvLaunch.Replication.Enabled = true;
        srvLaunch.Replication.AutoSpawnPlayerEntities = true;
        srvLaunch.Replication.UsePhysicsCharacterForPlayers = true;
        srvLaunch.Replication.ReplicationRateHz = 60;
        srvLaunch.Replication.PlayerMoveSpeed = 6.0f;
        srvLaunch.Replication.PlayerJumpSpeed = 7.0f;

        SimulationSession serverSession;
        EXPECT_TRUE(serverSession.Initialize(srvLaunch).IsSuccess(), "Initialized headless physics server session");

        Scene::Scene& srvScene = serverSession.GetScene();
        Physics::PhysicsWorld& physics = serverSession.GetPhysicsWorld();

        // Ground floor
        const Scene::EntityId floorEid = srvScene.CreateEntity("PhysFloor");
        srvScene.Transforms().TryGet(floorEid)->Position = { 0.0f, -0.5f, 0.0f };
        Scene::RigidBodyComponent floorRb{};
        floorRb.MotionType = Scene::RigidBodyMotionType::Static;
        floorRb.HalfExtents = { 30.0f, 0.5f, 30.0f };
        floorRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        srvScene.RigidBodies().EmplaceOrReplace(floorEid, floorRb);

        // Blocking wall at x = 6.0
        const Scene::EntityId wallEid = srvScene.CreateEntity("BlockingWall");
        srvScene.Transforms().TryGet(wallEid)->Position = { 6.0f, 1.0f, 0.0f };
        Scene::RigidBodyComponent wallRb{};
        wallRb.MotionType = Scene::RigidBodyMotionType::Static;
        wallRb.HalfExtents = { 0.5f, 2.0f, 10.0f };
        wallRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        srvScene.RigidBodies().EmplaceOrReplace(wallEid, wallRb);
        physics.RebuildFromScene(srvScene);

        // Pushable dynamic crate at x = 2.0
        ReplicatedEntitySpawnDesc crateDesc{};
        crateDesc.WorldId = 1;
        crateDesc.EntityName = "PushCrate";
        crateDesc.ComponentMask = SnapshotComponentFlags::kTransform | SnapshotComponentFlags::kPhysics;
        crateDesc.InitialTransform.PosX = 2.0f;
        crateDesc.InitialTransform.PosY = 0.5f;
        crateDesc.InitialPhysics.PhysicsFlags = ReplicatedPhysicsFlags::kRigidBody;
        const NetworkId crateNetId = serverSession.GetReplicationBridge().SpawnReplicatedEntity(
            srvScene, serverSession.GetNetworkManager().GetSessionRegistry(), crateDesc, 0);
        const Scene::EntityId crateEid = serverSession.GetReplicationBridge().ResolveSceneEntity(crateNetId);
        Scene::RigidBodyComponent crateRb{};
        crateRb.MotionType = Scene::RigidBodyMotionType::Dynamic;
        crateRb.HalfExtents = { 0.5f, 0.5f, 0.5f };
        crateRb.Mass = 5.0f;
        crateRb.Friction = 0.25f;
        crateRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        srvScene.RigidBodies().EmplaceOrReplace(crateEid, crateRb);
        EXPECT_TRUE(physics.SyncEntityBody(srvScene, crateEid), "Synced pushable dynamic crate into Jolt");

        // Connect real client
        ConnectionManager cliNet;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        EXPECT_TRUE(cliNet.Initialize(cc).IsSuccess(), "Initialized client net");
        Scene::Scene cliScene("PhysPlayerCli");
        SceneReplicationBridge cliBridge;
        cliBridge.Initialize(false, srvLaunch.Replication, cliNet);
        ConnectionHandle srvConn = kInvalidConnectionHandle;
        cliNet.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            srvConn = info.Handle;
        });
        cliNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            cliBridge.HandleNetworkMessage(ev, cliScene, cliNet);
        });
        EXPECT_TRUE(cliNet.Connect(Endpoint{ "127.0.0.1", 27152 }).IsSuccess(), "Client connecting to physics server");

        NetworkId playerNetId = kInvalidNetworkId;
        for (int i = 0; i < 120; ++i)
        {
            serverSession.StepFixedTick();
            cliNet.Poll();
            if (srvConn.IsValid() && cliBridge.GetClientApplier().GetEntityCount() >= 2)
            {
                for (const auto& [packed, ent] : cliBridge.GetClientApplier().GetEntities())
                {
                    (void)packed;
                    if (ent.NetId != crateNetId && ent.OwnerPlayer != kInvalidPlayerId)
                    {
                        playerNetId = ent.NetId;
                        break;
                    }
                }
                if (playerNetId.IsValid())
                {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(playerNetId.IsValid(), "Client received physics-backed CharacterVirtual player and crate");

        // Drive player forward (+X) to push crate and hit blocking wall, plus jump!
        u32 seq = 1;
        bool cratePushed = false;
        bool sawJumpAirborne = false;
        f32 maxPlayerX = 0.0f;

        for (int step = 0; step < 90; ++step)
        {
            PlayerInputCommand cmd{};
            cmd.TargetEntity = playerNetId;
            cmd.Sequence = seq++;
            cmd.ClientTick = static_cast<u32>(step + 1);
            cmd.MoveX = 1.0f;
            cmd.MoveZ = 0.0f;
            cmd.ActionFlags = (step == 60) ? InputActionFlags::kJump : InputActionFlags::kNone;
            EXPECT_TRUE(cliBridge.ClientSendInput(cliNet, srvConn, cmd), "Sent PlayerInputCommand");

            serverSession.StepFixedTick();
            cliNet.Poll();

            ClientReplicatedEntity pEnt{}, cEnt{};
            if (cliBridge.GetClientApplier().TryGetEntity(playerNetId, pEnt))
            {
                maxPlayerX = (std::max)(maxPlayerX, pEnt.Transform.PosX);
                if (step > 61 && (pEnt.Physics.PhysicsFlags & ReplicatedPhysicsFlags::kGrounded) == 0 &&
                    pEnt.Transform.PosY > 0.15f)
                {
                    sawJumpAirborne = true;
                }
            }
            if (cliBridge.GetClientApplier().TryGetEntity(crateNetId, cEnt) && cEnt.Transform.PosX > 2.3f)
            {
                cratePushed = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        EXPECT_TRUE(cratePushed, "Authoritative CharacterVirtual pushed dynamic crate and replicated crate displacement");
        EXPECT_TRUE(maxPlayerX > 1.5f && maxPlayerX < 5.6f,
            "Static wall blocked CharacterVirtual cleanly without tunneling (maxX=" + std::to_string(maxPlayerX) + ")");
        EXPECT_TRUE(sawJumpAirborne, "Authoritative CharacterVirtual jump transitioned grounded state and Y height");

        cliBridge.Shutdown(cliScene, cliNet);
        cliNet.Shutdown();
        serverSession.Shutdown();
        std::cout << "[PASS] Test_PhysicsCharacterVirtualInputCollisionAndPush\n";
    }

    void Test_PhysicsHeadlessDedicatedServerMultiClientCollision()
    {
        SimulationLaunchConfig srvLaunch{};
        srvLaunch.Profile = RuntimeProfile::DedicatedServer;
        srvLaunch.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvLaunch.Capabilities.Scripting = false;
        srvLaunch.BindAddress = "127.0.0.1";
        srvLaunch.Port = 27153;
        srvLaunch.TickRate = 60;
        srvLaunch.Replication.Enabled = true;
        srvLaunch.Replication.AutoSpawnPlayerEntities = true;
        srvLaunch.Replication.UsePhysicsCharacterForPlayers = true;
        srvLaunch.Replication.ReplicationRateHz = 30;

        SimulationSession serverSession;
        EXPECT_TRUE(serverSession.Initialize(srvLaunch).IsSuccess(), "Initialized multi-client physics server");

        Scene::Scene& srvScene = serverSession.GetScene();
        const Scene::EntityId floorEid = srvScene.CreateEntity("SharedFloor");
        srvScene.Transforms().TryGet(floorEid)->Position = { 0.0f, -0.5f, 0.0f };
        Scene::RigidBodyComponent floorRb{};
        floorRb.MotionType = Scene::RigidBodyMotionType::Static;
        floorRb.HalfExtents = { 30.0f, 0.5f, 30.0f };
        floorRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
        srvScene.RigidBodies().EmplaceOrReplace(floorEid, floorRb);
        serverSession.GetPhysicsWorld().RebuildFromScene(srvScene);

        ConnectionManager cliNetA, cliNetB;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        EXPECT_TRUE(cliNetA.Initialize(cc).IsSuccess() && cliNetB.Initialize(cc).IsSuccess(), "Initialized 2 clients");

        Scene::Scene sceneA("CliA"), sceneB("CliB");
        SceneReplicationBridge bridgeA, bridgeB;
        bridgeA.Initialize(false, srvLaunch.Replication, cliNetA);
        bridgeB.Initialize(false, srvLaunch.Replication, cliNetB);

        ConnectionHandle connA = kInvalidConnectionHandle, connB = kInvalidConnectionHandle;
        cliNetA.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { connA = i.Handle; });
        cliNetB.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { connB = i.Handle; });
        cliNetA.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { bridgeA.HandleNetworkMessage(ev, sceneA, cliNetA); });
        cliNetB.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { bridgeB.HandleNetworkMessage(ev, sceneB, cliNetB); });

        EXPECT_TRUE(cliNetA.Connect(Endpoint{ "127.0.0.1", 27153 }).IsSuccess() &&
                    cliNetB.Connect(Endpoint{ "127.0.0.1", 27153 }).IsSuccess(),
            "Both clients connected to physics server");

        bool bothSawTwoPlayers = false;
        for (int i = 0; i < 120; ++i)
        {
            serverSession.StepFixedTick();
            cliNetA.Poll();
            cliNetB.Poll();
            if (connA.IsValid() && connB.IsValid() &&
                bridgeA.GetClientApplier().GetEntityCount() == 2 &&
                bridgeB.GetClientApplier().GetEntityCount() == 2)
            {
                bothSawTwoPlayers = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(bothSawTwoPlayers, "Both clients observed both physics-backed player entities");

        // Find PlayerA's NetworkId (spawned at x=0.0, PlayerB spawned at x=3.0)
        NetworkId netA = kInvalidNetworkId;
        for (const auto& [packed, ent] : bridgeA.GetClientApplier().GetEntities())
        {
            (void)packed;
            if (std::fabs(ent.Transform.PosX) < 0.1f)
            {
                netA = ent.NetId;
                break;
            }
        }
        EXPECT_TRUE(netA.IsValid(), "Identified PlayerA NetworkId");

        bool clientBObservedPlayerAMove = false;
        for (u32 step = 1; step <= 45; ++step)
        {
            PlayerInputCommand cmd{};
            cmd.TargetEntity = netA;
            cmd.Sequence = step;
            cmd.ClientTick = step;
            cmd.MoveZ = 1.0f;
            (void)bridgeA.ClientSendInput(cliNetA, connA, cmd);

            serverSession.StepFixedTick();
            cliNetA.Poll();
            cliNetB.Poll();

            ClientReplicatedEntity entOnB{};
            if (bridgeB.GetClientApplier().TryGetEntity(netA, entOnB) && entOnB.Transform.PosZ > 1.0f)
            {
                clientBObservedPlayerAMove = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(clientBObservedPlayerAMove, "Client B observed Player A's authoritative physics movement");

        bridgeA.Shutdown(sceneA, cliNetA);
        bridgeB.Shutdown(sceneB, cliNetB);
        cliNetA.Shutdown();
        cliNetB.Shutdown();
        serverSession.Shutdown();
        std::cout << "[PASS] Test_PhysicsHeadlessDedicatedServerMultiClientCollision\n";
    }

    void Test_ClientInputHistoryRingBufferAndBounds()
    {
        ClientInputHistory history(16);
        EXPECT_TRUE(history.GetCapacity() == 16 && history.IsEmpty(), "Initialized bounded input history capacity=16");

        const NetworkId target{ 1, 1 };
        // Reject invalid / zero sequence
        ClientInputHistoryEntry bad{};
        bad.Command.TargetEntity = target;
        bad.Command.Sequence = 0;
        EXPECT_TRUE(!history.Push(bad), "Rejected sequence=0 input history entry");

        // Push 20 entries into capacity=16 -> 4 oldest evicted (Overflows == 4)
        for (u32 seq = 1; seq <= 20; ++seq)
        {
            ClientInputHistoryEntry e{};
            e.Command.TargetEntity = target;
            e.Command.Sequence = seq;
            e.Command.ClientTick = seq;
            e.Command.MoveX = 1.0f;
            e.FixedDt = 1.0f / 60.0f;
            e.PredictedPosX = static_cast<f32>(seq) * 0.1f;
            EXPECT_TRUE(history.Push(e), "Pushed monotonic input entry");
        }
        EXPECT_TRUE(history.GetCount() == 16 && history.GetStats().Overflows == 4 &&
                    history.GetLastPushedSequence() == 20,
            "Ring buffer bounded at 16 and tracked 4 overflows");

        // Reject duplicate / non-monotonic sequence
        ClientInputHistoryEntry dup{};
        dup.Command.TargetEntity = target;
        dup.Command.Sequence = 15;
        EXPECT_TRUE(!history.Push(dup), "Rejected non-monotonic sequence");

        // Invalid future ACK (> LastPushedSequence) safely ignored
        EXPECT_TRUE(history.AcknowledgeUpTo(999) == 0 &&
                    history.GetStats().InvalidFutureAcksIgnored == 1 &&
                    history.GetCount() == 16,
            "Future sequence ACK ignored without corrupting history");

        // Acknowledge up to seq=10 -> removes seq 5..10 (6 entries), leaves 11..20 (10 entries)
        const usize acked = history.AcknowledgeUpTo(10);
        EXPECT_TRUE(acked == 6 && history.GetCount() == 10 &&
                    history.GetLastAcknowledgedSequence() == 10,
            "Acknowledged confirmed inputs up to seq=10");

        // Stale ACK (<= 10) safely ignored
        EXPECT_TRUE(history.AcknowledgeUpTo(8) == 0 &&
                    history.GetStats().StaleAcksIgnored == 1 &&
                    history.GetCount() == 10,
            "Stale ACK ignored safely");

        std::vector<ClientInputHistoryEntry> unacked;
        history.CopyUnacknowledged(unacked);
        EXPECT_TRUE(unacked.size() == 10 && unacked.front().Command.Sequence == 11 && unacked.back().Command.Sequence == 20,
            "Unacknowledged entries preserved in ascending order (11..20)");

        history.Reset();
        EXPECT_TRUE(history.IsEmpty() && history.GetLastPushedSequence() == 0, "History reset cleanly");
        std::cout << "[PASS] Test_ClientInputHistoryRingBufferAndBounds\n";
    }

    void Test_ClientPredictionReconciliationAndSmoothing()
    {
        SimulationLaunchConfig srvLaunch{};
        srvLaunch.Profile = RuntimeProfile::DedicatedServer;
        srvLaunch.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvLaunch.Capabilities.Physics = false;
        srvLaunch.Capabilities.Scripting = false;
        srvLaunch.BindAddress = "127.0.0.1";
        srvLaunch.Port = 27161;
        srvLaunch.TickRate = 60;
        srvLaunch.Replication.Enabled = true;
        srvLaunch.Replication.AutoSpawnPlayerEntities = true;
        srvLaunch.Replication.ReplicationRateHz = 30;
        srvLaunch.Replication.PlayerMoveSpeed = 6.0f;

        SimulationSession serverSession;
        EXPECT_TRUE(serverSession.Initialize(srvLaunch).IsSuccess(), "Initialized prediction test server");

        // Configure adverse NetSim on client (latency + jitter + loss + reorder)
        ConnectionManager cliNet;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        cc.Transport.Simulation.Enabled = true;
        cc.Transport.Simulation.Seed = 606060u;
        cc.Transport.Simulation.BaseLatencyMs = 40.0f;
        cc.Transport.Simulation.JitterMs = 8.0f;
        cc.Transport.Simulation.PacketLossPercent = 10.0f;
        cc.Transport.Simulation.ReorderPercent = 10.0f;
        EXPECT_TRUE(cliNet.Initialize(cc).IsSuccess(), "Initialized client with NetSim for prediction test");

        Scene::Scene cliScene("PredCli");
        SceneReplicationBridge cliBridge;
        cliBridge.Initialize(false, srvLaunch.Replication, cliNet);
        ClientPredictionConfig predCfg{};
        predCfg.EnablePrediction = true;
        predCfg.EnableRemoteInterpolation = true;
        predCfg.ReconciliationIgnoreThreshold = 0.01f;
        predCfg.ReconciliationSnapThreshold = 1.5f;
        predCfg.VisualSmoothingRate = 18.0f;
        cliBridge.ConfigurePrediction(predCfg);

        ConnectionHandle srvConn = kInvalidConnectionHandle;
        cliNet.SetOnConnected([&](const ConnectionInfo& info, const std::string&) {
            srvConn = info.Handle;
        });
        cliNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            cliBridge.HandleNetworkMessage(ev, cliScene, cliNet);
        });

        EXPECT_TRUE(cliNet.Connect(Endpoint{ "127.0.0.1", 27161 }).IsSuccess(), "Connecting client for prediction test");

        for (int i = 0; i < 160; ++i)
        {
            serverSession.StepFixedTick();
            cliNet.Poll();
            if (srvConn.IsValid() && cliBridge.GetLocalOwnedNetworkId().IsValid())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        const NetworkId localNetId = cliBridge.GetLocalOwnedNetworkId();
        EXPECT_TRUE(localNetId.IsValid(), "Client identified owned NetworkId automatically");
        const Scene::EntityId localCliEid = cliBridge.ResolveSceneEntity(localNetId);

        // 1. Immediate prediction: send 10 inputs -> client moves immediately BEFORE server RTT completes!
        for (int i = 0; i < 10; ++i)
        {
            PlayerInputCommand cmd{};
            cmd.MoveX = 1.0f;
            EXPECT_TRUE(cliBridge.ClientPredictAndSendInput(1.0f / 60.0f, cmd, cliScene, nullptr, cliNet, srvConn),
                "Predicted & queued local input");
        }
        const f32 predictedXImmediate = cliScene.Transforms().TryGet(localCliEid)->Position.x;
        EXPECT_TRUE(predictedXImmediate > 0.9f && cliBridge.GetClientInputHistory().GetCount() == 10,
            "Local player moved immediately (~1.0m) without waiting for server RTT");

        // 2. Pump under latency/loss until server catches up and confirms inputs
        for (int i = 0; i < 120; ++i)
        {
            serverSession.StepFixedTick();
            cliNet.Poll();
            cliBridge.TickClientPredictionAndInterpolation(1.0f / 60.0f, cliScene, cliNet, srvConn);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        EXPECT_TRUE(cliBridge.GetPredictionStats().ReconciliationsPerformed > 0,
            "Server reconciliation ran against authoritative snapshots");

        // 3. Inject server-side nudge (0.6m < SnapThreshold 1.5m) -> verify smooth visual correction offset
        const Scene::EntityId srvEid = serverSession.GetReplicationBridge().ResolveSceneEntity(localNetId);
        if (auto* srvTf = serverSession.GetScene().Transforms().TryGet(srvEid))
        {
            srvTf->Position.x -= 0.6f;
        }
        bool observedSmoothCorrection = false;
        for (int i = 0; i < 60; ++i)
        {
            serverSession.StepFixedTick();
            cliNet.Poll();
            if (Math::Length(cliBridge.GetVisualCorrectionOffset()) > 0.05f)
            {
                observedSmoothCorrection = true;
            }
            cliBridge.TickClientPredictionAndInterpolation(1.0f / 60.0f, cliScene, cliNet, srvConn);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        EXPECT_TRUE(observedSmoothCorrection && cliBridge.GetPredictionStats().CorrectionsApplied > 0 &&
                    cliBridge.GetPredictionStats().LargeSnaps == 0,
            "Moderate misprediction applied smooth visual correction without hard snap");

        // 4. Inject large server teleport (5.0m > SnapThreshold 1.5m) -> verify immediate snap!
        if (auto* srvTf = serverSession.GetScene().Transforms().TryGet(srvEid))
        {
            srvTf->Position.x += 5.0f;
        }
        for (int i = 0; i < 60; ++i)
        {
            serverSession.StepFixedTick();
            cliNet.Poll();
            cliBridge.TickClientPredictionAndInterpolation(1.0f / 60.0f, cliScene, cliNet, srvConn);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        EXPECT_TRUE(cliBridge.GetPredictionStats().LargeSnaps >= 1 &&
                    Math::Length(cliBridge.GetVisualCorrectionOffset()) < 0.01f,
            "Large server teleport snapped immediately to authoritative state");

        const f32 finalSrvX = serverSession.GetScene().Transforms().TryGet(srvEid)->Position.x;
        const f32 finalCliX = cliScene.Transforms().TryGet(localCliEid)->Position.x;
        EXPECT_TRUE(std::fabs(finalSrvX - finalCliX) < 0.05f,
            "Client converged cleanly to authoritative server position (diff=" +
            std::to_string(std::fabs(finalSrvX - finalCliX)) + ")");

        cliBridge.Shutdown(cliScene, cliNet);
        cliNet.Shutdown();
        serverSession.Shutdown();
        std::cout << "[PASS] Test_ClientPredictionReconciliationAndSmoothing\n";
    }

    void Test_ClientPhysicsCharacterPredictionAndRemoteInterpolation()
    {
        SimulationLaunchConfig srvLaunch{};
        srvLaunch.Profile = RuntimeProfile::DedicatedServer;
        srvLaunch.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvLaunch.Capabilities.Scripting = false;
        srvLaunch.BindAddress = "127.0.0.1";
        srvLaunch.Port = 27162;
        srvLaunch.TickRate = 60;
        srvLaunch.Replication.Enabled = true;
        srvLaunch.Replication.AutoSpawnPlayerEntities = true;
        srvLaunch.Replication.UsePhysicsCharacterForPlayers = true;
        srvLaunch.Replication.ReplicationRateHz = 30;
        srvLaunch.Replication.PlayerMoveSpeed = 6.0f;
        srvLaunch.Replication.PlayerJumpSpeed = 7.0f;

        SimulationSession serverSession;
        EXPECT_TRUE(serverSession.Initialize(srvLaunch).IsSuccess(), "Initialized physics prediction server");

        auto buildStaticEnv = [](Scene::Scene& s, Physics::PhysicsWorld& p) {
            const Scene::EntityId floorEid = s.CreateEntity("Floor");
            s.Transforms().TryGet(floorEid)->Position = { 0.0f, -0.5f, 0.0f };
            Scene::RigidBodyComponent floorRb{};
            floorRb.MotionType = Scene::RigidBodyMotionType::Static;
            floorRb.HalfExtents = { 30.0f, 0.5f, 30.0f };
            floorRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
            s.RigidBodies().EmplaceOrReplace(floorEid, floorRb);

            const Scene::EntityId wallEid = s.CreateEntity("Wall");
            s.Transforms().TryGet(wallEid)->Position = { 5.0f, 1.0f, 0.0f };
            Scene::RigidBodyComponent wallRb{};
            wallRb.MotionType = Scene::RigidBodyMotionType::Static;
            wallRb.HalfExtents = { 0.5f, 2.0f, 10.0f };
            wallRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
            s.RigidBodies().EmplaceOrReplace(wallEid, wallRb);
            p.RebuildFromScene(s);
        };
        buildStaticEnv(serverSession.GetScene(), serverSession.GetPhysicsWorld());

        // Client A with local Jolt PhysicsWorld for CharacterVirtual prediction + remote interpolation
        Physics::PhysicsWorld cliPhysicsA(1.0f / 60.0f);
        EXPECT_TRUE(cliPhysicsA.Initialize(), "Initialized client A Jolt PhysicsWorld");
        Scene::Scene cliSceneA("PredPhysCliA");
        buildStaticEnv(cliSceneA, cliPhysicsA);

        ConnectionManager cliNetA, cliNetB;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        EXPECT_TRUE(cliNetA.Initialize(cc).IsSuccess() && cliNetB.Initialize(cc).IsSuccess(), "Initialized clients A & B");

        SceneReplicationBridge cliBridgeA, cliBridgeB;
        cliBridgeA.Initialize(false, srvLaunch.Replication, cliNetA);
        cliBridgeB.Initialize(false, srvLaunch.Replication, cliNetB);
        Scene::Scene cliSceneB("RemoteCliB");

        ClientPredictionConfig predCfgA{};
        predCfgA.EnablePrediction = true;
        predCfgA.EnableRemoteInterpolation = true;
        predCfgA.InterpolationDelaySec = 0.06;
        cliBridgeA.ConfigurePrediction(predCfgA);

        ConnectionHandle connA = kInvalidConnectionHandle, connB = kInvalidConnectionHandle;
        cliNetA.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { connA = i.Handle; });
        cliNetB.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { connB = i.Handle; });
        cliNetA.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { cliBridgeA.HandleNetworkMessage(ev, cliSceneA, cliNetA); });
        cliNetB.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { cliBridgeB.HandleNetworkMessage(ev, cliSceneB, cliNetB); });

        EXPECT_TRUE(cliNetA.Connect(Endpoint{ "127.0.0.1", 27162 }).IsSuccess() &&
                    cliNetB.Connect(Endpoint{ "127.0.0.1", 27162 }).IsSuccess(),
            "Connected clients A & B");

        for (int i = 0; i < 120; ++i)
        {
            serverSession.StepFixedTick();
            cliNetA.Poll();
            cliNetB.Poll();
            cliBridgeA.TickClientPredictionAndInterpolation(1.0f / 60.0f, cliSceneA, cliNetA, connA, &cliPhysicsA);
            if (cliBridgeA.GetLocalOwnedNetworkId().IsValid() &&
                cliBridgeB.GetLocalOwnedNetworkId().IsValid() &&
                cliBridgeA.GetClientApplier().GetEntityCount() == 2)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const NetworkId netA = cliBridgeA.GetLocalOwnedNetworkId();
        const NetworkId netB = cliBridgeB.GetLocalOwnedNetworkId();
        EXPECT_TRUE(netA.IsValid() && netB.IsValid() && netA != netB,
            "Both clients resolved distinct owned player NetworkIds");

        // Drive Client A (+X toward static wall at x=5.0) and Client B (+Z) simultaneously
        for (int step = 0; step < 75; ++step)
        {
            PlayerInputCommand cmdA{};
            cmdA.MoveX = 1.0f;
            (void)cliBridgeA.ClientPredictAndSendInput(1.0f / 60.0f, cmdA, cliSceneA, &cliPhysicsA, cliNetA, connA);

            PlayerInputCommand cmdB{};
            cmdB.MoveZ = 1.0f;
            (void)cliBridgeB.ClientPredictAndSendInput(1.0f / 60.0f, cmdB, cliSceneB, nullptr, cliNetB, connB);

            serverSession.StepFixedTick();
            cliNetA.Poll();
            cliNetB.Poll();
            cliBridgeA.TickClientPredictionAndInterpolation(1.0f / 60.0f, cliSceneA, cliNetA, connA, &cliPhysicsA);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const Scene::EntityId eidAOnCliA = cliBridgeA.ResolveSceneEntity(netA);
        const Scene::EntityId eidBOnCliA = cliBridgeA.ResolveSceneEntity(netB);
        const f32 posAX = cliSceneA.Transforms().TryGet(eidAOnCliA)->Position.x;
        const f32 posBZOnCliA = cliSceneA.Transforms().TryGet(eidBOnCliA)->Position.z;

        EXPECT_TRUE(posAX > 2.0f && posAX < 4.7f,
            "Client A Jolt CharacterVirtual predicted movement and stopped cleanly at static wall (x=" + std::to_string(posAX) + ")");
        EXPECT_TRUE(cliBridgeA.GetRemoteInterpolation().GetSampleCount(netB) >= 2 && posBZOnCliA > 0.5f,
            "Client A interpolated remote player B smoothly via RemoteInterpolationBuffer (z=" + std::to_string(posBZOnCliA) + ")");

        // Verify PrepareClientForReconnect clears history and interpolation buffers cleanly
        cliBridgeA.PrepareClientForReconnect();
        EXPECT_TRUE(cliBridgeA.GetClientInputHistory().IsEmpty() &&
                    cliBridgeA.GetRemoteInterpolation().GetSampleCount(netB) == 0,
            "PrepareClientForReconnect cleared input history and remote interpolation cleanly");

        cliBridgeA.Shutdown(cliSceneA, cliNetA);
        cliBridgeB.Shutdown(cliSceneB, cliNetB);
        cliNetA.Shutdown();
        cliNetB.Shutdown();
        cliPhysicsA.Shutdown();
        serverSession.Shutdown();
        std::cout << "[PASS] Test_ClientPhysicsCharacterPredictionAndRemoteInterpolation\n";
    }

    void Test_MultiWorldInstanceIsolationAndPhysics()
    {
        SimulationLaunchConfig srvLaunch{};
        srvLaunch.Profile = RuntimeProfile::DedicatedServer;
        srvLaunch.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvLaunch.Capabilities.Scripting = false;
        srvLaunch.BindAddress = "127.0.0.1";
        srvLaunch.Port = 27171;
        srvLaunch.TickRate = 60;
        srvLaunch.Replication.WorldId = 1;
        srvLaunch.Replication.Enabled = true;
        srvLaunch.Replication.AutoSpawnPlayerEntities = true;
        srvLaunch.Replication.ReplicationRateHz = 30;

        SimulationSession serverSession;
        EXPECT_TRUE(serverSession.Initialize(srvLaunch).IsSuccess(), "Initialized multi-world SimulationSession");

        // Create World 2 & World 3 inside the same SimulationSession
        WorldInstanceDesc w2Desc{};
        w2Desc.WorldId = 2;
        w2Desc.Name = "ArenaWorld2";
        EXPECT_TRUE(serverSession.CreateWorldInstance(w2Desc).IsSuccess(), "Created WorldInstance 2");

        WorldInstanceDesc w3Desc{};
        w3Desc.WorldId = 3;
        w3Desc.Name = "DungeonWorld3";
        EXPECT_TRUE(serverSession.CreateWorldInstance(w3Desc).IsSuccess(), "Created WorldInstance 3");
        EXPECT_TRUE(serverSession.GetWorldIds().size() == 3 &&
                    serverSession.HasWorldInstance(1) &&
                    serverSession.HasWorldInstance(2) &&
                    serverSession.HasWorldInstance(3),
            "All 3 isolated WorldInstances active");

        // Duplicate WorldId rejected cleanly
        EXPECT_TRUE(!serverSession.CreateWorldInstance(w2Desc).IsSuccess(), "Duplicate WorldId rejected");

        // Setup isolated physics floor & dynamic prop in World 1 (floor y=-0.5) and World 2 (floor y=9.5)
        auto setupWorldPhysics = [&](Network::WorldInstanceId wid, f32 floorY, const std::string& propName, f32 propStartY) {
            Scene::Scene* s = serverSession.TryGetWorldScene(wid);
            Physics::PhysicsWorld* p = serverSession.TryGetWorldPhysics(wid);
            SceneReplicationBridge* b = serverSession.TryGetWorldReplicationBridge(wid);
            EXPECT_TRUE(s != nullptr && p != nullptr && b != nullptr, "Resolved world Scene, Physics, Bridge");

            const Scene::EntityId fl = s->CreateEntity("Floor_W" + std::to_string(wid));
            s->Transforms().TryGet(fl)->Position = { 0.0f, floorY, 0.0f };
            Scene::RigidBodyComponent flRb{};
            flRb.MotionType = Scene::RigidBodyMotionType::Static;
            flRb.HalfExtents = { 20.0f, 0.5f, 20.0f };
            flRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
            s->RigidBodies().EmplaceOrReplace(fl, flRb);
            p->RebuildFromScene(*s);

            ReplicatedEntitySpawnDesc propDesc{};
            propDesc.WorldId = wid;
            propDesc.EntityName = propName;
            propDesc.ComponentMask = SnapshotComponentFlags::kTransform | SnapshotComponentFlags::kPhysics;
            propDesc.InitialTransform.PosX = 5.0f;
            propDesc.InitialTransform.PosY = propStartY;
            const NetworkId netId = b->SpawnReplicatedEntity(*s, serverSession.GetNetworkManager().GetSessionRegistry(), propDesc, 0);
            const Scene::EntityId eid = b->ResolveSceneEntity(netId);
            Scene::RigidBodyComponent dynRb{};
            dynRb.MotionType = Scene::RigidBodyMotionType::Dynamic;
            dynRb.HalfExtents = { 0.5f, 0.5f, 0.5f };
            dynRb.DirtyFlags = static_cast<u8>(Scene::RigidBodyDirtyFlags::All);
            s->RigidBodies().EmplaceOrReplace(eid, dynRb);
            EXPECT_TRUE(p->SyncEntityBody(*s, eid), "Synced dynamic prop into world physics");
            return netId;
        };

        const NetworkId propW1 = setupWorldPhysics(1, -0.5f, "World1Prop", 2.0f);
        const NetworkId propW2 = setupWorldPhysics(2, 9.5f, "World2Prop", 12.0f);
        const NetworkId propW3 = setupWorldPhysics(3, 19.5f, "World3Prop", 22.0f);

        // Connect Client A (stays in World 1) and Client B (transfers to World 2)
        ConnectionManager cliNetA, cliNetB;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        EXPECT_TRUE(cliNetA.Initialize(cc).IsSuccess() && cliNetB.Initialize(cc).IsSuccess(), "Initialized clients A & B");

        Scene::Scene sceneA("CliA_W1"), sceneB("CliB_W2");
        SceneReplicationBridge bridgeA, bridgeB;
        bridgeA.Initialize(false, srvLaunch.Replication, cliNetA);
        bridgeB.Initialize(false, srvLaunch.Replication, cliNetB);

        ConnectionHandle connA = kInvalidConnectionHandle, connB = kInvalidConnectionHandle;
        SessionId sessA{}, sessB{};
        cliNetA.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { connA = i.Handle; sessA = i.Session; });
        cliNetB.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { connB = i.Handle; sessB = i.Session; });
        cliNetA.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { bridgeA.HandleNetworkMessage(ev, sceneA, cliNetA); });
        cliNetB.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { bridgeB.HandleNetworkMessage(ev, sceneB, cliNetB); });

        EXPECT_TRUE(cliNetA.Connect(Endpoint{ "127.0.0.1", 27171 }).IsSuccess() &&
                    cliNetB.Connect(Endpoint{ "127.0.0.1", 27171 }).IsSuccess(),
            "Connected both clients");

        for (int i = 0; i < 100; ++i)
        {
            serverSession.StepFixedTick();
            cliNetA.Poll();
            cliNetB.Poll();
            if (sessA.IsValid() && sessB.IsValid() && bridgeB.GetClientApplier().GetActiveWorldId() == 1)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(sessA.IsValid() && sessB.IsValid(), "Both clients authenticated");

        // Transfer Client B from World 1 -> World 2
        EXPECT_TRUE(serverSession.TransferSessionToWorld(sessB, 2), "Transferred Client B to World 2");
        EXPECT_TRUE(serverSession.GetSessionWorldId(sessA) == 1 && serverSession.GetSessionWorldId(sessB) == 2,
            "Session world assignments isolated (A=1, B=2)");

        bool isolationVerified = false;
        for (int i = 0; i < 120; ++i)
        {
            serverSession.StepFixedTick();
            cliNetA.Poll();
            cliNetB.Poll();

            ClientReplicatedEntity checkProp1OnA{}, checkProp2OnB{};
            const bool aHas1 = bridgeA.GetClientApplier().TryGetEntity(propW1, checkProp1OnA);
            const bool bHas2 = bridgeB.GetClientApplier().TryGetEntity(propW2, checkProp2OnB);

            // Ensure Client A never sees World2Prop and Client B never sees World1Prop or World3Prop
            bool leakDetected = false;
            for (const auto& [p, ent] : bridgeA.GetClientApplier().GetEntities())
            {
                (void)p;
                if (ent.WorldId != 1 || ent.EntityName == "World2Prop" || ent.EntityName == "World3Prop")
                {
                    leakDetected = true;
                }
            }
            for (const auto& [p, ent] : bridgeB.GetClientApplier().GetEntities())
            {
                (void)p;
                if (ent.WorldId != 2 || ent.EntityName == "World1Prop" || ent.EntityName == "World3Prop")
                {
                    leakDetected = true;
                }
            }

            if (aHas1 && bHas2 && !leakDetected &&
                bridgeA.GetClientApplier().GetActiveWorldId() == 1 &&
                bridgeB.GetClientApplier().GetActiveWorldId() == 2 &&
                checkProp1OnA.Transform.PosY < 1.2f &&
                checkProp2OnB.Transform.PosY > 9.8f && checkProp2OnB.Transform.PosY < 11.2f)
            {
                isolationVerified = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(isolationVerified, "Worlds 1, 2, 3 simulated physics independently with zero cross-world entity leakage");
        (void)propW3;

        bridgeA.Shutdown(sceneA, cliNetA);
        bridgeB.Shutdown(sceneB, cliNetB);
        cliNetA.Shutdown();
        cliNetB.Shutdown();
        serverSession.Shutdown();
        std::cout << "[PASS] Test_MultiWorldInstanceIsolationAndPhysics\n";
    }

    void Test_MultiWorldSessionTransferAndWorldDestruction()
    {
        SimulationLaunchConfig srvLaunch{};
        srvLaunch.Profile = RuntimeProfile::DedicatedServer;
        srvLaunch.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvLaunch.Capabilities.Physics = false;
        srvLaunch.Capabilities.Scripting = false;
        srvLaunch.BindAddress = "127.0.0.1";
        srvLaunch.Port = 27172;
        srvLaunch.TickRate = 60;
        srvLaunch.Replication.WorldId = 1;
        srvLaunch.Replication.Enabled = true;
        srvLaunch.Replication.AutoSpawnPlayerEntities = true;
        srvLaunch.Replication.ReplicationRateHz = 30;

        SimulationSession serverSession;
        EXPECT_TRUE(serverSession.Initialize(srvLaunch).IsSuccess(), "Initialized multi-world transfer test server");
        EXPECT_TRUE(serverSession.CreateWorldInstance(WorldInstanceDesc{ 2, "HubWorld", {}, std::nullopt }).IsSuccess(),
            "Created World 2");
        EXPECT_TRUE(serverSession.CreateWorldInstance(WorldInstanceDesc{ 3, "BossWorld", {}, std::nullopt }).IsSuccess(),
            "Created World 3");

        // Spawn distinct marker entity in each world
        auto spawnMarker = [&](Network::WorldInstanceId wid, const std::string& name) {
            ReplicatedEntitySpawnDesc d{};
            d.WorldId = wid;
            d.EntityName = name;
            return serverSession.TryGetWorldReplicationBridge(wid)->SpawnReplicatedEntity(
                *serverSession.TryGetWorldScene(wid),
                serverSession.GetNetworkManager().GetSessionRegistry(),
                d,
                0);
        };
        const NetworkId m1 = spawnMarker(1, "Marker_W1");
        const NetworkId m2 = spawnMarker(2, "Marker_W2");
        const NetworkId m3 = spawnMarker(3, "Marker_W3");

        ConnectionManager cliNet;
        ConnectionConfig cc{};
        cc.Mode = NetworkMode::Client;
        EXPECT_TRUE(cliNet.Initialize(cc).IsSuccess(), "Initialized client for world transfers");

        Scene::Scene cliScene("TransferCli");
        SceneReplicationBridge cliBridge;
        cliBridge.Initialize(false, srvLaunch.Replication, cliNet);
        SessionId cliSession{};
        cliNet.SetOnConnected([&](const ConnectionInfo& i, const std::string&) { cliSession = i.Session; });
        cliNet.SetOnMessageReceived([&](const NetworkMessageEvent& ev) { cliBridge.HandleNetworkMessage(ev, cliScene, cliNet); });

        EXPECT_TRUE(cliNet.Connect(Endpoint{ "127.0.0.1", 27172 }).IsSuccess(), "Connected client");

        auto pumpUntilWorld = [&](Network::WorldInstanceId expectedWid, NetworkId expectedMarker) -> bool {
            for (int i = 0; i < 100; ++i)
            {
                serverSession.StepFixedTick();
                cliNet.Poll();
                ClientReplicatedEntity dummy{};
                if (cliSession.IsValid() &&
                    cliBridge.GetClientApplier().GetActiveWorldId() == expectedWid &&
                    cliBridge.GetClientApplier().TryGetEntity(expectedMarker, dummy) &&
                    cliBridge.GetClientApplier().GetEntityCount() == 2) // marker + player
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return false;
        };

        EXPECT_TRUE(pumpUntilWorld(1, m1), "Client entered Default World 1 cleanly");

        // Transfer 1 -> 2
        EXPECT_TRUE(serverSession.TransferSessionToWorld(cliSession, 2) && pumpUntilWorld(2, m2),
            "Client transferred cleanly from World 1 to World 2 (old entities despawned)");

        // Transfer 2 -> 3
        EXPECT_TRUE(serverSession.TransferSessionToWorld(cliSession, 3) && pumpUntilWorld(3, m3),
            "Client transferred cleanly from World 2 to World 3");

        // Cannot destroy default world 1
        EXPECT_TRUE(!serverSession.DestroyWorldInstance(1), "Refused destroying default World 1");

        // Destroy World 3 while client is inside it -> must migrate client safely back to World 1!
        EXPECT_TRUE(serverSession.DestroyWorldInstance(3) && !serverSession.HasWorldInstance(3),
            "Destroyed World 3 cleanly");
        EXPECT_TRUE(serverSession.GetSessionWorldId(cliSession) == 1 && pumpUntilWorld(1, m1),
            "Client migrated safely back to Default World 1 when World 3 was destroyed");

        cliBridge.Shutdown(cliScene, cliNet);
        cliNet.Shutdown();
        serverSession.Shutdown();
        std::cout << "[PASS] Test_MultiWorldSessionTransferAndWorldDestruction\n";
    }

    u32 ComputeManagedHash32(std::string_view value) noexcept
    {
        if (value.empty())
        {
            return 1u;
        }
        u64 hash = 14695981039346656037ull;
        for (const char ch : value)
        {
            hash ^= static_cast<u8>(ch);
            hash *= 1099511628211ull;
        }
        const u32 h32 = static_cast<u32>(hash ^ (hash >> 32));
        return h32 == 0u ? 1u : h32;
    }

    void Test_ManagedNetworkWireContractsAndBounds()
    {
        ManagedValue nanVal{};
        nanVal.Type = ManagedValueType::Float32;
        nanVal.FloatVal = std::numeric_limits<f32>::quiet_NaN();
        EXPECT_TRUE(!nanVal.IsValid(), "NaN float ManagedValue must be rejected");

        ManagedValue longStr{};
        longStr.Type = ManagedValueType::String;
        longStr.StringVal.assign(ReplicationConstants::kMaxManagedStringBytes + 1, 'x');
        EXPECT_TRUE(!longStr.IsValid(), "Oversized string ManagedValue must be rejected");

        ManagedPropertySyncPacket propPkt{};
        propPkt.WireVersion = ReplicationConstants::kManagedWireVersion;
        propPkt.WorldId = 2;
        propPkt.TargetNetId = NetworkId{ 7, 3 };
        propPkt.BehaviourTypeHash = ComputeManagedHash32("TestPlayerBehaviour");
        propPkt.Properties.push_back(ManagedPropertyEntry{ 1u, ManagedValue{ .Type = ManagedValueType::Int32, .IntVal = -42 } });
        propPkt.Properties.push_back(ManagedPropertyEntry{ 2u, ManagedValue{ .Type = ManagedValueType::Float32, .FloatVal = 3.25f } });
        propPkt.Properties.push_back(ManagedPropertyEntry{ 3u, ManagedValue{ .Type = ManagedValueType::Boolean, .BoolVal = true } });
        propPkt.Properties.push_back(ManagedPropertyEntry{ 4u, ManagedValue{ .Type = ManagedValueType::Vec3, .VecX = 1.0f, .VecY = -2.0f, .VecZ = 3.5f } });
        propPkt.Properties.push_back(ManagedPropertyEntry{ 5u, ManagedValue{ .Type = ManagedValueType::String, .StringVal = "Morbi" } });
        propPkt.Properties.push_back(ManagedPropertyEntry{ 6u, ManagedValue{ .Type = ManagedValueType::UInt64, .UInt64Val = 9001ull } });

        BitWriter pw(256);
        EXPECT_TRUE(ReplicationCodec::WriteManagedPropertySync(pw, propPkt), "WriteManagedPropertySync should succeed");
        ManagedPropertySyncPacket decodedProp{};
        BitReader pr(pw.GetData());
        EXPECT_TRUE(ReplicationCodec::ReadManagedPropertySync(pr, decodedProp), "ReadManagedPropertySync should succeed");
        EXPECT_TRUE(decodedProp.WorldId == 2 && decodedProp.TargetNetId == propPkt.TargetNetId, "WorldId and TargetNetId preserved");
        EXPECT_TRUE(decodedProp.Properties.size() == 6, "All 6 primitive types round-tripped");
        EXPECT_TRUE(decodedProp.Properties[4].Value.StringVal == "Morbi", "String property round-tripped");
        EXPECT_TRUE(decodedProp.Properties[5].Value.UInt64Val == 9001ull, "UInt64 property round-tripped");

        ManagedPropertySyncPacket tooManyProps = propPkt;
        tooManyProps.Properties.resize(ReplicationConstants::kMaxManagedPropertiesPerSync + 1, propPkt.Properties.front());
        BitWriter badPw(256);
        EXPECT_TRUE(!ReplicationCodec::WriteManagedPropertySync(badPw, tooManyProps), "Oversized property batch must be rejected");

        ManagedRpcPacket rpcPkt{};
        rpcPkt.WireVersion = ReplicationConstants::kManagedWireVersion;
        rpcPkt.WorldId = 1;
        rpcPkt.TargetNetId = NetworkId{ 9, 1 };
        rpcPkt.BehaviourTypeHash = ComputeManagedHash32("TestPlayerBehaviour");
        rpcPkt.MethodHash = ComputeManagedHash32("TakeDamage");
        rpcPkt.RequireOwnership = true;
        rpcPkt.TargetOwnerOnly = false;
        rpcPkt.Args.push_back(ManagedValue{ .Type = ManagedValueType::Int32, .IntVal = 25 });
        rpcPkt.Args.push_back(ManagedValue{ .Type = ManagedValueType::String, .StringVal = "sword" });

        BitWriter rw(128);
        EXPECT_TRUE(ReplicationCodec::WriteManagedRpc(rw, rpcPkt), "WriteManagedRpc should succeed");
        ManagedRpcPacket decodedRpc{};
        BitReader rr(rw.GetData());
        EXPECT_TRUE(ReplicationCodec::ReadManagedRpc(rr, decodedRpc), "ReadManagedRpc should succeed");
        EXPECT_TRUE(decodedRpc.RequireOwnership && !decodedRpc.TargetOwnerOnly, "RPC flags preserved");
        EXPECT_TRUE(decodedRpc.Args.size() == 2 && decodedRpc.Args[0].IntVal == 25 && decodedRpc.Args[1].StringVal == "sword",
            "RPC args round-tripped");

        ManagedRpcPacket tooManyArgs = rpcPkt;
        tooManyArgs.Args.resize(ReplicationConstants::kMaxManagedRpcArgs + 1, rpcPkt.Args.front());
        BitWriter badRw(128);
        EXPECT_TRUE(!ReplicationCodec::WriteManagedRpc(badRw, tooManyArgs), "Oversized RPC args must be rejected");
        std::cout << "[PASS] Test_ManagedNetworkWireContractsAndBounds\n";
    }

    void Test_CSharpNetworkBehaviourLifecyclePropertiesAndRpcs()
    {
        const std::filesystem::path tempProj = std::filesystem::temp_directory_path() / "morbi_phase8_test_1";
        std::error_code ec;
        std::filesystem::remove_all(tempProj, ec);
        std::filesystem::create_directories(tempProj / "Scripts", ec);

        {
            std::ofstream cs(tempProj / "Scripts" / "Phase8TestScripts.cs");
            cs << R"CS(
using System;
using Morbi.Engine;

[EntityScript]
public sealed class BadReplicatedBehaviour : NetworkBehaviour
{
    [Replicated]
    public object BadProp { get; set; } = new object();
}

[EntityScript]
public sealed class TestPlayerBehaviour : NetworkBehaviour
{
    public static int SpawnCount = 0;
    public static int DespawnCount = 0;
    public static bool BadTypeRejected = false;
    public static NetworkObject? LastSpawnedObj = null;

    [Replicated] public int Health { get; set; } = 100;
    [Replicated] public float Speed { get; set; } = 5.5f;
    [Replicated] public bool IsAlive { get; set; } = true;
    [Replicated] public Vec3 SpawnPos { get; set; } = new Vec3(1.0f, 2.0f, 3.0f);
    [Replicated] public string Tag { get; set; } = "Hero";
    [Replicated] public ulong Score { get; set; } = 42UL;

    public override void OnNetworkSpawn()
    {
        SpawnCount++;
        LastSpawnedObj = NetworkObject;
        if (BadTypeRejected)
        {
            Score = 9001UL;
        }
    }

    public override void OnNetworkDespawn()
    {
        DespawnCount++;
    }

    [ServerRpc(RequireOwnership = true)]
    public void TakeDamage(int amount, string reason)
    {
        Health -= amount;
        InvokeClientRpc(nameof(NotifyHit), reason, Health);
    }

    [ClientRpc]
    public void NotifyHit(string reason, int newHp)
    {
    }
}

public sealed class Phase8TestBootstrap : Script
{
    public override void OnStart()
    {
        try
        {
            Entity badEnt = Scene.CreateEntity("BadPropProbe");
            badEnt.AttachScript<BadReplicatedBehaviour>();
        }
        catch (Exception ex) when (ex is NotSupportedException || ex.InnerException is NotSupportedException)
        {
            TestPlayerBehaviour.BadTypeRejected = true;
        }
    }

    public override void OnUpdate(float dt)
    {
        Entity? player = Scene.FindEntity("NetTestPlayer");
        if (player != null && player.GetScript<TestPlayerBehaviour>() == null)
        {
            player.AttachScript<TestPlayerBehaviour>();
        }
    }
}
)CS";
        }

        const u16 port = 27281;
        SimulationLaunchConfig srvCfg{};
        srvCfg.Profile = RuntimeProfile::DedicatedServer;
        srvCfg.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvCfg.Capabilities.Scripting = true;
        srvCfg.Capabilities.Physics = false;
        srvCfg.BindAddress = "127.0.0.1";
        srvCfg.Port = port;
        srvCfg.TickRate = 60;
        srvCfg.ScriptsDir = tempProj / "Scripts";
        srvCfg.Replication.WorldId = 1;
        srvCfg.Replication.Enabled = true;
        srvCfg.Replication.AutoSpawnPlayerEntities = false;

        SimulationSession server;
        auto initRes = server.Initialize(srvCfg);
        EXPECT_TRUE(initRes.IsSuccess(), "Server SimulationSession with C# scripting must initialize: " +
            (initRes ? std::string{} : initRes.Error()));
        if (!initRes.IsSuccess())
        {
            std::filesystem::remove_all(tempProj, ec);
            return;
        }

        auto makeClient = [&](ConnectionManager& outNet, ConnectionHandle& outConn) {
            ConnectionConfig cfg{};
            cfg.Mode = NetworkMode::Client;
            EXPECT_TRUE(outNet.Initialize(cfg).IsSuccess(), "Client ConnectionManager init must succeed");
            auto cr = outNet.Connect(Endpoint{ "127.0.0.1", port });
            EXPECT_TRUE(cr.IsSuccess(), "Client Connect must succeed");
            outConn = cr.Value();
        };

        ConnectionManager clientA;
        ConnectionHandle connA = kInvalidConnectionHandle;
        makeClient(clientA, connA);

        std::vector<ManagedPropertySyncPacket> clientAProps;
        std::vector<ManagedRpcPacket> clientAClientRpcs;
        clientA.SetOnMessageReceived([&](const NetworkMessageEvent& ev) {
            if (ev.Header.Category == MessageCategory::Replication &&
                static_cast<ReplicationMessageSubType>(ev.Header.SubType) == ReplicationMessageSubType::ManagedPropertySync)
            {
                BitReader r(ev.Payload);
                ManagedPropertySyncPacket p{};
                if (ReplicationCodec::ReadManagedPropertySync(r, p))
                {
                    clientAProps.push_back(std::move(p));
                }
            }
            else if (ev.Header.Category == MessageCategory::Rpc)
            {
                BitReader r(ev.Payload);
                RpcInvocationEnvelope env{};
                if (RpcRouter::ReadInvocation(r, env) && env.RpcId == BuiltinRpcIds::kManagedClientRpc)
                {
                    BitReader pr(env.Payload);
                    ManagedRpcPacket rpc{};
                    if (ReplicationCodec::ReadManagedRpc(pr, rpc))
                    {
                        clientAClientRpcs.push_back(std::move(rpc));
                    }
                }
            }
        });

        ConnectionInfo ciA{};
        SessionId aliceSession = kInvalidSessionId;
        PlayerId alicePlayerId = kInvalidPlayerId;
        for (int i = 0; i < 120; ++i)
        {
            server.StepFixedTick();
            clientA.Poll();
            const auto conns = server.GetNetworkManager().GetConnections();
            if (!conns.empty() &&
                server.GetNetworkManager().TryGetConnectionInfo(conns.front(), ciA) &&
                ciA.Session.IsValid() &&
                ciA.AuthenticatedPlayerId != kInvalidPlayerId)
            {
                aliceSession = ciA.Session;
                alicePlayerId = ciA.AuthenticatedPlayerId;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(aliceSession.IsValid() && alicePlayerId != kInvalidPlayerId, "Alice session must be valid");

        ConnectionManager clientB;
        ConnectionHandle connB = kInvalidConnectionHandle;
        makeClient(clientB, connB);
        for (int i = 0; i < 120; ++i)
        {
            server.StepFixedTick();
            clientA.Poll();
            clientB.Poll();
            const auto conns2 = server.GetNetworkManager().GetConnections();
            if (conns2.size() == 2)
            {
                ConnectionInfo ciB{};
                if (server.GetNetworkManager().TryGetConnectionInfo(conns2[1], ciB) && ciB.Session.IsValid())
                {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(server.GetNetworkManager().GetConnectionCount() == 2, "ClientB must connect");

        ReplicatedEntitySpawnDesc spawnDesc{};
        spawnDesc.ArchetypeHash = 1;
        spawnDesc.OwnerSession = aliceSession;
        spawnDesc.OwnerPlayer = alicePlayerId;
        spawnDesc.WorldId = 1;
        spawnDesc.Authority = NetworkAuthority::ClientPredicted;
        spawnDesc.Scope = ReplicationScope::AllRelevant;
        spawnDesc.EntityName = "NetTestPlayer";
        spawnDesc.ComponentMask = SnapshotComponentFlags::kTransform;

        const NetworkId playerNetId = server.GetReplicationBridge().SpawnReplicatedEntity(
            server.GetScene(),
            server.GetNetworkManager().GetSessionRegistry(),
            spawnDesc,
            server.GetTickStats().TickCount);
        EXPECT_TRUE(playerNetId.IsValid(), "NetTestPlayer must spawn");

        for (int i = 0; i < 30; ++i)
        {
            server.StepFixedTick();
            clientA.Poll();
            clientB.Poll();
            if (!clientAProps.empty())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(!clientAProps.empty(), "ClientA must receive initial ManagedPropertySync");
        if (!clientAProps.empty())
        {
            const auto& props = clientAProps.back().Properties;
            EXPECT_TRUE(props.size() == 6, "Initial sync must contain all 6 replicated properties");
            bool sawScore9001 = false;
            bool sawTagHero = false;
            for (const auto& entry : props)
            {
                if (entry.Value.Type == ManagedValueType::UInt64 && entry.Value.UInt64Val == 9001ull)
                {
                    sawScore9001 = true;
                }
                if (entry.Value.Type == ManagedValueType::String && entry.Value.StringVal == "Hero")
                {
                    sawTagHero = true;
                }
            }
            EXPECT_TRUE(sawScore9001, "Unsupported [Replicated] type must throw NotSupportedException (probed via Score=9001)");
            EXPECT_TRUE(sawTagHero, "String [Replicated] property must replicate");
        }

        const u32 behaviourHash = ComputeManagedHash32("TestPlayerBehaviour");
        const u32 takeDamageHash = ComputeManagedHash32("TakeDamage");
        auto sendTakeDamageRpc = [&](ConnectionManager& cli, ConnectionHandle h, WorldInstanceId worldId, i32 dmg, const std::string& reason) {
            ManagedRpcPacket pkt{};
            pkt.WireVersion = ReplicationConstants::kManagedWireVersion;
            pkt.WorldId = worldId;
            pkt.TargetNetId = playerNetId;
            pkt.BehaviourTypeHash = behaviourHash;
            pkt.MethodHash = takeDamageHash;
            pkt.RequireOwnership = true;
            pkt.TargetOwnerOnly = false;
            pkt.Args.push_back(ManagedValue{ .Type = ManagedValueType::Int32, .IntVal = dmg });
            pkt.Args.push_back(ManagedValue{ .Type = ManagedValueType::String, .StringVal = reason });

            BitWriter pw(128);
            EXPECT_TRUE(ReplicationCodec::WriteManagedRpc(pw, pkt), "WriteManagedRpc must succeed");
            RpcInvocationEnvelope env{};
            env.RpcId = BuiltinRpcIds::kManagedServerRpc;
            env.TargetEntity = playerNetId;
            env.TargetWorldId = worldId;
            const auto sp = pw.GetData();
            env.Payload.assign(sp.begin(), sp.end());

            BitWriter ew(160);
            EXPECT_TRUE(RpcRouter::WriteInvocation(ew, env), "WriteInvocation must succeed");
            EXPECT_TRUE(cli.SendPacket(h, MessageCategory::Rpc, 0, ew.GetData(), NetDelivery::ReliableOrdered, NetChannel::GameplayEvents).Succeeded(),
                "Client RPC send must queue");
        };

        ConnectionManager* ownerCli = &clientA;
        ConnectionHandle ownerHandle = connA;
        ConnectionManager* nonOwnerCli = &clientB;
        ConnectionHandle nonOwnerHandle = connB;

        // 1. Non-owner attempts TakeDamage -> must be rejected by server ownership check
        const size_t propsBeforeCheat = clientAProps.size();
        sendTakeDamageRpc(*nonOwnerCli, nonOwnerHandle, 1, 50, "cheat");
        for (int i = 0; i < 15; ++i)
        {
            server.StepFixedTick();
            clientA.Poll();
            clientB.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(clientAProps.size() == propsBeforeCheat && clientAClientRpcs.empty(),
            "Non-owner [ServerRpc] must be rejected by server authority");

        // 2. Owner attempts TakeDamage(25, "sword") -> must succeed, update Health=75, and trigger ClientRpc NotifyHit("sword", 75)
        sendTakeDamageRpc(*ownerCli, ownerHandle, 1, 25, "sword");
        for (int i = 0; i < 30; ++i)
        {
            server.StepFixedTick();
            clientA.Poll();
            clientB.Poll();
            if (!clientAClientRpcs.empty() && clientAProps.size() > propsBeforeCheat)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(clientAProps.size() > propsBeforeCheat, "Owner [ServerRpc] must mutate [Replicated] Health and replicate delta");
        if (clientAProps.size() > propsBeforeCheat)
        {
            const auto& deltaProps = clientAProps.back().Properties;
            EXPECT_TRUE(deltaProps.size() == 1 && deltaProps[0].Value.Type == ManagedValueType::Int32 && deltaProps[0].Value.IntVal == 75,
                "Dirty property sync must send Health=75");
        }
        EXPECT_TRUE(!clientAClientRpcs.empty(), "Server must dispatch [ClientRpc] NotifyHit to connected client");
        if (!clientAClientRpcs.empty())
        {
            const auto& crpc = clientAClientRpcs.back();
            EXPECT_TRUE(crpc.MethodHash == ComputeManagedHash32("NotifyHit"), "ClientRpc MethodHash must match NotifyHit");
            EXPECT_TRUE(crpc.Args.size() == 2 && crpc.Args[0].StringVal == "sword" && crpc.Args[1].IntVal == 75,
                "ClientRpc args must match ('sword', 75)");
        }

        EXPECT_TRUE(server.GetReplicationBridge().DespawnReplicatedEntity(
            server.GetScene(),
            server.GetNetworkManager().GetSessionRegistry(),
            playerNetId), "DespawnReplicatedEntity must succeed");
        server.StepFixedTick();

        clientA.Shutdown();
        clientB.Shutdown();
        server.Shutdown();
        std::filesystem::remove_all(tempProj, ec);
        std::cout << "[PASS] Test_CSharpNetworkBehaviourLifecyclePropertiesAndRpcs\n";
    }

    void Test_CSharpMultiWorldIsolationAndCleanup()
    {
        const std::filesystem::path tempProj = std::filesystem::temp_directory_path() / "morbi_phase8_test_2";
        std::error_code ec;
        std::filesystem::remove_all(tempProj, ec);
        std::filesystem::create_directories(tempProj / "Scripts", ec);

        {
            std::ofstream cs(tempProj / "Scripts" / "MultiWorldScripts.cs");
            cs << R"CS(
using System;
using Morbi.Engine;

[EntityScript]
public sealed class WorldTagBehaviour : NetworkBehaviour
{
    public static int World1Spawns = 0;
    public static int World2Spawns = 0;
    public static int World2Despawns = 0;

    [Replicated] public ulong ReportedWorldId { get; set; } = 0UL;

    public override void OnNetworkSpawn()
    {
        ReportedWorldId = (ulong)WorldId.Value;
        if (WorldId.Value == 1) World1Spawns++;
        if (WorldId.Value == 2) World2Spawns++;
    }

    public override void OnNetworkDespawn()
    {
        if (WorldId.Value == 2) World2Despawns++;
    }

    [ClientRpc]
    public void PingWorld(int val) { }
}

public sealed class MultiWorldBootstrap : Script
{
    public override void OnUpdate(float dt)
    {
        Entity? w1 = Scene.FindEntity("World1Actor");
        if (w1 != null && w1.GetScript<WorldTagBehaviour>() == null)
        {
            w1.AttachScript<WorldTagBehaviour>();
        }
        Entity? w2 = Scene.FindEntity("World2Actor");
        if (w2 != null && w2.GetScript<WorldTagBehaviour>() == null)
        {
            w2.AttachScript<WorldTagBehaviour>();
        }
    }
}
)CS";
        }

        SimulationLaunchConfig srvCfg{};
        srvCfg.Profile = RuntimeProfile::DedicatedServer;
        srvCfg.Capabilities = RuntimeCapabilities::FromProfile(RuntimeProfile::DedicatedServer);
        srvCfg.Capabilities.Scripting = true;
        srvCfg.Capabilities.Physics = false;
        srvCfg.BindAddress = "127.0.0.1";
        srvCfg.Port = 27282;
        srvCfg.TickRate = 60;
        srvCfg.ScriptsDir = tempProj / "Scripts";
        srvCfg.Replication.WorldId = 1;
        srvCfg.Replication.Enabled = true;
        srvCfg.Replication.AutoSpawnPlayerEntities = false;

        SimulationSession server;
        auto mwInit = server.Initialize(srvCfg);
        EXPECT_TRUE(mwInit.IsSuccess(), "Multi-world SimulationSession must initialize: " +
            (mwInit ? std::string{} : mwInit.Error()));
        if (!mwInit.IsSuccess())
        {
            std::filesystem::remove_all(tempProj, ec);
            return;
        }

        WorldInstanceDesc w2Desc{};
        w2Desc.WorldId = 2;
        w2Desc.Name = "Arena2";
        EXPECT_TRUE(server.CreateWorldInstance(w2Desc).IsSuccess(), "CreateWorldInstance(2) must succeed");

        auto* bridge1 = server.TryGetWorldReplicationBridge(1);
        auto* scene1 = server.TryGetWorldScene(1);
        auto* bridge2 = server.TryGetWorldReplicationBridge(2);
        auto* scene2 = server.TryGetWorldScene(2);
        EXPECT_TRUE(bridge1 && scene1 && bridge2 && scene2, "Both worlds must exist");

        ReplicatedEntitySpawnDesc d1{};
        d1.EntityName = "World1Actor";
        d1.WorldId = 1;
        d1.ComponentMask = SnapshotComponentFlags::kTransform;
        const NetworkId net1 = bridge1->SpawnReplicatedEntity(*scene1, server.GetNetworkManager().GetSessionRegistry(), d1, 1);

        ReplicatedEntitySpawnDesc d2Dummy{};
        d2Dummy.EntityName = "World2Dummy";
        d2Dummy.WorldId = 2;
        d2Dummy.ComponentMask = SnapshotComponentFlags::kTransform;
        (void)bridge2->SpawnReplicatedEntity(*scene2, server.GetNetworkManager().GetSessionRegistry(), d2Dummy, 1);

        ReplicatedEntitySpawnDesc d2{};
        d2.EntityName = "World2Actor";
        d2.WorldId = 2;
        d2.ComponentMask = SnapshotComponentFlags::kTransform;
        const NetworkId net2 = bridge2->SpawnReplicatedEntity(*scene2, server.GetNetworkManager().GetSessionRegistry(), d2, 1);
        EXPECT_TRUE(net1.IsValid() && net2.IsValid(), "Entities in both worlds must spawn");

        server.StepFixedTick();

        const u32 typeHash = ComputeManagedHash32("WorldTagBehaviour");
        const u32 pingHash = ComputeManagedHash32("PingWorld");
        const u8 emptyArgs[1] = { 0 };
        EXPECT_TRUE(!bridge1->SendManagedRpc(1, net2, typeHash, pingHash, false, 0, emptyArgs, server.GetNetworkManager()),
            "Cross-world SendManagedRpc from World 1 to World 2 NetId must be rejected");
        EXPECT_TRUE(!bridge2->SendManagedRpc(1, net2, typeHash, pingHash, false, 0, emptyArgs, server.GetNetworkManager()),
            "Mismatched worldId in SendManagedRpc must be rejected");
        EXPECT_TRUE(bridge2->SendManagedRpc(2, net2, typeHash, pingHash, false, 0, emptyArgs, server.GetNetworkManager()),
            "Same-world SendManagedRpc in World 2 must succeed");

        EXPECT_TRUE(server.DestroyWorldInstance(2), "DestroyWorldInstance(2) must succeed");
        EXPECT_TRUE(!server.HasWorldInstance(2), "World 2 must be removed");
        server.StepFixedTick();

        EXPECT_TRUE(bridge1->SendManagedRpc(1, net1, typeHash, pingHash, false, 0, emptyArgs, server.GetNetworkManager()),
            "World 1 entity must remain active after World 2 destruction");

        server.Shutdown();
        std::filesystem::remove_all(tempProj, ec);
        std::cout << "[PASS] Test_CSharpMultiWorldIsolationAndCleanup\n";
    }
}

int main(int argc, char** argv)
{
    std::string role;
    u16 port = 19703;
    u32 clientId = 1;
    u32 holdMs = 0;
    std::string tokenHex;
    std::string mpMode = "playerA";
    std::string sessionHex;
    std::string ticketHex;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg(argv[i]);
        if (arg.rfind("--role=", 0) == 0)
        {
            role = std::string(arg.substr(7));
        }
        else if (arg.rfind("--port=", 0) == 0)
        {
            port = static_cast<u16>(std::stoi(std::string(arg.substr(7))));
        }
        else if (arg.rfind("--id=", 0) == 0)
        {
            clientId = static_cast<u32>(std::stoul(std::string(arg.substr(5))));
        }
        else if (arg.rfind("--hold-ms=", 0) == 0)
        {
            holdMs = static_cast<u32>(std::stoul(std::string(arg.substr(10))));
        }
        else if (arg.rfind("--token=", 0) == 0)
        {
            tokenHex = std::string(arg.substr(8));
        }
        else if (arg.rfind("--mp-mode=", 0) == 0)
        {
            mpMode = std::string(arg.substr(10));
        }
        else if (arg.rfind("--session=", 0) == 0)
        {
            sessionHex = std::string(arg.substr(10));
        }
        else if (arg.rfind("--ticket=", 0) == 0)
        {
            ticketHex = std::string(arg.substr(9));
        }
    }

    if (role == "server")
    {
        return RunRawServerProcess(port);
    }
    if (role == "client")
    {
        return RunRawClientProcess(port);
    }
    if (role == "server-client")
    {
        return RunServerClientProcess(port, clientId, holdMs);
    }
    if (role == "token-client")
    {
        return RunTokenAuthClientProcess(port, tokenHex);
    }
    if (role == "mp-client")
    {
        return RunMultiplayerClientProcess(port, mpMode, sessionHex, ticketHex);
    }

    Test_BitStreamRoundTripAndQuantization();
    Test_BitStreamBoundsAndAdversarialInputs();
    Test_ProtocolHeaderValidation();
    Test_NetworkSimulatorFaultInjection();
    Test_ConnectionLifecycleAndMessageExchange();
    Test_ProtocolMismatchAndMultiClient();
    Test_RuntimeProfilesAndCapabilities();
    Test_HeadlessSimulationSessionAndRecreation();
    Test_SeparateProcessRawTransportIntegration();
    Test_MorbiServerStartupAndHeadlessVerification();
    Test_MorbiServerRealClientAndFourClientIntegration();
    Test_AuthenticatedHandshakeSessionModelAndPinning();
    Test_InvalidAuthTokenReplayAndWrongServerPin();
    Test_PacketReplayAndTamperingProtection();
    Test_DuplicateHandshakeMalformedPayloadsAndTimeouts();
    Test_InvalidProtocolVersionNegotiation();
    Test_ReconnectLifecycleAndExpiredSession();
    Test_ConnectionFloodRateLimiting();
    Test_MorbiServerTokenAuthSeparateProcess();
    Test_NetworkIdAllocationAndReuse();
    Test_ComponentSerializationAndMalformedPayloads();
    Test_SnapshotDeltaAckBaselineAndRecovery();
    Test_InterestManagementAndWorldIsolation();
    Test_ServerInputValidationAndOwnership();
    Test_RpcAuthorizationAndWorldMembership();
    Test_TwoClientMultiplayerAcceptanceSeparateProcesses();
    Test_AckBaselineEpochHardeningAndOutOfOrder();
    Test_DeterministicNetworkSimulatorBurstAndRealConnection();
    Test_ChunkedKeyframeTransportAndAtomicFailureHandling();
    Test_StaticSceneOptInReplicationAndReloadLifecycle();
    Test_SpawnDespawnEdgeCasesOwnershipTransferAndReconnectRecovery();
    Test_PhysicsRigidBodyAuthoritativeReplicationAndSleep();
    Test_PhysicsCharacterVirtualInputCollisionAndPush();
    Test_PhysicsHeadlessDedicatedServerMultiClientCollision();
    Test_ClientInputHistoryRingBufferAndBounds();
    Test_ClientPredictionReconciliationAndSmoothing();
    Test_ClientPhysicsCharacterPredictionAndRemoteInterpolation();
    Test_MultiWorldInstanceIsolationAndPhysics();
    Test_MultiWorldSessionTransferAndWorldDestruction();
    Test_ManagedNetworkWireContractsAndBounds();
    Test_CSharpNetworkBehaviourLifecyclePropertiesAndRpcs();
    Test_CSharpMultiWorldIsolationAndCleanup();

    if (g_failures > 0)
    {
        std::cerr << "MorbiNetworkTests FAILED with " << g_failures << " failure(s).\n";
        return 1;
    }

    std::cout << "All MorbiNetworkTests PASSED (42/42 suites).\n";
    return 0;
}
