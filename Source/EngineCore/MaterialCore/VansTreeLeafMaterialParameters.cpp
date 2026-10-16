#include "VansTreeLeafMaterialParameters.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans
{
namespace
{
struct ScalarField
{
    const char* name;
    float VansTreeLeafMaterialParameters::*member;
    float minimum, maximum;
};
constexpr ScalarField fields[] = {
    {"transmission", &VansTreeLeafMaterialParameters::transmission, 0.0f, 1.0f},
    {"wrap", &VansTreeLeafMaterialParameters::wrap, 0.0f, 0.9f},
    {"scatterRoughness", &VansTreeLeafMaterialParameters::scatterRoughness, 0.05f, 1.0f},
    {"specularScale", &VansTreeLeafMaterialParameters::specularScale, 0.0f, std::numeric_limits<float>::max()},
    {"alphaClip", &VansTreeLeafMaterialParameters::alphaClip, 0.0f, 1.0f}
};

bool ReadScalar(const VansSerializedValue& input, float& output)
{
    if (input.kind != VansSerializedValue::Kind::Float && input.kind != VansSerializedValue::Kind::Int)
        return false;
    const double value = ReadSerializedNumber(input, 0.0);
    const float scalar = static_cast<float>(value);
    if (!std::isfinite(value) || !std::isfinite(scalar)) return false;
    output = scalar;
    return true;
}
}

bool ApplyTreeLeafMaterialParameter(VansTreeLeafMaterialParameters& value, const std::string& name,
    const VansSerializedValue& input, std::string& error)
{
    error.clear();
    if (name == "subsurfaceColor")
    {
        std::array<float, 3> color{};
        if (input.kind == VansSerializedValue::Kind::Array && input.arrayItems.size() == color.size())
        {
            bool valid = true;
            for (size_t i = 0; i < color.size(); ++i)
                valid &= ReadScalar(input.arrayItems[i], color[i]);
            if (valid)
            {
                for (float& channel : color) channel = std::max(channel, 0.0f);
                value.subsurfaceColor = color;
                return true;
            }
        }
        error = "Tree leaf subsurfaceColor must contain three finite linear RGB values";
        return false;
    }
    for (const auto& field : fields)
    {
        if (name != field.name) continue;
        float scalar;
        if (!ReadScalar(input, scalar))
        { error = "Tree leaf " + name + " must be a finite number"; return false; }
        // Preserve the scene loader's existing clamped author ranges.
        value.*(field.member) = std::clamp(scalar, field.minimum, field.maximum);
        return true;
    }
    error = "Unknown tree leaf parameter: " + name;
    return false;
}

bool ReadTreeLeafMaterialParameters(const VansSerializedValue& input,
    VansTreeLeafMaterialParameters& result, std::string& error)
{
    if (input.kind != VansSerializedValue::Kind::Object)
    { error = "Tree leaf parameters must be an object"; return false; }
    VansTreeLeafMaterialParameters candidate;
    for (const auto& [name, value] : input.objectFields)
        if (!ApplyTreeLeafMaterialParameter(candidate, name, value, error)) return false;
    result = candidate;
    error.clear();
    return true;
}

VansSerializedValue WriteTreeLeafMaterialParameters(const VansTreeLeafMaterialParameters& value)
{
    auto result = VansSerializedValue::Object({{"subsurfaceColor", VansSerializedValue::Array({
        VansSerializedValue::Float(value.subsurfaceColor[0]),
        VansSerializedValue::Float(value.subsurfaceColor[1]),
        VansSerializedValue::Float(value.subsurfaceColor[2])})}});
    for (const auto& field : fields)
        SetSerializedObjectField(result, field.name, VansSerializedValue::Float(value.*(field.member)));
    return result;
}
}
