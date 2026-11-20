#include "Morbi/Network/Transport/ITransportBackend.hpp"
#include "Morbi/Network/Transport/NetworkSimulator.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "Morbi/Core/Log.hpp"

namespace Morbi::Network
{
    namespace
    {
        std::mutex g_enetInitMutex;
        u32 g_enetRefCount = 0;

        bool AcquireEnetRuntime() noexcept
        {
            std::lock_guard<std::mutex> lock(g_enetInitMutex);
            if (g_enetRefCount == 0)
            {
                if (enet_initialize() != 0)
                {
                    return false;
                }
            }
            ++g_enetRefCount;
            return true;
        }

        void ReleaseEnetRuntime() noexcept
        {
            std::lock_guard<std::mutex> lock(g_enetInitMutex);
            if (g_enetRefCount > 0)
            {
                --g_enetRefCount;
                if (g_enetRefCount == 0)
                {
                    enet_deinitialize();
                }
            }
        }

        [[nodiscard]] u64 NowMs() noexcept
        {
            return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        [[nodiscard]] u32 ToEnetFlags(NetDelivery delivery) noexcept
        {
            switch (delivery)
            {
            case NetDelivery::ReliableOrdered:
                return ENET_PACKET_FLAG_RELIABLE;
            case NetDelivery::ReliableUnordered:
                // ENet supports per-channel ordered reliability; map ReliableUnordered to reliable on its lane.
                return ENET_PACKET_FLAG_RELIABLE;
            case NetDelivery::UnreliableSequenced:
                return 0u;
            case NetDelivery::Unreliable:
            default:
                return ENET_PACKET_FLAG_UNSEQUENCED;
            }
        }

        [[nodiscard]] std::string FormatEnetHost(const ENetAddress& addr)
        {
            char buf[64]{};
            if (enet_address_get_host_ip(&addr, buf, sizeof(buf)) == 0)
            {
                return std::string(buf);
            }
            return "unknown";
        }
    }

    class EnetTransportBackend final : public ITransportBackend
    {
    public:
        ~EnetTransportBackend() override
        {
            Shutdown();
        }

        NetResult<bool> Initialize(const TransportConfig& config, bool isServer) override
        {
            if (m_initialized)
            {
                return NetResult<bool>::Failure({ NetErrorCode::AlreadyInitialized, "Transport already initialized." });
            }

            if (config.ChannelCount == 0 || config.MaxConnections == 0)
            {
                return NetResult<bool>::Failure({ NetErrorCode::InvalidArgument, "Invalid channel or connection count." });
            }

            if (!AcquireEnetRuntime())
            {
                return NetResult<bool>::Failure({ NetErrorCode::TransportInitFailed, "enet_initialize() failed." });
            }

            ENetAddress bindAddr{};
            ENetAddress* bindPtr = nullptr;
            if (isServer)
            {
                if (config.BindEndpoint.Host.empty() || config.BindEndpoint.Host == "0.0.0.0")
                {
                    bindAddr.host = ENET_HOST_ANY;
                }
                else if (enet_address_set_host(&bindAddr, config.BindEndpoint.Host.c_str()) != 0)
                {
                    ReleaseEnetRuntime();
                    return NetResult<bool>::Failure({ NetErrorCode::InvalidEndpoint, "Invalid bind host address." });
                }
                bindAddr.port = config.BindEndpoint.Port;
                bindPtr = &bindAddr;
            }

            m_host = enet_host_create(
                bindPtr,
                static_cast<size_t>(config.MaxConnections),
                static_cast<size_t>(config.ChannelCount),
                config.IncomingBandwidthBytesPerSec,
                config.OutgoingBandwidthBytesPerSec);

            if (!m_host)
            {
                ReleaseEnetRuntime();
                return NetResult<bool>::Failure({
                    isServer ? NetErrorCode::BindFailed : NetErrorCode::TransportInitFailed,
                    "enet_host_create() failed."
                });
            }

            m_config = config;
            m_simulator.Configure(config.Simulation);
            m_initialized = true;
            return NetResult<bool>::Success(true);
        }

        void Shutdown() override
        {
            if (!m_initialized)
            {
                return;
            }

            for (auto& [id, peer] : m_peers)
            {
                (void)id;
                if (peer != nullptr)
                {
                    peer->data = nullptr;
                    enet_peer_reset(peer);
                }
            }
            m_peers.clear();
            m_stats.clear();
            m_simulator.Reset();
            m_readySimulated.clear();

            if (m_host != nullptr)
            {
                enet_host_flush(m_host);
                enet_host_destroy(m_host);
                m_host = nullptr;
            }

            ReleaseEnetRuntime();
            m_initialized = false;
        }

        bool IsInitialized() const noexcept override
        {
            return m_initialized;
        }

        NetResult<ConnectionHandle> Connect(const Endpoint& remote) override
        {
            if (!m_initialized || m_host == nullptr)
            {
                return NetResult<ConnectionHandle>::Failure({ NetErrorCode::NotInitialized, "Transport is not initialized." });
            }
            if (!remote.IsValid())
            {
                return NetResult<ConnectionHandle>::Failure({ NetErrorCode::InvalidEndpoint, "Invalid remote endpoint." });
            }

            ENetAddress addr{};
            if (enet_address_set_host(&addr, remote.Host.c_str()) != 0)
            {
                return NetResult<ConnectionHandle>::Failure({ NetErrorCode::InvalidEndpoint, "Failed to resolve remote host." });
            }
            addr.port = remote.Port;

            ENetPeer* peer = enet_host_connect(m_host, &addr, m_config.ChannelCount, 0);
            if (peer == nullptr)
            {
                return NetResult<ConnectionHandle>::Failure({ NetErrorCode::ConnectFailed, "No available ENet peer slots." });
            }

            const ConnectionHandle handle = RegisterPeer(peer);
            return NetResult<ConnectionHandle>::Success(handle);
        }

        void Disconnect(ConnectionHandle connection, u32 reasonCode, bool immediate) override
        {
            auto it = m_peers.find(connection.Value);
            if (it == m_peers.end() || it->second == nullptr)
            {
                return;
            }
            ENetPeer* peer = it->second;
            if (immediate)
            {
                enet_peer_disconnect_now(peer, reasonCode);
                UnregisterPeer(peer);
            }
            else
            {
                enet_peer_disconnect(peer, reasonCode);
            }
        }

        SendResult Send(
            ConnectionHandle connection,
            std::span<const u8> payload,
            NetDelivery delivery,
            NetChannel channel) override
        {
            if (!m_initialized || m_host == nullptr)
            {
                return { SendStatus::Failed, { NetErrorCode::NotInitialized, "Transport not initialized." } };
            }
            if (payload.size() > ProtocolConstants::kMaxMessageBytes)
            {
                return { SendStatus::Failed, { NetErrorCode::PayloadTooLarge, "Payload exceeds kMaxMessageBytes." } };
            }
            auto peerIt = m_peers.find(connection.Value);
            if (peerIt == m_peers.end() || peerIt->second == nullptr)
            {
                return { SendStatus::Failed, { NetErrorCode::InvalidConnection, "Unknown connection handle." } };
            }

            if (m_simulator.GetConfig().Enabled)
            {
                m_simulator.Submit(connection, channel, delivery, payload, NowMs());
                return { SendStatus::Queued, {} };
            }

            return SendRawToPeer(connection, peerIt->second, payload, delivery, channel);
        }

        void Poll(ITransportEventSink& sink) override
        {
            if (!m_initialized || m_host == nullptr)
            {
                return;
            }

            // Flush any simulated packets whose delivery time has elapsed
            if (m_simulator.GetConfig().Enabled)
            {
                m_simulator.DrainReady(NowMs(), m_readySimulated);
                for (const SimulatedPacket& simPkt : m_readySimulated)
                {
                    auto peerIt = m_peers.find(simPkt.Connection.Value);
                    if (peerIt != m_peers.end() && peerIt->second != nullptr)
                    {
                        (void)SendRawToPeer(
                            simPkt.Connection,
                            peerIt->second,
                            simPkt.Payload,
                            simPkt.Delivery,
                            simPkt.Channel);
                    }
                }
            }

            ENetEvent event{};
            while (enet_host_service(m_host, &event, m_config.PollTimeoutMs) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    ConnectionHandle handle = GetHandleForPeer(event.peer);
                    if (!handle.IsValid())
                    {
                        handle = RegisterPeer(event.peer);
                    }
                    Endpoint ep{ FormatEnetHost(event.peer->address), event.peer->address.port };
                    sink.OnTransportConnected(handle, ep);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                {
                    const ConnectionHandle handle = GetHandleForPeer(event.peer);
                    if (handle.IsValid() && event.packet != nullptr)
                    {
                        auto& st = m_stats[handle.Value];
                        st.PacketsReceived += 1;
                        st.BytesReceived += event.packet->dataLength;
                        st.RoundTripTimeMs = event.peer->roundTripTime;
                        st.RoundTripTimeVarianceMs = event.peer->roundTripTimeVariance;
                        st.PacketLossPercent = (static_cast<f32>(event.peer->packetLoss) /
                            static_cast<f32>(ENET_PEER_PACKET_LOSS_SCALE)) * 100.0f;

                        const u8 channelIdx = (std::min)(
                            event.channelID,
                            static_cast<enet_uint8>(ProtocolConstants::kChannelCount - 1u));
                        sink.OnTransportPacketReceived(
                            handle,
                            static_cast<NetChannel>(channelIdx),
                            std::span<const u8>(event.packet->data, event.packet->dataLength));
                    }
                    if (event.packet != nullptr)
                    {
                        enet_packet_destroy(event.packet);
                    }
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                {
                    const ConnectionHandle handle = GetHandleForPeer(event.peer);
                    UnregisterPeer(event.peer);
                    if (handle.IsValid())
                    {
                        sink.OnTransportDisconnected(handle, event.data);
                    }
                    break;
                }
                case ENET_EVENT_TYPE_NONE:
                default:
                    break;
                }
            }
        }

        void SetSimulationConfig(const NetSimConfig& sim) override
        {
            m_config.Simulation = sim;
            m_simulator.Configure(sim);
        }

        bool TryGetConnectionStats(ConnectionHandle connection, NetworkStats& outStats) const override
        {
            auto peerIt = m_peers.find(connection.Value);
            if (peerIt == m_peers.end() || peerIt->second == nullptr)
            {
                return false;
            }
            const ENetPeer* peer = peerIt->second;
            auto statIt = m_stats.find(connection.Value);
            if (statIt != m_stats.end())
            {
                outStats = statIt->second;
            }
            outStats.RoundTripTimeMs = peer->roundTripTime;
            outStats.RoundTripTimeVarianceMs = peer->roundTripTimeVariance;
            outStats.PacketLossPercent = (static_cast<f32>(peer->packetLoss) /
                static_cast<f32>(ENET_PEER_PACKET_LOSS_SCALE)) * 100.0f;
            return true;
        }

        bool TryGetRemoteEndpoint(ConnectionHandle connection, Endpoint& outEndpoint) const override
        {
            auto peerIt = m_peers.find(connection.Value);
            if (peerIt == m_peers.end() || peerIt->second == nullptr)
            {
                return false;
            }
            outEndpoint.Host = FormatEnetHost(peerIt->second->address);
            outEndpoint.Port = peerIt->second->address.port;
            return true;
        }

    private:
        SendResult SendRawToPeer(
            ConnectionHandle handle,
            ENetPeer* peer,
            std::span<const u8> payload,
            NetDelivery delivery,
            NetChannel channel)
        {
            ENetPacket* packet = enet_packet_create(payload.data(), payload.size(), ToEnetFlags(delivery));
            if (packet == nullptr)
            {
                return { SendStatus::Failed, { NetErrorCode::SendFailed, "enet_packet_create() failed." } };
            }
            const u8 ch = (std::min)(static_cast<u8>(channel), static_cast<u8>(m_config.ChannelCount - 1u));
            if (enet_peer_send(peer, ch, packet) != 0)
            {
                enet_packet_destroy(packet);
                return { SendStatus::Failed, { NetErrorCode::SendFailed, "enet_peer_send() failed." } };
            }
            auto& st = m_stats[handle.Value];
            st.PacketsSent += 1;
            st.BytesSent += payload.size();
            return { SendStatus::Queued, {} };
        }

        ConnectionHandle RegisterPeer(ENetPeer* peer)
        {
            const u32 id = m_nextHandleId++;
            if (m_nextHandleId == 0)
            {
                m_nextHandleId = 1;
            }
            peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
            m_peers[id] = peer;
            m_stats[id] = NetworkStats{};
            return ConnectionHandle{ id };
        }

        ConnectionHandle GetHandleForPeer(const ENetPeer* peer) const noexcept
        {
            if (peer == nullptr || peer->data == nullptr)
            {
                return kInvalidConnectionHandle;
            }
            return ConnectionHandle{ static_cast<u32>(reinterpret_cast<uintptr_t>(peer->data)) };
        }

        void UnregisterPeer(ENetPeer* peer)
        {
            if (peer == nullptr)
            {
                return;
            }
            const ConnectionHandle h = GetHandleForPeer(peer);
            peer->data = nullptr;
            if (h.IsValid())
            {
                m_peers.erase(h.Value);
                m_stats.erase(h.Value);
            }
        }

        ENetHost* m_host = nullptr;
        TransportConfig m_config{};
        NetworkSimulator m_simulator;
        std::vector<SimulatedPacket> m_readySimulated;
        std::unordered_map<u32, ENetPeer*> m_peers;
        std::unordered_map<u32, NetworkStats> m_stats;
        u32 m_nextHandleId = 1;
        bool m_initialized = false;
    };

    std::unique_ptr<ITransportBackend> CreateEnetTransportBackend()
    {
        return std::make_unique<EnetTransportBackend>();
    }
}
