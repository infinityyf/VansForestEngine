#include "VansSceneNavigationGeometry.h"

#include "../SceneCore/VansSceneObjectBuildPlan.h"
#include "../SceneRuntime/Transform/VansTransform.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
	return glm::vec3(value[0], value[1], value[2]);
}

glm::mat4 ToLocalMatrix(const VansSceneTransformConfig& transform)
{
	VansTransform value;
	value.m_Position = ToVec3(transform.position);
	value.m_Rotation = ToVec3(transform.rotation);
	value.m_Scale = ToVec3(transform.scale);
	return value.GetModelMatrix();
}

std::string ObjectLabel(const VansSceneObjectBuildConfig& object)
{
	return object.name.empty() ? object.entityGuid : object.name;
}

enum class TransformResolveState : std::uint8_t
{
	Unresolved,
	Resolving,
	Resolved
};

class SceneTransformResolver
{
public:
	explicit SceneTransformResolver(const VansSceneObjectBuildPlan& plan)
		: m_Plan(plan),
		  m_States(plan.objects.size(), TransformResolveState::Unresolved),
		  m_WorldTransforms(plan.objects.size(), glm::mat4(1.0f)),
		  m_Active(plan.objects.size(), false)
	{
	}

	bool Initialize(std::string& error)
	{
		for (std::size_t index = 0; index < m_Plan.objects.size(); ++index)
		{
			const std::string& guid = m_Plan.objects[index].entityGuid;
			if (guid.empty()) continue;
			if (!m_Indices.emplace(guid, index).second)
			{
				error = "Navigation geometry contains duplicate entity GUID: " + guid;
				return false;
			}
		}
		return true;
	}

	bool Resolve(std::size_t index, glm::mat4& worldTransform,
		bool& hierarchyActive, std::string& error)
	{
		if (index >= m_Plan.objects.size())
		{
			error = "Navigation geometry transform index is invalid";
			return false;
		}
		if (m_States[index] == TransformResolveState::Resolved)
		{
			worldTransform = m_WorldTransforms[index];
			hierarchyActive = m_Active[index];
			return true;
		}
		const VansSceneObjectBuildConfig& object = m_Plan.objects[index];
		if (m_States[index] == TransformResolveState::Resolving)
		{
			error = "Navigation geometry parent cycle reaches entity '" +
				ObjectLabel(object) + "'";
			return false;
		}
		if (!object.transform)
		{
			error = "Navigation geometry parent chain entity '" +
				ObjectLabel(object) + "' has no Transform";
			return false;
		}

		m_States[index] = TransformResolveState::Resolving;
		glm::mat4 parentWorld(1.0f);
		bool parentActive = true;
		if (object.parent)
		{
			if (!object.parent->IsEntity())
			{
				error = "Navigation geometry cannot bake bone/socket parent for entity '" +
					ObjectLabel(object) + "'";
				m_States[index] = TransformResolveState::Unresolved;
				return false;
			}
			const std::string parentGuid = object.parent->entityGuid.ToString();
			const auto parent = m_Indices.find(parentGuid);
			if (parent == m_Indices.end())
			{
				error = "Navigation geometry cannot resolve parent '" + parentGuid +
					"' for entity '" + ObjectLabel(object) + "'";
				m_States[index] = TransformResolveState::Unresolved;
				return false;
			}
			if (!Resolve(parent->second, parentWorld, parentActive, error))
			{
				m_States[index] = TransformResolveState::Unresolved;
				return false;
			}
		}

		m_WorldTransforms[index] = parentWorld * ToLocalMatrix(*object.transform);
		m_Active[index] = object.active && parentActive;
		m_States[index] = TransformResolveState::Resolved;
		worldTransform = m_WorldTransforms[index];
		hierarchyActive = m_Active[index];
		return true;
	}

private:
	const VansSceneObjectBuildPlan& m_Plan;
	std::unordered_map<std::string, std::size_t> m_Indices;
	std::vector<TransformResolveState> m_States;
	std::vector<glm::mat4> m_WorldTransforms;
	std::vector<bool> m_Active;
};

void AppendBox(VansNavigationGeometry& output,
	const glm::mat4& worldTransform,
	const VansScenePhysicsNodeConfig& physics,
	std::uint8_t areaId)
{
	const glm::vec3 extents = physics.boxExtents
		? glm::max(glm::abs(ToVec3(*physics.boxExtents)), glm::vec3(0.0001f))
		: glm::vec3(0.5f);
	const glm::vec3 offset = physics.colliderOffset
		? ToVec3(*physics.colliderOffset)
		: (physics.shapeOffset ? ToVec3(*physics.shapeOffset) : glm::vec3(0.0f));
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
		const glm::vec3 p = glm::vec3(worldTransform * glm::vec4(point, 1.0f));
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
	output.areas.insert(output.areas.end(), boxIndices.size() / 3u, areaId);
}

bool IsEnvironmentStaticCollider(const VansSceneObjectBuildConfig& object,
	const VansScenePhysicsNodeConfig& physics,
	std::string& colliderType)
{
	if (!object.active || !object.transform ||
		!physics.enabled.value_or(true) || physics.isTrigger.value_or(false) ||
		physics.layer.value_or("Default") != "Environment" ||
		physics.bodyType.value_or("static") != "static")
	{
		return false;
	}
	colliderType = physics.colliderType.value_or("box");
	return colliderType == "box" || colliderType == "mesh";
}

bool ValidateColliderOffsets(const VansSceneObjectBuildConfig& object,
	const VansScenePhysicsNodeConfig& physics,
	std::string& error)
{
	if (!physics.shapeOffset || !physics.colliderOffset) return true;
	error = "Navigation collider '" + ObjectLabel(object) +
		"' cannot define both shapeOffset and colliderOffset";
	return false;
}

bool AppendMesh(VansNavigationGeometry& output,
	const glm::mat4& worldTransform,
	const VansScenePhysicsNodeConfig& physics,
	const VansTriangleMeshData& mesh,
	std::uint8_t areaId,
	std::string& error)
{
	if (mesh.Empty() || mesh.positions.size() % 3u != 0u ||
		mesh.indices.size() % 3u != 0u ||
		mesh.VertexCount() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) -
			output.VertexCount())
	{
		error = "Navigation mesh collider geometry is invalid or too large";
		return false;
	}
	const glm::vec3 offset = physics.colliderOffset
		? ToVec3(*physics.colliderOffset)
		: (physics.shapeOffset ? ToVec3(*physics.shapeOffset) : glm::vec3(0.0f));
	const int baseVertex = static_cast<int>(output.VertexCount());
	output.vertices.reserve(output.vertices.size() + mesh.positions.size());
	for (std::size_t vertex = 0; vertex < mesh.VertexCount(); ++vertex)
	{
		const std::size_t base = vertex * 3u;
		const glm::vec3 local(mesh.positions[base], mesh.positions[base + 1u],
			mesh.positions[base + 2u]);
		const glm::vec3 point = glm::vec3(
			worldTransform * glm::vec4(local + offset, 1.0f));
		if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
			!std::isfinite(point.z))
		{
			error = "Navigation mesh collider contains a non-finite vertex";
			return false;
		}
		output.vertices.push_back(point.x);
		output.vertices.push_back(point.y);
		output.vertices.push_back(point.z);
	}
	output.indices.reserve(output.indices.size() + mesh.indices.size());
	for (std::uint32_t index : mesh.indices)
	{
		if (index >= mesh.VertexCount())
		{
			error = "Navigation mesh collider contains an out-of-range index";
			return false;
		}
		output.indices.push_back(baseVertex + static_cast<int>(index));
	}
	output.areas.insert(output.areas.end(), mesh.TriangleCount(), areaId);
	return true;
}
}

bool VansSceneNavigationGeometry::BuildEnvironmentGeometry(
	const VansSceneObjectBuildPlan& sceneObjects,
	const VansNavigationAreaSettings& areaSettings,
	const VansNavigationMeshResolver& resolveMesh,
	VansNavigationGeometry& output,
	std::string& error)
{
	error.clear();
	if (!ValidateNavigationAreaSettings(areaSettings, error)) return false;
	VansNavigationGeometry geometry;
	SceneTransformResolver transforms(sceneObjects);
	if (!transforms.Initialize(error)) return false;
	for (std::size_t index = 0; index < sceneObjects.objects.size(); ++index)
	{
		const VansSceneObjectBuildConfig& object = sceneObjects.objects[index];
		if (!object.physicsComponents.physics)
			continue;
		const VansScenePhysicsNodeConfig& physics = *object.physicsComponents.physics;
		std::string colliderType;
		if (!IsEnvironmentStaticCollider(object, physics, colliderType)) continue;
		if (!ValidateColliderOffsets(object, physics, error)) return false;
		const std::string& areaName = physics.navigationArea
			? *physics.navigationArea : areaSettings.defaultArea;
		const VansNavigationAreaDefinition* area =
			FindNavigationAreaByName(areaSettings, areaName);
		if (!area)
		{
			error = "Navigation collider '" + ObjectLabel(object) +
				"' references unknown area '" + areaName + "'";
			return false;
		}
		glm::mat4 worldTransform(1.0f);
		bool hierarchyActive = false;
		if (!transforms.Resolve(index, worldTransform, hierarchyActive, error))
			return false;
		if (!hierarchyActive) continue;
		if (colliderType == "box")
		{
			AppendBox(geometry, worldTransform, physics, area->id);
			continue;
		}
		if (!physics.useMeshCollider.value_or(false) || !physics.mesh ||
			physics.mesh->empty())
		{
			error = "Navigation mesh collider '" + ObjectLabel(object) +
				"' requires useMeshCollider and a Model GUID";
			return false;
		}
		if (!resolveMesh)
		{
			error = "Navigation mesh resolver is unavailable for collider '" +
				ObjectLabel(object) + "'";
			return false;
		}
		VansTriangleMeshData mesh;
		if (!resolveMesh(*physics.mesh, mesh, error))
		{
			if (error.empty()) error = "Could not resolve navigation mesh collider '" +
				ObjectLabel(object) + "'";
			return false;
		}
		if (!AppendMesh(geometry, worldTransform, physics, mesh, area->id, error))
		{
			error = "Navigation mesh collider '" + ObjectLabel(object) +
				"' is invalid: " + error;
			return false;
		}
	}
	output = std::move(geometry);
	return true;
}

bool VansSceneNavigationGeometry::CollectEnvironmentMeshAssets(
	const VansSceneObjectBuildPlan& sceneObjects,
	std::vector<std::string>& output,
	std::string& error)
{
	error.clear();
	std::vector<std::string> assetGuids;
	SceneTransformResolver transforms(sceneObjects);
	if (!transforms.Initialize(error)) return false;
	for (std::size_t index = 0; index < sceneObjects.objects.size(); ++index)
	{
		const VansSceneObjectBuildConfig& object = sceneObjects.objects[index];
		if (!object.physicsComponents.physics) continue;
		const VansScenePhysicsNodeConfig& physics = *object.physicsComponents.physics;
		std::string colliderType;
		if (!IsEnvironmentStaticCollider(object, physics, colliderType) ||
			colliderType != "mesh")
		{
			continue;
		}
		if (!ValidateColliderOffsets(object, physics, error)) return false;
		glm::mat4 worldTransform(1.0f);
		bool hierarchyActive = false;
		if (!transforms.Resolve(index, worldTransform, hierarchyActive, error))
			return false;
		if (!hierarchyActive) continue;
		if (!physics.useMeshCollider.value_or(false) || !physics.mesh ||
			physics.mesh->empty())
		{
			error = "Navigation mesh collider '" + ObjectLabel(object) +
				"' requires useMeshCollider and a Model GUID";
			return false;
		}
		assetGuids.push_back(*physics.mesh);
	}
	std::sort(assetGuids.begin(), assetGuids.end());
	assetGuids.erase(std::unique(assetGuids.begin(), assetGuids.end()),
		assetGuids.end());
	output = std::move(assetGuids);
	return true;
}
}
