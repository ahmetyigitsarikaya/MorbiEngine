#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Morbi/Core/Export.hpp"

namespace Morbi::Script
{
    class ScriptEngine;
}

namespace Morbi::Runtime
{
    struct ProjectBuildRequest
    {
        std::string ProjectName = "MorbiGame";
        std::filesystem::path ProjectRootDir;
        std::filesystem::path AssetsDir;
        std::filesystem::path ScriptsDir;
        std::filesystem::path ProjectFilePath;
        std::filesystem::path EngineBinDir;
        std::filesystem::path OutputDir;
        std::uint64_t MaxChunkSizeBytes = 256ull * 1024ull * 1024ull; // 256 MB chunks
        bool EncryptPaks = true;
        bool ReleaseMode = true;
    };

    struct ProjectBuildResult
    {
        bool Success = false;
        std::filesystem::path OutputExePath;
        std::filesystem::path ChunkIndexPath;
        std::uint32_t CompiledScriptCount = 0;
        std::uint32_t ChunkCount = 0;
        std::uint64_t TotalPackedBytes = 0;
        std::string Summary;
        std::string Error;
    };

    class MORBI_API ProjectBuilder
    {
    public:
        [[nodiscard]] static ProjectBuildResult BuildGame(
            const ProjectBuildRequest& request,
            Script::ScriptEngine& scriptEngine);
    };
}
