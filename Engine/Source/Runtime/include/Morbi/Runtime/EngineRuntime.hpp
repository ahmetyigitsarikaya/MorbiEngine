#pragma once

#include <filesystem>
#include <string>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Runtime/RuntimeProfile.hpp"

// ============================================================================
// Pure C ABI Bootstrapper Struct & Function Signatures for Thin Project Host
// Dynamically resolved at runtime via LoadLibraryW / GetProcAddress
// ============================================================================
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

    MORBI_DLLEXPORT const char* __cdecl Morbi_Runtime_GetEngineVersion();
    MORBI_DLLEXPORT int __cdecl Morbi_Runtime_LaunchProject(const MorbiHostLaunchArgs* args);
}

namespace Morbi::Runtime
{
    class MORBI_API EngineRuntime
    {
    public:
        [[nodiscard]] static Core::Result<ProjectDescriptor, std::string> LoadProjectFile(
            const std::filesystem::path& morbiprojPath);

        [[nodiscard]] static int RunProject(const MorbiHostLaunchArgs& launchArgs);
    };
}
