#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Morbi/Core/Export.hpp"

namespace Morbi::IO
{
    enum class FileChangeAction
    {
        Added,
        Removed,
        Modified,
        Renamed
    };

    struct FileChangeEvent
    {
        std::filesystem::path Path;
        FileChangeAction Action = FileChangeAction::Modified;
    };

    class MORBI_API FileWatcher
    {
    public:
        using Callback = std::function<void(const FileChangeEvent&)>;

        FileWatcher() = default;
        ~FileWatcher();

        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

        bool Start(const std::filesystem::path& directory, bool recursive = true);
        void Stop();

        void PollEvents(const Callback& callback);

        [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

    private:
        void WatchLoop();

        std::filesystem::path m_directory;
        bool m_recursive = true;
        std::atomic<bool> m_running{false};
        std::thread m_thread;
        void* m_directoryHandle = nullptr;
        void* m_stopEvent = nullptr;

        std::mutex m_eventsMutex;
        std::vector<FileChangeEvent> m_pendingEvents;
    };
}
