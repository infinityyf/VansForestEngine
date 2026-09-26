#include "VansLuaValueConverter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>

extern "C"
{
#include <lauxlib.h>
}

namespace VansRuntime
{
	namespace
	{
		struct CodecBudget
		{
			VansLuaValueCodecLimits limits;
			std::size_t nodes = 0;
			std::size_t stringBytes = 0;
			std::unordered_set<const void*> activeTables;
		};

		bool Fail(std::string& error, std::string message)
		{
			error = std::move(message);
			return false;
		}

		bool ConsumeNode(CodecBudget& budget, std::size_t depth, std::string& error)
		{
			if (depth > budget.limits.maxDepth)
				return Fail(error, "Lua UI value exceeds the nesting-depth budget");
			if (++budget.nodes > budget.limits.maxNodes)
				return Fail(error, "Lua UI value exceeds the node budget");
			return true;
		}

		bool ConsumeString(CodecBudget& budget, std::size_t bytes, std::string& error)
		{
			if (budget.stringBytes > budget.limits.maxStringBytes ||
				bytes > budget.limits.maxStringBytes - budget.stringBytes)
			{
				return Fail(error, "Lua UI value exceeds the string-byte budget");
			}
			budget.stringBytes += bytes;
			return true;
		}

		bool InspectTableShape(
			lua_State* state,
			int index,
			bool& isArray,
			lua_Integer& arrayLength,
			const CodecBudget& budget,
			std::string& error)
		{
			index = lua_absindex(state, index);
			std::size_t entryCount = 0;
			lua_Integer maximumIndex = 0;
			bool hasIntegerKeys = false;
			bool hasStringKeys = false;
			lua_pushnil(state);
			while (lua_next(state, index) != 0)
			{
				++entryCount;
				if (entryCount > budget.limits.maxNodes)
				{
					lua_pop(state, 2);
					return Fail(error, "Lua UI table exceeds the node budget");
				}
				if (lua_isinteger(state, -2))
				{
					const lua_Integer key = lua_tointeger(state, -2);
					if (key <= 0)
					{
						lua_pop(state, 2);
						return Fail(error, "Lua UI array keys must be positive integers");
					}
					hasIntegerKeys = true;
					maximumIndex = (std::max)(maximumIndex, key);
				}
				else if (lua_type(state, -2) == LUA_TSTRING)
				{
					hasStringKeys = true;
				}
				else
				{
					lua_pop(state, 2);
					return Fail(error, "Lua UI map keys must be strings");
				}
				if (hasIntegerKeys && hasStringKeys)
				{
					lua_pop(state, 2);
					return Fail(error, "Lua UI tables cannot mix array and map keys");
				}
				lua_pop(state, 1);
			}
			isArray = hasIntegerKeys;
			arrayLength = isArray ? maximumIndex : 0;
			if (isArray && static_cast<std::size_t>(maximumIndex) != entryCount)
				return Fail(error, "Lua UI arrays must contain every index from 1 through N");
			return true;
		}

		bool DecodeValue(lua_State* state, int index, std::size_t depth,
			CodecBudget& budget, VansUIVariant& value, std::string& error);

		bool DecodeMap(
			lua_State* state,
			int index,
			std::size_t depth,
			CodecBudget& budget,
			VansUIVariantMap& values,
			std::string& error)
		{
			index = lua_absindex(state, index);
			lua_pushnil(state);
			while (lua_next(state, index) != 0)
			{
				size_t keyLength = 0;
				const char* key = lua_tolstring(state, -2, &keyLength);
				if (!key || !ConsumeString(budget, keyLength, error))
				{
					lua_pop(state, 2);
					return false;
				}
				VansUIVariant item;
				if (!DecodeValue(state, -1, depth + 1, budget, item, error))
				{
					lua_pop(state, 2);
					return false;
				}
				values.emplace(std::string(key, keyLength), std::move(item));
				lua_pop(state, 1);
			}
			return true;
		}

		bool DecodeValue(
			lua_State* state,
			int index,
			std::size_t depth,
			CodecBudget& budget,
			VansUIVariant& value,
			std::string& error)
		{
			if (!ConsumeNode(budget, depth, error))
				return false;
			index = lua_absindex(state, index);
			switch (lua_type(state, index))
			{
			case LUA_TNIL:
				value = VansUIVariant();
				return true;
			case LUA_TBOOLEAN:
				value = VansUIVariant(lua_toboolean(state, index) != 0);
				return true;
			case LUA_TNUMBER:
				if (lua_isinteger(state, index))
					value = VansUIVariant(static_cast<std::int64_t>(lua_tointeger(state, index)));
				else
				{
					const double number = static_cast<double>(lua_tonumber(state, index));
					if (!std::isfinite(number))
						return Fail(error, "Lua UI numbers must be finite");
					value = VansUIVariant(number);
				}
				return true;
			case LUA_TSTRING:
			{
				size_t length = 0;
				const char* text = lua_tolstring(state, index, &length);
				if (!ConsumeString(budget, length, error))
					return false;
				value = VansUIVariant(std::string(text ? text : "", length));
				return true;
			}
			case LUA_TTABLE:
			{
				const void* identity = lua_topointer(state, index);
				if (!identity || !budget.activeTables.insert(identity).second)
					return Fail(error, "Lua UI values cannot contain table cycles");
				bool isArray = false;
				lua_Integer length = 0;
				if (!InspectTableShape(state, index, isArray, length, budget, error))
				{
					budget.activeTables.erase(identity);
					return false;
				}
				if (isArray)
				{
					VansUIVariantArray array;
					array.reserve(static_cast<std::size_t>(length));
					for (lua_Integer itemIndex = 1; itemIndex <= length; ++itemIndex)
					{
						lua_geti(state, index, itemIndex);
						VansUIVariant item;
						const bool decoded = DecodeValue(
							state, -1, depth + 1, budget, item, error);
						lua_pop(state, 1);
						if (!decoded)
						{
							budget.activeTables.erase(identity);
							return false;
						}
						array.push_back(std::move(item));
					}
					value = VansUIVariant(std::move(array));
				}
				else
				{
					VansUIVariantMap map;
					if (!DecodeMap(state, index, depth, budget, map, error))
					{
						budget.activeTables.erase(identity);
						return false;
					}
					value = VansUIVariant(std::move(map));
				}
				budget.activeTables.erase(identity);
				return true;
			}
			default:
				return Fail(error,
					"Lua UI values support only nil, bool, number, string, array, and map");
			}
		}

		bool EncodeValue(
			lua_State* state,
			const VansUIVariant& value,
			std::size_t depth,
			CodecBudget& budget,
			std::string& error)
		{
			if (!ConsumeNode(budget, depth, error))
				return false;
			return std::visit([&](const auto& typedValue) -> bool
			{
				using T = std::decay_t<decltype(typedValue)>;
				if constexpr (std::is_same_v<T, std::monostate>)
					lua_pushnil(state);
				else if constexpr (std::is_same_v<T, bool>)
					lua_pushboolean(state, typedValue);
				else if constexpr (std::is_same_v<T, std::int64_t>)
					lua_pushinteger(state, static_cast<lua_Integer>(typedValue));
				else if constexpr (std::is_same_v<T, double>)
				{
					if (!std::isfinite(typedValue))
						return Fail(error, "UI variant numbers must be finite");
					lua_pushnumber(state, static_cast<lua_Number>(typedValue));
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					if (!ConsumeString(budget, typedValue.size(), error))
						return false;
					lua_pushlstring(state, typedValue.data(), typedValue.size());
				}
				else if constexpr (std::is_same_v<T, VansUIVariantArray>)
				{
					lua_createtable(state, static_cast<int>(typedValue.size()), 0);
					lua_Integer itemIndex = 1;
					for (const VansUIVariant& item : typedValue)
					{
						if (!EncodeValue(state, item, depth + 1, budget, error))
							return false;
						lua_seti(state, -2, itemIndex++);
					}
				}
				else if constexpr (std::is_same_v<T, VansUIVariantMap>)
				{
					lua_createtable(state, 0, static_cast<int>(typedValue.size()));
					for (const auto& [key, item] : typedValue)
					{
						if (!ConsumeString(budget, key.size(), error) ||
							!EncodeValue(state, item, depth + 1, budget, error))
						{
							return false;
						}
						lua_setfield(state, -2, key.c_str());
					}
				}
				else if constexpr (std::is_same_v<T, VansUIHandleId>)
				{
					if (typedValue > static_cast<VansUIHandleId>(
						(std::numeric_limits<lua_Integer>::max)()))
					{
						return Fail(error, "UI handle does not fit in a Lua integer");
					}
					lua_pushinteger(state, static_cast<lua_Integer>(typedValue));
				}
				return true;
			}, value.value);
		}
	}

	bool VansLuaValueConverter::TryToVariant(
		lua_State* state, int index, VansUIVariant& value,
		std::string& error, VansLuaValueCodecLimits limits)
	{
		error.clear();
		if (!state)
			return Fail(error, "Lua state is null");
		CodecBudget budget{ limits };
		return DecodeValue(state, index, 0, budget, value, error);
	}

	bool VansLuaValueConverter::TryToVariantMap(
		lua_State* state, int index, VansUIVariantMap& values,
		std::string& error, VansLuaValueCodecLimits limits)
	{
		VansUIVariant decoded;
		if (!TryToVariant(state, index, decoded, error, limits))
			return false;
		const auto* map = std::get_if<VansUIVariantMap>(&decoded.value);
		if (!map)
			return Fail(error, "Lua UI value must be a map");
		values = *map;
		return true;
	}

	bool VansLuaValueConverter::TryPushVariant(
		lua_State* state, const VansUIVariant& value,
		std::string& error, VansLuaValueCodecLimits limits)
	{
		error.clear();
		if (!state)
			return Fail(error, "Lua state is null");
		const int originalTop = lua_gettop(state);
		CodecBudget budget{ limits };
		if (EncodeValue(state, value, 0, budget, error))
			return true;
		lua_settop(state, originalTop);
		return false;
	}

	bool VansLuaValueConverter::TryPushVariantMap(
		lua_State* state, const VansUIVariantMap& values,
		std::string& error, VansLuaValueCodecLimits limits)
	{
		return TryPushVariant(state, VansUIVariant(values), error, limits);
	}
}
