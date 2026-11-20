function(morbi_configure_shader_test_target TARGET_NAME)
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            Morbi::Core
            Morbi::IO
            Morbi::Shader
    )

    if(WIN32 AND MORBI_DXC_DIR AND EXISTS "${MORBI_DXC_DIR}/dxcompiler.dll")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Tools/dxc"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MORBI_DXC_DIR}/dxc.exe"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/Tools/dxc/dxc.exe"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MORBI_DXC_DIR}/dxcompiler.dll"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/Tools/dxc/dxcompiler.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MORBI_DXC_DIR}/dxil.dll"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/Tools/dxc/dxil.dll"
            COMMENT "Copying DXC tools for ${TARGET_NAME}"
        )
        if(EXISTS "${MORBI_DXC_DIR}/spirv-val.exe")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MORBI_DXC_DIR}/spirv-val.exe"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/Tools/dxc/spirv-val.exe"
                COMMENT "Copying spirv-val for ${TARGET_NAME}"
            )
        endif()
    endif()

    morbi_apply_target_options(${TARGET_NAME})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

add_executable(MorbiShaderTests
    ShaderCompilerTests.cpp
)
morbi_configure_shader_test_target(MorbiShaderTests)

add_executable(MorbiShaderReflectionTests
    ShaderReflectionTests.cpp
)
morbi_configure_shader_test_target(MorbiShaderReflectionTests)

add_executable(MorbiPipelineLayoutTests
    PipelineLayoutTests.cpp
)
morbi_configure_shader_test_target(MorbiPipelineLayoutTests)
target_link_libraries(MorbiPipelineLayoutTests
    PRIVATE
        Morbi::RHI
        Morbi::RHI.Vulkan
        Morbi::RHI.DX12
        Morbi::Render
        Morbi::Platform
)
