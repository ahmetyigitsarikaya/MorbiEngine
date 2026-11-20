include(FetchContent)

# 1. nlohmann_json (Reuse local header if available to avoid network download)
set(MORBI_LOCAL_JSON_DIR "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/Legacy/FlueEngine/External")
if(EXISTS "${MORBI_LOCAL_JSON_DIR}/nlohmann/json.hpp")
    add_library(morbi_nlohmann_json INTERFACE)
    target_include_directories(morbi_nlohmann_json INTERFACE "${MORBI_LOCAL_JSON_DIR}")
    add_library(nlohmann_json::nlohmann_json ALIAS morbi_nlohmann_json)
    message(STATUS "[MorbiEngine] Using local nlohmann/json headers")
else()
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# 2. SDL3 (Use prebuilt local binaries + headers if present, fallback to local source or FetchContent)
set(MORBI_LOCAL_SDL3_INC "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/build-vs/_deps/sdl3-src/include")
set(MORBI_LOCAL_SDL3_LIB_REL "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/lib/Release/SDL3.lib")
set(MORBI_LOCAL_SDL3_LIB_DBG "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/lib/Debug/SDL3d.lib")
set(MORBI_LOCAL_SDL3_DLL_REL "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/bin/Release/SDL3.dll")
set(MORBI_LOCAL_SDL3_DLL_DBG "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/bin/Debug/SDL3d.dll")

if(EXISTS "${MORBI_LOCAL_SDL3_INC}/SDL3/SDL.h" AND EXISTS "${MORBI_LOCAL_SDL3_LIB_REL}" AND EXISTS "${MORBI_LOCAL_SDL3_DLL_REL}")
    add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
    set_target_properties(SDL3::SDL3 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MORBI_LOCAL_SDL3_INC}"
        IMPORTED_IMPLIB "${MORBI_LOCAL_SDL3_LIB_REL}"
        IMPORTED_LOCATION "${MORBI_LOCAL_SDL3_DLL_REL}"
        IMPORTED_LOCATION_RELEASE "${MORBI_LOCAL_SDL3_DLL_REL}"
    )
    if(EXISTS "${MORBI_LOCAL_SDL3_LIB_DBG}")
        set_target_properties(SDL3::SDL3 PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${MORBI_LOCAL_SDL3_LIB_DBG}"
            IMPORTED_IMPLIB_RELEASE "${MORBI_LOCAL_SDL3_LIB_REL}"
        )
    endif()
    if(EXISTS "${MORBI_LOCAL_SDL3_DLL_DBG}")
        set_target_properties(SDL3::SDL3 PROPERTIES
            IMPORTED_LOCATION_DEBUG "${MORBI_LOCAL_SDL3_DLL_DBG}"
        )
    endif()
    set(MORBI_SDL3_DLL_PATH "${MORBI_LOCAL_SDL3_DLL_REL}" CACHE FILEPATH "Path to SDL3.dll" FORCE)
    message(STATUS "[MorbiEngine] Using prebuilt local SDL3 library and headers")
else()
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        set(SDL_SHARED ON CACHE BOOL "Build SDL shared library" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "Build SDL static library" FORCE)
        set(SDL_TEST OFF CACHE BOOL "Build SDL tests" FORCE)
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.0.zip
        )
        FetchContent_MakeAvailable(SDL3)
    endif()
endif()

# 3. Vulkan Headers (Local headers for zero-dependency compilation + runtime vulkan-1.dll loading)
set(MORBI_LOCAL_VULKAN_INC "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/Legacy/FlueEngine/External/vulkan-headers/include")
if(EXISTS "${MORBI_LOCAL_VULKAN_INC}/vulkan/vulkan.h")
    add_library(morbi_vulkan_headers INTERFACE)
    target_include_directories(morbi_vulkan_headers INTERFACE "${MORBI_LOCAL_VULKAN_INC}")
    target_compile_definitions(morbi_vulkan_headers INTERFACE
        VK_NO_PROTOTYPES
        $<$<BOOL:${WIN32}>:VK_USE_PLATFORM_WIN32_KHR>
    )
    add_library(Vulkan::Headers ALIAS morbi_vulkan_headers)
    message(STATUS "[MorbiEngine] Using local Vulkan 1.3 headers (dynamic dispatch via vulkan-1.dll)")
else()
    find_package(Vulkan REQUIRED)
    if(TARGET Vulkan::Headers)
        target_compile_definitions(Vulkan::Headers INTERFACE
            VK_NO_PROTOTYPES
            $<$<BOOL:${WIN32}>:VK_USE_PLATFORM_WIN32_KHR>
        )
    endif()
endif()

# 4. DirectX Shader Compiler (DXC) Tools Directory
set(MORBI_DXC_DIR "" CACHE PATH "Directory containing dxc.exe, dxcompiler.dll, dxil.dll")
if(NOT MORBI_DXC_DIR OR NOT EXISTS "${MORBI_DXC_DIR}/dxc.exe")
    set(MORBI_LOCAL_DXC_DIR "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/Legacy/FlueEngine/tools/ShaderCompiler/win64")
    if(EXISTS "${MORBI_LOCAL_DXC_DIR}/dxc.exe")
        set(MORBI_DXC_DIR "${MORBI_LOCAL_DXC_DIR}" CACHE PATH "Directory containing dxc.exe, dxcompiler.dll, dxil.dll" FORCE)
        message(STATUS "[MorbiEngine] Found local DXC toolchain at ${MORBI_DXC_DIR}")
    else()
        find_program(MORBI_DXC_EXE
            NAMES dxc dxc.exe
            HINTS
                "$ENV{VULKAN_SDK}/Bin"
                "$ENV{VULKAN_SDK}/bin"
        )
        if(MORBI_DXC_EXE)
            get_filename_component(MORBI_DETECTED_DXC_DIR "${MORBI_DXC_EXE}" DIRECTORY)
            set(MORBI_DXC_DIR "${MORBI_DETECTED_DXC_DIR}" CACHE PATH "Directory containing dxc.exe, dxcompiler.dll, dxil.dll" FORCE)
            message(STATUS "[MorbiEngine] Found system/SDK DXC toolchain at ${MORBI_DXC_DIR}")
        else()
            message(WARNING "[MorbiEngine] DXC not found. Pass -DMORBI_DXC_DIR=<path> or install the Vulkan SDK for runtime shader compilation.")
        endif()
    endif()
endif()

# 5. .NET CoreCLR Host (nethost / hostfxr) + dotnet CLI discovery
find_program(MORBI_DOTNET_EXECUTABLE
    NAMES dotnet dotnet.exe
    HINTS "$ENV{DOTNET_ROOT}" "$ENV{ProgramFiles}/dotnet"
)
if(MORBI_DOTNET_EXECUTABLE)
    message(STATUS "[MorbiEngine] Found .NET SDK CLI at ${MORBI_DOTNET_EXECUTABLE}")
endif()

if(WIN32)
    set(MORBI_DOTNET_SEARCH_ROOTS "")
    if(MORBI_DOTNET_EXECUTABLE)
        get_filename_component(MORBI_DOTNET_BIN_DIR "${MORBI_DOTNET_EXECUTABLE}" DIRECTORY)
        list(APPEND MORBI_DOTNET_SEARCH_ROOTS "${MORBI_DOTNET_BIN_DIR}")
    endif()
    if(DEFINED ENV{DOTNET_ROOT})
        list(APPEND MORBI_DOTNET_SEARCH_ROOTS "$ENV{DOTNET_ROOT}")
    endif()
    if(DEFINED ENV{ProgramFiles})
        list(APPEND MORBI_DOTNET_SEARCH_ROOTS "$ENV{ProgramFiles}/dotnet")
    endif()
    list(REMOVE_DUPLICATES MORBI_DOTNET_SEARCH_ROOTS)

    set(MORBI_DOTNET_HOST_PACKS "")
    foreach(MORBI_DOTNET_ROOT_CANDIDATE IN LISTS MORBI_DOTNET_SEARCH_ROOTS)
        file(GLOB MORBI_CANDIDATE_PACKS LIST_DIRECTORIES true "${MORBI_DOTNET_ROOT_CANDIDATE}/packs/Microsoft.NETCore.App.Host.win-x64/*")
        if(MORBI_CANDIDATE_PACKS)
            list(APPEND MORBI_DOTNET_HOST_PACKS ${MORBI_CANDIDATE_PACKS})
        endif()
    endforeach()

    if(MORBI_DOTNET_HOST_PACKS)
        list(SORT MORBI_DOTNET_HOST_PACKS COMPARE NATURAL ORDER DESCENDING)
        list(GET MORBI_DOTNET_HOST_PACKS 0 MORBI_DOTNET_HOST_PACK_DIR)
        set(MORBI_NETHOST_DIR "${MORBI_DOTNET_HOST_PACK_DIR}/runtimes/win-x64/native" CACHE PATH "nethost native directory" FORCE)
        message(STATUS "[MorbiEngine] Found .NET CoreCLR nethost at ${MORBI_NETHOST_DIR}")
    endif()
endif()

# 6. SoLoud 3D Audio Engine (Local source with Miniaudio backend)
set(MORBI_LOCAL_SOLOUD_DIR "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/Legacy/FlueEngine/External/soloud")
if(EXISTS "${MORBI_LOCAL_SOLOUD_DIR}/include/soloud.h")
    add_library(MorbiSoLoud STATIC
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_audiosource.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_bus.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_3d.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_basicops.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_faderops.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_filterops.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_getters.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_setters.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_voicegroup.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_core_voiceops.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_fader.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_fft.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_fft_lut.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_file.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_filter.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_misc.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_queue.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/core/soloud_thread.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/audiosource/wav/dr_impl.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/audiosource/wav/soloud_wav.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/audiosource/wav/stb_vorbis.c"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/backend/miniaudio/soloud_miniaudio.cpp"
        "${MORBI_LOCAL_SOLOUD_DIR}/src/backend/null/soloud_null.cpp"
    )
    target_include_directories(MorbiSoLoud
        SYSTEM PUBLIC
            "${MORBI_LOCAL_SOLOUD_DIR}/include"
        PRIVATE
            "${MORBI_LOCAL_SOLOUD_DIR}/src/audiosource/wav"
            "${MORBI_LOCAL_SOLOUD_DIR}/src/backend/miniaudio"
    )
    target_compile_definitions(MorbiSoLoud PRIVATE WITH_MINIAUDIO WITH_NULL SOLOUD_NO_ASSERTS _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(MorbiSoLoud PRIVATE /W0 /MP)
    endif()
    if(WIN32)
        target_link_libraries(MorbiSoLoud PUBLIC ole32 uuid winmm)
    endif()
    add_library(SoLoud::SoLoud ALIAS MorbiSoLoud)
    set(MORBI_HAS_SOLOUD ON CACHE BOOL "SoLoud audio library available" FORCE)
    message(STATUS "[MorbiEngine] Configured local SoLoud 3D Audio library")
endif()

# 7. Jolt Physics v5.2.0 (Pure C++ Multi-Core Physics Engine - Local ThirdParty or Pinned FetchContent)
option(MORBI_FETCH_JOLT_FROM_GITHUB "Fetch JoltPhysics v5.2.0 from GitHub if not vendored locally" ON)

set(MORBI_LOCAL_JOLT_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/JoltPhysics")
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(USE_AVX2 ${MORBI_ENABLE_AVX2} CACHE BOOL "" FORCE)

if(EXISTS "${MORBI_LOCAL_JOLT_DIR}/Build/CMakeLists.txt")
    add_subdirectory("${MORBI_LOCAL_JOLT_DIR}/Build" "${CMAKE_BINARY_DIR}/_deps/jolt-build" EXCLUDE_FROM_ALL)
    message(STATUS "[MorbiEngine] Using local vendored JoltPhysics v5.2.0 at ${MORBI_LOCAL_JOLT_DIR}")
elseif(MORBI_FETCH_JOLT_FROM_GITHUB)
    FetchContent_Declare(JoltPhysics
        URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.2.0.zip
        URL_HASH SHA256=6953A6293F1CCFE3790CB0D778C71E3736031C441E0F434FD2BAA8D307EBFEF3
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(JoltPhysics)
    message(STATUS "[MorbiEngine] Fetched pinned JoltPhysics v5.2.0 via FetchContent")
endif()

if(NOT TARGET Jolt)
    message(FATAL_ERROR "[MorbiEngine] JoltPhysics v5.2.0 is required! Ensure ThirdParty/JoltPhysics exists or enable MORBI_FETCH_JOLT_FROM_GITHUB=ON.")
endif()

# Treat Jolt headers as SYSTEM for consumers and silence third-party warnings inside Jolt itself
get_target_property(MORBI_JOLT_INC Jolt INTERFACE_INCLUDE_DIRECTORIES)
if(MORBI_JOLT_INC)
    set_target_properties(Jolt PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${MORBI_JOLT_INC}")
endif()
if(MSVC)
    target_compile_options(Jolt PRIVATE /W0)
else()
    target_compile_options(Jolt PRIVATE -w)
endif()

# 8. Assimp 3D Model Importer (Prebuilt local static library + matching v6.0.5 headers for FBX/glTF/OBJ)
set(MORBI_LOCAL_ASSIMP_INC "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/build-refine/_deps/assimp-src/include")
if(NOT EXISTS "${MORBI_LOCAL_ASSIMP_INC}/assimp/Importer.hpp")
    set(MORBI_LOCAL_ASSIMP_INC "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/Legacy/FlueEngine/External/assimp/include")
endif()
set(MORBI_LOCAL_ASSIMP_CFG_INC "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/build-refine/_deps/assimp-build/include")
set(MORBI_LOCAL_ASSIMP_LIB_REL "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/lib/Release/assimp-vc145-mt.lib")
set(MORBI_LOCAL_ASSIMP_LIB_DBG "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/lib/Debug/assimp-vc145-mtd.lib")
set(MORBI_LOCAL_ZLIB_LIB_REL "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/lib/Release/zlibstatic.lib")
set(MORBI_LOCAL_ZLIB_LIB_DBG "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/lib/Debug/zlibstaticd.lib")

if(EXISTS "${MORBI_LOCAL_ASSIMP_INC}/assimp/Importer.hpp" AND EXISTS "${MORBI_LOCAL_ASSIMP_CFG_INC}/assimp/config.h" AND EXISTS "${MORBI_LOCAL_ASSIMP_LIB_REL}")
    add_library(Morbi::Assimp STATIC IMPORTED GLOBAL)
    set_target_properties(Morbi::Assimp PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MORBI_LOCAL_ASSIMP_INC};${MORBI_LOCAL_ASSIMP_CFG_INC}"
        IMPORTED_LOCATION "${MORBI_LOCAL_ASSIMP_LIB_REL}"
        IMPORTED_LOCATION_RELEASE "${MORBI_LOCAL_ASSIMP_LIB_REL}"
    )
    if(EXISTS "${MORBI_LOCAL_ASSIMP_LIB_DBG}")
        set_target_properties(Morbi::Assimp PROPERTIES
            IMPORTED_LOCATION_DEBUG "${MORBI_LOCAL_ASSIMP_LIB_DBG}"
        )
    endif()
    if(EXISTS "${MORBI_LOCAL_ZLIB_LIB_REL}")
        set_target_properties(Morbi::Assimp PROPERTIES
            INTERFACE_LINK_LIBRARIES "$<$<CONFIG:Debug>:${MORBI_LOCAL_ZLIB_LIB_DBG}>$<$<NOT:$<CONFIG:Debug>>:${MORBI_LOCAL_ZLIB_LIB_REL}>"
        )
    endif()
    set(MORBI_HAS_ASSIMP ON CACHE BOOL "Assimp 3D model importer available" FORCE)
    message(STATUS "[MorbiEngine] Using prebuilt local Assimp library for FBX/glTF/OBJ import")
endif()

# 9. ENet v1.3.18 (MIT Reliable UDP Transport Backend for Morbi.Network)
set(MORBI_PREV_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(enet
    URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.zip
    URL_HASH SHA256=32D6B15611C2667B88CD865E0CB48C3C011DDEFCFF6DFBDB266B8E0B04FFFAB3
)
FetchContent_MakeAvailable(enet)

if(DEFINED MORBI_PREV_BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ${MORBI_PREV_BUILD_SHARED_LIBS} CACHE BOOL "" FORCE)
endif()

if(TARGET enet)
    if(MSVC)
        target_compile_options(enet PRIVATE /w /wd5287 /wd4996)
        target_compile_definitions(enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
    else()
        target_compile_options(enet PRIVATE -w)
    endif()
    if(WIN32)
        target_link_libraries(enet PUBLIC ws2_32 winmm)
    endif()
    if(DEFINED enet_SOURCE_DIR)
        target_include_directories(enet SYSTEM PUBLIC "${enet_SOURCE_DIR}/include")
    endif()
    if(NOT TARGET ENet::ENet)
        add_library(ENet::ENet ALIAS enet)
    endif()
    message(STATUS "[MorbiEngine] Configured pinned ENet v1.3.18 transport backend")
endif()

# 10. libsodium 1.0.20 (ISC Authenticated Encryption, X25519, Ed25519, CSPRNG for Morbi.Network)
set(MORBI_PREV_BUILD_SHARED_LIBS_SODIUM ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "Disable libsodium tests" FORCE)

FetchContent_Declare(sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG        9b2848dfc1b917a9410f0de9d81059b26cbfaa8d
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sodium)

if(DEFINED MORBI_PREV_BUILD_SHARED_LIBS_SODIUM)
    set(BUILD_SHARED_LIBS ${MORBI_PREV_BUILD_SHARED_LIBS_SODIUM} CACHE BOOL "" FORCE)
endif()

if(TARGET sodium)
    if(MSVC)
        target_compile_options(sodium PRIVATE /w)
    else()
        target_compile_options(sodium PRIVATE -w)
    endif()
    get_target_property(MORBI_SODIUM_INC sodium INCLUDE_DIRECTORIES)
    if(MORBI_SODIUM_INC)
        set_target_properties(sodium PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${MORBI_SODIUM_INC}")
    endif()
    if(NOT TARGET Sodium::Sodium)
        add_library(Sodium::Sodium ALIAS sodium)
    endif()
    message(STATUS "[MorbiEngine] Configured pinned libsodium v1.0.20 (commit 9b2848df)")
endif()

# 11. Khronos SPIRV-Reflect (Vendored Apache-2.0, tag vulkan-sdk-1.4.363.0 / commit f3e261fd)
set(MORBI_LOCAL_SPIRV_REFLECT_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/SPIRV-Reflect")
if(EXISTS "${MORBI_LOCAL_SPIRV_REFLECT_DIR}/spirv_reflect.c" AND EXISTS "${MORBI_LOCAL_SPIRV_REFLECT_DIR}/spirv_reflect.h")
    add_library(MorbiSPIRVReflect STATIC
        "${MORBI_LOCAL_SPIRV_REFLECT_DIR}/spirv_reflect.c"
    )
    set_target_properties(MorbiSPIRVReflect PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(MorbiSPIRVReflect
        SYSTEM PUBLIC
            "${MORBI_LOCAL_SPIRV_REFLECT_DIR}"
            "${MORBI_LOCAL_SPIRV_REFLECT_DIR}/include"
    )
    if(MSVC)
        target_compile_options(MorbiSPIRVReflect PRIVATE /W0)
    else()
        target_compile_options(MorbiSPIRVReflect PRIVATE -w)
    endif()
    add_library(SPIRVReflect::SPIRVReflect ALIAS MorbiSPIRVReflect)
    message(STATUS "[MorbiEngine] Configured vendored Khronos SPIRV-Reflect (vulkan-sdk-1.4.363.0)")
else()
    message(FATAL_ERROR "[MorbiEngine] Vendored SPIRV-Reflect is required at ThirdParty/SPIRV-Reflect!")
endif()
