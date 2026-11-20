#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkConfig.hpp"
#include "Morbi/Network/Security/SecurityTypes.hpp"

namespace Morbi::Network
{
    inline constexpr u8 kDirectionClientToServer = static_cast<u8>('C');
    inline constexpr u8 kDirectionServerToClient = static_cast<u8>('S');

    struct HandshakeTranscriptParams
    {
        u16 NegotiatedVersion = 0;
        u32 SchemaHash = 0;
        SessionId CandidateSession{};
        std::array<u8, kHandshakeNonceBytes> ClientNonce{};
        std::array<u8, kHandshakeNonceBytes> ServerNonce{};
        std::array<u8, kX25519PublicKeyBytes> ClientEphemeralPubKey{};
        std::array<u8, kX25519PublicKeyBytes> ServerEphemeralPubKey{};
        std::array<u8, kEd25519PublicKeyBytes> ServerIdentityPubKey{};
    };

    class MORBI_NETWORK_API CryptoContext final
    {
    public:
        [[nodiscard]] static bool EnsureInitialized() noexcept;
        [[nodiscard]] static u64 GetUnixNowMs() noexcept;

        static bool FillRandomBytes(std::span<u8> outBuffer) noexcept;
        [[nodiscard]] static SessionId GenerateSessionId() noexcept;

        static bool GenerateEd25519KeyPair(Ed25519KeyPair& outKeyPair) noexcept;
        static bool GenerateX25519KeyPair(X25519KeyPair& outKeyPair) noexcept;

        static bool SignDetached(
            std::span<const u8> message,
            const SensitiveArray<kEd25519SecretKeyBytes>& secretKey,
            std::array<u8, kEd25519SignatureBytes>& outSignature) noexcept;

        [[nodiscard]] static bool VerifyDetached(
            std::span<const u8> message,
            std::span<const u8> signature,
            std::span<const u8> publicKey) noexcept;

        [[nodiscard]] static bool ConstantTimeEquals(
            std::span<const u8> a,
            std::span<const u8> b) noexcept;

        static bool BuildHandshakeTranscript(
            const HandshakeTranscriptParams& params,
            std::vector<u8>& outTranscript);

        static bool DeriveClientSessionKeys(
            const X25519KeyPair& clientEphemeral,
            std::span<const u8> serverEphemeralPubKey,
            std::span<const u8> transcriptBytes,
            SessionAeadKeys& outKeys) noexcept;

        static bool DeriveServerSessionKeys(
            const X25519KeyPair& serverEphemeral,
            std::span<const u8> clientEphemeralPubKey,
            std::span<const u8> transcriptBytes,
            SessionAeadKeys& outKeys) noexcept;

        static bool ComputeKeyedProofMac(
            const SensitiveArray<kAeadKeyBytes>& authKey,
            std::span<const u8> transcriptBytes,
            std::array<u8, 32>& outMac) noexcept;

        static bool EncryptPacketAead(
            const SensitiveArray<kAeadKeyBytes>& txKey,
            u32 keyEpoch,
            SessionId session,
            u8 directionByte,
            u64 packetSequence,
            u8 channel,
            u8 wireFlags,
            u16 wireVersion,
            std::span<const u8> plaintext,
            std::vector<u8>& outCiphertext);

        static bool DecryptPacketAead(
            const SensitiveArray<kAeadKeyBytes>& rxKey,
            u32 keyEpoch,
            SessionId session,
            u8 directionByte,
            u64 packetSequence,
            u8 channel,
            u8 wireFlags,
            u16 wireVersion,
            std::span<const u8> ciphertext,
            std::vector<u8>& outPlaintext);

        static bool CreateSignedConnectToken(
            const SignedConnectToken& unsignedFields,
            const SensitiveArray<kEd25519SecretKeyBytes>& issuerSecretKey,
            SignedConnectToken& outSignedToken);

        static bool EncodeSignedConnectToken(
            const SignedConnectToken& token,
            std::vector<u8>& outBytes);

        static bool DecodeSignedConnectToken(
            std::span<const u8> bytes,
            SignedConnectToken& outToken);

        [[nodiscard]] static bool VerifySignedConnectToken(
            const SignedConnectToken& token,
            std::span<const u8> issuerPublicKey,
            std::span<const u8> serverIdentityPublicKey,
            u64 nowUnixMs) noexcept;

        [[nodiscard]] static std::string HexEncode(std::span<const u8> bytes);
        static bool HexDecode(std::string_view hex, std::span<u8> outBytes) noexcept;
    };

    /// Server-side AEAD ticket sealer & rotator for secure session resumption.
    class MORBI_NETWORK_API ReconnectTicketManager final
    {
    public:
        ReconnectTicketManager() = default;
        ~ReconnectTicketManager() noexcept;

        ReconnectTicketManager(const ReconnectTicketManager&) = delete;
        ReconnectTicketManager& operator=(const ReconnectTicketManager&) = delete;

        bool Initialize() noexcept;
        void Reset() noexcept;
        bool RotateKey() noexcept;

        [[nodiscard]] u32 GetCurrentEpoch() const noexcept { return m_currentEpoch; }

        bool MintTicket(
            const ReconnectTicketPayload& payload,
            std::vector<u8>& outSealedTicket);

        bool VerifyAndUnpackTicket(
            std::span<const u8> sealedTicket,
            u64 nowUnixMs,
            ReconnectTicketPayload& outPayload) const;

    private:
        SensitiveArray<kAeadKeyBytes> m_currentKey{};
        SensitiveArray<kAeadKeyBytes> m_previousKey{};
        u32 m_currentEpoch = 0;
        bool m_hasPreviousKey = false;
        bool m_initialized = false;
    };
}
