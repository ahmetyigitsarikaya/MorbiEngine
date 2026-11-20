add_library(Morbi.Core SHARED
    src/Log.cpp
    src/TaskScheduler.cpp
    src/Math.cpp
    src/EngineSettings.cpp
    src/Diagnostics/DebugConfig.cpp
    src/Diagnostics/BreadcrumbTracker.cpp
    src/Diagnostics/CrashHandler.cpp
)

add_library(Morbi::Core ALIAS Morbi.Core)
target_compile_definitions(Morbi.Core PRIVATE MORBI_CORE_EXPORTS=1)

target_include_directories(Morbi.Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Morbi.Core
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(Morbi.Core
        PRIVATE
            DbgHelp
            shell32
    )
endif()

morbi_apply_target_options(Morbi.Core)
