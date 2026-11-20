#include "Morbi/RHI/IDevice.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <d3d12.h>
    #include <dxgi1_6.h>
    #include <wrl/client.h>
#endif

#include "Morbi/Core/Assert.hpp"
#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Memory/PoolAllocator.hpp"
#include "Morbi/RHI/DX12/DX12Debug.h"

namespace Morbi::RHI
{
#if defined(_WIN32)
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr u32 kMaxFramesInFlight = 2;
        constexpr u32 kMaxBindlessDescriptors = 4096;
        // DX12 intentionally binds per-object uniform buffers (register b1, space0) via Root CBV (rootParams[2])
        // rather than a bindless CBV descriptor table. Heap slots [8192, 8448) are maintained only for
        // BindlessResourceIndex slot-tracking parity with Vulkan's 256-slot uniform buffer array.
        constexpr u32 kMaxBindlessUniformBuffers = 256;
        constexpr u32 kTotalBindlessHeapDescriptors = kMaxBindlessDescriptors + kMaxBindlessDescriptors + kMaxBindlessUniformBuffers;
        constexpr u32 kMaxRtvDescriptors = 256;
        constexpr u32 kMaxDsvDescriptors = 64;

        D3D12_SHADER_VISIBILITY ToD3D12ShaderVisibility(ShaderVisibility vis) noexcept
        {
            if (vis == ShaderVisibility::Vertex)
            {
                return D3D12_SHADER_VISIBILITY_VERTEX;
            }
            if (vis == ShaderVisibility::Pixel)
            {
                return D3D12_SHADER_VISIBILITY_PIXEL;
            }
            return D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_FILTER ToD3D12Filter(StaticSamplerFilter filter, bool isComparison) noexcept
        {
            if (isComparison)
            {
                switch (filter)
                {
                case StaticSamplerFilter::Nearest:     return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
                case StaticSamplerFilter::Anisotropic: return D3D12_FILTER_COMPARISON_ANISOTROPIC;
                case StaticSamplerFilter::Linear:
                default:                               return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
                }
            }
            switch (filter)
            {
            case StaticSamplerFilter::Nearest:     return D3D12_FILTER_MIN_MAG_MIP_POINT;
            case StaticSamplerFilter::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
            case StaticSamplerFilter::Linear:
            default:                               return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            }
        }

        D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(StaticSamplerAddressMode mode) noexcept
        {
            switch (mode)
            {
            case StaticSamplerAddressMode::ClampToEdge:   return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            case StaticSamplerAddressMode::ClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            case StaticSamplerAddressMode::MirrorRepeat:  return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
            case StaticSamplerAddressMode::Repeat:
            default:                                      return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            }
        }

        D3D12_DESCRIPTOR_RANGE_TYPE ToD3D12DescriptorRangeType(DescriptorType type) noexcept
        {
            switch (type)
            {
            case DescriptorType::UniformBuffer:         return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            case DescriptorType::RWStorageBuffer:
            case DescriptorType::StorageTexture:        return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            case DescriptorType::Sampler:               return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            case DescriptorType::StorageBuffer:
            case DescriptorType::SampledTexture:
            case DescriptorType::AccelerationStructure:
            default:                                    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            }
        }

        DXGI_FORMAT ToDxgiFormat(Format fmt) noexcept
        {
            switch (fmt)
            {
            case Format::RGBA8_UNorm:       return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA8_sRGB:        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case Format::BGRA8_UNorm:       return DXGI_FORMAT_B8G8R8A8_UNORM;
            case Format::BGRA8_sRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case Format::RG16_Float:        return DXGI_FORMAT_R16G16_FLOAT;
            case Format::RGBA16_Float:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case Format::R32_Float:         return DXGI_FORMAT_R32_FLOAT;
            case Format::RG32_Float:        return DXGI_FORMAT_R32G32_FLOAT;
            case Format::RGB32_Float:       return DXGI_FORMAT_R32G32B32_FLOAT;
            case Format::RGBA32_Float:      return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case Format::D32_Float:         return DXGI_FORMAT_D32_FLOAT;
            case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            default:                        return DXGI_FORMAT_B8G8R8A8_UNORM;
            }
        }

        D3D12_RESOURCE_STATES ToD3D12State(ResourceState state) noexcept
        {
            switch (state)
            {
            case ResourceState::Undefined:
            case ResourceState::Common:          return D3D12_RESOURCE_STATE_COMMON;
            case ResourceState::VertexBuffer:
            case ResourceState::ConstantBuffer:  return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            case ResourceState::IndexBuffer:     return D3D12_RESOURCE_STATE_INDEX_BUFFER;
            case ResourceState::ShaderResource:  return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            case ResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            case ResourceState::RenderTarget:    return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case ResourceState::DepthWrite:      return D3D12_RESOURCE_STATE_DEPTH_WRITE;
            case ResourceState::DepthRead:       return D3D12_RESOURCE_STATE_DEPTH_READ;
            case ResourceState::CopySource:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case ResourceState::CopyDest:        return D3D12_RESOURCE_STATE_COPY_DEST;
            case ResourceState::Present:         return D3D12_RESOURCE_STATE_PRESENT;
            default:                             return D3D12_RESOURCE_STATE_COMMON;
            }
        }

        // D3D12MA-style Placed Resource & Transient Aliasing Heap Allocator
        class DX12MemoryAllocator
        {
        public:
            void Initialize(ID3D12Device* device)
            {
                m_device = device;

                // Create a 32 MB Default GPU Heap for Placed / Transient Aliased resources
                D3D12_HEAP_DESC heapDesc{};
                heapDesc.SizeInBytes = 32ULL * 1024ULL * 1024ULL;
                heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
                heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

                if (SUCCEEDED(m_device->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_transientHeap))))
                {
                    DX12::DX12Debug::SetObjectName(m_transientHeap.Get(), "DX12_TransientAliasingHeap");
                    m_stats.HeapBlockCount = 1;
                    m_stats.TotalReservedBytes = heapDesc.SizeInBytes;
                    m_transientHeapSize = heapDesc.SizeInBytes;
                }
            }

            void Shutdown()
            {
                m_transientHeap.Reset();
                m_device = nullptr;
            }

            bool CreateResource(
                const D3D12_HEAP_PROPERTIES& heapProps,
                const D3D12_RESOURCE_DESC& resDesc,
                D3D12_RESOURCE_STATES initialState,
                const D3D12_CLEAR_VALUE* clearVal,
                u64 aliasingOffset,
                ComPtr<ID3D12Resource>& outResource,
                u64& outByteSize)
            {
                const D3D12_RESOURCE_ALLOCATION_INFO allocInfo = m_device->GetResourceAllocationInfo(0, 1, &resDesc);
                outByteSize = allocInfo.SizeInBytes;

                // If transient aliasing offset is requested on Default GPU memory and fits in our placed heap:
                if (aliasingOffset != 0xFFFFFFFFFFFFFFFFULL &&
                    heapProps.Type == D3D12_HEAP_TYPE_DEFAULT &&
                    m_transientHeap != nullptr)
                {
                    const u64 align = std::max<u64>(allocInfo.Alignment, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
                    const u64 alignedOffset = (aliasingOffset + align - 1) & ~(align - 1);
                    if (alignedOffset + allocInfo.SizeInBytes <= m_transientHeapSize)
                    {
                        if (SUCCEEDED(m_device->CreatePlacedResource(
                            m_transientHeap.Get(),
                            alignedOffset,
                            &resDesc,
                            initialState,
                            clearVal,
                            IID_PPV_ARGS(&outResource))))
                        {
                            if (alignedOffset + allocInfo.SizeInBytes > m_transientHighWaterMark)
                            {
                                m_transientHighWaterMark = alignedOffset + allocInfo.SizeInBytes;
                            }
                            else
                            {
                                m_stats.TransientAliasedBytesSaved += allocInfo.SizeInBytes;
                            }
                            ++m_stats.ActiveAllocations;
                            m_stats.TotalAllocatedBytes += allocInfo.SizeInBytes;
                            return true;
                        }
                    }
                }

                const HRESULT commitHr = m_device->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &resDesc,
                    initialState,
                    clearVal,
                    IID_PPV_ARGS(&outResource));
                if (FAILED(commitHr))
                {
                    Core::Log::Error("RHI.DX12.Debug", "CreateCommittedResource failed: " + DX12::DX12Debug::FormatHResult(commitHr));
                    if (commitHr == DXGI_ERROR_DEVICE_REMOVED || commitHr == DXGI_ERROR_DEVICE_HUNG)
                    {
                        DX12::DX12Debug::DumpDeviceRemovedExtendedData(m_device, commitHr, "CreateCommittedResource");
                    }
                    return false;
                }

                ++m_stats.ActiveAllocations;
                m_stats.TotalAllocatedBytes += allocInfo.SizeInBytes;
                return true;
            }

            void OnFreeResource(u64 byteSize)
            {
                if (m_stats.ActiveAllocations > 0)
                {
                    --m_stats.ActiveAllocations;
                }
                if (m_stats.TotalAllocatedBytes >= byteSize)
                {
                    m_stats.TotalAllocatedBytes -= byteSize;
                }
            }

            [[nodiscard]] MemoryAllocatorStats GetStats() const noexcept { return m_stats; }

        private:
            ID3D12Device* m_device = nullptr;
            ComPtr<ID3D12Heap> m_transientHeap;
            u64 m_transientHeapSize = 0;
            u64 m_transientHighWaterMark = 0;
            MemoryAllocatorStats m_stats{};
        };

        struct DX12BufferRecord
        {
            BufferDesc Desc;
            ComPtr<ID3D12Resource> Resource;
            u64 AllocatedBytes = 0;
            BindlessResourceIndex Bindless{};
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedFenceValue = 0;
            bool PendingDestroy = false;
        };

        struct DX12TextureRecord
        {
            TextureDesc Desc;
            ComPtr<ID3D12Resource> Resource;
            u64 AllocatedBytes = 0;
            D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle{};
            u32 RtvSlot = 0xFFFFFFFFu;
            u32 DsvSlot = 0xFFFFFFFFu;
            bool HasRtv = false;
            bool HasDsv = false;
            BindlessResourceIndex Bindless{};
            mutable D3D12_RESOURCE_STATES CurrentState = D3D12_RESOURCE_STATE_COMMON;
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedFenceValue = 0;
            bool PendingDestroy = false;
        };

        struct DX12PipelineLayoutRecord
        {
            PipelineLayoutDesc Desc;
            u64 CanonicalHash = 0;
            ComPtr<ID3D12RootSignature> RootSignature;
            std::optional<u32> RootConstantParamIndex;
            u32 MaxPushConstantBytes = 0;
            std::optional<u32> BindlessTableSlot;
            bool UsesGlobalBindlessHeap = false;
            bool IsDefaultLayout = false;
            u32 CacheRefCount = 1;
            u32 PipelineRefCount = 0;
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedFenceValue = 0;
            bool PendingDestroy = false;
        };

        struct DX12PipelineRecord
        {
            ComPtr<ID3D12PipelineState> PSO;
            PipelineLayoutHandle Layout = PipelineLayoutHandle::Invalid();
            ID3D12RootSignature* NativeRootSig = nullptr;
            std::optional<u32> RootConstantParamIndex = 0;
            u32 MaxPushConstantBytes = 128;
            std::optional<u32> BindlessTableSlot = 1;
            bool UsesGlobalBindlessHeap = true;
            bool IsCompute = false;
            u32 UnsubmittedRefCount = 0;
            u64 LastUsedFenceValue = 0;
            bool PendingDestroy = false;
        };

        // Deferred GPU resource destruction
        struct DeferredDestruction
        {
            enum class Type : u8 { Buffer, Texture, Pipeline, PipelineLayout };
            Type ResourceType;
            u64 CompletionFenceValue;
            union
            {
                BufferHandle BufferHandle;
                TextureHandle TextureHandle;
                PipelineHandle PipelineHandle;
                PipelineLayoutHandle PipelineLayoutHandle;
            };
        };

        class DX12Device;

        class DX12CommandList final : public ICommandList
        {
        public:
            DX12CommandList() = default;
            ~DX12CommandList() override;

            DX12CommandList(const DX12CommandList&) = delete;
            DX12CommandList& operator=(const DX12CommandList&) = delete;

            void BindContext(
                DX12Device* owner,
                ID3D12GraphicsCommandList6* cmdList,
                ID3D12CommandAllocator* cmdAlloc,
                ID3D12RootSignature* rootSig,
                ID3D12DescriptorHeap* bindlessHeap,
                u32 width,
                u32 height,
                bool ownsNativeObjects = false,
                ComPtr<ID3D12CommandAllocator> ownedAlloc = nullptr,
                ComPtr<ID3D12GraphicsCommandList6> ownedCmd = nullptr)
            {
                m_owner = owner;
                m_cmd = cmdList;
                m_alloc = cmdAlloc;
                m_rootSig = rootSig;
                m_bindlessHeap = bindlessHeap;
                m_width = width;
                m_height = height;
                m_ownsNativeObjects = ownsNativeObjects;
                m_ownedAlloc = std::move(ownedAlloc);
                m_ownedCmd = std::move(ownedCmd);
                if (m_ownedCmd)
                {
                    m_cmd = m_ownedCmd.Get();
                }
                if (m_ownedAlloc)
                {
                    m_alloc = m_ownedAlloc.Get();
                }
            }

            void Begin() override;
            void End() override;
            void Discard() override;

            void TrackBuffer(BufferHandle handle) override;
            void TrackTexture(TextureHandle handle) override;
            void TrackPipeline(PipelineHandle handle) override;
            void TrackPipelineLayout(PipelineLayoutHandle layout) override;

            void ReleaseUnsubmittedReferences();
            void OnSubmitted(u64 submissionFenceValue);
            void ShutdownInternal();

            [[nodiscard]] ID3D12GraphicsCommandList6* GetD3D12CommandList() const noexcept { return m_cmd; }
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
            void EndRendering() override {}

            void SetViewport(const Viewport& viewport) override
            {
                D3D12_VIEWPORT vp{};
                vp.TopLeftX = viewport.X;
                vp.TopLeftY = viewport.Y;
                vp.Width = viewport.Width;
                vp.Height = viewport.Height;
                vp.MinDepth = viewport.MinDepth;
                vp.MaxDepth = viewport.MaxDepth;
                m_cmd->RSSetViewports(1, &vp);
            }

            void SetScissor(const ScissorRect& scissor) override
            {
                D3D12_RECT rect{};
                rect.left = scissor.X;
                rect.top = scissor.Y;
                rect.right = scissor.X + static_cast<LONG>(scissor.Width);
                rect.bottom = scissor.Y + static_cast<LONG>(scissor.Height);
                m_cmd->RSSetScissorRects(1, &rect);
            }

            void SetPipeline(PipelineHandle pipeline) override;

            void PushConstants(const void* data, u32 sizeBytes, u32 offsetBytes) override
            {
                if (!data || sizeBytes == 0 || !m_activeRootConstantSlot.has_value() || offsetBytes >= m_activeMaxPushBytes)
                {
                    return;
                }
                const u32 clampedBytes = std::min<u32>(sizeBytes, m_activeMaxPushBytes - offsetBytes);
                const u32 num32BitValues = (clampedBytes + 3u) / 4u;
                if (num32BitValues == 0)
                {
                    return;
                }
                const u32 destOffset32 = offsetBytes / 4u;
                if (m_activeIsCompute)
                {
                    m_cmd->SetComputeRoot32BitConstants(*m_activeRootConstantSlot, num32BitValues, data, destOffset32);
                }
                else
                {
                    m_cmd->SetGraphicsRoot32BitConstants(*m_activeRootConstantSlot, num32BitValues, data, destOffset32);
                }
            }

            void BindVertexBuffer(BufferHandle buffer, u64 offsetBytes) override;
            void BindIndexBuffer(BufferHandle buffer, u64 offsetBytes, bool index32Bit) override;
            void BindConstantBuffer(u32 rootParamSlot, BufferHandle buffer, u64 offsetBytes = 0) override;

            void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override
            {
                m_cmd->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
            }

            void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override
            {
                m_cmd->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
            }

            void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override
            {
                m_cmd->Dispatch(groupCountX, groupCountY, groupCountZ);
            }

            void BeginDebugMarker(std::string_view) override {}
            void EndDebugMarker() override {}

        private:
            DX12Device* m_owner = nullptr;
            ID3D12GraphicsCommandList6* m_cmd = nullptr;
            ID3D12CommandAllocator* m_alloc = nullptr;
            ID3D12RootSignature* m_rootSig = nullptr;
            ID3D12RootSignature* m_activeGraphicsRootSig = nullptr;
            ID3D12RootSignature* m_activeComputeRootSig = nullptr;
            ID3D12DescriptorHeap* m_bindlessHeap = nullptr;
            u32 m_width = 1280;
            u32 m_height = 720;
            bool m_ownsNativeObjects = false;
            bool m_isRecording = false;
            bool m_activeIsCompute = false;
            std::optional<u32> m_activeRootConstantSlot = 0;
            u32 m_activeMaxPushBytes = 128;
            std::optional<u32> m_activeBindlessTableSlot = 1;
            u64 m_submissionFenceValue = 0;

            ComPtr<ID3D12CommandAllocator> m_ownedAlloc;
            ComPtr<ID3D12GraphicsCommandList6> m_ownedCmd;

            std::unordered_set<BufferHandle> m_referencedBuffers;
            std::unordered_set<TextureHandle> m_referencedTextures;
            std::unordered_set<PipelineHandle> m_referencedPipelines;
            std::unordered_set<PipelineLayoutHandle> m_referencedPipelineLayouts;
        };

        class DX12Device final : public IDevice
        {
        public:
            ~DX12Device() override
            {
                Shutdown();
            }

            BackendAPI GetAPI() const noexcept override { return BackendAPI::DirectX12; }
            std::string_view GetAdapterName() const noexcept override { return m_adapterName; }

            Core::Result<bool, std::string> Initialize(
                const DeviceDesc& desc,
                const Platform::NativeWindowHandle& windowHandle,
                u32 width,
                u32 height) override
            {
                m_width = std::max<u32>(width, 1);
                m_height = std::max<u32>(height, 1);

                const UINT dxgiFlags = m_debug.EnablePreDeviceDebugAndDred(
                    desc.EnableValidation,
                    Core::Diagnostics::IsGpuGbvMode());

                const HRESULT facHr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_factory));
                if (!m_debug.CheckHResult(facHr, nullptr, "CreateDXGIFactory2"))
                {
                    return Core::Result<bool, std::string>::Failure("CreateDXGIFactory2 failed.");
                }

                ComPtr<IDXGIAdapter1> chosenAdapter;
                for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&chosenAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
                {
                    DXGI_ADAPTER_DESC1 adDesc{};
                    chosenAdapter->GetDesc1(&adDesc);
                    if (adDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    {
                        continue;
                    }
                    if (SUCCEEDED(D3D12CreateDevice(chosenAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
                    {
                        char nameBuf[256]{};
                        WideCharToMultiByte(CP_UTF8, 0, adDesc.Description, -1, nameBuf, sizeof(nameBuf), nullptr, nullptr);
                        m_adapterName = nameBuf;
                        break;
                    }
                }

                const HRESULT devHr = D3D12CreateDevice(chosenAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));
                if (!m_debug.CheckHResult(devHr, nullptr, "D3D12CreateDevice"))
                {
                    return Core::Result<bool, std::string>::Failure("D3D12CreateDevice (Feature Level 12_0) failed.");
                }

                m_debug.InitializePostDevice(m_device.Get());
                DX12::DX12Debug::SetObjectName(m_device.Get(), "Morbi_DX12Device");

                m_limits = PipelineLayoutLimits{};
                m_limits.MaxRootSignatureDwords = kMaxDx12RootSignatureDwords;
                m_limits.MaxPushConstantsBytes = kMaxPortablePushConstantsBytes;
                m_limits.MaxBoundDescriptorSets = 8;
                D3D12_FEATURE_DATA_D3D12_OPTIONS dx12Options{};
                if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &dx12Options, sizeof(dx12Options))))
                {
                    if (dx12Options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3)
                    {
                        m_limits.MaxPerStageSamplers = 2048;
                        m_limits.MaxPerStageUniformBuffers = 14;
                        m_limits.MaxPerStageStorageBuffers = 64;
                        m_limits.MaxPerStageSampledImages = kMaxBindlessDescriptors;
                        m_limits.MaxPerStageStorageImages = 64;
                        m_limits.MaxPerStageResources = kMaxBindlessDescriptors;
                        m_limits.SupportsDescriptorIndexing = true;
                        m_limits.SupportsRuntimeDescriptorArray = true;
                        m_limits.SupportsUpdateAfterBind = true;
                    }
                    else if (dx12Options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_2)
                    {
                        m_limits.MaxPerStageSamplers = 16;
                        m_limits.MaxPerStageUniformBuffers = 14;
                        m_limits.MaxPerStageStorageBuffers = 64;
                        m_limits.MaxPerStageSampledImages = 128;
                        m_limits.MaxPerStageStorageImages = 64;
                    }
                    else
                    {
                        m_limits.MaxPerStageSamplers = 16;
                        m_limits.MaxPerStageUniformBuffers = 14;
                        m_limits.MaxPerStageStorageBuffers = 8;
                        m_limits.MaxPerStageSampledImages = 128;
                        m_limits.MaxPerStageStorageImages = 8;
                    }
                }

                m_allocator.Initialize(m_device.Get());

                D3D12_COMMAND_QUEUE_DESC qDesc{};
                qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                if (!m_debug.CheckHResult(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue)), m_device.Get(), "CreateCommandQueue"))
                {
                    return Core::Result<bool, std::string>::Failure("CreateCommandQueue failed.");
                }
                DX12::DX12Debug::SetObjectName(m_commandQueue.Get(), "Morbi_DirectCommandQueue");

                if (!CreateDescriptorHeaps() || !CreateBindlessRootSignature())
                {
                    return Core::Result<bool, std::string>::Failure("Failed to create DX12 Bindless Descriptor Heaps or RootSignature.");
                }

                for (u32 i = 0; i < kMaxFramesInFlight; ++i)
                {
                    if (!m_debug.CheckHResult(
                            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frameAllocators[i])),
                            m_device.Get(),
                            "CreateCommandAllocator"))
                    {
                        return Core::Result<bool, std::string>::Failure("CreateCommandAllocator failed.");
                    }
                    DX12::DX12Debug::SetObjectName(m_frameAllocators[i].Get(), "Morbi_FrameCmdAllocator_" + std::to_string(i));
                }

                if (!m_debug.CheckHResult(
                        m_device->CreateCommandList(
                            0,
                            D3D12_COMMAND_LIST_TYPE_DIRECT,
                            m_frameAllocators[0].Get(),
                            nullptr,
                            IID_PPV_ARGS(&m_commandList)),
                        m_device.Get(),
                        "CreateCommandList"))
                {
                    return Core::Result<bool, std::string>::Failure("CreateCommandList (ID3D12GraphicsCommandList6) failed.");
                }
                DX12::DX12Debug::SetObjectName(m_commandList.Get(), "Morbi_MainGraphicsCommandList");
                m_commandList->Close();

                if (!m_debug.CheckHResult(
                        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_uploadAllocator)),
                        m_device.Get(),
                        "CreateUploadCommandAllocator") ||
                    !m_debug.CheckHResult(
                        m_device->CreateCommandList(
                            0,
                            D3D12_COMMAND_LIST_TYPE_DIRECT,
                            m_uploadAllocator.Get(),
                            nullptr,
                            IID_PPV_ARGS(&m_uploadCommandList)),
                        m_device.Get(),
                        "CreateUploadCommandList"))
                {
                    return Core::Result<bool, std::string>::Failure("CreateCommandList (Upload) failed.");
                }
                DX12::DX12Debug::SetObjectName(m_uploadAllocator.Get(), "Morbi_UploadCmdAllocator");
                DX12::DX12Debug::SetObjectName(m_uploadCommandList.Get(), "Morbi_UploadCommandList");
                m_uploadCommandList->Close();

                if (!m_debug.CheckHResult(
                        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
                        m_device.Get(),
                        "CreateFence"))
                {
                    return Core::Result<bool, std::string>::Failure("CreateFence failed.");
                }
                DX12::DX12Debug::SetObjectName(m_fence.Get(), "Morbi_FrameFence");
                m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                m_uploadFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

                if (windowHandle.Window != nullptr)
                {
                    if (!CreateSwapchain(static_cast<HWND>(windowHandle.Window), m_width, m_height))
                    {
                        return Core::Result<bool, std::string>::Failure("Failed to create DXGI SwapChain4.");
                    }
                }

                m_activeCmdList.BindContext(
                    this,
                    m_commandList.Get(),
                    m_frameAllocators[0].Get(),
                    m_rootSignature.Get(),
                    m_bindlessHeap.Get(),
                    m_width,
                    m_height,
                    false);

                Core::Log::Info("RHI.DX12", "Initialized DirectX 12 Device on GPU: " + m_adapterName +
                    " [ID3D12GraphicsCommandList6=ON, BindlessDescriptors=" + std::to_string(kMaxBindlessDescriptors) +
                    ", Validation=" + ((desc.EnableValidation || Core::Diagnostics::IsDebugMode()) ? "ON" : "OFF") +
                    ", DRED=" + ((desc.EnableValidation || Core::Diagnostics::IsDebugMode()) ? "FORCED_ON" : "OFF") + "]");
                return Core::Result<bool, std::string>::Success(true);
            }

            void Shutdown() override
            {
                if (!m_device)
                {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_frameOpen = false;
                }

                // 1. Wait for all submitted GPU work to finish
                if (m_commandQueue && m_fence)
                {
                    u64 signalVal = 0;
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        signalVal = ++m_nextFenceValue;
                    }
                    {
                        std::lock_guard<std::mutex> submitLock(m_submitMutex);
                        m_commandQueue->Signal(m_fence.Get(), signalVal);
                    }
                    if (m_fence->GetCompletedValue() < signalVal)
                    {
                        m_fence->SetEventOnCompletion(signalVal, static_cast<HANDLE>(m_fenceEvent));
                        WaitForSingleObject(static_cast<HANDLE>(m_fenceEvent), INFINITE);
                    }
                }

                // 2. Release all unsubmitted command list references
                m_activeCmdList.Discard();
                {
                    std::vector<DX12CommandList*> createdLists;
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        createdLists = std::move(m_createdCommandLists);
                        m_createdCommandLists.clear();
                    }
                    for (DX12CommandList* cmd : createdLists)
                    {
                        if (cmd)
                        {
                            cmd->ShutdownInternal();
                        }
                    }
                }

                // 3. Drain all deferred destructions
                ProcessDeferredDestructions(UINT64_MAX);

                // 4. Clear all remaining resources under lock
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_swapchainTextures.clear();
                    m_buffers.Clear();
                    m_textures.Clear();
                    m_pipelines.Clear();
                    m_pipelineLayouts.Clear();
                    m_pipelineLayoutCache.clear();
                    m_defaultBindlessPipelineLayout = PipelineLayoutHandle::Invalid();
                    m_layoutStats = {};
                    m_deferredDestructions.clear();
                    m_freeBindlessBufferSlots.clear();
                    m_freeBindlessTextureSlots.clear();
                    m_freeBindlessUniformSlots.clear();
                    m_freeRtvSlots.clear();
                    m_freeDsvSlots.clear();
                    m_nextBindlessBufferSlot = 0;
                    m_nextBindlessTextureSlot = 0;
                    m_nextBindlessUniformSlot = 0;
                    m_nextRtvSlot = 0;
                    m_nextDsvSlot = 0;
                }

                if (m_fenceEvent)
                {
                    CloseHandle(static_cast<HANDLE>(m_fenceEvent));
                    m_fenceEvent = nullptr;
                }
                if (m_uploadFenceEvent)
                {
                    CloseHandle(static_cast<HANDLE>(m_uploadFenceEvent));
                    m_uploadFenceEvent = nullptr;
                }

                m_swapchain.Reset();
                m_uploadCommandList.Reset();
                m_uploadAllocator.Reset();
                m_commandList.Reset();
                for (auto& alloc : m_frameAllocators)
                {
                    alloc.Reset();
                }
                m_fence.Reset();
                m_rootSignature.Reset();
                m_bindlessHeap.Reset();
                m_rtvHeap.Reset();
                m_dsvHeap.Reset();
                m_commandQueue.Reset();
                m_allocator.Shutdown();
                m_debug.Shutdown(m_device.Get());
                m_device.Reset();
                m_factory.Reset();
            }

            bool BeginFrame() override
            {
                const u64 targetFence = m_frameFenceValues[m_frameIndex];
                if (m_fence->GetCompletedValue() < targetFence)
                {
                    m_fence->SetEventOnCompletion(targetFence, static_cast<HANDLE>(m_fenceEvent));
                    WaitForSingleObject(static_cast<HANDLE>(m_fenceEvent), INFINITE);
                }

                // Process any deferred destructions that have completed on GPU
                ProcessDeferredDestructions(GetCompletedTimelineValue());

                if (m_swapchain)
                {
                    m_backbufferIndex = m_swapchain->GetCurrentBackBufferIndex();
                }

                m_activeCmdList.Discard();
                m_activeCmdList.BindContext(
                    this,
                    m_commandList.Get(),
                    m_frameAllocators[m_frameIndex].Get(),
                    m_rootSignature.Get(),
                    m_bindlessHeap.Get(),
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
                m_activeCmdList.End();

                const u64 signalVal = CommitCommandListSubmission(
                    true,
                    m_activeCmdList.GetReferencedBuffers(),
                    m_activeCmdList.GetReferencedTextures(),
                    m_activeCmdList.GetReferencedPipelines(),
                    m_activeCmdList.GetReferencedPipelineLayouts());
                m_frameFenceValues[m_frameIndex] = signalVal;
                m_activeCmdList.OnSubmitted(signalVal);

                {
                    std::lock_guard<std::mutex> submitLock(m_submitMutex);
                    ID3D12CommandList* lists[] = { m_commandList.Get() };
                    m_commandQueue->ExecuteCommandLists(1, lists);
                    if (Core::Diagnostics::IsDebugMode())
                    {
                        m_debug.CheckHResult(m_device->GetDeviceRemovedReason(), m_device.Get(), "ExecuteCommandLists");
                    }

                    if (m_swapchain)
                    {
                        const UINT syncInterval = m_vsync ? 1u : 0u;
                        const UINT presentFlags = (!m_vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
                        const HRESULT presentHr = m_swapchain->Present(syncInterval, presentFlags);
                        m_debug.CheckHResult(presentHr, m_device.Get(), "SwapChain::Present");
                    }

                    const HRESULT signalHr = m_commandQueue->Signal(m_fence.Get(), signalVal);
                    m_debug.CheckHResult(signalHr, m_device.Get(), "CommandQueue::Signal");
                }

                m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
            }

            void ResizeSwapchain(u32 width, u32 height) override
            {
                if (width == 0 || height == 0 || !m_swapchain)
                {
                    return;
                }
                WaitIdle();
                m_width = width;
                m_height = height;

                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    for (TextureHandle h : m_swapchainTextures)
                    {
                        if (auto* rec = m_textures.Get(h))
                        {
                            if (rec->HasRtv && rec->RtvSlot != 0xFFFFFFFFu)
                            {
                                m_freeRtvSlots.push_back(rec->RtvSlot);
                            }
                            m_textures.Remove(h);
                        }
                    }
                    m_swapchainTextures.clear();
                }

                const UINT swapFlags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
                const HRESULT resizeHr = m_swapchain->ResizeBuffers(
                    kMaxFramesInFlight, m_width, m_height, DXGI_FORMAT_B8G8R8A8_UNORM, swapFlags);
                m_debug.CheckHResult(resizeHr, m_device.Get(), "SwapChain::ResizeBuffers");
                m_backbufferIndex = m_swapchain->GetCurrentBackBufferIndex();
                CreateSwapchainBackbuffers();
            }

            void SetVSync(bool enabled) override
            {
                m_vsync = enabled;
            }

            bool IsVSyncEnabled() const noexcept override { return m_vsync; }

            void WaitIdle() override
            {
                if (!m_commandQueue || !m_fence)
                {
                    return;
                }
                u64 signalVal = 0;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    signalVal = ++m_nextFenceValue;
                }
                {
                    std::lock_guard<std::mutex> submitLock(m_submitMutex);
                    m_commandQueue->Signal(m_fence.Get(), signalVal);
                }
                if (m_fence->GetCompletedValue() < signalVal)
                {
                    m_fence->SetEventOnCompletion(signalVal, static_cast<HANDLE>(m_fenceEvent));
                    WaitForSingleObject(static_cast<HANDLE>(m_fenceEvent), INFINITE);
                }
                // After device idle, all submitted GPU work is complete - drain eligible deferred destructions
                ProcessDeferredDestructions(UINT64_MAX);
            }

            u64 GetCompletedTimelineValue() const noexcept override
            {
                return m_fence ? m_fence->GetCompletedValue() : 0ULL;
            }

            void ProcessCompletedDestructions() override
            {
                ProcessDeferredDestructions(GetCompletedTimelineValue());
            }

            void ProcessDeferredDestructions(u64 completedFenceValue)
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
                            const u64 requiredFence = std::max(it->CompletionFenceValue, rec->LastUsedFenceValue);
                            it->CompletionFenceValue = requiredFence;
                            if (rec->UnsubmittedRefCount == 0 && requiredFence <= completedFenceValue)
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
                            const u64 requiredFence = std::max(it->CompletionFenceValue, rec->LastUsedFenceValue);
                            it->CompletionFenceValue = requiredFence;
                            if (rec->UnsubmittedRefCount == 0 && requiredFence <= completedFenceValue)
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
                            const u64 requiredFence = std::max(it->CompletionFenceValue, rec->LastUsedFenceValue);
                            it->CompletionFenceValue = requiredFence;
                            if (rec->UnsubmittedRefCount == 0 && requiredFence <= completedFenceValue)
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
                            const u64 requiredFence = std::max(it->CompletionFenceValue, rec->LastUsedFenceValue);
                            it->CompletionFenceValue = requiredFence;
                            if (rec->PipelineRefCount == 0 && rec->UnsubmittedRefCount == 0 && requiredFence <= completedFenceValue)
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
                if (!m_device)
                {
                    return nullptr;
                }

                ComPtr<ID3D12CommandAllocator> alloc;
                if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
                {
                    return nullptr;
                }

                ComPtr<ID3D12GraphicsCommandList6> cmd;
                if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd))))
                {
                    return nullptr;
                }
                cmd->Close();

                auto cmdList = std::make_unique<DX12CommandList>();
                cmdList->BindContext(
                    this,
                    cmd.Get(),
                    alloc.Get(),
                    m_rootSignature.Get(),
                    m_bindlessHeap.Get(),
                    m_width,
                    m_height,
                    true,
                    std::move(alloc),
                    std::move(cmd));

                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    m_createdCommandLists.push_back(cmdList.get());
                }
                return cmdList;
            }

            u64 SubmitCommandList(ICommandList& cmdList) override
            {
                auto& dxCmd = static_cast<DX12CommandList&>(cmdList);
                if (dxCmd.IsRecording())
                {
                    dxCmd.End();
                }

                ID3D12GraphicsCommandList6* nativeCmd = dxCmd.GetD3D12CommandList();
                if (!nativeCmd || !m_commandQueue || !m_fence)
                {
                    dxCmd.Discard();
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (&dxCmd == &m_activeCmdList)
                    {
                        m_frameOpen = false;
                    }
                    return m_nextFenceValue;
                }

                const u64 signalVal = CommitCommandListSubmission(
                    &dxCmd == &m_activeCmdList,
                    dxCmd.GetReferencedBuffers(),
                    dxCmd.GetReferencedTextures(),
                    dxCmd.GetReferencedPipelines(),
                    dxCmd.GetReferencedPipelineLayouts());
                dxCmd.OnSubmitted(signalVal);

                {
                    std::lock_guard<std::mutex> submitLock(m_submitMutex);
                    ID3D12CommandList* lists[] = { nativeCmd };
                    m_commandQueue->ExecuteCommandLists(1, lists);
                    if (Core::Diagnostics::IsDebugMode())
                    {
                        m_debug.CheckHResult(m_device->GetDeviceRemovedReason(), m_device.Get(), "SubmitCommandList::ExecuteCommandLists");
                    }

                    const HRESULT signalHr = m_commandQueue->Signal(m_fence.Get(), signalVal);
                    m_debug.CheckHResult(signalHr, m_device.Get(), "SubmitCommandList::Signal");
                }
                return signalVal;
            }

            TextureHandle GetCurrentBackbuffer() const noexcept override
            {
                if (m_backbufferIndex < m_swapchainTextures.size())
                {
                    return m_swapchainTextures[m_backbufferIndex];
                }
                return TextureHandle::Invalid();
            }

            Format GetSwapchainFormat() const noexcept override { return Format::BGRA8_UNorm; }

            BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData) override
            {
                D3D12_HEAP_PROPERTIES heapProps{};
                heapProps.Type = (desc.Memory == MemoryUsage::GpuOnly && initialData == nullptr)
                    ? D3D12_HEAP_TYPE_DEFAULT
                    : D3D12_HEAP_TYPE_UPLOAD;

                const bool isUniformBuffer = HasFlag(desc.Usage, BufferUsage::Uniform);
                const u64 rawWidth = std::max<u64>(desc.SizeBytes, 16ULL);
                const u64 alignedWidth = isUniformBuffer ? ((rawWidth + 255ULL) & ~255ULL) : rawWidth;

                D3D12_RESOURCE_DESC resDesc{};
                resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                resDesc.Width = alignedWidth;
                resDesc.Height = 1;
                resDesc.DepthOrArraySize = 1;
                resDesc.MipLevels = 1;
                resDesc.Format = DXGI_FORMAT_UNKNOWN;
                resDesc.SampleDesc.Count = 1;
                resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                const D3D12_RESOURCE_STATES initState = (heapProps.Type == D3D12_HEAP_TYPE_UPLOAD)
                    ? D3D12_RESOURCE_STATE_GENERIC_READ
                    : D3D12_RESOURCE_STATE_COMMON;

                ComPtr<ID3D12Resource> resource;
                u64 allocBytes = 0;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (!m_allocator.CreateResource(
                        heapProps,
                        resDesc,
                        initState,
                        nullptr,
                        desc.AliasingPoolOffset,
                        resource,
                        allocBytes))
                    {
                        m_debug.CheckHResult(m_device->GetDeviceRemovedReason(), m_device.Get(), "CreateBuffer");
                        return BufferHandle::Invalid();
                    }
                }
                DX12::DX12Debug::SetObjectName(resource.Get(), desc.DebugName);

                if (initialData && heapProps.Type == D3D12_HEAP_TYPE_UPLOAD)
                {
                    void* mapped = nullptr;
                    D3D12_RANGE readRange{ 0, 0 };
                    if (SUCCEEDED(resource->Map(0, &readRange, &mapped)))
                    {
                        std::memcpy(mapped, initialData, static_cast<usize>(desc.SizeBytes));
                        resource->Unmap(0, nullptr);
                    }
                }

                std::lock_guard<std::mutex> lock(m_resourceMutex);

                BindlessResourceIndex bindless{};
                const bool isUniformBindless = desc.Bindless && isUniformBuffer;
                if (isUniformBindless && (m_nextBindlessUniformSlot < kMaxBindlessUniformBuffers || !m_freeBindlessUniformSlots.empty()))
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
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
                    cbvDesc.BufferLocation = resource->GetGPUVirtualAddress();
                    cbvDesc.SizeInBytes = static_cast<UINT>(resDesc.Width);

                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_bindlessHeap->GetCPUDescriptorHandleForHeapStart();
                    cpuHandle.ptr += static_cast<SIZE_T>(2u * kMaxBindlessDescriptors + bindless.Index) * m_cbvSrvUavDescriptorSize;
                    m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);
                }
                else if (desc.Bindless && !isUniformBuffer && (m_nextBindlessBufferSlot < kMaxBindlessDescriptors || !m_freeBindlessBufferSlots.empty()))
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
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    if (desc.StrideBytes > 0)
                    {
                        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                        srvDesc.Buffer.FirstElement = 0;
                        srvDesc.Buffer.NumElements = static_cast<UINT>(resDesc.Width / desc.StrideBytes);
                        srvDesc.Buffer.StructureByteStride = desc.StrideBytes;
                    }
                    else
                    {
                        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                        srvDesc.Buffer.FirstElement = 0;
                        srvDesc.Buffer.NumElements = static_cast<UINT>(resDesc.Width / 4);
                        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                    }

                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_bindlessHeap->GetCPUDescriptorHandleForHeapStart();
                    cpuHandle.ptr += static_cast<SIZE_T>(bindless.Index) * m_cbvSrvUavDescriptorSize;
                    m_device->CreateShaderResourceView(resource.Get(), &srvDesc, cpuHandle);
                }

                return m_buffers.Emplace(DX12BufferRecord{
                    .Desc = desc,
                    .Resource = std::move(resource),
                    .AllocatedBytes = allocBytes,
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
                const u64 minSafeFence = m_frameOpen ? (m_nextFenceValue + 1) : m_nextFenceValue;
                const u64 targetFence = std::max(rec->LastUsedFenceValue, minSafeFence);

                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::Buffer,
                    .CompletionFenceValue = targetFence,
                    .BufferHandle = handle
                });
            }

            void DestroyBufferInternalLocked(BufferHandle handle)
            {
                if (const auto* rec = m_buffers.Get(handle))
                {
                    if (rec->Bindless.IsValid())
                    {
                        const bool isUniformBindless = HasFlag(rec->Desc.Usage, BufferUsage::Uniform);
                        if (isUniformBindless && rec->Bindless.Index < kMaxBindlessUniformBuffers)
                        {
                            m_freeBindlessUniformSlots.push_back(rec->Bindless.Index);
                        }
                        else if (!isUniformBindless && rec->Bindless.Index < kMaxBindlessDescriptors)
                        {
                            m_freeBindlessBufferSlots.push_back(rec->Bindless.Index);
                        }
                    }
                    m_allocator.OnFreeResource(rec->AllocatedBytes);
                    m_buffers.Remove(handle);
                }
            }

            void UpdateBufferData(BufferHandle handle, const void* data, usize sizeBytes, usize offsetBytes = 0) override
            {
                if (!data || sizeBytes == 0)
                {
                    return;
                }
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_buffers.Get(handle);
                if (!rec || !rec->Resource)
                {
                    return;
                }
                if (offsetBytes >= rec->Desc.SizeBytes)
                {
                    return;
                }

                const usize bytesToCopy = std::min<usize>(sizeBytes, static_cast<usize>(rec->Desc.SizeBytes) - offsetBytes);
                void* mapped = nullptr;
                D3D12_RANGE readRange{ 0, 0 };
                if (SUCCEEDED(rec->Resource->Map(0, &readRange, &mapped)))
                {
                    std::memcpy(static_cast<u8*>(mapped) + offsetBytes, data, bytesToCopy);
                    D3D12_RANGE writeRange{ offsetBytes, offsetBytes + bytesToCopy };
                    rec->Resource->Unmap(0, &writeRange);
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
                D3D12_HEAP_PROPERTIES heapProps{};
                heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

                D3D12_RESOURCE_DESC resDesc{};
                resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                resDesc.Width = std::max<u32>(desc.Width, 1);
                resDesc.Height = std::max<u32>(desc.Height, 1);
                resDesc.DepthOrArraySize = static_cast<UINT16>(std::max<u32>(desc.DepthOrArrayLayers, 1));
                resDesc.MipLevels = static_cast<UINT16>(std::max<u32>(desc.MipLevels, 1));
                resDesc.Format = ToDxgiFormat(desc.PixelFormat);
                resDesc.SampleDesc.Count = 1;
                resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                if (desc.RenderTarget) resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                if (desc.DepthStencil) resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                if (desc.Storage)      resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                D3D12_CLEAR_VALUE clearVal{};
                const D3D12_CLEAR_VALUE* pClearVal = nullptr;
                D3D12_RESOURCE_STATES initialState = (initialData != nullptr && !desc.DepthStencil && !desc.RenderTarget)
                    ? D3D12_RESOURCE_STATE_COPY_DEST
                    : D3D12_RESOURCE_STATE_COMMON;

                if (desc.RenderTarget)
                {
                    clearVal.Format = resDesc.Format;
                    clearVal.Color[0] = 0.06f;
                    clearVal.Color[1] = 0.08f;
                    clearVal.Color[2] = 0.12f;
                    clearVal.Color[3] = 1.0f;
                    pClearVal = &clearVal;
                    initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
                }
                else if (desc.DepthStencil)
                {
                    clearVal.Format = resDesc.Format;
                    clearVal.DepthStencil.Depth = 1.0f;
                    clearVal.DepthStencil.Stencil = 0;
                    pClearVal = &clearVal;
                    initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                }

                ComPtr<ID3D12Resource> resource;
                u64 allocBytes = 0;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (!m_allocator.CreateResource(
                        heapProps,
                        resDesc,
                        initialState,
                        pClearVal,
                        desc.AliasingPoolOffset,
                        resource,
                        allocBytes))
                    {
                        m_debug.CheckHResult(m_device->GetDeviceRemovedReason(), m_device.Get(), "CreateTexture");
                        return TextureHandle::Invalid();
                    }
                }
                DX12::DX12Debug::SetObjectName(resource.Get(), desc.DebugName);

                if (initialData != nullptr && !desc.DepthStencil && !desc.RenderTarget)
                {
                    const u32 width = static_cast<u32>(resDesc.Width);
                    const u32 height = static_cast<u32>(resDesc.Height);
                    const u32 srcRowBytes = width * 4u;
                    const u32 alignedRowPitch = (srcRowBytes + 255u) & ~255u;
                    const u64 uploadBufferSize = static_cast<u64>(alignedRowPitch) * static_cast<u64>(height);

                    D3D12_HEAP_PROPERTIES uploadHeapProps{};
                    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

                    D3D12_RESOURCE_DESC uploadDesc{};
                    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    uploadDesc.Width = std::max<u64>(uploadBufferSize, 256ULL);
                    uploadDesc.Height = 1;
                    uploadDesc.DepthOrArraySize = 1;
                    uploadDesc.MipLevels = 1;
                    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
                    uploadDesc.SampleDesc.Count = 1;
                    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                    ComPtr<ID3D12Resource> uploadResource;
                    if (SUCCEEDED(m_device->CreateCommittedResource(
                        &uploadHeapProps,
                        D3D12_HEAP_FLAG_NONE,
                        &uploadDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&uploadResource))))
                    {
                        u8* mapped = nullptr;
                        D3D12_RANGE readRange{ 0, 0 };
                        if (SUCCEEDED(uploadResource->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
                        {
                            const u8* srcBytes = static_cast<const u8*>(initialData);
                            for (u32 y = 0; y < height; ++y)
                            {
                                std::memcpy(
                                    mapped + static_cast<usize>(y) * alignedRowPitch,
                                    srcBytes + static_cast<usize>(y) * srcRowBytes,
                                    srcRowBytes);
                            }
                            uploadResource->Unmap(0, nullptr);

                            std::lock_guard<std::mutex> uploadLock(m_uploadMutex);
                            m_uploadAllocator->Reset();
                            m_uploadCommandList->Reset(m_uploadAllocator.Get(), nullptr);

                            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
                            dstLoc.pResource = resource.Get();
                            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                            dstLoc.SubresourceIndex = 0;

                            D3D12_TEXTURE_COPY_LOCATION srcLoc{};
                            srcLoc.pResource = uploadResource.Get();
                            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                            srcLoc.PlacedFootprint.Offset = 0;
                            srcLoc.PlacedFootprint.Footprint.Format = resDesc.Format;
                            srcLoc.PlacedFootprint.Footprint.Width = width;
                            srcLoc.PlacedFootprint.Footprint.Height = height;
                            srcLoc.PlacedFootprint.Footprint.Depth = 1;
                            srcLoc.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

                            m_uploadCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

                            D3D12_RESOURCE_BARRIER barrier{};
                            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            barrier.Transition.pResource = resource.Get();
                            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                            m_uploadCommandList->ResourceBarrier(1, &barrier);

                            m_uploadCommandList->Close();
                            u64 uploadFenceVal = 0;
                            {
                                std::lock_guard<std::mutex> lock(m_resourceMutex);
                                uploadFenceVal = ++m_nextFenceValue;
                            }
                            {
                                std::lock_guard<std::mutex> submitLock(m_submitMutex);
                                ID3D12CommandList* lists[] = { m_uploadCommandList.Get() };
                                m_commandQueue->ExecuteCommandLists(1, lists);
                                if (Core::Diagnostics::IsDebugMode())
                                {
                                    m_debug.CheckHResult(m_device->GetDeviceRemovedReason(), m_device.Get(), "CreateTexture::ExecuteCommandLists");
                                }
                                m_commandQueue->Signal(m_fence.Get(), uploadFenceVal);
                            }
                            if (m_fence->GetCompletedValue() < uploadFenceVal)
                            {
                                void* waitEvent = m_uploadFenceEvent ? m_uploadFenceEvent : m_fenceEvent;
                                m_fence->SetEventOnCompletion(uploadFenceVal, static_cast<HANDLE>(waitEvent));
                                WaitForSingleObject(static_cast<HANDLE>(waitEvent), INFINITE);
                            }

                            initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                        }
                    }
                }

                std::lock_guard<std::mutex> lock(m_resourceMutex);

                DX12TextureRecord rec{};
                rec.Desc = desc;
                rec.Resource = std::move(resource);
                rec.AllocatedBytes = allocBytes;
                rec.CurrentState = initialState;

                if (desc.RenderTarget && (!m_freeRtvSlots.empty() || m_nextRtvSlot < kMaxRtvDescriptors))
                {
                    if (!m_freeRtvSlots.empty())
                    {
                        rec.RtvSlot = m_freeRtvSlots.back();
                        m_freeRtvSlots.pop_back();
                    }
                    else
                    {
                        rec.RtvSlot = m_nextRtvSlot++;
                    }
                    rec.RtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
                    rec.RtvHandle.ptr += static_cast<SIZE_T>(rec.RtvSlot) * m_rtvDescriptorSize;
                    m_device->CreateRenderTargetView(rec.Resource.Get(), nullptr, rec.RtvHandle);
                    rec.HasRtv = true;
                }

                if (desc.DepthStencil && (!m_freeDsvSlots.empty() || m_nextDsvSlot < kMaxDsvDescriptors))
                {
                    if (!m_freeDsvSlots.empty())
                    {
                        rec.DsvSlot = m_freeDsvSlots.back();
                        m_freeDsvSlots.pop_back();
                    }
                    else
                    {
                        rec.DsvSlot = m_nextDsvSlot++;
                    }
                    rec.DsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
                    rec.DsvHandle.ptr += static_cast<SIZE_T>(rec.DsvSlot) * m_dsvDescriptorSize;
                    m_device->CreateDepthStencilView(rec.Resource.Get(), nullptr, rec.DsvHandle);
                    rec.HasDsv = true;
                }

                if (!desc.DepthStencil && (!m_freeBindlessTextureSlots.empty() || m_nextBindlessTextureSlot < kMaxBindlessDescriptors))
                {
                    if (!m_freeBindlessTextureSlots.empty())
                    {
                        rec.Bindless.Index = m_freeBindlessTextureSlots.back();
                        m_freeBindlessTextureSlots.pop_back();
                    }
                    else
                    {
                        rec.Bindless.Index = m_nextBindlessTextureSlot++;
                    }
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                    srvDesc.Format = resDesc.Format;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Texture2D.MipLevels = resDesc.MipLevels;

                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_bindlessHeap->GetCPUDescriptorHandleForHeapStart();
                    cpuHandle.ptr += static_cast<SIZE_T>(kMaxBindlessDescriptors + rec.Bindless.Index) * m_cbvSrvUavDescriptorSize;
                    m_device->CreateShaderResourceView(rec.Resource.Get(), &srvDesc, cpuHandle);
                }

                return m_textures.Emplace(std::move(rec));
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
                const u64 minSafeFence = m_frameOpen ? (m_nextFenceValue + 1) : m_nextFenceValue;
                const u64 targetFence = std::max(rec->LastUsedFenceValue, minSafeFence);

                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::Texture,
                    .CompletionFenceValue = targetFence,
                    .TextureHandle = handle
                });
            }

            void DestroyTextureInternalLocked(TextureHandle handle)
            {
                if (const auto* rec = m_textures.Get(handle))
                {
                    if (rec->HasRtv && rec->RtvSlot != 0xFFFFFFFFu)
                    {
                        m_freeRtvSlots.push_back(rec->RtvSlot);
                    }
                    if (rec->HasDsv && rec->DsvSlot != 0xFFFFFFFFu)
                    {
                        m_freeDsvSlots.push_back(rec->DsvSlot);
                    }
                    if (rec->Bindless.IsValid() && rec->Bindless.Index < kMaxBindlessDescriptors)
                    {
                        m_freeBindlessTextureSlots.push_back(rec->Bindless.Index);
                    }
                    m_allocator.OnFreeResource(rec->AllocatedBytes);
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
                if (!m_device)
                {
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure("DX12 device is not initialized.");
                }

                const PipelineLayoutDesc normalized = PipelineLayoutValidator::Normalize(desc);
                const PipelineLayoutValidationResult validation =
                    PipelineLayoutValidator::Validate(normalized, m_limits, BackendAPI::DirectX12);
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
                        errMsg = "PipelineLayout validation failed for DX12 backend.";
                    }
                    Core::Log::Error("RHI.DX12", "CreatePipelineLayout rejected: " + errMsg);
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
                                rec && !rec->PendingDestroy && rec->RootSignature &&
                                PipelineLayoutValidator::AreStructurallyEqual(rec->Desc, normalized))
                            {
                                ++rec->CacheRefCount;
                                ++m_layoutStats.CacheHitCount;
                                return Core::Result<PipelineLayoutHandle, std::string>::Success(candidate);
                            }
                        }
                    }
                }

                std::vector<D3D12_ROOT_PARAMETER> rootParams;
                std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> tableRanges;
                std::vector<usize> tableParamIndices;
                std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
                std::unordered_set<u64> emittedRootRegs;
                std::unordered_set<u64> staticSamplerKeys;

                std::optional<u32> rootConstantParamIndex;
                u32 maxPushBytes = 0;
                std::optional<u32> bindlessTableSlot;
                bool usesGlobalBindlessHeap = false;
                u32 totalDwords = 0;

                staticSamplers.reserve(normalized.StaticSamplers.size());
                for (const auto& ss : normalized.StaticSamplers)
                {
                    staticSamplerKeys.insert((static_cast<u64>(ss.Set) << 32) | ss.Binding);
                    D3D12_STATIC_SAMPLER_DESC dss{};
                    dss.Filter = ToD3D12Filter(ss.Filter, ss.IsComparison);
                    dss.AddressU = ToD3D12AddressMode(ss.AddressMode);
                    dss.AddressV = ToD3D12AddressMode(ss.AddressMode);
                    dss.AddressW = ToD3D12AddressMode(ss.AddressMode);
                    dss.MipLODBias = 0.0f;
                    dss.MaxAnisotropy = (ss.Filter == StaticSamplerFilter::Anisotropic) ? 16u : 1u;
                    dss.ComparisonFunc = ss.IsComparison ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
                    dss.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
                    dss.MinLOD = 0.0f;
                    dss.MaxLOD = D3D12_FLOAT32_MAX;
                    dss.ShaderRegister = ss.DxilRegister.value_or(ss.Binding);
                    dss.RegisterSpace = ss.DxilSpace.value_or(ss.Set);
                    dss.ShaderVisibility = ToD3D12ShaderVisibility(ss.Visibility);
                    staticSamplers.push_back(dss);
                }

                for (const auto& pc : normalized.PushConstants)
                {
                    const u32 numDwords = (pc.SizeBytes + 3u) / 4u;
                    if (numDwords == 0)
                    {
                        continue;
                    }
                    const u32 reg = pc.DxilRegister.value_or(0);
                    const u32 space = pc.DxilSpace.value_or(0);
                    emittedRootRegs.insert((static_cast<u64>(space) << 32) | reg);

                    if (!rootConstantParamIndex.has_value())
                    {
                        rootConstantParamIndex = static_cast<u32>(rootParams.size());
                    }
                    maxPushBytes = std::max(maxPushBytes, pc.OffsetBytes + pc.SizeBytes);

                    D3D12_ROOT_PARAMETER rp{};
                    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                    rp.Constants.ShaderRegister = reg;
                    rp.Constants.RegisterSpace = space;
                    rp.Constants.Num32BitValues = numDwords;
                    rp.ShaderVisibility = ToD3D12ShaderVisibility(pc.Visibility);
                    rootParams.push_back(rp);
                    totalDwords += numDwords;
                }

                if (normalized.Mode == PipelineLayoutMode::GlobalBindlessHybrid)
                {
                    bindlessTableSlot = static_cast<u32>(rootParams.size());
                    usesGlobalBindlessHeap = true;

                    std::vector<D3D12_DESCRIPTOR_RANGE> srvRanges(2);
                    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    srvRanges[0].NumDescriptors = kMaxBindlessDescriptors;
                    srvRanges[0].BaseShaderRegister = 0;
                    srvRanges[0].RegisterSpace = 0;
                    srvRanges[0].OffsetInDescriptorsFromTableStart = 0;

                    srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    srvRanges[1].NumDescriptors = kMaxBindlessDescriptors;
                    srvRanges[1].BaseShaderRegister = 0;
                    srvRanges[1].RegisterSpace = 1;
                    srvRanges[1].OffsetInDescriptorsFromTableStart = kMaxBindlessDescriptors;

                    D3D12_ROOT_PARAMETER rp{};
                    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    rp.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(srvRanges.size());
                    rp.DescriptorTable.pDescriptorRanges = nullptr;
                    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                    tableParamIndices.push_back(rootParams.size());
                    tableRanges.push_back(std::move(srvRanges));
                    rootParams.push_back(rp);
                    totalDwords += 1;

                    constexpr u64 kDefaultRootCbvKey = (0ULL << 32) | 1ULL;
                    if (!emittedRootRegs.contains(kDefaultRootCbvKey))
                    {
                        emittedRootRegs.insert(kDefaultRootCbvKey);
                        D3D12_ROOT_PARAMETER cbvRp{};
                        cbvRp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                        cbvRp.Descriptor.ShaderRegister = 1;
                        cbvRp.Descriptor.RegisterSpace = 0;
                        cbvRp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                        rootParams.push_back(cbvRp);
                        totalDwords += 2;
                    }

                    const bool hasDefaultSampler0 = std::any_of(
                        staticSamplers.begin(),
                        staticSamplers.end(),
                        [](const D3D12_STATIC_SAMPLER_DESC& s) {
                            return s.ShaderRegister == 0 && s.RegisterSpace == 0;
                        });
                    if (!hasDefaultSampler0)
                    {
                        D3D12_STATIC_SAMPLER_DESC dss{};
                        dss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                        dss.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                        dss.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                        dss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                        dss.MipLODBias = 0.0f;
                        dss.MaxAnisotropy = 1;
                        dss.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
                        dss.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
                        dss.MinLOD = 0.0f;
                        dss.MaxLOD = D3D12_FLOAT32_MAX;
                        dss.ShaderRegister = 0;
                        dss.RegisterSpace = 0;
                        dss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                        staticSamplers.push_back(dss);
                    }
                }

                auto computeTableVisibility = [](const std::vector<ShaderVisibility>& visList) -> D3D12_SHADER_VISIBILITY {
                    if (visList.empty())
                    {
                        return D3D12_SHADER_VISIBILITY_ALL;
                    }
                    const ShaderVisibility first = visList[0];
                    for (usize i = 1; i < visList.size(); ++i)
                    {
                        if (visList[i] != first)
                        {
                            return D3D12_SHADER_VISIBILITY_ALL;
                        }
                    }
                    return ToD3D12ShaderVisibility(first);
                };

                for (const auto& setDesc : normalized.Sets)
                {
                    if (normalized.Mode == PipelineLayoutMode::GlobalBindlessHybrid && setDesc.SetIndex == 0)
                    {
                        continue;
                    }

                    std::vector<D3D12_DESCRIPTOR_RANGE> cbvSrvUavRanges;
                    std::vector<ShaderVisibility> cbvSrvUavVis;
                    std::vector<D3D12_DESCRIPTOR_RANGE> samplerRanges;
                    std::vector<ShaderVisibility> samplerVis;

                    for (const auto& b : setDesc.Bindings)
                    {
                        if (b.Type == DescriptorType::Sampler && b.Count == 1 &&
                            staticSamplerKeys.contains((static_cast<u64>(setDesc.SetIndex) << 32) | b.Binding))
                        {
                            continue;
                        }

                        const u32 reg = b.DxilRegister.value_or(b.Binding);
                        const u32 space = b.DxilSpace.value_or(setDesc.SetIndex);

                        if (b.PreferRootDescriptorOnDx12 && b.Count == 1 && !b.IsUnboundedArray)
                        {
                            if (b.Type == DescriptorType::UniformBuffer)
                            {
                                emittedRootRegs.insert((static_cast<u64>(space) << 32) | reg);
                                D3D12_ROOT_PARAMETER rp{};
                                rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                                rp.Descriptor.ShaderRegister = reg;
                                rp.Descriptor.RegisterSpace = space;
                                rp.ShaderVisibility = ToD3D12ShaderVisibility(b.Visibility);
                                rootParams.push_back(rp);
                                totalDwords += 2;
                                continue;
                            }
                            if (b.Type == DescriptorType::StorageBuffer)
                            {
                                D3D12_ROOT_PARAMETER rp{};
                                rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                                rp.Descriptor.ShaderRegister = reg;
                                rp.Descriptor.RegisterSpace = space;
                                rp.ShaderVisibility = ToD3D12ShaderVisibility(b.Visibility);
                                rootParams.push_back(rp);
                                totalDwords += 2;
                                continue;
                            }
                            if (b.Type == DescriptorType::RWStorageBuffer)
                            {
                                D3D12_ROOT_PARAMETER rp{};
                                rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
                                rp.Descriptor.ShaderRegister = reg;
                                rp.Descriptor.RegisterSpace = space;
                                rp.ShaderVisibility = ToD3D12ShaderVisibility(b.Visibility);
                                rootParams.push_back(rp);
                                totalDwords += 2;
                                continue;
                            }
                        }

                        D3D12_DESCRIPTOR_RANGE range{};
                        range.RangeType = ToD3D12DescriptorRangeType(b.Type);
                        range.NumDescriptors = b.IsUnboundedArray ? UINT_MAX : std::max<u32>(b.Count, 1u);
                        range.BaseShaderRegister = reg;
                        range.RegisterSpace = space;
                        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                        if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                        {
                            samplerRanges.push_back(range);
                            samplerVis.push_back(b.Visibility);
                        }
                        else
                        {
                            cbvSrvUavRanges.push_back(range);
                            cbvSrvUavVis.push_back(b.Visibility);
                        }
                    }

                    if (!cbvSrvUavRanges.empty())
                    {
                        D3D12_ROOT_PARAMETER rp{};
                        rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                        rp.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(cbvSrvUavRanges.size());
                        rp.DescriptorTable.pDescriptorRanges = nullptr;
                        rp.ShaderVisibility = computeTableVisibility(cbvSrvUavVis);
                        tableParamIndices.push_back(rootParams.size());
                        tableRanges.push_back(std::move(cbvSrvUavRanges));
                        rootParams.push_back(rp);
                        totalDwords += 1;
                    }

                    if (!samplerRanges.empty())
                    {
                        D3D12_ROOT_PARAMETER rp{};
                        rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                        rp.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(samplerRanges.size());
                        rp.DescriptorTable.pDescriptorRanges = nullptr;
                        rp.ShaderVisibility = computeTableVisibility(samplerVis);
                        tableParamIndices.push_back(rootParams.size());
                        tableRanges.push_back(std::move(samplerRanges));
                        rootParams.push_back(rp);
                        totalDwords += 1;
                    }
                }

                for (const auto& cb : normalized.ConstantBuffers)
                {
                    if (!cb.UseRootDescriptor)
                    {
                        continue;
                    }
                    const u32 reg = cb.DxilRegister.value_or(cb.Binding);
                    const u32 space = cb.DxilSpace.value_or(cb.Set);
                    const u64 regKey = (static_cast<u64>(space) << 32) | reg;
                    if (emittedRootRegs.contains(regKey))
                    {
                        continue;
                    }
                    emittedRootRegs.insert(regKey);

                    D3D12_ROOT_PARAMETER rp{};
                    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    rp.Descriptor.ShaderRegister = reg;
                    rp.Descriptor.RegisterSpace = space;
                    rp.ShaderVisibility = ToD3D12ShaderVisibility(cb.Visibility);
                    rootParams.push_back(rp);
                    totalDwords += 2;
                }

                if (totalDwords > kMaxDx12RootSignatureDwords)
                {
                    const std::string err = "DX12 Root Signature exceeds 64-DWORD limit (" +
                        std::to_string(totalDwords) + " DWORDs).";
                    Core::Log::Error("RHI.DX12", "CreatePipelineLayout rejected: " + err);
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure(err);
                }

                for (usize i = 0; i < tableParamIndices.size(); ++i)
                {
                    rootParams[tableParamIndices[i]].DescriptorTable.pDescriptorRanges = tableRanges[i].data();
                }

                D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
                rootSigDesc.NumParameters = static_cast<UINT>(rootParams.size());
                rootSigDesc.pParameters = rootParams.empty() ? nullptr : rootParams.data();
                rootSigDesc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
                rootSigDesc.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();
                rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

                ComPtr<ID3DBlob> sigBlob;
                ComPtr<ID3DBlob> errBlob;
                const HRESULT serHr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
                if (FAILED(serHr) || !sigBlob)
                {
                    std::string errMsg = "D3D12SerializeRootSignature failed";
                    if (errBlob && errBlob->GetBufferPointer() && errBlob->GetBufferSize() > 0)
                    {
                        errMsg += ": ";
                        errMsg.append(static_cast<const char*>(errBlob->GetBufferPointer()), errBlob->GetBufferSize());
                    }
                    Core::Log::Error("RHI.DX12", errMsg);
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure(errMsg);
                }

                ComPtr<ID3D12RootSignature> rootSig;
                const HRESULT rsHr = m_device->CreateRootSignature(
                    0,
                    sigBlob->GetBufferPointer(),
                    sigBlob->GetBufferSize(),
                    IID_PPV_ARGS(&rootSig));
                if (!m_debug.CheckHResult(rsHr, m_device.Get(), "CreatePipelineLayout::CreateRootSignature") || !rootSig)
                {
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure("ID3D12Device::CreateRootSignature failed.");
                }

                if (!normalized.DebugName.empty())
                {
                    DX12::DX12Debug::SetObjectName(rootSig.Get(), normalized.DebugName);
                }

                std::lock_guard<std::mutex> lock(m_resourceMutex);
                if (auto cacheIt = m_pipelineLayoutCache.find(canonicalHash); cacheIt != m_pipelineLayoutCache.end())
                {
                    for (PipelineLayoutHandle candidate : cacheIt->second)
                    {
                        if (auto* rec = m_pipelineLayouts.Get(candidate);
                            rec && !rec->PendingDestroy && rec->RootSignature &&
                            PipelineLayoutValidator::AreStructurallyEqual(rec->Desc, normalized))
                        {
                            ++rec->CacheRefCount;
                            ++m_layoutStats.CacheHitCount;
                            return Core::Result<PipelineLayoutHandle, std::string>::Success(candidate);
                        }
                    }
                }

                PipelineLayoutHandle handle = m_pipelineLayouts.Emplace(DX12PipelineLayoutRecord{
                    .Desc = normalized,
                    .CanonicalHash = canonicalHash,
                    .RootSignature = std::move(rootSig),
                    .RootConstantParamIndex = rootConstantParamIndex,
                    .MaxPushConstantBytes = maxPushBytes,
                    .BindlessTableSlot = bindlessTableSlot,
                    .UsesGlobalBindlessHeap = usesGlobalBindlessHeap,
                    .IsDefaultLayout = false,
                    .CacheRefCount = 1,
                    .PipelineRefCount = 0,
                    .UnsubmittedRefCount = 0,
                    .LastUsedFenceValue = 0,
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

                const u64 minSafeFence = m_frameOpen ? (m_nextFenceValue + 1) : m_nextFenceValue;
                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::PipelineLayout,
                    .CompletionFenceValue = std::max(rec->LastUsedFenceValue, minSafeFence),
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
                    rec->RootSignature.Reset();
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
                return rec != nullptr && rec->RootSignature != nullptr;
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

                PipelineLayoutHandle resolvedLayoutHandle = m_defaultBindlessPipelineLayout;
                ID3D12RootSignature* resolvedRootSig = m_rootSignature.Get();
                std::optional<u32> resolvedRootConstSlot = 0;
                u32 resolvedMaxPushBytes = 128;
                std::optional<u32> resolvedBindlessSlot = 1;
                bool resolvedUsesBindless = true;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (desc.PipelineLayout.IsValid())
                    {
                        auto* layoutRec = m_pipelineLayouts.Get(desc.PipelineLayout);
                        if (!layoutRec || layoutRec->PendingDestroy || !layoutRec->RootSignature)
                        {
                            Core::Log::Error("RHI.DX12", "CreateGraphicsPipeline failed: invalid or destroyed PipelineLayoutHandle.");
                            return PipelineHandle::Invalid();
                        }
                        resolvedLayoutHandle = desc.PipelineLayout;
                        resolvedRootSig = layoutRec->RootSignature.Get();
                        resolvedRootConstSlot = layoutRec->RootConstantParamIndex;
                        resolvedMaxPushBytes = layoutRec->MaxPushConstantBytes;
                        resolvedBindlessSlot = layoutRec->BindlessTableSlot;
                        resolvedUsesBindless = layoutRec->UsesGlobalBindlessHeap;
                        ++layoutRec->PipelineRefCount;
                    }
                    else if (resolvedLayoutHandle.IsValid())
                    {
                        if (auto* defaultRec = m_pipelineLayouts.Get(resolvedLayoutHandle))
                        {
                            resolvedRootSig = defaultRec->RootSignature.Get();
                            resolvedRootConstSlot = defaultRec->RootConstantParamIndex;
                            resolvedMaxPushBytes = defaultRec->MaxPushConstantBytes;
                            resolvedBindlessSlot = defaultRec->BindlessTableSlot;
                            resolvedUsesBindless = defaultRec->UsesGlobalBindlessHeap;
                            ++defaultRec->PipelineRefCount;
                        }
                    }
                }

                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = resolvedRootSig;
                psoDesc.VS = { desc.VertexShaderBytecode.data(), desc.VertexShaderBytecode.size() };
                psoDesc.PS = { desc.PixelShaderBytecode.data(), desc.PixelShaderBytecode.size() };

                const D3D12_INPUT_ELEMENT_DESC staticMeshInputElements[5] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
                };
                const D3D12_INPUT_ELEMENT_DESC skinnedMeshInputElements[7] = {
                    { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT,  0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 76, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
                };
                if (desc.VertexStrideBytes == 60)
                {
                    psoDesc.InputLayout.pInputElementDescs = staticMeshInputElements;
                    psoDesc.InputLayout.NumElements = 5;
                }
                else if (desc.VertexStrideBytes == 92)
                {
                    psoDesc.InputLayout.pInputElementDescs = skinnedMeshInputElements;
                    psoDesc.InputLayout.NumElements = 7;
                }

                psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
                psoDesc.BlendState.IndependentBlendEnable = FALSE;
                if (desc.AlphaBlendEnable)
                {
                    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                }
                else
                {
                    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
                }
                psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                psoDesc.SampleMask = UINT_MAX;

                psoDesc.RasterizerState.FillMode = desc.Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
                psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
                psoDesc.RasterizerState.DepthClipEnable = TRUE;

                psoDesc.DepthStencilState.DepthEnable = desc.DepthTestEnable ? TRUE : FALSE;
                psoDesc.DepthStencilState.DepthWriteMask = desc.DepthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
                psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

                psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                psoDesc.NumRenderTargets = 1;
                psoDesc.RTVFormats[0] = ToDxgiFormat(desc.ColorTargetFormat);
                psoDesc.DSVFormat = (desc.DepthTargetFormat != Format::Unknown) ? ToDxgiFormat(desc.DepthTargetFormat) : DXGI_FORMAT_UNKNOWN;
                psoDesc.SampleDesc.Count = 1;

                ComPtr<ID3D12PipelineState> pso;
                const HRESULT psoHr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
                if (!m_debug.CheckHResult(psoHr, m_device.Get(), "CreateGraphicsPipelineState"))
                {
                    if (resolvedLayoutHandle.IsValid())
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        if (auto* layoutRec = m_pipelineLayouts.Get(resolvedLayoutHandle); layoutRec && layoutRec->PipelineRefCount > 0)
                        {
                            --layoutRec->PipelineRefCount;
                        }
                    }
                    return PipelineHandle::Invalid();
                }
                DX12::DX12Debug::SetObjectName(pso.Get(), desc.DebugName);

                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_pipelines.Emplace(DX12PipelineRecord{
                    .PSO = std::move(pso),
                    .Layout = resolvedLayoutHandle,
                    .NativeRootSig = resolvedRootSig,
                    .RootConstantParamIndex = resolvedRootConstSlot,
                    .MaxPushConstantBytes = resolvedMaxPushBytes,
                    .BindlessTableSlot = resolvedBindlessSlot,
                    .UsesGlobalBindlessHeap = resolvedUsesBindless,
                    .IsCompute = false
                });
            }

            PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override
            {
                if (desc.ComputeShaderBytecode.empty())
                {
                    return PipelineHandle::Invalid();
                }

                PipelineLayoutHandle resolvedLayoutHandle = m_defaultBindlessPipelineLayout;
                ID3D12RootSignature* resolvedRootSig = m_rootSignature.Get();
                std::optional<u32> resolvedRootConstSlot = 0;
                u32 resolvedMaxPushBytes = 128;
                std::optional<u32> resolvedBindlessSlot = 1;
                bool resolvedUsesBindless = true;
                {
                    std::lock_guard<std::mutex> lock(m_resourceMutex);
                    if (desc.PipelineLayout.IsValid())
                    {
                        auto* layoutRec = m_pipelineLayouts.Get(desc.PipelineLayout);
                        if (!layoutRec || layoutRec->PendingDestroy || !layoutRec->RootSignature)
                        {
                            Core::Log::Error("RHI.DX12", "CreateComputePipeline failed: invalid or destroyed PipelineLayoutHandle.");
                            return PipelineHandle::Invalid();
                        }
                        resolvedLayoutHandle = desc.PipelineLayout;
                        resolvedRootSig = layoutRec->RootSignature.Get();
                        resolvedRootConstSlot = layoutRec->RootConstantParamIndex;
                        resolvedMaxPushBytes = layoutRec->MaxPushConstantBytes;
                        resolvedBindlessSlot = layoutRec->BindlessTableSlot;
                        resolvedUsesBindless = layoutRec->UsesGlobalBindlessHeap;
                        ++layoutRec->PipelineRefCount;
                    }
                    else if (resolvedLayoutHandle.IsValid())
                    {
                        if (auto* defaultRec = m_pipelineLayouts.Get(resolvedLayoutHandle))
                        {
                            resolvedRootSig = defaultRec->RootSignature.Get();
                            resolvedRootConstSlot = defaultRec->RootConstantParamIndex;
                            resolvedMaxPushBytes = defaultRec->MaxPushConstantBytes;
                            resolvedBindlessSlot = defaultRec->BindlessTableSlot;
                            resolvedUsesBindless = defaultRec->UsesGlobalBindlessHeap;
                            ++defaultRec->PipelineRefCount;
                        }
                    }
                }

                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = resolvedRootSig;
                psoDesc.CS = { desc.ComputeShaderBytecode.data(), desc.ComputeShaderBytecode.size() };

                ComPtr<ID3D12PipelineState> pso;
                const HRESULT psoHr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
                if (!m_debug.CheckHResult(psoHr, m_device.Get(), "CreateComputePipelineState"))
                {
                    if (resolvedLayoutHandle.IsValid())
                    {
                        std::lock_guard<std::mutex> lock(m_resourceMutex);
                        if (auto* layoutRec = m_pipelineLayouts.Get(resolvedLayoutHandle); layoutRec && layoutRec->PipelineRefCount > 0)
                        {
                            --layoutRec->PipelineRefCount;
                        }
                    }
                    return PipelineHandle::Invalid();
                }
                DX12::DX12Debug::SetObjectName(pso.Get(), desc.DebugName);

                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_pipelines.Emplace(DX12PipelineRecord{
                    .PSO = std::move(pso),
                    .Layout = resolvedLayoutHandle,
                    .NativeRootSig = resolvedRootSig,
                    .RootConstantParamIndex = resolvedRootConstSlot,
                    .MaxPushConstantBytes = resolvedMaxPushBytes,
                    .BindlessTableSlot = resolvedBindlessSlot,
                    .UsesGlobalBindlessHeap = resolvedUsesBindless,
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
                const u64 minSafeFence = m_frameOpen ? (m_nextFenceValue + 1) : m_nextFenceValue;
                const u64 targetFence = std::max(rec->LastUsedFenceValue, minSafeFence);

                m_deferredDestructions.push_back(DeferredDestruction{
                    .ResourceType = DeferredDestruction::Type::Pipeline,
                    .CompletionFenceValue = targetFence,
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
                            layoutRec->LastUsedFenceValue = std::max(layoutRec->LastUsedFenceValue, rec->LastUsedFenceValue);
                            if (layoutRec->PipelineRefCount > 0)
                            {
                                --layoutRec->PipelineRefCount;
                            }
                        }
                    }
                    rec->PSO.Reset();
                    m_pipelines.Remove(handle);
                }
            }

            bool IsResourceAlive(PipelineHandle handle) const noexcept override
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                return m_pipelines.IsValid(handle);
            }

            bool AddUnsubmittedBufferRef(BufferHandle handle)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_buffers.Get(handle);
                if (!rec || rec->PendingDestroy || !rec->Resource)
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
                if (!rec || rec->PendingDestroy || !rec->Resource)
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
                if (!rec || rec->PendingDestroy || !rec->PSO)
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
                if (!rec || rec->PendingDestroy || !rec->RootSignature)
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
                const u64 submissionFenceValue = ++m_nextFenceValue;
                for (BufferHandle h : buffers)
                {
                    if (auto* rec = m_buffers.Get(h))
                    {
                        rec->LastUsedFenceValue = std::max(rec->LastUsedFenceValue, submissionFenceValue);
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
                        rec->LastUsedFenceValue = std::max(rec->LastUsedFenceValue, submissionFenceValue);
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
                        rec->LastUsedFenceValue = std::max(rec->LastUsedFenceValue, submissionFenceValue);
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
                        rec->LastUsedFenceValue = std::max(rec->LastUsedFenceValue, submissionFenceValue);
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                    }
                }
                return submissionFenceValue;
            }

            void UnregisterCommandList(DX12CommandList* cmdList)
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                std::erase(m_createdCommandLists, cmdList);
            }

            struct BufferSnapshot
            {
                ID3D12Resource* Resource = nullptr;
                D3D12_GPU_VIRTUAL_ADDRESS GpuVA = 0;
                u64 SizeBytes = 0;
                u32 StrideBytes = 0;
                MemoryUsage Memory = MemoryUsage::GpuOnly;
            };

            struct TextureSnapshot
            {
                ID3D12Resource* Resource = nullptr;
                u32 Width = 0;
                u32 Height = 0;
                bool HasRtv = false;
                bool HasDsv = false;
                D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle{};
                D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle{};
            };

            struct PipelineSnapshot
            {
                ID3D12PipelineState* PSO = nullptr;
                PipelineLayoutHandle Layout = PipelineLayoutHandle::Invalid();
                ID3D12RootSignature* NativeRootSig = nullptr;
                std::optional<u32> RootConstantParamIndex = 0;
                u32 MaxPushConstantBytes = 128;
                std::optional<u32> BindlessTableSlot = 1;
                bool UsesGlobalBindlessHeap = true;
                bool IsCompute = false;
            };

            [[nodiscard]] std::optional<BufferSnapshot> TryGetBufferSnapshot(BufferHandle h) const noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_buffers.Get(h);
                if (!rec || !rec->Resource)
                {
                    return std::nullopt;
                }
                return BufferSnapshot{
                    rec->Resource.Get(),
                    rec->Resource->GetGPUVirtualAddress(),
                    rec->Desc.SizeBytes,
                    rec->Desc.StrideBytes,
                    rec->Desc.Memory
                };
            }

            [[nodiscard]] std::optional<TextureSnapshot> TryGetTextureSnapshot(TextureHandle h) const noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_textures.Get(h);
                if (!rec || !rec->Resource)
                {
                    return std::nullopt;
                }
                return TextureSnapshot{
                    rec->Resource.Get(),
                    rec->Desc.Width,
                    rec->Desc.Height,
                    rec->HasRtv,
                    rec->HasDsv,
                    rec->RtvHandle,
                    rec->DsvHandle
                };
            }

            [[nodiscard]] bool TryTransitionTextureState(
                TextureHandle h,
                D3D12_RESOURCE_STATES stateAfter,
                ID3D12Resource*& outResource,
                D3D12_RESOURCE_STATES& outStateBefore) noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                auto* rec = m_textures.Get(h);
                if (!rec || !rec->Resource || rec->CurrentState == stateAfter)
                {
                    return false;
                }
                outResource = rec->Resource.Get();
                outStateBefore = rec->CurrentState;
                rec->CurrentState = stateAfter;
                return true;
            }

            [[nodiscard]] std::optional<PipelineSnapshot> TryGetPipelineSnapshot(PipelineHandle h) const noexcept
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                const auto* rec = m_pipelines.Get(h);
                if (!rec || !rec->PSO)
                {
                    return std::nullopt;
                }
                return PipelineSnapshot{
                    rec->PSO.Get(),
                    rec->Layout,
                    rec->NativeRootSig,
                    rec->RootConstantParamIndex,
                    rec->MaxPushConstantBytes,
                    rec->BindlessTableSlot,
                    rec->UsesGlobalBindlessHeap,
                    rec->IsCompute
                };
            }

        private:
            bool CreateDescriptorHeaps()
            {
                D3D12_DESCRIPTOR_HEAP_DESC bindlessDesc{};
                bindlessDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                bindlessDesc.NumDescriptors = kTotalBindlessHeapDescriptors;
                bindlessDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (FAILED(m_device->CreateDescriptorHeap(&bindlessDesc, IID_PPV_ARGS(&m_bindlessHeap))))
                {
                    return false;
                }
                m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
                rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                rtvDesc.NumDescriptors = kMaxRtvDescriptors;
                if (FAILED(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap))))
                {
                    return false;
                }
                m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

                D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
                dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                dsvDesc.NumDescriptors = kMaxDsvDescriptors;
                if (FAILED(m_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap))))
                {
                    return false;
                }
                m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                return true;
            }

            bool CreateBindlessRootSignature()
            {
                std::array<D3D12_DESCRIPTOR_RANGE, 2> srvRanges{};
                // Range 0: StructuredBuffers[] in register(t0, space0) at heap slots [0, kMaxBindlessDescriptors)
                srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                srvRanges[0].NumDescriptors = kMaxBindlessDescriptors;
                srvRanges[0].BaseShaderRegister = 0;
                srvRanges[0].RegisterSpace = 0;
                srvRanges[0].OffsetInDescriptorsFromTableStart = 0;

                // Range 1: Texture2D<float4>[] in register(t0, space1) at heap slots [kMaxBindlessDescriptors, 2 * kMaxBindlessDescriptors)
                srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                srvRanges[1].NumDescriptors = kMaxBindlessDescriptors;
                srvRanges[1].BaseShaderRegister = 0;
                srvRanges[1].RegisterSpace = 1;
                srvRanges[1].OffsetInDescriptorsFromTableStart = kMaxBindlessDescriptors;

                std::array<D3D12_ROOT_PARAMETER, 3> rootParams{};
                // Slot 0: 32 DWORDs (128 bytes) Root Constants (register b0, space0)
                rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                rootParams[0].Constants.ShaderRegister = 0;
                rootParams[0].Constants.RegisterSpace = 0;
                rootParams[0].Constants.Num32BitValues = 32;
                rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                // Slot 1: Global Bindless Descriptor Table (Buffers space0 + Textures space1)
                rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                rootParams[1].DescriptorTable.NumDescriptorRanges = static_cast<UINT>(srvRanges.size());
                rootParams[1].DescriptorTable.pDescriptorRanges = srvRanges.data();
                rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                // Slot 2: Per-Object Constant Buffer (register b1, space0) for BoneMatrices / Skinning
                // Intentional backend difference: DX12 uses a direct Root CBV here rather than indexing into
                // heap slots [8192, 8448), whereas Vulkan binds an array of uniform buffers at binding 3 (b1, space0).
                rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParams[2].Descriptor.ShaderRegister = 1;
                rootParams[2].Descriptor.RegisterSpace = 0;
                rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                D3D12_STATIC_SAMPLER_DESC staticSampler{};
                staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.MipLODBias = 0.0f;
                staticSampler.MaxAnisotropy = 1;
                staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
                staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
                staticSampler.MinLOD = 0.0f;
                staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
                staticSampler.ShaderRegister = 0; // s0
                staticSampler.RegisterSpace = 0;  // space0
                staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
                rootSigDesc.NumParameters = static_cast<UINT>(rootParams.size());
                rootSigDesc.pParameters = rootParams.data();
                rootSigDesc.NumStaticSamplers = 1;
                rootSigDesc.pStaticSamplers = &staticSampler;
                rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

                ComPtr<ID3DBlob> sigBlob;
                ComPtr<ID3DBlob> errBlob;
                if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)))
                {
                    return false;
                }

                if (FAILED(m_device->CreateRootSignature(
                        0,
                        sigBlob->GetBufferPointer(),
                        sigBlob->GetBufferSize(),
                        IID_PPV_ARGS(&m_rootSignature))) || !m_rootSignature)
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
                    m_defaultBindlessPipelineLayout = m_pipelineLayouts.Emplace(DX12PipelineLayoutRecord{
                        .Desc = normalizedDefault,
                        .CanonicalHash = defaultHash,
                        .RootSignature = m_rootSignature,
                        .RootConstantParamIndex = 0u,
                        .MaxPushConstantBytes = kMaxPortablePushConstantsBytes,
                        .BindlessTableSlot = 1u,
                        .UsesGlobalBindlessHeap = true,
                        .IsDefaultLayout = true,
                        .CacheRefCount = 1,
                        .PipelineRefCount = 0,
                        .UnsubmittedRefCount = 0,
                        .LastUsedFenceValue = 0,
                        .PendingDestroy = false
                    });
                    m_pipelineLayoutCache[defaultHash].push_back(m_defaultBindlessPipelineLayout);
                    ++m_layoutStats.CreatedCount;
                    ++m_layoutStats.ActiveCount;
                }

                return true;
            }

            bool CreateSwapchain(HWND hwnd, u32 width, u32 height)
            {
                BOOL allowTearing = FALSE;
                if (SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
                {
                    m_tearingSupported = (allowTearing == TRUE);
                }

                DXGI_SWAP_CHAIN_DESC1 scDesc{};
                scDesc.Width = width;
                scDesc.Height = height;
                scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                scDesc.SampleDesc.Count = 1;
                scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                scDesc.BufferCount = kMaxFramesInFlight;
                scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                scDesc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

                ComPtr<IDXGISwapChain1> sc1;
                const HRESULT scHr = m_factory->CreateSwapChainForHwnd(
                    m_commandQueue.Get(),
                    hwnd,
                    &scDesc,
                    nullptr,
                    nullptr,
                    &sc1);
                if (!m_debug.CheckHResult(scHr, m_device.Get(), "CreateSwapChainForHwnd"))
                {
                    return false;
                }

                m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
                sc1.As(&m_swapchain);
                m_backbufferIndex = m_swapchain->GetCurrentBackBufferIndex();
                return CreateSwapchainBackbuffers();
            }

            bool CreateSwapchainBackbuffers()
            {
                std::lock_guard<std::mutex> lock(m_resourceMutex);
                m_swapchainTextures.clear();
                for (u32 i = 0; i < kMaxFramesInFlight; ++i)
                {
                    ComPtr<ID3D12Resource> backbuffer;
                    if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffer))))
                    {
                        return false;
                    }

                    u32 rtvSlot = 0xFFFFFFFFu;
                    if (!m_freeRtvSlots.empty())
                    {
                        rtvSlot = m_freeRtvSlots.back();
                        m_freeRtvSlots.pop_back();
                    }
                    else if (m_nextRtvSlot < kMaxRtvDescriptors)
                    {
                        rtvSlot = m_nextRtvSlot++;
                    }
                    else
                    {
                        return false;
                    }

                    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
                    rtvHandle.ptr += static_cast<SIZE_T>(rtvSlot) * m_rtvDescriptorSize;
                    m_device->CreateRenderTargetView(backbuffer.Get(), nullptr, rtvHandle);

                    TextureDesc td{};
                    td.Width = m_width;
                    td.Height = m_height;
                    td.PixelFormat = Format::BGRA8_UNorm;
                    td.RenderTarget = true;
                    td.DebugName = "DX12SwapchainBackbuffer";

                    m_swapchainTextures.push_back(m_textures.Emplace(DX12TextureRecord{
                        .Desc = td,
                        .Resource = std::move(backbuffer),
                        .AllocatedBytes = 0,
                        .RtvHandle = rtvHandle,
                        .DsvHandle = {},
                        .RtvSlot = rtvSlot,
                        .DsvSlot = 0xFFFFFFFFu,
                        .HasRtv = true,
                        .HasDsv = false,
                        .Bindless = {},
                        .CurrentState = D3D12_RESOURCE_STATE_PRESENT
                    }));
                }
                return true;
            }

            ComPtr<IDXGIFactory6> m_factory;
            DX12::DX12Debug m_debug;
            ComPtr<ID3D12Device5> m_device;
            ComPtr<ID3D12CommandQueue> m_commandQueue;
            ComPtr<IDXGISwapChain4> m_swapchain;
            std::string m_adapterName = "DirectX 12 Adapter";

            DX12MemoryAllocator m_allocator;

            ComPtr<ID3D12DescriptorHeap> m_bindlessHeap;
            ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
            ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
            ComPtr<ID3D12RootSignature> m_rootSignature;
            u32 m_cbvSrvUavDescriptorSize = 0;
            u32 m_rtvDescriptorSize = 0;
            u32 m_dsvDescriptorSize = 0;
            u32 m_nextBindlessBufferSlot = 0;
            u32 m_nextBindlessTextureSlot = 0;
            u32 m_nextBindlessUniformSlot = 0;
            u32 m_nextRtvSlot = 0;
            u32 m_nextDsvSlot = 0;
            std::vector<u32> m_freeBindlessBufferSlots;
            std::vector<u32> m_freeBindlessTextureSlots;
            std::vector<u32> m_freeBindlessUniformSlots;
            std::vector<u32> m_freeRtvSlots;
            std::vector<u32> m_freeDsvSlots;

            std::array<ComPtr<ID3D12CommandAllocator>, kMaxFramesInFlight> m_frameAllocators;
            ComPtr<ID3D12GraphicsCommandList6> m_commandList;
            ComPtr<ID3D12CommandAllocator> m_uploadAllocator;
            ComPtr<ID3D12GraphicsCommandList6> m_uploadCommandList;
            ComPtr<ID3D12Fence> m_fence;
            void* m_fenceEvent = nullptr;
            void* m_uploadFenceEvent = nullptr;
            std::array<u64, kMaxFramesInFlight> m_frameFenceValues{ 0, 0 };
            u64 m_nextFenceValue = 0;
            u32 m_frameIndex = 0;
            u32 m_backbufferIndex = 0;
            bool m_frameOpen = false;

            std::vector<TextureHandle> m_swapchainTextures;
            u32 m_width = 1280;
            u32 m_height = 720;
            bool m_vsync = true;
            bool m_tearingSupported = false;

            DX12CommandList m_activeCmdList;
            std::vector<DX12CommandList*> m_createdCommandLists;
            Core::GenerationalPool<DX12BufferRecord, BufferTag> m_buffers;
            Core::GenerationalPool<DX12TextureRecord, TextureTag> m_textures;
            Core::GenerationalPool<DX12PipelineRecord, PipelineTag> m_pipelines;
            Core::GenerationalPool<DX12PipelineLayoutRecord, PipelineLayoutTag> m_pipelineLayouts;
            std::unordered_map<u64, std::vector<PipelineLayoutHandle>> m_pipelineLayoutCache;
            PipelineLayoutHandle m_defaultBindlessPipelineLayout = PipelineLayoutHandle::Invalid();
            PipelineLayoutLimits m_limits{};
            PipelineLayoutStats m_layoutStats{};

            // Thread Ownership & Synchronization Policy:
            // - Frame lifecycle and swapchain management (BeginFrame, EndFrameAndPresent, ResizeSwapchain, WaitIdle,
            //   Shutdown) are owned by the render/main thread.
            // - Individual ICommandList instances do not support concurrent recording from multiple threads simultaneously;
            //   each command list must be recorded by at most one thread at a time.
            // - m_resourceMutex protects CPU-side resource pools (m_buffers, m_textures, m_pipelines backed by std::deque),
            //   bindless/RTV/DSV descriptor free lists/counters, m_allocator, m_deferredDestructions, m_nextFenceValue,
            //   and m_frameOpen.
            // - m_submitMutex serializes ExecuteCommandLists/Signal/Present on m_commandQueue.
            // - Lock-order invariant: m_resourceMutex and m_submitMutex are never nested, and GPU wait operations
            //   (WaitForSingleObject) never hold either mutex.
            std::vector<DeferredDestruction> m_deferredDestructions;
            mutable std::mutex m_resourceMutex;
            std::mutex m_submitMutex;
            std::mutex m_uploadMutex;
        };

        DX12CommandList::~DX12CommandList()
        {
            ReleaseUnsubmittedReferences();
            if (m_owner && m_ownsNativeObjects)
            {
                m_owner->UnregisterCommandList(this);
            }
            m_ownedCmd.Reset();
            m_ownedAlloc.Reset();
        }

        void DX12CommandList::Begin()
        {
            ReleaseUnsubmittedReferences();
            if (!m_cmd || !m_alloc)
            {
                return;
            }
            if (m_isRecording)
            {
                m_cmd->Close();
                m_isRecording = false;
            }
            m_alloc->Reset();
            m_cmd->Reset(m_alloc, nullptr);
            m_isRecording = true;

            m_activeGraphicsRootSig = m_rootSig;
            m_activeComputeRootSig = m_rootSig;
            m_activeIsCompute = false;
            m_activeRootConstantSlot = 0;
            m_activeMaxPushBytes = 128;
            m_activeBindlessTableSlot = 1;

            if (m_bindlessHeap)
            {
                ID3D12DescriptorHeap* heaps[] = { m_bindlessHeap };
                m_cmd->SetDescriptorHeaps(1, heaps);
            }
            if (m_rootSig)
            {
                m_cmd->SetGraphicsRootSignature(m_rootSig);
                m_cmd->SetComputeRootSignature(m_rootSig);
                if (m_bindlessHeap)
                {
                    m_cmd->SetGraphicsRootDescriptorTable(1, m_bindlessHeap->GetGPUDescriptorHandleForHeapStart());
                    m_cmd->SetComputeRootDescriptorTable(1, m_bindlessHeap->GetGPUDescriptorHandleForHeapStart());
                }
            }
            m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        void DX12CommandList::End()
        {
            if (m_isRecording && m_cmd)
            {
                m_cmd->Close();
                m_isRecording = false;
            }
        }

        void DX12CommandList::Discard()
        {
            if (m_isRecording && m_cmd)
            {
                m_cmd->Close();
                m_isRecording = false;
            }
            ReleaseUnsubmittedReferences();
        }

        void DX12CommandList::TrackBuffer(BufferHandle handle)
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

        void DX12CommandList::TrackTexture(TextureHandle handle)
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

        void DX12CommandList::TrackPipeline(PipelineHandle handle)
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

        void DX12CommandList::TrackPipelineLayout(PipelineLayoutHandle layout)
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

        void DX12CommandList::ReleaseUnsubmittedReferences()
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

        void DX12CommandList::OnSubmitted(u64 submissionFenceValue)
        {
            m_submissionFenceValue = submissionFenceValue;
            m_referencedBuffers.clear();
            m_referencedTextures.clear();
            m_referencedPipelines.clear();
            m_referencedPipelineLayouts.clear();
        }

        void DX12CommandList::ShutdownInternal()
        {
            if (m_isRecording && m_cmd)
            {
                m_cmd->Close();
                m_isRecording = false;
            }
            ReleaseUnsubmittedReferences();
            m_ownedCmd.Reset();
            m_ownedAlloc.Reset();
            m_cmd = nullptr;
            m_alloc = nullptr;
            m_owner = nullptr;
        }

        void DX12CommandList::TransitionTexture(TextureHandle texture, ResourceState before, ResourceState after)
        {
            if (!m_owner) return;
            TrackTexture(texture);
            if (!m_referencedTextures.contains(texture) || before == after) return;

            const D3D12_RESOURCE_STATES stateAfter = ToD3D12State(after);
            ID3D12Resource* resource = nullptr;
            D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
            if (!m_owner->TryTransitionTextureState(texture, stateAfter, resource, stateBefore))
            {
                return;
            }

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_cmd->ResourceBarrier(1, &barrier);
        }

        void DX12CommandList::TransitionBuffer(BufferHandle buffer, ResourceState before, ResourceState after)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer) || before == after) return;
            const auto rec = m_owner->TryGetBufferSnapshot(buffer);
            if (!rec || !rec->Resource || rec->Memory != MemoryUsage::GpuOnly) return;

            const D3D12_RESOURCE_STATES stateBefore = ToD3D12State(before);
            const D3D12_RESOURCE_STATES stateAfter = ToD3D12State(after);
            if (stateBefore == stateAfter) return;

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = rec->Resource;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_cmd->ResourceBarrier(1, &barrier);
        }

        void DX12CommandList::BeginRendering(const DynamicRenderPassDesc& passDesc)
        {
            if (!m_owner) return;

            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
            u32 renderW = m_width;
            u32 renderH = m_height;

            for (const auto& att : passDesc.ColorAttachments)
            {
                TrackTexture(att.Texture);
                if (!m_referencedTextures.contains(att.Texture)) continue;
                const auto rec = m_owner->TryGetTextureSnapshot(att.Texture);
                if (!rec || !rec->HasRtv) continue;
                renderW = rec->Width;
                renderH = rec->Height;
                rtvs.push_back(rec->RtvHandle);
                if (att.ClearOnLoad)
                {
                    m_cmd->ClearRenderTargetView(rec->RtvHandle, att.ClearColor, 0, nullptr);
                }
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
            if (passDesc.DepthAttachment.IsValid())
            {
                TrackTexture(passDesc.DepthAttachment);
                if (m_referencedTextures.contains(passDesc.DepthAttachment))
                {
                    if (const auto depthRec = m_owner->TryGetTextureSnapshot(passDesc.DepthAttachment); depthRec && depthRec->HasDsv)
                    {
                        dsvHandle = depthRec->DsvHandle;
                        pDsv = &dsvHandle;
                        if (passDesc.ClearDepth)
                        {
                            m_cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, passDesc.DepthClearValue, 0, 0, nullptr);
                        }
                    }
                }
            }

            m_cmd->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.empty() ? nullptr : rtvs.data(), FALSE, pDsv);
            SetViewport(Viewport{ 0.0f, 0.0f, static_cast<f32>(renderW), static_cast<f32>(renderH), 0.0f, 1.0f });
            SetScissor(ScissorRect{ 0, 0, renderW, renderH });
        }

        void DX12CommandList::SetPipeline(PipelineHandle pipeline)
        {
            if (!m_owner) return;
            TrackPipeline(pipeline);
            if (!m_referencedPipelines.contains(pipeline)) return;
            if (const auto rec = m_owner->TryGetPipelineSnapshot(pipeline); rec && rec->PSO)
            {
                ID3D12RootSignature* targetRootSig = rec->NativeRootSig ? rec->NativeRootSig : m_rootSig;
                if (rec->IsCompute)
                {
                    if (targetRootSig && targetRootSig != m_activeComputeRootSig)
                    {
                        m_cmd->SetComputeRootSignature(targetRootSig);
                        m_activeComputeRootSig = targetRootSig;
                        if (rec->UsesGlobalBindlessHeap && rec->BindlessTableSlot.has_value() && m_bindlessHeap)
                        {
                            m_cmd->SetComputeRootDescriptorTable(*rec->BindlessTableSlot, m_bindlessHeap->GetGPUDescriptorHandleForHeapStart());
                        }
                    }
                }
                else
                {
                    if (targetRootSig && targetRootSig != m_activeGraphicsRootSig)
                    {
                        m_cmd->SetGraphicsRootSignature(targetRootSig);
                        m_activeGraphicsRootSig = targetRootSig;
                        if (rec->UsesGlobalBindlessHeap && rec->BindlessTableSlot.has_value() && m_bindlessHeap)
                        {
                            m_cmd->SetGraphicsRootDescriptorTable(*rec->BindlessTableSlot, m_bindlessHeap->GetGPUDescriptorHandleForHeapStart());
                        }
                    }
                }

                m_activeIsCompute = rec->IsCompute;
                m_activeRootConstantSlot = rec->RootConstantParamIndex;
                m_activeMaxPushBytes = rec->MaxPushConstantBytes;
                m_activeBindlessTableSlot = rec->BindlessTableSlot;

                m_cmd->SetPipelineState(rec->PSO);
            }
        }

        void DX12CommandList::BindVertexBuffer(BufferHandle buffer, u64 offsetBytes)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer)) return;
            if (const auto rec = m_owner->TryGetBufferSnapshot(buffer); rec && rec->Resource)
            {
                D3D12_VERTEX_BUFFER_VIEW vbv{};
                vbv.BufferLocation = rec->GpuVA + offsetBytes;
                vbv.SizeInBytes = static_cast<UINT>(rec->SizeBytes - offsetBytes);
                vbv.StrideInBytes = rec->StrideBytes;
                m_cmd->IASetVertexBuffers(0, 1, &vbv);
            }
        }

        void DX12CommandList::BindIndexBuffer(BufferHandle buffer, u64 offsetBytes, bool index32Bit)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer)) return;
            if (const auto rec = m_owner->TryGetBufferSnapshot(buffer); rec && rec->Resource)
            {
                D3D12_INDEX_BUFFER_VIEW ibv{};
                ibv.BufferLocation = rec->GpuVA + offsetBytes;
                ibv.SizeInBytes = static_cast<UINT>(rec->SizeBytes - offsetBytes);
                ibv.Format = index32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
                m_cmd->IASetIndexBuffer(&ibv);
            }
        }

        void DX12CommandList::BindConstantBuffer(u32 rootParamSlot, BufferHandle buffer, u64 offsetBytes)
        {
            if (!m_owner) return;
            TrackBuffer(buffer);
            if (!m_referencedBuffers.contains(buffer)) return;
            if (const auto rec = m_owner->TryGetBufferSnapshot(buffer); rec && rec->Resource)
            {
                const D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = rec->GpuVA + offsetBytes;
                if (m_activeIsCompute)
                {
                    m_cmd->SetComputeRootConstantBufferView(rootParamSlot, cbvAddress);
                }
                else
                {
                    m_cmd->SetGraphicsRootConstantBufferView(rootParamSlot, cbvAddress);
                }
            }
        }
    }

    std::unique_ptr<IDevice> CreateDX12Device()
    {
        return std::make_unique<DX12Device>();
    }
#else
    std::unique_ptr<IDevice> CreateDX12Device()
    {
        return CreateNullDevice();
    }
#endif
}
