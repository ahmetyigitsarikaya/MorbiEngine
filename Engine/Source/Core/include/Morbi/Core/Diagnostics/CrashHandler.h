#pragma once

#include "Morbi/Core/Export.hpp"

namespace Morbi::Core::Diagnostics
{
    class MORBI_CORE_API CrashHandler
    {
    public:
        // Installs native SEH (SetUnhandledExceptionFilter + VectoredExceptionHandler),
        // std::terminate, and purecall handlers. Idempotent and safe in Release builds.
        static void Install() noexcept;

        // Re-asserts SetUnhandledExceptionFilter after third-party runtimes (e.g. CoreCLR / WebView2)
        // initialize and potentially replace the process filter.
        static void ReassertFilter() noexcept;

        // Logs and persists an unhandled managed (.NET CoreCLR) exception immediately to disk.
        static void ReportManagedException(
            const char* exceptionType,
            const char* message,
            const char* stackTrace) noexcept;

        // Uninstalls handlers on orderly shutdown if needed.
        static void Uninstall() noexcept;
    };
}
