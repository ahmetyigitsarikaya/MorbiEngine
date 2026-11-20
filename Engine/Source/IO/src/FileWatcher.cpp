#include "Morbi/IO/FileWatcher.hpp"

#if defined(_WIN32)
    #include <Windows.h>
#endif

#include <utility>

namespace Morbi::IO
{
    FileWatcher::~FileWatcher()
    {
        Stop();
    }

    bool FileWatcher::Start(const std::filesystem::path& directory, bool recursive)
    {
        Stop();

        if (!std::filesystem::is_directory(directory))
        {
            return false;
        }

        m_directory = std::filesystem::absolute(directory).lexically_normal();
        m_recursive = recursive;

#if defined(_WIN32)
        HANDLE dirHandle = CreateFileW(
            m_directory.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (dirHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent == nullptr)
        {
            CloseHandle(dirHandle);
            return false;
        }

        m_directoryHandle = dirHandle;
        m_stopEvent = stopEvent;
        m_running.store(true, std::memory_order_release);
        m_thread = std::thread(&FileWatcher::WatchLoop, this);
        return true;
#else
        return false;
#endif
    }

    void FileWatcher::Stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

#if defined(_WIN32)
        if (m_stopEvent != nullptr)
        {
            SetEvent(static_cast<HANDLE>(m_stopEvent));
        }
#endif

        if (m_thread.joinable())
        {
            m_thread.join();
        }

#if defined(_WIN32)
        if (m_directoryHandle != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_directoryHandle));
            m_directoryHandle = nullptr;
        }
        if (m_stopEvent != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_stopEvent));
            m_stopEvent = nullptr;
        }
#endif
    }

    void FileWatcher::PollEvents(const Callback& callback)
    {
        std::vector<FileChangeEvent> events;
        {
            std::lock_guard<std::mutex> lock(m_eventsMutex);
            events.swap(m_pendingEvents);
        }

        if (!callback)
        {
            return;
        }

        for (const FileChangeEvent& ev : events)
        {
            callback(ev);
        }
    }

    void FileWatcher::WatchLoop()
    {
#if defined(_WIN32)
        HANDLE dirHandle = static_cast<HANDLE>(m_directoryHandle);
        HANDLE stopEvent = static_cast<HANDLE>(m_stopEvent);
        HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ioEvent == nullptr)
        {
            return;
        }

        alignas(DWORD) std::byte buffer[16384]{};

        while (m_running.load(std::memory_order_acquire))
        {
            ResetEvent(ioEvent);
            OVERLAPPED overlapped{};
            overlapped.hEvent = ioEvent;

            DWORD bytesReturned = 0;
            const BOOL issued = ReadDirectoryChangesW(
                dirHandle,
                buffer,
                static_cast<DWORD>(sizeof(buffer)),
                m_recursive ? TRUE : FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                &bytesReturned,
                &overlapped,
                nullptr);

            if (!issued)
            {
                break;
            }

            HANDLE handles[2] = { stopEvent, ioEvent };
            const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0)
            {
                CancelIoEx(dirHandle, &overlapped);
                break;
            }

            if (waitResult == WAIT_OBJECT_0 + 1)
            {
                if (GetOverlappedResult(dirHandle, &overlapped, &bytesReturned, FALSE) && bytesReturned > 0)
                {
                    const std::byte* cursor = buffer;
                    for (;;)
                    {
                        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
                        std::wstring relativeName(info->FileName, info->FileNameLength / sizeof(WCHAR));

                        FileChangeAction action = FileChangeAction::Modified;
                        switch (info->Action)
                        {
                        case FILE_ACTION_ADDED:            action = FileChangeAction::Added; break;
                        case FILE_ACTION_REMOVED:          action = FileChangeAction::Removed; break;
                        case FILE_ACTION_MODIFIED:         action = FileChangeAction::Modified; break;
                        case FILE_ACTION_RENAMED_OLD_NAME:
                        case FILE_ACTION_RENAMED_NEW_NAME: action = FileChangeAction::Renamed; break;
                        default:                           action = FileChangeAction::Modified; break;
                        }

                        {
                            std::lock_guard<std::mutex> lock(m_eventsMutex);
                            m_pendingEvents.push_back(FileChangeEvent{
                                .Path = (m_directory / relativeName).lexically_normal(),
                                .Action = action
                            });
                        }

                        if (info->NextEntryOffset == 0)
                        {
                            break;
                        }
                        cursor += info->NextEntryOffset;
                    }
                }
            }
        }

        CloseHandle(ioEvent);
#endif
    }
}
