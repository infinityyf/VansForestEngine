#include "../EngineCore/Util/VansInputManager.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/ScriptCore/VansLuaScriptInspectorService.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "GLFW/glfw3.h"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#endif
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
bool Check(bool value, const char* message)
{
    if (!value) std::cerr << "[CursorContract] " << message << '\n';
    return value;
}

bool RunLua(lua_State* state, const char* script)
{
    if (luaL_dostring(state, script) == LUA_OK) return true;
    std::cerr << "[CursorContract] " << lua_tostring(state, -1) << '\n';
    lua_pop(state, 1);
    return false;
}
}

bool RunCursorContractTests()
{
    using namespace Vans;
    VANS_INIT_MAIN_THREAD();
    const VansCursorMode modes[] = { VansCursorMode::Visible, VansCursorMode::Hidden, VansCursorMode::Captured };
    for (auto requested : modes)
    {
        if (!Check(ResolveCursorMode(requested, VansCursorContext::Inactive, true) == VansCursorMode::Visible,
            "Inactive host must remain visible")) return false;
        if (!Check(ResolveCursorMode(requested, VansCursorContext::Standalone, false) == VansCursorMode::Visible,
            "Unfocused host must release cursor")) return false;
        if (!Check(ResolveCursorMode(requested, VansCursorContext::Standalone, true) == requested,
            "Standalone must support all modes")) return false;
    }
    if (!Check(ResolveCursorMode(VansCursorMode::Hidden, VansCursorContext::Viewport, true) == VansCursorMode::Hidden &&
        ResolveCursorMode(VansCursorMode::Captured, VansCursorContext::Viewport, true) == VansCursorMode::Visible,
        "Editor viewport must hide without allowing capture")) return false;

    auto& input = VansInputManager::Get();
    VansScriptContext scripts;
    scripts.SetActiveProjectRoot(std::filesystem::current_path().string());
    scripts.VansScriptSetup();
    if (!RunLua(scripts.GetLuaState(), R"lua(
        local i = vans.input
        assert(i.set_mouse_capture == nil and i.is_mouse_captured == nil)
        for _, mode in ipairs({'visible','hidden','captured'}) do
            i.set_cursor_mode(mode)
            assert(i.get_cursor_mode() == mode)
            assert(i.get_effective_cursor_mode() == 'visible')
        end
        for _, mode in ipairs({'game','ui','lock','normal','HIDDEN','',true,1,{}}) do
            assert(not pcall(i.set_cursor_mode, mode))
            assert(i.get_cursor_mode() == 'captured')
        end
        assert(not pcall(i.set_cursor_mode))
    )lua")) return false;

    // Inspector 执行顶层脚本时只能检查字段，不能改变运行时的请求。
    const auto fixture = std::filesystem::temp_directory_path() /
        ("ForestCursorInspector_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lua");
    {
        std::ofstream file(fixture);
        file << "vans.input.set_cursor_mode('hidden')\n"
            "assert(vans.input.get_cursor_mode() == 'visible')\n"
            "assert(vans.input.get_effective_cursor_mode() == 'visible')\n"
            "return { Probe = { __fields = {} } }\n";
    }
    const auto inspected = VansLuaScriptInspectorService::BuildDefaultFieldData(
        fixture.parent_path(), fixture.filename().string(), "Probe");
    std::filesystem::remove(fixture);
    if (!Check(inspected.success && input.GetCursorMode() == VansCursorMode::Captured,
        "Inspector cursor stubs must be available and isolated")) return false;
    scripts.ClearTrackedModules();
    if (!Check(input.GetCursorMode() == VansCursorMode::Visible, "Scene script teardown must reset cursor")) return false;
    std::cout << "Cursor contracts passed: modes, host policy, Lua validation, inspector isolation, teardown\n";
    return true;
}

bool RunCursorWindowContractTests()
{
    using namespace Vans;
    VANS_INIT_MAIN_THREAD();
    if (!Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed")) return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(400, 240, "Forest cursor contract", nullptr, nullptr);
    auto* other = glfwCreateWindow(200, 120, "Forest cursor focus contract", nullptr, nullptr);
    if (!window || !other)
    {
        if (other) glfwDestroyWindow(other);
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return Check(false, "GLFW window creation failed");
    }
    auto& input = VansInputManager::Get();
    input.Initialize(window);
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOther(window, true);
    struct Cleanup
    {
        GLFWwindow* window;
        GLFWwindow* other;
        ~Cleanup()
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            VansInputManager::Get().Shutdown();
            glfwDestroyWindow(other);
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    } cleanup{ window, other };
    glfwShowWindow(window);
    glfwFocusWindow(window);
    glfwPollEvents();
    input.RefreshPolledState();
    if (!Check(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE, "Test window must receive focus")) return false;
    input.SetCursorContext(VansCursorContext::Standalone);
    glfwSetCursorPos(window, 100, 100);
    glfwPollEvents();
    VansScriptContext scripts;
    scripts.SetActiveProjectRoot(std::filesystem::current_path().string());
    scripts.VansScriptSetup();
    const char* modes[] = { "visible", "hidden", "captured" };
    const int nativeModes[] = { GLFW_CURSOR_NORMAL, GLFW_CURSOR_HIDDEN, GLFW_CURSOR_DISABLED };
    for (int from = 0; from < 3; ++from)
        for (int to = 0; to < 3; ++to)
        {
            input.SetCursorMode(static_cast<VansCursorMode>(from));
            const auto code = std::string("vans.input.set_cursor_mode('") + modes[to] +
                "'); assert(vans.input.get_effective_cursor_mode() == '" + modes[to] + "')";
            if (!RunLua(scripts.GetLuaState(), code.c_str())) return false;
            if (!Check(glfwGetInputMode(window, GLFW_CURSOR) == nativeModes[to], "Lua must set real GLFW mode")) return false;
#ifdef _WIN32
            glfwPollEvents();
            CURSORINFO cursorInfo{ sizeof(CURSORINFO) };
            bool cursorRead = false;
            // Windows 系统光标由合成器异步发布，等待可见状态收敛。
            for (int attempt = 0; attempt < 25; ++attempt)
            {
                cursorRead = GetCursorInfo(&cursorInfo) != FALSE;
                if (cursorRead && ((cursorInfo.flags & CURSOR_SHOWING) != 0) == (to == 0)) break;
                glfwWaitEventsTimeout(0.01);
            }
            if (!cursorRead || ((cursorInfo.flags & CURSOR_SHOWING) != 0) != (to == 0))
            {
                POINT client{100, 100};
                ClientToScreen(glfwGetWin32Window(window), &client);
                std::cerr << "[CursorNative] from=" << modes[from] << " to=" << modes[to]
                    << " read=" << cursorRead << " flags=" << cursorInfo.flags
                    << " handle=" << cursorInfo.hCursor << " threadCursor=" << GetCursor()
                    << " pointer=" << cursorInfo.ptScreenPos.x << ',' << cursorInfo.ptScreenPos.y
                    << " target=" << client.x << ',' << client.y
                    << " hit=" << (WindowFromPoint(cursorInfo.ptScreenPos) == glfwGetWin32Window(window))
                    << " focused=" << glfwGetWindowAttrib(window, GLFW_FOCUSED) << '\n';
                return Check(false, "Windows system cursor visibility must match mode");
            }
#endif
            if (glfwRawMouseMotionSupported() && !Check(glfwGetInputMode(window, GLFW_RAW_MOUSE_MOTION) ==
                (to == 2 ? GLFW_TRUE : GLFW_FALSE), "Raw motion must follow capture only")) return false;
        }

    input.SetCursorMode(VansCursorMode::Visible);
    glfwSetCursorPos(window, 100, 100);
    glfwPollEvents();
    input.RefreshPolledState();
    glfwSetCursorPos(window, 110, 105);
    glfwPollEvents();
    input.RefreshPolledState();
    double dx, dy, nextDx, nextDy;
    input.GetMouseDelta(dx, dy);
    if (!Check(dx != 0 || dy != 0, "Mouse delta fixture must contain movement")) return false;
    input.SetCursorMode(VansCursorMode::Hidden);
    input.SetCursorMode(VansCursorMode::Hidden);
    input.GetMouseDelta(nextDx, nextDy);
    if (!Check(dx == nextDx && dy == nextDy, "Visibility and repeated setters must preserve mouse delta")) return false;
#ifdef _WIN32
    input.Update();
    SendMessageW(glfwGetWin32Window(window), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(110, 105));
    SendMessageW(glfwGetWin32Window(window), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    input.RefreshPolledState();
    double scrollX, scrollY;
    input.GetScrollDelta(scrollX, scrollY);
    if (!Check(input.IsMouseButtonPressed(MouseButton::Left) && scrollY == 1.0,
        "Hidden cursor must preserve button and scroll input")) return false;
    input.Update();
    SendMessageW(glfwGetWin32Window(window), WM_LBUTTONUP, 0, MAKELPARAM(110, 105));
    input.RefreshPolledState();
    if (!Check(input.IsMouseButtonReleased(MouseButton::Left), "Hidden cursor must preserve button release")) return false;
#endif
    input.SetCursorMode(VansCursorMode::Captured);
    input.GetMouseDelta(nextDx, nextDy);
    if (!Check(nextDx == 0 && nextDy == 0, "Capture transition must clear virtual-position jump")) return false;

    // ImGui 安装的回调链必须保留输入管理器的焦点处理。
    glfwShowWindow(other);
    glfwFocusWindow(other);
    glfwPollEvents();
    if (!Check(input.GetCursorMode() == VansCursorMode::Captured &&
        input.GetEffectiveCursorMode() == VansCursorMode::Visible &&
        glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "Focus loss must release without losing request")) return false;
    glfwFocusWindow(window);
    glfwPollEvents();
    if (!Check(input.GetEffectiveCursorMode() == VansCursorMode::Captured, "Focus regain must restore requested mode")) return false;
    input.SetCursorContext(VansCursorContext::Viewport);
    if (!Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "Editor must reject capture")) return false;
    input.SetCursorMode(VansCursorMode::Hidden);
    if (!Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_HIDDEN, "Editor viewport must allow hidden mode")) return false;
    input.SetCursorContext(VansCursorContext::Inactive);
    if (!Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "Leaving viewport must restore cursor")) return false;
    input.SetCursorContext(VansCursorContext::Standalone);
    input.SetCursorMode(VansCursorMode::Captured);
    scripts.ClearTrackedModules();
    if (!Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "Scene teardown must restore native cursor")) return false;
    input.SetCursorMode(VansCursorMode::Hidden);
    scripts.ShutdownLua();
    if (!Check(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "Lua shutdown must restore native cursor")) return false;
    std::cout << "Cursor window contracts passed: 9 transitions, Lua/GLFW, raw motion, delta, ImGui focus chain, viewport, teardown\n";
    return true;
}

// 在独立 Lua 环境执行已有项目测试，避免测试里的 mock 污染真实绑定。
bool RunCursorProjectContractTests()
{
    auto workspace = std::filesystem::current_path();
    while (!std::filesystem::is_directory(workspace / "DemoHallProject"))
    {
        if (workspace == workspace.root_path()) return Check(false, "Cannot find project workspace");
        workspace = workspace.parent_path();
    }
    const char* fixtures[] = {
        "DemoHallProject/Tests/camera_controller_contract.lua",
        "DemoHallProject/Tests/camera_follow_contract.lua",
        "DemoHallProject/Tests/camera_collision_stability.lua"
    };
    for (const auto* fixture : fixtures)
    {
        lua_State* state = luaL_newstate();
        luaL_openlibs(state);
        VansInstallLuaProjectSearchPath(state, workspace / "DemoHallProject");
        const int result = luaL_dofile(state, (workspace / fixture).string().c_str());
        if (result != LUA_OK) std::cerr << "[CursorProject] " << lua_tostring(state, -1) << '\n';
        lua_close(state);
        if (result != LUA_OK) return false;
    }
    unsigned checked = 0;
    for (const char* project : { "DemoHallProject", "DustV3Project", "AnimationV2Project", "SponzaProject", "TestV2Project" })
    {
        lua_State* state = luaL_newstate();
        for (const auto& file : std::filesystem::recursive_directory_iterator(workspace / project / "Scripts"))
        {
            if (file.path().extension() != ".lua") continue;
            const int result = luaL_loadfile(state, file.path().string().c_str());
            if (result != LUA_OK) std::cerr << "[CursorProject] " << lua_tostring(state, -1) << '\n';
            lua_pop(state, 1);
            if (result != LUA_OK) { lua_close(state); return false; }
            ++checked;
        }
        lua_close(state);
    }
    std::cout << "Cursor project contracts passed; Lua syntax files=" << checked << '\n';
    return true;
}
