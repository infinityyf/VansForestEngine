#include "VansHairMaterialParameters.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include <cmath>
#include <algorithm>

namespace Vans
{
namespace
{
struct ScalarField
{
    const char* name;
    float VansHairMaterialParameters::*member;
    float minimum;
    float maximum;
};
constexpr ScalarField fields[] = {
    {"longitudinalRoughness", &VansHairMaterialParameters::longitudinalRoughness, 0.035f, 1.0f},
    {"azimuthalRoughness", &VansHairMaterialParameters::azimuthalRoughness, 0.035f, 1.0f},
    {"cuticleTiltDegrees", &VansHairMaterialParameters::cuticleTiltDegrees, -10.0f, 10.0f},
    {"normalScale", &VansHairMaterialParameters::normalScale, 0.0f, 2.0f},
    {"aoStrength", &VansHairMaterialParameters::aoStrength, 0.0f, 1.0f},
    {"coverageCutoff", &VansHairMaterialParameters::coverageCutoff, 0.0f, 0.99f},
    {"coverageScale", &VansHairMaterialParameters::coverageScale, 0.01f, 16.0f},
    {"flowStrength", &VansHairMaterialParameters::flowStrength, 0.0f, 1.0f}
};
}
bool ApplyHairMaterialParameter(VansHairMaterialParameters& value, const std::string& name,
    const VansSerializedValue& input, std::string& error)
{
    if (name == "castShadows")
    {
        if (input.kind != VansSerializedValue::Kind::Bool)
        { error = "Hair castShadows must be boolean"; return false; }
        value.castShadows = input.boolValue;
        return true;
    }
    for (const auto& field : fields)
    {
        if (name != field.name) continue;
        if (input.kind != VansSerializedValue::Kind::Float && input.kind != VansSerializedValue::Kind::Int)
        { error = "Hair " + name + " must be numeric"; return false; }
        const double number = ReadSerializedNumber(input, 0.0);
        // 按运行期 float 精度比较边界，避免合法十进制 0.035 小于 0.035f。
        const float scalar = static_cast<float>(number);
        if (!std::isfinite(number) || !std::isfinite(scalar) || scalar < field.minimum || scalar > field.maximum)
        { error = "Hair " + name + " is outside its finite authoring range"; return false; }
        value.*(field.member) = scalar;
        return true;
    }
    error = "Unknown Hair parameter: " + name;
    return false;
}
bool ReadHairMaterialParameters(const VansSerializedValue& input,
    VansHairMaterialParameters& result, std::string& error)
{
    if (input.kind != VansSerializedValue::Kind::Object)
    { error = "Hair parameters must be an object"; return false; }
    VansHairMaterialParameters candidate;
    for (const auto& entry : input.objectFields)
        if (!ApplyHairMaterialParameter(candidate, entry.first, entry.second, error)) return false;
    for (const auto& field : fields)
        if (!FindObjectField(input, field.name))
        { error = std::string("Missing Hair parameter: ") + field.name; return false; }
    if (!FindObjectField(input, "castShadows") || input.objectFields.size() != std::size(fields) + 1)
    { error = "Hair parameters must contain every current field exactly once"; return false; }
    result = candidate;
    return true;
}
VansSerializedValue WriteHairMaterialParameters(const VansHairMaterialParameters& value)
{
    auto result = VansSerializedValue::Object({});
    for (const auto& field : fields)
        SetSerializedObjectField(result, field.name, VansSerializedValue::Float(value.*(field.member)));
    SetSerializedObjectField(result, "castShadows", VansSerializedValue::Bool(value.castShadows));
    return result;
}
bool IsHairTextureSlot(const std::string& slot)
{
    return slot == "basecolor" || slot == "alpha" || slot == "normal" ||
        slot == "roughness" || slot == "ao" || slot == "flow";
}
}
