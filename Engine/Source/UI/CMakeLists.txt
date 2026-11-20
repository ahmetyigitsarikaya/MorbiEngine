add_library(Morbi.UI SHARED
    src/BinaryIPCBridge.cpp
    src/WebView2Compositor.cpp
    src/GameUIRenderer.cpp
)

add_library(Morbi::UI ALIAS Morbi.UI)
target_compile_definitions(Morbi.UI PRIVATE MORBI_UI_EXPORTS=1)

target_include_directories(Morbi.UI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(Morbi.UI
    PUBLIC
        Morbi::Core
        Morbi::IO
        Morbi::Platform
        Morbi::RHI
        Morbi::Render
        Morbi::Scene
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(Morbi.UI
        PRIVATE
            dcomp
            dxgi
            user32
            ole32
            comdlg32
    )
endif()

morbi_apply_target_options(Morbi.UI)
