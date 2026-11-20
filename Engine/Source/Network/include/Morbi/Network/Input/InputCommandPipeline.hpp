#pragma once

#include <deque>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Network/NetworkTypes.hpp"
#include "Morbi/Network/Serialization/BitStream.hpp"
#include "Morbi/Network/Session/SessionRegistry.hpp"

namespace Morbi::Network
{
    namespace InputActionFlags
    {
        inline constexpr u32 kNone = 0;
        inline constexpr u32 kJump = 1u << 0;
        inline constexpr u32 kSprint = 1u << 1;
        inline constexpr u32 kPrimaryAction = 1u << 2;
        inline constexpr u32 kSecondaryAction = 1u << 3;
        inline constexpr u32 kAllAllowed = kJump | kSprint | kPrimaryAction | kSecondaryAction;
    }

    struct MORBI_NETWORK_API PlayerInputCommand
    {
        NetworkId TargetEntity = kInvalidNetworkId;
        u32 Sequence = 0;
        u32 ClientTick = 0;
        f32 MoveX = 0.0f;
        f32 MoveZ = 0.0f;
        f32 YawRadians = 0.0f;
        u32 ActionFlags = InputActionFlags::kNone;

        [[nodiscard]] bool IsValid() const noexcept;
        bool Write(BitWriter& writer) const;
        bool Read(BitReader& reader);
    };

    struct InputValidationConfig
    {
        usize MaxBufferedPerEntity = 32;
        u32 MaxFutureSequenceLead = 64;
        u32 MaxCommandsPerSecond = 120;
    };

    struct InputPipelineStats
    {
        u64 AcceptedCommands = 0;
        u64 RejectedMalformed = 0;
        u64 RejectedOwnership = 0;
        u64 RejectedStaleOrDuplicate = 0;
        u64 RejectedFutureLead = 0;
        u64 RejectedRateLimit = 0;
        u64 RejectedBufferFull = 0;
    };

    enum class InputSubmitStatus : u8
    {
        Accepted = 0,
        Malformed = 1,
        Unauthorized = 2,
        StaleOrDuplicate = 3,
        TooFarInFuture = 4,
        RateLimited = 5,
        BufferFull = 6
    };

    /// Server-authoritative bounded input command pipeline.
    /// Validates sender session, entity ownership, world membership, sequence window, rate limit, and value bounds.
    class MORBI_NETWORK_API ServerInputPipeline final
    {
    public:
        explicit ServerInputPipeline(const InputValidationConfig& config = {}) noexcept;

        void Configure(const InputValidationConfig& config) noexcept;
        void Reset() noexcept;
        void RemoveEntityQueue(NetworkId entityId) noexcept;

        InputSubmitStatus SubmitCommand(
            ConnectionHandle senderConnection,
            const SessionRegistry& sessions,
            WorldInstanceId activeWorldId,
            const PlayerInputCommand& cmd,
            u64 nowMs);

        /// Consumes validated commands queued for `entityId` (up to `maxCount`).
        usize DrainCommands(
            NetworkId entityId,
            std::vector<PlayerInputCommand>& outCommands,
            usize maxCount = 8);

        [[nodiscard]] u32 GetLastAcceptedSequence(NetworkId entityId) const noexcept;
        [[nodiscard]] usize GetQueuedCount(NetworkId entityId) const noexcept;
        [[nodiscard]] const InputPipelineStats& GetStats() const noexcept { return m_stats; }

    private:
        struct EntityQueue
        {
            u32 LastAcceptedSequence = 0;
            u64 WindowStartMs = 0;
            u32 CommandsInWindow = 0;
            std::deque<PlayerInputCommand> Pending;
        };

        InputValidationConfig m_config{};
        std::unordered_map<u64, EntityQueue> m_queues;
        InputPipelineStats m_stats{};
    };

    struct ClientInputHistoryEntry
    {
        PlayerInputCommand Command{};
        f32 FixedDt = 1.0f / 60.0f;
        f32 PredictedPosX = 0.0f;
        f32 PredictedPosY = 0.0f;
        f32 PredictedPosZ = 0.0f;
        f32 PredictedVelX = 0.0f;
        f32 PredictedVelY = 0.0f;
        f32 PredictedVelZ = 0.0f;
    };

    struct ClientInputHistoryStats
    {
        u64 Pushed = 0;
        u64 Acknowledged = 0;
        u64 Overflows = 0;
        u64 StaleAcksIgnored = 0;
        u64 InvalidFutureAcksIgnored = 0;
    };

    /// Bounded client-side ring buffer of predicted inputs awaiting server confirmation.
    class MORBI_NETWORK_API ClientInputHistory final
    {
    public:
        static constexpr usize kDefaultCapacity = 128;
        static constexpr usize kMinCapacity = 16;
        static constexpr usize kMaxCapacity = 512;

        explicit ClientInputHistory(usize capacity = kDefaultCapacity) noexcept;

        void SetCapacity(usize capacity) noexcept;
        [[nodiscard]] usize GetCapacity() const noexcept { return m_capacity; }

        void Reset() noexcept;
        void ClearEntries() noexcept;

        /// Appends a predicted input entry. Rejects invalid/non-monotonic sequences.
        /// If full, evicts the oldest unacknowledged entry and increments `Overflows`.
        bool Push(const ClientInputHistoryEntry& entry);

        /// Removes all entries with Sequence <= `lastProcessedInputSeq`.
        /// Safely ignores stale (<= LastAcknowledgedSeq) and invalid future (> LastPushedSeq) sequences.
        usize AcknowledgeUpTo(u32 lastProcessedInputSeq) noexcept;

        void CopyUnacknowledged(std::vector<ClientInputHistoryEntry>& outEntries) const;
        [[nodiscard]] bool TryGetEntry(u32 sequence, ClientInputHistoryEntry& outEntry) const noexcept;

        [[nodiscard]] usize GetCount() const noexcept { return m_entries.size(); }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_entries.empty(); }
        [[nodiscard]] u32 GetLastPushedSequence() const noexcept { return m_lastPushedSeq; }
        [[nodiscard]] u32 GetLastAcknowledgedSequence() const noexcept { return m_lastAcknowledgedSeq; }
        [[nodiscard]] const ClientInputHistoryStats& GetStats() const noexcept { return m_stats; }

    private:
        usize m_capacity = kDefaultCapacity;
        std::deque<ClientInputHistoryEntry> m_entries;
        u32 m_lastPushedSeq = 0;
        u32 m_lastAcknowledgedSeq = 0;
        ClientInputHistoryStats m_stats{};
    };
}
