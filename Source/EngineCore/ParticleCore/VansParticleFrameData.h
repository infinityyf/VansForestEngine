#pragma once
#include "VansParticleInstanceData.h"
#include <vector>
#include <cstdint>

namespace VansGraphics
{
struct VansParticleRibbonPoint
{
    glm::vec3 position{0.0f};
    float width = 0.0f;
    glm::vec4 color{1.0f};
    float u = 0.0f;
    uint64_t sequence = 0;
};
struct VansParticleRibbonStrip
{
    uint64_t ribbonId = 0;
    bool hasSourceRoot = false;
    std::vector<VansParticleRibbonPoint> points;
};
struct VansParticleEmitterRange
{
    uint32_t emitterIndex = 0;
    uint32_t surfaceFirst = 0, surfaceCount = 0;
    uint32_t mediumFirst = 0, mediumCount = 0;
    uint32_t ribbonFirst = 0, ribbonCount = 0;
};
// 连续载荷只存一份，范围将输出绑定到不可变定义中的发射器。
struct VansParticleFrameData
{
    std::vector<VansParticleInstanceData> instances;
    std::vector<VansVolumetricParticleInstanceData> medium;
    std::vector<VansParticleRibbonStrip> ribbons;
    std::vector<VansParticleEmitterRange> emitters;
};
}
