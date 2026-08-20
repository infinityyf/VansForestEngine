cmake_minimum_required(VERSION 3.24)

project(ForestEngine LANGUAGES CXX)

option(
    FOREST_ENABLE_STREAMLINE_DLSS
    "Build and package the NVIDIA Streamline DLSS backend"
    ON
)

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

target_link_libraries(ForestRuntimeCore
    PUBLIC
        ForestThirdParty
        ForestExternalNoesisVk
)

add_library(ForestPackagingCore STATIC
    ${FOREST_PACKAGING_CORE_SOURCES}
)
add_library(Forest::PackagingCore ALIAS ForestPackagingCore)

forest_apply_common_settings(ForestPackagingCore)
forest_apply_source_groups(ForestPackagingCore)

target_link_libraries(ForestPackagingCore
    PUBLIC
        ForestRuntimeCore
)

add_library(ForestEditorCore STATIC
    ${FOREST_EDITOR_CORE_SOURCES}
)
add_library(Forest::EditorCore ALIAS ForestEditorCore)

forest_apply_common_settings(ForestEditorCore)
forest_apply_source_groups(ForestEditorCore)

target_compile_definitions(ForestEditorCore
    PRIVATE
        FOREST_ENGINE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ForestEditorCore
    PUBLIC
        ForestRuntimeCore
        ForestExternalImGui
    PRIVATE
        ForestPackagingCore
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

add_executable(ForestPackageTool
    Source/Application/ForestPackageToolEntry.cpp
)

forest_apply_common_settings(ForestPackageTool)
set_target_properties(ForestPackageTool PROPERTIES OUTPUT_NAME "ForestPackageTool")

target_compile_definitions(ForestPackageTool
    PRIVATE
        FOREST_ENGINE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ForestPackageTool
    PRIVATE
        ForestPackagingCore
)

forest_copy_runtime_dlls(ForestPackageTool)

add_executable(ForestAssetTool
    Source/Application/ForestAssetToolEntry.cpp
)

forest_apply_common_settings(ForestAssetTool)
set_target_properties(ForestAssetTool PROPERTIES OUTPUT_NAME "ForestAssetTool")

target_link_libraries(ForestAssetTool
    PRIVATE
        ForestRuntimeCore
)

forest_copy_runtime_dlls(ForestAssetTool)

add_executable(ForestGameLauncher
    Source/Application/ForestGameLauncherEntry.cpp
)

forest_apply_common_settings(ForestGameLauncher)
set_target_properties(ForestGameLauncher PROPERTIES OUTPUT_NAME "ForestGameLauncher")

forest_copy_runtime_dlls(ForestGameLauncher)

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

enable_testing()

add_executable(ForestContractTests
    ${FOREST_CONTRACT_TEST_SOURCES}
)
forest_apply_common_settings(ForestContractTests)
forest_apply_source_groups(ForestContractTests)
target_link_libraries(ForestContractTests
    PRIVATE
        ForestRuntimeCore
		ForestEditorCore
)
if(MSVC)
    target_compile_options(ForestContractTests PRIVATE /bigobj)
endif()
add_test(NAME ForestContractTests COMMAND ForestContractTests)

add_custom_target(ForestGamePackageBinaries
    DEPENDS
        ForestRuntime
        ForestGameLauncher
)

add_custom_target(ForestEngineComplete ALL
    DEPENDS
        ForestEditor
        ForestPackageTool
        ForestAssetTool
        ForestGamePackageBinaries
        ForestContractTests
)
