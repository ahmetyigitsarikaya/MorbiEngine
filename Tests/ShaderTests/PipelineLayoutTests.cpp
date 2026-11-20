#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/IDevice.hpp"
#include "Morbi/RHI/PipelineLayout.hpp"
#include "Morbi/Shader/PipelineLayoutBuilder.hpp"
#include "Morbi/Shader/ShaderCompiler.hpp"
#include "Morbi/Shader/ShaderReflection.hpp"

using namespace Morbi;
using namespace Morbi::RHI;
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

    struct TestDeviceContext
    {
        std::unique_ptr<IDevice> Device;
        Platform::NativeWindowHandle WindowHandle{};
    };

    TestDeviceContext CreateTestDevice(BackendAPI api)
    {
        TestDeviceContext ctx{};
        if (api == BackendAPI::Null)
        {
            ctx.Device = CreateDevice(BackendAPI::Null);
            DeviceDesc desc{
                .PreferredAPI = BackendAPI::Null,
                .EnableValidation = false,
                .ApplicationName = "MorbiPipelineLayoutTests_Null"
            };
            if (!ctx.Device || !ctx.Device->Initialize(desc, ctx.WindowHandle, 256, 256))
            {
                ctx.Device.reset();
            }
            return ctx;
        }

#if defined(_WIN32)
        ctx.WindowHandle.Backend = Platform::NativeWindowBackend::Win32;
        ctx.WindowHandle.Window = CreateWindowExW(
            0,
            L"STATIC",
            L"MorbiPipelineLayoutTests Window",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            256,
            256,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (!ctx.WindowHandle.Window)
        {
            return ctx;
        }
#endif

        ctx.Device = CreateDevice(api);
        if (!ctx.Device)
        {
#if defined(_WIN32)
            if (ctx.WindowHandle.Window)
            {
                DestroyWindow(static_cast<HWND>(ctx.WindowHandle.Window));
                ctx.WindowHandle.Window = nullptr;
            }
#endif
            return ctx;
        }

        DeviceDesc desc{
            .PreferredAPI = api,
            .EnableValidation = true,
            .ApplicationName = "MorbiPipelineLayoutTests"
        };
        auto initRes = ctx.Device->Initialize(desc, ctx.WindowHandle, 256, 256);
        if (!initRes)
        {
            std::cerr << "[INFO] Backend " << (api == BackendAPI::Vulkan ? "Vulkan" : "DX12")
                      << " init failed: " << initRes.Error() << "\n";
            ctx.Device.reset();
#if defined(_WIN32)
            if (ctx.WindowHandle.Window)
            {
                DestroyWindow(static_cast<HWND>(ctx.WindowHandle.Window));
                ctx.WindowHandle.Window = nullptr;
            }
#endif
        }
        return ctx;
    }

    void DestroyTestDevice(TestDeviceContext& ctx)
    {
        if (ctx.Device)
        {
            ctx.Device->Shutdown();
            ctx.Device.reset();
        }
#if defined(_WIN32)
        if (ctx.WindowHandle.Window)
        {
            DestroyWindow(static_cast<HWND>(ctx.WindowHandle.Window));
            ctx.WindowHandle.Window = nullptr;
        }
#endif
    }

    Core::Result<CompiledShaderArtifact, std::string> CompileDualShader(
        ShaderCompiler& compiler,
        std::string_view hlslSource,
        std::string_view virtualPath,
        std::string_view entryPoint,
        ShaderStage stage,
        ShaderTargetFormat targetFormat)
    {
        ShaderCompileRequest req{};
        req.SourceCode = std::string(hlslSource);
        req.VirtualFilePath = std::string(virtualPath);
        req.EntryPoint = std::string(entryPoint);
        req.Stage = stage;
        req.TargetFormat = targetFormat;
        req.EnableOptimization = true;
        req.ValidateSpirV = (targetFormat == ShaderTargetFormat::SPIRV);
        if (targetFormat == ShaderTargetFormat::SPIRV)
        {
            req.Defines.push_back(ShaderMacro{ "MORBI_VULKAN", "1" });
        }
        auto res = compiler.Compile(req);
        if (!res)
        {
            std::cerr << "[ShaderCompileError] " << virtualPath << " (" << entryPoint << "): " << res.Error() << "\n";
        }
        return res;
    }

    // -------------------------------------------------------------------------
    // HLSL Test Shaders (Dual Target SPIR-V & DXIL)
    // -------------------------------------------------------------------------
    constexpr const char* kSimplePushGraphicsHlsl = R"(
struct FramePushConstants
{
    float4 ColorTint;
    float4 ScaleOffset;
};

#ifdef MORBI_VULKAN
[[vk::push_constant]]
FramePushConstants g_Push;
#else
cbuffer FramePushConstants : register(b0, space0)
{
    FramePushConstants g_Push;
};
#endif

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    o.UV = pos;
    o.Pos = float4(pos * 2.0f - 1.0f + g_Push.ScaleOffset.xy, 0.0f, 1.0f);
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    return float4(i.UV * g_Push.ScaleOffset.zw, 0.25f, 1.0f) * g_Push.ColorTint;
}
)";

    constexpr const char* kSecondPushGraphicsHlsl = R"(
struct LargePushConstants
{
    float4 ColorA;
    float4 ColorB;
    float4 ParamsA;
    float4 ParamsB;
};

#ifdef MORBI_VULKAN
[[vk::push_constant]]
LargePushConstants g_LargePush;
#else
cbuffer LargePushConstants : register(b0, space0)
{
    LargePushConstants g_LargePush;
};
#endif

struct VSOut
{
    float4 Pos : SV_POSITION;
    float4 Col : COLOR0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2);
    o.Pos = float4(pos * 2.0f - 1.0f, 0.0f, 1.0f);
    o.Col = g_LargePush.ColorA + g_LargePush.ColorB;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    return i.Col * g_LargePush.ParamsA + g_LargePush.ParamsB;
}
)";

    constexpr const char* kSimplePushComputeHlsl = R"(
struct ComputePush
{
    uint Value0;
    uint Value1;
    uint Value2;
    uint Value3;
};

#ifdef MORBI_VULKAN
[[vk::push_constant]]
ComputePush g_Push;
#else
cbuffer ComputePush : register(b0, space0)
{
    ComputePush g_Push;
};
#endif

groupshared uint s_Scratch;

[numthreads(1, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    s_Scratch = g_Push.Value0 + g_Push.Value1 + g_Push.Value2 + g_Push.Value3 + dtid.x;
    GroupMemoryBarrierWithGroupSync();
}
)";

    constexpr const char* kDescriptorMappingHlsl = R"(
#ifdef MORBI_VULKAN
[[vk::binding(0, 1)]] cbuffer g_Scene : register(b0, space1)
{
    float4x4 ViewProj;
    float4   Exposure;
};
[[vk::binding(1, 1)]] Texture2D<float4>        g_TexArray[4] : register(t0, space1);
[[vk::binding(2, 1)]] StructuredBuffer<float4> g_Instances   : register(t4, space1);
[[vk::binding(3, 1)]] RWTexture2D<float4>      g_OutImage    : register(u0, space1);
[[vk::binding(4, 1)]] SamplerState             g_Linear      : register(s0, space1);
#else
cbuffer g_Scene : register(b0, space1)
{
    float4x4 ViewProj;
    float4   Exposure;
};
Texture2D<float4>        g_TexArray[4] : register(t0, space1);
StructuredBuffer<float4> g_Instances   : register(t4, space1);
RWTexture2D<float4>      g_OutImage    : register(u0, space1);
SamplerState             g_Linear      : register(s0, space1);
#endif

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    float4 texVal = g_TexArray[dtid.x & 3].SampleLevel(g_Linear, float2(0.5f, 0.5f), 0.0f);
    float4 inst   = g_Instances[0];
    g_OutImage[dtid.xy] = mul(texVal + inst, ViewProj) * Exposure.x;
}
)";

    constexpr const char* kBindlessHybridPbrHlsl = R"(
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
    row_major float4x4 BoneMatrices[4];
};

#ifdef MORBI_VULKAN
[[vk::push_constant]]
MeshPushConstants g_Push;
[[vk::binding(0, 0)]] StructuredBuffer<float4>          g_Materials[]   : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<float4>                 g_Textures[]    : register(t0, space1);
[[vk::binding(2, 0)]] SamplerState                      g_LinearSampler : register(s0, space0);
[[vk::binding(3, 0)]] ConstantBuffer<PerObjectBoneData> g_Bones[]       : register(b1, space0);
#else
cbuffer MeshPushConstants : register(b0, space0)
{
    MeshPushConstants g_Push;
};
StructuredBuffer<float4> g_Materials[] : register(t0, space0);
Texture2D<float4>        g_Textures[]  : register(t0, space1);
SamplerState             g_LinearSampler : register(s0, space0);
cbuffer g_Bones : register(b1, space0)
{
    row_major float4x4 BoneMatrices[4];
};
#endif

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2);
#ifdef MORBI_VULKAN
    float4 worldPos = mul(float4(p, 0.0f, 1.0f), g_Bones[0].BoneMatrices[0]);
#else
    float4 worldPos = mul(float4(p, 0.0f, 1.0f), BoneMatrices[0]);
#endif
    o.Pos = float4(dot(worldPos, g_Push.MvpRow0), dot(worldPos, g_Push.MvpRow1), dot(worldPos, g_Push.MvpRow2), dot(worldPos, g_Push.MvpRow3));
    o.UV = p;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float4 c = g_Textures[NonUniformResourceIndex(g_Push.AlbedoTexIndex)].Sample(g_LinearSampler, i.UV);
    float4 m = g_Materials[NonUniformResourceIndex(g_Push.SpecularTexIndex)][0];
    return c * m + g_Push.ModelRow0;
}
)";

    constexpr const char* kIncompatibleStagePsHlsl = R"(
struct BadPSIn
{
    float4 Pos : SV_POSITION;
    float4 UV  : TEXCOORD0;
    float4 ExtraSemantic : COLOR2;
};

float4 PSMain(BadPSIn i) : SV_TARGET
{
    return i.ExtraSemantic;
}
)";

    constexpr const char* kConflictingBindingPsHlsl = R"(
Texture2D<float4> g_SharedSlot : register(t0, space1);
SamplerState      g_Sampler    : register(s0, space1);

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

float4 PSMain(VSOut i) : SV_TARGET
{
    return g_SharedSlot.Sample(g_Sampler, i.UV);
}
)";

    constexpr const char* kConflictingBindingVsHlsl = R"(
StructuredBuffer<float4> g_SharedSlot : register(t0, space1);

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float4 d = g_SharedSlot[vid];
    o.Pos = d;
    o.UV = float2(0.0f, 0.0f);
    return o;
}
)";

    // -------------------------------------------------------------------------
    // Helper: Run Lifecycle, Caching, Collision & Validation Tests on Any Device
    // -------------------------------------------------------------------------
    void RunLayoutModelAndValidationTests()
    {
        std::cout << "[Test] 1. Structural Equality, Canonical Hash & Validator Rules\n";

        PipelineLayoutDesc d1{};
        d1.Mode = PipelineLayoutMode::Standalone;
        d1.DebugName = "AlphaLayout";
        d1.PushConstants.push_back(PipelinePushConstantRange{
            .Name = "Push",
            .OffsetBytes = 0,
            .SizeBytes = 32,
            .Visibility = ShaderVisibility::Vertex | ShaderVisibility::Pixel,
            .DxilRegister = 0,
            .DxilSpace = 0
        });
        DescriptorSetLayoutDesc setB{};
        setB.SetIndex = 2;
        setB.Bindings.push_back(PipelineBindingLayout{
            .Name = "TexB",
            .Type = DescriptorType::SampledTexture,
            .Set = 2,
            .Binding = 1,
            .DxilRegister = 1,
            .DxilSpace = 2,
            .Count = 4,
            .Visibility = ShaderVisibility::Pixel
        });
        setB.Bindings.push_back(PipelineBindingLayout{
            .Name = "CbA",
            .Type = DescriptorType::UniformBuffer,
            .Set = 2,
            .Binding = 0,
            .DxilRegister = 0,
            .DxilSpace = 2,
            .Count = 1,
            .Visibility = ShaderVisibility::Vertex
        });
        d1.Sets.push_back(setB);

        // Same structure, reversed binding order and different DebugName
        PipelineLayoutDesc d2{};
        d2.Mode = PipelineLayoutMode::Standalone;
        d2.DebugName = "RenamedLayout";
        d2.PushConstants = d1.PushConstants;
        DescriptorSetLayoutDesc setBRev{};
        setBRev.SetIndex = 2;
        setBRev.Bindings.push_back(setB.Bindings[1]);
        setBRev.Bindings.push_back(setB.Bindings[0]);
        d2.Sets.push_back(setBRev);

        EXPECT_TRUE(PipelineLayoutValidator::ComputeCanonicalHash(d1) == PipelineLayoutValidator::ComputeCanonicalHash(d2));
        EXPECT_TRUE(PipelineLayoutValidator::AreStructurallyEqual(d1, d2));

        // Explicit structural inequality checks (guards against hash collisions)
        PipelineLayoutDesc dDiffType = d1;
        dDiffType.Sets[0].Bindings[0].Type = DescriptorType::StorageBuffer;
        EXPECT_TRUE(!PipelineLayoutValidator::AreStructurallyEqual(d1, dDiffType));

        PipelineLayoutDesc dDiffCount = d1;
        dDiffCount.Sets[0].Bindings[1].Count = 8;
        EXPECT_TRUE(!PipelineLayoutValidator::AreStructurallyEqual(d1, dDiffCount));

        PipelineLayoutDesc dDiffVis = d1;
        dDiffVis.PushConstants[0].Visibility = ShaderVisibility::Vertex;
        EXPECT_TRUE(!PipelineLayoutValidator::AreStructurallyEqual(d1, dDiffVis));

        // CombinedImageSampler must fail on DX12 and Null portable check, allowed on Vulkan
        PipelineLayoutDesc combined{};
        combined.Mode = PipelineLayoutMode::Standalone;
        combined.Sets.push_back(DescriptorSetLayoutDesc{
            .SetIndex = 0,
            .Bindings = {
                PipelineBindingLayout{
                    .Name = "CombinedTex",
                    .Type = DescriptorType::CombinedImageSampler,
                    .Set = 0,
                    .Binding = 0,
                    .Count = 1,
                    .Visibility = ShaderVisibility::Pixel
                }
            }
        });
        const PipelineLayoutLimits defaultLimits{};
        EXPECT_TRUE(!PipelineLayoutValidator::Validate(combined, defaultLimits, BackendAPI::DirectX12).IsValid);
        EXPECT_TRUE(!PipelineLayoutValidator::Validate(combined, defaultLimits, BackendAPI::Null).IsValid);
        EXPECT_TRUE(PipelineLayoutValidator::Validate(combined, defaultLimits, BackendAPI::Vulkan).IsValid);

        // Simulated feature rejection on Vulkan when UpdateAfterBind / RuntimeArray are disabled
        PipelineLayoutDesc bindlessFeatureDesc{};
        bindlessFeatureDesc.Mode = PipelineLayoutMode::Standalone;
        bindlessFeatureDesc.Sets.push_back(DescriptorSetLayoutDesc{
            .SetIndex = 0,
            .AllowUpdateAfterBind = true,
            .Bindings = {
                PipelineBindingLayout{
                    .Name = "UnboundedTex",
                    .Type = DescriptorType::SampledTexture,
                    .Set = 0,
                    .Binding = 0,
                    .DxilRegister = 0,
                    .DxilSpace = 0,
                    .Count = 0,
                    .IsUnboundedArray = true,
                    .Visibility = ShaderVisibility::Pixel
                }
            }
        });
        PipelineLayoutLimits disabledFeaturesLimits{};
        disabledFeaturesLimits.SupportsRuntimeDescriptorArray = false;
        disabledFeaturesLimits.SupportsUpdateAfterBind = false;
        disabledFeaturesLimits.SupportsDescriptorIndexing = false;
        EXPECT_TRUE(!PipelineLayoutValidator::Validate(bindlessFeatureDesc, disabledFeaturesLimits, BackendAPI::Vulkan).IsValid);
    }

    void RunShaderReflectionLayoutBuilderTests(ShaderCompiler& compiler)
    {
        std::cout << "[Test] 2. ShaderReflection -> PipelineLayoutBuilder (Dual Backend, Arrays, Bindless, Conflicts)\n";

        // A. Dual-backend VS + PS (Push/Root constants)
        auto vsSpv = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::SPIRV);
        auto psSpv = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);
        auto vsDxil = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::DXIL);
        auto psDxil = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::DXIL);

        EXPECT_TRUE(vsSpv.IsSuccess(), !vsSpv.IsSuccess() ? vsSpv.Error() : "");
        EXPECT_TRUE(psSpv.IsSuccess(), !psSpv.IsSuccess() ? psSpv.Error() : "");
        EXPECT_TRUE(vsDxil.IsSuccess(), !vsDxil.IsSuccess() ? vsDxil.Error() : "");
        EXPECT_TRUE(psDxil.IsSuccess(), !psDxil.IsSuccess() ? psDxil.Error() : "");

        if (vsSpv && psSpv && vsDxil && psDxil)
        {
            const std::array<ShaderReflectionData, 2> spvStages = { vsSpv.Value().Reflection, psSpv.Value().Reflection };
            const std::array<ShaderReflectionData, 2> dxilStages = { vsDxil.Value().Reflection, psDxil.Value().Reflection };

            PipelineLayoutBuildOptions opts{};
            opts.Policy = CrossBackendBindingPolicy::MorbiBindless();
            opts.Mode = PipelineLayoutMode::Standalone;
            opts.DebugName = "DualBackendSimplePush";

            auto dualRes = PipelineLayoutBuilder::BuildFromDualBackendReflections(spvStages, dxilStages, opts);
            EXPECT_TRUE(dualRes.IsSuccess(), !dualRes.IsSuccess() ? dualRes.Error() : "");
            if (dualRes)
            {
                const auto& layout = dualRes.Value();
                EXPECT_TRUE(layout.PushConstants.size() == 1);
                if (!layout.PushConstants.empty())
                {
                    EXPECT_TRUE(layout.PushConstants[0].SizeBytes == 32);
                    EXPECT_TRUE(layout.PushConstants[0].OffsetBytes == 0);
                    EXPECT_TRUE(HasVisibility(layout.PushConstants[0].Visibility, ShaderVisibility::Vertex));
                    EXPECT_TRUE(HasVisibility(layout.PushConstants[0].Visibility, ShaderVisibility::Pixel));
                }
            }
        }

        // B. Descriptor mapping, array cardinality (Texture2D[4]), CBV + SRV + UAV + Sampler
        auto mapSpv = CompileDualShader(compiler, kDescriptorMappingHlsl, "DescMap.hlsl", "CSMain", ShaderStage::Compute, ShaderTargetFormat::SPIRV);
        auto mapDxil = CompileDualShader(compiler, kDescriptorMappingHlsl, "DescMap.hlsl", "CSMain", ShaderStage::Compute, ShaderTargetFormat::DXIL);
        EXPECT_TRUE(mapSpv.IsSuccess(), !mapSpv.IsSuccess() ? mapSpv.Error() : "");
        EXPECT_TRUE(mapDxil.IsSuccess(), !mapDxil.IsSuccess() ? mapDxil.Error() : "");

        if (mapSpv && mapDxil)
        {
            const std::array<ShaderReflectionData, 1> spvArr = { mapSpv.Value().Reflection };
            const std::array<ShaderReflectionData, 1> dxilArr = { mapDxil.Value().Reflection };

            PipelineLayoutBuildOptions opts{};
            opts.Policy = CrossBackendBindingPolicy::StrictOneToOne();
            opts.Policy.FallbackToIdentitySetSpaceMapping = false;
            opts.Policy.ExplicitBindingRules = {
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::ConstantBuffer, .DxilSpace = 1, .DxilRegister = 0, .VulkanSet = 1, .VulkanBinding = 0, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::ShaderResource, .DxilSpace = 1, .DxilRegister = 0, .VulkanSet = 1, .VulkanBinding = 1, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::ShaderResource, .DxilSpace = 1, .DxilRegister = 4, .VulkanSet = 1, .VulkanBinding = 2, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::UnorderedAccess, .DxilSpace = 1, .DxilRegister = 0, .VulkanSet = 1, .VulkanBinding = 3, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::Sampler,        .DxilSpace = 1, .DxilRegister = 0, .VulkanSet = 1, .VulkanBinding = 4, .AllowUnboundedArrayToSingleCbv = false }
            };
            opts.Mode = PipelineLayoutMode::Standalone;
            opts.DebugName = "DescriptorMappingCS";

            auto mapRes = PipelineLayoutBuilder::BuildFromDualBackendReflections(spvArr, dxilArr, opts);
            EXPECT_TRUE(mapRes.IsSuccess(), !mapRes.IsSuccess() ? mapRes.Error() : "");
            if (mapRes)
            {
                const auto& desc = mapRes.Value();
                EXPECT_TRUE(desc.Sets.size() == 1);
                EXPECT_TRUE(!desc.ConstantBuffers.empty());
                if (!desc.ConstantBuffers.empty())
                {
                    EXPECT_TRUE(desc.ConstantBuffers[0].SizeBytes == 80); // 64B float4x4 + 16B float4
                    EXPECT_TRUE(desc.ConstantBuffers[0].MemberCount == 2);
                }

                bool foundTexArray = false;
                bool foundUbo = false;
                bool foundSsbo = false;
                bool foundUavTex = false;
                bool foundSampler = false;
                for (const auto& b : desc.Sets[0].Bindings)
                {
                    if (b.Name == "g_TexArray")
                    {
                        foundTexArray = true;
                        EXPECT_TRUE(b.Type == DescriptorType::SampledTexture);
                        EXPECT_TRUE(b.Count == 4);
                        EXPECT_TRUE(!b.IsUnboundedArray);
                    }
                    else if (b.Name == "g_Scene")
                    {
                        foundUbo = true;
                        EXPECT_TRUE(b.Type == DescriptorType::UniformBuffer);
                    }
                    else if (b.Name == "g_Instances")
                    {
                        foundSsbo = true;
                        EXPECT_TRUE(b.Type == DescriptorType::StorageBuffer);
                    }
                    else if (b.Name == "g_OutImage")
                    {
                        foundUavTex = true;
                        EXPECT_TRUE(b.Type == DescriptorType::StorageTexture);
                    }
                    else if (b.Name == "g_Linear")
                    {
                        foundSampler = true;
                        EXPECT_TRUE(b.Type == DescriptorType::Sampler);
                    }
                }
                EXPECT_TRUE(foundTexArray && foundUbo && foundSsbo && foundUavTex && foundSampler);
            }

            // Explicit non-identity mapping rule test (Set 1 <-> Space 1 fallback disabled -> must reject if unmapped)
            PipelineLayoutBuildOptions strictNoFallback = opts;
            strictNoFallback.Policy.FallbackToIdentitySetSpaceMapping = false;
            strictNoFallback.Policy.ExplicitBindingRules.clear();
            auto unmappedRes = PipelineLayoutBuilder::BuildFromReflection(spvArr, strictNoFallback);
            EXPECT_TRUE(!unmappedRes.IsSuccess());
        }

        // C. GlobalBindlessHybrid PBR shader
        auto pbrVsSpv = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPBR_VS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::SPIRV);
        auto pbrPsSpv = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPBR_PS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);
        auto pbrVsDxil = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPBR_VS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::DXIL);
        auto pbrPsDxil = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPBR_PS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::DXIL);
        EXPECT_TRUE(pbrVsSpv.IsSuccess() && pbrPsSpv.IsSuccess() && pbrVsDxil.IsSuccess() && pbrPsDxil.IsSuccess());

        if (pbrVsSpv && pbrPsSpv && pbrVsDxil && pbrPsDxil)
        {
            PipelineLayoutBuildOptions hybridOpts{};
            hybridOpts.Policy = CrossBackendBindingPolicy::MorbiBindless();
            hybridOpts.Mode = PipelineLayoutMode::GlobalBindlessHybrid;
            hybridOpts.UseRootDescriptorForSingleDynamicCbv = true;
            hybridOpts.DebugName = "HybridPBR";

            const std::array<ShaderReflectionData, 2> spvPbr = { pbrVsSpv.Value().Reflection, pbrPsSpv.Value().Reflection };
            const std::array<ShaderReflectionData, 2> dxilPbr = { pbrVsDxil.Value().Reflection, pbrPsDxil.Value().Reflection };
            auto hybridRes = PipelineLayoutBuilder::BuildFromDualBackendReflections(spvPbr, dxilPbr, hybridOpts);
            EXPECT_TRUE(hybridRes.IsSuccess(), !hybridRes.IsSuccess() ? hybridRes.Error() : "");
            if (hybridRes)
            {
                EXPECT_TRUE(hybridRes.Value().Mode == PipelineLayoutMode::GlobalBindlessHybrid);
                EXPECT_TRUE(hybridRes.Value().Sets.empty()); // All Set 0 slots absorbed
                EXPECT_TRUE(hybridRes.Value().PushConstants.size() == 1);
                EXPECT_TRUE(hybridRes.Value().PushConstants[0].SizeBytes == 128);
                EXPECT_TRUE(hybridRes.Value().ConstantBuffers.size() == 1);
            }
        }

        // D. Stage interface mismatch rejection
        auto badPsSpv = CompileDualShader(compiler, kIncompatibleStagePsHlsl, "BadPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);
        EXPECT_TRUE(badPsSpv.IsSuccess());
        if (vsSpv && badPsSpv)
        {
            const std::array<ShaderReflectionData, 2> mismatchStages = { vsSpv.Value().Reflection, badPsSpv.Value().Reflection };
            auto mismatchRes = PipelineLayoutBuilder::BuildFromReflection(mismatchStages);
            EXPECT_TRUE(!mismatchRes.IsSuccess());
        }

        // E. Cross-stage conflicting binding type at same (set=1, binding=0) rejection
        auto confVsSpv = CompileDualShader(compiler, kConflictingBindingVsHlsl, "ConfVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::SPIRV);
        auto confPsSpv = CompileDualShader(compiler, kConflictingBindingPsHlsl, "ConfPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);
        EXPECT_TRUE(confVsSpv.IsSuccess() && confPsSpv.IsSuccess());
        if (confVsSpv && confPsSpv)
        {
            const std::array<ShaderReflectionData, 2> conflictStages = { confVsSpv.Value().Reflection, confPsSpv.Value().Reflection };
            PipelineLayoutBuildOptions confOpts{};
            confOpts.ValidateStageInterfaces = false;
            auto confRes = PipelineLayoutBuilder::BuildFromReflection(conflictStages, confOpts);
            EXPECT_TRUE(!confRes.IsSuccess());
        }
    }

    void RunBackendDeviceTests(
        BackendAPI api,
        const char* apiLabel,
        ShaderCompiler& compiler,
        const CompiledShaderArtifact& simpleVs,
        const CompiledShaderArtifact& simplePs,
        const CompiledShaderArtifact& secondVs,
        const CompiledShaderArtifact& secondPs,
        const CompiledShaderArtifact& simpleCs,
        const CompiledShaderArtifact& pbrVs,
        const CompiledShaderArtifact& pbrPs)
    {
        (void)compiler;
        std::cout << "[Test] 3. Backend Device Suite [" << apiLabel << "]\n";

        TestDeviceContext ctx = CreateTestDevice(api);
        if (!ctx.Device)
        {
            std::cerr << "[FAIL] Unable to create " << apiLabel << " device for WP3 tests.\n";
            ++g_failedAssertions;
            return;
        }
        IDevice& dev = *ctx.Device;
        const PipelineLayoutLimits limits = dev.GetPipelineLayoutLimits();

        // 1. Default bindless layout validity
        PipelineLayoutHandle defaultLayout = dev.GetDefaultBindlessPipelineLayout();
        EXPECT_TRUE(defaultLayout.IsValid(), apiLabel);
        EXPECT_TRUE(dev.IsResourceAlive(defaultLayout), apiLabel);

        // 2. Cache hit + reference-counted destruction
        PipelineLayoutDesc cacheDescA{};
        cacheDescA.Mode = PipelineLayoutMode::Standalone;
        cacheDescA.DebugName = "CacheLayout_A";
        cacheDescA.PushConstants.push_back(PipelinePushConstantRange{
            .Name = "Push",
            .OffsetBytes = 0,
            .SizeBytes = 32,
            .Visibility = ShaderVisibility::Vertex | ShaderVisibility::Pixel,
            .DxilRegister = 0,
            .DxilSpace = 0
        });
        cacheDescA.Sets.push_back(DescriptorSetLayoutDesc{
            .SetIndex = 0,
            .Bindings = {
                PipelineBindingLayout{
                    .Name = "SceneCB",
                    .Type = DescriptorType::UniformBuffer,
                    .Set = 0,
                    .Binding = 1,
                    .DxilRegister = 1,
                    .DxilSpace = 0,
                    .Count = 1,
                    .Visibility = ShaderVisibility::All
                },
                PipelineBindingLayout{
                    .Name = "Tex0",
                    .Type = DescriptorType::SampledTexture,
                    .Set = 0,
                    .Binding = 0,
                    .DxilRegister = 0,
                    .DxilSpace = 0,
                    .Count = 2,
                    .Visibility = ShaderVisibility::Pixel
                }
            }
        });
        cacheDescA.StaticSamplers.push_back(StaticSamplerDesc{
            .Name = "StaticSmp",
            .Set = 0,
            .Binding = 2,
            .DxilRegister = 0,
            .DxilSpace = 0,
            .Filter = StaticSamplerFilter::Linear,
            .AddressMode = StaticSamplerAddressMode::ClampToEdge,
            .Visibility = ShaderVisibility::Pixel
        });

        PipelineLayoutDesc cacheDescB = cacheDescA;
        cacheDescB.DebugName = "CacheLayout_B_Reordered";
        std::reverse(cacheDescB.Sets[0].Bindings.begin(), cacheDescB.Sets[0].Bindings.end());

        auto resA = dev.CreatePipelineLayout(cacheDescA);
        EXPECT_TRUE(resA.IsSuccess(), !resA.IsSuccess() ? resA.Error() : apiLabel);
        if (resA)
        {
            const u64 prevHits = dev.GetPipelineLayoutStats().CacheHitCount;
            auto resB = dev.CreatePipelineLayout(cacheDescB);
            EXPECT_TRUE(resB.IsSuccess(), apiLabel);
            if (resB)
            {
                EXPECT_TRUE(resA.Value() == resB.Value(), apiLabel);
                EXPECT_TRUE(dev.GetPipelineLayoutStats().CacheHitCount == prevHits + 1, apiLabel);

                // Structurally different descriptor must NOT reuse cache handle
                PipelineLayoutDesc diffDesc = cacheDescA;
                diffDesc.Sets[0].Bindings[0].Type = DescriptorType::RWStorageBuffer;
                diffDesc.Sets[0].Bindings[0].Binding = 5;
                diffDesc.Sets[0].Bindings[0].DxilRegister = 5;
                auto resDiff = dev.CreatePipelineLayout(diffDesc);
                EXPECT_TRUE(resDiff.IsSuccess(), apiLabel);
                if (resDiff)
                {
                    EXPECT_TRUE(resDiff.Value() != resA.Value(), apiLabel);
                    dev.DestroyPipelineLayout(resDiff.Value());
                }

                // Destroy ref 1 -> still alive for ref 2!
                dev.DestroyPipelineLayout(resB.Value());
                EXPECT_TRUE(dev.IsResourceAlive(resA.Value()), apiLabel);
            }
            dev.DestroyPipelineLayout(resA.Value());
            dev.WaitIdle();
            dev.BeginFrame();
            dev.EndFrameAndPresent();
            dev.WaitIdle();
        }

        // 3. Backend-specific limit rejections
        if (api == BackendAPI::DirectX12)
        {
            // > 64 DWORD Root Signature must fail
            PipelineLayoutDesc over64{};
            over64.Mode = PipelineLayoutMode::Standalone;
            over64.PushConstants.push_back(PipelinePushConstantRange{
                .Name = "MaxPush",
                .OffsetBytes = 0,
                .SizeBytes = 128,
                .Visibility = ShaderVisibility::All,
                .DxilRegister = 0,
                .DxilSpace = 0
            });
            DescriptorSetLayoutDesc cbvSet{};
            cbvSet.SetIndex = 0;
            for (u32 i = 1; i <= 17; ++i)
            {
                cbvSet.Bindings.push_back(PipelineBindingLayout{
                    .Name = "RootCB_" + std::to_string(i),
                    .Type = DescriptorType::UniformBuffer,
                    .Set = 0,
                    .Binding = i,
                    .DxilRegister = i,
                    .DxilSpace = 0,
                    .Count = 1,
                    .PreferRootDescriptorOnDx12 = true,
                    .Visibility = ShaderVisibility::All
                });
            }
            over64.Sets.push_back(cbvSet);
            EXPECT_TRUE(!dev.CreatePipelineLayout(over64).IsSuccess(), "DX12 >64 DWORDs must reject");

            // Exact 64 DWORDs (32 DWORD push + 16 Root CBVs * 2 = 64 DWORDs) must succeed
            PipelineLayoutDesc exact64 = over64;
            exact64.Sets[0].Bindings.pop_back();
            auto exactRes = dev.CreatePipelineLayout(exact64);
            EXPECT_TRUE(exactRes.IsSuccess(), !exactRes.IsSuccess() ? exactRes.Error() : "DX12 exact 64 DWORDs");
            if (exactRes)
            {
                dev.DestroyPipelineLayout(exactRes.Value());
            }

            // CombinedImageSampler must fail on DX12
            PipelineLayoutDesc combinedDx12{};
            combinedDx12.Mode = PipelineLayoutMode::Standalone;
            combinedDx12.Sets.push_back(DescriptorSetLayoutDesc{
                .SetIndex = 0,
                .Bindings = {
                    PipelineBindingLayout{
                        .Name = "Combined",
                        .Type = DescriptorType::CombinedImageSampler,
                        .Set = 0,
                        .Binding = 0,
                        .Count = 1,
                        .Visibility = ShaderVisibility::Pixel
                    }
                }
            });
            EXPECT_TRUE(!dev.CreatePipelineLayout(combinedDx12).IsSuccess(), "DX12 CombinedImageSampler must reject");
        }
        else if (api == BackendAPI::Vulkan)
        {
            // MaxBoundDescriptorSets + 1 must fail
            PipelineLayoutDesc tooManySets{};
            tooManySets.Mode = PipelineLayoutMode::Standalone;
            tooManySets.Sets.push_back(DescriptorSetLayoutDesc{
                .SetIndex = limits.MaxBoundDescriptorSets,
                .Bindings = {
                    PipelineBindingLayout{
                        .Name = "SetBeyondMax",
                        .Type = DescriptorType::UniformBuffer,
                        .Set = limits.MaxBoundDescriptorSets,
                        .Binding = 0,
                        .Count = 1,
                        .Visibility = ShaderVisibility::Vertex
                    }
                }
            });
            EXPECT_TRUE(!dev.CreatePipelineLayout(tooManySets).IsSuccess(), "Vulkan MaxBoundDescriptorSets+1 must reject");

            // Per-stage sampled images > limit must fail
            PipelineLayoutDesc tooManyImages{};
            tooManyImages.Mode = PipelineLayoutMode::Standalone;
            tooManyImages.Sets.push_back(DescriptorSetLayoutDesc{
                .SetIndex = 0,
                .Bindings = {
                    PipelineBindingLayout{
                        .Name = "TooManySampled",
                        .Type = DescriptorType::SampledTexture,
                        .Set = 0,
                        .Binding = 0,
                        .Count = limits.MaxPerStageSampledImages + 1u,
                        .Visibility = ShaderVisibility::Pixel
                    }
                }
            });
            EXPECT_TRUE(!dev.CreatePipelineLayout(tooManyImages).IsSuccess(), "Vulkan MaxPerStageSampledImages+1 must reject");

            // Push constants > MaxPushConstantsBytes (portable 128B) must fail
            PipelineLayoutDesc tooLargePush{};
            tooLargePush.Mode = PipelineLayoutMode::Standalone;
            tooLargePush.PushConstants.push_back(PipelinePushConstantRange{
                .Name = "OversizePush",
                .OffsetBytes = 0,
                .SizeBytes = limits.MaxPushConstantsBytes + 4u,
                .Visibility = ShaderVisibility::Vertex
            });
            EXPECT_TRUE(!dev.CreatePipelineLayout(tooLargePush).IsSuccess(), "Vulkan PushConstants > limit must reject");

            // Set Index gap (Set 0 omitted, Set 2 defined) -> must allocate empty Set 1 and succeed
            PipelineLayoutDesc gapLayout{};
            gapLayout.Mode = PipelineLayoutMode::Standalone;
            gapLayout.PushConstants.push_back(PipelinePushConstantRange{
                .Name = "Push",
                .OffsetBytes = 0,
                .SizeBytes = 32,
                .Visibility = ShaderVisibility::Vertex | ShaderVisibility::Pixel
            });
            gapLayout.Sets.push_back(DescriptorSetLayoutDesc{
                .SetIndex = 2,
                .Bindings = {
                    PipelineBindingLayout{
                        .Name = "SparseCB",
                        .Type = DescriptorType::UniformBuffer,
                        .Set = 2,
                        .Binding = 0,
                        .Count = 1,
                        .Visibility = ShaderVisibility::Vertex
                    }
                }
            });
            auto gapRes = dev.CreatePipelineLayout(gapLayout);
            EXPECT_TRUE(gapRes.IsSuccess(), !gapRes.IsSuccess() ? gapRes.Error() : "Vulkan set gap failed");
            if (gapRes)
            {
                dev.DestroyPipelineLayout(gapRes.Value());
            }
        }

        // 4. Stale / PendingDestroy PipelineLayoutHandle protection in CreateGraphicsPipeline
        if (api != BackendAPI::Null)
        {
            const std::array<ShaderReflectionData, 2> simpleStages = { simpleVs.Reflection, simplePs.Reflection };
            auto builtSimple = PipelineLayoutBuilder::BuildFromReflection(simpleStages);
            EXPECT_TRUE(builtSimple.IsSuccess(), apiLabel);
            if (builtSimple)
            {
                auto tempLayoutRes = dev.CreatePipelineLayout(builtSimple.Value());
                EXPECT_TRUE(tempLayoutRes.IsSuccess(), apiLabel);
                if (tempLayoutRes)
                {
                    PipelineLayoutHandle deadLayout = tempLayoutRes.Value();
                    dev.DestroyPipelineLayout(deadLayout);
                    dev.WaitIdle();
                    dev.BeginFrame();
                    dev.EndFrameAndPresent();
                    dev.WaitIdle();
                    EXPECT_TRUE(!dev.IsResourceAlive(deadLayout), apiLabel);

                    GraphicsPipelineDesc stalePsoDesc{};
                    stalePsoDesc.VertexShaderBytecode = simpleVs.Bytecode;
                    stalePsoDesc.PixelShaderBytecode = simplePs.Bytecode;
                    stalePsoDesc.PipelineLayout = deadLayout;
                    stalePsoDesc.ColorTargetFormat = Format::RGBA8_UNorm;
                    stalePsoDesc.DebugName = "StaleLayoutPSO";
                    PipelineHandle stalePipe = dev.CreateGraphicsPipeline(stalePsoDesc);
                    EXPECT_TRUE(!stalePipe.IsValid(), apiLabel);
                }
            }
        }

        // 5. Real Graphics + Compute Pipelines, Layout Switching & GPU Execution + Deferred Destruction
        if (api != BackendAPI::Null)
        {
            // Build Layout A (32B PushConstants) and Layout B (64B PushConstants) + Compute Layout + Hybrid PBR Layout
            PipelineLayoutBuildOptions optA{};
            optA.Policy = CrossBackendBindingPolicy::MorbiBindless();
            optA.Mode = PipelineLayoutMode::Standalone;
            optA.DebugName = "LayoutA_32B";
            auto layoutDescA = PipelineLayoutBuilder::BuildFromReflection(
                std::array<ShaderReflectionData, 2>{ simpleVs.Reflection, simplePs.Reflection }, optA);
            EXPECT_TRUE(layoutDescA.IsSuccess(), apiLabel);

            PipelineLayoutBuildOptions optB = optA;
            optB.DebugName = "LayoutB_64B";
            auto layoutDescB = PipelineLayoutBuilder::BuildFromReflection(
                std::array<ShaderReflectionData, 2>{ secondVs.Reflection, secondPs.Reflection }, optB);
            EXPECT_TRUE(layoutDescB.IsSuccess(), apiLabel);

            PipelineLayoutBuildOptions optCs = optA;
            optCs.DebugName = "LayoutCS_16B";
            auto layoutDescCs = PipelineLayoutBuilder::BuildFromReflection(
                std::array<ShaderReflectionData, 1>{ simpleCs.Reflection }, optCs);
            EXPECT_TRUE(layoutDescCs.IsSuccess(), apiLabel);

            PipelineLayoutBuildOptions optHybrid = optA;
            optHybrid.Mode = PipelineLayoutMode::GlobalBindlessHybrid;
            optHybrid.UseRootDescriptorForSingleDynamicCbv = true;
            optHybrid.DebugName = "LayoutHybridPBR";
            auto layoutDescHybrid = PipelineLayoutBuilder::BuildFromReflection(
                std::array<ShaderReflectionData, 2>{ pbrVs.Reflection, pbrPs.Reflection }, optHybrid);
            EXPECT_TRUE(layoutDescHybrid.IsSuccess(), apiLabel);

            if (layoutDescA && layoutDescB && layoutDescCs && layoutDescHybrid)
            {
                auto handleARes = dev.CreatePipelineLayout(layoutDescA.Value());
                auto handleBRes = dev.CreatePipelineLayout(layoutDescB.Value());
                auto handleCsRes = dev.CreatePipelineLayout(layoutDescCs.Value());
                auto handleHybridRes = dev.CreatePipelineLayout(layoutDescHybrid.Value());

                EXPECT_TRUE(handleARes.IsSuccess() && handleBRes.IsSuccess() && handleCsRes.IsSuccess() && handleHybridRes.IsSuccess(), apiLabel);
                if (handleARes && handleBRes && handleCsRes && handleHybridRes)
                {
                    GraphicsPipelineDesc psoDescA{};
                    psoDescA.VertexShaderBytecode = simpleVs.Bytecode;
                    psoDescA.PixelShaderBytecode = simplePs.Bytecode;
                    psoDescA.PipelineLayout = handleARes.Value();
                    psoDescA.ColorTargetFormat = Format::RGBA8_UNorm;
                    psoDescA.DebugName = "PipeA_32B";

                    GraphicsPipelineDesc psoDescB{};
                    psoDescB.VertexShaderBytecode = secondVs.Bytecode;
                    psoDescB.PixelShaderBytecode = secondPs.Bytecode;
                    psoDescB.PipelineLayout = handleBRes.Value();
                    psoDescB.ColorTargetFormat = Format::RGBA8_UNorm;
                    psoDescB.DebugName = "PipeB_64B";

                    ComputePipelineDesc psoDescCs{};
                    psoDescCs.ComputeShaderBytecode = simpleCs.Bytecode;
                    psoDescCs.PipelineLayout = handleCsRes.Value();
                    psoDescCs.DebugName = "PipeCS_16B";

                    GraphicsPipelineDesc psoDescHybrid{};
                    psoDescHybrid.VertexShaderBytecode = pbrVs.Bytecode;
                    psoDescHybrid.PixelShaderBytecode = pbrPs.Bytecode;
                    psoDescHybrid.PipelineLayout = handleHybridRes.Value();
                    psoDescHybrid.ColorTargetFormat = Format::RGBA8_UNorm;
                    psoDescHybrid.DebugName = "PipeHybridPBR";

                    PipelineHandle pipeA = dev.CreateGraphicsPipeline(psoDescA);
                    PipelineHandle pipeB = dev.CreateGraphicsPipeline(psoDescB);
                    PipelineHandle pipeCs = dev.CreateComputePipeline(psoDescCs);
                    PipelineHandle pipeHybrid = dev.CreateGraphicsPipeline(psoDescHybrid);

                    EXPECT_TRUE(pipeA.IsValid(), apiLabel);
                    EXPECT_TRUE(pipeB.IsValid(), apiLabel);
                    EXPECT_TRUE(pipeCs.IsValid(), apiLabel);
                    EXPECT_TRUE(pipeHybrid.IsValid(), apiLabel);

                    // Create offscreen RGBA8 RenderTarget and execute Graphics (switching layouts A -> B) + Compute Dispatch!
                    TextureHandle rt = dev.CreateTexture(TextureDesc{
                        .Width = 64,
                        .Height = 64,
                        .PixelFormat = Format::RGBA8_UNorm,
                        .RenderTarget = true,
                        .DebugName = "WP3_OffscreenRT"
                    });
                    EXPECT_TRUE(rt.IsValid(), apiLabel);

                    if (pipeA.IsValid() && pipeB.IsValid() && pipeCs.IsValid() && rt.IsValid())
                    {
                        auto cmd = dev.CreateCommandList();
                        EXPECT_TRUE(cmd != nullptr, apiLabel);
                        if (cmd)
                        {
                            cmd->Begin();
                            cmd->TransitionTexture(rt, ResourceState::Undefined, ResourceState::RenderTarget);

                            const std::array<RenderAttachmentDesc, 1> colorAtts = {{
                                RenderAttachmentDesc{
                                    .Texture = rt,
                                    .ClearOnLoad = true,
                                    .ClearColor = { 0.1f, 0.2f, 0.3f, 1.0f }
                                }
                            }};
                            DynamicRenderPassDesc pass{};
                            pass.ColorAttachments = colorAtts;
                            cmd->BeginRendering(pass);

                            // Draw with Layout A (32B push)
                            cmd->SetPipeline(pipeA);
                            const std::array<float, 8> push32 = { 1.0f, 0.5f, 0.25f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
                            cmd->PushConstants(push32.data(), sizeof(push32), 0);
                            cmd->Draw(3, 1, 0, 0);

                            // Switch to Layout B (64B push) within same render pass & command list!
                            cmd->SetPipeline(pipeB);
                            const std::array<float, 16> push64 = {
                                0.5f, 0.5f, 0.5f, 1.0f,
                                0.25f, 0.25f, 0.25f, 0.0f,
                                1.0f, 1.0f, 1.0f, 1.0f,
                                0.1f, 0.2f, 0.3f, 0.0f
                            };
                            cmd->PushConstants(push64.data(), sizeof(push64), 0);
                            cmd->Draw(3, 1, 0, 0);

                            cmd->EndRendering();
                            cmd->TransitionTexture(rt, ResourceState::RenderTarget, ResourceState::ShaderResource);

                            // Switch to Compute Pipeline & Layout CS + PushConstants + Dispatch
                            cmd->SetPipeline(pipeCs);
                            const std::array<u32, 4> csPush = { 10u, 20u, 30u, 40u };
                            cmd->PushConstants(csPush.data(), sizeof(csPush), 0);
                            cmd->Dispatch(1, 1, 1);

                            cmd->End();
                            const u64 timelineVal = dev.SubmitCommandList(*cmd);
                            EXPECT_TRUE(timelineVal > 0, apiLabel);
                            dev.WaitIdle();
                        }

                        // Test deferred destruction while live pipeline still references handleARes
                        PipelineLayoutHandle layoutA = handleARes.Value();
                        dev.DestroyPipelineLayout(layoutA);
                        dev.WaitIdle();
                        dev.BeginFrame();
                        dev.EndFrameAndPresent();
                        dev.WaitIdle();
                        // MUST remain alive because live pipeA holds PipelineRefCount == 1!
                        EXPECT_TRUE(dev.IsResourceAlive(layoutA), "Live pipeline must keep layout alive");

                        // Record an unsubmitted secondary command list holding pipeA / layoutA
                        auto unsubmittedCmd = dev.CreateCommandList();
                        EXPECT_TRUE(unsubmittedCmd != nullptr, apiLabel);
                        if (unsubmittedCmd)
                        {
                            unsubmittedCmd->Begin();
                            unsubmittedCmd->SetPipeline(pipeA);

                            // Destroy pipeA now, while unsubmittedCmd still holds UnsubmittedRefCount!
                            dev.DestroyPipeline(pipeA);
                            dev.WaitIdle();
                            dev.BeginFrame();
                            dev.EndFrameAndPresent();
                            dev.WaitIdle();
                            // Layout must STILL stay alive while unsubmittedCmd holds a ref!
                            EXPECT_TRUE(dev.IsResourceAlive(layoutA), "Unsubmitted cmdlist must keep layout alive");

                            unsubmittedCmd->Discard();
                        }

                        // After Discard() + WaitIdle() + frame pump, pipeA and layoutA drain cleanly!
                        dev.WaitIdle();
                        dev.BeginFrame();
                        dev.EndFrameAndPresent();
                        dev.WaitIdle();
                        EXPECT_TRUE(!dev.IsResourceAlive(pipeA), apiLabel);
                        EXPECT_TRUE(!dev.IsResourceAlive(layoutA), apiLabel);
                    }
                    else
                    {
                        dev.DestroyPipeline(pipeA);
                        dev.DestroyPipelineLayout(handleARes.Value());
                    }

                    dev.DestroyTexture(rt);
                    dev.DestroyPipeline(pipeB);
                    dev.DestroyPipeline(pipeCs);
                    dev.DestroyPipeline(pipeHybrid);
                    dev.DestroyPipelineLayout(handleBRes.Value());
                    dev.DestroyPipelineLayout(handleCsRes.Value());
                    dev.DestroyPipelineLayout(handleHybridRes.Value());
                    dev.WaitIdle();
                }
            }
        }
        else // Null device pipeline lifecycle + deferred destruction test
        {
            PipelineLayoutDesc nullLayoutDesc{};
            nullLayoutDesc.Mode = PipelineLayoutMode::Standalone;
            nullLayoutDesc.PushConstants.push_back(PipelinePushConstantRange{
                .Name = "Push",
                .OffsetBytes = 0,
                .SizeBytes = 32,
                .Visibility = ShaderVisibility::Vertex | ShaderVisibility::Pixel,
                .DxilRegister = 0,
                .DxilSpace = 0
            });
            auto layoutRes = dev.CreatePipelineLayout(nullLayoutDesc);
            EXPECT_TRUE(layoutRes.IsSuccess(), "Null CreatePipelineLayout");
            if (layoutRes)
            {
                GraphicsPipelineDesc pipeDesc{};
                pipeDesc.VertexShaderBytecode = simpleVs.Bytecode;
                pipeDesc.PixelShaderBytecode = simplePs.Bytecode;
                pipeDesc.PipelineLayout = layoutRes.Value();
                PipelineHandle pipe = dev.CreateGraphicsPipeline(pipeDesc);
                EXPECT_TRUE(pipe.IsValid(), "Null CreateGraphicsPipeline");

                dev.DestroyPipelineLayout(layoutRes.Value());
                dev.BeginFrame();
                dev.EndFrameAndPresent();
                EXPECT_TRUE(dev.IsResourceAlive(layoutRes.Value()), "Null live pipeline keeps layout alive");

                dev.DestroyPipeline(pipe);
                dev.BeginFrame();
                dev.EndFrameAndPresent();
                EXPECT_TRUE(!dev.IsResourceAlive(layoutRes.Value()), "Null layout freed after pipeline destroyed");
            }
        }

        DestroyTestDevice(ctx);
    }
}

int main()
{
    ShaderCompiler compiler;
    if (!compiler.IsDXCAvailable())
    {
        std::cerr << "[FATAL] DXC compiler is not available for MorbiPipelineLayoutTests.\n";
        return 1;
    }

    RunLayoutModelAndValidationTests();
    RunShaderReflectionLayoutBuilderTests(compiler);

    // Pre-compile dual-target shaders for Null, Vulkan, and DX12 device tests
    auto simpleVsSpv = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::SPIRV);
    auto simplePsSpv = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);
    auto secondVsSpv = CompileDualShader(compiler, kSecondPushGraphicsHlsl, "SecondPushVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::SPIRV);
    auto secondPsSpv = CompileDualShader(compiler, kSecondPushGraphicsHlsl, "SecondPushPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);
    auto simpleCsSpv = CompileDualShader(compiler, kSimplePushComputeHlsl, "SimplePushCS.hlsl", "CSMain", ShaderStage::Compute, ShaderTargetFormat::SPIRV);
    auto pbrVsSpv    = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPbrVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::SPIRV);
    auto pbrPsSpv    = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPbrPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::SPIRV);

    auto simpleVsDxil = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::DXIL);
    auto simplePsDxil = CompileDualShader(compiler, kSimplePushGraphicsHlsl, "SimplePushPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::DXIL);
    auto secondVsDxil = CompileDualShader(compiler, kSecondPushGraphicsHlsl, "SecondPushVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::DXIL);
    auto secondPsDxil = CompileDualShader(compiler, kSecondPushGraphicsHlsl, "SecondPushPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::DXIL);
    auto simpleCsDxil = CompileDualShader(compiler, kSimplePushComputeHlsl, "SimplePushCS.hlsl", "CSMain", ShaderStage::Compute, ShaderTargetFormat::DXIL);
    auto pbrVsDxil    = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPbrVS.hlsl", "VSMain", ShaderStage::Vertex, ShaderTargetFormat::DXIL);
    auto pbrPsDxil    = CompileDualShader(compiler, kBindlessHybridPbrHlsl, "HybridPbrPS.hlsl", "PSMain", ShaderStage::Pixel, ShaderTargetFormat::DXIL);

    EXPECT_TRUE(simpleVsSpv && simplePsSpv && secondVsSpv && secondPsSpv && simpleCsSpv && pbrVsSpv && pbrPsSpv);
    EXPECT_TRUE(simpleVsDxil && simplePsDxil && secondVsDxil && secondPsDxil && simpleCsDxil && pbrVsDxil && pbrPsDxil);

    if (simpleVsSpv && simplePsSpv && secondVsSpv && secondPsSpv && simpleCsSpv && pbrVsSpv && pbrPsSpv)
    {
        RunBackendDeviceTests(
            BackendAPI::Null, "Null", compiler,
            simpleVsSpv.Value(), simplePsSpv.Value(),
            secondVsSpv.Value(), secondPsSpv.Value(),
            simpleCsSpv.Value(),
            pbrVsSpv.Value(), pbrPsSpv.Value());

        RunBackendDeviceTests(
            BackendAPI::Vulkan, "Vulkan", compiler,
            simpleVsSpv.Value(), simplePsSpv.Value(),
            secondVsSpv.Value(), secondPsSpv.Value(),
            simpleCsSpv.Value(),
            pbrVsSpv.Value(), pbrPsSpv.Value());
    }

    if (simpleVsDxil && simplePsDxil && secondVsDxil && secondPsDxil && simpleCsDxil && pbrVsDxil && pbrPsDxil)
    {
        RunBackendDeviceTests(
            BackendAPI::DirectX12, "DirectX12", compiler,
            simpleVsDxil.Value(), simplePsDxil.Value(),
            secondVsDxil.Value(), secondPsDxil.Value(),
            simpleCsDxil.Value(),
            pbrVsDxil.Value(), pbrPsDxil.Value());
    }

    std::cout << "[MorbiPipelineLayoutTests] Passed: " << g_passedAssertions
              << ", Failed: " << g_failedAssertions << "\n";
    return (g_failedAssertions == 0) ? 0 : 1;
}
