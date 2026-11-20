# ============================================================================
# MorbiSandbox - Thin Project Host Executable (Zero Static Engine Linking)
# ============================================================================
add_executable(MorbiSandbox
    src/main.cpp
)

# Ensure all Shared Engine DLLs and Managed C# assemblies are built before MorbiSandbox,
# WITHOUT linking any Morbi.*.lib into MorbiSandbox.exe!
add_dependencies(MorbiSandbox
    Morbi.Runtime
)

if(TARGET MorbiManagedBuild)
    add_dependencies(MorbiSandbox MorbiManagedBuild)
endif()

morbi_apply_target_options(MorbiSandbox)

if(WIN32 AND MORBI_SDL3_DLL_PATH AND EXISTS "${MORBI_SDL3_DLL_PATH}")
    add_custom_command(TARGET MorbiSandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_SDL3_DLL_PATH}"
            "$<TARGET_FILE_DIR:MorbiSandbox>/SDL3.dll"
        COMMENT "Copying SDL3.dll to central Engine Binaries directory"
    )
endif()

if(WIN32 AND MORBI_NETHOST_DIR AND EXISTS "${MORBI_NETHOST_DIR}/nethost.dll")
    add_custom_command(TARGET MorbiSandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MORBI_NETHOST_DIR}/nethost.dll"
            "$<TARGET_FILE_DIR:MorbiSandbox>/nethost.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/bin/Managed"
            "$<TARGET_FILE_DIR:MorbiSandbox>/Managed"
        COMMENT "Copying nethost.dll and Managed assemblies to central Engine Binaries directory"
    )
endif()

# Deploy the lightweight Thin Host .exe into the Samples/Sandbox/ project folder
add_custom_command(TARGET MorbiSandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:MorbiSandbox>"
        "${CMAKE_CURRENT_SOURCE_DIR}/MorbiSandbox.exe"
    COMMENT "Deploying Thin Host executable (MorbiSandbox.exe) to Samples/Sandbox project folder"
)
