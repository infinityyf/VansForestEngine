#pragma once

#include "Serialization/VansSerializedValue.h"

#include <string>

namespace Vans
{
struct VansMaterialAuthoringAsset
{
    std::string guid;
    std::string materialType = "pbr";
    std::string preferredImportModel;
    VansSerializedValue importSource = VansSerializedValue::Null();
    VansSerializedValue shader = VansSerializedValue::Null();
    VansSerializedValue shaderPasses = VansSerializedValue::Null();
    VansSerializedValue textures = VansSerializedValue::Null();
    VansSerializedValue parameters = VansSerializedValue::Object({});
    VansSerializedValue customParameters = VansSerializedValue::Null();
    VansSerializedValue customTextures = VansSerializedValue::Null();
};

const char* DefaultMaterialAuthoringType();
std::string MaterialAuthoringTypeOrDefault(const std::string& materialType);
bool IsCustomShaderMaterialAuthoringType(const std::string& materialType);
bool IsTransparentMaterialAuthoringType(const std::string& materialType);

VansSerializedValue CreatePbrMaterialAuthoringParameters(
    float albedoR,
    float albedoG,
    float albedoB,
    float metallic,
    float roughness,
    float ao);

bool ReadMaterialAuthoringAsset(
    const VansSerializedValue& root,
    VansMaterialAuthoringAsset& asset,
    std::string& error);

VansSerializedValue WriteMaterialAuthoringAssetRoot(const VansMaterialAuthoringAsset& asset);
}
