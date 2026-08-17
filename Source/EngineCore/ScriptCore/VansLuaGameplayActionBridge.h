#pragma once

extern "C"
{
#include <lua.h>
}

namespace VansRuntime
{
class VansLuaGameplayActionBridge
{
public:
	// Expects the root `vans` table on top of the Lua stack.
	static void Register(lua_State* state);
	static void Shutdown(lua_State* state);
};
}
