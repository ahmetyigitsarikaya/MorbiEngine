#pragma once

// ============================================================================
// MorbiEngine Shared Library (DLL) Symbol Export / Import Macros
// ============================================================================
#if defined(_WIN32)
    #define MORBI_DLLEXPORT __declspec(dllexport)
    #define MORBI_DLLIMPORT __declspec(dllimport)
#else
    #define MORBI_DLLEXPORT __attribute__((visibility("default")))
    #define MORBI_DLLIMPORT __attribute__((visibility("default")))
#endif

// Generic MORBI_API macro controlled by MORBI_BUILDING_DLL in any shared module
#if defined(MORBI_STATIC_BUILD)
    #define MORBI_API
#elif defined(MORBI_BUILDING_DLL)
    #define MORBI_API MORBI_DLLEXPORT
#else
    #define MORBI_API MORBI_DLLIMPORT
#endif

// Per-Module Export/Import Macros for Fine-Grained Cross-DLL Dependency Linking
#if defined(MORBI_CORE_EXPORTS)
    #define MORBI_CORE_API MORBI_DLLEXPORT
#else
    #define MORBI_CORE_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_IO_EXPORTS)
    #define MORBI_IO_API MORBI_DLLEXPORT
#else
    #define MORBI_IO_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_PLATFORM_EXPORTS)
    #define MORBI_PLATFORM_API MORBI_DLLEXPORT
#else
    #define MORBI_PLATFORM_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_SHADER_EXPORTS)
    #define MORBI_SHADER_API MORBI_DLLEXPORT
#else
    #define MORBI_SHADER_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_RHI_EXPORTS)
    #define MORBI_RHI_API MORBI_DLLEXPORT
#else
    #define MORBI_RHI_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_RHI_VULKAN_EXPORTS)
    #define MORBI_RHI_VULKAN_API MORBI_DLLEXPORT
#else
    #define MORBI_RHI_VULKAN_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_RHI_DX12_EXPORTS)
    #define MORBI_RHI_DX12_API MORBI_DLLEXPORT
#else
    #define MORBI_RHI_DX12_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_RENDER_EXPORTS)
    #define MORBI_RENDER_API MORBI_DLLEXPORT
#else
    #define MORBI_RENDER_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_SCENE_EXPORTS)
    #define MORBI_SCENE_API MORBI_DLLEXPORT
#else
    #define MORBI_SCENE_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_PHYSICS_EXPORTS)
    #define MORBI_PHYSICS_API MORBI_DLLEXPORT
#else
    #define MORBI_PHYSICS_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_AUDIO_EXPORTS)
    #define MORBI_AUDIO_API MORBI_DLLEXPORT
#else
    #define MORBI_AUDIO_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_SCRIPT_EXPORTS)
    #define MORBI_SCRIPT_API MORBI_DLLEXPORT
#else
    #define MORBI_SCRIPT_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_RUNTIME_EXPORTS)
    #define MORBI_RUNTIME_API MORBI_DLLEXPORT
#else
    #define MORBI_RUNTIME_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_UI_EXPORTS)
    #define MORBI_UI_API MORBI_DLLEXPORT
#else
    #define MORBI_UI_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_ASSET_EXPORTS)
    #define MORBI_ASSET_API MORBI_DLLEXPORT
#else
    #define MORBI_ASSET_API MORBI_DLLIMPORT
#endif

#if defined(MORBI_NETWORK_EXPORTS)
    #define MORBI_NETWORK_API MORBI_DLLEXPORT
#else
    #define MORBI_NETWORK_API MORBI_DLLIMPORT
#endif
