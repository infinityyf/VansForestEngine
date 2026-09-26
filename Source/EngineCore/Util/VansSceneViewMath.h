#pragma once

#include <glm/glm.hpp>

namespace Vans
{
// 视口反投影、包围盒相交与取景的无状态数学，不依赖 Editor DTO 或运行时对象。
class VansSceneViewMath final
{
  public:
	static bool BuildRay(
		const glm::mat4& viewProjection,
		glm::vec2 normalizedPosition,
		glm::vec3& origin,
		glm::vec3& direction,
		float& length);

	static bool IntersectBounds(
		const glm::vec3& origin,
		const glm::vec3& direction,
		const glm::vec3& minimum,
		const glm::vec3& maximum,
		float limit);

	static bool CalculateFramePosition(
		bool boundsAvailable,
		const glm::vec3& minimum,
		const glm::vec3& maximum,
		glm::vec3 forward,
		float verticalFovDegrees,
		float aspect,
		float nearClip,
		glm::vec3& position,
		float& requiredFarClip);
};
} // namespace Vans
