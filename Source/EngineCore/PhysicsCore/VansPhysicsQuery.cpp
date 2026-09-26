#include "VansPhysicsQuery.h"

#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansPhysicsNode.h"

#include <PxPhysicsAPI.h>
#include <characterkinematic/PxControllerManager.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>

namespace VansEngine
{
	namespace
	{
		using ControllerActors = std::unordered_set<const physx::PxRigidActor*>;

		ControllerActors CollectControllerActors(physx::PxControllerManager* manager)
		{
			ControllerActors actors;
			if (manager == nullptr)
				return actors;
			actors.reserve(manager->getNbControllers());
			for (physx::PxU32 index = 0; index < manager->getNbControllers(); ++index)
			{
				const physx::PxController* controller = manager->getController(index);
				if (controller != nullptr && controller->getActor() != nullptr)
					actors.insert(controller->getActor());
			}
			return actors;
		}

		VansPhysicsGeometryType ToGeometryType(const physx::PxShape* shape)
		{
			if (shape == nullptr)
				return VansPhysicsGeometryType::Unknown;
			switch (shape->getGeometry().getType())
			{
			case physx::PxGeometryType::eBOX: return VansPhysicsGeometryType::Box;
			case physx::PxGeometryType::eSPHERE: return VansPhysicsGeometryType::Sphere;
			case physx::PxGeometryType::eCAPSULE: return VansPhysicsGeometryType::Capsule;
			case physx::PxGeometryType::eCONVEXMESH: return VansPhysicsGeometryType::ConvexMesh;
			case physx::PxGeometryType::eTRIANGLEMESH: return VansPhysicsGeometryType::TriangleMesh;
			case physx::PxGeometryType::eHEIGHTFIELD: return VansPhysicsGeometryType::HeightField;
			default: return VansPhysicsGeometryType::Unknown;
			}
		}

		VansPhysicsQueryCandidate BuildCandidate(
			const physx::PxShape* shape,
			const physx::PxRigidActor* actor,
			const ControllerActors& controllers)
		{
			VansPhysicsQueryCandidate candidate;
			candidate.actorIdentity = actor;
			if (shape != nullptr)
			{
				const physx::PxFilterData data = shape->getQueryFilterData();
				candidate.layerIndex = data.word0;
				candidate.isTrigger = (data.word2 & 0x1u) != 0u ||
					shape->getFlags().isSet(physx::PxShapeFlag::eTRIGGER_SHAPE);
			}
			if (actor != nullptr)
			{
				candidate.isStatic = actor->getType() == physx::PxActorType::eRIGID_STATIC;
				candidate.isDynamic = actor->getType() == physx::PxActorType::eRIGID_DYNAMIC;
				candidate.isController = controllers.find(actor) != controllers.end();
				if (!candidate.isController && actor->userData != nullptr)
				{
					const auto* node = static_cast<const VansPhysicsNode*>(actor->userData);
					candidate.transformId = node->GetTransformID();
				}
			}
			return candidate;
		}

		bool AcceptCandidate(
			const VansPhysicsQueryFilter& filter,
			const VansPhysicsQueryCandidate& candidate)
		{
			if (candidate.layerIndex >= 32u ||
				(filter.layerMask & (1u << candidate.layerIndex)) == 0u)
				return false;
			if ((!filter.includeStatic && candidate.isStatic) ||
				(!filter.includeDynamic && candidate.isDynamic) ||
				(!filter.includeTriggers && candidate.isTrigger) ||
				(!filter.includeControllers && candidate.isController) ||
				(filter.ignoredTransformId != (std::numeric_limits<std::uint32_t>::max)() &&
					candidate.transformId == filter.ignoredTransformId))
				return false;
			return !filter.accept || filter.accept(candidate);
		}

		class QueryFilterCallback final : public physx::PxQueryFilterCallback
		{
		public:
			QueryFilterCallback(
				const VansPhysicsQueryFilter& filter,
				const ControllerActors& controllers,
				physx::PxQueryHitType::Enum acceptedType)
				: m_Filter(filter), m_Controllers(controllers), m_AcceptedType(acceptedType)
			{
			}

			physx::PxQueryHitType::Enum preFilter(
				const physx::PxFilterData&,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor,
				physx::PxHitFlags&) override
			{
				return AcceptCandidate(m_Filter, BuildCandidate(shape, actor, m_Controllers))
					? m_AcceptedType : physx::PxQueryHitType::eNONE;
			}

			physx::PxQueryHitType::Enum postFilter(
				const physx::PxFilterData&,
				const physx::PxQueryHit&,
				const physx::PxShape* shape,
				const physx::PxRigidActor* actor) override
			{
				return AcceptCandidate(m_Filter, BuildCandidate(shape, actor, m_Controllers))
					? m_AcceptedType : physx::PxQueryHitType::eNONE;
			}

		private:
			const VansPhysicsQueryFilter& m_Filter;
			const ControllerActors& m_Controllers;
			physx::PxQueryHitType::Enum m_AcceptedType;
		};

		physx::PxQueryFilterData BuildFilterData(const VansPhysicsQueryFilter& filter)
		{
			physx::PxQueryFilterData data;
			data.flags = physx::PxQueryFlag::ePREFILTER;
			if (filter.includeStatic) data.flags |= physx::PxQueryFlag::eSTATIC;
			if (filter.includeDynamic) data.flags |= physx::PxQueryFlag::eDYNAMIC;
			return data;
		}

		void PopulateCommonHit(
			const physx::PxShape* shape,
			const physx::PxRigidActor* actor,
			const ControllerActors& controllers,
			VansPhysicsQueryHit& hit)
		{
			hit.actorIdentity = actor;
			hit.hasShape = shape != nullptr;
			hit.geometry = ToGeometryType(shape);
			const VansPhysicsQueryCandidate candidate = BuildCandidate(shape, actor, controllers);
			hit.layerIndex = candidate.layerIndex;
			hit.transformId = candidate.transformId;
			if (actor == nullptr)
				return;

			hit.supportMovable = candidate.isDynamic;
			const physx::PxTransform pose = actor->getGlobalPose();
			hit.supportPosition = { pose.p.x, pose.p.y, pose.p.z };
			hit.supportRotation = glm::normalize(glm::quat(
				pose.q.w, pose.q.x, pose.q.y, pose.q.z));
			hit.hasSupportTransform = true;

			if (!candidate.isController && actor->userData != nullptr)
			{
				const auto* node = static_cast<const VansPhysicsNode*>(actor->userData);
				hit.objectName = node->GetName();
				hit.hitRegion = node->GetProperties().hitRegion;
			}
			else if (actor->getName() != nullptr)
			{
				hit.objectName = actor->getName();
			}
		}

		template <typename THit>
		VansPhysicsQueryHit BuildHit(
			const THit& nativeHit,
			const ControllerActors& controllers)
		{
			VansPhysicsQueryHit hit;
			hit.position = {
				nativeHit.position.x, nativeHit.position.y, nativeHit.position.z };
			const glm::vec3 normal(
				nativeHit.normal.x, nativeHit.normal.y, nativeHit.normal.z);
			hit.normal = glm::dot(normal, normal) > 1.0e-10f
				? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
			hit.distance = nativeHit.distance;
			PopulateCommonHit(nativeHit.shape, nativeHit.actor, controllers, hit);
			return hit;
		}

		VansPhysicsQueryHit BuildOverlapHit(
			const physx::PxOverlapHit& nativeHit,
			const ControllerActors& controllers)
		{
			VansPhysicsQueryHit hit;
			PopulateCommonHit(nativeHit.shape, nativeHit.actor, controllers, hit);
			return hit;
		}
	}

	bool VansPhysicsQuery::IsAvailable()
	{
		auto& physics = VansPhysicsSystem::GetInstance();
		return VansPhysicsNativeAccess::Scene(physics) != nullptr;
	}

	bool VansPhysicsQuery::RaycastClosest(
		const VansPhysicsRaycastRequest& request,
		VansPhysicsQueryHit& hit)
	{
		hit = {};
		const float directionLength = glm::length(request.direction);
		if (!std::isfinite(request.distance) || request.distance <= 0.0f ||
			!std::isfinite(directionLength) || directionLength <= 1.0e-6f ||
			request.filter.layerMask == 0u)
			return false;

		VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
		if (scene == nullptr)
			return false;
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		physx::PxSceneReadLock sceneReadLock(*scene);
		const ControllerActors controllers = CollectControllerActors(
			VansPhysicsNativeAccess::ControllerManager(physics));
		QueryFilterCallback callback(request.filter, controllers, physx::PxQueryHitType::eBLOCK);
		physx::PxRaycastBuffer nativeHit;
		const glm::vec3 direction = request.direction / directionLength;
		const bool blocked = scene->raycast(
			physx::PxVec3(request.origin.x, request.origin.y, request.origin.z),
			physx::PxVec3(direction.x, direction.y, direction.z),
			request.distance,
			nativeHit,
			physx::PxHitFlag::eDEFAULT,
			BuildFilterData(request.filter),
			&callback) && nativeHit.hasBlock;
		if (blocked)
			hit = BuildHit(nativeHit.block, controllers);
		return blocked;
	}

	void VansPhysicsQuery::RaycastAll(
		const VansPhysicsRaycastRequest& request,
		std::size_t maxHits,
		std::vector<VansPhysicsQueryHit>& hits)
	{
		hits.clear();
		const float directionLength = glm::length(request.direction);
		if (maxHits == 0 || !std::isfinite(request.distance) || request.distance <= 0.0f ||
			!std::isfinite(directionLength) || directionLength <= 1.0e-6f ||
			request.filter.layerMask == 0u)
			return;

		VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
		if (scene == nullptr)
			return;
		const physx::PxU32 capacity = static_cast<physx::PxU32>(
			(std::min)(maxHits, static_cast<std::size_t>((std::numeric_limits<physx::PxU32>::max)())));
		std::vector<physx::PxRaycastHit> nativeHits(capacity);
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		physx::PxSceneReadLock sceneReadLock(*scene);
		const ControllerActors controllers = CollectControllerActors(
			VansPhysicsNativeAccess::ControllerManager(physics));
		// Preserve the previous Lua buffer contract: default raycast candidates are
		// blocking, so the result contains the closest block plus any native touches.
		QueryFilterCallback callback(request.filter, controllers, physx::PxQueryHitType::eBLOCK);
		physx::PxRaycastBuffer buffer(nativeHits.data(), capacity);
		const glm::vec3 direction = request.direction / directionLength;
		scene->raycast(
			physx::PxVec3(request.origin.x, request.origin.y, request.origin.z),
			physx::PxVec3(direction.x, direction.y, direction.z),
			request.distance,
			buffer,
			physx::PxHitFlag::eDEFAULT,
			BuildFilterData(request.filter),
			&callback);
		hits.reserve(buffer.nbTouches + (buffer.hasBlock ? 1u : 0u));
		for (physx::PxU32 index = 0; index < buffer.nbTouches; ++index)
			hits.push_back(BuildHit(buffer.touches[index], controllers));
		if (buffer.hasBlock)
			hits.push_back(BuildHit(buffer.block, controllers));
	}

	bool VansPhysicsQuery::SweepSphereClosest(
		const VansPhysicsSphereSweepRequest& request,
		VansPhysicsQueryHit& hit)
	{
		hit = {};
		const float directionLength = glm::length(request.direction);
		if (!std::isfinite(request.distance) || request.distance <= 0.0f ||
			!std::isfinite(request.radius) || request.radius <= 0.0f ||
			!std::isfinite(directionLength) || directionLength <= 1.0e-6f ||
			request.filter.layerMask == 0u)
			return false;

		VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
		if (scene == nullptr)
			return false;
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		physx::PxSceneReadLock sceneReadLock(*scene);
		const ControllerActors controllers = CollectControllerActors(
			VansPhysicsNativeAccess::ControllerManager(physics));
		QueryFilterCallback callback(request.filter, controllers, physx::PxQueryHitType::eBLOCK);
		physx::PxSweepBuffer nativeHit;
		const glm::vec3 direction = request.direction / directionLength;
		const bool blocked = scene->sweep(
			physx::PxSphereGeometry(request.radius),
			physx::PxTransform(physx::PxVec3(request.origin.x, request.origin.y, request.origin.z)),
			physx::PxVec3(direction.x, direction.y, direction.z),
			request.distance,
			nativeHit,
			physx::PxHitFlag::eDEFAULT,
			BuildFilterData(request.filter),
			&callback) && nativeHit.hasBlock;
		if (blocked)
			hit = BuildHit(nativeHit.block, controllers);
		return blocked;
	}

	void VansPhysicsQuery::CastClosestBatch(
		const std::vector<VansPhysicsShapeCastRequest>& requests,
		std::vector<VansPhysicsShapeCastResult>& results)
	{
		results.assign(requests.size(), VansPhysicsShapeCastResult{});
		if (requests.empty())
			return;

		VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
		if (scene == nullptr)
			return;
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		physx::PxSceneReadLock sceneReadLock(*scene);
		const ControllerActors controllers = CollectControllerActors(
			VansPhysicsNativeAccess::ControllerManager(physics));

		for (std::size_t index = 0; index < requests.size(); ++index)
		{
			const VansPhysicsShapeCastRequest& request = requests[index];
			const float directionLength = glm::length(request.direction);
			if (!std::isfinite(request.distance) || request.distance <= 0.0f ||
				!std::isfinite(request.sphereRadius) || request.sphereRadius < 0.0f ||
				!std::isfinite(directionLength) || directionLength <= 1.0e-6f ||
				request.filter.layerMask == 0u)
				continue;

			QueryFilterCallback callback(
				request.filter, controllers, physx::PxQueryHitType::eBLOCK);
			const glm::vec3 direction = request.direction / directionLength;
			if (request.sphereRadius > 1.0e-6f)
			{
				physx::PxSweepBuffer nativeHit;
				results[index].hit = scene->sweep(
					physx::PxSphereGeometry(request.sphereRadius),
					physx::PxTransform(physx::PxVec3(
						request.origin.x, request.origin.y, request.origin.z)),
					physx::PxVec3(direction.x, direction.y, direction.z),
					request.distance,
					nativeHit,
					physx::PxHitFlag::eDEFAULT,
					BuildFilterData(request.filter),
					&callback) && nativeHit.hasBlock;
				if (results[index].hit)
					results[index].value = BuildHit(nativeHit.block, controllers);
			}
			else
			{
				physx::PxRaycastBuffer nativeHit;
				results[index].hit = scene->raycast(
					physx::PxVec3(request.origin.x, request.origin.y, request.origin.z),
					physx::PxVec3(direction.x, direction.y, direction.z),
					request.distance,
					nativeHit,
					physx::PxHitFlag::eDEFAULT,
					BuildFilterData(request.filter),
					&callback) && nativeHit.hasBlock;
				if (results[index].hit)
					results[index].value = BuildHit(nativeHit.block, controllers);
			}
		}
	}

	void VansPhysicsQuery::OverlapSphere(
		const VansPhysicsSphereOverlapRequest& request,
		std::size_t maxHits,
		std::vector<VansPhysicsQueryHit>& hits)
	{
		hits.clear();
		if (maxHits == 0 || !std::isfinite(request.radius) || request.radius <= 0.0f ||
			request.filter.layerMask == 0u)
			return;

		VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
		physx::PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
		if (scene == nullptr)
			return;
		const physx::PxU32 capacity = static_cast<physx::PxU32>(
			(std::min)(maxHits, static_cast<std::size_t>((std::numeric_limits<physx::PxU32>::max)())));
		std::vector<physx::PxOverlapHit> nativeHits(capacity);
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		physx::PxSceneReadLock sceneReadLock(*scene);
		const ControllerActors controllers = CollectControllerActors(
			VansPhysicsNativeAccess::ControllerManager(physics));
		QueryFilterCallback callback(request.filter, controllers, physx::PxQueryHitType::eTOUCH);
		physx::PxOverlapBuffer buffer(nativeHits.data(), capacity);
		scene->overlap(
			physx::PxSphereGeometry(request.radius),
			physx::PxTransform(physx::PxVec3(request.center.x, request.center.y, request.center.z)),
			buffer,
			BuildFilterData(request.filter),
			&callback);
		hits.reserve(buffer.nbTouches);
		for (physx::PxU32 index = 0; index < buffer.nbTouches; ++index)
			hits.push_back(BuildOverlapHit(buffer.touches[index], controllers));
	}
}
