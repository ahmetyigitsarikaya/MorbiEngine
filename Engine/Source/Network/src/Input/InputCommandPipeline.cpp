#include "Morbi/Network/Input/InputCommandPipeline.hpp"

#include <cmath>

namespace Morbi::Network
{
    bool PlayerInputCommand::IsValid() const noexcept
    {
        if (!TargetEntity.IsValid() || Sequence == 0)
        {
            return false;
        }
        if (!std::isfinite(MoveX) || !std::isfinite(MoveZ) || !std::isfinite(YawRadians))
        {
            return false;
        }
        if (std::abs(MoveX) > 1.0f || std::abs(MoveZ) > 1.0f || std::abs(YawRadians) > 100.0f)
        {
            return false;
        }
        if ((ActionFlags & ~InputActionFlags::kAllAllowed) != 0)
        {
            return false;
        }
        return true;
    }

    bool PlayerInputCommand::Write(BitWriter& writer) const
    {
        if (!IsValid())
        {
            return false;
        }
        return writer.WriteU32(TargetEntity.Index) &&
            writer.WriteU32(TargetEntity.Generation) &&
            writer.WriteU32(Sequence) &&
            writer.WriteU32(ClientTick) &&
            writer.WriteF32(MoveX) &&
            writer.WriteF32(MoveZ) &&
            writer.WriteF32(YawRadians) &&
            writer.WriteU32(ActionFlags);
    }

    bool PlayerInputCommand::Read(BitReader& reader)
    {
        if (!reader.ReadU32(TargetEntity.Index) ||
            !reader.ReadU32(TargetEntity.Generation) ||
            !reader.ReadU32(Sequence) ||
            !reader.ReadU32(ClientTick) ||
            !reader.ReadF32(MoveX) ||
            !reader.ReadF32(MoveZ) ||
            !reader.ReadF32(YawRadians) ||
            !reader.ReadU32(ActionFlags) ||
            reader.HasError() ||
            reader.GetRemainingBits() != 0)
        {
            return false;
        }
        return IsValid();
    }

    ServerInputPipeline::ServerInputPipeline(const InputValidationConfig& config) noexcept
        : m_config(config)
    {
    }

    void ServerInputPipeline::Configure(const InputValidationConfig& config) noexcept
    {
        m_config = config;
    }

    void ServerInputPipeline::Reset() noexcept
    {
        m_queues.clear();
        m_stats = InputPipelineStats{};
    }

    void ServerInputPipeline::RemoveEntityQueue(NetworkId entityId) noexcept
    {
        m_queues.erase(entityId.Pack());
    }

    InputSubmitStatus ServerInputPipeline::SubmitCommand(
        ConnectionHandle senderConnection,
        const SessionRegistry& sessions,
        WorldInstanceId activeWorldId,
        const PlayerInputCommand& cmd,
        u64 nowMs)
    {
        if (!cmd.IsValid())
        {
            ++m_stats.RejectedMalformed;
            return InputSubmitStatus::Malformed;
        }

        NetworkSession senderSession{};
        if (!senderConnection.IsValid() ||
            !sessions.TryGetSessionByConnection(senderConnection, senderSession) ||
            senderSession.LifecycleState != SessionLifecycleState::Active)
        {
            ++m_stats.RejectedOwnership;
            return InputSubmitStatus::Unauthorized;
        }

        if (activeWorldId != kInvalidWorldInstanceId && senderSession.WorldId != activeWorldId)
        {
            ++m_stats.RejectedOwnership;
            return InputSubmitStatus::Unauthorized;
        }

        NetworkEntityBinding binding{};
        if (!sessions.TryGetEntityBinding(cmd.TargetEntity, binding) ||
            binding.OwnerSession != senderSession.Id ||
            (activeWorldId != kInvalidWorldInstanceId && binding.WorldId != activeWorldId))
        {
            ++m_stats.RejectedOwnership;
            return InputSubmitStatus::Unauthorized;
        }

        EntityQueue& queue = m_queues[cmd.TargetEntity.Pack()];

        if (queue.WindowStartMs == 0 || (nowMs >= queue.WindowStartMs && (nowMs - queue.WindowStartMs) >= 1000))
        {
            queue.WindowStartMs = nowMs;
            queue.CommandsInWindow = 0;
        }
        if (queue.CommandsInWindow >= m_config.MaxCommandsPerSecond)
        {
            ++m_stats.RejectedRateLimit;
            return InputSubmitStatus::RateLimited;
        }
        ++queue.CommandsInWindow;

        if (cmd.Sequence <= queue.LastAcceptedSequence)
        {
            ++m_stats.RejectedStaleOrDuplicate;
            return InputSubmitStatus::StaleOrDuplicate;
        }
        if (cmd.Sequence - queue.LastAcceptedSequence > m_config.MaxFutureSequenceLead)
        {
            ++m_stats.RejectedFutureLead;
            return InputSubmitStatus::TooFarInFuture;
        }
        if (queue.Pending.size() >= m_config.MaxBufferedPerEntity)
        {
            ++m_stats.RejectedBufferFull;
            return InputSubmitStatus::BufferFull;
        }

        queue.LastAcceptedSequence = cmd.Sequence;
        queue.Pending.push_back(cmd);
        ++m_stats.AcceptedCommands;
        return InputSubmitStatus::Accepted;
    }

    usize ServerInputPipeline::DrainCommands(
        NetworkId entityId,
        std::vector<PlayerInputCommand>& outCommands,
        usize maxCount)
    {
        outCommands.clear();
        auto it = m_queues.find(entityId.Pack());
        if (it == m_queues.end())
        {
            return 0;
        }
        auto& q = it->second.Pending;
        while (!q.empty() && outCommands.size() < maxCount)
        {
            outCommands.push_back(q.front());
            q.pop_front();
        }
        return outCommands.size();
    }

    u32 ServerInputPipeline::GetLastAcceptedSequence(NetworkId entityId) const noexcept
    {
        auto it = m_queues.find(entityId.Pack());
        return it != m_queues.end() ? it->second.LastAcceptedSequence : 0;
    }

    usize ServerInputPipeline::GetQueuedCount(NetworkId entityId) const noexcept
    {
        auto it = m_queues.find(entityId.Pack());
        return it != m_queues.end() ? it->second.Pending.size() : 0u;
    }

    // ------------------------------------------------------------------------
    // ClientInputHistory
    // ------------------------------------------------------------------------

    ClientInputHistory::ClientInputHistory(usize capacity) noexcept
    {
        SetCapacity(capacity);
    }

    void ClientInputHistory::SetCapacity(usize capacity) noexcept
    {
        if (capacity < kMinCapacity)
        {
            m_capacity = kMinCapacity;
        }
        else if (capacity > kMaxCapacity)
        {
            m_capacity = kMaxCapacity;
        }
        else
        {
            m_capacity = capacity;
        }

        while (m_entries.size() > m_capacity)
        {
            m_entries.pop_front();
            ++m_stats.Overflows;
        }
    }

    void ClientInputHistory::Reset() noexcept
    {
        m_entries.clear();
        m_lastPushedSeq = 0;
        m_lastAcknowledgedSeq = 0;
        m_stats = ClientInputHistoryStats{};
    }

    void ClientInputHistory::ClearEntries() noexcept
    {
        m_entries.clear();
    }

    bool ClientInputHistory::Push(const ClientInputHistoryEntry& entry)
    {
        if (!entry.Command.IsValid() ||
            !std::isfinite(entry.FixedDt) ||
            entry.FixedDt <= 0.0f ||
            entry.FixedDt > 0.25f)
        {
            return false;
        }
        if (entry.Command.Sequence <= m_lastPushedSeq)
        {
            return false;
        }

        if (m_entries.size() >= m_capacity)
        {
            m_entries.pop_front();
            ++m_stats.Overflows;
        }

        m_entries.push_back(entry);
        m_lastPushedSeq = entry.Command.Sequence;
        ++m_stats.Pushed;
        return true;
    }

    usize ClientInputHistory::AcknowledgeUpTo(u32 lastProcessedInputSeq) noexcept
    {
        if (lastProcessedInputSeq == 0)
        {
            return 0;
        }
        if (lastProcessedInputSeq <= m_lastAcknowledgedSeq)
        {
            ++m_stats.StaleAcksIgnored;
            return 0;
        }
        if (lastProcessedInputSeq > m_lastPushedSeq)
        {
            ++m_stats.InvalidFutureAcksIgnored;
            return 0;
        }

        usize removed = 0;
        while (!m_entries.empty() && m_entries.front().Command.Sequence <= lastProcessedInputSeq)
        {
            m_entries.pop_front();
            ++removed;
        }
        m_lastAcknowledgedSeq = lastProcessedInputSeq;
        m_stats.Acknowledged += removed;
        return removed;
    }

    void ClientInputHistory::CopyUnacknowledged(std::vector<ClientInputHistoryEntry>& outEntries) const
    {
        outEntries.assign(m_entries.begin(), m_entries.end());
    }

    bool ClientInputHistory::TryGetEntry(u32 sequence, ClientInputHistoryEntry& outEntry) const noexcept
    {
        for (const auto& e : m_entries)
        {
            if (e.Command.Sequence == sequence)
            {
                outEntry = e;
                return true;
            }
        }
        return false;
    }
}
