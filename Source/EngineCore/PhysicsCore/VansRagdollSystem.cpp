#include "VansRagdollSystem.h"

#include "VansCollisionLayerManager.h"
#include "VansPhysics.h"
#include <extensions/PxD6Joint.h>
#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/matrix_decompose.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace physx;
using namespace VansEngine;
using namespace VansGraphics;

namespace
{
	constexpr PxU32 kFullCollisionPositionIterations = 255;
	constexpr PxU32 kFullCollisionVelocityIterations = 16;
	static_assert(kFullCollisionPositionIterations > 0 && kFullCollisionPositionIterations <= 255 &&
		kFullCollisionVelocityIterations <= 255, "PhysX solver iteration counts exceed their packed range");
	PxVec3 ToPxVec3(const glm::vec3& v)
	{
		return PxVec3(v.x, v.y, v.z);
	}

	PxQuat ToPxQuat(const glm::quat& q)
	{
		return PxQuat(q.x, q.y, q.z, q.w);
	}

	glm::quat ToGlmQuat(const PxQuat& q)
	{
		return glm::quat(q.w, q.x, q.y, q.z);
	}

	bool IsFiniteMatrix(const glm::mat4& m)
	{
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				if (!std::isfinite(m[c][r]))
					return false;
			}
		}
		return true;
	}

	glm::mat4 ComposeTRS(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), pos)
			* glm::toMat4(glm::normalize(rot))
			* glm::scale(glm::mat4(1.0f), scale);
	}

	glm::mat4 ConvertWorldBoneToModelTransform(const glm::mat4& boneWorld,
	                                           const glm::mat4& rootWorld,
	                                           const glm::mat4& referenceModelTransform)
	{
		glm::vec3 rootScale;
		glm::quat rootRotation;
		glm::vec3 rootPosition;
		glm::vec3 rootSkew;
		glm::vec4 rootPerspective;
		glm::decompose(rootWorld, rootScale, rootRotation, rootPosition, rootSkew, rootPerspective);
		rootRotation = glm::normalize(rootRotation);

		glm::vec3 boneScale;
		glm::quat boneRotation;
		glm::vec3 bonePosition;
		glm::vec3 boneSkew;
		glm::vec4 bonePerspective;
		glm::decompose(boneWorld, boneScale, boneRotation, bonePosition, boneSkew, bonePerspective);
		boneRotation = glm::normalize(boneRotation);

		glm::vec3 referenceScale;
		glm::quat referenceRotation;
		glm::vec3 referencePosition;
		glm::vec3 referenceSkew;
		glm::vec4 referencePerspective;
		glm::decompose(referenceModelTransform, referenceScale, referenceRotation, referencePosition, referenceSkew, referencePerspective);

		// PhysX 姿态不携带 scale，不能直接用 rootWorldInverse * boneWorld 生成骨骼矩阵。
		// 对带 0.01 这类对象缩放的角色，直接相乘会把 inverse scale 写进骨骼矩阵，导致模型瞬间放大/错乱。
		glm::vec3 modelPosition = glm::vec3(glm::inverse(rootWorld) * glm::vec4(bonePosition, 1.0f));
		glm::quat modelRotation = glm::normalize(glm::inverse(rootRotation) * boneRotation);
		return ComposeTRS(modelPosition, modelRotation, referenceScale);
	}

	PxU32 MakeRagdollCollisionGroup(const VansAnimationNode* animNode)
	{
		static PxU32 nextGroup = 1;
		const PxU32 group = nextGroup++ & 0xFFFFFFu;
		return group ? group : (nextGroup++ & 0xFFFFFFu);
	}

	void SetActorRagdollContactsEnabled(PxRigidDynamic* body, bool enabled)
	{
		if (body == nullptr)
			return;

		PxU32 shapeCount = body->getNbShapes();
		if (shapeCount == 0)
			return;

		std::vector<PxShape*> shapes(shapeCount);
		body->getShapes(shapes.data(), shapeCount);
		auto& layerMgr = VansCollisionLayerManager::Get();
		for (PxShape* shape : shapes)
		{
			if (shape == nullptr)
				continue;

			PxFilterData filterData = shape->getSimulationFilterData();
			int layerIndex = static_cast<int>(filterData.word0);
			filterData.word1 = enabled ? layerMgr.GetCollisionMask(layerIndex) : 0u;
			if (enabled && (filterData.word2 & 2u)) filterData.word1 |= 1u << layerIndex;
			shape->setSimulationFilterData(filterData);
			shape->setQueryFilterData(filterData);
		}

		PxScene* scene = body->getScene();
		if (scene != nullptr)
			scene->resetFiltering(*body);
	}

	bool HasInitialVelocity(const glm::vec3& initialVelocity)
	{
		return glm::dot(initialVelocity, initialVelocity) > 0.0001f;
	}

	std::vector<glm::mat4> BuildLocalModelTransforms(const Skeleton& skeleton,
	                                                const std::vector<glm::mat4>& modelTransforms)
	{
		std::vector<glm::mat4> localTransforms(modelTransforms.size(), glm::mat4(1.0f));
		for (size_t i = 0; i < modelTransforms.size(); ++i)
		{
			int parentIndex = skeleton.bones[i].parentIndex;
			if (parentIndex >= 0 && parentIndex < static_cast<int>(modelTransforms.size()))
				localTransforms[i] = glm::inverse(modelTransforms[parentIndex]) * modelTransforms[i];
			else
				localTransforms[i] = modelTransforms[i];
		}
		return localTransforms;
	}

	void PropagateDrivenDescendants(const RagdollInstance& inst,
	                              const Skeleton& skeleton,
	                              const std::vector<glm::mat4>& sourceLocalTransforms,
	                              std::vector<glm::mat4>& modelTransforms)
	{
		if (modelTransforms.size() != skeleton.bones.size() || sourceLocalTransforms.size() != skeleton.bones.size())
			return;

		std::vector<bool> driven(modelTransforms.size(), false);
		std::vector<bool> inherited(modelTransforms.size(), false);
		for (const auto& entry : inst.boneEntries)
		{
			if (entry.boneIndex >= 0 && entry.boneIndex < static_cast<int>(driven.size()))
				driven[entry.boneIndex] = true;
		}

		if (!skeleton.topologicalOrder.empty())
		{
			for (int boneIndex : skeleton.topologicalOrder)
			{
				if (boneIndex < 0 || boneIndex >= static_cast<int>(modelTransforms.size()))
					continue;

				int parentIndex = skeleton.bones[boneIndex].parentIndex;
				bool parentDriven = parentIndex >= 0 && parentIndex < static_cast<int>(inherited.size()) && inherited[parentIndex];
				if (driven[boneIndex])
				{
					inherited[boneIndex] = true;
					continue;
				}

				if (parentDriven)
				{
					modelTransforms[boneIndex] = modelTransforms[parentIndex] * sourceLocalTransforms[boneIndex];
					inherited[boneIndex] = true;
				}
			}
			return;
		}

		for (size_t boneIndex = 0; boneIndex < modelTransforms.size(); ++boneIndex)
		{
			int parentIndex = skeleton.bones[boneIndex].parentIndex;
			bool parentDriven = parentIndex >= 0 && parentIndex < static_cast<int>(inherited.size()) && inherited[parentIndex];
			if (driven[boneIndex])
			{
				inherited[boneIndex] = true;
				continue;
			}

			if (parentDriven)
			{
				modelTransforms[boneIndex] = modelTransforms[parentIndex] * sourceLocalTransforms[boneIndex];
				inherited[boneIndex] = true;
			}
		}
	}
}

VansRagdollSystem& VansRagdollSystem::GetInstance()
{
	static VansRagdollSystem instance;
	return instance;
}

void VansRagdollSystem::Initialize()
{
	// 当前系统没有额外全局资源，保留接口便于后续扩展。
}

void VansRagdollSystem::Shutdown()
{
	for (auto& inst : m_Instances)
		ReleaseInstance(inst);
	m_Instances.clear();
}

bool VansRagdollSystem::CreateRagdoll(VansAnimationNode* animNode, const RagdollProfile& profile)
{
	if (animNode == nullptr || animNode->GetController() == nullptr)
		return false;

	const Skeleton& skeleton = animNode->GetSkeleton();
	const auto& globalTransforms = animNode->GetController()->GetCachedGlobalTransforms();
	if (skeleton.bones.empty() || globalTransforms.size() != skeleton.bones.size())
	{
		VANS_LOG_WARN("[Ragdoll] 创建失败：动画缓存为空或骨骼数量不匹配 animNode=" << animNode->GetName());
		return false;
	}

	uint32_t rootTransformID = animNode->GetTransformID();
	if (rootTransformID >= VansTransformStore::GlobalTransforms.size())
	{
		VANS_LOG_WARN("[Ragdoll] 创建失败：AnimationNode 缺少有效 TransformID animNode=" << animNode->GetName());
		return false;
	}

	VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
	PxPhysics* physics = physicsSystem.GetPhysics();
	PxScene* scene = physicsSystem.GetScene();
	if (physics == nullptr || scene == nullptr)
	{
		VANS_LOG_WARN("[Ragdoll] 创建失败：PhysX 未初始化");
		return false;
	}

	std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

	auto existingIt = std::find_if(m_Instances.begin(), m_Instances.end(),
		[animNode](const RagdollInstance& inst) { return inst.animNode == animNode; });
	if (existingIt != m_Instances.end())
	{
		ReleaseInstance(*existingIt);
		m_Instances.erase(existingIt);
	}

	RagdollInstance inst;
	inst.animNode = animNode;
	inst.driveMode = RagdollDriveMode::Animation;
	inst.blendWeight = 0.0f;

	glm::mat4 rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();
	PxU32 ragdollCollisionGroup = MakeRagdollCollisionGroup(animNode);

	for (size_t bodyIndex = 0; bodyIndex < profile.bodies.size(); ++bodyIndex)
	{
		const auto& bodyConfig = profile.bodies[bodyIndex];
		auto boneIt = skeleton.boneNameToIndex.find(bodyConfig.boneName);
		if (boneIt == skeleton.boneNameToIndex.end())
		{
			VANS_LOG_WARN("[Ragdoll] 跳过不存在骨骼: " << bodyConfig.boneName);
			continue;
		}

		int boneIndex = boneIt->second;
		glm::mat4 shapeOffset = MakeTRS(bodyConfig.offsetPosition, bodyConfig.offsetRotation, glm::vec3(1.0f));
		glm::mat4 bodyWorld = rootWorld * globalTransforms[boneIndex] * shapeOffset;
		if (!IsFiniteMatrix(bodyWorld))
		{
			VANS_LOG_WARN("[Ragdoll] 跳过非法初始矩阵 bone=" << bodyConfig.boneName);
			continue;
		}

		PxTransform pxPose = GlmToPx(bodyWorld);
		PxRigidDynamic* body = physics->createRigidDynamic(pxPose);
		if (body == nullptr)
			continue;

		PxMaterial* material = physics->createMaterial(bodyConfig.staticFriction,
		                                             bodyConfig.dynamicFriction,
		                                             bodyConfig.restitution);
		PxGeometry* geometry = nullptr;
		PxCapsuleGeometry capsuleGeom;
		PxBoxGeometry boxGeom;
		PxSphereGeometry sphereGeom;
		if (bodyConfig.shapeType == "box")
		{
			boxGeom = PxBoxGeometry(bodyConfig.boxExtents.x, bodyConfig.boxExtents.y, bodyConfig.boxExtents.z);
			geometry = &boxGeom;
		}
		else if (bodyConfig.shapeType == "sphere")
		{
			sphereGeom = PxSphereGeometry(bodyConfig.sphereRadius);
			geometry = &sphereGeom;
		}
		else
		{
			capsuleGeom = PxCapsuleGeometry(bodyConfig.capsuleRadius, bodyConfig.capsuleHalfHeight);
			geometry = &capsuleGeom;
		}

		PxShape* shape = geometry ? physics->createShape(*geometry, *material) : nullptr;
		if (shape == nullptr)
		{
			body->release();
			material->release();
			continue;
		}

		auto& layerMgr = VansCollisionLayerManager::Get();
		int layerIndex = layerMgr.GetLayerIndex(bodyConfig.layerName);
		PxFilterData filterData;
		filterData.word0 = static_cast<PxU32>(layerIndex);
		// Animation 模式下保持 shape 在 broadphase 中，但先禁用接触过滤。
		// 切 Physics 时只更新 filterData，避免动态切换 eSIMULATION_SHAPE 触发 ABP 重新插入崩溃。
		filterData.word1 = 0u;
		filterData.word2 = profile.selfCollision ? 2u : 0u;
		filterData.word3 = ragdollCollisionGroup;
		shape->setSimulationFilterData(filterData);
		shape->setQueryFilterData(filterData);

		body->attachShape(*shape);
		shape->release();
		PxRigidBodyExt::setMassAndUpdateInertia(*body, (std::max)(0.001f, bodyConfig.mass));
		body->setMassSpaceInertiaTensor(body->getMassSpaceInertiaTensor() * bodyConfig.inertiaScale);
		// PhysX 的迭代计数必须在 1..255 内。
		body->setSolverIterationCounts(profile.selfCollision ? kFullCollisionPositionIterations : 12,
			profile.selfCollision ? kFullCollisionVelocityIterations : 4);
		body->setLinearDamping(0.05f);
		body->setAngularDamping(profile.selfCollision ? 2.0f : 0.2f);
		body->setMaxDepenetrationVelocity(2.0f);
		body->setSleepThreshold(0.0001f);
		body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		body->setName(bodyConfig.boneName.c_str());
		body->userData = nullptr;
		scene->addActor(*body);

		RagdollBoneEntry entry;
		entry.boneName = bodyConfig.boneName;
		entry.boneIndex = boneIndex;
		entry.body = body;
		entry.material = material;
		entry.shapeOffset = shapeOffset;
		entry.shapeOffsetInverse = glm::inverse(shapeOffset);
		entry.stationaryAngularVelocity = bodyConfig.stationaryAngularVelocity;
		entry.stationaryImpulse = bodyConfig.stationaryImpulse;

		int entryIndex = static_cast<int>(inst.boneEntries.size());
		inst.boneNameToEntryIndex[entry.boneName] = entryIndex;
		inst.boneEntries.push_back(entry);
	}

	if (inst.boneEntries.empty())
	{
		VANS_LOG_WARN("[Ragdoll] 创建失败：没有有效刚体 animNode=" << animNode->GetName());
		return false;
	}

	for (const auto& jointConfig : profile.joints)
	{
		auto childEntryIt = inst.boneNameToEntryIndex.find(jointConfig.childBoneName);
		if (childEntryIt == inst.boneNameToEntryIndex.end())
			continue;

		int childEntryIndex = childEntryIt->second;
		RagdollBoneEntry& childEntry = inst.boneEntries[childEntryIndex];
		int parentEntryIndex = FindNearestParentEntry(inst, skeleton, childEntry.boneIndex);
		if (parentEntryIndex < 0)
			continue;

		RagdollBoneEntry& parentEntry = inst.boneEntries[parentEntryIndex];
		PxTransform parentPose = parentEntry.body->getGlobalPose();
		PxTransform childPose = childEntry.body->getGlobalPose();
		// 关节固定在骨骼原点，不是偏移后的碰撞体中心；否则弯曲动作转物理时会被拉回。
		PxTransform jointWorld = childPose * GlmToPx(childEntry.shapeOffsetInverse);
		PxTransform parentFrame = parentPose.transformInv(jointWorld);
		PxTransform childFrame = childPose.transformInv(jointWorld);
		if (jointConfig.hasLocalFrames)
		{
			parentFrame = GlmToPx(parentEntry.shapeOffsetInverse * MakeTRS(
				jointConfig.parentFramePosition, jointConfig.parentFrameRotation, glm::vec3(1)));
			childFrame = GlmToPx(childEntry.shapeOffsetInverse * MakeTRS(
				jointConfig.childFramePosition, jointConfig.childFrameRotation, glm::vec3(1)));
		}

		PxD6Joint* joint = PxD6JointCreate(*physics, parentEntry.body, parentFrame, childEntry.body, childFrame);
		if (joint == nullptr)
			continue;

		joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, profile.selfCollision);

		joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eSWING1, jointConfig.swingYLimit > 0 ? PxD6Motion::eLIMITED : PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eSWING2, jointConfig.swingZLimit > 0 ? PxD6Motion::eLIMITED : PxD6Motion::eLOCKED);
		const bool lockTwist = jointConfig.twistLowLimit == 0.f && jointConfig.twistHighLimit == 0.f;
		joint->setMotion(PxD6Axis::eTWIST, lockTwist ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED);

		PxSpring spring(jointConfig.limitStiffness, jointConfig.limitDamping);
		joint->setSwingLimit(PxJointLimitCone(glm::radians((std::max)(0.01f, jointConfig.swingYLimit)),
		                                     glm::radians((std::max)(0.01f, jointConfig.swingZLimit)),
		                                     spring));
		if (!lockTwist) joint->setTwistLimit(PxJointAngularLimitPair(glm::radians(jointConfig.twistLowLimit),
		                                           glm::radians(jointConfig.twistHighLimit),
		                                           spring));
		if (jointConfig.enableDrive)
		{
			PxD6JointDrive drive(jointConfig.driveStiffness,
			                    jointConfig.driveDamping,
			                    jointConfig.driveForceLimit);
			joint->setDrive(PxD6Drive::eSLERP, drive);
		}

		childEntry.joint = joint;
	}

	m_Instances.push_back(std::move(inst));
	VANS_LOG("[Ragdoll] 已创建 profile='" << profile.name << "' animNode=" << animNode->GetName());
	return true;
}

void VansRagdollSystem::DestroyRagdoll(VansAnimationNode* animNode)
{
	if (animNode == nullptr)
		return;

	VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
	std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

	auto it = std::find_if(m_Instances.begin(), m_Instances.end(),
		[animNode](const RagdollInstance& inst) { return inst.animNode == animNode; });
	if (it == m_Instances.end())
		return;

	ReleaseInstance(*it);
	m_Instances.erase(it);
}

bool VansRagdollSystem::HasRagdoll(VansAnimationNode* animNode) const
{
	return FindInstance(animNode) != nullptr;
}

void VansRagdollSystem::SetDriveMode(VansAnimationNode* animNode,
                                      RagdollDriveMode mode,
                                      const glm::vec3& initialVelocity)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr)
	{
		VANS_LOG_WARN("[Ragdoll] SetDriveMode 失败：找不到运行时实例");
		return;
	}
	if (inst->driveMode == mode)
		return;

	VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
	std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

	RagdollDriveMode oldMode = inst->driveMode;
	if (oldMode == RagdollDriveMode::Animation &&
		(mode == RagdollDriveMode::Physics || mode == RagdollDriveMode::Blend))
	{
		WarmStartBodies(*inst, initialVelocity);
	}
	else if ((oldMode == RagdollDriveMode::Physics || oldMode == RagdollDriveMode::Blend) &&
		mode == RagdollDriveMode::Animation)
	{
		ReenableKinematic(*inst);
	}

	inst->driveMode = mode;
	VANS_LOG("[Ragdoll] DriveMode 切换完成 old=" << static_cast<int>(oldMode)
		<< " new=" << static_cast<int>(mode)
		<< " bodies=" << inst->boneEntries.size());
}

RagdollDriveMode VansRagdollSystem::GetDriveMode(VansAnimationNode* animNode) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	return inst ? inst->driveMode : RagdollDriveMode::Animation;
}

void VansRagdollSystem::SetBlendWeight(VansAnimationNode* animNode, float weight)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr)
		return;
	inst->blendWeight = glm::clamp(weight, 0.0f, 1.0f);
}

float VansRagdollSystem::GetBlendWeight(VansAnimationNode* animNode) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	return inst ? inst->blendWeight : 0.0f;
}

int VansRagdollSystem::GetBodyCount(VansAnimationNode* animNode) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	return inst ? static_cast<int>(inst->boneEntries.size()) : 0;
}

int VansRagdollSystem::GetJointCount(VansAnimationNode* animNode) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr)
		return 0;

	int jointCount = 0;
	for (const auto& entry : inst->boneEntries)
	{
		if (entry.joint != nullptr)
			jointCount++;
	}
	return jointCount;
}

RagdollDiagnostics VansRagdollSystem::GetDiagnostics(VansAnimationNode* animNode) const
{
	RagdollDiagnostics result;
	const auto* inst = FindInstance(animNode);
	if (!inst) return result;
	std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
	for (size_t i = 0; i < inst->boneEntries.size(); ++i)
	{
		const auto& entry = inst->boneEntries[i];
		result.maxLinearSpeed = (std::max)(result.maxLinearSpeed, entry.body->getLinearVelocity().magnitude());
		if (auto* joint = entry.joint)
		{
			PxRigidActor *a = nullptr, *b = nullptr; joint->getActors(a,b);
			const auto p = a->getGlobalPose()*joint->getLocalPose(PxJointActorIndex::eACTOR0);
			const auto q = b->getGlobalPose()*joint->getLocalPose(PxJointActorIndex::eACTOR1);
			const float anchorError = (p.p-q.p).magnitude();
			if (anchorError > result.maxAnchorError) { result.maxAnchorError=anchorError; result.anchorBone=entry.boneName; }
			const auto principal = [](float angle) { return std::remainder(angle, 2.f*PxPi); };
			const auto twist = joint->getTwistLimit(); const float angle = principal(joint->getTwistAngle());
			float error = joint->getMotion(PxD6Axis::eTWIST)==PxD6Motion::eLOCKED ? std::abs(angle) :
				(std::max)(twist.lower-angle,angle-twist.upper);
			const auto swing = joint->getSwingLimit();
			error = (std::max)(error,std::abs(principal(joint->getSwingYAngle()))-
				(joint->getMotion(PxD6Axis::eSWING1)==PxD6Motion::eLOCKED ? 0.f : swing.yAngle));
			error = (std::max)(error,std::abs(principal(joint->getSwingZAngle()))-
				(joint->getMotion(PxD6Axis::eSWING2)==PxD6Motion::eLOCKED ? 0.f : swing.zAngle));
			if (glm::degrees(error) > result.maxAngularErrorDegrees)
			{
				result.maxAngularErrorDegrees=glm::degrees(error); result.angularBone=entry.boneName;
				result.worstJointAnglesDegrees=glm::degrees(glm::vec3(angle,
					principal(joint->getSwingYAngle()),principal(joint->getSwingZAngle())));
			}
		}
		for (size_t j = i+1; j < inst->boneEntries.size(); ++j)
		{
			auto* other = inst->boneEntries[j].body;
			PxShape *a = nullptr, *b = nullptr; entry.body->getShapes(&a,1); other->getShapes(&b,1);
			PxPairFlags flags;
			if (VansCollisionFilterShader(0,a->getSimulationFilterData(),0,b->getSimulationFilterData(),flags,nullptr,0)&PxFilterFlag::eSUPPRESS) continue;
			++result.collidingBodyPairs;
			PxVec3 direction; PxReal depth;
			if (PxGeometryQuery::computePenetration(direction,depth,a->getGeometry(),entry.body->getGlobalPose()*a->getLocalPose(),
				b->getGeometry(),other->getGlobalPose()*b->getLocalPose()))
				if (depth > result.maxPenetration) { result.maxPenetration=depth; result.penetrationPair=entry.boneName+":"+inst->boneEntries[j].boneName; }
		}
	}
	return result;
}

std::vector<std::string> VansRagdollSystem::GetBodyBoneNames(VansAnimationNode* animNode) const
{
	std::vector<std::string> names;
	const RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr)
		return names;

	names.reserve(inst->boneEntries.size());
	for (const auto& entry : inst->boneEntries)
		names.push_back(entry.boneName);
	return names;
}

bool VansRagdollSystem::GetBoneWorldPosition(VansAnimationNode* animNode,
                                              const std::string& boneName,
                                              glm::vec3& outPos) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr)
		return false;

	auto it = inst->boneNameToEntryIndex.find(boneName);
	if (it == inst->boneNameToEntryIndex.end())
		return false;

	const RagdollBoneEntry& entry = inst->boneEntries[it->second];
	if (entry.body == nullptr)
		return false;

	// 直接读取 PhysX 刚体世界位置（质心）
	const PxTransform pose = entry.body->getGlobalPose();
	outPos = glm::vec3(pose.p.x, pose.p.y, pose.p.z);
	return true;
}

void VansRagdollSystem::ApplyImpulse(VansAnimationNode* animNode,
                                      const std::string& boneName,
                                      const glm::vec3& worldImpulse)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr || inst->driveMode == RagdollDriveMode::Animation)
		return;

	auto entryIt = inst->boneNameToEntryIndex.find(boneName);
	if (entryIt == inst->boneNameToEntryIndex.end())
		return;

	RagdollBoneEntry& entry = inst->boneEntries[entryIt->second];
	if (entry.body == nullptr)
		return;

	VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
	std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());
	entry.body->addForce(ToPxVec3(worldImpulse), PxForceMode::eIMPULSE, true);
}

bool VansRagdollSystem::AddLinearVelocity(VansAnimationNode* animNode, const glm::vec3& velocityDelta)
{
    const PxVec3 delta = ToPxVec3(velocityDelta);
    if (!delta.isFinite()) return false;
    auto* inst = FindInstance(animNode);
    if (!inst || inst->driveMode != RagdollDriveMode::Physics || inst->boneEntries.empty()) return false;
    std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
    // 先验证整组，再一次性叠加，避免部分节点成功而部分失败。
    for (const auto& entry : inst->boneEntries)
        if (!entry.body || entry.body->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC)
            || !(entry.body->getLinearVelocity() + delta).isFinite()) return false;
    for (auto& entry : inst->boneEntries)
        entry.body->setLinearVelocity(entry.body->getLinearVelocity() + delta, true);
    return true;
}

bool VansRagdollSystem::AddVelocityAtPosition(VansAnimationNode* animNode,
    const std::string& boneName, const glm::vec3& worldVelocityDelta,
    const glm::vec3& worldPosition, float maxAngularVelocityDelta)
{
	const PxVec3 delta = ToPxVec3(worldVelocityDelta), point = ToPxVec3(worldPosition);
	if (!delta.isFinite() || !point.isFinite() || !std::isfinite(maxAngularVelocityDelta)
		|| maxAngularVelocityDelta < 0.0f)
		return false;
	RagdollInstance* inst = FindInstance(animNode);
	if (!inst || inst->driveMode == RagdollDriveMode::Animation)
		return false;
	const auto found = inst->boneNameToEntryIndex.find(boneName);
	if (found == inst->boneNameToEntryIndex.end())
		return false;
	PxRigidDynamic* body = inst->boneEntries[found->second].body;
	std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
	if (!body || body->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
		return false;
	// 按质量换算冲量，使轻手掌和重躯干获得相同的线速度增量。
	PxVec3 linearDelta, angularDelta;
	PxRigidBodyExt::computeVelocityDeltaFromImpulse(*body, body->getGlobalPose(), point,
		delta * body->getMass(), 1.0f, 1.0f, linearDelta, angularDelta);
	if (!linearDelta.isFinite() || !angularDelta.isFinite())
		return false;
	const float angularSpeed = angularDelta.magnitude();
	if (!std::isfinite(angularSpeed))
		return false;
	if (angularSpeed > maxAngularVelocityDelta)
		angularDelta *= maxAngularVelocityDelta / angularSpeed;
	const PxVec3 linear = body->getLinearVelocity() + linearDelta;
	const PxVec3 angular = body->getAngularVelocity() + angularDelta;
	if (!linear.isFinite() || !angular.isFinite())
		return false;
	body->setLinearVelocity(linear, true);
	body->setAngularVelocity(angular, true);
	return true;
}

void VansRagdollSystem::PostAnimationUpdate(VansAnimationNode* animNode)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (inst == nullptr)
		return;

	VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
	std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

	switch (inst->driveMode)
	{
	case RagdollDriveMode::Animation:
		SyncBodiesToAnimPose(*inst);
		break;
	case RagdollDriveMode::Physics:
		SyncAnimToPhysicsPose(*inst);
		break;
	case RagdollDriveMode::Blend:
		BlendAndApplyPose(*inst);
		break;
	}
}

void VansRagdollSystem::SyncBodiesToAnimPose(RagdollInstance& inst)
{
	if (inst.animNode == nullptr || inst.animNode->GetController() == nullptr)
		return;

	const auto& globalTransforms = inst.animNode->GetController()->GetCachedGlobalTransforms();
	uint32_t rootTransformID = inst.animNode->GetTransformID();
	if (rootTransformID >= VansTransformStore::GlobalTransforms.size())
		return;

	glm::mat4 rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();
	for (auto& entry : inst.boneEntries)
	{
		if (entry.body == nullptr || entry.boneIndex < 0 || entry.boneIndex >= static_cast<int>(globalTransforms.size()))
			continue;

		glm::mat4 bodyWorld = rootWorld * globalTransforms[entry.boneIndex] * entry.shapeOffset;
		entry.body->setKinematicTarget(GlmToPx(bodyWorld));
	}
}

void VansRagdollSystem::SyncAnimToPhysicsPose(RagdollInstance& inst)
{
	if (inst.animNode == nullptr || inst.animNode->GetController() == nullptr)
		return;

	VansAnimationController* controller = inst.animNode->GetController();
	const Skeleton& skeleton = inst.animNode->GetSkeleton();
	std::vector<glm::mat4> modelTransforms = controller->GetCachedGlobalTransforms();
	if (modelTransforms.size() != skeleton.bones.size())
		return;
	std::vector<glm::mat4> sourceLocalTransforms = BuildLocalModelTransforms(skeleton, modelTransforms);

	uint32_t rootTransformID = inst.animNode->GetTransformID();
	if (rootTransformID >= VansTransformStore::GlobalTransforms.size())
		return;

	glm::mat4 rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();

	for (const auto& entry : inst.boneEntries)
	{
		if (entry.body == nullptr || entry.boneIndex < 0 || entry.boneIndex >= static_cast<int>(modelTransforms.size()))
			continue;

		glm::mat4 bodyWorld = PxToGlm(entry.body->getGlobalPose());
		glm::mat4 boneWorld = bodyWorld * entry.shapeOffsetInverse;
		modelTransforms[entry.boneIndex] = ConvertWorldBoneToModelTransform(boneWorld, rootWorld, modelTransforms[entry.boneIndex]);
	}
	PropagateDrivenDescendants(inst, skeleton, sourceLocalTransforms, modelTransforms);

	controller->SubmitExternalModelPose(
		modelTransforms, skeleton, 0.0f,
		VansGraphics::VansExternalPoseEvaluationMode::DirectFinalPose);
}

void VansRagdollSystem::BlendAndApplyPose(RagdollInstance& inst)
{
	if (inst.animNode == nullptr || inst.animNode->GetController() == nullptr)
		return;

	VansAnimationController* controller = inst.animNode->GetController();
	const Skeleton& skeleton = inst.animNode->GetSkeleton();
	std::vector<glm::mat4> animTransforms = controller->GetCachedGlobalTransforms();
	std::vector<glm::mat4> physTransforms = animTransforms;
	if (animTransforms.size() != skeleton.bones.size())
		return;
	std::vector<glm::mat4> sourceLocalTransforms = BuildLocalModelTransforms(skeleton, animTransforms);

	uint32_t rootTransformID = inst.animNode->GetTransformID();
	if (rootTransformID >= VansTransformStore::GlobalTransforms.size())
		return;

	glm::mat4 rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();

	for (const auto& entry : inst.boneEntries)
	{
		if (entry.body == nullptr || entry.boneIndex < 0 || entry.boneIndex >= static_cast<int>(physTransforms.size()))
			continue;

		glm::mat4 bodyWorld = PxToGlm(entry.body->getGlobalPose());
		glm::mat4 boneWorld = bodyWorld * entry.shapeOffsetInverse;
		physTransforms[entry.boneIndex] = ConvertWorldBoneToModelTransform(boneWorld, rootWorld, animTransforms[entry.boneIndex]);
	}
	PropagateDrivenDescendants(inst, skeleton, sourceLocalTransforms, physTransforms);

	std::vector<glm::mat4> blended;
	BlendModelTransforms(animTransforms, physTransforms, inst.blendWeight, blended);
	controller->SubmitExternalModelPose(
		blended, skeleton, 0.0f,
		VansGraphics::VansExternalPoseEvaluationMode::DirectFinalPose);
}

void VansRagdollSystem::WarmStartBodies(RagdollInstance& inst, const glm::vec3& initialVelocity)
{
	if (inst.animNode == nullptr || inst.animNode->GetController() == nullptr)
		return;

	const auto& globalTransforms = inst.animNode->GetController()->GetCachedGlobalTransforms();
	uint32_t rootTransformID = inst.animNode->GetTransformID();
	if (rootTransformID >= VansTransformStore::GlobalTransforms.size())
		return;

	glm::mat4 rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();
	bool hasInitialVelocity = HasInitialVelocity(initialVelocity);
	for (auto& entry : inst.boneEntries)
	{
		if (entry.body == nullptr || entry.boneIndex < 0 || entry.boneIndex >= static_cast<int>(globalTransforms.size()))
			continue;

		glm::mat4 bodyWorld = rootWorld * globalTransforms[entry.boneIndex] * entry.shapeOffset;
		entry.body->setGlobalPose(GlmToPx(bodyWorld), true);
		SetActorRagdollContactsEnabled(entry.body, true);
		entry.body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
		entry.body->setLinearVelocity(ToPxVec3(initialVelocity), true);
		PxVec3 angularVelocity = hasInitialVelocity ? PxVec3(0.0f) : ToPxVec3(entry.stationaryAngularVelocity);
		entry.body->setAngularVelocity(angularVelocity, true);
		entry.body->clearForce(PxForceMode::eFORCE);
		entry.body->clearTorque(PxForceMode::eFORCE);
		if (!hasInitialVelocity)
			entry.body->addForce(ToPxVec3(entry.stationaryImpulse), PxForceMode::eIMPULSE, true);
		entry.body->wakeUp();
	}
}

void VansRagdollSystem::ReenableKinematic(RagdollInstance& inst)
{
	for (auto& entry : inst.boneEntries)
	{
		if (entry.body == nullptr)
			continue;
		entry.body->setLinearVelocity(PxVec3(0.0f), true);
		entry.body->setAngularVelocity(PxVec3(0.0f), true);
		entry.body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		SetActorRagdollContactsEnabled(entry.body, false);
	}
}

void VansRagdollSystem::ReleaseInstance(RagdollInstance& inst)
{
	PxScene* scene = VansPhysicsSystem::GetInstance().GetScene();
	for (auto& entry : inst.boneEntries)
	{
		if (entry.joint != nullptr)
		{
			entry.joint->release();
			entry.joint = nullptr;
		}
	}

	for (auto& entry : inst.boneEntries)
	{
		if (entry.body != nullptr)
		{
			if (scene != nullptr)
				scene->removeActor(*entry.body);
			entry.body->release();
			entry.body = nullptr;
		}

		if (entry.material != nullptr)
		{
			entry.material->release();
			entry.material = nullptr;
		}
	}
	inst.boneEntries.clear();
	inst.boneNameToEntryIndex.clear();
}

RagdollInstance* VansRagdollSystem::FindInstance(VansAnimationNode* animNode)
{
	for (auto& inst : m_Instances)
	{
		if (inst.animNode == animNode)
			return &inst;
	}
	return nullptr;
}

const RagdollInstance* VansRagdollSystem::FindInstance(VansAnimationNode* animNode) const
{
	for (const auto& inst : m_Instances)
	{
		if (inst.animNode == animNode)
			return &inst;
	}
	return nullptr;
}

glm::mat4 VansRagdollSystem::MakeTRS(const glm::vec3& pos,
                                      const glm::vec3& rotDeg,
                                      const glm::vec3& scale)
{
	glm::mat4 result(1.0f);
	result = glm::translate(result, pos);
	result = glm::rotate(result, glm::radians(rotDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
	result = glm::rotate(result, glm::radians(rotDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
	result = glm::rotate(result, glm::radians(rotDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
	result = glm::scale(result, scale);
	return result;
}

int VansRagdollSystem::FindNearestParentEntry(const RagdollInstance& inst,
                                               const Skeleton& skeleton,
                                               int childBoneIndex)
{
	if (childBoneIndex < 0 || childBoneIndex >= static_cast<int>(skeleton.bones.size()))
		return -1;

	int parentIndex = skeleton.bones[childBoneIndex].parentIndex;
	while (parentIndex >= 0)
	{
		const BoneInfo& bone = skeleton.bones[parentIndex];
		auto it = inst.boneNameToEntryIndex.find(bone.name);
		if (it != inst.boneNameToEntryIndex.end())
			return it->second;
		parentIndex = bone.parentIndex;
	}
	return -1;
}

void VansRagdollSystem::BlendModelTransforms(const std::vector<glm::mat4>& a,
                                              const std::vector<glm::mat4>& b,
                                              float t,
                                              std::vector<glm::mat4>& out)
{
	out.clear();
	if (a.size() != b.size())
		return;

	float alpha = glm::clamp(t, 0.0f, 1.0f);
	out.resize(a.size(), glm::mat4(1.0f));
	for (size_t i = 0; i < a.size(); ++i)
	{
		glm::vec3 skewA, skewB;
		glm::vec4 perspectiveA, perspectiveB;
		glm::vec3 scaleA, scaleB;
		glm::quat rotA, rotB;
		glm::vec3 posA, posB;
		glm::decompose(a[i], scaleA, rotA, posA, skewA, perspectiveA);
		glm::decompose(b[i], scaleB, rotB, posB, skewB, perspectiveB);
		if (glm::dot(rotA, rotB) < 0.0f)
			rotB = -rotB;

		glm::vec3 pos = glm::mix(posA, posB, alpha);
		glm::quat rot = glm::normalize(glm::slerp(rotA, rotB, alpha));
		glm::vec3 scale = glm::mix(scaleA, scaleB, alpha);

		out[i] = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), scale);
	}
}

glm::mat4 VansRagdollSystem::PxToGlm(const PxTransform& t)
{
	glm::quat q = ToGlmQuat(t.q);
	glm::mat4 result = glm::toMat4(q);
	result[3] = glm::vec4(t.p.x, t.p.y, t.p.z, 1.0f);
	return result;
}

PxTransform VansRagdollSystem::GlmToPx(const glm::mat4& m)
{
	glm::vec3 scale;
	glm::quat rotation;
	glm::vec3 translation;
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(m, scale, rotation, translation, skew, perspective);
	rotation = glm::normalize(rotation);
	return PxTransform(ToPxVec3(translation), ToPxQuat(rotation));
}
