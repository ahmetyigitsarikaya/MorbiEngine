#pragma once

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkConfig.hpp"
#include "Morbi/Network/Protocol/ProtocolTypes.hpp"
#include "Morbi/Network/Security/ConnectionRateLimiter.hpp"
#include "Morbi/Network/Security/CryptoContext.hpp"
#include "Morbi/Network/Security/SecurityTypes.hpp"
#include "Morbi/Network/Session/SessionRegistry.hpp"
#include "Morbi/Network/Transport/ITransportBackend.hpp"

namespace Morbi::Network
{
    struct NetworkMessageEvent
    {
        ConnectionHandle Connection = kInvalidConnectionHandle;
        SessionId Session{};
        PlayerId SenderPlayerId = kInvalidPlayerId;
        NetChannel Channel = NetChannel::GameplayEvents;
        MessageHeader Header{};
        std::span<const u8> Payload;
    };

    using ConnectionStateCallback = std::function<void(const ConnectionInfo&, const std::string& reason)>;
    using SessionClosedCallback = std::function<void(SessionId, ConnectionHandle)>;
    using MessageReceivedCallback = std::function<void(const NetworkMessageEvent&)>;

    /// Manages connection lifecycle, authenticated cryptographic handshake (X25519 + Ed25519),
    /// AEAD packet protection (XChaCha20-Poly1305-IETF), replay windowing, rate limiting, and session registry.
    class MORBI_NETWORK_API ConnectionManager final
    {
    public:
        explicit ConnectionManager(std::unique_ptr<ITransportBackend> backend = nullptr);
        ~ConnectionManager();

        ConnectionManager(const ConnectionManager&) = delete;
        ConnectionManager& operator=(const ConnectionManager&) = delete;

        NetResult<bool> Initialize(const ConnectionConfig& config);
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
        [[nodiscard]] NetworkMode GetNetworkMode() const noexcept { return m_config.Mode; }

        NetResult<ConnectionHandle> Connect(const Endpoint& remote, const ConnectAuthOptions& auth = {});
        NetResult<ConnectionHandle> Reconnect(
            const Endpoint& remote,
            SessionId sessionId,
            std::span<const u8> reconnectTicket,
            std::span<const u8> connectToken = {});

        void Disconnect(
            ConnectionHandle connection,
            const std::string& reason = "Disconnected",
            bool allowReconnect = true);

        void Poll();

        SendResult SendPacket(
            ConnectionHandle connection,
            MessageCategory category,
            u8 subType,
            std::span<const u8> payload,
            NetDelivery delivery = NetDelivery::ReliableOrdered,
            NetChannel channel = NetChannel::GameplayEvents);

        void SetOnConnected(ConnectionStateCallback callback) { m_onConnected = std::move(callback); }
        void SetOnDisconnected(ConnectionStateCallback callback) { m_onDisconnected = std::move(callback); }
        void SetOnSessionClosed(SessionClosedCallback callback) { m_onSessionClosed = std::move(callback); }
        void SetOnMessageReceived(MessageReceivedCallback callback) { m_onMessageReceived = std::move(callback); }

        void SetSimulationConfig(const NetSimConfig& sim);

        [[nodiscard]] bool TryGetConnectionInfo(ConnectionHandle connection, ConnectionInfo& outInfo) const;
        [[nodiscard]] bool TryGetTransportConnection(ConnectionHandle connection, TransportConnection& outTransport) const;
        [[nodiscard]] bool TryGetAuthenticatedConnection(ConnectionHandle connection, AuthenticatedConnection& outAuth) const;
        [[nodiscard]] bool TryGetLastReconnectTicket(ConnectionHandle connection, std::vector<u8>& outTicket) const;
        [[nodiscard]] std::vector<ConnectionHandle> GetConnections() const;
        [[nodiscard]] usize GetConnectionCount() const noexcept { return m_connections.size(); }

        [[nodiscard]] const std::array<u8, kEd25519PublicKeyBytes>& GetServerIdentityPublicKey() const noexcept
        {
            return m_serverIdentityKeys.PublicKey;
        }
        [[nodiscard]] const SecurityCounters& GetSecurityCounters() const noexcept { return m_securityCounters; }
        [[nodiscard]] SessionRegistry& GetSessionRegistry() noexcept { return m_sessionRegistry; }
        [[nodiscard]] const SessionRegistry& GetSessionRegistry() const noexcept { return m_sessionRegistry; }
        bool RotateReconnectKey() noexcept { return m_ticketManager.RotateKey(); }

    private:
        struct SinkAdapter;
        friend struct SinkAdapter;

        struct ManagedConnection
        {
            TransportConnection Transport{};
            AuthenticatedConnection Auth{};
            SessionId CryptoSession{};
            PlayerId PlayerIdValue = kInvalidPlayerId;
            u32 OutgoingMessageSequence = 0;
            SessionAeadKeys Keys{};
            ConnectAuthOptions ClientAuth{};
            std::array<u8, kHandshakeNonceBytes> ClientNonce{};
            std::array<u8, kHandshakeNonceBytes> ServerNonce{};
            X25519KeyPair LocalEphemeral{};
            std::array<u8, kEd25519PublicKeyBytes> PeerServerIdentityPubKey{};
            std::vector<u8> TranscriptBytes;
            std::vector<u8> ClientReconnectTicket;
            std::array<u8, kReconnectNonceBytes> ServerReconnectNonce{};
            bool HandshakeSlotTracked = false;
            bool SecurityRevoked = false;
            bool AllowReconnect = true;
            bool WasAuthenticated = false;

            void WipeSecrets() noexcept;
        };

        void OnTransportConnected(ConnectionHandle connection, const Endpoint& remote);
        void OnTransportDisconnected(ConnectionHandle connection, u32 reasonCode);
        void OnTransportPacketReceived(
            ConnectionHandle connection,
            NetChannel channel,
            std::span<const u8> payload);

        [[nodiscard]] ConnectionInfo BuildConnectionInfo(const ManagedConnection& conn) const;

        void RejectAndDisconnect(
            ManagedConnection& conn,
            ControlMessageKind noticeKind,
            NetErrorCode errorCode,
            std::string_view reason,
            bool trackIpViolation);

        void SendHandshakePlaintextControl(
            ConnectionHandle connection,
            ControlMessageKind kind,
            std::span<const u8> body);

        SendResult SendEncryptedFrame(
            ManagedConnection& conn,
            bool isHandshake,
            MessageCategory category,
            u8 subType,
            std::span<const u8> payload,
            NetDelivery delivery,
            NetChannel channel);

        void HandlePlaintextHandshakePacket(
            ManagedConnection& conn,
            const WireEnvelopeHeader& envelope,
            std::span<const u8> innerBytes);

        void HandleEncryptedPacket(
            ManagedConnection& conn,
            NetChannel channel,
            const WireEnvelopeHeader& envelope,
            std::span<const u8> ciphertextBytes);

        void HandleServerClientHello(ManagedConnection& conn, std::span<const u8> body);
        void HandleClientServerChallenge(ManagedConnection& conn, std::span<const u8> body);
        void HandleServerClientAuth(ManagedConnection& conn, const MessageHeader& header, std::span<const u8> body);
        void HandleClientServerAuthAccept(ManagedConnection& conn, std::span<const u8> body);
        void HandlePeerReject(ManagedConnection& conn, ControlMessageKind kind, std::span<const u8> body);

        bool TrySpendConnectToken(const SignedConnectToken& token, u64 nowUnixMs);
        void PruneSpentTokens(u64 nowUnixMs);
        void CheckTimeoutsAndPrune(u64 nowMs);

        std::unique_ptr<ITransportBackend> m_backend;
        ConnectionConfig m_config{};
        std::unordered_map<u32, ManagedConnection> m_connections;
        std::unordered_map<std::string, u64> m_spentTokenIds;

        Ed25519KeyPair m_serverIdentityKeys{};
        ReconnectTicketManager m_ticketManager{};
        ConnectionRateLimiter m_rateLimiter{};
        SessionRegistry m_sessionRegistry{};
        SecurityCounters m_securityCounters{};
        u64 m_lastPruneMs = 0;

        bool m_initialized = false;
        bool m_isServer = false;

        ConnectionStateCallback m_onConnected;
        ConnectionStateCallback m_onDisconnected;
        SessionClosedCallback m_onSessionClosed;
        MessageReceivedCallback m_onMessageReceived;
    };
}
