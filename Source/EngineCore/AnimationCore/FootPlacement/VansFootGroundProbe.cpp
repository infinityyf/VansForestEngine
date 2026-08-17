#include "VansFootGroundProbe.h"
#include "../../PhysicsCore/VansPhysics.h"

#include <PxPhysicsAPI.h>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtc/quaternion.hpp>

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
			                                      const physx::PxRigidActor*,
			                                      physx::PxHitFlags&) override
			{
				return Filter(filterData, shape);
			}

			physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData& filterData,
			                                       const physx::PxQueryHit&,
			                                       const physx::PxShape* shape,
			                                       const physx::PxRigidActor*) override
			{
				return Filter(filterData, shape);
			}

		private:
			static physx::PxQueryHitType::Enum Filter(const physx::PxFilterData& filterData,
			                                               const physx::PxShape* shape)
			{
				if (!shape)
					return physx::PxQueryHitType::eNONE;
				const physx::PxFilterData target = shape->getQueryFilterData();
				if ((target.word2 & 0x1u) != 0u)
					return physx::PxQueryHitType::eNONE;
				const uint32_t layer = target.word0;
				if (layer >= 32u || (filterData.word1 & (1u << layer)) == 0u)
					return physx::PxQueryHitType::eNONE;
				return physx::PxQueryHitType::eBLOCK;
			}
		};
	}

	std::vector<FootGroundHit> VansFootGroundProbe::RaycastBatch(
		const std::vector<FootGroundRayRequest>& requests,
		uint32_t collisionMask) const
	{
		std::vector<FootGroundHit> results(requests.size());
		if (requests.empty())
			return results;

		VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = physics.GetScene();
		if (!scene)
			return results;

		physx::PxQueryFilterData filterData;
		filterData.data.word1 = collisionMask;
		filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC |
		                   physx::PxQueryFlag::ePREFILTER;
		FootGroundQueryFilter filter;
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());

		for (size_t index = 0; index < requests.size(); ++index)
		{
			const FootGroundRayRequest& request = requests[index];
			const float directionLength = glm::length(request.direction);
			if (request.distance <= 0.0f || directionLength <= 1e-5f)
				continue;

			const glm::vec3 direction = request.direction / directionLength;
			physx::PxRaycastBuffer hit;
			if (!scene->raycast(physx::PxVec3(request.origin.x, request.origin.y, request.origin.z),
			                    physx::PxVec3(direction.x, direction.y, direction.z),
			                    request.distance,
			                    hit,
			                    physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL,
			                    filterData,
			                    &filter) || !hit.hasBlock)
				continue;

			FootGroundHit& result = results[index];
			result.hasHit = true;
			result.position = glm::vec3(hit.block.position.x, hit.block.position.y, hit.block.position.z);
			const glm::vec3 normal(hit.block.normal.x, hit.block.normal.y, hit.block.normal.z);
			result.normal = glm::dot(normal, normal) > 1e-10f
				? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
			result.distance = hit.block.distance;
			if (hit.block.shape)
				result.layerIndex = hit.block.shape->getQueryFilterData().word0;
			if (hit.block.actor)
			{
				result.actorId = reinterpret_cast<uintptr_t>(hit.block.actor);
				const physx::PxTransform actorPose = hit.block.actor->getGlobalPose();
				const glm::quat actorRotation(
					actorPose.q.w, actorPose.q.x, actorPose.q.y, actorPose.q.z);
				result.actorWorldTransform = glm::translate(
					glm::mat4(1.0f),
					glm::vec3(actorPose.p.x, actorPose.p.y, actorPose.p.z)) *
					glm::mat4_cast(actorRotation);
				result.hasActorWorldTransform = true;
				if (hit.block.actor->getName())
					result.actorName = hit.block.actor->getName();
			}
		}
		return results;
	}
}
