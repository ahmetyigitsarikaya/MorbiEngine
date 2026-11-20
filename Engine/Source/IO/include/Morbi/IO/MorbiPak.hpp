#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Morbi/Core/Export.hpp"

namespace Morbi::IO::Pak
{
    enum class PakCompression
    {
        None
    };

    enum class PakEncryption
    {
        None,
        ChaCha20Sha256
    };

    enum class PakCategory
    {
        Scenes,
        Models,
        Textures,
        Materials,
        Audio,
        Scripts,
        Prefabs,
        EngineContent,
        Other
    };

    struct PakFileSource
    {
        std::filesystem::path SourcePath;
        std::string VirtualPath;
        std::string OriginalRelativePath;
        PakCategory Category = PakCategory::Other;
    };

    struct PakEntry
    {
        std::string VirtualPath;
        std::string OriginalRelativePath;
        std::uint64_t Offset = 0;
        std::uint64_t CompressedSize = 0;
        std::uint64_t UncompressedSize = 0;
        PakCompression Compression = PakCompression::None;
        PakEncryption Encryption = PakEncryption::None;
        std::string PlaintextSha256;
        std::string PayloadSha256;
        std::string NonceHex;
        std::string AuthTagHex;
        PakCategory Category = PakCategory::Other;
    };

    struct PakWriteOptions
    {
        std::string PackageName;
        PakCategory Category = PakCategory::Other;
        bool EncryptPayloads = true;
        std::vector<std::uint8_t> EncryptionKey;
    };

    struct PakWriteResult
    {
        std::filesystem::path PackagePath;
        std::string PackageName;
        PakCategory Category = PakCategory::Other;
        std::vector<PakEntry> Entries;
        std::uint64_t UncompressedBytes = 0;
        std::uint64_t PayloadBytes = 0;
        std::uint64_t PackageBytes = 0;
    };

    struct PakManifest
    {
        std::string PackageName;
        PakCategory Category = PakCategory::Other;
        std::vector<PakEntry> Entries;
    };

    struct PakChunkWriteOptions
    {
        std::string ProjectName = "MorbiGame";
        std::string BaseChunkName = "pakchunk";
        std::uint64_t MaxChunkSizeBytes = 256ull * 1024ull * 1024ull; // 256 MB default chunk cap
        bool EncryptPayloads = true;
        std::vector<std::uint8_t> EncryptionKey;
    };

    struct PakChunkRecord
    {
        std::uint32_t ChunkIndex = 0;
        std::string FileName;
        PakCategory Category = PakCategory::Other;
        std::uint64_t EntryCount = 0;
        std::uint64_t UncompressedBytes = 0;
        std::uint64_t PackageBytes = 0;
        std::uint64_t PayloadOffset = 0;
    };

    struct PakChunkLocation
    {
        std::filesystem::path ChunkPath;
        std::uint64_t PayloadOffset = 0;
        PakEntry Entry;
    };

    struct PakChunkIndex
    {
        std::string ProjectName;
        bool Encrypted = false;
        std::uint64_t MaxChunkSizeBytes = 0;
        std::vector<PakChunkRecord> Chunks;
        std::unordered_map<std::string, PakChunkLocation> VirtualPathIndex;
    };

    [[nodiscard]] MORBI_API const char* CategoryToString(PakCategory category);
    [[nodiscard]] MORBI_API PakCategory CategorizeAssetPath(std::string_view virtualOrRelativePath) noexcept;
    [[nodiscard]] MORBI_API std::string Sha256Hex(std::string_view bytes);
    [[nodiscard]] MORBI_API std::string Sha256Hex(const std::vector<std::uint8_t>& bytes);
    [[nodiscard]] MORBI_API std::vector<std::uint8_t> GenerateExportKey();

    MORBI_API bool WritePak(
        const std::filesystem::path& packagePath,
        const std::vector<PakFileSource>& files,
        const PakWriteOptions& options,
        PakWriteResult& result,
        std::string& error);

    MORBI_API bool ReadPakManifest(const std::filesystem::path& packagePath, PakManifest& manifest, std::string& error);

    MORBI_API bool ReadPakFile(
        const std::filesystem::path& packagePath,
        std::string_view virtualPath,
        const std::vector<std::uint8_t>& encryptionKey,
        std::vector<std::uint8_t>& bytes,
        std::string& error);

    // Streaming O(1) chunked reader/writer API for multi-GB games
    MORBI_API bool WriteChunkedPaks(
        const std::filesystem::path& outputDir,
        const std::vector<PakFileSource>& files,
        const PakChunkWriteOptions& options,
        PakChunkIndex& outIndex,
        std::string& error);

    MORBI_API bool LoadChunkedPakIndex(
        const std::filesystem::path& indexJsonPath,
        PakChunkIndex& outIndex,
        std::string& error);

    MORBI_API bool ReadChunkedPakFile(
        const PakChunkIndex& index,
        std::string_view virtualPath,
        const std::vector<std::uint8_t>& encryptionKey,
        std::vector<std::uint8_t>& outBytes,
        std::string& error);
}
