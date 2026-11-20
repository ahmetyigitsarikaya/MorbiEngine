#pragma once

#include <array>
#include <string>
#include <vector>

#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Replication/ReplicationContracts.hpp"
#include "Morbi/Network/Security/ConnectionRateLimiter.hpp"
#include "Morbi/Network/Security/ReplayProtection.hpp"
#include "Morbi/Network/Security/SecurityTypes.hpp"

namespace Morbi::Network
{
    enum class SessionLifecycleState : u8
    {
        Active = 0,
        SuspendedReconnectable = 1,
        Expired = 2,
        Closed = 3
    };

    /// Layer 1: Raw transport peer tracking (handle, remote address, timeouts, rate budget).
    struct TransportConnection
    {
        ConnectionHandle Handle = kInvalidConnectionHandle;
        Endpoint RemoteEndpoint{};
        ConnectionState State = ConnectionState::Disconnected;
        u64 ConnectedAtMs = 0;
        u64 HandshakeStartedMs = 0;
        u64 DisconnectStartedMs = 0;
        u64 LastPacketReceivedMs = 0;
        NetworkStats Stats{};
        ConnectionPacketBudget PacketBudget{};
    };

    /// Layer 2: Cryptographically authenticated channel state bound to a ConnectionHandle + SessionId.
    struct AuthenticatedConnection
    {
        ConnectionHandle Handle = kInvalidConnectionHandle;
        SessionId Session = kInvalidSessionId;
        AuthMethod Method = AuthMethod::None;
        u16 NegotiatedProtocolVersion = 0;
        u32 KeyEpoch = 0;
        u64 AuthenticatedAtMs = 0;
        u64 TxPacketSequence = 0;
        ReplayWindow Replay{};
    };

    /// Layer 3: Logical player principal decoupled from transient socket connections.
    struct PlayerIdentity
    {
        PlayerId Id = kInvalidPlayerId;
        std::string DisplayName;
        u32 Roles = 0;
        bool VerifiedByToken = false;
        u64 AuthenticatedAtMs = 0;
        SessionId BoundSession = kInvalidSessionId;
    };

    /// Layer 4: Logical gameplay/match/dungeon session that can outlive a transport drop.
    struct NetworkSession
    {
        SessionId Id = kInvalidSessionId;
        ConnectionHandle PrimaryConnection = kInvalidConnectionHandle;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        u64 PartyId = 0;
        u32 SceneAssetHash = 0;
        SessionLifecycleState LifecycleState = SessionLifecycleState::Active;
        std::vector<PlayerId> Players;
        std::vector<NetworkId> OwnedEntities;
        std::array<u8, kReconnectNonceBytes> ExpectedReconnectNonce{};
        u64 CreatedAtMs = 0;
        u64 LastActivityMs = 0;
        u64 ReconnectExpiresAtMs = 0;
    };

    /// Layer 5: Network Entity ownership binding decoupled from concrete Scene/ECS storage.
    struct NetworkEntityBinding
    {
        NetworkId NetId = kInvalidNetworkId;
        SessionId OwnerSession = kInvalidSessionId;
        PlayerId OwnerPlayer = kInvalidPlayerId;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        u32 SceneEntityIndex = 0;
        u32 SceneEntityGeneration = 0;
        NetworkAuthority Authority = NetworkAuthority::Server;
    };
}
