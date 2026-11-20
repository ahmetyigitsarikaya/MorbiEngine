#include "Morbi/IO/PathService.hpp"

#include <fstream>

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

namespace Morbi::IO
{
    void PathService::Mount(std::string scheme, std::filesystem::path physicalRoot)
    {
        MORBI_BREADCRUMB("VFS Mount");
        const auto normalized = NormalizePhysicalPath(physicalRoot);
        const std::u8string u8Norm = normalized.u8string();
        const std::string utf8Norm(reinterpret_cast<const char*>(u8Norm.data()), u8Norm.size());
        MORBI_TRACE("VFS", "Mount '" + scheme + "://' -> '" + utf8Norm + "'");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mountPoints[std::move(scheme)] = normalized;
    }

    bool PathService::IsMounted(std::string_view scheme) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mountPoints.find(std::string(scheme)) != m_mountPoints.end();
    }

    std::optional<std::filesystem::path> PathService::Resolve(std::string_view virtualPath) const
    {
        const auto split = SplitVirtualPath(virtualPath);
        if (!split.has_value())
        {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_mountPoints.find(split->first);
        if (it == m_mountPoints.end())
        {
            return std::nullopt;
        }

        if (split->second.empty() || split->second == ".")
        {
            return it->second;
        }

        const std::filesystem::path subPath(std::u8string_view(
            reinterpret_cast<const char8_t*>(split->second.data()),
            split->second.size()));
        return NormalizePhysicalPath(it->second / subPath);
    }

    std::filesystem::path PathService::NormalizePhysicalPath(const std::filesystem::path& path) const
    {
        std::error_code ec;
        const auto absPath = std::filesystem::absolute(path, ec);
        if (ec)
        {
            return path.lexically_normal();
        }
        return absPath.lexically_normal();
    }

    std::optional<std::pair<std::string, std::string>> PathService::SplitVirtualPath(std::string_view value)
    {
        constexpr std::string_view kSeparator = "://";
        const auto pos = value.find(kSeparator);
        if (pos == std::string_view::npos || pos == 0)
        {
            return std::nullopt;
        }

        std::string scheme(value.substr(0, pos));
        std::string relative(value.substr(pos + kSeparator.size()));
        return std::make_pair(std::move(scheme), std::move(relative));
    }

    bool PathService::MountPakIndex(const std::filesystem::path& indexJsonPath, std::vector<u8> encryptionKey, std::string& outError)
    {
        MORBI_BREADCRUMB("VFS Mount");
        MORBI_TRACE("VFS", "MountPakIndex: '" + indexJsonPath.string() + "'");
        Pak::PakChunkIndex parsed;
        if (!Pak::LoadChunkedPakIndex(indexJsonPath, parsed, outError))
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pakIndex = std::move(parsed);
        m_pakKey = std::move(encryptionKey);
        return true;
    }

    void PathService::UnmountPakIndex()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pakIndex.reset();
        m_pakKey.clear();
    }

    bool PathService::HasPakIndex() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pakIndex.has_value();
    }

    std::vector<std::string> PathService::GetMountedPakVirtualPaths() const
    {
        std::vector<std::string> paths;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pakIndex.has_value())
        {
            paths.reserve(m_pakIndex->VirtualPathIndex.size());
            for (const auto& [vpath, loc] : m_pakIndex->VirtualPathIndex)
            {
                (void)loc;
                paths.push_back(vpath);
            }
        }
        return paths;
    }

    bool PathService::Exists(std::string_view virtualPath) const
    {
        if (const auto physical = Resolve(virtualPath))
        {
            std::error_code ec;
            if (std::filesystem::exists(*physical, ec) && !ec)
            {
                return true;
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pakIndex.has_value())
        {
            return m_pakIndex->VirtualPathIndex.find(std::string(virtualPath)) != m_pakIndex->VirtualPathIndex.end();
        }
        return false;
    }

    bool PathService::ReadVirtualFile(std::string_view virtualPath, std::vector<u8>& outBytes, std::string& outError) const
    {
        if (const auto physical = Resolve(virtualPath))
        {
            std::error_code ec;
            if (std::filesystem::exists(*physical, ec) && !ec)
            {
                std::ifstream in(*physical, std::ios::binary | std::ios::ate);
                if (!in)
                {
                    outError = "Failed to open physical file: " + physical->string();
                    return false;
                }
                const auto size = in.tellg();
                in.seekg(0, std::ios::beg);
                outBytes.resize(size > 0 ? static_cast<usize>(size) : 0);
                if (size > 0)
                {
                    in.read(reinterpret_cast<char*>(outBytes.data()), size);
                }
                return true;
            }
        }

        std::optional<Pak::PakChunkIndex> indexCopy;
        std::vector<u8> keyCopy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_pakIndex.has_value())
            {
                outError = "Virtual file not found and no MorbiPak chunk index mounted: " + std::string(virtualPath);
                return false;
            }
            indexCopy = m_pakIndex;
            keyCopy = m_pakKey;
        }

        return Pak::ReadChunkedPakFile(*indexCopy, virtualPath, keyCopy, outBytes, outError);
    }

    std::optional<std::filesystem::path> PathService::ResolveOrStreamToCache(std::string_view virtualPath) const
    {
        if (const auto physical = Resolve(virtualPath))
        {
            std::error_code ec;
            if (std::filesystem::exists(*physical, ec) && !ec)
            {
                return *physical;
            }
        }

        std::vector<u8> bytes;
        std::string err;
        if (!ReadVirtualFile(virtualPath, bytes, err))
        {
            return std::nullopt;
        }

        const auto cacheBase = Resolve("cache://vfs_stream");
        if (!cacheBase.has_value())
        {
            return std::nullopt;
        }

        const auto split = SplitVirtualPath(virtualPath);
        const std::filesystem::path targetPath = split.has_value()
            ? (*cacheBase / split->first / split->second)
            : (*cacheBase / std::filesystem::path(virtualPath).filename());

        std::error_code ec;
        std::filesystem::create_directories(targetPath.parent_path(), ec);
        std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return std::nullopt;
        }
        if (!bytes.empty())
        {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        return targetPath;
    }
}
