#pragma once
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include <string>

namespace Vans
{
// 作者值只有一份定义；GPU 打包不属于持久化合同。
struct VansHairMaterialParameters
{
    float longitudinalRoughness = 0.3f;
    float azimuthalRoughness = 0.3f;
    float cuticleTiltDegrees = 2.0f;
    float normalScale = 1.0f;
    float aoStrength = 1.0f;
    float coverageCutoff = 0.0f;
    float coverageScale = 1.0f;
    float flowStrength = 0.0f;
    bool castShadows = true;
};

bool ReadHairMaterialParameters(const VansSerializedValue& value,
    VansHairMaterialParameters& result, std::string& error);
VansSerializedValue WriteHairMaterialParameters(const VansHairMaterialParameters& value);
bool ApplyHairMaterialParameter(VansHairMaterialParameters& value, const std::string& name,
    const VansSerializedValue& input, std::string& error);
bool IsHairTextureSlot(const std::string& slot);
}
