#pragma once

#include <vulkan/vulkan.h>

#include "Morbi/Core/Types.hpp"

namespace Morbi::RHI::Vulkan
{
    struct VulkanGlobalDispatch
    {
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
        PFN_vkCreateInstance CreateInstance = nullptr;
        PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
        PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayerProperties = nullptr;
    };

    struct VulkanInstanceDispatch
    {
        PFN_vkDestroyInstance DestroyInstance = nullptr;
        PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
        PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
        PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
        PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
        PFN_vkCreateDevice CreateDevice = nullptr;
        PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
        PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR = nullptr;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR = nullptr;
#endif
    };

    struct VulkanDeviceDispatch
    {
        PFN_vkDestroyDevice DestroyDevice = nullptr;
        PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
        PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
        PFN_vkQueueSubmit QueueSubmit = nullptr;
        PFN_vkQueueWaitIdle QueueWaitIdle = nullptr;
        PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
        PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
        PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
        PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
        PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
        PFN_vkCreateImageView CreateImageView = nullptr;
        PFN_vkDestroyImageView DestroyImageView = nullptr;
        PFN_vkCreateCommandPool CreateCommandPool = nullptr;
        PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
        PFN_vkResetCommandPool ResetCommandPool = nullptr;
        PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
        PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
        PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
        PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
        PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
        PFN_vkCmdBeginRendering CmdBeginRendering = nullptr;
        PFN_vkCmdEndRendering CmdEndRendering = nullptr;
        PFN_vkCmdSetViewport CmdSetViewport = nullptr;
        PFN_vkCmdSetScissor CmdSetScissor = nullptr;
        PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
        PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
        PFN_vkCmdPushConstants CmdPushConstants = nullptr;
        PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers = nullptr;
        PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = nullptr;
        PFN_vkCmdDraw CmdDraw = nullptr;
        PFN_vkCmdDrawIndexed CmdDrawIndexed = nullptr;
        PFN_vkCmdDispatch CmdDispatch = nullptr;
        PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
        PFN_vkCreateSampler CreateSampler = nullptr;
        PFN_vkDestroySampler DestroySampler = nullptr;
        PFN_vkCreateSemaphore CreateSemaphore = nullptr;
        PFN_vkDestroySemaphore DestroySemaphore = nullptr;
        PFN_vkWaitSemaphores WaitSemaphores = nullptr;
        PFN_vkSignalSemaphore SignalSemaphore = nullptr;
        PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue = nullptr;
        PFN_vkCreateFence CreateFence = nullptr;
        PFN_vkDestroyFence DestroyFence = nullptr;
        PFN_vkWaitForFences WaitForFences = nullptr;
        PFN_vkResetFences ResetFences = nullptr;
        PFN_vkCreateBuffer CreateBuffer = nullptr;
        PFN_vkDestroyBuffer DestroyBuffer = nullptr;
        PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
        PFN_vkBindBufferMemory BindBufferMemory = nullptr;
        PFN_vkCreateImage CreateImage = nullptr;
        PFN_vkDestroyImage DestroyImage = nullptr;
        PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
        PFN_vkBindImageMemory BindImageMemory = nullptr;
        PFN_vkAllocateMemory AllocateMemory = nullptr;
        PFN_vkFreeMemory FreeMemory = nullptr;
        PFN_vkMapMemory MapMemory = nullptr;
        PFN_vkUnmapMemory UnmapMemory = nullptr;
        PFN_vkCreateShaderModule CreateShaderModule = nullptr;
        PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
        PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
        PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
        PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
        PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
        PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
        PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;
        PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
        PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
        PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
        PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
        PFN_vkDestroyPipeline DestroyPipeline = nullptr;
    };
}
