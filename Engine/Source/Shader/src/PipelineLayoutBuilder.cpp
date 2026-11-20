#include "Morbi/Shader/PipelineLayoutBuilder.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Morbi::Shader
{
    namespace
    {
        [[nodiscard]] RHI::ShaderVisibility ToRhiVisibility(ShaderStage stage) noexcept
        {
            switch (stage)
            {
            case ShaderStage::Vertex:  return RHI::ShaderVisibility::Vertex;
            case ShaderStage::Pixel:   return RHI::ShaderVisibility::Pixel;
            case ShaderStage::Compute: return RHI::ShaderVisibility::Compute;
            }
            return RHI::ShaderVisibility::None;
        }

        [[nodiscard]] RHI::ShaderVisibility ConvertVisibility(ReflectedShaderVisibility vis, ShaderStage fallbackStage) noexcept
        {
            RHI::ShaderVisibility out = RHI::ShaderVisibility::None;
            if (static_cast<u32>(vis & ReflectedShaderVisibility::Vertex) != 0u)  out |= RHI::ShaderVisibility::Vertex;
            if (static_cast<u32>(vis & ReflectedShaderVisibility::Pixel) != 0u)   out |= RHI::ShaderVisibility::Pixel;
            if (static_cast<u32>(vis & ReflectedShaderVisibility::Compute) != 0u) out |= RHI::ShaderVisibility::Compute;
            return out != RHI::ShaderVisibility::None ? out : ToRhiVisibility(fallbackStage);
        }

        [[nodiscard]] std::optional<RHI::DescriptorType> ConvertDescriptorType(const ReflectedBinding& b) noexcept
        {
            switch (b.Kind)
            {
            case ReflectedResourceKind::UniformBuffer:         return RHI::DescriptorType::UniformBuffer;
            case ReflectedResourceKind::StorageBuffer:         return RHI::DescriptorType::StorageBuffer;
            case ReflectedResourceKind::RWStorageBuffer:       return RHI::DescriptorType::RWStorageBuffer;
            case ReflectedResourceKind::SampledTexture:        return RHI::DescriptorType::SampledTexture;
            case ReflectedResourceKind::StorageTexture:        return RHI::DescriptorType::StorageTexture;
            case ReflectedResourceKind::Sampler:               return RHI::DescriptorType::Sampler;
            case ReflectedResourceKind::CombinedImageSampler:  return RHI::DescriptorType::CombinedImageSampler;
            case ReflectedResourceKind::AccelerationStructure: return RHI::DescriptorType::AccelerationStructure;
            default:                                           return std::nullopt;
            }
        }

        [[nodiscard]] bool IsMorbiGlobalBindlessSet0Slot(u32 set, u32 binding, RHI::DescriptorType type) noexcept
        {
            if (set != 0u)
            {
                return false;
            }
            switch (binding)
            {
            case 0u: return type == RHI::DescriptorType::StorageBuffer;
            case 1u: return type == RHI::DescriptorType::SampledTexture;
            case 2u: return type == RHI::DescriptorType::Sampler;
            case 3u: return type == RHI::DescriptorType::UniformBuffer;
            default: return false;
            }
        }

        [[nodiscard]] const CrossBackendBindingRule* FindRuleForSpirv(
            const CrossBackendBindingPolicy& policy,
            ResourceBindingType type,
            u32 set,
            u32 binding) noexcept
        {
            const CrossBackendBindingRule* fallbackClassMismatch = nullptr;
            for (const auto& rule : policy.ExplicitBindingRules)
            {
                if (rule.VulkanSet == set && rule.VulkanBinding == binding)
                {
                    if (rule.DxilRegisterClass == type)
                    {
                        return &rule;
                    }
                    fallbackClassMismatch = &rule;
                }
            }
            return fallbackClassMismatch;
        }

        [[nodiscard]] const CrossBackendBindingRule* FindRuleForDxil(
            const CrossBackendBindingPolicy& policy,
            ResourceBindingType type,
            u32 space,
            u32 reg) noexcept
        {
            for (const auto& rule : policy.ExplicitBindingRules)
            {
                if (rule.DxilRegisterClass == type &&
                    rule.DxilSpace == space &&
                    rule.DxilRegister == reg)
                {
                    return &rule;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::string FormatIssues(const RHI::PipelineLayoutValidationResult& val)
        {
            std::string msg;
            for (const auto& issue : val.Issues)
            {
                if (issue.Level != RHI::PipelineLayoutIssue::Severity::Error)
                {
                    continue;
                }
                if (!msg.empty()) msg += "; ";
                msg += "[" + issue.Code + "] " + issue.Message;
            }
            return msg;
        }

        [[nodiscard]] std::string FormatReflectionIssues(const ReflectionValidationResult& val)
        {
            std::string msg;
            for (const auto& issue : val.Issues)
            {
                if (issue.Level != ReflectionValidationIssue::Severity::Error)
                {
                    continue;
                }
                if (!msg.empty()) msg += "; ";
                msg += "[" + issue.Code + "] " + issue.Message;
            }
            return msg;
        }

        bool ValidateStageComposition(
            std::span<const ShaderReflectionData> stages,
            bool validateInterfaces,
            std::string& outError)
        {
            if (stages.empty())
            {
                outError = "PipelineLayoutBuilder failed: no shader stage reflection inputs provided.";
                return false;
            }

            const ShaderReflectionData* vertexStage = nullptr;
            const ShaderReflectionData* pixelStage = nullptr;
            const ShaderReflectionData* computeStage = nullptr;

            for (const auto& s : stages)
            {
                if (!s.IsValid)
                {
                    outError = "PipelineLayoutBuilder failed: invalid ShaderReflectionData input.";
                    return false;
                }

                switch (s.Stage)
                {
                case ShaderStage::Vertex:
                    if (vertexStage != nullptr)
                    {
                        outError = "PipelineLayoutBuilder failed: duplicate Vertex stage provided.";
                        return false;
                    }
                    vertexStage = &s;
                    break;
                case ShaderStage::Pixel:
                    if (pixelStage != nullptr)
                    {
                        outError = "PipelineLayoutBuilder failed: duplicate Pixel stage provided.";
                        return false;
                    }
                    pixelStage = &s;
                    break;
                case ShaderStage::Compute:
                    if (computeStage != nullptr)
                    {
                        outError = "PipelineLayoutBuilder failed: duplicate Compute stage provided.";
                        return false;
                    }
                    computeStage = &s;
                    break;
                }
            }

            if (computeStage != nullptr && (vertexStage != nullptr || pixelStage != nullptr))
            {
                outError = "PipelineLayoutBuilder failed: Compute stage cannot be combined with Graphics stages.";
                return false;
            }

            if (validateInterfaces && vertexStage != nullptr && pixelStage != nullptr)
            {
                const auto stageVal = ShaderReflectionValidator::ValidateStageInterface(*vertexStage, *pixelStage);
                if (!stageVal.IsCompatible)
                {
                    outError = "PipelineLayoutBuilder failed (stage interface mismatch): " +
                        FormatReflectionIssues(stageVal);
                    return false;
                }
            }

            return true;
        }

        bool MergeBindingIntoSet(
            std::unordered_map<u32, RHI::DescriptorSetLayoutDesc>& setMap,
            const RHI::PipelineBindingLayout& incoming,
            bool allowUpdateAfterBind,
            std::string& outError)
        {
            auto& setDesc = setMap[incoming.Set];
            setDesc.SetIndex = incoming.Set;
            if (allowUpdateAfterBind)
            {
                setDesc.AllowUpdateAfterBind = true;
            }

            for (auto& existing : setDesc.Bindings)
            {
                if (existing.Binding != incoming.Binding)
                {
                    continue;
                }

                if (existing.Type != incoming.Type ||
                    existing.IsReadOnly != incoming.IsReadOnly ||
                    existing.IsComparisonSampler != incoming.IsComparisonSampler ||
                    existing.IsUnboundedArray != incoming.IsUnboundedArray ||
                    existing.Count != incoming.Count ||
                    existing.DxilRegister != incoming.DxilRegister ||
                    existing.DxilSpace != incoming.DxilSpace ||
                    existing.PreferRootDescriptorOnDx12 != incoming.PreferRootDescriptorOnDx12)
                {
                    outError = "Conflicting cross-stage resource binding at (set=" +
                        std::to_string(incoming.Set) + ", binding=" + std::to_string(incoming.Binding) +
                        ") between '" + existing.Name + "' and '" + incoming.Name + "'.";
                    return false;
                }

                existing.Visibility |= incoming.Visibility;
                return true;
            }

            setDesc.Bindings.push_back(incoming);
            return true;
        }

        bool MergePushConstant(
            std::vector<RHI::PipelinePushConstantRange>& pushRanges,
            const RHI::PipelinePushConstantRange& incoming,
            std::string& outError)
        {
            for (auto& existing : pushRanges)
            {
                if (existing.OffsetBytes == incoming.OffsetBytes &&
                    existing.SizeBytes == incoming.SizeBytes)
                {
                    if (existing.DxilRegister != incoming.DxilRegister ||
                        existing.DxilSpace != incoming.DxilSpace)
                    {
                        outError = "Push constant range '" + incoming.Name +
                            "' conflicts in DXIL register/space across stages.";
                        return false;
                    }
                    existing.Visibility |= incoming.Visibility;
                    return true;
                }

                const u64 exEnd = static_cast<u64>(existing.OffsetBytes) + static_cast<u64>(existing.SizeBytes);
                const u64 inEnd = static_cast<u64>(incoming.OffsetBytes) + static_cast<u64>(incoming.SizeBytes);
                if (incoming.OffsetBytes < exEnd && existing.OffsetBytes < inEnd)
                {
                    if (existing.OffsetBytes == 0u && incoming.OffsetBytes == 0u &&
                        existing.DxilRegister == incoming.DxilRegister &&
                        existing.DxilSpace == incoming.DxilSpace)
                    {
                        // Combined VS/PS view of the same root/push constant buffer with different trimmed tails
                        existing.SizeBytes = std::max(existing.SizeBytes, incoming.SizeBytes);
                        existing.Visibility |= incoming.Visibility;
                        return true;
                    }

                    outError = "Overlapping push constant ranges across stages: '" +
                        existing.Name + "' and '" + incoming.Name + "'.";
                    return false;
                }
            }

            pushRanges.push_back(incoming);
            return true;
        }

        bool MergeConstantBuffer(
            std::vector<RHI::PipelineConstantBufferLayout>& cbList,
            const RHI::PipelineConstantBufferLayout& incoming,
            std::string& outError)
        {
            for (auto& existing : cbList)
            {
                if (existing.Set == incoming.Set && existing.Binding == incoming.Binding)
                {
                    if (existing.SizeBytes != incoming.SizeBytes ||
                        existing.MemberCount != incoming.MemberCount ||
                        existing.DxilRegister != incoming.DxilRegister ||
                        existing.DxilSpace != incoming.DxilSpace)
                    {
                        outError = "ConstantBuffer layout conflict across stages at (set=" +
                            std::to_string(incoming.Set) + ", binding=" + std::to_string(incoming.Binding) +
                            ") for '" + incoming.Name + "'.";
                        return false;
                    }
                    existing.Visibility |= incoming.Visibility;
                    return true;
                }
            }

            cbList.push_back(incoming);
            return true;
        }
    }

    Core::Result<RHI::PipelineLayoutDesc, std::string> PipelineLayoutBuilder::BuildFromReflection(
        std::span<const ShaderReflectionData> stages,
        const PipelineLayoutBuildOptions& options)
    {
        std::string compError;
        if (!ValidateStageComposition(stages, options.ValidateStageInterfaces, compError))
        {
            return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(compError));
        }

        RHI::PipelineLayoutDesc desc{};
        desc.Mode = options.Mode;
        desc.DebugName = std::string(options.DebugName);

        std::unordered_map<u32, RHI::DescriptorSetLayoutDesc> setMap;

        for (const auto& stageData : stages)
        {
            const RHI::ShaderVisibility stageVis = ToRhiVisibility(stageData.Stage);

            // Push constants explicitly present in SPIR-V reflection
            for (const auto& spvPush : stageData.PushConstants)
            {
                RHI::PipelinePushConstantRange range{};
                range.Name = spvPush.Name.empty() ? std::string("PushConstants") : spvPush.Name;
                range.OffsetBytes = spvPush.OffsetBytes;
                range.SizeBytes = spvPush.SizeBytes;
                range.Visibility = ConvertVisibility(spvPush.Visibility, stageData.Stage);
                if (options.Policy.RootConstantRule.Enabled)
                {
                    range.DxilRegister = options.Policy.RootConstantRule.DxilRegister;
                    range.DxilSpace = options.Policy.RootConstantRule.DxilSpace;
                }
                std::string mergeErr;
                if (!MergePushConstant(desc.PushConstants, range, mergeErr))
                {
                    return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(mergeErr));
                }
            }

            for (const auto& b : stageData.Bindings)
            {
                const auto descTypeOpt = ConvertDescriptorType(b);
                if (!descTypeOpt.has_value())
                {
                    return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                        "Unsupported reflected resource kind for binding '" + b.Name + "'.");
                }
                const RHI::DescriptorType descType = *descTypeOpt;

                // Handle DXIL Root Constant mapped explicitly via CrossBackendRootConstantRule
                if (stageData.TargetFormat == ShaderTargetFormat::DXIL &&
                    b.Type == ResourceBindingType::ConstantBuffer &&
                    options.Policy.RootConstantRule.Enabled &&
                    b.DxilSpace == options.Policy.RootConstantRule.DxilSpace &&
                    b.DxilRegister == options.Policy.RootConstantRule.DxilRegister)
                {
                    u32 cbSize = 0u;
                    if (b.ConstantBufferIndex.has_value() &&
                        *b.ConstantBufferIndex < stageData.ConstantBuffers.size())
                    {
                        cbSize = stageData.ConstantBuffers[*b.ConstantBufferIndex].SizeBytes;
                    }
                    RHI::PipelinePushConstantRange rootRange{};
                    rootRange.Name = b.Name.empty() ? std::string("RootConstants") : b.Name;
                    rootRange.OffsetBytes = 0u;
                    rootRange.SizeBytes = cbSize;
                    rootRange.Visibility = ConvertVisibility(b.Visibility, stageData.Stage);
                    rootRange.DxilRegister = b.DxilRegister;
                    rootRange.DxilSpace = b.DxilSpace;

                    std::string mergeErr;
                    if (!MergePushConstant(desc.PushConstants, rootRange, mergeErr))
                    {
                        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(mergeErr));
                    }
                    continue;
                }

                RHI::PipelineBindingLayout outB{};
                outB.Name = b.Name;
                outB.Type = descType;
                outB.Count = b.IsUnboundedArray ? RHI::kUnboundedDescriptorCount : b.Count;
                outB.IsUnboundedArray = b.IsUnboundedArray;
                outB.IsReadOnly = b.IsReadOnly;
                outB.IsComparisonSampler = b.IsComparisonSampler;
                outB.Visibility = ConvertVisibility(b.Visibility, stageData.Stage);

                if (stageData.TargetFormat == ShaderTargetFormat::SPIRV)
                {
                    if (!b.DescriptorSet.has_value() || !b.Binding.has_value())
                    {
                        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                            "SPIR-V binding '" + b.Name + "' has no DescriptorSet/Binding metadata.");
                    }
                    outB.Set = *b.DescriptorSet;
                    outB.Binding = *b.Binding;

                    const auto* rule = FindRuleForSpirv(options.Policy, b.Type, outB.Set, outB.Binding);
                    if (rule != nullptr)
                    {
                        if (rule->DxilRegisterClass != b.Type)
                        {
                            return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                                "Explicit CrossBackendBindingRule class mismatch for SPIR-V binding '" + b.Name + "'.");
                        }
                        outB.DxilRegister = rule->DxilRegister;
                        outB.DxilSpace = rule->DxilSpace;
                        outB.PreferRootDescriptorOnDx12 = rule->AllowUnboundedArrayToSingleCbv;
                    }
                    else if (options.Policy.FallbackToIdentitySetSpaceMapping)
                    {
                        outB.DxilRegister = outB.Binding;
                        outB.DxilSpace = outB.Set;
                    }
                    else if (options.TargetBackend == RHI::BackendAPI::DirectX12 ||
                             options.TargetBackend == RHI::BackendAPI::Null)
                    {
                        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                            "No explicit CrossBackendBindingRule maps SPIR-V binding '" + b.Name +
                            "' (set=" + std::to_string(outB.Set) + ", binding=" + std::to_string(outB.Binding) +
                            ") to DX12 register/space with FallbackToIdentitySetSpaceMapping=false.");
                    }
                }
                else // DXIL
                {
                    if (!b.DxilRegister.has_value() || !b.DxilSpace.has_value())
                    {
                        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                            "DXIL binding '" + b.Name + "' has no DxilRegister/DxilSpace metadata.");
                    }
                    outB.DxilRegister = b.DxilRegister;
                    outB.DxilSpace = b.DxilSpace;

                    const auto* rule = FindRuleForDxil(options.Policy, b.Type, *b.DxilSpace, *b.DxilRegister);
                    if (rule != nullptr)
                    {
                        outB.Set = rule->VulkanSet;
                        outB.Binding = rule->VulkanBinding;
                        outB.PreferRootDescriptorOnDx12 = rule->AllowUnboundedArrayToSingleCbv;
                    }
                    else if (options.Policy.FallbackToIdentitySetSpaceMapping)
                    {
                        outB.Set = *b.DxilSpace;
                        outB.Binding = *b.DxilRegister;
                    }
                    else if (options.TargetBackend == RHI::BackendAPI::Vulkan ||
                             options.TargetBackend == RHI::BackendAPI::Null)
                    {
                        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                            "No explicit CrossBackendBindingRule maps DXIL binding '" + b.Name +
                            "' (space=" + std::to_string(*b.DxilSpace) + ", reg=" + std::to_string(*b.DxilRegister) +
                            ") to Vulkan set/binding with FallbackToIdentitySetSpaceMapping=false.");
                    }
                }

                if (outB.Type == RHI::DescriptorType::UniformBuffer &&
                    !outB.IsUnboundedArray && outB.Count == 1u &&
                    options.UseRootDescriptorForSingleDynamicCbv)
                {
                    outB.PreferRootDescriptorOnDx12 = true;
                }

                if (options.Mode == RHI::PipelineLayoutMode::GlobalBindlessHybrid)
                {
                    if (outB.Set == 0u)
                    {
                        if (!IsMorbiGlobalBindlessSet0Slot(outB.Set, outB.Binding, outB.Type))
                        {
                            return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                                "GlobalBindlessHybrid reserves Set 0 for Morbi bindless slots 0..3; binding '" +
                                outB.Name + "' at (set=0, binding=" + std::to_string(outB.Binding) +
                                ") is incompatible.");
                        }
                        // Track CBuffer metadata (e.g., PerObjectBuffer b1, space0) while omitting Set 0
                        // from custom Sets so PipelineLayoutValidator recognizes reserved Set 0 cleanly.
                        if (b.ConstantBufferIndex.has_value() &&
                            *b.ConstantBufferIndex < stageData.ConstantBuffers.size())
                        {
                            const auto& cb = stageData.ConstantBuffers[*b.ConstantBufferIndex];
                            RHI::PipelineConstantBufferLayout cbMeta{};
                            cbMeta.Name = cb.Name.empty() ? outB.Name : cb.Name;
                            cbMeta.Set = outB.Set;
                            cbMeta.Binding = outB.Binding;
                            cbMeta.DxilRegister = outB.DxilRegister;
                            cbMeta.DxilSpace = outB.DxilSpace;
                            cbMeta.SizeBytes = cb.SizeBytes;
                            cbMeta.MemberCount = static_cast<u32>(cb.Members.size());
                            cbMeta.UseRootDescriptor = outB.PreferRootDescriptorOnDx12;
                            cbMeta.Visibility = stageVis;

                            std::string cbErr;
                            if (!MergeConstantBuffer(desc.ConstantBuffers, cbMeta, cbErr))
                            {
                                return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(cbErr));
                            }
                        }
                        continue;
                    }
                }

                if (b.ConstantBufferIndex.has_value() &&
                    *b.ConstantBufferIndex < stageData.ConstantBuffers.size())
                {
                    const auto& cb = stageData.ConstantBuffers[*b.ConstantBufferIndex];
                    RHI::PipelineConstantBufferLayout cbMeta{};
                    cbMeta.Name = cb.Name.empty() ? outB.Name : cb.Name;
                    cbMeta.Set = outB.Set;
                    cbMeta.Binding = outB.Binding;
                    cbMeta.DxilRegister = outB.DxilRegister;
                    cbMeta.DxilSpace = outB.DxilSpace;
                    cbMeta.SizeBytes = cb.SizeBytes;
                    cbMeta.MemberCount = static_cast<u32>(cb.Members.size());
                    cbMeta.UseRootDescriptor = outB.PreferRootDescriptorOnDx12;
                    cbMeta.Visibility = stageVis;

                    std::string cbErr;
                    if (!MergeConstantBuffer(desc.ConstantBuffers, cbMeta, cbErr))
                    {
                        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(cbErr));
                    }
                }

                std::string mergeErr;
                if (!MergeBindingIntoSet(setMap, outB, outB.IsUnboundedArray, mergeErr))
                {
                    return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(mergeErr));
                }
            }
        }

        desc.Sets.reserve(setMap.size());
        for (auto& [setIdx, setDesc] : setMap)
        {
            (void)setIdx;
            desc.Sets.push_back(std::move(setDesc));
        }

        RHI::PipelineLayoutDesc normalized = RHI::PipelineLayoutValidator::Normalize(desc);
        const auto val = RHI::PipelineLayoutValidator::Validate(
            normalized, options.Limits, options.TargetBackend);
        if (!val.IsValid)
        {
            return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                "PipelineLayout validation failed: " + FormatIssues(val));
        }

        return Core::Result<RHI::PipelineLayoutDesc, std::string>::Success(std::move(normalized));
    }

    Core::Result<RHI::PipelineLayoutDesc, std::string> PipelineLayoutBuilder::BuildFromDualBackendReflections(
        std::span<const ShaderReflectionData> spirvStages,
        std::span<const ShaderReflectionData> dxilStages,
        const PipelineLayoutBuildOptions& options)
    {
        if (spirvStages.empty() || dxilStages.empty())
        {
            return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                "BuildFromDualBackendReflections failed: empty SPIR-V or DXIL stage span.");
        }
        if (spirvStages.size() != dxilStages.size())
        {
            return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                "BuildFromDualBackendReflections failed: stage count mismatch between SPIR-V and DXIL.");
        }

        for (const auto& spv : spirvStages)
        {
            const ShaderReflectionData* matchingDxil = nullptr;
            for (const auto& dxil : dxilStages)
            {
                if (dxil.Stage == spv.Stage)
                {
                    matchingDxil = &dxil;
                    break;
                }
            }
            if (matchingDxil == nullptr)
            {
                return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                    "BuildFromDualBackendReflections failed: no matching DXIL stage for SPIR-V stage.");
            }

            const auto compat = ShaderReflectionValidator::ValidateCrossBackendCompatibility(
                spv, *matchingDxil, options.Policy);
            if (!compat.IsCompatible)
            {
                return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(
                    "BuildFromDualBackendReflections failed (cross-backend incompatibility): " +
                    FormatReflectionIssues(compat));
            }
        }

        if (options.ValidateStageInterfaces)
        {
            std::string dxilErr;
            if (!ValidateStageComposition(dxilStages, true, dxilErr))
            {
                return Core::Result<RHI::PipelineLayoutDesc, std::string>::Failure(std::move(dxilErr));
            }
        }

        return BuildFromReflection(spirvStages, options);
    }
}
