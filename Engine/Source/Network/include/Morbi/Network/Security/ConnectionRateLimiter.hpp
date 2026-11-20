#pragma once

#include <deque>
#include <string>
#include <unordered_map>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkConfig.hpp"

namespace Morbi::Network
{
    struct ConnectionPacketBudget
    {
        u64 WindowStartMs = 0;
        u32 PacketCount = 0;
        u32 ByteCount = 0;
        u32 SecurityStrikes = 0;
    };

    class MORBI_NETWORK_API ConnectionRateLimiter final
    {
    public:
        void Configure(const RateLimitConfig& config) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool AllowIncomingConnection(const std::string& host, u64 nowMs);
        void OnHandshakeEnded(const std::string& host);

        [[nodiscard]] bool AllowPacket(
            ConnectionPacketBudget& budget,
            usize packetBytes,
            u64 nowMs,
            bool isAuthenticated) const noexcept;

        /// Increments strike count for connection + remote IP; returns true if peer should be disconnected/cooled down.
        bool RecordSecurityViolation(
            ConnectionPacketBudget& budget,
            const std::string& host,
            u64 nowMs);

        [[nodiscard]] bool IsHostInCooldown(const std::string& host, u64 nowMs) const noexcept;
        [[nodiscard]] u32 GetGlobalPendingHandshakes() const noexcept { return m_globalPendingHandshakes; }

        void Prune(u64 nowMs);

    private:
        struct HostRecord
        {
            std::deque<u64> ConnectTimestampsMs;
            u32 PendingHandshakes = 0;
            u32 Strikes = 0;
            u64 CooldownUntilMs = 0;
        };

        RateLimitConfig m_config{};
        std::unordered_map<std::string, HostRecord> m_hosts;
        u32 m_globalPendingHandshakes = 0;
    };
}
