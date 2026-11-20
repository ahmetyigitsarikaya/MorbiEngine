add_library(Morbi.Shader SHARED
    src/ShaderCompiler.cpp
    src/ShaderCache.cpp
    src/SpirvReflector.cpp
    src/DxilReflector.cpp
    src/ShaderReflectionValidator.cpp
    src/PipelineLayoutBuilder.cpp
)

add_library(Morbi::Shader ALIAS Morbi.Shader)
target_compile_definitions(Morbi.Shader PRIVATE MORBI_SHADER_EXPORTS=1)

if(MORBI_DXC_DIR)
    target_compile_definitions(Morbi.Shader PRIVATE MORBI_DEFAULT_DXC_DIR="${MORBI_DXC_DIR}")
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(Morbi.Shader PRIVATE ${CMAKE_DL_LIBS})
    find_path(MORBI_SYSTEM_DXC_INCLUDE_DIR NAMES dxc/dxcapi.h)
    if(MORBI_SYSTEM_DXC_INCLUDE_DIR AND EXISTS "${MORBI_SYSTEM_DXC_INCLUDE_DIR}/dxc/WinAdapter.h")
        target_include_directories(Morbi.Shader PRIVATE "${MORBI_SYSTEM_DXC_INCLUDE_DIR}")
        target_compile_definitions(Morbi.Shader PRIVATE MORBI_HAS_SYSTEM_DXCAPI_H=1)
    endif()
endif()

target_include_directories(Morbi.Shader
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Morbi.Shader
    PUBLIC
        Morbi::Core
        Morbi::IO
        Morbi::RHI
    PRIVATE
        SPIRVReflect::SPIRVReflect
)

morbi_apply_target_options(Morbi.Shader)
