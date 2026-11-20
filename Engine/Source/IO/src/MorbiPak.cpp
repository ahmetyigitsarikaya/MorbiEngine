#include "Morbi/IO/MorbiPak.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Morbi::IO::Pak
{
    namespace
    {
        constexpr std::uint32_t kPakVersion = 1;
        constexpr std::size_t kHeaderSize = 64;
        constexpr std::string_view kMagic = "MORBIPAK";

        constexpr std::array<std::uint32_t, 64> kSha256RoundConstants {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        std::string BytesToHex(const std::uint8_t* bytes, std::size_t size)
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (std::size_t index = 0; index < size; ++index)
            {
                stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
            }
            return stream.str();
        }

        std::vector<std::uint8_t> HexToBytes(std::string_view hex)
        {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(hex.size() / 2u);
            for (std::size_t index = 0; index + 1u < hex.size(); index += 2u)
            {
                bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(index, 2u)), nullptr, 16)));
            }
            return bytes;
        }

        std::array<std::uint8_t, 32> Sha256Bytes(const std::uint8_t* data, std::size_t size)
        {
            std::vector<std::uint8_t> message;
            if (size > 0)
            {
                message.assign(data, data + size);
            }
            const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8ull;
            message.push_back(0x80u);
            while ((message.size() % 64u) != 56u)
            {
                message.push_back(0u);
            }
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffu));
            }

            std::array<std::uint32_t, 8> state {
                0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
            };

            for (std::size_t chunk = 0; chunk < message.size(); chunk += 64u)
            {
                std::array<std::uint32_t, 64> words {};
                for (std::size_t index = 0; index < 16u; ++index)
                {
                    const std::size_t offset = chunk + index * 4u;
                    words[index] = (static_cast<std::uint32_t>(message[offset]) << 24u) |
                        (static_cast<std::uint32_t>(message[offset + 1u]) << 16u) |
                        (static_cast<std::uint32_t>(message[offset + 2u]) << 8u) |
                        static_cast<std::uint32_t>(message[offset + 3u]);
                }
                for (std::size_t index = 16u; index < 64u; ++index)
                {
                    const std::uint32_t s0 = std::rotr(words[index - 15u], 7) ^ std::rotr(words[index - 15u], 18) ^ (words[index - 15u] >> 3u);
                    const std::uint32_t s1 = std::rotr(words[index - 2u], 17) ^ std::rotr(words[index - 2u], 19) ^ (words[index - 2u] >> 10u);
                    words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
                }

                std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
                std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

                for (std::size_t index = 0; index < 64u; ++index)
                {
                    const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
                    const std::uint32_t ch = (e & f) ^ ((~e) & g);
                    const std::uint32_t temp1 = h + s1 + ch + kSha256RoundConstants[index] + words[index];
                    const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
                    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t temp2 = s0 + maj;
                    h = g; g = f; f = e; e = d + temp1;
                    d = c; c = b; b = a; a = temp1 + temp2;
                }

                state[0] += a; state[1] += b; state[2] += c; state[3] += d;
                state[4] += e; state[5] += f; state[6] += g; state[7] += h;
            }

            std::array<std::uint8_t, 32> digest {};
            for (std::size_t index = 0; index < state.size(); ++index)
            {
                digest[index * 4u]      = static_cast<std::uint8_t>((state[index] >> 24u) & 0xffu);
                digest[index * 4u + 1u] = static_cast<std::uint8_t>((state[index] >> 16u) & 0xffu);
                digest[index * 4u + 2u] = static_cast<std::uint8_t>((state[index] >> 8u) & 0xffu);
                digest[index * 4u + 3u] = static_cast<std::uint8_t>(state[index] & 0xffu);
            }
            return digest;
        }

        void WriteU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
        }

        void WriteU64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffull));
            }
        }

        std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
                (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
                (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
        }

        std::uint64_t ReadU64(const std::vector<std::uint8_t>& bytes, std::size_t offset)
        {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8)
            {
                value |= static_cast<std::uint64_t>(bytes[offset + static_cast<std::size_t>(shift / 8)]) << shift;
            }
            return value;
        }

        std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }

        std::uint32_t Load32(const std::uint8_t* bytes, std::size_t offset)
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
                (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
                (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
        }

        void QuarterRound(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d)
        {
            a += b; d ^= a; d = std::rotl(d, 16);
            c += d; b ^= c; b = std::rotl(b, 12);
            a += b; d ^= a; d = std::rotl(d, 8);
            c += d; b ^= c; b = std::rotl(b, 7);
        }

        std::array<std::uint8_t, 64> ChaChaBlock(const std::array<std::uint8_t, 32>& key, const std::vector<std::uint8_t>& nonce, std::uint32_t counter)
        {
            std::array<std::uint32_t, 16> state {
                0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u,
                Load32(key.data(), 0), Load32(key.data(), 4), Load32(key.data(), 8), Load32(key.data(), 12),
                Load32(key.data(), 16), Load32(key.data(), 20), Load32(key.data(), 24), Load32(key.data(), 28),
                counter, Load32(nonce.data(), 0), Load32(nonce.data(), 4), Load32(nonce.data(), 8)
            };
            std::array<std::uint32_t, 16> working = state;
            for (int round = 0; round < 10; ++round)
            {
                QuarterRound(working[0], working[4], working[8], working[12]);
                QuarterRound(working[1], working[5], working[9], working[13]);
                QuarterRound(working[2], working[6], working[10], working[14]);
                QuarterRound(working[3], working[7], working[11], working[15]);
                QuarterRound(working[0], working[5], working[10], working[15]);
                QuarterRound(working[1], working[6], working[11], working[12]);
                QuarterRound(working[2], working[7], working[8], working[13]);
                QuarterRound(working[3], working[4], working[9], working[14]);
            }
            std::array<std::uint8_t, 64> block {};
            for (std::size_t index = 0; index < working.size(); ++index)
            {
                const std::uint32_t word = working[index] + state[index];
                block[index * 4u]      = static_cast<std::uint8_t>(word & 0xffu);
                block[index * 4u + 1u] = static_cast<std::uint8_t>((word >> 8u) & 0xffu);
                block[index * 4u + 2u] = static_cast<std::uint8_t>((word >> 16u) & 0xffu);
                block[index * 4u + 3u] = static_cast<std::uint8_t>((word >> 24u) & 0xffu);
            }
            return block;
        }

        std::vector<std::uint8_t> ChaChaXor(const std::vector<std::uint8_t>& input, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& nonce)
        {
            std::array<std::uint8_t, 32> keyBytes {};
            for (std::size_t index = 0; index < keyBytes.size() && index < key.size(); ++index)
            {
                keyBytes[index] = key[index];
            }
            std::vector<std::uint8_t> output = input;
            std::uint32_t counter = 1;
            for (std::size_t offset = 0; offset < output.size(); offset += 64u, ++counter)
            {
                const auto block = ChaChaBlock(keyBytes, nonce, counter);
                const std::size_t blockSize = std::min<std::size_t>(64u, output.size() - offset);
                for (std::size_t index = 0; index < blockSize; ++index)
                {
                    output[offset + index] ^= block[index];
                }
            }
            return output;
        }

        std::vector<std::uint8_t> DeriveNonce(const std::vector<std::uint8_t>& key, std::string_view packageName, std::string_view virtualPath)
        {
            std::vector<std::uint8_t> seed = key;
            seed.insert(seed.end(), packageName.begin(), packageName.end());
            seed.insert(seed.end(), virtualPath.begin(), virtualPath.end());
            const std::string hex = Sha256Hex(seed);
            return HexToBytes(std::string_view(hex).substr(0, 24));
        }

        PakCategory CategoryFromString(const std::string& value)
        {
            if (value == "scenes") return PakCategory::Scenes;
            if (value == "models") return PakCategory::Models;
            if (value == "textures") return PakCategory::Textures;
            if (value == "materials") return PakCategory::Materials;
            if (value == "audio") return PakCategory::Audio;
            if (value == "scripts") return PakCategory::Scripts;
            if (value == "prefabs") return PakCategory::Prefabs;
            if (value == "engineContent") return PakCategory::EngineContent;
            return PakCategory::Other;
        }
    }

    const char* CategoryToString(PakCategory category)
    {
        switch (category)
        {
        case PakCategory::Scenes:        return "scenes";
        case PakCategory::Models:        return "models";
        case PakCategory::Textures:      return "textures";
        case PakCategory::Materials:     return "materials";
        case PakCategory::Audio:         return "audio";
        case PakCategory::Scripts:       return "scripts";
        case PakCategory::Prefabs:       return "prefabs";
        case PakCategory::EngineContent: return "engineContent";
        case PakCategory::Other:         return "other";
        }
        return "other";
    }

    std::string Sha256Hex(std::string_view bytes)
    {
        const auto digest = Sha256Bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        return BytesToHex(digest.data(), digest.size());
    }

    std::string Sha256Hex(const std::vector<std::uint8_t>& bytes)
    {
        const auto digest = Sha256Bytes(bytes.data(), bytes.size());
        return BytesToHex(digest.data(), digest.size());
    }

    std::vector<std::uint8_t> GenerateExportKey()
    {
        std::vector<std::uint8_t> key(32u);
        std::random_device random;
        for (std::uint8_t& byte : key)
        {
            byte = static_cast<std::uint8_t>(random() & 0xffu);
        }
        return key;
    }

    bool WritePak(
        const std::filesystem::path& packagePath,
        const std::vector<PakFileSource>& files,
        const PakWriteOptions& options,
        PakWriteResult& result,
        std::string& error)
    {
        if (options.EncryptPayloads && options.EncryptionKey.size() < 32u)
        {
            error = "MorbiPak encrypted writes require a 256-bit key.";
            return false;
        }

        std::vector<std::uint8_t> payload;
        std::vector<PakEntry> entries;
        entries.reserve(files.size());

        for (const PakFileSource& file : files)
        {
            if (!std::filesystem::is_regular_file(file.SourcePath))
            {
                error = "Asset source file is missing: " + file.SourcePath.string();
                return false;
            }

            std::vector<std::uint8_t> plain = ReadFileBytes(file.SourcePath);
            std::vector<std::uint8_t> encoded = plain;
            std::vector<std::uint8_t> nonce;
            PakEncryption encryption = PakEncryption::None;
            std::string authTag;

            if (options.EncryptPayloads)
            {
                nonce = DeriveNonce(options.EncryptionKey, options.PackageName, file.VirtualPath);
                encoded = ChaChaXor(plain, options.EncryptionKey, nonce);

                std::vector<std::uint8_t> tagInput = options.EncryptionKey;
                tagInput.insert(tagInput.end(), nonce.begin(), nonce.end());
                tagInput.insert(tagInput.end(), encoded.begin(), encoded.end());
                authTag = Sha256Hex(tagInput);
                encryption = PakEncryption::ChaCha20Sha256;
            }

            PakEntry entry{};
            entry.VirtualPath = file.VirtualPath;
            entry.OriginalRelativePath = file.OriginalRelativePath;
            entry.Offset = static_cast<std::uint64_t>(payload.size());
            entry.CompressedSize = static_cast<std::uint64_t>(encoded.size());
            entry.UncompressedSize = static_cast<std::uint64_t>(plain.size());
            entry.Compression = PakCompression::None;
            entry.Encryption = encryption;
            entry.PlaintextSha256 = Sha256Hex(plain);
            entry.PayloadSha256 = Sha256Hex(encoded);
            entry.NonceHex = BytesToHex(nonce.data(), nonce.size());
            entry.AuthTagHex = authTag;
            entry.Category = file.Category;

            entries.push_back(std::move(entry));
            payload.insert(payload.end(), encoded.begin(), encoded.end());
        }

        nlohmann::json manifest;
        manifest["version"] = kPakVersion;
        manifest["package"] = options.PackageName;
        manifest["category"] = CategoryToString(options.Category);
        manifest["files"] = nlohmann::json::array();

        for (const PakEntry& entry : entries)
        {
            manifest["files"].push_back({
                {"virtualPath", entry.VirtualPath},
                {"originalRelativePath", entry.OriginalRelativePath},
                {"offset", entry.Offset},
                {"compressedSize", entry.CompressedSize},
                {"uncompressedSize", entry.UncompressedSize},
                {"compression", "none"},
                {"encryption", entry.Encryption == PakEncryption::None ? "none" : "chacha20-sha256"},
                {"sha256", entry.PlaintextSha256},
                {"payloadSha256", entry.PayloadSha256},
                {"nonce", entry.NonceHex},
                {"authTag", entry.AuthTagHex},
                {"category", CategoryToString(entry.Category)}
            });
        }

        const std::string manifestText = manifest.dump(2);

        std::filesystem::create_directories(packagePath.parent_path());
        std::ofstream output(packagePath, std::ios::binary);
        if (!output.is_open())
        {
            error = "Failed to open pak output: " + packagePath.string();
            return false;
        }

        std::vector<std::uint8_t> header;
        header.insert(header.end(), kMagic.begin(), kMagic.end());
        WriteU32(header, kPakVersion);
        WriteU32(header, options.EncryptPayloads ? 1u : 0u);
        WriteU32(header, static_cast<std::uint32_t>(entries.size()));
        WriteU32(header, 0u);
        WriteU64(header, kHeaderSize);
        WriteU64(header, static_cast<std::uint64_t>(manifestText.size()));
        WriteU64(header, kHeaderSize + static_cast<std::uint64_t>(manifestText.size()));
        WriteU64(header, static_cast<std::uint64_t>(payload.size()));
        header.resize(kHeaderSize, 0u);

        output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        output.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
        if (!payload.empty())
        {
            output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        }
        output.close();

        result.PackagePath = packagePath;
        result.PackageName = options.PackageName;
        result.Category = options.Category;
        result.Entries = std::move(entries);
        result.PayloadBytes = static_cast<std::uint64_t>(payload.size());
        result.UncompressedBytes = 0;
        for (const PakEntry& entry : result.Entries)
        {
            result.UncompressedBytes += entry.UncompressedSize;
        }
        result.PackageBytes = std::filesystem::file_size(packagePath);
        return true;
    }

    bool ReadPakManifest(const std::filesystem::path& packagePath, PakManifest& manifest, std::string& error)
    {
        const std::vector<std::uint8_t> bytes = ReadFileBytes(packagePath);
        if (bytes.size() < kHeaderSize || std::string_view(reinterpret_cast<const char*>(bytes.data()), kMagic.size()) != kMagic)
        {
            error = "Invalid MorbiPak header: " + packagePath.string();
            return false;
        }
        if (ReadU32(bytes, 8) != kPakVersion)
        {
            error = "Unsupported MorbiPak version.";
            return false;
        }

        const std::uint64_t manifestOffset = ReadU64(bytes, 24);
        const std::uint64_t manifestSize = ReadU64(bytes, 32);
        if (manifestOffset + manifestSize > bytes.size())
        {
            error = "Invalid MorbiPak manifest bounds.";
            return false;
        }

        try
        {
            const std::string manifestText(reinterpret_cast<const char*>(bytes.data() + manifestOffset), static_cast<std::size_t>(manifestSize));
            const auto json = nlohmann::json::parse(manifestText);
            manifest.PackageName = json.value("package", "");
            manifest.Category = CategoryFromString(json.value("category", "other"));
            manifest.Entries.clear();

            for (const auto& file : json.value("files", nlohmann::json::array()))
            {
                PakEntry entry;
                entry.VirtualPath = file.value("virtualPath", "");
                entry.OriginalRelativePath = file.value("originalRelativePath", "");
                entry.Offset = file.value("offset", 0ull);
                entry.CompressedSize = file.value("compressedSize", 0ull);
                entry.UncompressedSize = file.value("uncompressedSize", 0ull);
                entry.Encryption = file.value("encryption", "none") == "none" ? PakEncryption::None : PakEncryption::ChaCha20Sha256;
                entry.PlaintextSha256 = file.value("sha256", "");
                entry.PayloadSha256 = file.value("payloadSha256", "");
                entry.NonceHex = file.value("nonce", "");
                entry.AuthTagHex = file.value("authTag", "");
                entry.Category = CategoryFromString(file.value("category", "other"));
                manifest.Entries.push_back(std::move(entry));
            }
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }

    namespace
    {
        bool StreamReadPakEntry(
            const std::filesystem::path& packagePath,
            std::uint64_t payloadOffset,
            const PakEntry& entry,
            const std::vector<std::uint8_t>& encryptionKey,
            std::vector<std::uint8_t>& outputBytes,
            std::string& error)
        {
            std::ifstream stream(packagePath, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                error = "Failed to open MorbiPak chunk: " + packagePath.string();
                return false;
            }

            const std::uint64_t fileSize = static_cast<std::uint64_t>(stream.tellg());
            if (payloadOffset == 0)
            {
                if (fileSize < kHeaderSize)
                {
                    error = "Invalid MorbiPak file size.";
                    return false;
                }
                std::vector<std::uint8_t> header(kHeaderSize);
                stream.seekg(0, std::ios::beg);
                stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(kHeaderSize));
                payloadOffset = ReadU64(header, 40);
            }

            if (payloadOffset + entry.Offset + entry.CompressedSize > fileSize)
            {
                error = "Invalid MorbiPak payload bounds.";
                return false;
            }

            std::vector<std::uint8_t> payload(static_cast<std::size_t>(entry.CompressedSize));
            if (entry.CompressedSize > 0)
            {
                stream.seekg(static_cast<std::streamoff>(payloadOffset + entry.Offset), std::ios::beg);
                stream.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(entry.CompressedSize));
                if (!stream)
                {
                    error = "Failed while reading MorbiPak entry payload.";
                    return false;
                }
            }

            if (Sha256Hex(payload) != entry.PayloadSha256)
            {
                error = "MorbiPak encrypted payload hash mismatch.";
                return false;
            }

            if (entry.Encryption == PakEncryption::ChaCha20Sha256)
            {
                const std::vector<std::uint8_t> nonce = HexToBytes(entry.NonceHex);
                if (encryptionKey.size() < 32u || nonce.size() != 12u)
                {
                    error = "Missing or invalid MorbiPak decryption key.";
                    return false;
                }
                payload = ChaChaXor(payload, encryptionKey, nonce);
            }

            if (Sha256Hex(payload) != entry.PlaintextSha256)
            {
                error = "MorbiPak plaintext hash mismatch.";
                return false;
            }

            outputBytes = std::move(payload);
            return true;
        }
    }

    PakCategory CategorizeAssetPath(std::string_view virtualOrRelativePath) noexcept
    {
        auto containsIC = [&](std::string_view needle) {
            if (virtualOrRelativePath.size() < needle.size()) return false;
            for (std::size_t i = 0; i + needle.size() <= virtualOrRelativePath.size(); ++i)
            {
                bool match = true;
                for (std::size_t j = 0; j < needle.size(); ++j)
                {
                    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(virtualOrRelativePath[i + j])));
                    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
                    if (a != b) { match = false; break; }
                }
                if (match) return true;
            }
            return false;
        };

        if (containsIC(".mscene") || containsIC("scenes/")) return PakCategory::Scenes;
        if (containsIC("models/") || containsIC("animations/") || containsIC(".fbx") || containsIC(".gltf") || containsIC(".glb") || containsIC(".obj"))
            return PakCategory::Models;
        if (containsIC("textures/") || containsIC(".png") || containsIC(".jpg") || containsIC(".tga") || containsIC(".dds"))
            return PakCategory::Textures;
        if (containsIC("materials/") || containsIC(".mmat")) return PakCategory::Materials;
        if (containsIC("audio/") || containsIC(".wav") || containsIC(".ogg") || containsIC(".mp3")) return PakCategory::Audio;
        if (containsIC("scripts/") || containsIC(".cs") || containsIC(".dll")) return PakCategory::Scripts;
        if (containsIC("prefabs/")) return PakCategory::Prefabs;
        return PakCategory::Other;
    }

    bool ReadPakFile(
        const std::filesystem::path& packagePath,
        std::string_view virtualPath,
        const std::vector<std::uint8_t>& encryptionKey,
        std::vector<std::uint8_t>& outputBytes,
        std::string& error)
    {
        PakManifest manifest;
        if (!ReadPakManifest(packagePath, manifest, error))
        {
            return false;
        }

        const auto it = std::find_if(manifest.Entries.begin(), manifest.Entries.end(), [&](const PakEntry& e) {
            return e.VirtualPath == virtualPath;
        });
        if (it == manifest.Entries.end())
        {
            error = "Virtual path not found in MorbiPak.";
            return false;
        }

        return StreamReadPakEntry(packagePath, 0, *it, encryptionKey, outputBytes, error);
    }

    bool WriteChunkedPaks(
        const std::filesystem::path& outputDir,
        const std::vector<PakFileSource>& files,
        const PakChunkWriteOptions& options,
        PakChunkIndex& outIndex,
        std::string& error)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec)
        {
            error = "Failed to create pak chunk output directory: " + ec.message();
            return false;
        }

        outIndex = PakChunkIndex{};
        outIndex.ProjectName = options.ProjectName;
        outIndex.Encrypted = options.EncryptPayloads;
        outIndex.MaxChunkSizeBytes = options.MaxChunkSizeBytes > 0 ? options.MaxChunkSizeBytes : (256ull * 1024ull * 1024ull);

        if (files.empty())
        {
            nlohmann::json idxJson{
                { "schema_version", 1 },
                { "project", outIndex.ProjectName },
                { "encrypted", outIndex.Encrypted },
                { "max_chunk_bytes", outIndex.MaxChunkSizeBytes },
                { "chunks", nlohmann::json::array() }
            };
            std::ofstream idxOut(outputDir / "pakchunks.index.json", std::ios::binary | std::ios::trunc);
            if (!idxOut) { error = "Failed to write pakchunks.index.json"; return false; }
            idxOut << idxJson.dump(2);
            return idxOut.good();
        }

        // Group files by category and pack into bounded chunks (core/scenes first -> chunk 0)
        std::vector<PakFileSource> sorted = files;
        std::stable_sort(sorted.begin(), sorted.end(), [](const PakFileSource& a, const PakFileSource& b) {
            return static_cast<int>(a.Category) < static_cast<int>(b.Category);
        });

        std::uint32_t nextChunkIdx = 0;
        std::size_t cursor = 0;
        while (cursor < sorted.size())
        {
            const PakCategory chunkCat = sorted[cursor].Category;
            std::vector<PakFileSource> chunkFiles;
            std::uint64_t runningBytes = 0;

            while (cursor < sorted.size() && sorted[cursor].Category == chunkCat)
            {
                const std::uint64_t fSize = std::filesystem::exists(sorted[cursor].SourcePath, ec)
                    ? std::filesystem::file_size(sorted[cursor].SourcePath, ec)
                    : 0ull;
                if (!chunkFiles.empty() && runningBytes + fSize > outIndex.MaxChunkSizeBytes)
                {
                    break;
                }
                runningBytes += fSize;
                chunkFiles.push_back(sorted[cursor]);
                ++cursor;
            }

            std::ostringstream nameStream;
            nameStream << (options.BaseChunkName.empty() ? "pakchunk" : options.BaseChunkName)
                       << nextChunkIdx << "_" << CategoryToString(chunkCat) << ".mpak";
            const std::string chunkFileName = nameStream.str();
            const std::filesystem::path chunkPath = outputDir / chunkFileName;

            PakWriteOptions writeOpts{};
            writeOpts.PackageName = chunkFileName;
            writeOpts.Category = chunkCat;
            writeOpts.EncryptPayloads = options.EncryptPayloads;
            writeOpts.EncryptionKey = options.EncryptionKey;

            PakWriteResult writeRes{};
            if (!WritePak(chunkPath, chunkFiles, writeOpts, writeRes, error))
            {
                return false;
            }

            // Read 64-byte header payloadOffset once for zero-overhead direct streaming later
            std::uint64_t payloadOffset = 0;
            {
                std::ifstream hdrIn(chunkPath, std::ios::binary);
                std::vector<std::uint8_t> hdr(kHeaderSize);
                if (hdrIn.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(kHeaderSize)))
                {
                    payloadOffset = ReadU64(hdr, 40);
                }
            }

            outIndex.Chunks.push_back(PakChunkRecord{
                .ChunkIndex = nextChunkIdx,
                .FileName = chunkFileName,
                .Category = chunkCat,
                .EntryCount = writeRes.Entries.size(),
                .UncompressedBytes = writeRes.UncompressedBytes,
                .PackageBytes = writeRes.PackageBytes,
                .PayloadOffset = payloadOffset
            });

            for (const auto& entry : writeRes.Entries)
            {
                outIndex.VirtualPathIndex[entry.VirtualPath] = PakChunkLocation{
                    .ChunkPath = chunkPath,
                    .PayloadOffset = payloadOffset,
                    .Entry = entry
                };
            }

            ++nextChunkIdx;
        }

        nlohmann::json chunksJson = nlohmann::json::array();
        for (const auto& c : outIndex.Chunks)
        {
            chunksJson.push_back({
                { "chunk_index", c.ChunkIndex },
                { "file", c.FileName },
                { "category", CategoryToString(c.Category) },
                { "entry_count", c.EntryCount },
                { "uncompressed_bytes", c.UncompressedBytes },
                { "package_bytes", c.PackageBytes },
                { "payload_offset", c.PayloadOffset }
            });
        }

        const nlohmann::json masterJson{
            { "schema_version", 1 },
            { "project", outIndex.ProjectName },
            { "encrypted", outIndex.Encrypted },
            { "max_chunk_bytes", outIndex.MaxChunkSizeBytes },
            { "chunks", chunksJson }
        };

        std::ofstream masterOut(outputDir / "pakchunks.index.json", std::ios::binary | std::ios::trunc);
        if (!masterOut)
        {
            error = "Failed to write pakchunks.index.json";
            return false;
        }
        masterOut << masterJson.dump(2);
        return masterOut.good();
    }

    bool LoadChunkedPakIndex(
        const std::filesystem::path& indexJsonPath,
        PakChunkIndex& outIndex,
        std::string& error)
    {
        try
        {
            std::ifstream in(indexJsonPath, std::ios::binary);
            if (!in)
            {
                error = "Chunk index not found: " + indexJsonPath.string();
                return false;
            }
            const auto master = nlohmann::json::parse(in);

            outIndex = PakChunkIndex{};
            outIndex.ProjectName = master.value("project", "");
            outIndex.Encrypted = master.value("encrypted", false);
            outIndex.MaxChunkSizeBytes = master.value("max_chunk_bytes", 0ull);

            const std::filesystem::path baseDir = indexJsonPath.parent_path();
            for (const auto& c : master.value("chunks", nlohmann::json::array()))
            {
                PakChunkRecord rec{};
                rec.ChunkIndex = c.value("chunk_index", 0u);
                rec.FileName = c.value("file", "");
                rec.Category = CategoryFromString(c.value("category", "other"));
                rec.EntryCount = c.value("entry_count", 0ull);
                rec.UncompressedBytes = c.value("uncompressed_bytes", 0ull);
                rec.PackageBytes = c.value("package_bytes", 0ull);
                rec.PayloadOffset = c.value("payload_offset", 0ull);

                const std::filesystem::path chunkPath = baseDir / rec.FileName;
                PakManifest manifest;
                if (!ReadPakManifest(chunkPath, manifest, error))
                {
                    return false;
                }

                for (const auto& entry : manifest.Entries)
                {
                    outIndex.VirtualPathIndex[entry.VirtualPath] = PakChunkLocation{
                        .ChunkPath = chunkPath,
                        .PayloadOffset = rec.PayloadOffset,
                        .Entry = entry
                    };
                }
                outIndex.Chunks.push_back(std::move(rec));
            }
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }

    bool ReadChunkedPakFile(
        const PakChunkIndex& index,
        std::string_view virtualPath,
        const std::vector<std::uint8_t>& encryptionKey,
        std::vector<std::uint8_t>& outBytes,
        std::string& error)
    {
        const auto it = index.VirtualPathIndex.find(std::string(virtualPath));
        if (it == index.VirtualPathIndex.end())
        {
            error = "Virtual path not found in chunked pak index: " + std::string(virtualPath);
            return false;
        }
        return StreamReadPakEntry(it->second.ChunkPath, it->second.PayloadOffset, it->second.Entry, encryptionKey, outBytes, error);
    }
}
