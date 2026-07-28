include_guard(GLOBAL)

function(forest_collect_engine_sources)
    file(GLOB_RECURSE all_engine_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/EngineCore/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Graphics/*.cpp"
    )

    set(runtime_sources ${all_engine_sources})
    list(FILTER runtime_sources EXCLUDE REGEX "/EditorCore/")
    list(FILTER runtime_sources EXCLUDE REGEX "/EngineAPILayer/")
    list(FILTER runtime_sources EXCLUDE REGEX "/AssetCore/Importers/")
    list(FILTER runtime_sources EXCLUDE REGEX "/RenderCore/VulkanCore/VansGUIVulkanBackEnd\\.cpp$")
    list(APPEND runtime_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/EngineCore/AssetCore/Importers/VansTextureCooker.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/EngineCore/AssetCore/Importers/Shader/VansShaderArtifactCache.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/EngineCore/AssetCore/Importers/Shader/VansShaderCompiler.cpp"
    )

    set(editor_sources ${all_engine_sources})
    list(FILTER editor_sources INCLUDE REGEX "/EditorCore/|/EngineAPILayer/|/AssetCore/Importers/|/RenderCore/VulkanCore/VansGUIVulkanBackEnd\\.cpp$")
    list(FILTER editor_sources EXCLUDE REGEX "/AssetCore/Importers/VansTextureCooker\\.cpp$")
    list(FILTER editor_sources EXCLUDE REGEX "/AssetCore/Importers/Shader/VansShaderArtifactCache\\.cpp$")
    list(FILTER editor_sources EXCLUDE REGEX "/AssetCore/Importers/Shader/VansShaderCompiler\\.cpp$")
    set(runtime_export_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/RuntimeExport/ForestRuntimeExports.cpp"
    )

    set(FOREST_RUNTIME_CORE_SOURCES ${runtime_sources} PARENT_SCOPE)
    set(FOREST_EDITOR_CORE_SOURCES ${editor_sources} PARENT_SCOPE)
    set(FOREST_RUNTIME_EXPORT_SOURCES ${runtime_export_sources} PARENT_SCOPE)
endfunction()

function(forest_apply_source_groups target_name)
    get_target_property(target_sources ${target_name} SOURCES)
    if(target_sources)
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${target_sources})
    endif()
endfunction()
