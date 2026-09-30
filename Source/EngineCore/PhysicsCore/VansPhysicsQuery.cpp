#include "VansPhysicsQuery.h"

#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansPhysicsNode.h"
#include "VansCharacterControllerNode.h"
#include "VansCollisionLayerManager.h"
#include "VansRagdollSystem.h"

#include <PxPhysicsAPI.h>
#include <characterkinematic/PxControllerManager.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <type_traits>
#include <array>

namespace VansEngine
{
	namespace
	{
		struct ControllerActorInfo
		{
			std::uint32_t transformId = (std::numeric_limits<std::uint32_t>::max)();
			const VansCharacterControllerNode* node = nullptr;
		};
		using ControllerActors = std::unordered_map<const physx::PxRigidActor*,ControllerActorInfo>;

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
				{
					const auto* node=static_cast<const VansCharacterControllerNode*>(controller->getUserData());
					actors.emplace(controller->getActor(),ControllerActorInfo{node?node->GetTransformID():
						(std::numeric_limits<std::uint32_t>::max)(),node});
				}
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
				if(candidate.isController)candidate.transformId=controllers.at(actor).transformId;
				if (!candidate.isController && actor->userData != nullptr)
				{
					const auto* node = static_cast<const VansPhysicsNode*>(actor->userData);
					candidate.transformId = node->GetTransformID();
				}
				else if (!candidate.isController)
				{
					VansRagdollSystem::GetInstance().TryGetActorTransformIdLocked(actor, candidate.transformId);
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
			if(filter.collisionLayerIndex < -1 || filter.collisionLayerIndex>=32 ||
				(filter.collisionLayerIndex>=0 && !VansCollisionLayerManager::Get().CanLayersCollide(filter.collisionLayerIndex,static_cast<int>(candidate.layerIndex))))return false;
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
			hit.isController = candidate.isController;
			hit.isCharacterBody = candidate.isController ||
				(candidate.transformId != (std::numeric_limits<std::uint32_t>::max)() &&
					std::any_of(controllers.begin(),controllers.end(),[&](const auto& item)
						{return item.second.transformId == candidate.transformId;}));
			if (actor == nullptr)
				return;

			hit.supportMovable = candidate.isDynamic;
			const physx::PxTransform pose = actor->getGlobalPose();
			hit.supportPosition = { pose.p.x, pose.p.y, pose.p.z };
			hit.supportRotation = glm::normalize(glm::quat(
				pose.q.w, pose.q.x, pose.q.y, pose.q.z));
			hit.hasSupportTransform = true;
			if (const auto* body=actor->is<physx::PxRigidDynamic>())
			{
				const auto linear=body->getLinearVelocity(),angular=body->getAngularVelocity();
				hit.supportLinearVelocity={linear.x,linear.y,linear.z};
				hit.supportAngularVelocity={angular.x,angular.y,angular.z};
				hit.supportSimulated=!body->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) &&
					!body->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION);
				if (hit.supportSimulated)
				{
					hit.supportContactVelocity={linear.x,linear.y,linear.z};
					if (std::abs(angular.x)>1.e-4f || std::abs(angular.y)>1.e-4f || std::abs(angular.z)>1.e-4f)
						hit.supportContactVelocity+=glm::cross(glm::vec3(angular.x,angular.y,angular.z),
							hit.position-hit.supportPosition);
				}
			}
			if (candidate.isController)
			{
				const auto& controller=controllers.at(actor);
				if (controller.node)
				{
					// The CCT owns its position and resolved velocity. Its PhysX actor
					// pose/velocity can remain stale after a controller move.
					hit.supportPosition=controller.node->GetPosition();
					hit.supportLinearVelocity=controller.node->GetResolvedVelocity();
					if (Vans::VansTransformStore::IsAllocated(controller.transformId))
						hit.supportRotation=glm::quat(glm::radians(
						Vans::VansTransformStore::Read(controller.transformId).m_Rotation));
				}
			}
			else if (candidate.isDynamic)
			{
				glm::vec3 bonePosition;
				glm::quat boneRotation;
				if (VansRagdollSystem::GetInstance().TryGetActorBoneTransformLocked(
					actor,bonePosition,boneRotation))
				{
					hit.supportPosition=bonePosition;
					hit.supportRotation=boneRotation;
				}
			}

			if (!candidate.isController && actor->userData != nullptr)
			{
				const auto* node = static_cast<const VansPhysicsNode*>(actor->userData);
				hit.objectName = node->GetName();
				hit.hitRegion = node->GetProperties().hitRegion;
				hit.canCharacterStepUp = node->GetProperties().canCharacterStepUp;
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
			hit.impactNormal = hit.normal;
			hit.distance = nativeHit.distance;
			if constexpr(std::is_same_v<THit,physx::PxSweepHit>)
			{
				hit.initialOverlap=nativeHit.hadInitialOverlap();
				if(hit.initialOverlap)
				{
					hit.penetrationDepth=(std::max)(0.f,-nativeHit.distance);
					hit.distance=0;
				}
			}
			PopulateCommonHit(nativeHit.shape, nativeHit.actor, controllers, hit);
			return hit;
		}

		glm::vec3 OpposingSurfaceNormal(const physx::PxSweepHit& hit, const glm::vec3& direction,
			const glm::vec3& contactNormal)
		{
			if (!hit.shape || !hit.actor || hit.hadInitialOverlap()) return contactNormal;
			const auto pose = hit.actor->getGlobalPose()*hit.shape->getLocalPose();
			const auto& geometry = hit.shape->getGeometry();
			const auto localDirection = pose.q.rotateInv(physx::PxVec3(direction.x,direction.y,direction.z));
			const auto localNormal = pose.q.rotateInv(physx::PxVec3(contactNormal.x,contactNormal.y,contactNormal.z));
			physx::PxVec3 surface = localNormal;
			if (geometry.getType() == physx::PxGeometryType::eBOX)
			{
				float best = (std::numeric_limits<float>::max)();
				for (int axis=0;axis<3;++axis)
				{
					if (std::abs(localNormal[axis]) <= 1.e-4f) continue;
					const float sign = localNormal[axis]>0 ? 1.f : -1.f;
					const float opposing = sign*localDirection[axis];
					if (opposing < best)
					{
						best=opposing;surface=physx::PxVec3(0);surface[axis]=sign;
					}
				}
			}
			else if (geometry.getType() == physx::PxGeometryType::eCONVEXMESH &&
				hit.faceIndex != 0xFFFFFFFFu)
			{
				const auto& convex = static_cast<const physx::PxConvexMeshGeometry&>(geometry);
				physx::PxHullPolygon polygon;
				if (convex.convexMesh->getPolygonData(hit.faceIndex,polygon))
				{
					// 非均匀缩放的平面法线使用逆转置。
					const auto inverse = convex.scale.toMat33().getInverse();
					surface = inverse.getTranspose()*physx::PxVec3(polygon.mPlane[0],polygon.mPlane[1],polygon.mPlane[2]);
					surface.normalize();
				}
			}
			else if ((geometry.getType() == physx::PxGeometryType::eTRIANGLEMESH ||
				geometry.getType() == physx::PxGeometryType::eHEIGHTFIELD) && hit.faceIndex != 0xFFFFFFFFu)
			{
				physx::PxTriangle triangle;
				if (geometry.getType() == physx::PxGeometryType::eTRIANGLEMESH)
					physx::PxMeshQuery::getTriangle(static_cast<const physx::PxTriangleMeshGeometry&>(geometry),pose,hit.faceIndex,triangle);
				else physx::PxMeshQuery::getTriangle(static_cast<const physx::PxHeightFieldGeometry&>(geometry),pose,hit.faceIndex,triangle);
				auto normal = (triangle.verts[1]-triangle.verts[0]).cross(triangle.verts[2]-triangle.verts[0]);
				if (normal.normalize() > 1.e-8f)
				{
					if (normal.dot(physx::PxVec3(contactNormal.x,contactNormal.y,contactNormal.z))<0) normal=-normal;
					return {normal.x,normal.y,normal.z};
				}
			}
			const auto world = pose.q.rotate(surface);
			return {world.x,world.y,world.z};
		}

		// 仅在首次命中为重叠时枚举 MTD；正常移动仍用最接近命中的快速路径。
		class PenetrationSweepCallback final : public physx::PxSweepCallback
		{
		public:
			PenetrationSweepCallback(const glm::vec3& direction, bool ignoreMovingOut,
				const physx::PxCapsuleGeometry& geometry,const physx::PxTransform& pose)
				: physx::PxSweepCallback(m_Buffer.data(),static_cast<physx::PxU32>(m_Buffer.size())),
				m_Direction(direction.x,direction.y,direction.z),m_IgnoreMovingOut(ignoreMovingOut),
				m_Geometry(geometry),m_Pose(pose) {}

			void Consume(const physx::PxSweepHit* values,physx::PxU32 count)
			{
				for(physx::PxU32 i=0;i<count;++i)
				{
					auto value=values[i];
					const bool penetrating=value.hadInitialOverlap();
					if(penetrating && value.shape && value.actor)
					{
						physx::PxVec3 normal;float depth=0;
						if(physx::PxGeometryQuery::computePenetration(normal,depth,m_Geometry,m_Pose,
							value.shape->getGeometry(),value.actor->getGlobalPose()*value.shape->getLocalPose()))
						{
							value.normal=normal;
							// 半径、轴旋转和端点相加会把恰好相切产生为几 ULP 的正深度。
							// 将不可分辨的接触深度保留为零，让移动层执行零深度穿透的既定修正。
							const float contactRoundoff=4.f*std::numeric_limits<float>::epsilon()*
								(m_Geometry.radius+m_Geometry.halfHeight);
							value.distance=depth>contactRoundoff ? -depth : -0.f;
						}
					}
					const float opposing=value.normal.dot(m_Direction);
					if(penetrating && m_IgnoreMovingOut && opposing>0) continue;
					if(!found || (penetrating && (!best.hadInitialOverlap() || opposing<m_BestOpposing)) ||
						(!penetrating && !best.hadInitialOverlap() && value.distance<best.distance))
					{
						best=value;found=true;m_BestOpposing=opposing;
					}
				}
			}
			physx::PxAgain processTouches(const physx::PxSweepHit* values,physx::PxU32 count) override
			{
				Consume(values,count);return true;
			}
			bool found=false;
			physx::PxSweepHit best;
		private:
			std::array<physx::PxSweepHit,8> m_Buffer;
			physx::PxVec3 m_Direction;
			bool m_IgnoreMovingOut=false;
			float m_BestOpposing=0;
			const physx::PxCapsuleGeometry& m_Geometry;
			const physx::PxTransform& m_Pose;
		};

		VansPhysicsQueryHit BuildOverlapHit(
			const physx::PxOverlapHit& nativeHit,
			const ControllerActors& controllers)
		{
			VansPhysicsQueryHit hit;
			PopulateCommonHit(nativeHit.shape, nativeHit.actor, controllers, hit);
			if (nativeHit.actor && nativeHit.shape)
			{
				const auto p = (nativeHit.actor->getGlobalPose() * nativeHit.shape->getLocalPose()).p;
				hit.position = {p.x,p.y,p.z};
			}
			return hit;
		}
	}

	bool VansPhysicsQuery::GetBodyMotionLocked(const void* identity,std::uint32_t transformId,
		VansPhysicsQueryHit& motion)
	{
		motion={};
		auto& physics=VansPhysicsSystem::GetInstance();
		auto* scene=VansPhysicsNativeAccess::Scene(physics);
		if (!scene || !identity) return false;
		physx::PxSceneReadLock lock(*scene);
		const auto controllers=CollectControllerActors(VansPhysicsNativeAccess::ControllerManager(physics));
		std::array<physx::PxActor*,64> page;
		// Movement bases are normally movable: avoid traversing static scene geometry for those reads.
		for (const auto type:{physx::PxActorTypeFlag::eRIGID_DYNAMIC,physx::PxActorTypeFlag::eRIGID_STATIC})
		{
			const auto count=scene->getNbActors(type);
			for (physx::PxU32 start=0;start<count;)
			{
				const auto read=scene->getActors(type,page.data(),static_cast<physx::PxU32>(page.size()),start);
				if (!read) break;
				start+=read;
				for (physx::PxU32 index=0;index<read;++index)
				{
					if (page[index]!=identity) continue;
					const auto* actor=page[index]->is<physx::PxRigidActor>();
					if (!actor || BuildCandidate(nullptr,actor,controllers).transformId!=transformId ||
						actor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION)) return false;
					std::array<physx::PxShape*,16> shapes;
					for (physx::PxU32 first=0;first<actor->getNbShapes();)
					{
						const auto shapeCount=actor->getShapes(shapes.data(),static_cast<physx::PxU32>(shapes.size()),first);
						if (!shapeCount) break;
						first+=shapeCount;
						for (physx::PxU32 shapeIndex=0;shapeIndex<shapeCount;++shapeIndex)
						{
							if (!shapes[shapeIndex]->getFlags().isSet(physx::PxShapeFlag::eSCENE_QUERY_SHAPE)) continue;
							PopulateCommonHit(shapes[shapeIndex],actor,controllers,motion);
							return true;
						}
					}
					return false;
				}
			}
		}
		return false;
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
		auto& physics = VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		return RaycastClosestLocked(request, hit);
	}

	bool VansPhysicsQuery::RaycastClosestLocked(
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
		{
			hit = BuildHit(nativeHit.block, controllers);
			hit.impactNormal = OpposingSurfaceNormal(nativeHit.block,direction,hit.normal);
		}
		return blocked;
	}

	bool VansPhysicsQuery::SweepCapsuleClosest(const VansPhysicsCapsuleSweepRequest& request,VansPhysicsQueryHit& hit)
	{
		auto& physics=VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		return SweepCapsuleClosestLocked(request,hit);
	}

	bool VansPhysicsQuery::SweepCapsuleClosestLocked(const VansPhysicsCapsuleSweepRequest& request,VansPhysicsQueryHit& hit)
	{
		hit={};
		const auto finite=[](const glm::vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
		const float directionLength=glm::length(request.direction),axisLength=glm::length(request.axis);
		if(!finite(request.origin)||!finite(request.direction)||!finite(request.axis)||
			!std::isfinite(request.distance)||request.distance<=0||
			!std::isfinite(request.radius)||request.radius<=0||
			!std::isfinite(request.halfHeight)||request.halfHeight<request.radius||
			!std::isfinite(directionLength)||directionLength<=1e-6f||
			!std::isfinite(axisLength)||axisLength<=1e-6f||request.filter.layerMask==0)return false;
		auto& physics=VansPhysicsSystem::GetInstance();auto* scene=VansPhysicsNativeAccess::Scene(physics);
		if(!scene)return false;
		physx::PxSceneReadLock sceneReadLock(*scene);
		const auto controllers=CollectControllerActors(VansPhysicsNativeAccess::ControllerManager(physics));
		QueryFilterCallback callback(request.filter,controllers,physx::PxQueryHitType::eBLOCK);
		const glm::vec3 direction=request.direction/directionLength;
		const auto rotation=glm::quat(glm::vec3(1,0,0),request.axis/axisLength);
		physx::PxQuat nativeRotation(rotation.x,rotation.y,rotation.z,rotation.w);
		nativeRotation.normalize();
		physx::PxSweepBuffer result;
		const physx::PxCapsuleGeometry geometry(request.radius,request.halfHeight-request.radius);
		const physx::PxTransform pose(physx::PxVec3(request.origin.x,request.origin.y,request.origin.z),
			nativeRotation);
		const physx::PxVec3 nativeDirection(direction.x,direction.y,direction.z);
		auto hitFlags=physx::PxHitFlags(physx::PxHitFlag::eDEFAULT);
		if(request.computePenetration || request.ignoreInitialOverlapsMovingOut)hitFlags|=physx::PxHitFlag::eMTD;
		const bool blocked=scene->sweep(geometry,pose,
			physx::PxVec3(direction.x,direction.y,direction.z),request.distance,result,hitFlags,
			BuildFilterData(request.filter),&callback)&&result.hasBlock;
		if(blocked && result.block.hadInitialOverlap() &&
			(request.computePenetration || request.ignoreInitialOverlapsMovingOut))
		{
			QueryFilterCallback penetrationFilter(request.filter,controllers,physx::PxQueryHitType::eTOUCH);
			PenetrationSweepCallback penetration(direction,request.ignoreInitialOverlapsMovingOut,geometry,pose);
			auto filter=BuildFilterData(request.filter);filter.flags|=physx::PxQueryFlag::eNO_BLOCK;
			scene->sweep(geometry,pose,nativeDirection,request.distance,penetration,
				physx::PxHitFlag::eDEFAULT|physx::PxHitFlag::eMTD,filter,&penetrationFilter);
			penetration.Consume(penetration.touches,penetration.nbTouches);
			if(!penetration.found) return false;
			hit=BuildHit(penetration.best,controllers);
			hit.impactNormal=OpposingSurfaceNormal(penetration.best,direction,hit.normal);
			return true;
		}
		if(blocked)
		{
			hit=BuildHit(result.block,controllers);
			hit.impactNormal=OpposingSurfaceNormal(result.block,direction,hit.normal);
		}
		return blocked;
	}

	bool VansPhysicsQuery::OverlapCapsuleAny(const VansPhysicsCapsuleOverlapRequest& request)
	{
		auto& physics=VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		return OverlapCapsuleAnyLocked(request);
	}

	bool VansPhysicsQuery::OverlapCapsuleAnyLocked(const VansPhysicsCapsuleOverlapRequest& request)
	{
		const float axisLength=glm::length(request.axis);
		if(!std::isfinite(request.center.x)||!std::isfinite(request.center.y)||!std::isfinite(request.center.z)||
			!std::isfinite(axisLength)||axisLength<=1.e-6f||!std::isfinite(request.radius)||request.radius<=0||
			!std::isfinite(request.halfHeight)||request.halfHeight<request.radius||request.filter.layerMask==0) return false;
		auto& physics=VansPhysicsSystem::GetInstance();auto* scene=VansPhysicsNativeAccess::Scene(physics);
		if(!scene)return false;
		physx::PxSceneReadLock sceneReadLock(*scene);
		const auto controllers=CollectControllerActors(VansPhysicsNativeAccess::ControllerManager(physics));
		QueryFilterCallback callback(request.filter,controllers,physx::PxQueryHitType::eTOUCH);
		auto filter=BuildFilterData(request.filter);filter.flags|=physx::PxQueryFlag::eANY_HIT;
		const auto rotation=glm::quat(glm::vec3(1,0,0),request.axis/axisLength);
		physx::PxQuat nativeRotation(rotation.x,rotation.y,rotation.z,rotation.w);
		nativeRotation.normalize();
		physx::PxOverlapBuffer result;
		return scene->overlap(physx::PxCapsuleGeometry(request.radius,request.halfHeight-request.radius),
			physx::PxTransform(physx::PxVec3(request.center.x,request.center.y,request.center.z),
				nativeRotation),result,filter,&callback);
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
				{
					results[index].value = BuildHit(nativeHit.block, controllers);
					results[index].value.impactNormal = OpposingSurfaceNormal(nativeHit.block,direction,
						results[index].value.normal);
				}
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

	std::size_t VansPhysicsQuery::ApplyRadialImpulse(const VansPhysicsRadialImpulseRequest& request)
	{
		if (!std::isfinite(glm::length(request.center)) || !std::isfinite(request.radius) || request.radius <= 0
			|| !std::isfinite(request.impulse) || request.impulse < 0 || !std::isfinite(request.maxVelocityChange)
			|| request.maxVelocityChange <= 0 || !std::isfinite(request.upwardBias) || request.upwardBias < 0) return 0;
		auto& physics = VansPhysicsSystem::GetInstance();
		auto* scene = VansPhysicsNativeAccess::Scene(physics);
		if (!scene) return 0;
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		physx::PxSceneWriteLock writeLock(*scene);
		const auto controllers = CollectControllerActors(VansPhysicsNativeAccess::ControllerManager(physics));
		auto filter = request.filter;
		filter.includeStatic = false; filter.includeTriggers = false; filter.includeControllers = false;
		QueryFilterCallback callback(filter, controllers, physx::PxQueryHitType::eTOUCH);
		// Drain PhysX's overlap pages so a crowded scene never drops actors silently.
		struct Collector final : physx::PxOverlapCallback
		{
			physx::PxOverlapHit page[128];
			std::vector<physx::PxOverlapHit> hits;
			Collector() : physx::PxOverlapCallback(page,128) {}
			bool processTouches(const physx::PxOverlapHit* values,physx::PxU32 count) override
			{ hits.insert(hits.end(),values,values+count); return true; }
		} collector;
		const physx::PxVec3 center(request.center.x,request.center.y,request.center.z);
		scene->overlap(physx::PxSphereGeometry(request.radius),physx::PxTransform(center),collector,
			BuildFilterData(filter),&callback);
		collector.hits.insert(collector.hits.end(),collector.touches,collector.touches+collector.nbTouches);
		std::unordered_set<physx::PxRigidActor*> seen;
		std::size_t applied = 0;
		for (const auto& hit : collector.hits)
		{
			auto* body = hit.actor ? hit.actor->is<physx::PxRigidDynamic>() : nullptr;
			if (!body || !seen.insert(body).second || body->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)
				|| body->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION)) continue;
			const auto point = (body->getGlobalPose()*body->getCMassLocalPose()).p;
			auto direction = point-center;
			const float distance = direction.magnitude();
			if (!std::isfinite(distance) || distance >= request.radius) continue;
			if (request.blockingLayerMask && distance > 0.001f)
			{
				VansPhysicsQueryFilter blocker;
				blocker.layerMask=request.blockingLayerMask; blocker.includeTriggers=false; blocker.includeControllers=false;
				blocker.accept=[body](const auto& candidate) { return candidate.actorIdentity != body; };
				QueryFilterCallback obstruction(blocker,controllers,physx::PxQueryHitType::eBLOCK);
				physx::PxRaycastBuffer ray;
				if (scene->raycast(center,direction/distance,distance-0.001f,ray,physx::PxHitFlag::eDEFAULT,
					BuildFilterData(blocker),&obstruction) && ray.hasBlock) continue;
			}
			direction = distance > 0.001f ? direction/distance : physx::PxVec3(0,1,0);
			direction.y += request.upwardBias;
			if (direction.magnitudeSquared() < 1.0e-12f) direction = physx::PxVec3(0,1,0);
			else direction.normalize();
			const float strength = (std::min)(request.impulse*(1-distance/request.radius),
				body->getMass()*request.maxVelocityChange);
			if (strength <= 0) continue;
			body->addForce(direction*strength,physx::PxForceMode::eIMPULSE,true);
			++applied;
		}
		return applied;
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
