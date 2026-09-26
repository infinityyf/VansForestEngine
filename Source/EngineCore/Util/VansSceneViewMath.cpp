#include "VansSceneViewMath.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
bool IsFinite(glm::vec3 value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

bool VansSceneViewMath::BuildRay(
	const glm::mat4& viewProjection,
	glm::vec2 normalizedPosition,
	glm::vec3& origin,
	glm::vec3& direction,
	float& length)
{
	origin = {};
	direction = {};
	length = 0.0f;
	if (!std::isfinite(normalizedPosition.x) || !std::isfinite(normalizedPosition.y) ||
		normalizedPosition.x < 0.0f || normalizedPosition.y < 0.0f ||
		normalizedPosition.x > 1.0f || normalizedPosition.y > 1.0f)
	{
		return false;
	}

	const float determinant = glm::determinant(viewProjection);
	if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f)
		return false;

	const glm::mat4 inverse = glm::inverse(viewProjection);
	glm::vec4 nearPoint = inverse * glm::vec4(
		normalizedPosition.x * 2.0f - 1.0f,
		1.0f - normalizedPosition.y * 2.0f,
		0.0f,
		1.0f);
	glm::vec4 farPoint = inverse * glm::vec4(
		normalizedPosition.x * 2.0f - 1.0f,
		1.0f - normalizedPosition.y * 2.0f,
		1.0f,
		1.0f);
	if (std::abs(nearPoint.w) < 1e-8f || std::abs(farPoint.w) < 1e-8f)
		return false;

	nearPoint /= nearPoint.w;
	farPoint /= farPoint.w;
	const glm::vec3 delta(farPoint - nearPoint);
	length = glm::length(delta);
	if (!IsFinite(glm::vec3(nearPoint)) || !IsFinite(delta) ||
		!std::isfinite(length) || length <= 0.0f)
	{
		origin = {};
		length = 0.0f;
		return false;
	}

	origin = glm::vec3(nearPoint);
	direction = delta / length;
	return true;
}

bool VansSceneViewMath::IntersectBounds(
	const glm::vec3& origin,
	const glm::vec3& direction,
	const glm::vec3& minimum,
	const glm::vec3& maximum,
	float limit)
{
	if (!IsFinite(origin) || !IsFinite(direction) || !IsFinite(minimum) ||
		!IsFinite(maximum) || !std::isfinite(limit) || limit <= 0.0f ||
		glm::any(glm::greaterThan(minimum, maximum)))
	{
		return false;
	}

	float enter = 0.0f;
	float leave = limit;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (std::abs(direction[axis]) < 1e-12f)
		{
			if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
				return false;
			continue;
		}

		float first = (minimum[axis] - origin[axis]) / direction[axis];
		float second = (maximum[axis] - origin[axis]) / direction[axis];
		if (first > second)
			std::swap(first, second);
		enter = std::max(enter, first);
		leave = std::min(leave, second);
		if (enter > leave)
			return false;
	}
	return true;
}

bool VansSceneViewMath::CalculateFramePosition(
	bool boundsAvailable,
	const glm::vec3& minimum,
	const glm::vec3& maximum,
	glm::vec3 forward,
	float verticalFovDegrees,
	float aspect,
	float nearClip,
	glm::vec3& position,
	float& requiredFarClip)
{
	if (!boundsAvailable || !IsFinite(minimum) || !IsFinite(maximum) ||
		glm::any(glm::greaterThan(minimum, maximum)) || !IsFinite(forward) ||
		glm::length(forward) < 1e-6f || !std::isfinite(verticalFovDegrees) ||
		verticalFovDegrees <= 0.0f || verticalFovDegrees >= 179.0f ||
		!std::isfinite(aspect) || aspect <= 0.0f ||
		!std::isfinite(nearClip) || nearClip <= 0.0f)
	{
		return false;
	}

	const float radius = std::max(glm::length((maximum - minimum) * 0.5f), 0.5f);
	const float vertical = glm::radians(verticalFovDegrees) * 0.5f;
	const float horizontal = std::atan(std::tan(vertical) * aspect);
	const float distance = std::max(
		radius * 1.12f / std::sin(std::min(vertical, horizontal)),
		radius + nearClip * 2.0f);
	position = (minimum + maximum) * 0.5f - glm::normalize(forward) * distance;
	requiredFarClip = distance + radius * 1.25f;
	return IsFinite(position) && std::isfinite(requiredFarClip);
}
} // namespace Vans
