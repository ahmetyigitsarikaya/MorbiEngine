#include "Morbi/Shader/ShaderReflection.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Morbi::Shader
{
    namespace
    {
        [[nodiscard]] bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (usize i = 0; i < a.size(); ++i)
            {
                const unsigned char ca = static_cast<unsigned char>(a[i]);
                const unsigned char cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] const char* ScalarTypeName(ReflectedScalarType t) noexcept
        {
            switch (t)
            {
            case ReflectedScalarType::Unknown: return "Unknown";
            case ReflectedScalarType::Bool:    return "Bool";
            case ReflectedScalarType::Int16:   return "Int16";
            case ReflectedScalarType::UInt16:  return "UInt16";
            case ReflectedScalarType::Int32:   return "Int32";
            case ReflectedScalarType::UInt32:  return "UInt32";
            case ReflectedScalarType::Int64:   return "Int64";
            case ReflectedScalarType::UInt64:  return "UInt64";
            case ReflectedScalarType::Float16: return "Float16";
            case ReflectedScalarType::Float32: return "Float32";
            case ReflectedScalarType::Float64: return "Float64";
            case ReflectedScalarType::Struct:  return "Struct";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string FormatStageParam(const ReflectedStageParameter& p)
        {
            std::string desc = !p.SemanticName.empty()
                ? (p.SemanticName + std::to_string(p.SemanticIndex))
                : (!p.Name.empty() ? p.Name : std::string("<unnamed>"));
            if (p.Location.has_value())
            {
                desc += " (location=" + std::to_string(*p.Location) + ")";
            }
            else if (p.Register.has_value())
            {
                desc += " (register=" + std::to_string(*p.Register) + ")";
            }
            return desc;
        }

        [[nodiscard]] const ReflectedStageParameter* FindMatchingProducerOutput(
            const std::vector<ReflectedStageParameter>& producerOutputs,
            const ReflectedStageParameter& consumerInput,
            std::vector<bool>& outMatchedFlags)
        {
            for (usize i = 0; i < producerOutputs.size(); ++i)
            {
                const auto& prod = producerOutputs[i];
                if (prod.IsSystemValue)
                {
                    continue;
                }

                if (consumerInput.Location.has_value() && prod.Location.has_value())
                {
                    if (*consumerInput.Location == *prod.Location)
                    {
                        outMatchedFlags[i] = true;
                        return &prod;
                    }
                    continue;
                }

                if (!consumerInput.SemanticName.empty() && !prod.SemanticName.empty())
                {
                    if (EqualsIgnoreCase(consumerInput.SemanticName, prod.SemanticName) &&
                        consumerInput.SemanticIndex == prod.SemanticIndex)
                    {
                        outMatchedFlags[i] = true;
                        return &prod;
                    }
                    continue;
                }

                if (consumerInput.Register.has_value() && prod.Register.has_value() &&
                    *consumerInput.Register == *prod.Register)
                {
                    outMatchedFlags[i] = true;
                    return &prod;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const char* BindingTypeName(ResourceBindingType t) noexcept
        {
            switch (t)
            {
            case ResourceBindingType::ConstantBuffer:  return "ConstantBuffer";
            case ResourceBindingType::ShaderResource:  return "ShaderResource";
            case ResourceBindingType::UnorderedAccess: return "UnorderedAccess";
            case ResourceBindingType::Sampler:         return "Sampler";
            case ResourceBindingType::PushConstants:   return "PushConstants";
            }
            return "Unknown";
        }

        [[nodiscard]] u32 AlignUp16(u32 value) noexcept
        {
            return (value + 15u) & ~15u;
        }

        void ValidateMembersLayoutCompatibility(
            const std::string& scopeName,
            const std::vector<ReflectedShaderMember>& spvMembers,
            const std::vector<ReflectedShaderMember>& dxilMembers,
            const CrossBackendBindingPolicy& policy,
            ReflectionValidationResult& result)
        {
            if (spvMembers.size() != dxilMembers.size())
            {
                result.AddError(
                    "CROSS_LAYOUT_MEMBER_COUNT_MISMATCH",
                    scopeName + ": member count mismatch (SPIR-V has " +
                    std::to_string(spvMembers.size()) + ", DXIL has " +
                    std::to_string(dxilMembers.size()) + ").");
                return;
            }

            for (usize i = 0; i < spvMembers.size(); ++i)
            {
                const auto& spvM = spvMembers[i];
                const auto& dxilM = dxilMembers[i];
                const std::string memberScope = scopeName + "." +
                    (!spvM.Name.empty() ? spvM.Name : (!dxilM.Name.empty() ? dxilM.Name : ("[" + std::to_string(i) + "]")));

                if (!spvM.Name.empty() && !dxilM.Name.empty() && spvM.Name != dxilM.Name)
                {
                    result.AddError(
                        "CROSS_LAYOUT_MEMBER_NAME_MISMATCH",
                        memberScope + ": member name mismatch (SPIR-V='" + spvM.Name +
                        "', DXIL='" + dxilM.Name + "').");
                }

                if (spvM.OffsetBytes != dxilM.OffsetBytes)
                {
                    result.AddError(
                        "CROSS_LAYOUT_OFFSET_MISMATCH",
                        memberScope + ": offset mismatch (SPIR-V=" + std::to_string(spvM.OffsetBytes) +
                        "B, DXIL=" + std::to_string(dxilM.OffsetBytes) + "B).");
                }

                if (spvM.SizeBytes != dxilM.SizeBytes)
                {
                    result.AddError(
                        "CROSS_LAYOUT_SIZE_MISMATCH",
                        memberScope + ": byte size mismatch (SPIR-V=" + std::to_string(spvM.SizeBytes) +
                        "B, DXIL=" + std::to_string(dxilM.SizeBytes) + "B).");
                }

                if (spvM.ScalarType != ReflectedScalarType::Unknown &&
                    dxilM.ScalarType != ReflectedScalarType::Unknown &&
                    spvM.ScalarType != dxilM.ScalarType)
                {
                    result.AddError(
                        "CROSS_LAYOUT_TYPE_MISMATCH",
                        memberScope + ": scalar type mismatch (SPIR-V=" + ScalarTypeName(spvM.ScalarType) +
                        ", DXIL=" + ScalarTypeName(dxilM.ScalarType) + ").");
                }

                const bool spvIsMatrix =
                    spvM.Class == ReflectedVariableClass::MatrixRows ||
                    spvM.Class == ReflectedVariableClass::MatrixColumns;
                const bool dxilIsMatrix =
                    dxilM.Class == ReflectedVariableClass::MatrixRows ||
                    dxilM.Class == ReflectedVariableClass::MatrixColumns;

                if (spvIsMatrix || dxilIsMatrix)
                {
                    if (!spvIsMatrix || !dxilIsMatrix)
                    {
                        result.AddError(
                            "CROSS_LAYOUT_SHAPE_MISMATCH",
                            memberScope + ": matrix vs non-matrix variable class mismatch.");
                    }
                    else
                    {
                        if (spvM.Rows != dxilM.Rows || spvM.Columns != dxilM.Columns)
                        {
                            result.AddError(
                                "CROSS_LAYOUT_MATRIX_DIMS_MISMATCH",
                                memberScope + ": matrix dimensions mismatch (SPIR-V=" +
                                std::to_string(spvM.Rows) + "x" + std::to_string(spvM.Columns) +
                                ", DXIL=" + std::to_string(dxilM.Rows) + "x" + std::to_string(dxilM.Columns) + ").");
                        }
                        if (!policy.AllowMatrixMajornessRepresentationDifference &&
                            spvM.IsRowMajor != dxilM.IsRowMajor)
                        {
                            result.AddError(
                                "CROSS_LAYOUT_MATRIX_MAJORNESS_MISMATCH",
                                memberScope + ": matrix majorness representation mismatch.");
                        }
                        if (spvM.MatrixStrideBytes != 0u && dxilM.MatrixStrideBytes != 0u &&
                            spvM.MatrixStrideBytes != dxilM.MatrixStrideBytes)
                        {
                            result.AddError(
                                "CROSS_LAYOUT_MATRIX_STRIDE_MISMATCH",
                                memberScope + ": matrix stride mismatch (SPIR-V=" +
                                std::to_string(spvM.MatrixStrideBytes) + "B, DXIL=" +
                                std::to_string(dxilM.MatrixStrideBytes) + "B).");
                        }
                    }
                }
                else if (spvM.Class != dxilM.Class || spvM.Rows != dxilM.Rows || spvM.Columns != dxilM.Columns)
                {
                    result.AddError(
                        "CROSS_LAYOUT_SHAPE_MISMATCH",
                        memberScope + ": variable class or vector dimension mismatch.");
                }

                if (spvM.ArrayElementCount != dxilM.ArrayElementCount)
                {
                    result.AddError(
                        "CROSS_LAYOUT_ARRAY_COUNT_MISMATCH",
                        memberScope + ": array element count mismatch (SPIR-V=" +
                        std::to_string(spvM.ArrayElementCount) + ", DXIL=" +
                        std::to_string(dxilM.ArrayElementCount) + ").");
                }

                if (spvM.ArrayStrideBytes != 0u && dxilM.ArrayStrideBytes != 0u &&
                    spvM.ArrayStrideBytes != dxilM.ArrayStrideBytes)
                {
                    result.AddError(
                        "CROSS_LAYOUT_ARRAY_STRIDE_MISMATCH",
                        memberScope + ": array stride mismatch (SPIR-V=" +
                        std::to_string(spvM.ArrayStrideBytes) + "B, DXIL=" +
                        std::to_string(dxilM.ArrayStrideBytes) + "B).");
                }

                if (!spvM.Members.empty() || !dxilM.Members.empty())
                {
                    ValidateMembersLayoutCompatibility(
                        memberScope,
                        spvM.Members,
                        dxilM.Members,
                        policy,
                        result);
                }
            }
        }

        void ValidateVertexInputsCrossBackend(
            const ShaderReflectionData& spirvReflection,
            const ShaderReflectionData& dxilReflection,
            const CrossBackendBindingPolicy& policy,
            ReflectionValidationResult& result)
        {
            std::vector<const ReflectedStageParameter*> spvInputs;
            for (const auto& p : spirvReflection.StageInputs)
            {
                if (!p.IsSystemValue)
                {
                    spvInputs.push_back(&p);
                }
            }

            std::vector<const ReflectedStageParameter*> dxilInputs;
            for (const auto& p : dxilReflection.StageInputs)
            {
                if (!p.IsSystemValue)
                {
                    dxilInputs.push_back(&p);
                }
            }

            if (spvInputs.size() != dxilInputs.size())
            {
                result.AddError(
                    "CROSS_VERTEX_INPUT_COUNT_MISMATCH",
                    "Vertex input count mismatch (SPIR-V=" + std::to_string(spvInputs.size()) +
                    ", DXIL=" + std::to_string(dxilInputs.size()) + ").");
                return;
            }

            for (usize i = 0; i < spvInputs.size(); ++i)
            {
                const ReflectedStageParameter& spvIn = *spvInputs[i];
                const ReflectedStageParameter* matchedDxil = nullptr;

                if (spvIn.Location.has_value())
                {
                    for (const ReflectedStageParameter* cand : dxilInputs)
                    {
                        if (cand->Register.has_value() && *cand->Register == *spvIn.Location)
                        {
                            matchedDxil = cand;
                            break;
                        }
                    }
                }
                if (matchedDxil == nullptr)
                {
                    matchedDxil = dxilInputs[i];
                }

                if (spvIn.ComponentType != ReflectedScalarType::Unknown &&
                    matchedDxil->ComponentType != ReflectedScalarType::Unknown &&
                    spvIn.ComponentType != matchedDxil->ComponentType)
                {
                    result.AddError(
                        "CROSS_VERTEX_INPUT_TYPE_MISMATCH",
                        "Vertex input " + FormatStageParam(spvIn) + " vs " + FormatStageParam(*matchedDxil) +
                        " scalar type mismatch (SPIR-V=" + ScalarTypeName(spvIn.ComponentType) +
                        ", DXIL=" + ScalarTypeName(matchedDxil->ComponentType) + ").");
                }

                if (spvIn.ComponentCount != matchedDxil->ComponentCount)
                {
                    result.AddError(
                        "CROSS_VERTEX_INPUT_WIDTH_MISMATCH",
                        "Vertex input " + FormatStageParam(spvIn) + " vs " + FormatStageParam(*matchedDxil) +
                        " component count mismatch (SPIR-V=" + std::to_string(spvIn.ComponentCount) +
                        ", DXIL=" + std::to_string(matchedDxil->ComponentCount) + ").");
                }

                if (policy.RequireMatchingVertexInputSemantics &&
                    !spvIn.SemanticName.empty() && !matchedDxil->SemanticName.empty() &&
                    (!EqualsIgnoreCase(spvIn.SemanticName, matchedDxil->SemanticName) ||
                     spvIn.SemanticIndex != matchedDxil->SemanticIndex))
                {
                    result.AddError(
                        "CROSS_VERTEX_INPUT_SEMANTIC_MISMATCH",
                        "Vertex input semantic mismatch (SPIR-V='" + spvIn.SemanticName +
                        "', DXIL='" + matchedDxil->SemanticName + "').");
                }
            }
        }
    }

    ReflectionValidationResult ShaderReflectionValidator::ValidateStageInterface(
        const ShaderReflectionData& producerStage,
        const ShaderReflectionData& consumerStage)
    {
        ReflectionValidationResult result{};

        if (!producerStage.IsValid || !consumerStage.IsValid)
        {
            result.AddError(
                "STAGE_INVALID_REFLECTION",
                "Cannot validate stage interface: producer or consumer ShaderReflectionData is invalid.");
            return result;
        }

        if (producerStage.TargetFormat != consumerStage.TargetFormat)
        {
            result.AddError(
                "STAGE_BACKEND_MISMATCH",
                "Cannot link shader stages compiled for different target formats (SPIR-V vs DXIL).");
            return result;
        }

        if (producerStage.Stage != ShaderStage::Vertex || consumerStage.Stage != ShaderStage::Pixel)
        {
            result.AddError(
                "STAGE_ORDER_INVALID",
                "Stage interface validation requires a Vertex producer stage feeding a Pixel consumer stage.");
            return result;
        }

        std::vector<bool> matchedOutputs(producerStage.StageOutputs.size(), false);

        for (const auto& consumerIn : consumerStage.StageInputs)
        {
            if (consumerIn.IsSystemValue)
            {
                continue;
            }

            const ReflectedStageParameter* prodOut = FindMatchingProducerOutput(
                producerStage.StageOutputs,
                consumerIn,
                matchedOutputs);
            if (prodOut == nullptr)
            {
                result.AddError(
                    "STAGE_MISSING_OUTPUT",
                    "Consumer Pixel input " + FormatStageParam(consumerIn) +
                    " is not produced by Vertex stage outputs.");
                continue;
            }

            if (prodOut->ComponentType != ReflectedScalarType::Unknown &&
                consumerIn.ComponentType != ReflectedScalarType::Unknown &&
                prodOut->ComponentType != consumerIn.ComponentType)
            {
                result.AddError(
                    "STAGE_TYPE_MISMATCH",
                    "Stage parameter " + FormatStageParam(consumerIn) +
                    " scalar type mismatch: producer writes " + ScalarTypeName(prodOut->ComponentType) +
                    ", consumer reads " + ScalarTypeName(consumerIn.ComponentType) + ".");
            }

            if (consumerIn.ComponentMask != 0u && prodOut->ComponentMask != 0u)
            {
                if ((consumerIn.ComponentMask & prodOut->ComponentMask) != consumerIn.ComponentMask)
                {
                    result.AddError(
                        "STAGE_COMPONENT_MASK_MISMATCH",
                        "Stage parameter " + FormatStageParam(consumerIn) +
                        " reads component mask " + std::to_string(consumerIn.ComponentMask) +
                        " which is not covered by producer mask " + std::to_string(prodOut->ComponentMask) + ".");
                }
            }
            else if (consumerIn.ComponentCount > prodOut->ComponentCount)
            {
                result.AddError(
                    "STAGE_COMPONENT_COUNT_MISMATCH",
                    "Stage parameter " + FormatStageParam(consumerIn) +
                    " expects " + std::to_string(consumerIn.ComponentCount) +
                    " components, but producer outputs only " + std::to_string(prodOut->ComponentCount) + ".");
            }
        }

        for (usize i = 0; i < producerStage.StageOutputs.size(); ++i)
        {
            const auto& prodOut = producerStage.StageOutputs[i];
            if (!prodOut.IsSystemValue && !matchedOutputs[i])
            {
                result.AddWarning(
                    "STAGE_UNUSED_PRODUCER_OUTPUT",
                    "Producer Vertex output " + FormatStageParam(prodOut) +
                    " is not consumed by Pixel stage inputs.");
            }
        }

        return result;
    }

    ReflectionValidationResult ShaderReflectionValidator::ValidateCrossBackendCompatibility(
        const ShaderReflectionData& spirvReflection,
        const ShaderReflectionData& dxilReflection,
        const CrossBackendBindingPolicy& policy)
    {
        ReflectionValidationResult result{};

        if (!spirvReflection.IsValid || !dxilReflection.IsValid)
        {
            result.AddError(
                "CROSS_INVALID_REFLECTION",
                "Cannot validate cross-backend compatibility: SPIR-V or DXIL ShaderReflectionData is invalid.");
            return result;
        }

        if (spirvReflection.TargetFormat != ShaderTargetFormat::SPIRV ||
            dxilReflection.TargetFormat != ShaderTargetFormat::DXIL)
        {
            result.AddError(
                "CROSS_WRONG_TARGET_FORMAT",
                "ValidateCrossBackendCompatibility expects first parameter SPIRV and second parameter DXIL.");
            return result;
        }

        if (spirvReflection.Stage != dxilReflection.Stage)
        {
            result.AddError(
                "CROSS_STAGE_MISMATCH",
                "Cross-backend shader stage mismatch between SPIR-V and DXIL artifacts.");
            return result;
        }

        if (!spirvReflection.EntryPoint.empty() && !dxilReflection.EntryPoint.empty() &&
            spirvReflection.EntryPoint != dxilReflection.EntryPoint)
        {
            result.AddError(
                "CROSS_ENTRYPOINT_MISMATCH",
                "Cross-backend entry point mismatch (SPIR-V='" + spirvReflection.EntryPoint +
                "', DXIL='" + dxilReflection.EntryPoint + "').");
        }

        if (spirvReflection.Stage == ShaderStage::Compute &&
            (spirvReflection.ThreadGroupSizeX != dxilReflection.ThreadGroupSizeX ||
             spirvReflection.ThreadGroupSizeY != dxilReflection.ThreadGroupSizeY ||
             spirvReflection.ThreadGroupSizeZ != dxilReflection.ThreadGroupSizeZ))
        {
            result.AddError(
                "CROSS_THREADGROUP_SIZE_MISMATCH",
                "Compute thread group size mismatch (SPIR-V=" +
                std::to_string(spirvReflection.ThreadGroupSizeX) + "x" +
                std::to_string(spirvReflection.ThreadGroupSizeY) + "x" +
                std::to_string(spirvReflection.ThreadGroupSizeZ) + ", DXIL=" +
                std::to_string(dxilReflection.ThreadGroupSizeX) + "x" +
                std::to_string(dxilReflection.ThreadGroupSizeY) + "x" +
                std::to_string(dxilReflection.ThreadGroupSizeZ) + ").");
        }

        if (spirvReflection.Stage == ShaderStage::Vertex)
        {
            ValidateVertexInputsCrossBackend(spirvReflection, dxilReflection, policy, result);
        }

        std::vector<bool> matchedDxilBindings(dxilReflection.Bindings.size(), false);

        // 1. Validate Push Constants vs DX12 Root Constant rule
        if (!spirvReflection.PushConstants.empty())
        {
            if (!policy.RootConstantRule.Enabled)
            {
                result.AddError(
                    "CROSS_UNMAPPED_PUSH_CONSTANT",
                    "SPIR-V defines Push Constants, but CrossBackendBindingPolicy has RootConstantRule disabled.");
            }
            else
            {
                if (spirvReflection.PushConstantSizeBytes > policy.RootConstantRule.MaxSizeBytes)
                {
                    result.AddError(
                        "CROSS_PUSH_CONSTANT_SIZE_EXCEEDS_POLICY",
                        "SPIR-V PushConstant size (" + std::to_string(spirvReflection.PushConstantSizeBytes) +
                        "B) exceeds policy limit (" + std::to_string(policy.RootConstantRule.MaxSizeBytes) + "B).");
                }

                usize matchedRootIdx = std::numeric_limits<usize>::max();
                for (usize j = 0; j < dxilReflection.Bindings.size(); ++j)
                {
                    const auto& b = dxilReflection.Bindings[j];
                    if (b.Type == ResourceBindingType::ConstantBuffer &&
                        b.DxilSpace == policy.RootConstantRule.DxilSpace &&
                        b.DxilRegister == policy.RootConstantRule.DxilRegister)
                    {
                        matchedRootIdx = j;
                        matchedDxilBindings[j] = true;
                        break;
                    }
                }

                if (matchedRootIdx == std::numeric_limits<usize>::max())
                {
                    result.AddError(
                        "CROSS_MISSING_DXIL_ROOT_CONSTANT",
                        "SPIR-V defines Push Constants, but DXIL has no matching root ConstantBuffer at (b" +
                        std::to_string(policy.RootConstantRule.DxilRegister) + ", space" +
                        std::to_string(policy.RootConstantRule.DxilSpace) + ").");
                }
                else
                {
                    const auto& dxilRootBinding = dxilReflection.Bindings[matchedRootIdx];
                    const auto& spvPush = spirvReflection.PushConstants[0];

                    if (spvPush.Visibility != dxilRootBinding.Visibility)
                    {
                        result.AddError(
                            "CROSS_ROOT_CONSTANT_VISIBILITY_MISMATCH",
                            "PushConstant vs DX12 RootConstant stage visibility mismatch.");
                    }

                    if (!dxilRootBinding.ConstantBufferIndex.has_value() ||
                        *dxilRootBinding.ConstantBufferIndex >= dxilReflection.ConstantBuffers.size())
                    {
                        result.AddError(
                            "CROSS_MISSING_DXIL_ROOT_CONSTANT_LAYOUT",
                            "DXIL RootConstant binding has no reflected ConstantBuffer member layout.");
                    }
                    else
                    {
                        const auto& dxilRootCb = dxilReflection.ConstantBuffers[*dxilRootBinding.ConstantBufferIndex];
                        const u32 spvAlignedEnd = AlignUp16(spvPush.OffsetBytes + spvPush.SizeBytes);
                        if (spvPush.OffsetBytes != 0u)
                        {
                            result.AddError(
                                "CROSS_ROOT_CONSTANT_OFFSET_MISMATCH",
                                "SPIR-V PushConstant block starts at non-zero byte offset (" +
                                std::to_string(spvPush.OffsetBytes) + "B), which mismatches b" +
                                std::to_string(policy.RootConstantRule.DxilRegister) + ".");
                        }
                        if (spvAlignedEnd != AlignUp16(dxilRootCb.SizeBytes))
                        {
                            result.AddError(
                                "CROSS_ROOT_CONSTANT_SIZE_MISMATCH",
                                "PushConstant aligned byte size (" + std::to_string(spvAlignedEnd) +
                                "B) mismatches DX12 RootConstant size (" +
                                std::to_string(dxilRootCb.SizeBytes) + "B).");
                        }

                        // HLSL [[vk::push_constant]] StructType var; unwraps into top-level SPIR-V block members,
                        // whereas cbuffer RootConstantsBuffer { StructType var; }; wraps them inside one struct field.
                        const std::vector<ReflectedShaderMember>* effectiveSpvMembers = &spvPush.Members;
                        const std::vector<ReflectedShaderMember>* effectiveDxilMembers = &dxilRootCb.Members;
                        if (effectiveSpvMembers->size() > 1u &&
                            effectiveDxilMembers->size() == 1u &&
                            (*effectiveDxilMembers)[0].Class == ReflectedVariableClass::Struct &&
                            (*effectiveDxilMembers)[0].OffsetBytes == 0u &&
                            !(*effectiveDxilMembers)[0].Members.empty())
                        {
                            effectiveDxilMembers = &(*effectiveDxilMembers)[0].Members;
                        }
                        else if (effectiveDxilMembers->size() > 1u &&
                            effectiveSpvMembers->size() == 1u &&
                            (*effectiveSpvMembers)[0].Class == ReflectedVariableClass::Struct &&
                            (*effectiveSpvMembers)[0].OffsetBytes == 0u &&
                            !(*effectiveSpvMembers)[0].Members.empty())
                        {
                            effectiveSpvMembers = &(*effectiveSpvMembers)[0].Members;
                        }

                        ValidateMembersLayoutCompatibility(
                            "PushConstants",
                            *effectiveSpvMembers,
                            *effectiveDxilMembers,
                            policy,
                            result);
                    }
                }
            }
        }
        else if (policy.RootConstantRule.Enabled)
        {
            for (usize j = 0; j < dxilReflection.Bindings.size(); ++j)
            {
                const auto& b = dxilReflection.Bindings[j];
                if (b.Type == ResourceBindingType::ConstantBuffer &&
                    b.DxilSpace == policy.RootConstantRule.DxilSpace &&
                    b.DxilRegister == policy.RootConstantRule.DxilRegister)
                {
                    matchedDxilBindings[j] = true;
                    result.AddError(
                        "CROSS_UNMATCHED_DXIL_ROOT_CONSTANT",
                        "DXIL binds root ConstantBuffer at (b" +
                        std::to_string(policy.RootConstantRule.DxilRegister) + ", space" +
                        std::to_string(policy.RootConstantRule.DxilSpace) +
                        "), but SPIR-V declares no Push Constants.");
                    break;
                }
            }
        }

        // 2. Validate descriptor bindings
        for (const auto& spvB : spirvReflection.Bindings)
        {
            if (!spvB.DescriptorSet.has_value() || !spvB.Binding.has_value())
            {
                result.AddError(
                    "CROSS_SPIRV_MISSING_SET_BINDING",
                    "SPIR-V binding '" + spvB.Name + "' has no reflected DescriptorSet/Binding.");
                continue;
            }

            usize matchedIdx = std::numeric_limits<usize>::max();
            const CrossBackendBindingRule* appliedRule = nullptr;
            bool hadExplicitRuleForVulkanSlot = false;

            for (const auto& rule : policy.ExplicitBindingRules)
            {
                if (rule.VulkanSet == *spvB.DescriptorSet && rule.VulkanBinding == *spvB.Binding)
                {
                    hadExplicitRuleForVulkanSlot = true;
                    for (usize j = 0; j < dxilReflection.Bindings.size(); ++j)
                    {
                        if (matchedDxilBindings[j])
                        {
                            continue;
                        }
                        const auto& dxilB = dxilReflection.Bindings[j];
                        if (dxilB.Type == rule.DxilRegisterClass &&
                            dxilB.DxilSpace == rule.DxilSpace &&
                            dxilB.DxilRegister == rule.DxilRegister)
                        {
                            matchedIdx = j;
                            appliedRule = &rule;
                            break;
                        }
                    }
                    if (matchedIdx != std::numeric_limits<usize>::max())
                    {
                        break;
                    }
                }
            }

            if (matchedIdx == std::numeric_limits<usize>::max() &&
                !hadExplicitRuleForVulkanSlot &&
                policy.FallbackToIdentitySetSpaceMapping)
            {
                for (usize j = 0; j < dxilReflection.Bindings.size(); ++j)
                {
                    if (matchedDxilBindings[j])
                    {
                        continue;
                    }
                    const auto& dxilB = dxilReflection.Bindings[j];
                    if (dxilB.Type == spvB.Type &&
                        dxilB.DxilSpace == *spvB.DescriptorSet &&
                        dxilB.DxilRegister == *spvB.Binding)
                    {
                        matchedIdx = j;
                        break;
                    }
                }
            }

            if (matchedIdx == std::numeric_limits<usize>::max())
            {
                result.AddError(
                    "CROSS_MISSING_DXIL_BINDING",
                    "No compatible DXIL binding found for SPIR-V binding '" + spvB.Name +
                    "' (set=" + std::to_string(*spvB.DescriptorSet) +
                    ", binding=" + std::to_string(*spvB.Binding) +
                    ", type=" + BindingTypeName(spvB.Type) + ").");
                continue;
            }

            matchedDxilBindings[matchedIdx] = true;
            const auto& dxilB = dxilReflection.Bindings[matchedIdx];

            if (spvB.Type != dxilB.Type)
            {
                result.AddError(
                    "CROSS_BINDING_TYPE_MISMATCH",
                    "Binding '" + spvB.Name + "' class mismatch (SPIR-V=" +
                    BindingTypeName(spvB.Type) + ", DXIL=" + BindingTypeName(dxilB.Type) + ").");
            }

            if (spvB.Kind != dxilB.Kind)
            {
                result.AddError(
                    "CROSS_BINDING_KIND_MISMATCH",
                    "Binding '" + spvB.Name + "' vs DXIL '" + dxilB.Name + "' resource kind mismatch.");
            }

            if (spvB.Dimension != ReflectedResourceDimension::Unknown &&
                dxilB.Dimension != ReflectedResourceDimension::Unknown &&
                spvB.Dimension != dxilB.Dimension)
            {
                result.AddError(
                    "CROSS_BINDING_DIMENSION_MISMATCH",
                    "Binding '" + spvB.Name + "' vs DXIL '" + dxilB.Name + "' dimension mismatch.");
            }

            if (spvB.IsReadOnly != dxilB.IsReadOnly)
            {
                result.AddError(
                    "CROSS_BINDING_READONLY_MISMATCH",
                    "Binding '" + spvB.Name + "' vs DXIL '" + dxilB.Name + "' read-only/UAV access mismatch.");
            }

            if (spvB.Visibility != dxilB.Visibility)
            {
                result.AddError(
                    "CROSS_BINDING_VISIBILITY_MISMATCH",
                    "Binding '" + spvB.Name + "' vs DXIL '" + dxilB.Name + "' stage visibility mismatch.");
            }

            if (spvB.IsUnboundedArray != dxilB.IsUnboundedArray || spvB.Count != dxilB.Count)
            {
                const bool allowedCbvException =
                    appliedRule != nullptr &&
                    appliedRule->AllowUnboundedArrayToSingleCbv &&
                    spvB.Kind == ReflectedResourceKind::UniformBuffer &&
                    dxilB.Kind == ReflectedResourceKind::UniformBuffer &&
                    dxilB.Count == 1u;

                if (allowedCbvException)
                {
                    result.AddWarning(
                        "CROSS_BINDLESS_CBV_POLICY_EXCEPTION",
                        "SPIR-V UBO descriptor array '" + spvB.Name + "' mapped to single DX12 Root CBV '" +
                        dxilB.Name + "' via explicit MorbiBindless policy rule.");
                }
                else
                {
                    result.AddError(
                        "CROSS_BINDING_COUNT_MISMATCH",
                        "Binding '" + spvB.Name + "' vs DXIL '" + dxilB.Name + "' descriptor cardinality mismatch (SPIR-V count=" +
                        (spvB.IsUnboundedArray ? std::string("unbounded") : std::to_string(spvB.Count)) +
                        ", DXIL count=" +
                        (dxilB.IsUnboundedArray ? std::string("unbounded") : std::to_string(dxilB.Count)) + ").");
                }
            }

            if (spvB.ConstantBufferIndex.has_value() && dxilB.ConstantBufferIndex.has_value())
            {
                const auto& spvCb = spirvReflection.ConstantBuffers[*spvB.ConstantBufferIndex];
                const auto& dxilCb = dxilReflection.ConstantBuffers[*dxilB.ConstantBufferIndex];
                if (spvCb.SizeBytes != dxilCb.SizeBytes)
                {
                    result.AddError(
                        "CROSS_CBUFFER_SIZE_MISMATCH",
                        "ConstantBuffer '" + spvCb.Name + "' size mismatch (SPIR-V=" +
                        std::to_string(spvCb.SizeBytes) + "B, DXIL=" +
                        std::to_string(dxilCb.SizeBytes) + "B).");
                }
                ValidateMembersLayoutCompatibility(
                    "ConstantBuffer(" + spvCb.Name + ")",
                    spvCb.Members,
                    dxilCb.Members,
                    policy,
                    result);
            }
        }

        // 3. Report any unmatched DXIL bindings
        for (usize j = 0; j < dxilReflection.Bindings.size(); ++j)
        {
            if (matchedDxilBindings[j])
            {
                continue;
            }
            const auto& dxilB = dxilReflection.Bindings[j];
            result.AddError(
                "CROSS_UNMATCHED_DXIL_BINDING",
                "DXIL binding '" + dxilB.Name + "' (reg=" +
                std::to_string(dxilB.DxilRegister.value_or(dxilB.RegisterSlot)) +
                ", space=" + std::to_string(dxilB.DxilSpace.value_or(dxilB.RegisterSpace)) +
                ", type=" + BindingTypeName(dxilB.Type) + ") has no compatible SPIR-V binding.");
        }

        return result;
    }
}
