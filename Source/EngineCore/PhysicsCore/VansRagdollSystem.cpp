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
#include <fstream>
#include <vector>

using namespace physx;
using namespace VansEngine;
using namespace VansGraphics;

namespace
{
	glm::vec3 ReadVec3(const nlohmann::json& source, const char* key, const glm::vec3& defaultValue)
	{
		if (!source.contains(key) || !source[key].is_array() || source[key].size() < 3)
			return defaultValue;

		return glm::vec3(
			source[key][0].get<float>(),
			source[key][1].get<float>(),
			source[key][2].get<float>());
	}

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
		uintptr_t raw = reinterpret_cast<uintptr_t>(animNode);
		return 0x52000000u | static_cast<PxU32>((raw >> 4) & 0x00FFFFFFu);
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

bool RagdollProfile::LoadFromFile(const std::string& filePath, RagdollProfile& out)
{
	std::ifstream file(filePath);
	if (!file.is_open())
	{
		VANS_LOG_WARN("[RagdollProfile] 无法打开文件: " << filePath);
		return false;
	}

	nlohmann::json root;
	try
	{
		file >> root;
	}
	catch (const std::exception& e)
	{
		VANS_LOG_WARN("[RagdollProfile] JSON 解析失败: " << filePath << " error=" << e.what());
		return false;
	}

	return LoadFromJson(root, out);
}

bool RagdollProfile::LoadFromJson(const nlohmann::json& j, RagdollProfile& out)
{
	if (!j.is_object())
		return false;

	RagdollProfile profile;
	profile.name = j.value("name", "RagdollProfile");

	if (j.contains("bodies") && j["bodies"].is_array())
	{
		for (const auto& item : j["bodies"])
		{
			RagdollBodyConfig body;
			body.boneName = item.value("bone_name", item.value("boneName", ""));
			body.shapeType = item.value("shape_type", item.value("shapeType", "capsule"));
			body.capsuleRadius = item.value("capsule_radius", item.value("capsuleRadius", body.capsuleRadius));
			body.capsuleHalfHeight = item.value("capsule_half_height", item.value("capsuleHalfHeight", body.capsuleHalfHeight));
			body.boxExtents = ReadVec3(item, "box_extents", ReadVec3(item, "boxExtents", body.boxExtents));
			body.sphereRadius = item.value("sphere_radius", item.value("sphereRadius", body.sphereRadius));
			body.mass = item.value("mass", body.mass);
			body.staticFriction = item.value("static_friction", item.value("staticFriction", body.staticFriction));
			body.dynamicFriction = item.value("dynamic_friction", item.value("dynamicFriction", body.dynamicFriction));
			body.restitution = item.value("restitution", body.restitution);
			body.offsetPosition = ReadVec3(item, "offset_position", ReadVec3(item, "offsetPosition", body.offsetPosition));
			body.offsetRotation = ReadVec3(item, "offset_rotation", ReadVec3(item, "offsetRotation", body.offsetRotation));
			body.layerName = item.value("layer", item.value("layerName", body.layerName));

			if (body.boneName.empty())
			{
				VANS_LOG_WARN("[RagdollProfile] 跳过缺少 bone_name 的 body 配置");
				continue;
			}
			profile.bodies.push_back(std::move(body));
		}
	}

	if (j.contains("joints") && j["joints"].is_array())
	{
		for (const auto& item : j["joints"])
		{
			RagdollJointConfig joint;
			joint.childBoneName = item.value("child_bone_name", item.value("child_bone", item.value("childBoneName", "")));
			joint.swingYLimit = item.value("swing_y_limit", item.value("swingYLimit", joint.swingYLimit));
			joint.swingZLimit = item.value("swing_z_limit", item.value("swingZLimit", joint.swingZLimit));
			joint.twistLowLimit = item.value("twist_low_limit", item.value("twistLowLimit", joint.twistLowLimit));
			joint.twistHighLimit = item.value("twist_high_limit", item.value("twistHighLimit", joint.twistHighLimit));
			joint.limitStiffness = item.value("limit_stiffness", item.value("limitStiffness", joint.limitStiffness));
			joint.limitDamping = item.value("limit_damping", item.value("limitDamping", joint.limitDamping));
			joint.projectionTolerance = item.value("projection_tolerance", item.value("projectionTolerance", joint.projectionTolerance));
			joint.enableDrive = item.value("enable_drive", item.value("enableDrive", joint.enableDrive));
			joint.driveStiffness = item.value("drive_stiffness", item.value("driveStiffness", joint.driveStiffness));
			joint.driveDamping = item.value("drive_damping", item.value("driveDamping", joint.driveDamping));
			joint.driveForceLimit = item.value("drive_force_limit", item.value("driveForceLimit", joint.driveForceLimit));

			if (joint.childBoneName.empty())
			{
				VANS_LOG_WARN("[RagdollProfile] 跳过缺少 child_bone 的 joint 配置");
				continue;
			}
			profile.joints.push_back(std::move(joint));
		}
	}

	if (profile.bodies.empty())
	{
		VANS_LOG_WARN("[RagdollProfile] profile '" << profile.name << "' 没有有效 body");
		return false;
	}

	out = std::move(profile);
	return true;
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

	for (const auto& bodyConfig : profile.bodies)
	{
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
		filterData.word2 = 0;
		filterData.word3 = ragdollCollisionGroup;
		shape->setSimulationFilterData(filterData);
		shape->setQueryFilterData(filterData);

		body->attachShape(*shape);
		shape->release();
		PxRigidBodyExt::setMassAndUpdateInertia(*body, (std::max)(0.001f, bodyConfig.mass));
		body->setSolverIterationCounts(12, 4);
		body->setLinearDamping(0.05f);
		body->setAngularDamping(0.2f);
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
		PxTransform parentFrame = parentPose.transformInv(childPose);
		PxTransform childFrame(PxIdentity);

		PxD6Joint* joint = PxD6JointCreate(*physics, parentEntry.body, parentFrame, childEntry.body, childFrame);
		if (joint == nullptr)
			continue;

		joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);

		joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
		joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);
		joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);

		PxSpring spring(jointConfig.limitStiffness, jointConfig.limitDamping);
		joint->setSwingLimit(PxJointLimitCone(glm::radians(jointConfig.swingYLimit),
		                                     glm::radians(jointConfig.swingZLimit),
		                                     spring));
		joint->setTwistLimit(PxJointAngularLimitPair(glm::radians(jointConfig.twistLowLimit),
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

	controller->FeedExternalBoneWorldTransforms(modelTransforms, skeleton);
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
	controller->FeedExternalBoneWorldTransforms(blended, skeleton);
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
		PxVec3 angularVelocity(0.0f);
		if (!hasInitialVelocity)
		{
			if (entry.boneName == "pelvis" || entry.boneName == "spine_01" || entry.boneName == "spine_02" || entry.boneName == "spine_03")
				angularVelocity = PxVec3(0.0f, 0.0f, 1.4f);
		}
		entry.body->setAngularVelocity(angularVelocity, true);
		entry.body->clearForce(PxForceMode::eFORCE);
		entry.body->clearTorque(PxForceMode::eFORCE);
		if (!hasInitialVelocity && entry.boneName == "spine_03")
			entry.body->addForce(PxVec3(0.0f, 0.0f, -18.0f), PxForceMode::eIMPULSE, true);
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
