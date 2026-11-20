#pragma once

#include <filesystem>
#include <mutex>
#include <unordered_map>

#include "Morbi/Shader/ShaderCompiler.hpp"

namespace Morbi::Shader
{
    struct ShaderCacheStats
    {
        u32 MemoryHits = 0;
        u32 DiskHits = 0;
        u32 Compilations = 0;
        u32 Invalidations = 0;
        u32 CorruptRecoveries = 0;
    };

    class MORBI_API ShaderCache
    {
    public:
        ShaderCache(std::filesystem::path cacheDirectory, ShaderCompiler compiler);
        ShaderCache(const ShaderCache&) = delete;
        ShaderCache& operator=(const ShaderCache&) = delete;

        [[nodiscard]] Core::Result<CompiledShaderArtifact, std::string> GetOrCompile(const ShaderCompileRequest& request);
        bool Invalidate(const ShaderCompileRequest& request);
        void ClearMemoryCache();
        void ClearAll();

        [[nodiscard]] ShaderCacheStats GetStats() const noexcept;
        [[nodiscard]] const ShaderCompiler& GetCompiler() const noexcept { return m_compiler; }
        [[nodiscard]] const std::filesystem::path& GetCacheDirectory() const noexcept { return m_cacheDirectory; }
        [[nodiscard]] std::filesystem::path GetCacheFilePathForRequest(const ShaderCompileRequest& request) const;

    private:
        enum class DiskLoadStatus : u8
        {
            Miss = 0,
            Hit,
            Corrupt,
            StaleDependency
        };

        [[nodiscard]] std::filesystem::path GetCacheFilePath(u64 baseHash, ShaderTargetFormat format, ShaderStage stage) const;
        [[nodiscard]] bool AreDependenciesValid(const CompiledShaderArtifact& artifact) const noexcept;
        [[nodiscard]] DiskLoadStatus TryLoadFromDisk(
            const std::filesystem::path& path,
            u64 expectedBaseHash,
            CompiledShaderArtifact& outArtifact) const;
        bool SaveToDisk(
            const std::filesystem::path& path,
            u64 baseHash,
            const CompiledShaderArtifact& artifact) const;

        std::filesystem::path m_cacheDirectory;
        ShaderCompiler m_compiler;
        mutable std::mutex m_mutex;
        mutable std::mutex m_diskMutex;
        std::unordered_map<u64, CompiledShaderArtifact> m_memoryCache;
        ShaderCacheStats m_stats{};
    };
}
