#pragma once
#include "VansParticleFrameData.h"
#include "../RuntimeCore/VansGenerationPool.h"
#include <string>

namespace VansGraphics
{
// 按需复制已发布的帧，不暴露模拟池或渲染资源；上限只影响调试显示。
struct VansParticleDebugEmitter
{
    Vans::VansGenerationHandle instance;
    std::uint32_t emitterIndex = 0;
    std::string effectName, emitterName;
    std::vector<VansParticleRibbonStrip> ribbons;
};
struct VansParticleDebugSnapshot
{
    static constexpr std::size_t MaxPoints = 4096, MaxEmitters = 256;
    std::size_t totalPoints = 0, capturedPoints = 0, totalEmitters = 0;
    bool truncated = false;
    std::vector<VansParticleDebugEmitter> emitters;
};
}
