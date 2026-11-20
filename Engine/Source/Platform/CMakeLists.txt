add_library(Morbi.Platform SHARED
    src/InputState.cpp
    src/Window.cpp
)

add_library(Morbi::Platform ALIAS Morbi.Platform)
target_compile_definitions(Morbi.Platform PRIVATE MORBI_PLATFORM_EXPORTS=1)

target_include_directories(Morbi.Platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Morbi.Platform
    PUBLIC
        Morbi::Core
    PRIVATE
        SDL3::SDL3
)

morbi_apply_target_options(Morbi.Platform)

if(WIN32 AND TARGET SDL3::SDL3)
    add_custom_command(TARGET Morbi.Platform POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:Morbi.Platform>/$<TARGET_FILE_NAME:SDL3::SDL3>"
        COMMENT "Copying SDL3 runtime DLL alongside Morbi.Platform.dll"
    )
endif()
