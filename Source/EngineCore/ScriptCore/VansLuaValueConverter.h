#pragma once

#include "../RuntimeUI/Public/VansUIVariant.h"

extern "C"
{
#include <lua.h>
}

#include <cstddef>
#include <string>

namespace VansRuntime
{
	struct VansLuaValueCodecLimits
	{
		std::size_t maxDepth = 16;
		std::size_t maxNodes = 1024;
		std::size_t maxStringBytes = 64 * 1024;
	};

	class VansLuaValueConverter
	{
	public:
		static bool TryToVariant(lua_State* state, int index, VansUIVariant& value,
			std::string& error, VansLuaValueCodecLimits limits = {});
		static bool TryToVariantMap(lua_State* state, int index, VansUIVariantMap& values,
			std::string& error, VansLuaValueCodecLimits limits = {});
		static bool TryPushVariant(lua_State* state, const VansUIVariant& value,
			std::string& error, VansLuaValueCodecLimits limits = {});
		static bool TryPushVariantMap(lua_State* state, const VansUIVariantMap& values,
			std::string& error, VansLuaValueCodecLimits limits = {});
	};
}
