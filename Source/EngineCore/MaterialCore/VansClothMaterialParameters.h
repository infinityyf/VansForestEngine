#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include <array>
#include <string>

namespace Vans
{
enum class VansClothAlphaMode { Opaque, Mask };

// 作者值的唯一真值。公共 PBR 载荷和 Cloth 扩展均从本结构派生。
struct VansClothMaterialParameters
{
    std::array<float, 3> albedo{1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float ao = 1.0f;
    float specularWeight = 0.25f;
    std::array<float, 3> sheenColor{1.0f, 1.0f, 1.0f};
    float sheenWeight = 0.2f;
    float sheenRoughness = 0.6f;
    float anisotropy = 0.0f;
    float fiberRotationRadians = 0.0f;
    std::array<float, 3> diffuseTransmittance{0.0f, 0.0f, 0.0f};
    VansClothAlphaMode alphaMode = VansClothAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = true;
};

// 顺序对应 Cloth 自有 Set 4，不能改变公共 PBR 五槽合同。
inline constexpr std::array<const char*, 7> VansClothTextureSlots{
    "basecolor", "normal", "roughness", "ao", "sheenMask", "fiberDirection", "opacity"
};

bool ReadClothMaterialParameters(const VansSerializedValue& input,
    VansClothMaterialParameters& result, std::string& error);
VansSerializedValue WriteClothMaterialParameters(const VansClothMaterialParameters& value);
bool ApplyClothMaterialParameter(VansClothMaterialParameters& value, const std::string& name,
    const VansSerializedValue& input, std::string& error);
bool IsClothTextureSlot(const std::string& slot);
struct VansAssetMeta;
struct VansClothTextureImportSettings
{
    bool linear = true;
    int channelCount = 4;
};
// Validate the effective import settings before a texture enters a Cloth slot.
// Pixel content (normal handedness / doubled fiber angle) is an authoring contract.
bool ValidateClothTextureImport(const std::string& slot,
    const VansClothTextureImportSettings& settings, std::string& error);
bool ValidateClothTextureImport(const std::string& slot, const VansAssetMeta& meta,
    std::string& error);
bool GetClothMaterialScalarRange(const std::string& name, float& minimum, float& maximum);

struct VansClothMaterialPreset
{
    const char* name;
    VansClothMaterialParameters parameters;
};
// Authoring starters only. A selected preset is stored as ordinary canonical
// parameters; it never adds a second runtime model or a persistent preset key.
const std::array<VansClothMaterialPreset, 7>& GetClothMaterialPresets();
}
