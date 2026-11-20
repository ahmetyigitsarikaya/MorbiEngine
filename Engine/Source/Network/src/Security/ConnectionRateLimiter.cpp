#include "Morbi/Network/Security/ConnectionRateLimiter.hpp"

#include <algorithm>

namespace Morbi::Network
{
    void ConnectionRateLimiter::Configure(const RateLimitConfig& config) noexcept
    {
        m_config = config;
        Reset();
    }

    void ConnectionRateLimiter::Reset() noexcept
    {
        m_hosts.clear();
        m_globalPendingHandshakes = 0;
    }

    bool ConnectionRateLimiter::AllowIncomingConnection(const std::string& host, u64 nowMs)
    {
        if (!m_config.Enabled)
        {
            ++m_globalPendingHandshakes;
            ++m_hosts[host].PendingHandshakes;
            return true;
        }

        if (m_globalPendingHandshakes >= m_config.MaxGlobalPendingHandshakes)
        {
            return false;
        }

        auto& record = m_hosts[host];
        if (record.CooldownUntilMs > nowMs)
        {
            return false;
        }

        while (!record.ConnectTimestampsMs.empty() &&
            nowMs > record.ConnectTimestampsMs.front() &&
            (nowMs - record.ConnectTimestampsMs.front()) > m_config.ConnectRateWindowMs)
        {
            record.ConnectTimestampsMs.pop_front();
        }

        if (record.PendingHandshakes >= m_config.MaxPendingHandshakesPerIp)
        {
            return false;
        }

        if (record.ConnectTimestampsMs.size() >= m_config.MaxConnectAttemptsPerIpWindow)
        {
            record.CooldownUntilMs = nowMs + m_config.IpCooldownMs;
            return false;
        }

        record.ConnectTimestampsMs.push_back(nowMs);
        ++record.PendingHandshakes;
        ++m_globalPendingHandshakes;
        return true;
    }

    void ConnectionRateLimiter::OnHandshakeEnded(const std::string& host)
    {
        if (m_globalPendingHandshakes > 0)
        {
            --m_globalPendingHandshakes;
        }
        auto it = m_hosts.find(host);
        if (it != m_hosts.end() && it->second.PendingHandshakes > 0)
        {
            --it->second.PendingHandshakes;
        }
    }

    bool ConnectionRateLimiter::AllowPacket(
        ConnectionPacketBudget& budget,
        usize packetBytes,
        u64 nowMs,
        bool isAuthenticated) const noexcept
    {
        if (!m_config.Enabled)
        {
            return true;
        }

        if (budget.WindowStartMs == 0 || (nowMs >= budget.WindowStartMs && (nowMs - budget.WindowStartMs) >= 1000))
        {
            budget.WindowStartMs = nowMs;
            budget.PacketCount = 0;
            budget.ByteCount = 0;
        }

        const u32 maxPackets = isAuthenticated ? m_config.MaxPacketsPerSec : m_config.MaxUnauthenticatedPacketsPerSec;
        if (budget.PacketCount + 1 > maxPackets)
        {
            return false;
        }
        if (budget.ByteCount + packetBytes > m_config.MaxBytesPerSec)
        {
            return false;
        }

        budget.PacketCount += 1;
        budget.ByteCount += static_cast<u32>(packetBytes);
        return true;
    }

    bool ConnectionRateLimiter::RecordSecurityViolation(
        ConnectionPacketBudget& budget,
        const std::string& host,
        u64 nowMs)
    {
        ++budget.SecurityStrikes;
        auto& record = m_hosts[host];
        ++record.Strikes;
        if (!m_config.Enabled)
        {
            return false;
        }
        if (budget.SecurityStrikes >= m_config.MaxSecurityStrikes ||
            record.Strikes >= m_config.MaxSecurityStrikes * 2)
        {
            record.CooldownUntilMs = nowMs + m_config.IpCooldownMs;
            return true;
        }
        return false;
    }

    bool ConnectionRateLimiter::IsHostInCooldown(const std::string& host, u64 nowMs) const noexcept
    {
        if (!m_config.Enabled)
        {
            return false;
        }
        auto it = m_hosts.find(host);
        return it != m_hosts.end() && it->second.CooldownUntilMs > nowMs;
    }

    void ConnectionRateLimiter::Prune(u64 nowMs)
    {
        for (auto it = m_hosts.begin(); it != m_hosts.end();)
        {
            auto& rec = it->second;
            while (!rec.ConnectTimestampsMs.empty() &&
                nowMs > rec.ConnectTimestampsMs.front() &&
                (nowMs - rec.ConnectTimestampsMs.front()) > m_config.ConnectRateWindowMs)
            {
                rec.ConnectTimestampsMs.pop_front();
            }

            if (rec.PendingHandshakes == 0 &&
                rec.ConnectTimestampsMs.empty() &&
                rec.CooldownUntilMs <= nowMs)
            {
                it = m_hosts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
