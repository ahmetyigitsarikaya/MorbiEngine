#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Morbi/Shader/ShaderCache.hpp"
#include "Morbi/Shader/ShaderCompiler.hpp"
#include "Morbi/Shader/ShaderReflection.hpp"

using namespace Morbi;
using namespace Morbi::Shader;

namespace
{
    int g_passedAssertions = 0;
    int g_failedAssertions = 0;

    void ExpectTrue(bool cond, const char* expr, int line, const std::string& detail = {})
    {
        if (cond)
        {
            ++g_passedAssertions;
            return;
        }
        ++g_failedAssertions;
        std::cerr << "[FAIL] line " << line << ": " << expr;
        if (!detail.empty())
        {
            std::cerr << " (" << detail << ")";
        }
        std::cerr << "\n";
    }

#define EXPECT_TRUE(cond, ...) ExpectTrue((cond), #cond, __LINE__, ##__VA_ARGS__)

    struct ScopedTempDir final
    {
        std::filesystem::path Path;

        explicit ScopedTempDir(const std::string& tag)
        {
            std::random_device rd;
            const auto nonce = (static_cast<u64>(rd()) << 32) ^ static_cast<u64>(rd());
            Path = std::filesystem::temp_directory_path() / ("morbi_refl_" + tag + "_" + std::to_string(nonce));
            std::error_code ec;
            std::filesystem::create_directories(Path, ec);
        }

        ~ScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }
    };

    const ReflectedBinding* FindBindingByName(const ShaderReflectionData& refl, std::string_view name)
    {
        for (const auto& b : refl.Bindings)
        {
            if (b.Name == name)
            {
                return &b;
            }
        }
        return nullptr;
    }

    const ReflectedConstantBuffer* FindConstantBufferByName(const ShaderReflectionData& refl, std::string_view name)
    {
        for (const auto& cb : refl.ConstantBuffers)
        {
            if (cb.Name == name)
            {
                return &cb;
            }
        }
        return nullptr;
    }

    const ReflectedShaderMember* FindMemberByName(
        const std::vector<ReflectedShaderMember>& members,
        std::string_view name)
    {
        for (const auto& m : members)
        {
            if (m.Name == name)
            {
                return &m;
            }
        }
        return nullptr;
    }

    bool HasIssueWithCode(const ReflectionValidationResult& res, std::string_view code)
    {
        for (const auto& issue : res.Issues)
        {
            if (issue.Code == code)
            {
                return true;
            }
        }
        return false;
    }

    constexpr const char* kNestedCbAndResourcesHlsl = R"(
struct SubMaterial
{
    float4 Tint;
    float  Roughness;
    uint   Flags;
};

cbuffer SceneGlobals : register(b0, space0)
{
    float4x4    ViewProj;
    SubMaterial PrimaryMat;
    float4      ExtraWeights[3];
    uint        FrameIndex;
};

Texture2D<float4>        g_AlbedoTex  : register(t0, space0);
Texture2DArray<float4>   g_ShadowMaps : register(t1, space0);
SamplerComparisonState   g_ShadowCmp  : register(s0, space0);
SamplerState             g_LinearSmp  : register(s1, space0);
StructuredBuffer<float4> g_Lights     : register(t2, space0);
RWTexture2D<float4>      g_OutputImg  : register(u0, space0);
RWStructuredBuffer<uint> g_CounterBuf : register(u1, space0);

[numthreads(8, 8, 4)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    float4 sampleCol = g_AlbedoTex.Load(int3(dtid.xy, 0));
    float4 arrayCol  = g_ShadowMaps.SampleLevel(g_LinearSmp, float3(0.5, 0.5, 0.0), 0.0);
    float  cmpVal    = g_ShadowMaps.SampleCmpLevelZero(g_ShadowCmp, float3(0.5, 0.5, 0.0), 0.5);
    float4 lightData = g_Lights[FrameIndex];
    float4 combined  = mul(float4(sampleCol.xyz, 1.0), ViewProj) + PrimaryMat.Tint + ExtraWeights[dtid.x % 3] + arrayCol + lightData + cmpVal;
    g_OutputImg[dtid.xy] = combined;
    InterlockedAdd(g_CounterBuf[0], 1u + PrimaryMat.Flags);
}
)";

    constexpr const char* kMorbiPbrHlsl = R"(
struct MeshPushConstants
{
    float4 MvpRow0;
    float4 MvpRow1;
    float4 MvpRow2;
    float4 MvpRow3;
    float4 ModelRow0;
    float4 ModelRow1;
    float4 ModelRow2;
    uint   AlbedoTexIndex;
    uint   NormalTexIndex;
    uint   SpecularTexIndex;
    uint   PackedRoughMetal;
};

struct PerObjectBoneData
{
    row_major float4x4 BoneMatrices[100];
};

#if defined(MORBI_VULKAN)
[[vk::push_constant]] MeshPushConstants PushConstants;
[[vk::binding(1, 0)]] Texture2D<float4> g_BindlessTextures[] : register(t0, space1);
[[vk::binding(2, 0)]] SamplerState      g_LinearSampler      : register(s0, space0);
[[vk::binding(3, 0)]] ConstantBuffer<PerObjectBoneData> g_BoneBuffers[] : register(b1, space0);
#else
cbuffer RootConstantsBuffer : register(b0, space0)
{
    MeshPushConstants PushConstants;
};
Texture2D<float4> g_BindlessTextures[] : register(t0, space1);
SamplerState      g_LinearSampler      : register(s0, space0);
cbuffer PerObjectBuffer : register(b1, space0)
{
    row_major float4x4 BoneMatrices[100];
};
#endif

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float2 UV           : TEXCOORD0;
    float4 Tangent      : TANGENT;
    float3 Bitangent    : BINORMAL;
    int4   BoneIDs      : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 Normal    : NORMAL;
    float2 UV        : TEXCOORD0;
    float3 Tangent   : TANGENT;
    float3 Bitangent : BINORMAL;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float3 localPos = input.Position;
    float3 localNorm = input.Normal;
    float3 localTan = input.Tangent.xyz;
    float3 localBit = input.Bitangent;

#if defined(MORBI_VULKAN)
    uint boneBufIdx = (PushConstants.PackedRoughMetal >> 16u) & 0xFFFFu;
    if (boneBufIdx != 0xFFFFu && input.BoneIDs[0] >= 0 && (input.BoneWeights[0] + input.BoneWeights[1] + input.BoneWeights[2] + input.BoneWeights[3]) > 0.0001)
#else
    if (input.BoneIDs[0] >= 0 && (input.BoneWeights[0] + input.BoneWeights[1] + input.BoneWeights[2] + input.BoneWeights[3]) > 0.0001)
#endif
    {
        row_major float4x4 skinMatrix = (row_major float4x4)0;
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            int boneId = input.BoneIDs[i];
            float weight = input.BoneWeights[i];
            if (boneId >= 0 && boneId < 100 && weight > 0.0)
            {
#if defined(MORBI_VULKAN)
                skinMatrix += g_BoneBuffers[boneBufIdx].BoneMatrices[boneId] * weight;
#else
                skinMatrix += BoneMatrices[boneId] * weight;
#endif
            }
        }
        localPos = mul(float4(input.Position, 1.0), skinMatrix).xyz;
        localNorm = mul(float4(input.Normal, 0.0), skinMatrix).xyz;
        localTan = mul(float4(input.Tangent.xyz, 0.0), skinMatrix).xyz;
        localBit = mul(float4(input.Bitangent, 0.0), skinMatrix).xyz;
    }

    o.Position = localPos.x * PushConstants.MvpRow0
               + localPos.y * PushConstants.MvpRow1
               + localPos.z * PushConstants.MvpRow2
               + PushConstants.MvpRow3;
    o.Normal = normalize(localNorm);
    o.Tangent = normalize(localTan);
    o.Bitangent = normalize(localBit);
    o.UV = input.UV;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 nrm = g_BindlessTextures[PushConstants.NormalTexIndex].Sample(g_LinearSampler, input.UV).xyz;
    return float4(normalize(input.Normal + input.Tangent + input.Bitangent + nrm), 1.0);
}
)";

    // Test 1, 2, 5: ConstantBuffer nested struct/matrix/array, Resources, Dimensions, Compute ThreadGroup
    void Test_ConstantBuffersResourcesAndCompute(const ShaderCompiler& compiler)
    {
        std::cout << "\n--- Test 1/2/5: CB Layouts, Resource Bindings, Dimensions & Compute Threads ---\n";

        for (ShaderTargetFormat fmt : { ShaderTargetFormat::SPIRV, ShaderTargetFormat::DXIL })
        {
            ShaderCompileRequest req{};
            req.SourceCode = kNestedCbAndResourcesHlsl;
            req.VirtualFilePath = "NestedCbCompute.hlsl";
            req.EntryPoint = "CSMain";
            req.Stage = ShaderStage::Compute;
            req.TargetFormat = fmt;
            req.Model = ShaderModel::SM6_2;

            auto res = compiler.Compile(req);
            EXPECT_TRUE(res.IsSuccess(), res ? "" : res.Error());
            if (!res)
            {
                continue;
            }

            const ShaderReflectionData& refl = res.Value().Reflection;
            EXPECT_TRUE(refl.IsValid);
            EXPECT_TRUE(refl.TargetFormat == fmt);
            EXPECT_TRUE(refl.Stage == ShaderStage::Compute);
            EXPECT_TRUE(refl.EntryPoint == "CSMain");
            EXPECT_TRUE(refl.ThreadGroupSizeX == 8u);
            EXPECT_TRUE(refl.ThreadGroupSizeY == 8u);
            EXPECT_TRUE(refl.ThreadGroupSizeZ == 4u);

            const auto* cbBinding = FindBindingByName(refl, "SceneGlobals");
            EXPECT_TRUE(cbBinding != nullptr);
            if (cbBinding)
            {
                EXPECT_TRUE(cbBinding->Type == ResourceBindingType::ConstantBuffer);
                EXPECT_TRUE(cbBinding->Kind == ReflectedResourceKind::UniformBuffer);
                EXPECT_TRUE(cbBinding->ConstantBufferIndex.has_value());
                if (fmt == ShaderTargetFormat::SPIRV)
                {
                    EXPECT_TRUE(cbBinding->DescriptorSet == 0u && cbBinding->Binding == 0u);
                    EXPECT_TRUE(!cbBinding->DxilRegister.has_value() && !cbBinding->DxilSpace.has_value());
                }
                else
                {
                    EXPECT_TRUE(cbBinding->DxilRegister == 0u && cbBinding->DxilSpace == 0u);
                    EXPECT_TRUE(!cbBinding->DescriptorSet.has_value() && !cbBinding->Binding.has_value());
                }
            }

            const auto* cb = FindConstantBufferByName(refl, "SceneGlobals");
            EXPECT_TRUE(cb != nullptr);
            if (cb)
            {
                EXPECT_TRUE(cb->SizeBytes == 160u, "SceneGlobals should be 160B, got " + std::to_string(cb->SizeBytes));

                const auto* vp = FindMemberByName(cb->Members, "ViewProj");
                EXPECT_TRUE(vp != nullptr);
                if (vp)
                {
                    EXPECT_TRUE(vp->OffsetBytes == 0u);
                    EXPECT_TRUE(vp->SizeBytes == 64u);
                    EXPECT_TRUE(vp->Rows == 4u && vp->Columns == 4u);
                    EXPECT_TRUE(vp->ScalarType == ReflectedScalarType::Float32);
                }

                const auto* mat = FindMemberByName(cb->Members, "PrimaryMat");
                EXPECT_TRUE(mat != nullptr);
                if (mat)
                {
                    EXPECT_TRUE(mat->OffsetBytes == 64u);
                    EXPECT_TRUE(mat->Class == ReflectedVariableClass::Struct);
                    EXPECT_TRUE(mat->Members.size() == 3u);
                    const auto* roughness = FindMemberByName(mat->Members, "Roughness");
                    const auto* flags = FindMemberByName(mat->Members, "Flags");
                    EXPECT_TRUE(roughness != nullptr && roughness->OffsetBytes == 16u && roughness->ScalarType == ReflectedScalarType::Float32);
                    EXPECT_TRUE(flags != nullptr && flags->OffsetBytes == 20u && flags->ScalarType == ReflectedScalarType::UInt32);
                }

                const auto* weights = FindMemberByName(cb->Members, "ExtraWeights");
                EXPECT_TRUE(weights != nullptr);
                if (weights)
                {
                    EXPECT_TRUE(weights->OffsetBytes == 96u);
                    EXPECT_TRUE(weights->ArrayElementCount == 3u);
                }

                const auto* frameIdx = FindMemberByName(cb->Members, "FrameIndex");
                EXPECT_TRUE(frameIdx != nullptr && frameIdx->OffsetBytes == 144u);
            }

            const auto* tex2d = FindBindingByName(refl, "g_AlbedoTex");
            EXPECT_TRUE(tex2d != nullptr && tex2d->Kind == ReflectedResourceKind::SampledTexture && tex2d->Dimension == ReflectedResourceDimension::Texture2D);

            const auto* texArr = FindBindingByName(refl, "g_ShadowMaps");
            EXPECT_TRUE(texArr != nullptr && texArr->Kind == ReflectedResourceKind::SampledTexture && texArr->Dimension == ReflectedResourceDimension::Texture2DArray);

            const auto* lights = FindBindingByName(refl, "g_Lights");
            EXPECT_TRUE(lights != nullptr && lights->Kind == ReflectedResourceKind::StorageBuffer && lights->IsReadOnly);

            const auto* uavTex = FindBindingByName(refl, "g_OutputImg");
            EXPECT_TRUE(uavTex != nullptr && uavTex->Kind == ReflectedResourceKind::StorageTexture && !uavTex->IsReadOnly && uavTex->Dimension == ReflectedResourceDimension::Texture2D);

            const auto* uavBuf = FindBindingByName(refl, "g_CounterBuf");
            EXPECT_TRUE(uavBuf != nullptr && uavBuf->Kind == ReflectedResourceKind::RWStorageBuffer && !uavBuf->IsReadOnly);

            const auto* smp = FindBindingByName(refl, "g_LinearSmp");
            EXPECT_TRUE(smp != nullptr && smp->Kind == ReflectedResourceKind::Sampler);
        }

        // Verify strict 1:1 cross-backend compatibility passes for matching CS
        ShaderCompileRequest reqSpv{};
        reqSpv.SourceCode = kNestedCbAndResourcesHlsl;
        reqSpv.VirtualFilePath = "NestedCbCompute.hlsl";
        reqSpv.EntryPoint = "CSMain";
        reqSpv.Stage = ShaderStage::Compute;
        reqSpv.TargetFormat = ShaderTargetFormat::SPIRV;
        reqSpv.Model = ShaderModel::SM6_2;

        auto reqDxil = reqSpv;
        reqDxil.TargetFormat = ShaderTargetFormat::DXIL;

        auto spvRes = compiler.Compile(reqSpv);
        auto dxilRes = compiler.Compile(reqDxil);
        EXPECT_TRUE(spvRes && dxilRes);
        if (spvRes && dxilRes)
        {
            const auto val = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                spvRes.Value().Reflection,
                dxilRes.Value().Reflection,
                CrossBackendBindingPolicy::StrictOneToOne());
            EXPECT_TRUE(val.IsCompatible, val.Issues.empty() ? "" : val.Issues[0].Message);
        }
    }

    // Test 3, 4, 9: Real Morbi PBR shader Push/Root Constants, Vertex Inputs & MorbiBindless Validation
    void Test_MorbiPbrShaderAndVertexInputs(const ShaderCompiler& compiler)
    {
        std::cout << "\n--- Test 3/4/9: Real Morbi PBR Shader (VS+PS SPIR-V vs DXIL + Vertex Inputs) ---\n";

        auto compileStage = [&](ShaderStage stage, std::string_view entry, ShaderTargetFormat fmt) {
            ShaderCompileRequest req{};
            req.SourceCode = kMorbiPbrHlsl;
            req.VirtualFilePath = "MorbiScenePBR.hlsl";
            req.EntryPoint = std::string(entry);
            req.Stage = stage;
            req.TargetFormat = fmt;
            req.Model = ShaderModel::SM6_2;
            if (fmt == ShaderTargetFormat::SPIRV)
            {
                req.Defines.push_back(ShaderMacro{ "MORBI_VULKAN", "1" });
            }
            return compiler.Compile(req);
        };

        auto vsSpv  = compileStage(ShaderStage::Vertex, "VSMain", ShaderTargetFormat::SPIRV);
        auto psSpv  = compileStage(ShaderStage::Pixel,  "PSMain", ShaderTargetFormat::SPIRV);
        auto vsDxil = compileStage(ShaderStage::Vertex, "VSMain", ShaderTargetFormat::DXIL);
        auto psDxil = compileStage(ShaderStage::Pixel,  "PSMain", ShaderTargetFormat::DXIL);

        EXPECT_TRUE(vsSpv.IsSuccess(),  vsSpv  ? "" : vsSpv.Error());
        EXPECT_TRUE(psSpv.IsSuccess(),  psSpv  ? "" : psSpv.Error());
        EXPECT_TRUE(vsDxil.IsSuccess(), vsDxil ? "" : vsDxil.Error());
        EXPECT_TRUE(psDxil.IsSuccess(), psDxil ? "" : psDxil.Error());
        if (!vsSpv || !psSpv || !vsDxil || !psDxil)
        {
            return;
        }

        // Check Vertex inputs reflect all 7 PBR mesh attributes including int4 BLENDINDICES
        EXPECT_TRUE(vsSpv.Value().Reflection.StageInputs.size() == 7u);
        EXPECT_TRUE(vsDxil.Value().Reflection.StageInputs.size() == 7u);

        bool foundInt4Spv = false;
        for (const auto& in : vsSpv.Value().Reflection.StageInputs)
        {
            if (in.ComponentType == ReflectedScalarType::Int32 && in.ComponentCount == 4u)
            {
                foundInt4Spv = true;
            }
        }
        bool foundInt4Dxil = false;
        for (const auto& in : vsDxil.Value().Reflection.StageInputs)
        {
            if (in.SemanticName == "BLENDINDICES" && in.ComponentType == ReflectedScalarType::Int32 && in.ComponentCount == 4u)
            {
                foundInt4Dxil = true;
            }
        }
        EXPECT_TRUE(foundInt4Spv, "SPIR-V VS must reflect int4 BLENDINDICES");
        EXPECT_TRUE(foundInt4Dxil, "DXIL VS must reflect int4 BLENDINDICES");

        // Check Push Constants on SPIR-V VS+PS (128 bytes)
        EXPECT_TRUE(vsSpv.Value().Reflection.PushConstants.size() == 1u);
        EXPECT_TRUE(vsSpv.Value().Reflection.PushConstantSizeBytes == 128u);
        EXPECT_TRUE(psSpv.Value().Reflection.PushConstantSizeBytes == 128u);

        // Check bindless detection
        EXPECT_TRUE(vsSpv.Value().Reflection.UsesBindlessHeap);
        EXPECT_TRUE(psSpv.Value().Reflection.UsesBindlessHeap);
        EXPECT_TRUE(psDxil.Value().Reflection.UsesBindlessHeap);

        // Cross-backend validation on StrictOneToOne MUST FAIL (Push Constants & UBO array mismatch!)
        {
            const auto strictVs = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                vsSpv.Value().Reflection, vsDxil.Value().Reflection, CrossBackendBindingPolicy::StrictOneToOne());
            EXPECT_TRUE(!strictVs.IsCompatible, "StrictOneToOne must reject Vulkan PushConstants vs DX12 Root CBV + UBO array");

            const auto strictPs = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                psSpv.Value().Reflection, psDxil.Value().Reflection, CrossBackendBindingPolicy::StrictOneToOne());
            EXPECT_TRUE(!strictPs.IsCompatible, "StrictOneToOne must reject MorbiBindless explicit remappings");
        }

        // Cross-backend validation with explicit MorbiBindless policy MUST PASS for both VSMain and PSMain
        {
            const auto bindlessVs = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                vsSpv.Value().Reflection, vsDxil.Value().Reflection, CrossBackendBindingPolicy::MorbiBindless());
            EXPECT_TRUE(bindlessVs.IsCompatible, bindlessVs.Issues.empty() ? "" : bindlessVs.Issues[0].Message);

            const auto bindlessPs = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                psSpv.Value().Reflection, psDxil.Value().Reflection, CrossBackendBindingPolicy::MorbiBindless());
            EXPECT_TRUE(bindlessPs.IsCompatible, bindlessPs.Issues.empty() ? "" : bindlessPs.Issues[0].Message);
        }

        // Stage Interface Validation (VS -> PS) on BOTH backends
        {
            const auto stageSpv = ShaderReflectionValidator::ValidateStageInterface(
                vsSpv.Value().Reflection, psSpv.Value().Reflection);
            EXPECT_TRUE(stageSpv.IsCompatible, stageSpv.Issues.empty() ? "" : stageSpv.Issues[0].Message);

            const auto stageDxil = ShaderReflectionValidator::ValidateStageInterface(
                vsDxil.Value().Reflection, psDxil.Value().Reflection);
            EXPECT_TRUE(stageDxil.IsCompatible, stageDxil.Issues.empty() ? "" : stageDxil.Issues[0].Message);
        }
    }

    // Test 6 & 7: Stage Interface Validation (Pass & Negative Cases)
    void Test_StageInterfaceValidation(const ShaderCompiler& compiler)
    {
        std::cout << "\n--- Test 6/7: Stage Interface Validation (Matching & Mismatched Stages) ---\n";

        constexpr const char* kVsSrc = R"(
struct VSOut { float4 Pos : SV_POSITION; float3 Color : COLOR0; float2 UV : TEXCOORD0; };
VSOut VSMain(float3 pos : POSITION)
{
    VSOut o;
    o.Pos = float4(pos, 1.0);
    o.Color = float3(1.0, 0.5, 0.25);
    o.UV = pos.xy;
    return o;
}
)";
        constexpr const char* kPsMissingSrc = R"(
struct PSIn { float4 Pos : SV_POSITION; float3 Color : COLOR0; float2 UV : TEXCOORD0; float4 Extra : TEXCOORD3; };
float4 PSMain(PSIn i) : SV_TARGET { return float4(i.Color + float3(i.UV, 0.0), 1.0) + i.Extra; }
)";
        constexpr const char* kPsWidthMismatchSrc = R"(
struct PSIn { float4 Pos : SV_POSITION; float4 Color : COLOR0; float2 UV : TEXCOORD0; };
float4 PSMain(PSIn i) : SV_TARGET { return i.Color + float4(i.UV, 0.0, 1.0); }
)";
        constexpr const char* kPsTypeMismatchSrc = R"(
struct PSIn { float4 Pos : SV_POSITION; uint3 Color : COLOR0; float2 UV : TEXCOORD0; };
float4 PSMain(PSIn i) : SV_TARGET { return float4(float3(i.Color), 1.0) + float4(i.UV, 0.0, 1.0); }
)";

        for (ShaderTargetFormat fmt : { ShaderTargetFormat::SPIRV, ShaderTargetFormat::DXIL })
        {
            auto compile = [&](const char* src, ShaderStage stg, const char* ep) {
                ShaderCompileRequest r{};
                r.SourceCode = src;
                r.VirtualFilePath = "StageValidation.hlsl";
                r.EntryPoint = ep;
                r.Stage = stg;
                r.TargetFormat = fmt;
                r.Model = ShaderModel::SM6_2;
                return compiler.Compile(r);
            };

            auto vs = compile(kVsSrc, ShaderStage::Vertex, "VSMain");
            auto psMissing = compile(kPsMissingSrc, ShaderStage::Pixel, "PSMain");
            auto psWidth = compile(kPsWidthMismatchSrc, ShaderStage::Pixel, "PSMain");
            auto psType = compile(kPsTypeMismatchSrc, ShaderStage::Pixel, "PSMain");

            EXPECT_TRUE(vs && psMissing && psWidth && psType);
            if (!vs || !psMissing || !psWidth || !psType)
            {
                continue;
            }

            const auto missingRes = ShaderReflectionValidator::ValidateStageInterface(
                vs.Value().Reflection, psMissing.Value().Reflection);
            EXPECT_TRUE(!missingRes.IsCompatible);
            EXPECT_TRUE(HasIssueWithCode(missingRes, "STAGE_MISSING_OUTPUT"));

            const auto widthRes = ShaderReflectionValidator::ValidateStageInterface(
                vs.Value().Reflection, psWidth.Value().Reflection);
            EXPECT_TRUE(!widthRes.IsCompatible);
            EXPECT_TRUE(HasIssueWithCode(widthRes, "STAGE_COMPONENT_MASK_MISMATCH") ||
                        HasIssueWithCode(widthRes, "STAGE_COMPONENT_COUNT_MISMATCH"));

            const auto typeRes = ShaderReflectionValidator::ValidateStageInterface(
                vs.Value().Reflection, psType.Value().Reflection);
            EXPECT_TRUE(!typeRes.IsCompatible);
            EXPECT_TRUE(HasIssueWithCode(typeRes, "STAGE_TYPE_MISMATCH"));

            // Wrong stage order (PS -> VS)
            const auto badOrder = ShaderReflectionValidator::ValidateStageInterface(
                psMissing.Value().Reflection, vs.Value().Reflection);
            EXPECT_TRUE(!badOrder.IsCompatible);
            EXPECT_TRUE(HasIssueWithCode(badOrder, "STAGE_ORDER_INVALID"));
        }
    }

    // Test 10: Negative Cross-Backend Validation Cases
    void Test_CrossBackendNegativeValidation(const ShaderCompiler& compiler)
    {
        std::cout << "\n--- Test 10: Cross-Backend Negative Validation Cases ---\n";

        auto compileCs = [&](const char* src, ShaderTargetFormat fmt) {
            ShaderCompileRequest r{};
            r.SourceCode = src;
            r.VirtualFilePath = "NegativeCross.hlsl";
            r.EntryPoint = "CSMain";
            r.Stage = ShaderStage::Compute;
            r.TargetFormat = fmt;
            r.Model = ShaderModel::SM6_2;
            return compiler.Compile(r);
        };

        // 10a: CB layout member offset/type mismatch + compute thread group mismatch
        constexpr const char* kCsSpvA = R"(
cbuffer Params : register(b0, space0) { float4 Scale; uint Count; };
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[id.x % Count] = Scale; }
)";
        constexpr const char* kCsDxilMismatch = R"(
cbuffer Params : register(b0, space0) { float2 Scale; int Count; };
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(16, 4, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[uint(id.x) % uint(Count)] = float4(Scale, 0, 1); }
)";
        {
            auto spv = compileCs(kCsSpvA, ShaderTargetFormat::SPIRV);
            auto dxil = compileCs(kCsDxilMismatch, ShaderTargetFormat::DXIL);
            EXPECT_TRUE(spv && dxil);
            if (spv && dxil)
            {
                const auto res = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                    spv.Value().Reflection, dxil.Value().Reflection, CrossBackendBindingPolicy::StrictOneToOne());
                EXPECT_TRUE(!res.IsCompatible);
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_THREADGROUP_SIZE_MISMATCH"));
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_LAYOUT_OFFSET_MISMATCH"));
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_LAYOUT_TYPE_MISMATCH"));
            }
        }

        // 10b: Descriptor cardinality mismatch (array[4] vs array[8] and UBO array vs single CBV under StrictOneToOne)
        constexpr const char* kTexArray4 = R"(
Texture2D<float4> TexArr[4] : register(t0, space0);
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[0] = TexArr[id.x % 4].Load(int3(0,0,0)); }
)";
        constexpr const char* kTexArray8 = R"(
Texture2D<float4> TexArr[8] : register(t0, space0);
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[0] = TexArr[id.x % 8].Load(int3(0,0,0)); }
)";
        {
            auto spv = compileCs(kTexArray4, ShaderTargetFormat::SPIRV);
            auto dxil = compileCs(kTexArray8, ShaderTargetFormat::DXIL);
            EXPECT_TRUE(spv && dxil);
            if (spv && dxil)
            {
                const auto res = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                    spv.Value().Reflection, dxil.Value().Reflection, CrossBackendBindingPolicy::StrictOneToOne());
                EXPECT_TRUE(!res.IsCompatible);
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_BINDING_COUNT_MISMATCH"));
            }
        }

        // 10d: Vulkan UBO array vs DX12 single Root CBV MUST fail under StrictOneToOne
        constexpr const char* kUboArrSpv = R"(
struct BoneBlock { float4 Data; };
[[vk::binding(3, 0)]] ConstantBuffer<BoneBlock> g_UboArr[] : register(b1, space0);
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[0] = g_UboArr[id.x].Data; }
)";
        constexpr const char* kUboSingleDxil = R"(
cbuffer g_UboSingle : register(b1, space0) { float4 Data; };
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[0] = Data; }
)";
        {
            auto spv = compileCs(kUboArrSpv, ShaderTargetFormat::SPIRV);
            auto dxil = compileCs(kUboSingleDxil, ShaderTargetFormat::DXIL);
            EXPECT_TRUE(spv && dxil);
            if (spv && dxil)
            {
                const auto res = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                    spv.Value().Reflection, dxil.Value().Reflection, CrossBackendBindingPolicy::StrictOneToOne());
                EXPECT_TRUE(!res.IsCompatible);
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_MISSING_DXIL_BINDING"));
            }
        }

        // 10e: Vulkan unbounded Texture2D[] vs DX12 single Texture2D must fail EVEN under MorbiBindless
        constexpr const char* kBindlessTexArrSpv = R"(
[[vk::binding(1, 0)]] Texture2D<float4> g_TexArr[] : register(t0, space1);
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[0] = g_TexArr[id.x].Load(int3(0,0,0)); }
)";
        constexpr const char* kSingleTexDxil = R"(
Texture2D<float4> g_SingleTex : register(t0, space1);
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[0] = g_SingleTex.Load(int3(0,0,0)); }
)";
        {
            auto spv = compileCs(kBindlessTexArrSpv, ShaderTargetFormat::SPIRV);
            auto dxil = compileCs(kSingleTexDxil, ShaderTargetFormat::DXIL);
            EXPECT_TRUE(spv && dxil);
            if (spv && dxil)
            {
                const auto res = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                    spv.Value().Reflection, dxil.Value().Reflection, CrossBackendBindingPolicy::MorbiBindless());
                EXPECT_TRUE(!res.IsCompatible);
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_BINDING_COUNT_MISMATCH"));
            }
        }

        // 10c: Push constant >128B limit & member mismatch vs DX12 RootConstant
        constexpr const char* kPushOverflowSpv = R"(
struct BigPush { float4 A[9]; }; // 144 bytes > 128
[[vk::push_constant]] BigPush Push;
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[id.x] = Push.A[0]; }
)";
        constexpr const char* kRootSmallerDxil = R"(
struct SmallPush { float4 A[4]; }; // 64 bytes
cbuffer RootConstantsBuffer : register(b0, space0) { SmallPush Push; };
RWStructuredBuffer<float4> OutBuf : register(u0, space0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) { OutBuf[id.x] = Push.A[0]; }
)";
        {
            auto spv = compileCs(kPushOverflowSpv, ShaderTargetFormat::SPIRV);
            auto dxil = compileCs(kRootSmallerDxil, ShaderTargetFormat::DXIL);
            EXPECT_TRUE(spv && dxil);
            if (spv && dxil)
            {
                auto policy = CrossBackendBindingPolicy::MorbiBindless();
                const auto res = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                    spv.Value().Reflection, dxil.Value().Reflection, policy);
                EXPECT_TRUE(!res.IsCompatible);
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_PUSH_CONSTANT_SIZE_EXCEEDS_POLICY"));
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_ROOT_CONSTANT_SIZE_MISMATCH"));
                EXPECT_TRUE(HasIssueWithCode(res, "CROSS_LAYOUT_ARRAY_COUNT_MISMATCH"));
            }
        }
    }

    // Test 11 & 12: Corrupt Binaries, Unaligned NO_COPY Sub-span, Caller Buffer Lifetime & Missing Entry/Stage
    void Test_ErrorPathsNoCopyAndLifetime(const ShaderCompiler& compiler)
    {
        std::cout << "\n--- Test 11/12: Corrupt Bytecode, Unaligned NO_COPY Sub-span & Lifetime ---\n";

        // Empty buffers
        EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, {}).IsSuccess());
        EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::DXIL, {}).IsSuccess());

        // Truncated / corrupt magic / garbage
        const std::array<u8, 64> garbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
        EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, garbage).IsSuccess());
        EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::DXIL, garbage).IsSuccess());

        // Valid SPIR-V magic but invalid instruction body
        const std::array<u8, 24> badSpv = {
            0x03, 0x02, 0x23, 0x07, // magic
            0x00, 0x06, 0x01, 0x00, // ver 1.6
            0x00, 0x00, 0x00, 0x00, // gen
            0x05, 0x00, 0x00, 0x00, // bound
            0x00, 0x00, 0x00, 0x00, // schema
            0xFF, 0xFF, 0xFF, 0x7F  // truncated/bad op
        };
        EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, badSpv).IsSuccess());

        // Compile valid SPIR-V + DXIL then test unaligned copy + freeing caller buffer + stage/entry mismatches
        ShaderCompileRequest reqSpv{};
        reqSpv.SourceCode = kNestedCbAndResourcesHlsl;
        reqSpv.VirtualFilePath = "LifetimeTest.hlsl";
        reqSpv.EntryPoint = "CSMain";
        reqSpv.Stage = ShaderStage::Compute;
        reqSpv.TargetFormat = ShaderTargetFormat::SPIRV;
        reqSpv.Model = ShaderModel::SM6_2;

        auto spvRes = compiler.Compile(reqSpv);
        EXPECT_TRUE(spvRes.IsSuccess());
        if (spvRes)
        {
            ShaderReflectionData reflectedAfterFree;
            {
                std::vector<u8> unalignedBuf(1 + spvRes.Value().Bytecode.size() + 3, 0u);
                std::memcpy(unalignedBuf.data() + 1, spvRes.Value().Bytecode.data(), spvRes.Value().Bytecode.size());
                std::span<const u8> unalignedSpan(unalignedBuf.data() + 1, spvRes.Value().Bytecode.size());

                auto reflRes = compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, unalignedSpan, ShaderStage::Compute, "CSMain");
                EXPECT_TRUE(reflRes.IsSuccess(), reflRes ? "" : reflRes.Error());
                if (reflRes)
                {
                    reflectedAfterFree = std::move(reflRes).ConsumeValue();
                }

                // Wrong stage / missing entry point checks
                EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, unalignedSpan, ShaderStage::Pixel, "CSMain").IsSuccess());
                EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, unalignedSpan, ShaderStage::Compute, "NonExistentMain").IsSuccess());
            }
            // Buffer destroyed above; verify deep-copied reflection remains completely intact
            EXPECT_TRUE(reflectedAfterFree == spvRes.Value().Reflection);
        }

        auto reqDxil = reqSpv;
        reqDxil.TargetFormat = ShaderTargetFormat::DXIL;
        auto dxilRes = compiler.Compile(reqDxil);
        EXPECT_TRUE(dxilRes.IsSuccess());
        if (dxilRes)
        {
            EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::DXIL, dxilRes.Value().Bytecode, ShaderStage::Vertex, "CSMain").IsSuccess());
            EXPECT_TRUE(!compiler.ReflectBytecode(ShaderTargetFormat::SPIRV, dxilRes.Value().Bytecode).IsSuccess());
        }
    }

    // Test 13 & 14: ShaderCache v3 Lossless Round-Trip, v2 Stale Migration & Multi-Threaded Parallel Reflection
    void Test_ShaderCacheV3AndParallelReflection(const ShaderCompiler& compiler)
    {
        std::cout << "\n--- Test 13/14: ShaderCache v3 Round-Trip, v2 Invalidation & Parallel Reflection ---\n";

        ScopedTempDir temp("cachev3");
        const std::filesystem::path cacheDir = temp.Path / "cache";

        ShaderCompileRequest req{};
        req.SourceCode = kMorbiPbrHlsl;
        req.VirtualFilePath = "MorbiScenePBR_Cache.hlsl";
        req.EntryPoint = "VSMain";
        req.Stage = ShaderStage::Vertex;
        req.TargetFormat = ShaderTargetFormat::SPIRV;
        req.Model = ShaderModel::SM6_2;
        req.Defines.push_back(ShaderMacro{ "MORBI_VULKAN", "1" });

        CompiledShaderArtifact compiledRef{};
        {
            ShaderCache cache(cacheDir, compiler);
            auto r1 = cache.GetOrCompile(req);
            EXPECT_TRUE(r1.IsSuccess(), r1 ? "" : r1.Error());
            if (!r1)
            {
                return;
            }
            compiledRef = r1.Value();
            EXPECT_TRUE(cache.GetStats().Compilations == 1u);
            EXPECT_TRUE(compiledRef.Reflection.IsValid);
            EXPECT_TRUE(!compiledRef.Reflection.ConstantBuffers.empty());
            EXPECT_TRUE(!compiledRef.Reflection.PushConstants.empty());
            EXPECT_TRUE(!compiledRef.Reflection.StageInputs.empty());
            EXPECT_TRUE(!compiledRef.Reflection.StageOutputs.empty());
        }

        // Reload in brand-new ShaderCache instance -> must hit disk cleanly with IDENTICAL reflection!
        {
            ShaderCache cacheReloaded(cacheDir, compiler);
            auto r2 = cacheReloaded.GetOrCompile(req);
            EXPECT_TRUE(r2.IsSuccess(), r2 ? "" : r2.Error());
            if (r2)
            {
                EXPECT_TRUE(cacheReloaded.GetStats().DiskHits == 1u);
                EXPECT_TRUE(cacheReloaded.GetStats().Compilations == 0u);
                EXPECT_TRUE(r2.Value().Reflection == compiledRef.Reflection,
                    "ShaderCache v3 deserialized reflection must equal compiled reflection bit-for-bit");
            }
        }

        // Downgrade on-disk header Version byte to 2 (simulate old v2 cache file) -> must invalidate & recompile cleanly
        const std::filesystem::path cacheFile =
            ShaderCache(cacheDir, compiler).GetCacheFilePathForRequest(req);
        {
            std::fstream f(cacheFile, std::ios::binary | std::ios::in | std::ios::out);
            EXPECT_TRUE(static_cast<bool>(f));
            if (f)
            {
                const u32 oldV2 = 2u;
                f.seekp(4, std::ios::beg); // Magic is 4 bytes, Version is next 4 bytes
                f.write(reinterpret_cast<const char*>(&oldV2), sizeof(oldV2));
            }
        }
        {
            ShaderCache migratedCache(cacheDir, compiler);
            auto r3 = migratedCache.GetOrCompile(req);
            EXPECT_TRUE(r3.IsSuccess());
            if (r3)
            {
                EXPECT_TRUE(migratedCache.GetStats().Invalidations == 1u);
                EXPECT_TRUE(migratedCache.GetStats().Compilations == 1u);
                EXPECT_TRUE(r3.Value().Reflection == compiledRef.Reflection);
            }
        }

        // Parallel stress across 6 threads on both SPIR-V & DXIL
        std::atomic<int> threadFailures{ 0 };
        std::vector<std::thread> threads;
        ShaderCache sharedCache(temp.Path / "parallel_cache", compiler);

        for (int t = 0; t < 6; ++t)
        {
            threads.emplace_back([&, t]() {
                for (int iter = 0; iter < 4; ++iter)
                {
                    ShaderCompileRequest r = req;
                    r.TargetFormat = ((t + iter) % 2 == 0) ? ShaderTargetFormat::SPIRV : ShaderTargetFormat::DXIL;
                    if (r.TargetFormat == ShaderTargetFormat::DXIL)
                    {
                        r.Defines.clear();
                    }
                    auto res = sharedCache.GetOrCompile(r);
                    if (!res || !res.Value().Reflection.IsValid || res.Value().Reflection.StageInputs.size() != 7u)
                    {
                        threadFailures.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (auto& th : threads)
        {
            th.join();
        }
        EXPECT_TRUE(threadFailures.load() == 0, "Parallel ShaderCache v3 + binary reflection must succeed cleanly");
    }

    // Test 15: Repeated DXC ContainerReflection & Compiler Lifecycle Stress
    void Test_DxcContainerReflectionLifetimeStress()
    {
        std::cout << "\n--- Test 15: DXC ContainerReflection & Compiler Lifecycle Stress ---\n";

        std::vector<u8> dxilBytes;
        {
            ShaderCompiler c;
            EXPECT_TRUE(c.IsDXCAvailable());
            ShaderCompileRequest r{};
            r.SourceCode = kNestedCbAndResourcesHlsl;
            r.VirtualFilePath = "LifecycleDxil.hlsl";
            r.EntryPoint = "CSMain";
            r.Stage = ShaderStage::Compute;
            r.TargetFormat = ShaderTargetFormat::DXIL;
            r.Model = ShaderModel::SM6_2;
            auto compiled = c.Compile(r);
            EXPECT_TRUE(compiled.IsSuccess());
            if (compiled)
            {
                dxilBytes = compiled.Value().Bytecode;
            }
        }

        for (int i = 0; i < 6; ++i)
        {
            ShaderCompiler localCompiler;
            EXPECT_TRUE(localCompiler.IsDXCAvailable());
            auto refl = localCompiler.ReflectBytecode(
                ShaderTargetFormat::DXIL, dxilBytes, ShaderStage::Compute, "CSMain");
            EXPECT_TRUE(refl.IsSuccess());
            if (refl)
            {
                EXPECT_TRUE(refl.Value().ThreadGroupSizeX == 8u);
                EXPECT_TRUE(refl.Value().ConstantBuffers.size() == 1u);
            }
        }
    }
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "MorbiEngine Phase 2 WP2 Reflection Tests\n";
    std::cout << "========================================\n";

    ShaderCompiler compiler;
    EXPECT_TRUE(compiler.IsDXCAvailable(), "Default ShaderCompiler must initialize DXC");
    if (!compiler.IsDXCAvailable())
    {
        return 1;
    }

    Test_ConstantBuffersResourcesAndCompute(compiler);
    Test_MorbiPbrShaderAndVertexInputs(compiler);
    Test_StageInterfaceValidation(compiler);
    Test_CrossBackendNegativeValidation(compiler);
    Test_ErrorPathsNoCopyAndLifetime(compiler);
    Test_ShaderCacheV3AndParallelReflection(compiler);
    Test_DxcContainerReflectionLifetimeStress();

    std::cout << "\n========================================\n";
    std::cout << "TOTAL: " << g_passedAssertions << "/" << (g_passedAssertions + g_failedAssertions)
              << " assertions passed\n";
    std::cout << "========================================\n";

    return (g_failedAssertions == 0) ? 0 : 1;
}
