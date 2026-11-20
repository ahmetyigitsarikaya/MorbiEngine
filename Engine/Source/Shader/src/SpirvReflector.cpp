#include "SpirvReflector.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "spirv_reflect.h"

namespace Morbi::Shader::Detail
{
    namespace
    {
        constexpr u32 kSpirvMagicNumber = 0x07230203u;
        constexpr usize kMinSpirvHeaderBytes = 20u;

        [[nodiscard]] std::string SafeCString(const char* ptr)
        {
            return ptr != nullptr ? std::string(ptr) : std::string{};
        }

        [[nodiscard]] const char* SpvReflectResultName(SpvReflectResult result) noexcept
        {
            switch (result)
            {
            case SPV_REFLECT_RESULT_SUCCESS:                             return "SUCCESS";
            case SPV_REFLECT_RESULT_NOT_READY:                           return "NOT_READY";
            case SPV_REFLECT_RESULT_ERROR_PARSE_FAILED:                  return "PARSE_FAILED";
            case SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED:                  return "ALLOC_FAILED";
            case SPV_REFLECT_RESULT_ERROR_RANGE_EXCEEDED:                return "RANGE_EXCEEDED";
            case SPV_REFLECT_RESULT_ERROR_NULL_POINTER:                  return "NULL_POINTER";
            case SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR:                return "INTERNAL_ERROR";
            case SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH:                return "COUNT_MISMATCH";
            case SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND:             return "ELEMENT_NOT_FOUND";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_CODE_SIZE:       return "SPIRV_INVALID_CODE_SIZE";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_MAGIC_NUMBER:    return "SPIRV_INVALID_MAGIC_NUMBER";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_EOF:          return "SPIRV_UNEXPECTED_EOF";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ID_REFERENCE:    return "SPIRV_INVALID_ID_REFERENCE";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_SET_NUMBER_OVERFLOW:     return "SPIRV_SET_NUMBER_OVERFLOW";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_STORAGE_CLASS:   return "SPIRV_INVALID_STORAGE_CLASS";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_RECURSION:               return "SPIRV_RECURSION";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_INSTRUCTION:     return "SPIRV_INVALID_INSTRUCTION";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_BLOCK_DATA:   return "SPIRV_UNEXPECTED_BLOCK_DATA";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_BLOCK_MEMBER_REFERENCE: return "SPIRV_INVALID_BLOCK_MEMBER_REFERENCE";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ENTRY_POINT:     return "SPIRV_INVALID_ENTRY_POINT";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_EXECUTION_MODE:  return "SPIRV_INVALID_EXECUTION_MODE";
            case SPV_REFLECT_RESULT_ERROR_SPIRV_MAX_RECURSIVE_EXCEEDED:  return "SPIRV_MAX_RECURSIVE_EXCEEDED";
            }
            return "UNKNOWN_ERROR";
        }

        class ScopedSpvReflectModule final
        {
        public:
            ScopedSpvReflectModule() = default;
            ScopedSpvReflectModule(const ScopedSpvReflectModule&) = delete;
            ScopedSpvReflectModule& operator=(const ScopedSpvReflectModule&) = delete;
            ScopedSpvReflectModule(ScopedSpvReflectModule&&) = delete;
            ScopedSpvReflectModule& operator=(ScopedSpvReflectModule&&) = delete;

            ~ScopedSpvReflectModule()
            {
                if (m_initialized)
                {
                    spvReflectDestroyShaderModule(&m_module);
                    m_initialized = false;
                }
            }

            [[nodiscard]] bool Create(std::span<const u8> bytecode, std::string& outError)
            {
                if (bytecode.empty())
                {
                    outError = "SPIR-V reflection failed: bytecode buffer is empty.";
                    return false;
                }
                if (bytecode.size() < kMinSpirvHeaderBytes || (bytecode.size() % sizeof(u32)) != 0u)
                {
                    outError = "SPIR-V reflection failed: invalid bytecode size (" +
                        std::to_string(bytecode.size()) + " bytes).";
                    return false;
                }

                u32 magic = 0u;
                std::memcpy(&magic, bytecode.data(), sizeof(magic));
                if (magic != kSpirvMagicNumber)
                {
                    outError = "SPIR-V reflection failed: invalid SPIR-V magic number.";
                    return false;
                }

                const void* modulePtr = bytecode.data();
                if ((reinterpret_cast<std::uintptr_t>(modulePtr) % alignof(u32)) != 0u)
                {
                    // Copy only when caller passed an unaligned byte view so NO_COPY always
                    // observes 4-byte aligned SPIR-V words backed for m_module's full lifetime.
                    m_alignedCopy.resize(bytecode.size() / sizeof(u32));
                    std::memcpy(m_alignedCopy.data(), bytecode.data(), bytecode.size());
                    modulePtr = m_alignedCopy.data();
                }

                const SpvReflectResult res = spvReflectCreateShaderModule2(
                    SPV_REFLECT_MODULE_FLAG_NO_COPY,
                    bytecode.size(),
                    modulePtr,
                    &m_module);
                if (res != SPV_REFLECT_RESULT_SUCCESS)
                {
                    m_alignedCopy.clear();
                    outError = std::string("SPIR-V reflection parse failed: ") + SpvReflectResultName(res);
                    return false;
                }

                m_initialized = true;
                return true;
            }

            [[nodiscard]] const SpvReflectShaderModule& Get() const noexcept
            {
                return m_module;
            }

        private:
            // Kept alive alongside m_module because SPV_REFLECT_MODULE_FLAG_NO_COPY references it.
            std::vector<u32> m_alignedCopy;
            SpvReflectShaderModule m_module{};
            bool m_initialized = false;
        };

        [[nodiscard]] std::optional<ShaderStage> MapSpirvStage(SpvReflectShaderStageFlagBits stageBits) noexcept
        {
            switch (stageBits)
            {
            case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:   return ShaderStage::Vertex;
            case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: return ShaderStage::Pixel;
            case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:  return ShaderStage::Compute;
            default:                                    return std::nullopt;
            }
        }

        [[nodiscard]] const char* ShaderStageName(ShaderStage stage) noexcept
        {
            switch (stage)
            {
            case ShaderStage::Vertex:  return "Vertex";
            case ShaderStage::Pixel:   return "Pixel";
            case ShaderStage::Compute: return "Compute";
            }
            return "Unknown";
        }

        [[nodiscard]] ReflectedScalarType MapNumericScalarType(
            SpvReflectTypeFlags typeFlags,
            const SpvReflectNumericTraits::Scalar& scalar) noexcept
        {
            if ((typeFlags & SPV_REFLECT_TYPE_FLAG_STRUCT) != 0u)
            {
                return ReflectedScalarType::Struct;
            }
            if ((typeFlags & SPV_REFLECT_TYPE_FLAG_BOOL) != 0u)
            {
                return ReflectedScalarType::Bool;
            }
            if ((typeFlags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0u)
            {
                if (scalar.width == 16u) return ReflectedScalarType::Float16;
                if (scalar.width == 64u) return ReflectedScalarType::Float64;
                return ReflectedScalarType::Float32;
            }
            if ((typeFlags & SPV_REFLECT_TYPE_FLAG_INT) != 0u)
            {
                const bool isSigned = scalar.signedness != 0u;
                if (scalar.width == 16u) return isSigned ? ReflectedScalarType::Int16 : ReflectedScalarType::UInt16;
                if (scalar.width == 64u) return isSigned ? ReflectedScalarType::Int64 : ReflectedScalarType::UInt64;
                return isSigned ? ReflectedScalarType::Int32 : ReflectedScalarType::UInt32;
            }
            return ReflectedScalarType::Unknown;
        }

        [[nodiscard]] ReflectedResourceDimension MapImageDimension(
            const SpvReflectImageTraits& image,
            ReflectedResourceKind kind) noexcept
        {
            if (kind == ReflectedResourceKind::UniformBuffer ||
                kind == ReflectedResourceKind::StorageBuffer ||
                kind == ReflectedResourceKind::RWStorageBuffer)
            {
                return ReflectedResourceDimension::Buffer;
            }
            if (kind == ReflectedResourceKind::Sampler)
            {
                return ReflectedResourceDimension::Unknown;
            }

            switch (image.dim)
            {
            case SpvDim1D:
                return image.arrayed != 0u
                    ? ReflectedResourceDimension::Texture1DArray
                    : ReflectedResourceDimension::Texture1D;
            case SpvDim2D:
                if (image.ms != 0u)
                {
                    return image.arrayed != 0u
                        ? ReflectedResourceDimension::Texture2DMSArray
                        : ReflectedResourceDimension::Texture2DMS;
                }
                return image.arrayed != 0u
                    ? ReflectedResourceDimension::Texture2DArray
                    : ReflectedResourceDimension::Texture2D;
            case SpvDim3D:
                return ReflectedResourceDimension::Texture3D;
            case SpvDimCube:
                return image.arrayed != 0u
                    ? ReflectedResourceDimension::TextureCubeArray
                    : ReflectedResourceDimension::TextureCube;
            case SpvDimBuffer:
                return ReflectedResourceDimension::Buffer;
            default:
                return ReflectedResourceDimension::Unknown;
            }
        }

        void MapSpirvFormat(
            SpvReflectFormat fmt,
            ReflectedScalarType& outType,
            u32& outComponentCount) noexcept
        {
            outType = ReflectedScalarType::Unknown;
            outComponentCount = 0u;

            switch (fmt)
            {
            case SPV_REFLECT_FORMAT_R16_UINT:            outType = ReflectedScalarType::UInt16;  outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R16G16_UINT:         outType = ReflectedScalarType::UInt16;  outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R16G16B16_UINT:      outType = ReflectedScalarType::UInt16;  outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R16G16B16A16_UINT:   outType = ReflectedScalarType::UInt16;  outComponentCount = 4u; break;
            case SPV_REFLECT_FORMAT_R16_SINT:            outType = ReflectedScalarType::Int16;   outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R16G16_SINT:         outType = ReflectedScalarType::Int16;   outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R16G16B16_SINT:      outType = ReflectedScalarType::Int16;   outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R16G16B16A16_SINT:   outType = ReflectedScalarType::Int16;   outComponentCount = 4u; break;
            case SPV_REFLECT_FORMAT_R16_SFLOAT:          outType = ReflectedScalarType::Float16; outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R16G16_SFLOAT:       outType = ReflectedScalarType::Float16; outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT:    outType = ReflectedScalarType::Float16; outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT: outType = ReflectedScalarType::Float16; outComponentCount = 4u; break;

            case SPV_REFLECT_FORMAT_R32_UINT:            outType = ReflectedScalarType::UInt32;  outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R32G32_UINT:         outType = ReflectedScalarType::UInt32;  outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R32G32B32_UINT:      outType = ReflectedScalarType::UInt32;  outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:   outType = ReflectedScalarType::UInt32;  outComponentCount = 4u; break;
            case SPV_REFLECT_FORMAT_R32_SINT:            outType = ReflectedScalarType::Int32;   outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R32G32_SINT:         outType = ReflectedScalarType::Int32;   outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R32G32B32_SINT:      outType = ReflectedScalarType::Int32;   outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:   outType = ReflectedScalarType::Int32;   outComponentCount = 4u; break;
            case SPV_REFLECT_FORMAT_R32_SFLOAT:          outType = ReflectedScalarType::Float32; outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R32G32_SFLOAT:       outType = ReflectedScalarType::Float32; outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:    outType = ReflectedScalarType::Float32; outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT: outType = ReflectedScalarType::Float32; outComponentCount = 4u; break;

            case SPV_REFLECT_FORMAT_R64_UINT:            outType = ReflectedScalarType::UInt64;  outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R64G64_UINT:         outType = ReflectedScalarType::UInt64;  outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R64G64B64_UINT:      outType = ReflectedScalarType::UInt64;  outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R64G64B64A64_UINT:   outType = ReflectedScalarType::UInt64;  outComponentCount = 4u; break;
            case SPV_REFLECT_FORMAT_R64_SINT:            outType = ReflectedScalarType::Int64;   outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R64G64_SINT:         outType = ReflectedScalarType::Int64;   outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R64G64B64_SINT:      outType = ReflectedScalarType::Int64;   outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R64G64B64A64_SINT:   outType = ReflectedScalarType::Int64;   outComponentCount = 4u; break;
            case SPV_REFLECT_FORMAT_R64_SFLOAT:          outType = ReflectedScalarType::Float64; outComponentCount = 1u; break;
            case SPV_REFLECT_FORMAT_R64G64_SFLOAT:       outType = ReflectedScalarType::Float64; outComponentCount = 2u; break;
            case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT:    outType = ReflectedScalarType::Float64; outComponentCount = 3u; break;
            case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT: outType = ReflectedScalarType::Float64; outComponentCount = 4u; break;
            default: break;
            }
        }

        [[nodiscard]] std::string MapSpvBuiltInName(int builtIn)
        {
            switch (static_cast<SpvBuiltIn>(builtIn))
            {
            case SpvBuiltInPosition:           return "SV_Position";
            case SpvBuiltInFragCoord:          return "SV_Position";
            case SpvBuiltInVertexId:           return "SV_VertexID";
            case SpvBuiltInVertexIndex:        return "SV_VertexID";
            case SpvBuiltInInstanceId:         return "SV_InstanceID";
            case SpvBuiltInInstanceIndex:      return "SV_InstanceID";
            case SpvBuiltInPrimitiveId:        return "SV_PrimitiveID";
            case SpvBuiltInFrontFacing:        return "SV_IsFrontFace";
            case SpvBuiltInFragDepth:          return "SV_Depth";
            case SpvBuiltInSampleId:           return "SV_SampleIndex";
            case SpvBuiltInSampleMask:         return "SV_Coverage";
            case SpvBuiltInGlobalInvocationId: return "SV_DispatchThreadID";
            case SpvBuiltInLocalInvocationId:  return "SV_GroupThreadID";
            case SpvBuiltInLocalInvocationIndex: return "SV_GroupIndex";
            case SpvBuiltInWorkgroupId:        return "SV_GroupID";
            case SpvBuiltInClipDistance:       return "SV_ClipDistance";
            case SpvBuiltInCullDistance:       return "SV_CullDistance";
            case SpvBuiltInLayer:              return "SV_RenderTargetArrayIndex";
            case SpvBuiltInViewportIndex:      return "SV_ViewportArrayIndex";
            default:                           return "SV_BuiltIn_" + std::to_string(builtIn);
            }
        }

        [[nodiscard]] ReflectedShaderMember ConvertBlockMember(const SpvReflectBlockVariable& var)
        {
            ReflectedShaderMember out{};
            out.Name = SafeCString(var.name);
            out.OffsetBytes = var.offset;
            out.SizeBytes = var.size;
            out.PaddedSizeBytes = var.padded_size;

            const SpvReflectTypeFlags typeFlags =
                var.type_description != nullptr ? var.type_description->type_flags : 0u;
            const SpvReflectDecorationFlags decFlags =
                var.decoration_flags |
                (var.type_description != nullptr ? var.type_description->decoration_flags : 0u);

            if ((typeFlags & SPV_REFLECT_TYPE_FLAG_STRUCT) != 0u || var.member_count > 0u)
            {
                out.Class = ReflectedVariableClass::Struct;
                out.ScalarType = ReflectedScalarType::Struct;
            }
            else if ((typeFlags & SPV_REFLECT_TYPE_FLAG_MATRIX) != 0u)
            {
                const bool isRowMajor = (decFlags & SPV_REFLECT_DECORATION_ROW_MAJOR) != 0u;
                out.IsRowMajor = isRowMajor;
                out.Class = isRowMajor
                    ? ReflectedVariableClass::MatrixRows
                    : ReflectedVariableClass::MatrixColumns;
                out.ScalarType = MapNumericScalarType(typeFlags, var.numeric.scalar);
                out.Rows = std::max(1u, var.numeric.matrix.row_count);
                out.Columns = std::max(1u, var.numeric.matrix.column_count);
                out.MatrixStrideBytes = var.numeric.matrix.stride;
            }
            else if ((typeFlags & SPV_REFLECT_TYPE_FLAG_VECTOR) != 0u)
            {
                out.Class = ReflectedVariableClass::Vector;
                out.ScalarType = MapNumericScalarType(typeFlags, var.numeric.scalar);
                out.Rows = 1u;
                out.Columns = std::max(1u, var.numeric.vector.component_count);
            }
            else
            {
                out.Class = ReflectedVariableClass::Scalar;
                out.ScalarType = MapNumericScalarType(typeFlags, var.numeric.scalar);
                out.Rows = 1u;
                out.Columns = 1u;
            }

            if (var.array.dims_count > 0u)
            {
                u64 totalElements = 1u;
                for (u32 d = 0u; d < var.array.dims_count; ++d)
                {
                    if (var.array.dims[d] == static_cast<u32>(SPV_REFLECT_ARRAY_DIM_RUNTIME))
                    {
                        totalElements = kUnboundedDescriptorCount;
                        break;
                    }
                    totalElements *= var.array.dims[d];
                }
                out.ArrayElementCount = static_cast<u32>(
                    std::min<u64>(totalElements, std::numeric_limits<u32>::max()));
                out.ArrayStrideBytes = var.array.stride;
            }

            if (var.member_count > 0u && var.members != nullptr)
            {
                out.Members.reserve(var.member_count);
                for (u32 m = 0u; m < var.member_count; ++m)
                {
                    out.Members.push_back(ConvertBlockMember(var.members[m]));
                }
                // spirv-reflect rounds struct member var.size up to 16-byte alignment (padded_size).
                // Compute unpadded struct byte extent for parity with D3D12_SHADER_VARIABLE_DESC::Size.
                if (out.Class == ReflectedVariableClass::Struct &&
                    out.ArrayElementCount == 0u &&
                    !out.Members.empty())
                {
                    u32 maxEnd = 0u;
                    for (const auto& child : out.Members)
                    {
                        maxEnd = std::max(maxEnd, child.OffsetBytes + child.SizeBytes);
                    }
                    if (maxEnd > 0u)
                    {
                        out.SizeBytes = maxEnd;
                    }
                }
            }

            return out;
        }

        [[nodiscard]] std::vector<ReflectedShaderMember> ExtractBlockMembers(const SpvReflectBlockVariable& block)
        {
            std::vector<ReflectedShaderMember> members;
            if (block.member_count == 0u || block.members == nullptr)
            {
                return members;
            }
            members.reserve(block.member_count);
            for (u32 i = 0u; i < block.member_count; ++i)
            {
                members.push_back(ConvertBlockMember(block.members[i]));
            }
            return members;
        }

        void AppendInterfaceVariable(
            const SpvReflectInterfaceVariable& var,
            std::vector<ReflectedStageParameter>& outParams)
        {
            if (var.member_count > 0u && var.members != nullptr)
            {
                for (u32 m = 0u; m < var.member_count; ++m)
                {
                    AppendInterfaceVariable(var.members[m], outParams);
                }
                return;
            }

            ReflectedStageParameter param{};
            param.Name = SafeCString(var.name);
            param.SemanticName = SafeCString(var.semantic);

            const bool isBuiltIn =
                (var.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) != 0u ||
                var.built_in != -1;
            param.IsSystemValue = isBuiltIn;
            if (isBuiltIn)
            {
                param.SystemValueName = MapSpvBuiltInName(var.built_in);
                if (param.SemanticName.empty())
                {
                    param.SemanticName = param.SystemValueName;
                }
            }
            else if (var.location != std::numeric_limits<u32>::max())
            {
                param.Location = var.location;
            }

            u32 componentCount = 0u;
            MapSpirvFormat(var.format, param.ComponentType, componentCount);
            if (componentCount == 0u && var.type_description != nullptr)
            {
                const auto& traits = var.type_description->traits.numeric;
                param.ComponentType = MapNumericScalarType(var.type_description->type_flags, traits.scalar);
                if ((var.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) != 0u)
                {
                    componentCount = traits.vector.component_count;
                }
                else if (param.ComponentType != ReflectedScalarType::Unknown)
                {
                    componentCount = 1u;
                }
            }

            param.ComponentCount = componentCount;
            if (componentCount > 0u && componentCount <= 4u)
            {
                // SPIRV-Reflect initializes undecorated var.component to ~0u (0xFFFFFFFF).
                const u32 shift = (var.component <= 3u) ? var.component : 0u;
                const u32 rawMask = ((1u << componentCount) - 1u) << shift;
                param.ComponentMask = static_cast<u8>(rawMask & 0xFu);
            }

            outParams.push_back(std::move(param));
        }

        void SortStageParameters(std::vector<ReflectedStageParameter>& params)
        {
            std::sort(params.begin(), params.end(),
                [](const ReflectedStageParameter& a, const ReflectedStageParameter& b)
                {
                    if (a.IsSystemValue != b.IsSystemValue)
                    {
                        return !a.IsSystemValue;
                    }
                    const u32 locA = a.Location.value_or(std::numeric_limits<u32>::max());
                    const u32 locB = b.Location.value_or(std::numeric_limits<u32>::max());
                    if (locA != locB)
                    {
                        return locA < locB;
                    }
                    return a.Name < b.Name;
                });
        }

        void PopulateDescriptorBinding(
            const SpvReflectDescriptorBinding& spvBinding,
            ReflectedShaderVisibility visibility,
            ShaderReflectionData& outData)
        {
            ReflectedBinding binding{};
            binding.Name = SafeCString(spvBinding.name);
            binding.DescriptorSet = spvBinding.set;
            binding.Binding = spvBinding.binding;
            binding.RegisterSlot = spvBinding.binding;
            binding.RegisterSpace = spvBinding.set;
            binding.Visibility = visibility;

            bool isUnbounded = false;
            for (u32 d = 0u; d < spvBinding.array.dims_count; ++d)
            {
                if (spvBinding.array.dims[d] == static_cast<u32>(SPV_REFLECT_ARRAY_DIM_RUNTIME))
                {
                    isUnbounded = true;
                    break;
                }
            }
            binding.IsUnboundedArray = isUnbounded;
            binding.Count = isUnbounded ? kUnboundedDescriptorCount : std::max(1u, spvBinding.count);
            if (isUnbounded)
            {
                outData.UsesBindlessHeap = true;
            }

            const bool nonWritable = (spvBinding.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0u;
            switch (spvBinding.descriptor_type)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                binding.Type = ResourceBindingType::Sampler;
                binding.Kind = ReflectedResourceKind::Sampler;
                binding.IsReadOnly = true;
                break;

            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                binding.Type = ResourceBindingType::ShaderResource;
                binding.Kind = ReflectedResourceKind::CombinedImageSampler;
                binding.IsReadOnly = true;
                break;

            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                binding.Type = ResourceBindingType::ShaderResource;
                binding.Kind = ReflectedResourceKind::SampledTexture;
                binding.IsReadOnly = true;
                break;

            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                binding.Type = ResourceBindingType::UnorderedAccess;
                binding.Kind = ReflectedResourceKind::StorageTexture;
                binding.IsReadOnly = nonWritable;
                break;

            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            {
                binding.Type = ResourceBindingType::ConstantBuffer;
                binding.Kind = ReflectedResourceKind::UniformBuffer;
                binding.IsReadOnly = true;

                ReflectedConstantBuffer cb{};
                cb.Name = !spvBinding.block.name && binding.Name.empty()
                    ? std::string{}
                    : (spvBinding.block.name != nullptr ? SafeCString(spvBinding.block.name) : binding.Name);
                cb.SizeBytes = spvBinding.block.padded_size > 0u
                    ? spvBinding.block.padded_size
                    : spvBinding.block.size;
                cb.DescriptorSet = spvBinding.set;
                cb.Binding = spvBinding.binding;
                cb.Members = ExtractBlockMembers(spvBinding.block);

                binding.ConstantBufferIndex = static_cast<u32>(outData.ConstantBuffers.size());
                outData.ConstantBuffers.push_back(std::move(cb));
                break;
            }

            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            {
                const bool isUav =
                    ((spvBinding.resource_type & SPV_REFLECT_RESOURCE_FLAG_UAV) != 0) && !nonWritable;
                if (isUav)
                {
                    binding.Type = ResourceBindingType::UnorderedAccess;
                    binding.Kind = ReflectedResourceKind::RWStorageBuffer;
                    binding.IsReadOnly = false;
                }
                else
                {
                    binding.Type = ResourceBindingType::ShaderResource;
                    binding.Kind = ReflectedResourceKind::StorageBuffer;
                    binding.IsReadOnly = true;
                }
                break;
            }

            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                binding.Type = ResourceBindingType::ShaderResource;
                binding.Kind = ReflectedResourceKind::AccelerationStructure;
                binding.IsReadOnly = true;
                break;

            default:
                binding.Type = ResourceBindingType::ShaderResource;
                binding.Kind = ReflectedResourceKind::Unknown;
                binding.IsReadOnly = true;
                break;
            }

            binding.Dimension = MapImageDimension(spvBinding.image, binding.Kind);
            outData.Bindings.push_back(std::move(binding));
        }

        [[nodiscard]] Core::Result<const SpvReflectEntryPoint*, std::string> ResolveEntryPoint(
            const SpvReflectShaderModule& module,
            std::string_view expectedEntryPoint)
        {
            if (module.entry_point_count == 0u || module.entry_points == nullptr)
            {
                return Core::Result<const SpvReflectEntryPoint*, std::string>::Failure(
                    "SPIR-V reflection failed: shader module contains no entry points.");
            }

            if (expectedEntryPoint.empty())
            {
                return Core::Result<const SpvReflectEntryPoint*, std::string>::Success(&module.entry_points[0]);
            }

            const std::string entryName(expectedEntryPoint);
            const SpvReflectEntryPoint* entry = spvReflectGetEntryPoint(&module, entryName.c_str());
            if (entry == nullptr)
            {
                return Core::Result<const SpvReflectEntryPoint*, std::string>::Failure(
                    "SPIR-V reflection failed: entry point '" + entryName + "' not found.");
            }

            return Core::Result<const SpvReflectEntryPoint*, std::string>::Success(entry);
        }
    }

    Core::Result<ShaderReflectionData, std::string> ReflectSpirvBinary(
        std::span<const u8> bytecode,
        std::optional<ShaderStage> expectedStage,
        std::string_view expectedEntryPoint)
    {
        ScopedSpvReflectModule scopedModule;
        std::string createError;
        if (!scopedModule.Create(bytecode, createError))
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(std::move(createError));
        }

        const SpvReflectShaderModule& module = scopedModule.Get();
        auto entryRes = ResolveEntryPoint(module, expectedEntryPoint);
        if (!entryRes.IsSuccess())
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(entryRes.Error());
        }
        const SpvReflectEntryPoint* entry = entryRes.Value();

        const auto stageOpt = MapSpirvStage(entry->shader_stage);
        if (!stageOpt.has_value())
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "SPIR-V reflection failed: unsupported shader stage.");
        }
        if (expectedStage.has_value() && *stageOpt != *expectedStage)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                std::string("SPIR-V reflection stage mismatch: expected ") +
                ShaderStageName(*expectedStage) + ", got " + ShaderStageName(*stageOpt));
        }

        ShaderReflectionData data{};
        data.TargetFormat = ShaderTargetFormat::SPIRV;
        data.Stage = *stageOpt;
        data.EntryPoint = SafeCString(entry->name);
        data.IsValid = true;

        if (data.Stage == ShaderStage::Compute)
        {
            data.ThreadGroupSizeX = std::max(1u, entry->local_size.x);
            data.ThreadGroupSizeY = std::max(1u, entry->local_size.y);
            data.ThreadGroupSizeZ = std::max(1u, entry->local_size.z);
        }

        const ReflectedShaderVisibility visibility = VisibilityFromStage(data.Stage);

        // Descriptor bindings for this entry point
        u32 bindingCount = 0u;
        SpvReflectResult enumRes = spvReflectEnumerateEntryPointDescriptorBindings(
            &module, entry->name, &bindingCount, nullptr);
        if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                std::string("SPIR-V descriptor binding count failed: ") + SpvReflectResultName(enumRes));
        }
        if (bindingCount > 0u)
        {
            std::vector<SpvReflectDescriptorBinding*> spvBindings(bindingCount, nullptr);
            enumRes = spvReflectEnumerateEntryPointDescriptorBindings(
                &module, entry->name, &bindingCount, spvBindings.data());
            if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
            {
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    std::string("SPIR-V descriptor binding enumeration failed: ") + SpvReflectResultName(enumRes));
            }
            std::sort(spvBindings.begin(), spvBindings.end(),
                [](const SpvReflectDescriptorBinding* a, const SpvReflectDescriptorBinding* b)
                {
                    if (a->set != b->set)
                    {
                        return a->set < b->set;
                    }
                    return a->binding < b->binding;
                });
            data.Bindings.reserve(bindingCount);
            for (const SpvReflectDescriptorBinding* spvBinding : spvBindings)
            {
                if (spvBinding != nullptr)
                {
                    PopulateDescriptorBinding(*spvBinding, visibility, data);
                }
            }
        }

        // Push constant blocks for this entry point
        u32 pushCount = 0u;
        enumRes = spvReflectEnumerateEntryPointPushConstantBlocks(
            &module, entry->name, &pushCount, nullptr);
        if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                std::string("SPIR-V push constant count failed: ") + SpvReflectResultName(enumRes));
        }
        if (pushCount > 0u)
        {
            std::vector<SpvReflectBlockVariable*> pushBlocks(pushCount, nullptr);
            enumRes = spvReflectEnumerateEntryPointPushConstantBlocks(
                &module, entry->name, &pushCount, pushBlocks.data());
            if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
            {
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    std::string("SPIR-V push constant enumeration failed: ") + SpvReflectResultName(enumRes));
            }
            data.PushConstants.reserve(pushCount);
            for (const SpvReflectBlockVariable* block : pushBlocks)
            {
                if (block == nullptr)
                {
                    continue;
                }
                ReflectedPushConstantRange range{};
                range.Name = SafeCString(block->name);
                range.OffsetBytes = block->offset;
                range.SizeBytes = block->size > 0u ? block->size : block->padded_size;
                range.Visibility = visibility;
                range.Members = ExtractBlockMembers(*block);
                data.PushConstantSizeBytes = std::max(
                    data.PushConstantSizeBytes,
                    range.OffsetBytes + range.SizeBytes);
                data.PushConstants.push_back(std::move(range));
            }
        }

        // Input interface variables
        u32 inputCount = 0u;
        enumRes = spvReflectEnumerateEntryPointInputVariables(
            &module, entry->name, &inputCount, nullptr);
        if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                std::string("SPIR-V input variable count failed: ") + SpvReflectResultName(enumRes));
        }
        if (inputCount > 0u)
        {
            std::vector<SpvReflectInterfaceVariable*> inputs(inputCount, nullptr);
            enumRes = spvReflectEnumerateEntryPointInputVariables(
                &module, entry->name, &inputCount, inputs.data());
            if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
            {
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    std::string("SPIR-V input variable enumeration failed: ") + SpvReflectResultName(enumRes));
            }
            for (const SpvReflectInterfaceVariable* var : inputs)
            {
                if (var != nullptr)
                {
                    AppendInterfaceVariable(*var, data.StageInputs);
                }
            }
            SortStageParameters(data.StageInputs);
        }

        // Output interface variables
        u32 outputCount = 0u;
        enumRes = spvReflectEnumerateEntryPointOutputVariables(
            &module, entry->name, &outputCount, nullptr);
        if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                std::string("SPIR-V output variable count failed: ") + SpvReflectResultName(enumRes));
        }
        if (outputCount > 0u)
        {
            std::vector<SpvReflectInterfaceVariable*> outputs(outputCount, nullptr);
            enumRes = spvReflectEnumerateEntryPointOutputVariables(
                &module, entry->name, &outputCount, outputs.data());
            if (enumRes != SPV_REFLECT_RESULT_SUCCESS)
            {
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    std::string("SPIR-V output variable enumeration failed: ") + SpvReflectResultName(enumRes));
            }
            for (const SpvReflectInterfaceVariable* var : outputs)
            {
                if (var != nullptr)
                {
                    AppendInterfaceVariable(*var, data.StageOutputs);
                }
            }
            SortStageParameters(data.StageOutputs);
        }

        return Core::Result<ShaderReflectionData, std::string>::Success(std::move(data));
    }
}
