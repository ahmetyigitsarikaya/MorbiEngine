option(MORBI_ENABLE_AVX2 "Enable AVX2 SIMD instructions" ON)

function(morbi_apply_target_options TARGET_NAME)
    target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)

    if(MSVC)
        get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
        if(TARGET_TYPE STREQUAL "SHARED_LIBRARY")
            set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
            target_compile_definitions(${TARGET_NAME} PRIVATE MORBI_BUILDING_DLL=1)
        endif()

        target_compile_options(${TARGET_NAME} PRIVATE
            /W4
            /wd4251
            /wd4275
            /MP
            /Zi
            /permissive-
            /Zc:__cplusplus
            /EHsc
            $<$<BOOL:${MORBI_ENABLE_AVX2}>:/arch:AVX2>
        )
        target_link_options(${TARGET_NAME} PRIVATE
            /DEBUG
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
            $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
        )
        target_compile_definitions(${TARGET_NAME} PUBLIC
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            UNICODE
            _UNICODE
        )
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<BOOL:${MORBI_ENABLE_AVX2}>:-mavx2>
            $<$<BOOL:${MORBI_ENABLE_AVX2}>:-mfma>
        )
    endif()
endfunction()
