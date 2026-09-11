#pragma once
#include <algorithm>
#include <cmath>
namespace VansGraphics
{
    struct VansSkyLightingFrame
    {
        float intensity = 1.0f;
        float radianceScale = 1.0f;
    };
    inline VansSkyLightingFrame BuildSkyLightingFrame(float intensity, float daylightScale)
    {
        intensity = std::isfinite(intensity) ? std::max(intensity, 0.0f) : 1.0f;
        daylightScale = std::isfinite(daylightScale) ? std::max(daylightScale, 0.0f) : 1.0f;
        // 静态天空只受天空强度和太阳高度/昼夜系数调节，不乘主光强度。
        return { intensity, intensity * daylightScale };
    }
}
