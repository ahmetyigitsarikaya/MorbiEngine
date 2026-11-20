#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/IO/MorbiPak.hpp"

namespace Morbi::IO
{
    class MORBI_API PathService
    {
    public:
        PathService() = default;
        PathService(const PathService&) = delete;
        PathService& operator=(const PathService&) = delete;

        void Mount(std::string scheme, std::filesystem::path physicalRoot);
        [[nodiscard]] bool IsMounted(std::string_view scheme) const;
        [[nodiscard]] std::optional<std::filesystem::path> Resolve(std::string_view virtualPath) const;
        [[nodiscard]] std::filesystem::path NormalizePhysicalPath(const std::filesystem::path& path) const;

        // Chunked MorbiPak VFS Integration
        bool MountPakIndex(const std::filesystem::path& indexJsonPath, std::vector<u8> encryptionKey, std::string& outError);
        void UnmountPakIndex();
        [[nodiscard]] bool HasPakIndex() const noexcept;
        [[nodiscard]] std::vector<std::string> GetMountedPakVirtualPaths() const;
        [[nodiscard]] bool Exists(std::string_view virtualPath) const;
        bool ReadVirtualFile(std::string_view virtualPath, std::vector<u8>& outBytes, std::string& outError) const;
        [[nodiscard]] std::optional<std::filesystem::path> ResolveOrStreamToCache(std::string_view virtualPath) const;

    private:
        [[nodiscard]] static std::optional<std::pair<std::string, std::string>> SplitVirtualPath(std::string_view value);

        mutable std::mutex m_mutex;
        std::unordered_map<std::string, std::filesystem::path> m_mountPoints;
        std::optional<Pak::PakChunkIndex> m_pakIndex;
        std::vector<u8> m_pakKey;
    };
}
