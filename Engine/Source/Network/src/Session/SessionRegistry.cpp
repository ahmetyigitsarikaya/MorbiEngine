#include "Morbi/Network/Session/SessionRegistry.hpp"

#include <algorithm>
#include <cstring>

#include "Morbi/Network/Security/CryptoContext.hpp"

namespace Morbi::Network
{
    void SessionRegistry::Clear() noexcept
    {
        m_sessions.clear();
        m_connectionToSession.clear();
        m_players.clear();
        m_entities.clear();
    }

    NetworkSession& SessionRegistry::CreateSession(
        SessionId sessionId,
        ConnectionHandle connection,
        u64 nowMs,
        WorldInstanceId worldId)
    {
        NetworkSession session{};
        session.Id = sessionId;
        session.PrimaryConnection = connection;
        session.WorldId = worldId;
        session.LifecycleState = SessionLifecycleState::Active;
        session.CreatedAtMs = nowMs;
        session.LastActivityMs = nowMs;

        auto [it, inserted] = m_sessions.insert_or_assign(sessionId, std::move(session));
        (void)inserted;
        if (connection.IsValid())
        {
            m_connectionToSession[connection.Value] = sessionId;
        }
        return it->second;
    }

    bool SessionRegistry::AttachPlayer(
        SessionId sessionId,
        const PlayerIdentity& player)
    {
        if (player.Id == kInvalidPlayerId)
        {
            return false;
        }
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end() || it->second.LifecycleState == SessionLifecycleState::Closed)
        {
            return false;
        }

        PlayerIdentity stored = player;
        stored.BoundSession = sessionId;
        m_players.insert_or_assign(player.Id, std::move(stored));

        auto& players = it->second.Players;
        if (std::find(players.begin(), players.end(), player.Id) == players.end())
        {
            players.push_back(player.Id);
        }
        return true;
    }

    bool SessionRegistry::BindEntity(const NetworkEntityBinding& binding)
    {
        if (!binding.NetId.IsValid() || !binding.OwnerSession.IsValid())
        {
            return false;
        }
        auto it = m_sessions.find(binding.OwnerSession);
        if (it == m_sessions.end())
        {
            return false;
        }

        m_entities.insert_or_assign(binding.NetId.Pack(), binding);
        auto& owned = it->second.OwnedEntities;
        if (std::find(owned.begin(), owned.end(), binding.NetId) == owned.end())
        {
            owned.push_back(binding.NetId);
        }
        return true;
    }

    bool SessionRegistry::UnbindEntity(NetworkId netId)
    {
        auto entIt = m_entities.find(netId.Pack());
        if (entIt == m_entities.end())
        {
            return false;
        }
        auto sessIt = m_sessions.find(entIt->second.OwnerSession);
        if (sessIt != m_sessions.end())
        {
            auto& owned = sessIt->second.OwnedEntities;
            owned.erase(std::remove(owned.begin(), owned.end(), netId), owned.end());
        }
        m_entities.erase(entIt);
        return true;
    }

    bool SessionRegistry::AssignSessionWorld(
        SessionId sessionId,
        WorldInstanceId worldId,
        u32 sceneAssetHash)
    {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return false;
        }
        it->second.WorldId = worldId;
        it->second.SceneAssetHash = sceneAssetHash;
        return true;
    }

    bool SessionRegistry::AssignSessionParty(SessionId sessionId, u64 partyId)
    {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return false;
        }
        it->second.PartyId = partyId;
        return true;
    }

    bool SessionRegistry::SuspendSessionForReconnect(
        SessionId sessionId,
        ConnectionHandle oldConnection,
        u64 nowMs,
        u32 reconnectWindowMs,
        std::span<const u8> expectedReconnectNonce)
    {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end() || it->second.LifecycleState != SessionLifecycleState::Active)
        {
            return false;
        }
        if (oldConnection.IsValid())
        {
            m_connectionToSession.erase(oldConnection.Value);
        }
        if (expectedReconnectNonce.size() != kReconnectNonceBytes || reconnectWindowMs == 0)
        {
            return false;
        }

        it->second.PrimaryConnection = kInvalidConnectionHandle;
        it->second.LifecycleState = SessionLifecycleState::SuspendedReconnectable;
        it->second.LastActivityMs = nowMs;
        it->second.ReconnectExpiresAtMs = nowMs + static_cast<u64>(reconnectWindowMs);
        std::memcpy(it->second.ExpectedReconnectNonce.data(), expectedReconnectNonce.data(), kReconnectNonceBytes);
        return true;
    }

    bool SessionRegistry::TryResumeSession(
        SessionId sessionId,
        ConnectionHandle newConnection,
        u64 nowMs,
        std::span<const u8> presentedReconnectNonce,
        std::span<const u8> nextReconnectNonce)
    {
        if (!newConnection.IsValid() ||
            presentedReconnectNonce.size() != kReconnectNonceBytes ||
            nextReconnectNonce.size() != kReconnectNonceBytes)
        {
            return false;
        }

        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return false;
        }
        NetworkSession& session = it->second;
        if (session.LifecycleState != SessionLifecycleState::SuspendedReconnectable)
        {
            return false;
        }
        if (nowMs > session.ReconnectExpiresAtMs)
        {
            session.LifecycleState = SessionLifecycleState::Expired;
            return false;
        }
        if (!CryptoContext::ConstantTimeEquals(presentedReconnectNonce, session.ExpectedReconnectNonce))
        {
            return false;
        }

        session.PrimaryConnection = newConnection;
        session.LifecycleState = SessionLifecycleState::Active;
        session.LastActivityMs = nowMs;
        session.ReconnectExpiresAtMs = 0;
        std::memcpy(session.ExpectedReconnectNonce.data(), nextReconnectNonce.data(), kReconnectNonceBytes);
        m_connectionToSession[newConnection.Value] = sessionId;
        return true;
    }

    bool SessionRegistry::CloseSession(SessionId sessionId, SessionLifecycleState finalState)
    {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return false;
        }
        if (it->second.PrimaryConnection.IsValid())
        {
            m_connectionToSession.erase(it->second.PrimaryConnection.Value);
        }
        for (const PlayerId pid : it->second.Players)
        {
            m_players.erase(pid);
        }
        for (const NetworkId netId : it->second.OwnedEntities)
        {
            m_entities.erase(netId.Pack());
        }
        it->second.PrimaryConnection = kInvalidConnectionHandle;
        it->second.LifecycleState = finalState;
        m_sessions.erase(it);
        return true;
    }

    u32 SessionRegistry::PruneExpired(u64 nowMs, u32 idleTimeoutMs, std::vector<SessionId>* outClosedSessions)
    {
        std::vector<SessionId> toRemove;
        for (auto& [id, sess] : m_sessions)
        {
            if (sess.LifecycleState == SessionLifecycleState::SuspendedReconnectable &&
                nowMs > sess.ReconnectExpiresAtMs)
            {
                sess.LifecycleState = SessionLifecycleState::Expired;
                toRemove.push_back(id);
            }
            else if (idleTimeoutMs > 0 &&
                sess.LifecycleState == SessionLifecycleState::Active &&
                nowMs > sess.LastActivityMs &&
                (nowMs - sess.LastActivityMs) > idleTimeoutMs)
            {
                sess.LifecycleState = SessionLifecycleState::Expired;
                toRemove.push_back(id);
            }
        }

        for (const SessionId& id : toRemove)
        {
            (void)CloseSession(id, SessionLifecycleState::Expired);
            if (outClosedSessions != nullptr)
            {
                outClosedSessions->push_back(id);
            }
        }
        return static_cast<u32>(toRemove.size());
    }

    bool SessionRegistry::TryGetSession(SessionId sessionId, NetworkSession& outSession) const
    {
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end())
        {
            return false;
        }
        outSession = it->second;
        return true;
    }

    bool SessionRegistry::TryGetSessionByConnection(ConnectionHandle connection, NetworkSession& outSession) const
    {
        auto mapIt = m_connectionToSession.find(connection.Value);
        if (mapIt == m_connectionToSession.end())
        {
            return false;
        }
        return TryGetSession(mapIt->second, outSession);
    }

    bool SessionRegistry::TryGetPlayer(PlayerId playerId, PlayerIdentity& outPlayer) const
    {
        auto it = m_players.find(playerId);
        if (it == m_players.end())
        {
            return false;
        }
        outPlayer = it->second;
        return true;
    }

    bool SessionRegistry::TryGetEntityBinding(NetworkId netId, NetworkEntityBinding& outBinding) const
    {
        auto it = m_entities.find(netId.Pack());
        if (it == m_entities.end())
        {
            return false;
        }
        outBinding = it->second;
        return true;
    }

    usize SessionRegistry::GetActiveSessionCount() const noexcept
    {
        usize count = 0;
        for (const auto& [id, sess] : m_sessions)
        {
            (void)id;
            if (sess.LifecycleState == SessionLifecycleState::Active)
            {
                ++count;
            }
        }
        return count;
    }

    usize SessionRegistry::GetSuspendedSessionCount() const noexcept
    {
        usize count = 0;
        for (const auto& [id, sess] : m_sessions)
        {
            (void)id;
            if (sess.LifecycleState == SessionLifecycleState::SuspendedReconnectable)
            {
                ++count;
            }
        }
        return count;
    }
}
