#pragma once
#include "../EngineAPILayer/Public/EngineDTOs.h"
#include <glm/glm.hpp>

namespace Vans
{
// 编辑器视口共用的坐标与取景计算，不改变运行时相机或渲染状态。
bool BuildEditorSceneRay(const glm::mat4& viewProjection, glm::vec2 normalizedPosition,
    EditorAPI::Ray& ray, float& length);
bool IntersectEditorBounds(const glm::vec3& origin, const glm::vec3& direction,
    const glm::vec3& minimum, const glm::vec3& maximum, float limit);
bool CalculateEditorFramePosition(const EditorAPI::EditorSceneBounds& bounds,
    glm::vec3 forward, float verticalFovDegrees, float aspect, float nearClip,
    glm::vec3& position, float& requiredFarClip);
}
