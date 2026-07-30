#include "VansLuaValueConverter.h"

#include <type_traits>
#include <utility>

extern "C"
{
#include <lauxlib.h>
}

namespace VansRuntime
{
	namespace
	{
		bool IsSequentialArray(lua_State* L, int index)
		{
			index = lua_absindex(L, index);
			lua_Integer expected = 1;
			lua_pushnil(L);
			while (lua_next(L, index) != 0)
			{
				if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) != expected++)
				{
					lua_pop(L, 2);
					return false;
				}
				lua_pop(L, 1);
			}
			return expected > 1;
		}
	}

	VansUIVariant VansLuaValueConverter::ToVariant(lua_State* L, int index)
	{
		index = lua_absindex(L, index);
		switch (lua_type(L, index))
		{
		case LUA_TBOOLEAN:
			return VansUIVariant(lua_toboolean(L, index) != 0);
		case LUA_TNUMBER:
			if (lua_isinteger(L, index))
				return VansUIVariant(static_cast<std::int64_t>(lua_tointeger(L, index)));
			return VansUIVariant(static_cast<double>(lua_tonumber(L, index)));
		case LUA_TSTRING:
			return VansUIVariant(std::string(lua_tostring(L, index)));
		case LUA_TTABLE:
			if (IsSequentialArray(L, index))
			{
				VansUIVariantArray array;
				const lua_Integer length = luaL_len(L, index);
				array.reserve(static_cast<std::size_t>(length));
				for (lua_Integer i = 1; i <= length; ++i)
				{
					lua_geti(L, index, i);
					array.push_back(ToVariant(L, -1));
					lua_pop(L, 1);
				}
				return VansUIVariant(std::move(array));
			}
			return VansUIVariant(ToVariantMap(L, index));
		default:
			return VansUIVariant();
		}
	}

	VansUIVariantMap VansLuaValueConverter::ToVariantMap(lua_State* L, int index)
	{
		VansUIVariantMap result;
		if (!lua_istable(L, index))
			return result;

		index = lua_absindex(L, index);
		lua_pushnil(L);
		while (lua_next(L, index) != 0)
		{
			if (lua_type(L, -2) == LUA_TSTRING)
				result.emplace(lua_tostring(L, -2), ToVariant(L, -1));
			lua_pop(L, 1);
		}
		return result;
	}

	void VansLuaValueConverter::PushVariant(lua_State* L, const VansUIVariant& value)
	{
		std::visit([L](const auto& typedValue)
		{
			using T = std::decay_t<decltype(typedValue)>;
			if constexpr (std::is_same_v<T, std::monostate>)
				lua_pushnil(L);
			else if constexpr (std::is_same_v<T, bool>)
				lua_pushboolean(L, typedValue);
			else if constexpr (std::is_same_v<T, std::int64_t>)
				lua_pushinteger(L, static_cast<lua_Integer>(typedValue));
			else if constexpr (std::is_same_v<T, double>)
				lua_pushnumber(L, static_cast<lua_Number>(typedValue));
			else if constexpr (std::is_same_v<T, std::string>)
				lua_pushstring(L, typedValue.c_str());
			else if constexpr (std::is_same_v<T, VansUIVariantArray>)
			{
				lua_newtable(L);
				lua_Integer index = 1;
				for (const VansUIVariant& item : typedValue)
				{
					PushVariant(L, item);
					lua_seti(L, -2, index++);
				}
			}
			else if constexpr (std::is_same_v<T, VansUIVariantMap>)
				PushVariantMap(L, typedValue);
			else if constexpr (std::is_same_v<T, VansUIHandleId>)
				lua_pushinteger(L, static_cast<lua_Integer>(typedValue));
		}, value.value);
	}

	void VansLuaValueConverter::PushVariantMap(lua_State* L, const VansUIVariantMap& values)
	{
		lua_newtable(L);
		for (const auto& [key, value] : values)
		{
			PushVariant(L, value);
			lua_setfield(L, -2, key.c_str());
		}
	}
}
