#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "Morbi/Core/Export.hpp"
#include "Morbi/RHI/Vulkan/VulkanDispatch.hpp"

namespace Morbi::RHI::Vulkan
{
    struct VulkanDebugInstanceConfig
    {
        bool DebugActive = false;
        bool ValidationLayerAvailable = false;
        bool DebugUtilsAvailable = false;
        bool ValidationFeaturesAvailable = false;
        std::vector<const char*> EnabledLayers;
        std::vector<const char*> EnabledExtensions;
        VkDebugUtilsMessengerCreateInfoEXT MessengerCreateInfo{};
        VkValidationFeaturesEXT ValidationFeatures{};
        std::vector<VkValidationFeatureEnableEXT> EnabledValidationFeatures;
    };

    class MORBI_RHI_VULKAN_API VulkanDebug
    {
    public:
        // Queries available layers/extensions and populates instance extensions, validation layer,
        // optional GPU-assisted validation features, and early messenger create info.
        void PrepareInstanceConfig(
            const VulkanGlobalDispatch& globalDisp,
            bool enableValidation,
            bool enableGpuGbv,
            const char* const* baseExtensions,
            std::size_t baseExtensionCount,
            VulkanDebugInstanceConfig& outConfig) noexcept;

        // Returns a pNext chain pointer to attach to VkInstanceCreateInfo (or nullptr if inactive).
        [[nodiscard]] static void* GetInstanceCreatePNext(VulkanDebugInstanceConfig& config) noexcept;

        // Loads VK_EXT_debug_utils function pointers and creates VkDebugUtilsMessengerEXT.
        void InitializePostInstance(
            VkInstance instance,
            PFN_vkGetInstanceProcAddr getInstanceProcAddr,
            const VulkanDebugInstanceConfig& config) noexcept;

        void Shutdown(VkInstance instance) noexcept;

        void SetObjectName(
            VkDevice device,
            VkObjectType objectType,
            std::uint64_t objectHandle,
            std::string_view name) const noexcept;

        void BeginDebugLabel(VkCommandBuffer cmd, std::string_view label) const noexcept;
        void EndDebugLabel(VkCommandBuffer cmd) const noexcept;

        static bool CheckVkResult(VkResult result, const char* operationContext) noexcept;
        [[nodiscard]] static const char* VkResultToString(VkResult result) noexcept;
        [[nodiscard]] bool IsDebugUtilsReady() const noexcept { return m_debugUtilsActive; }

    private:
        bool m_debugUtilsActive = false;
        VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
        PFN_vkCreateDebugUtilsMessengerEXT m_createMessenger = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT m_destroyMessenger = nullptr;
        PFN_vkSetDebugUtilsObjectNameEXT m_setObjectName = nullptr;
        PFN_vkCmdBeginDebugUtilsLabelEXT m_cmdBeginLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT m_cmdEndLabel = nullptr;
    };
}
