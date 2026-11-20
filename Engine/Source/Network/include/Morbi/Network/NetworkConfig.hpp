#pragma once

#include <array>

#include "Morbi/Network/NetworkTypes.hpp"

namespace Morbi::Network
{
    namespace ProtocolConstants
    {
        inline constexpr u32 kMagic = 0x4D52424E; // 'MRBN'
        inline constexpr u16 kMinSupportedVersion = 2;
        inline constexpr u16 kCurrentVersion = 2;
        inline constexpr u8 kChannelCount = 4;
        inline constexpr usize kMaxMessageBytes = 65536;
        inline constexpr usize kMaxStringBytes = 4096;
        inline constexpr usize kMaxHandshakePacketBytes = 1024;
        inline constexpr usize kMaxControlPacketBytes = 2048;
        inline constexpr usize kMaxWirePacketBytes = 65536 + 64;
        inline constexpr usize kPathMtuPayloadBudgetBytes = 1200;
        inline constexpr usize kReplayWindowBits = 128;
    }

    struct NetSimConfig
    {
        bool Enabled = false;
        u32 Seed = 1337u;
        f32 BaseLatencyMs = 0.0f;
        f32 JitterMs = 0.0f;
        f32 PacketLossPercent = 0.0f;
        f32 BurstLossChancePercent = 0.0f;
        u32 BurstLossMinPackets = 2u;
        u32 BurstLossMaxPackets = 5u;
        f32 DuplicationPercent = 0.0f;
        f32 ReorderPercent = 0.0f;
        u32 BandwidthLimitBytesPerSec = 0; // 0 = unlimited
    };

    struct TransportConfig
    {
        Endpoint BindEndpoint{ "0.0.0.0", 0 };
        u32 MaxConnections = 64;
        u8 ChannelCount = ProtocolConstants::kChannelCount;
        u32 IncomingBandwidthBytesPerSec = 0; // 0 = unlimited
        u32 OutgoingBandwidthBytesPerSec = 0; // 0 = unlimited
        u32 PollTimeoutMs = 0;
        NetSimConfig Simulation{};
    };

    struct RateLimitConfig
    {
        bool Enabled = true;
        u32 MaxConnectAttemptsPerIpWindow = 8;
        u32 ConnectRateWindowMs = 5000;
        u32 MaxPendingHandshakesPerIp = 4;
        u32 MaxGlobalPendingHandshakes = 32;
        u32 MaxUnauthenticatedPacketsPerSec = 16;
        u32 MaxPacketsPerSec = 600;
        u32 MaxBytesPerSec = 2u * 1024u * 1024u;
        u32 IpCooldownMs = 5000;
        u32 MaxSecurityStrikes = 3;
    };

    struct SecurityConfig
    {
        bool RequireSignedConnectToken = false;
        bool HasServerIdentityKeyPair = false;
        std::array<u8, 32> ServerIdentityPublicKey{};
        std::array<u8, 64> ServerIdentitySecretKey{};

        bool RequirePinnedServerPublicKey = false;
        std::array<u8, 32> ExpectedServerPublicKey{};

        bool HasTokenIssuerPublicKey = false;
        std::array<u8, 32> TokenIssuerPublicKey{};

        u32 ReconnectWindowMs = 15000;
        u32 SessionIdleTimeoutMs = 60000;
        u32 ReconnectKeyRotationMs = 300000;
        RateLimitConfig RateLimit{};
    };

    struct ConnectionConfig
    {
        NetworkMode Mode = NetworkMode::Standalone;
        TransportConfig Transport{};
        u16 MinProtocolVersion = ProtocolConstants::kMinSupportedVersion;
        u16 MaxProtocolVersion = ProtocolConstants::kCurrentVersion;
        u32 HandshakeTimeoutMs = 5000;
        u32 DisconnectTimeoutMs = 10000;
        u32 SchemaHash = 1;
        SecurityConfig Security{};
    };
}
