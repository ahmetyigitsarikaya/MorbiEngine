#pragma once

#include <deque>
#include <random>
#include <span>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkConfig.hpp"

namespace Morbi::Network
{
    struct SimulatedPacket
    {
        ConnectionHandle Connection = kInvalidConnectionHandle;
        NetChannel Channel = NetChannel::Control;
        NetDelivery Delivery = NetDelivery::ReliableOrdered;
        std::vector<u8> Payload;
        u64 ReleaseTimeMs = 0;
    };

    struct NetworkSimulatorStats
    {
        u64 PacketsSubmitted = 0;
        u64 PacketsDelivered = 0;
        u64 PacketsDroppedLoss = 0;
        u64 PacketsDroppedBurst = 0;
        u64 PacketsDuplicated = 0;
        u64 PacketsReordered = 0;
    };

    /// Deterministic packet fault-injection pipeline supporting latency, jitter,
    /// packet loss, burst loss, duplication, reordering, and bandwidth rate limiting.
    class MORBI_NETWORK_API NetworkSimulator
    {
    public:
        void Configure(const NetSimConfig& config) noexcept;
        [[nodiscard]] const NetSimConfig& GetConfig() const noexcept { return m_config; }

        void Reset() noexcept;
        void ResetStats() noexcept { m_stats = {}; }

        /// Submits an outgoing or incoming packet into the simulator at `nowMs`.
        void Submit(
            ConnectionHandle connection,
            NetChannel channel,
            NetDelivery delivery,
            std::span<const u8> payload,
            u64 nowMs);

        /// Drains all packets whose simulated delivery time <= `nowMs`, ordered by release time.
        void DrainReady(u64 nowMs, std::vector<SimulatedPacket>& outReady);

        [[nodiscard]] usize GetQueuedCount() const noexcept { return m_queue.size(); }
        [[nodiscard]] const NetworkSimulatorStats& GetStats() const noexcept { return m_stats; }

    private:
        f32 NextUnitFloat() noexcept;
        u32 NextBurstLength() noexcept;

        NetSimConfig m_config{};
        std::mt19937 m_rng{ 1337u };
        std::vector<SimulatedPacket> m_queue;
        NetworkSimulatorStats m_stats{};
        u64 m_nextBandwidthWindowStartMs = 0;
        u64 m_bytesSentInWindow = 0;
        u32 m_burstDropRemaining = 0;
    };
}
