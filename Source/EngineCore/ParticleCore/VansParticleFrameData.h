#pragma once
#include "VansParticleInstanceData.h"
#include "VansParticleSortPolicy.h"
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
    // ParticleCore 已完成断带、根部附着、宽度、颜色和 UV 计算；点按 sequence
    // 严格递减。RenderCore 只能依据当前视图把该不可变点串展开为三角形带。
    std::vector<VansParticleRibbonPoint> points;
};
struct VansParticleEmitterRange
{
    uint32_t emitterIndex = 0;
    VansParticleRenderSortMode renderSortMode = VansParticleRenderSortMode::None;
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
