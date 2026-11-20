add_library(Morbi.Script SHARED
    src/ScriptEngine.cpp
)

add_library(Morbi::Script ALIAS Morbi.Script)
target_compile_definitions(Morbi.Script PRIVATE MORBI_SCRIPT_EXPORTS=1)

target_include_directories(Morbi.Script
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Script
    PUBLIC
        Morbi::Core
        Morbi::Asset
        Morbi::Scene
        Morbi::Physics
        Morbi::Audio
    PRIVATE
        Morbi::Platform
        Morbi::UI
)

if(WIN32 AND MORBI_NETHOST_DIR AND EXISTS "${MORBI_NETHOST_DIR}/nethost.h" AND EXISTS "${MORBI_NETHOST_DIR}/nethost.lib")
    target_include_directories(Morbi.Script PRIVATE "${MORBI_NETHOST_DIR}")
    target_link_libraries(Morbi.Script PRIVATE "${MORBI_NETHOST_DIR}/nethost.lib")
    target_compile_definitions(Morbi.Script PUBLIC MORBI_HAS_DOTNET_HOST=1)
else()
    target_compile_definitions(Morbi.Script PUBLIC MORBI_HAS_DOTNET_HOST=0)
endif()

morbi_apply_target_options(Morbi.Script)
