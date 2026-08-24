#include "VansRenderBounds.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	glm::vec3 SafeAxis(const glm::vec3& v, const glm::vec3& fallback)
	{
		const float len = glm::length(v);
		return len > 1.0e-8f ? v / len : fallback;
	}

	glm::vec4 ClipRow(const glm::mat4& m, int row)
	{
		return glm::vec4(m[0][row], m[1][row], m[2][row], m[3][row]);
	}
}

VansGraphics::VansRenderBounds VansGraphics::MakeRenderBoundsFromLocalAABB(
	const glm::vec3& localMin,
	const glm::vec3& localMax,
	const glm::mat4& localToWorld)
{
	const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
	const glm::vec3 localHalfExtent = (localMax - localMin) * 0.5f;
	if (glm::any(glm::lessThan(localHalfExtent, glm::vec3(0.0f))))
		return VansRenderBounds{};
	return MakeRenderBoundsFromLocalOBB(
		localCenter,
		{ glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
		localHalfExtent,
		localToWorld);
}

VansGraphics::VansRenderBounds VansGraphics::MakeRenderBoundsFromLocalOBB(
	const glm::vec3& localCenter,
	const std::array<glm::vec3, 3>& localAxes,
	const glm::vec3& localHalfExtent,
	const glm::mat4& localToWorld)
{
	VansRenderBounds bounds;
	if (glm::any(glm::lessThan(localHalfExtent, glm::vec3(0.0f))))
		return bounds;

	const glm::vec3 worldCenter = glm::vec3(localToWorld * glm::vec4(localCenter, 1.0f));
	const glm::mat3 linear(localToWorld);
	const glm::vec3 basisX = linear * localAxes[0] * localHalfExtent.x;
	const glm::vec3 basisY = linear * localAxes[1] * localHalfExtent.y;
	const glm::vec3 basisZ = linear * localAxes[2] * localHalfExtent.z;

	bounds.obb.center = worldCenter;
	bounds.obb.axisX = SafeAxis(basisX, glm::vec3(1.0f, 0.0f, 0.0f));
	bounds.obb.axisY = SafeAxis(basisY, glm::vec3(0.0f, 1.0f, 0.0f));
	bounds.obb.axisZ = SafeAxis(basisZ, glm::vec3(0.0f, 0.0f, 1.0f));
	bounds.obb.halfExtent = glm::vec3(glm::length(basisX), glm::length(basisY), glm::length(basisZ));
	bounds.obb.valid = true;

	for (uint32_t corner = 0; corner < 8; ++corner)
	{
		const glm::vec3 world =
			worldCenter +
			((corner & 1u) ? basisX : -basisX) +
			((corner & 2u) ? basisY : -basisY) +
			((corner & 4u) ? basisZ : -basisZ);
		bounds.corners[corner] = world;
		bounds.aabb.min = glm::min(bounds.aabb.min, world);
		bounds.aabb.max = glm::max(bounds.aabb.max, world);
	}

	bounds.valid = bounds.obb.IsValid() && bounds.aabb.IsValid();
	return bounds;
}

bool VansGraphics::RenderBoundsChanged(
	const VansRenderBounds& a,
	const VansRenderBounds& b,
	float epsilon)
{
	if (a.IsValid() != b.IsValid())
		return true;
	if (!a.IsValid())
		return false;

	const glm::vec3 eps(epsilon);
	return glm::any(glm::greaterThan(glm::abs(a.obb.center - b.obb.center), eps)) ||
		glm::any(glm::greaterThan(glm::abs(a.obb.axisX - b.obb.axisX), eps)) ||
		glm::any(glm::greaterThan(glm::abs(a.obb.axisY - b.obb.axisY), eps)) ||
		glm::any(glm::greaterThan(glm::abs(a.obb.axisZ - b.obb.axisZ), eps)) ||
		glm::any(glm::greaterThan(glm::abs(a.obb.halfExtent - b.obb.halfExtent), eps));
}

bool VansGraphics::RenderBoundsIntersectsClipFrustum(
	const VansRenderBounds& bounds,
	const glm::mat4& worldToClip)
{
	if (!bounds.IsValid())
		return true;

	const glm::vec4 row0 = ClipRow(worldToClip, 0);
	const glm::vec4 row1 = ClipRow(worldToClip, 1);
	const glm::vec4 row2 = ClipRow(worldToClip, 2);
	const glm::vec4 row3 = ClipRow(worldToClip, 3);
	const glm::vec4 planes[6] = {
		row3 + row0,
		row3 - row0,
		row3 + row1,
		row3 - row1,
		row3 + row2,
		row3 - row2
	};

	const VansRenderOBB& obb = bounds.obb;
	for (const glm::vec4& plane : planes)
	{
		const glm::vec3 normal(plane);
		const float distance = glm::dot(normal, obb.center) + plane.w;
		const float projectedExtent =
			std::abs(glm::dot(normal, obb.axisX)) * obb.halfExtent.x +
			std::abs(glm::dot(normal, obb.axisY)) * obb.halfExtent.y +
			std::abs(glm::dot(normal, obb.axisZ)) * obb.halfExtent.z;
		if (distance + projectedExtent < 0.0f)
			return false;
	}
	return true;
}

bool VansGraphics::RenderAABBIntersectsClipFrustum(
	const glm::vec3& boundsMin,
	const glm::vec3& boundsMax,
	const glm::mat4& worldToClip)
{
	if (glm::any(glm::greaterThan(boundsMin, boundsMax)))
		return true;

	const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
	const glm::vec3 halfExtent = (boundsMax - boundsMin) * 0.5f;
	const glm::vec4 row0 = ClipRow(worldToClip, 0);
	const glm::vec4 row1 = ClipRow(worldToClip, 1);
	const glm::vec4 row2 = ClipRow(worldToClip, 2);
	const glm::vec4 row3 = ClipRow(worldToClip, 3);
	const glm::vec4 planes[6] = {
		row3 + row0,
		row3 - row0,
		row3 + row1,
		row3 - row1,
		row3 + row2,
		row3 - row2
	};

	for (const glm::vec4& plane : planes)
	{
		const glm::vec3 normal(plane);
		const float distance = glm::dot(normal, center) + plane.w;
		const float projectedExtent = glm::dot(glm::abs(normal), halfExtent);
		if (distance + projectedExtent < 0.0f)
			return false;
	}
	return true;
}

bool VansGraphics::ProjectRenderBoundsToScreen(
	const VansRenderBounds& bounds,
	const glm::mat4& view,
	const glm::mat4& projection,
	const glm::vec2& viewportSize,
	float nearPlane,
	VansProjectedBounds& projected)
{
	projected = {};
	if (!bounds.IsValid() || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
		return false;

	glm::vec2 uvMin(1.0f);
	glm::vec2 uvMax(0.0f);
	float nearestDepth = std::numeric_limits<float>::max();
	bool hasProjectedCorner = false;

	for (const glm::vec3& corner : bounds.corners)
	{
		const glm::vec4 viewPos = view * glm::vec4(corner, 1.0f);
		const float linearDepth = -viewPos.z;
		if (linearDepth <= std::max(nearPlane, 0.001f))
			return false;

		const glm::vec4 clip = projection * viewPos;
		if (clip.w <= 0.0001f)
			return false;

		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		const glm::vec2 uv(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
		uvMin = glm::min(uvMin, uv);
		uvMax = glm::max(uvMax, uv);
		nearestDepth = std::min(nearestDepth, linearDepth);
		hasProjectedCorner = true;
	}

	if (!hasProjectedCorner)
		return false;
	if (uvMax.x < 0.0f || uvMin.x > 1.0f || uvMax.y < 0.0f || uvMin.y > 1.0f)
		return false;

	projected.uvMin = glm::clamp(uvMin, glm::vec2(0.0f), glm::vec2(1.0f));
	projected.uvMax = glm::clamp(uvMax, glm::vec2(0.0f), glm::vec2(1.0f));
	projected.nearestLinearDepth = nearestDepth;
	projected.valid = projected.uvMax.x >= projected.uvMin.x && projected.uvMax.y >= projected.uvMin.y;
	return projected.valid;
}
