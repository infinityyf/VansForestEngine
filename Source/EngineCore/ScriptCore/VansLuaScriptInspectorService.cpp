#include "VansLuaScriptInspectorService.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <algorithm>
#include <cctype>
#include <memory>

namespace Vans
{
namespace
{
struct LuaStateDeleter
{
    void operator()(lua_State* state) const
    {
        if (state)
            lua_close(state);
    }
};

using LuaStatePtr = std::unique_ptr<lua_State, LuaStateDeleter>;

int LuaStubLog(lua_State*)
{
    return 0;
}

int LuaStubFalse(lua_State* state)
{
    lua_pushboolean(state, 0);
    return 1;
}

int LuaStubMouseDelta(lua_State* state)
{
    lua_pushnumber(state, 0.0);
    lua_pushnumber(state, 0.0);
    return 2;
}

int LuaStubNil(lua_State* state)
{
    lua_pushnil(state);
    return 1;
}

int LuaStubTimeSeconds(lua_State* state)
{
    lua_pushnumber(state, 0.0);
    return 1;
}

void InstallEditorLuaStubs(lua_State* state)
{
    lua_newtable(state);

    lua_pushcfunction(state, LuaStubLog);
    lua_setfield(state, -2, "log");
    lua_pushcfunction(state, LuaStubNil);
    lua_setfield(state, -2, "find_object");
    lua_pushcfunction(state, LuaStubTimeSeconds);
    lua_setfield(state, -2, "time_seconds");

    lua_newtable(state);
    lua_pushcfunction(state, LuaStubFalse);
    lua_setfield(state, -2, "is_key_down");
    lua_pushcfunction(state, LuaStubFalse);
    lua_setfield(state, -2, "is_key_pressed");
    lua_pushcfunction(state, LuaStubFalse);
    lua_setfield(state, -2, "is_key_released");
    lua_pushcfunction(state, LuaStubMouseDelta);
    lua_setfield(state, -2, "get_mouse_delta");
    lua_setfield(state, -2, "input");

    lua_setglobal(state, "vans");
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string ReadLuaStringField(lua_State* state, int tableIndex, const char* fieldName)
{
    tableIndex = lua_absindex(state, tableIndex);
    lua_getfield(state, tableIndex, fieldName);
    std::string value;
    if (lua_isstring(state, -1))
        value = lua_tostring(state, -1);
    lua_pop(state, 1);
    return value;
}

bool ReadLuaNumberField(lua_State* state, int tableIndex, const char* fieldName, double& out)
{
    tableIndex = lua_absindex(state, tableIndex);
    lua_getfield(state, tableIndex, fieldName);
    const bool ok = lua_isnumber(state, -1);
    if (ok)
        out = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return ok;
}

LuaScriptInspectableFieldKind KindFromText(const std::string& value)
{
    const std::string kind = Lower(value);
    if (kind == "bool" || kind == "boolean")
        return LuaScriptInspectableFieldKind::Bool;
    if (kind == "int" || kind == "integer")
        return LuaScriptInspectableFieldKind::Int;
    if (kind == "float" || kind == "number" || kind == "double")
        return LuaScriptInspectableFieldKind::Float;
    if (kind == "string")
        return LuaScriptInspectableFieldKind::String;
    if (kind == "entity" || kind == "sceneentity" || kind == "scene_entity")
        return LuaScriptInspectableFieldKind::SceneEntityReference;
    if (kind == "component" || kind == "scenecomponent" || kind == "scene_component")
        return LuaScriptInspectableFieldKind::SceneComponentReference;
    if (kind == "asset" || kind == "projectasset" || kind == "project_asset")
        return LuaScriptInspectableFieldKind::ProjectAssetReference;
    return LuaScriptInspectableFieldKind::Unknown;
}

VansSerializedValue ReadLuaValue(lua_State* state, int valueIndex)
{
    valueIndex = lua_absindex(state, valueIndex);
    switch (lua_type(state, valueIndex))
    {
    case LUA_TBOOLEAN:
        return VansSerializedValue::Bool(lua_toboolean(state, valueIndex) != 0);
    case LUA_TNUMBER:
        if (lua_isinteger(state, valueIndex))
            return VansSerializedValue::Int(static_cast<std::int64_t>(lua_tointeger(state, valueIndex)));
        return VansSerializedValue::Float(static_cast<double>(lua_tonumber(state, valueIndex)));
    case LUA_TSTRING:
        return VansSerializedValue::String(lua_tostring(state, valueIndex));
    case LUA_TTABLE:
    {
        const lua_Integer length = luaL_len(state, valueIndex);
        if (length > 0)
        {
            std::vector<VansSerializedValue> items;
            items.reserve(static_cast<std::size_t>(length));
            for (lua_Integer i = 1; i <= length; ++i)
            {
                lua_geti(state, valueIndex, i);
                items.push_back(ReadLuaValue(state, -1));
                lua_pop(state, 1);
            }
            return VansSerializedValue::Array(std::move(items));
        }

        std::vector<std::pair<std::string, VansSerializedValue>> fields;
        lua_pushnil(state);
        while (lua_next(state, valueIndex) != 0)
        {
            if (lua_type(state, -2) == LUA_TSTRING)
                fields.emplace_back(lua_tostring(state, -2), ReadLuaValue(state, -1));
            lua_pop(state, 1);
        }
        return VansSerializedValue::Object(std::move(fields));
    }
    default:
        return VansSerializedValue::Null();
    }
}

LuaScriptFieldDescriptor ReadFieldDescriptor(
    lua_State* state,
    const std::string& fieldName,
    int fieldIndex)
{
    fieldIndex = lua_absindex(state, fieldIndex);

    LuaScriptFieldDescriptor descriptor;
    descriptor.name = fieldName;

    if (lua_istable(state, fieldIndex))
    {
        descriptor.kind = KindFromText(ReadLuaStringField(state, fieldIndex, "type"));
        if (descriptor.kind == LuaScriptInspectableFieldKind::Unknown)
            descriptor.kind = KindFromText(ReadLuaStringField(state, fieldIndex, "kind"));
        descriptor.componentType = ReadLuaStringField(state, fieldIndex, "component");
        if (descriptor.componentType.empty())
            descriptor.componentType = ReadLuaStringField(state, fieldIndex, "componentType");
        descriptor.assetType = ReadLuaStringField(state, fieldIndex, "asset");
        if (descriptor.assetType.empty())
            descriptor.assetType = ReadLuaStringField(state, fieldIndex, "assetType");

        lua_getfield(state, fieldIndex, "default");
        descriptor.defaultValue = ReadLuaValue(state, -1);
        lua_pop(state, 1);

        descriptor.hasMinValue = ReadLuaNumberField(state, fieldIndex, "min", descriptor.minValue);
        descriptor.hasMaxValue = ReadLuaNumberField(state, fieldIndex, "max", descriptor.maxValue);
        descriptor.hasSpeed = ReadLuaNumberField(state, fieldIndex, "speed", descriptor.speed);
    }
    else
    {
        descriptor.defaultValue = ReadLuaValue(state, fieldIndex);
        switch (descriptor.defaultValue.kind)
        {
        case VansSerializedValue::Kind::Bool:
            descriptor.kind = LuaScriptInspectableFieldKind::Bool;
            break;
        case VansSerializedValue::Kind::Int:
            descriptor.kind = LuaScriptInspectableFieldKind::Int;
            break;
        case VansSerializedValue::Kind::Float:
            descriptor.kind = LuaScriptInspectableFieldKind::Float;
            break;
        case VansSerializedValue::Kind::String:
            descriptor.kind = LuaScriptInspectableFieldKind::String;
            break;
        default:
            descriptor.kind = LuaScriptInspectableFieldKind::Unknown;
            break;
        }
    }

    if (descriptor.kind != LuaScriptInspectableFieldKind::Unknown &&
        descriptor.defaultValue.IsNull())
    {
        switch (descriptor.kind)
        {
        case LuaScriptInspectableFieldKind::Bool:
            descriptor.defaultValue = VansSerializedValue::Bool(false);
            break;
        case LuaScriptInspectableFieldKind::Int:
            descriptor.defaultValue = VansSerializedValue::Int(0);
            break;
        case LuaScriptInspectableFieldKind::Float:
            descriptor.defaultValue = VansSerializedValue::Float(0.0);
            break;
        case LuaScriptInspectableFieldKind::String:
            descriptor.defaultValue = VansSerializedValue::String("");
            break;
        case LuaScriptInspectableFieldKind::SceneEntityReference:
        case LuaScriptInspectableFieldKind::SceneComponentReference:
        case LuaScriptInspectableFieldKind::ProjectAssetReference:
            descriptor.defaultValue = VansSerializedValue::Object({});
            break;
        default:
            break;
        }
    }

    return descriptor;
}
}

LuaScriptFieldDefaultsResult VansLuaScriptInspectorService::BuildDefaultFieldData(
    const std::filesystem::path& projectRoot,
    const std::string& scriptPath,
    const std::string& entryName)
{
    LuaScriptFieldDefaultsResult result;
    if (scriptPath.empty())
    {
        result.message = "Lua script path is empty";
        return result;
    }

    std::filesystem::path absolutePath(scriptPath);
    if (!absolutePath.is_absolute())
        absolutePath = projectRoot / scriptPath;
    absolutePath = absolutePath.lexically_normal();

    LuaStatePtr state(luaL_newstate());
    if (!state)
    {
        result.message = "Cannot create Lua state";
        return result;
    }
    luaL_openlibs(state.get());
    InstallEditorLuaStubs(state.get());

    if (luaL_loadfile(state.get(), absolutePath.string().c_str()) != LUA_OK ||
        lua_pcall(state.get(), 0, 1, 0) != LUA_OK)
    {
        result.message = lua_tostring(state.get(), -1) ? lua_tostring(state.get(), -1) : "Cannot load Lua script";
        return result;
    }

    if (!lua_istable(state.get(), -1))
    {
        result.message = "Lua script must return a table";
        return result;
    }
    if (!entryName.empty())
    {
        lua_getfield(state.get(), -1, entryName.c_str());
        if (lua_istable(state.get(), -1))
            lua_remove(state.get(), -2);
        else
            lua_pop(state.get(), 1);
    }

    lua_getfield(state.get(), -1, "__fields");
    if (!lua_istable(state.get(), -1))
    {
        result.success = true;
        lua_pop(state.get(), 2);
        return result;
    }

    lua_pushnil(state.get());
    while (lua_next(state.get(), -2) != 0)
    {
        if (lua_type(state.get(), -2) == LUA_TSTRING)
        {
            LuaScriptFieldDescriptor descriptor =
                ReadFieldDescriptor(state.get(), lua_tostring(state.get(), -2), -1);
            if (!descriptor.name.empty())
            {
                if (!descriptor.defaultValue.IsNull())
                    result.fields.emplace_back(descriptor.name, descriptor.defaultValue);
                result.descriptors.push_back(std::move(descriptor));
            }
        }
        lua_pop(state.get(), 1);
    }

    lua_pop(state.get(), 2);
    result.success = true;
    return result;
}

const char* ToString(LuaScriptInspectableFieldKind kind)
{
    switch (kind)
    {
    case LuaScriptInspectableFieldKind::Bool: return "Bool";
    case LuaScriptInspectableFieldKind::Int: return "Int";
    case LuaScriptInspectableFieldKind::Float: return "Float";
    case LuaScriptInspectableFieldKind::String: return "String";
    case LuaScriptInspectableFieldKind::SceneEntityReference: return "SceneEntityReference";
    case LuaScriptInspectableFieldKind::SceneComponentReference: return "SceneComponentReference";
    case LuaScriptInspectableFieldKind::ProjectAssetReference: return "ProjectAssetReference";
    default: return "Unknown";
    }
}

bool HasLuaScriptFieldDefault(const LuaScriptFieldDescriptor& descriptor)
{
    return !descriptor.defaultValue.IsNull();
}

bool NormalizeLuaScriptFieldValue(
    VansSerializedValue& value,
    const LuaScriptFieldDescriptor& descriptor)
{
    bool changed = false;
    switch (descriptor.kind)
    {
    case LuaScriptInspectableFieldKind::Bool:
        if (value.kind != VansSerializedValue::Kind::Bool)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::Bool
                ? descriptor.defaultValue
                : VansSerializedValue::Bool(false);
            changed = true;
        }
        break;
    case LuaScriptInspectableFieldKind::Int:
        if (value.kind != VansSerializedValue::Kind::Int)
        {
            if (value.kind == VansSerializedValue::Kind::Float)
                value = VansSerializedValue::Int(static_cast<std::int64_t>(value.floatValue));
            else
                value = descriptor.defaultValue.kind == VansSerializedValue::Kind::Int
                    ? descriptor.defaultValue
                    : VansSerializedValue::Int(0);
            changed = true;
        }
        break;
    case LuaScriptInspectableFieldKind::Float:
        if (value.kind != VansSerializedValue::Kind::Float && value.kind != VansSerializedValue::Kind::Int)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::Float
                ? descriptor.defaultValue
                : VansSerializedValue::Float(0.0);
            changed = true;
        }
        break;
    case LuaScriptInspectableFieldKind::String:
        if (value.kind != VansSerializedValue::Kind::String)
        {
            value = descriptor.defaultValue.kind == VansSerializedValue::Kind::String
                ? descriptor.defaultValue
                : VansSerializedValue::String("");
            changed = true;
        }
        break;
    default:
        break;
    }
    return changed;
}
}
