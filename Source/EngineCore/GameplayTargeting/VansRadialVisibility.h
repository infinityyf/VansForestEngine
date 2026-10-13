#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Vans
{
struct VansRadialVisibilityProbe
{
    float left = 0, up = 0, weight = 0;
};

struct VansRadialVisibilityTrace
{
    glm::vec3 end{0};
    bool clear = false;
};

// A blocked center can see a clipped peripheral point. The trace callback owns
// collision policy and returns the actual end, including an early obstruction.
template<class Trace>
float VansEvaluateRadialVisibility(const glm::vec3& origin, const glm::vec3& eye,
    const std::vector<VansRadialVisibilityProbe>& probes, Trace&& trace)
{
    if (trace(origin, eye).clear) return 1;
    const glm::vec3 delta = eye-origin;
    const float length = glm::length(delta);
    if (!(length > 0) || !std::isfinite(length)) return 0;
    const glm::vec3 forward = delta/length;
    const float horizontal = glm::length(glm::vec2(forward.x, forward.z));
    const glm::vec3 left = horizontal > 0
        ? glm::vec3(forward.z/horizontal, 0, -forward.x/horizontal)
        : glm::vec3(0, 0, -1);
    const glm::vec3 up = glm::cross(forward, left);
    float result = 0;
    for (const auto& probe : probes)
    {
        const glm::vec3 point = origin+left*probe.left+up*probe.up;
        const auto projected = trace(origin, point);
        if (trace(eye, projected.end).clear) result += probe.weight;
    }
    return std::clamp(result, 0.f, 1.f);
}
}
