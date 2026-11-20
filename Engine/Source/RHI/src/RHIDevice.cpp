#include "Morbi/RHI/IDevice.hpp"

#include <unordered_map>
#include <vector>

#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Memory/PoolAllocator.hpp"

namespace Morbi::RHI
{
    namespace
    {
        struct NullBufferRecord
        {
            BufferDesc Desc;
            BindlessResourceIndex Bindless;
        };

        struct NullTextureRecord
        {
            TextureDesc Desc;
            BindlessResourceIndex Bindless;
        };

        struct NullPipelineLayoutRecord
        {
            PipelineLayoutDesc Desc;
            u64 CanonicalHash = 0;
            u32 CacheRefCount = 1;
            u32 PipelineRefCount = 0;
            u32 UnsubmittedRefCount = 0;
            bool IsDefaultLayout = false;
            bool PendingDestroy = false;
        };

        struct NullPipelineRecord
        {
            PipelineLayoutHandle Layout = PipelineLayoutHandle::Invalid();
            bool IsCompute = false;
        };

        class NullDevice;

        class NullCommandList final : public ICommandList
        {
        public:
            explicit NullCommandList(NullDevice* owner = nullptr) noexcept : m_owner(owner) {}

            void BindOwner(NullDevice* owner) noexcept { m_owner = owner; }

            void Begin() override
            {
                Discard();
            }
            void End() override {}
            void Discard() override;

            void TrackBuffer(BufferHandle buffer) override
            {
                if (buffer.IsValid()) m_referencedBuffers.insert(buffer);
            }
            void TrackTexture(TextureHandle texture) override
            {
                if (texture.IsValid()) m_referencedTextures.insert(texture);
            }
            void TrackPipeline(PipelineHandle pipeline) override;
            void TrackPipelineLayout(PipelineLayoutHandle layout) override;

            const std::unordered_set<BufferHandle>& GetReferencedBuffers() const noexcept override { return m_referencedBuffers; }
            const std::unordered_set<TextureHandle>& GetReferencedTextures() const noexcept override { return m_referencedTextures; }
            const std::unordered_set<PipelineHandle>& GetReferencedPipelines() const noexcept override { return m_referencedPipelines; }
            const std::unordered_set<PipelineLayoutHandle>& GetReferencedPipelineLayouts() const noexcept override { return m_referencedPipelineLayouts; }

            void TransitionTexture(TextureHandle tex, ResourceState, ResourceState) override { TrackTexture(tex); }
            void TransitionBuffer(BufferHandle buf, ResourceState, ResourceState) override { TrackBuffer(buf); }
            void BeginRendering(const DynamicRenderPassDesc& passDesc) override
            {
                for (const auto& att : passDesc.ColorAttachments) TrackTexture(att.Texture);
                if (passDesc.DepthAttachment.IsValid()) TrackTexture(passDesc.DepthAttachment);
            }
            void EndRendering() override {}
            void SetViewport(const Viewport&) override {}
            void SetScissor(const ScissorRect&) override {}
            void SetPipeline(PipelineHandle pipe) override { TrackPipeline(pipe); }
            void PushConstants(const void*, u32, u32) override {}
            void BindVertexBuffer(BufferHandle buf, u64) override { TrackBuffer(buf); }
            void BindIndexBuffer(BufferHandle buf, u64, bool) override { TrackBuffer(buf); }
            void BindConstantBuffer(u32, BufferHandle buf, u64) override { TrackBuffer(buf); }
            void Draw(u32, u32, u32, u32) override {}
            void DrawIndexed(u32, u32, u32, i32, u32) override {}
            void Dispatch(u32, u32, u32) override {}
            void BeginDebugMarker(std::string_view) override {}
            void EndDebugMarker() override {}

        private:
            NullDevice* m_owner = nullptr;
            std::unordered_set<BufferHandle> m_referencedBuffers;
            std::unordered_set<TextureHandle> m_referencedTextures;
            std::unordered_set<PipelineHandle> m_referencedPipelines;
            std::unordered_set<PipelineLayoutHandle> m_referencedPipelineLayouts;
        };

        class NullDevice final : public IDevice
        {
        public:
            NullDevice()
            {
                m_cmdList.BindOwner(this);
            }

            BackendAPI GetAPI() const noexcept override { return BackendAPI::Null; }
            std::string_view GetAdapterName() const noexcept override { return "Morbi Null Adapter"; }

            Core::Result<bool, std::string> Initialize(const DeviceDesc& desc, const Platform::NativeWindowHandle&, u32 width, u32 height) override
            {
                TextureDesc backbufferDesc{};
                backbufferDesc.Width = width;
                backbufferDesc.Height = height;
                backbufferDesc.RenderTarget = true;
                backbufferDesc.DebugName = "SwapchainBackbuffer";
                m_backbuffer = CreateTexture(backbufferDesc, nullptr);

                PipelineLayoutDesc defaultDesc{};
                defaultDesc.Mode = PipelineLayoutMode::GlobalBindlessHybrid;
                defaultDesc.PushConstants.push_back(PipelinePushConstantRange{
                    .Name = "GlobalBindlessPushConstants",
                    .OffsetBytes = 0,
                    .SizeBytes = kMaxPortablePushConstantsBytes,
                    .Visibility = ShaderVisibility::All,
                    .DxilRegister = 0,
                    .DxilSpace = 0
                });
                defaultDesc.DebugName = "NullDefaultBindlessLayout";
                const PipelineLayoutDesc norm = PipelineLayoutValidator::Normalize(defaultDesc);
                const u64 hash = PipelineLayoutValidator::ComputeCanonicalHash(norm);
                m_defaultBindlessLayout = m_pipelineLayouts.Emplace(NullPipelineLayoutRecord{
                    .Desc = norm,
                    .CanonicalHash = hash,
                    .CacheRefCount = 1,
                    .IsDefaultLayout = true
                });
                m_layoutCache[hash].push_back(m_defaultBindlessLayout);

                Core::Log::Info("RHI", "Initialized Null RHI Device for app '" + std::string(desc.ApplicationName) + "'");
                return Core::Result<bool, std::string>::Success(true);
            }

            void Shutdown() override
            {
                m_cmdList.Discard();
                m_buffers.Clear();
                m_textures.Clear();
                m_pipelines.Clear();
                m_pipelineLayouts.Clear();
                m_layoutCache.clear();
                m_defaultBindlessLayout = PipelineLayoutHandle::Invalid();
            }

            bool BeginFrame() override
            {
                m_cmdList.Begin();
                return true;
            }

            void EndFrameAndPresent() override
            {
                m_cmdList.End();
                m_cmdList.Discard();
                ++m_timelineValue;
            }

            void ResizeSwapchain(u32, u32) override {}
            void SetVSync(bool enabled) override { m_vsync = enabled; }
            bool IsVSyncEnabled() const noexcept override { return m_vsync; }
            void WaitIdle() override {}

            u64 GetCompletedTimelineValue() const noexcept override { return m_timelineValue; }
            MemoryAllocatorStats GetMemoryStats() const noexcept override { return m_memStats; }

            ICommandList& GetActiveCommandList() override { return m_cmdList; }
            std::unique_ptr<ICommandList> CreateCommandList() override { return std::make_unique<NullCommandList>(this); }
            u64 SubmitCommandList(ICommandList& cmdList) override
            {
                cmdList.End();
                cmdList.Discard();
                return ++m_timelineValue;
            }
            TextureHandle GetCurrentBackbuffer() const noexcept override { return m_backbuffer; }
            Format GetSwapchainFormat() const noexcept override { return Format::BGRA8_UNorm; }

            BufferHandle CreateBuffer(const BufferDesc& desc, const void*) override
            {
                ++m_memStats.ActiveAllocations;
                m_memStats.TotalAllocatedBytes += desc.SizeBytes;
                return m_buffers.Emplace(NullBufferRecord{
                    .Desc = desc,
                    .Bindless = BindlessResourceIndex{ m_nextBindlessIndex++ }
                });
            }

            void DestroyBuffer(BufferHandle handle) override
            {
                if (const auto* rec = m_buffers.Get(handle))
                {
                    if (m_memStats.ActiveAllocations > 0) --m_memStats.ActiveAllocations;
                    if (m_memStats.TotalAllocatedBytes >= rec->Desc.SizeBytes) m_memStats.TotalAllocatedBytes -= rec->Desc.SizeBytes;
                }
                m_buffers.Remove(handle);
            }

            BindlessResourceIndex GetBindlessIndex(BufferHandle handle) const noexcept override
            {
                const auto* rec = m_buffers.Get(handle);
                return rec ? rec->Bindless : BindlessResourceIndex{};
            }

            bool IsResourceAlive(BufferHandle handle) const noexcept override
            {
                return m_buffers.IsValid(handle);
            }

            TextureHandle CreateTexture(const TextureDesc& desc, const void*) override
            {
                const u64 estBytes = static_cast<u64>(desc.Width) * static_cast<u64>(desc.Height) * 4ULL;
                ++m_memStats.ActiveAllocations;
                m_memStats.TotalAllocatedBytes += estBytes;
                return m_textures.Emplace(NullTextureRecord{
                    .Desc = desc,
                    .Bindless = BindlessResourceIndex{ m_nextBindlessIndex++ }
                });
            }

            void DestroyTexture(TextureHandle handle) override
            {
                if (const auto* rec = m_textures.Get(handle))
                {
                    const u64 estBytes = static_cast<u64>(rec->Desc.Width) * static_cast<u64>(rec->Desc.Height) * 4ULL;
                    if (m_memStats.ActiveAllocations > 0) --m_memStats.ActiveAllocations;
                    if (m_memStats.TotalAllocatedBytes >= estBytes) m_memStats.TotalAllocatedBytes -= estBytes;
                }
                m_textures.Remove(handle);
            }

            BindlessResourceIndex GetBindlessIndex(TextureHandle handle) const noexcept override
            {
                const auto* rec = m_textures.Get(handle);
                return rec ? rec->Bindless : BindlessResourceIndex{};
            }

            bool IsResourceAlive(TextureHandle handle) const noexcept override
            {
                return m_textures.IsValid(handle);
            }

            Core::Result<PipelineLayoutHandle, std::string> CreatePipelineLayout(const PipelineLayoutDesc& desc) override
            {
                const auto val = PipelineLayoutValidator::Validate(desc, m_limits, BackendAPI::Null);
                if (!val.IsValid)
                {
                    std::string errMsg;
                    for (const auto& issue : val.Issues)
                    {
                        if (issue.Level == PipelineLayoutIssue::Severity::Error)
                        {
                            if (!errMsg.empty()) errMsg += "; ";
                            errMsg += "[" + issue.Code + "] " + issue.Message;
                        }
                    }
                    return Core::Result<PipelineLayoutHandle, std::string>::Failure(std::move(errMsg));
                }

                const PipelineLayoutDesc norm = PipelineLayoutValidator::Normalize(desc);
                const u64 hash = PipelineLayoutValidator::ComputeCanonicalHash(norm);

                if (const auto it = m_layoutCache.find(hash); it != m_layoutCache.end())
                {
                    for (PipelineLayoutHandle existingHandle : it->second)
                    {
                        if (auto* rec = m_pipelineLayouts.Get(existingHandle);
                            rec != nullptr && !rec->PendingDestroy &&
                            PipelineLayoutValidator::AreStructurallyEqual(rec->Desc, norm))
                        {
                            ++rec->CacheRefCount;
                            ++m_layoutStats.CacheHitCount;
                            return Core::Result<PipelineLayoutHandle, std::string>::Success(existingHandle);
                        }
                    }
                }

                const PipelineLayoutHandle handle = m_pipelineLayouts.Emplace(NullPipelineLayoutRecord{
                    .Desc = norm,
                    .CanonicalHash = hash,
                    .CacheRefCount = 1
                });
                m_layoutCache[hash].push_back(handle);
                ++m_layoutStats.CreatedCount;
                ++m_layoutStats.ActiveCount;
                return Core::Result<PipelineLayoutHandle, std::string>::Success(handle);
            }

            void DestroyPipelineLayout(PipelineLayoutHandle handle) override
            {
                if (!handle.IsValid() || handle == m_defaultBindlessLayout)
                {
                    return;
                }
                auto* rec = m_pipelineLayouts.Get(handle);
                if (!rec || rec->PendingDestroy || rec->IsDefaultLayout)
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
                EraseFromCacheLocked(rec->CanonicalHash, handle);
                TryFinalizeLayoutDestruction(handle);
            }

            bool IsResourceAlive(PipelineLayoutHandle handle) const noexcept override
            {
                const auto* rec = m_pipelineLayouts.Get(handle);
                return rec != nullptr;
            }

            PipelineLayoutHandle GetDefaultBindlessPipelineLayout() const noexcept override
            {
                return m_defaultBindlessLayout;
            }

            PipelineLayoutLimits GetPipelineLayoutLimits() const noexcept override
            {
                return m_limits;
            }

            PipelineLayoutStats GetPipelineLayoutStats() const noexcept override
            {
                return m_layoutStats;
            }

            PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override
            {
                PipelineLayoutHandle resolvedLayout = m_defaultBindlessLayout;
                if (desc.PipelineLayout.IsValid())
                {
                    auto* layoutRec = m_pipelineLayouts.Get(desc.PipelineLayout);
                    if (!layoutRec || layoutRec->PendingDestroy)
                    {
                        return PipelineHandle::Invalid();
                    }
                    ++layoutRec->PipelineRefCount;
                    resolvedLayout = desc.PipelineLayout;
                }
                return m_pipelines.Emplace(NullPipelineRecord{
                    .Layout = resolvedLayout,
                    .IsCompute = false
                });
            }

            PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override
            {
                PipelineLayoutHandle resolvedLayout = m_defaultBindlessLayout;
                if (desc.PipelineLayout.IsValid())
                {
                    auto* layoutRec = m_pipelineLayouts.Get(desc.PipelineLayout);
                    if (!layoutRec || layoutRec->PendingDestroy)
                    {
                        return PipelineHandle::Invalid();
                    }
                    ++layoutRec->PipelineRefCount;
                    resolvedLayout = desc.PipelineLayout;
                }
                return m_pipelines.Emplace(NullPipelineRecord{
                    .Layout = resolvedLayout,
                    .IsCompute = true
                });
            }

            void DestroyPipeline(PipelineHandle handle) override
            {
                if (const auto* rec = m_pipelines.Get(handle))
                {
                    const PipelineLayoutHandle layout = rec->Layout;
                    m_pipelines.Remove(handle);
                    if (layout.IsValid() && layout != m_defaultBindlessLayout)
                    {
                        if (auto* layoutRec = m_pipelineLayouts.Get(layout); layoutRec && layoutRec->PipelineRefCount > 0)
                        {
                            --layoutRec->PipelineRefCount;
                            TryFinalizeLayoutDestruction(layout);
                        }
                    }
                }
            }

            bool IsResourceAlive(PipelineHandle handle) const noexcept override
            {
                return m_pipelines.IsValid(handle);
            }

            PipelineLayoutHandle GetPipelineLayoutForPipeline(PipelineHandle handle) const noexcept
            {
                const auto* rec = m_pipelines.Get(handle);
                return rec ? rec->Layout : PipelineLayoutHandle::Invalid();
            }

            bool AddUnsubmittedPipelineLayoutRef(PipelineLayoutHandle layout) noexcept
            {
                auto* rec = m_pipelineLayouts.Get(layout);
                if (!rec || rec->PendingDestroy)
                {
                    return false;
                }
                ++rec->UnsubmittedRefCount;
                return true;
            }

            void ReleaseUnsubmittedPipelineLayoutRefs(const std::unordered_set<PipelineLayoutHandle>& layouts) noexcept
            {
                for (PipelineLayoutHandle layout : layouts)
                {
                    if (auto* rec = m_pipelineLayouts.Get(layout))
                    {
                        if (rec->UnsubmittedRefCount > 0)
                        {
                            --rec->UnsubmittedRefCount;
                        }
                        TryFinalizeLayoutDestruction(layout);
                    }
                }
            }

        private:
            void EraseFromCacheLocked(u64 hash, PipelineLayoutHandle handle)
            {
                if (const auto it = m_layoutCache.find(hash); it != m_layoutCache.end())
                {
                    std::erase(it->second, handle);
                    if (it->second.empty())
                    {
                        m_layoutCache.erase(it);
                    }
                }
            }

            void TryFinalizeLayoutDestruction(PipelineLayoutHandle handle)
            {
                const auto* rec = m_pipelineLayouts.Get(handle);
                if (!rec || !rec->PendingDestroy || rec->IsDefaultLayout)
                {
                    return;
                }
                if (rec->CacheRefCount == 0 && rec->PipelineRefCount == 0 && rec->UnsubmittedRefCount == 0)
                {
                    m_pipelineLayouts.Remove(handle);
                    ++m_layoutStats.DestroyedCount;
                    if (m_layoutStats.ActiveCount > 0)
                    {
                        --m_layoutStats.ActiveCount;
                    }
                }
            }

            NullCommandList m_cmdList;
            Core::GenerationalPool<NullBufferRecord, BufferTag> m_buffers;
            Core::GenerationalPool<NullTextureRecord, TextureTag> m_textures;
            Core::GenerationalPool<NullPipelineRecord, PipelineTag> m_pipelines;
            Core::GenerationalPool<NullPipelineLayoutRecord, PipelineLayoutTag> m_pipelineLayouts;
            std::unordered_map<u64, std::vector<PipelineLayoutHandle>> m_layoutCache;
            PipelineLayoutHandle m_defaultBindlessLayout = PipelineLayoutHandle::Invalid();
            PipelineLayoutLimits m_limits{};
            PipelineLayoutStats m_layoutStats{};
            TextureHandle m_backbuffer = TextureHandle::Invalid();
            u32 m_nextBindlessIndex = 0;
            u64 m_timelineValue = 0;
            bool m_vsync = true;
            MemoryAllocatorStats m_memStats{};
        };

        void NullCommandList::Discard()
        {
            if (m_owner)
            {
                m_owner->ReleaseUnsubmittedPipelineLayoutRefs(m_referencedPipelineLayouts);
            }
            m_referencedBuffers.clear();
            m_referencedTextures.clear();
            m_referencedPipelines.clear();
            m_referencedPipelineLayouts.clear();
        }

        void NullCommandList::TrackPipeline(PipelineHandle pipeline)
        {
            if (!pipeline.IsValid()) return;
            m_referencedPipelines.insert(pipeline);
            if (m_owner)
            {
                TrackPipelineLayout(m_owner->GetPipelineLayoutForPipeline(pipeline));
            }
        }

        void NullCommandList::TrackPipelineLayout(PipelineLayoutHandle layout)
        {
            if (!m_owner || !layout.IsValid() || m_referencedPipelineLayouts.contains(layout))
            {
                return;
            }
            if (m_owner->AddUnsubmittedPipelineLayoutRef(layout))
            {
                m_referencedPipelineLayouts.insert(layout);
            }
        }
    }

    std::unique_ptr<IDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
}
