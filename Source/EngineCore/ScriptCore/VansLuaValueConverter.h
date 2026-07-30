#pragma once

#include "../RuntimeUI/Public/VansUIVariant.h"

extern "C"
{
#include <lua.h>
}

namespace VansRuntime
{
	class VansLuaValueConverter
	{
	public:
		static VansUIVariant ToVariant(lua_State* L, int index);
		static VansUIVariantMap ToVariantMap(lua_State* L, int index);
		static void PushVariant(lua_State* L, const VansUIVariant& value);
		static void PushVariantMap(lua_State* L, const VansUIVariantMap& values);
	};
}
