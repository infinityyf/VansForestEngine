#pragma once

#include <../../GLM/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Vans
{
// 纯空间判定独立于 PhysX 查询，便于行为测试和后续替换感知后端。
inline bool IsTargetInsideAIVisionCone(
	const glm::vec3& observerPosition,
	const glm::vec3& observerVisualForward,
	const glm::vec3& targetPosition,
	float range,
	float horizontalFovDegrees)
{
	glm::vec3 toTarget = targetPosition - observerPosition;
	toTarget.y = 0.0f;
	const float distance = glm::length(toTarget);
	if (!std::isfinite(distance) || distance > (std::max)(range, 0.0f))
		return false;
	if (distance <= 1.0e-4f)
		return true;

	glm::vec3 forward = observerVisualForward;
	forward.y = 0.0f;
	const float forwardLength = glm::length(forward);
	if (!std::isfinite(forwardLength) || forwardLength <= 1.0e-4f)
		return false;

	const float halfFov = glm::radians(glm::clamp(
		horizontalFovDegrees, 0.0f, 360.0f) * 0.5f);
	const float minimumDot = std::cos(halfFov);
	return glm::dot(forward / forwardLength, toTarget / distance) >= minimumDot;
}

// 导航切线只决定移动；追击时的视觉正面始终指向实时目标。
inline glm::vec3 ResolveAIChaseFacingDirection(
	const glm::vec3& agentPosition,
	const glm::vec3& targetPosition,
	const glm::vec3& routeFallbackDirection)
{
	glm::vec3 toTarget(
		targetPosition.x - agentPosition.x,
		0.0f,
		targetPosition.z - agentPosition.z);
	const float targetLength = glm::length(toTarget);
	if (std::isfinite(targetLength) && targetLength > 1.0e-4f)
		return toTarget / targetLength;

	glm::vec3 fallback(routeFallbackDirection.x, 0.0f, routeFallbackDirection.z);
	const float fallbackLength = glm::length(fallback);
	return std::isfinite(fallbackLength) && fallbackLength > 1.0e-4f
		? fallback / fallbackLength
		: glm::vec3(0.0f, 0.0f, 1.0f);
}

// 将世界空间 +Z 前向转换成 Transform 的绝对 Yaw。这个绝对朝向同时供
// 视觉正面和视野判定使用，不能为了改变转弯弧线而镜像目标方向。
inline float ResolveAIVisualFacingYawDegrees(const glm::vec3& direction)
{
	return glm::degrees(std::atan2(direction.x, direction.z));
}
}
