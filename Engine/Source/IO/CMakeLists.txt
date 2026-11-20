add_library(Morbi.IO SHARED
    src/PathService.cpp
    src/BinaryStream.cpp
    src/FileWatcher.cpp
    src/MorbiPak.cpp
)

add_library(Morbi::IO ALIAS Morbi.IO)
target_compile_definitions(Morbi.IO PRIVATE MORBI_IO_EXPORTS=1)

target_include_directories(Morbi.IO
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Morbi.IO
    PUBLIC
        Morbi::Core
    PRIVATE
        nlohmann_json::nlohmann_json
)

morbi_apply_target_options(Morbi.IO)
