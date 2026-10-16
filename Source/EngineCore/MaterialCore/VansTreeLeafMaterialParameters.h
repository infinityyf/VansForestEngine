#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include <array>
#include <string>

namespace Vans
{
// Shared author values for scene loading and live tree-material previews.
struct VansTreeLeafMaterialParameters
{
    std::array<float, 3> subsurfaceColor{0.70f, 1.00f, 0.32f};
    float transmission = 0.75f;
    float wrap = 0.50f;
    float scatterRoughness = 0.60f;
    float specularScale = 0.45f;
    float alphaClip = 0.50f;
};

bool ReadTreeLeafMaterialParameters(const VansSerializedValue& input,
    VansTreeLeafMaterialParameters& result, std::string& error);
VansSerializedValue WriteTreeLeafMaterialParameters(const VansTreeLeafMaterialParameters& value);
bool ApplyTreeLeafMaterialParameter(VansTreeLeafMaterialParameters& value, const std::string& name,
    const VansSerializedValue& input, std::string& error);
}
