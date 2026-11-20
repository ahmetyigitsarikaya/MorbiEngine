#pragma once

#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/Session/SessionTypes.hpp"

namespace Morbi::Network
{
    struct SessionIdHasher
    {
        usize operator()(const SessionId& id) const noexcept
        {
            const u64 mixed = id.High ^ (id.Low * 0x9E3779B97F4A7C15ull);
            return static_cast<usize>(mixed ^ (mixed >> 32));
        }
    };

    /// Tracks NetworkSession, PlayerIdentity, and NetworkEntityBinding independently of TransportConnection.
    class MORBI_NETWORK_API SessionRegistry final
    {
    public:
        void Clear() noexcept;

        [[nodiscard]] NetworkSession& CreateSession(
            SessionId sessionId,
            ConnectionHandle connection,
            u64 nowMs,
            WorldInstanceId worldId = 1);

        bool AttachPlayer(
            SessionId sessionId,
            const PlayerIdentity& player);

        bool BindEntity(const NetworkEntityBinding& binding);
        bool UnbindEntity(NetworkId netId);

        bool AssignSessionWorld(
            SessionId sessionId,
            WorldInstanceId worldId,
            u32 sceneAssetHash = 0);

        bool AssignSessionParty(SessionId sessionId, u64 partyId);

        bool SuspendSessionForReconnect(
            SessionId sessionId,
            ConnectionHandle oldConnection,
            u64 nowMs,
            u32 reconnectWindowMs,
            std::span<const u8> expectedReconnectNonce);

        bool TryResumeSession(
            SessionId sessionId,
            ConnectionHandle newConnection,
            u64 nowMs,
            std::span<const u8> presentedReconnectNonce,
            std::span<const u8> nextReconnectNonce);

        bool CloseSession(SessionId sessionId, SessionLifecycleState finalState = SessionLifecycleState::Closed);

        u32 PruneExpired(u64 nowMs, u32 idleTimeoutMs, std::vector<SessionId>* outClosedSessions = nullptr);

        [[nodiscard]] bool TryGetSession(SessionId sessionId, NetworkSession& outSession) const;
        [[nodiscard]] bool TryGetSessionByConnection(ConnectionHandle connection, NetworkSession& outSession) const;
        [[nodiscard]] bool TryGetPlayer(PlayerId playerId, PlayerIdentity& outPlayer) const;
        [[nodiscard]] bool TryGetEntityBinding(NetworkId netId, NetworkEntityBinding& outBinding) const;

        [[nodiscard]] usize GetActiveSessionCount() const noexcept;
        [[nodiscard]] usize GetSuspendedSessionCount() const noexcept;
        [[nodiscard]] usize GetPlayerCount() const noexcept { return m_players.size(); }
        [[nodiscard]] usize GetEntityBindingCount() const noexcept { return m_entities.size(); }

    private:
        std::unordered_map<SessionId, NetworkSession, SessionIdHasher> m_sessions;
        std::unordered_map<u32, SessionId> m_connectionToSession;
        std::unordered_map<PlayerId, PlayerIdentity> m_players;
        std::unordered_map<u64, NetworkEntityBinding> m_entities;
    };
}
