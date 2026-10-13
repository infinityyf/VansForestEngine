#pragma once
struct lua_State;
namespace VansRuntime
{
class VansLuaTerrainBridge final
{
public:
    static void Register(lua_State* state);
};
}
