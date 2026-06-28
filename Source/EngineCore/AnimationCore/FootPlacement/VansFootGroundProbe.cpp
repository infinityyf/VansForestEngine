#include "VansFootGroundProbe.h"
#include "../../PhysicsCore/VansPhysics.h"

#include <PxPhysicsAPI.h>

#include <cmath>
#include <limits>
#include <mutex>

namespace VansGraphics
{
	namespace
	{
		class FootGroundQueryFilter final : public physx::PxQueryFilterCallback
		{
		public:
			physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& filterData,
			                                      const physx::PxShape* shape,
			                                      const physx::PxRigidActor* actor,
			                                      physx::PxHitFlags& queryFlags) override
			{
				(void)actor;
				(void)queryFlags;
				return FilterShape(filterData, shape);
			}

			physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData& filterData,
			                                       const physx::PxQueryHit& hit,
			                                       const physx::PxShape* shape,
			                                       const physx::PxRigidActor* actor) override
			{
				(void)hit;
				(void)actor;
				return FilterShape(filterData, shape);
			}

		private:
			physx::PxQueryHitType::Enum FilterShape(const physx::PxFilterData& filterData,
			                                        const physx::PxShape* shape) const
			{
				if (!shape)
					return physx::PxQueryHitType::eNONE;

				const physx::PxFilterData targetData = shape->getQueryFilterData();
				const bool targetIsTrigger = (targetData.word2 & 0x1u) != 0u;
				if (targetIsTrigger)
					return physx::PxQueryHitType::eNONE;

				const uint32_t targetLayer = targetData.word0;
				const uint32_t targetMask = filterData.word1;
				if (targetLayer >= 32u || (targetMask & (1u << targetLayer)) == 0u)
					return physx::PxQueryHitType::eNONE;

				return physx::PxQueryHitType::eBLOCK;
			}
		};
	}

	FootGroundHit VansFootGroundProbe::Raycast(const glm::vec3& origin,
	                                           const glm::vec3& direction,
	                                           float distance,
	                                           uint32_t collisionMask) const
	{
		FootGroundHit result;

		if (distance <= 0.0f)
			return result;

		const float len = glm::length(direction);
		if (len <= 1e-5f)
			return result;

		VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = physics.GetScene();
		if (!scene)
			return result;

		const glm::vec3 dir = direction / len;
		physx::PxRaycastBuffer hit;
		physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
		physx::PxVec3 pxDir(dir.x, dir.y, dir.z);
		physx::PxQueryFilterData filterData;
		filterData.data.word1 = collisionMask;
		filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
		FootGroundQueryFilter filterCallback;

		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		if (!scene->raycast(pxOrigin,
		                    pxDir,
		                    distance,
		                    hit,
		                    physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL,
		                    filterData,
		                    &filterCallback))
			return result;

		if (!hit.hasBlock)
			return result;

		result.hasHit = true;
		result.position = glm::vec3(hit.block.position.x, hit.block.position.y, hit.block.position.z);
		result.normal = glm::normalize(glm::vec3(hit.block.normal.x, hit.block.normal.y, hit.block.normal.z));
		result.distance = hit.block.distance;
		if (hit.block.shape)
			result.layerIndex = hit.block.shape->getQueryFilterData().word0;
		if (hit.block.actor && hit.block.actor->getName())
			result.actorName = hit.block.actor->getName();
		return result;
	}

	FootGroundHit VansFootGroundProbe::RaycastDown(const glm::vec3& origin,
	                                               float distance,
	                                               uint32_t collisionMask) const
	{
		return Raycast(origin, glm::vec3(0.0f, -1.0f, 0.0f), distance, collisionMask);
	}

	FootOverlapHit VansFootGroundProbe::OverlapBox(const glm::vec3& center,
	                                               const glm::vec3& halfExtents,
	                                               uint32_t collisionMask) const
	{
		return OverlapBox(center, halfExtents, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), collisionMask);
	}

	FootOverlapHit VansFootGroundProbe::OverlapBox(const glm::vec3& center,
	                                               const glm::vec3& halfExtents,
	                                               const glm::quat& rotation,
	                                               uint32_t collisionMask) const
	{
		FootOverlapHit result;
		if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
			return result;

		VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = physics.GetScene();
		if (!scene)
			return result;

		physx::PxOverlapHit hits[16];
		physx::PxOverlapBuffer buffer(hits, 16);
		physx::PxBoxGeometry geometry(halfExtents.x, halfExtents.y, halfExtents.z);
		physx::PxQuat pxRotation(rotation.x, rotation.y, rotation.z, rotation.w);
		if (!pxRotation.isFinite() || !pxRotation.isUnit())
			pxRotation = physx::PxQuat(physx::PxIdentity);
		physx::PxTransform pose(physx::PxVec3(center.x, center.y, center.z), pxRotation);
		physx::PxQueryFilterData filterData;
		filterData.data.word1 = collisionMask;
		filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
		FootGroundQueryFilter filterCallback;

		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		if (!scene->overlap(geometry, pose, buffer, filterData, &filterCallback))
			return result;

		if (buffer.nbTouches == 0)
			return result;

		int bestIndex = -1;
		float bestTop = -std::numeric_limits<float>::max();
		for (physx::PxU32 i = 0; i < buffer.nbTouches; ++i)
		{
			const physx::PxOverlapHit& candidate = buffer.touches[i];
			if (!candidate.actor)
			{
				if (bestIndex < 0)
					bestIndex = static_cast<int>(i);
				continue;
			}

			const physx::PxBounds3 bounds = candidate.actor->getWorldBounds();
			if (!bounds.isValid())
				continue;

			if (bounds.maximum.y > bestTop)
			{
				bestTop = bounds.maximum.y;
				bestIndex = static_cast<int>(i);
			}
		}

		if (bestIndex < 0)
			return result;

		const physx::PxOverlapHit& hit = buffer.touches[bestIndex];
		result.hasHit = true;
		if (hit.actor)
		{
			const physx::PxBounds3 bounds = hit.actor->getWorldBounds();
			if (bounds.isValid())
			{
				result.hasBounds = true;
				result.boundsMin = glm::vec3(bounds.minimum.x, bounds.minimum.y, bounds.minimum.z);
				result.boundsMax = glm::vec3(bounds.maximum.x, bounds.maximum.y, bounds.maximum.z);
			}
		}
		if (hit.shape)
			result.layerIndex = hit.shape->getQueryFilterData().word0;
		if (hit.actor && hit.actor->getName())
			result.actorName = hit.actor->getName();
		return result;
	}
}
