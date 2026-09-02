#include "VansSceneNavigationGeometry.h"

#include "../SceneCore/VansSceneObjectBuildPlan.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <array>
#include <cmath>

namespace Vans
{
namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
	return glm::vec3(value[0], value[1], value[2]);
}

void AppendBox(VansNavigationGeometry& output,
	const VansSceneTransformConfig& transform,
	const VansScenePhysicsNodeConfig& physics)
{
	const glm::vec3 scale = glm::abs(ToVec3(transform.scale));
	const glm::vec3 extents = physics.boxExtents
		? glm::max(ToVec3(*physics.boxExtents) * scale, glm::vec3(0.0001f))
		: glm::vec3(0.5f) * scale;
	const glm::vec3 offset = physics.colliderOffset
		? ToVec3(*physics.colliderOffset)
		: (physics.shapeOffset ? ToVec3(*physics.shapeOffset) : glm::vec3(0.0f));
	const glm::vec3 position = ToVec3(transform.position);
	const glm::vec3 rotation = glm::radians(ToVec3(transform.rotation));
	const glm::mat4 world = glm::translate(glm::mat4(1.0f), position)
		* glm::eulerAngleXYZ(rotation.x, rotation.y, rotation.z);
	const std::array<glm::vec3, 8> local = {
		offset + glm::vec3(-extents.x, -extents.y, -extents.z),
		offset + glm::vec3( extents.x, -extents.y, -extents.z),
		offset + glm::vec3( extents.x, -extents.y,  extents.z),
		offset + glm::vec3(-extents.x, -extents.y,  extents.z),
		offset + glm::vec3(-extents.x,  extents.y, -extents.z),
		offset + glm::vec3( extents.x,  extents.y, -extents.z),
		offset + glm::vec3( extents.x,  extents.y,  extents.z),
		offset + glm::vec3(-extents.x,  extents.y,  extents.z)
	};
	const int baseVertex = static_cast<int>(output.VertexCount());
	for (const glm::vec3& point : local)
	{
		const glm::vec3 p = glm::vec3(world * glm::vec4(point, 1.0f));
		output.vertices.push_back(p.x);
		output.vertices.push_back(p.y);
		output.vertices.push_back(p.z);
	}
	// 每个面保持面向外部；Recast 只会把坡度允许的三角形标为可行走。
	constexpr std::array<int, 36> boxIndices = {
		4, 7, 6, 4, 6, 5,
		0, 1, 2, 0, 2, 3,
		0, 4, 5, 0, 5, 1,
		1, 5, 6, 1, 6, 2,
		2, 6, 7, 2, 7, 3,
		3, 7, 4, 3, 4, 0
	};
	for (int index : boxIndices)
		output.indices.push_back(baseVertex + index);
}
}

VansNavigationGeometry VansSceneNavigationGeometry::BuildEnvironmentGeometry(
	const VansSceneObjectBuildPlan& sceneObjects)
{
	VansNavigationGeometry geometry;
	for (const VansSceneObjectBuildConfig& object : sceneObjects.objects)
	{
		if (!object.active || !object.transform || !object.physicsComponents.physics)
			continue;
		const VansScenePhysicsNodeConfig& physics = *object.physicsComponents.physics;
		if (!physics.enabled.value_or(true) || physics.isTrigger.value_or(false) ||
			physics.layer.value_or("Default") != "Environment" ||
			physics.colliderType.value_or("box") != "box")
			continue;
		AppendBox(geometry, *object.transform, physics);
	}
	return geometry;
}
}
