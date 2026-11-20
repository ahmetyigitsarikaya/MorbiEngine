#pragma once

#include <compare>
#include <string>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Network
{
    enum class NetworkMode : u8
    {
        Standalone = 0,
        Client = 1,
        ListenServer = 2,
        DedicatedServer = 3,
        EditorPIE = 4
    };

    enum class NetDelivery : u8
    {
        Unreliable = 0,
        UnreliableSequenced = 1,
        ReliableUnordered = 2,
        ReliableOrdered = 3
    };

    enum class NetChannel : u8
    {
        Control = 0,
        Snapshot = 1,
        GameplayEvents = 2,
        BulkTransfer = 3
    };

    enum class ConnectionState : u8
    {
        Disconnected = 0,
        Connecting = 1,
        Handshaking = 2,
        ChallengeIssued = 3,
        Connected = 4,
        Disconnecting = 5
    };

    enum class NetErrorCode : u16
    {
        None = 0,
        NotInitialized = 1,
        AlreadyInitialized = 2,
        InvalidArgument = 3,
        InvalidEndpoint = 4,
        TransportInitFailed = 5,
        BindFailed = 6,
        ConnectFailed = 7,
        InvalidConnection = 8,
        NotConnected = 9,
        PayloadTooLarge = 10,
        SerializationError = 11,
        ProtocolMismatch = 12,
        Timeout = 13,
        CapacityExceeded = 14,
        SendFailed = 15,
        AuthenticationFailed = 16,
        HandshakeRejected = 17,
        ReplayDetected = 18,
        TamperingDetected = 19,
        RateLimited = 20,
        SessionExpired = 21,
        InvalidState = 22,
        CryptoError = 23,
        DuplicateHandshake = 24,
        EntityNotFound = 25,
        Unauthorized = 26
    };

    struct NetError
    {
        NetErrorCode Code = NetErrorCode::None;
        std::string Message;
    };

    template <typename T>
    using NetResult = Core::Result<T, NetError>;

    struct ConnectionHandle
    {
        u32 Value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] constexpr bool operator==(const ConnectionHandle&) const noexcept = default;
        [[nodiscard]] constexpr auto operator<=>(const ConnectionHandle&) const noexcept = default;
    };

    inline constexpr ConnectionHandle kInvalidConnectionHandle{ 0 };

    using PlayerId = u64;
    inline constexpr PlayerId kInvalidPlayerId = 0;

    using WorldInstanceId = u32;
    inline constexpr WorldInstanceId kInvalidWorldInstanceId = 0;

    struct NetworkId
    {
        u32 Index = 0;
        u32 Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Generation != 0; }
        [[nodiscard]] constexpr u64 Pack() const noexcept
        {
            return (static_cast<u64>(Generation) << 32) | static_cast<u64>(Index);
        }
        [[nodiscard]] static constexpr NetworkId Unpack(u64 packed) noexcept
        {
            return NetworkId{
                static_cast<u32>(packed & 0xFFFFFFFFull),
                static_cast<u32>((packed >> 32) & 0xFFFFFFFFull)
            };
        }
        [[nodiscard]] constexpr bool operator==(const NetworkId&) const noexcept = default;
        [[nodiscard]] constexpr auto operator<=>(const NetworkId&) const noexcept = default;
    };

    inline constexpr NetworkId kInvalidNetworkId{ 0, 0 };

    struct MORBI_NETWORK_API SessionId
    {
        u64 High = 0;
        u64 Low = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return High != 0 || Low != 0; }
        [[nodiscard]] constexpr bool operator==(const SessionId&) const noexcept = default;
        [[nodiscard]] constexpr auto operator<=>(const SessionId&) const noexcept = default;
        [[nodiscard]] std::string ToHexString() const;
    };

    inline constexpr SessionId kInvalidSessionId{ 0, 0 };

    struct Endpoint
    {
        std::string Host = "127.0.0.1";
        u16 Port = 0;

        [[nodiscard]] bool IsValid() const noexcept { return !Host.empty() && Port != 0; }
    };

    struct NetworkStats
    {
        u64 PacketsSent = 0;
        u64 PacketsReceived = 0;
        u64 BytesSent = 0;
        u64 BytesReceived = 0;
        u32 RoundTripTimeMs = 0;
        u32 RoundTripTimeVarianceMs = 0;
        f32 PacketLossPercent = 0.0f;
    };

    struct ConnectionInfo
    {
        ConnectionHandle Handle = kInvalidConnectionHandle;
        SessionId Session{};
        PlayerId AuthenticatedPlayerId = kInvalidPlayerId;
        Endpoint RemoteEndpoint{};
        ConnectionState State = ConnectionState::Disconnected;
        u16 NegotiatedProtocolVersion = 0;
        bool IsEncrypted = false;
        bool IsAuthenticated = false;
        NetworkStats Stats{};
    };
}
