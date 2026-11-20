#include "Morbi/Core/Task/TaskScheduler.hpp"

#include <algorithm>

namespace Morbi::Core
{
    namespace
    {
        thread_local isize t_workerIndex = -1;
    }

    TaskScheduler::TaskScheduler(usize workerCount)
    {
        const usize hardwareThreads = std::max<usize>(1u, std::thread::hardware_concurrency());
        const usize resolvedCount = workerCount == 0 ? std::max<usize>(1u, hardwareThreads - 1u) : workerCount;

        m_queues.reserve(resolvedCount);
        for (usize i = 0; i < resolvedCount; ++i)
        {
            m_queues.push_back(std::make_unique<WorkerQueue>());
        }

        m_workers.reserve(resolvedCount);
        for (usize i = 0; i < resolvedCount; ++i)
        {
            m_workers.emplace_back(&TaskScheduler::WorkerLoop, this, i);
        }
    }

    TaskScheduler::~TaskScheduler()
    {
        WaitIdle();
        m_stopping.store(true, std::memory_order_release);
        m_wakeCv.notify_all();

        for (std::thread& worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void TaskScheduler::Submit(TaskFn task, TaskCounter* counter)
    {
        if (!task)
        {
            return;
        }

        if (counter != nullptr)
        {
            counter->Increment(1);
        }

        m_inFlightTasks.fetch_add(1, std::memory_order_relaxed);

        const usize queueCount = m_queues.size();
        const usize targetQueue = (t_workerIndex >= 0 && static_cast<usize>(t_workerIndex) < queueCount)
            ? static_cast<usize>(t_workerIndex)
            : (m_nextQueueIndex.fetch_add(1, std::memory_order_relaxed) % queueCount);

        {
            std::lock_guard<std::mutex> lock(m_queues[targetQueue]->Mutex);
            m_queues[targetQueue]->Deque.push_back(ScheduledTask{ std::move(task), counter });
        }

        m_wakeCv.notify_one();
    }

    void TaskScheduler::ParallelFor(u32 itemCount, u32 batchSize, const std::function<void(u32 start, u32 end)>& body)
    {
        if (itemCount == 0 || !body)
        {
            return;
        }

        const u32 effectiveBatch = std::max<u32>(1u, batchSize);
        const u32 batchCount = (itemCount + effectiveBatch - 1u) / effectiveBatch;

        if (batchCount == 1u)
        {
            body(0u, itemCount);
            return;
        }

        TaskCounter counter(0);
        for (u32 batchIdx = 0; batchIdx < batchCount; ++batchIdx)
        {
            const u32 start = batchIdx * effectiveBatch;
            const u32 end = std::min<u32>(start + effectiveBatch, itemCount);
            Submit([&body, start, end]() {
                body(start, end);
            }, &counter);
        }

        Wait(counter);
    }

    void TaskScheduler::Wait(const TaskCounter& counter)
    {
        const usize startQueue = (t_workerIndex >= 0) ? static_cast<usize>(t_workerIndex) : 0u;
        while (!counter.IsReady())
        {
            ScheduledTask task{};
            if (TryPopOrSteal(startQueue, task))
            {
                ExecuteTask(task);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    void TaskScheduler::WaitIdle()
    {
        const usize startQueue = (t_workerIndex >= 0) ? static_cast<usize>(t_workerIndex) : 0u;
        while (m_inFlightTasks.load(std::memory_order_acquire) > 0u)
        {
            ScheduledTask task{};
            if (TryPopOrSteal(startQueue, task))
            {
                ExecuteTask(task);
            }
            else
            {
                std::unique_lock<std::mutex> lock(m_wakeMutex);
                m_idleCv.wait_for(lock, std::chrono::microseconds(100), [this]() {
                    return m_inFlightTasks.load(std::memory_order_acquire) == 0u;
                });
            }
        }
    }

    bool TaskScheduler::TryPopOrSteal(usize workerIndex, ScheduledTask& outTask)
    {
        const usize queueCount = m_queues.size();
        if (queueCount == 0)
        {
            return false;
        }

        // 1. Try local queue LIFO (better cache locality)
        {
            WorkerQueue& local = *m_queues[workerIndex % queueCount];
            std::unique_lock<std::mutex> lock(local.Mutex, std::try_to_lock);
            if (lock.owns_lock() && !local.Deque.empty())
            {
                outTask = std::move(local.Deque.back());
                local.Deque.pop_back();
                return true;
            }
        }

        // 2. Steal from other queues FIFO
        for (usize offset = 1; offset <= queueCount; ++offset)
        {
            WorkerQueue& victim = *m_queues[(workerIndex + offset) % queueCount];
            std::unique_lock<std::mutex> lock(victim.Mutex, std::try_to_lock);
            if (lock.owns_lock() && !victim.Deque.empty())
            {
                outTask = std::move(victim.Deque.front());
                victim.Deque.pop_front();
                return true;
            }
        }

        return false;
    }

    void TaskScheduler::ExecuteTask(ScheduledTask& task)
    {
        if (task.Function)
        {
            task.Function();
        }

        if (task.Counter != nullptr)
        {
            task.Counter->Decrement();
        }

        if (m_inFlightTasks.fetch_sub(1, std::memory_order_acq_rel) == 1u)
        {
            std::lock_guard<std::mutex> lock(m_wakeMutex);
            m_idleCv.notify_all();
        }
    }

    void TaskScheduler::WorkerLoop(usize workerIndex)
    {
        t_workerIndex = static_cast<isize>(workerIndex);

        while (!m_stopping.load(std::memory_order_acquire))
        {
            ScheduledTask task{};
            if (TryPopOrSteal(workerIndex, task))
            {
                ExecuteTask(task);
                continue;
            }

            std::unique_lock<std::mutex> lock(m_wakeMutex);
            m_wakeCv.wait_for(lock, std::chrono::milliseconds(1), [this]() {
                return m_stopping.load(std::memory_order_acquire) ||
                       m_inFlightTasks.load(std::memory_order_acquire) > 0u;
            });
        }
    }
}
