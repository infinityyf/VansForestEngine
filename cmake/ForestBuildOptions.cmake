include_guard(GLOBAL)

option(FOREST_BUILD_EDITOR "Build the Windows editor executable." ON)
option(FOREST_BUILD_GAME_LAUNCHER "Build the Windows game launcher executable." ON)
option(FOREST_BUILD_PACKAGE_TOOL "Build the command-line game package tool." ON)
option(FOREST_BUILD_RUNTIME_SHARED "Build the future runtime DLL facade." OFF)
option(FOREST_WITH_TOOLS "Build authoring/cooking tools when targets are added." ON)
option(VANS_SCRIPT_LUA "Build the Lua runtime scripting backend." ON)
option(VANS_SCRIPT_LUA_HOT_RELOAD_EDITOR "Enable Lua hot reload in editor targets." ON)

set(FOREST_PLATFORM "Windows" CACHE STRING "Target platform: Windows now; Android is reserved for the future.")
set_property(CACHE FOREST_PLATFORM PROPERTY STRINGS Windows Android)

if(NOT FOREST_PLATFORM STREQUAL "Windows")
    message(FATAL_ERROR "Only FOREST_PLATFORM=Windows is implemented right now. Android is reserved in the target layout.")
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
