#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/RHITypes.hpp"

namespace Morbi::RHI
{
    inline constexpr u32 kUnboundedDescriptorCount = 0xFFFFFFFFu;
    inline constexpr u32 kMaxPortablePushConstantsBytes = 128u;
    inline constexpr u32 kMaxDx12RootSignatureDwords = 64u;

    enum class ShaderVisibility : u32
    {
        None        = 0,
        Vertex      = 1u << 0,
        Pixel       = 1u << 1,
        Compute     = 1u << 2,
        AllGraphics = Vertex | Pixel,
        All         = Vertex | Pixel | Compute
    };

    [[nodiscard]] constexpr ShaderVisibility operator|(ShaderVisibility a, ShaderVisibility b) noexcept
    {
        return static_cast<ShaderVisibility>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    [[nodiscard]] constexpr ShaderVisibility operator&(ShaderVisibility a, ShaderVisibility b) noexcept
    {
        return static_cast<ShaderVisibility>(static_cast<u32>(a) & static_cast<u32>(b));
    }

    constexpr ShaderVisibility& operator|=(ShaderVisibility& a, ShaderVisibility b) noexcept
    {
        a = a | b;
        return a;
    }

    [[nodiscard]] constexpr bool HasVisibility(ShaderVisibility mask, ShaderVisibility bit) noexcept
    {
        return (static_cast<u32>(mask) & static_cast<u32>(bit)) != 0u;
    }

    enum class DescriptorType : u8
    {
        UniformBuffer = 0,
        StorageBuffer,
        RWStorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
        AccelerationStructure
    };

    enum class PipelineLayoutMode : u8
    {
        Standalone = 0,
        GlobalBindlessHybrid
    };

    enum class StaticSamplerFilter : u8
    {
        Nearest = 0,
        Linear,
        Anisotropic
    };

    enum class StaticSamplerAddressMode : u8
    {
        Repeat = 0,
        ClampToEdge,
        ClampToBorder,
        MirrorRepeat
    };

    struct PipelineBindingLayout
    {
        std::string Name;
        DescriptorType Type = DescriptorType::SampledTexture;
        u32 Set = 0;
        u32 Binding = 0;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;
        u32 Count = 1;
        bool IsUnboundedArray = false;
        bool IsReadOnly = true;
        bool IsComparisonSampler = false;
        bool PreferRootDescriptorOnDx12 = false;
        ShaderVisibility Visibility = ShaderVisibility::All;

        [[nodiscard]] bool operator==(const PipelineBindingLayout& other) const noexcept = default;
    };

    struct DescriptorSetLayoutDesc
    {
        u32 SetIndex = 0;
        bool AllowUpdateAfterBind = false;
        std::vector<PipelineBindingLayout> Bindings;

        [[nodiscard]] bool operator==(const DescriptorSetLayoutDesc& other) const noexcept = default;
    };

    struct StaticSamplerDesc
    {
        std::string Name;
        u32 Set = 0;
        u32 Binding = 0;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;
        StaticSamplerFilter Filter = StaticSamplerFilter::Linear;
        StaticSamplerAddressMode AddressMode = StaticSamplerAddressMode::Repeat;
        bool IsComparison = false;
        ShaderVisibility Visibility = ShaderVisibility::All;

        [[nodiscard]] bool operator==(const StaticSamplerDesc& other) const noexcept = default;
    };

    struct PipelinePushConstantRange
    {
        std::string Name;
        u32 OffsetBytes = 0;
        u32 SizeBytes = 0;
        ShaderVisibility Visibility = ShaderVisibility::All;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;

        [[nodiscard]] bool operator==(const PipelinePushConstantRange& other) const noexcept = default;
    };

    struct PipelineConstantBufferLayout
    {
        std::string Name;
        u32 Set = 0;
        u32 Binding = 0;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;
        u32 SizeBytes = 0;
        u32 MemberCount = 0;
        bool UseRootDescriptor = false;
        ShaderVisibility Visibility = ShaderVisibility::All;

        [[nodiscard]] bool operator==(const PipelineConstantBufferLayout& other) const noexcept = default;
    };

    struct PipelineLayoutDesc
    {
        PipelineLayoutMode Mode = PipelineLayoutMode::Standalone;
        std::vector<DescriptorSetLayoutDesc> Sets;
        std::vector<PipelinePushConstantRange> PushConstants;
        std::vector<PipelineConstantBufferLayout> ConstantBuffers;
        std::vector<StaticSamplerDesc> StaticSamplers;
        std::string DebugName;

        [[nodiscard]] bool operator==(const PipelineLayoutDesc& other) const noexcept = default;
    };

    struct PipelineLayoutLimits
    {
        u32 MaxBoundDescriptorSets = 4;
        u32 MaxPushConstantsBytes = kMaxPortablePushConstantsBytes;
        u32 MaxRootSignatureDwords = kMaxDx12RootSignatureDwords;
        u32 MaxPerStageSamplers = 16;
        u32 MaxPerStageUniformBuffers = 12;
        u32 MaxPerStageStorageBuffers = 4;
        u32 MaxPerStageSampledImages = 16;
        u32 MaxPerStageStorageImages = 4;
        u32 MaxPerStageResources = 128;
        u32 MaxUpdateAfterBindUniformBuffers = 256;
        u32 MaxUpdateAfterBindSampledImages = 4096;
        u32 MaxUpdateAfterBindStorageBuffers = 4096;
        bool SupportsDescriptorIndexing = true;
        bool SupportsRuntimeDescriptorArray = true;
        bool SupportsUpdateAfterBind = true;
    };

    struct PipelineLayoutCostBreakdown
    {
        u32 EstimatedDx12Dwords = 0;
        u32 TotalDescriptorSets = 0;
        u32 TotalBindings = 0;
        u32 TotalStaticSamplers = 0;
        u32 TotalPushConstantBytes = 0;
        u32 MaxStageSamplers = 0;
        u32 MaxStageUniformBuffers = 0;
        u32 MaxStageStorageBuffers = 0;
        u32 MaxStageSampledImages = 0;
        u32 MaxStageStorageImages = 0;
        u32 MaxStageResources = 0;
        bool UsesBindlessRuntimeArray = false;
        bool UsesUpdateAfterBind = false;
    };

    struct PipelineLayoutIssue
    {
        enum class Severity : u8
        {
            Warning = 0,
            Error
        };

        Severity Level = Severity::Error;
        std::string Code;
        std::string Message;
    };

    struct PipelineLayoutValidationResult
    {
        bool IsValid = true;
        PipelineLayoutCostBreakdown Cost{};
        std::vector<PipelineLayoutIssue> Issues;

        void AddError(std::string code, std::string message)
        {
            IsValid = false;
            Issues.push_back(PipelineLayoutIssue{
                .Level = PipelineLayoutIssue::Severity::Error,
                .Code = std::move(code),
                .Message = std::move(message)
            });
        }

        void AddWarning(std::string code, std::string message)
        {
            Issues.push_back(PipelineLayoutIssue{
                .Level = PipelineLayoutIssue::Severity::Warning,
                .Code = std::move(code),
                .Message = std::move(message)
            });
        }
    };

    struct PipelineLayoutStats
    {
        u64 CreatedCount = 0;
        u64 CacheHitCount = 0;
        u64 DestroyedCount = 0;
        u32 ActiveCount = 0;
    };

    class MORBI_API PipelineLayoutValidator
    {
    public:
        [[nodiscard]] static PipelineLayoutDesc Normalize(const PipelineLayoutDesc& desc);
        [[nodiscard]] static u64 ComputeCanonicalHash(const PipelineLayoutDesc& desc) noexcept;
        [[nodiscard]] static bool AreStructurallyEqual(
            const PipelineLayoutDesc& a,
            const PipelineLayoutDesc& b) noexcept;
        [[nodiscard]] static PipelineLayoutValidationResult Validate(
            const PipelineLayoutDesc& desc,
            const PipelineLayoutLimits& limits = PipelineLayoutLimits{},
            BackendAPI targetBackend = BackendAPI::Null);
    };
}
