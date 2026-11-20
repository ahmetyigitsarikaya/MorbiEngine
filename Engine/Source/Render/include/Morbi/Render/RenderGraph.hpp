#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Types.hpp"
#include "Morbi/RHI/IDevice.hpp"

namespace Morbi::Render
{
    struct RGTextureHandle
    {
        static constexpr u32 kInvalid = 0xFFFFFFFFu;
        u32 Index = kInvalid;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != kInvalid; }
        [[nodiscard]] constexpr bool operator==(const RGTextureHandle& other) const noexcept = default;
    };

    struct RGBufferHandle
    {
        static constexpr u32 kInvalid = 0xFFFFFFFFu;
        u32 Index = kInvalid;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != kInvalid; }
        [[nodiscard]] constexpr bool operator==(const RGBufferHandle& other) const noexcept = default;
    };

    struct RGBarrierEvent
    {
        bool IsTexture = true;
        u32 ResourceIndex = 0;
        RHI::ResourceState StateBefore = RHI::ResourceState::Undefined;
        RHI::ResourceState StateAfter = RHI::ResourceState::Common;
    };

    struct RenderGraphCompileStats
    {
        u32 TotalPasses = 0;
        u32 TransientTextures = 0;
        u32 AliasedMemoryBlocksReused = 0;
        u32 SynthesizedBarriers = 0;
        u64 UnaliasedTransientBytes = 0;
        u64 AliasedTransientBytesPeak = 0;
    };

    class RenderGraph;

    class MORBI_API RenderGraphPassBuilder
    {
    public:
        RenderGraphPassBuilder(RenderGraph& graph, u32 passIndex) noexcept
            : m_graph(graph), m_passIndex(passIndex)
        {
        }
        RenderGraphPassBuilder(const RenderGraphPassBuilder&) = delete;
        RenderGraphPassBuilder& operator=(const RenderGraphPassBuilder&) = delete;

        RGTextureHandle ReadTexture(RGTextureHandle handle, RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        RGTextureHandle WriteTexture(RGTextureHandle handle, RHI::ResourceState state = RHI::ResourceState::RenderTarget);
        RGBufferHandle ReadBuffer(RGBufferHandle handle, RHI::ResourceState state = RHI::ResourceState::ShaderResource);
        RGBufferHandle WriteBuffer(RGBufferHandle handle, RHI::ResourceState state = RHI::ResourceState::UnorderedAccess);

    private:
        RenderGraph& m_graph;
        u32 m_passIndex = 0;
    };

    class MORBI_API RenderGraphRegistry
    {
    public:
        explicit RenderGraphRegistry(const RenderGraph& graph) noexcept : m_graph(graph) {}
        RenderGraphRegistry(const RenderGraphRegistry&) = delete;
        RenderGraphRegistry& operator=(const RenderGraphRegistry&) = delete;

        [[nodiscard]] RHI::TextureHandle GetTexture(RGTextureHandle handle) const noexcept;
        [[nodiscard]] RHI::BufferHandle GetBuffer(RGBufferHandle handle) const noexcept;

    private:
        const RenderGraph& m_graph;
    };

    class MORBI_API RenderGraph
    {
    public:
        using ExecuteCallback = std::function<void(const RenderGraphRegistry& registry, RHI::ICommandList& cmd)>;

        explicit RenderGraph(RHI::IDevice& device) noexcept : m_device(device) {}
        ~RenderGraph() { ReleaseTransientPool(); }
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        void Reset();
        void ReleaseTransientPool();

        [[nodiscard]] RGTextureHandle ImportTexture(
            std::string_view name,
            RHI::TextureHandle physicalTexture,
            const RHI::TextureDesc& desc,
            RHI::ResourceState initialState = RHI::ResourceState::Undefined,
            RHI::ResourceState finalState = RHI::ResourceState::Present);

        [[nodiscard]] RGTextureHandle CreateTransientTexture(
            std::string_view name,
            const RHI::TextureDesc& desc);

        [[nodiscard]] RGBufferHandle CreateTransientBuffer(
            std::string_view name,
            const RHI::BufferDesc& desc);

        void AddPass(
            std::string_view passName,
            const std::function<void(RenderGraphPassBuilder&)>& setup,
            ExecuteCallback execute);

        void Compile();
        void Execute();

        [[nodiscard]] const RenderGraphCompileStats& GetStats() const noexcept { return m_stats; }
        [[nodiscard]] RHI::TextureHandle ResolvePhysicalTexture(RGTextureHandle handle) const noexcept;
        [[nodiscard]] RHI::BufferHandle ResolvePhysicalBuffer(RGBufferHandle handle) const noexcept;

    private:
        friend class RenderGraphPassBuilder;

        struct TextureAccess
        {
            RGTextureHandle Handle{};
            RHI::ResourceState RequiredState = RHI::ResourceState::Common;
            bool IsWrite = false;
        };

        struct BufferAccess
        {
            RGBufferHandle Handle{};
            RHI::ResourceState RequiredState = RHI::ResourceState::Common;
            bool IsWrite = false;
        };

        struct PassNode
        {
            std::string Name;
            std::vector<TextureAccess> TextureAccesses;
            std::vector<BufferAccess> BufferAccesses;
            std::vector<RGBarrierEvent> PrePassBarriers;
            ExecuteCallback Execute;
        };

        struct TextureNode
        {
            std::string Name;
            RHI::TextureDesc Desc{};
            RHI::TextureHandle PhysicalHandle = RHI::TextureHandle::Invalid();
            bool Imported = false;
            RHI::ResourceState InitialState = RHI::ResourceState::Undefined;
            RHI::ResourceState CurrentState = RHI::ResourceState::Undefined;
            RHI::ResourceState FinalState = RHI::ResourceState::Undefined;
            u32 FirstPass = 0xFFFFFFFFu;
            u32 LastPass = 0;
            u64 EstimatedByteSize = 0;
            u64 AliasingOffset = 0xFFFFFFFFFFFFFFFFULL;
        };

        struct BufferNode
        {
            std::string Name;
            RHI::BufferDesc Desc{};
            RHI::BufferHandle PhysicalHandle = RHI::BufferHandle::Invalid();
            bool Imported = false;
            RHI::ResourceState CurrentState = RHI::ResourceState::Undefined;
            u32 FirstPass = 0xFFFFFFFFu;
            u32 LastPass = 0;
            u64 AliasingOffset = 0xFFFFFFFFFFFFFFFFULL;
        };

        struct PooledTransientTexture
        {
            std::string Name;
            RHI::TextureDesc Desc{};
            RHI::TextureHandle PhysicalHandle = RHI::TextureHandle::Invalid();
            bool InUseThisFrame = false;
        };

        RHI::IDevice& m_device;
        std::vector<TextureNode> m_textures;
        std::vector<BufferNode> m_buffers;
        std::vector<PassNode> m_passes;
        std::vector<RGBarrierEvent> m_epilogueBarriers;
        std::vector<PooledTransientTexture> m_transientTexturePool;
        RenderGraphCompileStats m_stats{};
        bool m_compiled = false;
    };
}
