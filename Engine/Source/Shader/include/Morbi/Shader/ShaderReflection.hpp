#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::Shader
{
    enum class ShaderStage : u8
    {
        Vertex = 0,
        Pixel,
        Compute
    };

    enum class ShaderTargetFormat : u8
    {
        DXIL = 0,  // DirectX 12 SM 6.0-6.6 (-T vs_6_X / ps_6_X / cs_6_X)
        SPIRV      // Vulkan 1.3 (-spirv -fspv-target-env=vulkan1.3)
    };

    enum class ResourceBindingType : u8
    {
        ConstantBuffer = 0,
        ShaderResource,
        UnorderedAccess,
        Sampler,
        PushConstants
    };

    inline constexpr u32 kUnboundedDescriptorCount = 0xFFFFFFFFu;

    enum class ReflectedResourceKind : u8
    {
        Unknown = 0,
        UniformBuffer,
        StorageBuffer,
        RWStorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
        AccelerationStructure,
        PushConstants
    };

    enum class ReflectedResourceDimension : u8
    {
        Unknown = 0,
        Buffer,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D,
        TextureCube,
        TextureCubeArray
    };

    enum class ReflectedScalarType : u8
    {
        Unknown = 0,
        Bool,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float16,
        Float32,
        Float64,
        Struct
    };

    enum class ReflectedVariableClass : u8
    {
        Unknown = 0,
        Scalar,
        Vector,
        MatrixRows,
        MatrixColumns,
        Struct
    };

    enum class ReflectedShaderVisibility : u32
    {
        None    = 0,
        Vertex  = 1u << 0,
        Pixel   = 1u << 1,
        Compute = 1u << 2,
        All     = Vertex | Pixel | Compute
    };

    [[nodiscard]] constexpr ReflectedShaderVisibility operator|(
        ReflectedShaderVisibility a,
        ReflectedShaderVisibility b) noexcept
    {
        return static_cast<ReflectedShaderVisibility>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    [[nodiscard]] constexpr ReflectedShaderVisibility operator&(
        ReflectedShaderVisibility a,
        ReflectedShaderVisibility b) noexcept
    {
        return static_cast<ReflectedShaderVisibility>(static_cast<u32>(a) & static_cast<u32>(b));
    }

    [[nodiscard]] constexpr ReflectedShaderVisibility VisibilityFromStage(ShaderStage stage) noexcept
    {
        switch (stage)
        {
        case ShaderStage::Vertex:  return ReflectedShaderVisibility::Vertex;
        case ShaderStage::Pixel:   return ReflectedShaderVisibility::Pixel;
        case ShaderStage::Compute: return ReflectedShaderVisibility::Compute;
        }
        return ReflectedShaderVisibility::None;
    }

    struct ReflectedShaderMember
    {
        std::string Name;
        u32 OffsetBytes = 0;
        u32 SizeBytes = 0;
        u32 PaddedSizeBytes = 0;
        ReflectedVariableClass Class = ReflectedVariableClass::Unknown;
        ReflectedScalarType ScalarType = ReflectedScalarType::Unknown;
        u32 Rows = 1;
        u32 Columns = 1;
        u32 ArrayElementCount = 0; // 0 = scalar/vector/matrix/struct (not an array)
        u32 ArrayStrideBytes = 0;
        u32 MatrixStrideBytes = 0;
        bool IsRowMajor = false;
        std::vector<ReflectedShaderMember> Members;

        [[nodiscard]] bool operator==(const ReflectedShaderMember& other) const noexcept = default;
    };

    struct ReflectedConstantBuffer
    {
        std::string Name;
        u32 SizeBytes = 0;
        std::optional<u32> DescriptorSet;
        std::optional<u32> Binding;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;
        std::vector<ReflectedShaderMember> Members;

        [[nodiscard]] bool operator==(const ReflectedConstantBuffer& other) const noexcept = default;
    };

    struct ReflectedPushConstantRange
    {
        std::string Name;
        u32 OffsetBytes = 0;
        u32 SizeBytes = 0;
        ReflectedShaderVisibility Visibility = ReflectedShaderVisibility::None;
        std::vector<ReflectedShaderMember> Members;

        [[nodiscard]] bool operator==(const ReflectedPushConstantRange& other) const noexcept = default;
    };

    struct ReflectedStageParameter
    {
        std::string Name;
        std::string SemanticName;
        u32 SemanticIndex = 0;
        std::optional<u32> Location;
        std::optional<u32> Register;
        ReflectedScalarType ComponentType = ReflectedScalarType::Unknown;
        u32 ComponentCount = 0; // 1..4
        u8 ComponentMask = 0;   // bitmask 0x1..0xF
        bool IsSystemValue = false;
        std::string SystemValueName;

        [[nodiscard]] bool operator==(const ReflectedStageParameter& other) const noexcept = default;
    };

    struct ReflectedBinding
    {
        std::string Name;
        ResourceBindingType Type = ResourceBindingType::ShaderResource;
        ReflectedResourceKind Kind = ReflectedResourceKind::Unknown;
        ReflectedResourceDimension Dimension = ReflectedResourceDimension::Unknown;
        u32 RegisterSlot = 0;
        u32 RegisterSpace = 0;
        u32 Count = 1; // kUnboundedDescriptorCount = unbounded bindless array
        bool IsUnboundedArray = false;
        bool IsReadOnly = true;
        bool IsComparisonSampler = false;
        std::optional<u32> DescriptorSet;
        std::optional<u32> Binding;
        std::optional<u32> DxilRegister;
        std::optional<u32> DxilSpace;
        std::optional<u32> ConstantBufferIndex;
        ReflectedShaderVisibility Visibility = ReflectedShaderVisibility::None;

        [[nodiscard]] bool operator==(const ReflectedBinding& other) const noexcept = default;
    };

    struct ShaderReflectionData
    {
        ShaderTargetFormat TargetFormat = ShaderTargetFormat::DXIL;
        ShaderStage Stage = ShaderStage::Vertex;
        std::string EntryPoint;
        bool IsValid = false;
        u32 PushConstantSizeBytes = 0;
        u32 ThreadGroupSizeX = 1;
        u32 ThreadGroupSizeY = 1;
        u32 ThreadGroupSizeZ = 1;
        bool UsesBindlessHeap = false;
        std::vector<ReflectedBinding> Bindings;
        std::vector<ReflectedConstantBuffer> ConstantBuffers;
        std::vector<ReflectedPushConstantRange> PushConstants;
        std::vector<ReflectedStageParameter> StageInputs;
        std::vector<ReflectedStageParameter> StageOutputs;

        [[nodiscard]] bool operator==(const ShaderReflectionData& other) const noexcept = default;
    };

    struct CrossBackendBindingRule
    {
        ResourceBindingType DxilRegisterClass = ResourceBindingType::ShaderResource;
        u32 DxilSpace = 0;
        u32 DxilRegister = 0;
        u32 VulkanSet = 0;
        u32 VulkanBinding = 0;
        bool AllowUnboundedArrayToSingleCbv = false;
    };

    struct CrossBackendRootConstantRule
    {
        bool Enabled = false;
        u32 DxilSpace = 0;
        u32 DxilRegister = 0;
        u32 MaxSizeBytes = 128;
    };

    struct CrossBackendBindingPolicy
    {
        bool RequireMatchingVertexInputSemantics = false;
        bool AllowMatrixMajornessRepresentationDifference = true;
        bool FallbackToIdentitySetSpaceMapping = true;
        CrossBackendRootConstantRule RootConstantRule{};
        std::vector<CrossBackendBindingRule> ExplicitBindingRules;

        [[nodiscard]] static CrossBackendBindingPolicy StrictOneToOne() noexcept
        {
            return CrossBackendBindingPolicy{
                .RequireMatchingVertexInputSemantics = false,
                .AllowMatrixMajornessRepresentationDifference = true,
                .FallbackToIdentitySetSpaceMapping = true,
                .RootConstantRule = {},
                .ExplicitBindingRules = {}
            };
        }

        [[nodiscard]] static CrossBackendBindingPolicy MorbiBindless()
        {
            CrossBackendBindingPolicy policy{};
            policy.RequireMatchingVertexInputSemantics = false;
            policy.AllowMatrixMajornessRepresentationDifference = true;
            policy.FallbackToIdentitySetSpaceMapping = true;
            policy.RootConstantRule = CrossBackendRootConstantRule{
                .Enabled = true,
                .DxilSpace = 0,
                .DxilRegister = 0,
                .MaxSizeBytes = 128
            };
            policy.ExplicitBindingRules = {
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::ShaderResource, .DxilSpace = 0, .DxilRegister = 0, .VulkanSet = 0, .VulkanBinding = 0, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::ShaderResource, .DxilSpace = 1, .DxilRegister = 0, .VulkanSet = 0, .VulkanBinding = 1, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::Sampler,        .DxilSpace = 0, .DxilRegister = 0, .VulkanSet = 0, .VulkanBinding = 2, .AllowUnboundedArrayToSingleCbv = false },
                CrossBackendBindingRule{ .DxilRegisterClass = ResourceBindingType::ConstantBuffer, .DxilSpace = 0, .DxilRegister = 1, .VulkanSet = 0, .VulkanBinding = 3, .AllowUnboundedArrayToSingleCbv = true }
            };
            return policy;
        }
    };

    struct ReflectionValidationIssue
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

    struct ReflectionValidationResult
    {
        bool IsCompatible = true;
        std::vector<ReflectionValidationIssue> Issues;

        void AddError(std::string code, std::string message)
        {
            IsCompatible = false;
            Issues.push_back(ReflectionValidationIssue{
                .Level = ReflectionValidationIssue::Severity::Error,
                .Code = std::move(code),
                .Message = std::move(message)
            });
        }

        void AddWarning(std::string code, std::string message)
        {
            Issues.push_back(ReflectionValidationIssue{
                .Level = ReflectionValidationIssue::Severity::Warning,
                .Code = std::move(code),
                .Message = std::move(message)
            });
        }
    };

    class MORBI_API ShaderReflectionValidator
    {
    public:
        [[nodiscard]] static ReflectionValidationResult ValidateStageInterface(
            const ShaderReflectionData& producerStage,
            const ShaderReflectionData& consumerStage);

        [[nodiscard]] static ReflectionValidationResult ValidateCrossBackendCompatibility(
            const ShaderReflectionData& spirvReflection,
            const ShaderReflectionData& dxilReflection,
            const CrossBackendBindingPolicy& policy = CrossBackendBindingPolicy::StrictOneToOne());
    };
}
