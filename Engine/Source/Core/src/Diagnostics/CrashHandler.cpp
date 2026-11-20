#include "Morbi/Core/Diagnostics/CrashHandler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #pragma warning(push)
    #pragma warning(disable : 4091)
    #include <DbgHelp.h>
    #pragma warning(pop)
    #include <eh.h>
#endif

namespace Morbi::Core::Diagnostics
{
#if defined(_WIN32)
    namespace
    {
        constexpr DWORD kExceptionCppThrow       = 0xE06D7363u;
        constexpr DWORD kExceptionClrManaged     = 0xE0434352u;
        constexpr DWORD kExceptionSetThreadName  = 0x406D1388u;
        constexpr DWORD kExceptionOutputDebugA   = 0x40010006u;
        constexpr DWORD kExceptionOutputDebugW   = 0x4001000Au;
        constexpr DWORD kExceptionCtrlC          = 0x40010005u;
        constexpr DWORD kExceptionCtrlBreak      = 0x40010008u;
        constexpr DWORD kExceptionRpcServerUnavl = 0x000006BAu;
        constexpr DWORD kExceptionComDisconnected = 0x80010108u;
        constexpr DWORD kExceptionComCantCall    = 0x8001010Du;
        constexpr DWORD kExceptionHeapCorruption = 0xC0000374u;
        constexpr DWORD kExceptionStackBufOverrun = 0xC0000409u;

        struct CrashHandlerState
        {
            std::atomic<bool> Installed{false};
            std::atomic<bool> HandlingCrash{false};
            PVOID VehHandle = nullptr;
            LPTOP_LEVEL_EXCEPTION_FILTER PreviousFilter = nullptr;
            std::terminate_handler PreviousTerminate = nullptr;
            _purecall_handler PreviousPureCall = nullptr;
        };

        CrashHandlerState& GetCrashState() noexcept
        {
            static CrashHandlerState state;
            return state;
        }

        const char* ExceptionCodeToString(DWORD code) noexcept
        {
            switch (code)
            {
            case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
            case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
            case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
            case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
            case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
            case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
            case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
            case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
            case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
            case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
            case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
            case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
            case kExceptionCppThrow:                 return "MSVC_CPP_UNHANDLED_EXCEPTION";
            case kExceptionClrManaged:               return "DOTNET_CLR_MANAGED_EXCEPTION";
            case kExceptionHeapCorruption:           return "STATUS_HEAP_CORRUPTION";
            case kExceptionStackBufOverrun:          return "STATUS_STACK_BUFFER_OVERRUN";
            default:                                 return "UNKNOWN_SEH_EXCEPTION";
            }
        }

        bool IsBenignFirstChanceException(DWORD code) noexcept
        {
            switch (code)
            {
            case kExceptionOutputDebugA:
            case kExceptionOutputDebugW:
            case kExceptionSetThreadName:
            case kExceptionCtrlC:
            case kExceptionCtrlBreak:
            case kExceptionCppThrow:
            case kExceptionClrManaged:
            case kExceptionRpcServerUnavl:
            case kExceptionComDisconnected:
            case kExceptionComCantCall:
            case EXCEPTION_BREAKPOINT:
            case EXCEPTION_SINGLE_STEP:
                return true;
            default:
                return false;
            }
        }

        std::string BuildSymbolSearchPath() noexcept
        {
            std::string searchPath;
            auto appendDir = [&](const std::filesystem::path& dir) {
                if (dir.empty())
                {
                    return;
                }
                const std::string s = dir.string();
                if (!searchPath.empty())
                {
                    searchPath.push_back(';');
                }
                searchPath.append(s);
            };

            wchar_t exeBuf[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH) > 0)
            {
                appendDir(std::filesystem::path(exeBuf).parent_path());
            }

            HMODULE coreMod = nullptr;
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&CrashHandler::Install),
                    &coreMod) &&
                coreMod != nullptr)
            {
                wchar_t modBuf[MAX_PATH]{};
                if (GetModuleFileNameW(coreMod, modBuf, MAX_PATH) > 0)
                {
                    appendDir(std::filesystem::path(modBuf).parent_path());
                }
            }

            std::error_code ec;
            appendDir(std::filesystem::current_path(ec));
            return searchPath;
        }

        void AppendStr(std::string& dst, const char* text) noexcept
        {
            if (text != nullptr)
            {
                dst.append(text);
            }
        }

        void PrintRedToConsole(std::string_view text) noexcept
        {
            HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
            if (hStdErr == INVALID_HANDLE_VALUE || hStdErr == nullptr)
            {
                std::fwrite(text.data(), 1, text.size(), stderr);
                std::fflush(stderr);
                return;
            }

            CONSOLE_SCREEN_BUFFER_INFO csbi{};
            const bool isConsole = GetConsoleScreenBufferInfo(hStdErr, &csbi) != FALSE;
            if (isConsole)
            {
                SetConsoleTextAttribute(hStdErr, FOREGROUND_RED | FOREGROUND_INTENSITY);
            }

            DWORD mode = 0;
            if (GetConsoleMode(hStdErr, &mode) != FALSE)
            {
                DWORD written = 0;
                WriteConsoleA(hStdErr, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            }
            else
            {
                DWORD written = 0;
                WriteFile(hStdErr, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            }

            if (isConsole)
            {
                SetConsoleTextAttribute(hStdErr, csbi.wAttributes);
            }
        }

        void WriteUnbufferedFile(const std::filesystem::path& filePath, std::string_view content) noexcept
        {
            std::error_code ec;
            if (filePath.has_parent_path())
            {
                std::filesystem::create_directories(filePath.parent_path(), ec);
            }

            HANDLE hFile = CreateFileW(
                filePath.wstring().c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                nullptr);

            if (hFile == INVALID_HANDLE_VALUE)
            {
                return;
            }

            const char* ptr = content.data();
            DWORD remaining = static_cast<DWORD>(content.size());
            while (remaining > 0)
            {
                DWORD written = 0;
                if (!WriteFile(hFile, ptr, remaining, &written, nullptr) || written == 0)
                {
                    break;
                }
                ptr += written;
                remaining -= written;
            }
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
        }

        struct CrashCaptureContext
        {
            EXCEPTION_POINTERS* ExceptionPointers = nullptr;
            DWORD FaultingThreadId = 0;
            HANDLE FaultingThreadHandle = nullptr;
            const char* CustomHeader = nullptr;
        };

        void CaptureAndEmitCrashReport(const CrashCaptureContext& ctx) noexcept
        {
            Log::Flush();

            SYSTEMTIME st{};
            GetLocalTime(&st);

            std::error_code ec;
            const std::filesystem::path crashDir = std::filesystem::current_path(ec) / "Logs" / "Crashes";
            std::filesystem::create_directories(crashDir, ec);

            char dmpFileName[96]{};
            std::snprintf(
                dmpFileName,
                sizeof(dmpFileName),
                "Crash_%04u%02u%02u_%02u%02u%02u.dmp",
                st.wYear,
                st.wMonth,
                st.wDay,
                st.wHour,
                st.wMinute,
                st.wSecond);
            const std::filesystem::path dumpPath = crashDir / dmpFileName;
            const std::filesystem::path latestLogPath = crashDir / "Crash_Latest.log";

            // 1. Write MiniDump first while process state is clean
            bool dumpWritten = false;
            if (ctx.ExceptionPointers != nullptr)
            {
                HANDLE hDumpFile = CreateFileW(
                    dumpPath.wstring().c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);

                if (hDumpFile != INVALID_HANDLE_VALUE)
                {
                    MINIDUMP_EXCEPTION_INFORMATION mei{};
                    mei.ThreadId = ctx.FaultingThreadId;
                    mei.ExceptionPointers = ctx.ExceptionPointers;
                    mei.ClientPointers = FALSE;

                    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                        MiniDumpWithDataSegs |
                        MiniDumpWithHandleData |
                        MiniDumpWithThreadInfo |
                        MiniDumpWithUnloadedModules |
                        MiniDumpWithIndirectlyReferencedMemory);

                    dumpWritten = MiniDumpWriteDump(
                        GetCurrentProcess(),
                        GetCurrentProcessId(),
                        hDumpFile,
                        dumpType,
                        &mei,
                        nullptr,
                        nullptr) != FALSE;

                    FlushFileBuffers(hDumpFile);
                    CloseHandle(hDumpFile);
                }
            }

            // 2. Build textual crash report (Breadcrumbs at top, Exception, Registers, Call Stack)
            std::string report;
            report.reserve(16384);

            char line[1024]{};
            AppendStr(report, "================================================================================\n");
            AppendStr(report, "                       MORBI ENGINE FATAL CRASH REPORT                          \n");
            AppendStr(report, "================================================================================\n");

            std::snprintf(
                line,
                sizeof(line),
                "Timestamp      : %04u-%02u-%02u %02u:%02u:%02u.%03u\n"
                "Process ID     : %lu\n"
                "Faulting TID   : %lu\n"
                "Debug Mode     : %s (--gpu-gbv=%s)\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                static_cast<unsigned long>(GetCurrentProcessId()),
                static_cast<unsigned long>(ctx.FaultingThreadId),
                IsDebugMode() ? "ON" : "OFF",
                IsGpuGbvMode() ? "ON" : "OFF");
            report.append(line);

            if (ctx.CustomHeader != nullptr)
            {
                std::snprintf(line, sizeof(line), "Failure Origin : %s\n", ctx.CustomHeader);
                report.append(line);
            }

            // Last Breadcrumbs section right at the top of the report
            AppendStr(report, "\n--- LAST THREAD BREADCRUMBS (LATEST STEP PER THREAD) ---\n");
            std::array<char, 8192> breadcrumbBuf{};
            const std::size_t bcLen = BreadcrumbTracker::WriteSummaryToBuffer(
                breadcrumbBuf.data(),
                breadcrumbBuf.size());
            report.append(breadcrumbBuf.data(), bcLen);

            // Exception Details & CPU Registers
            if (ctx.ExceptionPointers != nullptr && ctx.ExceptionPointers->ExceptionRecord != nullptr)
            {
                const EXCEPTION_RECORD* rec = ctx.ExceptionPointers->ExceptionRecord;
                std::snprintf(
                    line,
                    sizeof(line),
                    "\n--- EXCEPTION DETAILS ---\n"
                    "Exception Code : 0x%08lX (%s)\n"
                    "Exception Flags: 0x%08lX\n"
                    "Faulting Addr  : 0x%016llX\n",
                    static_cast<unsigned long>(rec->ExceptionCode),
                    ExceptionCodeToString(rec->ExceptionCode),
                    static_cast<unsigned long>(rec->ExceptionFlags),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress)));
                report.append(line);

                if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
                    rec->NumberParameters >= 2)
                {
                    const char* opStr = (rec->ExceptionInformation[0] == 0) ? "READ" :
                                        (rec->ExceptionInformation[0] == 1) ? "WRITE" :
                                        (rec->ExceptionInformation[0] == 8) ? "DEP_EXEC" : "UNKNOWN";
                    std::snprintf(
                        line,
                        sizeof(line),
                        "AV Operation   : %s at virtual address 0x%016llX\n",
                        opStr,
                        static_cast<unsigned long long>(rec->ExceptionInformation[1]));
                    report.append(line);
                }
            }

            CONTEXT walkContext{};
            bool hasContext = false;
            if (ctx.ExceptionPointers != nullptr && ctx.ExceptionPointers->ContextRecord != nullptr)
            {
                walkContext = *ctx.ExceptionPointers->ContextRecord;
                hasContext = true;
            }
            else
            {
                RtlCaptureContext(&walkContext);
                hasContext = true;
            }

#if defined(_M_X64) || defined(__x86_64__)
            if (hasContext)
            {
                std::snprintf(
                    line,
                    sizeof(line),
                    "\n--- CPU REGISTERS (x64) ---\n"
                    "RAX=0x%016llX  RBX=0x%016llX  RCX=0x%016llX  RDX=0x%016llX\n"
                    "RSI=0x%016llX  RDI=0x%016llX  RBP=0x%016llX  RSP=0x%016llX\n"
                    "R8 =0x%016llX  R9 =0x%016llX  R10=0x%016llX  R11=0x%016llX\n"
                    "R12=0x%016llX  R13=0x%016llX  R14=0x%016llX  R15=0x%016llX\n"
                    "RIP=0x%016llX  EFLAGS=0x%08lX\n",
                    walkContext.Rax, walkContext.Rbx, walkContext.Rcx, walkContext.Rdx,
                    walkContext.Rsi, walkContext.Rdi, walkContext.Rbp, walkContext.Rsp,
                    walkContext.R8,  walkContext.R9,  walkContext.R10, walkContext.R11,
                    walkContext.R12, walkContext.R13, walkContext.R14, walkContext.R15,
                    walkContext.Rip, static_cast<unsigned long>(walkContext.EFlags));
                report.append(line);
            }
#endif

            // 3. Resolve Call Stack via DbgHelp + PDB
            AppendStr(report, "\n--- NATIVE CALL STACK (DbgHelp + PDB) ---\n");
            HANDLE hProcess = GetCurrentProcess();
            HANDLE hWalkThread = (ctx.FaultingThreadHandle != nullptr) ? ctx.FaultingThreadHandle : GetCurrentThread();

            SymSetOptions(
                SYMOPT_LOAD_LINES |
                SYMOPT_UNDNAME |
                SYMOPT_DEFERRED_LOADS |
                SYMOPT_FAIL_CRITICAL_ERRORS);

            const std::string symPath = BuildSymbolSearchPath();
            const BOOL symReady = SymInitialize(hProcess, symPath.empty() ? nullptr : symPath.c_str(), TRUE);

            if (hasContext)
            {
                STACKFRAME64 frame{};
                DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#if defined(_M_X64) || defined(__x86_64__)
                frame.AddrPC.Offset = walkContext.Rip;
                frame.AddrPC.Mode = AddrModeFlat;
                frame.AddrFrame.Offset = walkContext.Rbp;
                frame.AddrFrame.Mode = AddrModeFlat;
                frame.AddrStack.Offset = walkContext.Rsp;
                frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
                machineType = IMAGE_FILE_MACHINE_ARM64;
                frame.AddrPC.Offset = walkContext.Pc;
                frame.AddrPC.Mode = AddrModeFlat;
                frame.AddrFrame.Offset = walkContext.Fp;
                frame.AddrFrame.Mode = AddrModeFlat;
                frame.AddrStack.Offset = walkContext.Sp;
                frame.AddrStack.Mode = AddrModeFlat;
#endif

                std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)> symStorage{};
                auto* symInfo = reinterpret_cast<SYMBOL_INFO*>(symStorage.data());

                for (int frameIdx = 0; frameIdx < 64; ++frameIdx)
                {
                    const BOOL walked = StackWalk64(
                        machineType,
                        hProcess,
                        hWalkThread,
                        &frame,
                        &walkContext,
                        nullptr,
                        SymFunctionTableAccess64,
                        SymGetModuleBase64,
                        nullptr);

                    if (!walked || frame.AddrPC.Offset == 0)
                    {
                        break;
                    }

                    const DWORD64 addr = frame.AddrPC.Offset;

                    const char* moduleName = "<unknown>";
                    IMAGEHLP_MODULE64 modInfo{};
                    modInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
                    if (symReady && SymGetModuleInfo64(hProcess, addr, &modInfo))
                    {
                        moduleName = modInfo.ModuleName;
                    }

                    std::memset(symStorage.data(), 0, symStorage.size());
                    symInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symInfo->MaxNameLen = MAX_SYM_NAME;

                    DWORD64 symDisplacement = 0;
                    const char* funcName = "<unresolved>";
                    if (symReady && SymFromAddr(hProcess, addr, &symDisplacement, symInfo))
                    {
                        funcName = symInfo->Name;
                    }

                    IMAGEHLP_LINE64 lineInfo{};
                    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                    DWORD lineDisplacement = 0;
                    if (symReady && SymGetLineFromAddr64(hProcess, addr, &lineDisplacement, &lineInfo))
                    {
                        std::snprintf(
                            line,
                            sizeof(line),
                            "  [%02d] 0x%016llX %s!%s + 0x%llX (%s:%lu)\n",
                            frameIdx,
                            static_cast<unsigned long long>(addr),
                            moduleName,
                            funcName,
                            static_cast<unsigned long long>(symDisplacement),
                            lineInfo.FileName ? lineInfo.FileName : "?",
                            static_cast<unsigned long>(lineInfo.LineNumber));
                    }
                    else
                    {
                        std::snprintf(
                            line,
                            sizeof(line),
                            "  [%02d] 0x%016llX %s!%s + 0x%llX\n",
                            frameIdx,
                            static_cast<unsigned long long>(addr),
                            moduleName,
                            funcName,
                            static_cast<unsigned long long>(symDisplacement));
                    }
                    report.append(line);
                }
            }

            if (symReady)
            {
                SymCleanup(hProcess);
            }

            std::snprintf(
                line,
                sizeof(line),
                "\nMiniDump File  : %s (%s)\n"
                "Crash Log File : %s\n"
                "================================================================================\n",
                dumpPath.string().c_str(),
                dumpWritten ? "WRITTEN" : "FAILED",
                latestLogPath.string().c_str());
            report.append(line);

            // 4. Write unbuffered crash log immediately to disk + print red to console
            WriteUnbufferedFile(latestLogPath, report);
            PrintRedToConsole(report);
        }

        DWORD WINAPI CrashWorkerThreadProc(LPVOID param) noexcept
        {
            const auto* ctx = static_cast<const CrashCaptureContext*>(param);
            if (ctx != nullptr)
            {
                CaptureAndEmitCrashReport(*ctx);
            }
            return 0;
        }

        void ExecuteCrashHandlingSafely(EXCEPTION_POINTERS* exceptionPtrs, const char* customHeader) noexcept
        {
            auto& state = GetCrashState();
            bool expected = false;
            if (!state.HandlingCrash.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return;
            }

            const DWORD faultingTid = GetCurrentThreadId();
            HANDLE hFaultingThread = OpenThread(THREAD_ALL_ACCESS, FALSE, faultingTid);

            CrashCaptureContext ctx{};
            ctx.ExceptionPointers = exceptionPtrs;
            ctx.FaultingThreadId = faultingTid;
            ctx.FaultingThreadHandle = hFaultingThread;
            ctx.CustomHeader = customHeader;

            // Execute dump & DbgHelp stack walk on a dedicated worker thread with a fresh 1 MB stack
            // so even EXCEPTION_STACK_OVERFLOW captures a clean minidump and PDB line numbers.
            HANDLE hWorker = CreateThread(
                nullptr,
                1024 * 1024,
                CrashWorkerThreadProc,
                &ctx,
                0,
                nullptr);

            if (hWorker != nullptr)
            {
                WaitForSingleObject(hWorker, 30000);
                CloseHandle(hWorker);
            }
            else
            {
                CaptureAndEmitCrashReport(ctx);
            }

            if (hFaultingThread != nullptr)
            {
                CloseHandle(hFaultingThread);
            }
        }

        LONG WINAPI VectoredExceptionCallback(EXCEPTION_POINTERS* exceptionInfo) noexcept
        {
            if (exceptionInfo == nullptr || exceptionInfo->ExceptionRecord == nullptr)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
            if (IsBenignFirstChanceException(code))
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            // Stack overflow and heap/stack-cookie corruption may bypass UnhandledExceptionFilter
            // or fail to unwind cleanly; capture immediately on a dedicated crash thread.
            if (code == EXCEPTION_STACK_OVERFLOW ||
                code == kExceptionHeapCorruption ||
                code == kExceptionStackBufOverrun)
            {
                ExecuteCrashHandlingSafely(exceptionInfo, "VectoredExceptionHandler (Fatal Non-Recoverable)");
                TerminateProcess(GetCurrentProcess(), code);
            }

            return EXCEPTION_CONTINUE_SEARCH;
        }

        LONG WINAPI UnhandledExceptionCallback(EXCEPTION_POINTERS* exceptionInfo) noexcept
        {
            ExecuteCrashHandlingSafely(exceptionInfo, "UnhandledExceptionFilter");
            return EXCEPTION_EXECUTE_HANDLER;
        }

        void TerminateHandlerCallback() noexcept
        {
            ExecuteCrashHandlingSafely(nullptr, "std::terminate (Uncaught C++ exception or abort)");
            TerminateProcess(GetCurrentProcess(), 0xE06D7363u);
        }

        void PureCallHandlerCallback() noexcept
        {
            ExecuteCrashHandlingSafely(nullptr, "_purecall_handler (Pure virtual function call)");
            TerminateProcess(GetCurrentProcess(), STATUS_NONCONTINUABLE_EXCEPTION);
        }

        struct CrashHandlerAutoBootstrap
        {
            CrashHandlerAutoBootstrap() noexcept
            {
                CrashHandler::Install();
            }
        };

        static CrashHandlerAutoBootstrap s_autoCrashBootstrap;
    }
#endif

    void CrashHandler::Install() noexcept
    {
#if defined(_WIN32)
        auto& state = GetCrashState();
        bool expected = false;
        if (!state.Installed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            ReassertFilter();
            return;
        }

        // Reserve guaranteed stack space so EXCEPTION_STACK_OVERFLOW can invoke handlers cleanly.
        ULONG stackGuaranteeBytes = 32768;
        SetThreadStackGuarantee(&stackGuaranteeBytes);

        // Install VEH at the end of the chain (First=0) so CoreCLR/WebView2 internal JIT handlers run first.
        state.VehHandle = AddVectoredExceptionHandler(0, VectoredExceptionCallback);
        state.PreviousFilter = SetUnhandledExceptionFilter(UnhandledExceptionCallback);
        state.PreviousTerminate = std::set_terminate(TerminateHandlerCallback);
        state.PreviousPureCall = _set_purecall_handler(PureCallHandlerCallback);

        MORBI_TRACE("CrashHandler", "Installed always-active Native SEH CrashHandler (VEH + UnhandledExceptionFilter + DbgHelp)");
#endif
    }

    void CrashHandler::ReassertFilter() noexcept
    {
#if defined(_WIN32)
        auto& state = GetCrashState();
        if (state.Installed.load(std::memory_order_acquire))
        {
            SetUnhandledExceptionFilter(UnhandledExceptionCallback);
        }
#endif
    }

    void CrashHandler::ReportManagedException(
        const char* exceptionType,
        const char* message,
        const char* stackTrace) noexcept
    {
        const char* safeType = (exceptionType != nullptr && exceptionType[0] != '\0') ? exceptionType : "System.Exception";
        const char* safeMsg = (message != nullptr) ? message : "";
        const char* safeStack = (stackTrace != nullptr) ? stackTrace : "<no managed stack trace>";

        std::string combined;
        combined.reserve(2048);
        combined.append(safeType);
        combined.append(": ");
        combined.append(safeMsg);
        combined.append("\n--- Managed C# StackTrace ---\n");
        combined.append(safeStack);

        Log::Fatal("C#.Exception", combined);
        Log::Flush();

#if defined(_WIN32)
        std::error_code ec;
        const std::filesystem::path crashDir = std::filesystem::current_path(ec) / "Logs" / "Crashes";
        const std::filesystem::path latestLogPath = crashDir / "Crash_Latest.log";

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char header[256]{};
        std::snprintf(
            header,
            sizeof(header),
            "=== UNHANDLED C# (.NET CoreCLR) EXCEPTION ===\nTimestamp: %04u-%02u-%02u %02u:%02u:%02u.%03u\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        std::string report;
        report.append(header);
        report.append("\n--- LAST THREAD BREADCRUMBS ---\n");
        report.append(BreadcrumbTracker::FormatSummary());
        report.append("\n--- C# EXCEPTION & STACKTRACE ---\n");
        report.append(combined);
        report.push_back('\n');

        WriteUnbufferedFile(latestLogPath, report);
#endif
    }

    void CrashHandler::Uninstall() noexcept
    {
#if defined(_WIN32)
        auto& state = GetCrashState();
        if (!state.Installed.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }
        if (state.VehHandle != nullptr)
        {
            RemoveVectoredExceptionHandler(state.VehHandle);
            state.VehHandle = nullptr;
        }
        SetUnhandledExceptionFilter(state.PreviousFilter);
        if (state.PreviousTerminate != nullptr)
        {
            std::set_terminate(state.PreviousTerminate);
        }
        if (state.PreviousPureCall != nullptr)
        {
            _set_purecall_handler(state.PreviousPureCall);
        }
#endif
    }
}
