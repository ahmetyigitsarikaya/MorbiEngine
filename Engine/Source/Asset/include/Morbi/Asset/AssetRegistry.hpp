#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/StringId.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Asset
{
    // ============================================================================
    // 64-Bit Deterministic AssetUUID (Backed by FNV-1a StringId or Explicit 64-bit ID)
    // ============================================================================
    struct AssetUUID
    {
        u64 Value = 0;

        constexpr AssetUUID() noexcept = default;
        constexpr explicit AssetUUID(u64 val) noexcept : Value(val) {}
        constexpr explicit AssetUUID(Core::StringId sid) noexcept : Value(sid.Value()) {}

        [[nodiscard]] static AssetUUID FromVirtualPath(std::string_view virtualPath) noexcept
        {
            return AssetUUID(Core::StringId::FromString(virtualPath));
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] constexpr bool operator==(const AssetUUID& other) const noexcept = default;
    };

    enum class AssetType : u8
    {
        Unknown    = 0,
        StaticMesh = 1,
        Texture2D  = 2,
        Material   = 3,
        AudioClip  = 4,
        Scene      = 5,
        Script     = 6
    };

    struct AssetMetadata
    {
        AssetUUID UUID{};
        AssetType Type = AssetType::Unknown;
        std::string VirtualPath;
        std::filesystem::path SourcePhysicalPath;
        std::filesystem::path CookedPhysicalPath;
        u64 ContentHash = 0;
    };

    class MORBI_API AssetRegistry
    {
    public:
        AssetRegistry() = default;
        ~AssetRegistry() = default;

        AssetRegistry(const AssetRegistry&) = delete;
        AssetRegistry& operator=(const AssetRegistry&) = delete;

        AssetUUID RegisterAsset(
            std::string_view virtualPath,
            const std::filesystem::path& sourcePhysicalPath,
            AssetType type,
            const std::filesystem::path& cookedPhysicalPath = {},
            u64 contentHash = 0);

        [[nodiscard]] std::optional<AssetMetadata> FindByUUID(AssetUUID uuid) const;
        [[nodiscard]] std::optional<AssetMetadata> FindByVirtualPath(std::string_view virtualPath) const;
        [[nodiscard]] std::vector<AssetMetadata> GetAllAssets() const;
        [[nodiscard]] usize GetRegisteredCount() const noexcept;

        // Scans a physical Assets/ directory and registers discovered .obj, .gltf, .glb, .fbx, .png, .jpg, .tga, .mscene.json files
        u32 ScanDirectory(const std::filesystem::path& physicalRoot, std::string_view virtualPrefix = "assets://");

        bool SaveToJsonFile(const std::filesystem::path& filePath) const;
        void Clear();

        [[nodiscard]] static const char* AssetTypeToString(AssetType type) noexcept;

    private:
        mutable std::mutex m_mutex;
        std::unordered_map<u64, AssetMetadata> m_byUuid;
        std::unordered_map<std::string, u64> m_pathToUuid;
    };
}
