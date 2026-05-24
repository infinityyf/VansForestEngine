#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <extensions/PxD6Joint.h>
#include <nlohmann/json.hpp>
#include <../../GLM/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	class VansAnimationNode;
}

namespace VansEngine
{
	// ── 布娃娃驱动模式 ────────────────────────────────────────────────
	enum class RagdollDriveMode
	{
		Animation,
		Physics,
		Blend
	};

	// ── 单个骨骼刚体配置 ──────────────────────────────────────────────
	struct RagdollBodyConfig
	{
		std::string boneName;

		std::string shapeType = "capsule";
		float capsuleRadius = 0.08f;
		float capsuleHalfHeight = 0.15f;
		glm::vec3 boxExtents = glm::vec3(0.1f, 0.2f, 0.1f);
		float sphereRadius = 0.1f;

		float mass = 5.0f;
		float staticFriction = 0.5f;
		float dynamicFriction = 0.4f;
		float restitution = 0.1f;

		glm::vec3 offsetPosition = glm::vec3(0.0f);
		glm::vec3 offsetRotation = glm::vec3(0.0f);

		std::string layerName = "Default";
	};

	// ── D6 关节配置 ─────────────────────────────────────────────────
	struct RagdollJointConfig
	{
		std::string childBoneName;

		float swingYLimit = 45.0f;
		float swingZLimit = 45.0f;

		float twistLowLimit = -30.0f;
		float twistHighLimit = 30.0f;

		float limitStiffness = 20.0f;
		float limitDamping = 2.0f;

		float projectionTolerance = 0.1f;

		bool enableDrive = false;
		float driveStiffness = 0.0f;
		float driveDamping = 20.0f;
		float driveForceLimit = PX_MAX_F32;
	};

	// ── Ragdoll 资产配置 ─────────────────────────────────────────────
	struct RagdollProfile
	{
		std::string name;
		std::vector<RagdollBodyConfig> bodies;
		std::vector<RagdollJointConfig> joints;

		static bool LoadFromFile(const std::string& filePath, RagdollProfile& out);
		static bool LoadFromJson(const nlohmann::json& j, RagdollProfile& out);
	};

	// ── 运行时骨骼条目 ───────────────────────────────────────────────
	struct RagdollBoneEntry
	{
		std::string boneName;
		int boneIndex = -1;
		physx::PxRigidDynamic* body = nullptr;
		physx::PxD6Joint* joint = nullptr;
		physx::PxMaterial* material = nullptr;

		glm::mat4 shapeOffset = glm::mat4(1.0f);
		glm::mat4 shapeOffsetInverse = glm::mat4(1.0f);
	};

	// ── 单个 AnimationNode 对应的布娃娃实例 ─────────────────────────
	struct RagdollInstance
	{
		VansGraphics::VansAnimationNode* animNode = nullptr;
		RagdollDriveMode driveMode = RagdollDriveMode::Animation;
		float blendWeight = 0.0f;

		std::vector<RagdollBoneEntry> boneEntries;
		std::unordered_map<std::string, int> boneNameToEntryIndex;
	};
}
