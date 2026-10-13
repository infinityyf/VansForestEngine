#pragma once
struct lua_State;
namespace VansRuntime
{
class VansLuaProjectRenderPassBridge final
{
public:
    // 栈顶为当前 ScriptContext 的 vans 根表；仅暴露 Main 线程值接口。
    static void Register(lua_State* state);
};
}
