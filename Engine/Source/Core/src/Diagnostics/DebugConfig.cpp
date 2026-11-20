#include "Morbi/Core/Diagnostics/DebugConfig.h"

#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Log.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <shellapi.h>
#endif

namespace Morbi::Core::Diagnostics
{
    MORBI_CORE_API std::atomic<bool> g_IsDebugMode{false};
    MORBI_CORE_API std::atomic<bool> g_IsGpuGbvMode{false};

    namespace
    {
        void ApplyRuntimeDebugSettings() noexcept
        {
            if (g_IsDebugMode.load(std::memory_order_relaxed))
            {
                Log::SetMinLevel(LogLevel::Trace);
                Log::SetImmediateFlush(true);
                Log::EnableFileLogging("Logs/MorbiEngine_Debug.log");
            }
        }

        void ParseSingleToken(std::string_view token, bool& outDebug, bool& outGbv) noexcept
        {
            if (token == "--debug" || token == "-d")
            {
                outDebug = true;
            }
            else if (token == "--gpu-gbv")
            {
                outGbv = true;
            }
        }

        struct DebugConfigAutoBootstrap
        {
            DebugConfigAutoBootstrap() noexcept
            {
                DebugConfig::InitializeFromProcessCommandLine();
            }
        };

        // Runs at Morbi.Core.dll load time so flags are active before any subsystem starts.
        static DebugConfigAutoBootstrap s_autoBootstrap;
    }

    void DebugConfig::Initialize(int argc, const char* const* argv) noexcept
    {
        bool debugEnabled = g_IsDebugMode.load(std::memory_order_relaxed);
        bool gbvEnabled = g_IsGpuGbvMode.load(std::memory_order_relaxed);

        if (argv != nullptr)
        {
            for (int i = 0; i < argc; ++i)
            {
                if (argv[i] != nullptr)
                {
                    ParseSingleToken(argv[i], debugEnabled, gbvEnabled);
                }
            }
        }

        g_IsDebugMode.store(debugEnabled, std::memory_order_relaxed);
        g_IsGpuGbvMode.store(gbvEnabled, std::memory_order_relaxed);
        ApplyRuntimeDebugSettings();

        if (debugEnabled)
        {
            Log::Trace("DebugConfig", std::string("Diagnostics active: --debug=ON, --gpu-gbv=") +
                (gbvEnabled ? "ON" : "OFF"));
        }
    }

    void DebugConfig::InitializeFromProcessCommandLine() noexcept
    {
        bool debugEnabled = g_IsDebugMode.load(std::memory_order_relaxed);
        bool gbvEnabled = g_IsGpuGbvMode.load(std::memory_order_relaxed);

#if defined(_WIN32)
        const wchar_t* cmdLine = GetCommandLineW();
        if (cmdLine != nullptr)
        {
            int argc = 0;
            LPWSTR* argvW = CommandLineToArgvW(cmdLine, &argc);
            if (argvW != nullptr)
            {
                for (int i = 0; i < argc; ++i)
                {
                    if (argvW[i] == nullptr)
                    {
                        continue;
                    }
                    const int needed = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
                    if (needed > 1)
                    {
                        std::string utf8(static_cast<size_t>(needed - 1), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, utf8.data(), needed, nullptr, nullptr);
                        ParseSingleToken(utf8, debugEnabled, gbvEnabled);
                    }
                }
                LocalFree(argvW);
            }
        }
#endif

        g_IsDebugMode.store(debugEnabled, std::memory_order_relaxed);
        g_IsGpuGbvMode.store(gbvEnabled, std::memory_order_relaxed);
        ApplyRuntimeDebugSettings();
    }

    void DebugConfig::SetDebugMode(bool enabled) noexcept
    {
        g_IsDebugMode.store(enabled, std::memory_order_relaxed);
        if (enabled)
        {
            ApplyRuntimeDebugSettings();
        }
        else
        {
            Log::SetImmediateFlush(false);
            Log::SetMinLevel(LogLevel::Info);
        }
    }

    void DebugConfig::SetGpuGbvMode(bool enabled) noexcept
    {
        g_IsGpuGbvMode.store(enabled, std::memory_order_relaxed);
    }
}
