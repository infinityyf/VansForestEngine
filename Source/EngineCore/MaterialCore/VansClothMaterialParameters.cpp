#include "VansClothMaterialParameters.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include <cmath>
#include <set>

namespace Vans
{
namespace
{
struct ScalarField
{
    const char* name;
    float VansClothMaterialParameters::*member;
    float minimum;
    float maximum;
};
constexpr ScalarField scalars[] = {
    {"roughness", &VansClothMaterialParameters::roughness, 0.0f, 1.0f},
    {"ao", &VansClothMaterialParameters::ao, 0.0f, 1.0f},
    {"specularWeight", &VansClothMaterialParameters::specularWeight, 0.0f, 1.0f},
    {"sheenWeight", &VansClothMaterialParameters::sheenWeight, 0.0f, 1.0f},
    {"sheenRoughness", &VansClothMaterialParameters::sheenRoughness, 0.045f, 1.0f},
    {"anisotropy", &VansClothMaterialParameters::anisotropy, 0.0f, 0.95f},
    {"fiberRotationRadians", &VansClothMaterialParameters::fiberRotationRadians, -3.141592654f, 3.141592654f},
    {"alphaCutoff", &VansClothMaterialParameters::alphaCutoff, 0.0f, 1.0f}
};
struct ColorField
{
    const char* name;
    std::array<float, 3> VansClothMaterialParameters::*member;
};
constexpr ColorField colors[] = {
    {"albedo", &VansClothMaterialParameters::albedo},
    {"sheenColor", &VansClothMaterialParameters::sheenColor},
    {"diffuseTransmittance", &VansClothMaterialParameters::diffuseTransmittance}
};
bool ReadScalar(const VansSerializedValue& input, float minimum, float maximum, float& output)
{
    if (input.kind != VansSerializedValue::Kind::Float && input.kind != VansSerializedValue::Kind::Int)
        return false;
    const double number = ReadSerializedNumber(input, 0.0);
    const float scalar = static_cast<float>(number);
    if (!std::isfinite(number) || !std::isfinite(scalar) || scalar < minimum || scalar > maximum)
        return false;
    output = scalar;
    return true;
}
}

bool ApplyClothMaterialParameter(VansClothMaterialParameters& value, const std::string& name,
    const VansSerializedValue& input, std::string& error)
{
    error.clear();
    for (const auto& field : scalars)
    {
        if (name != field.name) continue;
        float scalar;
        if (!ReadScalar(input, field.minimum, field.maximum, scalar))
        { error = "Cloth " + name + " must be a finite number in its authoring range"; return false; }
        value.*(field.member) = scalar;
        return true;
    }
    for (const auto& field : colors)
    {
        if (name != field.name) continue;
        std::array<float, 3> color{};
        if (input.kind != VansSerializedValue::Kind::Array || input.arrayItems.size() != color.size())
        { error = "Cloth " + name + " must contain exactly three linear RGB values"; return false; }
        for (std::size_t i = 0; i < color.size(); ++i)
            if (!ReadScalar(input.arrayItems[i], 0.0f, 1.0f, color[i]))
            { error = "Cloth " + name + " RGB must be finite and between zero and one"; return false; }
        value.*(field.member) = color;
        return true;
    }
    if (name == "doubleSided")
    {
        if (input.kind != VansSerializedValue::Kind::Bool)
        { error = "Cloth doubleSided must be boolean"; return false; }
        value.doubleSided = input.boolValue;
        return true;
    }
    if (name == "alphaMode")
    {
        if (input.kind != VansSerializedValue::Kind::String ||
            (input.stringValue != "opaque" && input.stringValue != "mask"))
        { error = "Cloth alphaMode must be opaque or mask"; return false; }
        value.alphaMode = input.stringValue == "mask" ? VansClothAlphaMode::Mask : VansClothAlphaMode::Opaque;
        return true;
    }
    error = "Unknown Cloth parameter: " + name;
    return false;
}

bool ReadClothMaterialParameters(const VansSerializedValue& input,
    VansClothMaterialParameters& result, std::string& error)
{
    error.clear();
    if (input.kind != VansSerializedValue::Kind::Object)
    { error = "Cloth parameters must be an object"; return false; }
    VansClothMaterialParameters candidate;
    std::set<std::string> seen;
    for (const auto& [name, value] : input.objectFields)
    {
        if (!seen.insert(name).second)
        { error = "Duplicate Cloth parameter: " + name; return false; }
        if (!ApplyClothMaterialParameter(candidate, name, value, error)) return false;
    }
    // 缺省字段统一在此补全；失败不发布部分结果。
    result = candidate;
    return true;
}

VansSerializedValue WriteClothMaterialParameters(const VansClothMaterialParameters& value)
{
    auto result = VansSerializedValue::Object({});
    for (const auto& field : scalars)
        SetSerializedObjectField(result, field.name, VansSerializedValue::Float(value.*(field.member)));
    for (const auto& field : colors)
    {
        const auto& color = value.*(field.member);
        SetSerializedObjectField(result, field.name, VansSerializedValue::Array({
            VansSerializedValue::Float(color[0]), VansSerializedValue::Float(color[1]),
            VansSerializedValue::Float(color[2])}));
    }
    SetSerializedObjectField(result, "alphaMode", VansSerializedValue::String(
        value.alphaMode == VansClothAlphaMode::Mask ? "mask" : "opaque"));
    SetSerializedObjectField(result, "doubleSided", VansSerializedValue::Bool(value.doubleSided));
    return result;
}

bool IsClothTextureSlot(const std::string& slot)
{
    for (const char* candidate : VansClothTextureSlots)
        if (slot == candidate) return true;
    return false;
}

bool ValidateClothTextureImport(const std::string& slot,
    const VansClothTextureImportSettings& settings,
    std::string& error)
{
    error.clear();
    if (!IsClothTextureSlot(slot))
    {
        error = "Unknown Cloth texture slot: " + slot;
        return false;
    }
    if (settings.linear != (slot != "basecolor"))
    {
        error = "Cloth texture slot '" + slot + "' requires " +
            (slot == "basecolor" ? "sRGB color" : "linear data");
        return false;
    }
    const int channels = settings.channelCount;
    const int requiredChannels = slot == "basecolor" || slot == "normal" ? 3 :
        slot == "fiberDirection" ? 2 : 1;
    if ((channels != 1 && channels != 2 && channels != 4) || channels < requiredChannels)
    {
        error = "Cloth texture slot '" + slot + "' requires at least " +
            std::to_string(requiredChannels) + " channels in a supported 1/2/4-channel import";
        return false;
    }
    return true;
}

bool ValidateClothTextureImport(const std::string& slot, const VansAssetMeta& meta,
    std::string& error)
{
    VansTextureAssetImportSettings import;
    if (!VansAssetDatabase::ReadTextureImportSettings(meta, import, error)) return false;
    return ValidateClothTextureImport(slot,
        VansClothTextureImportSettings{import.linear, import.channelCount}, error);
}

bool GetClothMaterialScalarRange(const std::string& name, float& minimum, float& maximum)
{
    for (const auto& field : scalars)
    {
        if (name != field.name) continue;
        minimum = field.minimum;
        maximum = field.maximum;
        return true;
    }
    return false;
}

const std::array<VansClothMaterialPreset, 7>& GetClothMaterialPresets()
{
    static const auto presets = []
    {
        std::array<VansClothMaterialPreset, 7> result{};
        const auto set = [&](size_t index, const char* name, float roughness,
            float specular, float sheen, float sheenRoughness, float anisotropy, float tau)
        {
            auto& preset = result[index];
            preset.name = name;
            auto& p = preset.parameters;
            p.roughness = roughness;
            p.specularWeight = specular;
            p.sheenWeight = sheen;
            p.sheenRoughness = sheenRoughness;
            p.anisotropy = anisotropy;
            p.diffuseTransmittance = {tau, tau, tau};
        };
        set(0, "Cotton", .85f, .2f, .2f, .7f, .1f, 0);
        set(1, "Denim", .8f, .25f, .25f, .6f, .25f, 0);
        set(2, "Wool", .95f, .15f, .45f, .8f, .1f, 0);
        set(3, "Velvet", .8f, .15f, .8f, .45f, .2f, 0);
        set(4, "Satin", .25f, 1, .15f, .5f, .85f, 0);
        set(5, "Nylon", .4f, .65f, .12f, .55f, .35f, 0);
        set(6, "Thin linen", .9f, .15f, .2f, .7f, .2f, .2f);
        return result;
    }();
    return presets;
}
}
