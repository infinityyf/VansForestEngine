#pragma once
#include "../VansParticleFieldRule.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"
#include <vector>

namespace VansGraphics
{
// 配置约束由 ParticleCore 定义；工具只消费 API 翻译后的快照。
class VansParticleAuthoringSchema
{
public:
    static const std::vector<VansParticleFieldRule>& Fields();
    static Vans::VansSerializedValue Defaults();
    static void ValidateFields(const Vans::VansSerializedValue& root);
};
}
