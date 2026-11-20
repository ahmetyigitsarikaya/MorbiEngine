#pragma once

#include <atomic>
#include <string_view>

#include "Morbi/Core/Export.hpp"

namespace Morbi::Core::Diagnostics
{
    // Process-wide atomic flag enabled by `--debug` or `-d` at engine bootstrap.
    // Outside debug mode, callers pay only a single relaxed atomic read.
    extern MORBI_CORE_API std::atomic<bool> g_IsDebugMode;
    extern MORBI_CORE_API std::atomic<bool> g_IsGpuGbvMode;

    class MORBI_CORE_API DebugConfig
    {
    public:
        // Parses command-line arguments (`--debug`, `-d`, `--gpu-gbv`) and activates
        // TRACE logging + immediate disk/console flushing when debug mode is enabled.
        static void Initialize(int argc, const char* const* argv) noexcept;

        // Parses the OS process command line (GetCommandLineW on Windows) at DLL bootstrap.
        static void InitializeFromProcessCommandLine() noexcept;

        [[nodiscard]] static bool IsDebugMode() noexcept
        {
            return g_IsDebugMode.load(std::memory_order_relaxed);
        }

        [[nodiscard]] static bool IsGpuGbvMode() noexcept
        {
            return g_IsGpuGbvMode.load(std::memory_order_relaxed);
        }

        static void SetDebugMode(bool enabled) noexcept;
        static void SetGpuGbvMode(bool enabled) noexcept;
    };

    [[nodiscard]] inline bool IsDebugMode() noexcept
    {
        return g_IsDebugMode.load(std::memory_order_relaxed);
    }

    [[nodiscard]] inline bool IsGpuGbvMode() noexcept
    {
        return g_IsGpuGbvMode.load(std::memory_order_relaxed);
    }
}
