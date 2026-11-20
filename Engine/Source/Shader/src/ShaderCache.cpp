#include "Morbi/Shader/ShaderCache.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <optional>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

#include "Morbi/Core/Log.hpp"
#include "Morbi/IO/BinaryStream.hpp"

namespace Morbi::Shader
{
    namespace
    {
        constexpr u32 kShaderCacheMagic = 0x4D534843u; // 'MSHC'
        constexpr u32 kShaderCacheVersion = 3u;
        constexpr u32 kMaxSerializedCollectionCount = 65536u;
        constexpr u32 kMaxSerializedMemberDepth = 16u;

#pragma pack(push, 1)
        struct ShaderCacheFileHeaderV3
        {
            u32 Magic = kShaderCacheMagic;
            u32 Version = kShaderCacheVersion;
            u64 BaseRequestHash = 0;
            u64 CompilerVersionHash = 0;
            u64 PayloadSizeBytes = 0;
            u32 PayloadCrc32 = 0;
            u32 Reserved = 0;
        };
#pragma pack(pop)
        static_assert(sizeof(ShaderCacheFileHeaderV3) == 40, "ShaderCacheFileHeaderV3 must be 40 bytes.");

        void WriteOptionalU32(IO::BinaryWriter& writer, const std::optional<u32>& value)
        {
            writer.Write<u8>(value.has_value() ? 1u : 0u);
            if (value.has_value())
            {
                writer.Write<u32>(*value);
            }
        }

        bool ReadOptionalU32(IO::BinaryReader& reader, std::optional<u32>& outValue)
        {
            const auto hasVal = reader.Read<u8>();
            if (!hasVal)
            {
                return false;
            }
            if (*hasVal == 0u)
            {
                outValue = std::nullopt;
                return true;
            }
            const auto val = reader.Read<u32>();
            if (!val)
            {
                return false;
            }
            outValue = *val;
            return true;
        }

        void WriteShaderMembers(IO::BinaryWriter& writer, const std::vector<ReflectedShaderMember>& members)
        {
            writer.Write<u32>(static_cast<u32>(members.size()));
            for (const auto& m : members)
            {
                writer.WriteString(m.Name);
                writer.Write<u32>(m.OffsetBytes);
                writer.Write<u32>(m.SizeBytes);
                writer.Write<u32>(m.PaddedSizeBytes);
                writer.Write<u8>(static_cast<u8>(m.Class));
                writer.Write<u8>(static_cast<u8>(m.ScalarType));
                writer.Write<u32>(m.Rows);
                writer.Write<u32>(m.Columns);
                writer.Write<u32>(m.ArrayElementCount);
                writer.Write<u32>(m.ArrayStrideBytes);
                writer.Write<u32>(m.MatrixStrideBytes);
                writer.Write<u8>(m.IsRowMajor ? 1u : 0u);
                WriteShaderMembers(writer, m.Members);
            }
        }

        bool ReadShaderMembers(IO::BinaryReader& reader, std::vector<ReflectedShaderMember>& outMembers, u32 depth = 0u)
        {
            if (depth > kMaxSerializedMemberDepth)
            {
                return false;
            }
            const auto count = reader.Read<u32>();
            if (!count || *count > kMaxSerializedCollectionCount)
            {
                return false;
            }
            outMembers.clear();
            outMembers.reserve(*count);

            for (u32 i = 0u; i < *count; ++i)
            {
                const auto name = reader.ReadString();
                const auto offsetBytes = reader.Read<u32>();
                const auto sizeBytes = reader.Read<u32>();
                const auto paddedSizeBytes = reader.Read<u32>();
                const auto varClass = reader.Read<u8>();
                const auto scalarType = reader.Read<u8>();
                const auto rows = reader.Read<u32>();
                const auto columns = reader.Read<u32>();
                const auto arrayCount = reader.Read<u32>();
                const auto arrayStride = reader.Read<u32>();
                const auto matrixStride = reader.Read<u32>();
                const auto isRowMajor = reader.Read<u8>();

                if (!name || !offsetBytes || !sizeBytes || !paddedSizeBytes || !varClass ||
                    !scalarType || !rows || !columns || !arrayCount || !arrayStride ||
                    !matrixStride || !isRowMajor)
                {
                    return false;
                }

                ReflectedShaderMember member{};
                member.Name = *name;
                member.OffsetBytes = *offsetBytes;
                member.SizeBytes = *sizeBytes;
                member.PaddedSizeBytes = *paddedSizeBytes;
                member.Class = static_cast<ReflectedVariableClass>(*varClass);
                member.ScalarType = static_cast<ReflectedScalarType>(*scalarType);
                member.Rows = *rows;
                member.Columns = *columns;
                member.ArrayElementCount = *arrayCount;
                member.ArrayStrideBytes = *arrayStride;
                member.MatrixStrideBytes = *matrixStride;
                member.IsRowMajor = (*isRowMajor != 0u);
                if (!ReadShaderMembers(reader, member.Members, depth + 1u))
                {
                    return false;
                }
                outMembers.push_back(std::move(member));
            }
            return true;
        }

        void WriteStageParameters(IO::BinaryWriter& writer, const std::vector<ReflectedStageParameter>& params)
        {
            writer.Write<u32>(static_cast<u32>(params.size()));
            for (const auto& p : params)
            {
                writer.WriteString(p.Name);
                writer.WriteString(p.SemanticName);
                writer.Write<u32>(p.SemanticIndex);
                WriteOptionalU32(writer, p.Location);
                WriteOptionalU32(writer, p.Register);
                writer.Write<u8>(static_cast<u8>(p.ComponentType));
                writer.Write<u32>(p.ComponentCount);
                writer.Write<u8>(p.ComponentMask);
                writer.Write<u8>(p.IsSystemValue ? 1u : 0u);
                writer.WriteString(p.SystemValueName);
            }
        }

        bool ReadStageParameters(IO::BinaryReader& reader, std::vector<ReflectedStageParameter>& outParams)
        {
            const auto count = reader.Read<u32>();
            if (!count || *count > kMaxSerializedCollectionCount)
            {
                return false;
            }
            outParams.clear();
            outParams.reserve(*count);

            for (u32 i = 0u; i < *count; ++i)
            {
                const auto name = reader.ReadString();
                const auto semName = reader.ReadString();
                const auto semIdx = reader.Read<u32>();
                std::optional<u32> location;
                std::optional<u32> reg;
                if (!name || !semName || !semIdx ||
                    !ReadOptionalU32(reader, location) ||
                    !ReadOptionalU32(reader, reg))
                {
                    return false;
                }
                const auto compType = reader.Read<u8>();
                const auto compCount = reader.Read<u32>();
                const auto compMask = reader.Read<u8>();
                const auto isSv = reader.Read<u8>();
                const auto svName = reader.ReadString();
                if (!compType || !compCount || !compMask || !isSv || !svName)
                {
                    return false;
                }

                outParams.push_back(ReflectedStageParameter{
                    .Name = *name,
                    .SemanticName = *semName,
                    .SemanticIndex = *semIdx,
                    .Location = location,
                    .Register = reg,
                    .ComponentType = static_cast<ReflectedScalarType>(*compType),
                    .ComponentCount = *compCount,
                    .ComponentMask = *compMask,
                    .IsSystemValue = (*isSv != 0u),
                    .SystemValueName = *svName
                });
            }
            return true;
        }

        u32 ComputeCrc32(std::span<const std::byte> data) noexcept
        {
            u32 crc = 0xFFFFFFFFu;
            for (const std::byte b : data)
            {
                crc ^= static_cast<u32>(static_cast<u8>(b));
                for (int bit = 0; bit < 8; ++bit)
                {
                    const u32 mask = static_cast<u32>(-(static_cast<i32>(crc & 1u)));
                    crc = (crc >> 1) ^ (0xEDB88320u & mask);
                }
            }
            return ~crc;
        }
    }

    ShaderCache::ShaderCache(std::filesystem::path cacheDirectory, ShaderCompiler compiler)
        : m_cacheDirectory(std::move(cacheDirectory))
        , m_compiler(std::move(compiler))
    {
        std::error_code ec;
        std::filesystem::create_directories(m_cacheDirectory, ec);
    }

    std::filesystem::path ShaderCache::GetCacheFilePath(u64 baseHash, ShaderTargetFormat format, ShaderStage stage) const
    {
        const char* fmtExt = (format == ShaderTargetFormat::SPIRV) ? "spv" : "dxil";
        const char* stgExt = (stage == ShaderStage::Vertex) ? "vs" : (stage == ShaderStage::Pixel ? "ps" : "cs");
        return m_cacheDirectory / (std::to_string(baseHash) + "_" + stgExt + "." + fmtExt + ".mshc");
    }

    std::filesystem::path ShaderCache::GetCacheFilePathForRequest(const ShaderCompileRequest& request) const
    {
        const u64 baseHash = ShaderCompiler::ComputeRequestHash(request, &m_compiler.GetCompilerVersion());
        return GetCacheFilePath(baseHash, request.TargetFormat, request.Stage);
    }

    bool ShaderCache::AreDependenciesValid(const CompiledShaderArtifact& artifact) const noexcept
    {
        if (artifact.CompilerVersionHash != m_compiler.GetCompilerVersion().VersionHash)
        {
            return false;
        }

        for (const auto& dep : artifact.IncludeDependencies)
        {
            u64 currentHash = 0;
            u64 currentSize = 0;
            if (!ShaderCompiler::ComputeFileContentHash(std::filesystem::path(dep.CanonicalPath), currentHash, currentSize))
            {
                return false;
            }
            if (currentHash != dep.ContentHash || currentSize != dep.ByteSize)
            {
                return false;
            }
        }
        return true;
    }

    ShaderCache::DiskLoadStatus ShaderCache::TryLoadFromDisk(
        const std::filesystem::path& path,
        u64 expectedBaseHash,
        CompiledShaderArtifact& outArtifact) const
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
        {
            return DiskLoadStatus::Miss;
        }

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return DiskLoadStatus::Miss;
        }

        const std::streamsize totalSize = file.tellg();
        if (totalSize < static_cast<std::streamsize>(sizeof(ShaderCacheFileHeaderV3)))
        {
            return DiskLoadStatus::Corrupt;
        }
        file.seekg(0, std::ios::beg);

        std::vector<std::byte> raw(static_cast<usize>(totalSize));
        if (!file.read(reinterpret_cast<char*>(raw.data()), totalSize))
        {
            return DiskLoadStatus::Corrupt;
        }

        ShaderCacheFileHeaderV3 header{};
        std::memcpy(&header, raw.data(), sizeof(ShaderCacheFileHeaderV3));

        if (header.Magic != kShaderCacheMagic ||
            header.Version == 0u ||
            header.Version > kShaderCacheVersion)
        {
            return DiskLoadStatus::Corrupt;
        }
        if (header.Version < kShaderCacheVersion)
        {
            return DiskLoadStatus::StaleDependency;
        }

        if (header.BaseRequestHash != expectedBaseHash ||
            header.CompilerVersionHash != m_compiler.GetCompilerVersion().VersionHash)
        {
            return DiskLoadStatus::StaleDependency;
        }

        const usize payloadOffset = sizeof(ShaderCacheFileHeaderV3);
        const usize remainingBytes = raw.size() - payloadOffset;
        if (header.PayloadSizeBytes != static_cast<u64>(remainingBytes))
        {
            return DiskLoadStatus::Corrupt;
        }

        const std::span<const std::byte> payloadSpan(raw.data() + payloadOffset, remainingBytes);
        const u32 actualCrc = ComputeCrc32(payloadSpan);
        if (actualCrc != header.PayloadCrc32)
        {
            return DiskLoadStatus::Corrupt;
        }

        IO::BinaryReader reader(payloadSpan);
        const auto stage = reader.Read<u8>();
        const auto format = reader.Read<u8>();
        const auto model = reader.Read<u8>();
        const auto contentHash = reader.Read<u64>();
        const auto compilerVerStr = reader.ReadString();
        const auto entryPoint = reader.ReadString();
        const auto reflValid = reader.Read<u8>();
        const auto pushConstSize = reader.Read<u32>();
        const auto tgX = reader.Read<u32>();
        const auto tgY = reader.Read<u32>();
        const auto tgZ = reader.Read<u32>();
        const auto usesBindless = reader.Read<u8>();
        const auto bindingCount = reader.Read<u32>();

        if (!stage || !format || !model || !contentHash || !compilerVerStr || !entryPoint ||
            !reflValid || !pushConstSize || !tgX || !tgY || !tgZ || !usesBindless ||
            !bindingCount || *bindingCount > kMaxSerializedCollectionCount)
        {
            return DiskLoadStatus::Corrupt;
        }

        outArtifact = {};
        outArtifact.Stage = static_cast<ShaderStage>(*stage);
        outArtifact.TargetFormat = static_cast<ShaderTargetFormat>(*format);
        outArtifact.Model = static_cast<ShaderModel>(*model);
        outArtifact.ContentHash = *contentHash;
        outArtifact.CompilerVersionHash = header.CompilerVersionHash;
        outArtifact.CompilerVersionString = *compilerVerStr;
        outArtifact.Reflection.TargetFormat = outArtifact.TargetFormat;
        outArtifact.Reflection.Stage = outArtifact.Stage;
        outArtifact.Reflection.EntryPoint = *entryPoint;
        outArtifact.Reflection.IsValid = (*reflValid != 0u);
        outArtifact.Reflection.PushConstantSizeBytes = *pushConstSize;
        outArtifact.Reflection.ThreadGroupSizeX = *tgX;
        outArtifact.Reflection.ThreadGroupSizeY = *tgY;
        outArtifact.Reflection.ThreadGroupSizeZ = *tgZ;
        outArtifact.Reflection.UsesBindlessHeap = (*usesBindless != 0u);
        outArtifact.Reflection.Bindings.reserve(*bindingCount);

        for (u32 i = 0; i < *bindingCount; ++i)
        {
            const auto bName = reader.ReadString();
            const auto bType = reader.Read<u8>();
            const auto bKind = reader.Read<u8>();
            const auto bDim = reader.Read<u8>();
            const auto bSlot = reader.Read<u32>();
            const auto bSpace = reader.Read<u32>();
            const auto bCount = reader.Read<u32>();
            const auto bUnbounded = reader.Read<u8>();
            const auto bReadOnly = reader.Read<u8>();
            const auto bCompSampler = reader.Read<u8>();
            std::optional<u32> descSet;
            std::optional<u32> bindingIdx;
            std::optional<u32> dxilReg;
            std::optional<u32> dxilSpace;
            std::optional<u32> cbIdx;
            if (!bName || !bType || !bKind || !bDim || !bSlot || !bSpace || !bCount ||
                !bUnbounded || !bReadOnly || !bCompSampler ||
                !ReadOptionalU32(reader, descSet) ||
                !ReadOptionalU32(reader, bindingIdx) ||
                !ReadOptionalU32(reader, dxilReg) ||
                !ReadOptionalU32(reader, dxilSpace) ||
                !ReadOptionalU32(reader, cbIdx))
            {
                return DiskLoadStatus::Corrupt;
            }
            const auto bVis = reader.Read<u32>();
            if (!bVis)
            {
                return DiskLoadStatus::Corrupt;
            }
            outArtifact.Reflection.Bindings.push_back(ReflectedBinding{
                .Name = *bName,
                .Type = static_cast<ResourceBindingType>(*bType),
                .Kind = static_cast<ReflectedResourceKind>(*bKind),
                .Dimension = static_cast<ReflectedResourceDimension>(*bDim),
                .RegisterSlot = *bSlot,
                .RegisterSpace = *bSpace,
                .Count = *bCount,
                .IsUnboundedArray = (*bUnbounded != 0u),
                .IsReadOnly = (*bReadOnly != 0u),
                .IsComparisonSampler = (*bCompSampler != 0u),
                .DescriptorSet = descSet,
                .Binding = bindingIdx,
                .DxilRegister = dxilReg,
                .DxilSpace = dxilSpace,
                .ConstantBufferIndex = cbIdx,
                .Visibility = static_cast<ReflectedShaderVisibility>(*bVis)
            });
        }

        const auto cbCount = reader.Read<u32>();
        if (!cbCount || *cbCount > kMaxSerializedCollectionCount)
        {
            return DiskLoadStatus::Corrupt;
        }
        outArtifact.Reflection.ConstantBuffers.reserve(*cbCount);
        for (u32 i = 0; i < *cbCount; ++i)
        {
            const auto cbName = reader.ReadString();
            const auto cbSize = reader.Read<u32>();
            std::optional<u32> descSet;
            std::optional<u32> bindingIdx;
            std::optional<u32> dxilReg;
            std::optional<u32> dxilSpace;
            if (!cbName || !cbSize ||
                !ReadOptionalU32(reader, descSet) ||
                !ReadOptionalU32(reader, bindingIdx) ||
                !ReadOptionalU32(reader, dxilReg) ||
                !ReadOptionalU32(reader, dxilSpace))
            {
                return DiskLoadStatus::Corrupt;
            }
            ReflectedConstantBuffer cb{};
            cb.Name = *cbName;
            cb.SizeBytes = *cbSize;
            cb.DescriptorSet = descSet;
            cb.Binding = bindingIdx;
            cb.DxilRegister = dxilReg;
            cb.DxilSpace = dxilSpace;
            if (!ReadShaderMembers(reader, cb.Members))
            {
                return DiskLoadStatus::Corrupt;
            }
            outArtifact.Reflection.ConstantBuffers.push_back(std::move(cb));
        }

        const auto pushCount = reader.Read<u32>();
        if (!pushCount || *pushCount > kMaxSerializedCollectionCount)
        {
            return DiskLoadStatus::Corrupt;
        }
        outArtifact.Reflection.PushConstants.reserve(*pushCount);
        for (u32 i = 0; i < *pushCount; ++i)
        {
            const auto pcName = reader.ReadString();
            const auto pcOffset = reader.Read<u32>();
            const auto pcSize = reader.Read<u32>();
            const auto pcVis = reader.Read<u32>();
            if (!pcName || !pcOffset || !pcSize || !pcVis)
            {
                return DiskLoadStatus::Corrupt;
            }
            ReflectedPushConstantRange range{};
            range.Name = *pcName;
            range.OffsetBytes = *pcOffset;
            range.SizeBytes = *pcSize;
            range.Visibility = static_cast<ReflectedShaderVisibility>(*pcVis);
            if (!ReadShaderMembers(reader, range.Members))
            {
                return DiskLoadStatus::Corrupt;
            }
            outArtifact.Reflection.PushConstants.push_back(std::move(range));
        }

        if (!ReadStageParameters(reader, outArtifact.Reflection.StageInputs) ||
            !ReadStageParameters(reader, outArtifact.Reflection.StageOutputs))
        {
            return DiskLoadStatus::Corrupt;
        }

        const auto depCount = reader.Read<u32>();
        if (!depCount || *depCount > kMaxSerializedCollectionCount)
        {
            return DiskLoadStatus::Corrupt;
        }
        outArtifact.IncludeDependencies.reserve(*depCount);
        for (u32 i = 0; i < *depCount; ++i)
        {
            const auto depPath = reader.ReadString();
            const auto depHash = reader.Read<u64>();
            const auto depSize = reader.Read<u64>();
            if (!depPath || !depHash || !depSize)
            {
                return DiskLoadStatus::Corrupt;
            }
            outArtifact.IncludeDependencies.push_back(ShaderIncludeDependency{
                .CanonicalPath = *depPath,
                .ContentHash = *depHash,
                .ByteSize = *depSize
            });
        }

        const auto bytecodeSize = reader.Read<u64>();
        if (!bytecodeSize || (payloadSpan.size() - reader.Offset()) != static_cast<usize>(*bytecodeSize))
        {
            return DiskLoadStatus::Corrupt;
        }

        if (*bytecodeSize > 0)
        {
            const auto bytesOpt = reader.ReadBytes(static_cast<usize>(*bytecodeSize));
            if (!bytesOpt)
            {
                return DiskLoadStatus::Corrupt;
            }
            outArtifact.Bytecode.resize(bytesOpt->size());
            std::memcpy(outArtifact.Bytecode.data(), bytesOpt->data(), bytesOpt->size());
        }

        if (!AreDependenciesValid(outArtifact))
        {
            return DiskLoadStatus::StaleDependency;
        }

        return DiskLoadStatus::Hit;
    }

    bool ShaderCache::SaveToDisk(
        const std::filesystem::path& path,
        u64 baseHash,
        const CompiledShaderArtifact& artifact) const
    {
        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        IO::BinaryWriter payloadWriter;
        payloadWriter.Write<u8>(static_cast<u8>(artifact.Stage));
        payloadWriter.Write<u8>(static_cast<u8>(artifact.TargetFormat));
        payloadWriter.Write<u8>(static_cast<u8>(artifact.Model));
        payloadWriter.Write<u64>(artifact.ContentHash);
        payloadWriter.WriteString(artifact.CompilerVersionString);
        payloadWriter.WriteString(artifact.Reflection.EntryPoint);
        payloadWriter.Write<u8>(artifact.Reflection.IsValid ? 1u : 0u);
        payloadWriter.Write<u32>(artifact.Reflection.PushConstantSizeBytes);
        payloadWriter.Write<u32>(artifact.Reflection.ThreadGroupSizeX);
        payloadWriter.Write<u32>(artifact.Reflection.ThreadGroupSizeY);
        payloadWriter.Write<u32>(artifact.Reflection.ThreadGroupSizeZ);
        payloadWriter.Write<u8>(artifact.Reflection.UsesBindlessHeap ? 1u : 0u);
        payloadWriter.Write<u32>(static_cast<u32>(artifact.Reflection.Bindings.size()));

        for (const auto& b : artifact.Reflection.Bindings)
        {
            payloadWriter.WriteString(b.Name);
            payloadWriter.Write<u8>(static_cast<u8>(b.Type));
            payloadWriter.Write<u8>(static_cast<u8>(b.Kind));
            payloadWriter.Write<u8>(static_cast<u8>(b.Dimension));
            payloadWriter.Write<u32>(b.RegisterSlot);
            payloadWriter.Write<u32>(b.RegisterSpace);
            payloadWriter.Write<u32>(b.Count);
            payloadWriter.Write<u8>(b.IsUnboundedArray ? 1u : 0u);
            payloadWriter.Write<u8>(b.IsReadOnly ? 1u : 0u);
            payloadWriter.Write<u8>(b.IsComparisonSampler ? 1u : 0u);
            WriteOptionalU32(payloadWriter, b.DescriptorSet);
            WriteOptionalU32(payloadWriter, b.Binding);
            WriteOptionalU32(payloadWriter, b.DxilRegister);
            WriteOptionalU32(payloadWriter, b.DxilSpace);
            WriteOptionalU32(payloadWriter, b.ConstantBufferIndex);
            payloadWriter.Write<u32>(static_cast<u32>(b.Visibility));
        }

        payloadWriter.Write<u32>(static_cast<u32>(artifact.Reflection.ConstantBuffers.size()));
        for (const auto& cb : artifact.Reflection.ConstantBuffers)
        {
            payloadWriter.WriteString(cb.Name);
            payloadWriter.Write<u32>(cb.SizeBytes);
            WriteOptionalU32(payloadWriter, cb.DescriptorSet);
            WriteOptionalU32(payloadWriter, cb.Binding);
            WriteOptionalU32(payloadWriter, cb.DxilRegister);
            WriteOptionalU32(payloadWriter, cb.DxilSpace);
            WriteShaderMembers(payloadWriter, cb.Members);
        }

        payloadWriter.Write<u32>(static_cast<u32>(artifact.Reflection.PushConstants.size()));
        for (const auto& pc : artifact.Reflection.PushConstants)
        {
            payloadWriter.WriteString(pc.Name);
            payloadWriter.Write<u32>(pc.OffsetBytes);
            payloadWriter.Write<u32>(pc.SizeBytes);
            payloadWriter.Write<u32>(static_cast<u32>(pc.Visibility));
            WriteShaderMembers(payloadWriter, pc.Members);
        }

        WriteStageParameters(payloadWriter, artifact.Reflection.StageInputs);
        WriteStageParameters(payloadWriter, artifact.Reflection.StageOutputs);

        payloadWriter.Write<u32>(static_cast<u32>(artifact.IncludeDependencies.size()));
        for (const auto& dep : artifact.IncludeDependencies)
        {
            payloadWriter.WriteString(dep.CanonicalPath);
            payloadWriter.Write<u64>(dep.ContentHash);
            payloadWriter.Write<u64>(dep.ByteSize);
        }

        payloadWriter.Write<u64>(static_cast<u64>(artifact.Bytecode.size()));
        if (!artifact.Bytecode.empty())
        {
            payloadWriter.WriteBytes(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(artifact.Bytecode.data()), artifact.Bytecode.size()));
        }

        ShaderCacheFileHeaderV3 header{};
        header.Magic = kShaderCacheMagic;
        header.Version = kShaderCacheVersion;
        header.BaseRequestHash = baseHash;
        header.CompilerVersionHash = artifact.CompilerVersionHash;
        header.PayloadSizeBytes = static_cast<u64>(payloadWriter.Size());
        header.PayloadCrc32 = ComputeCrc32(payloadWriter.Bytes());

        static std::atomic<u64> s_atomicWriteId{ 0 };
        const u64 writeId = s_atomicWriteId.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path tempPath = path.string() + ".tmp." + std::to_string(writeId);

        {
            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                return false;
            }
            out.write(reinterpret_cast<const char*>(&header), sizeof(ShaderCacheFileHeaderV3));
            if (payloadWriter.Size() > 0)
            {
                out.write(
                    reinterpret_cast<const char*>(payloadWriter.Bytes().data()),
                    static_cast<std::streamsize>(payloadWriter.Size()));
            }
            out.flush();
            if (!out.good())
            {
                out.close();
                std::filesystem::remove(tempPath, ec);
                return false;
            }
        }

#if defined(_WIN32)
        if (!MoveFileExW(
                tempPath.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
#else
        std::filesystem::rename(tempPath, path, ec);
        if (ec)
        {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
#endif
        return true;
    }

    Core::Result<CompiledShaderArtifact, std::string> ShaderCache::GetOrCompile(const ShaderCompileRequest& request)
    {
        const u64 baseHash = ShaderCompiler::ComputeRequestHash(request, &m_compiler.GetCompilerVersion());
        const std::filesystem::path diskPath = GetCacheFilePath(baseHash, request.TargetFormat, request.Stage);

        bool invalidatedInMemory = false;
        {
            std::scoped_lock lock(m_mutex);
            if (const auto it = m_memoryCache.find(baseHash); it != m_memoryCache.end())
            {
                if (AreDependenciesValid(it->second))
                {
                    ++m_stats.MemoryHits;
                    return Core::Result<CompiledShaderArtifact, std::string>::Success(it->second);
                }

                m_memoryCache.erase(it);
                ++m_stats.Invalidations;
                invalidatedInMemory = true;
            }
        }

        CompiledShaderArtifact diskArtifact{};
        DiskLoadStatus diskStatus = DiskLoadStatus::Miss;
        {
            std::scoped_lock diskLock(m_diskMutex);
            if (invalidatedInMemory)
            {
                std::error_code ec;
                std::filesystem::remove(diskPath, ec);
            }
            else
            {
                diskStatus = TryLoadFromDisk(diskPath, baseHash, diskArtifact);
                if (diskStatus == DiskLoadStatus::Corrupt || diskStatus == DiskLoadStatus::StaleDependency)
                {
                    std::error_code ec;
                    std::filesystem::remove(diskPath, ec);
                }
            }
        }

        if (diskStatus == DiskLoadStatus::Hit)
        {
            std::scoped_lock lock(m_mutex);
            ++m_stats.DiskHits;
            m_memoryCache.insert_or_assign(baseHash, diskArtifact);
            return Core::Result<CompiledShaderArtifact, std::string>::Success(std::move(diskArtifact));
        }

        if (diskStatus == DiskLoadStatus::Corrupt || diskStatus == DiskLoadStatus::StaleDependency)
        {
            std::scoped_lock lock(m_mutex);
            if (diskStatus == DiskLoadStatus::Corrupt)
            {
                ++m_stats.CorruptRecoveries;
            }
            else
            {
                ++m_stats.Invalidations;
            }
        }

        auto compiled = m_compiler.Compile(request);
        if (!compiled)
        {
            return compiled;
        }

        {
            std::scoped_lock diskLock(m_diskMutex);
            (void)SaveToDisk(diskPath, baseHash, compiled.Value());
        }

        {
            std::scoped_lock lock(m_mutex);
            ++m_stats.Compilations;
            m_memoryCache.insert_or_assign(baseHash, compiled.Value());
        }

        return compiled;
    }

    bool ShaderCache::Invalidate(const ShaderCompileRequest& request)
    {
        const u64 baseHash = ShaderCompiler::ComputeRequestHash(request, &m_compiler.GetCompilerVersion());
        const std::filesystem::path diskPath = GetCacheFilePath(baseHash, request.TargetFormat, request.Stage);

        bool removed = false;
        {
            std::scoped_lock lock(m_mutex);
            if (m_memoryCache.erase(baseHash) > 0)
            {
                removed = true;
            }
        }

        {
            std::scoped_lock diskLock(m_diskMutex);
            std::error_code ec;
            if (std::filesystem::remove(diskPath, ec) && !ec)
            {
                removed = true;
            }
        }

        if (removed)
        {
            std::scoped_lock lock(m_mutex);
            ++m_stats.Invalidations;
        }
        return removed;
    }

    void ShaderCache::ClearMemoryCache()
    {
        std::scoped_lock lock(m_mutex);
        m_memoryCache.clear();
    }

    void ShaderCache::ClearAll()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_memoryCache.clear();
        }

        std::scoped_lock diskLock(m_diskMutex);
        std::error_code ec;
        if (std::filesystem::exists(m_cacheDirectory, ec) && !ec)
        {
            for (const auto& entry : std::filesystem::directory_iterator(m_cacheDirectory, ec))
            {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".mshc")
                {
                    std::filesystem::remove(entry.path(), ec);
                }
            }
        }
    }

    ShaderCacheStats ShaderCache::GetStats() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_stats;
    }
}
