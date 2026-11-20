add_library(Morbi.Render SHARED
    src/RenderGraph.cpp
    src/RHIFactory.cpp
)

add_library(Morbi::Render ALIAS Morbi.Render)
target_compile_definitions(Morbi.Render PRIVATE MORBI_RENDER_EXPORTS=1)

target_include_directories(Morbi.Render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Render
    PUBLIC
        Morbi::Core
        Morbi::RHI
        Morbi::RHI.Vulkan
        Morbi::RHI.DX12
)

morbi_apply_target_options(Morbi.Render)
