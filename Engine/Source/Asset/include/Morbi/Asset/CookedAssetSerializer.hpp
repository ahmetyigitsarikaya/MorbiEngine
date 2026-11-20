#pragma once

#include <filesystem>
#include <string>

#include "Morbi/Asset/AssetRegistry.hpp"
#include "Morbi/Asset/ModelImporter.hpp"
#include "Morbi/Asset/TextureImporter.hpp"
#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/IDevice.hpp"

namespace Morbi::Asset
{
    inline constexpr u32 kCookedAssetMagic = 0x4D415354u; // 'MAST' (Morbi Cooked Asset)
    inline constexpr u32 kCookedAssetVersion = 2u;

#pragma pack(push, 1)
    struct CookedAssetHeader
    {
        u32 Magic = kCookedAssetMagic;
        u32 Version = kCookedAssetVersion;
        u64 AssetUUIDValue = 0;
        u8  Type = static_cast<u8>(AssetType::Unknown);
        u8  Reserved[7]{};
        u64 PayloadSizeBytes = 0;
    };
    static_assert(sizeof(CookedAssetHeader) == 32, "CookedAssetHeader must be 32 bytes.");
#pragma pack(pop)

    struct GPUMeshBuffers
    {
        RHI::BufferHandle VertexBuffer = RHI::BufferHandle::Invalid();
        RHI::BufferHandle IndexBuffer  = RHI::BufferHandle::Invalid();
        RHI::BindlessResourceIndex VertexBindlessSlot{};
        RHI::BindlessResourceIndex IndexBindlessSlot{};
        u32 VertexCount = 0;
        u32 IndexCount  = 0;
        std::vector<SubmeshDescriptor> Submeshes;
        std::vector<MaterialInstance> MaterialInstances;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return VertexBuffer.IsValid() && IndexBuffer.IsValid() && IndexCount > 0;
        }
    };

    // ============================================================================
    // Cooked Binary Asset Serializer (.mAsset) & GPU Buffer Uploader
    // ============================================================================
    class MORBI_API CookedAssetSerializer
    {
    public:
        static bool CookStaticMeshToFile(
            const ImportedMeshData& mesh,
            AssetUUID uuid,
            const std::filesystem::path& outputPath);

        static bool LoadCookedStaticMesh(
            const std::filesystem::path& inputPath,
            ImportedMeshData& outMesh,
            AssetUUID& outUuid);

        static bool CookTextureToFile(
            const ImportedTextureData& texture,
            AssetUUID uuid,
            const std::filesystem::path& outputPath);

        static bool LoadCookedTexture(
            const std::filesystem::path& inputPath,
            ImportedTextureData& outTexture,
            AssetUUID& outUuid);

        [[nodiscard]] static GPUMeshBuffers UploadMeshToGPU(
            RHI::IDevice& rhiDevice,
            const ImportedMeshData& mesh);

        static void DestroyGPUMeshBuffers(
            RHI::IDevice& rhiDevice,
            GPUMeshBuffers& buffers);
    };
}
