include_guard(GLOBAL)

if(NOT WIN32)
    message(FATAL_ERROR "ForestEngine currently supports the Windows x64 toolchain only.")
endif()

function(forest_apply_common_settings target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_17)

    if(MSVC)
        target_compile_options(${target_name}
            PRIVATE
                /MP
                /GR
                /utf-8
        )
        target_compile_definitions(${target_name}
            PRIVATE
                _CRT_SECURE_NO_WARNINGS
                NOMINMAX
        )
    endif()

    set_target_properties(${target_name}
        PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
    )
endfunction()
