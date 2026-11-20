#include "Morbi/Core/Log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace Morbi::Core
{
    namespace
    {
        struct LogState
        {
            std::atomic<LogLevel> MinLogLevel{LogLevel::Info};
            std::atomic<bool> ImmediateFlush{false};
            std::atomic<LogCallbackFn> Callback{nullptr};
            std::mutex Mutex;
#if defined(_WIN32)
            HANDLE FileHandle = INVALID_HANDLE_VALUE;
#else
            std::FILE* FileHandle = nullptr;
#endif
            ~LogState()
            {
#if defined(_WIN32)
                if (FileHandle != INVALID_HANDLE_VALUE)
                {
                    FlushFileBuffers(FileHandle);
                    CloseHandle(FileHandle);
                    FileHandle = INVALID_HANDLE_VALUE;
                }
#else
                if (FileHandle != nullptr)
                {
                    std::fflush(FileHandle);
                    std::fclose(FileHandle);
                    FileHandle = nullptr;
                }
#endif
            }
        };

        LogState& GetLogState() noexcept
        {
            static LogState state;
            return state;
        }

        [[nodiscard]] const char* LevelName(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            }
            return "INFO ";
        }
    }

    void Log::SetMinLevel(LogLevel level) noexcept
    {
        GetLogState().MinLogLevel.store(level, std::memory_order_relaxed);
    }

    LogLevel Log::GetMinLevel() noexcept
    {
        return GetLogState().MinLogLevel.load(std::memory_order_relaxed);
    }

    void Log::SetImmediateFlush(bool enabled) noexcept
    {
        GetLogState().ImmediateFlush.store(enabled, std::memory_order_relaxed);
    }

    bool Log::IsImmediateFlushEnabled() noexcept
    {
        return GetLogState().ImmediateFlush.load(std::memory_order_relaxed);
    }

    void Log::EnableFileLogging(std::string_view filePath) noexcept
    {
        if (filePath.empty())
        {
            return;
        }

        auto& state = GetLogState();
        std::lock_guard<std::mutex> lock(state.Mutex);

#if defined(_WIN32)
        if (state.FileHandle != INVALID_HANDLE_VALUE)
        {
            return;
        }

        std::error_code ec;
        const std::filesystem::path targetPath(filePath);
        if (targetPath.has_parent_path())
        {
            std::filesystem::create_directories(targetPath.parent_path(), ec);
        }

        HANDLE hFile = CreateFileW(
            targetPath.wstring().c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            state.FileHandle = hFile;
        }
#else
        if (state.FileHandle != nullptr)
        {
            return;
        }
        std::error_code ec;
        const std::filesystem::path targetPath(filePath);
        if (targetPath.has_parent_path())
        {
            std::filesystem::create_directories(targetPath.parent_path(), ec);
        }
        state.FileHandle = std::fopen(std::string(filePath).c_str(), "ab");
#endif
    }

    void Log::Flush() noexcept
    {
        auto& state = GetLogState();
        std::lock_guard<std::mutex> lock(state.Mutex);
        std::cout.flush();
        std::cerr.flush();
        std::fflush(stdout);
        std::fflush(stderr);
#if defined(_WIN32)
        if (state.FileHandle != INVALID_HANDLE_VALUE)
        {
            FlushFileBuffers(state.FileHandle);
        }
#else
        if (state.FileHandle != nullptr)
        {
            std::fflush(state.FileHandle);
        }
#endif
    }

    void Log::SetCallback(LogCallbackFn callback) noexcept
    {
        GetLogState().Callback.store(callback, std::memory_order_relaxed);
    }

    void Log::Write(LogLevel level, std::string_view category, std::string_view message)
    {
        auto& state = GetLogState();
        if (static_cast<int>(level) < static_cast<int>(state.MinLogLevel.load(std::memory_order_relaxed)))
        {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        const auto timeT = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm localTime{};
#if defined(_WIN32)
        localtime_s(&localTime, &timeT);
#else
        localtime_r(&timeT, &localTime);
#endif

        char timeBuffer[16]{};
        std::snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

        char prefixBuffer[96]{};
        const int prefixLen = std::snprintf(
            prefixBuffer,
            sizeof(prefixBuffer),
            "[%s.%03lld] [%s] [",
            timeBuffer,
            static_cast<long long>(ms.count()),
            LevelName(level));

        std::string line;
        line.reserve(static_cast<size_t>(prefixLen > 0 ? prefixLen : 32) + category.size() + message.size() + 4);
        if (prefixLen > 0)
        {
            line.append(prefixBuffer, static_cast<size_t>(prefixLen));
        }
        line.append(category);
        line.append("] ");
        line.append(message);
        line.push_back('\n');

        const bool immediateFlush = state.ImmediateFlush.load(std::memory_order_relaxed) ||
                                    static_cast<int>(level) >= static_cast<int>(LogLevel::Error);

        {
            std::lock_guard<std::mutex> lock(state.Mutex);
            std::ostream& stream = (static_cast<int>(level) >= static_cast<int>(LogLevel::Error)) ? std::cerr : std::cout;
            stream.write(line.data(), static_cast<std::streamsize>(line.size()));
            if (immediateFlush)
            {
                stream.flush();
                std::fflush(static_cast<int>(level) >= static_cast<int>(LogLevel::Error) ? stderr : stdout);
            }

#if defined(_WIN32)
            if (state.FileHandle != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile(
                    state.FileHandle,
                    line.data(),
                    static_cast<DWORD>(line.size()),
                    &written,
                    nullptr);
                if (immediateFlush)
                {
                    FlushFileBuffers(state.FileHandle);
                }
            }
#else
            if (state.FileHandle != nullptr)
            {
                std::fwrite(line.data(), 1, line.size(), state.FileHandle);
                if (immediateFlush)
                {
                    std::fflush(state.FileHandle);
                }
            }
#endif
        }

        if (auto* cb = state.Callback.load(std::memory_order_relaxed))
        {
            const std::string catStr(category);
            const std::string msgStr(message);
            cb(level, catStr.c_str(), msgStr.c_str(), timeBuffer);
        }

#if defined(_WIN32)
        if (IsDebuggerPresent())
        {
            OutputDebugStringA(line.c_str());
        }
#endif
    }
}
