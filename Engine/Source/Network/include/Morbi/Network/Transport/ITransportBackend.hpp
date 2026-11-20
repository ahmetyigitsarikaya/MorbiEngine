#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkConfig.hpp"

namespace Morbi::Network
{
    enum class SendStatus : u8
    {
        Queued = 0,
        DroppedSimulated = 1,
        Failed = 2
    };

    struct SendResult
    {
        SendStatus Status = SendStatus::Failed;
        NetError Error{};

        [[nodiscard]] constexpr bool Succeeded() const noexcept
        {
            return Status == SendStatus::Queued || Status == SendStatus::DroppedSimulated;
        }
    };

    class ITransportEventSink
    {
    public:
        virtual ~ITransportEventSink() = default;

        virtual void OnTransportConnected(ConnectionHandle connection, const Endpoint& remote) = 0;
        virtual void OnTransportDisconnected(ConnectionHandle connection, u32 reasonCode) = 0;
        virtual void OnTransportPacketReceived(
            ConnectionHandle connection,
            NetChannel channel,
            std::span<const u8> payload) = 0;
    };

    /// Pure MorbiEngine transport boundary hiding all third-party library types (e.g., ENet).
    class ITransportBackend
    {
    public:
        virtual ~ITransportBackend() = default;

        virtual NetResult<bool> Initialize(const TransportConfig& config, bool isServer) = 0;
        virtual void Shutdown() = 0;
        [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;

        virtual NetResult<ConnectionHandle> Connect(const Endpoint& remote) = 0;
        virtual void Disconnect(ConnectionHandle connection, u32 reasonCode, bool immediate) = 0;

        virtual SendResult Send(
            ConnectionHandle connection,
            std::span<const u8> payload,
            NetDelivery delivery,
            NetChannel channel) = 0;

        virtual void Poll(ITransportEventSink& sink) = 0;
        virtual void SetSimulationConfig(const NetSimConfig& sim) = 0;
        [[nodiscard]] virtual bool TryGetConnectionStats(ConnectionHandle connection, NetworkStats& outStats) const = 0;
        [[nodiscard]] virtual bool TryGetRemoteEndpoint(ConnectionHandle connection, Endpoint& outEndpoint) const = 0;
    };

    [[nodiscard]] MORBI_NETWORK_API std::unique_ptr<ITransportBackend> CreateEnetTransportBackend();
}
