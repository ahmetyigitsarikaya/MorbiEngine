add_library(Morbi.RHI SHARED
    src/RHIDevice.cpp
    src/PipelineLayout.cpp
)

add_library(Morbi::RHI ALIAS Morbi.RHI)
target_compile_definitions(Morbi.RHI PRIVATE MORBI_RHI_EXPORTS=1)

target_include_directories(Morbi.RHI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Morbi.RHI
    PUBLIC
        Morbi::Core
        Morbi::Platform
)

morbi_apply_target_options(Morbi.RHI)
