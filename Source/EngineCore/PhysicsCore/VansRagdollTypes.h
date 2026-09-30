#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <extensions/PxD6Joint.h>
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include "../RuntimeCore/VansRagdollPose.h"

#include <string>
#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace VansEngine
{
	// ── 布娃娃驱动模式 ────────────────────────────────────────────────
	enum class RagdollDriveMode
	{
		Animation,
		Physics,
		Blend
	};

	// 每个刚体可附着多个独立形状；位置和旋转均相对骨骼原点。
	struct RagdollShapeConfig
	{
		std::string shapeType = "capsule";
		float capsuleRadius = 0.08f;
		float capsuleHalfHeight = 0.15f;
		glm::vec3 boxExtents = glm::vec3(0.1f, 0.2f, 0.1f);
		float sphereRadius = 0.1f;
		std::vector<glm::vec3> convexVertices;
		glm::vec3 offsetPosition = glm::vec3(0.0f);
		glm::vec3 offsetRotation = glm::vec3(0.0f);
	};

	// ── 单个骨骼刚体配置 ──────────────────────────────────────────────
	struct RagdollBodyConfig : RagdollShapeConfig
	{
		std::string boneName;
		std::vector<RagdollShapeConfig> additionalShapes;

		float mass = 5.0f;
		float linearDamping = 0.05f;
		// 未指定时使用自身碰撞模式对应的通用默认值。
		std::optional<float> angularDamping;
		// 碰撞体为关节留出的空隙不应同样削弱完整肢段的转动惯量。
		float inertiaScale = 1.0f;
		float staticFriction = 0.5f;
		float dynamicFriction = 0.4f;
		float restitution = 0.1f;

		// 静止进入布娃娃时的可选演示推力；普通角色保持零，由受击逻辑施加冲量。
		glm::vec3 stationaryAngularVelocity{0.0f};
		glm::vec3 stationaryImpulse{0.0f};
		// Some bodies remain kinematic while their descendants simulate.
		bool simulateInPhysicsMode = true;
		bool collisionEnabled = true;

		std::string layerName = "Default";
	};

	// ── D6 关节配置 ─────────────────────────────────────────────────
	enum class RagdollJointMotion { Locked, Limited, Free };
	enum class RagdollAngularDriveMode { Slerp, SwingTwist };

	struct RagdollJointConfig
	{
		std::string childBoneName;
		// 显式关节坐标使用父/子骨骼的局部基底，与当前播放姿态无关。
		bool hasLocalFrames = false;
		glm::vec3 parentFramePosition{0.0f}, parentFrameRotation{0.0f};
		glm::vec3 childFramePosition{0.0f}, childFrameRotation{0.0f};
		std::array<RagdollJointMotion, 3> linearMotion{
			RagdollJointMotion::Locked, RagdollJointMotion::Locked, RagdollJointMotion::Locked};
		float linearLimit = 0.0f;
		// When omitted, zero angular limits lock an axis and nonzero limits bound it.
		std::optional<RagdollJointMotion> swingYMotion, swingZMotion, twistMotion;

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
		RagdollAngularDriveMode angularDriveMode = RagdollAngularDriveMode::Slerp;
		bool driveAcceleration = false;
		// Channel order: swing, twist, slerp. Enable flags are independent of gains.
		std::array<bool, 3> drivePositionEnabled{true, true, true};
		std::array<bool, 3> driveVelocityEnabled{true, true, true};
	};

	// ── Ragdoll 资产配置 ─────────────────────────────────────────────
	struct RagdollProfile
	{
		std::string name;
		bool selfCollision = false;
		std::vector<RagdollBodyConfig> bodies;
		std::vector<RagdollJointConfig> joints;
		std::vector<std::array<std::string,2>> disabledCollisionPairs;
	};

	// word2 bit0 保留给触发器，bit1 开启全部自身碰撞；word3 为实例身份。
	inline bool RagdollPairSuppressed(const physx::PxFilterData& a, const physx::PxFilterData& b)
	{
		return a.word3 != 0 && a.word3 == b.word3 && !(a.word2 & b.word2 & 2u);
	}

	// ── 运行时骨骼条目 ───────────────────────────────────────────────
	struct RagdollBoneEntry
	{
		std::string boneName;
		int boneIndex = -1;
		physx::PxRigidDynamic* body = nullptr;
		physx::PxD6Joint* joint = nullptr;
		physx::PxMaterial* material = nullptr;
		std::vector<physx::PxConvexMesh*> convexMeshes;

		glm::mat4 shapeOffset = glm::mat4(1.0f);
		glm::mat4 shapeOffsetInverse = glm::mat4(1.0f);
		glm::vec3 stationaryAngularVelocity{0.0f};
		glm::vec3 stationaryImpulse{0.0f};
		bool simulateInPhysicsMode = true;
		bool collisionEnabled = true;
		bool enableDrive = false;
		RagdollAngularDriveMode angularDriveMode = RagdollAngularDriveMode::Slerp;
		bool driveAcceleration = false;
		std::array<bool, 3> drivePositionEnabled{true, true, true};
		std::array<bool, 3> driveVelocityEnabled{true, true, true};
	};

	// ── 单个运行时布娃娃实例 ───────────────────────────────────────
	struct RagdollInstance
	{
		Vans::VansRagdollKey key;
		RagdollDriveMode driveMode = RagdollDriveMode::Animation;
		float blendWeight = 0.0f;

		std::vector<RagdollBoneEntry> boneEntries;
		std::unordered_map<std::string, int> boneNameToEntryIndex;
		std::vector<int> parentIndices;
		std::vector<int> topologicalOrder;
	};
	struct RagdollDiagnostics
	{
		int collidingBodyPairs = 0;
		float maxPenetration = 0, maxAnchorError = 0, maxAngularErrorDegrees = 0, maxLinearSpeed = 0;
		glm::vec3 worstJointAnglesDegrees{0.f};
		std::string anchorBone, angularBone, penetrationPair;
	};
	struct RagdollBodyState
	{
		glm::vec3 boneWorldPosition{0.0f};
		glm::quat boneWorldRotation{1.0f, 0.0f, 0.0f, 0.0f};
		glm::vec3 linearVelocity{0.0f};
	};
}
