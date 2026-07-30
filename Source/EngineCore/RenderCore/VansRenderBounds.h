#pragma once

#include "../ScriptCore/VansCommonUtils.h"

#include <array>
#include <cstdint>
#include <limits>

namespace VansGraphics
{
	struct VansRenderOBB
	{
		glm::vec3 center = glm::vec3(0.0f);
		glm::vec3 axisX = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 axisY = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 axisZ = glm::vec3(0.0f, 0.0f, 1.0f);
		glm::vec3 halfExtent = glm::vec3(0.0f);
		bool valid = false;

		bool IsValid() const { return valid && glm::all(glm::greaterThanEqual(halfExtent, glm::vec3(0.0f))); }
	};

	struct VansProjectedBounds
	{
		glm::vec2 uvMin = glm::vec2(0.0f);
		glm::vec2 uvMax = glm::vec2(0.0f);
		float nearestLinearDepth = 0.0f;
		bool valid = false;
	};

	struct VansRenderAABB
	{
		glm::vec3 min = glm::vec3((std::numeric_limits<float>::max)());
		glm::vec3 max = glm::vec3(-(std::numeric_limits<float>::max)());

		bool IsValid() const
		{
			return min.x <= max.x && min.y <= max.y && min.z <= max.z;
		}
	};

	struct VansRenderBounds
	{
		VansRenderOBB obb;
		VansRenderAABB aabb;
		std::array<glm::vec3, 8> corners{};
		bool valid = false;

		bool IsValid() const { return valid && obb.IsValid() && aabb.IsValid(); }
	};

	VansRenderBounds MakeRenderBoundsFromLocalAABB(
		const glm::vec3& localMin,
		const glm::vec3& localMax,
		const glm::mat4& localToWorld);

	VansRenderBounds MakeRenderBoundsFromLocalOBB(
		const glm::vec3& localCenter,
		const std::array<glm::vec3, 3>& localAxes,
		const glm::vec3& localHalfExtent,
		const glm::mat4& localToWorld);

	bool RenderBoundsChanged(
		const VansRenderBounds& a,
		const VansRenderBounds& b,
		float epsilon = 1.0e-4f);

	bool RenderBoundsIntersectsClipFrustum(
		const VansRenderBounds& bounds,
		const glm::mat4& worldToClip);

	bool ProjectRenderBoundsToScreen(
		const VansRenderBounds& bounds,
		const glm::mat4& view,
		const glm::mat4& projection,
		const glm::vec2& viewportSize,
		float nearPlane,
		VansProjectedBounds& projected);
}
