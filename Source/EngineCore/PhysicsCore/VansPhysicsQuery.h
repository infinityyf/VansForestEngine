#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace VansEngine
{
	enum class VansPhysicsGeometryType
	{
		Unknown,
		Box,
		Sphere,
		Capsule,
		ConvexMesh,
		TriangleMesh,
		HeightField
	};

	struct VansPhysicsQueryCandidate
	{
		const void* actorIdentity = nullptr;
		std::uint32_t layerIndex = (std::numeric_limits<std::uint32_t>::max)();
		std::uint32_t transformId = (std::numeric_limits<std::uint32_t>::max)();
		bool isStatic = false;
		bool isDynamic = false;
		bool isTrigger = false;
		bool isController = false;
	};

	struct VansPhysicsQueryFilter
	{
		std::uint32_t layerMask = 0xFFFFFFFFu;
		std::uint32_t ignoredTransformId = (std::numeric_limits<std::uint32_t>::max)();
		bool includeStatic = true;
		bool includeDynamic = true;
		bool includeTriggers = true;
		bool includeControllers = true;
		std::function<bool(const VansPhysicsQueryCandidate&)> accept;
	};

	struct VansPhysicsRaycastRequest
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
		float distance = 0.0f;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsSphereSweepRequest
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
		float distance = 0.0f;
		float radius = 0.0f;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsShapeCastRequest
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
		float distance = 0.0f;
		// Zero selects a raycast; a positive value selects a sphere sweep.
		float sphereRadius = 0.0f;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsSphereOverlapRequest
	{
		glm::vec3 center{ 0.0f };
		float radius = 0.0f;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsQueryHit
	{
		glm::vec3 position{ 0.0f };
		glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
		float distance = 0.0f;
		const void* actorIdentity = nullptr;
		std::uint32_t layerIndex = (std::numeric_limits<std::uint32_t>::max)();
		std::uint32_t transformId = (std::numeric_limits<std::uint32_t>::max)();
		std::string objectName;
		std::string hitRegion;
		VansPhysicsGeometryType geometry = VansPhysicsGeometryType::Unknown;
		bool hasShape = false;
		bool supportMovable = false;
		bool hasSupportTransform = false;
		glm::vec3 supportPosition{ 0.0f };
		glm::quat supportRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	struct VansPhysicsShapeCastResult
	{
		bool hit = false;
		VansPhysicsQueryHit value;
	};

	// Thread-safe, engine-value scene queries. PhysX types and query callbacks stay
	// inside PhysicsCore; callers express only geometry, layer and identity policy.
	class VansPhysicsQuery final
	{
	public:
		static bool IsAvailable();
		static bool RaycastClosest(
			const VansPhysicsRaycastRequest& request,
			VansPhysicsQueryHit& hit);
		static void RaycastAll(
			const VansPhysicsRaycastRequest& request,
			std::size_t maxHits,
			std::vector<VansPhysicsQueryHit>& hits);
		static bool SweepSphereClosest(
			const VansPhysicsSphereSweepRequest& request,
			VansPhysicsQueryHit& hit);
		static void CastClosestBatch(
			const std::vector<VansPhysicsShapeCastRequest>& requests,
			std::vector<VansPhysicsShapeCastResult>& results);
		static void OverlapSphere(
			const VansPhysicsSphereOverlapRequest& request,
			std::size_t maxHits,
			std::vector<VansPhysicsQueryHit>& hits);
	};
}
