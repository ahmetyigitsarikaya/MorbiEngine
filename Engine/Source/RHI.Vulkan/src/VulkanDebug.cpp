#include "Morbi/RHI/Vulkan/VulkanDebug.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"

namespace Morbi::RHI::Vulkan
{
    namespace
    {
        constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

        const char* VkObjectTypeName(VkObjectType type) noexcept
        {
            switch (type)
            {
            case VK_OBJECT_TYPE_INSTANCE:              return "VkInstance";
            case VK_OBJECT_TYPE_PHYSICAL_DEVICE:       return "VkPhysicalDevice";
            case VK_OBJECT_TYPE_DEVICE:                return "VkDevice";
            case VK_OBJECT_TYPE_QUEUE:                 return "VkQueue";
            case VK_OBJECT_TYPE_SEMAPHORE:             return "VkSemaphore";
            case VK_OBJECT_TYPE_COMMAND_BUFFER:        return "VkCommandBuffer";
            case VK_OBJECT_TYPE_FENCE:                 return "VkFence";
            case VK_OBJECT_TYPE_DEVICE_MEMORY:         return "VkDeviceMemory";
            case VK_OBJECT_TYPE_BUFFER:                return "VkBuffer";
            case VK_OBJECT_TYPE_IMAGE:                 return "VkImage";
            case VK_OBJECT_TYPE_IMAGE_VIEW:            return "VkImageView";
            case VK_OBJECT_TYPE_SHADER_MODULE:         return "VkShaderModule";
            case VK_OBJECT_TYPE_PIPELINE_LAYOUT:       return "VkPipelineLayout";
            case VK_OBJECT_TYPE_PIPELINE:              return "VkPipeline";
            case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "VkDescriptorSetLayout";
            case VK_OBJECT_TYPE_SAMPLER:               return "VkSampler";
            case VK_OBJECT_TYPE_DESCRIPTOR_POOL:       return "VkDescriptorPool";
            case VK_OBJECT_TYPE_DESCRIPTOR_SET:        return "VkDescriptorSet";
            case VK_OBJECT_TYPE_COMMAND_POOL:          return "VkCommandPool";
            case VK_OBJECT_TYPE_SURFACE_KHR:           return "VkSurfaceKHR";
            case VK_OBJECT_TYPE_SWAPCHAIN_KHR:         return "VkSwapchainKHR";
            default:                                   return "VkObject";
            }
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugMessengerCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageTypes,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData)
        {
            (void)pUserData;
            if (pCallbackData == nullptr)
            {
                return VK_FALSE;
            }

            const char* typeTag = "GENERAL";
            if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
            {
                typeTag = "VALIDATION";
            }
            else if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
            {
                typeTag = "PERFORMANCE";
            }

            std::string formatted;
            formatted.reserve(512);
            formatted.append("[");
            formatted.append(typeTag);
            formatted.append("] ");
            if (pCallbackData->pMessageIdName != nullptr)
            {
                formatted.append("[");
                formatted.append(pCallbackData->pMessageIdName);
                formatted.append("] ");
            }
            if (pCallbackData->pMessage != nullptr)
            {
                formatted.append(pCallbackData->pMessage);
            }

            if (pCallbackData->objectCount > 0 && pCallbackData->pObjects != nullptr)
            {
                formatted.append(" | Objects=[");
                for (uint32_t i = 0; i < pCallbackData->objectCount; ++i)
                {
                    if (i > 0) formatted.append(", ");
                    const auto& obj = pCallbackData->pObjects[i];
                    formatted.append(VkObjectTypeName(obj.objectType));
                    formatted.append(":");
                    if (obj.pObjectName != nullptr && obj.pObjectName[0] != '\0')
                    {
                        formatted.append("'");
                        formatted.append(obj.pObjectName);
                        formatted.append("'");
                    }
                    else
                    {
                        char handleBuf[32]{};
                        std::snprintf(handleBuf, sizeof(handleBuf), "0x%llX", static_cast<unsigned long long>(obj.objectHandle));
                        formatted.append(handleBuf);
                    }
                }
                formatted.append("]");
            }

            if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                Core::Log::Error("RHI.Vulkan.Debug", formatted);
            }
            else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                Core::Log::Warn("RHI.Vulkan.Debug", formatted);
            }
            else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            {
                Core::Log::Info("RHI.Vulkan.Debug", formatted);
            }
            else
            {
                Core::Log::Trace("RHI.Vulkan.Debug", formatted);
            }

            return VK_FALSE;
        }
    }

    void VulkanDebug::PrepareInstanceConfig(
        const VulkanGlobalDispatch& globalDisp,
        bool enableValidation,
        bool enableGpuGbv,
        const char* const* baseExtensions,
        std::size_t baseExtensionCount,
        VulkanDebugInstanceConfig& outConfig) noexcept
    {
        outConfig = VulkanDebugInstanceConfig{};
        for (std::size_t i = 0; i < baseExtensionCount; ++i)
        {
            if (baseExtensions[i] != nullptr)
            {
                outConfig.EnabledExtensions.push_back(baseExtensions[i]);
            }
        }

        const bool debugActive = enableValidation || Core::Diagnostics::IsDebugMode();
        const bool gbvActive = enableGpuGbv || Core::Diagnostics::IsGpuGbvMode();
        outConfig.DebugActive = debugActive;

        if (!debugActive)
        {
            return;
        }

        if (globalDisp.EnumerateInstanceLayerProperties != nullptr)
        {
            uint32_t layerCount = 0;
            globalDisp.EnumerateInstanceLayerProperties(&layerCount, nullptr);
            if (layerCount > 0)
            {
                std::vector<VkLayerProperties> layers(layerCount);
                globalDisp.EnumerateInstanceLayerProperties(&layerCount, layers.data());
                for (const auto& layer : layers)
                {
                    if (std::strcmp(layer.layerName, kValidationLayerName) == 0)
                    {
                        outConfig.ValidationLayerAvailable = true;
                        break;
                    }
                }
            }
        }

        auto checkExtensionsInLayer = [&](const char* layerName) {
            if (globalDisp.EnumerateInstanceExtensionProperties == nullptr)
            {
                return;
            }
            uint32_t extCount = 0;
            globalDisp.EnumerateInstanceExtensionProperties(layerName, &extCount, nullptr);
            if (extCount > 0)
            {
                std::vector<VkExtensionProperties> exts(extCount);
                globalDisp.EnumerateInstanceExtensionProperties(layerName, &extCount, exts.data());
                for (const auto& ext : exts)
                {
                    if (std::strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                    {
                        outConfig.DebugUtilsAvailable = true;
                    }
                    else if (std::strcmp(ext.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0)
                    {
                        outConfig.ValidationFeaturesAvailable = true;
                    }
                }
            }
        };

        checkExtensionsInLayer(nullptr);
        if (outConfig.ValidationLayerAvailable)
        {
            checkExtensionsInLayer(kValidationLayerName);
            outConfig.EnabledLayers.push_back(kValidationLayerName);
            Core::Log::Info("RHI.Vulkan.Debug", "Enabling VK_LAYER_KHRONOS_validation dynamically.");
        }
        else
        {
            Core::Log::Warn("RHI.Vulkan.Debug", "VK_LAYER_KHRONOS_validation not found on this machine (install Vulkan SDK for full validation).");
        }

        if (outConfig.DebugUtilsAvailable)
        {
            outConfig.EnabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            outConfig.MessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            outConfig.MessengerCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            outConfig.MessengerCreateInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            outConfig.MessengerCreateInfo.pfnUserCallback = VulkanDebugMessengerCallback;
            Core::Log::Info("RHI.Vulkan.Debug", "Enabling VK_EXT_debug_utils messenger & object naming.");
        }
        else
        {
            Core::Log::Warn("RHI.Vulkan.Debug", "VK_EXT_debug_utils extension unavailable.");
        }

        if (gbvActive && outConfig.ValidationLayerAvailable && outConfig.ValidationFeaturesAvailable)
        {
            outConfig.EnabledValidationFeatures = {
                VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
                VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
                VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
            };
            outConfig.ValidationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
            outConfig.ValidationFeatures.enabledValidationFeatureCount =
                static_cast<uint32_t>(outConfig.EnabledValidationFeatures.size());
            outConfig.ValidationFeatures.pEnabledValidationFeatures =
                outConfig.EnabledValidationFeatures.data();
            Core::Log::Info("RHI.Vulkan.Debug", "Enabling Vulkan GPU-Assisted & Synchronization Validation (--gpu-gbv).");
        }
    }

    void* VulkanDebug::GetInstanceCreatePNext(VulkanDebugInstanceConfig& config) noexcept
    {
        if (!config.DebugActive)
        {
            return nullptr;
        }

        void* head = nullptr;
        if (config.DebugUtilsAvailable)
        {
            config.MessengerCreateInfo.pNext = head;
            head = &config.MessengerCreateInfo;
        }
        if (!config.EnabledValidationFeatures.empty())
        {
            config.ValidationFeatures.pNext = head;
            head = &config.ValidationFeatures;
        }
        return head;
    }

    void VulkanDebug::InitializePostInstance(
        VkInstance instance,
        PFN_vkGetInstanceProcAddr getInstanceProcAddr,
        const VulkanDebugInstanceConfig& config) noexcept
    {
        if (!config.DebugActive || !config.DebugUtilsAvailable || instance == VK_NULL_HANDLE || getInstanceProcAddr == nullptr)
        {
            return;
        }

        m_createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            getInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        m_destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            getInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        m_setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            getInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
        m_cmdBeginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            getInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
        m_cmdEndLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            getInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));

        if (m_createMessenger != nullptr)
        {
            const VkResult res = m_createMessenger(instance, &config.MessengerCreateInfo, nullptr, &m_messenger);
            if (res == VK_SUCCESS)
            {
                m_debugUtilsActive = true;
                Core::Log::Info("RHI.Vulkan.Debug", "VkDebugUtilsMessengerEXT active.");
            }
            else
            {
                Core::Log::Warn("RHI.Vulkan.Debug",
                    std::string("vkCreateDebugUtilsMessengerEXT failed: ") + VkResultToString(res));
            }
        }
    }

    void VulkanDebug::Shutdown(VkInstance instance) noexcept
    {
        if (m_messenger != VK_NULL_HANDLE && m_destroyMessenger != nullptr && instance != VK_NULL_HANDLE)
        {
            m_destroyMessenger(instance, m_messenger, nullptr);
            m_messenger = VK_NULL_HANDLE;
        }
        m_debugUtilsActive = false;
        m_createMessenger = nullptr;
        m_destroyMessenger = nullptr;
        m_setObjectName = nullptr;
        m_cmdBeginLabel = nullptr;
        m_cmdEndLabel = nullptr;
    }

    void VulkanDebug::SetObjectName(
        VkDevice device,
        VkObjectType objectType,
        std::uint64_t objectHandle,
        std::string_view name) const noexcept
    {
        if (!Core::Diagnostics::IsDebugMode() ||
            m_setObjectName == nullptr ||
            device == VK_NULL_HANDLE ||
            objectHandle == 0 ||
            name.empty())
        {
            return;
        }

        const std::string nullTerminated(name);
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = objectType;
        nameInfo.objectHandle = objectHandle;
        nameInfo.pObjectName = nullTerminated.c_str();
        m_setObjectName(device, &nameInfo);
    }

    void VulkanDebug::BeginDebugLabel(VkCommandBuffer cmd, std::string_view label) const noexcept
    {
        if (!Core::Diagnostics::IsDebugMode() || m_cmdBeginLabel == nullptr || cmd == VK_NULL_HANDLE || label.empty())
        {
            return;
        }
        const std::string nullTerminated(label);
        VkDebugUtilsLabelEXT labelInfo{};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pLabelName = nullTerminated.c_str();
        labelInfo.color[0] = 0.25f;
        labelInfo.color[1] = 0.65f;
        labelInfo.color[2] = 0.95f;
        labelInfo.color[3] = 1.0f;
        m_cmdBeginLabel(cmd, &labelInfo);
    }

    void VulkanDebug::EndDebugLabel(VkCommandBuffer cmd) const noexcept
    {
        if (!Core::Diagnostics::IsDebugMode() || m_cmdEndLabel == nullptr || cmd == VK_NULL_HANDLE)
        {
            return;
        }
        m_cmdEndLabel(cmd);
    }

    bool VulkanDebug::CheckVkResult(VkResult result, const char* operationContext) noexcept
    {
        if (result >= VK_SUCCESS)
        {
            return true;
        }
        const char* ctx = operationContext ? operationContext : "Vulkan Operation";
        if (result == VK_ERROR_DEVICE_LOST)
        {
            Core::Log::Fatal("RHI.Vulkan.Debug", std::string(ctx) + " failed: VK_ERROR_DEVICE_LOST");
            Core::Log::Flush();
        }
        else
        {
            Core::Log::Error("RHI.Vulkan.Debug", std::string(ctx) + " failed: " + VkResultToString(result));
        }
        return false;
    }

    const char* VulkanDebug::VkResultToString(VkResult result) noexcept
    {
        switch (result)
        {
        case VK_SUCCESS:                        return "VK_SUCCESS";
        case VK_NOT_READY:                      return "VK_NOT_READY";
        case VK_TIMEOUT:                        return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
        default:                                return "VK_ERROR_UNKNOWN";
        }
    }
}
