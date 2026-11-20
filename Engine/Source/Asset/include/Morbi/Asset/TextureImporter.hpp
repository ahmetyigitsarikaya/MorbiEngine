#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/IDevice.hpp"

namespace Morbi::Asset
{
    struct ImportedTextureData
    {
        std::string Name = "UnnamedTexture";
        u32 Width = 0;
        u32 Height = 0;
        u32 Channels = 4; // Always converted to tightly-packed 4-channel RGBA8 for GPU RHI upload
        RHI::Format RhiFormat = RHI::Format::RGBA8_UNorm;
        std::vector<u8> PixelsRGBA8;
    };

    struct TextureImportOptions
    {
        bool sRGB = true;
        bool FlipVertical = false;
    };

    // ============================================================================
    // TextureImporter (.png, .jpg, .tga, .bmp -> CPU RGBA8 & Morbi.RHI Formats)
    // ============================================================================
    class MORBI_API TextureImporter
    {
    public:
        [[nodiscard]] static Core::Result<ImportedTextureData, std::string> ImportFromFile(
            const std::filesystem::path& filePath,
            const TextureImportOptions& options = {});

        [[nodiscard]] static Core::Result<ImportedTextureData, std::string> ImportFromMemory(
            std::span<const u8> encodedBytes,
            std::string_view debugName = "EmbeddedTexture",
            const TextureImportOptions& options = {});

        // Generates a valid uncompressed 32-bit RGBA Truevision .tga texture on disk
        static bool GenerateProceduralCheckerboardTGA(
            const std::filesystem::path& outputPath,
            u32 width = 64,
            u32 height = 64);

        // Uploads decoded CPU RGBA8 pixels into a Bindless GPU Texture via Morbi.RHI
        [[nodiscard]] static RHI::TextureHandle UploadToGPU(
            RHI::IDevice& rhiDevice,
            const ImportedTextureData& textureData);
    };
}
