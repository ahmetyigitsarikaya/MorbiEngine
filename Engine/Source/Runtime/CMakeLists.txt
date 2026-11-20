# Shared headless gameplay & networking simulation host (no RHI backends, Render, UI, or Windowing)
add_library(Morbi.Simulation STATIC
    src/SimulationSession.cpp
    src/SceneReplicationBridge.cpp
)

add_library(Morbi::Simulation ALIAS Morbi.Simulation)

target_include_directories(Morbi.Simulation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Simulation
    PUBLIC
        Morbi::Core
        Morbi::IO
        Morbi::Scene
        Morbi::Physics
        Morbi::Audio
        Morbi::Network
        Morbi::Script
    PRIVATE
        nlohmann_json::nlohmann_json
)

morbi_apply_target_options(Morbi.Simulation)

add_library(Morbi.Runtime SHARED
    src/EngineRuntime.cpp
    src/ProjectBuilder.cpp
    src/SimulationSession.cpp
    src/SceneReplicationBridge.cpp
)

add_library(Morbi::Runtime ALIAS Morbi.Runtime)
target_compile_definitions(Morbi.Runtime PRIVATE MORBI_RUNTIME_EXPORTS=1)

target_include_directories(Morbi.Runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Runtime
    PUBLIC
        Morbi::Core
        Morbi::IO
        Morbi::Platform
        Morbi::Shader
        Morbi::RHI
        Morbi::RHI.Vulkan
        Morbi::RHI.DX12
        Morbi::Render
        Morbi::Scene
        Morbi::Physics
        Morbi::Audio
        Morbi::Network
        Morbi::Script
        Morbi::UI
        Morbi::Asset
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(Morbi.Runtime PRIVATE shell32 DbgHelp dxguid)
endif()

morbi_apply_target_options(Morbi.Runtime)

# Headless Dedicated Server Executable (reuses Morbi.Simulation without Editor, RHI backends, Render, or UI)
add_executable(MorbiServer
    Server/ServerMain.cpp
)

target_link_libraries(MorbiServer
    PRIVATE
        Morbi::Simulation
)

if(TARGET MorbiManagedBuild)
    add_dependencies(MorbiServer MorbiManagedBuild)
endif()

if(WIN32 AND MORBI_NETHOST_DIR AND EXISTS "${MORBI_NETHOST_DIR}/nethost.dll")
    add_custom_command(TARGET MorbiServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_NETHOST_DIR}/nethost.dll"
            "$<TARGET_FILE_DIR:MorbiServer>/nethost.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/bin/Managed"
            "$<TARGET_FILE_DIR:MorbiServer>/Managed"
        COMMENT "Staging nethost.dll and Managed assemblies for MorbiServer"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/Tools/dotnet")
    add_custom_command(TARGET MorbiServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/ThirdParty/Tools/dotnet"
            "$<TARGET_FILE_DIR:MorbiServer>/Tools/dotnet"
        COMMENT "Staging portable .NET CoreCLR runtime for MorbiServer"
    )
endif()

morbi_apply_target_options(MorbiServer)
