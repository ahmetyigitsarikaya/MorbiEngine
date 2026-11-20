#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Morbi/Core/Export.hpp"

namespace Morbi::Core::Diagnostics
{
    // Lock-free, zero-allocation-on-hot-path per-thread breadcrumb tracker.
    // Records the most recent execution steps for every thread so CrashHandler
    // can always report where each thread was when an exception occurred.
    class MORBI_CORE_API BreadcrumbTracker
    {
    public:
        static void Record(const char* stepName, const char* detail = nullptr) noexcept;
        static void SetCurrentThreadName(const char* threadName) noexcept;

        // Formats each thread's last recorded step and recent history into outBuffer
        // without performing heap allocations (safe during SEH crash handling).
        static std::size_t WriteSummaryToBuffer(char* outBuffer, std::size_t bufferCapacity) noexcept;

        [[nodiscard]] static std::string FormatSummary();
    };
}

#define MORBI_BREADCRUMB(stepName) \
    ::Morbi::Core::Diagnostics::BreadcrumbTracker::Record(stepName)

#define MORBI_BREADCRUMB_DETAIL(stepName, detail) \
    ::Morbi::Core::Diagnostics::BreadcrumbTracker::Record(stepName, detail)
