#pragma once

extern "C"
{
#include <lua.h>
}

namespace VansRuntime
{
	class VansLuaUIBridge
	{
	public:
		static void Register(lua_State* L);
		static void Shutdown(lua_State* L);
	};
}
