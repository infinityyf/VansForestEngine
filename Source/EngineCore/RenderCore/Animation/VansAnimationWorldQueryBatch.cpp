#include "VansAnimationWorldQueryBatch.h"

#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../ScriptCore/VansTransform.h"

#include <PxPhysicsAPI.h>

#include <cmath>
#include <mutex>

namespace VansGraphics
{
	namespace
	{
		class AnimationQueryFilter final : public physx::PxQueryFilterCallback
		{
		public:
			explicit AnimationQueryFilter(const VansWorldQueryRequest& request)
				: m_Request(request) {}

			physx::PxQueryHitType::Enum preFilter(
				const physx::PxFilterData& query,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor,
				physx::PxHitFlags&) override
			{
				return Filter(query, shape, actor);
			}

			physx::PxQueryHitType::Enum postFilter(
				const physx::PxFilterData& query,
				const physx::PxQueryHit&,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor) override
			{
				return Filter(query, shape, actor);
			}

		private:
			physx::PxQueryHitType::Enum Filter(
				const physx::PxFilterData& query,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor) const
			{
				if (!shape) return physx::PxQueryHitType::eNONE;
				const physx::PxFilterData target = shape->getQueryFilterData();
				if ((target.word2 & 0x1u) != 0u || target.word0 >= 32u
					|| (query.word1 & (1u << target.word0)) == 0u)
					return physx::PxQueryHitType::eNONE;
				if (m_Request.ignoredOwnerId != 0 && actor && actor->userData)
				{
					const auto* node = static_cast<const VansEngine::VansPhysicsNode*>(actor->userData);
					if (static_cast<std::uint64_t>(node->GetTransformID()) + 1u
						== m_Request.ignoredOwnerId) return physx::PxQueryHitType::eNONE;
				}
				return physx::PxQueryHitType::eBLOCK;
			}

			const VansWorldQueryRequest& m_Request;
		};

		template <typename THit>
		void PopulateHit(const THit& hit, VansWorldQueryResult& result)
		{
			result.hit = true;
			result.positionWorld = { hit.position.x, hit.position.y, hit.position.z };
			const glm::vec3 normal(hit.normal.x, hit.normal.y, hit.normal.z);
			result.normalWorld = glm::dot(normal, normal) > 1.0e-10f
				? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
			result.distance = hit.distance;
			if (hit.shape) result.layerIndex = hit.shape->getQueryFilterData().word0;
			if (!hit.actor) return;
			result.supportMovable = hit.actor->getType() == physx::PxActorType::eRIGID_DYNAMIC;
			const physx::PxTransform pose = hit.actor->getGlobalPose();
			result.supportPositionWorld = { pose.p.x, pose.p.y, pose.p.z };
			result.supportRotationWorld = glm::normalize(glm::quat(
				pose.q.w, pose.q.x, pose.q.y, pose.q.z));
			result.hasSupportTransform = true;
			if (hit.actor->userData)
			{
				const auto* node = static_cast<const VansEngine::VansPhysicsNode*>(hit.actor->userData);
				const std::uint32_t transformId = node->GetTransformID();
				result.support.id = static_cast<std::uint64_t>(transformId) + 1u;
				result.support.generation = VansTransformStore::GetGeneration(transformId);
			}
		}
	}

	void VansAnimationWorldQueryBatch::Execute(
		const std::vector<VansWorldQueryRequest>& requests,
		std::vector<VansWorldQueryResult>& results)
	{
		results.assign(requests.size(), VansWorldQueryResult{});
		if (requests.empty()) return;
		VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = physics.GetScene();
		if (!scene) return;
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		for (std::size_t index = 0; index < requests.size(); ++index)
		{
			const VansWorldQueryRequest& request = requests[index];
			VansWorldQueryResult& result = results[index];
			result.requestId = request.requestId;
			const float directionLength = glm::length(request.directionWorld);
			if (request.distance <= 0.0f || directionLength <= 1.0e-6f
				|| request.collisionMask == 0 || !std::isfinite(request.sweepRadius)) continue;
			const glm::vec3 direction = request.directionWorld / directionLength;
			physx::PxQueryFilterData filterData;
			filterData.data.word1 = request.collisionMask;
			filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC
				| physx::PxQueryFlag::ePREFILTER;
			AnimationQueryFilter filter(request);
			const physx::PxVec3 origin(request.originWorld.x, request.originWorld.y, request.originWorld.z);
			const physx::PxVec3 unitDirection(direction.x, direction.y, direction.z);
			const physx::PxHitFlags hitFlags = physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL;
			if (request.sweepRadius > 1.0e-6f)
			{
				physx::PxSweepBuffer hit;
				if (scene->sweep(physx::PxSphereGeometry(request.sweepRadius),
					physx::PxTransform(origin), unitDirection, request.distance,
					hit, hitFlags, filterData, &filter) && hit.hasBlock)
					PopulateHit(hit.block, result);
			}
			else
			{
				physx::PxRaycastBuffer hit;
				if (scene->raycast(origin, unitDirection, request.distance,
					hit, hitFlags, filterData, &filter) && hit.hasBlock)
					PopulateHit(hit.block, result);
			}
		}
	}
}
