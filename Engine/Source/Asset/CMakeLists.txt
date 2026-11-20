add_library(Morbi.Asset SHARED
    src/AssetRegistry.cpp
    src/ModelImporter.cpp
    src/TextureImporter.cpp
    src/CookedAssetSerializer.cpp
    src/AnimationEvaluator.cpp
)

add_library(Morbi::Asset ALIAS Morbi.Asset)
target_compile_definitions(Morbi.Asset PRIVATE MORBI_ASSET_EXPORTS=1)

target_include_directories(Morbi.Asset
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.Asset
    PUBLIC
        Morbi::Core
        Morbi::IO
        Morbi::RHI
)

if(WIN32)
    target_link_libraries(Morbi.Asset
        PRIVATE
            windowscodecs
            ole32
    )
endif()

if(TARGET Morbi::Assimp)
    target_link_libraries(Morbi.Asset PRIVATE Morbi::Assimp)
    target_compile_definitions(Morbi.Asset PRIVATE MORBI_HAS_ASSIMP=1)
endif()

morbi_apply_target_options(Morbi.Asset)
