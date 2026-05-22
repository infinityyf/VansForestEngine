#include "VansRagdollSystem.h"
#include "VansPhysics.h"
#include "VansCollisionLayerManager.h"
#include "../AnimationCore/VansAnimationController.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <limits>

using namespace VansEngine;

// ════════════════════════════════════════════════════════════════
//  RagdollProfile — static loaders
// ════════════════════════════════════════════════════════════════

bool RagdollProfile::LoadFromFile(const std::string& filePath, RagdollProfile& out)
{
	std::ifstream file(filePath);
	if (!file.is_open())
	{
		VANS_LOG_WARN("[RagdollProfile] Cannot open: " << filePath);
		return false;
	}

	nlohmann::json j;
	try
	{
		file >> j;
	}
	catch (const nlohmann::json::parse_error& e)
	{
		VANS_LOG_ERROR("[RagdollProfile] JSON parse error in " << filePath << ": " << e.what());
		return false;
	}

	return LoadFromJson(j, out);
}

bool RagdollProfile::LoadFromJson(const nlohmann::json& j, RagdollProfile& out)
{
	out.name    = j.value("name", "");
	out.bodies.clear();
	out.joints.clear();

	// ── Bodies ──────────────────────────────────────────────────────
	if (j.contains("bodies") && j["bodies"].is_array())
	{
		for (const auto& bj : j["bodies"])
		{
			RagdollBodyConfig cfg;
			cfg.boneName          = bj.value("bone_name", "");
			cfg.shapeType         = bj.value("shape_type", "capsule");
			cfg.capsuleRadius     = bj.value("capsule_radius", 0.08f);
			cfg.capsuleHalfHeight = bj.value("capsule_half_height", 0.15f);
			cfg.sphereRadius      = bj.value("sphere_radius", 0.1f);
			cfg.mass              = bj.value("mass", 5.0f);
			cfg.layerName         = bj.value("layer", "Default");

			if (bj.contains("box_extents") && bj["box_extents"].is_array() && bj["box_extents"].size() >= 3)
				cfg.boxExtents = glm::vec3(bj["box_extents"][0], bj["box_extents"][1], bj["box_extents"][2]);

			if (bj.contains("offset_position") && bj["offset_position"].is_array() && bj["offset_position"].size() >= 3)
				cfg.offsetPosition = glm::vec3(bj["offset_position"][0], bj["offset_position"][1], bj["offset_position"][2]);

			if (bj.contains("offset_rotation") && bj["offset_rotation"].is_array() && bj["offset_rotation"].size() >= 3)
				cfg.offsetRotation = glm::vec3(bj["offset_rotation"][0], bj["offset_rotation"][1], bj["offset_rotation"][2]);

			if (!cfg.boneName.empty())
				out.bodies.push_back(std::move(cfg));
		}
	}

	// ── Joints ──────────────────────────────────────────────────────
	if (j.contains("joints") && j["joints"].is_array())
	{
		for (const auto& jj : j["joints"])
		{
			RagdollJointConfig cfg;
			cfg.childBoneName    = jj.value("child_bone", "");
			cfg.swingYLimit      = jj.value("swing_y_limit", 45.0f);
			cfg.swingZLimit      = jj.value("swing_z_limit", 45.0f);
			cfg.twistLowLimit    = jj.value("twist_low_limit", -30.0f);
			cfg.twistHighLimit   = jj.value("twist_high_limit", 30.0f);
			cfg.enableDrive      = jj.value("enable_drive", false);
			cfg.driveStiffness   = jj.value("drive_stiffness", 0.0f);
			cfg.driveDamping     = jj.value("drive_damping", 20.0f);
			cfg.driveForceLimit  = jj.value("drive_force_limit", PX_MAX_F32);

			if (!cfg.childBoneName.empty())
				out.joints.push_back(std::move(cfg));
		}
	}

	VANS_LOG("[RagdollProfile] Loaded profile '" << out.name << "' with "
	         << out.bodies.size() << " bodies and " << out.joints.size() << " joints");
	return true;
}

// ════════════════════════════════════════════════════════════════
//  VansRagdollSystem — singleton
// ════════════════════════════════════════════════════════════════

VansRagdollSystem& VansRagdollSystem::GetInstance()
{
	static VansRagdollSystem instance;
	return instance;
}

void VansRagdollSystem::Initialize()
{
	VANS_LOG("[RagdollSystem] Initialized");
}

void VansRagdollSystem::Shutdown()
{
	// Release all ragdoll instances
	while (!m_Instances.empty())
	{
		DestroyRagdoll(m_Instances.back().animNode);
	}
	VANS_LOG("[RagdollSystem] Shutdown complete");
}

// ════════════════════════════════════════════════════════════════
//  CreateRagdoll
// ════════════════════════════════════════════════════════════════

bool VansRagdollSystem::CreateRagdoll(VansGraphics::VansAnimationNode* animNode,
                                       const RagdollProfile& profile)
{
	if (!animNode)
	{
		VANS_LOG_WARN("[RagdollSystem] CreateRagdoll: null animNode");
		return false;
	}

	// Don't create twice
	if (HasRagdoll(animNode))
	{
		VANS_LOG_WARN("[RagdollSystem] CreateRagdoll: ragdoll already exists for '"
		              << animNode->GetName() << "', destroying old one first");
		DestroyRagdoll(animNode);
	}

	VansAnimationController* controller = animNode->GetController();
	if (!controller)
	{
		VANS_LOG_WARN("[RagdollSystem] CreateRagdoll: no controller on animNode '"
		              << animNode->GetName() << "'");
		return false;
	}

	const VansGraphics::Skeleton& skeleton = animNode->GetSkeleton();
	const std::vector<glm::mat4>& globalTransforms = controller->GetCachedGlobalTransforms();

	if (globalTransforms.empty() || globalTransforms.size() != skeleton.bones.size())
	{
		VANS_LOG_WARN("[RagdollSystem] CreateRagdoll: globalTransforms not ready for '"
		              << animNode->GetName() << "' (size=" << globalTransforms.size()
		              << " vs " << skeleton.bones.size() << " bones)");
		return false;
	}

	VansPhysicsSystem& physSys = VansPhysicsSystem::GetInstance();
	PxPhysics*  physics = physSys.GetPhysics();
	PxScene*    scene   = physSys.GetScene();
	if (!physics || !scene)
	{
		VANS_LOG_ERROR("[RagdollSystem] PhysX not initialized");
		return false;
	}

	// Character root world matrix
	glm::mat4 rootWorld = glm::mat4(1.0f);
	uint32_t rootTransformID = animNode->GetTransformID();
	if (rootTransformID < VansTransformStore::GlobalTransforms.size())
		rootWorld = VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();

	// ── Build bone entry map for joints (boneName → entry index) ────
	RagdollInstance inst;
	inst.animNode  = animNode;
	inst.driveMode = RagdollDriveMode::Animation;

	// ── Create one PxRigidDynamic per body config ───────────────────
	auto& layerMgr = VansCollisionLayerManager::Get();

	for (const RagdollBodyConfig& bodyCfg : profile.bodies)
	{
		auto it = skeleton.boneNameToIndex.find(bodyCfg.boneName);
		if (it == skeleton.boneNameToIndex.end())
		{
			VANS_LOG_WARN("[RagdollSystem] Bone '" << bodyCfg.boneName << "' not found in skeleton, skipping");
			continue;
		}
		int boneIndex = it->second;

		// Bone world transform: rootWorld * globalTransforms[boneIndex]
		glm::mat4 boneWorld = rootWorld * globalTransforms[boneIndex];

		// Shape-offset local matrix: compose from config offset position + rotation
		glm::mat4 shapeOffset = glm::mat4(1.0f);
		shapeOffset = glm::translate(shapeOffset, bodyCfg.offsetPosition);
		shapeOffset = glm::rotate(shapeOffset, glm::radians(bodyCfg.offsetRotation.z), glm::vec3(0, 0, 1));
		shapeOffset = glm::rotate(shapeOffset, glm::radians(bodyCfg.offsetRotation.y), glm::vec3(0, 1, 0));
		shapeOffset = glm::rotate(shapeOffset, glm::radians(bodyCfg.offsetRotation.x), glm::vec3(1, 0, 0));
		glm::mat4 shapeOffsetInv = glm::inverse(shapeOffset);

		// Body initial world transform = boneWorld * shapeOffset
		glm::mat4 bodyWorld  = boneWorld * shapeOffset;
		PxTransform bodyPose = GlmToPx(bodyWorld);

		// Create actor
		PxRigidDynamic* body = physics->createRigidDynamic(bodyPose);
		if (!body)
		{
			VANS_LOG_ERROR("[RagdollSystem] Failed to create PxRigidDynamic for bone '"
			               << bodyCfg.boneName << "'");
			continue;
		}

		// Default material
		PxMaterial* mat = physics->createMaterial(0.5f, 0.4f, 0.1f);

		// Create collision shape
		PxShape* shape = nullptr;
		if (bodyCfg.shapeType == "capsule")
		{
			PxCapsuleGeometry geom(bodyCfg.capsuleRadius, bodyCfg.capsuleHalfHeight);
			shape = physics->createShape(geom, *mat);
		}
		else if (bodyCfg.shapeType == "box")
		{
			PxBoxGeometry geom(bodyCfg.boxExtents.x, bodyCfg.boxExtents.y, bodyCfg.boxExtents.z);
			shape = physics->createShape(geom, *mat);
		}
		else  // sphere or fallback
		{
			PxSphereGeometry geom(bodyCfg.sphereRadius);
			shape = physics->createShape(geom, *mat);
		}

		if (shape)
		{
			// Apply collision layer filter
			int layerIdx = layerMgr.GetLayerIndex(bodyCfg.layerName);
			PxFilterData filterData;
			filterData.word0 = static_cast<PxU32>(1u << layerIdx);
			filterData.word1 = layerMgr.GetCollisionMask(layerIdx);
			shape->setSimulationFilterData(filterData);
			shape->setQueryFilterData(filterData);

			body->attachShape(*shape);
			shape->release();
		}

		// Set mass
		PxRigidBodyExt::setMassAndUpdateInertia(*body, bodyCfg.mass);

		// Start as kinematic — animation drives the pose
		body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

		// Add to scene
		{
			std::lock_guard<std::mutex> lock(physSys.GetSimulationMutex());
			scene->addActor(*body);
		}

		RagdollBoneEntry entry;
		entry.boneName          = bodyCfg.boneName;
		entry.boneIndex         = boneIndex;
		entry.body              = body;
		entry.material          = mat;
		entry.shapeOffset       = shapeOffset;
		entry.shapeOffsetInverse = shapeOffsetInv;

		int entryIdx = static_cast<int>(inst.boneEntries.size());
		inst.boneEntries.push_back(std::move(entry));
		inst.boneNameToEntryIndex[bodyCfg.boneName] = entryIdx;
	}

	if (inst.boneEntries.empty())
	{
		VANS_LOG_WARN("[RagdollSystem] No bone entries created for '" << animNode->GetName() << "'");
		return false;
	}

	// ── Create D6 joints ────────────────────────────────────────────
	for (const RagdollJointConfig& jntCfg : profile.joints)
	{
		auto childIt = inst.boneNameToEntryIndex.find(jntCfg.childBoneName);
		if (childIt == inst.boneNameToEntryIndex.end())
		{
			VANS_LOG_WARN("[RagdollSystem] Joint child bone '" << jntCfg.childBoneName << "' has no ragdoll body, skipping");
			continue;
		}

		int childIdx = childIt->second;
		RagdollBoneEntry& childEntry = inst.boneEntries[childIdx];

		// Find parent bone entry: walk up skeleton until we find a bone with a body
		int parentEntryIdx = -1;
		int boneIdx = childEntry.boneIndex;
		while (boneIdx >= 0)
		{
			int parentBoneIdx = skeleton.bones[boneIdx].parentIndex;
			if (parentBoneIdx < 0) break;

			const std::string& parentBoneName = skeleton.bones[parentBoneIdx].name;
			auto pIt = inst.boneNameToEntryIndex.find(parentBoneName);
			if (pIt != inst.boneNameToEntryIndex.end())
			{
				parentEntryIdx = pIt->second;
				break;
			}
			boneIdx = parentBoneIdx;
		}

		if (parentEntryIdx < 0)
		{
			VANS_LOG("[RagdollSystem] Joint for '" << jntCfg.childBoneName
			         << "' has no parent body — will be root body, skipping joint");
			continue;
		}

		RagdollBoneEntry& parentEntry = inst.boneEntries[parentEntryIdx];

		// Joint pivot at child body origin.
		// parentLocalFrame: child's world position expressed in parent body's local space
		PxTransform parentBodyPose  = parentEntry.body->getGlobalPose();
		PxTransform childBodyPose   = childEntry.body->getGlobalPose();

		// parentFrame: transform from parent body to joint pivot (child body origin)
		PxTransform parentFrameWorld = childBodyPose;
		PxTransform parentFrame = parentBodyPose.transformInv(parentFrameWorld);
		PxTransform childFrame  = PxTransform(PxIdentity);

		PxD6Joint* joint = PxD6JointCreate(*physics,
		    parentEntry.body, parentFrame,
		    childEntry.body,  childFrame);

		if (!joint)
		{
			VANS_LOG_WARN("[RagdollSystem] Failed to create D6 joint for '" << jntCfg.childBoneName << "'");
			continue;
		}

		// Lock linear axes (bodies don't translate relative to each other)
		joint->setMotion(PxD6Axis::eX,     PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eY,     PxD6Motion::eLOCKED);
		joint->setMotion(PxD6Axis::eZ,     PxD6Motion::eLOCKED);

		// Angular: limited swing + limited twist
		joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
		joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);
		joint->setMotion(PxD6Axis::eTWIST,  PxD6Motion::eLIMITED);

		// Swing limits (Y and Z axes)
		PxJointLimitCone swingLimit(
		    PxMath::degToRad(jntCfg.swingYLimit),
		    PxMath::degToRad(jntCfg.swingZLimit));
		swingLimit.stiffness = 20.0f;
		swingLimit.damping   = 2.0f;
		joint->setSwingLimit(swingLimit);

		// Twist limit
		PxJointAngularLimitPair twistLimit(
		    PxMath::degToRad(jntCfg.twistLowLimit),
		    PxMath::degToRad(jntCfg.twistHighLimit));
		twistLimit.stiffness = 20.0f;
		twistLimit.damping   = 2.0f;
		joint->setTwistLimit(twistLimit);

		// Optional drive (used in Blend mode to attract physics pose toward animation pose)
		if (jntCfg.enableDrive)
		{
			PxD6JointDrive drive(jntCfg.driveStiffness, jntCfg.driveDamping,
			                     jntCfg.driveForceLimit, /*acceleration=*/false);
			joint->setDrive(PxD6Drive::eSWING, drive);
			joint->setDrive(PxD6Drive::eTWIST, drive);
			joint->setDrive(PxD6Drive::eSLERP, drive);
		}

		joint->setProjectionLinearTolerance(0.1f);
		joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, true);

		childEntry.joint = joint;
		VANS_LOG("[RagdollSystem] Created D6 joint: '" << parentEntry.boneName
		         << "' → '" << childEntry.boneName << "'");
	}

	m_Instances.push_back(std::move(inst));

	VANS_LOG("[RagdollSystem] Ragdoll created for '" << animNode->GetName()
	         << "': " << m_Instances.back().boneEntries.size() << " bodies");
	return true;
}

// ════════════════════════════════════════════════════════════════
//  DestroyRagdoll
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::DestroyRagdoll(VansGraphics::VansAnimationNode* animNode)
{
	if (!animNode) return;

	auto it = std::find_if(m_Instances.begin(), m_Instances.end(),
	    [&](const RagdollInstance& i) { return i.animNode == animNode; });

	if (it == m_Instances.end()) return;

	VansPhysicsSystem& physSys = VansPhysicsSystem::GetInstance();
	PxScene* scene = physSys.GetScene();

	std::lock_guard<std::mutex> lock(physSys.GetSimulationMutex());

	for (auto& entry : it->boneEntries)
	{
		if (entry.joint)
		{
			entry.joint->release();
			entry.joint = nullptr;
		}
		if (entry.body)
		{
			if (scene) scene->removeActor(*entry.body);
			entry.body->release();
			entry.body = nullptr;
		}
		if (entry.material)
		{
			entry.material->release();
			entry.material = nullptr;
		}
	}

	m_Instances.erase(it);
	VANS_LOG("[RagdollSystem] Ragdoll destroyed for '" << animNode->GetName() << "'");
}

// ════════════════════════════════════════════════════════════════
//  HasRagdoll
// ════════════════════════════════════════════════════════════════

bool VansRagdollSystem::HasRagdoll(VansGraphics::VansAnimationNode* animNode) const
{
	return FindInstance(animNode) != nullptr;
}

// ════════════════════════════════════════════════════════════════
//  Drive mode setters/getters
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::SetDriveMode(VansGraphics::VansAnimationNode* animNode, RagdollDriveMode mode)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (!inst) return;

	if (inst->driveMode == mode) return;

	RagdollDriveMode prevMode = inst->driveMode;
	inst->driveMode = mode;

	// When switching from Animation to Physics/Blend, make bodies dynamic
	if (prevMode == RagdollDriveMode::Animation &&
	    (mode == RagdollDriveMode::Physics || mode == RagdollDriveMode::Blend))
	{
		std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
		for (auto& entry : inst->boneEntries)
		{
			if (entry.body)
				entry.body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
		}
		VANS_LOG("[RagdollSystem] '" << animNode->GetName()
		         << "' switched to " << (mode == RagdollDriveMode::Physics ? "Physics" : "Blend") << " mode");
	}
	// When switching back to Animation, make bodies kinematic again
	else if ((prevMode == RagdollDriveMode::Physics || prevMode == RagdollDriveMode::Blend) &&
	         mode == RagdollDriveMode::Animation)
	{
		std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
		for (auto& entry : inst->boneEntries)
		{
			if (entry.body)
				entry.body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		}
		VANS_LOG("[RagdollSystem] '" << animNode->GetName() << "' switched back to Animation mode");
	}
}

RagdollDriveMode VansRagdollSystem::GetDriveMode(VansGraphics::VansAnimationNode* animNode) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	return inst ? inst->driveMode : RagdollDriveMode::Animation;
}

void VansRagdollSystem::SetBlendWeight(VansGraphics::VansAnimationNode* animNode, float weight)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (inst)
		inst->blendWeight = glm::clamp(weight, 0.0f, 1.0f);
}

float VansRagdollSystem::GetBlendWeight(VansGraphics::VansAnimationNode* animNode) const
{
	const RagdollInstance* inst = FindInstance(animNode);
	return inst ? inst->blendWeight : 0.0f;
}

// ════════════════════════════════════════════════════════════════
//  ApplyImpulse
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::ApplyImpulse(VansGraphics::VansAnimationNode* animNode,
                                      const std::string& boneName,
                                      const glm::vec3& impulse)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (!inst) return;

	auto it = inst->boneNameToEntryIndex.find(boneName);
	if (it == inst->boneNameToEntryIndex.end()) return;

	RagdollBoneEntry& entry = inst->boneEntries[it->second];
	if (!entry.body) return;

	std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
	entry.body->addForce(PxVec3(impulse.x, impulse.y, impulse.z), PxForceMode::eIMPULSE);
}

// ════════════════════════════════════════════════════════════════
//  PostAnimationUpdate — per-frame hook
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::PostAnimationUpdate(VansGraphics::VansAnimationNode* animNode)
{
	RagdollInstance* inst = FindInstance(animNode);
	if (!inst) return;

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

// ════════════════════════════════════════════════════════════════
//  SyncBodiesToAnimPose
//  Animation mode: kinematically drive bodies to follow animation.
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::SyncBodiesToAnimPose(RagdollInstance& inst)
{
	VansAnimationController* ctrl = inst.animNode->GetController();
	if (!ctrl) return;

	const std::vector<glm::mat4>& globalTransforms = ctrl->GetCachedGlobalTransforms();
	if (globalTransforms.empty()) return;

	glm::mat4 rootWorld = GetRootWorldMatrix(inst);

	std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());

	for (auto& entry : inst.boneEntries)
	{
		if (!entry.body || entry.boneIndex < 0 ||
		    entry.boneIndex >= static_cast<int>(globalTransforms.size()))
			continue;

		glm::mat4 boneWorld  = rootWorld * globalTransforms[entry.boneIndex];
		glm::mat4 bodyWorld  = boneWorld * entry.shapeOffset;
		PxTransform target   = GlmToPx(bodyWorld);

		entry.body->setKinematicTarget(target);
	}
}

// ════════════════════════════════════════════════════════════════
//  SyncAnimToPhysicsPose
//  Physics mode: read body poses and overwrite bone matrices.
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::SyncAnimToPhysicsPose(RagdollInstance& inst)
{
	VansAnimationController* ctrl = inst.animNode->GetController();
	if (!ctrl) return;

	const VansGraphics::Skeleton& skeleton = inst.animNode->GetSkeleton();
	size_t boneCount = skeleton.bones.size();
	if (boneCount == 0) return;

	// Start from animation global transforms as fallback for bones without bodies
	std::vector<glm::mat4> modelTransforms = ctrl->GetCachedGlobalTransforms();
	if (modelTransforms.size() != boneCount)
		modelTransforms.assign(boneCount, glm::mat4(1.0f));

	glm::mat4 rootWorld    = GetRootWorldMatrix(inst);
	glm::mat4 rootWorldInv = glm::inverse(rootWorld);

	for (const auto& entry : inst.boneEntries)
	{
		if (!entry.body || entry.boneIndex < 0 ||
		    entry.boneIndex >= static_cast<int>(boneCount))
			continue;

		// Get body world pose from PhysX
		PxTransform bodyPose = entry.body->getGlobalPose();
		glm::mat4   bodyWorld = PxToGlm(bodyPose);

		// Bone world = body world * shapeOffsetInverse
		glm::mat4 boneWorld = bodyWorld * entry.shapeOffsetInverse;

		// Model-space = inverse(rootWorld) * boneWorld
		modelTransforms[entry.boneIndex] = rootWorldInv * boneWorld;
	}

	// Feed the physics-driven pose into the controller output
	ctrl->FeedExternalBoneWorldTransforms(modelTransforms, skeleton);
}

// ════════════════════════════════════════════════════════════════
//  BlendAndApplyPose
//  Blend mode: lerp animation and physics model-space transforms.
// ════════════════════════════════════════════════════════════════

void VansRagdollSystem::BlendAndApplyPose(RagdollInstance& inst)
{
	VansAnimationController* ctrl = inst.animNode->GetController();
	if (!ctrl) return;

	const VansGraphics::Skeleton& skeleton = inst.animNode->GetSkeleton();
	size_t boneCount = skeleton.bones.size();
	if (boneCount == 0) return;

	// Animation global transforms (already computed by animNode->Update())
	const std::vector<glm::mat4>& animTransforms = ctrl->GetCachedGlobalTransforms();
	if (animTransforms.size() != boneCount) return;

	// Also kinematically drive bodies toward animation pose
	// (so we have physics-driven "targets" but bodies follow animation with drive damping)
	SyncBodiesToAnimPose(inst);

	// Build physics model-space transforms for blended bones
	glm::mat4 rootWorld    = GetRootWorldMatrix(inst);
	glm::mat4 rootWorldInv = glm::inverse(rootWorld);

	std::vector<glm::mat4> physTransforms = animTransforms; // default: use anim

	for (const auto& entry : inst.boneEntries)
	{
		if (!entry.body || entry.boneIndex < 0 ||
		    entry.boneIndex >= static_cast<int>(boneCount))
			continue;

		PxTransform bodyPose  = entry.body->getGlobalPose();
		glm::mat4   bodyWorld = PxToGlm(bodyPose);
		glm::mat4   boneWorld = bodyWorld * entry.shapeOffsetInverse;
		physTransforms[entry.boneIndex] = rootWorldInv * boneWorld;
	}

	// Blend per-bone
	std::vector<glm::mat4> blended;
	BlendModelTransforms(animTransforms, physTransforms, inst.blendWeight, blended);

	ctrl->FeedExternalBoneWorldTransforms(blended, skeleton);
}

// ════════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════════

RagdollInstance* VansRagdollSystem::FindInstance(VansGraphics::VansAnimationNode* animNode)
{
	for (auto& inst : m_Instances)
	{
		if (inst.animNode == animNode)
			return &inst;
	}
	return nullptr;
}

const RagdollInstance* VansRagdollSystem::FindInstance(VansGraphics::VansAnimationNode* animNode) const
{
	for (const auto& inst : m_Instances)
	{
		if (inst.animNode == animNode)
			return &inst;
	}
	return nullptr;
}

glm::mat4 VansRagdollSystem::GetRootWorldMatrix(const RagdollInstance& inst)
{
	uint32_t rootTransformID = inst.animNode->GetTransformID();
	if (rootTransformID < VansTransformStore::GlobalTransforms.size())
		return VansTransformStore::GetTransform(rootTransformID).GetModelMatrix();
	return glm::mat4(1.0f);
}

glm::mat4 VansRagdollSystem::PxToGlm(const PxTransform& t)
{
	glm::quat q(t.q.w, t.q.x, t.q.y, t.q.z);
	glm::mat4 rot = glm::toMat4(q);
	rot[3] = glm::vec4(t.p.x, t.p.y, t.p.z, 1.0f);
	return rot;
}

PxTransform VansRagdollSystem::GlmToPx(const glm::mat4& m)
{
	glm::vec3 pos   = glm::vec3(m[3]);
	glm::vec3 scale, skew;
	glm::quat rot;
	glm::vec4 perspective;
	glm::decompose(m, scale, rot, pos, skew, perspective);
	rot = glm::normalize(rot);
	return PxTransform(PxVec3(pos.x, pos.y, pos.z),
	                   PxQuat(rot.x, rot.y, rot.z, rot.w));
}

void VansRagdollSystem::BlendModelTransforms(const std::vector<glm::mat4>& a,
                                              const std::vector<glm::mat4>& b,
                                              float t,
                                              std::vector<glm::mat4>& out)
{
	size_t count = (std::min)(a.size(), b.size());
	out.resize(count);

	for (size_t i = 0; i < count; i++)
	{
		glm::vec3 scaleA, posA, skewA;
		glm::quat rotA;
		glm::vec4 perspA;
		glm::decompose(a[i], scaleA, rotA, posA, skewA, perspA);

		glm::vec3 scaleB, posB, skewB;
		glm::quat rotB;
		glm::vec4 perspB;
		glm::decompose(b[i], scaleB, rotB, posB, skewB, perspB);

		glm::vec3 blendedPos   = glm::mix(posA, posB, t);
		glm::quat blendedRot   = glm::slerp(rotA, rotB, t);
		glm::vec3 blendedScale = glm::mix(scaleA, scaleB, t);

		glm::mat4 T = glm::translate(glm::mat4(1.0f), blendedPos);
		glm::mat4 R = glm::toMat4(blendedRot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), blendedScale);
		out[i] = T * R * S;
	}
}
