#pragma once

#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/RPC/RpcContracts.hpp"
#include "Morbi/Network/Serialization/BitStream.hpp"
#include "Morbi/Network/Session/SessionRegistry.hpp"

namespace Morbi::Network
{
    struct RpcInvocationEnvelope
    {
        u32 RpcId = 0;
        NetworkId TargetEntity = kInvalidNetworkId;
        WorldInstanceId TargetWorldId = kInvalidWorldInstanceId;
        std::vector<u8> Payload;
    };

    struct RpcInvocationContext
    {
        ConnectionHandle SenderConnection = kInvalidConnectionHandle;
        SessionId SenderSession = kInvalidSessionId;
        PlayerId SenderPlayer = kInvalidPlayerId;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        NetworkId TargetEntity = kInvalidNetworkId;
        u32 RpcId = 0;
        std::span<const u8> Payload;
    };

    enum class RpcValidationResult : u8
    {
        Authorized = 0,
        Malformed = 1,
        UnknownRpc = 2,
        InvalidDirection = 3,
        PayloadTooLarge = 4,
        UnauthenticatedSession = 5,
        WorldMismatch = 6,
        AuthorityDenied = 7,
        RateLimited = 8
    };

    struct RpcRouterStats
    {
        u64 Dispatched = 0;
        u64 RejectedMalformed = 0;
        u64 RejectedUnknownRpc = 0;
        u64 RejectedDirection = 0;
        u64 RejectedPayloadTooLarge = 0;
        u64 RejectedUnauthenticated = 0;
        u64 RejectedWorldMismatch = 0;
        u64 RejectedAuthority = 0;
        u64 RejectedRateLimit = 0;
    };

    using RpcHandlerFn = std::function<void(const RpcInvocationContext&)>;

    /// Authoritative RPC registration & validation router using native RpcContracts.
    class MORBI_NETWORK_API RpcRouter final
    {
    public:
        explicit RpcRouter(bool isServer = true) noexcept;

        void SetServerRole(bool isServer) noexcept;
        void Clear() noexcept;

        bool RegisterRpc(const RpcDescriptor& descriptor, RpcHandlerFn handler);
        [[nodiscard]] bool IsRegistered(u32 rpcId) const noexcept;

        static bool WriteInvocation(BitWriter& writer, const RpcInvocationEnvelope& envelope);
        static bool ReadInvocation(BitReader& reader, RpcInvocationEnvelope& outEnvelope);

        RpcValidationResult ProcessIncomingRpc(
            ConnectionHandle senderConnection,
            const SessionRegistry& sessions,
            WorldInstanceId authoritativeWorldId,
            std::span<const u8> rawEnvelopeBytes,
            u64 nowMs);

        [[nodiscard]] const RpcRouterStats& GetStats() const noexcept { return m_stats; }

    private:
        struct RegisteredRpc
        {
            RpcDescriptor Descriptor{};
            RpcHandlerFn Handler;
        };

        struct RateWindowKey
        {
            u32 ConnectionVal = 0;
            u32 RpcId = 0;
            bool operator==(const RateWindowKey&) const noexcept = default;
        };

        struct RateWindowKeyHasher
        {
            usize operator()(const RateWindowKey& k) const noexcept
            {
                return static_cast<usize>((static_cast<u64>(k.ConnectionVal) << 32) | k.RpcId);
            }
        };

        struct RateBucket
        {
            u64 WindowStartMs = 0;
            u32 Count = 0;
        };

        bool m_isServer = true;
        std::unordered_map<u32, RegisteredRpc> m_registry;
        std::unordered_map<RateWindowKey, RateBucket, RateWindowKeyHasher> m_rateLimits;
        RpcRouterStats m_stats{};
    };
}
