include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ForestSourceManifest.cmake")

function(forest_collect_engine_sources)
    set(manifest_sources
        ${FOREST_RUNTIME_CORE_SOURCES}
        ${FOREST_EDITOR_CORE_SOURCES}
        ${FOREST_PACKAGING_CORE_SOURCES}
        ${FOREST_CONTRACT_TEST_SOURCES}
    )
    list(LENGTH manifest_sources manifest_source_count)
    list(REMOVE_DUPLICATES manifest_sources)
    list(LENGTH manifest_sources unique_manifest_source_count)
    if(NOT manifest_source_count EQUAL unique_manifest_source_count)
        message(FATAL_ERROR "Forest source manifest contains duplicate compile-unit ownership.")
    endif()

    file(GLOB_RECURSE discovered_engine_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/EngineCore/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Graphics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Tests/*.cpp"
    )

    foreach(source_file IN LISTS discovered_engine_sources)
        list(FIND manifest_sources "${source_file}" source_index)
        if(source_index EQUAL -1)
            file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source_file}")
            message(FATAL_ERROR "Unowned ForestEngine compile unit: ${relative_source}. Assign it in cmake/ForestSourceManifest.cmake.")
        endif()
    endforeach()

    foreach(source_file IN LISTS manifest_sources)
        if(NOT EXISTS "${source_file}")
            file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source_file}")
            message(FATAL_ERROR "Forest source manifest references a missing compile unit: ${relative_source}.")
        endif()
        list(FIND discovered_engine_sources "${source_file}" source_index)
        if(source_index EQUAL -1)
            file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source_file}")
            message(FATAL_ERROR "Manifest compile unit is outside the owned EngineCore/Graphics/Tests roots: ${relative_source}.")
        endif()
    endforeach()

    set(FOREST_RUNTIME_CORE_SOURCES ${FOREST_RUNTIME_CORE_SOURCES} PARENT_SCOPE)
    set(FOREST_EDITOR_CORE_SOURCES ${FOREST_EDITOR_CORE_SOURCES} PARENT_SCOPE)
    set(FOREST_PACKAGING_CORE_SOURCES ${FOREST_PACKAGING_CORE_SOURCES} PARENT_SCOPE)
    set(FOREST_RUNTIME_EXPORT_SOURCES ${FOREST_RUNTIME_EXPORT_SOURCES} PARENT_SCOPE)
    set(FOREST_CONTRACT_TEST_SOURCES ${FOREST_CONTRACT_TEST_SOURCES} PARENT_SCOPE)
endfunction()

function(forest_apply_source_groups target_name)
    get_target_property(target_sources ${target_name} SOURCES)
    if(target_sources)
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${target_sources})
    endif()
endfunction()
