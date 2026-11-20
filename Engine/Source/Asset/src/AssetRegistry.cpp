#include "Morbi/Asset/AssetRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "Morbi/Core/Log.hpp"

namespace Morbi::Asset
{
    namespace
    {
        std::string ToLowerExt(std::string ext)
        {
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext;
        }

        AssetType DeduceAssetTypeFromExtension(const std::filesystem::path& path)
        {
            const std::string ext = ToLowerExt(path.extension().string());
            if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx")
            {
                return AssetType::StaticMesh;
            }
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp")
            {
                return AssetType::Texture2D;
            }
            if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
            {
                return AssetType::AudioClip;
            }
            if (ext == ".cs")
            {
                return AssetType::Script;
            }
            if (ext == ".json")
            {
                return AssetType::Scene;
            }
            return AssetType::Unknown;
        }
    }

    AssetUUID AssetRegistry::RegisterAsset(
        std::string_view virtualPath,
        const std::filesystem::path& sourcePhysicalPath,
        AssetType type,
        const std::filesystem::path& cookedPhysicalPath,
        u64 contentHash)
    {
        const AssetUUID uuid = AssetUUID::FromVirtualPath(virtualPath);

        AssetMetadata meta{};
        meta.UUID = uuid;
        meta.Type = type;
        meta.VirtualPath = std::string(virtualPath);
        meta.SourcePhysicalPath = sourcePhysicalPath;
        meta.CookedPhysicalPath = cookedPhysicalPath;
        meta.ContentHash = contentHash != 0 ? contentHash : uuid.Value;

        std::lock_guard lock(m_mutex);
        m_byUuid[uuid.Value] = meta;
        m_pathToUuid[meta.VirtualPath] = uuid.Value;
        return uuid;
    }

    std::optional<AssetMetadata> AssetRegistry::FindByUUID(AssetUUID uuid) const
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_byUuid.find(uuid.Value);
        if (it == m_byUuid.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<AssetMetadata> AssetRegistry::FindByVirtualPath(std::string_view virtualPath) const
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_pathToUuid.find(std::string(virtualPath));
        if (it == m_pathToUuid.end())
        {
            return std::nullopt;
        }
        const auto metaIt = m_byUuid.find(it->second);
        if (metaIt == m_byUuid.end())
        {
            return std::nullopt;
        }
        return metaIt->second;
    }

    std::vector<AssetMetadata> AssetRegistry::GetAllAssets() const
    {
        std::lock_guard lock(m_mutex);
        std::vector<AssetMetadata> list;
        list.reserve(m_byUuid.size());
        for (const auto& [_, meta] : m_byUuid)
        {
            list.push_back(meta);
        }
        return list;
    }

    usize AssetRegistry::GetRegisteredCount() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_byUuid.size();
    }

    u32 AssetRegistry::ScanDirectory(const std::filesystem::path& physicalRoot, std::string_view virtualPrefix)
    {
        std::error_code ec;
        if (!std::filesystem::exists(physicalRoot, ec))
        {
            return 0;
        }

        u32 count = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(physicalRoot, ec))
        {
            if (!entry.is_regular_file(ec))
            {
                continue;
            }

            const AssetType type = DeduceAssetTypeFromExtension(entry.path());
            if (type == AssetType::Unknown)
            {
                continue;
            }

            const auto rel = std::filesystem::relative(entry.path(), physicalRoot, ec);
            const std::u8string u8Rel = rel.generic_u8string();
            const std::string relStr(reinterpret_cast<const char*>(u8Rel.data()), u8Rel.size());
            const std::string vpath = std::string(virtualPrefix) + relStr;
            RegisterAsset(vpath, entry.path(), type);
            ++count;
        }
        return count;
    }

    bool AssetRegistry::SaveToJsonFile(const std::filesystem::path& filePath) const
    {
        std::error_code ec;
        if (filePath.has_parent_path())
        {
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }

        std::ofstream out(filePath);
        if (!out.is_open())
        {
            return false;
        }

        const auto assets = GetAllAssets();
        out << "{\n  \"schema_version\": 1,\n  \"asset_count\": " << assets.size() << ",\n  \"assets\": [\n";
        for (usize i = 0; i < assets.size(); ++i)
        {
            const auto& a = assets[i];
            out << "    {\n"
                << "      \"uuid\": " << a.UUID.Value << ",\n"
                << "      \"type\": \"" << AssetTypeToString(a.Type) << "\",\n"
                << "      \"virtual_path\": \"" << a.VirtualPath << "\",\n"
                << "      \"source_path\": \"" << a.SourcePhysicalPath.generic_string() << "\",\n"
                << "      \"cooked_path\": \"" << a.CookedPhysicalPath.generic_string() << "\"\n"
                << "    }" << (i + 1 < assets.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        return true;
    }

    void AssetRegistry::Clear()
    {
        std::lock_guard lock(m_mutex);
        m_byUuid.clear();
        m_pathToUuid.clear();
    }

    const char* AssetRegistry::AssetTypeToString(AssetType type) noexcept
    {
        switch (type)
        {
        case AssetType::StaticMesh: return "StaticMesh";
        case AssetType::Texture2D:  return "Texture2D";
        case AssetType::Material:   return "Material";
        case AssetType::AudioClip:  return "AudioClip";
        case AssetType::Scene:      return "Scene";
        case AssetType::Script:     return "Script";
        default:                    return "Unknown";
        }
    }
}
