// ============================================================================
// MorbiEngine - Editor Thin Host / Bootstrapper (MorbiEditor.exe)
// ============================================================================
// Zero static or import-lib dependency on MorbiEngine C++ modules.
// Dynamically resolves the central Engine DLL directory, parses project file,
// loads Morbi.Runtime.dll via LoadLibraryW, and invokes Morbi_Runtime_LaunchProject
// with the --editor flag enabled.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
    #include <objbase.h>
#endif

extern "C"
{
    struct MorbiHostLaunchArgs
    {
        int Argc;
        char** Argv;
        const char* ProjectFilePath;
        const char* EngineBinDir;
    };

    using MorbiRuntimeGetVersionFn = const char* (__cdecl*)();
    using MorbiRuntimeLaunchProjectFn = int (__cdecl*)(const MorbiHostLaunchArgs* args);
}

namespace
{
    std::string ReadJsonField(std::string_view json, std::string_view key, std::string_view defaultVal)
    {
        const std::string pattern = "\"" + std::string(key) + "\"";
        const size_t pos = json.find(pattern);
        if (pos == std::string_view::npos)
        {
            return std::string(defaultVal);
        }
        const size_t colon = json.find(':', pos + pattern.size());
        if (colon == std::string_view::npos)
        {
            return std::string(defaultVal);
        }
        const size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string_view::npos)
        {
            return std::string(defaultVal);
        }
        const size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string_view::npos)
        {
            return std::string(defaultVal);
        }
        return std::string(json.substr(q1 + 1, q2 - q1 - 1));
    }

    std::filesystem::path GetExecutableDirectory(const char* argv0)
    {
#if defined(_WIN32)
        wchar_t buffer[MAX_PATH]{};
        const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
        {
            return std::filesystem::path(buffer).parent_path();
        }
#endif
        if (argv0 != nullptr)
        {
            std::error_code ec;
            const auto p = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
            if (!ec && p.has_parent_path())
            {
                return p.parent_path();
            }
        }
        return std::filesystem::current_path();
    }

    std::filesystem::path LocateProjectFile(const std::filesystem::path& exeDir, int argc, char** argv)
    {
        bool isSmokeTest = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if (arg == "--smoke-test")
            {
                isSmokeTest = true;
            }
            else if (arg == "--project" && i + 1 < argc)
            {
                const std::filesystem::path cliPath(argv[++i]);
                if (std::filesystem::exists(cliPath))
                {
                    return std::filesystem::weakly_canonical(cliPath);
                }
            }
        }

        // 1. Check current working directory
        const auto cwd = std::filesystem::current_path();
        if (std::filesystem::exists(cwd / "game.morbiproj"))
        {
            return std::filesystem::weakly_canonical(cwd / "game.morbiproj");
        }

        // 2. Check alongside the Thin Host executable
        if (std::filesystem::exists(exeDir / "game.morbiproj"))
        {
            return std::filesystem::weakly_canonical(exeDir / "game.morbiproj");
        }

        // 3. Walk up from exeDir or cwd in dev environment to locate Samples/Sandbox
        const std::vector<std::filesystem::path> roots = { exeDir, cwd };
        for (const auto& start : roots)
        {
            for (std::filesystem::path probe = start; !probe.empty() && probe != probe.root_path(); probe = probe.parent_path())
            {
                if (std::filesystem::exists(probe / "Samples" / "Sandbox" / "game.morbiproj"))
                {
                    return std::filesystem::weakly_canonical(probe / "Samples" / "Sandbox" / "game.morbiproj");
                }
            }
        }
        (void)isSmokeTest;

        return {};
    }

    std::filesystem::path LocateEngineBinDirectory(const std::filesystem::path& exeDir, const std::filesystem::path& projectFile)
    {
        // 1. Check if Morbi.Runtime.dll is in the same folder as MorbiEditor.exe
        if (std::filesystem::exists(exeDir / "Morbi.Runtime.dll"))
        {
            return exeDir;
        }

        // 2. Check environment variable MORBI_ENGINE_BIN
#if defined(_WIN32)
        wchar_t envBuf[MAX_PATH]{};
        const DWORD envLen = GetEnvironmentVariableW(L"MORBI_ENGINE_BIN", envBuf, MAX_PATH);
        if (envLen > 0 && envLen < MAX_PATH)
        {
            const std::filesystem::path envPath(envBuf);
            if (std::filesystem::exists(envPath / "Morbi.Runtime.dll"))
            {
                return envPath;
            }
        }
#endif

        // 3. Walk up from exeDir and projectFile to find build/bin/Release or build/bin/Debug
        const std::vector<std::filesystem::path> roots = {
            exeDir,
            projectFile.empty() ? std::filesystem::current_path() : projectFile.parent_path()
        };

        for (const auto& start : roots)
        {
            for (std::filesystem::path probe = start; !probe.empty() && probe != probe.root_path(); probe = probe.parent_path())
            {
                if (std::filesystem::exists(probe / "build" / "bin" / "Release" / "Morbi.Runtime.dll"))
                {
                    return probe / "build" / "bin" / "Release";
                }
                if (std::filesystem::exists(probe / "build" / "bin" / "Debug" / "Morbi.Runtime.dll"))
                {
                    return probe / "build" / "bin" / "Debug";
                }
                if (std::filesystem::exists(probe / "bin" / "Release" / "Morbi.Runtime.dll"))
                {
                    return probe / "bin" / "Release";
                }
            }
        }

        return exeDir;
    }

#if defined(_WIN32)
    LONG WINAPI EditorHostEarlyUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) noexcept
    {
        std::error_code ec;
        const std::filesystem::path crashDir = std::filesystem::current_path(ec) / "Logs" / "Crashes";
        std::filesystem::create_directories(crashDir, ec);

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char dmpName[96]{};
        std::snprintf(dmpName, sizeof(dmpName), "Crash_%04u%02u%02u_%02u%02u%02u.dmp",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        const std::filesystem::path dmpPath = crashDir / dmpName;

        HANDLE hDump = CreateFileW(dmpPath.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = exceptionInfo;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDump,
                MiniDumpWithDataSegs, exceptionInfo ? &mei : nullptr, nullptr, nullptr);
            FlushFileBuffers(hDump);
            CloseHandle(hDump);
        }

        const DWORD code = (exceptionInfo && exceptionInfo->ExceptionRecord)
            ? exceptionInfo->ExceptionRecord->ExceptionCode
            : 0u;
        std::cerr << "[EditorHost] FATAL early SEH exception 0x" << std::hex << code << std::dec
                  << " | Dump: " << dmpPath.string() << std::endl;
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    SetUnhandledExceptionFilter(EditorHostEarlyUnhandledExceptionFilter);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
    bool debugRequested = false;
    bool gpuGbvRequested = false;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr) continue;
        const std::string_view arg(argv[i]);
        if (arg == "--debug" || arg == "-d") debugRequested = true;
        else if (arg == "--gpu-gbv") gpuGbvRequested = true;
    }
    if (debugRequested)
    {
        std::cout << "[EditorHost] Diagnostics Mode Enabled (--debug"
                  << (gpuGbvRequested ? ", --gpu-gbv" : "") << ")" << std::endl;
    }
    const std::filesystem::path exeDir = GetExecutableDirectory(argc > 0 ? argv[0] : nullptr);
    const std::filesystem::path projectFile = LocateProjectFile(exeDir, argc, argv);
    const std::filesystem::path engineBinDir = LocateEngineBinDirectory(exeDir, projectFile);

    std::string projectName = "MorbiEditor";

    if (!projectFile.empty() && std::filesystem::exists(projectFile))
    {
        std::ifstream in(projectFile);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string json = ss.str();
        projectName = ReadJsonField(json, "project_name", "MorbiEditor");
    }

    std::cout << "[EditorHost] Project='" << projectName
              << "' | Config='" << projectFile.string()
              << "' | EngineBin='" << engineBinDir.string() << "'" << std::endl;

#if defined(_WIN32)
    // Configure Win32 DLL search path so Morbi.Runtime.dll and dependent Morbi.*.dll resolve cleanly
    SetDllDirectoryW(engineBinDir.wstring().c_str());

    const std::filesystem::path runtimeDllPath = engineBinDir / "Morbi.Runtime.dll";
    HMODULE runtimeModule = LoadLibraryExW(
        runtimeDllPath.wstring().c_str(),
        nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);

    if (runtimeModule == nullptr)
    {
        const DWORD err = GetLastError();
        std::cerr << "[EditorHost] FATAL: Failed to LoadLibraryW('" << runtimeDllPath.string()
                  << "'), Win32 Error=" << err << std::endl;
        return 1;
    }

    const auto getVersionFn = reinterpret_cast<MorbiRuntimeGetVersionFn>(
        GetProcAddress(runtimeModule, "Morbi_Runtime_GetEngineVersion"));
    const auto launchProjectFn = reinterpret_cast<MorbiRuntimeLaunchProjectFn>(
        GetProcAddress(runtimeModule, "Morbi_Runtime_LaunchProject"));

    if (launchProjectFn == nullptr)
    {
        std::cerr << "[EditorHost] FATAL: GetProcAddress('Morbi_Runtime_LaunchProject') failed!" << std::endl;
        FreeLibrary(runtimeModule);
        return 1;
    }

    const char* engineVer = (getVersionFn != nullptr) ? getVersionFn() : "unknown";
    std::cout << "[EditorHost] Dynamically loaded Morbi.Runtime.dll (Version: " << engineVer << ")" << std::endl;

    // Prepare argument vector ensuring --editor is present
    std::vector<std::string> argStorage;
    argStorage.reserve(static_cast<size_t>(argc) + 2);
    bool hasEditorFlag = false;

    for (int i = 0; i < argc; ++i)
    {
        if (argv[i] != nullptr)
        {
            argStorage.emplace_back(argv[i]);
            if (argStorage.back() == "--editor")
            {
                hasEditorFlag = true;
            }
        }
    }

    if (!hasEditorFlag)
    {
        argStorage.emplace_back("--editor");
    }

    std::vector<char*> argPointers;
    argPointers.reserve(argStorage.size());
    for (auto& s : argStorage)
    {
        argPointers.push_back(s.data());
    }

    const std::string projectFileStr = projectFile.string();
    const std::string engineBinStr = engineBinDir.string();

    MorbiHostLaunchArgs launchArgs{
        .Argc = static_cast<int>(argPointers.size()),
        .Argv = argPointers.data(),
        .ProjectFilePath = projectFileStr.c_str(),
        .EngineBinDir = engineBinStr.c_str()
    };

    const int exitCode = launchProjectFn(&launchArgs);

    FreeLibrary(runtimeModule);
    return exitCode;
#else
    std::cerr << "[EditorHost] Unsupported host OS." << std::endl;
    return 1;
#endif
}
