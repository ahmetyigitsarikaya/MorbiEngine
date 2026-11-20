add_library(Morbi.Audio SHARED
    src/AudioWorld.cpp
)

add_library(Morbi::Audio ALIAS Morbi.Audio)
target_compile_definitions(Morbi.Audio PRIVATE MORBI_AUDIO_EXPORTS=1)

target_include_directories(Morbi.Audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Audio
    PUBLIC
        Morbi::Core
        Morbi::Scene
)

if(TARGET SoLoud::SoLoud)
    target_link_libraries(Morbi.Audio PRIVATE SoLoud::SoLoud)
    target_compile_definitions(Morbi.Audio PUBLIC MORBI_ENABLE_SOLOUD=1)
else()
    target_compile_definitions(Morbi.Audio PUBLIC MORBI_ENABLE_SOLOUD=0)
endif()

morbi_apply_target_options(Morbi.Audio)
