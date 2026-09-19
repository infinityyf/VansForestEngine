#include "VansEditorSceneMath.h"
#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
bool BuildEditorSceneRay(const glm::mat4& viewProjection, glm::vec2 uv,
    EditorAPI::Ray& ray, float& length)
{
    ray = {}; length = 0;
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1)
        return false;
    const float determinant = glm::determinant(viewProjection);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) return false;
    const glm::mat4 inverse = glm::inverse(viewProjection);
    glm::vec4 a = inverse * glm::vec4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
    glm::vec4 b = inverse * glm::vec4(uv.x * 2 - 1, 1 - uv.y * 2, 1, 1);
    if (std::abs(a.w) < 1e-8f || std::abs(b.w) < 1e-8f) return false;
    a /= a.w; b /= b.w;
    const glm::vec3 delta(b - a);
    length = glm::length(delta);
    if (!Finite(glm::vec3(a)) || !Finite(delta) || !std::isfinite(length) || length <= 0) return false;
    const glm::vec3 direction = delta / length;
    ray.origin = {a.x, a.y, a.z}; ray.direction = {direction.x, direction.y, direction.z};
    return true;
}
bool IntersectEditorBounds(const glm::vec3& origin, const glm::vec3& direction,
    const glm::vec3& minimum, const glm::vec3& maximum, float limit)
{
    if (!Finite(origin) || !Finite(direction) || !Finite(minimum) || !Finite(maximum) ||
        !std::isfinite(limit) || limit <= 0 || glm::any(glm::greaterThan(minimum, maximum))) return false;
    float enter = 0, leave = limit;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1e-12f)
        {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
            continue;
        }
        float a = (minimum[axis] - origin[axis]) / direction[axis];
        float b = (maximum[axis] - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a); leave = std::min(leave, b);
        if (enter > leave) return false;
    }
    return true;
}
bool CalculateEditorFramePosition(const EditorAPI::EditorSceneBounds& bounds,
    glm::vec3 forward, float fov, float aspect, float nearClip,
    glm::vec3& position, float& requiredFarClip)
{
    const glm::vec3 lo(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
    const glm::vec3 hi(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
    if (!bounds.available || !Finite(lo) || !Finite(hi) || glm::any(glm::greaterThan(lo, hi)) ||
        !Finite(forward) || glm::length(forward) < 1e-6f || !std::isfinite(fov) || fov <= 0 || fov >= 179 ||
        !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(nearClip) || nearClip <= 0) return false;
    const float radius = std::max(glm::length((hi - lo) * .5f), .5f);
    const float vertical = glm::radians(fov) * .5f;
    const float horizontal = std::atan(std::tan(vertical) * aspect);
    const float distance = std::max(radius * 1.12f / std::sin(std::min(vertical, horizontal)), radius + nearClip * 2);
    position = (lo + hi) * .5f - glm::normalize(forward) * distance;
    requiredFarClip = distance + radius * 1.25f;
    return Finite(position) && std::isfinite(requiredFarClip);
}
}
