// ============================================================================
// MorbiEngine - Open C++ Game Launcher (Compiled Per Project Build)
// ============================================================================
#include <filesystem>
#include <iostream>
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
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    const std::filesystem::path exeDir = GetExecutableDirectory(argc > 0 ? argv[0] : nullptr);
    const std::filesystem::path projectFile = exeDir / "game.morbiproj";
    const std::filesystem::path engineBinDir = exeDir;

#if defined(_WIN32)
    SetDllDirectoryW(engineBinDir.wstring().c_str());

    const std::filesystem::path runtimeDllPath = engineBinDir / "Morbi.Runtime.dll";
    HMODULE runtimeModule = LoadLibraryExW(
        runtimeDllPath.wstring().c_str(),
        nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);

    if (runtimeModule == nullptr)
    {
        std::cerr << "[MorbiGame] FATAL: Could not load Morbi.Runtime.dll from '"
                  << runtimeDllPath.string() << "' (Error=" << GetLastError() << ")" << std::endl;
        return 1;
    }

    const auto launchProjectFn = reinterpret_cast<MorbiRuntimeLaunchProjectFn>(
        GetProcAddress(runtimeModule, "Morbi_Runtime_LaunchProject"));
    if (launchProjectFn == nullptr)
    {
        std::cerr << "[MorbiGame] FATAL: GetProcAddress('Morbi_Runtime_LaunchProject') failed." << std::endl;
        FreeLibrary(runtimeModule);
        return 1;
    }

    std::vector<std::string> argStorage;
    argStorage.reserve(static_cast<size_t>(argc) + 1);
    for (int i = 0; i < argc; ++i)
    {
        if (argv[i] != nullptr)
        {
            // Strip --editor if accidentally passed to standalone game launcher
            if (std::string_view(argv[i]) != "--editor")
            {
                argStorage.emplace_back(argv[i]);
            }
        }
    }

    std::vector<char*> argPointers;
    argPointers.reserve(argStorage.size());
    for (auto& s : argStorage)
    {
        argPointers.push_back(s.data());
    }

    const std::string projectFileStr = std::filesystem::exists(projectFile) ? projectFile.string() : std::string{};
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
    std::cerr << "[MorbiGame] Unsupported platform." << std::endl;
    return 1;
#endif
}
