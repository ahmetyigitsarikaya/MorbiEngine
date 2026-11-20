#pragma once

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"

namespace Morbi::Network
{
    inline constexpr usize kEd25519PublicKeyBytes = 32;
    inline constexpr usize kEd25519SecretKeyBytes = 64;
    inline constexpr usize kEd25519SignatureBytes = 64;
    inline constexpr usize kX25519PublicKeyBytes = 32;
    inline constexpr usize kX25519SecretKeyBytes = 32;
    inline constexpr usize kAeadKeyBytes = 32;
    inline constexpr usize kAeadNonceBytes = 24;
    inline constexpr usize kAeadTagBytes = 16;
    inline constexpr usize kHandshakeNonceBytes = 32;
    inline constexpr usize kTokenIdBytes = 16;
    inline constexpr usize kReconnectNonceBytes = 16;
    inline constexpr usize kMaxPlayerNameBytes = 64;

    /// Wipes a contiguous memory span cleanly (delegated to sodium_memzero in .cpp).
    MORBI_NETWORK_API void WipeSensitiveBytes(void* ptr, usize byteCount) noexcept;

    template <usize N>
    struct SensitiveArray
    {
        std::array<u8, N> Bytes{};

        SensitiveArray() noexcept = default;
        SensitiveArray(const SensitiveArray&) = default;
        SensitiveArray& operator=(const SensitiveArray&) = default;
        SensitiveArray(SensitiveArray&& other) noexcept
            : Bytes(other.Bytes)
        {
            other.Wipe();
        }
        SensitiveArray& operator=(SensitiveArray&& other) noexcept
        {
            if (this != &other)
            {
                Bytes = other.Bytes;
                other.Wipe();
            }
            return *this;
        }
        ~SensitiveArray() noexcept
        {
            Wipe();
        }

        void Wipe() noexcept
        {
            WipeSensitiveBytes(Bytes.data(), Bytes.size());
        }
    };

    struct Ed25519KeyPair
    {
        std::array<u8, kEd25519PublicKeyBytes> PublicKey{};
        SensitiveArray<kEd25519SecretKeyBytes> SecretKey{};
    };

    struct X25519KeyPair
    {
        std::array<u8, kX25519PublicKeyBytes> PublicKey{};
        SensitiveArray<kX25519SecretKeyBytes> SecretKey{};
    };

    struct SessionAeadKeys
    {
        SensitiveArray<kAeadKeyBytes> RxKey{};
        SensitiveArray<kAeadKeyBytes> TxKey{};
        SensitiveArray<kAeadKeyBytes> HandshakeAuthKey{};
        u32 KeyEpoch = 1;

        void Wipe() noexcept
        {
            RxKey.Wipe();
            TxKey.Wipe();
            HandshakeAuthKey.Wipe();
            KeyEpoch = 0;
        }
    };

    enum class AuthMethod : u8
    {
        None = 0,
        EphemeralChallenge = 1,
        SignedConnectToken = 2,
        ReconnectTicket = 3
    };

    struct SignedConnectToken
    {
        std::array<u8, kTokenIdBytes> TokenId{};
        PlayerId PlayerIdValue = kInvalidPlayerId;
        std::string PlayerName = "Player";
        u64 IssuedAtUnixMs = 0;
        u64 ExpiresAtUnixMs = 0;
        u32 Roles = 0;
        bool HasBoundServerPublicKey = false;
        std::array<u8, kEd25519PublicKeyBytes> BoundServerPublicKey{};
        std::array<u8, kEd25519SignatureBytes> Signature{};
    };

    struct ConnectAuthOptions
    {
        std::vector<u8> EncodedConnectToken;
        std::string PlayerDisplayName = "Player";
        SessionId ResumeSessionId{};
        std::vector<u8> ReconnectTicket;
        bool IsReconnect = false;
    };

    struct ReconnectTicketPayload
    {
        SessionId Session{};
        PlayerId PlayerIdValue = kInvalidPlayerId;
        std::string PlayerName;
        WorldInstanceId WorldId = kInvalidWorldInstanceId;
        u64 IssuedAtUnixMs = 0;
        u64 ExpiresAtUnixMs = 0;
        u32 TicketEpoch = 0;
        std::array<u8, kReconnectNonceBytes> ReconnectNonce{};
    };

    struct SecurityCounters
    {
        u64 SuccessfulHandshakes = 0;
        u64 SuccessfulReconnects = 0;
        u64 RejectedHandshakes = 0;
        u64 AuthFailures = 0;
        u64 ReplayDrops = 0;
        u64 TamperDrops = 0;
        u64 RateLimitDrops = 0;
        u64 MalformedDrops = 0;
        u64 TimeoutDrops = 0;
        u64 DuplicateHandshakeDrops = 0;
        u64 ProtocolMismatchDrops = 0;
        u64 TokenReplayDrops = 0;
    };
}
