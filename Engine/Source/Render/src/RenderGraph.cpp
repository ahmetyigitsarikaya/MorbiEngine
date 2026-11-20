#include "Morbi/Render/RenderGraph.hpp"

#include <algorithm>

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

namespace Morbi::Render
{
    namespace
    {
        u64 EstimateTextureBytes(const RHI::TextureDesc& desc) noexcept
        {
            u64 bpp = 4;
            switch (desc.PixelFormat)
            {
            case RHI::Format::RGBA16_Float:
            case RHI::Format::RG32_Float:   bpp = 8; break;
            case RHI::Format::RGB32_Float:  bpp = 12; break;
            case RHI::Format::RGBA32_Float: bpp = 16; break;
            default:                        bpp = 4; break;
            }
            const u64 raw = static_cast<u64>(std::max<u32>(desc.Width, 1)) *
                            static_cast<u64>(std::max<u32>(desc.Height, 1)) *
                            static_cast<u64>(std::max<u32>(desc.DepthOrArrayLayers, 1)) * bpp;
            // Align to 64 KB placement boundary
            constexpr u64 kAlign = 65536ULL;
            return (raw + kAlign - 1) & ~(kAlign - 1);
        }
    }

    RGTextureHandle RenderGraphPassBuilder::ReadTexture(RGTextureHandle handle, RHI::ResourceState state)
    {
        if (handle.IsValid() && handle.Index < m_graph.m_textures.size())
        {
            auto& tex = m_graph.m_textures[handle.Index];
            tex.FirstPass = std::min(tex.FirstPass, m_passIndex);
            tex.LastPass = std::max(tex.LastPass, m_passIndex);
            m_graph.m_passes[m_passIndex].TextureAccesses.push_back({ handle, state, false });
        }
        return handle;
    }

    RGTextureHandle RenderGraphPassBuilder::WriteTexture(RGTextureHandle handle, RHI::ResourceState state)
    {
        if (handle.IsValid() && handle.Index < m_graph.m_textures.size())
        {
            auto& tex = m_graph.m_textures[handle.Index];
            tex.FirstPass = std::min(tex.FirstPass, m_passIndex);
            tex.LastPass = std::max(tex.LastPass, m_passIndex);
            m_graph.m_passes[m_passIndex].TextureAccesses.push_back({ handle, state, true });
        }
        return handle;
    }

    RGBufferHandle RenderGraphPassBuilder::ReadBuffer(RGBufferHandle handle, RHI::ResourceState state)
    {
        if (handle.IsValid() && handle.Index < m_graph.m_buffers.size())
        {
            auto& buf = m_graph.m_buffers[handle.Index];
            buf.FirstPass = std::min(buf.FirstPass, m_passIndex);
            buf.LastPass = std::max(buf.LastPass, m_passIndex);
            m_graph.m_passes[m_passIndex].BufferAccesses.push_back({ handle, state, false });
        }
        return handle;
    }

    RGBufferHandle RenderGraphPassBuilder::WriteBuffer(RGBufferHandle handle, RHI::ResourceState state)
    {
        if (handle.IsValid() && handle.Index < m_graph.m_buffers.size())
        {
            auto& buf = m_graph.m_buffers[handle.Index];
            buf.FirstPass = std::min(buf.FirstPass, m_passIndex);
            buf.LastPass = std::max(buf.LastPass, m_passIndex);
            m_graph.m_passes[m_passIndex].BufferAccesses.push_back({ handle, state, true });
        }
        return handle;
    }

    RHI::TextureHandle RenderGraphRegistry::GetTexture(RGTextureHandle handle) const noexcept
    {
        return m_graph.ResolvePhysicalTexture(handle);
    }

    RHI::BufferHandle RenderGraphRegistry::GetBuffer(RGBufferHandle handle) const noexcept
    {
        return m_graph.ResolvePhysicalBuffer(handle);
    }

    void RenderGraph::Reset()
    {
        for (auto& entry : m_transientTexturePool)
        {
            entry.InUseThisFrame = false;
        }
        for (auto& buf : m_buffers)
        {
            if (!buf.Imported && buf.PhysicalHandle.IsValid())
            {
                m_device.DestroyBuffer(buf.PhysicalHandle);
                buf.PhysicalHandle = RHI::BufferHandle::Invalid();
            }
        }
        m_textures.clear();
        m_buffers.clear();
        m_passes.clear();
        m_epilogueBarriers.clear();
        m_stats = {};
        m_compiled = false;
    }

    void RenderGraph::ReleaseTransientPool()
    {
        Reset();
        for (auto& entry : m_transientTexturePool)
        {
            if (entry.PhysicalHandle.IsValid())
            {
                m_device.DestroyTexture(entry.PhysicalHandle);
                entry.PhysicalHandle = RHI::TextureHandle::Invalid();
            }
        }
        m_transientTexturePool.clear();
    }

    RGTextureHandle RenderGraph::ImportTexture(
        std::string_view name,
        RHI::TextureHandle physicalTexture,
        const RHI::TextureDesc& desc,
        RHI::ResourceState initialState,
        RHI::ResourceState finalState)
    {
        const u32 idx = static_cast<u32>(m_textures.size());
        m_textures.push_back(TextureNode{
            .Name = std::string(name),
            .Desc = desc,
            .PhysicalHandle = physicalTexture,
            .Imported = true,
            .InitialState = initialState,
            .CurrentState = initialState,
            .FinalState = finalState
        });
        return RGTextureHandle{ idx };
    }

    RGTextureHandle RenderGraph::CreateTransientTexture(std::string_view name, const RHI::TextureDesc& desc)
    {
        const u32 idx = static_cast<u32>(m_textures.size());
        m_textures.push_back(TextureNode{
            .Name = std::string(name),
            .Desc = desc,
            .PhysicalHandle = RHI::TextureHandle::Invalid(),
            .Imported = false,
            .InitialState = RHI::ResourceState::Undefined,
            .CurrentState = RHI::ResourceState::Undefined,
            .FinalState = RHI::ResourceState::Undefined,
            .EstimatedByteSize = EstimateTextureBytes(desc)
        });
        return RGTextureHandle{ idx };
    }

    RGBufferHandle RenderGraph::CreateTransientBuffer(std::string_view name, const RHI::BufferDesc& desc)
    {
        const u32 idx = static_cast<u32>(m_buffers.size());
        m_buffers.push_back(BufferNode{
            .Name = std::string(name),
            .Desc = desc,
            .PhysicalHandle = RHI::BufferHandle::Invalid(),
            .Imported = false,
            .CurrentState = RHI::ResourceState::Undefined
        });
        return RGBufferHandle{ idx };
    }

    void RenderGraph::AddPass(
        std::string_view passName,
        const std::function<void(RenderGraphPassBuilder&)>& setup,
        ExecuteCallback execute)
    {
        const u32 passIndex = static_cast<u32>(m_passes.size());
        m_passes.push_back(PassNode{
            .Name = std::string(passName),
            .Execute = std::move(execute)
        });

        RenderGraphPassBuilder builder(*this, passIndex);
        setup(builder);
    }

    void RenderGraph::Compile()
    {
        m_stats = {};
        m_stats.TotalPasses = static_cast<u32>(m_passes.size());
        m_epilogueBarriers.clear();

        // 1. Transient Resource Memory Aliasing (Non-overlapping lifetimes share heap offsets)
        struct AliasedMemorySlot
        {
            u64 Offset = 0;
            u64 Size = 0;
            u32 LastPassUsed = 0;
        };
        std::vector<AliasedMemorySlot> memorySlots;
        u64 currentHeapPeak = 0;

        for (auto& tex : m_textures)
        {
            if (tex.Imported || tex.FirstPass == 0xFFFFFFFFu)
            {
                continue;
            }

            ++m_stats.TransientTextures;
            m_stats.UnaliasedTransientBytes += tex.EstimatedByteSize;

            bool reusedSlot = false;
            for (auto& slot : memorySlots)
            {
                // If the previous resource occupying this slot finished before our FirstPass, reuse its memory!
                if (slot.LastPassUsed < tex.FirstPass && slot.Size >= tex.EstimatedByteSize)
                {
                    tex.AliasingOffset = slot.Offset;
                    slot.LastPassUsed = tex.LastPass;
                    reusedSlot = true;
                    ++m_stats.AliasedMemoryBlocksReused;
                    break;
                }
            }

            if (!reusedSlot)
            {
                tex.AliasingOffset = currentHeapPeak;
                memorySlots.push_back(AliasedMemorySlot{
                    .Offset = currentHeapPeak,
                    .Size = tex.EstimatedByteSize,
                    .LastPassUsed = tex.LastPass
                });
                currentHeapPeak += tex.EstimatedByteSize;
            }

            tex.Desc.AliasingPoolOffset = 0xFFFFFFFFFFFFFFFFULL;
            tex.Desc.DebugName = tex.Name;

            RHI::TextureHandle pooledHandle = RHI::TextureHandle::Invalid();
            for (auto& entry : m_transientTexturePool)
            {
                if (!entry.InUseThisFrame && entry.Name == tex.Name)
                {
                    if (entry.Desc.Width == tex.Desc.Width &&
                        entry.Desc.Height == tex.Desc.Height &&
                        entry.Desc.PixelFormat == tex.Desc.PixelFormat &&
                        entry.Desc.RenderTarget == tex.Desc.RenderTarget &&
                        entry.Desc.DepthStencil == tex.Desc.DepthStencil &&
                        entry.PhysicalHandle.IsValid())
                    {
                        entry.InUseThisFrame = true;
                        pooledHandle = entry.PhysicalHandle;
                    }
                    else
                    {
                        if (entry.PhysicalHandle.IsValid())
                        {
                            m_device.DestroyTexture(entry.PhysicalHandle);
                        }
                        entry.Desc = tex.Desc;
                        entry.PhysicalHandle = m_device.CreateTexture(tex.Desc, nullptr);
                        entry.InUseThisFrame = true;
                        pooledHandle = entry.PhysicalHandle;
                    }
                    break;
                }
            }

            if (!pooledHandle.IsValid())
            {
                pooledHandle = m_device.CreateTexture(tex.Desc, nullptr);
                m_transientTexturePool.push_back(PooledTransientTexture{
                    .Name = tex.Name,
                    .Desc = tex.Desc,
                    .PhysicalHandle = pooledHandle,
                    .InUseThisFrame = true
                });
            }

            tex.PhysicalHandle = pooledHandle;
        }

        m_stats.AliasedTransientBytesPeak = currentHeapPeak;

        for (auto& buf : m_buffers)
        {
            if (!buf.Imported && buf.FirstPass != 0xFFFFFFFFu)
            {
                buf.Desc.DebugName = buf.Name;
                buf.PhysicalHandle = m_device.CreateBuffer(buf.Desc, nullptr);
            }
        }

        // 2. Synthesize Automatic Resource State Barriers per Pass
        for (auto& pass : m_passes)
        {
            pass.PrePassBarriers.clear();

            for (const auto& texAcc : pass.TextureAccesses)
            {
                auto& tex = m_textures[texAcc.Handle.Index];
                if (tex.CurrentState != texAcc.RequiredState)
                {
                    pass.PrePassBarriers.push_back(RGBarrierEvent{
                        .IsTexture = true,
                        .ResourceIndex = texAcc.Handle.Index,
                        .StateBefore = tex.CurrentState,
                        .StateAfter = texAcc.RequiredState
                    });
                    tex.CurrentState = texAcc.RequiredState;
                    ++m_stats.SynthesizedBarriers;
                }
            }

            for (const auto& bufAcc : pass.BufferAccesses)
            {
                auto& buf = m_buffers[bufAcc.Handle.Index];
                if (buf.CurrentState != bufAcc.RequiredState)
                {
                    pass.PrePassBarriers.push_back(RGBarrierEvent{
                        .IsTexture = false,
                        .ResourceIndex = bufAcc.Handle.Index,
                        .StateBefore = buf.CurrentState,
                        .StateAfter = bufAcc.RequiredState
                    });
                    buf.CurrentState = bufAcc.RequiredState;
                    ++m_stats.SynthesizedBarriers;
                }
            }
        }

        // 3. Epilogue transitions for imported textures (e.g. Backbuffer -> Present)
        for (u32 i = 0; i < static_cast<u32>(m_textures.size()); ++i)
        {
            auto& tex = m_textures[i];
            if (tex.Imported && tex.FinalState != RHI::ResourceState::Undefined && tex.CurrentState != tex.FinalState)
            {
                m_epilogueBarriers.push_back(RGBarrierEvent{
                    .IsTexture = true,
                    .ResourceIndex = i,
                    .StateBefore = tex.CurrentState,
                    .StateAfter = tex.FinalState
                });
                tex.CurrentState = tex.FinalState;
                ++m_stats.SynthesizedBarriers;
            }
        }

        m_compiled = true;
    }

    void RenderGraph::Execute()
    {
        if (!m_compiled)
        {
            Compile();
        }

        RHI::ICommandList& cmd = m_device.GetActiveCommandList();
        const RenderGraphRegistry registry(*this);

        for (const auto& pass : m_passes)
        {
            MORBI_BREADCRUMB_DETAIL("RenderGraph Pass Execute", pass.Name.c_str());
            MORBI_TRACE("RenderGraph", "Executing Pass '" + pass.Name + "' (Barriers=" +
                std::to_string(pass.PrePassBarriers.size()) + ")");
            cmd.BeginDebugMarker(pass.Name);

            for (const auto& barrier : pass.PrePassBarriers)
            {
                if (barrier.IsTexture)
                {
                    const auto physTex = m_textures[barrier.ResourceIndex].PhysicalHandle;
                    if (physTex.IsValid())
                    {
                        cmd.TransitionTexture(physTex, barrier.StateBefore, barrier.StateAfter);
                    }
                }
                else
                {
                    const auto physBuf = m_buffers[barrier.ResourceIndex].PhysicalHandle;
                    if (physBuf.IsValid())
                    {
                        cmd.TransitionBuffer(physBuf, barrier.StateBefore, barrier.StateAfter);
                    }
                }
            }

            if (pass.Execute)
            {
                pass.Execute(registry, cmd);
            }

            cmd.EndDebugMarker();
        }

        for (const auto& barrier : m_epilogueBarriers)
        {
            if (barrier.IsTexture)
            {
                const auto physTex = m_textures[barrier.ResourceIndex].PhysicalHandle;
                if (physTex.IsValid())
                {
                    cmd.TransitionTexture(physTex, barrier.StateBefore, barrier.StateAfter);
                }
            }
        }
    }

    RHI::TextureHandle RenderGraph::ResolvePhysicalTexture(RGTextureHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.Index >= m_textures.size())
        {
            return RHI::TextureHandle::Invalid();
        }
        return m_textures[handle.Index].PhysicalHandle;
    }

    RHI::BufferHandle RenderGraph::ResolvePhysicalBuffer(RGBufferHandle handle) const noexcept
    {
        if (!handle.IsValid() || handle.Index >= m_buffers.size())
        {
            return RHI::BufferHandle::Invalid();
        }
        return m_buffers[handle.Index].PhysicalHandle;
    }
}
