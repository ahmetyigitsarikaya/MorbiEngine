add_library(Morbi.RHI.Vulkan SHARED
    src/VulkanDebug.cpp
    src/VulkanDevice.cpp
)

add_library(Morbi::RHI.Vulkan ALIAS Morbi.RHI.Vulkan)
target_compile_definitions(Morbi.RHI.Vulkan PRIVATE MORBI_RHI_VULKAN_EXPORTS=1)

target_include_directories(Morbi.RHI.Vulkan
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.RHI.Vulkan
    PUBLIC
        Morbi::Core
        Morbi::Platform
        Morbi::RHI
    PRIVATE
        Vulkan::Headers
)

morbi_apply_target_options(Morbi.RHI.Vulkan)
