# ============================================================================
# MorbiEditor - Editor Thin Host Executable (Zero Static Engine Linking)
# ============================================================================
set(MORBI_FRONTEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Frontend")
set(MORBI_FRONTEND_STAMP "${CMAKE_CURRENT_BINARY_DIR}/frontend_built.stamp")
file(GLOB_RECURSE MORBI_FRONTEND_INPUTS CONFIGURE_DEPENDS
    "${MORBI_FRONTEND_DIR}/src/*"
    "${MORBI_FRONTEND_DIR}/public/*"
    "${MORBI_FRONTEND_DIR}/index.html"
    "${MORBI_FRONTEND_DIR}/package.json"
    "${MORBI_FRONTEND_DIR}/vite.config.ts"
)

option(MORBI_BUILD_FRONTEND "Build React editor frontend via npm during CMake build" ON)
option(MORBI_DEPLOY_TO_SOURCE_TREE "Copy MorbiEditor.exe and MorbiEditor.pdb into source tree" OFF)

if(WIN32 AND MORBI_BUILD_FRONTEND)
    find_program(MORBI_NPM_EXECUTABLE NAMES npm.cmd npm)
    if(MORBI_NPM_EXECUTABLE AND EXISTS "${MORBI_FRONTEND_DIR}/node_modules")
        add_custom_command(
            OUTPUT "${MORBI_FRONTEND_STAMP}"
            COMMAND "${MORBI_NPM_EXECUTABLE}" run build
            COMMAND ${CMAKE_COMMAND} -E touch "${MORBI_FRONTEND_STAMP}"
            WORKING_DIRECTORY "${MORBI_FRONTEND_DIR}"
            DEPENDS ${MORBI_FRONTEND_INPUTS}
            COMMENT "Building Editor Frontend (React + Vite singlefile) for EXE embedding"
            VERBATIM
        )
        add_custom_target(MorbiFrontendBuild DEPENDS "${MORBI_FRONTEND_STAMP}")
    elseif(NOT EXISTS "${MORBI_FRONTEND_DIR}/dist/index.html")
        message(WARNING "[MorbiEngine] Editor frontend dist/index.html not found and npm/node_modules unavailable. Run 'npm install && npm run build' inside Editor/Frontend.")
    endif()
endif()

add_executable(MorbiEditor
    src/main.cpp
    src/EmbeddedFrontend.rc
)

# Ensure all Shared Engine DLLs, Managed C# assemblies, and Frontend bundle are built before MorbiEditor,
# WITHOUT linking any Morbi.*.lib into MorbiEditor.exe!
add_dependencies(MorbiEditor
    Morbi.Runtime
)

if(TARGET MorbiFrontendBuild)
    add_dependencies(MorbiEditor MorbiFrontendBuild)
endif()

if(TARGET MorbiManagedBuild)
    add_dependencies(MorbiEditor MorbiManagedBuild)
endif()

morbi_apply_target_options(MorbiEditor)

if(WIN32)
    target_link_libraries(MorbiEditor PRIVATE DbgHelp dxguid)
endif()

if(WIN32 AND MORBI_SDL3_DLL_PATH AND EXISTS "${MORBI_SDL3_DLL_PATH}")
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_SDL3_DLL_PATH}"
            "$<TARGET_FILE_DIR:MorbiEditor>/SDL3.dll"
        COMMENT "Copying SDL3.dll to central Engine Binaries directory for Editor"
    )
endif()

if(WIN32 AND MORBI_NETHOST_DIR AND EXISTS "${MORBI_NETHOST_DIR}/nethost.dll")
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_NETHOST_DIR}/nethost.dll"
            "$<TARGET_FILE_DIR:MorbiEditor>/nethost.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/bin/Managed"
            "$<TARGET_FILE_DIR:MorbiEditor>/Managed"
        COMMENT "Copying nethost.dll and Managed assemblies to central Engine Binaries directory for Editor"
    )
endif()

set(MORBI_WEBVIEW2_LOADER_PATH "" CACHE FILEPATH "Optional path to WebView2Loader.dll")
if(MORBI_WEBVIEW2_LOADER_PATH AND EXISTS "${MORBI_WEBVIEW2_LOADER_PATH}")
    set(MORBI_WEBVIEW2_LOADER_SRC "${MORBI_WEBVIEW2_LOADER_PATH}")
else()
    set(MORBI_WEBVIEW2_LOADER_SRC "${CMAKE_SOURCE_DIR}/legacy/FlueperEngine/Legacy/FlueEngine/External/webview2/runtimes/win-x64/native/WebView2Loader.dll")
endif()
if(WIN32 AND EXISTS "${MORBI_WEBVIEW2_LOADER_SRC}")
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_WEBVIEW2_LOADER_SRC}"
            "$<TARGET_FILE_DIR:MorbiEditor>/WebView2Loader.dll"
        COMMENT "Copying WebView2Loader.dll to central Engine Binaries directory for Editor"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/LauncherTemplate")
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/LauncherTemplate"
            "$<TARGET_FILE_DIR:MorbiEditor>/Tools/Launcher"
        COMMENT "Packaging open C++ Launcher template into Tools/Launcher"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/Tools")
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/cmake/SyncDir.ps1" "${CMAKE_SOURCE_DIR}/ThirdParty/Tools" "$<TARGET_FILE_DIR:MorbiEditor>/Tools"
        COMMENT "Packaging portable CMake, Zig C++ compiler, and .NET runtime into Tools/"
    )
endif()

if(WIN32 AND MORBI_DXC_DIR AND EXISTS "${MORBI_DXC_DIR}/dxc.exe")
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MorbiEditor>/Tools/dxc"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_DXC_DIR}/dxc.exe"
            "$<TARGET_FILE_DIR:MorbiEditor>/Tools/dxc/dxc.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_DXC_DIR}/dxcompiler.dll"
            "$<TARGET_FILE_DIR:MorbiEditor>/Tools/dxc/dxcompiler.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_DXC_DIR}/dxil.dll"
            "$<TARGET_FILE_DIR:MorbiEditor>/Tools/dxc/dxil.dll"
        COMMENT "Packaging standalone DXC shader compiler tools into Editor build directory"
    )
    if(EXISTS "${MORBI_DXC_DIR}/spirv-val.exe")
        add_custom_command(TARGET MorbiEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MORBI_DXC_DIR}/spirv-val.exe"
                "$<TARGET_FILE_DIR:MorbiEditor>/Tools/dxc/spirv-val.exe"
            COMMENT "Packaging spirv-val tool into Editor build directory"
        )
    endif()
endif()

if(MORBI_DEPLOY_TO_SOURCE_TREE)
    add_custom_command(TARGET MorbiEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MorbiEditor>"
            "${CMAKE_SOURCE_DIR}/MorbiEditor.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:MorbiEditor>"
            "${CMAKE_SOURCE_DIR}/MorbiEditor.pdb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MorbiEditor>"
            "${CMAKE_CURRENT_SOURCE_DIR}/MorbiEditor.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:MorbiEditor>"
            "${CMAKE_CURRENT_SOURCE_DIR}/MorbiEditor.pdb"
        COMMENT "Deploying MorbiEditor.exe and MorbiEditor.pdb to root and Editor folder"
    )
endif()
