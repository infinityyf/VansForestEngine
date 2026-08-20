include_guard(GLOBAL)

set(FOREST_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")
set(FOREST_EXTERNAL_DIR "${FOREST_ROOT}/External" CACHE INTERNAL "")

add_library(ForestThirdParty INTERFACE)

set(FOREST_LUA_DIR "${FOREST_EXTERNAL_DIR}/Lua/lua-5.4.8" CACHE INTERNAL "")

set(FOREST_LUA_SOURCES
        "${FOREST_LUA_DIR}/src/lapi.c"
        "${FOREST_LUA_DIR}/src/lauxlib.c"
        "${FOREST_LUA_DIR}/src/lbaselib.c"
        "${FOREST_LUA_DIR}/src/lcode.c"
        "${FOREST_LUA_DIR}/src/lcorolib.c"
        "${FOREST_LUA_DIR}/src/lctype.c"
        "${FOREST_LUA_DIR}/src/ldblib.c"
        "${FOREST_LUA_DIR}/src/ldebug.c"
        "${FOREST_LUA_DIR}/src/ldo.c"
        "${FOREST_LUA_DIR}/src/ldump.c"
        "${FOREST_LUA_DIR}/src/lfunc.c"
        "${FOREST_LUA_DIR}/src/lgc.c"
        "${FOREST_LUA_DIR}/src/linit.c"
        "${FOREST_LUA_DIR}/src/liolib.c"
        "${FOREST_LUA_DIR}/src/llex.c"
        "${FOREST_LUA_DIR}/src/lmathlib.c"
        "${FOREST_LUA_DIR}/src/lmem.c"
        "${FOREST_LUA_DIR}/src/loadlib.c"
        "${FOREST_LUA_DIR}/src/lobject.c"
        "${FOREST_LUA_DIR}/src/lopcodes.c"
        "${FOREST_LUA_DIR}/src/loslib.c"
        "${FOREST_LUA_DIR}/src/lparser.c"
        "${FOREST_LUA_DIR}/src/lstate.c"
        "${FOREST_LUA_DIR}/src/lstring.c"
        "${FOREST_LUA_DIR}/src/lstrlib.c"
        "${FOREST_LUA_DIR}/src/ltable.c"
        "${FOREST_LUA_DIR}/src/ltablib.c"
        "${FOREST_LUA_DIR}/src/ltm.c"
        "${FOREST_LUA_DIR}/src/lundump.c"
        "${FOREST_LUA_DIR}/src/lutf8lib.c"
        "${FOREST_LUA_DIR}/src/lvm.c"
        "${FOREST_LUA_DIR}/src/lzio.c"
)
set_source_files_properties(${FOREST_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(ForestExternalLua STATIC ${FOREST_LUA_SOURCES})
source_group(TREE "${FOREST_ROOT}" FILES ${FOREST_LUA_SOURCES})
forest_apply_common_settings(ForestExternalLua)
target_include_directories(ForestExternalLua PUBLIC "${FOREST_LUA_DIR}/src")
target_compile_definitions(ForestExternalLua
    PUBLIC
        LUA_COMPAT_5_3
)

target_include_directories(ForestThirdParty
    INTERFACE
        "${FOREST_EXTERNAL_DIR}/PhysX/include"
        "${FOREST_EXTERNAL_DIR}/FidelityFX/ffx_api/include"
        "${FOREST_EXTERNAL_DIR}/assimp/include"
        "${FOREST_EXTERNAL_DIR}/json/include"
        "${FOREST_EXTERNAL_DIR}/STBImge"
        "${FOREST_EXTERNAL_DIR}/GUI/glfw-3.3.9/include"
        "${FOREST_EXTERNAL_DIR}/Graphics/Vulkan/1.4.321.1/Include"
        "${FOREST_EXTERNAL_DIR}/NvCloth/include"
        "${FOREST_EXTERNAL_DIR}/NvCloth/extensions/include"
        "${FOREST_EXTERNAL_DIR}/NvCloth/PxShared/include"
        "${FOREST_EXTERNAL_DIR}"
        "${FOREST_EXTERNAL_DIR}/NoesisGUI/Include"
        "${FOREST_EXTERNAL_DIR}/NoesisGUI/Src/Packages/Render/VKRenderDevice/Include"
        "${FOREST_EXTERNAL_DIR}/NoesisGUI/Src/Packages/App/Providers/Include"
        "${FOREST_EXTERNAL_DIR}/VMA"
        "${FOREST_EXTERNAL_DIR}/ffmpeg/include"
        "${FOREST_EXTERNAL_DIR}/openal/include"
)

if(FOREST_ENABLE_STREAMLINE_DLSS)
    set(FOREST_STREAMLINE_REQUIRED_FILES
        "${FOREST_EXTERNAL_DIR}/Streamline/include/sl.h"
        "${FOREST_EXTERNAL_DIR}/Streamline/include/sl_dlss.h"
        "${FOREST_EXTERNAL_DIR}/Streamline/include/sl_security.h"
        "${FOREST_EXTERNAL_DIR}/Streamline/bin/x64/sl.interposer.dll"
        "${FOREST_EXTERNAL_DIR}/Streamline/bin/x64/sl.common.dll"
        "${FOREST_EXTERNAL_DIR}/Streamline/bin/x64/sl.dlss.dll"
        "${FOREST_EXTERNAL_DIR}/Streamline/bin/x64/nvngx_dlss.dll"
        "${FOREST_EXTERNAL_DIR}/Streamline/license.txt"
        "${FOREST_EXTERNAL_DIR}/Streamline/3rd-party-licenses.md"
        "${FOREST_EXTERNAL_DIR}/Streamline/bin/x64/nvngx_dlss.license.txt"
    )
    foreach(streamline_file IN LISTS FOREST_STREAMLINE_REQUIRED_FILES)
        if(NOT EXISTS "${streamline_file}")
            message(FATAL_ERROR
                "FOREST_ENABLE_STREAMLINE_DLSS=ON requires the fixed Streamline SDK file: ${streamline_file}"
            )
        endif()
    endforeach()
    target_include_directories(ForestThirdParty INTERFACE
        "${FOREST_EXTERNAL_DIR}/Streamline/include"
    )
    target_compile_definitions(ForestThirdParty INTERFACE VANS_HAS_STREAMLINE=1)
endif()

target_link_directories(ForestThirdParty
    INTERFACE
        "$<$<CONFIG:Debug>:${FOREST_EXTERNAL_DIR}/PhysX/lib/debug>"
        "$<$<NOT:$<CONFIG:Debug>>:${FOREST_EXTERNAL_DIR}/PhysX/lib/release>"
        "${FOREST_EXTERNAL_DIR}/NvCloth/lib"
        "${FOREST_EXTERNAL_DIR}/FidelityFX/PrebuiltSignedDLL"
        "${FOREST_EXTERNAL_DIR}/assimp/build/lib/Release"
        "${FOREST_EXTERNAL_DIR}/GUI/glfw-3.3.9/build/src/$<CONFIG>"
        "${FOREST_EXTERNAL_DIR}/NoesisGUI/Lib/windows_x86_64"
        "${FOREST_EXTERNAL_DIR}/ffmpeg/lib"
        "${FOREST_EXTERNAL_DIR}/openal/lib"
)

target_link_libraries(ForestThirdParty
    INTERFACE
        avformat
        avcodec
        avutil
        swscale
        swresample
        PhysXFoundation_64
        PhysXCommon_64
        PhysX_64
        PhysXCooking_64
        PhysXExtensions_static_64
        PhysXCharacterKinematic_static_64
        PhysXPvdSDK_static_64
        PhysXVehicle2_static_64
        "$<$<CONFIG:Debug>:NvClothDEBUG_x64>"
        "$<$<NOT:$<CONFIG:Debug>>:NvCloth_x64>"
        glfw3
        assimp-vc143-mt
        "$<$<CONFIG:Debug>:amd_fidelityfx_vkd>"
        "$<$<NOT:$<CONFIG:Debug>>:amd_fidelityfx_vk>"
        Noesis
        OpenAL32
)

target_link_libraries(ForestThirdParty INTERFACE ForestExternalLua)

add_library(ForestExternalNoesisVk STATIC
    "${FOREST_EXTERNAL_DIR}/NoesisGUI/Src/Packages/Render/VKRenderDevice/Src/Render.VKRenderDevice.cpp"
    "${FOREST_EXTERNAL_DIR}/NoesisGUI/Src/Packages/Render/VKRenderDevice/Src/VKRenderDevice.cpp"
    "${FOREST_EXTERNAL_DIR}/NoesisGUI/Src/Packages/App/Providers/Src/FastLZ.cpp"
)
forest_apply_common_settings(ForestExternalNoesisVk)
target_compile_definitions(ForestExternalNoesisVk
    PUBLIC
        NS_RENDER_VKRENDERDEVICE_API=
        NS_APP_PROVIDERS_API=
)
target_link_libraries(ForestExternalNoesisVk PUBLIC ForestThirdParty)

add_library(ForestExternalImGui STATIC
    "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2/backends/imgui_impl_glfw.cpp"
        "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2/backends/imgui_impl_vulkan.cpp"
        "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2/imgui.cpp"
        "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2/imgui_draw.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2/imgui_tables.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2/imgui_widgets.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/ImGuizmo/ImGuizmo.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/ImGuiNode/imgui_node_editor.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/ImGuiNode/imgui_node_editor_api.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/ImGuiNode/imgui_canvas.cpp"
    "${FOREST_EXTERNAL_DIR}/GUI/ImGuiNode/crude_json.cpp"
)
forest_apply_common_settings(ForestExternalImGui)
target_include_directories(ForestExternalImGui
    PUBLIC
        "${FOREST_EXTERNAL_DIR}/GUI/imgui-1.90.2"
        "${FOREST_EXTERNAL_DIR}/GUI/ImGuizmo"
        "${FOREST_EXTERNAL_DIR}/GUI/ImGuiNode"
)
target_link_libraries(ForestExternalImGui PUBLIC ForestThirdParty)

include("${CMAKE_CURRENT_LIST_DIR}/ForestRuntimeDependencies.cmake")

function(forest_copy_engine_assets target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FOREST_ROOT}/EngineAssets"
            "$<TARGET_FILE_DIR:${target_name}>/../EngineAssets"
        COMMENT "Copying ForestEngine built-in assets"
        VERBATIM
    )
endfunction()
