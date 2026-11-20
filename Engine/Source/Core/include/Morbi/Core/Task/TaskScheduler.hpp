#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Morbi/Core/Types.hpp"

namespace Morbi::Core
{
    class TaskCounter
    {
    public:
        explicit TaskCounter(u32 initialCount = 0) noexcept
            : m_count(initialCount)
        {
        }

        void Increment(u32 count = 1) noexcept
        {
            m_count.fetch_add(count, std::memory_order_relaxed);
        }

        bool Decrement() noexcept
        {
            return m_count.fetch_sub(1, std::memory_order_acq_rel) == 1u;
        }

        [[nodiscard]] u32 Value() const noexcept
        {
            return m_count.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsReady() const noexcept
        {
            return Value() == 0u;
        }

    private:
        std::atomic<u32> m_count{0};
    };

    class MORBI_API TaskScheduler
    {
    public:
        using TaskFn = std::function<void()>;

        explicit TaskScheduler(usize workerCount = 0);
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;

        void Submit(TaskFn task, TaskCounter* counter = nullptr);

        void ParallelFor(u32 itemCount, u32 batchSize, const std::function<void(u32 start, u32 end)>& body);

        void Wait(const TaskCounter& counter);
        void WaitIdle();

        [[nodiscard]] usize WorkerCount() const noexcept { return m_workers.size(); }

    private:
        struct ScheduledTask
        {
            TaskFn Function;
            TaskCounter* Counter = nullptr;
        };

        struct WorkerQueue
        {
            std::mutex Mutex;
            std::deque<ScheduledTask> Deque;
        };

        bool TryPopOrSteal(usize workerIndex, ScheduledTask& outTask);
        void ExecuteTask(ScheduledTask& task);
        void WorkerLoop(usize workerIndex);

        std::vector<std::unique_ptr<WorkerQueue>> m_queues;
        std::vector<std::thread> m_workers;
        std::atomic<usize> m_nextQueueIndex{0};
        std::atomic<u32> m_inFlightTasks{0};
        std::atomic<bool> m_stopping{false};

        std::mutex m_wakeMutex;
        std::condition_variable m_wakeCv;
        std::condition_variable m_idleCv;
    };
}
