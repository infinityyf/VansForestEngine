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
		// -1 仅使用 layerMask；其余值同时应用项目的双向碰撞矩阵。
		int collisionLayerIndex = -1;
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

	struct VansPhysicsCapsuleSweepRequest
	{
		glm::vec3 origin{0};
		glm::vec3 direction{0,0,1};
		glm::vec3 axis{0,1,0};
		float distance=0;
		float radius=0;
		// 从胶囊中心到端点，包含端部半球半径。
		float halfHeight=0;
		bool computePenetration=false;
		bool ignoreInitialOverlapsMovingOut=false;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsCapsuleOverlapRequest
	{
		glm::vec3 center{0}, axis{0,1,0};
		float radius=0, halfHeight=0;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsSphereOverlapRequest
	{
		glm::vec3 center{ 0.0f };
		float radius = 0.0f;
		VansPhysicsQueryFilter filter;
	};

	// One impulse per simulated actor, including articulation-free ragdoll bodies.
	// impulse is N*s; maxVelocityChange bounds light props and individual limbs.
	struct VansPhysicsRadialImpulseRequest
	{
		glm::vec3 center{0};
		float radius = 0, impulse = 0, maxVelocityChange = 8, upwardBias = 0;
		std::uint32_t blockingLayerMask = 0;
		VansPhysicsQueryFilter filter;
	};

	struct VansPhysicsQueryHit
	{
		glm::vec3 position{ 0.0f };
		glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
		// 表面法线与胶囊接触法线在棱边处不同；滑动用 normal，坡度用 impactNormal。
		glm::vec3 impactNormal{ 0.0f, 1.0f, 0.0f };
		float distance = 0.0f;
		float penetrationDepth = 0.0f;
		const void* actorIdentity = nullptr;
		bool initialOverlap = false;
		bool isController = false;
		bool isCharacterBody = false;
		bool canCharacterStepUp = true;
		std::uint32_t layerIndex = (std::numeric_limits<std::uint32_t>::max)();
		std::uint32_t transformId = (std::numeric_limits<std::uint32_t>::max)();
		std::string objectName;
		std::string hitRegion;
		VansPhysicsGeometryType geometry = VansPhysicsGeometryType::Unknown;
		bool hasShape = false;
		bool supportMovable = false;
		bool supportSimulated = false;
		glm::vec3 supportContactVelocity{0};
		glm::vec3 supportLinearVelocity{0}, supportAngularVelocity{0};
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
		// Validates identity against live scene actors before reading a saved movement base.
		// Caller holds SimulationMutex; a removed body returns false without dereferencing its identity.
		static bool GetBodyMotionLocked(const void* actorIdentity, std::uint32_t transformId,
			VansPhysicsQueryHit& motion);
		static bool RaycastClosest(
			const VansPhysicsRaycastRequest& request,
			VansPhysicsQueryHit& hit);
		// 调用方已持有 SimulationMutex，供一次运动求解内的支撑查询使用。
		static bool RaycastClosestLocked(
			const VansPhysicsRaycastRequest& request,
			VansPhysicsQueryHit& hit);
		static void RaycastAll(
			const VansPhysicsRaycastRequest& request,
			std::size_t maxHits,
			std::vector<VansPhysicsQueryHit>& hits);
		static bool SweepSphereClosest(
			const VansPhysicsSphereSweepRequest& request,
			VansPhysicsQueryHit& hit);
		static bool SweepCapsuleClosest(
			const VansPhysicsCapsuleSweepRequest& request,
			VansPhysicsQueryHit& hit);
		// PhysicsCore 运动求解期间调用；调用方必须已持有 SimulationMutex。
		static bool SweepCapsuleClosestLocked(
			const VansPhysicsCapsuleSweepRequest& request,
			VansPhysicsQueryHit& hit);
		static bool OverlapCapsuleAny(const VansPhysicsCapsuleOverlapRequest& request);
		static bool OverlapCapsuleAnyLocked(const VansPhysicsCapsuleOverlapRequest& request);
		static void CastClosestBatch(
			const std::vector<VansPhysicsShapeCastRequest>& requests,
			std::vector<VansPhysicsShapeCastResult>& results);
		static void OverlapSphere(
			const VansPhysicsSphereOverlapRequest& request,
			std::size_t maxHits,
			std::vector<VansPhysicsQueryHit>& hits);
		static std::size_t ApplyRadialImpulse(const VansPhysicsRadialImpulseRequest& request);
	};
}
