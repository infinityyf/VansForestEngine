#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Vans
{
enum class PythonScriptInspectableFieldKind
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

struct PythonScriptFieldDescriptor
{
    std::string name;
    PythonScriptInspectableFieldKind kind = PythonScriptInspectableFieldKind::Unknown;
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

struct PythonScriptFieldDefaultsResult
{
    bool success = false;
    std::string message;
    std::vector<std::pair<std::string, VansSerializedValue>> fields;
    std::vector<PythonScriptFieldDescriptor> descriptors;

    explicit operator bool() const { return success; }
};

class VansPythonScriptInspectorService
{
public:
    static PythonScriptFieldDefaultsResult BuildDefaultFieldData(
        const std::filesystem::path& projectRoot,
        const std::string& scriptPath,
        const std::string& className);
};

const char* ToString(PythonScriptInspectableFieldKind kind);
bool HasPythonScriptFieldDefault(const PythonScriptFieldDescriptor& descriptor);
bool NormalizePythonScriptFieldValue(
    VansSerializedValue& value,
    const PythonScriptFieldDescriptor& descriptor);
}
