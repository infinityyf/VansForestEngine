#pragma once
#include <cmath>
#include <cstdint>
namespace Vans
{
struct VansSceneImpactDecalConfig
{
    uint32_t capacity = 0;
    float lifetimeSeconds = 30.0f;
    float diameter = 0.08f;
    float depth = 0.02f;
    float minimumNormalDot = 0.2f;
    bool IsValid() const
    {
        return capacity > 0 && capacity <= 256 && std::isfinite(lifetimeSeconds) && lifetimeSeconds > 0 && lifetimeSeconds <= 3600 &&
            std::isfinite(diameter) && diameter >= 0.001f && diameter <= 2 && std::isfinite(depth) && depth >= 0.001f && depth <= 0.1f &&
            std::isfinite(minimumNormalDot) && minimumNormalDot >= 0 && minimumNormalDot <= 1;
    }
};
}
