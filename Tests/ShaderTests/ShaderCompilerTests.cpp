#include "Morbi/Shader/ShaderCache.hpp"
#include "Morbi/Shader/ShaderCompiler.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace Morbi;
using namespace Morbi::Shader;

namespace
{
    int g_passedAssertions = 0;
    int g_failedAssertions = 0;

    void Check(bool condition, const char* expr, const std::string& detail = {})
    {
        if (condition)
        {
            ++g_passedAssertions;
        }
        else
        {
            ++g_failedAssertions;
            std::cerr << "[FAIL] " << expr;
            if (!detail.empty())
            {
                std::cerr << " | " << detail;
            }
            std::cerr << "\n";
        }
    }

    #define EXPECT_TRUE(cond, ...) Check((cond), #cond, ##__VA_ARGS__)

    bool HasDxilMagic(std::span<const u8> bytecode)
    {
        if (bytecode.size() < 4)
        {
            return false;
        }
        u32 magic = 0;
        std::memcpy(&magic, bytecode.data(), sizeof(u32));
        return magic == 0x43425844u; // 'DXBC'
    }

    bool HasSpirvMagic(std::span<const u8> bytecode)
    {
        if (bytecode.size() < 20 || (bytecode.size() % 4) != 0)
        {
            return false;
        }
        u32 magic = 0;
        std::memcpy(&magic, bytecode.data(), sizeof(u32));
        return magic == 0x07230203u;
    }

    struct ScopedTempDir
    {
        std::filesystem::path Path;

        explicit ScopedTempDir(const std::string& tag)
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            Path = std::filesystem::temp_directory_path() / ("MorbiShaderTests_" + tag + "_" + std::to_string(now));
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
            std::filesystem::create_directories(Path, ec);
        }

        ~ScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }
    };

    void WriteTextFile(const std::filesystem::path& path, const std::string& content)
    {
        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    void Test_DxcInitVersionAndLifetime()
    {
        std::cout << "\n--- Test: In-Process DXC Initialization, Version & Lifetime ---\n";

        ShaderCompiler compilerCopy;
        {
            ShaderCompiler primary;
            EXPECT_TRUE(primary.IsDXCAvailable(), "Default ShaderCompiler must locate in-process DXC");
            EXPECT_TRUE(primary.IsSpirVValidatorAvailable(), "Default ShaderCompiler must locate spirv-val");
            EXPECT_TRUE(!primary.GetDxcLibraryPath().empty());

            const ShaderCompilerVersion& ver = primary.GetCompilerVersion();
            EXPECT_TRUE(ver.Major >= 1);
            EXPECT_TRUE(!ver.DisplayString.empty());
            EXPECT_TRUE(ver.VersionHash != 0);
            std::cout << "  Detected DXC Version: " << ver.DisplayString << "\n";

            compilerCopy = primary;
        }

        // Primary destroyed; copied compiler must still keep DXC module alive and compile cleanly
        ShaderCompileRequest req{
            .SourceCode = "float4 VSMain(float3 p : POSITION) : SV_POSITION { return float4(p, 1.0); }",
            .VirtualFilePath = "LifetimeTest.hlsl",
            .EntryPoint = "VSMain",
            .Stage = ShaderStage::Vertex,
            .TargetFormat = ShaderTargetFormat::SPIRV
        };
        auto res = compilerCopy.Compile(req);
        EXPECT_TRUE(res.IsSuccess(), res ? "" : res.Error());
        if (res)
        {
            EXPECT_TRUE(HasSpirvMagic(res.Value().Bytecode));
            EXPECT_TRUE(res.Value().CompilerVersionHash == compilerCopy.GetCompilerVersion().VersionHash);
        }

        // Repeated construction/destruction stress
        for (int i = 0; i < 25; ++i)
        {
            ShaderCompiler transient;
            EXPECT_TRUE(transient.IsDXCAvailable());
        }
    }

    void Test_UnavailableDxcAndFailurePaths()
    {
        std::cout << "\n--- Test: Unavailable DXC & Compiler Failure Paths ---\n";

        ShaderCompiler broken(std::filesystem::path("C:/Morbi_NonExistent_DXC_Directory_12345"));
        EXPECT_TRUE(!broken.IsDXCAvailable(), "Explicit invalid DXC directory must report unavailable");

        ShaderCompileRequest validReq{
            .SourceCode = "float4 PSMain() : SV_TARGET { return float4(1, 0, 0, 1); }",
            .VirtualFilePath = "Valid.hlsl",
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::DXIL
        };
        auto unavailRes = broken.Compile(validReq);
        EXPECT_TRUE(!unavailRes.IsSuccess(), "Compile must fail cleanly when DXC is unavailable");
        EXPECT_TRUE(!unavailRes.Error().empty());

        ShaderCompiler compiler;
        EXPECT_TRUE(compiler.IsDXCAvailable());

        ShaderCompileRequest emptySrc = validReq;
        emptySrc.SourceCode.clear();
        EXPECT_TRUE(!compiler.Compile(emptySrc).IsSuccess());

        ShaderCompileRequest emptyEntry = validReq;
        emptyEntry.EntryPoint.clear();
        EXPECT_TRUE(!compiler.Compile(emptyEntry).IsSuccess());

        ShaderCompileRequest badSyntax{
            .SourceCode = "float4 PSMain() : SV_TARGET { THIS_IS_INVALID_HLSL(; }",
            .VirtualFilePath = "BrokenSyntax.hlsl",
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::DXIL
        };
        auto syntaxRes = compiler.Compile(badSyntax);
        EXPECT_TRUE(!syntaxRes.IsSuccess(), "Invalid HLSL syntax must fail compilation");
        EXPECT_TRUE(syntaxRes.Error().find("error") != std::string::npos || !syntaxRes.Error().empty());

        ShaderCompileRequest missingEntry{
            .SourceCode = "float4 OtherMain() : SV_TARGET { return float4(1,1,1,1); }",
            .VirtualFilePath = "MissingEntry.hlsl",
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::SPIRV
        };
        EXPECT_TRUE(!compiler.Compile(missingEntry).IsSuccess());

        // ValidateSpirVBytecode failure paths
        std::string valDiag;
        const std::array<u8, 8> badMagicBytes = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
        EXPECT_TRUE(!compiler.ValidateSpirVBytecode(badMagicBytes, valDiag));
        EXPECT_TRUE(!valDiag.empty());

        const std::array<u8, 20> badSpirv = {
            0x03, 0x02, 0x23, 0x07, // valid magic
            0x00, 0x06, 0x01, 0x00, // version 1.6
            0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0xFF, 0xFF  // invalid instruction/word
        };
        EXPECT_TRUE(!compiler.ValidateSpirVBytecode(badSpirv, valDiag));
        EXPECT_TRUE(!valDiag.empty());
    }

    void Test_ShaderModels60To66AndVulkan13Validation()
    {
        std::cout << "\n--- Test: Shader Models 6.0-6.6 (DXIL & Vulkan 1.3 SPIR-V + spirv-val) ---\n";

        ShaderCompiler compiler;
        EXPECT_TRUE(compiler.IsDXCAvailable());
        EXPECT_TRUE(compiler.IsSpirVValidatorAvailable());

        constexpr std::array<ShaderModel, 7> models = {
            ShaderModel::SM6_0,
            ShaderModel::SM6_1,
            ShaderModel::SM6_2,
            ShaderModel::SM6_3,
            ShaderModel::SM6_4,
            ShaderModel::SM6_5,
            ShaderModel::SM6_6
        };

        const std::string vsSource = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(float3 p : POSITION, float2 uv : TEXCOORD0)
{
    VSOut o;
    o.pos = float4(p, 1.0);
    o.uv = uv;
    return o;
}
)";
        const std::string psSource = R"(
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return float4(uv, 0.5, 1.0);
}
)";
        const std::string csSource = R"(
RWStructuredBuffer<uint> gOut : register(u0, space0);
[numthreads(8, 4, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    gOut[dtid.x] = dtid.x + dtid.y;
}
)";

        for (const ShaderModel sm : models)
        {
            for (const ShaderTargetFormat fmt : { ShaderTargetFormat::DXIL, ShaderTargetFormat::SPIRV })
            {
                ShaderCompileRequest vsReq{
                    .SourceCode = vsSource,
                    .VirtualFilePath = "SMMatrixVS.hlsl",
                    .EntryPoint = "VSMain",
                    .Stage = ShaderStage::Vertex,
                    .TargetFormat = fmt,
                    .EnableOptimization = true,
                    .GenerateDebugInfo = (sm == ShaderModel::SM6_6),
                    .Model = sm,
                    .ValidateSpirV = true
                };
                auto vsRes = compiler.Compile(vsReq);
                EXPECT_TRUE(vsRes.IsSuccess(), vsRes ? "" : vsRes.Error());
                if (vsRes)
                {
                    EXPECT_TRUE(fmt == ShaderTargetFormat::DXIL ? HasDxilMagic(vsRes.Value().Bytecode) : HasSpirvMagic(vsRes.Value().Bytecode));
                }

                ShaderCompileRequest psReq{
                    .SourceCode = psSource,
                    .VirtualFilePath = "SMMatrixPS.hlsl",
                    .EntryPoint = "PSMain",
                    .Stage = ShaderStage::Pixel,
                    .TargetFormat = fmt,
                    .Model = sm,
                    .ValidateSpirV = true
                };
                auto psRes = compiler.Compile(psReq);
                EXPECT_TRUE(psRes.IsSuccess(), psRes ? "" : psRes.Error());
                if (psRes)
                {
                    EXPECT_TRUE(fmt == ShaderTargetFormat::DXIL ? HasDxilMagic(psRes.Value().Bytecode) : HasSpirvMagic(psRes.Value().Bytecode));
                }

                ShaderCompileRequest csReq{
                    .SourceCode = csSource,
                    .VirtualFilePath = "SMMatrixCS.hlsl",
                    .EntryPoint = "CSMain",
                    .Stage = ShaderStage::Compute,
                    .TargetFormat = fmt,
                    .Model = sm,
                    .ValidateSpirV = true
                };
                auto csRes = compiler.Compile(csReq);
                EXPECT_TRUE(csRes.IsSuccess(), csRes ? "" : csRes.Error());
                if (csRes)
                {
                    EXPECT_TRUE(fmt == ShaderTargetFormat::DXIL ? HasDxilMagic(csRes.Value().Bytecode) : HasSpirvMagic(csRes.Value().Bytecode));
                }
            }
        }
    }

    void Test_IncludeHandlerTransitiveDiamondMissingAndCycle()
    {
        std::cout << "\n--- Test: Custom IDxcIncludeHandler (Canonical, Transitive, Diamond, Missing, Cycle) ---\n";

        ScopedTempDir temp("includes");
        const auto incRoot = temp.Path / "IncludeRoot";
        const auto subDir = incRoot / "Sub";
        const auto shaderFile = temp.Path / "Shaders" / "Main.hlsl";

        WriteTextFile(subDir / "Leaf.hlsli", "static const float kLeafVal = 0.75;\n");
        WriteTextFile(incRoot / "Mid.hlsli", "#include \"./Sub/../Sub/Leaf.hlsli\"\nfloat GetMid() { return kLeafVal; }\n");
        WriteTextFile(shaderFile, R"(
#include "Mid.hlsli"
float4 PSMain() : SV_TARGET
{
    return float4(GetMid(), 0.25, 0.5, 1.0);
}
)");

        ShaderCompiler compiler;
        ShaderCompileRequest req{
            .SourceCode = R"(
#include "Mid.hlsli"
float4 PSMain() : SV_TARGET
{
    return float4(GetMid(), 0.25, 0.5, 1.0);
}
)",
            .VirtualFilePath = shaderFile.string(),
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::SPIRV,
            .IncludeDirs = { incRoot }
        };

        auto res = compiler.Compile(req);
        EXPECT_TRUE(res.IsSuccess(), res ? "" : res.Error());
        if (res)
        {
            const auto& deps = res.Value().IncludeDependencies;
            EXPECT_TRUE(deps.size() == 2, "Transitive include count must be 2 (Mid.hlsli & Leaf.hlsli)");
            bool foundMid = false;
            bool foundLeaf = false;
            for (const auto& d : deps)
            {
                if (d.CanonicalPath.find("Mid.hlsli") != std::string::npos) foundMid = true;
                if (d.CanonicalPath.find("Leaf.hlsli") != std::string::npos)
                {
                    foundLeaf = true;
                    EXPECT_TRUE(d.CanonicalPath.find("..") == std::string::npos, "Canonical path must not contain '..'");
                }
                EXPECT_TRUE(d.ContentHash != 0);
                EXPECT_TRUE(d.ByteSize > 0);
            }
            EXPECT_TRUE(foundMid && foundLeaf);
        }

        // Nested subdirectory relative include: Nested/Parent.hlsli -> #include "Child.hlsli"
        WriteTextFile(incRoot / "Nested" / "Child.hlsli", "static const float kNestedChild = 0.42;\n");
        WriteTextFile(incRoot / "Nested" / "Parent.hlsli", "#include \"Child.hlsli\"\nfloat GetNested() { return kNestedChild; }\n");
        ShaderCompileRequest nestedReq{
            .SourceCode = "#include \"Nested/Parent.hlsli\"\nfloat4 PSMain() : SV_TARGET { return float4(GetNested(), 0, 0, 1); }\n",
            .VirtualFilePath = "InlineNested.hlsl",
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::SPIRV,
            .IncludeDirs = { incRoot }
        };
        auto nestedRes = compiler.Compile(nestedReq);
        EXPECT_TRUE(nestedRes.IsSuccess(), nestedRes ? "" : nestedRes.Error());
        if (nestedRes)
        {
            EXPECT_TRUE(nestedRes.Value().IncludeDependencies.size() == 2);
        }

        // Diamond include with #ifndef guard
        WriteTextFile(incRoot / "CommonGuard.hlsli", "#ifndef COMMON_GUARD_H\n#define COMMON_GUARD_H\nstatic const float kCommon = 1.0;\n#endif\n");
        WriteTextFile(incRoot / "Left.hlsli", "#include \"CommonGuard.hlsli\"\nfloat LeftFn() { return kCommon; }\n");
        WriteTextFile(incRoot / "Right.hlsli", "#include \"CommonGuard.hlsli\"\nfloat RightFn() { return kCommon * 2.0; }\n");
        ShaderCompileRequest diamondReq{
            .SourceCode = "#include \"Left.hlsli\"\n#include \"Right.hlsli\"\nfloat4 PSMain() : SV_TARGET { return float4(LeftFn(), RightFn(), 0, 1); }\n",
            .VirtualFilePath = (temp.Path / "Shaders" / "Diamond.hlsl").string(),
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::DXIL,
            .IncludeDirs = { incRoot }
        };
        auto diamondRes = compiler.Compile(diamondReq);
        EXPECT_TRUE(diamondRes.IsSuccess(), diamondRes ? "" : diamondRes.Error());
        if (diamondRes)
        {
            EXPECT_TRUE(diamondRes.Value().IncludeDependencies.size() == 3);
        }

        // Missing include
        ShaderCompileRequest missingIncReq{
            .SourceCode = "#include \"NonExistentHeader_XYZ.hlsli\"\nfloat4 PSMain() : SV_TARGET { return float4(1,1,1,1); }\n",
            .VirtualFilePath = "MissingInc.hlsl",
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::DXIL,
            .IncludeDirs = { incRoot }
        };
        auto missingRes = compiler.Compile(missingIncReq);
        EXPECT_TRUE(!missingRes.IsSuccess(), "Missing #include must fail");

        // Circular include without guards
        WriteTextFile(incRoot / "CycleA.hlsli", "#include \"CycleB.hlsli\"\nfloat A() { return 1.0; }\n");
        WriteTextFile(incRoot / "CycleB.hlsli", "#include \"CycleA.hlsli\"\nfloat B() { return 2.0; }\n");
        ShaderCompileRequest cycleReq{
            .SourceCode = "#include \"CycleA.hlsli\"\nfloat4 PSMain() : SV_TARGET { return float4(1,1,1,1); }\n",
            .VirtualFilePath = "CycleMain.hlsl",
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::DXIL,
            .IncludeDirs = { incRoot }
        };
        auto cycleRes = compiler.Compile(cycleReq);
        EXPECT_TRUE(!cycleRes.IsSuccess(), "Circular #include must fail cleanly");
        EXPECT_TRUE(cycleRes.Error().find("Circular") != std::string::npos ||
                    cycleRes.Error().find("nested") != std::string::npos,
            cycleRes.Error());
    }

    void Test_HashingAndShaderCacheV2()
    {
        std::cout << "\n--- Test: Request Hashing & ShaderCache v2 (Invalidation, Corruption Recovery, Atomic Write) ---\n";

        ScopedTempDir temp("cache");
        const auto incDir = temp.Path / "Includes";
        const auto cacheDir = temp.Path / "Cache";
        const auto leafHeader = incDir / "LeafParam.hlsli";
        const auto midHeader = incDir / "MidParam.hlsli";

        WriteTextFile(leafHeader, "static const float kScale = 0.25;\n");
        WriteTextFile(midHeader, "#include \"LeafParam.hlsli\"\nfloat GetScale() { return kScale; }\n");

        ShaderCompiler compiler;
        ShaderCompileRequest baseReq{
            .SourceCode = "#include \"MidParam.hlsli\"\nfloat4 PSMain() : SV_TARGET { return float4(GetScale(), 0.0, 0.0, 1.0); }\n",
            .VirtualFilePath = (temp.Path / "TestCache.hlsl").string(),
            .EntryPoint = "PSMain",
            .Stage = ShaderStage::Pixel,
            .TargetFormat = ShaderTargetFormat::SPIRV,
            .EnableOptimization = true,
            .GenerateDebugInfo = false,
            .Defines = { { "TEST_FLAG", "1" } },
            .IncludeDirs = { incDir },
            .Model = ShaderModel::SM6_0
        };

        // Verify ComputeRequestHash sensitivity to every parameter
        const u64 hBase = ShaderCompiler::ComputeRequestHash(baseReq, &compiler.GetCompilerVersion());
        {
            auto r = baseReq; r.VirtualFilePath = "OtherPath.hlsl";
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(r, &compiler.GetCompilerVersion()) != hBase);
        }
        {
            auto r = baseReq; r.GenerateDebugInfo = true;
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(r, &compiler.GetCompilerVersion()) != hBase);
        }
        {
            auto r = baseReq; r.EnableOptimization = false;
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(r, &compiler.GetCompilerVersion()) != hBase);
        }
        {
            auto r = baseReq; r.Model = ShaderModel::SM6_6;
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(r, &compiler.GetCompilerVersion()) != hBase);
        }
        {
            auto r = baseReq; r.Defines[0].Value = "2";
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(r, &compiler.GetCompilerVersion()) != hBase);
        }
        {
            auto r = baseReq; r.IncludeDirs.push_back(temp.Path / "ExtraInc");
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(r, &compiler.GetCompilerVersion()) != hBase);
        }
        {
            ShaderCompilerVersion fakeVer = compiler.GetCompilerVersion();
            fakeVer.Major += 1;
            EXPECT_TRUE(ShaderCompiler::ComputeRequestHash(baseReq, &fakeVer) != hBase);
        }

        // 1. Cold compile -> memory hit -> disk hit
        std::vector<u8> originalBytes;
        {
            ShaderCache cache(cacheDir, compiler);
            auto r1 = cache.GetOrCompile(baseReq);
            EXPECT_TRUE(r1.IsSuccess(), r1 ? "" : r1.Error());
            EXPECT_TRUE(cache.GetStats().Compilations == 1);
            if (r1)
            {
                originalBytes = r1.Value().Bytecode;
            }

            auto r2 = cache.GetOrCompile(baseReq);
            EXPECT_TRUE(r2.IsSuccess());
            EXPECT_TRUE(cache.GetStats().MemoryHits == 1);
        }

        {
            ShaderCache diskCache(cacheDir, compiler);
            auto r3 = diskCache.GetOrCompile(baseReq);
            EXPECT_TRUE(r3.IsSuccess());
            EXPECT_TRUE(diskCache.GetStats().DiskHits == 1);
            EXPECT_TRUE(diskCache.GetStats().Compilations == 0);
            if (r3)
            {
                EXPECT_TRUE(r3.Value().Bytecode == originalBytes);
            }

            // 2. Transitive include modification invalidates BOTH memory cache and disk cache!
            WriteTextFile(leafHeader, "static const float kScale = 0.875;\n");

            auto r4 = diskCache.GetOrCompile(baseReq);
            EXPECT_TRUE(r4.IsSuccess(), r4 ? "" : r4.Error());
            EXPECT_TRUE(diskCache.GetStats().Invalidations == 1, "Memory hit must invalidate on modified transitive include");
            EXPECT_TRUE(diskCache.GetStats().Compilations == 1);
            if (r4)
            {
                EXPECT_TRUE(r4.Value().Bytecode != originalBytes, "Updated include must change compiled bytecode");
            }
        }

        // 3. Disk-only transitive include invalidation on fresh ShaderCache
        WriteTextFile(leafHeader, "static const float kScale = 0.125;\n");
        {
            ShaderCache freshCache(cacheDir, compiler);
            auto r5 = freshCache.GetOrCompile(baseReq);
            EXPECT_TRUE(r5.IsSuccess(), r5 ? "" : r5.Error());
            EXPECT_TRUE(freshCache.GetStats().Invalidations == 1, "Disk hit must invalidate on modified transitive include");
            EXPECT_TRUE(freshCache.GetStats().Compilations == 1);
        }

        // 4. Corrupt cache recovery (CRC mismatch & truncation)
        {
            ShaderCache corruptTestCache(cacheDir, compiler);
            const std::filesystem::path cacheFile = corruptTestCache.GetCacheFilePathForRequest(baseReq);
            EXPECT_TRUE(std::filesystem::exists(cacheFile));

            // Flip a byte near the end (CRC mismatch)
            {
                std::fstream io(cacheFile, std::ios::binary | std::ios::in | std::ios::out);
                io.seekg(0, std::ios::end);
                const std::streamoff sz = io.tellg();
                EXPECT_TRUE(sz > 48);
                io.seekg(sz - 4, std::ios::beg);
                char b = 0;
                io.read(&b, 1);
                b ^= static_cast<char>(0xFF);
                io.seekp(sz - 4, std::ios::beg);
                io.write(&b, 1);
            }

            auto rec1 = corruptTestCache.GetOrCompile(baseReq);
            EXPECT_TRUE(rec1.IsSuccess(), rec1 ? "" : rec1.Error());
            EXPECT_TRUE(corruptTestCache.GetStats().CorruptRecoveries == 1);
            EXPECT_TRUE(corruptTestCache.GetStats().Compilations == 1);

            // Verify repaired cache now hits disk cleanly on a new ShaderCache
            ShaderCache verifyRepaired(cacheDir, compiler);
            auto hitAfterRepair = verifyRepaired.GetOrCompile(baseReq);
            EXPECT_TRUE(hitAfterRepair.IsSuccess());
            EXPECT_TRUE(verifyRepaired.GetStats().DiskHits == 1);

            // Truncated cache file recovery
            WriteTextFile(cacheFile, "MSHC_CORRUPT_TRUNCATED");
            ShaderCache truncCache(cacheDir, compiler);
            auto rec2 = truncCache.GetOrCompile(baseReq);
            EXPECT_TRUE(rec2.IsSuccess());
            EXPECT_TRUE(truncCache.GetStats().CorruptRecoveries == 1);
            EXPECT_TRUE(truncCache.GetStats().Compilations == 1);
        }
    }

    void Test_ConcurrentCompilationAndCacheStress()
    {
        std::cout << "\n--- Test: Concurrent Parallel Shader Compilation & Cache Stress ---\n";

        ScopedTempDir temp("concurrent");
        const auto incDir = temp.Path / "Inc";
        const auto cacheDir = temp.Path / "Cache";
        WriteTextFile(incDir / "SharedMath.hlsli", "float3 ScaleColor(float3 c, float s) { return c * s; }\n");

        ShaderCompiler compiler;
        ShaderCache sharedCache(cacheDir, compiler);

        constexpr int kNumThreads = 8;
        constexpr int kTasksPerThread = 6;
        std::atomic<int> failures{ 0 };
        std::vector<std::thread> threads;
        threads.reserve(kNumThreads);

        for (int t = 0; t < kNumThreads; ++t)
        {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < kTasksPerThread; ++i)
                {
                    const int variantId = (t * kTasksPerThread + i) % 12; // intentional contention on shared cache keys
                    const ShaderTargetFormat fmt = (i % 2 == 0) ? ShaderTargetFormat::SPIRV : ShaderTargetFormat::DXIL;
                    const ShaderModel sm = (i % 3 == 0) ? ShaderModel::SM6_6 : ShaderModel::SM6_0;

                    ShaderCompileRequest req{
                        .SourceCode = "#include \"SharedMath.hlsli\"\n"
                                      "float4 PSMain() : SV_TARGET { return float4(ScaleColor(float3(1, 0.5, 0.25), float(VARIANT_VAL) * 0.1), 1.0); }\n",
                        .VirtualFilePath = (temp.Path / ("Concurrent_" + std::to_string(variantId) + ".hlsl")).string(),
                        .EntryPoint = "PSMain",
                        .Stage = ShaderStage::Pixel,
                        .TargetFormat = fmt,
                        .Defines = { { "VARIANT_VAL", std::to_string(variantId + 1) } },
                        .IncludeDirs = { incDir },
                        .Model = sm,
                        .ValidateSpirV = true
                    };

                    auto directRes = compiler.Compile(req);
                    if (!directRes)
                    {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    const bool validMagic = (fmt == ShaderTargetFormat::DXIL)
                        ? HasDxilMagic(directRes.Value().Bytecode)
                        : HasSpirvMagic(directRes.Value().Bytecode);
                    if (!validMagic)
                    {
                        failures.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    auto cachedRes = sharedCache.GetOrCompile(req);
                    if (!cachedRes || cachedRes.Value().Bytecode != directRes.Value().Bytecode)
                    {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& th : threads)
        {
            th.join();
        }

        EXPECT_TRUE(failures.load() == 0, "All concurrent direct + cached compilations must succeed cleanly");
    }
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "MorbiEngine Phase 2 WP1 Shader Tests\n";
    std::cout << "========================================\n";

    Test_DxcInitVersionAndLifetime();
    Test_UnavailableDxcAndFailurePaths();
    Test_ShaderModels60To66AndVulkan13Validation();
    Test_IncludeHandlerTransitiveDiamondMissingAndCycle();
    Test_HashingAndShaderCacheV2();
    Test_ConcurrentCompilationAndCacheStress();

    std::cout << "\n========================================\n";
    std::cout << "TOTAL: " << g_passedAssertions << "/" << (g_passedAssertions + g_failedAssertions)
              << " assertions passed\n";
    std::cout << "========================================\n";

    return (g_failedAssertions == 0) ? 0 : 1;
}
