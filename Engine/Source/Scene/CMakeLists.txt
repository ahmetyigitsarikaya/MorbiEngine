add_library(Morbi.Scene SHARED
    src/Scene.cpp
)

add_library(Morbi::Scene ALIAS Morbi.Scene)
target_compile_definitions(Morbi.Scene PRIVATE MORBI_SCENE_EXPORTS=1)

target_include_directories(Morbi.Scene
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Scene
    PUBLIC
        Morbi::Core
        Morbi::IO
        Morbi::Asset
    PRIVATE
        nlohmann_json::nlohmann_json
)

morbi_apply_target_options(Morbi.Scene)
