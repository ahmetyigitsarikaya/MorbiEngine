#include "Morbi/Asset/TextureImporter.hpp"

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <wincodec.h>
    #include <wrl/client.h>
#endif

namespace Morbi::Asset
{
    namespace
    {
#pragma pack(push, 1)
        struct TGAHeader
        {
            u8  IdLength = 0;
            u8  ColorMapType = 0;
            u8  ImageType = 2; // Uncompressed True-Color
            u16 ColorMapOrigin = 0;
            u16 ColorMapLength = 0;
            u8  ColorMapDepth = 0;
            u16 XOrigin = 0;
            u16 YOrigin = 0;
            u16 Width = 0;
            u16 Height = 0;
            u8  BitsPerPixel = 32;
            u8  ImageDescriptor = 0x28; // Top-left origin + 8-bit alpha
        };
        static_assert(sizeof(TGAHeader) == 18, "TGAHeader must be 18 bytes.");
#pragma pack(pop)

        void FlipRowsVertical(std::vector<u8>& rgba, u32 width, u32 height)
        {
            const usize rowBytes = static_cast<usize>(width) * 4u;
            std::vector<u8> tempRow(rowBytes);
            for (u32 y = 0; y < height / 2; ++y)
            {
                u8* top = rgba.data() + static_cast<usize>(y) * rowBytes;
                u8* bot = rgba.data() + static_cast<usize>(height - 1 - y) * rowBytes;
                std::memcpy(tempRow.data(), top, rowBytes);
                std::memcpy(top, bot, rowBytes);
                std::memcpy(bot, tempRow.data(), rowBytes);
            }
        }

        Core::Result<ImportedTextureData, std::string> LoadTGAFile(
            const std::filesystem::path& filePath,
            const TextureImportOptions& options)
        {
            std::ifstream in(filePath, std::ios::binary);
            if (!in.is_open())
            {
                return Core::Result<ImportedTextureData, std::string>::Failure(
                    "Failed to open TGA file: " + filePath.string());
            }

            TGAHeader hdr{};
            in.read(reinterpret_cast<char*>(&hdr), sizeof(TGAHeader));
            if (!in || hdr.ImageType != 2 || hdr.Width == 0 || hdr.Height == 0 ||
                (hdr.BitsPerPixel != 24 && hdr.BitsPerPixel != 32))
            {
                return Core::Result<ImportedTextureData, std::string>::Failure(
                    "Unsupported or corrupt TGA header: " + filePath.string());
            }

            if (hdr.IdLength > 0)
            {
                in.seekg(hdr.IdLength, std::ios::cur);
            }

            const usize pixelCount = static_cast<usize>(hdr.Width) * static_cast<usize>(hdr.Height);
            const usize bytesPerPixel = hdr.BitsPerPixel / 8u;
            std::vector<u8> rawData(pixelCount * bytesPerPixel);
            in.read(reinterpret_cast<char*>(rawData.data()), static_cast<std::streamsize>(rawData.size()));

            ImportedTextureData out{};
            out.Name = filePath.stem().string();
            out.Width = hdr.Width;
            out.Height = hdr.Height;
            out.Channels = 4;
            out.RhiFormat = options.sRGB ? RHI::Format::RGBA8_sRGB : RHI::Format::RGBA8_UNorm;
            out.PixelsRGBA8.resize(pixelCount * 4u);

            // Convert BGRA/BGR -> RGBA8
            for (usize i = 0; i < pixelCount; ++i)
            {
                const u8 b = rawData[i * bytesPerPixel + 0];
                const u8 g = rawData[i * bytesPerPixel + 1];
                const u8 r = rawData[i * bytesPerPixel + 2];
                const u8 a = (bytesPerPixel == 4) ? rawData[i * bytesPerPixel + 3] : 255u;

                out.PixelsRGBA8[i * 4 + 0] = r;
                out.PixelsRGBA8[i * 4 + 1] = g;
                out.PixelsRGBA8[i * 4 + 2] = b;
                out.PixelsRGBA8[i * 4 + 3] = a;
            }

            const bool topOrigin = (hdr.ImageDescriptor & 0x20) != 0;
            if (!topOrigin ^ options.FlipVertical)
            {
                FlipRowsVertical(out.PixelsRGBA8, out.Width, out.Height);
            }

            return Core::Result<ImportedTextureData, std::string>::Success(std::move(out));
        }
    }

    Core::Result<ImportedTextureData, std::string> TextureImporter::ImportFromFile(
        const std::filesystem::path& filePath,
        const TextureImportOptions& options)
    {
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure(
                "Texture file not found: " + filePath.string());
        }

        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (ext == ".tga")
        {
            return LoadTGAFile(filePath, options);
        }

#if defined(_WIN32)
        // Decode .png, .jpg, .bmp, .tiff via Windows Imaging Component (WIC)
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("Failed to create WICImagingFactory.");
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(
            filePath.wstring().c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure(
                "WIC failed to decode image: " + filePath.string());
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC failed to get image frame 0.");
        }

        UINT width = 0;
        UINT height = 0;
        frame->GetSize(&width, &height);

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        factory->CreateFormatConverter(&converter);
        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC format conversion to RGBA8 failed.");
        }

        ImportedTextureData out{};
        out.Name = filePath.stem().string();
        out.Width = width;
        out.Height = height;
        out.Channels = 4;
        out.RhiFormat = options.sRGB ? RHI::Format::RGBA8_sRGB : RHI::Format::RGBA8_UNorm;
        out.PixelsRGBA8.resize(static_cast<usize>(width) * static_cast<usize>(height) * 4u);

        const UINT stride = width * 4u;
        hr = converter->CopyPixels(
            nullptr,
            stride,
            static_cast<UINT>(out.PixelsRGBA8.size()),
            out.PixelsRGBA8.data());
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC CopyPixels failed.");
        }

        if (options.FlipVertical)
        {
            FlipRowsVertical(out.PixelsRGBA8, out.Width, out.Height);
        }

        return Core::Result<ImportedTextureData, std::string>::Success(std::move(out));
#else
        return Core::Result<ImportedTextureData, std::string>::Failure("Unsupported platform for WIC image decoding.");
#endif
    }

    Core::Result<ImportedTextureData, std::string> TextureImporter::ImportFromMemory(
        std::span<const u8> encodedBytes,
        std::string_view debugName,
        const TextureImportOptions& options)
    {
        if (encodedBytes.empty())
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("Empty encoded texture byte buffer.");
        }

#if defined(_WIN32)
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("Failed to create WICImagingFactory.");
        }

        Microsoft::WRL::ComPtr<IWICStream> stream;
        hr = factory->CreateStream(&stream);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC CreateStream failed.");
        }

        hr = stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(encodedBytes.data())),
            static_cast<DWORD>(encodedBytes.size()));
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC InitializeFromMemory failed.");
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromStream(
            stream.Get(),
            nullptr,
            WICDecodeMetadataCacheOnDemand,
            &decoder);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC CreateDecoderFromStream failed.");
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC GetFrame(0) failed.");
        }

        UINT width = 0;
        UINT height = 0;
        frame->GetSize(&width, &height);

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        factory->CreateFormatConverter(&converter);
        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC format conversion to RGBA8 failed.");
        }

        ImportedTextureData out{};
        out.Name = std::string(debugName);
        out.Width = width;
        out.Height = height;
        out.Channels = 4;
        out.RhiFormat = options.sRGB ? RHI::Format::RGBA8_sRGB : RHI::Format::RGBA8_UNorm;
        out.PixelsRGBA8.resize(static_cast<usize>(width) * static_cast<usize>(height) * 4u);

        const UINT stride = width * 4u;
        hr = converter->CopyPixels(
            nullptr,
            stride,
            static_cast<UINT>(out.PixelsRGBA8.size()),
            out.PixelsRGBA8.data());
        if (FAILED(hr))
        {
            return Core::Result<ImportedTextureData, std::string>::Failure("WIC CopyPixels failed.");
        }

        if (options.FlipVertical)
        {
            FlipRowsVertical(out.PixelsRGBA8, out.Width, out.Height);
        }

        return Core::Result<ImportedTextureData, std::string>::Success(std::move(out));
#else
        (void)debugName;
        (void)options;
        return Core::Result<ImportedTextureData, std::string>::Failure("Unsupported platform for WIC memory decoding.");
#endif
    }

    bool TextureImporter::GenerateProceduralCheckerboardTGA(
        const std::filesystem::path& outputPath,
        u32 width,
        u32 height)
    {
        std::error_code ec;
        if (outputPath.has_parent_path())
        {
            std::filesystem::create_directories(outputPath.parent_path(), ec);
        }

        std::ofstream out(outputPath, std::ios::binary);
        if (!out.is_open())
        {
            return false;
        }

        TGAHeader hdr{};
        hdr.ImageType = 2;
        hdr.Width = static_cast<u16>(width);
        hdr.Height = static_cast<u16>(height);
        hdr.BitsPerPixel = 32;
        hdr.ImageDescriptor = 0x28;
        out.write(reinterpret_cast<const char*>(&hdr), sizeof(TGAHeader));

        std::vector<u8> bgra(static_cast<usize>(width) * static_cast<usize>(height) * 4u);
        for (u32 y = 0; y < height; ++y)
        {
            for (u32 x = 0; x < width; ++x)
            {
                const bool isAccent = (((x / 8u) + (y / 8u)) % 2u) == 0u;
                const usize idx = (static_cast<usize>(y) * width + x) * 4u;
                bgra[idx + 0] = isAccent ? 240u : 38u;  // B
                bgra[idx + 1] = isAccent ? 180u : 28u;  // G
                bgra[idx + 2] = isAccent ? 56u  : 20u;  // R
                bgra[idx + 3] = 255u;                   // A
            }
        }

        out.write(reinterpret_cast<const char*>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
        return true;
    }

    RHI::TextureHandle TextureImporter::UploadToGPU(
        RHI::IDevice& rhiDevice,
        const ImportedTextureData& textureData)
    {
        MORBI_BREADCRUMB("Asset GPU Upload");
        MORBI_TRACE("Asset", "UploadTextureToGPU '" + textureData.Name + "' (" +
            std::to_string(textureData.Width) + "x" + std::to_string(textureData.Height) + ")");
        if (textureData.Width == 0 || textureData.Height == 0 || textureData.PixelsRGBA8.empty())
        {
            return RHI::TextureHandle::Invalid();
        }

        const RHI::TextureDesc desc{
            .Width = textureData.Width,
            .Height = textureData.Height,
            .PixelFormat = textureData.RhiFormat,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = textureData.Name
        };

        return rhiDevice.CreateTexture(desc, textureData.PixelsRGBA8.data());
    }
}
