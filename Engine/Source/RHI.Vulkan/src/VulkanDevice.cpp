#include "Morbi/RHI/IDevice.hpp"
#include "Morbi/RHI/Vulkan/VulkanDispatch.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

#include "Morbi/Core/Assert.hpp"
#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Memory/PoolAllocator.hpp"
#include "Morbi/RHI/Vulkan/VulkanDebug.h"

namespace Morbi::RHI
{
    namespace
    {
        using namespace Morbi::RHI::Vulkan;

        constexpr u32 kMaxFramesInFlight = 2;
        constexpr u32 kMaxBindlessResources = 4096;
        constexpr u32 kMaxBindlessUniformBuffers = 256;

        VkDescriptorType ToVkDescriptorType(DescriptorType type) noexcept
        {
            switch (type)
            {
            case DescriptorType::UniformBuffer:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case DescriptorType::StorageBuffer:
            case DescriptorType::RWStorageBuffer:       return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case DescriptorType::SampledTexture:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case DescriptorType::StorageTexture:        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case DescriptorType::Sampler:               return VK_DESCRIPTOR_TYPE_SAMPLER;
            case DescriptorType::CombinedImageSampler:  return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case DescriptorType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            }
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        }

        VkShaderStageFlags ToVkShaderStageFlags(ShaderVisibility vis) noexcept
        {
            VkShaderStageFlags flags = 0;
            if (HasVisibility(vis, ShaderVisibility::Vertex))  flags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (HasVisibility(vis, ShaderVisibility::Pixel))   flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (HasVisibility(vis, ShaderVisibility::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
            return flags != 0 ? flags : VK_SHADER_STAGE_ALL;
        }

        VkFilter ToVkFilter(StaticSamplerFilter filter) noexcept
        {
            switch (filter)
            {
            case StaticSamplerFilter::Nearest:     return VK_FILTER_NEAREST;
            case StaticSamplerFilter::Linear:
            case StaticSamplerFilter::Anisotropic:
            default:                               return VK_FILTER_LINEAR;
            }
        }

        VkSamplerAddressMode ToVkAddressMode(StaticSamplerAddressMode mode) noexcept
        {
            switch (mode)
            {
            case StaticSamplerAddressMode::Repeat:        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case StaticSamplerAddressMode::ClampToEdge:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case StaticSamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case StaticSamplerAddressMode::MirrorRepeat:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        VkFormat ToVkFormat(Format fmt) noexcept
        {
            switch (fmt)
            {
            case Format::RGBA8_UNorm:       return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA8_sRGB:        return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::BGRA8_UNorm:       return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::BGRA8_sRGB:        return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::RG16_Float:        return VK_FORMAT_R16G16_SFLOAT;
            case Format::RGBA16_Float:      return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::R32_Float:         return VK_FORMAT_R32_SFLOAT;
            case Format::RG32_Float:        return VK_FORMAT_R32G32_SFLOAT;
            case Format::RGB32_Float:       return VK_FORMAT_R32G32B32_SFLOAT;
            case Format::RGBA32_Float:      return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Format::D32_Float:         return VK_FORMAT_D32_SFLOAT;
            case Format::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
            default:                        return VK_FORMAT_B8G8R8A8_UNORM;
            }
        }

        Format FromVkFormat(VkFormat fmt) noexcept
        {
            switch (fmt)
            {
            case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNorm;
            case VK_FORMAT_R8G8B8A8_SRGB:  return Format::RGBA8_sRGB;
            case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNorm;
            case VK_FORMAT_B8G8R8A8_SRGB:  return Format::BGRA8_sRGB;
            default:                       return Format::BGRA8_UNorm;
            }
        }

        VkImageLayout ToVkImageLayout(ResourceState state) noexcept
        {
            switch (state)
            {
            case ResourceState::Undefined:       return VK_IMAGE_LAYOUT_UNDEFINED;
            case ResourceState::Common:          return VK_IMAGE_LAYOUT_GENERAL;
            case ResourceState::ShaderResource:  return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case ResourceState::UnorderedAccess: return VK_IMAGE_LAYOUT_GENERAL;
            case ResourceState::RenderTarget:    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case ResourceState::DepthWrite:      return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case ResourceState::DepthRead:       return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case ResourceState::CopySource:      return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case ResourceState::CopyDest:        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case ResourceState::Present:         return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            default:                             return VK_IMAGE_LAYOUT_GENERAL;
            }
        }

        VkAccessFlags ToVkAccessFlags(ResourceState state) noexcept
        {
            switch (state)
            {
            case ResourceState::VertexBuffer:    return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            case ResourceState::IndexBuffer:     return VK_ACCESS_INDEX_READ_BIT;
            case ResourceState::ConstantBuffer:  return VK_ACCESS_UNIFORM_READ_BIT;
            case ResourceState::ShaderResource:  return VK_ACCESS_SHADER_READ_BIT;
            case ResourceState::UnorderedAccess: return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            case ResourceState::RenderTarget:    return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case ResourceState::DepthWrite:      return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            case ResourceState::DepthRead:       return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            case ResourceState::CopySource:      return VK_ACCESS_TRANSFER_READ_BIT;
            case ResourceState::CopyDest:        return VK_ACCESS_TRANSFER_WRITE_BIT;
            default:                             return 0;
            }
        }

        VkPipelineStageFlags ToVkStageFlags(ResourceState state) noexcept
        {
            switch (state)
            {
            case ResourceState::Undefined:       return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            case ResourceState::VertexBuffer:
            case ResourceState::IndexBuffer:     return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            case ResourceState::ConstantBuffer:
            case ResourceState::ShaderResource:
            case ResourceState::UnorderedAccess: return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            case ResourceState::RenderTarget:    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            case ResourceState::DepthWrite:
            case ResourceState::DepthRead:       return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            case ResourceState::CopySource:
            case ResourceState::CopyDest:        return VK_PIPELINE_STAGE_TRANSFER_BIT;
            case ResourceState::Present:         return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            default:                             return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            }
        }

        // VMA-style Block Sub-Allocator for Vulkan Device Memory & Transient Aliasing
        struct VulkanMemoryAllocation
        {
            VkDeviceMemory DeviceMemory = VK_NULL_HANDLE;
            VkDeviceSize Offset = 0;
            VkDeviceSize Size = 0;
            void* MappedPtr = nullptr;
            u32 BlockIndex = 0xFFFFFFFFu;
            bool Dedicated = false;
            bool Aliased = false;
        };

        class VulkanMemoryAllocator
        {
        public:
            void Initialize(VkPhysicalDevice physDevice, VkDevice device, const VulkanInstanceDispatch& instDisp, const VulkanDeviceDispatch& devDisp)
            {
                m_device = device;
                m_devDisp = &devDisp;
                instDisp.GetPhysicalDeviceMemoryProperties(physDevice, &m_memProps);
            }

            void Shutdown()
            {
                if (!m_devDisp || m_device == VK_NULL_HANDLE)
                {
                    return;
                }
                for (auto& block : m_blocks)
                {
                    if (block.MappedBase)
                    {
                        m_devDisp->UnmapMemory(m_device, block.Memory);
                    }
                    if (block.Memory != VK_NULL_HANDLE)
                    {
                        m_devDisp->FreeMemory(m_device, block.Memory, nullptr);
                    }
                }
                m_blocks.clear();
                m_stats = {};
            }

            u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const
            {
                for (u32 i = 0; i < m_memProps.memoryTypeCount; ++i)
                {
                    if ((typeFilter & (1u << i)) && (m_memProps.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        return i;
                    }
                }
                for (u32 i = 0; i < m_memProps.memoryTypeCount; ++i)
                {
                    if (typeFilter & (1u << i))
                    {
                        return i;
                    }
                }
                return 0;
            }

            bool Allocate(
                const VkMemoryRequirements& reqs,
                VkMemoryPropertyFlags props,
                u64 aliasingOffset,
                VulkanMemoryAllocation& outAlloc)
            {
                const u32 memType = FindMemoryType(reqs.memoryTypeBits, props);
                const VkDeviceSize align = std::max<VkDeviceSize>(reqs.alignment, 256);

                // Check existing slab blocks
                for (u32 i = 0; i < static_cast<u32>(m_blocks.size()); ++i)
                {
                    auto& block = m_blocks[i];
                    if (block.Memory == VK_NULL_HANDLE || block.MemoryTypeIndex != memType)
                    {
                        continue;
                    }

                    if (aliasingOffset != 0xFFFFFFFFFFFFFFFFULL)
                    {
                        const VkDeviceSize alignedAlias = (aliasingOffset + align - 1) & ~(align - 1);
                        if (alignedAlias + reqs.size <= block.Size)
                        {
                            outAlloc.DeviceMemory = block.Memory;
                            outAlloc.Offset = alignedAlias;
                            outAlloc.Size = reqs.size;
                            outAlloc.BlockIndex = i;
                            outAlloc.Aliased = true;
                            outAlloc.MappedPtr = block.MappedBase ? (static_cast<u8*>(block.MappedBase) + alignedAlias) : nullptr;
                            if (alignedAlias + reqs.size > block.HeadOffset)
                            {
                                block.HeadOffset = alignedAlias + reqs.size;
                            }
                            else
                            {
                                m_stats.TransientAliasedBytesSaved += reqs.size;
                            }
                            ++block.ActiveAllocations;
                            ++m_stats.ActiveAllocations;
                            return true;
                        }
                    }
                    else
                    {
                        // 1. Try reclaiming a coalesced free range first
                        for (usize rIdx = 0; rIdx < block.FreeRanges.size(); ++rIdx)
                        {
                            const FreeRange range = block.FreeRanges[rIdx];
                            const VkDeviceSize alignedOffset = (range.Offset + align - 1) & ~(align - 1);
                            const VkDeviceSize padding = alignedOffset - range.Offset;
                            if (padding + reqs.size <= range.Size)
                            {
                                const VkDeviceSize suffixOffset = alignedOffset + reqs.size;
                                const VkDeviceSize suffixSize = range.Size - (padding + reqs.size);
                                if (padding > 0 && suffixSize > 0)
                                {
                                    block.FreeRanges[rIdx] = FreeRange{ range.Offset, padding };
                                    block.FreeRanges.insert(
                                        block.FreeRanges.begin() + static_cast<isize>(rIdx + 1),
                                        FreeRange{ suffixOffset, suffixSize });
                                }
                                else if (padding > 0)
                                {
                                    block.FreeRanges[rIdx] = FreeRange{ range.Offset, padding };
                                }
                                else if (suffixSize > 0)
                                {
                                    block.FreeRanges[rIdx] = FreeRange{ suffixOffset, suffixSize };
                                }
                                else
                                {
                                    block.FreeRanges.erase(block.FreeRanges.begin() + static_cast<isize>(rIdx));
                                }

                                outAlloc.DeviceMemory = block.Memory;
                                outAlloc.Offset = alignedOffset;
                                outAlloc.Size = reqs.size;
                                outAlloc.BlockIndex = i;
                                outAlloc.Aliased = false;
                                outAlloc.MappedPtr = block.MappedBase ? (static_cast<u8*>(block.MappedBase) + alignedOffset) : nullptr;
                                ++block.ActiveAllocations;
                                ++m_stats.ActiveAllocations;
                                m_stats.TotalAllocatedBytes += reqs.size;
                                return true;
                            }
                        }

                        // 2. Otherwise bump-allocate from HeadOffset
                        const VkDeviceSize alignedHead = (block.HeadOffset + align - 1) & ~(align - 1);
                        if (alignedHead + reqs.size <= block.Size)
                        {
                            if (alignedHead > block.HeadOffset)
                            {
                                InsertFreeRange(block, block.HeadOffset, alignedHead - block.HeadOffset);
                            }
                            outAlloc.DeviceMemory = block.Memory;
                            outAlloc.Offset = alignedHead;
                            outAlloc.Size = reqs.size;
                            outAlloc.BlockIndex = i;
                            outAlloc.Aliased = false;
                            outAlloc.MappedPtr = block.MappedBase ? (static_cast<u8*>(block.MappedBase) + alignedHead) : nullptr;
                            block.HeadOffset = alignedHead + reqs.size;
                            ++block.ActiveAllocations;
                            ++m_stats.ActiveAllocations;
                            m_stats.TotalAllocatedBytes += reqs.size;
                            return true;
                        }
                    }
                }

                // Allocate a new 32 MB slab block (or larger if requested)
                constexpr VkDeviceSize kDefaultSlabSize = 32ULL * 1024ULL * 1024ULL;
                const VkDeviceSize blockSize = std::max<VkDeviceSize>(kDefaultSlabSize, reqs.size * 2);

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = blockSize;
                allocInfo.memoryTypeIndex = memType;

                VkDeviceMemory newMem = VK_NULL_HANDLE;
                if (m_devDisp->AllocateMemory(m_device, &allocInfo, nullptr, &newMem) != VK_SUCCESS)
                {
                    return false;
                }

                void* mappedBase = nullptr;
                if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                {
                    m_devDisp->MapMemory(m_device, newMem, 0, blockSize, 0, &mappedBase);
                }

                const u32 blockIdx = [&]() -> u32 {
                    for (u32 i = 0; i < static_cast<u32>(m_blocks.size()); ++i)
                    {
                        if (m_blocks[i].Memory == VK_NULL_HANDLE)
                        {
                            return i;
                        }
                    }
                    m_blocks.push_back(MemoryBlock{});
                    return static_cast<u32>(m_blocks.size() - 1);
                }();
                const bool isAliased = (aliasingOffset != 0xFFFFFFFFFFFFFFFFULL);
                const VkDeviceSize initialOffset = isAliased
                    ? ((aliasingOffset + align - 1) & ~(align - 1))
                    : 0ULL;

                m_blocks[blockIdx] = MemoryBlock{
                    .Memory = newMem,
                    .MemoryTypeIndex = memType,
                    .Size = blockSize,
                    .HeadOffset = initialOffset + reqs.size,
                    .ActiveAllocations = 1u,
                    .MappedBase = mappedBase,
                    .FreeRanges = {}
                };

                ++m_stats.HeapBlockCount;
                m_stats.TotalReservedBytes += blockSize;
                ++m_stats.ActiveAllocations;
                if (!isAliased)
                {
                    m_stats.TotalAllocatedBytes += reqs.size;
                }

                outAlloc.DeviceMemory = newMem;
                outAlloc.Offset = initialOffset;
                outAlloc.Size = reqs.size;
                outAlloc.BlockIndex = blockIdx;
                outAlloc.Aliased = isAliased;
                outAlloc.MappedPtr = mappedBase ? (static_cast<u8*>(mappedBase) + initialOffset) : nullptr;
                return true;
            }

            void Free(const VulkanMemoryAllocation& alloc)
            {
                if (alloc.BlockIndex >= m_blocks.size() || alloc.DeviceMemory == VK_NULL_HANDLE)
                {
                    return;
                }
                if (m_stats.ActiveAllocations > 0)
                {
                    --m_stats.ActiveAllocations;
                }
                if (!alloc.Aliased && m_stats.TotalAllocatedBytes >= alloc.Size)
                {
                    m_stats.TotalAllocatedBytes -= alloc.Size;
                }

                auto& block = m_blocks[alloc.BlockIndex];
                if (block.Memory == VK_NULL_HANDLE)
                {
                    return;
                }
                if (block.ActiveAllocations > 0)
                {
                    --block.ActiveAllocations;
                }

                if (block.ActiveAllocations == 0)
                {
                    block.HeadOffset = 0;
                    block.FreeRanges.clear();

                    constexpr VkDeviceSize kDefaultSlabSize = 32ULL * 1024ULL * 1024ULL;
                    bool hasAnotherLiveSameType = false;
                    for (usize i = 0; i < m_blocks.size(); ++i)
                    {
                        if (i != alloc.BlockIndex &&
                            m_blocks[i].Memory != VK_NULL_HANDLE &&
                            m_blocks[i].MemoryTypeIndex == block.MemoryTypeIndex)
                        {
                            hasAnotherLiveSameType = true;
                            break;
                        }
                    }

                    if (m_devDisp && m_device != VK_NULL_HANDLE && (block.Size > kDefaultSlabSize || hasAnotherLiveSameType))
                    {
                        if (block.MappedBase)
                        {
                            m_devDisp->UnmapMemory(m_device, block.Memory);
                            block.MappedBase = nullptr;
                        }
                        m_devDisp->FreeMemory(m_device, block.Memory, nullptr);
                        block.Memory = VK_NULL_HANDLE;
                        if (m_stats.HeapBlockCount > 0)
                        {
                            --m_stats.HeapBlockCount;
                        }
                        if (m_stats.TotalReservedBytes >= block.Size)
                        {
                            m_stats.TotalReservedBytes -= block.Size;
                        }
                        block.Size = 0;
                    }
                }
                else if (!alloc.Aliased && alloc.Size > 0)
                {
                    InsertFreeRange(block, alloc.Offset, alloc.Size);
                    if (!block.FreeRanges.empty())
                    {
                        const FreeRange& tail = block.FreeRanges.back();
                        if (tail.Offset + tail.Size == block.HeadOffset)
                        {
                            block.HeadOffset = tail.Offset;
                            block.FreeRanges.pop_back();
                        }
                    }
                }
            }

            [[nodiscard]] MemoryAllocatorStats GetStats() const noexcept { return m_stats; }

        private:
            struct FreeRange
            {
                VkDeviceSize Offset = 0;
                VkDeviceSize Size = 0;
            };

            struct MemoryBlock
            {
                VkDeviceMemory Memory = VK_NULL_HANDLE;
                u32 MemoryTypeIndex = 0;
                VkDeviceSize Size = 0;
                VkDeviceSize HeadOffset = 0;
                u32 ActiveAllocations = 0;
                void* MappedBase = nullptr;
                std::vector<FreeRange> FreeRanges;
            };

            static void InsertFreeRange(MemoryBlock& block, VkDeviceSize offset, VkDeviceSize size)
            {
                if (size == 0)
                {
                    return;
                }
                auto it = std::lower_bound(
                    block.FreeRanges.begin(),
                    block.FreeRanges.end(),
                    offset,
                    [](const FreeRange& r, VkDeviceSize off) { return r.Offset < off; });
                block.FreeRanges.insert(it, FreeRange{ offset, size });

                // Coalesce adjacent or overlapping ranges
                usize writeIdx = 0;
                for (usize readIdx = 1; readIdx < block.FreeRanges.size(); ++readIdx)
                {
                    auto& prev = block.FreeRanges[writeIdx];
                    const auto& curr = block.FreeRanges[readIdx];
                    if (prev.Offset + prev.Size >= curr.Offset)
                    {
                        const VkDeviceSize newEnd = std::max(prev.Offset + prev.Size, curr.Offset + curr.Size);
                        prev.Size = newEnd - prev.Offset;
                    }
                    else
                    {
                        ++writeIdx;
                        block.FreeRanges[writeIdx] = curr;
                    }
                }
                block.FreeRanges.resize(writeIdx + 1);
            }

            VkDevice m_device = VK_NULL_HANDLE;
            const VulkanDeviceDispatch* m_devDisp = nullptr;
            VkPhysicalDeviceMemoryProperties m_memProps{};
            std::vector<MemoryBlock> m_blocks;
            MemoryAllocatorStats m_stats{};
        };

        struct VulkanBufferRecord
        {
            BufferDesc Desc;
            VkBuffer Buffer = VK_NULL_HANDLE;
            VulkanMemoryAllocation Allocation{};
            BindlessResourceIndex Bindless{};
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedTimelineValue = 0;
            bool PendingDestroy = false;
        };

        struct VulkanTextureRecord
        {
            TextureDesc Desc;
            VkImage Image = VK_NULL_HANDLE;
            VkImageView View = VK_NULL_HANDLE;
            VulkanMemoryAllocation Allocation{};
            BindlessResourceIndex Bindless{};
            bool OwnsImage = true;
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedTimelineValue = 0;
            bool PendingDestroy = false;
        };

        struct VulkanPipelineLayoutRecord
        {
            PipelineLayoutDesc Desc;
            u64 CanonicalHash = 0;
            VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
            std::vector<VkDescriptorSetLayout> OwnedSetLayouts;
            std::vector<VkSampler> OwnedSamplers;
            VkShaderStageFlags PushConstantStageMask = 0;
            u32 MaxPushConstantBytes = 0;
            bool IncludesGlobalBindlessSet = false;
            bool IsDefaultLayout = false;
            u32 CacheRefCount = 1;
            u32 PipelineRefCount = 0;
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedTimelineValue = 0;
            bool PendingDestroy = false;
        };

        struct VulkanPipelineRecord
        {
            VkPipeline Pipeline = VK_NULL_HANDLE;
            PipelineLayoutHandle Layout = PipelineLayoutHandle::Invalid();
            VkPipelineLayout NativeLayout = VK_NULL_HANDLE;
            VkShaderStageFlags PushConstantStageMask = VK_SHADER_STAGE_ALL;
            u32 PushConstantMaxBytes = 128;
            bool IncludesGlobalBindlessSet = true;
            bool IsCompute = false;
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedTimelineValue = 0;
            bool PendingDestroy = false;
        };

        // Deferred GPU resource destruction
        struct DeferredDestruction
        {
            enum class Type : u8 { Buffer, Texture, Pipeline, PipelineLayout };
            Type ResourceType;
            u64 CompletionTimelineValue;
            union
            {
                BufferHandle BufferHandle;
                TextureHandle TextureHandle;
                PipelineHandle PipelineHandle;
                PipelineLayoutHandle PipelineLayoutHandle;
            };
        };

        class VulkanDevice;

        class VulkanCommandList final : public ICommandList
        {
        public:
            VulkanCommandList() = default;
            ~VulkanCommandList() override;

            VulkanCommandList(const VulkanCommandList&) = delete;
            VulkanCommandList& operator=(const VulkanCommandList&) = delete;

            void BindContext(
                VulkanDevice* owner,
                VkDevice device,
                VkCommandPool cmdPool,
                VkCommandBuffer cmdBuf,
                const VulkanDeviceDispatch* disp,
                VkPipelineLayout pipelineLayout,
                VkDescriptorSet bindlessSet,
                u32 renderWidth,
                u32 renderHeight,
                bool ownsPool = false)
            {
                m_owner = owner;
                m_device = device;
                m_cmdPool = cmdPool;
                m_cmd = cmdBuf;
                m_disp = disp;
                m_pipelineLayout = pipelineLayout;
                m_activeVkLayout = pipelineLayout;
                m_activePushStageMask = VK_SHADER_STAGE_ALL;
                m_activePushMaxBytes = 128;
                m_activeLayoutIncludesBindless = true;
                m_bindlessSet = bindlessSet;
                m_renderWidth = renderWidth;
                m_renderHeight = renderHeight;
                m_ownsPool = ownsPool;
            }

            void Begin() override;
            void End() override;
            void Discard() override;

            void TrackBuffer(BufferHandle handle) override;
            void TrackTexture(TextureHandle handle) override;
            void TrackPipeline(PipelineHandle handle) override;
            void TrackPipelineLayout(PipelineLayoutHandle layout) override;

            void ReleaseUnsubmittedReferences();
            void OnSubmitted(u64 submissionTimelineValue);
            void ShutdownInternal();

            void SetSubmissionTimelineValue(u64 timelineValue) noexcept
            {
                m_submissionTimelineValue = timelineValue;
            }

            [[nodiscard]] u64 GetSubmissionTimelineValue() const noexcept
            {
                return m_submissionTimelineValue;
            }

            [[nodiscard]] VkCommandBuffer GetVkCommandBuffer() const noexcept { return m_cmd; }
            [[nodiscard]] bool IsRecording() const noexcept { return m_isRecording; }

            const std::unordered_set<BufferHandle>& GetReferencedBuffers() const noexcept override
            {
                return m_referencedBuffers;
            }

            const std::unordered_set<TextureHandle>& GetReferencedTextures() const noexcept override
            {
                return m_referencedTextures;
            }

            const std::unordered_set<PipelineHandle>& GetReferencedPipelines() const noexcept override
            {
                return m_referencedPipelines;
            }

            const std::unordered_set<PipelineLayoutHandle>& GetReferencedPipelineLayouts() const noexcept override
            {
                return m_referencedPipelineLayouts;
            }

            void TransitionTexture(TextureHandle texture, ResourceState before, ResourceState after) override;
            void TransitionBuffer(BufferHandle buffer, ResourceState before, ResourceState after) override;
            void BeginRendering(const DynamicRenderPassDesc& passDesc) override;

            void EndRendering() override
            {
                if (m_disp && m_disp->CmdEndRendering)
                {
                    m_disp->CmdEndRendering(m_cmd);
                }
            }

            void SetViewport(const Viewport& viewport) override
            {
                VkViewport vp{};
                vp.x = viewport.X;
                vp.y = viewport.Y;
                vp.width = viewport.Width;
                vp.height = viewport.Height;
                vp.minDepth = viewport.MinDepth;
                vp.maxDepth = viewport.MaxDepth;
                m_disp->CmdSetViewport(m_cmd, 0, 1, &vp);
            }

            void SetScissor(const ScissorRect& scissor) override
            {
                VkRect2D rect{};
                rect.offset = { scissor.X, scissor.Y };
                rect.extent = { scissor.Width, scissor.Height };
                m_disp->CmdSetScissor(m_cmd, 0, 1, &rect);
            }

            void SetPipeline(PipelineHandle pipeline) override;

            void PushConstants(const void* data, u32 sizeBytes, u32 offsetBytes) override
            {
                const VkPipelineLayout targetLayout = (m_activeVkLayout != VK_NULL_HANDLE) ? m_activeVkLayout : m_pipelineLayout;
                if (targetLayout != VK_NULL_HANDLE && m_activePushStageMask != 0 && data && sizeBytes > 0 && offsetBytes < m_activePushMaxBytes)
                {
                    const u32 pushSize = std::min<u32>(sizeBytes, m_activePushMaxBytes - offsetBytes);
                    if (pushSize > 0)
                    {
                        m_disp->CmdPushConstants(
                            m_cmd,
                            targetLayout,
                            m_activePushStageMask,
                            offsetBytes,
                            pushSize,
                            data);
                    }
                }
            }

            void BindVertexBuffer(BufferHandle buffer, u64 offsetBytes) override;
            void BindIndexBuffer(BufferHandle buffer, u64 offsetBytes, bool index32Bit) override;
            void BindConstantBuffer(u32 rootParamSlot, BufferHandle buffer, u64 offsetBytes = 0) override;

            void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override
            {
                m_disp->CmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
            }

            void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override
            {
                m_disp->CmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
            }

            void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override
            {
                m_disp->CmdDispatch(m_cmd, groupCountX, groupCountY, groupCountZ);
            }

            void BeginDebugMarker(std::string_view label) override;
            void EndDebugMarker() override;

        private:
            VulkanDevice* m_owner = nullptr;
            VkDevice m_device = VK_NULL_HANDLE;
            VkCommandPool m_cmdPool = VK_NULL_HANDLE;
            VkCommandBuffer m_cmd = VK_NULL_HANDLE;
            const VulkanDeviceDispatch* m_disp = nullptr;
            VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
            VkPipelineLayout m_activeVkLayout = VK_NULL_HANDLE;
            VkShaderStageFlags m_activePushStageMask = VK_SHADER_STAGE_ALL;
            u32 m_activePushMaxBytes = 128;
            bool m_activeLayoutIncludesBindless = true;
            VkDescriptorSet m_bindlessSet = VK_NULL_HANDLE;
            u32 m_renderWidth = 1280;
            u32 m_renderHeight = 720;
            bool m_ownsPool = false;
            bool m_isRecording = false;
            std::unordered_set<BufferHandle> m_referencedBuffers;
            std::unordered_set<TextureHandle> m_referencedTextures;
            std::unordered_set<PipelineHandle> m_referencedPipelines;
            std::unordered_set<PipelineLayoutHandle> m_referencedPipelineLayouts;
            u64 m_submissionTimelineValue = 0;
        };

        // Thread Ownership & Synchronization Policy:
        // - Frame lifecycle and swapchain management (BeginFrame, EndFrameAndPresent, ResizeSwapchain, WaitIdle,
        //   Shutdown) are owned by the render/main thread.
        // - Individual ICommandList instances do not support concurrent recording from multiple threads simultaneously;
        //   each command list must be recorded by at most one thread at a time.
        // - m_resourceMutex protects CPU-side resource pools (m_buffers, m_textures, m_pipelines backed by std::deque),
        //   bindless descriptor free lists/counters, memory allocator sub-allocations (m_allocator),
        //   m_deferredDestructions, m_nextTimelineValue, and m_frameOpen.
        // - m_submitMutex serializes vkQueueSubmit calls on m_graphicsQueue.
        // - Lock-order invariant: m_resourceMutex and m_submitMutex are never nested, and GPU wait operations
        //   (vkWaitSemaphores, vkWaitForFences, vkDeviceWaitIdle, vkQueueWaitIdle) never hold either mutex.
        class VulkanDevice final : public IDevice
        {
        public:
            ~VulkanDevice() override
            {
                Shutdown();
            }

            BackendAPI GetAPI() const noexcept override { return BackendAPI::Vulkan; }
            std::string_view GetAdapterName() const noexcept override { return m_adapterName; }

            Core::Result<bool, std::string> Initialize(
                const DeviceDesc& desc,
                const Platform::NativeWindowHandle& windowHandle,
                u32 width,
                u32 height) override
            {
                m_width = std::max<u32>(width, 1);
                m_height = std::max<u32>(height, 1);

#if defined(_WIN32)
                m_vulkanLib = LoadLibraryA("vulkan-1.dll");
                if (!m_vulkanLib)
                {
                    return Core::Result<bool, std::string>::Failure("Failed to load vulkan-1.dll.");
                }
                m_globalDisp.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                    GetProcAddress(static_cast<HMODULE>(m_vulkanLib), "vkGetInstanceProcAddr"));
#endif
                if (!m_globalDisp.GetInstanceProcAddr)
                {
                    return Core::Result<bool, std::string>::Failure("vkGetInstanceProcAddr not found.");
                }

                m_globalDisp.CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
                    m_globalDisp.GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
                m_globalDisp.EnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                    m_globalDisp.GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
                m_globalDisp.EnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
                    m_globalDisp.GetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"));

                VkApplicationInfo appInfo{};
                appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                appInfo.pApplicationName = desc.ApplicationName.data();
                appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
                appInfo.pEngineName = "MorbiEngine";
                appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
                appInfo.apiVersion = VK_API_VERSION_1_3;

                const std::array<const char*, 2> baseExtensions = {
                    VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
                    VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
                };

                VulkanDebugInstanceConfig debugConfig{};
                m_debug.PrepareInstanceConfig(
                    m_globalDisp,
                    desc.EnableValidation,
                    Core::Diagnostics::IsGpuGbvMode(),
                    baseExtensions.data(),
                    baseExtensions.size(),
                    debugConfig);

                VkInstanceCreateInfo instInfo{};
                instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
                instInfo.pNext = VulkanDebug::GetInstanceCreatePNext(debugConfig);
                instInfo.pApplicationInfo = &appInfo;
                instInfo.enabledLayerCount = static_cast<u32>(debugConfig.EnabledLayers.size());
                instInfo.ppEnabledLayerNames = debugConfig.EnabledLayers.empty() ? nullptr : debugConfig.EnabledLayers.data();
                instInfo.enabledExtensionCount = static_cast<u32>(debugConfig.EnabledExtensions.size());
                instInfo.ppEnabledExtensionNames = debugConfig.EnabledExtensions.empty() ? nullptr : debugConfig.EnabledExtensions.data();

                const VkResult instRes = m_globalDisp.CreateInstance(&instInfo, nullptr, &m_instance);
                if (!VulkanDebug::CheckVkResult(instRes, "vkCreateInstance"))
                {
                    return Core::Result<bool, std::string>::Failure("vkCreateInstance (Vulkan 1.3) failed.");
                }

                LoadInstanceDispatch();
                m_debug.InitializePostInstance(m_instance, m_globalDisp.GetInstanceProcAddr, debugConfig);

#if defined(VK_USE_PLATFORM_WIN32_KHR)
                if (windowHandle.Window != nullptr && m_instDisp.CreateWin32SurfaceKHR)
                {
                    VkWin32SurfaceCreateInfoKHR surfInfo{};
                    surfInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                    surfInfo.hinstance = static_cast<HINSTANCE>(windowHandle.DisplayOrInstance);
                    surfInfo.hwnd = static_cast<HWND>(windowHandle.Window);
                    if (m_instDisp.CreateWin32SurfaceKHR(m_instance, &surfInfo, nullptr, &m_surface) != VK_SUCCESS)
                    {
                        return Core::Result<bool, std::string>::Failure("vkCreateWin32SurfaceKHR failed.");
                    }
                }
#endif

                u32 gpuCount = 0;
                m_instDisp.EnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);
                if (gpuCount == 0)
                {
                    return Core::Result<bool, std::string>::Failure("No Vulkan physical devices found.");
                }

                std::vector<VkPhysicalDevice> gpus(gpuCount);
                m_instDisp.EnumeratePhysicalDevices(m_instance, &gpuCount, gpus.data());
                m_physDevice = gpus[0];

                for (VkPhysicalDevice candidate : gpus)
                {
                    VkPhysicalDeviceProperties props{};
                    m_instDisp.GetPhysicalDeviceProperties(candidate, &props);
                    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    {
                        m_physDevice = candidate;
                        break;
                    }
                }

                VkPhysicalDeviceProperties chosenProps{};
                m_instDisp.GetPhysicalDeviceProperties(m_physDevice, &chosenProps);
                m_adapterName = chosenProps.deviceName;

                u32 qFamilyCount = 0;
                m_instDisp.GetPhysicalDeviceQueueFamilyProperties(m_physDevice, &qFamilyCount, nullptr);
                std::vector<VkQueueFamilyProperties> qFamilies(qFamilyCount);
                m_instDisp.GetPhysicalDeviceQueueFamilyProperties(m_physDevice, &qFamilyCount, qFamilies.data());

                for (u32 i = 0; i < qFamilyCount; ++i)
                {
                    if (qFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    {
                        m_graphicsQueueFamily = i;
                        break;
                    }
                }

                const float queuePriority = 1.0f;
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = m_graphicsQueueFamily;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &queuePriority;

                VkPhysicalDeviceVulkan12Features supported12{};
                supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
                VkPhysicalDeviceVulkan13Features supported13{};
                supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                supported13.pNext = &supported12;
                VkPhysicalDeviceFeatures2 supportedFeatures2{};
                supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                supportedFeatures2.pNext = &supported13;

                if (m_instDisp.GetPhysicalDeviceFeatures2)
                {
                    m_instDisp.GetPhysicalDeviceFeatures2(m_physDevice, &supportedFeatures2);
                }
                else
                {
                    supported13.dynamicRendering = VK_TRUE;
                    supported12.timelineSemaphore = VK_TRUE;
                    supported12.descriptorIndexing = VK_TRUE;
                    supported12.runtimeDescriptorArray = VK_TRUE;
                    supported12.descriptorBindingPartiallyBound = VK_TRUE;
                    supported12.descriptorBindingVariableDescriptorCount = VK_TRUE;
                    supported12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
                    supported12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
                    supported12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
                    supported12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
                    supported12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
                }

                if (supported13.dynamicRendering != VK_TRUE ||
                    supported12.timelineSemaphore != VK_TRUE ||
                    supported12.descriptorIndexing != VK_TRUE)
                {
                    return Core::Result<bool, std::string>::Failure(
                        "Selected Vulkan GPU does not support required Vulkan 1.3 dynamicRendering, timelineSemaphore, or descriptorIndexing.");
                }

                VkPhysicalDeviceVulkan12Features features12{};
                features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
                features12.timelineSemaphore = VK_TRUE;
                features12.descriptorIndexing = VK_TRUE;
                features12.runtimeDescriptorArray = supported12.runtimeDescriptorArray;
                features12.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
                features12.descriptorBindingVariableDescriptorCount = supported12.descriptorBindingVariableDescriptorCount;
                features12.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
                features12.shaderStorageBufferArrayNonUniformIndexing = supported12.shaderStorageBufferArrayNonUniformIndexing;
                features12.descriptorBindingSampledImageUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind;
                features12.descriptorBindingStorageBufferUpdateAfterBind = supported12.descriptorBindingStorageBufferUpdateAfterBind;
                features12.descriptorBindingUniformBufferUpdateAfterBind = supported12.descriptorBindingUniformBufferUpdateAfterBind;
                m_enabledVk12Features = features12;

                VkPhysicalDeviceVulkan13Features features13{};
                features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                features13.pNext = &features12;
                features13.dynamicRendering = VK_TRUE;

                VkPhysicalDeviceVulkan12Properties vk12Props{};
                vk12Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
                VkPhysicalDeviceProperties2 props2{};
                props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                props2.pNext = &vk12Props;
                if (m_globalDisp.GetInstanceProcAddr)
                {
                    auto pfnGetProps2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                        m_globalDisp.GetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceProperties2"));
                    if (pfnGetProps2)
                    {
                        pfnGetProps2(m_physDevice, &props2);
                    }
                }

                m_limits = PipelineLayoutLimits{};
                m_limits.MaxBoundDescriptorSets = chosenProps.limits.maxBoundDescriptorSets > 0
                    ? chosenProps.limits.maxBoundDescriptorSets
                    : 4u;
                m_limits.MaxPushConstantsBytes = chosenProps.limits.maxPushConstantsSize > 0
                    ? chosenProps.limits.maxPushConstantsSize
                    : kMaxPortablePushConstantsBytes;
                m_limits.MaxPerStageSamplers = chosenProps.limits.maxPerStageDescriptorSamplers > 0
                    ? chosenProps.limits.maxPerStageDescriptorSamplers
                    : 16u;
                m_limits.MaxPerStageUniformBuffers = chosenProps.limits.maxPerStageDescriptorUniformBuffers > 0
                    ? chosenProps.limits.maxPerStageDescriptorUniformBuffers
                    : 12u;
                m_limits.MaxPerStageStorageBuffers = chosenProps.limits.maxPerStageDescriptorStorageBuffers > 0
                    ? chosenProps.limits.maxPerStageDescriptorStorageBuffers
                    : 4u;
                m_limits.MaxPerStageSampledImages = chosenProps.limits.maxPerStageDescriptorSampledImages > 0
                    ? chosenProps.limits.maxPerStageDescriptorSampledImages
                    : 16u;
                m_limits.MaxPerStageStorageImages = chosenProps.limits.maxPerStageDescriptorStorageImages > 0
                    ? chosenProps.limits.maxPerStageDescriptorStorageImages
                    : 4u;
                m_limits.MaxPerStageResources = chosenProps.limits.maxPerStageResources > 0
                    ? chosenProps.limits.maxPerStageResources
                    : 128u;
                if (vk12Props.maxDescriptorSetUpdateAfterBindUniformBuffers > 0)
                {
                    m_limits.MaxUpdateAfterBindUniformBuffers = vk12Props.maxDescriptorSetUpdateAfterBindUniformBuffers;
                }
                if (vk12Props.maxDescriptorSetUpdateAfterBindSampledImages > 0)
                {
                    m_limits.MaxUpdateAfterBindSampledImages = vk12Props.maxDescriptorSetUpdateAfterBindSampledImages;
                }
                if (vk12Props.maxDescriptorSetUpdateAfterBindStorageBuffers > 0)
                {
                    m_limits.MaxUpdateAfterBindStorageBuffers = vk12Props.maxDescriptorSetUpdateAfterBindStorageBuffers;
                }
                m_limits.SupportsDescriptorIndexing = (m_enabledVk12Features.descriptorIndexing == VK_TRUE);
                m_limits.SupportsRuntimeDescriptorArray = (m_enabledVk12Features.runtimeDescriptorArray == VK_TRUE);
                m_limits.SupportsUpdateAfterBind =
                    (m_enabledVk12Features.descriptorBindingUniformBufferUpdateAfterBind == VK_TRUE) &&
                    (m_enabledVk12Features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE) &&
                    (m_enabledVk12Features.descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE);

                const std::array<const char*, 1> deviceExtensions = {
                    VK_KHR_SWAPCHAIN_EXTENSION_NAME
                };

                VkDeviceCreateInfo devInfo{};
                devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                devInfo.pNext = &features13;
                devInfo.queueCreateInfoCount = 1;
                devInfo.pQueueCreateInfos = &queueInfo;
                devInfo.enabledExtensionCount = static_cast<u32>(deviceExtensions.size());
                devInfo.ppEnabledExtensionNames = deviceExtensions.data();

                if (m_instDisp.CreateDevice(m_physDevice, &devInfo, nullptr, &m_device) != VK_SUCCESS)
                {
                    return Core::Result<bool, std::string>::Failure("vkCreateDevice (Vulkan 1.3 Dynamic Rendering + Timeline Semaphore) failed.");
                }

                LoadDeviceDispatch();
                m_devDisp.GetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_DEVICE, reinterpret_cast<u64>(m_device), "Morbi_VulkanDevice");
                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_QUEUE, reinterpret_cast<u64>(m_graphicsQueue), "Morbi_VulkanGraphicsQueue");

                m_allocator.Initialize(m_physDevice, m_device, m_instDisp, m_devDisp);

                if (!CreateTimelineAndFrameSync())
                {
                    return Core::Result<bool, std::string>::Failure("Failed to create Vulkan 1.3 Timeline Semaphore and Frame contexts.");
                }

                if (!CreateBindlessDescriptors())
                {
                    return Core::Result<bool, std::string>::Failure("Failed to create Global Bindless Descriptor Set.");
                }

                if (m_surface != VK_NULL_HANDLE && !CreateSwapchain(m_width, m_height))
                {
                    return Core::Result<bool, std::string>::Failure("Failed to create Vulkan swapchain.");
                }

                m_activeCmdList.BindContext(
                    this,
                    m_device,
                    m_frames[0].CmdPool,
                    m_frames[0].CmdBuffer,
                    &m_devDisp,
                    m_pipelineLayout,
                    m_bindlessSet,
                    m_width,
                    m_height,
                    false);

                Core::Log::Info("RHI.Vulkan", "Initialized Vulkan 1.3 Device on GPU: " + m_adapterName +
                    " [DynamicRendering=ON, TimelineSemaphore=ON, BindlessSlots=" + std::to_string(kMaxBindlessResources) +
                    ", Validation=" + (m_debug.IsDebugUtilsReady() ? "ON" : "OFF") + "]");
                return Core::Result<bool, std::string>::Success(true);
            }

            void Shutdown() override
            {
                if (m_device != VK_NULL_HANDLE)
                {
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        m_frameOpen = false;
                    }

                    // 1. Wait for all in-flight GPU work to complete
                    if (m_devDisp.DeviceWaitIdle)
                    {
                        m_devDisp.DeviceWaitIdle(m_device);
                    }

                    // 2. Resolve and release any outstanding unsubmitted command-list references
                    m_activeCmdList.ShutdownInternal();
                    {
                        std::vector<VulkanCommandList*> createdLists;
                        {
                            std::lock_guard<std::mutex> lock(m_resourceMutex);
                            createdLists = std::move(m_createdCommandLists);
                            m_createdCommandLists.clear();
                        }
                        for (VulkanCommandList* cmd : createdLists)
                        {
                            if (cmd)
                            {
                                cmd->ShutdownInternal();
                            }
                        }
                    }

                    // 3. Destroy swapchain image views and swapchain before draining/clearing pools
                    DestroySwapchain();

                    // 4. Drain all deferred destructions now that GPU is idle and unsubmitted refs are released
                    ProcessDeferredDestructions(UINT64_MAX);

                    // 5. Destroy all remaining live pipelines, buffers, and textures
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        m_pipelines.ForEach([&](PipelineHandle, VulkanPipelineRecord& rec) {
                            if (rec.Pipeline != VK_NULL_HANDLE && m_devDisp.DestroyPipeline)
                            {
                                m_devDisp.DestroyPipeline(m_device, rec.Pipeline, nullptr);
                                rec.Pipeline = VK_NULL_HANDLE;
                            }
                        });
                        m_pipelines.Clear();

                        m_pipelineLayouts.ForEach([&](PipelineLayoutHandle, VulkanPipelineLayoutRecord& rec) {
                            if (!rec.IsDefaultLayout && rec.PipelineLayout != VK_NULL_HANDLE && m_devDisp.DestroyPipelineLayout)
                            {
                                m_devDisp.DestroyPipelineLayout(m_device, rec.PipelineLayout, nullptr);
                                rec.PipelineLayout = VK_NULL_HANDLE;
                            }
                            for (VkDescriptorSetLayout setLayout : rec.OwnedSetLayouts)
                            {
                                if (setLayout != VK_NULL_HANDLE && setLayout != m_bindlessLayout && m_devDisp.DestroyDescriptorSetLayout)
                                {
                                    m_devDisp.DestroyDescriptorSetLayout(m_device, setLayout, nullptr);
                                }
                            }
                            rec.OwnedSetLayouts.clear();
                            for (VkSampler samp : rec.OwnedSamplers)
                            {
                                if (samp != VK_NULL_HANDLE && m_devDisp.DestroySampler)
                                {
                                    m_devDisp.DestroySampler(m_device, samp, nullptr);
                                }
                            }
                            rec.OwnedSamplers.clear();
                        });
                        m_pipelineLayouts.Clear();
                        m_pipelineLayoutCache.clear();
                        m_defaultBindlessPipelineLayout = PipelineLayoutHandle::Invalid();
                        m_layoutStats = {};

                        m_buffers.ForEach([&](BufferHandle, VulkanBufferRecord& rec) {
                            if (rec.Buffer != VK_NULL_HANDLE && m_devDisp.DestroyBuffer)
                            {
                                m_devDisp.DestroyBuffer(m_device, rec.Buffer, nullptr);
                                rec.Buffer = VK_NULL_HANDLE;
                            }
                            if (rec.Allocation.DeviceMemory != VK_NULL_HANDLE)
                            {
                                m_allocator.Free(rec.Allocation);
                                rec.Allocation = {};
                            }
                        });
                        m_buffers.Clear();

                        m_textures.ForEach([&](TextureHandle, VulkanTextureRecord& rec) {
                            if (rec.View != VK_NULL_HANDLE && m_devDisp.DestroyImageView)
                            {
                                m_devDisp.DestroyImageView(m_device, rec.View, nullptr);
                                rec.View = VK_NULL_HANDLE;
                            }
                            if (rec.OwnsImage && rec.Image != VK_NULL_HANDLE && m_devDisp.DestroyImage)
                            {
                                m_devDisp.DestroyImage(m_device, rec.Image, nullptr);
                                rec.Image = VK_NULL_HANDLE;
                                if (rec.Allocation.DeviceMemory != VK_NULL_HANDLE)
                                {
                                    m_allocator.Free(rec.Allocation);
                                    rec.Allocation = {};
                                }
                            }
                        });
                        m_textures.Clear();

                        m_deferredDestructions.clear();
                        m_freeBindlessUniformSlots.clear();
                        m_freeBindlessTextureSlots.clear();
                        m_freeBindlessBufferSlots.clear();
                        m_nextBindlessBufferSlot = 0;
                        m_nextBindlessTextureSlot = 0;
                        m_nextBindlessUniformSlot = 0;
                    }

                    // 6. Destroy device-level command pools, descriptors, sync objects, and allocator
                    if (m_uploadFence != VK_NULL_HANDLE && m_devDisp.DestroyFence)
                    {
                        m_devDisp.DestroyFence(m_device, m_uploadFence, nullptr);
                        m_uploadFence = VK_NULL_HANDLE;
                    }
                    if (m_uploadCmdPool != VK_NULL_HANDLE && m_devDisp.DestroyCommandPool)
                    {
                        m_devDisp.DestroyCommandPool(m_device, m_uploadCmdPool, nullptr);
                        m_uploadCmdPool = VK_NULL_HANDLE;
                        m_uploadCmdBuffer = VK_NULL_HANDLE;
                    }
                    if (m_linearSampler != VK_NULL_HANDLE && m_devDisp.DestroySampler)
                    {
                        m_devDisp.DestroySampler(m_device, m_linearSampler, nullptr);
                        m_linearSampler = VK_NULL_HANDLE;
                    }
                    if (m_pipelineLayout != VK_NULL_HANDLE && m_devDisp.DestroyPipelineLayout)
                    {
                        m_devDisp.DestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
                        m_pipelineLayout = VK_NULL_HANDLE;
                    }
                    if (m_bindlessPool != VK_NULL_HANDLE && m_devDisp.DestroyDescriptorPool)
                    {
                        m_devDisp.DestroyDescriptorPool(m_device, m_bindlessPool, nullptr);
                        m_bindlessPool = VK_NULL_HANDLE;
                    }
                    if (m_bindlessLayout != VK_NULL_HANDLE && m_devDisp.DestroyDescriptorSetLayout)
                    {
                        m_devDisp.DestroyDescriptorSetLayout(m_device, m_bindlessLayout, nullptr);
                        m_bindlessLayout = VK_NULL_HANDLE;
                    }
                    if (m_timelineSemaphore != VK_NULL_HANDLE && m_devDisp.DestroySemaphore)
                    {
                        m_devDisp.DestroySemaphore(m_device, m_timelineSemaphore, nullptr);
                        m_timelineSemaphore = VK_NULL_HANDLE;
                    }

                    for (auto& frame : m_frames)
                    {
                        if (frame.ImageAvailableSemaphore != VK_NULL_HANDLE && m_devDisp.DestroySemaphore)
                        {
                            m_devDisp.DestroySemaphore(m_device, frame.ImageAvailableSemaphore, nullptr);
                            frame.ImageAvailableSemaphore = VK_NULL_HANDLE;
                        }
                        if (frame.RenderFinishedSemaphore != VK_NULL_HANDLE && m_devDisp.DestroySemaphore)
                        {
                            m_devDisp.DestroySemaphore(m_device, frame.RenderFinishedSemaphore, nullptr);
                            frame.RenderFinishedSemaphore = VK_NULL_HANDLE;
                        }
                        if (frame.InFlightFence != VK_NULL_HANDLE && m_devDisp.DestroyFence)
                        {
                            m_devDisp.DestroyFence(m_device, frame.InFlightFence, nullptr);
                            frame.InFlightFence = VK_NULL_HANDLE;
                        }
                        if (frame.CmdPool != VK_NULL_HANDLE && m_devDisp.DestroyCommandPool)
                        {
                            m_devDisp.DestroyCommandPool(m_device, frame.CmdPool, nullptr);
                            frame.CmdPool = VK_NULL_HANDLE;
                            frame.CmdBuffer = VK_NULL_HANDLE;
                        }
                    }

                    m_allocator.Shutdown();
                    if (m_devDisp.DestroyDevice)
                    {
                        m_devDisp.DestroyDevice(m_device, nullptr);
                    }
                    m_device = VK_NULL_HANDLE;
                }

                if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE)
                {
                    if (m_instDisp.DestroySurfaceKHR)
                    {
                        m_instDisp.DestroySurfaceKHR(m_instance, m_surface, nullptr);
                    }
                    m_surface = VK_NULL_HANDLE;
                }

                if (m_instance != VK_NULL_HANDLE)
                {
                    m_debug.Shutdown(m_instance);
                    if (m_instDisp.DestroyInstance)
                    {
                        m_instDisp.DestroyInstance(m_instance, nullptr);
                    }
                    m_instance = VK_NULL_HANDLE;
                }

#if defined(_WIN32)
                if (m_vulkanLib)
                {
                    FreeLibrary(static_cast<HMODULE>(m_vulkanLib));
                    m_vulkanLib = nullptr;
                }
#endif
            }

            bool BeginFrame() override
            {
                auto& frame = m_frames[m_frameIndex];

                // Wait on Timeline Semaphore for this frame slot
                if (frame.TargetTimelineValue > 0)
                {
                    VkSemaphoreWaitInfo waitInfo{};
                    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                    waitInfo.semaphoreCount = 1;
                    waitInfo.pSemaphores = &m_timelineSemaphore;
                    waitInfo.pValues = &frame.TargetTimelineValue;
                    m_devDisp.WaitSemaphores(m_device, &waitInfo, UINT64_MAX);
                }

                m_devDisp.WaitForFences(m_device, 1, &frame.InFlightFence, VK_TRUE, UINT64_MAX);

                // Process any deferred destructions that have completed on GPU
                ProcessDeferredDestructions(GetCompletedTimelineValue());

                if (m_swapchain != VK_NULL_HANDLE)
                {
                    const VkResult acq = m_devDisp.AcquireNextImageKHR(
                        m_device,
                        m_swapchain,
                        UINT64_MAX,
                        frame.ImageAvailableSemaphore,
                        VK_NULL_HANDLE,
                        &m_acquiredImageIndex);
                    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
                    {
                        ResizeSwapchain(m_width, m_height);
                        return false;
                    }
                }

                // Reset the in-flight fence only after swapchain acquisition succeeds so an
                // out-of-date early return cannot leave the fence permanently unsignaled.
                m_devDisp.ResetFences(m_device, 1, &frame.InFlightFence);

                m_activeCmdList.Discard();
                m_devDisp.ResetCommandPool(m_device, frame.CmdPool, 0);
                m_activeCmdList.BindContext(
                    this,
                    m_device,
                    frame.CmdPool,
                    frame.CmdBuffer,
                    &m_devDisp,
                    m_pipelineLayout,
                    m_bindlessSet,
                    m_width,
                    m_height,
                    false);
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_frameOpen = true;
                }
                m_activeCmdList.Begin();
                return true;
            }

            void EndFrameAndPresent() override
            {
                auto& frame = m_frames[m_frameIndex];
                m_activeCmdList.End();

                const u64 nextTimelineVal = CommitCommandListSubmission(
                    true,
                    m_activeCmdList.GetReferencedBuffers(),
                    m_activeCmdList.GetReferencedTextures(),
                    m_activeCmdList.GetReferencedPipelines(),
                    m_activeCmdList.GetReferencedPipelineLayouts());
                frame.TargetTimelineValue = nextTimelineVal;
                m_activeCmdList.OnSubmitted(nextTimelineVal);

                std::array<VkSemaphore, 2> signalSemaphores = {
                    m_timelineSemaphore,
                    frame.RenderFinishedSemaphore
                };
                std::array<u64, 2> signalValues = {
                    nextTimelineVal,
                    0ULL
                };

                VkTimelineSemaphoreSubmitInfo timelineInfo{};
                timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
                timelineInfo.signalSemaphoreValueCount = (m_swapchain != VK_NULL_HANDLE) ? 2u : 1u;
                timelineInfo.pSignalSemaphoreValues = signalValues.data();

                const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.pNext = &timelineInfo;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &frame.CmdBuffer;

                if (m_swapchain != VK_NULL_HANDLE)
                {
                    submitInfo.waitSemaphoreCount = 1;
                    submitInfo.pWaitSemaphores = &frame.ImageAvailableSemaphore;
                    submitInfo.pWaitDstStageMask = &waitStage;
                    submitInfo.signalSemaphoreCount = 2;
                    submitInfo.pSignalSemaphores = signalSemaphores.data();
                }
                else
                {
                    submitInfo.signalSemaphoreCount = 1;
                    submitInfo.pSignalSemaphores = &m_timelineSemaphore;
                }

                {
                    std::lock_guard<std::mutex> submitLock(m_submitMutex);
                    const VkResult subRes = m_devDisp.QueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.InFlightFence);
                    VulkanDebug::CheckVkResult(subRes, "vkQueueSubmit");
                }

                if (m_swapchain != VK_NULL_HANDLE)
                {
                    VkPresentInfoKHR presentInfo{};
                    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                    presentInfo.waitSemaphoreCount = 1;
                    presentInfo.pWaitSemaphores = &frame.RenderFinishedSemaphore;
                    presentInfo.swapchainCount = 1;
                    presentInfo.pSwapchains = &m_swapchain;
                    presentInfo.pImageIndices = &m_acquiredImageIndex;
                    const VkResult presRes = m_devDisp.QueuePresentKHR(m_graphicsQueue, &presentInfo);
                    if (presRes == VK_ERROR_OUT_OF_DATE_KHR || presRes == VK_SUBOPTIMAL_KHR)
                    {
                        ResizeSwapchain(m_width, m_height);
                    }
                    else
                    {
                        VulkanDebug::CheckVkResult(presRes, "vkQueuePresentKHR");
                    }
                }

                m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
            }

            void ResizeSwapchain(u32 width, u32 height) override
            {
                if (width == 0 || height == 0)
                {
                    return;
                }
                WaitIdle();
                m_width = width;
                m_height = height;
                if (m_surface != VK_NULL_HANDLE)
                {
                    CreateSwapchain(m_width, m_height);
                }
            }

            void SetVSync(bool enabled) override
            {
                if (m_vsync == enabled)
                {
                    return;
                }
                m_vsync = enabled;
                if (m_surface != VK_NULL_HANDLE && m_swapchain != VK_NULL_HANDLE)
                {
                    ResizeSwapchain(m_width, m_height);
                }
            }

            bool IsVSyncEnabled() const noexcept override { return m_vsync; }

            void WaitIdle() override
            {
                if (m_device != VK_NULL_HANDLE && m_devDisp.DeviceWaitIdle)
                {
                    m_devDisp.DeviceWaitIdle(m_device);
                }
                // After device idle, all submitted GPU work is complete - drain eligible deferred destructions
                ProcessDeferredDestructions(UINT64_MAX);
            }

            u64 GetCompletedTimelineValue() const noexcept override
            {
                if (m_device == VK_NULL_HANDLE || m_timelineSemaphore == VK_NULL_HANDLE || !m_devDisp.GetSemaphoreCounterValue)
                {
                    return 0;
                }
                u64 val = 0;
                m_devDisp.GetSemaphoreCounterValue(m_device, m_timelineSemaphore, &val);
                return val;
            }

            void ProcessCompletedDestructions() override
            {
                ProcessDeferredDestructions(GetCompletedTimelineValue());
            }

            void ProcessDeferredDestructions(u64 completedTimelineValue)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                if (m_deferredDestructions.empty())
                {
                    return;
                }

                auto it = m_deferredDestructions.begin();
                while (it != m_deferredDestructions.end())
                {
                    bool canDestroy = false;
                    switch (it->ResourceType)
                    {
                    case DeferredDestruction::Type::Buffer:
                        if (auto* rec = m_buffers.Get(it->BufferHandle))
                        {
                            const u64 requiredTimeline = std::max(it->CompletionTimelineValue, rec->LastUsedTimelineValue);
                            it->CompletionTimelineValue = requiredTimeline;
                            if (rec->UnsubmittedRefCount == 0 && requiredTimeline <= completedTimelineValue)
                            {
                                DestroyBufferInternalLocked(it->BufferHandle);
                                canDestroy = true;
                            }
                        }
                        else
                        {
                            canDestroy = true;
                        }
                        break;

                    case DeferredDestruction::Type::Texture:
                        if (auto* rec = m_textures.Get(it->TextureHandle))
                        {
                            const u64 requiredTimeline = std::max(it->CompletionTimelineValue, rec->LastUsedTimelineValue);
                            it->CompletionTimelineValue = requiredTimeline;
                            if (rec->UnsubmittedRefCount == 0 && requiredTimeline <= completedTimelineValue)
                            {
                                DestroyTextureInternalLocked(it->TextureHandle);
                                canDestroy = true;
                            }
                        }
                        else
                        {
                            canDestroy = true;
                        }
                        break;

                    case DeferredDestruction::Type::Pipeline:
                        if (auto* rec = m_pipelines.Get(it->PipelineHandle))
                        {
                            const u64 requiredTimeline = std::max(it->CompletionTimelineValue, rec->LastUsedTimelineValue);
                            it->CompletionTimelineValue = requiredTimeline;
                            if (rec->UnsubmittedRefCount == 0 && requiredTimeline <= completedTimelineValue)
                            {
                                DestroyPipelineInternalLocked(it->PipelineHandle);
                                canDestroy = true;
                            }
                        }
                        else
                        {
                            canDestroy = true;
                        }
                        break;

                    case DeferredDestruction::Type::PipelineLayout:
                        if (auto* rec = m_pipelineLayouts.Get(it->PipelineLayoutHandle))
                        {
                            const u64 requiredTimeline = std::max(it->CompletionTimelineValue, rec->LastUsedTimelineValue);
                            it->CompletionTimelineValue = requiredTimeline;
                            if (rec->PipelineRefCount == 0 && rec->UnsubmittedRefCount == 0 && requiredTimeline <= completedTimelineValue)
                            {
                                DestroyPipelineLayoutInternalLocked(it->PipelineLayoutHandle);
                                canDestroy = true;
                            }
                        }
                        else
                        {
                            canDestroy = true;
                        }
                        break;
                    }

                    if (canDestroy)
                    {
                        it = m_deferredDestructions.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            MemoryAllocatorStats GetMemoryStats() const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_allocator.GetStats();
            }

            ICommandList& GetActiveCommandList() override { return m_activeCmdList; }

            std::unique_ptr<ICommandList> CreateCommandList() override
            {
                if (m_device == VK_NULL_HANDLE)
                {
                    return nullptr;
                }

                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
                poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                VkCommandPool cmdPool = VK_NULL_HANDLE;
                if (m_devDisp.CreateCommandPool(m_device, &poolInfo, nullptr, &cmdPool) != VK_SUCCESS)
                {
                    return nullptr;
                }

                VkCommandBufferAllocateInfo cmdAlloc{};
                cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                cmdAlloc.commandPool = cmdPool;
                cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cmdAlloc.commandBufferCount = 1;

                VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
                if (m_devDisp.AllocateCommandBuffers(m_device, &cmdAlloc, &cmdBuf) != VK_SUCCESS)
                {
                    m_devDisp.DestroyCommandPool(m_device, cmdPool, nullptr);
                    return nullptr;
                }

                auto cmdList = std::make_unique<VulkanCommandList>();
                cmdList->BindContext(
                    this,
                    m_device,
                    cmdPool,
                    cmdBuf,
                    &m_devDisp,
                    m_pipelineLayout,
                    m_bindlessSet,
                    m_width,
                    m_height,
                    true);

                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_createdCommandLists.push_back(cmdList.get());
                }
                return cmdList;
            }

            u64 SubmitCommandList(ICommandList& cmdList) override
            {
                if (m_device == VK_NULL_HANDLE || m_graphicsQueue == VK_NULL_HANDLE)
                {
                    return 0;
                }

                auto& vkCmdList = static_cast<VulkanCommandList&>(cmdList);
                if (vkCmdList.IsRecording())
                {
                    vkCmdList.End();
                }

                const u64 nextTimelineVal = CommitCommandListSubmission(
                    &vkCmdList == &m_activeCmdList,
                    vkCmdList.GetReferencedBuffers(),
                    vkCmdList.GetReferencedTextures(),
                    vkCmdList.GetReferencedPipelines(),
                    vkCmdList.GetReferencedPipelineLayouts());
                vkCmdList.OnSubmitted(nextTimelineVal);

                VkTimelineSemaphoreSubmitInfo timelineInfo{};
                timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
                timelineInfo.signalSemaphoreValueCount = 1;
                timelineInfo.pSignalSemaphoreValues = &nextTimelineVal;

                VkCommandBuffer rawCmd = vkCmdList.GetVkCommandBuffer();
                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.pNext = &timelineInfo;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &rawCmd;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &m_timelineSemaphore;

                {
                    std::lock_guard<std::mutex> submitLock(m_submitMutex);
                    const VkResult subRes = m_devDisp.QueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
                    VulkanDebug::CheckVkResult(subRes, "vkQueueSubmit(SubmitCommandList)");
                }
                return nextTimelineVal;
            }

            TextureHandle GetCurrentBackbuffer() const noexcept override
            {
                if (m_acquiredImageIndex < m_swapchainTextures.size())
                {
                    return m_swapchainTextures[m_acquiredImageIndex];
                }
                return TextureHandle::Invalid();
            }

            Format GetSwapchainFormat() const noexcept override { return m_swapchainFormat; }

            BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData) override
            {
                VkBufferCreateInfo bufInfo{};
                bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufInfo.size = std::max<u64>(desc.SizeBytes, 16ULL);
                bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

                if (HasFlag(desc.Usage, BufferUsage::Vertex))   bufInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                if (HasFlag(desc.Usage, BufferUsage::Index))    bufInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                if (HasFlag(desc.Usage, BufferUsage::Uniform))  bufInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                if (HasFlag(desc.Usage, BufferUsage::Storage) || desc.Bindless) bufInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                if (HasFlag(desc.Usage, BufferUsage::Indirect)) bufInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

                VkBuffer vkBuf = VK_NULL_HANDLE;
                if (m_devDisp.CreateBuffer(m_device, &bufInfo, nullptr, &vkBuf) != VK_SUCCESS)
                {
                    return BufferHandle::Invalid();
                }

                VkMemoryRequirements reqs{};
                m_devDisp.GetBufferMemoryRequirements(m_device, vkBuf, &reqs);

                VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                if (desc.Memory != MemoryUsage::GpuOnly || initialData != nullptr)
                {
                    props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                }

                std::lock_guard<std::mutex> lock(m_resourceMutex);

                VulkanMemoryAllocation alloc{};
                if (!m_allocator.Allocate(reqs, props, desc.AliasingPoolOffset, alloc))
                {
                    m_devDisp.DestroyBuffer(m_device, vkBuf, nullptr);
                    return BufferHandle::Invalid();
                }

                m_devDisp.BindBufferMemory(m_device, vkBuf, alloc.DeviceMemory, alloc.Offset);

                if (initialData && alloc.MappedPtr)
                {
                    std::memcpy(alloc.MappedPtr, initialData, static_cast<usize>(desc.SizeBytes));
                }

                BindlessResourceIndex bindless{};
                if (desc.Bindless)
                {
                    const bool isUniform = HasFlag(desc.Usage, BufferUsage::Uniform);
                    if (isUniform && (m_nextBindlessUniformSlot < kMaxBindlessUniformBuffers || !m_freeBindlessUniformSlots.empty()))
                    {
                        if (!m_freeBindlessUniformSlots.empty())
                        {
                            bindless.Index = m_freeBindlessUniformSlots.back();
                            m_freeBindlessUniformSlots.pop_back();
                        }
                        else
                        {
                            bindless.Index = m_nextBindlessUniformSlot++;
                        }
                        VkDescriptorBufferInfo dbi{};
                        dbi.buffer = vkBuf;
                        dbi.offset = 0;
                        dbi.range = bufInfo.size;

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = m_bindlessSet;
                        write.dstBinding = 3; // Binding 3: Uniform Buffers[]
                        write.dstArrayElement = bindless.Index;
                        write.descriptorCount = 1;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        write.pBufferInfo = &dbi;
                        m_devDisp.UpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
                    }
                    else if (!isUniform && (m_nextBindlessBufferSlot < kMaxBindlessResources || !m_freeBindlessBufferSlots.empty()))
                    {
                        if (!m_freeBindlessBufferSlots.empty())
                        {
                            bindless.Index = m_freeBindlessBufferSlots.back();
                            m_freeBindlessBufferSlots.pop_back();
                        }
                        else
                        {
                            bindless.Index = m_nextBindlessBufferSlot++;
                        }
                        VkDescriptorBufferInfo dbi{};
                        dbi.buffer = vkBuf;
                        dbi.offset = 0;
                        dbi.range = bufInfo.size;

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = m_bindlessSet;
                        write.dstBinding = 0; // Binding 0: Storage Buffers[]
                        write.dstArrayElement = bindless.Index;
                        write.descriptorCount = 1;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.pBufferInfo = &dbi;
                        m_devDisp.UpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
                    }
                }

                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(vkBuf), desc.DebugName);

                return m_buffers.Emplace(VulkanBufferRecord{
                    .Desc = desc,
                    .Buffer = vkBuf,
                    .Allocation = alloc,
                    .Bindless = bindless
                });
            }

            void DestroyBuffer(BufferHandle handle) override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_buffers.Get(handle);
                if (!rec || rec->PendingDestroy)
                {
                    return;
                }

                rec->PendingDestroy = true;
                const u64 minSafeTimeline = m_frameOpen ? (m_nextTimelineValue + 1) : m_nextTimelineValue;
                const u64 targetTimeline = std::max(rec->LastUsedTimelineValue, minSafeTimeline);

                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::Buffer,
                    .CompletionTimelineValue = targetTimeline,
                    .BufferHandle = handle
                });
            }

            void DestroyBufferInternalLocked(BufferHandle handle)
            {
                if (auto* rec = m_buffers.Get(handle))
                {
                    if (HasFlag(rec->Desc.Usage, BufferUsage::Uniform) &&
                        rec->Bindless.IsValid() &&
                        rec->Bindless.Index < kMaxBindlessUniformBuffers)
                    {
                        m_freeBindlessUniformSlots.push_back(rec->Bindless.Index);
                    }
                    else if (rec->Bindless.IsValid() &&
                             rec->Bindless.Index < kMaxBindlessResources)
                    {
                        m_freeBindlessBufferSlots.push_back(rec->Bindless.Index);
                    }
                    if (rec->Buffer != VK_NULL_HANDLE)
                    {
                        m_devDisp.DestroyBuffer(m_device, rec->Buffer, nullptr);
                        rec->Buffer = VK_NULL_HANDLE;
                    }
                    m_allocator.Free(rec->Allocation);
                    m_buffers.Remove(handle);
                }
            }

            void UpdateBufferData(BufferHandle handle, const void* data, usize sizeBytes, usize offsetBytes = 0) override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                if (auto* rec = m_buffers.Get(handle))
                {
                    if (rec->Allocation.MappedPtr && data && sizeBytes > 0 && offsetBytes < rec->Desc.SizeBytes)
                    {
                        const usize bytesToCopy = std::min<usize>(sizeBytes, static_cast<usize>(rec->Desc.SizeBytes) - offsetBytes);
                        std::memcpy(static_cast<u8*>(rec->Allocation.MappedPtr) + offsetBytes, data, bytesToCopy);
                    }
                }
            }

            BindlessResourceIndex GetBindlessIndex(BufferHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_buffers.Get(handle);
                return rec ? rec->Bindless : BindlessResourceIndex{};
            }

            bool IsResourceAlive(BufferHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_buffers.IsValid(handle);
            }

            TextureHandle CreateTexture(const TextureDesc& desc, const void* initialData) override
            {
                VkImageCreateInfo imgInfo{};
                imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imgInfo.imageType = VK_IMAGE_TYPE_2D;
                imgInfo.format = ToVkFormat(desc.PixelFormat);
                imgInfo.extent = { std::max<u32>(desc.Width, 1), std::max<u32>(desc.Height, 1), 1 };
                imgInfo.mipLevels = std::max<u32>(desc.MipLevels, 1);
                imgInfo.arrayLayers = std::max<u32>(desc.DepthOrArrayLayers, 1);
                imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

                if (desc.RenderTarget) imgInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                if (desc.DepthStencil) imgInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                if (desc.Storage)      imgInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

                VkImage image = VK_NULL_HANDLE;
                if (m_devDisp.CreateImage(m_device, &imgInfo, nullptr, &image) != VK_SUCCESS)
                {
                    return TextureHandle::Invalid();
                }

                VkMemoryRequirements reqs{};
                m_devDisp.GetImageMemoryRequirements(m_device, image, &reqs);

                VulkanMemoryAllocation alloc{};
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (!m_allocator.Allocate(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, desc.AliasingPoolOffset, alloc))
                    {
                        m_devDisp.DestroyImage(m_device, image, nullptr);
                        return TextureHandle::Invalid();
                    }
                    m_devDisp.BindImageMemory(m_device, image, alloc.DeviceMemory, alloc.Offset);
                }

                if (initialData != nullptr && !desc.DepthStencil && m_devDisp.CmdCopyBufferToImage != nullptr && m_uploadCmdBuffer != VK_NULL_HANDLE)
                {
                    const u64 uploadSize = static_cast<u64>(imgInfo.extent.width) *
                                           static_cast<u64>(imgInfo.extent.height) * 4ULL;
                    const BufferDesc stagingDesc{
                        .SizeBytes = uploadSize,
                        .Usage = BufferUsage::TransferSrc,
                        .Memory = MemoryUsage::CpuToGpu,
                        .Bindless = false,
                        .DebugName = "VulkanTextureUploadStaging"
                    };
                    const BufferHandle stagingHandle = CreateBuffer(stagingDesc, initialData);
                    VkBuffer stagingVkBuffer = VK_NULL_HANDLE;
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        if (const auto* stagingRec = m_buffers.Get(stagingHandle))
                        {
                            stagingVkBuffer = stagingRec->Buffer;
                        }
                    }

                    if (stagingVkBuffer != VK_NULL_HANDLE)
                    {
                        std::lock_guard<std::mutex> uploadLock(m_uploadMutex);
                        m_devDisp.ResetCommandPool(m_device, m_uploadCmdPool, 0);

                        VkCommandBuffer cmd = m_uploadCmdBuffer;
                        VkCommandBufferBeginInfo beginInfo{};
                        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                        m_devDisp.BeginCommandBuffer(cmd, &beginInfo);

                        VkImageMemoryBarrier toCopyDst{};
                        toCopyDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        toCopyDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        toCopyDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        toCopyDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        toCopyDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        toCopyDst.image = image;
                        toCopyDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        toCopyDst.subresourceRange.baseMipLevel = 0;
                        toCopyDst.subresourceRange.levelCount = 1;
                        toCopyDst.subresourceRange.baseArrayLayer = 0;
                        toCopyDst.subresourceRange.layerCount = 1;
                        toCopyDst.srcAccessMask = 0;
                        toCopyDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                        m_devDisp.CmdPipelineBarrier(
                            cmd,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0,
                            0, nullptr,
                            0, nullptr,
                            1, &toCopyDst);

                        VkBufferImageCopy copyRegion{};
                        copyRegion.bufferOffset = 0;
                        copyRegion.bufferRowLength = 0;
                        copyRegion.bufferImageHeight = 0;
                        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        copyRegion.imageSubresource.mipLevel = 0;
                        copyRegion.imageSubresource.baseArrayLayer = 0;
                        copyRegion.imageSubresource.layerCount = 1;
                        copyRegion.imageOffset = { 0, 0, 0 };
                        copyRegion.imageExtent = imgInfo.extent;

                        m_devDisp.CmdCopyBufferToImage(
                            cmd,
                            stagingVkBuffer,
                            image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &copyRegion);

                        VkImageMemoryBarrier toShaderRead = toCopyDst;
                        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                        m_devDisp.CmdPipelineBarrier(
                            cmd,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0,
                            0, nullptr,
                            0, nullptr,
                            1, &toShaderRead);

                        m_devDisp.EndCommandBuffer(cmd);

                        VkSubmitInfo submitInfo{};
                        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        submitInfo.commandBufferCount = 1;
                        submitInfo.pCommandBuffers = &cmd;
                        if (m_uploadFence != VK_NULL_HANDLE)
                        {
                            m_devDisp.ResetFences(m_device, 1, &m_uploadFence);
                            {
                                std::lock_guard<std::mutex> submitLock(m_submitMutex);
                                m_devDisp.QueueSubmit(m_graphicsQueue, 1, &submitInfo, m_uploadFence);
                            }
                            m_devDisp.WaitForFences(m_device, 1, &m_uploadFence, VK_TRUE, UINT64_MAX);
                        }
                        else
                        {
                            {
                                std::lock_guard<std::mutex> submitLock(m_submitMutex);
                                m_devDisp.QueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
                            }
                            m_devDisp.QueueWaitIdle(m_graphicsQueue);
                        }
                    }

                    // Staging upload has synchronously completed on the queue; reclaim staging buffer immediately
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        DestroyBufferInternalLocked(stagingHandle);
                    }
                }

                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = imgInfo.format;
                viewInfo.subresourceRange.aspectMask = desc.DepthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = imgInfo.arrayLayers;

                VkImageView view = VK_NULL_HANDLE;
                if (m_devDisp.CreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS)
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_devDisp.DestroyImage(m_device, image, nullptr);
                    m_allocator.Free(alloc);
                    return TextureHandle::Invalid();
                }

                std::lock_guard<std::mutex> lock(m_resourceMutex);

                BindlessResourceIndex bindless{};
                if (!desc.DepthStencil && (m_nextBindlessTextureSlot < kMaxBindlessResources || !m_freeBindlessTextureSlots.empty()))
                {
                    if (!m_freeBindlessTextureSlots.empty())
                    {
                        bindless.Index = m_freeBindlessTextureSlots.back();
                        m_freeBindlessTextureSlots.pop_back();
                    }
                    else
                    {
                        bindless.Index = m_nextBindlessTextureSlot++;
                    }
                    if (m_bindlessSet != VK_NULL_HANDLE)
                    {
                        VkDescriptorImageInfo imgDescInfo{};
                        imgDescInfo.sampler = VK_NULL_HANDLE;
                        imgDescInfo.imageView = view;
                        imgDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                        VkWriteDescriptorSet texWrite{};
                        texWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        texWrite.dstSet = m_bindlessSet;
                        texWrite.dstBinding = 1; // Binding 1: Texture2D[]
                        texWrite.dstArrayElement = bindless.Index;
                        texWrite.descriptorCount = 1;
                        texWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                        texWrite.pImageInfo = &imgDescInfo;
                        m_devDisp.UpdateDescriptorSets(m_device, 1, &texWrite, 0, nullptr);
                    }
                }

                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(image), desc.DebugName);
                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<u64>(view), desc.DebugName);

                return m_textures.Emplace(VulkanTextureRecord{
                    .Desc = desc,
                    .Image = image,
                    .View = view,
                    .Allocation = alloc,
                    .Bindless = bindless,
                    .OwnsImage = true
                });
            }

            void DestroyTexture(TextureHandle handle) override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_textures.Get(handle);
                if (!rec || rec->PendingDestroy)
                {
                    return;
                }

                rec->PendingDestroy = true;
                const u64 minSafeTimeline = m_frameOpen ? (m_nextTimelineValue + 1) : m_nextTimelineValue;
                const u64 targetTimeline = std::max(rec->LastUsedTimelineValue, minSafeTimeline);

                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::Texture,
                    .CompletionTimelineValue = targetTimeline,
                    .TextureHandle = handle
                });
            }

            void DestroyTextureInternalLocked(TextureHandle handle)
            {
                if (auto* rec = m_textures.Get(handle))
                {
                    if (rec->View != VK_NULL_HANDLE)
                    {
                        m_devDisp.DestroyImageView(m_device, rec->View, nullptr);
                        rec->View = VK_NULL_HANDLE;
                    }
                    if (rec->OwnsImage && rec->Image != VK_NULL_HANDLE)
                    {
                        m_devDisp.DestroyImage(m_device, rec->Image, nullptr);
                        rec->Image = VK_NULL_HANDLE;
                        m_allocator.Free(rec->Allocation);
                    }
                    if (rec->Bindless.IsValid() && rec->Bindless.Index < kMaxBindlessResources)
                    {
                        m_freeBindlessTextureSlots.push_back(rec->Bindless.Index);
                    }
                    m_textures.Remove(handle);
                }
            }

            BindlessResourceIndex GetBindlessIndex(TextureHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_textures.Get(handle);
                return rec ? rec->Bindless : BindlessResourceIndex{};
            }

            bool IsResourceAlive(TextureHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_textures.IsValid(handle);
            }

            Core::Result<PipelineLayoutHandle, std::string> CreatePipelineLayout(const PipelineLayoutDesc& desc) override
            {
                if (m_device == VK_NULL_HANDLE)
                {
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure("Vulkan device is not initialized.");
                }

                const PipelineLayoutDesc normalized = PipelineLayoutValidator::Normalize(desc);
                const PipelineLayoutValidationResult validation =
                    PipelineLayoutValidator::Validate(normalized, m_limits, BackendAPI::Vulkan);
                if (!validation.IsValid)
                {
                    std::string errMsg;
                    for (const auto& issue : validation.Issues)
                    {
                        if (issue.Level == PipelineLayoutIssue::Severity::Error)
                        {
                            if (!errMsg.empty()) errMsg += "; ";
                            errMsg += "[" + issue.Code + "] " + issue.Message;
                        }
                    }
                    if (errMsg.empty())
                    {
                        errMsg = "PipelineLayout validation failed for Vulkan backend.";
                    }
                    Core::Log::Error("RHI.Vulkan", "CreatePipelineLayout rejected: " + errMsg);
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure(errMsg);
                }

                const u64 canonicalHash = PipelineLayoutValidator::ComputeCanonicalHash(normalized);
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (auto cacheIt = m_pipelineLayoutCache.find(canonicalHash); cacheIt != m_pipelineLayoutCache.end())
                    {
                        for (PipelineLayoutHandle candidate : cacheIt->second)
                        {
                            if (auto* rec = m_pipelineLayouts.Get(candidate);
                                rec && !rec->PendingDestroy && rec->PipelineLayout != VK_NULL_HANDLE &&
                                PipelineLayoutValidator::AreStructurallyEqual(rec->Desc, normalized))
                            {
                                ++rec->CacheRefCount;
                                ++m_layoutStats.CacheHitCount;
                                return Core::Result<PipelineLayoutHandle, std::string>::Success(candidate);
                            }
                        }
                    }
                }

                std::vector<VkSampler> ownedSamplers;
                std::vector<VkDescriptorSetLayout> ownedSetLayouts;
                auto cleanupNative = [&]() {
                    for (VkDescriptorSetLayout setLayout : ownedSetLayouts)
                    {
                        if (setLayout != VK_NULL_HANDLE && setLayout != m_bindlessLayout && m_devDisp.DestroyDescriptorSetLayout)
                        {
                            m_devDisp.DestroyDescriptorSetLayout(m_device, setLayout, nullptr);
                        }
                    }
                    for (VkSampler sampler : ownedSamplers)
                    {
                        if (sampler != VK_NULL_HANDLE && m_devDisp.DestroySampler)
                        {
                            m_devDisp.DestroySampler(m_device, sampler, nullptr);
                        }
                    }
                };

                struct BuiltStaticSampler
                {
                    u32 SetIndex = 0;
                    u32 Binding = 0;
                    ShaderVisibility Visibility = ShaderVisibility::All;
                    VkSampler Sampler = VK_NULL_HANDLE;
                };
                std::vector<BuiltStaticSampler> builtStaticSamplers;
                builtStaticSamplers.reserve(normalized.StaticSamplers.size());

                for (const auto& ss : normalized.StaticSamplers)
                {
                    VkSamplerCreateInfo sampInfo{};
                    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                    sampInfo.magFilter = ToVkFilter(ss.Filter);
                    sampInfo.minFilter = ToVkFilter(ss.Filter);
                    sampInfo.mipmapMode = (ss.Filter == StaticSamplerFilter::Nearest)
                        ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                        : VK_SAMPLER_MIPMAP_MODE_LINEAR;
                    sampInfo.addressModeU = ToVkAddressMode(ss.AddressMode);
                    sampInfo.addressModeV = ToVkAddressMode(ss.AddressMode);
                    sampInfo.addressModeW = ToVkAddressMode(ss.AddressMode);
                    sampInfo.mipLodBias = 0.0f;
                    sampInfo.anisotropyEnable = (ss.Filter == StaticSamplerFilter::Anisotropic) ? VK_TRUE : VK_FALSE;
                    sampInfo.maxAnisotropy = (ss.Filter == StaticSamplerFilter::Anisotropic) ? 16.0f : 1.0f;
                    sampInfo.compareEnable = ss.IsComparison ? VK_TRUE : VK_FALSE;
                    sampInfo.compareOp = ss.IsComparison ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_NEVER;
                    sampInfo.minLod = 0.0f;
                    sampInfo.maxLod = VK_LOD_CLAMP_NONE;
                    sampInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

                    VkSampler vkSampler = VK_NULL_HANDLE;
                    const VkResult sRes = m_devDisp.CreateSampler(m_device, &sampInfo, nullptr, &vkSampler);
                    if (sRes != VK_SUCCESS || vkSampler == VK_NULL_HANDLE)
                    {
                        cleanupNative();
                        return Core::Result<PipelineLayoutHandle, std::string>::Failure(
                            "vkCreateSampler failed for static sampler at binding " + std::to_string(ss.Binding) + ".");
                    }
                    ownedSamplers.push_back(vkSampler);
                    builtStaticSamplers.push_back(BuiltStaticSampler{
                        .SetIndex = ss.Set,
                        .Binding = ss.Binding,
                        .Visibility = ss.Visibility,
                        .Sampler = vkSampler
                    });
                }

                const bool includesGlobalBindless = (normalized.Mode == PipelineLayoutMode::GlobalBindlessHybrid);
                bool hasAnySetOrStatic = includesGlobalBindless || !normalized.Sets.empty() || !builtStaticSamplers.empty();
                u32 maxSetIndex = 0;
                for (const auto& setDesc : normalized.Sets)
                {
                    maxSetIndex = std::max(maxSetIndex, setDesc.SetIndex);
                }
                for (const auto& ss : builtStaticSamplers)
                {
                    maxSetIndex = std::max(maxSetIndex, ss.SetIndex);
                }

                const u32 denseSetCount = hasAnySetOrStatic ? (maxSetIndex + 1u) : 0u;
                std::vector<VkDescriptorSetLayout> setLayouts(denseSetCount, VK_NULL_HANDLE);
                if (includesGlobalBindless && denseSetCount > 0)
                {
                    setLayouts[0] = m_bindlessLayout;
                }

                for (u32 s = 0; s < denseSetCount; ++s)
                {
                    if (includesGlobalBindless && s == 0)
                    {
                        continue;
                    }

                    const DescriptorSetLayoutDesc* matchingSet = nullptr;
                    for (const auto& setDesc : normalized.Sets)
                    {
                        if (setDesc.SetIndex == s)
                        {
                            matchingSet = &setDesc;
                            break;
                        }
                    }

                    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
                    std::vector<VkDescriptorBindingFlags> vkBindingFlags;
                    bool needsUpdateAfterBindPool = false;

                    if (matchingSet)
                    {
                        vkBindings.reserve(matchingSet->Bindings.size());
                        vkBindingFlags.reserve(matchingSet->Bindings.size());
                        for (const auto& b : matchingSet->Bindings)
                        {
                            VkDescriptorSetLayoutBinding vkB{};
                            vkB.binding = b.Binding;
                            vkB.descriptorType = ToVkDescriptorType(b.Type);
                            vkB.descriptorCount = (b.Count > 0) ? b.Count : kMaxBindlessResources;
                            vkB.stageFlags = ToVkShaderStageFlags(b.Visibility);

                            for (const auto& ss : builtStaticSamplers)
                            {
                                if (ss.SetIndex == s && ss.Binding == b.Binding && b.Type == DescriptorType::Sampler && vkB.descriptorCount == 1)
                                {
                                    vkB.pImmutableSamplers = &ss.Sampler;
                                    vkB.stageFlags = ToVkShaderStageFlags(b.Visibility | ss.Visibility);
                                    break;
                                }
                            }

                            VkDescriptorBindingFlags bFlags = 0;
                            if (b.IsUnboundedArray || matchingSet->AllowUpdateAfterBind || b.Count == 0)
                            {
                                if (m_enabledVk12Features.descriptorBindingPartiallyBound == VK_TRUE)
                                {
                                    bFlags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                                }
                                if ((b.IsUnboundedArray || b.Count == 0) && m_enabledVk12Features.descriptorBindingVariableDescriptorCount == VK_TRUE)
                                {
                                    bFlags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                                }
                                if (matchingSet->AllowUpdateAfterBind && m_limits.SupportsUpdateAfterBind)
                                {
                                    bFlags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
                                    needsUpdateAfterBindPool = true;
                                }
                            }

                            vkBindings.push_back(vkB);
                            vkBindingFlags.push_back(bFlags);
                        }
                    }

                    for (const auto& ss : builtStaticSamplers)
                    {
                        if (ss.SetIndex != s)
                        {
                            continue;
                        }
                        const bool alreadyAdded = matchingSet && std::any_of(
                            matchingSet->Bindings.begin(),
                            matchingSet->Bindings.end(),
                            [&](const PipelineBindingLayout& b) { return b.Binding == ss.Binding; });
                        if (!alreadyAdded)
                        {
                            VkDescriptorSetLayoutBinding ssB{};
                            ssB.binding = ss.Binding;
                            ssB.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                            ssB.descriptorCount = 1;
                            ssB.stageFlags = ToVkShaderStageFlags(ss.Visibility);
                            ssB.pImmutableSamplers = &ss.Sampler;
                            vkBindings.push_back(ssB);
                            vkBindingFlags.push_back(0);
                        }
                    }

                    if (!vkBindings.empty())
                    {
                        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
                        flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                        flagsInfo.bindingCount = static_cast<u32>(vkBindingFlags.size());
                        flagsInfo.pBindingFlags = vkBindingFlags.data();

                        const bool hasAnyFlags = std::any_of(
                            vkBindingFlags.begin(),
                            vkBindingFlags.end(),
                            [](VkDescriptorBindingFlags f) { return f != 0; });

                        VkDescriptorSetLayoutCreateInfo setInfo{};
                        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                        setInfo.pNext = hasAnyFlags ? &flagsInfo : nullptr;
                        setInfo.flags = needsUpdateAfterBindPool ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT : 0u;
                        setInfo.bindingCount = static_cast<u32>(vkBindings.size());
                        setInfo.pBindings = vkBindings.data();

                        VkDescriptorSetLayout vkSetLayout = VK_NULL_HANDLE;
                        if (m_devDisp.CreateDescriptorSetLayout(m_device, &setInfo, nullptr, &vkSetLayout) != VK_SUCCESS ||
                            vkSetLayout == VK_NULL_HANDLE)
                        {
                            cleanupNative();
                            return Core::Result<PipelineLayoutHandle, std::string>::Failure(
                                "vkCreateDescriptorSetLayout failed for set index " + std::to_string(s) + ".");
                        }
                        ownedSetLayouts.push_back(vkSetLayout);
                        setLayouts[s] = vkSetLayout;
                    }
                }

                VkDescriptorSetLayout emptySetLayout = VK_NULL_HANDLE;
                for (u32 s = 0; s < denseSetCount; ++s)
                {
                    if (setLayouts[s] == VK_NULL_HANDLE)
                    {
                        if (emptySetLayout == VK_NULL_HANDLE)
                        {
                            VkDescriptorSetLayoutCreateInfo emptyInfo{};
                            emptyInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                            if (m_devDisp.CreateDescriptorSetLayout(m_device, &emptyInfo, nullptr, &emptySetLayout) != VK_SUCCESS ||
                                emptySetLayout == VK_NULL_HANDLE)
                            {
                                cleanupNative();
                                return Core::Result<PipelineLayoutHandle, std::string>::Failure(
                                    "vkCreateDescriptorSetLayout failed for empty placeholder set.");
                            }
                            ownedSetLayouts.push_back(emptySetLayout);
                        }
                        setLayouts[s] = emptySetLayout;
                    }
                }

                std::vector<VkPushConstantRange> pushRanges;
                pushRanges.reserve(normalized.PushConstants.size());
                VkShaderStageFlags pushStageMask = 0;
                u32 maxPushBytes = 0;
                for (const auto& pc : normalized.PushConstants)
                {
                    VkPushConstantRange r{};
                    r.stageFlags = ToVkShaderStageFlags(pc.Visibility);
                    r.offset = pc.OffsetBytes;
                    r.size = pc.SizeBytes;
                    pushStageMask |= r.stageFlags;
                    maxPushBytes = std::max(maxPushBytes, pc.OffsetBytes + pc.SizeBytes);
                    pushRanges.push_back(r);
                }

                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
                layoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
                layoutInfo.pushConstantRangeCount = static_cast<u32>(pushRanges.size());
                layoutInfo.pPushConstantRanges = pushRanges.empty() ? nullptr : pushRanges.data();

                VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
                if (m_devDisp.CreatePipelineLayout(m_device, &layoutInfo, nullptr, &vkPipelineLayout) != VK_SUCCESS ||
                    vkPipelineLayout == VK_NULL_HANDLE)
                {
                    cleanupNative();
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure(
                        "vkCreatePipelineLayout failed.");
                }

                if (!normalized.DebugName.empty())
                {
                    m_debug.SetObjectName(
                        m_device,
                        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        reinterpret_cast<u64>(vkPipelineLayout),
                        normalized.DebugName);
                }

                std::lock_guard<std::mutex> lock(m_resourceMutex);
                if (auto cacheIt = m_pipelineLayoutCache.find(canonicalHash); cacheIt != m_pipelineLayoutCache.end())
                {
                    for (PipelineLayoutHandle candidate : cacheIt->second)
                    {
                        if (auto* rec = m_pipelineLayouts.Get(candidate);
                            rec && !rec->PendingDestroy && rec->PipelineLayout != VK_NULL_HANDLE &&
                            PipelineLayoutValidator::AreStructurallyEqual(rec->Desc, normalized))
                        {
                            if (m_devDisp.DestroyPipelineLayout)
                            {
                                m_devDisp.DestroyPipelineLayout(m_device, vkPipelineLayout, nullptr);
                            }
                            cleanupNative();
                            ++rec->CacheRefCount;
                            ++m_layoutStats.CacheHitCount;
                            return Core::Result<PipelineLayoutHandle, std::string>::Success(candidate);
                        }
                    }
                }

                PipelineLayoutHandle handle = m_pipelineLayouts.Emplace(VulkanPipelineLayoutRecord{
                    .Desc = normalized,
                    .CanonicalHash = canonicalHash,
                    .PipelineLayout = vkPipelineLayout,
                    .OwnedSetLayouts = std::move(ownedSetLayouts),
                    .OwnedSamplers = std::move(ownedSamplers),
                    .PushConstantStageMask = pushStageMask,
                    .MaxPushConstantBytes = maxPushBytes,
                    .IncludesGlobalBindlessSet = includesGlobalBindless,
                    .IsDefaultLayout = false,
                    .CacheRefCount = 1,
                    .PipelineRefCount = 0,
                    .UnsubmittedRefCount = 0,
                    .LastUsedTimelineValue = 0,
                    .PendingDestroy = false
                });
                m_pipelineLayoutCache[canonicalHash].push_back(handle);
                ++m_layoutStats.CreatedCount;
                ++m_layoutStats.ActiveCount;
                return Core::Result<PipelineLayoutHandle, std::string>::Success(handle);
            }

            void DestroyPipelineLayout(PipelineLayoutHandle handle) override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_pipelineLayouts.Get(handle);
                if (!rec || rec->IsDefaultLayout || rec->PendingDestroy)
                {
                    return;
                }
                if (rec->CacheRefCount > 1)
                {
                    --rec->CacheRefCount;
                    return;
                }
                rec->CacheRefCount = 0;
                rec->PendingDestroy = true;

                if (auto cacheIt = m_pipelineLayoutCache.find(rec->CanonicalHash); cacheIt != m_pipelineLayoutCache.end())
                {
                    std::erase(cacheIt->second, handle);
                    if (cacheIt->second.empty())
                    {
                        m_pipelineLayoutCache.erase(cacheIt);
                    }
                }

                const u64 minSafeTimeline = m_frameOpen ? (m_nextTimelineValue + 1) : m_nextTimelineValue;
                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::PipelineLayout,
                    .CompletionTimelineValue = std::max(rec->LastUsedTimelineValue, minSafeTimeline),
                    .PipelineLayoutHandle = handle
                });
            }

            void DestroyPipelineLayoutInternalLocked(PipelineLayoutHandle handle)
            {
                if (auto* rec = m_pipelineLayouts.Get(handle))
                {
                    if (auto cacheIt = m_pipelineLayoutCache.find(rec->CanonicalHash); cacheIt != m_pipelineLayoutCache.end())
                    {
                        std::erase(cacheIt->second, handle);
                        if (cacheIt->second.empty())
                        {
                            m_pipelineLayoutCache.erase(cacheIt);
                        }
                    }
                    if (!rec->IsDefaultLayout && rec->PipelineLayout != VK_NULL_HANDLE && m_devDisp.DestroyPipelineLayout)
                    {
                        m_devDisp.DestroyPipelineLayout(m_device, rec->PipelineLayout, nullptr);
                        rec->PipelineLayout = VK_NULL_HANDLE;
                    }
                    for (VkDescriptorSetLayout setLayout : rec->OwnedSetLayouts)
                    {
                        if (setLayout != VK_NULL_HANDLE && setLayout != m_bindlessLayout && m_devDisp.DestroyDescriptorSetLayout)
                        {
                            m_devDisp.DestroyDescriptorSetLayout(m_device, setLayout, nullptr);
                        }
                    }
                    rec->OwnedSetLayouts.clear();
                    for (VkSampler sampler : rec->OwnedSamplers)
                    {
                        if (sampler != VK_NULL_HANDLE && m_devDisp.DestroySampler)
                        {
                            m_devDisp.DestroySampler(m_device, sampler, nullptr);
                        }
                    }
                    rec->OwnedSamplers.clear();
                    m_pipelineLayouts.Remove(handle);
                    ++m_layoutStats.DestroyedCount;
                    if (m_layoutStats.ActiveCount > 0)
                    {
                        --m_layoutStats.ActiveCount;
                    }
                }
            }

            bool IsResourceAlive(PipelineLayoutHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_pipelineLayouts.Get(handle);
                return rec != nullptr && rec->PipelineLayout != VK_NULL_HANDLE;
            }

            PipelineLayoutHandle GetDefaultBindlessPipelineLayout() const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_defaultBindlessPipelineLayout;
            }

            PipelineLayoutLimits GetPipelineLayoutLimits() const noexcept override
            {
                return m_limits;
            }

            PipelineLayoutStats GetPipelineLayoutStats() const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_layoutStats;
            }

            PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override
            {
                if (desc.VertexShaderBytecode.empty() || desc.PixelShaderBytecode.empty())
                {
                    return PipelineHandle::Invalid();
                }

                VkShaderModuleCreateInfo vsModInfo{};
                vsModInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                vsModInfo.codeSize = desc.VertexShaderBytecode.size();
                vsModInfo.pCode = reinterpret_cast<const u32*>(desc.VertexShaderBytecode.data());

                VkShaderModule vsMod = VK_NULL_HANDLE;
                if (m_devDisp.CreateShaderModule(m_device, &vsModInfo, nullptr, &vsMod) != VK_SUCCESS)
                {
                    return PipelineHandle::Invalid();
                }

                VkShaderModuleCreateInfo psModInfo{};
                psModInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                psModInfo.codeSize = desc.PixelShaderBytecode.size();
                psModInfo.pCode = reinterpret_cast<const u32*>(desc.PixelShaderBytecode.data());

                VkShaderModule psMod = VK_NULL_HANDLE;
                if (m_devDisp.CreateShaderModule(m_device, &psModInfo, nullptr, &psMod) != VK_SUCCESS)
                {
                    m_devDisp.DestroyShaderModule(m_device, vsMod, nullptr);
                    return PipelineHandle::Invalid();
                }

                std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
                stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vsMod;
                stages[0].pName = "VSMain";

                stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = psMod;
                stages[1].pName = "PSMain";

                VkVertexInputBindingDescription vertexBinding{};
                vertexBinding.binding = 0;
                vertexBinding.stride = desc.VertexStrideBytes;
                vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                const std::array<VkVertexInputAttributeDescription, 5> vertexAttrs = {{
                    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,     0 },
                    { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 },
                    { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 },
                    { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
                    { 4, 0, VK_FORMAT_R32G32B32_SFLOAT,    48 }
                }};

                const std::array<VkVertexInputAttributeDescription, 7> vertexAttrsSkinned = {{
                    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,     0 },
                    { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 },
                    { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 },
                    { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
                    { 4, 0, VK_FORMAT_R32G32B32_SFLOAT,    48 },
                    { 5, 0, VK_FORMAT_R32G32B32A32_SINT,   60 },
                    { 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 76 }
                }};

                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                if (desc.VertexStrideBytes == 60)
                {
                    vertexInput.vertexBindingDescriptionCount = 1;
                    vertexInput.pVertexBindingDescriptions = &vertexBinding;
                    vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(vertexAttrs.size());
                    vertexInput.pVertexAttributeDescriptions = vertexAttrs.data();
                }
                else if (desc.VertexStrideBytes == 92)
                {
                    vertexInput.vertexBindingDescriptionCount = 1;
                    vertexInput.pVertexBindingDescriptions = &vertexBinding;
                    vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(vertexAttrsSkinned.size());
                    vertexInput.pVertexAttributeDescriptions = vertexAttrsSkinned.data();
                }

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                VkPipelineRasterizationStateCreateInfo raster{};
                raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                raster.polygonMode = desc.Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
                raster.lineWidth = 1.0f;

                VkPipelineMultisampleStateCreateInfo msaa{};
                msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = desc.DepthTestEnable ? VK_TRUE : VK_FALSE;
                depthStencil.depthWriteEnable = desc.DepthWriteEnable ? VK_TRUE : VK_FALSE;
                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

                VkPipelineColorBlendAttachmentState blendAtt{};
                blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                if (desc.AlphaBlendEnable)
                {
                    blendAtt.blendEnable = VK_TRUE;
                    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
                    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
                }
                else
                {
                    blendAtt.blendEnable = VK_FALSE;
                }

                VkPipelineColorBlendStateCreateInfo blend{};
                blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                blend.attachmentCount = 1;
                blend.pAttachments = &blendAtt;

                const std::array<VkDynamicState, 2> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
                VkPipelineDynamicStateCreateInfo dynInfo{};
                dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynInfo.dynamicStateCount = static_cast<u32>(dynStates.size());
                dynInfo.pDynamicStates = dynStates.data();

                const VkFormat colorFormat = ToVkFormat(desc.ColorTargetFormat);
                VkPipelineRenderingCreateInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &colorFormat;
                if (desc.DepthTargetFormat != Format::Unknown)
                {
                    renderingInfo.depthAttachmentFormat = ToVkFormat(desc.DepthTargetFormat);
                }

                PipelineLayoutHandle resolvedLayoutHandle = m_defaultBindlessPipelineLayout;
                VkPipelineLayout resolvedVkLayout = m_pipelineLayout;
                VkShaderStageFlags resolvedPushMask = VK_SHADER_STAGE_ALL;
                u32 resolvedPushMaxBytes = 128;
                bool resolvedIncludesBindless = true;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (desc.PipelineLayout.IsValid())
                    {
                        auto* layoutRec = m_pipelineLayouts.Get(desc.PipelineLayout);
                        if (!layoutRec || layoutRec->PendingDestroy || layoutRec->PipelineLayout == VK_NULL_HANDLE)
                        {
                            m_devDisp.DestroyShaderModule(m_device, vsMod, nullptr);
                            m_devDisp.DestroyShaderModule(m_device, psMod, nullptr);
                            Core::Log::Error("RHI.Vulkan", "CreateGraphicsPipeline failed: invalid or destroyed PipelineLayoutHandle.");
                            return PipelineHandle::Invalid();
                        }
                        resolvedLayoutHandle = desc.PipelineLayout;
                        resolvedVkLayout = layoutRec->PipelineLayout;
                        resolvedPushMask = layoutRec->PushConstantStageMask;
                        resolvedPushMaxBytes = layoutRec->MaxPushConstantBytes;
                        resolvedIncludesBindless = layoutRec->IncludesGlobalBindlessSet;
                        ++layoutRec->PipelineRefCount;
                    }
                    else if (resolvedLayoutHandle.IsValid())
                    {
                        if (auto* defaultRec = m_pipelineLayouts.Get(resolvedLayoutHandle))
                        {
                            resolvedVkLayout = defaultRec->PipelineLayout;
                            resolvedPushMask = defaultRec->PushConstantStageMask;
                            resolvedPushMaxBytes = defaultRec->MaxPushConstantBytes;
                            resolvedIncludesBindless = defaultRec->IncludesGlobalBindlessSet;
                            ++defaultRec->PipelineRefCount;
                        }
                    }
                }

                VkGraphicsPipelineCreateInfo pipeInfo{};
                pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeInfo.pNext = &renderingInfo;
                pipeInfo.stageCount = static_cast<u32>(stages.size());
                pipeInfo.pStages = stages.data();
                pipeInfo.pVertexInputState = &vertexInput;
                pipeInfo.pInputAssemblyState = &inputAssembly;
                pipeInfo.pViewportState = &viewportState;
                pipeInfo.pRasterizationState = &raster;
                pipeInfo.pMultisampleState = &msaa;
                pipeInfo.pDepthStencilState = &depthStencil;
                pipeInfo.pColorBlendState = &blend;
                pipeInfo.pDynamicState = &dynInfo;
                pipeInfo.layout = resolvedVkLayout;

                VkPipeline vkPipe = VK_NULL_HANDLE;
                const VkResult res = m_devDisp.CreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &vkPipe);

                m_devDisp.DestroyShaderModule(m_device, vsMod, nullptr);
                m_devDisp.DestroyShaderModule(m_device, psMod, nullptr);

                if (res != VK_SUCCESS)
                {
                    if (resolvedLayoutHandle.IsValid())
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        if (auto* layoutRec = m_pipelineLayouts.Get(resolvedLayoutHandle); layoutRec && layoutRec->PipelineRefCount > 0)
                        {
                            --layoutRec->PipelineRefCount;
                        }
                    }
                    VulkanDebug::CheckVkResult(res, "vkCreateGraphicsPipelines");
                    return PipelineHandle::Invalid();
                }

                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(vkPipe), desc.DebugName);

                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_pipelines.Emplace(VulkanPipelineRecord{
                    .Pipeline = vkPipe,
                    .Layout = resolvedLayoutHandle,
                    .NativeLayout = resolvedVkLayout,
                    .PushConstantStageMask = resolvedPushMask,
                    .PushConstantMaxBytes = resolvedPushMaxBytes,
                    .IncludesGlobalBindlessSet = resolvedIncludesBindless,
                    .IsCompute = false
                });
            }

            PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override
            {
                if (desc.ComputeShaderBytecode.empty())
                {
                    return PipelineHandle::Invalid();
                }

                VkShaderModuleCreateInfo csModInfo{};
                csModInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                csModInfo.codeSize = desc.ComputeShaderBytecode.size();
                csModInfo.pCode = reinterpret_cast<const u32*>(desc.ComputeShaderBytecode.data());

                VkShaderModule csMod = VK_NULL_HANDLE;
                if (m_devDisp.CreateShaderModule(m_device, &csModInfo, nullptr, &csMod) != VK_SUCCESS)
                {
                    return PipelineHandle::Invalid();
                }

                PipelineLayoutHandle resolvedLayoutHandle = m_defaultBindlessPipelineLayout;
                VkPipelineLayout resolvedVkLayout = m_pipelineLayout;
                VkShaderStageFlags resolvedPushMask = VK_SHADER_STAGE_ALL;
                u32 resolvedPushMaxBytes = 128;
                bool resolvedIncludesBindless = true;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (desc.PipelineLayout.IsValid())
                    {
                        auto* layoutRec = m_pipelineLayouts.Get(desc.PipelineLayout);
                        if (!layoutRec || layoutRec->PendingDestroy || layoutRec->PipelineLayout == VK_NULL_HANDLE)
                        {
                            m_devDisp.DestroyShaderModule(m_device, csMod, nullptr);
                            Core::Log::Error("RHI.Vulkan", "CreateComputePipeline failed: invalid or destroyed PipelineLayoutHandle.");
                            return PipelineHandle::Invalid();
                        }
                        resolvedLayoutHandle = desc.PipelineLayout;
                        resolvedVkLayout = layoutRec->PipelineLayout;
                        resolvedPushMask = layoutRec->PushConstantStageMask;
                        resolvedPushMaxBytes = layoutRec->MaxPushConstantBytes;
                        resolvedIncludesBindless = layoutRec->IncludesGlobalBindlessSet;
                        ++layoutRec->PipelineRefCount;
                    }
                    else if (resolvedLayoutHandle.IsValid())
                    {
                        if (auto* defaultRec = m_pipelineLayouts.Get(resolvedLayoutHandle))
                        {
                            resolvedVkLayout = defaultRec->PipelineLayout;
                            resolvedPushMask = defaultRec->PushConstantStageMask;
                            resolvedPushMaxBytes = defaultRec->MaxPushConstantBytes;
                            resolvedIncludesBindless = defaultRec->IncludesGlobalBindlessSet;
                            ++defaultRec->PipelineRefCount;
                        }
                    }
                }

                VkComputePipelineCreateInfo pipeInfo{};
                pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                pipeInfo.stage.module = csMod;
                pipeInfo.stage.pName = "CSMain";
                pipeInfo.layout = resolvedVkLayout;

                VkPipeline vkPipe = VK_NULL_HANDLE;
                const VkResult res = m_devDisp.CreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &vkPipe);
                m_devDisp.DestroyShaderModule(m_device, csMod, nullptr);

                if (res != VK_SUCCESS)
                {
                    if (resolvedLayoutHandle.IsValid())
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        if (auto* layoutRec = m_pipelineLayouts.Get(resolvedLayoutHandle); layoutRec && layoutRec->PipelineRefCount > 0)
                        {
                            --layoutRec->PipelineRefCount;
                        }
                    }
                    VulkanDebug::CheckVkResult(res, "vkCreateComputePipelines");
                    return PipelineHandle::Invalid();
                }

                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(vkPipe), desc.DebugName);

                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_pipelines.Emplace(VulkanPipelineRecord{
                    .Pipeline = vkPipe,
                    .Layout = resolvedLayoutHandle,
                    .NativeLayout = resolvedVkLayout,
                    .PushConstantStageMask = resolvedPushMask,
                    .PushConstantMaxBytes = resolvedPushMaxBytes,
                    .IncludesGlobalBindlessSet = resolvedIncludesBindless,
                    .IsCompute = true
                });
            }

            void DestroyPipeline(PipelineHandle handle) override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_pipelines.Get(handle);
                if (!rec || rec->PendingDestroy)
                {
                    return;
                }
                rec->PendingDestroy = true;

                const u64 minSafeTimeline = m_frameOpen ? (m_nextTimelineValue + 1) : m_nextTimelineValue;
                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::Pipeline,
                    .CompletionTimelineValue = std::max(rec->LastUsedTimelineValue, minSafeTimeline),
                    .PipelineHandle = handle
                });
            }

            void DestroyPipelineInternalLocked(PipelineHandle handle)
            {
                if (auto* rec = m_pipelines.Get(handle))
                {
                    if (rec->Layout.IsValid())
                    {
                        if (auto* layoutRec = m_pipelineLayouts.Get(rec->Layout))
                        {
                            layoutRec->LastUsedTimelineValue = std::max(layoutRec->LastUsedTimelineValue, rec->LastUsedTimelineValue);
                            if (layoutRec->PipelineRefCount > 0)
                            {
                                --layoutRec->PipelineRefCount;
                            }
                        }
                    }
                    if (rec->Pipeline != VK_NULL_HANDLE)
                    {
                        m_devDisp.DestroyPipeline(m_device, rec->Pipeline, nullptr);
                        rec->Pipeline = VK_NULL_HANDLE;
                    }
                    m_pipelines.Remove(handle);
                }
            }

            bool IsResourceAlive(PipelineHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_pipelines.Get(handle);
                return rec != nullptr && rec->Pipeline != VK_NULL_HANDLE;
            }

            bool AddUnsubmittedBufferRef(BufferHandle handle)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_buffers.Get(handle);
                if (!rec || rec->PendingDestroy || rec->Buffer == VK_NULL_HANDLE)
                {
                    return false;
                }
                ++rec->UnsubmittedRefCount;
                return true;
            }

            bool AddUnsubmittedTextureRef(TextureHandle handle)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_textures.Get(handle);
                if (!rec || rec->PendingDestroy || (rec->Image == VK_NULL_HANDLE && rec->View == VK_NULL_HANDLE))
                {
                    return false;
                }
                ++rec->UnsubmittedRefCount;
                return true;
            }

            bool AddUnsubmittedPipelineRef(PipelineHandle handle)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_pipelines.Get(handle);
                if (!rec || rec->PendingDestroy || rec->Pipeline == VK_NULL_HANDLE)
                {
                    return false;
                }
                ++rec->UnsubmittedRefCount;
                return true;
            }

            bool AddUnsubmittedPipelineLayoutRef(PipelineLayoutHandle handle)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_pipelineLayouts.Get(handle);
                if (!rec || rec->PendingDestroy || rec->PipelineLayout == VK_NULL_HANDLE)
                {
                    return false;
                }
                ++rec->UnsubmittedRefCount;
                return true;
            }

            void ReleaseUnsubmittedRefs(
                const std::unordered_set<BufferHandle>& buffers,
                const std::unordered_set<TextureHandle>& textures,
                const std::unordered_set<PipelineHandle>& pipelines,
                const std::unordered_set<PipelineLayoutHandle>& layouts)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                for (BufferHandle h : buffers)
                {
                    if (auto* rec = m_buffers.Get(h))
                    {
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                for (TextureHandle h : textures)
                {
                    if (auto* rec = m_textures.Get(h))
                    {
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                for (PipelineHandle h : pipelines)
                {
                    if (auto* rec = m_pipelines.Get(h))
                    {
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                for (PipelineLayoutHandle h : layouts)
                {
                    if (auto* rec = m_pipelineLayouts.Get(h))
                    {
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
            }

            u64 CommitCommandListSubmission(
                bool isActiveCmdList,
                const std::unordered_set<BufferHandle>& buffers,
                const std::unordered_set<TextureHandle>& textures,
                const std::unordered_set<PipelineHandle>& pipelines,
                const std::unordered_set<PipelineLayoutHandle>& layouts)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                if (isActiveCmdList)
                {
                    m_frameOpen = false;
                }
                const u64 submissionTimelineValue = ++m_nextTimelineValue;
                for (BufferHandle h : buffers)
                {
                    if (auto* rec = m_buffers.Get(h))
                    {
                        rec->LastUsedTimelineValue = std::max(rec->LastUsedTimelineValue, submissionTimelineValue);
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                for (TextureHandle h : textures)
                {
                    if (auto* rec = m_textures.Get(h))
                    {
                        rec->LastUsedTimelineValue = std::max(rec->LastUsedTimelineValue, submissionTimelineValue);
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                for (PipelineHandle h : pipelines)
                {
                    if (auto* rec = m_pipelines.Get(h))
                    {
                        rec->LastUsedTimelineValue = std::max(rec->LastUsedTimelineValue, submissionTimelineValue);
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                for (PipelineLayoutHandle h : layouts)
                {
                    if (auto* rec = m_pipelineLayouts.Get(h))
                    {
                        rec->LastUsedTimelineValue = std::max(rec->LastUsedTimelineValue, submissionTimelineValue);
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                return submissionTimelineValue;
            }

            void UnregisterCommandList(VulkanCommandList* cmdList)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                std::erase(m_createdCommandLists, cmdList);
            }

            void DestroyCommandPool(VkCommandPool pool)
            {
                if (m_device != VK_NULL_HANDLE && pool != VK_NULL_HANDLE && m_devDisp.DestroyCommandPool)
                {
                    m_devDisp.DestroyCommandPool(m_device, pool, nullptr);
                }
            }

            struct BufferSnapshot
            {
                VkBuffer Buffer = VK_NULL_HANDLE;
                u64 SizeBytes = 0;
                BindlessResourceIndex Bindless{};
            };

            struct TextureSnapshot
            {
                VkImage Image = VK_NULL_HANDLE;
                VkImageView View = VK_NULL_HANDLE;
                u32 Width = 0;
                u32 Height = 0;
                u32 MipLevels = 1;
                u32 DepthOrArrayLayers = 1;
                bool DepthStencil = false;
            };

            struct PipelineSnapshot
            {
                VkPipeline Pipeline = VK_NULL_HANDLE;
                PipelineLayoutHandle Layout = PipelineLayoutHandle::Invalid();
                VkPipelineLayout NativeLayout = VK_NULL_HANDLE;
                VkShaderStageFlags PushConstantStageMask = VK_SHADER_STAGE_ALL;
                u32 PushConstantMaxBytes = 128;
                bool IncludesGlobalBindlessSet = true;
                bool IsCompute = false;
            };

            [[nodiscard]] std::optional<BufferSnapshot> TryGetBufferSnapshot(BufferHandle h) const noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_buffers.Get(h);
                if (!rec || rec->Buffer == VK_NULL_HANDLE)
                {
                    return std::nullopt;
                }
                return BufferSnapshot{ rec->Buffer, rec->Desc.SizeBytes, rec->Bindless };
            }

            [[nodiscard]] std::optional<TextureSnapshot> TryGetTextureSnapshot(TextureHandle h) const noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_textures.Get(h);
                if (!rec)
                {
                    return std::nullopt;
                }
                return TextureSnapshot{ rec->Image, rec->View, rec->Desc.Width, rec->Desc.Height, rec->Desc.MipLevels, rec->Desc.DepthOrArrayLayers, rec->Desc.DepthStencil };
            }

            [[nodiscard]] std::optional<PipelineSnapshot> TryGetPipelineSnapshot(PipelineHandle h) const noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_pipelines.Get(h);
                if (!rec || rec->Pipeline == VK_NULL_HANDLE)
                {
                    return std::nullopt;
                }
                return PipelineSnapshot{
                    rec->Pipeline,
                    rec->Layout,
                    rec->NativeLayout,
                    rec->PushConstantStageMask,
                    rec->PushConstantMaxBytes,
                    rec->IncludesGlobalBindlessSet,
                    rec->IsCompute
                };
            }
            [[nodiscard]] const VulkanDebug& Debug() const noexcept { return m_debug; }
            void UpdateDescriptorSets(u32 count, const VkWriteDescriptorSet* writes) { m_devDisp.UpdateDescriptorSets(m_device, count, writes, 0, nullptr); }

        private:
            struct FrameContext
            {
                VkCommandPool CmdPool = VK_NULL_HANDLE;
                VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
                VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;
                VkSemaphore RenderFinishedSemaphore = VK_NULL_HANDLE;
                VkFence InFlightFence = VK_NULL_HANDLE;
                u64 TargetTimelineValue = 0;
            };

            void LoadInstanceDispatch()
            {
                auto load = [&](const char* name) {
                    return m_globalDisp.GetInstanceProcAddr(m_instance, name);
                };
                m_instDisp.DestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(load("vkDestroyInstance"));
                m_instDisp.EnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(load("vkEnumeratePhysicalDevices"));
                m_instDisp.GetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(load("vkGetPhysicalDeviceProperties"));
                m_instDisp.GetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(load("vkGetPhysicalDeviceFeatures2"));
                m_instDisp.GetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(load("vkGetPhysicalDeviceQueueFamilyProperties"));
                m_instDisp.GetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(load("vkGetPhysicalDeviceMemoryProperties"));
                m_instDisp.CreateDevice = reinterpret_cast<PFN_vkCreateDevice>(load("vkCreateDevice"));
                m_instDisp.GetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(load("vkGetDeviceProcAddr"));
                m_instDisp.DestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(load("vkDestroySurfaceKHR"));
                m_instDisp.GetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(load("vkGetPhysicalDeviceSurfaceSupportKHR"));
                m_instDisp.GetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(load("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
                m_instDisp.GetPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(load("vkGetPhysicalDeviceSurfaceFormatsKHR"));
                m_instDisp.GetPhysicalDeviceSurfacePresentModesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(load("vkGetPhysicalDeviceSurfacePresentModesKHR"));
#if defined(VK_USE_PLATFORM_WIN32_KHR)
                m_instDisp.CreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(load("vkCreateWin32SurfaceKHR"));
#endif
            }

            void LoadDeviceDispatch()
            {
                auto load = [&](const char* name) {
                    return m_instDisp.GetDeviceProcAddr(m_device, name);
                };
                m_devDisp.DestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(load("vkDestroyDevice"));
                m_devDisp.GetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(load("vkGetDeviceQueue"));
                m_devDisp.DeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(load("vkDeviceWaitIdle"));
                m_devDisp.QueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(load("vkQueueSubmit"));
                m_devDisp.QueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(load("vkQueueWaitIdle"));
                m_devDisp.CreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(load("vkCreateSwapchainKHR"));
                m_devDisp.DestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(load("vkDestroySwapchainKHR"));
                m_devDisp.GetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(load("vkGetSwapchainImagesKHR"));
                m_devDisp.AcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(load("vkAcquireNextImageKHR"));
                m_devDisp.QueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(load("vkQueuePresentKHR"));
                m_devDisp.CreateImageView = reinterpret_cast<PFN_vkCreateImageView>(load("vkCreateImageView"));
                m_devDisp.DestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(load("vkDestroyImageView"));
                m_devDisp.CreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(load("vkCreateCommandPool"));
                m_devDisp.DestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(load("vkDestroyCommandPool"));
                m_devDisp.ResetCommandPool = reinterpret_cast<PFN_vkResetCommandPool>(load("vkResetCommandPool"));
                m_devDisp.AllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(load("vkAllocateCommandBuffers"));
                m_devDisp.BeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(load("vkBeginCommandBuffer"));
                m_devDisp.EndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(load("vkEndCommandBuffer"));
                m_devDisp.ResetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(load("vkResetCommandBuffer"));
                m_devDisp.CmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(load("vkCmdPipelineBarrier"));
                m_devDisp.CmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(load("vkCmdBeginRendering"));
                m_devDisp.CmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(load("vkCmdEndRendering"));
                m_devDisp.CmdSetViewport = reinterpret_cast<PFN_vkCmdSetViewport>(load("vkCmdSetViewport"));
                m_devDisp.CmdSetScissor = reinterpret_cast<PFN_vkCmdSetScissor>(load("vkCmdSetScissor"));
                m_devDisp.CmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(load("vkCmdBindPipeline"));
                m_devDisp.CmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(load("vkCmdBindDescriptorSets"));
                m_devDisp.CmdPushConstants = reinterpret_cast<PFN_vkCmdPushConstants>(load("vkCmdPushConstants"));
                m_devDisp.CmdBindVertexBuffers = reinterpret_cast<PFN_vkCmdBindVertexBuffers>(load("vkCmdBindVertexBuffers"));
                m_devDisp.CmdBindIndexBuffer = reinterpret_cast<PFN_vkCmdBindIndexBuffer>(load("vkCmdBindIndexBuffer"));
                m_devDisp.CmdDraw = reinterpret_cast<PFN_vkCmdDraw>(load("vkCmdDraw"));
                m_devDisp.CmdDrawIndexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(load("vkCmdDrawIndexed"));
                m_devDisp.CmdDispatch = reinterpret_cast<PFN_vkCmdDispatch>(load("vkCmdDispatch"));
                m_devDisp.CmdCopyBufferToImage = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(load("vkCmdCopyBufferToImage"));
                m_devDisp.CreateSampler = reinterpret_cast<PFN_vkCreateSampler>(load("vkCreateSampler"));
                m_devDisp.DestroySampler = reinterpret_cast<PFN_vkDestroySampler>(load("vkDestroySampler"));
                m_devDisp.CreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(load("vkCreateSemaphore"));
                m_devDisp.DestroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(load("vkDestroySemaphore"));
                m_devDisp.WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(load("vkWaitSemaphores"));
                m_devDisp.SignalSemaphore = reinterpret_cast<PFN_vkSignalSemaphore>(load("vkSignalSemaphore"));
                m_devDisp.GetSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(load("vkGetSemaphoreCounterValue"));
                m_devDisp.CreateFence = reinterpret_cast<PFN_vkCreateFence>(load("vkCreateFence"));
                m_devDisp.DestroyFence = reinterpret_cast<PFN_vkDestroyFence>(load("vkDestroyFence"));
                m_devDisp.WaitForFences = reinterpret_cast<PFN_vkWaitForFences>(load("vkWaitForFences"));
                m_devDisp.ResetFences = reinterpret_cast<PFN_vkResetFences>(load("vkResetFences"));
                m_devDisp.CreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(load("vkCreateBuffer"));
                m_devDisp.DestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(load("vkDestroyBuffer"));
                m_devDisp.GetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(load("vkGetBufferMemoryRequirements"));
                m_devDisp.BindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(load("vkBindBufferMemory"));
                m_devDisp.CreateImage = reinterpret_cast<PFN_vkCreateImage>(load("vkCreateImage"));
                m_devDisp.DestroyImage = reinterpret_cast<PFN_vkDestroyImage>(load("vkDestroyImage"));
                m_devDisp.GetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(load("vkGetImageMemoryRequirements"));
                m_devDisp.BindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(load("vkBindImageMemory"));
                m_devDisp.AllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(load("vkAllocateMemory"));
                m_devDisp.FreeMemory = reinterpret_cast<PFN_vkFreeMemory>(load("vkFreeMemory"));
                m_devDisp.MapMemory = reinterpret_cast<PFN_vkMapMemory>(load("vkMapMemory"));
                m_devDisp.UnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(load("vkUnmapMemory"));
                m_devDisp.CreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(load("vkCreateShaderModule"));
                m_devDisp.DestroyShaderModule = reinterpret_cast<PFN_vkDestroyShaderModule>(load("vkDestroyShaderModule"));
                m_devDisp.CreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(load("vkCreateDescriptorSetLayout"));
                m_devDisp.DestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(load("vkDestroyDescriptorSetLayout"));
                m_devDisp.CreateDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(load("vkCreateDescriptorPool"));
                m_devDisp.DestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(load("vkDestroyDescriptorPool"));
                m_devDisp.AllocateDescriptorSets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(load("vkAllocateDescriptorSets"));
                m_devDisp.UpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(load("vkUpdateDescriptorSets"));
                m_devDisp.CreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(load("vkCreatePipelineLayout"));
                m_devDisp.DestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(load("vkDestroyPipelineLayout"));
                m_devDisp.CreateGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(load("vkCreateGraphicsPipelines"));
                m_devDisp.CreateComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(load("vkCreateComputePipelines"));
                m_devDisp.DestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(load("vkDestroyPipeline"));
            }

            bool CreateTimelineAndFrameSync()
            {
                VkSemaphoreTypeCreateInfo timelineType{};
                timelineType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
                timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
                timelineType.initialValue = 0;

                VkSemaphoreCreateInfo timelineInfo{};
                timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                timelineInfo.pNext = &timelineType;

                if (m_devDisp.CreateSemaphore(m_device, &timelineInfo, nullptr, &m_timelineSemaphore) != VK_SUCCESS)
                {
                    return false;
                }

                for (auto& frame : m_frames)
                {
                    VkCommandPoolCreateInfo poolInfo{};
                    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
                    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                    if (m_devDisp.CreateCommandPool(m_device, &poolInfo, nullptr, &frame.CmdPool) != VK_SUCCESS)
                    {
                        return false;
                    }

                    VkCommandBufferAllocateInfo cmdAlloc{};
                    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                    cmdAlloc.commandPool = frame.CmdPool;
                    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                    cmdAlloc.commandBufferCount = 1;
                    if (m_devDisp.AllocateCommandBuffers(m_device, &cmdAlloc, &frame.CmdBuffer) != VK_SUCCESS)
                    {
                        return false;
                    }

                    VkSemaphoreCreateInfo binSem{};
                    binSem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                    m_devDisp.CreateSemaphore(m_device, &binSem, nullptr, &frame.ImageAvailableSemaphore);
                    m_devDisp.CreateSemaphore(m_device, &binSem, nullptr, &frame.RenderFinishedSemaphore);

                    VkFenceCreateInfo fenceInfo{};
                    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                    m_devDisp.CreateFence(m_device, &fenceInfo, nullptr, &frame.InFlightFence);
                }

                VkCommandPoolCreateInfo uploadPoolInfo{};
                uploadPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                uploadPoolInfo.queueFamilyIndex = m_graphicsQueueFamily;
                uploadPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                if (m_devDisp.CreateCommandPool(m_device, &uploadPoolInfo, nullptr, &m_uploadCmdPool) != VK_SUCCESS)
                {
                    return false;
                }

                VkCommandBufferAllocateInfo uploadAlloc{};
                uploadAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                uploadAlloc.commandPool = m_uploadCmdPool;
                uploadAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                uploadAlloc.commandBufferCount = 1;
                if (m_devDisp.AllocateCommandBuffers(m_device, &uploadAlloc, &m_uploadCmdBuffer) != VK_SUCCESS)
                {
                    return false;
                }

                VkFenceCreateInfo uploadFenceInfo{};
                uploadFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                if (m_devDisp.CreateFence(m_device, &uploadFenceInfo, nullptr, &m_uploadFence) != VK_SUCCESS)
                {
                    return false;
                }

                return true;
            }

            bool CreateBindlessDescriptors()
            {
                std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
                bindings[0].binding = 0;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = kMaxBindlessResources;
                bindings[0].stageFlags = VK_SHADER_STAGE_ALL;

                bindings[1].binding = 1;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                bindings[1].descriptorCount = kMaxBindlessResources;
                bindings[1].stageFlags = VK_SHADER_STAGE_ALL;

                bindings[2].binding = 2;
                bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                bindings[2].descriptorCount = 1;
                bindings[2].stageFlags = VK_SHADER_STAGE_ALL;

                bindings[3].binding = 3;
                bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                bindings[3].descriptorCount = kMaxBindlessUniformBuffers;
                bindings[3].stageFlags = VK_SHADER_STAGE_ALL;

                const VkDescriptorBindingFlags bindFlags =
                    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                    VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
                std::array<VkDescriptorBindingFlags, 4> bindingFlags = { bindFlags, bindFlags, bindFlags, bindFlags };

                VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
                flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                flagsInfo.bindingCount = static_cast<u32>(bindingFlags.size());
                flagsInfo.pBindingFlags = bindingFlags.data();

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.pNext = &flagsInfo;
                layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
                layoutInfo.bindingCount = static_cast<u32>(bindings.size());
                layoutInfo.pBindings = bindings.data();

                if (m_devDisp.CreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bindlessLayout) != VK_SUCCESS)
                {
                    return false;
                }

                std::array<VkDescriptorPoolSize, 4> poolSizes{};
                poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                poolSizes[0].descriptorCount = kMaxBindlessResources;
                poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                poolSizes[1].descriptorCount = kMaxBindlessResources;
                poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
                poolSizes[2].descriptorCount = 4;
                poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                poolSizes[3].descriptorCount = kMaxBindlessUniformBuffers;

                VkDescriptorPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
                poolInfo.maxSets = 1;
                poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();

                if (m_devDisp.CreateDescriptorPool(m_device, &poolInfo, nullptr, &m_bindlessPool) != VK_SUCCESS)
                {
                    return false;
                }

                VkDescriptorSetAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = m_bindlessPool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &m_bindlessLayout;

                if (m_devDisp.AllocateDescriptorSets(m_device, &allocInfo, &m_bindlessSet) != VK_SUCCESS)
                {
                    return false;
                }

                if (m_devDisp.CreateSampler != nullptr)
                {
                    VkSamplerCreateInfo sampInfo{};
                    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                    sampInfo.magFilter = VK_FILTER_LINEAR;
                    sampInfo.minFilter = VK_FILTER_LINEAR;
                    sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    sampInfo.maxAnisotropy = 1.0f;
                    sampInfo.minLod = 0.0f;
                    sampInfo.maxLod = VK_LOD_CLAMP_NONE;
                    sampInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

                    if (m_devDisp.CreateSampler(m_device, &sampInfo, nullptr, &m_linearSampler) == VK_SUCCESS)
                    {
                        VkDescriptorImageInfo sampDescInfo{};
                        sampDescInfo.sampler = m_linearSampler;

                        VkWriteDescriptorSet sampWrite{};
                        sampWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        sampWrite.dstSet = m_bindlessSet;
                        sampWrite.dstBinding = 2;
                        sampWrite.dstArrayElement = 0;
                        sampWrite.descriptorCount = 1;
                        sampWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                        sampWrite.pImageInfo = &sampDescInfo;
                        m_devDisp.UpdateDescriptorSets(m_device, 1, &sampWrite, 0, nullptr);
                    }
                }

                VkPushConstantRange pushRange{};
                pushRange.stageFlags = VK_SHADER_STAGE_ALL;
                pushRange.offset = 0;
                pushRange.size = 128; // 32 DWORDs Push Constants

                VkPipelineLayoutCreateInfo pipeLayoutInfo{};
                pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeLayoutInfo.setLayoutCount = 1;
                pipeLayoutInfo.pSetLayouts = &m_bindlessLayout;
                pipeLayoutInfo.pushConstantRangeCount = 1;
                pipeLayoutInfo.pPushConstantRanges = &pushRange;

                if (m_devDisp.CreatePipelineLayout(m_device, &pipeLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
                {
                    return false;
                }

                PipelineLayoutDesc defaultDesc{};
                defaultDesc.Mode = PipelineLayoutMode::GlobalBindlessHybrid;
                defaultDesc.PushConstants.push_back(PipelinePushConstantRange{
                    .OffsetBytes = 0,
                    .SizeBytes = kMaxPortablePushConstantsBytes,
                    .Visibility = ShaderVisibility::All
                });
                defaultDesc.DebugName = "Morbi_DefaultBindlessPipelineLayout";
                const PipelineLayoutDesc normalizedDefault = PipelineLayoutValidator::Normalize(defaultDesc);
                const u64 defaultHash = PipelineLayoutValidator::ComputeCanonicalHash(normalizedDefault);
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_defaultBindlessPipelineLayout = m_pipelineLayouts.Emplace(VulkanPipelineLayoutRecord{
                        .Desc = normalizedDefault,
                        .CanonicalHash = defaultHash,
                        .PipelineLayout = m_pipelineLayout,
                        .OwnedSetLayouts = {},
                        .OwnedSamplers = {},
                        .PushConstantStageMask = VK_SHADER_STAGE_ALL,
                        .MaxPushConstantBytes = kMaxPortablePushConstantsBytes,
                        .IncludesGlobalBindlessSet = true,
                        .IsDefaultLayout = true,
                        .CacheRefCount = 1,
                        .PipelineRefCount = 0,
                        .UnsubmittedRefCount = 0,
                        .LastUsedTimelineValue = 0,
                        .PendingDestroy = false
                    });
                    m_pipelineLayoutCache[defaultHash].push_back(m_defaultBindlessPipelineLayout);
                    ++m_layoutStats.CreatedCount;
                    ++m_layoutStats.ActiveCount;
                }

                return true;
            }

            bool CreateSwapchain(u32 width, u32 height)
            {
                VkSurfaceCapabilitiesKHR caps{};
                m_instDisp.GetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDevice, m_surface, &caps);

                u32 fmtCount = 0;
                m_instDisp.GetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &fmtCount, nullptr);
                std::vector<VkSurfaceFormatKHR> formats(std::max<u32>(fmtCount, 1));
                if (fmtCount > 0)
                {
                    m_instDisp.GetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &fmtCount, formats.data());
                }

                VkSurfaceFormatKHR chosenFmt = formats[0];
                for (const auto& f : formats)
                {
                    if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
                    {
                        chosenFmt = f;
                        break;
                    }
                }
                m_swapchainFormat = FromVkFormat(chosenFmt.format);

                VkExtent2D extent = caps.currentExtent;
                if (extent.width == 0xFFFFFFFFu)
                {
                    extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
                    extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
                }
                if (extent.width == 0 || extent.height == 0)
                {
                    return false;
                }
                m_width = extent.width;
                m_height = extent.height;

                u32 imageCount = std::max<u32>(caps.minImageCount + 1, 2);
                if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
                {
                    imageCount = caps.maxImageCount;
                }

                VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
                if (!m_vsync && m_instDisp.GetPhysicalDeviceSurfacePresentModesKHR)
                {
                    u32 modeCount = 0;
                    m_instDisp.GetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &modeCount, nullptr);
                    std::vector<VkPresentModeKHR> modes(modeCount);
                    if (modeCount > 0)
                    {
                        m_instDisp.GetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &modeCount, modes.data());
                    }
                    for (VkPresentModeKHR m : modes)
                    {
                        if (m == VK_PRESENT_MODE_MAILBOX_KHR || m == VK_PRESENT_MODE_IMMEDIATE_KHR)
                        {
                            chosenPresentMode = m;
                            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) break;
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    for (TextureHandle h : m_swapchainTextures)
                    {
                        DestroyTextureInternalLocked(h);
                    }
                }
                m_swapchainTextures.clear();

                const VkSwapchainKHR oldSwapchain = m_swapchain;
                m_swapchain = VK_NULL_HANDLE;

                VkSwapchainCreateInfoKHR scInfo{};
                scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                scInfo.surface = m_surface;
                scInfo.minImageCount = imageCount;
                scInfo.imageFormat = chosenFmt.format;
                scInfo.imageColorSpace = chosenFmt.colorSpace;
                scInfo.imageExtent = extent;
                scInfo.imageArrayLayers = 1;
                scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                scInfo.preTransform = caps.currentTransform;
                scInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                scInfo.presentMode = chosenPresentMode;
                scInfo.clipped = VK_TRUE;
                scInfo.oldSwapchain = oldSwapchain;

                const VkResult scRes = m_devDisp.CreateSwapchainKHR(m_device, &scInfo, nullptr, &m_swapchain);
                if (oldSwapchain != VK_NULL_HANDLE)
                {
                    m_devDisp.DestroySwapchainKHR(m_device, oldSwapchain, nullptr);
                }
                if (scRes != VK_SUCCESS)
                {
                    VulkanDebug::CheckVkResult(scRes, "vkCreateSwapchainKHR");
                    return false;
                }
                m_debug.SetObjectName(m_device, VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<u64>(m_swapchain), "Morbi_VulkanSwapchain");

                u32 actualImgCount = 0;
                m_devDisp.GetSwapchainImagesKHR(m_device, m_swapchain, &actualImgCount, nullptr);
                std::vector<VkImage> scImages(actualImgCount);
                m_devDisp.GetSwapchainImagesKHR(m_device, m_swapchain, &actualImgCount, scImages.data());

                m_swapchainTextures.clear();
                for (u32 i = 0; i < actualImgCount; ++i)
                {
                    VkImageViewCreateInfo viewInfo{};
                    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    viewInfo.image = scImages[i];
                    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = chosenFmt.format;
                    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    viewInfo.subresourceRange.baseMipLevel = 0;
                    viewInfo.subresourceRange.levelCount = 1;
                    viewInfo.subresourceRange.baseArrayLayer = 0;
                    viewInfo.subresourceRange.layerCount = 1;

                    VkImageView view = VK_NULL_HANDLE;
                    m_devDisp.CreateImageView(m_device, &viewInfo, nullptr, &view);

                    TextureDesc td{};
                    td.Width = extent.width;
                    td.Height = extent.height;
                    td.PixelFormat = m_swapchainFormat;
                    td.RenderTarget = true;
                    td.DebugName = "VulkanSwapchainBackbuffer";

                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_swapchainTextures.push_back(m_textures.Emplace(VulkanTextureRecord{
                        .Desc = td,
                        .Image = scImages[i],
                        .View = view,
                        .Allocation = {},
                        .Bindless = {},
                        .OwnsImage = false
                    }));
                }

                return true;
            }

            void DestroySwapchain()
            {
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    for (TextureHandle h : m_swapchainTextures)
                    {
                        DestroyTextureInternalLocked(h);
                    }
                }
                m_swapchainTextures.clear();

                if (m_swapchain != VK_NULL_HANDLE && m_devDisp.DestroySwapchainKHR)
                {
                    m_devDisp.DestroySwapchainKHR(m_device, m_swapchain, nullptr);
                    m_swapchain = VK_NULL_HANDLE;
                }
            }

            void* m_vulkanLib = nullptr;
            VulkanDebug m_debug;
            VulkanGlobalDispatch m_globalDisp{};
            VulkanInstanceDispatch m_instDisp{};
            VulkanDeviceDispatch m_devDisp{};

            VkInstance m_instance = VK_NULL_HANDLE;
            VkSurfaceKHR m_surface = VK_NULL_HANDLE;
            VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
            VkDevice m_device = VK_NULL_HANDLE;
            VkQueue m_graphicsQueue = VK_NULL_HANDLE;
            u32 m_graphicsQueueFamily = 0;
            std::string m_adapterName = "Vulkan 1.3 Device";

            VulkanMemoryAllocator m_allocator;

            VkDescriptorSetLayout m_bindlessLayout = VK_NULL_HANDLE;
            VkDescriptorPool m_bindlessPool = VK_NULL_HANDLE;
            VkDescriptorSet m_bindlessSet = VK_NULL_HANDLE;
            VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
            VkSampler m_linearSampler = VK_NULL_HANDLE;
            u32 m_nextBindlessBufferSlot = 0;
            u32 m_nextBindlessTextureSlot = 0;
            u32 m_nextBindlessUniformSlot = 0;
            std::vector<u32> m_freeBindlessUniformSlots;
            std::vector<u32> m_freeBindlessTextureSlots;
            std::vector<u32> m_freeBindlessBufferSlots;

            VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
            u64 m_nextTimelineValue = 0;
            std::array<FrameContext, kMaxFramesInFlight> m_frames{};
            u32 m_frameIndex = 0;
            u32 m_acquiredImageIndex = 0;
            bool m_frameOpen = false;

            VkCommandPool m_uploadCmdPool = VK_NULL_HANDLE;
            VkCommandBuffer m_uploadCmdBuffer = VK_NULL_HANDLE;
            VkFence m_uploadFence = VK_NULL_HANDLE;

            VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
            Format m_swapchainFormat = Format::BGRA8_UNorm;
            std::vector<TextureHandle> m_swapchainTextures;
            u32 m_width = 1280;
            u32 m_height = 720;
            bool m_vsync = true;

            VulkanCommandList m_activeCmdList;
            std::vector<VulkanCommandList*> m_createdCommandLists;
            Core::GenerationalPool<VulkanBufferRecord, BufferTag> m_buffers;
            Core::GenerationalPool<VulkanTextureRecord, TextureTag> m_textures;
            Core::GenerationalPool<VulkanPipelineRecord, PipelineTag> m_pipelines;
            Core::GenerationalPool<VulkanPipelineLayoutRecord, PipelineLayoutTag> m_pipelineLayouts;
            std::unordered_map<u64, std::vector<PipelineLayoutHandle>> m_pipelineLayoutCache;
            PipelineLayoutHandle m_defaultBindlessPipelineLayout = PipelineLayoutHandle::Invalid();
            PipelineLayoutLimits m_limits{};
            PipelineLayoutStats m_layoutStats{};
            VkPhysicalDeviceVulkan12Features m_enabledVk12Features{};

            std::vector<DeferredDestruction> m_deferredDestructions;
            mutable std::mutex m_resourceMutex;
            std::mutex m_submitMutex;
            std::mutex m_uploadMutex;
        };

        VulkanCommandList::~VulkanCommandList()
        {
            ReleaseUnsubmittedReferences();
            if (m_owner)
            {
                if (m_ownsPool && m_cmdPool != VK_NULL_HANDLE)
                {
                    m_owner->DestroyCommandPool(m_cmdPool);
                    m_cmdPool = VK_NULL_HANDLE;
                    m_cmd = VK_NULL_HANDLE;
                }
                if (m_ownsPool)
                {
                    m_owner->UnregisterCommandList(this);
                }
            }
        }

        void VulkanCommandList::Begin()
        {
            ReleaseUnsubmittedReferences();
            m_activeVkLayout = m_pipelineLayout;
            m_activePushStageMask = VK_SHADER_STAGE_ALL;
            m_activePushMaxBytes = 128;
            m_activeLayoutIncludesBindless = true;
            if (m_ownsPool && m_device != VK_NULL_HANDLE && m_cmdPool != VK_NULL_HANDLE && m_disp && m_disp->ResetCommandPool)
            {
                m_disp->ResetCommandPool(m_device, m_cmdPool, 0);
            }
            if (m_disp && m_disp->BeginCommandBuffer && m_cmd != VK_NULL_HANDLE)
            {
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                m_disp->BeginCommandBuffer(m_cmd, &beginInfo);
                m_isRecording = true;
            }
        }

        void VulkanCommandList::End()
        {
            if (m_isRecording && m_disp && m_disp->EndCommandBuffer && m_cmd != VK_NULL_HANDLE)
            {
                m_disp->EndCommandBuffer(m_cmd);
                m_isRecording = false;
            }
        }

        void VulkanCommandList::Discard()
        {
            if (m_isRecording && m_disp && m_disp->EndCommandBuffer && m_cmd != VK_NULL_HANDLE)
            {
                m_disp->EndCommandBuffer(m_cmd);
                m_isRecording = false;
            }
            ReleaseUnsubmittedReferences();
        }

        void VulkanCommandList::TrackBuffer(BufferHandle handle)
        {
            if (!m_owner || !handle.IsValid())
            {
                return;
            }
            if (m_referencedBuffers.contains(handle))
            {
                return;
            }
            if (m_owner->AddUnsubmittedBufferRef(handle))
            {
                m_referencedBuffers.insert(handle);
            }
        }

        void VulkanCommandList::TrackTexture(TextureHandle handle)
        {
            if (!m_owner || !handle.IsValid())
            {
                return;
            }
            if (m_referencedTextures.contains(handle))
            {
                return;
            }
            if (m_owner->AddUnsubmittedTextureRef(handle))
            {
                m_referencedTextures.insert(handle);
            }
        }

        void VulkanCommandList::TrackPipeline(PipelineHandle handle)
        {
            if (!m_owner || !handle.IsValid())
            {
                return;
            }
            if (!m_referencedPipelines.contains(handle))
            {
                if (m_owner->AddUnsubmittedPipelineRef(handle))
                {
                    m_referencedPipelines.insert(handle);
                }
            }
            if (const auto snap = m_owner->TryGetPipelineSnapshot(handle))
            {
                TrackPipelineLayout(snap->Layout);
            }
        }

        void VulkanCommandList::TrackPipelineLayout(PipelineLayoutHandle layout)
        {
            if (!m_owner || !layout.IsValid())
            {
                return;
            }
            if (m_referencedPipelineLayouts.contains(layout))
            {
                return;
            }
            if (m_owner->AddUnsubmittedPipelineLayoutRef(layout))
            {
                m_referencedPipelineLayouts.insert(layout);
            }
        }

        void VulkanCommandList::ReleaseUnsubmittedReferences()
        {
            if (m_owner && (!m_referencedBuffers.empty() || !m_referencedTextures.empty() || !m_referencedPipelines.empty() || !m_referencedPipelineLayouts.empty()))
            {
                m_owner->ReleaseUnsubmittedRefs(m_referencedBuffers, m_referencedTextures, m_referencedPipelines, m_referencedPipelineLayouts);
            }
            m_referencedBuffers.clear();
            m_referencedTextures.clear();
            m_referencedPipelines.clear();
            m_referencedPipelineLayouts.clear();
        }

        void VulkanCommandList::OnSubmitted(u64 submissionTimelineValue)
        {
            m_submissionTimelineValue = submissionTimelineValue;
            m_referencedBuffers.clear();
            m_referencedTextures.clear();
            m_referencedPipelines.clear();
            m_referencedPipelineLayouts.clear();
        }

        void VulkanCommandList::ShutdownInternal()
        {
            ReleaseUnsubmittedReferences();
            if (m_owner && m_ownsPool && m_cmdPool != VK_NULL_HANDLE)
            {
                m_owner->DestroyCommandPool(m_cmdPool);
            }
            m_cmdPool = VK_NULL_HANDLE;
            m_cmd = VK_NULL_HANDLE;
            m_owner = nullptr;
            m_isRecording = false;
        }

        void VulkanCommandList::TransitionTexture(TextureHandle texture, ResourceState before, ResourceState after)
        {
            if (!m_owner)
            {
                return;
            }
            TrackTexture(texture);
            if (!m_referencedTextures.contains(texture) || before == after)
            {
                return;
            }
            const auto rec = m_owner->TryGetTextureSnapshot(texture);
            if (!rec || rec->Image == VK_NULL_HANDLE)
            {
                return;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = ToVkImageLayout(before);
            barrier.newLayout = ToVkImageLayout(after);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = rec->Image;
            barrier.subresourceRange.aspectMask = rec->DepthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = std::max<u32>(rec->MipLevels, 1);
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = std::max<u32>(rec->DepthOrArrayLayers, 1);
            barrier.srcAccessMask = ToVkAccessFlags(before);
            barrier.dstAccessMask = ToVkAccessFlags(after);

            m_disp->CmdPipelineBarrier(
                m_cmd,
                ToVkStageFlags(before),
                ToVkStageFlags(after),
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        }

        void VulkanCommandList::TransitionBuffer(BufferHandle buffer, ResourceState before, ResourceState after)
        {
            if (!m_owner)
            {
                return;
            }
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer) || before == after)
            {
                return;
            }
            const auto rec = m_owner->TryGetBufferSnapshot(buffer);
            if (!rec || rec->Buffer == VK_NULL_HANDLE)
            {
                return;
            }

            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = ToVkAccessFlags(before);
            barrier.dstAccessMask = ToVkAccessFlags(after);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = rec->Buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;

            m_disp->CmdPipelineBarrier(
                m_cmd,
                ToVkStageFlags(before),
                ToVkStageFlags(after),
                0,
                0, nullptr,
                1, &barrier,
                0, nullptr);
        }

        void VulkanCommandList::BeginRendering(const DynamicRenderPassDesc& passDesc)
        {
            if (!m_owner || !m_disp->CmdBeginRendering)
            {
                return;
            }

            std::vector<VkRenderingAttachmentInfo> colorAtts;
            colorAtts.reserve(passDesc.ColorAttachments.size());
            u32 renderW = m_renderWidth;
            u32 renderH = m_renderHeight;

            for (const auto& att : passDesc.ColorAttachments)
            {
                TrackTexture(att.Texture);
                if (!m_referencedTextures.contains(att.Texture))
                {
                    continue;
                }
                const auto texRec = m_owner->TryGetTextureSnapshot(att.Texture);
                if (!texRec || texRec->View == VK_NULL_HANDLE)
                {
                    continue;
                }
                renderW = texRec->Width;
                renderH = texRec->Height;

                VkRenderingAttachmentInfo vkAtt{};
                vkAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                vkAtt.imageView = texRec->View;
                vkAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                vkAtt.loadOp = att.ClearOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                vkAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                vkAtt.clearValue.color.float32[0] = att.ClearColor[0];
                vkAtt.clearValue.color.float32[1] = att.ClearColor[1];
                vkAtt.clearValue.color.float32[2] = att.ClearColor[2];
                vkAtt.clearValue.color.float32[3] = att.ClearColor[3];
                colorAtts.push_back(vkAtt);
            }

            VkRenderingAttachmentInfo depthAtt{};
            const bool hasDepth = passDesc.DepthAttachment.IsValid();
            if (hasDepth)
            {
                TrackTexture(passDesc.DepthAttachment);
                if (m_referencedTextures.contains(passDesc.DepthAttachment))
                {
                    if (const auto depthRec = m_owner->TryGetTextureSnapshot(passDesc.DepthAttachment))
                    {
                        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        depthAtt.imageView = depthRec->View;
                        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        depthAtt.loadOp = passDesc.ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        depthAtt.clearValue.depthStencil.depth = passDesc.DepthClearValue;
                    }
                }
            }

            VkRenderingInfo renderInfo{};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderInfo.renderArea.offset = { 0, 0 };
            renderInfo.renderArea.extent = { renderW, renderH };
            renderInfo.layerCount = 1;
            renderInfo.colorAttachmentCount = static_cast<u32>(colorAtts.size());
            renderInfo.pColorAttachments = colorAtts.data();
            if (hasDepth && depthAtt.imageView != VK_NULL_HANDLE)
            {
                renderInfo.pDepthAttachment = &depthAtt;
            }

            m_disp->CmdBeginRendering(m_cmd, &renderInfo);

            SetViewport(Viewport{ 0.0f, 0.0f, static_cast<f32>(renderW), static_cast<f32>(renderH), 0.0f, 1.0f });
            SetScissor(ScissorRect{ 0, 0, renderW, renderH });
        }

        void VulkanCommandList::SetPipeline(PipelineHandle pipeline)
        {
            if (!m_owner)
            {
                return;
            }
            TrackPipeline(pipeline);
            if (!m_referencedPipelines.contains(pipeline))
            {
                return;
            }
            const auto rec = m_owner->TryGetPipelineSnapshot(pipeline);
            if (!rec || rec->Pipeline == VK_NULL_HANDLE)
            {
                return;
            }

            const VkPipelineBindPoint bindPoint = rec->IsCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
            const VkPipelineLayout targetLayout = (rec->NativeLayout != VK_NULL_HANDLE) ? rec->NativeLayout : m_pipelineLayout;
            m_activeVkLayout = targetLayout;
            m_activePushStageMask = rec->PushConstantStageMask;
            m_activePushMaxBytes = rec->PushConstantMaxBytes;
            m_activeLayoutIncludesBindless = rec->IncludesGlobalBindlessSet;

            m_disp->CmdBindPipeline(m_cmd, bindPoint, rec->Pipeline);
            if (rec->IncludesGlobalBindlessSet && m_bindlessSet != VK_NULL_HANDLE && targetLayout != VK_NULL_HANDLE)
            {
                m_disp->CmdBindDescriptorSets(m_cmd, bindPoint, targetLayout, 0, 1, &m_bindlessSet, 0, nullptr);
            }
        }

        void VulkanCommandList::BindVertexBuffer(BufferHandle buffer, u64 offsetBytes)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer)) return;
            if (const auto rec = m_owner->TryGetBufferSnapshot(buffer))
            {
                const VkDeviceSize offset = offsetBytes;
                m_disp->CmdBindVertexBuffers(m_cmd, 0, 1, &rec->Buffer, &offset);
            }
        }

        void VulkanCommandList::BindIndexBuffer(BufferHandle buffer, u64 offsetBytes, bool index32Bit)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer)) return;
            if (const auto rec = m_owner->TryGetBufferSnapshot(buffer))
            {
                m_disp->CmdBindIndexBuffer(m_cmd, rec->Buffer, offsetBytes, index32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
            }
        }

        void VulkanCommandList::BindConstantBuffer([[maybe_unused]] u32 rootParamSlot, BufferHandle buffer, u64 offsetBytes)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer)) return;
            if (const auto rec = m_owner->TryGetBufferSnapshot(buffer))
            {
                // Bindless uniform buffers are already bound at CreateBuffer to their permanent slot;
                // only rebind when offsetBytes != 0 or for non-bindless fallback slot 0.
                if (rec->Bindless.IsValid() && offsetBytes == 0)
                {
                    return;
                }

                const u32 targetElem = rec->Bindless.IsValid() ? rec->Bindless.Index : 0u;
                VkDescriptorBufferInfo dbi{};
                dbi.buffer = rec->Buffer;
                dbi.offset = offsetBytes;
                dbi.range = rec->SizeBytes - offsetBytes;

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_bindlessSet;
                write.dstBinding = 3;
                write.dstArrayElement = targetElem;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &dbi;
                m_owner->UpdateDescriptorSets(1, &write);
            }
        }

        void VulkanCommandList::BeginDebugMarker(std::string_view label)
        {
            if (m_owner)
            {
                m_owner->Debug().BeginDebugLabel(m_cmd, label);
            }
        }

        void VulkanCommandList::EndDebugMarker()
        {
            if (m_owner)
            {
                m_owner->Debug().EndDebugLabel(m_cmd);
            }
        }
    }

    std::unique_ptr<IDevice> CreateVulkanDevice()
    {
        return std::make_unique<VulkanDevice>();
    }
}
