#include "Morbi/RHI/PipelineLayout.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>

namespace Morbi::RHI
{
    namespace
    {
        constexpr u64 kFnvOffsetBasis = 14695981039346656037ULL;
        constexpr u64 kFnvPrime = 1099511628211ULL;

        void HashBytes(u64& seed, const void* data, usize size) noexcept
        {
            const auto* bytes = static_cast<const u8*>(data);
            for (usize i = 0; i < size; ++i)
            {
                seed ^= bytes[i];
                seed *= kFnvPrime;
            }
        }

        template <typename T>
        void HashPod(u64& seed, const T& val) noexcept
        {
            HashBytes(seed, &val, sizeof(T));
        }

        void HashString(u64& seed, const std::string& str) noexcept
        {
            const u64 len = static_cast<u64>(str.size());
            HashPod(seed, len);
            if (!str.empty())
            {
                HashBytes(seed, str.data(), str.size());
            }
        }

        void HashOptionalU32(u64& seed, const std::optional<u32>& opt) noexcept
        {
            const u8 has = opt.has_value() ? 1u : 0u;
            HashPod(seed, has);
            if (opt.has_value())
            {
                HashPod(seed, *opt);
            }
        }

        enum class DxilRegNamespace : u8
        {
            CBV = 0, // b#
            SRV,     // t#
            UAV,     // u#
            Sampler  // s#
        };

        [[nodiscard]] DxilRegNamespace GetDxilNamespace(DescriptorType type) noexcept
        {
            switch (type)
            {
            case DescriptorType::UniformBuffer:
                return DxilRegNamespace::CBV;
            case DescriptorType::RWStorageBuffer:
            case DescriptorType::StorageTexture:
                return DxilRegNamespace::UAV;
            case DescriptorType::Sampler:
                return DxilRegNamespace::Sampler;
            case DescriptorType::StorageBuffer:
            case DescriptorType::SampledTexture:
            case DescriptorType::CombinedImageSampler:
            case DescriptorType::AccelerationStructure:
            default:
                return DxilRegNamespace::SRV;
            }
        }

        [[nodiscard]] u64 MakeDxilKey(DxilRegNamespace ns, u32 space, u32 reg) noexcept
        {
            return (static_cast<u64>(ns) << 56) |
                   (static_cast<u64>(space & 0xFFFFu) << 32) |
                   static_cast<u64>(reg);
        }
    }

    PipelineLayoutDesc PipelineLayoutValidator::Normalize(const PipelineLayoutDesc& desc)
    {
        PipelineLayoutDesc norm = desc;

        for (auto& set : norm.Sets)
        {
            for (auto& b : set.Bindings)
            {
                b.Set = set.SetIndex;
                if (b.IsUnboundedArray)
                {
                    b.Count = kUnboundedDescriptorCount;
                }
            }
            std::sort(set.Bindings.begin(), set.Bindings.end(),
                [](const PipelineBindingLayout& a, const PipelineBindingLayout& b)
                {
                    if (a.Binding != b.Binding) return a.Binding < b.Binding;
                    if (a.Type != b.Type) return static_cast<u8>(a.Type) < static_cast<u8>(b.Type);
                    const u32 spA = a.DxilSpace.value_or(a.Set);
                    const u32 spB = b.DxilSpace.value_or(b.Set);
                    if (spA != spB) return spA < spB;
                    const u32 regA = a.DxilRegister.value_or(a.Binding);
                    const u32 regB = b.DxilRegister.value_or(b.Binding);
                    if (regA != regB) return regA < regB;
                    return a.Name < b.Name;
                });
        }

        std::sort(norm.Sets.begin(), norm.Sets.end(),
            [](const DescriptorSetLayoutDesc& a, const DescriptorSetLayoutDesc& b)
            {
                return a.SetIndex < b.SetIndex;
            });

        std::sort(norm.PushConstants.begin(), norm.PushConstants.end(),
            [](const PipelinePushConstantRange& a, const PipelinePushConstantRange& b)
            {
                if (a.OffsetBytes != b.OffsetBytes) return a.OffsetBytes < b.OffsetBytes;
                if (a.SizeBytes != b.SizeBytes) return a.SizeBytes < b.SizeBytes;
                return a.Name < b.Name;
            });

        std::sort(norm.ConstantBuffers.begin(), norm.ConstantBuffers.end(),
            [](const PipelineConstantBufferLayout& a, const PipelineConstantBufferLayout& b)
            {
                if (a.Set != b.Set) return a.Set < b.Set;
                if (a.Binding != b.Binding) return a.Binding < b.Binding;
                return a.Name < b.Name;
            });

        std::sort(norm.StaticSamplers.begin(), norm.StaticSamplers.end(),
            [](const StaticSamplerDesc& a, const StaticSamplerDesc& b)
            {
                if (a.Set != b.Set) return a.Set < b.Set;
                if (a.Binding != b.Binding) return a.Binding < b.Binding;
                return a.Name < b.Name;
            });

        return norm;
    }

    u64 PipelineLayoutValidator::ComputeCanonicalHash(const PipelineLayoutDesc& desc) noexcept
    {
        const PipelineLayoutDesc norm = Normalize(desc);
        u64 hash = kFnvOffsetBasis;

        HashPod(hash, norm.Mode);

        const u64 setCount = static_cast<u64>(norm.Sets.size());
        HashPod(hash, setCount);
        for (const auto& set : norm.Sets)
        {
            HashPod(hash, set.SetIndex);
            const u8 upd = set.AllowUpdateAfterBind ? 1u : 0u;
            HashPod(hash, upd);

            const u64 bindCount = static_cast<u64>(set.Bindings.size());
            HashPod(hash, bindCount);
            for (const auto& b : set.Bindings)
            {
                HashString(hash, b.Name);
                HashPod(hash, b.Type);
                HashPod(hash, b.Set);
                HashPod(hash, b.Binding);
                HashOptionalU32(hash, b.DxilRegister);
                HashOptionalU32(hash, b.DxilSpace);
                HashPod(hash, b.Count);
                const u8 flags = static_cast<u8>(
                    (b.IsUnboundedArray ? 1u : 0u) |
                    (b.IsReadOnly ? 2u : 0u) |
                    (b.IsComparisonSampler ? 4u : 0u) |
                    (b.PreferRootDescriptorOnDx12 ? 8u : 0u));
                HashPod(hash, flags);
                HashPod(hash, b.Visibility);
            }
        }

        const u64 pushCount = static_cast<u64>(norm.PushConstants.size());
        HashPod(hash, pushCount);
        for (const auto& pc : norm.PushConstants)
        {
            HashString(hash, pc.Name);
            HashPod(hash, pc.OffsetBytes);
            HashPod(hash, pc.SizeBytes);
            HashPod(hash, pc.Visibility);
            HashOptionalU32(hash, pc.DxilRegister);
            HashOptionalU32(hash, pc.DxilSpace);
        }

        const u64 cbCount = static_cast<u64>(norm.ConstantBuffers.size());
        HashPod(hash, cbCount);
        for (const auto& cb : norm.ConstantBuffers)
        {
            HashString(hash, cb.Name);
            HashPod(hash, cb.Set);
            HashPod(hash, cb.Binding);
            HashOptionalU32(hash, cb.DxilRegister);
            HashOptionalU32(hash, cb.DxilSpace);
            HashPod(hash, cb.SizeBytes);
            HashPod(hash, cb.MemberCount);
            const u8 useRoot = cb.UseRootDescriptor ? 1u : 0u;
            HashPod(hash, useRoot);
            HashPod(hash, cb.Visibility);
        }

        const u64 sampCount = static_cast<u64>(norm.StaticSamplers.size());
        HashPod(hash, sampCount);
        for (const auto& s : norm.StaticSamplers)
        {
            HashString(hash, s.Name);
            HashPod(hash, s.Set);
            HashPod(hash, s.Binding);
            HashOptionalU32(hash, s.DxilRegister);
            HashOptionalU32(hash, s.DxilSpace);
            HashPod(hash, s.Filter);
            HashPod(hash, s.AddressMode);
            const u8 cmp = s.IsComparison ? 1u : 0u;
            HashPod(hash, cmp);
            HashPod(hash, s.Visibility);
        }

        return hash;
    }

    bool PipelineLayoutValidator::AreStructurallyEqual(
        const PipelineLayoutDesc& a,
        const PipelineLayoutDesc& b) noexcept
    {
        const PipelineLayoutDesc normA = Normalize(a);
        const PipelineLayoutDesc normB = Normalize(b);
        return normA.Mode == normB.Mode &&
               normA.Sets == normB.Sets &&
               normA.PushConstants == normB.PushConstants &&
               normA.ConstantBuffers == normB.ConstantBuffers &&
               normA.StaticSamplers == normB.StaticSamplers;
    }

    PipelineLayoutValidationResult PipelineLayoutValidator::Validate(
        const PipelineLayoutDesc& desc,
        const PipelineLayoutLimits& limits,
        BackendAPI targetBackend)
    {
        PipelineLayoutValidationResult result{};
        const PipelineLayoutDesc norm = Normalize(desc);

        const bool checkVulkan = (targetBackend == BackendAPI::Null || targetBackend == BackendAPI::Vulkan);
        const bool checkDx12   = (targetBackend == BackendAPI::Null || targetBackend == BackendAPI::DirectX12);

        if (norm.Mode == PipelineLayoutMode::GlobalBindlessHybrid)
        {
            // Base GlobalBindless Root Signature uses 35 DWORDs:
            //   Slot 0: 32 DWORD Root Constants (b0, space0)
            //   Slot 1:  1 DWORD SRV Descriptor Table (t0, space0 + t0, space1)
            //   Slot 2:  2 DWORD Root CBV (b1, space0)
            //   Static Sampler: 0 DWORDs (s0, space0)
            result.Cost.EstimatedDx12Dwords += 35u;
            result.Cost.UsesBindlessRuntimeArray = true;
            result.Cost.UsesUpdateAfterBind = true;
        }

        std::unordered_set<u64> usedDxilKeys;
        if (norm.Mode == PipelineLayoutMode::GlobalBindlessHybrid)
        {
            usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::CBV, 0u, 0u));
            usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::CBV, 0u, 1u));
            usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::SRV, 0u, 0u));
            usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::SRV, 1u, 0u));
            usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::Sampler, 0u, 0u));
        }

        // 1. Validate Push Constants
        u64 maxPushEnd = 0u;
        for (usize i = 0; i < norm.PushConstants.size(); ++i)
        {
            const auto& pc = norm.PushConstants[i];
            if (pc.SizeBytes == 0u)
            {
                result.AddError("LAYOUT_ZERO_PUSH_CONSTANT_SIZE",
                    "Push constant range '" + pc.Name + "' has zero SizeBytes.");
                continue;
            }
            if ((pc.OffsetBytes % 4u) != 0u || (pc.SizeBytes % 4u) != 0u)
            {
                result.AddError("LAYOUT_UNALIGNED_PUSH_CONSTANT",
                    "Push constant range '" + pc.Name + "' offset/size must be 4-byte aligned.");
            }
            if (pc.Visibility == ShaderVisibility::None)
            {
                result.AddError("LAYOUT_EMPTY_PUSH_CONSTANT_VISIBILITY",
                    "Push constant range '" + pc.Name + "' has ShaderVisibility::None.");
            }
            if (checkDx12 && pc.OffsetBytes != 0u)
            {
                result.AddError("LAYOUT_DX12_NONZERO_PUSH_CONSTANT_OFFSET",
                    "Push constant range '" + pc.Name + "' starts at non-zero byte offset (" +
                    std::to_string(pc.OffsetBytes) + "B), which DX12 32-bit root constants cannot offset cleanly.");
            }

            const u64 endBytes = static_cast<u64>(pc.OffsetBytes) + static_cast<u64>(pc.SizeBytes);
            maxPushEnd = std::max(maxPushEnd, endBytes);

            if (i + 1u < norm.PushConstants.size())
            {
                const auto& next = norm.PushConstants[i + 1u];
                if (endBytes > next.OffsetBytes)
                {
                    result.AddError("LAYOUT_OVERLAPPING_PUSH_CONSTANT",
                        "Push constant ranges '" + pc.Name + "' and '" + next.Name + "' overlap in byte range.");
                }
            }

            if (norm.Mode == PipelineLayoutMode::Standalone)
            {
                const u32 dwords = (pc.SizeBytes + 3u) / 4u;
                result.Cost.EstimatedDx12Dwords += dwords;
                const u32 dxilReg = pc.DxilRegister.value_or(0u);
                const u32 dxilSpace = pc.DxilSpace.value_or(0u);
                if (!usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::CBV, dxilSpace, dxilReg)).second)
                {
                    result.AddError("LAYOUT_DUPLICATE_DXIL_REGISTER",
                        "Duplicate DXIL CBV register (b" + std::to_string(dxilReg) +
                        ", space" + std::to_string(dxilSpace) + ") for push constant '" + pc.Name + "'.");
                }
            }
        }

        result.Cost.TotalPushConstantBytes = static_cast<u32>(std::min<u64>(maxPushEnd, UINT32_MAX));
        const u32 effectiveMaxPush = std::min(limits.MaxPushConstantsBytes, kMaxPortablePushConstantsBytes);
        if (maxPushEnd > effectiveMaxPush)
        {
            result.AddError("LAYOUT_PUSH_CONSTANT_EXCEEDS_LIMIT",
                "Total push constant size (" + std::to_string(maxPushEnd) +
                "B) exceeds hardware/portable limit (" + std::to_string(effectiveMaxPush) + "B).");
        }

        // 2. Validate Static Samplers & collect Vulkan/DX12 set+binding occupancy
        std::unordered_set<u64> usedSetBindings;
        auto makeSetBindKey = [](u32 setIdx, u32 bindIdx) noexcept -> u64 {
            return (static_cast<u64>(setIdx) << 32) | static_cast<u64>(bindIdx);
        };

        u32 maxSetIndexPlusOne = (norm.Mode == PipelineLayoutMode::GlobalBindlessHybrid) ? 1u : 0u;

        struct StageCounters
        {
            u32 Samplers = 0;
            u32 UniformBuffers = 0;
            u32 StorageBuffers = 0;
            u32 SampledImages = 0;
            u32 StorageImages = 0;
            u32 Resources = 0;
        };
        std::array<StageCounters, 3> perStage{}; // 0=VS, 1=PS, 2=CS

        auto addStageCount = [&](ShaderVisibility vis, DescriptorType type, u32 count, bool isUpdateAfterBind) {
            if (isUpdateAfterBind)
            {
                return;
            }
            const std::array<ShaderVisibility, 3> stageBits = {
                ShaderVisibility::Vertex, ShaderVisibility::Pixel, ShaderVisibility::Compute
            };
            for (usize s = 0; s < 3; ++s)
            {
                if (!HasVisibility(vis, stageBits[s])) continue;
                auto& c = perStage[s];
                c.Resources += count;
                switch (type)
                {
                case DescriptorType::Sampler:              c.Samplers += count; break;
                case DescriptorType::UniformBuffer:        c.UniformBuffers += count; break;
                case DescriptorType::StorageBuffer:
                case DescriptorType::RWStorageBuffer:      c.StorageBuffers += count; break;
                case DescriptorType::SampledTexture:
                case DescriptorType::CombinedImageSampler: c.SampledImages += count; break;
                case DescriptorType::StorageTexture:       c.StorageImages += count; break;
                default: break;
                }
            }
        };

        for (const auto& samp : norm.StaticSamplers)
        {
            if (norm.Mode == PipelineLayoutMode::GlobalBindlessHybrid && samp.Set == 0u)
            {
                result.AddError("LAYOUT_RESERVED_BINDLESS_SET_ZERO",
                    "Static sampler '" + samp.Name + "' cannot target reserved GlobalBindless Set 0.");
            }
            if (!usedSetBindings.insert(makeSetBindKey(samp.Set, samp.Binding)).second)
            {
                result.AddError("LAYOUT_DUPLICATE_BINDING",
                    "Duplicate static sampler at (set=" + std::to_string(samp.Set) +
                    ", binding=" + std::to_string(samp.Binding) + ").");
            }
            const u32 dxilReg = samp.DxilRegister.value_or(samp.Binding);
            const u32 dxilSpace = samp.DxilSpace.value_or(samp.Set);
            if (!usedDxilKeys.insert(MakeDxilKey(DxilRegNamespace::Sampler, dxilSpace, dxilReg)).second)
            {
                result.AddError("LAYOUT_DUPLICATE_DXIL_REGISTER",
                    "Duplicate DXIL sampler register (s" + std::to_string(dxilReg) +
                    ", space" + std::to_string(dxilSpace) + ").");
            }
            maxSetIndexPlusOne = std::max(maxSetIndexPlusOne, samp.Set + 1u);
            addStageCount(samp.Visibility, DescriptorType::Sampler, 1u, false);
            ++result.Cost.TotalStaticSamplers;
        }

        // 3. Validate Descriptor Sets & Bindings
        std::unordered_set<u32> seenSetIndices;
        u32 uabUniformBuffers = 0u;
        u32 uabSampledImages = 0u;
        u32 uabStorageBuffers = 0u;

        for (const auto& set : norm.Sets)
        {
            if (!seenSetIndices.insert(set.SetIndex).second)
            {
                result.AddError("LAYOUT_DUPLICATE_SET_INDEX",
                    "Duplicate DescriptorSetLayoutDesc for set " + std::to_string(set.SetIndex) + ".");
                continue;
            }
            if (norm.Mode == PipelineLayoutMode::GlobalBindlessHybrid && set.SetIndex == 0u)
            {
                result.AddError("LAYOUT_RESERVED_BINDLESS_SET_ZERO",
                    "Set 0 is reserved for GlobalBindless Set in GlobalBindlessHybrid mode; custom sets must use SetIndex >= 1.");
            }

            maxSetIndexPlusOne = std::max(maxSetIndexPlusOne, set.SetIndex + 1u);

            if (checkVulkan && set.AllowUpdateAfterBind)
            {
                result.Cost.UsesUpdateAfterBind = true;
                if (!limits.SupportsUpdateAfterBind)
                {
                    result.AddError("LAYOUT_UNSUPPORTED_UPDATE_AFTER_BIND",
                        "Set " + std::to_string(set.SetIndex) +
                        " requests UpdateAfterBind, which is not enabled on the Vulkan logical device.");
                }
            }

            bool hasCbvSrvUavTable = false;
            bool hasSamplerTable = false;

            for (const auto& b : set.Bindings)
            {
                ++result.Cost.TotalBindings;
                if (!usedSetBindings.insert(makeSetBindKey(set.SetIndex, b.Binding)).second)
                {
                    result.AddError("LAYOUT_DUPLICATE_BINDING",
                        "Duplicate binding at (set=" + std::to_string(set.SetIndex) +
                        ", binding=" + std::to_string(b.Binding) + ") for '" + b.Name + "'.");
                }

                if (b.Count == 0u && !b.IsUnboundedArray)
                {
                    result.AddError("LAYOUT_ZERO_DESCRIPTOR_COUNT",
                        "Binding '" + b.Name + "' has descriptor Count=0 without IsUnboundedArray=true.");
                }

                if (b.Visibility == ShaderVisibility::None)
                {
                    result.AddError("LAYOUT_EMPTY_BINDING_VISIBILITY",
                        "Binding '" + b.Name + "' has ShaderVisibility::None.");
                }

                if (b.IsUnboundedArray)
                {
                    result.Cost.UsesBindlessRuntimeArray = true;
                    if (checkVulkan)
                    {
                        if (!limits.SupportsDescriptorIndexing)
                        {
                            result.AddError("LAYOUT_UNSUPPORTED_DESCRIPTOR_INDEXING",
                                "Binding '" + b.Name + "' uses unbounded array, but DescriptorIndexing is not enabled.");
                        }
                        if (!limits.SupportsRuntimeDescriptorArray)
                        {
                            result.AddError("LAYOUT_UNSUPPORTED_RUNTIME_ARRAY",
                                "Binding '" + b.Name + "' uses unbounded array, but RuntimeDescriptorArray is not enabled.");
                        }
                    }
                    if (b.PreferRootDescriptorOnDx12)
                    {
                        result.AddError("LAYOUT_INVALID_ROOT_UNBOUNDED",
                            "Unbounded descriptor array '" + b.Name + "' cannot use PreferRootDescriptorOnDx12.");
                    }
                }

                if (b.Type == DescriptorType::CombinedImageSampler && checkDx12)
                {
                    result.AddError("LAYOUT_UNSUPPORTED_COMBINED_IMAGE_SAMPLER",
                        "Binding '" + b.Name + "' uses CombinedImageSampler which is unsupported on DX12; split into SampledTexture + Sampler.");
                }

                const u32 dxilReg = b.DxilRegister.value_or(b.Binding);
                const u32 dxilSpace = b.DxilSpace.value_or(set.SetIndex);
                const DxilRegNamespace dxilNs = GetDxilNamespace(b.Type);
                if (!usedDxilKeys.insert(MakeDxilKey(dxilNs, dxilSpace, dxilReg)).second)
                {
                    result.AddError("LAYOUT_DUPLICATE_DXIL_REGISTER",
                        "Duplicate DXIL register conflict for binding '" + b.Name +
                        "' at (space=" + std::to_string(dxilSpace) + ", reg=" + std::to_string(dxilReg) + ").");
                }

                if (b.PreferRootDescriptorOnDx12 && !b.IsUnboundedArray && b.Count == 1u)
                {
                    if (b.Type != DescriptorType::UniformBuffer &&
                        b.Type != DescriptorType::StorageBuffer &&
                        b.Type != DescriptorType::RWStorageBuffer)
                    {
                        result.AddError("LAYOUT_INVALID_ROOT_DESCRIPTOR_TYPE",
                            "Binding '" + b.Name + "' sets PreferRootDescriptorOnDx12 on non-buffer resource type.");
                    }
                    result.Cost.EstimatedDx12Dwords += 2u;
                }
                else if (b.Type == DescriptorType::Sampler)
                {
                    hasSamplerTable = true;
                }
                else
                {
                    hasCbvSrvUavTable = true;
                }

                const u32 effectiveCount = b.IsUnboundedArray ? 1u : b.Count;
                if (set.AllowUpdateAfterBind && !b.IsUnboundedArray)
                {
                    switch (b.Type)
                    {
                    case DescriptorType::UniformBuffer:   uabUniformBuffers += effectiveCount; break;
                    case DescriptorType::SampledTexture:  uabSampledImages += effectiveCount; break;
                    case DescriptorType::StorageBuffer:
                    case DescriptorType::RWStorageBuffer: uabStorageBuffers += effectiveCount; break;
                    default: break;
                    }
                }
                addStageCount(b.Visibility, b.Type, effectiveCount, set.AllowUpdateAfterBind);
            }

            if (hasCbvSrvUavTable) result.Cost.EstimatedDx12Dwords += 1u;
            if (hasSamplerTable)   result.Cost.EstimatedDx12Dwords += 1u;
        }

        result.Cost.TotalDescriptorSets = maxSetIndexPlusOne;

        for (const auto& c : perStage)
        {
            result.Cost.MaxStageSamplers = std::max(result.Cost.MaxStageSamplers, c.Samplers);
            result.Cost.MaxStageUniformBuffers = std::max(result.Cost.MaxStageUniformBuffers, c.UniformBuffers);
            result.Cost.MaxStageStorageBuffers = std::max(result.Cost.MaxStageStorageBuffers, c.StorageBuffers);
            result.Cost.MaxStageSampledImages = std::max(result.Cost.MaxStageSampledImages, c.SampledImages);
            result.Cost.MaxStageStorageImages = std::max(result.Cost.MaxStageStorageImages, c.StorageImages);
            result.Cost.MaxStageResources = std::max(result.Cost.MaxStageResources, c.Resources);
        }

        if (checkVulkan)
        {
            if (result.Cost.TotalDescriptorSets > limits.MaxBoundDescriptorSets)
            {
                result.AddError("LAYOUT_EXCEEDS_MAX_DESCRIPTOR_SETS",
                    "Descriptor set count (" + std::to_string(result.Cost.TotalDescriptorSets) +
                    ") exceeds device MaxBoundDescriptorSets (" + std::to_string(limits.MaxBoundDescriptorSets) + ").");
            }
            if (result.Cost.MaxStageSamplers > limits.MaxPerStageSamplers)
            {
                result.AddError("LAYOUT_EXCEEDS_STAGE_SAMPLERS",
                    "Per-stage sampler count (" + std::to_string(result.Cost.MaxStageSamplers) +
                    ") exceeds MaxPerStageSamplers (" + std::to_string(limits.MaxPerStageSamplers) + ").");
            }
            if (result.Cost.MaxStageUniformBuffers > limits.MaxPerStageUniformBuffers)
            {
                result.AddError("LAYOUT_EXCEEDS_STAGE_UBOS",
                    "Per-stage uniform buffer count (" + std::to_string(result.Cost.MaxStageUniformBuffers) +
                    ") exceeds MaxPerStageUniformBuffers (" + std::to_string(limits.MaxPerStageUniformBuffers) + ").");
            }
            if (result.Cost.MaxStageStorageBuffers > limits.MaxPerStageStorageBuffers)
            {
                result.AddError("LAYOUT_EXCEEDS_STAGE_SSBOS",
                    "Per-stage storage buffer count (" + std::to_string(result.Cost.MaxStageStorageBuffers) +
                    ") exceeds MaxPerStageStorageBuffers (" + std::to_string(limits.MaxPerStageStorageBuffers) + ").");
            }
            if (result.Cost.MaxStageSampledImages > limits.MaxPerStageSampledImages)
            {
                result.AddError("LAYOUT_EXCEEDS_STAGE_SAMPLED_IMAGES",
                    "Per-stage sampled image count (" + std::to_string(result.Cost.MaxStageSampledImages) +
                    ") exceeds MaxPerStageSampledImages (" + std::to_string(limits.MaxPerStageSampledImages) + ").");
            }
            if (result.Cost.MaxStageStorageImages > limits.MaxPerStageStorageImages)
            {
                result.AddError("LAYOUT_EXCEEDS_STAGE_STORAGE_IMAGES",
                    "Per-stage storage image count (" + std::to_string(result.Cost.MaxStageStorageImages) +
                    ") exceeds MaxPerStageStorageImages (" + std::to_string(limits.MaxPerStageStorageImages) + ").");
            }
            if (result.Cost.MaxStageResources > limits.MaxPerStageResources)
            {
                result.AddError("LAYOUT_EXCEEDS_STAGE_RESOURCES",
                    "Per-stage total resources (" + std::to_string(result.Cost.MaxStageResources) +
                    ") exceeds MaxPerStageResources (" + std::to_string(limits.MaxPerStageResources) + ").");
            }
            if (uabUniformBuffers > limits.MaxUpdateAfterBindUniformBuffers)
            {
                result.AddError("LAYOUT_EXCEEDS_UAB_UBOS",
                    "UpdateAfterBind uniform buffers (" + std::to_string(uabUniformBuffers) +
                    ") exceeds device limit (" + std::to_string(limits.MaxUpdateAfterBindUniformBuffers) + ").");
            }
            if (uabSampledImages > limits.MaxUpdateAfterBindSampledImages)
            {
                result.AddError("LAYOUT_EXCEEDS_UAB_SAMPLED_IMAGES",
                    "UpdateAfterBind sampled images (" + std::to_string(uabSampledImages) +
                    ") exceeds device limit (" + std::to_string(limits.MaxUpdateAfterBindSampledImages) + ").");
            }
            if (uabStorageBuffers > limits.MaxUpdateAfterBindStorageBuffers)
            {
                result.AddError("LAYOUT_EXCEEDS_UAB_STORAGE_BUFFERS",
                    "UpdateAfterBind storage buffers (" + std::to_string(uabStorageBuffers) +
                    ") exceeds device limit (" + std::to_string(limits.MaxUpdateAfterBindStorageBuffers) + ").");
            }
        }

        if (checkDx12 && result.Cost.EstimatedDx12Dwords > limits.MaxRootSignatureDwords)
        {
            result.AddError("LAYOUT_EXCEEDS_DX12_ROOT_SIGNATURE_DWORDS",
                "Estimated DX12 Root Signature cost (" + std::to_string(result.Cost.EstimatedDx12Dwords) +
                " DWORDs) exceeds hardware limit (" + std::to_string(limits.MaxRootSignatureDwords) + " DWORDs).");
        }

        return result;
    }
}
