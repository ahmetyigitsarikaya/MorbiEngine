#include "Morbi/Network/Connection/ConnectionManager.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "Morbi/Core/Log.hpp"

namespace Morbi::Network
{
    namespace
    {
        [[nodiscard]] u64 NowSteadyMs() noexcept
        {
            return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        [[nodiscard]] bool HasDisallowedControlChars(std::string_view s) noexcept
        {
            for (const char c : s)
            {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || uc == 0x7F)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::string SanitizeDisplayName(std::string_view raw)
        {
            std::string cleaned;
            cleaned.reserve((std::min)(raw.size(), kMaxPlayerNameBytes));
            for (const char c : raw)
            {
                if (cleaned.size() >= kMaxPlayerNameBytes)
                {
                    break;
                }
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc >= 0x20 && uc != 0x7F)
                {
                    cleaned.push_back(c);
                }
            }
            if (cleaned.empty())
            {
                return "Player";
            }
            return cleaned;
        }
    }

    void ConnectionManager::ManagedConnection::WipeSecrets() noexcept
    {
        Keys.Wipe();
        LocalEphemeral.SecretKey.Wipe();
        if (!TranscriptBytes.empty())
        {
            WipeSensitiveBytes(TranscriptBytes.data(), TranscriptBytes.size());
            TranscriptBytes.clear();
        }
        if (!ClientAuth.EncodedConnectToken.empty())
        {
            WipeSensitiveBytes(ClientAuth.EncodedConnectToken.data(), ClientAuth.EncodedConnectToken.size());
            ClientAuth.EncodedConnectToken.clear();
        }
        if (!ClientAuth.ReconnectTicket.empty())
        {
            WipeSensitiveBytes(ClientAuth.ReconnectTicket.data(), ClientAuth.ReconnectTicket.size());
            ClientAuth.ReconnectTicket.clear();
        }
        if (!ClientReconnectTicket.empty())
        {
            WipeSensitiveBytes(ClientReconnectTicket.data(), ClientReconnectTicket.size());
            ClientReconnectTicket.clear();
        }
        WipeSensitiveBytes(ServerReconnectNonce.data(), ServerReconnectNonce.size());
    }

    struct ConnectionManager::SinkAdapter final : public ITransportEventSink
    {
        ConnectionManager& Owner;
        explicit SinkAdapter(ConnectionManager& owner) noexcept : Owner(owner) {}

        void OnTransportConnected(ConnectionHandle connection, const Endpoint& remote) override
        {
            Owner.OnTransportConnected(connection, remote);
        }

        void OnTransportDisconnected(ConnectionHandle connection, u32 reasonCode) override
        {
            Owner.OnTransportDisconnected(connection, reasonCode);
        }

        void OnTransportPacketReceived(
            ConnectionHandle connection,
            NetChannel channel,
            std::span<const u8> payload) override
        {
            Owner.OnTransportPacketReceived(connection, channel, payload);
        }
    };

    ConnectionManager::ConnectionManager(std::unique_ptr<ITransportBackend> backend)
        : m_backend(backend ? std::move(backend) : CreateEnetTransportBackend())
    {
    }

    ConnectionManager::~ConnectionManager()
    {
        Shutdown();
    }

    NetResult<bool> ConnectionManager::Initialize(const ConnectionConfig& config)
    {
        if (m_initialized)
        {
            return NetResult<bool>::Failure({ NetErrorCode::AlreadyInitialized, "ConnectionManager already initialized." });
        }
        if (config.Mode == NetworkMode::Standalone)
        {
            return NetResult<bool>::Failure({ NetErrorCode::InvalidArgument, "Cannot initialize ConnectionManager in Standalone mode." });
        }
        if (config.MinProtocolVersion == 0 || config.MinProtocolVersion > config.MaxProtocolVersion)
        {
            return NetResult<bool>::Failure({ NetErrorCode::InvalidArgument, "Invalid protocol version range." });
        }
        if (!CryptoContext::EnsureInitialized())
        {
            return NetResult<bool>::Failure({ NetErrorCode::CryptoError, "Failed to initialize libsodium crypto runtime." });
        }

        m_isServer = (config.Mode == NetworkMode::DedicatedServer || config.Mode == NetworkMode::ListenServer);

        m_serverIdentityKeys = Ed25519KeyPair{};
        if (m_isServer)
        {
            if (config.Security.HasServerIdentityKeyPair)
            {
                m_serverIdentityKeys.PublicKey = config.Security.ServerIdentityPublicKey;
                m_serverIdentityKeys.SecretKey.Bytes = config.Security.ServerIdentitySecretKey;
            }
            else if (!CryptoContext::GenerateEd25519KeyPair(m_serverIdentityKeys))
            {
                return NetResult<bool>::Failure({ NetErrorCode::CryptoError, "Failed to generate server Ed25519 identity keypair." });
            }

            if (!m_ticketManager.Initialize())
            {
                m_serverIdentityKeys.SecretKey.Wipe();
                return NetResult<bool>::Failure({ NetErrorCode::CryptoError, "Failed to initialize ReconnectTicketManager." });
            }
        }

        auto initRes = m_backend->Initialize(config.Transport, m_isServer);
        if (!initRes)
        {
            m_serverIdentityKeys.SecretKey.Wipe();
            m_ticketManager.Reset();
            return initRes;
        }

        m_config = config;
        m_connections.clear();
        m_spentTokenIds.clear();
        m_sessionRegistry.Clear();
        m_rateLimiter.Configure(config.Security.RateLimit);
        m_securityCounters = SecurityCounters{};
        m_lastPruneMs = NowSteadyMs();
        m_initialized = true;
        return NetResult<bool>::Success(true);
    }

    void ConnectionManager::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        for (auto& [handleVal, conn] : m_connections)
        {
            (void)handleVal;
            conn.WipeSecrets();
        }
        m_connections.clear();
        m_spentTokenIds.clear();
        m_sessionRegistry.Clear();
        m_rateLimiter.Reset();
        m_ticketManager.Reset();
        m_serverIdentityKeys.SecretKey.Wipe();
        m_serverIdentityKeys.PublicKey.fill(0);

        if (m_backend)
        {
            m_backend->Shutdown();
        }
        m_initialized = false;
    }

    NetResult<ConnectionHandle> ConnectionManager::Connect(const Endpoint& remote, const ConnectAuthOptions& auth)
    {
        if (!m_initialized)
        {
            return NetResult<ConnectionHandle>::Failure({ NetErrorCode::NotInitialized, "ConnectionManager is not initialized." });
        }
        if (m_isServer)
        {
            return NetResult<ConnectionHandle>::Failure({ NetErrorCode::InvalidArgument, "Server mode cannot initiate outbound client connect." });
        }
        if (auth.EncodedConnectToken.size() > 512 || auth.ReconnectTicket.size() > 512)
        {
            return NetResult<ConnectionHandle>::Failure({ NetErrorCode::PayloadTooLarge, "Auth token or reconnect ticket exceeds maximum size." });
        }

        auto res = m_backend->Connect(remote);
        if (!res)
        {
            return res;
        }

        const u64 nowMs = NowSteadyMs();
        ManagedConnection managed{};
        managed.Transport.Handle = res.Value();
        managed.Transport.RemoteEndpoint = remote;
        managed.Transport.State = ConnectionState::Connecting;
        managed.Transport.ConnectedAtMs = nowMs;
        managed.Transport.HandshakeStartedMs = nowMs;
        managed.Transport.LastPacketReceivedMs = nowMs;
        managed.ClientAuth = auth;
        managed.ClientAuth.PlayerDisplayName = SanitizeDisplayName(auth.PlayerDisplayName);

        m_connections.emplace(res.Value().Value, std::move(managed));
        return res;
    }

    NetResult<ConnectionHandle> ConnectionManager::Reconnect(
        const Endpoint& remote,
        SessionId sessionId,
        std::span<const u8> reconnectTicket,
        std::span<const u8> connectToken)
    {
        if (!sessionId.IsValid() || reconnectTicket.empty() || reconnectTicket.size() > 512)
        {
            return NetResult<ConnectionHandle>::Failure({ NetErrorCode::InvalidArgument, "Invalid session or reconnect ticket." });
        }

        ConnectAuthOptions opts{};
        opts.IsReconnect = true;
        opts.ResumeSessionId = sessionId;
        opts.ReconnectTicket.assign(reconnectTicket.begin(), reconnectTicket.end());
        opts.EncodedConnectToken.assign(connectToken.begin(), connectToken.end());
        return Connect(remote, opts);
    }

    void ConnectionManager::Disconnect(
        ConnectionHandle connection,
        const std::string& reason,
        bool allowReconnect)
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return;
        }

        it->second.AllowReconnect = allowReconnect;
        if (!allowReconnect)
        {
            // Mark local session non-resumable on intentional leave (distinct from SecurityRevoked)
        }

        if (it->second.Transport.State == ConnectionState::Connected)
        {
            BitWriter notice(300);
            if (notice.WriteU8(allowReconnect ? 1u : 0u) && notice.WriteString(reason, 256))
            {
                (void)SendEncryptedFrame(
                    it->second,
                    false,
                    MessageCategory::Control,
                    static_cast<u8>(ControlMessageKind::DisconnectNotice),
                    notice.GetData(),
                    NetDelivery::ReliableOrdered,
                    NetChannel::Control);
            }
        }
        it->second.Transport.State = ConnectionState::Disconnecting;
        it->second.Transport.DisconnectStartedMs = NowSteadyMs();
        m_backend->Disconnect(connection, 0, false);
    }

    void ConnectionManager::Poll()
    {
        if (!m_initialized || !m_backend)
        {
            return;
        }

        SinkAdapter sink(*this);
        m_backend->Poll(sink);
        CheckTimeoutsAndPrune(NowSteadyMs());
    }

    SendResult ConnectionManager::SendPacket(
        ConnectionHandle connection,
        MessageCategory category,
        u8 subType,
        std::span<const u8> payload,
        NetDelivery delivery,
        NetChannel channel)
    {
        if (!m_initialized)
        {
            return { SendStatus::Failed, { NetErrorCode::NotInitialized, "ConnectionManager not initialized." } };
        }
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return { SendStatus::Failed, { NetErrorCode::InvalidConnection, "Unknown connection handle." } };
        }
        if (it->second.Transport.State != ConnectionState::Connected)
        {
            return { SendStatus::Failed, { NetErrorCode::NotConnected, "Encrypted session is not established." } };
        }
        if (category == MessageCategory::Control)
        {
            return { SendStatus::Failed, { NetErrorCode::InvalidArgument, "Control channel is managed internally." } };
        }
        if (payload.size() > ProtocolConstants::kMaxMessageBytes - ProtocolCodec::kEncodedHeaderBytes)
        {
            return { SendStatus::Failed, { NetErrorCode::PayloadTooLarge, "Message exceeds maximum framed payload." } };
        }

        return SendEncryptedFrame(it->second, false, category, subType, payload, delivery, channel);
    }

    void ConnectionManager::SetSimulationConfig(const NetSimConfig& sim)
    {
        m_config.Transport.Simulation = sim;
        if (m_backend)
        {
            m_backend->SetSimulationConfig(sim);
        }
    }

    bool ConnectionManager::TryGetConnectionInfo(ConnectionHandle connection, ConnectionInfo& outInfo) const
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return false;
        }
        outInfo = BuildConnectionInfo(it->second);
        return true;
    }

    bool ConnectionManager::TryGetTransportConnection(ConnectionHandle connection, TransportConnection& outTransport) const
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return false;
        }
        outTransport = it->second.Transport;
        if (m_backend)
        {
            (void)m_backend->TryGetConnectionStats(connection, outTransport.Stats);
        }
        return true;
    }

    bool ConnectionManager::TryGetAuthenticatedConnection(ConnectionHandle connection, AuthenticatedConnection& outAuth) const
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end() || it->second.Transport.State != ConnectionState::Connected)
        {
            return false;
        }
        outAuth = it->second.Auth;
        return true;
    }

    bool ConnectionManager::TryGetLastReconnectTicket(ConnectionHandle connection, std::vector<u8>& outTicket) const
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end() || it->second.ClientReconnectTicket.empty())
        {
            return false;
        }
        outTicket = it->second.ClientReconnectTicket;
        return true;
    }

    std::vector<ConnectionHandle> ConnectionManager::GetConnections() const
    {
        std::vector<ConnectionHandle> result;
        result.reserve(m_connections.size());
        for (const auto& [id, conn] : m_connections)
        {
            (void)conn;
            result.push_back(ConnectionHandle{ id });
        }
        return result;
    }

    ConnectionInfo ConnectionManager::BuildConnectionInfo(const ManagedConnection& conn) const
    {
        ConnectionInfo info{};
        info.Handle = conn.Transport.Handle;
        info.Session = conn.Auth.Session;
        info.AuthenticatedPlayerId = conn.PlayerIdValue;
        info.RemoteEndpoint = conn.Transport.RemoteEndpoint;
        info.State = conn.Transport.State;
        info.NegotiatedProtocolVersion = conn.Auth.NegotiatedProtocolVersion;
        info.IsEncrypted = (conn.Transport.State == ConnectionState::Connected);
        info.IsAuthenticated = (conn.Transport.State == ConnectionState::Connected);
        info.Stats = conn.Transport.Stats;
        if (m_backend)
        {
            (void)m_backend->TryGetConnectionStats(conn.Transport.Handle, info.Stats);
        }
        return info;
    }

    void ConnectionManager::OnTransportConnected(ConnectionHandle connection, const Endpoint& remote)
    {
        const u64 nowMs = NowSteadyMs();

        if (m_isServer)
        {
            if (!m_rateLimiter.AllowIncomingConnection(remote.Host, nowMs))
            {
                ++m_securityCounters.RateLimitDrops;
                ++m_securityCounters.RejectedHandshakes;
                Core::Log::Warn("Network.Security", "Rate-limited incoming connection from " + remote.Host);
                m_backend->Disconnect(connection, static_cast<u32>(NetErrorCode::RateLimited), true);
                return;
            }

            ManagedConnection& managed = m_connections[connection.Value];
            managed.Transport.Handle = connection;
            managed.Transport.RemoteEndpoint = remote;
            managed.Transport.State = ConnectionState::Handshaking;
            managed.Transport.ConnectedAtMs = nowMs;
            managed.Transport.HandshakeStartedMs = nowMs;
            managed.Transport.LastPacketReceivedMs = nowMs;
            managed.HandshakeSlotTracked = true;
            return;
        }

        auto& managed = m_connections[connection.Value];
        managed.Transport.Handle = connection;
        managed.Transport.RemoteEndpoint = remote;
        managed.Transport.State = ConnectionState::Handshaking;
        managed.Transport.HandshakeStartedMs = nowMs;
        managed.Transport.LastPacketReceivedMs = nowMs;

        if (!CryptoContext::FillRandomBytes(managed.ClientNonce) ||
            !CryptoContext::GenerateX25519KeyPair(managed.LocalEphemeral))
        {
            RejectAndDisconnect(managed, ControlMessageKind::AuthReject, NetErrorCode::CryptoError, "Client crypto failure", false);
            return;
        }

        BitWriter helloBody(256);
        if (!helloBody.WriteU16(m_config.MinProtocolVersion) ||
            !helloBody.WriteU16(m_config.MaxProtocolVersion) ||
            !helloBody.WriteU32(m_config.SchemaHash) ||
            !helloBody.WriteBytes(managed.ClientNonce) ||
            !helloBody.WriteBytes(managed.LocalEphemeral.PublicKey))
        {
            RejectAndDisconnect(managed, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "ClientHello serialize error", false);
            return;
        }

        SendHandshakePlaintextControl(connection, ControlMessageKind::ClientHello, helloBody.GetData());
    }

    void ConnectionManager::OnTransportDisconnected(ConnectionHandle connection, u32 reasonCode)
    {
        (void)reasonCode;
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return;
        }

        ManagedConnection& conn = it->second;
        const bool canResume = conn.WasAuthenticated &&
            conn.AllowReconnect &&
            !conn.SecurityRevoked &&
            conn.Auth.Session.IsValid() &&
            m_config.Security.ReconnectWindowMs > 0;

        if (conn.HandshakeSlotTracked)
        {
            m_rateLimiter.OnHandshakeEnded(conn.Transport.RemoteEndpoint.Host);
            conn.HandshakeSlotTracked = false;
        }

        SessionId closedSessionId = kInvalidSessionId;
        if (m_isServer && conn.Auth.Session.IsValid())
        {
            if (canResume)
            {
                const u64 nowMs = NowSteadyMs();
                if (!m_sessionRegistry.SuspendSessionForReconnect(
                        conn.Auth.Session,
                        connection,
                        nowMs,
                        m_config.Security.ReconnectWindowMs,
                        conn.ServerReconnectNonce))
                {
                    closedSessionId = conn.Auth.Session;
                    (void)m_sessionRegistry.CloseSession(conn.Auth.Session, SessionLifecycleState::Closed);
                }
            }
            else
            {
                closedSessionId = conn.Auth.Session;
                (void)m_sessionRegistry.CloseSession(conn.Auth.Session, SessionLifecycleState::Closed);
            }
        }

        ConnectionInfo info = BuildConnectionInfo(conn);
        info.State = ConnectionState::Disconnected;
        info.IsEncrypted = false;
        info.IsAuthenticated = false;

        conn.WipeSecrets();
        m_connections.erase(it);

        if (closedSessionId.IsValid() && m_onSessionClosed)
        {
            m_onSessionClosed(closedSessionId, connection);
        }

        if (m_onDisconnected)
        {
            m_onDisconnected(info, "Disconnected");
        }
    }

    void ConnectionManager::RejectAndDisconnect(
        ManagedConnection& conn,
        ControlMessageKind noticeKind,
        NetErrorCode errorCode,
        std::string_view reason,
        bool trackIpViolation)
    {
        if (conn.Transport.State == ConnectionState::Disconnecting ||
            conn.Transport.State == ConnectionState::Disconnected)
        {
            return;
        }

        const u64 nowMs = NowSteadyMs();
        conn.SecurityRevoked = true;
        Core::Log::Warn("Network.Security", "Rejecting connection " + std::to_string(conn.Transport.Handle.Value) +
            " (code=" + std::to_string(static_cast<u32>(errorCode)) + "): " + std::string(reason));

        if (conn.HandshakeSlotTracked)
        {
            m_rateLimiter.OnHandshakeEnded(conn.Transport.RemoteEndpoint.Host);
            conn.HandshakeSlotTracked = false;
        }

        if (trackIpViolation)
        {
            (void)m_rateLimiter.RecordSecurityViolation(
                conn.Transport.PacketBudget,
                conn.Transport.RemoteEndpoint.Host,
                nowMs);
        }

        if (conn.Transport.State == ConnectionState::Handshaking ||
            conn.Transport.State == ConnectionState::ChallengeIssued)
        {
            ++m_securityCounters.RejectedHandshakes;
            BitWriter rejectBody(320);
            if (rejectBody.WriteU16(static_cast<u16>(errorCode)) &&
                rejectBody.WriteString(reason, 256))
            {
                SendHandshakePlaintextControl(conn.Transport.Handle, noticeKind, rejectBody.GetData());
            }
        }

        if (m_isServer && conn.Auth.Session.IsValid())
        {
            (void)m_sessionRegistry.CloseSession(conn.Auth.Session, SessionLifecycleState::Closed);
            conn.Auth.Session = kInvalidSessionId;
        }

        conn.WipeSecrets();
        conn.Transport.State = ConnectionState::Disconnecting;
        conn.Transport.DisconnectStartedMs = nowMs;
        m_backend->Disconnect(conn.Transport.Handle, static_cast<u32>(errorCode), false);
    }

    void ConnectionManager::SendHandshakePlaintextControl(
        ConnectionHandle connection,
        ControlMessageKind kind,
        std::span<const u8> body)
    {
        BitWriter packetWriter(ProtocolConstants::kMaxHandshakePacketBytes);
        WireEnvelopeHeader env{};
        env.Magic = ProtocolConstants::kMagic;
        env.Flags = WireEnvelopeFlags::kHandshake;
        env.ProtocolVersion = m_config.MaxProtocolVersion;
        env.PacketSequence = 0;

        MessageHeader inner{};
        inner.Magic = ProtocolConstants::kMagic;
        inner.ProtocolVersion = m_config.MaxProtocolVersion;
        inner.SchemaHash = m_config.SchemaHash;
        inner.Category = MessageCategory::Control;
        inner.SubType = static_cast<u8>(kind);
        inner.Sequence = 0;
        inner.PayloadBytes = static_cast<u32>(body.size());

        if (!ProtocolCodec::WriteWireEnvelope(packetWriter, env) ||
            !ProtocolCodec::WriteHeader(packetWriter, inner) ||
            !packetWriter.WriteBytes(body))
        {
            Core::Log::Error("Network.Security", "SendHandshakePlaintextControl encode failed for kind=" + std::to_string(static_cast<u32>(kind)));
            return;
        }

        const auto sr = m_backend->Send(connection, packetWriter.GetData(), NetDelivery::ReliableOrdered, NetChannel::Control);
        Core::Log::Trace("Network.Security", "Sent plaintext handshake kind=" + std::to_string(static_cast<u32>(kind)) +
            " status=" + std::to_string(static_cast<u32>(sr.Status)));
    }

    SendResult ConnectionManager::SendEncryptedFrame(
        ManagedConnection& conn,
        bool isHandshake,
        MessageCategory category,
        u8 subType,
        std::span<const u8> payload,
        NetDelivery delivery,
        NetChannel channel)
    {
        if (!conn.CryptoSession.IsValid())
        {
            return { SendStatus::Failed, { NetErrorCode::InvalidState, "Missing crypto session." } };
        }

        BitWriter plainWriter(ProtocolCodec::kEncodedHeaderBytes + payload.size());
        MessageHeader inner{};
        inner.Magic = ProtocolConstants::kMagic;
        inner.ProtocolVersion = conn.Auth.NegotiatedProtocolVersion;
        inner.SchemaHash = m_config.SchemaHash;
        inner.Category = category;
        inner.SubType = subType;
        inner.Sequence = ++conn.OutgoingMessageSequence;
        inner.PayloadBytes = static_cast<u32>(payload.size());

        if (!ProtocolCodec::WriteHeader(plainWriter, inner) || !plainWriter.WriteBytes(payload))
        {
            return { SendStatus::Failed, { NetErrorCode::SerializationError, "Failed to encode inner message." } };
        }

        const u64 pktSeq = ++conn.Auth.TxPacketSequence;
        const u8 wireFlags = static_cast<u8>(WireEnvelopeFlags::kEncrypted | (isHandshake ? WireEnvelopeFlags::kHandshake : 0));
        const u8 direction = m_isServer ? kDirectionServerToClient : kDirectionClientToServer;

        std::vector<u8> ciphertext;
        if (!CryptoContext::EncryptPacketAead(
                conn.Keys.TxKey,
                conn.Keys.KeyEpoch,
                conn.CryptoSession,
                direction,
                pktSeq,
                static_cast<u8>(channel),
                wireFlags,
                conn.Auth.NegotiatedProtocolVersion,
                plainWriter.GetData(),
                ciphertext))
        {
            return { SendStatus::Failed, { NetErrorCode::CryptoError, "AEAD packet encryption failed." } };
        }

        BitWriter wireWriter(ProtocolCodec::kEncodedWireEnvelopeBytes + ciphertext.size());
        WireEnvelopeHeader env{};
        env.Magic = ProtocolConstants::kMagic;
        env.Flags = wireFlags;
        env.ProtocolVersion = conn.Auth.NegotiatedProtocolVersion;
        env.PacketSequence = pktSeq;

        if (!ProtocolCodec::WriteWireEnvelope(wireWriter, env) || !wireWriter.WriteBytes(ciphertext))
        {
            return { SendStatus::Failed, { NetErrorCode::SerializationError, "Failed to encode wire envelope." } };
        }

        return m_backend->Send(conn.Transport.Handle, wireWriter.GetData(), delivery, channel);
    }

    void ConnectionManager::OnTransportPacketReceived(
        ConnectionHandle connection,
        NetChannel channel,
        std::span<const u8> payload)
    {
        auto it = m_connections.find(connection.Value);
        if (it == m_connections.end())
        {
            return;
        }

        ManagedConnection& conn = it->second;
        if (conn.Transport.State == ConnectionState::Disconnecting ||
            conn.Transport.State == ConnectionState::Disconnected)
        {
            return;
        }

        const u64 nowMs = NowSteadyMs();
        if (payload.size() > ProtocolConstants::kMaxWirePacketBytes)
        {
            ++m_securityCounters.MalformedDrops;
            Core::Log::Warn("Network.Security", "Dropped oversized wire packet.");
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::PayloadTooLarge, "Oversized packet", true);
            return;
        }

        const bool isAuthenticated = (conn.Transport.State == ConnectionState::Connected);
        if (!m_rateLimiter.AllowPacket(conn.Transport.PacketBudget, payload.size(), nowMs, isAuthenticated))
        {
            ++m_securityCounters.RateLimitDrops;
            Core::Log::Warn("Network.Security", "Packet rate limit exceeded for connection " + std::to_string(connection.Value));
            if (m_rateLimiter.RecordSecurityViolation(conn.Transport.PacketBudget, conn.Transport.RemoteEndpoint.Host, nowMs))
            {
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::RateLimited, "Rate limit exceeded", false);
            }
            return;
        }

        conn.Transport.LastPacketReceivedMs = nowMs;

        BitReader envReader(payload);
        WireEnvelopeHeader envelope{};
        if (!ProtocolCodec::ReadWireEnvelope(envReader, envelope))
        {
            ++m_securityCounters.MalformedDrops;
            Core::Log::Warn("Network.Security", "Dropped malformed wire envelope.");
            if (m_rateLimiter.RecordSecurityViolation(conn.Transport.PacketBudget, conn.Transport.RemoteEndpoint.Host, nowMs))
            {
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed envelope", false);
            }
            return;
        }

        const std::span<const u8> remainingBody = payload.subspan(ProtocolCodec::kEncodedWireEnvelopeBytes);
        const bool flagEncrypted = (envelope.Flags & WireEnvelopeFlags::kEncrypted) != 0;
        const bool flagHandshake = (envelope.Flags & WireEnvelopeFlags::kHandshake) != 0;
        const u8 unknownFlags = static_cast<u8>(envelope.Flags & ~(WireEnvelopeFlags::kEncrypted | WireEnvelopeFlags::kHandshake));
        if (unknownFlags != 0)
        {
            ++m_securityCounters.MalformedDrops;
            if (m_rateLimiter.RecordSecurityViolation(conn.Transport.PacketBudget, conn.Transport.RemoteEndpoint.Host, nowMs))
            {
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Invalid wire flags", false);
            }
            return;
        }

        if (!flagEncrypted)
        {
            if (!flagHandshake ||
                (conn.Transport.State != ConnectionState::Handshaking &&
                 conn.Transport.State != ConnectionState::ChallengeIssued) ||
                channel != NetChannel::Control ||
                payload.size() > ProtocolConstants::kMaxHandshakePacketBytes)
            {
                if (conn.Transport.State == ConnectionState::Connected && flagHandshake)
                {
                    ++m_securityCounters.DuplicateHandshakeDrops;
                }
                else
                {
                    ++m_securityCounters.TamperDrops;
                }
                Core::Log::Warn("Network.Security", "Rejected unencrypted or out-of-state plaintext packet.");
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidState, "Unencrypted packet rejected", true);
                return;
            }
            HandlePlaintextHandshakePacket(conn, envelope, remainingBody);
            return;
        }

        // Encrypted packet path (post-challenge ClientAuth, ServerAuthAccept, or Connected traffic)
        HandleEncryptedPacket(conn, channel, envelope, remainingBody);
    }

    void ConnectionManager::HandlePlaintextHandshakePacket(
        ManagedConnection& conn,
        const WireEnvelopeHeader& envelope,
        std::span<const u8> innerBytes)
    {
        (void)envelope;
        BitReader reader(innerBytes);
        MessageHeader header{};
        if (!ProtocolCodec::ReadHeader(reader, header) || header.Category != MessageCategory::Control)
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed handshake control header", true);
            return;
        }

        std::vector<u8> body(static_cast<usize>(header.PayloadBytes));
        if (!reader.ReadBytes(body))
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Truncated handshake body", true);
            return;
        }

        const auto kind = static_cast<ControlMessageKind>(header.SubType);
        switch (kind)
        {
        case ControlMessageKind::ClientHello:
            if (!m_isServer || conn.Transport.State != ConnectionState::Handshaking)
            {
                ++m_securityCounters.DuplicateHandshakeDrops;
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::DuplicateHandshake, "Unexpected ClientHello", true);
                return;
            }
            HandleServerClientHello(conn, body);
            break;

        case ControlMessageKind::ServerChallenge:
            if (m_isServer || conn.Transport.State != ConnectionState::Handshaking)
            {
                ++m_securityCounters.DuplicateHandshakeDrops;
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::DuplicateHandshake, "Unexpected ServerChallenge", true);
                return;
            }
            HandleClientServerChallenge(conn, body);
            break;

        case ControlMessageKind::VersionReject:
        case ControlMessageKind::AuthReject:
            HandlePeerReject(conn, kind, body);
            break;

        default:
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidState, "Unsupported plaintext control kind", true);
            break;
        }
    }

    void ConnectionManager::HandleEncryptedPacket(
        ManagedConnection& conn,
        NetChannel channel,
        const WireEnvelopeHeader& envelope,
        std::span<const u8> ciphertextBytes)
    {
        if (conn.Transport.State != ConnectionState::ChallengeIssued &&
            conn.Transport.State != ConnectionState::Connected)
        {
            ++m_securityCounters.TamperDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidState, "Encrypted packet before key agreement", true);
            return;
        }

        const bool flagHandshake = (envelope.Flags & WireEnvelopeFlags::kHandshake) != 0;
        if (conn.Transport.State == ConnectionState::Connected && flagHandshake)
        {
            ++m_securityCounters.DuplicateHandshakeDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::DuplicateHandshake, "Handshake packet on established session", true);
            return;
        }
        if (conn.Transport.State == ConnectionState::ChallengeIssued && !flagHandshake)
        {
            ++m_securityCounters.TamperDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidState, "Application packet before auth accept", true);
            return;
        }
        if (envelope.ProtocolVersion != conn.Auth.NegotiatedProtocolVersion)
        {
            ++m_securityCounters.ProtocolMismatchDrops;
            RejectAndDisconnect(conn, ControlMessageKind::VersionReject, NetErrorCode::ProtocolMismatch, "Wire version mismatch", true);
            return;
        }

        // Phase 1 replay check (read-only BEFORE AEAD authentication)
        if (!conn.Auth.Replay.CanAccept(envelope.PacketSequence))
        {
            ++m_securityCounters.ReplayDrops;
            Core::Log::Warn("Network.Security", "Dropped replayed/stale packet seq=" + std::to_string(envelope.PacketSequence));
            if (m_rateLimiter.RecordSecurityViolation(conn.Transport.PacketBudget, conn.Transport.RemoteEndpoint.Host, NowSteadyMs()))
            {
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::ReplayDetected, "Replay violation limit exceeded", false);
            }
            return;
        }

        const u8 expectedDirection = m_isServer ? kDirectionClientToServer : kDirectionServerToClient;
        std::vector<u8> plaintext;
        if (!CryptoContext::DecryptPacketAead(
                conn.Keys.RxKey,
                conn.Keys.KeyEpoch,
                conn.CryptoSession,
                expectedDirection,
                envelope.PacketSequence,
                static_cast<u8>(channel),
                envelope.Flags,
                envelope.ProtocolVersion,
                ciphertextBytes,
                plaintext))
        {
            ++m_securityCounters.TamperDrops;
            Core::Log::Warn("Network.Security", "Dropped packet failing AEAD tag verification.");
            if (conn.Transport.State == ConnectionState::ChallengeIssued)
            {
                ++m_securityCounters.AuthFailures;
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::AuthenticationFailed, "Authentication failed", true);
            }
            else if (m_rateLimiter.RecordSecurityViolation(conn.Transport.PacketBudget, conn.Transport.RemoteEndpoint.Host, NowSteadyMs()))
            {
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::TamperingDetected, "Packet tampering limit exceeded", false);
            }
            return;
        }

        // Phase 2 replay commit ONLY AFTER AEAD succeeded
        conn.Auth.Replay.Commit(envelope.PacketSequence);

        BitReader innerReader(plaintext);
        MessageHeader header{};
        if (!ProtocolCodec::ReadHeader(innerReader, header) ||
            !ProtocolCodec::ValidateHeader(header, conn.Auth.NegotiatedProtocolVersion, m_config.SchemaHash))
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed decrypted payload header", true);
            return;
        }

        std::vector<u8> body(static_cast<usize>(header.PayloadBytes));
        if (!innerReader.ReadBytes(body))
        {
            ++m_securityCounters.MalformedDrops;
            return;
        }

        if (header.Category == MessageCategory::Control)
        {
            const auto kind = static_cast<ControlMessageKind>(header.SubType);
            if (conn.Transport.State == ConnectionState::ChallengeIssued)
            {
                if (m_isServer && kind == ControlMessageKind::ClientAuth)
                {
                    HandleServerClientAuth(conn, header, body);
                }
                else if (!m_isServer && kind == ControlMessageKind::ServerAuthAccept)
                {
                    HandleClientServerAuthAccept(conn, body);
                }
                else
                {
                    ++m_securityCounters.DuplicateHandshakeDrops;
                    RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidState, "Invalid handshake control state", true);
                }
                return;
            }

            if (kind == ControlMessageKind::DisconnectNotice)
            {
                BitReader noticeReader(body);
                u8 allowReconnectByte = 1;
                std::string reason;
                (void)noticeReader.ReadU8(allowReconnectByte);
                (void)noticeReader.ReadString(reason, 256);
                conn.AllowReconnect = (allowReconnectByte != 0);
                const ConnectionHandle h = conn.Transport.Handle;
                m_backend->Disconnect(h, 0, true);
                OnTransportDisconnected(h, 0);
                return;
            }

            ++m_securityCounters.DuplicateHandshakeDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::DuplicateHandshake, "Unexpected control message post-auth", true);
            return;
        }

        if (conn.Transport.State != ConnectionState::Connected)
        {
            ++m_securityCounters.TamperDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidState, "Non-control message before Connected", true);
            return;
        }

        if (m_isServer && conn.Auth.Session.IsValid())
        {
            NetworkSession activeSess{};
            if (m_sessionRegistry.TryGetSession(conn.Auth.Session, activeSess))
            {
                (void)activeSess;
            }
        }

        if (m_onMessageReceived)
        {
            NetworkMessageEvent ev{};
            ev.Connection = conn.Transport.Handle;
            ev.Session = conn.Auth.Session;
            ev.SenderPlayerId = conn.PlayerIdValue;
            ev.Channel = channel;
            ev.Header = header;
            ev.Payload = body;
            m_onMessageReceived(ev);
        }
    }

    void ConnectionManager::HandleServerClientHello(ManagedConnection& conn, std::span<const u8> body)
    {
        BitReader reader(body);
        u16 clientMinVer = 0;
        u16 clientMaxVer = 0;
        u32 clientSchema = 0;
        std::array<u8, kHandshakeNonceBytes> clientNonce{};
        std::array<u8, kX25519PublicKeyBytes> clientEphPub{};

        if (!reader.ReadU16(clientMinVer) ||
            !reader.ReadU16(clientMaxVer) ||
            !reader.ReadU32(clientSchema) ||
            !reader.ReadBytes(clientNonce) ||
            !reader.ReadBytes(clientEphPub) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0 ||
            clientMinVer > clientMaxVer)
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed ClientHello", true);
            return;
        }

        const u16 overlapLow = (std::max)(clientMinVer, m_config.MinProtocolVersion);
        const u16 overlapHigh = (std::min)(clientMaxVer, m_config.MaxProtocolVersion);
        if (overlapLow > overlapHigh || clientSchema != m_config.SchemaHash)
        {
            ++m_securityCounters.ProtocolMismatchDrops;
            RejectAndDisconnect(conn, ControlMessageKind::VersionReject, NetErrorCode::ProtocolMismatch, "Protocol version or schema mismatch", false);
            return;
        }

        const u16 negotiatedVersion = overlapHigh;
        const SessionId candidateSession = CryptoContext::GenerateSessionId();
        if (!candidateSession.IsValid() ||
            !CryptoContext::FillRandomBytes(conn.ServerNonce) ||
            !CryptoContext::GenerateX25519KeyPair(conn.LocalEphemeral))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::CryptoError, "Server keygen failed", false);
            return;
        }

        conn.ClientNonce = clientNonce;
        conn.CryptoSession = candidateSession;
        conn.Auth.NegotiatedProtocolVersion = negotiatedVersion;

        HandshakeTranscriptParams tp{};
        tp.NegotiatedVersion = negotiatedVersion;
        tp.SchemaHash = m_config.SchemaHash;
        tp.CandidateSession = candidateSession;
        tp.ClientNonce = clientNonce;
        tp.ServerNonce = conn.ServerNonce;
        tp.ClientEphemeralPubKey = clientEphPub;
        tp.ServerEphemeralPubKey = conn.LocalEphemeral.PublicKey;
        tp.ServerIdentityPubKey = m_serverIdentityKeys.PublicKey;

        std::array<u8, kEd25519SignatureBytes> challengeSig{};
        if (!CryptoContext::BuildHandshakeTranscript(tp, conn.TranscriptBytes) ||
            !CryptoContext::SignDetached(conn.TranscriptBytes, m_serverIdentityKeys.SecretKey, challengeSig) ||
            !CryptoContext::DeriveServerSessionKeys(conn.LocalEphemeral, clientEphPub, conn.TranscriptBytes, conn.Keys))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::CryptoError, "Key derivation failed", false);
            return;
        }
        conn.LocalEphemeral.SecretKey.Wipe();
        conn.Auth.KeyEpoch = conn.Keys.KeyEpoch;
        conn.Transport.State = ConnectionState::ChallengeIssued;

        BitWriter chalWriter(512);
        const bool requireToken = m_config.Security.RequireSignedConnectToken || m_config.Security.HasTokenIssuerPublicKey;
        if (!chalWriter.WriteU16(negotiatedVersion) ||
            !chalWriter.WriteU32(m_config.SchemaHash) ||
            !chalWriter.WriteU64(candidateSession.High) ||
            !chalWriter.WriteU64(candidateSession.Low) ||
            !chalWriter.WriteBytes(conn.ServerNonce) ||
            !chalWriter.WriteBytes(conn.LocalEphemeral.PublicKey) ||
            !chalWriter.WriteBytes(m_serverIdentityKeys.PublicKey) ||
            !chalWriter.WriteBytes(challengeSig) ||
            !chalWriter.WriteU8(requireToken ? 1u : 0u))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Challenge serialize failed", false);
            return;
        }

        SendHandshakePlaintextControl(conn.Transport.Handle, ControlMessageKind::ServerChallenge, chalWriter.GetData());
    }

    void ConnectionManager::HandleClientServerChallenge(ManagedConnection& conn, std::span<const u8> body)
    {
        BitReader reader(body);
        u16 negotiatedVer = 0;
        u32 serverSchema = 0;
        SessionId candidateSession{};
        std::array<u8, kHandshakeNonceBytes> serverNonce{};
        std::array<u8, kX25519PublicKeyBytes> serverEphPub{};
        std::array<u8, kEd25519PublicKeyBytes> serverIdPub{};
        std::array<u8, kEd25519SignatureBytes> signature{};
        u8 serverRequiresTokenByte = 0;

        if (!reader.ReadU16(negotiatedVer) ||
            !reader.ReadU32(serverSchema) ||
            !reader.ReadU64(candidateSession.High) ||
            !reader.ReadU64(candidateSession.Low) ||
            !reader.ReadBytes(serverNonce) ||
            !reader.ReadBytes(serverEphPub) ||
            !reader.ReadBytes(serverIdPub) ||
            !reader.ReadBytes(signature) ||
            !reader.ReadU8(serverRequiresTokenByte) ||
            serverRequiresTokenByte > 1 ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed ServerChallenge", true);
            return;
        }

        if (negotiatedVer < m_config.MinProtocolVersion ||
            negotiatedVer > m_config.MaxProtocolVersion ||
            serverSchema != m_config.SchemaHash ||
            !candidateSession.IsValid())
        {
            ++m_securityCounters.ProtocolMismatchDrops;
            RejectAndDisconnect(conn, ControlMessageKind::VersionReject, NetErrorCode::ProtocolMismatch, "Invalid server challenge version/schema", false);
            return;
        }

        if (m_config.Security.RequirePinnedServerPublicKey &&
            !CryptoContext::ConstantTimeEquals(serverIdPub, m_config.Security.ExpectedServerPublicKey))
        {
            ++m_securityCounters.AuthFailures;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::AuthenticationFailed, "Server public key pin mismatch", false);
            return;
        }

        HandshakeTranscriptParams tp{};
        tp.NegotiatedVersion = negotiatedVer;
        tp.SchemaHash = serverSchema;
        tp.CandidateSession = candidateSession;
        tp.ClientNonce = conn.ClientNonce;
        tp.ServerNonce = serverNonce;
        tp.ClientEphemeralPubKey = conn.LocalEphemeral.PublicKey;
        tp.ServerEphemeralPubKey = serverEphPub;
        tp.ServerIdentityPubKey = serverIdPub;

        if (!CryptoContext::BuildHandshakeTranscript(tp, conn.TranscriptBytes) ||
            !CryptoContext::VerifyDetached(conn.TranscriptBytes, signature, serverIdPub))
        {
            ++m_securityCounters.AuthFailures;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::AuthenticationFailed, "Invalid server Ed25519 signature", false);
            return;
        }

        if (!CryptoContext::DeriveClientSessionKeys(conn.LocalEphemeral, serverEphPub, conn.TranscriptBytes, conn.Keys))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::CryptoError, "Client key derivation failed", false);
            return;
        }
        conn.LocalEphemeral.SecretKey.Wipe();

        conn.ServerNonce = serverNonce;
        conn.PeerServerIdentityPubKey = serverIdPub;
        conn.CryptoSession = candidateSession;
        conn.Auth.NegotiatedProtocolVersion = negotiatedVer;
        conn.Auth.KeyEpoch = conn.Keys.KeyEpoch;
        conn.Transport.State = ConnectionState::ChallengeIssued;

        std::array<u8, 32> clientProofMac{};
        if (!CryptoContext::ComputeKeyedProofMac(conn.Keys.HandshakeAuthKey, conn.TranscriptBytes, clientProofMac))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::CryptoError, "Failed to compute auth MAC", false);
            return;
        }

        BitWriter authWriter(1536);
        auto writeByteBlob = [&](std::span<const u8> blob, usize maxLen) -> bool {
            if (blob.size() > maxLen)
            {
                return false;
            }
            return authWriter.WriteU32(static_cast<u32>(blob.size())) && authWriter.WriteBytes(blob);
        };

        if (!authWriter.WriteBytes(clientProofMac) ||
            !authWriter.WriteU8(conn.ClientAuth.IsReconnect ? 1u : 0u) ||
            !authWriter.WriteString(conn.ClientAuth.PlayerDisplayName, kMaxPlayerNameBytes) ||
            !writeByteBlob(conn.ClientAuth.EncodedConnectToken, 512) ||
            !authWriter.WriteU64(conn.ClientAuth.ResumeSessionId.High) ||
            !authWriter.WriteU64(conn.ClientAuth.ResumeSessionId.Low) ||
            !writeByteBlob(conn.ClientAuth.ReconnectTicket, 512))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Failed to encode ClientAuth", false);
            return;
        }

        const auto sr = SendEncryptedFrame(
            conn,
            true,
            MessageCategory::Control,
            static_cast<u8>(ControlMessageKind::ClientAuth),
            authWriter.GetData(),
            NetDelivery::ReliableOrdered,
            NetChannel::Control);
        Core::Log::Trace("Network.Security", "Sent ClientAuth status=" + std::to_string(static_cast<u32>(sr.Status)));
    }

    void ConnectionManager::HandleServerClientAuth(
        ManagedConnection& conn,
        const MessageHeader& header,
        std::span<const u8> body)
    {
        (void)header;
        BitReader reader(body);
        std::array<u8, 32> presentedMac{};
        u8 isReconnectByte = 0;
        std::string playerDisplayName;
        std::vector<u8> connectTokenBytes;
        SessionId resumeSession{};
        std::vector<u8> reconnectTicketBytes;

        if (!reader.ReadBytes(presentedMac) ||
            !reader.ReadU8(isReconnectByte) ||
            isReconnectByte > 1 ||
            !reader.ReadString(playerDisplayName, kMaxPlayerNameBytes) ||
            !reader.ReadByteVector(connectTokenBytes, 512) ||
            !reader.ReadU64(resumeSession.High) ||
            !reader.ReadU64(resumeSession.Low) ||
            !reader.ReadByteVector(reconnectTicketBytes, 512) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed ClientAuth", true);
            return;
        }
        const bool isReconnect = (isReconnectByte != 0);

        std::array<u8, 32> expectedMac{};
        if (!CryptoContext::ComputeKeyedProofMac(conn.Keys.HandshakeAuthKey, conn.TranscriptBytes, expectedMac) ||
            !CryptoContext::ConstantTimeEquals(presentedMac, expectedMac))
        {
            ++m_securityCounters.AuthFailures;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::AuthenticationFailed, "Authentication failed", true);
            return;
        }

        const u64 nowSteadyMs = NowSteadyMs();
        const u64 nowUnixMs = CryptoContext::GetUnixNowMs();
        SessionId assignedSession = conn.CryptoSession;
        PlayerIdentity playerIdentity{};
        AuthMethod usedAuthMethod = AuthMethod::EphemeralChallenge;

        if (isReconnect)
        {
            ReconnectTicketPayload ticket{};
            if (!m_ticketManager.VerifyAndUnpackTicket(reconnectTicketBytes, nowSteadyMs, ticket) ||
                ticket.Session != resumeSession)
            {
                ++m_securityCounters.AuthFailures;
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SessionExpired, "Reconnect ticket invalid or expired", true);
                return;
            }

            std::array<u8, kReconnectNonceBytes> nextNonce{};
            if (!CryptoContext::FillRandomBytes(nextNonce) ||
                !m_sessionRegistry.TryResumeSession(
                    resumeSession,
                    conn.Transport.Handle,
                    nowSteadyMs,
                    ticket.ReconnectNonce,
                    nextNonce))
            {
                ++m_securityCounters.AuthFailures;
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SessionExpired, "Session expired or unavailable", true);
                return;
            }

            assignedSession = resumeSession;
            conn.ServerReconnectNonce = nextNonce;
            if (!m_sessionRegistry.TryGetPlayer(ticket.PlayerIdValue, playerIdentity))
            {
                playerIdentity.Id = ticket.PlayerIdValue;
                playerIdentity.DisplayName = SanitizeDisplayName(ticket.PlayerName);
                playerIdentity.AuthenticatedAtMs = nowSteadyMs;
                playerIdentity.BoundSession = assignedSession;
            }
            usedAuthMethod = AuthMethod::ReconnectTicket;
            ++m_securityCounters.SuccessfulReconnects;
        }
        else
        {
            const bool tokenRequired = m_config.Security.RequireSignedConnectToken || m_config.Security.HasTokenIssuerPublicKey;
            if (tokenRequired)
            {
                if (!m_config.Security.HasTokenIssuerPublicKey || connectTokenBytes.empty())
                {
                    ++m_securityCounters.AuthFailures;
                    RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::AuthenticationFailed, "Connect token required", true);
                    return;
                }

                SignedConnectToken token{};
                if (!CryptoContext::DecodeSignedConnectToken(connectTokenBytes, token) ||
                    !CryptoContext::VerifySignedConnectToken(
                        token,
                        m_config.Security.TokenIssuerPublicKey,
                        m_serverIdentityKeys.PublicKey,
                        nowUnixMs))
                {
                    ++m_securityCounters.AuthFailures;
                    RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::AuthenticationFailed, "Invalid or expired connect token", true);
                    return;
                }

                if (!TrySpendConnectToken(token, nowUnixMs))
                {
                    ++m_securityCounters.TokenReplayDrops;
                    ++m_securityCounters.AuthFailures;
                    RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::ReplayDetected, "Connect token already used", true);
                    return;
                }

                playerIdentity.Id = token.PlayerIdValue;
                playerIdentity.DisplayName = SanitizeDisplayName(token.PlayerName);
                playerIdentity.Roles = token.Roles;
                playerIdentity.VerifiedByToken = true;
                playerIdentity.AuthenticatedAtMs = nowSteadyMs;
                playerIdentity.BoundSession = assignedSession;
                usedAuthMethod = AuthMethod::SignedConnectToken;
            }
            else
            {
                if (HasDisallowedControlChars(playerDisplayName))
                {
                    ++m_securityCounters.MalformedDrops;
                    RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::InvalidArgument, "Invalid player name", true);
                    return;
                }
                u64 randomPid = 0;
                (void)CryptoContext::FillRandomBytes(std::span<u8>(reinterpret_cast<u8*>(&randomPid), sizeof(randomPid)));
                if (randomPid == kInvalidPlayerId)
                {
                    randomPid = 1;
                }
                playerIdentity.Id = randomPid;
                playerIdentity.DisplayName = SanitizeDisplayName(playerDisplayName);
                playerIdentity.VerifiedByToken = false;
                playerIdentity.AuthenticatedAtMs = nowSteadyMs;
                playerIdentity.BoundSession = assignedSession;
                usedAuthMethod = AuthMethod::EphemeralChallenge;
            }

            if (!CryptoContext::FillRandomBytes(conn.ServerReconnectNonce))
            {
                RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::CryptoError, "Reconnect nonce failure", false);
                return;
            }

            (void)m_sessionRegistry.CreateSession(assignedSession, conn.Transport.Handle, nowSteadyMs, 1);
            (void)m_sessionRegistry.AttachPlayer(assignedSession, playerIdentity);
            ++m_securityCounters.SuccessfulHandshakes;
        }

        if (conn.HandshakeSlotTracked)
        {
            m_rateLimiter.OnHandshakeEnded(conn.Transport.RemoteEndpoint.Host);
            conn.HandshakeSlotTracked = false;
        }

        std::vector<u8> sealedTicket;
        if (m_config.Security.ReconnectWindowMs > 0)
        {
            ReconnectTicketPayload ticketPayload{};
            ticketPayload.Session = assignedSession;
            ticketPayload.PlayerIdValue = playerIdentity.Id;
            ticketPayload.PlayerName = playerIdentity.DisplayName;
            ticketPayload.WorldId = 1;
            ticketPayload.IssuedAtUnixMs = nowSteadyMs;
            ticketPayload.ExpiresAtUnixMs = nowSteadyMs + static_cast<u64>(m_config.Security.ReconnectWindowMs);
            ticketPayload.ReconnectNonce = conn.ServerReconnectNonce;
            (void)m_ticketManager.MintTicket(ticketPayload, sealedTicket);
        }

        conn.Auth.Handle = conn.Transport.Handle;
        conn.Auth.Session = assignedSession;
        conn.Auth.Method = usedAuthMethod;
        conn.Auth.AuthenticatedAtMs = nowSteadyMs;
        conn.PlayerIdValue = playerIdentity.Id;
        conn.Transport.State = ConnectionState::Connected;
        conn.WasAuthenticated = true;
        conn.Keys.HandshakeAuthKey.Wipe();

        BitWriter acceptBody(768);
        if (!acceptBody.WriteU64(assignedSession.High) ||
            !acceptBody.WriteU64(assignedSession.Low) ||
            !acceptBody.WriteU64(playerIdentity.Id) ||
            !acceptBody.WriteU32(static_cast<u32>(sealedTicket.size())) ||
            !acceptBody.WriteBytes(sealedTicket))
        {
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "ServerAuthAccept encode error", false);
            return;
        }

        const auto sr = SendEncryptedFrame(
            conn,
            true,
            MessageCategory::Control,
            static_cast<u8>(ControlMessageKind::ServerAuthAccept),
            acceptBody.GetData(),
            NetDelivery::ReliableOrdered,
            NetChannel::Control);
        Core::Log::Trace("Network.Security", "Sent ServerAuthAccept status=" + std::to_string(static_cast<u32>(sr.Status)));

        if (m_onConnected)
        {
            m_onConnected(BuildConnectionInfo(conn), isReconnect ? "Reconnected" : "Connected");
        }
    }

    void ConnectionManager::HandleClientServerAuthAccept(ManagedConnection& conn, std::span<const u8> body)
    {
        BitReader reader(body);
        SessionId logicalSession{};
        PlayerId assignedPlayerId = kInvalidPlayerId;
        std::vector<u8> sealedTicket;

        if (!reader.ReadU64(logicalSession.High) ||
            !reader.ReadU64(logicalSession.Low) ||
            !reader.ReadU64(assignedPlayerId) ||
            !reader.ReadByteVector(sealedTicket, 512) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0 ||
            !logicalSession.IsValid() ||
            assignedPlayerId == kInvalidPlayerId)
        {
            ++m_securityCounters.MalformedDrops;
            RejectAndDisconnect(conn, ControlMessageKind::AuthReject, NetErrorCode::SerializationError, "Malformed ServerAuthAccept", true);
            return;
        }

        conn.Auth.Handle = conn.Transport.Handle;
        conn.Auth.Session = logicalSession;
        conn.Auth.Method = conn.ClientAuth.IsReconnect
            ? AuthMethod::ReconnectTicket
            : (conn.ClientAuth.EncodedConnectToken.empty() ? AuthMethod::EphemeralChallenge : AuthMethod::SignedConnectToken);
        conn.Auth.AuthenticatedAtMs = NowSteadyMs();
        conn.PlayerIdValue = assignedPlayerId;
        conn.ClientReconnectTicket = std::move(sealedTicket);
        conn.Transport.State = ConnectionState::Connected;
        conn.WasAuthenticated = true;
        conn.Keys.HandshakeAuthKey.Wipe();
        ++m_securityCounters.SuccessfulHandshakes;

        if (m_onConnected)
        {
            m_onConnected(BuildConnectionInfo(conn), conn.ClientAuth.IsReconnect ? "Reconnected" : "Connected");
        }
    }

    void ConnectionManager::HandlePeerReject(ManagedConnection& conn, ControlMessageKind kind, std::span<const u8> body)
    {
        BitReader reader(body);
        u16 remoteCode = 0;
        std::string reason;
        (void)reader.ReadU16(remoteCode);
        (void)reader.ReadString(reason, 256);

        if (kind == ControlMessageKind::VersionReject)
        {
            ++m_securityCounters.ProtocolMismatchDrops;
        }
        else
        {
            ++m_securityCounters.AuthFailures;
        }

        ConnectionInfo info = BuildConnectionInfo(conn);
        info.State = ConnectionState::Disconnected;
        info.IsEncrypted = false;
        info.IsAuthenticated = false;

        const ConnectionHandle h = conn.Transport.Handle;
        if (conn.HandshakeSlotTracked)
        {
            m_rateLimiter.OnHandshakeEnded(conn.Transport.RemoteEndpoint.Host);
            conn.HandshakeSlotTracked = false;
        }
        conn.WipeSecrets();
        m_connections.erase(h.Value);
        m_backend->Disconnect(h, static_cast<u32>(remoteCode), true);

        if (m_onDisconnected)
        {
            m_onDisconnected(info, reason.empty() ? "Rejected" : reason);
        }
    }

    bool ConnectionManager::TrySpendConnectToken(const SignedConnectToken& token, u64 nowUnixMs)
    {
        PruneSpentTokens(nowUnixMs);
        const std::string key = CryptoContext::HexEncode(token.TokenId);
        if (m_spentTokenIds.find(key) != m_spentTokenIds.end())
        {
            return false;
        }
        if (m_spentTokenIds.size() >= 4096)
        {
            m_spentTokenIds.erase(m_spentTokenIds.begin());
        }
        m_spentTokenIds.emplace(key, token.ExpiresAtUnixMs);
        return true;
    }

    void ConnectionManager::PruneSpentTokens(u64 nowUnixMs)
    {
        for (auto it = m_spentTokenIds.begin(); it != m_spentTokenIds.end();)
        {
            if (nowUnixMs > it->second)
            {
                it = m_spentTokenIds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void ConnectionManager::CheckTimeoutsAndPrune(u64 nowMs)
    {
        struct TimedOutEntry
        {
            ConnectionHandle Handle;
            std::string Reason;
            bool TrackAsHandshakeTimeout = false;
        };
        std::vector<TimedOutEntry> expired;

        for (auto& [handleVal, conn] : m_connections)
        {
            const ConnectionState st = conn.Transport.State;
            if ((st == ConnectionState::Connecting ||
                 st == ConnectionState::Handshaking ||
                 st == ConnectionState::ChallengeIssued) &&
                m_config.HandshakeTimeoutMs > 0 &&
                nowMs > conn.Transport.HandshakeStartedMs &&
                (nowMs - conn.Transport.HandshakeStartedMs) > m_config.HandshakeTimeoutMs)
            {
                expired.push_back({ ConnectionHandle{ handleVal }, "HandshakeTimeout", true });
            }
            else if (st == ConnectionState::Disconnecting &&
                m_config.DisconnectTimeoutMs > 0 &&
                nowMs > conn.Transport.DisconnectStartedMs &&
                (nowMs - conn.Transport.DisconnectStartedMs) > m_config.DisconnectTimeoutMs)
            {
                expired.push_back({ ConnectionHandle{ handleVal }, "DisconnectTimeout", false });
            }
        }

        for (const auto& entry : expired)
        {
            auto it = m_connections.find(entry.Handle.Value);
            if (it == m_connections.end())
            {
                continue;
            }
            ManagedConnection& conn = it->second;
            if (entry.TrackAsHandshakeTimeout)
            {
                ++m_securityCounters.TimeoutDrops;
                ++m_securityCounters.RejectedHandshakes;
                Core::Log::Warn("Network.Security", "Handshake timed out for connection " + std::to_string(entry.Handle.Value));
            }
            if (conn.HandshakeSlotTracked)
            {
                m_rateLimiter.OnHandshakeEnded(conn.Transport.RemoteEndpoint.Host);
                conn.HandshakeSlotTracked = false;
            }
            if (m_isServer && conn.Auth.Session.IsValid())
            {
                (void)m_sessionRegistry.CloseSession(conn.Auth.Session, SessionLifecycleState::Closed);
            }

            ConnectionInfo info = BuildConnectionInfo(conn);
            info.State = ConnectionState::Disconnected;
            info.IsEncrypted = false;
            info.IsAuthenticated = false;

            conn.WipeSecrets();
            m_connections.erase(it);
            m_backend->Disconnect(entry.Handle, static_cast<u32>(NetErrorCode::Timeout), true);

            if (m_onDisconnected)
            {
                m_onDisconnected(info, entry.Reason);
            }
        }

        if (nowMs >= m_lastPruneMs + 250)
        {
            m_lastPruneMs = nowMs;
            m_rateLimiter.Prune(nowMs);
            if (m_isServer)
            {
                std::vector<SessionId> closedIds;
                (void)m_sessionRegistry.PruneExpired(nowMs, m_config.Security.SessionIdleTimeoutMs, &closedIds);
                for (const SessionId& closedId : closedIds)
                {
                    if (m_onSessionClosed)
                    {
                        m_onSessionClosed(closedId, kInvalidConnectionHandle);
                    }
                }
                PruneSpentTokens(CryptoContext::GetUnixNowMs());
            }
        }
    }
}
