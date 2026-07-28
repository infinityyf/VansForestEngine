#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Vans
{
enum class LuaScriptInspectableFieldKind
{
    Unknown,
    Bool,
    Int,
    Float,
    String,
    SceneEntityReference,
    SceneComponentReference,
    ProjectAssetReference
};

struct LuaScriptFieldDescriptor
{
    std::string name;
    LuaScriptInspectableFieldKind kind = LuaScriptInspectableFieldKind::Unknown;
    std::string componentType;
    std::string assetType;
    VansSerializedValue defaultValue;
    bool hasMinValue = false;
    bool hasMaxValue = false;
    bool hasSpeed = false;
    double minValue = 0.0;
    double maxValue = 0.0;
    double speed = 0.05;
};

struct LuaScriptFieldDefaultsResult
{
    bool success = false;
    std::string message;
    std::vector<std::pair<std::string, VansSerializedValue>> fields;
    std::vector<LuaScriptFieldDescriptor> descriptors;

    explicit operator bool() const { return success; }
};

class VansLuaScriptInspectorService
{
public:
    static LuaScriptFieldDefaultsResult BuildDefaultFieldData(
        const std::filesystem::path& projectRoot,
        const std::string& scriptPath,
        const std::string& entryName);
};

const char* ToString(LuaScriptInspectableFieldKind kind);
bool HasLuaScriptFieldDefault(const LuaScriptFieldDescriptor& descriptor);
bool NormalizeLuaScriptFieldValue(
    VansSerializedValue& value,
    const LuaScriptFieldDescriptor& descriptor);
}
