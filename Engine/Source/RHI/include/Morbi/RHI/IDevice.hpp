#pragma once

#include <memory>
#include <string>

#include "Morbi/Core/Result.hpp"
#include "Morbi/Platform/Window.hpp"
#include "Morbi/RHI/ICommandList.hpp"
#include "Morbi/RHI/PipelineLayout.hpp"
#include "Morbi/RHI/RHITypes.hpp"

namespace Morbi::RHI
{
    struct DeviceDesc
    {
        BackendAPI PreferredAPI = BackendAPI::Vulkan;
        bool EnableValidation = false;
        std::string_view ApplicationName = "MorbiEngine";
    };

    class IDevice
    {
    public:
        virtual ~IDevice() = default;

        [[nodiscard]] virtual BackendAPI GetAPI() const noexcept = 0;
        [[nodiscard]] virtual std::string_view GetAdapterName() const noexcept = 0;
        [[nodiscard]] virtual Core::Result<bool, std::string> Initialize(const DeviceDesc& desc, const Platform::NativeWindowHandle& windowHandle, u32 width, u32 height) = 0;
        virtual void Shutdown() = 0;

        virtual bool BeginFrame() = 0;
        virtual void EndFrameAndPresent() = 0;
        virtual void ResizeSwapchain(u32 width, u32 height) = 0;
        virtual void SetVSync(bool enabled) = 0;
        [[nodiscard]] virtual bool IsVSyncEnabled() const noexcept = 0;
        virtual void WaitIdle() = 0;

        [[nodiscard]] virtual u64 GetCompletedTimelineValue() const noexcept = 0;
        virtual void ProcessCompletedDestructions() {}
        [[nodiscard]] virtual MemoryAllocatorStats GetMemoryStats() const noexcept = 0;

        [[nodiscard]] virtual ICommandList& GetActiveCommandList() = 0;
        [[nodiscard]] virtual std::unique_ptr<ICommandList> CreateCommandList() { return nullptr; }
        virtual u64 SubmitCommandList(ICommandList& cmdList)
        {
            (void)cmdList;
            return 0;
        }
        [[nodiscard]] virtual TextureHandle GetCurrentBackbuffer() const noexcept = 0;
        [[nodiscard]] virtual Format GetSwapchainFormat() const noexcept = 0;

        [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
        virtual void DestroyBuffer(BufferHandle handle) = 0;
        virtual void UpdateBufferData(BufferHandle handle, const void* data, usize sizeBytes, usize offsetBytes = 0)
        {
            (void)handle;
            (void)data;
            (void)sizeBytes;
            (void)offsetBytes;
        }
        [[nodiscard]] virtual BindlessResourceIndex GetBindlessIndex(BufferHandle handle) const noexcept = 0;
        [[nodiscard]] virtual bool IsResourceAlive(BufferHandle handle) const noexcept
        {
            return GetBindlessIndex(handle).IsValid();
        }

        [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDesc& desc, const void* initialData = nullptr) = 0;
        virtual void DestroyTexture(TextureHandle handle) = 0;
        [[nodiscard]] virtual BindlessResourceIndex GetBindlessIndex(TextureHandle handle) const noexcept = 0;
        [[nodiscard]] virtual bool IsResourceAlive(TextureHandle handle) const noexcept
        {
            return GetBindlessIndex(handle).IsValid();
        }

        [[nodiscard]] virtual Core::Result<PipelineLayoutHandle, std::string> CreatePipelineLayout(const PipelineLayoutDesc& desc)
        {
            (void)desc;
            return Core::Result<PipelineLayoutHandle, std::string>::Failure(
                "CreatePipelineLayout is not supported by this RHI device.");
        }
        virtual void DestroyPipelineLayout(PipelineLayoutHandle handle)
        {
            (void)handle;
        }
        [[nodiscard]] virtual bool IsResourceAlive(PipelineLayoutHandle handle) const noexcept
        {
            (void)handle;
            return false;
        }
        [[nodiscard]] virtual PipelineLayoutHandle GetDefaultBindlessPipelineLayout() const noexcept
        {
            return PipelineLayoutHandle::Invalid();
        }
        [[nodiscard]] virtual PipelineLayoutLimits GetPipelineLayoutLimits() const noexcept
        {
            return PipelineLayoutLimits{};
        }
        [[nodiscard]] virtual PipelineLayoutStats GetPipelineLayoutStats() const noexcept
        {
            return PipelineLayoutStats{};
        }

        [[nodiscard]] virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
        [[nodiscard]] virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
        virtual void DestroyPipeline(PipelineHandle handle) = 0;
        [[nodiscard]] virtual bool IsResourceAlive(PipelineHandle handle) const noexcept
        {
            (void)handle;
            return false;
        }
    };

    [[nodiscard]] MORBI_API std::unique_ptr<IDevice> CreateNullDevice();
    [[nodiscard]] MORBI_API std::unique_ptr<IDevice> CreateVulkanDevice();
    [[nodiscard]] MORBI_API std::unique_ptr<IDevice> CreateDX12Device();
    [[nodiscard]] MORBI_API std::unique_ptr<IDevice> CreateDevice(BackendAPI api);
}
