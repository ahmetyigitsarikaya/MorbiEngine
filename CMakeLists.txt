cmake_minimum_required(VERSION 3.24)

project(MorbiEngine
    VERSION 1.0.0
    DESCRIPTION "MorbiEngine - High-Performance Multi-API 3D Game Engine"
    LANGUAGES CXX C
)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(MorbiCompilerOptions)
include(MorbiDependencies)

add_subdirectory(Engine/Source/Core)
add_subdirectory(Engine/Source/IO)
add_subdirectory(Engine/Source/Platform)
add_subdirectory(Engine/Source/RHI)
add_subdirectory(Engine/Source/Shader)
add_subdirectory(Engine/Source/RHI.Vulkan)
if(WIN32)
    add_subdirectory(Engine/Source/RHI.DX12)
endif()
add_subdirectory(Engine/Source/Asset)
add_subdirectory(Engine/Source/Render)
add_subdirectory(Engine/Source/Scene)
add_subdirectory(Engine/Source/Physics)
add_subdirectory(Engine/Source/Audio)
add_subdirectory(Engine/Source/Network)
add_subdirectory(Engine/Source/Script)
add_subdirectory(Engine/Source/UI)
add_subdirectory(Engine/Source/Runtime)

if(MORBI_DOTNET_EXECUTABLE)
    add_custom_target(MorbiManagedBuild ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/dotnet-home"
            "${CMAKE_BINARY_DIR}/bin/Managed"
        COMMAND ${CMAKE_COMMAND} -E env
            "DOTNET_CLI_HOME=${CMAKE_BINARY_DIR}/dotnet-home"
            "DOTNET_CLI_TELEMETRY_OPTOUT=1"
            "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
            "${MORBI_DOTNET_EXECUTABLE}" build
            "${CMAKE_SOURCE_DIR}/Managed/Morbi.Engine/Morbi.Engine.csproj"
            --configuration Release
            --output "${CMAKE_BINARY_DIR}/bin/Managed"
            --nologo
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building Morbi.Engine (.NET C# Scripting & Zero-Copy Span Bridge)"
        VERBATIM
    )
endif()

add_subdirectory(Editor)

option(MORBI_BUILD_SAMPLES "Build MorbiEngine sample projects" OFF)
if(MORBI_BUILD_SAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Samples/Sandbox/CMakeLists.txt")
    add_subdirectory(Samples/Sandbox)
endif()

option(MORBI_BUILD_TESTS "Build MorbiEngine automated tests" ON)
if(MORBI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests/PhysicsTests)
    add_subdirectory(Tests/NetworkTests)
    add_subdirectory(Tests/RHITests)
    add_subdirectory(Tests/ShaderTests)
endif()
