#pragma once

#include "Serialization/VansSerializedValue.h"

#include <string>

namespace Vans
{
struct VansShaderAuthoringAsset
{
    std::string name;
    VansSerializedValue root = VansSerializedValue::Object({});
    VansSerializedValue parameters = VansSerializedValue::Object({});
    VansSerializedValue textures = VansSerializedValue::Object({});
};

bool ReadShaderAuthoringAsset(
    const VansSerializedValue& root,
    VansShaderAuthoringAsset& asset,
    std::string& error);
}
