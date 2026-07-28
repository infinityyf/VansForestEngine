cmake_minimum_required(VERSION 3.24)

project(ForestEngine LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ForestBuildOptions)
include(ForestThirdParty)
include(ForestSourceGroups)

forest_collect_engine_sources()

add_library(ForestRuntimeCore STATIC
    ${FOREST_RUNTIME_CORE_SOURCES}
)
add_library(Forest::RuntimeCore ALIAS ForestRuntimeCore)

forest_apply_common_settings(ForestRuntimeCore)
forest_apply_source_groups(ForestRuntimeCore)

target_compile_definitions(ForestRuntimeCore
    PUBLIC
        FOREST_RUNTIME_CORE=1
        VANS_SCRIPT_LUA=$<BOOL:${VANS_SCRIPT_LUA}>
        VANS_SCRIPT_LUA_HOT_RELOAD_EDITOR=$<BOOL:${VANS_SCRIPT_LUA_HOT_RELOAD_EDITOR}>
)

target_link_libraries(ForestRuntimeCore
    PUBLIC
        ForestThirdParty
        ForestExternalSpirvCross
        ForestExternalNoesisVk
)

if(FOREST_BUILD_EDITOR)
    add_library(ForestEditorCore STATIC
        ${FOREST_EDITOR_CORE_SOURCES}
    )
    add_library(Forest::EditorCore ALIAS ForestEditorCore)

    forest_apply_common_settings(ForestEditorCore)
    forest_apply_source_groups(ForestEditorCore)

    target_compile_definitions(ForestEditorCore
        PUBLIC
            FOREST_EDITOR=1
        PRIVATE
            FOREST_ENGINE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(ForestEditorCore
        PUBLIC
            ForestRuntimeCore
            ForestExternalImGui
    )
    add_executable(ForestEditor
        Source/Application/ForestEngineEntry.cpp
    )

    forest_apply_common_settings(ForestEditor)
    set_target_properties(ForestEditor PROPERTIES OUTPUT_NAME "ForestEditor")

    target_link_libraries(ForestEditor
        PRIVATE
            ForestEditorCore
    )

    forest_copy_runtime_dlls(ForestEditor)
    forest_copy_engine_assets(ForestEditor)
    forest_copy_editor_runtime_assets(ForestEditor)
endif()

if(FOREST_BUILD_PACKAGE_TOOL)
    if(NOT TARGET ForestEditorCore)
        message(FATAL_ERROR "ForestPackageTool requires ForestEditorCore. Enable FOREST_BUILD_EDITOR or disable FOREST_BUILD_PACKAGE_TOOL.")
    endif()

    add_executable(ForestPackageTool
        Source/Application/ForestPackageToolEntry.cpp
    )

    forest_apply_common_settings(ForestPackageTool)
    set_target_properties(ForestPackageTool PROPERTIES OUTPUT_NAME "ForestPackageTool")

    target_compile_definitions(ForestPackageTool
        PRIVATE
            FOREST_PACKAGE_TOOL=1
            FOREST_ENGINE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(ForestPackageTool
        PRIVATE
            ForestEditorCore
    )

    forest_copy_runtime_dlls(ForestPackageTool)
endif()

if(FOREST_BUILD_GAME_LAUNCHER)
    add_executable(ForestGameLauncher
        Source/Application/ForestGameLauncherEntry.cpp
    )

    forest_apply_common_settings(ForestGameLauncher)
    set_target_properties(ForestGameLauncher PROPERTIES OUTPUT_NAME "ForestGameLauncher")

    target_compile_definitions(ForestGameLauncher
        PRIVATE
            FOREST_GAME_LAUNCHER=1
    )

    forest_copy_runtime_dlls(ForestGameLauncher)
endif()

if(FOREST_BUILD_RUNTIME_SHARED)
    add_library(ForestRuntime SHARED
        ${FOREST_RUNTIME_EXPORT_SOURCES}
    )

    forest_apply_common_settings(ForestRuntime)
    set_target_properties(ForestRuntime PROPERTIES OUTPUT_NAME "ForestRuntime")

    target_compile_definitions(ForestRuntime
        PRIVATE
            FOREST_RUNTIME_EXPORTS=1
    )

    target_link_libraries(ForestRuntime
        PRIVATE
            ForestRuntimeCore
    )

    forest_copy_runtime_dlls(ForestRuntime)
endif()
