#pragma once

#include <string_view>

#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Export.hpp"

namespace Morbi::Core
{
    enum class LogLevel
    {
        Trace = 0,
        Info,
        Warn,
        Error,
        Fatal
    };

    using LogCallbackFn = void (*)(LogLevel level, const char* category, const char* message, const char* timestamp);

    class MORBI_CORE_API Log
    {
    public:
        static void SetMinLevel(LogLevel level) noexcept;
        [[nodiscard]] static LogLevel GetMinLevel() noexcept;

        static void SetImmediateFlush(bool enabled) noexcept;
        [[nodiscard]] static bool IsImmediateFlushEnabled() noexcept;

        static void EnableFileLogging(std::string_view filePath) noexcept;
        static void Flush() noexcept;

        static void SetCallback(LogCallbackFn callback) noexcept;
        static void Write(LogLevel level, std::string_view category, std::string_view message);

        static void Trace(std::string_view category, std::string_view message)
        {
            Write(LogLevel::Trace, category, message);
        }

        static void Info(std::string_view category, std::string_view message)
        {
            Write(LogLevel::Info, category, message);
        }

        static void Warn(std::string_view category, std::string_view message)
        {
            Write(LogLevel::Warn, category, message);
        }

        static void Error(std::string_view category, std::string_view message)
        {
            Write(LogLevel::Error, category, message);
        }

        static void Fatal(std::string_view category, std::string_view message)
        {
            Write(LogLevel::Fatal, category, message);
        }
    };
}

// Zero-overhead trace macro: message expression is NOT evaluated unless --debug is active.
#define MORBI_TRACE(category, messageExpr) \
    do { \
        if (::Morbi::Core::Diagnostics::IsDebugMode()) { \
            ::Morbi::Core::Log::Trace((category), (messageExpr)); \
        } \
    } while (false)
