#pragma once
#include "../SceneRuntime/VansRuntimeHandle.h"
#include <string>
struct lua_State;
namespace VansRuntime
{
// 脚本对象 userdata 与当前执行 owner 由 ScriptContext 唯一解析，领域绑定只消费稳定身份。
struct VansLuaNavigationAccess
{
    Vans::VansEntityHandle (*readEntity)(lua_State*,int)=nullptr;
    std::string (*readOwner)(lua_State*)=nullptr;
};
class VansLuaNavigationBridge final
{
public:
    static void Register(lua_State* state,const VansLuaNavigationAccess& access);
    static void CancelOwner(const std::string& owner);
    // 在既有脚本组件 source scope 内消费状态，事件回调不建立第二条导航队列。
    static bool DispatchOwner(lua_State* state,const std::string& owner,std::string& error);
};
}
