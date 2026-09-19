#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace VansGraphics
{
    // 运行时可向上归并远场；16 个有效位可由现有 float 掩码精确传输。
    constexpr uint32_t GIWorldMaxLevels = 16;
    // 默认关闭。关闭状态不创建场、模板、描述符或工作队列。
    struct GIWorldSettings
    {
        bool enabled = false;
        float voxelSize = 0.25f;
        float coverageDistance = 256.0f; // 相机细节层半径上限；最粗层覆盖所有已加载体素来源。
        float extinctionScale = 1.0f;
        uint32_t levelCount = 5; // 最少层数；远场超出预留容量时自动选择更粗父级。
        uint32_t maxBricks = 6144;
        uint32_t bricksPerFrame = 64;
        uint32_t maxTraceSteps = 256;
    };

    inline void NormalizeGIWorldSettings(GIWorldSettings& s)
    {
        s.voxelSize = std::isfinite(s.voxelSize) ? std::clamp(s.voxelSize, 0.125f, 4.0f) : 0.25f;
        s.coverageDistance = std::isfinite(s.coverageDistance) ? std::clamp(s.coverageDistance, 16.0f, 2048.0f) : 256.0f;
        s.extinctionScale = std::isfinite(s.extinctionScale) ? std::clamp(s.extinctionScale, 0.0f, 16.0f) : 1.0f;
        s.levelCount = std::clamp(s.levelCount, 1u, 6u);
        s.maxBricks = std::clamp(s.maxBricks, 64u, 16384u);
        s.bricksPerFrame = std::clamp(s.bricksPerFrame, 1u, 1024u);
        s.maxTraceSteps = std::clamp(s.maxTraceSteps, 32u, 4096u);
    }

    inline bool GIWorldResourceLayoutEquals(const GIWorldSettings& a, const GIWorldSettings& b)
    {
        if (a.enabled != b.enabled) return false;
        if (!a.enabled) return true;
        return a.voxelSize == b.voxelSize && a.coverageDistance == b.coverageDistance &&
            a.extinctionScale == b.extinctionScale && a.levelCount == b.levelCount &&
            a.maxBricks == b.maxBricks && a.bricksPerFrame == b.bricksPerFrame && a.maxTraceSteps == b.maxTraceSteps;
    }
}
