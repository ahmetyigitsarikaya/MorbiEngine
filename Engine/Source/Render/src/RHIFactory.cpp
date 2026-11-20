#include "Morbi/RHI/IDevice.hpp"

namespace Morbi::RHI
{
    std::unique_ptr<IDevice> CreateDevice(BackendAPI api)
    {
        switch (api)
        {
        case BackendAPI::Vulkan:
            return CreateVulkanDevice();
        case BackendAPI::DirectX12:
            return CreateDX12Device();
        case BackendAPI::Null:
        case BackendAPI::Metal:
        default:
            return CreateNullDevice();
        }
    }
}
