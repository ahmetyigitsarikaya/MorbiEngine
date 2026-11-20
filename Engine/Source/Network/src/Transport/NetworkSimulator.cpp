#include "Morbi/Network/Transport/NetworkSimulator.hpp"

#include <algorithm>
#include <cmath>

namespace Morbi::Network
{
    void NetworkSimulator::Configure(const NetSimConfig& config) noexcept
    {
        m_config = config;
        Reset();
    }

    void NetworkSimulator::Reset() noexcept
    {
        m_rng.seed(m_config.Seed);
        m_queue.clear();
        m_stats = {};
        m_nextBandwidthWindowStartMs = 0;
        m_bytesSentInWindow = 0;
        m_burstDropRemaining = 0;
    }

    f32 NetworkSimulator::NextUnitFloat() noexcept
    {
        return static_cast<f32>(m_rng() - m_rng.min()) / static_cast<f32>(m_rng.max() - m_rng.min());
    }

    u32 NetworkSimulator::NextBurstLength() noexcept
    {
        const u32 minPkts = (std::max)(1u, m_config.BurstLossMinPackets);
        const u32 maxPkts = (std::max)(minPkts, m_config.BurstLossMaxPackets);
        const u32 span = maxPkts - minPkts + 1u;
        return minPkts + static_cast<u32>(m_rng() % span);
    }

    void NetworkSimulator::Submit(
        ConnectionHandle connection,
        NetChannel channel,
        NetDelivery delivery,
        std::span<const u8> payload,
        u64 nowMs)
    {
        ++m_stats.PacketsSubmitted;

        if (!m_config.Enabled)
        {
            SimulatedPacket pkt{};
            pkt.Connection = connection;
            pkt.Channel = channel;
            pkt.Delivery = delivery;
            pkt.Payload.assign(payload.begin(), payload.end());
            pkt.ReleaseTimeMs = nowMs;
            m_queue.push_back(std::move(pkt));
            return;
        }

        // Ongoing or newly triggered burst packet loss
        if (m_burstDropRemaining > 0)
        {
            --m_burstDropRemaining;
            ++m_stats.PacketsDroppedBurst;
            return;
        }

        if (m_config.BurstLossChancePercent > 0.0f &&
            (NextUnitFloat() * 100.0f) < m_config.BurstLossChancePercent)
        {
            const u32 burstCount = NextBurstLength();
            m_burstDropRemaining = burstCount > 0 ? (burstCount - 1u) : 0u;
            ++m_stats.PacketsDroppedBurst;
            return;
        }

        // Independent uniform packet loss
        if (m_config.PacketLossPercent > 0.0f)
        {
            const f32 roll = NextUnitFloat() * 100.0f;
            if (roll < m_config.PacketLossPercent)
            {
                ++m_stats.PacketsDroppedLoss;
                return;
            }
        }

        // Simulated bandwidth cap (1-second sliding window)
        u64 bandwidthDelayMs = 0;
        if (m_config.BandwidthLimitBytesPerSec > 0)
        {
            if (nowMs >= m_nextBandwidthWindowStartMs + 1000ull)
            {
                m_nextBandwidthWindowStartMs = nowMs;
                m_bytesSentInWindow = 0;
            }
            if (m_bytesSentInWindow + payload.size() > m_config.BandwidthLimitBytesPerSec)
            {
                const u64 overflowBytes = (m_bytesSentInWindow + payload.size()) - m_config.BandwidthLimitBytesPerSec;
                bandwidthDelayMs = (overflowBytes * 1000ull) / m_config.BandwidthLimitBytesPerSec + 1ull;
            }
            m_bytesSentInWindow += payload.size();
        }

        const f32 jitterSigned = (m_config.JitterMs > 0.0f)
            ? ((NextUnitFloat() * 2.0f - 1.0f) * m_config.JitterMs)
            : 0.0f;
        f32 delayMs = (std::max)(0.0f, m_config.BaseLatencyMs + jitterSigned);

        // Simulated reordering: occasionally add extra delay so subsequent packets overtake this one
        if (m_config.ReorderPercent > 0.0f && (NextUnitFloat() * 100.0f) < m_config.ReorderPercent)
        {
            delayMs += (std::max)(15.0f, m_config.JitterMs + 15.0f);
            ++m_stats.PacketsReordered;
        }

        const u64 releaseAt = nowMs + static_cast<u64>(std::llround(delayMs)) + bandwidthDelayMs;

        SimulatedPacket pkt{};
        pkt.Connection = connection;
        pkt.Channel = channel;
        pkt.Delivery = delivery;
        pkt.Payload.assign(payload.begin(), payload.end());
        pkt.ReleaseTimeMs = releaseAt;
        m_queue.push_back(std::move(pkt));

        // Simulated packet duplication
        if (m_config.DuplicationPercent > 0.0f && (NextUnitFloat() * 100.0f) < m_config.DuplicationPercent)
        {
            SimulatedPacket dup = pkt;
            dup.ReleaseTimeMs += 2ull;
            m_queue.push_back(std::move(dup));
            ++m_stats.PacketsDuplicated;
        }
    }

    void NetworkSimulator::DrainReady(u64 nowMs, std::vector<SimulatedPacket>& outReady)
    {
        outReady.clear();
        for (auto it = m_queue.begin(); it != m_queue.end();)
        {
            if (it->ReleaseTimeMs <= nowMs)
            {
                outReady.push_back(std::move(*it));
                it = m_queue.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (outReady.size() > 1)
        {
            std::stable_sort(
                outReady.begin(),
                outReady.end(),
                [](const SimulatedPacket& lhs, const SimulatedPacket& rhs)
                {
                    return lhs.ReleaseTimeMs < rhs.ReleaseTimeMs;
                });
        }
        m_stats.PacketsDelivered += outReady.size();
    }
}
