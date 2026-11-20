#include "DxilReflector.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace Morbi::Shader::Detail
{
    namespace
    {
        constexpr u32 kDxbcFourCc = 0x43425844u; // "DXBC" little-endian
        constexpr usize kMinDxbcHeaderBytes = 32u;
        constexpr UINT64 kDxilRequiresResourceHeapBit = 0x02000000ull;
        constexpr UINT64 kDxilRequiresSamplerHeapBit = 0x04000000ull;

        [[nodiscard]] std::string SafeCString(const char* ptr)
        {
            return ptr != nullptr ? std::string(ptr) : std::string{};
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

        [[nodiscard]] std::optional<ShaderStage> MapDxilStage(UINT shaderVersion) noexcept
        {
            const UINT type = D3D12_SHVER_GET_TYPE(shaderVersion);
            switch (type)
            {
            case D3D12_SHVER_VERTEX_SHADER:  return ShaderStage::Vertex;
            case D3D12_SHVER_PIXEL_SHADER:   return ShaderStage::Pixel;
            case D3D12_SHVER_COMPUTE_SHADER: return ShaderStage::Compute;
            default:                         return std::nullopt;
            }
        }

        [[nodiscard]] ReflectedScalarType MapDxilVariableScalarType(D3D_SHADER_VARIABLE_TYPE type) noexcept
        {
            switch (type)
            {
            case D3D_SVT_BOOL:       return ReflectedScalarType::Bool;
            case D3D_SVT_INT:        return ReflectedScalarType::Int32;
            case D3D_SVT_UINT:       return ReflectedScalarType::UInt32;
            case D3D_SVT_FLOAT:      return ReflectedScalarType::Float32;
            case D3D_SVT_DOUBLE:     return ReflectedScalarType::Float64;
            case D3D_SVT_INT16:
            case D3D_SVT_MIN12INT:
            case D3D_SVT_MIN16INT:   return ReflectedScalarType::Int16;
            case D3D_SVT_UINT16:
            case D3D_SVT_MIN16UINT:  return ReflectedScalarType::UInt16;
            case D3D_SVT_FLOAT16:
            case D3D_SVT_MIN8FLOAT:
            case D3D_SVT_MIN10FLOAT:
            case D3D_SVT_MIN16FLOAT: return ReflectedScalarType::Float16;
            case D3D_SVT_INT64:      return ReflectedScalarType::Int64;
            case D3D_SVT_UINT64:     return ReflectedScalarType::UInt64;
            default:                 return ReflectedScalarType::Unknown;
            }
        }

        [[nodiscard]] u32 ScalarByteSize(ReflectedScalarType scalar) noexcept
        {
            switch (scalar)
            {
            case ReflectedScalarType::Bool:
            case ReflectedScalarType::Int32:
            case ReflectedScalarType::UInt32:
            case ReflectedScalarType::Float32: return 4u;
            case ReflectedScalarType::Int16:
            case ReflectedScalarType::UInt16:
            case ReflectedScalarType::Float16: return 2u;
            case ReflectedScalarType::Int64:
            case ReflectedScalarType::UInt64:
            case ReflectedScalarType::Float64: return 8u;
            default:                           return 0u;
            }
        }

        [[nodiscard]] ReflectedScalarType MapSignatureComponentType(D3D_REGISTER_COMPONENT_TYPE type) noexcept
        {
            switch (type)
            {
            case D3D_REGISTER_COMPONENT_UINT32:  return ReflectedScalarType::UInt32;
            case D3D_REGISTER_COMPONENT_SINT32:  return ReflectedScalarType::Int32;
            case D3D_REGISTER_COMPONENT_FLOAT32: return ReflectedScalarType::Float32;
            case D3D_REGISTER_COMPONENT_UINT16:  return ReflectedScalarType::UInt16;
            case D3D_REGISTER_COMPONENT_SINT16:  return ReflectedScalarType::Int16;
            case D3D_REGISTER_COMPONENT_FLOAT16: return ReflectedScalarType::Float16;
            case D3D_REGISTER_COMPONENT_UINT64:  return ReflectedScalarType::UInt64;
            case D3D_REGISTER_COMPONENT_SINT64:  return ReflectedScalarType::Int64;
            case D3D_REGISTER_COMPONENT_FLOAT64: return ReflectedScalarType::Float64;
            default:                             return ReflectedScalarType::Unknown;
            }
        }

        [[nodiscard]] ReflectedResourceDimension MapDxilDimension(
            D3D_SRV_DIMENSION dim,
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

            switch (dim)
            {
            case D3D_SRV_DIMENSION_BUFFER:
            case D3D_SRV_DIMENSION_BUFFEREX:         return ReflectedResourceDimension::Buffer;
            case D3D_SRV_DIMENSION_TEXTURE1D:        return ReflectedResourceDimension::Texture1D;
            case D3D_SRV_DIMENSION_TEXTURE1DARRAY:   return ReflectedResourceDimension::Texture1DArray;
            case D3D_SRV_DIMENSION_TEXTURE2D:        return ReflectedResourceDimension::Texture2D;
            case D3D_SRV_DIMENSION_TEXTURE2DARRAY:   return ReflectedResourceDimension::Texture2DArray;
            case D3D_SRV_DIMENSION_TEXTURE2DMS:      return ReflectedResourceDimension::Texture2DMS;
            case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: return ReflectedResourceDimension::Texture2DMSArray;
            case D3D_SRV_DIMENSION_TEXTURE3D:        return ReflectedResourceDimension::Texture3D;
            case D3D_SRV_DIMENSION_TEXTURECUBE:      return ReflectedResourceDimension::TextureCube;
            case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: return ReflectedResourceDimension::TextureCubeArray;
            default:                                 return ReflectedResourceDimension::Unknown;
            }
        }

        [[nodiscard]] std::string MapSystemValueName(D3D_NAME svType, const std::string& fallbackSemantic)
        {
            switch (svType)
            {
            case D3D_NAME_POSITION:                  return "SV_Position";
            case D3D_NAME_CLIP_DISTANCE:             return "SV_ClipDistance";
            case D3D_NAME_CULL_DISTANCE:             return "SV_CullDistance";
            case D3D_NAME_RENDER_TARGET_ARRAY_INDEX: return "SV_RenderTargetArrayIndex";
            case D3D_NAME_VIEWPORT_ARRAY_INDEX:      return "SV_ViewportArrayIndex";
            case D3D_NAME_VERTEX_ID:                 return "SV_VertexID";
            case D3D_NAME_PRIMITIVE_ID:              return "SV_PrimitiveID";
            case D3D_NAME_INSTANCE_ID:               return "SV_InstanceID";
            case D3D_NAME_IS_FRONT_FACE:             return "SV_IsFrontFace";
            case D3D_NAME_SAMPLE_INDEX:              return "SV_SampleIndex";
            case D3D_NAME_TARGET:                    return "SV_Target";
            case D3D_NAME_DEPTH:                     return "SV_Depth";
            case D3D_NAME_COVERAGE:                  return "SV_Coverage";
            default:                                 return fallbackSemantic;
            }
        }

        [[nodiscard]] u32 EstimateNonArrayHlslMemberSize(const D3D12_SHADER_TYPE_DESC& desc, ReflectedScalarType scalar) noexcept
        {
            const u32 scalarSize = ScalarByteSize(scalar);
            if (scalarSize == 0u)
            {
                return 0u;
            }

            if (desc.Class == D3D_SVC_SCALAR)
            {
                return scalarSize;
            }
            if (desc.Class == D3D_SVC_VECTOR)
            {
                return std::max(1u, desc.Columns) * scalarSize;
            }
            if (desc.Class == D3D_SVC_MATRIX_ROWS)
            {
                const u32 rows = std::max(1u, desc.Rows);
                const u32 cols = std::max(1u, desc.Columns);
                const u32 rowStride = std::max(16u, cols * scalarSize);
                return (rows - 1u) * rowStride + cols * scalarSize;
            }
            if (desc.Class == D3D_SVC_MATRIX_COLUMNS)
            {
                const u32 rows = std::max(1u, desc.Rows);
                const u32 cols = std::max(1u, desc.Columns);
                const u32 colStride = std::max(16u, rows * scalarSize);
                return (cols - 1u) * colStride + rows * scalarSize;
            }
            return 0u;
        }

        [[nodiscard]] ReflectedShaderMember ConvertDxilTypeMember(
            ID3D12ShaderReflectionType* typeInterface,
            std::string name,
            u32 offsetBytes,
            std::optional<u32> explicitSizeBytes)
        {
            ReflectedShaderMember member{};
            member.Name = std::move(name);
            member.OffsetBytes = offsetBytes;

            if (typeInterface == nullptr)
            {
                member.SizeBytes = explicitSizeBytes.value_or(0u);
                return member;
            }

            D3D12_SHADER_TYPE_DESC typeDesc{};
            if (FAILED(typeInterface->GetDesc(&typeDesc)))
            {
                member.SizeBytes = explicitSizeBytes.value_or(0u);
                return member;
            }

            if (typeDesc.Elements > 0u)
            {
                member.ArrayElementCount = typeDesc.Elements;
            }

            switch (typeDesc.Class)
            {
            case D3D_SVC_SCALAR:
                member.Class = ReflectedVariableClass::Scalar;
                member.ScalarType = MapDxilVariableScalarType(typeDesc.Type);
                member.Rows = 1u;
                member.Columns = 1u;
                break;

            case D3D_SVC_VECTOR:
                member.Class = ReflectedVariableClass::Vector;
                member.ScalarType = MapDxilVariableScalarType(typeDesc.Type);
                member.Rows = 1u;
                member.Columns = std::max(1u, typeDesc.Columns);
                break;

            case D3D_SVC_MATRIX_ROWS:
                member.Class = ReflectedVariableClass::MatrixRows;
                member.ScalarType = MapDxilVariableScalarType(typeDesc.Type);
                member.Rows = std::max(1u, typeDesc.Rows);
                member.Columns = std::max(1u, typeDesc.Columns);
                member.IsRowMajor = true;
                break;

            case D3D_SVC_MATRIX_COLUMNS:
                member.Class = ReflectedVariableClass::MatrixColumns;
                member.ScalarType = MapDxilVariableScalarType(typeDesc.Type);
                member.Rows = std::max(1u, typeDesc.Rows);
                member.Columns = std::max(1u, typeDesc.Columns);
                member.IsRowMajor = false;
                break;

            case D3D_SVC_STRUCT:
                member.Class = ReflectedVariableClass::Struct;
                member.ScalarType = ReflectedScalarType::Struct;
                break;

            default:
                member.Class = ReflectedVariableClass::Unknown;
                member.ScalarType = ReflectedScalarType::Unknown;
                break;
            }

            if (typeDesc.Members > 0u)
            {
                member.Members.reserve(typeDesc.Members);
                for (UINT m = 0u; m < typeDesc.Members; ++m)
                {
                    ID3D12ShaderReflectionType* childType = typeInterface->GetMemberTypeByIndex(m);
                    const char* childNameRaw = typeInterface->GetMemberTypeName(m);
                    D3D12_SHADER_TYPE_DESC childDesc{};
                    u32 childOffset = 0u;
                    if (childType != nullptr && SUCCEEDED(childType->GetDesc(&childDesc)))
                    {
                        childOffset = childDesc.Offset;
                    }
                    member.Members.push_back(ConvertDxilTypeMember(
                        childType,
                        SafeCString(childNameRaw),
                        childOffset,
                        std::nullopt));
                }
            }

            if (explicitSizeBytes.has_value())
            {
                member.SizeBytes = *explicitSizeBytes;
                member.PaddedSizeBytes = *explicitSizeBytes;
            }
            else if (typeDesc.Class == D3D_SVC_STRUCT && !member.Members.empty())
            {
                const auto& lastChild = member.Members.back();
                const u32 singleStructSize = lastChild.OffsetBytes + lastChild.SizeBytes;
                if (member.ArrayElementCount > 1u)
                {
                    const u32 elementStride = (singleStructSize + 15u) & ~15u;
                    member.ArrayStrideBytes = elementStride;
                    member.SizeBytes = (member.ArrayElementCount - 1u) * elementStride + singleStructSize;
                }
                else
                {
                    member.SizeBytes = singleStructSize;
                }
                member.PaddedSizeBytes = member.SizeBytes;
            }
            else
            {
                const u32 singleSize = EstimateNonArrayHlslMemberSize(typeDesc, member.ScalarType);
                if (member.ArrayElementCount > 1u && singleSize > 0u)
                {
                    const u32 elementStride = ((singleSize + 15u) / 16u) * 16u;
                    member.ArrayStrideBytes = elementStride;
                    member.SizeBytes = (member.ArrayElementCount - 1u) * elementStride + singleSize;
                }
                else
                {
                    member.SizeBytes = singleSize;
                }
                member.PaddedSizeBytes = member.SizeBytes;
            }

            return member;
        }

        [[nodiscard]] ReflectedConstantBuffer ConvertDxilConstantBuffer(ID3D12ShaderReflectionConstantBuffer* cb)
        {
            ReflectedConstantBuffer out{};
            if (cb == nullptr)
            {
                return out;
            }

            D3D12_SHADER_BUFFER_DESC cbDesc{};
            if (FAILED(cb->GetDesc(&cbDesc)))
            {
                return out;
            }

            out.Name = SafeCString(cbDesc.Name);
            out.SizeBytes = cbDesc.Size;
            out.Members.reserve(cbDesc.Variables);

            for (UINT v = 0u; v < cbDesc.Variables; ++v)
            {
                ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
                if (var == nullptr)
                {
                    continue;
                }

                D3D12_SHADER_VARIABLE_DESC varDesc{};
                if (FAILED(var->GetDesc(&varDesc)))
                {
                    continue;
                }

                out.Members.push_back(ConvertDxilTypeMember(
                    var->GetType(),
                    SafeCString(varDesc.Name),
                    varDesc.StartOffset,
                    varDesc.Size));
            }

            return out;
        }

        void PopulateSignatureParameter(
            const D3D12_SIGNATURE_PARAMETER_DESC& desc,
            std::vector<ReflectedStageParameter>& outParams)
        {
            ReflectedStageParameter param{};
            param.SemanticName = SafeCString(desc.SemanticName);
            param.SemanticIndex = desc.SemanticIndex;
            param.Name = desc.SemanticIndex > 0u
                ? (param.SemanticName + std::to_string(desc.SemanticIndex))
                : param.SemanticName;
            param.Register = desc.Register;
            param.IsSystemValue = (desc.SystemValueType != D3D_NAME_UNDEFINED);
            if (param.IsSystemValue)
            {
                param.SystemValueName = MapSystemValueName(desc.SystemValueType, param.SemanticName);
            }
            param.ComponentType = MapSignatureComponentType(desc.ComponentType);
            param.ComponentMask = static_cast<u8>(desc.Mask & 0xFu);
            param.ComponentCount = static_cast<u32>(std::popcount(param.ComponentMask));
            outParams.push_back(std::move(param));
        }

        void PopulateDxilBinding(
            const D3D12_SHADER_INPUT_BIND_DESC& bindDesc,
            ReflectedShaderVisibility visibility,
            const std::unordered_map<std::string, u32>& cbIndexByName,
            ShaderReflectionData& outData)
        {
            ReflectedBinding binding{};
            binding.Name = SafeCString(bindDesc.Name);
            binding.RegisterSlot = bindDesc.BindPoint;
            binding.RegisterSpace = bindDesc.Space;
            binding.DxilRegister = bindDesc.BindPoint;
            binding.DxilSpace = bindDesc.Space;
            binding.Visibility = visibility;

            const bool isUnbounded =
                (bindDesc.BindCount == 0u || bindDesc.BindCount == std::numeric_limits<UINT>::max());
            binding.IsUnboundedArray = isUnbounded;
            binding.Count = isUnbounded ? kUnboundedDescriptorCount : bindDesc.BindCount;
            if (isUnbounded)
            {
                outData.UsesBindlessHeap = true;
            }

            switch (bindDesc.Type)
            {
            case D3D_SIT_CBUFFER:
                binding.Type = ResourceBindingType::ConstantBuffer;
                binding.Kind = ReflectedResourceKind::UniformBuffer;
                binding.IsReadOnly = true;
                if (const auto it = cbIndexByName.find(binding.Name); it != cbIndexByName.end())
                {
                    binding.ConstantBufferIndex = it->second;
                    auto& cb = outData.ConstantBuffers[it->second];
                    cb.DxilRegister = bindDesc.BindPoint;
                    cb.DxilSpace = bindDesc.Space;
                }
                break;

            case D3D_SIT_TBUFFER:
            case D3D_SIT_TEXTURE:
                binding.Type = ResourceBindingType::ShaderResource;
                binding.Kind = ReflectedResourceKind::SampledTexture;
                binding.IsReadOnly = true;
                break;

            case D3D_SIT_SAMPLER:
                binding.Type = ResourceBindingType::Sampler;
                binding.Kind = ReflectedResourceKind::Sampler;
                binding.IsReadOnly = true;
                binding.IsComparisonSampler = (bindDesc.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0u;
                break;

            case D3D_SIT_UAV_RWTYPED:
                binding.Type = ResourceBindingType::UnorderedAccess;
                binding.Kind = ReflectedResourceKind::StorageTexture;
                binding.IsReadOnly = false;
                break;

            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
                binding.Type = ResourceBindingType::ShaderResource;
                binding.Kind = ReflectedResourceKind::StorageBuffer;
                binding.IsReadOnly = true;
                break;

            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                binding.Type = ResourceBindingType::UnorderedAccess;
                binding.Kind = ReflectedResourceKind::RWStorageBuffer;
                binding.IsReadOnly = false;
                break;

            case D3D_SIT_RTACCELERATIONSTRUCTURE:
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

            binding.Dimension = MapDxilDimension(bindDesc.Dimension, binding.Kind);
            outData.Bindings.push_back(std::move(binding));
        }
    }

    Core::Result<ShaderReflectionData, std::string> ReflectDxilBinary(
        std::span<const u8> bytecode,
        IDxcUtils* utils,
        IDxcContainerReflection* containerReflection,
        std::optional<ShaderStage> expectedStage,
        std::string_view expectedEntryPoint)
    {
        if (utils == nullptr || containerReflection == nullptr)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: DXC runtime interfaces are not initialized.");
        }
        if (bytecode.empty())
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: bytecode buffer is empty.");
        }
        if (bytecode.size() < kMinDxbcHeaderBytes ||
            bytecode.size() > static_cast<usize>(std::numeric_limits<UINT32>::max()))
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: invalid DXIL container size (" +
                std::to_string(bytecode.size()) + " bytes).");
        }

        u32 magic = 0u;
        std::memcpy(&magic, bytecode.data(), sizeof(magic));
        if (magic != kDxbcFourCc)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: invalid DXBC container magic.");
        }

        struct ContainerLoadResetGuard final
        {
            IDxcContainerReflection* Reflection = nullptr;
            ~ContainerLoadResetGuard()
            {
                if (Reflection != nullptr)
                {
                    Reflection->Load(nullptr);
                }
            }
        };

        DxcComPtr<IDxcBlobEncoding> containerBlob;
        HRESULT hr = utils->CreateBlobFromPinned(
            bytecode.data(),
            static_cast<UINT32>(bytecode.size()),
            DXC_CP_ACP,
            containerBlob.GetAddressOf());
        if (FAILED(hr) || !containerBlob)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: CreateBlobFromPinned failed.");
        }

        hr = containerReflection->Load(containerBlob.Get());
        if (FAILED(hr))
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: IDxcContainerReflection::Load failed.");
        }
        ContainerLoadResetGuard loadGuard{ containerReflection };

        UINT32 partIndex = 0u;
        DxcComPtr<ID3D12ShaderReflection> d3dReflection;
        if (SUCCEEDED(containerReflection->FindFirstPartKind(DXC_PART_DXIL, &partIndex)))
        {
            hr = containerReflection->GetPartReflection(
                partIndex,
                GetDxcInterfaceId<ID3D12ShaderReflection>(),
                d3dReflection.PutVoid());
        }
        if (!d3dReflection && SUCCEEDED(containerReflection->FindFirstPartKind(DXC_PART_REFLECTION_DATA, &partIndex)))
        {
            hr = containerReflection->GetPartReflection(
                partIndex,
                GetDxcInterfaceId<ID3D12ShaderReflection>(),
                d3dReflection.PutVoid());
        }
        if (!d3dReflection)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: container has no reflectable DXIL/STAT part.");
        }

        D3D12_SHADER_DESC shaderDesc{};
        hr = d3dReflection->GetDesc(&shaderDesc);
        if (FAILED(hr))
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: ID3D12ShaderReflection::GetDesc failed.");
        }

        const auto stageOpt = MapDxilStage(shaderDesc.Version);
        if (!stageOpt.has_value())
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                "DXIL reflection failed: unsupported DXIL shader stage.");
        }
        if (expectedStage.has_value() && *stageOpt != *expectedStage)
        {
            return Core::Result<ShaderReflectionData, std::string>::Failure(
                std::string("DXIL reflection stage mismatch: expected ") +
                ShaderStageName(*expectedStage) + ", got " + ShaderStageName(*stageOpt));
        }

        ShaderReflectionData data{};
        data.TargetFormat = ShaderTargetFormat::DXIL;
        data.Stage = *stageOpt;
        data.EntryPoint = std::string(expectedEntryPoint);
        data.IsValid = true;

        if (data.Stage == ShaderStage::Compute)
        {
            UINT gx = 1u;
            UINT gy = 1u;
            UINT gz = 1u;
            d3dReflection->GetThreadGroupSize(&gx, &gy, &gz);
            data.ThreadGroupSizeX = std::max(1u, static_cast<u32>(gx));
            data.ThreadGroupSizeY = std::max(1u, static_cast<u32>(gy));
            data.ThreadGroupSizeZ = std::max(1u, static_cast<u32>(gz));
        }

        const UINT64 requiresFlags = d3dReflection->GetRequiresFlags();
        if ((requiresFlags & (kDxilRequiresResourceHeapBit | kDxilRequiresSamplerHeapBit)) != 0ull)
        {
            data.UsesBindlessHeap = true;
        }

        std::unordered_map<std::string, u32> cbIndexByName;
        for (UINT i = 0u; i < shaderDesc.ConstantBuffers; ++i)
        {
            ID3D12ShaderReflectionConstantBuffer* cb = d3dReflection->GetConstantBufferByIndex(i);
            if (cb == nullptr)
            {
                continue;
            }
            D3D12_SHADER_BUFFER_DESC cbDesc{};
            if (FAILED(cb->GetDesc(&cbDesc)) || cbDesc.Type != D3D_CT_CBUFFER)
            {
                continue;
            }

            ReflectedConstantBuffer converted = ConvertDxilConstantBuffer(cb);
            const u32 index = static_cast<u32>(data.ConstantBuffers.size());
            if (!converted.Name.empty())
            {
                cbIndexByName.try_emplace(converted.Name, index);
            }
            data.ConstantBuffers.push_back(std::move(converted));
        }

        const ReflectedShaderVisibility visibility = VisibilityFromStage(data.Stage);
        data.Bindings.reserve(shaderDesc.BoundResources);
        for (UINT i = 0u; i < shaderDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
            if (SUCCEEDED(d3dReflection->GetResourceBindingDesc(i, &bindDesc)))
            {
                PopulateDxilBinding(bindDesc, visibility, cbIndexByName, data);
            }
        }

        std::sort(data.Bindings.begin(), data.Bindings.end(),
            [](const ReflectedBinding& a, const ReflectedBinding& b)
            {
                if (a.Type != b.Type)
                {
                    return static_cast<u8>(a.Type) < static_cast<u8>(b.Type);
                }
                if (a.RegisterSpace != b.RegisterSpace)
                {
                    return a.RegisterSpace < b.RegisterSpace;
                }
                if (a.RegisterSlot != b.RegisterSlot)
                {
                    return a.RegisterSlot < b.RegisterSlot;
                }
                return a.Name < b.Name;
            });

        data.StageInputs.reserve(shaderDesc.InputParameters);
        for (UINT i = 0u; i < shaderDesc.InputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc{};
            if (SUCCEEDED(d3dReflection->GetInputParameterDesc(i, &paramDesc)))
            {
                PopulateSignatureParameter(paramDesc, data.StageInputs);
            }
        }

        data.StageOutputs.reserve(shaderDesc.OutputParameters);
        for (UINT i = 0u; i < shaderDesc.OutputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc{};
            if (SUCCEEDED(d3dReflection->GetOutputParameterDesc(i, &paramDesc)))
            {
                PopulateSignatureParameter(paramDesc, data.StageOutputs);
            }
        }

        return Core::Result<ShaderReflectionData, std::string>::Success(std::move(data));
    }
}
