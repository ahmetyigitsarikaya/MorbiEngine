// ============================================================================
// MorbiEngine - Thin Project Host / Bootstrapper (MorbiSandbox.exe)
// ============================================================================
// Zero static or import-lib dependency on MorbiEngine C++ modules.
// Dynamically resolves the central Engine DLL directory, parses game.morbiproj,
// loads Morbi.Runtime.dll (and dependent Morbi.*.dll modules) via LoadLibraryW,
// and invokes Morbi_Runtime_LaunchProject via GetProcAddress.
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
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--project" && i + 1 < argc)
            {
                const std::filesystem::path cliPath(argv[++i]);
                if (std::filesystem::exists(cliPath))
                {
                    return std::filesystem::weakly_canonical(cliPath);
                }
            }
        }

        // 1. Check alongside the Thin Host executable (e.g. Samples/Sandbox/game.morbiproj)
        if (std::filesystem::exists(exeDir / "game.morbiproj"))
        {
            return std::filesystem::weakly_canonical(exeDir / "game.morbiproj");
        }

        // 2. Check current working directory
        const auto cwd = std::filesystem::current_path();
        if (std::filesystem::exists(cwd / "game.morbiproj"))
        {
            return std::filesystem::weakly_canonical(cwd / "game.morbiproj");
        }

        // 3. Walk up from exeDir or cwd to locate Samples/Sandbox/game.morbiproj
        for (std::filesystem::path probe = exeDir; !probe.empty() && probe != probe.root_path(); probe = probe.parent_path())
        {
            if (std::filesystem::exists(probe / "Samples" / "Sandbox" / "game.morbiproj"))
            {
                return std::filesystem::weakly_canonical(probe / "Samples" / "Sandbox" / "game.morbiproj");
            }
        }

        return {};
    }

    std::filesystem::path LocateEngineBinDirectory(const std::filesystem::path& exeDir, const std::filesystem::path& projectFile)
    {
        // 1. Check if Morbi.Runtime.dll is in the same folder as the Thin Host
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
}

int main(int argc, char** argv)
{
    const std::filesystem::path exeDir = GetExecutableDirectory(argc > 0 ? argv[0] : nullptr);
    const std::filesystem::path projectFile = LocateProjectFile(exeDir, argc, argv);
    const std::filesystem::path engineBinDir = LocateEngineBinDirectory(exeDir, projectFile);

    std::string projectName = "MorbiSandbox";
    std::filesystem::path assetsDir;
    std::filesystem::path scriptsDir;

    if (!projectFile.empty() && std::filesystem::exists(projectFile))
    {
        std::ifstream in(projectFile);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string json = ss.str();

        projectName = ReadJsonField(json, "project_name", "MorbiSandbox");
        const std::string assetsRel = ReadJsonField(json, "assets_dir", "Assets");
        const std::string scriptsRel = ReadJsonField(json, "scripts_dir", "Scripts");
        assetsDir = projectFile.parent_path() / assetsRel;
        scriptsDir = projectFile.parent_path() / scriptsRel;
    }

    std::cout << "[ThinHost] Project='" << projectName
              << "' | Config='" << projectFile.string()
              << "' | EngineBin='" << engineBinDir.string() << "'" << std::endl;

#if defined(_WIN32)
    // Configure Win32 DLL search path so Morbi.Runtime.dll and all dependent Morbi.*.dll,
    // SDL3.dll, and nethost.dll resolve directly from the central engine bin folder.
    SetDllDirectoryW(engineBinDir.wstring().c_str());

    const std::filesystem::path runtimeDllPath = engineBinDir / "Morbi.Runtime.dll";
    HMODULE runtimeModule = LoadLibraryExW(
        runtimeDllPath.wstring().c_str(),
        nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);

    if (runtimeModule == nullptr)
    {
        const DWORD err = GetLastError();
        std::cerr << "[ThinHost] FATAL: Failed to LoadLibraryW('" << runtimeDllPath.string()
                  << "'), Win32 Error=" << err << std::endl;
        return 1;
    }

    const auto getVersionFn = reinterpret_cast<MorbiRuntimeGetVersionFn>(
        GetProcAddress(runtimeModule, "Morbi_Runtime_GetEngineVersion"));
    const auto launchProjectFn = reinterpret_cast<MorbiRuntimeLaunchProjectFn>(
        GetProcAddress(runtimeModule, "Morbi_Runtime_LaunchProject"));

    if (launchProjectFn == nullptr)
    {
        std::cerr << "[ThinHost] FATAL: GetProcAddress('Morbi_Runtime_LaunchProject') failed!" << std::endl;
        FreeLibrary(runtimeModule);
        return 1;
    }

    const char* engineVer = (getVersionFn != nullptr) ? getVersionFn() : "unknown";
    std::cout << "[ThinHost] Dynamically loaded Morbi.Runtime.dll (Version: " << engineVer << ")" << std::endl;

    const std::string projectFileStr = projectFile.string();
    const std::string engineBinStr = engineBinDir.string();

    MorbiHostLaunchArgs launchArgs{
        .Argc = argc,
        .Argv = argv,
        .ProjectFilePath = projectFileStr.c_str(),
        .EngineBinDir = engineBinStr.c_str()
    };

    const int exitCode = launchProjectFn(&launchArgs);

    FreeLibrary(runtimeModule);
    return exitCode;
#else
    std::cerr << "[ThinHost] Unsupported host OS." << std::endl;
    return 1;
#endif
}
