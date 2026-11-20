#pragma once

#include <span>
#include <string_view>
#include <unordered_set>

#include "Morbi/RHI/RHITypes.hpp"

namespace Morbi::RHI
{
    struct RenderAttachmentDesc
    {
        TextureHandle Texture = TextureHandle::Invalid();
        bool ClearOnLoad = true;
        f32 ClearColor[4]{ 0.06f, 0.08f, 0.12f, 1.0f };
    };

    struct DynamicRenderPassDesc
    {
        std::span<const RenderAttachmentDesc> ColorAttachments;
        TextureHandle DepthAttachment = TextureHandle::Invalid();
        bool ClearDepth = true;
        f32 DepthClearValue = 1.0f;
    };

    class ICommandList
    {
    public:
        virtual ~ICommandList() = default;

        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void Discard() = 0;

        virtual void TrackBuffer(BufferHandle buffer) = 0;
        virtual void TrackTexture(TextureHandle texture) = 0;
        virtual void TrackPipeline(PipelineHandle pipeline) = 0;
        virtual void TrackPipelineLayout(PipelineLayoutHandle layout)
        {
            (void)layout;
        }

        [[nodiscard]] virtual const std::unordered_set<BufferHandle>& GetReferencedBuffers() const noexcept = 0;
        [[nodiscard]] virtual const std::unordered_set<TextureHandle>& GetReferencedTextures() const noexcept = 0;
        [[nodiscard]] virtual const std::unordered_set<PipelineHandle>& GetReferencedPipelines() const noexcept = 0;
        [[nodiscard]] virtual const std::unordered_set<PipelineLayoutHandle>& GetReferencedPipelineLayouts() const noexcept
        {
            static const std::unordered_set<PipelineLayoutHandle> s_empty;
            return s_empty;
        }

        virtual void TransitionTexture(TextureHandle texture, ResourceState before, ResourceState after) = 0;
        virtual void TransitionBuffer(BufferHandle buffer, ResourceState before, ResourceState after) = 0;

        virtual void BeginRendering(const DynamicRenderPassDesc& passDesc) = 0;
        virtual void EndRendering() = 0;

        virtual void SetViewport(const Viewport& viewport) = 0;
        virtual void SetScissor(const ScissorRect& scissor) = 0;
        virtual void SetPipeline(PipelineHandle pipeline) = 0;

        virtual void PushConstants(const void* data, u32 sizeBytes, u32 offsetBytes = 0) = 0;

        virtual void BindVertexBuffer(BufferHandle buffer, u64 offsetBytes = 0) = 0;
        virtual void BindIndexBuffer(BufferHandle buffer, u64 offsetBytes = 0, bool index32Bit = true) = 0;
        virtual void BindConstantBuffer(u32 rootParamSlot, BufferHandle buffer, u64 offsetBytes = 0)
        {
            (void)rootParamSlot;
            (void)buffer;
            (void)offsetBytes;
        }

        virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
        virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) = 0;
        virtual void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) = 0;

        virtual void BeginDebugMarker(std::string_view label) = 0;
        virtual void EndDebugMarker() = 0;
    };
}
