#include "Morbi/Network/Security/CryptoContext.hpp"

#include <sodium.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "Morbi/Network/Serialization/BitStream.hpp"

namespace Morbi::Network
{
    void WipeSensitiveBytes(void* ptr, usize byteCount) noexcept
    {
        if (ptr != nullptr && byteCount > 0)
        {
            sodium_memzero(ptr, byteCount);
        }
    }

    namespace
    {
        void WriteU16LE(u8* dst, u16 val) noexcept
        {
            dst[0] = static_cast<u8>(val & 0xFFu);
            dst[1] = static_cast<u8>((val >> 8) & 0xFFu);
        }

        void WriteU32LE(u8* dst, u32 val) noexcept
        {
            dst[0] = static_cast<u8>(val & 0xFFu);
            dst[1] = static_cast<u8>((val >> 8) & 0xFFu);
            dst[2] = static_cast<u8>((val >> 16) & 0xFFu);
            dst[3] = static_cast<u8>((val >> 24) & 0xFFu);
        }

        void WriteU64LE(u8* dst, u64 val) noexcept
        {
            for (usize i = 0; i < 8; ++i)
            {
                dst[i] = static_cast<u8>((val >> (i * 8)) & 0xFFull);
            }
        }

        void BuildPacketNonce(
            u8 directionByte,
            u32 keyEpoch,
            SessionId session,
            u64 packetSequence,
            std::array<u8, kAeadNonceBytes>& outNonce) noexcept
        {
            outNonce.fill(0);
            outNonce[0] = directionByte;
            WriteU32LE(outNonce.data() + 4, keyEpoch);
            WriteU64LE(outNonce.data() + 8, session.Low ^ session.High);
            WriteU64LE(outNonce.data() + 16, packetSequence);
        }

        void BuildPacketAad(
            u32 magic,
            u8 wireFlags,
            u16 wireVersion,
            u64 packetSequence,
            SessionId session,
            u8 directionByte,
            u8 channel,
            u32 keyEpoch,
            std::array<u8, 37>& outAad) noexcept
        {
            WriteU32LE(outAad.data() + 0, magic);
            outAad[4] = wireFlags;
            WriteU16LE(outAad.data() + 5, wireVersion);
            WriteU64LE(outAad.data() + 7, packetSequence);
            WriteU64LE(outAad.data() + 15, session.High);
            WriteU64LE(outAad.data() + 23, session.Low);
            outAad[31] = directionByte;
            outAad[32] = channel;
            WriteU32LE(outAad.data() + 33, keyEpoch);
        }

        bool DeriveKeyWithDomain(
            std::span<const u8> sharedSecret,
            std::span<const u8> transcriptBytes,
            std::string_view domain,
            SensitiveArray<kAeadKeyBytes>& outKey) noexcept
        {
            crypto_generichash_blake2b_state state{};
            if (crypto_generichash_blake2b_init(&state, sharedSecret.data(), sharedSecret.size(), kAeadKeyBytes) != 0)
            {
                return false;
            }
            crypto_generichash_blake2b_update(&state, reinterpret_cast<const u8*>(domain.data()), domain.size());
            crypto_generichash_blake2b_update(&state, transcriptBytes.data(), transcriptBytes.size());
            const int rc = crypto_generichash_blake2b_final(&state, outKey.Bytes.data(), outKey.Bytes.size());
            sodium_memzero(&state, sizeof(state));
            return rc == 0;
        }

        bool SerializeSignableConnectTokenFields(const SignedConnectToken& token, BitWriter& writer)
        {
            constexpr char kTokenDomain[] = "MORBI-CONNECT-TOKEN-v2";
            return writer.WriteBytes(std::span<const u8>(reinterpret_cast<const u8*>(kTokenDomain), sizeof(kTokenDomain) - 1)) &&
                writer.WriteBytes(token.TokenId) &&
                writer.WriteU64(token.PlayerIdValue) &&
                writer.WriteString(token.PlayerName, kMaxPlayerNameBytes) &&
                writer.WriteU64(token.IssuedAtUnixMs) &&
                writer.WriteU64(token.ExpiresAtUnixMs) &&
                writer.WriteU32(token.Roles) &&
                writer.WriteU8(token.HasBoundServerPublicKey ? 1u : 0u) &&
                writer.WriteBytes(token.BoundServerPublicKey);
        }
    }

    bool CryptoContext::EnsureInitialized() noexcept
    {
        return sodium_init() >= 0;
    }

    u64 CryptoContext::GetUnixNowMs() noexcept
    {
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    bool CryptoContext::FillRandomBytes(std::span<u8> outBuffer) noexcept
    {
        if (!EnsureInitialized())
        {
            return false;
        }
        if (!outBuffer.empty())
        {
            randombytes_buf(outBuffer.data(), outBuffer.size());
        }
        return true;
    }

    SessionId CryptoContext::GenerateSessionId() noexcept
    {
        if (!EnsureInitialized())
        {
            return kInvalidSessionId;
        }
        SessionId id{};
        for (int attempt = 0; attempt < 8 && !id.IsValid(); ++attempt)
        {
            randombytes_buf(&id.High, sizeof(id.High));
            randombytes_buf(&id.Low, sizeof(id.Low));
        }
        return id;
    }

    bool CryptoContext::GenerateEd25519KeyPair(Ed25519KeyPair& outKeyPair) noexcept
    {
        if (!EnsureInitialized())
        {
            return false;
        }
        return crypto_sign_ed25519_keypair(outKeyPair.PublicKey.data(), outKeyPair.SecretKey.Bytes.data()) == 0;
    }

    bool CryptoContext::GenerateX25519KeyPair(X25519KeyPair& outKeyPair) noexcept
    {
        if (!EnsureInitialized())
        {
            return false;
        }
        return crypto_kx_keypair(outKeyPair.PublicKey.data(), outKeyPair.SecretKey.Bytes.data()) == 0;
    }

    bool CryptoContext::SignDetached(
        std::span<const u8> message,
        const SensitiveArray<kEd25519SecretKeyBytes>& secretKey,
        std::array<u8, kEd25519SignatureBytes>& outSignature) noexcept
    {
        if (!EnsureInitialized())
        {
            return false;
        }
        unsigned long long sigLen = 0;
        return crypto_sign_ed25519_detached(
            outSignature.data(),
            &sigLen,
            message.data(),
            static_cast<unsigned long long>(message.size()),
            secretKey.Bytes.data()) == 0 && sigLen == kEd25519SignatureBytes;
    }

    bool CryptoContext::VerifyDetached(
        std::span<const u8> message,
        std::span<const u8> signature,
        std::span<const u8> publicKey) noexcept
    {
        if (!EnsureInitialized())
        {
            return false;
        }
        if (signature.size() != kEd25519SignatureBytes || publicKey.size() != kEd25519PublicKeyBytes)
        {
            return false;
        }
        return crypto_sign_ed25519_verify_detached(
            signature.data(),
            message.data(),
            static_cast<unsigned long long>(message.size()),
            publicKey.data()) == 0;
    }

    bool CryptoContext::ConstantTimeEquals(std::span<const u8> a, std::span<const u8> b) noexcept
    {
        if (a.size() != b.size())
        {
            return false;
        }
        if (a.empty())
        {
            return true;
        }
        return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
    }

    bool CryptoContext::BuildHandshakeTranscript(
        const HandshakeTranscriptParams& params,
        std::vector<u8>& outTranscript)
    {
        constexpr char kDomain[] = "MORBI-HANDSHAKE-V2";
        BitWriter writer(512);
        if (!writer.WriteBytes(std::span<const u8>(reinterpret_cast<const u8*>(kDomain), sizeof(kDomain) - 1)) ||
            !writer.WriteU16(params.NegotiatedVersion) ||
            !writer.WriteU32(params.SchemaHash) ||
            !writer.WriteU64(params.CandidateSession.High) ||
            !writer.WriteU64(params.CandidateSession.Low) ||
            !writer.WriteBytes(params.ClientNonce) ||
            !writer.WriteBytes(params.ServerNonce) ||
            !writer.WriteBytes(params.ClientEphemeralPubKey) ||
            !writer.WriteBytes(params.ServerEphemeralPubKey) ||
            !writer.WriteBytes(params.ServerIdentityPubKey))
        {
            return false;
        }

        const auto span = writer.GetData();
        outTranscript.assign(span.begin(), span.end());
        return true;
    }

    bool CryptoContext::DeriveClientSessionKeys(
        const X25519KeyPair& clientEphemeral,
        std::span<const u8> serverEphemeralPubKey,
        std::span<const u8> transcriptBytes,
        SessionAeadKeys& outKeys) noexcept
    {
        if (!EnsureInitialized() || serverEphemeralPubKey.size() != kX25519PublicKeyBytes)
        {
            return false;
        }

        SensitiveArray<kAeadKeyBytes> baseRx;
        SensitiveArray<kAeadKeyBytes> baseTx;
        if (crypto_kx_client_session_keys(
                baseRx.Bytes.data(),
                baseTx.Bytes.data(),
                clientEphemeral.PublicKey.data(),
                clientEphemeral.SecretKey.Bytes.data(),
                serverEphemeralPubKey.data()) != 0)
        {
            return false;
        }

        outKeys.KeyEpoch = 1;
        const bool ok =
            DeriveKeyWithDomain(baseRx.Bytes, transcriptBytes, "MORBI-S2C-AEAD", outKeys.RxKey) &&
            DeriveKeyWithDomain(baseTx.Bytes, transcriptBytes, "MORBI-C2S-AEAD", outKeys.TxKey) &&
            DeriveKeyWithDomain(baseTx.Bytes, transcriptBytes, "MORBI-AUTH-MAC", outKeys.HandshakeAuthKey);
        if (!ok)
        {
            outKeys.Wipe();
        }
        return ok;
    }

    bool CryptoContext::DeriveServerSessionKeys(
        const X25519KeyPair& serverEphemeral,
        std::span<const u8> clientEphemeralPubKey,
        std::span<const u8> transcriptBytes,
        SessionAeadKeys& outKeys) noexcept
    {
        if (!EnsureInitialized() || clientEphemeralPubKey.size() != kX25519PublicKeyBytes)
        {
            return false;
        }

        SensitiveArray<kAeadKeyBytes> baseRx;
        SensitiveArray<kAeadKeyBytes> baseTx;
        if (crypto_kx_server_session_keys(
                baseRx.Bytes.data(),
                baseTx.Bytes.data(),
                serverEphemeral.PublicKey.data(),
                serverEphemeral.SecretKey.Bytes.data(),
                clientEphemeralPubKey.data()) != 0)
        {
            return false;
        }

        outKeys.KeyEpoch = 1;
        const bool ok =
            DeriveKeyWithDomain(baseRx.Bytes, transcriptBytes, "MORBI-C2S-AEAD", outKeys.RxKey) &&
            DeriveKeyWithDomain(baseTx.Bytes, transcriptBytes, "MORBI-S2C-AEAD", outKeys.TxKey) &&
            DeriveKeyWithDomain(baseRx.Bytes, transcriptBytes, "MORBI-AUTH-MAC", outKeys.HandshakeAuthKey);
        if (!ok)
        {
            outKeys.Wipe();
        }
        return ok;
    }

    bool CryptoContext::ComputeKeyedProofMac(
        const SensitiveArray<kAeadKeyBytes>& authKey,
        std::span<const u8> transcriptBytes,
        std::array<u8, 32>& outMac) noexcept
    {
        if (!EnsureInitialized())
        {
            return false;
        }
        return crypto_generichash_blake2b(
            outMac.data(),
            outMac.size(),
            transcriptBytes.data(),
            transcriptBytes.size(),
            authKey.Bytes.data(),
            authKey.Bytes.size()) == 0;
    }

    bool CryptoContext::EncryptPacketAead(
        const SensitiveArray<kAeadKeyBytes>& txKey,
        u32 keyEpoch,
        SessionId session,
        u8 directionByte,
        u64 packetSequence,
        u8 channel,
        u8 wireFlags,
        u16 wireVersion,
        std::span<const u8> plaintext,
        std::vector<u8>& outCiphertext)
    {
        if (!EnsureInitialized() || packetSequence == 0 || !session.IsValid())
        {
            return false;
        }
        if (plaintext.size() > ProtocolConstants::kMaxMessageBytes)
        {
            return false;
        }

        std::array<u8, kAeadNonceBytes> nonce{};
        BuildPacketNonce(directionByte, keyEpoch, session, packetSequence, nonce);

        std::array<u8, 37> aad{};
        BuildPacketAad(
            ProtocolConstants::kMagic,
            wireFlags,
            wireVersion,
            packetSequence,
            session,
            directionByte,
            channel,
            keyEpoch,
            aad);

        outCiphertext.resize(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        unsigned long long cipherLen = 0;
        const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
            outCiphertext.data(),
            &cipherLen,
            plaintext.data(),
            static_cast<unsigned long long>(plaintext.size()),
            aad.data(),
            static_cast<unsigned long long>(aad.size()),
            nullptr,
            nonce.data(),
            txKey.Bytes.data());
        if (rc != 0)
        {
            outCiphertext.clear();
            return false;
        }
        outCiphertext.resize(static_cast<usize>(cipherLen));
        return true;
    }

    bool CryptoContext::DecryptPacketAead(
        const SensitiveArray<kAeadKeyBytes>& rxKey,
        u32 keyEpoch,
        SessionId session,
        u8 directionByte,
        u64 packetSequence,
        u8 channel,
        u8 wireFlags,
        u16 wireVersion,
        std::span<const u8> ciphertext,
        std::vector<u8>& outPlaintext)
    {
        if (!EnsureInitialized() || packetSequence == 0 || !session.IsValid())
        {
            return false;
        }
        if (ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES ||
            ciphertext.size() > ProtocolConstants::kMaxMessageBytes + crypto_aead_xchacha20poly1305_ietf_ABYTES)
        {
            return false;
        }

        std::array<u8, kAeadNonceBytes> nonce{};
        BuildPacketNonce(directionByte, keyEpoch, session, packetSequence, nonce);

        std::array<u8, 37> aad{};
        BuildPacketAad(
            ProtocolConstants::kMagic,
            wireFlags,
            wireVersion,
            packetSequence,
            session,
            directionByte,
            channel,
            keyEpoch,
            aad);

        outPlaintext.resize(ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
        unsigned long long plainLen = 0;
        const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
            outPlaintext.data(),
            &plainLen,
            nullptr,
            ciphertext.data(),
            static_cast<unsigned long long>(ciphertext.size()),
            aad.data(),
            static_cast<unsigned long long>(aad.size()),
            nonce.data(),
            rxKey.Bytes.data());
        if (rc != 0)
        {
            if (!outPlaintext.empty())
            {
                sodium_memzero(outPlaintext.data(), outPlaintext.size());
            }
            outPlaintext.clear();
            return false;
        }
        outPlaintext.resize(static_cast<usize>(plainLen));
        return true;
    }

    bool CryptoContext::CreateSignedConnectToken(
        const SignedConnectToken& unsignedFields,
        const SensitiveArray<kEd25519SecretKeyBytes>& issuerSecretKey,
        SignedConnectToken& outSignedToken)
    {
        if (!EnsureInitialized() ||
            unsignedFields.PlayerIdValue == kInvalidPlayerId ||
            unsignedFields.PlayerName.empty() ||
            unsignedFields.PlayerName.size() > kMaxPlayerNameBytes)
        {
            return false;
        }

        outSignedToken = unsignedFields;
        bool allZeroId = true;
        for (u8 b : outSignedToken.TokenId)
        {
            if (b != 0)
            {
                allZeroId = false;
                break;
            }
        }
        if (allZeroId && !FillRandomBytes(outSignedToken.TokenId))
        {
            return false;
        }

        BitWriter signWriter(256);
        if (!SerializeSignableConnectTokenFields(outSignedToken, signWriter))
        {
            return false;
        }

        return SignDetached(signWriter.GetData(), issuerSecretKey, outSignedToken.Signature);
    }

    bool CryptoContext::EncodeSignedConnectToken(
        const SignedConnectToken& token,
        std::vector<u8>& outBytes)
    {
        BitWriter writer(512);
        if (!writer.WriteBytes(token.TokenId) ||
            !writer.WriteU64(token.PlayerIdValue) ||
            !writer.WriteString(token.PlayerName, kMaxPlayerNameBytes) ||
            !writer.WriteU64(token.IssuedAtUnixMs) ||
            !writer.WriteU64(token.ExpiresAtUnixMs) ||
            !writer.WriteU32(token.Roles) ||
            !writer.WriteU8(token.HasBoundServerPublicKey ? 1u : 0u) ||
            !writer.WriteBytes(token.BoundServerPublicKey) ||
            !writer.WriteBytes(token.Signature))
        {
            return false;
        }
        const auto data = writer.GetData();
        outBytes.assign(data.begin(), data.end());
        return true;
    }

    bool CryptoContext::DecodeSignedConnectToken(
        std::span<const u8> bytes,
        SignedConnectToken& outToken)
    {
        if (bytes.empty() || bytes.size() > 512)
        {
            return false;
        }
        BitReader reader(bytes);
        u8 boundFlag = 0;
        if (!reader.ReadBytes(outToken.TokenId) ||
            !reader.ReadU64(outToken.PlayerIdValue) ||
            !reader.ReadString(outToken.PlayerName, kMaxPlayerNameBytes) ||
            !reader.ReadU64(outToken.IssuedAtUnixMs) ||
            !reader.ReadU64(outToken.ExpiresAtUnixMs) ||
            !reader.ReadU32(outToken.Roles) ||
            !reader.ReadU8(boundFlag) ||
            boundFlag > 1 ||
            !reader.ReadBytes(outToken.BoundServerPublicKey) ||
            !reader.ReadBytes(outToken.Signature))
        {
            return false;
        }
        outToken.HasBoundServerPublicKey = (boundFlag != 0);
        return !reader.HasError() && reader.GetRemainingBits() == 0 && outToken.PlayerIdValue != kInvalidPlayerId;
    }

    bool CryptoContext::VerifySignedConnectToken(
        const SignedConnectToken& token,
        std::span<const u8> issuerPublicKey,
        std::span<const u8> serverIdentityPublicKey,
        u64 nowUnixMs) noexcept
    {
        if (!EnsureInitialized() ||
            token.PlayerIdValue == kInvalidPlayerId ||
            token.PlayerName.empty() ||
            token.PlayerName.size() > kMaxPlayerNameBytes ||
            issuerPublicKey.size() != kEd25519PublicKeyBytes)
        {
            return false;
        }

        if (token.ExpiresAtUnixMs <= token.IssuedAtUnixMs || nowUnixMs > token.ExpiresAtUnixMs)
        {
            return false;
        }

        if (token.HasBoundServerPublicKey)
        {
            if (serverIdentityPublicKey.size() != kEd25519PublicKeyBytes ||
                !ConstantTimeEquals(token.BoundServerPublicKey, serverIdentityPublicKey))
            {
                return false;
            }
        }

        BitWriter signWriter(256);
        if (!SerializeSignableConnectTokenFields(token, signWriter))
        {
            return false;
        }

        return VerifyDetached(signWriter.GetData(), token.Signature, issuerPublicKey);
    }

    std::string CryptoContext::HexEncode(std::span<const u8> bytes)
    {
        if (bytes.empty())
        {
            return {};
        }
        std::string out(bytes.size() * 2 + 1, '\0');
        sodium_bin2hex(out.data(), out.size(), bytes.data(), bytes.size());
        out.resize(bytes.size() * 2);
        return out;
    }

    bool CryptoContext::HexDecode(std::string_view hex, std::span<u8> outBytes) noexcept
    {
        if (hex.size() != outBytes.size() * 2)
        {
            return false;
        }
        size_t decodedLen = 0;
        const int rc = sodium_hex2bin(
            outBytes.data(),
            outBytes.size(),
            hex.data(),
            hex.size(),
            nullptr,
            &decodedLen,
            nullptr);
        return rc == 0 && decodedLen == outBytes.size();
    }

    ReconnectTicketManager::~ReconnectTicketManager() noexcept
    {
        Reset();
    }

    bool ReconnectTicketManager::Initialize() noexcept
    {
        if (!CryptoContext::EnsureInitialized())
        {
            return false;
        }
        Reset();
        if (!CryptoContext::FillRandomBytes(m_currentKey.Bytes))
        {
            return false;
        }
        m_currentEpoch = 1;
        m_hasPreviousKey = false;
        m_initialized = true;
        return true;
    }

    void ReconnectTicketManager::Reset() noexcept
    {
        m_currentKey.Wipe();
        m_previousKey.Wipe();
        m_currentEpoch = 0;
        m_hasPreviousKey = false;
        m_initialized = false;
    }

    bool ReconnectTicketManager::RotateKey() noexcept
    {
        if (!m_initialized)
        {
            return false;
        }
        m_previousKey = m_currentKey;
        m_hasPreviousKey = true;
        if (!CryptoContext::FillRandomBytes(m_currentKey.Bytes))
        {
            return false;
        }
        ++m_currentEpoch;
        return true;
    }

    bool ReconnectTicketManager::MintTicket(
        const ReconnectTicketPayload& payload,
        std::vector<u8>& outSealedTicket)
    {
        if (!m_initialized || !payload.Session.IsValid() || payload.PlayerIdValue == kInvalidPlayerId)
        {
            return false;
        }

        BitWriter plainWriter(256);
        if (!plainWriter.WriteU64(payload.Session.High) ||
            !plainWriter.WriteU64(payload.Session.Low) ||
            !plainWriter.WriteU64(payload.PlayerIdValue) ||
            !plainWriter.WriteString(payload.PlayerName, kMaxPlayerNameBytes) ||
            !plainWriter.WriteU32(payload.WorldId) ||
            !plainWriter.WriteU64(payload.IssuedAtUnixMs) ||
            !plainWriter.WriteU64(payload.ExpiresAtUnixMs) ||
            !plainWriter.WriteU32(m_currentEpoch) ||
            !plainWriter.WriteBytes(payload.ReconnectNonce))
        {
            return false;
        }

        std::array<u8, kAeadNonceBytes> nonce{};
        if (!CryptoContext::FillRandomBytes(nonce))
        {
            return false;
        }

        std::array<u8, 4> epochAad{};
        WriteU32LE(epochAad.data(), m_currentEpoch);

        const auto plainSpan = plainWriter.GetData();
        std::vector<u8> ciphertext(plainSpan.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        unsigned long long cipherLen = 0;
        if (crypto_aead_xchacha20poly1305_ietf_encrypt(
                ciphertext.data(),
                &cipherLen,
                plainSpan.data(),
                static_cast<unsigned long long>(plainSpan.size()),
                epochAad.data(),
                epochAad.size(),
                nullptr,
                nonce.data(),
                m_currentKey.Bytes.data()) != 0)
        {
            return false;
        }
        ciphertext.resize(static_cast<usize>(cipherLen));

        BitWriter ticketWriter(512);
        if (!ticketWriter.WriteU32(m_currentEpoch) ||
            !ticketWriter.WriteBytes(nonce) ||
            !ticketWriter.WriteBytes(ciphertext))
        {
            return false;
        }

        const auto data = ticketWriter.GetData();
        outSealedTicket.assign(data.begin(), data.end());
        return true;
    }

    bool ReconnectTicketManager::VerifyAndUnpackTicket(
        std::span<const u8> sealedTicket,
        u64 nowUnixMs,
        ReconnectTicketPayload& outPayload) const
    {
        if (!m_initialized || sealedTicket.size() < 4 + kAeadNonceBytes + crypto_aead_xchacha20poly1305_ietf_ABYTES || sealedTicket.size() > 512)
        {
            return false;
        }

        BitReader envelopeReader(sealedTicket);
        u32 epoch = 0;
        std::array<u8, kAeadNonceBytes> nonce{};
        if (!envelopeReader.ReadU32(epoch) || !envelopeReader.ReadBytes(nonce))
        {
            return false;
        }
        const usize remainingBytes = envelopeReader.GetRemainingBytes();
        if (remainingBytes < crypto_aead_xchacha20poly1305_ietf_ABYTES)
        {
            return false;
        }
        std::vector<u8> ciphertext(remainingBytes);
        if (!envelopeReader.ReadBytes(ciphertext))
        {
            return false;
        }

        const SensitiveArray<kAeadKeyBytes>* keyPtr = nullptr;
        if (epoch == m_currentEpoch)
        {
            keyPtr = &m_currentKey;
        }
        else if (m_hasPreviousKey && m_currentEpoch > 0 && epoch == m_currentEpoch - 1)
        {
            keyPtr = &m_previousKey;
        }
        else
        {
            return false;
        }

        std::array<u8, 4> epochAad{};
        WriteU32LE(epochAad.data(), epoch);

        std::vector<u8> plaintext(ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES);
        unsigned long long plainLen = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                plaintext.data(),
                &plainLen,
                nullptr,
                ciphertext.data(),
                static_cast<unsigned long long>(ciphertext.size()),
                epochAad.data(),
                epochAad.size(),
                nonce.data(),
                keyPtr->Bytes.data()) != 0)
        {
            return false;
        }
        plaintext.resize(static_cast<usize>(plainLen));

        BitReader plainReader(plaintext);
        const bool ok =
            plainReader.ReadU64(outPayload.Session.High) &&
            plainReader.ReadU64(outPayload.Session.Low) &&
            plainReader.ReadU64(outPayload.PlayerIdValue) &&
            plainReader.ReadString(outPayload.PlayerName, kMaxPlayerNameBytes) &&
            plainReader.ReadU32(outPayload.WorldId) &&
            plainReader.ReadU64(outPayload.IssuedAtUnixMs) &&
            plainReader.ReadU64(outPayload.ExpiresAtUnixMs) &&
            plainReader.ReadU32(outPayload.TicketEpoch) &&
            plainReader.ReadBytes(outPayload.ReconnectNonce);

        sodium_memzero(plaintext.data(), plaintext.size());
        if (!ok || plainReader.HasError() || plainReader.GetRemainingBits() != 0)
        {
            return false;
        }
        if (!outPayload.Session.IsValid() || outPayload.PlayerIdValue == kInvalidPlayerId)
        {
            return false;
        }
        if (outPayload.ExpiresAtUnixMs <= outPayload.IssuedAtUnixMs || nowUnixMs > outPayload.ExpiresAtUnixMs)
        {
            return false;
        }
        return true;
    }
}
