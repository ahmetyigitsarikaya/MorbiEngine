#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace Morbi::Core::Diagnostics
{
    namespace
    {
        constexpr std::size_t kMaxTrackedThreads = 128;
        constexpr std::size_t kHistoryCapacity = 16;

        struct BreadcrumbEntry
        {
            const char* StepName = nullptr;
            char Detail[96]{};
            std::uint64_t QpcTimestamp = 0;
        };

        struct ThreadBreadcrumbSlot
        {
            std::atomic<bool> Occupied{false};
            std::atomic<bool> Active{false};
            std::uint32_t ThreadId = 0;
            char ThreadName[32]{};
            std::atomic<std::uint64_t> WriteCount{0};
            std::array<BreadcrumbEntry, kHistoryCapacity> History{};
        };

        struct TrackerPool
        {
            std::array<ThreadBreadcrumbSlot, kMaxTrackedThreads> Slots{};
        };

        TrackerPool& GetTrackerPool() noexcept
        {
            static TrackerPool pool;
            return pool;
        }

        std::uint32_t GetCurrentOsThreadId() noexcept
        {
#if defined(_WIN32)
            return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
            return 0u;
#endif
        }

        std::uint64_t QueryFastTimestamp() noexcept
        {
#if defined(_WIN32)
            LARGE_INTEGER li{};
            QueryPerformanceCounter(&li);
            return static_cast<std::uint64_t>(li.QuadPart);
#else
            return 0ull;
#endif
        }

        struct ThreadSlotBinding
        {
            ThreadBreadcrumbSlot* Slot = nullptr;

            ThreadSlotBinding() noexcept
            {
                auto& pool = GetTrackerPool();
                for (auto& candidate : pool.Slots)
                {
                    bool expected = false;
                    if (candidate.Occupied.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        candidate.ThreadId = GetCurrentOsThreadId();
                        std::snprintf(candidate.ThreadName, sizeof(candidate.ThreadName), "Thread-%u", candidate.ThreadId);
                        candidate.WriteCount.store(0, std::memory_order_relaxed);
                        candidate.Active.store(true, std::memory_order_release);
                        Slot = &candidate;
                        return;
                    }
                }
            }

            ~ThreadSlotBinding() noexcept
            {
                if (Slot != nullptr)
                {
                    Slot->Active.store(false, std::memory_order_release);
                }
            }
        };

        ThreadBreadcrumbSlot* GetOrBindCurrentThreadSlot() noexcept
        {
            thread_local ThreadSlotBinding binding;
            return binding.Slot;
        }

        void AppendFormatted(char* outBuffer, std::size_t capacity, std::size_t& cursor, const char* text) noexcept
        {
            if (outBuffer == nullptr || capacity == 0 || text == nullptr || cursor >= capacity - 1)
            {
                return;
            }
            const std::size_t len = std::strlen(text);
            const std::size_t avail = (capacity - 1) - cursor;
            const std::size_t toCopy = (std::min)(len, avail);
            std::memcpy(outBuffer + cursor, text, toCopy);
            cursor += toCopy;
            outBuffer[cursor] = '\0';
        }
    }

    void BreadcrumbTracker::SetCurrentThreadName(const char* threadName) noexcept
    {
        if (threadName == nullptr || threadName[0] == '\0')
        {
            return;
        }
        if (ThreadBreadcrumbSlot* slot = GetOrBindCurrentThreadSlot())
        {
            std::snprintf(slot->ThreadName, sizeof(slot->ThreadName), "%s", threadName);
        }
    }

    void BreadcrumbTracker::Record(const char* stepName, const char* detail) noexcept
    {
        if (stepName == nullptr)
        {
            return;
        }

        ThreadBreadcrumbSlot* slot = GetOrBindCurrentThreadSlot();
        if (slot != nullptr)
        {
            const std::uint64_t seq = slot->WriteCount.load(std::memory_order_relaxed);
            BreadcrumbEntry& entry = slot->History[seq % kHistoryCapacity];
            entry.StepName = stepName;
            if (detail != nullptr && detail[0] != '\0')
            {
                std::snprintf(entry.Detail, sizeof(entry.Detail), "%s", detail);
            }
            else
            {
                entry.Detail[0] = '\0';
            }
            entry.QpcTimestamp = QueryFastTimestamp();
            slot->WriteCount.store(seq + 1, std::memory_order_release);
        }

        if (IsDebugMode())
        {
            char traceMsg[256]{};
            const std::uint32_t tid = slot != nullptr ? slot->ThreadId : GetCurrentOsThreadId();
            if (detail != nullptr && detail[0] != '\0')
            {
                std::snprintf(traceMsg, sizeof(traceMsg), "[TID:%u] %s | %s", tid, stepName, detail);
            }
            else
            {
                std::snprintf(traceMsg, sizeof(traceMsg), "[TID:%u] %s", tid, stepName);
            }
            Log::Trace("Breadcrumb", traceMsg);
        }
    }

    std::size_t BreadcrumbTracker::WriteSummaryToBuffer(char* outBuffer, std::size_t bufferCapacity) noexcept
    {
        if (outBuffer == nullptr || bufferCapacity == 0)
        {
            return 0;
        }

        outBuffer[0] = '\0';
        std::size_t cursor = 0;

        auto& pool = GetTrackerPool();
        bool anyRecorded = false;
        char lineBuf[320]{};

        for (const auto& slot : pool.Slots)
        {
            if (!slot.Occupied.load(std::memory_order_acquire))
            {
                continue;
            }
            const std::uint64_t totalWrites = slot.WriteCount.load(std::memory_order_acquire);
            if (totalWrites == 0)
            {
                continue;
            }

            anyRecorded = true;
            const std::size_t latestIdx = static_cast<std::size_t>((totalWrites - 1) % kHistoryCapacity);
            const BreadcrumbEntry& latest = slot.History[latestIdx];
            const bool isActive = slot.Active.load(std::memory_order_acquire);

            if (latest.Detail[0] != '\0')
            {
                std::snprintf(
                    lineBuf,
                    sizeof(lineBuf),
                    "  [%s | TID=%u | %s] LastStep: %s (%s)\n",
                    slot.ThreadName,
                    slot.ThreadId,
                    isActive ? "ACTIVE" : "EXITED",
                    latest.StepName ? latest.StepName : "<null>",
                    latest.Detail);
            }
            else
            {
                std::snprintf(
                    lineBuf,
                    sizeof(lineBuf),
                    "  [%s | TID=%u | %s] LastStep: %s\n",
                    slot.ThreadName,
                    slot.ThreadId,
                    isActive ? "ACTIVE" : "EXITED",
                    latest.StepName ? latest.StepName : "<null>");
            }
            AppendFormatted(outBuffer, bufferCapacity, cursor, lineBuf);

            // Print up to 5 recent steps leading up to the latest step
            const std::uint64_t historyCount = (std::min)(totalWrites, std::uint64_t{5});
            for (std::uint64_t back = 1; back < historyCount; ++back)
            {
                const std::size_t idx = static_cast<std::size_t>((totalWrites - 1 - back) % kHistoryCapacity);
                const BreadcrumbEntry& prev = slot.History[idx];
                if (prev.StepName == nullptr)
                {
                    continue;
                }
                if (prev.Detail[0] != '\0')
                {
                    std::snprintf(
                        lineBuf,
                        sizeof(lineBuf),
                        "      <- -%llu: %s (%s)\n",
                        static_cast<unsigned long long>(back),
                        prev.StepName,
                        prev.Detail);
                }
                else
                {
                    std::snprintf(
                        lineBuf,
                        sizeof(lineBuf),
                        "      <- -%llu: %s\n",
                        static_cast<unsigned long long>(back),
                        prev.StepName);
                }
                AppendFormatted(outBuffer, bufferCapacity, cursor, lineBuf);
            }
        }

        if (!anyRecorded)
        {
            AppendFormatted(outBuffer, bufferCapacity, cursor, "  (No breadcrumbs recorded yet)\n");
        }

        return cursor;
    }

    std::string BreadcrumbTracker::FormatSummary()
    {
        std::array<char, 8192> buffer{};
        const std::size_t len = WriteSummaryToBuffer(buffer.data(), buffer.size());
        return std::string(buffer.data(), len);
    }
}
