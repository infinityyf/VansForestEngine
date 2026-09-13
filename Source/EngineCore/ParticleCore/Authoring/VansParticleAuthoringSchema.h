#pragma once
#include "../VansParticleJson.h"
#include <string>
#include <vector>

namespace VansGraphics
{
struct VansParticleAuthoringField
{
    std::string pathPattern;
    std::vector<std::string> choices;
    bool hasLimits = false;
    double minimum = 0, maximum = 0, step = 0.01;
};

// 配置约束由 ParticleCore 定义；工具只消费 API 翻译后的快照。
class VansParticleAuthoringSchema
{
public:
    static const std::vector<VansParticleAuthoringField>& Fields();
    static Vans::ParticleJson Defaults();
    static void ValidateFields(const Vans::ParticleJson& root);
};
}
