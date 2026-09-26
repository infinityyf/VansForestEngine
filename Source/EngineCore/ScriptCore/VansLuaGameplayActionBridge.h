#pragma once

#include "../GameplayTargeting/VansGameplayTargeting.h"

#include <functional>
#include <string>
#include <string_view>

extern "C"
{
#include <lua.h>
}

namespace VansRuntime
{
using VansLuaTargetEntityResolver =
	std::function<Vans::VansEntityHandle(std::string_view)>;

bool VansDecodeLuaTargetData(lua_State* state, int index,
	const VansLuaTargetEntityResolver& resolveEntity,
	Vans::VansTargetData& targetData,
	std::string& error);

class VansLuaGameplayActionBridge
{
public:
	// Expects the root `vans` table on top of the Lua stack.
	static void Register(lua_State* state);
	static void Shutdown(lua_State* state);
};
}
