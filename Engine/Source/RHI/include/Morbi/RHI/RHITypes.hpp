#pragma once

#include <span>
#include <string_view>

#include "Morbi/Core/Memory/GenerationalHandle.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::RHI
{
    enum class BackendAPI : u8
    {
        Null = 0,
        Vulkan,
        DirectX12,
        Metal
    };

    struct BufferTag {};
    struct TextureTag {};
    struct SamplerTag {};
    struct PipelineTag {};
    struct PipelineLayoutTag {};
    struct ShaderTag {};

    using BufferHandle         = Core::GenerationalHandle<BufferTag>;
    using TextureHandle        = Core::GenerationalHandle<TextureTag>;
    using SamplerHandle        = Core::GenerationalHandle<SamplerTag>;
    using PipelineHandle       = Core::GenerationalHandle<PipelineTag>;
    using PipelineLayoutHandle = Core::GenerationalHandle<PipelineLayoutTag>;
    using ShaderHandle         = Core::GenerationalHandle<ShaderTag>;

    // Bindless heap indices (SM 6.6 / VK_EXT_descriptor_indexing / Metal Argument Buffers)
    struct BindlessResourceIndex
    {
        static constexpr u32 kInvalid = 0xFFFFFFFFu;
        u32 Index = kInvalid;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != kInvalid; }
    };

    enum class Format : u8
    {
        Unknown = 0,
        RGBA8_UNorm,
        RGBA8_sRGB,
        BGRA8_UNorm,
        BGRA8_sRGB,
        RG16_Float,
        RGBA16_Float,
        R32_Float,
        RG32_Float,
        RGB32_Float,
        RGBA32_Float,
        D32_Float,
        D24_UNorm_S8_UInt
    };

    enum class BufferUsage : u32
    {
        None         = 0,
        Vertex       = 1u << 0,
        Index        = 1u << 1,
        Uniform      = 1u << 2,
        Storage      = 1u << 3,
        Indirect     = 1u << 4,
        TransferSrc  = 1u << 5,
        TransferDst  = 1u << 6
    };

    [[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept
    {
        return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    [[nodiscard]] constexpr bool HasFlag(BufferUsage flags, BufferUsage bit) noexcept
    {
        return (static_cast<u32>(flags) & static_cast<u32>(bit)) != 0;
    }

    enum class MemoryUsage : u8
    {
        GpuOnly = 0,
        CpuToGpu,
        GpuToCpu
    };

    enum class ResourceState : u16
    {
        Undefined = 0,
        Common,
        VertexBuffer,
        IndexBuffer,
        ConstantBuffer,
        ShaderResource,
        UnorderedAccess,
        RenderTarget,
        DepthWrite,
        DepthRead,
        CopySource,
        CopyDest,
        Present
    };

    struct BufferDesc
    {
        u64 SizeBytes = 0;
        u32 StrideBytes = 0;
        BufferUsage Usage = BufferUsage::None;
        MemoryUsage Memory = MemoryUsage::GpuOnly;
        bool Bindless = true;
        u64 AliasingPoolOffset = 0xFFFFFFFFFFFFFFFFULL; // For transient RenderGraph memory aliasing
        std::string_view DebugName;
    };

    struct TextureDesc
    {
        u32 Width = 1;
        u32 Height = 1;
        u32 DepthOrArrayLayers = 1;
        u32 MipLevels = 1;
        Format PixelFormat = Format::RGBA8_UNorm;
        bool RenderTarget = false;
        bool DepthStencil = false;
        bool Storage = false;
        u64 AliasingPoolOffset = 0xFFFFFFFFFFFFFFFFULL; // For transient RenderGraph memory aliasing
        std::string_view DebugName;
    };

    struct GraphicsPipelineDesc
    {
        std::span<const u8> VertexShaderBytecode;
        std::span<const u8> PixelShaderBytecode;
        PipelineLayoutHandle PipelineLayout = PipelineLayoutHandle::Invalid();
        Format ColorTargetFormat = Format::BGRA8_UNorm;
        Format DepthTargetFormat = Format::Unknown;
        bool DepthTestEnable = false;
        bool DepthWriteEnable = false;
        bool AlphaBlendEnable = false;
        bool Wireframe = false;
        u32 VertexStrideBytes = 0;
        u32 PushConstantSizeBytes = 64;
        std::string_view DebugName;
    };

    struct ComputePipelineDesc
    {
        std::span<const u8> ComputeShaderBytecode;
        PipelineLayoutHandle PipelineLayout = PipelineLayoutHandle::Invalid();
        u32 PushConstantSizeBytes = 64;
        std::string_view DebugName;
    };

    struct MemoryAllocatorStats
    {
        u32 HeapBlockCount = 0;
        u32 ActiveAllocations = 0;
        u64 TotalReservedBytes = 0;
        u64 TotalAllocatedBytes = 0;
        u64 TransientAliasedBytesSaved = 0;
    };

    struct Viewport
    {
        f32 X = 0.0f;
        f32 Y = 0.0f;
        f32 Width = 0.0f;
        f32 Height = 0.0f;
        f32 MinDepth = 0.0f;
        f32 MaxDepth = 1.0f;
    };

    struct ScissorRect
    {
        i32 X = 0;
        i32 Y = 0;
        u32 Width = 0;
        u32 Height = 0;
    };

    struct PushConstantsDesc
    {
        const void* Data = nullptr;
        u32 SizeBytes = 0;
        u32 OffsetBytes = 0;
    };
}
