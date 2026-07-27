#include "VansShaderAuthoringAsset.h"

#include "Serialization/VansSerializedValueAccess.h"

#include <utility>

namespace Vans
{
bool ReadShaderAuthoringAsset(
    const VansSerializedValue& root,
    VansShaderAuthoringAsset& asset,
    std::string& error)
{
    if (root.kind != VansSerializedValue::Kind::Object)
    {
        error = "Shader asset root must be an object";
        return false;
    }

    VansShaderAuthoringAsset parsed;
    parsed.root = root;
    parsed.name = ReadSerializedStringField(root, "name");

    if (const VansSerializedValue* parameters = FindObjectField(root, "parameters"))
        parsed.parameters = *parameters;
    if (const VansSerializedValue* textures = FindObjectField(root, "textures"))
        parsed.textures = *textures;

    asset = std::move(parsed);
    return true;
}
}
