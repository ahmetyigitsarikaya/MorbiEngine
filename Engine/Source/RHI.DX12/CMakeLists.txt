add_library(Morbi.RHI.DX12 SHARED
    src/DX12Debug.cpp
    src/DX12Device.cpp
)

add_library(Morbi::RHI.DX12 ALIAS Morbi.RHI.DX12)
target_compile_definitions(Morbi.RHI.DX12 PRIVATE MORBI_RHI_DX12_EXPORTS=1)

target_include_directories(Morbi.RHI.DX12
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.RHI.DX12
    PUBLIC
        Morbi::Core
        Morbi::Platform
        Morbi::RHI
    PRIVATE
        d3d12
        dxgi
        dxguid
)

morbi_apply_target_options(Morbi.RHI.DX12)
