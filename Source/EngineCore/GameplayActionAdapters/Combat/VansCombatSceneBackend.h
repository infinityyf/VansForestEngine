#pragma once
#include "../../GameplayTargeting/VansSurfaceImpact.h"
#include <glm/glm.hpp>
#include <functional>

namespace Vans
{
// Scene 提供当前输出视图和静态表面查询；Combat 不读取编辑器或渲染资源。
struct VansCombatSceneBackend
{
    std::function<bool(glm::vec3&, glm::vec3&)> viewRay;
    std::function<bool(const VansSurfaceQueryRequest&, VansSurfaceImpact&, std::string&)> querySurface;
};
}
