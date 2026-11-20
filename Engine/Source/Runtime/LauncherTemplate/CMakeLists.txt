cmake_minimum_required(VERSION 3.20)
project(MorbiGameLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MorbiGame
    src/main.cpp
)

if(MSVC)
    target_compile_options(MorbiGame PRIVATE /Zi)
    target_link_options(MorbiGame PRIVATE
        /DEBUG
        $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
        $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
    )
endif()

if(WIN32)
    target_link_libraries(MorbiGame PRIVATE ole32 DbgHelp dxguid)
endif()
