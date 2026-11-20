#include "Morbi/Network/RPC/RpcRouter.hpp"

#include "Morbi/Network/NetworkConfig.hpp"

namespace Morbi::Network
{
    RpcRouter::RpcRouter(bool isServer) noexcept
        : m_isServer(isServer)
    {
    }

    void RpcRouter::SetServerRole(bool isServer) noexcept
    {
        m_isServer = isServer;
        m_rateLimits.clear();
    }

    void RpcRouter::Clear() noexcept
    {
        m_registry.clear();
        m_rateLimits.clear();
        m_stats = RpcRouterStats{};
    }

    bool RpcRouter::RegisterRpc(const RpcDescriptor& descriptor, RpcHandlerFn handler)
    {
        if (descriptor.RpcId == 0 ||
            descriptor.MaxPayloadBytes > ProtocolConstants::kMaxMessageBytes ||
            !handler)
        {
            return false;
        }
        m_registry.insert_or_assign(descriptor.RpcId, RegisteredRpc{
            .Descriptor = descriptor,
            .Handler = std::move(handler)
        });
        return true;
    }

    bool RpcRouter::IsRegistered(u32 rpcId) const noexcept
    {
        return m_registry.find(rpcId) != m_registry.end();
    }

    bool RpcRouter::WriteInvocation(BitWriter& writer, const RpcInvocationEnvelope& envelope)
    {
        if (envelope.RpcId == 0 || envelope.Payload.size() > ProtocolConstants::kMaxMessageBytes - 64)
        {
            return false;
        }
        return writer.WriteU32(envelope.RpcId) &&
            writer.WriteU32(envelope.TargetEntity.Index) &&
            writer.WriteU32(envelope.TargetEntity.Generation) &&
            writer.WriteU32(envelope.TargetWorldId) &&
            writer.WriteU32(static_cast<u32>(envelope.Payload.size())) &&
            writer.WriteBytes(envelope.Payload);
    }

    bool RpcRouter::ReadInvocation(BitReader& reader, RpcInvocationEnvelope& outEnvelope)
    {
        outEnvelope = RpcInvocationEnvelope{};
        if (!reader.ReadU32(outEnvelope.RpcId) ||
            outEnvelope.RpcId == 0 ||
            !reader.ReadU32(outEnvelope.TargetEntity.Index) ||
            !reader.ReadU32(outEnvelope.TargetEntity.Generation) ||
            !reader.ReadU32(outEnvelope.TargetWorldId) ||
            !reader.ReadByteVector(outEnvelope.Payload, ProtocolConstants::kMaxMessageBytes) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            return false;
        }
        return true;
    }

    RpcValidationResult RpcRouter::ProcessIncomingRpc(
        ConnectionHandle senderConnection,
        const SessionRegistry& sessions,
        WorldInstanceId authoritativeWorldId,
        std::span<const u8> rawEnvelopeBytes,
        u64 nowMs)
    {
        BitReader reader(rawEnvelopeBytes);
        RpcInvocationEnvelope env{};
        if (!ReadInvocation(reader, env))
        {
            ++m_stats.RejectedMalformed;
            return RpcValidationResult::Malformed;
        }

        auto regIt = m_registry.find(env.RpcId);
        if (regIt == m_registry.end())
        {
            ++m_stats.RejectedUnknownRpc;
            return RpcValidationResult::UnknownRpc;
        }
        const RegisteredRpc& entry = regIt->second;

        // Direction enforcement
        if (m_isServer)
        {
            if (entry.Descriptor.Direction != RpcDirection::ClientToServer)
            {
                ++m_stats.RejectedDirection;
                return RpcValidationResult::InvalidDirection;
            }
        }
        else
        {
            if (entry.Descriptor.Direction != RpcDirection::ServerToClient &&
                entry.Descriptor.Direction != RpcDirection::Multicast)
            {
                ++m_stats.RejectedDirection;
                return RpcValidationResult::InvalidDirection;
            }
        }

        // Payload bound check per descriptor
        if (env.Payload.size() > entry.Descriptor.MaxPayloadBytes)
        {
            ++m_stats.RejectedPayloadTooLarge;
            return RpcValidationResult::PayloadTooLarge;
        }

        if (!m_isServer)
        {
            // Client receives authorized server RPCs on established ConnectionManager encrypted channel
            RpcInvocationContext ctx{};
            ctx.SenderConnection = senderConnection;
            ctx.WorldId = env.TargetWorldId;
            ctx.TargetEntity = env.TargetEntity;
            ctx.RpcId = env.RpcId;
            ctx.Payload = env.Payload;
            ++m_stats.Dispatched;
            entry.Handler(ctx);
            return RpcValidationResult::Authorized;
        }

        // Server-side: sender MUST have active session in matching world
        NetworkSession senderSession{};
        if (!senderConnection.IsValid() ||
            !sessions.TryGetSessionByConnection(senderConnection, senderSession) ||
            senderSession.LifecycleState != SessionLifecycleState::Active)
        {
            ++m_stats.RejectedUnauthenticated;
            return RpcValidationResult::UnauthenticatedSession;
        }

        if (authoritativeWorldId != kInvalidWorldInstanceId &&
            senderSession.WorldId != authoritativeWorldId)
        {
            ++m_stats.RejectedWorldMismatch;
            return RpcValidationResult::WorldMismatch;
        }

        if (env.TargetWorldId != kInvalidWorldInstanceId &&
            env.TargetWorldId != senderSession.WorldId)
        {
            ++m_stats.RejectedWorldMismatch;
            return RpcValidationResult::WorldMismatch;
        }

        // Authority enforcement (ServerOnly can NEVER be called by a client)
        switch (entry.Descriptor.Authority)
        {
        case RpcAuthorityRule::ServerOnly:
            ++m_stats.RejectedAuthority;
            return RpcValidationResult::AuthorityDenied;

        case RpcAuthorityRule::OwnerOnly:
        {
            if (!env.TargetEntity.IsValid())
            {
                ++m_stats.RejectedAuthority;
                return RpcValidationResult::AuthorityDenied;
            }
            NetworkEntityBinding binding{};
            if (!sessions.TryGetEntityBinding(env.TargetEntity, binding) ||
                binding.OwnerSession != senderSession.Id ||
                binding.WorldId != senderSession.WorldId)
            {
                ++m_stats.RejectedAuthority;
                return RpcValidationResult::AuthorityDenied;
            }
            break;
        }

        case RpcAuthorityRule::AnyAuthenticatedPeer:
        default:
            break;
        }

        // Per-(Connection, RpcId) sliding rate limit
        if (entry.Descriptor.RateLimitPerSec > 0)
        {
            RateBucket& bucket = m_rateLimits[RateWindowKey{ senderConnection.Value, env.RpcId }];
            if (bucket.WindowStartMs == 0 || (nowMs >= bucket.WindowStartMs && (nowMs - bucket.WindowStartMs) >= 1000))
            {
                bucket.WindowStartMs = nowMs;
                bucket.Count = 0;
            }
            if (bucket.Count >= entry.Descriptor.RateLimitPerSec)
            {
                ++m_stats.RejectedRateLimit;
                return RpcValidationResult::RateLimited;
            }
            ++bucket.Count;
        }

        RpcInvocationContext ctx{};
        ctx.SenderConnection = senderConnection;
        ctx.SenderSession = senderSession.Id;
        ctx.SenderPlayer = senderSession.Players.empty() ? kInvalidPlayerId : senderSession.Players.front();
        ctx.WorldId = senderSession.WorldId;
        ctx.TargetEntity = env.TargetEntity;
        ctx.RpcId = env.RpcId;
        ctx.Payload = env.Payload;

        ++m_stats.Dispatched;
        entry.Handler(ctx);
        return RpcValidationResult::Authorized;
    }
}
