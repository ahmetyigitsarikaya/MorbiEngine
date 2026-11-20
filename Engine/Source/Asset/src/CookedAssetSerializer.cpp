#include "Morbi/Asset/CookedAssetSerializer.hpp"

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

#include <fstream>

#include "Morbi/IO/BinaryStream.hpp"

namespace Morbi::Asset
{
    bool CookedAssetSerializer::CookStaticMeshToFile(
        const ImportedMeshData& mesh,
        AssetUUID uuid,
        const std::filesystem::path& outputPath)
    {
        std::error_code ec;
        if (outputPath.has_parent_path())
        {
            std::filesystem::create_directories(outputPath.parent_path(), ec);
        }

        IO::BinaryWriter payload;
        payload.WriteString(mesh.Name);
        payload.Write<Math::Vec3>(mesh.BoundingBoxMin);
        payload.Write<Math::Vec3>(mesh.BoundingBoxMax);
        payload.Write<u8>(mesh.TangentsComputed ? 1u : 0u);

        const u32 vertexCount = static_cast<u32>(mesh.Vertices.size());
        payload.Write<u32>(vertexCount);
        if (vertexCount > 0)
        {
            payload.WriteBytes(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(mesh.Vertices.data()),
                mesh.Vertices.size() * sizeof(StaticMeshVertex)));
        }

        const u32 indexCount = static_cast<u32>(mesh.Indices.size());
        payload.Write<u32>(indexCount);
        if (indexCount > 0)
        {
            payload.WriteBytes(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(mesh.Indices.data()),
                mesh.Indices.size() * sizeof(u32)));
        }

        const u32 submeshCount = static_cast<u32>(mesh.Submeshes.size());
        payload.Write<u32>(submeshCount);
        for (const auto& sub : mesh.Submeshes)
        {
            payload.WriteString(sub.Name);
            payload.Write<u32>(sub.IndexOffset);
            payload.Write<u32>(sub.IndexCount);
            payload.Write<u32>(sub.MaterialIndex);
            payload.Write<Math::Vec3>(sub.BoundsMin);
            payload.Write<Math::Vec3>(sub.BoundsMax);
        }

        const u32 matCount = static_cast<u32>(mesh.Materials.size());
        payload.Write<u32>(matCount);
        for (const auto& mat : mesh.Materials)
        {
            payload.WriteString(mat.Name);
            payload.WriteString(mat.AlbedoTexturePath);
            payload.WriteString(mat.NormalTexturePath);
            payload.WriteString(mat.SpecularTexturePath);
            payload.Write<Math::Vec4>(mat.BaseColorFactor);
            payload.Write<f32>(mat.Roughness);
            payload.Write<f32>(mat.Metallic);
        }

        CookedAssetHeader header{};
        header.Magic = kCookedAssetMagic;
        header.Version = kCookedAssetVersion;
        header.AssetUUIDValue = uuid.Value;
        header.Type = static_cast<u8>(AssetType::StaticMesh);
        header.PayloadSizeBytes = payload.Size();

        std::ofstream out(outputPath, std::ios::binary);
        if (!out.is_open())
        {
            return false;
        }

        out.write(reinterpret_cast<const char*>(&header), sizeof(CookedAssetHeader));
        out.write(
            reinterpret_cast<const char*>(payload.Bytes().data()),
            static_cast<std::streamsize>(payload.Size()));
        return static_cast<bool>(out);
    }

    bool CookedAssetSerializer::LoadCookedStaticMesh(
        const std::filesystem::path& inputPath,
        ImportedMeshData& outMesh,
        AssetUUID& outUuid)
    {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in.is_open())
        {
            return false;
        }

        CookedAssetHeader header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(CookedAssetHeader));
        if (!in ||
            header.Magic != kCookedAssetMagic ||
            header.Version != kCookedAssetVersion ||
            header.Type != static_cast<u8>(AssetType::StaticMesh))
        {
            return false;
        }

        std::vector<std::byte> payloadBytes(static_cast<usize>(header.PayloadSizeBytes));
        in.read(
            reinterpret_cast<char*>(payloadBytes.data()),
            static_cast<std::streamsize>(payloadBytes.size()));
        if (!in)
        {
            return false;
        }

        IO::BinaryReader reader(payloadBytes);
        const auto nameOpt = reader.ReadString();
        const auto bminOpt = reader.Read<Math::Vec3>();
        const auto bmaxOpt = reader.Read<Math::Vec3>();
        const auto tanOpt  = reader.Read<u8>();
        const auto vtxCountOpt = reader.Read<u32>();
        if (!nameOpt || !bminOpt || !bmaxOpt || !tanOpt || !vtxCountOpt)
        {
            return false;
        }

        outUuid = AssetUUID(header.AssetUUIDValue);
        outMesh = ImportedMeshData{};
        outMesh.Name = *nameOpt;
        outMesh.BoundingBoxMin = *bminOpt;
        outMesh.BoundingBoxMax = *bmaxOpt;
        outMesh.TangentsComputed = (*tanOpt != 0);

        const u32 vtxCount = *vtxCountOpt;
        if (vtxCount > 0)
        {
            const auto rawVerts = reader.ReadBytes(static_cast<usize>(vtxCount) * sizeof(StaticMeshVertex));
            if (!rawVerts)
            {
                return false;
            }
            outMesh.Vertices.resize(vtxCount);
            std::memcpy(outMesh.Vertices.data(), rawVerts->data(), rawVerts->size());
        }

        const auto idxCountOpt = reader.Read<u32>();
        if (!idxCountOpt)
        {
            return false;
        }
        const u32 idxCount = *idxCountOpt;
        if (idxCount > 0)
        {
            const auto rawIndices = reader.ReadBytes(static_cast<usize>(idxCount) * sizeof(u32));
            if (!rawIndices)
            {
                return false;
            }
            outMesh.Indices.resize(idxCount);
            std::memcpy(outMesh.Indices.data(), rawIndices->data(), rawIndices->size());
        }

        const auto subCountOpt = reader.Read<u32>();
        if (!subCountOpt)
        {
            return false;
        }
        outMesh.Submeshes.reserve(*subCountOpt);
        for (u32 i = 0; i < *subCountOpt; ++i)
        {
            SubmeshDescriptor sub{};
            const auto sName = reader.ReadString();
            const auto sOff  = reader.Read<u32>();
            const auto sCnt  = reader.Read<u32>();
            const auto sMat  = reader.Read<u32>();
            const auto sMin  = reader.Read<Math::Vec3>();
            const auto sMax  = reader.Read<Math::Vec3>();
            if (!sName || !sOff || !sCnt || !sMat || !sMin || !sMax)
            {
                return false;
            }
            sub.Name = *sName;
            sub.IndexOffset = *sOff;
            sub.IndexCount = *sCnt;
            sub.MaterialIndex = *sMat;
            sub.BoundsMin = *sMin;
            sub.BoundsMax = *sMax;
            outMesh.Submeshes.push_back(std::move(sub));
        }

        if (const auto matCountOpt = reader.Read<u32>())
        {
            outMesh.Materials.reserve(*matCountOpt);
            for (u32 i = 0; i < *matCountOpt; ++i)
            {
                MaterialDescriptor mat{};
                const auto mName = reader.ReadString();
                const auto mAlb  = reader.ReadString();
                const auto mNrm  = reader.ReadString();
                const auto mSpc  = reader.ReadString();
                const auto mCol  = reader.Read<Math::Vec4>();
                const auto mRgh  = reader.Read<f32>();
                const auto mMet  = reader.Read<f32>();
                if (!mName || !mAlb || !mNrm || !mSpc || !mCol || !mRgh || !mMet)
                {
                    return false;
                }
                mat.Name = *mName;
                mat.AlbedoTexturePath = *mAlb;
                mat.NormalTexturePath = *mNrm;
                mat.SpecularTexturePath = *mSpc;
                mat.BaseColorFactor = *mCol;
                mat.Roughness = *mRgh;
                mat.Metallic = *mMet;
                outMesh.Materials.push_back(std::move(mat));
            }
        }

        return true;
    }

    bool CookedAssetSerializer::CookTextureToFile(
        const ImportedTextureData& texture,
        AssetUUID uuid,
        const std::filesystem::path& outputPath)
    {
        std::error_code ec;
        if (outputPath.has_parent_path())
        {
            std::filesystem::create_directories(outputPath.parent_path(), ec);
        }

        IO::BinaryWriter payload;
        payload.WriteString(texture.Name);
        payload.Write<u32>(texture.Width);
        payload.Write<u32>(texture.Height);
        payload.Write<u32>(texture.Channels);
        payload.Write<u8>(static_cast<u8>(texture.RhiFormat));
        payload.Write<u32>(static_cast<u32>(texture.PixelsRGBA8.size()));
        if (!texture.PixelsRGBA8.empty())
        {
            payload.WriteBytes(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(texture.PixelsRGBA8.data()),
                texture.PixelsRGBA8.size()));
        }

        CookedAssetHeader header{};
        header.Magic = kCookedAssetMagic;
        header.Version = kCookedAssetVersion;
        header.AssetUUIDValue = uuid.Value;
        header.Type = static_cast<u8>(AssetType::Texture2D);
        header.PayloadSizeBytes = payload.Size();

        std::ofstream out(outputPath, std::ios::binary);
        if (!out.is_open())
        {
            return false;
        }
        out.write(reinterpret_cast<const char*>(&header), sizeof(CookedAssetHeader));
        out.write(
            reinterpret_cast<const char*>(payload.Bytes().data()),
            static_cast<std::streamsize>(payload.Size()));
        return static_cast<bool>(out);
    }

    bool CookedAssetSerializer::LoadCookedTexture(
        const std::filesystem::path& inputPath,
        ImportedTextureData& outTexture,
        AssetUUID& outUuid)
    {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in.is_open())
        {
            return false;
        }

        CookedAssetHeader header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(CookedAssetHeader));
        if (!in ||
            header.Magic != kCookedAssetMagic ||
            header.Version != kCookedAssetVersion ||
            header.Type != static_cast<u8>(AssetType::Texture2D))
        {
            return false;
        }

        std::vector<std::byte> payloadBytes(static_cast<usize>(header.PayloadSizeBytes));
        in.read(
            reinterpret_cast<char*>(payloadBytes.data()),
            static_cast<std::streamsize>(payloadBytes.size()));
        if (!in)
        {
            return false;
        }

        IO::BinaryReader reader(payloadBytes);
        const auto nameOpt = reader.ReadString();
        const auto wOpt = reader.Read<u32>();
        const auto hOpt = reader.Read<u32>();
        const auto cOpt = reader.Read<u32>();
        const auto fOpt = reader.Read<u8>();
        const auto szOpt = reader.Read<u32>();
        if (!nameOpt || !wOpt || !hOpt || !cOpt || !fOpt || !szOpt)
        {
            return false;
        }

        outUuid = AssetUUID(header.AssetUUIDValue);
        outTexture = ImportedTextureData{};
        outTexture.Name = *nameOpt;
        outTexture.Width = *wOpt;
        outTexture.Height = *hOpt;
        outTexture.Channels = *cOpt;
        outTexture.RhiFormat = static_cast<RHI::Format>(*fOpt);

        if (*szOpt > 0)
        {
            const auto rawPixels = reader.ReadBytes(*szOpt);
            if (!rawPixels)
            {
                return false;
            }
            outTexture.PixelsRGBA8.resize(*szOpt);
            std::memcpy(outTexture.PixelsRGBA8.data(), rawPixels->data(), *szOpt);
        }

        return true;
    }

    GPUMeshBuffers CookedAssetSerializer::UploadMeshToGPU(
        RHI::IDevice& rhiDevice,
        const ImportedMeshData& mesh)
    {
        MORBI_BREADCRUMB("Asset GPU Upload");
        MORBI_TRACE("Asset", "UploadMeshToGPU '" + mesh.Name + "' (Verts=" +
            std::to_string(mesh.Vertices.size()) + ", Indices=" + std::to_string(mesh.Indices.size()) + ")");
        GPUMeshBuffers gpu{};
        if (mesh.Vertices.empty() || mesh.Indices.empty())
        {
            return gpu;
        }

        gpu.VertexCount = static_cast<u32>(mesh.Vertices.size());
        gpu.IndexCount  = static_cast<u32>(mesh.Indices.size());
        gpu.Submeshes   = mesh.Submeshes;
        if (gpu.Submeshes.empty())
        {
            gpu.Submeshes.push_back(SubmeshDescriptor{
                .Name = mesh.Name,
                .IndexOffset = 0,
                .IndexCount = gpu.IndexCount,
                .MaterialIndex = 0
            });
        }

        const RHI::BufferDesc vbDesc{
            .SizeBytes = mesh.Vertices.size() * sizeof(StaticMeshVertex),
            .StrideBytes = sizeof(StaticMeshVertex),
            .Usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::Storage,
            .Memory = RHI::MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = mesh.Name
        };
        gpu.VertexBuffer = rhiDevice.CreateBuffer(vbDesc, mesh.Vertices.data());
        gpu.VertexBindlessSlot = rhiDevice.GetBindlessIndex(gpu.VertexBuffer);

        const RHI::BufferDesc ibDesc{
            .SizeBytes = mesh.Indices.size() * sizeof(u32),
            .StrideBytes = sizeof(u32),
            .Usage = RHI::BufferUsage::Index | RHI::BufferUsage::Storage,
            .Memory = RHI::MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = mesh.Name
        };
        gpu.IndexBuffer = rhiDevice.CreateBuffer(ibDesc, mesh.Indices.data());
        gpu.IndexBindlessSlot = rhiDevice.GetBindlessIndex(gpu.IndexBuffer);

        return gpu;
    }

    void CookedAssetSerializer::DestroyGPUMeshBuffers(
        RHI::IDevice& rhiDevice,
        GPUMeshBuffers& buffers)
    {
        if (buffers.VertexBuffer.IsValid())
        {
            rhiDevice.DestroyBuffer(buffers.VertexBuffer);
            buffers.VertexBuffer = RHI::BufferHandle::Invalid();
        }
        if (buffers.IndexBuffer.IsValid())
        {
            rhiDevice.DestroyBuffer(buffers.IndexBuffer);
            buffers.IndexBuffer = RHI::BufferHandle::Invalid();
        }
    }
}
