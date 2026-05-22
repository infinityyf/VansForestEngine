#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <unordered_map>

using namespace physx;

// Forward declarations
namespace VansGraphics
{
	class VansAnimationNode;
}

namespace VansEngine
{
	// ════════════════════════════════════════════════════════════════
	//  RagdollDriveMode — controls how the ragdoll body poses are driven
	// ════════════════════════════════════════════════════════════════

	enum class RagdollDriveMode
	{
		Animation,   // PhysX bodies are kinematically driven to follow animation pose
		Physics,     // Bone matrices are driven by PhysX body poses (full ragdoll)
		Blend        // Lerp between animation pose and physics pose by blendWeight
	};

	// ════════════════════════════════════════════════════════════════
	//  RagdollBodyConfig — per-bone rigid body configuration (from JSON)
	// ════════════════════════════════════════════════════════════════

	struct RagdollBodyConfig
	{
		std::string boneName;

		// Collision shape: "capsule", "box", "sphere"
		std::string shapeType = "capsule";

		// Capsule parameters
		float capsuleRadius     = 0.08f;
		float capsuleHalfHeight = 0.15f;

		// Box parameters
		glm::vec3 boxExtents = glm::vec3(0.1f, 0.2f, 0.1f);

		// Sphere parameter
		float sphereRadius = 0.1f;

		// Physical properties
		float mass = 5.0f;

		// Collision material properties
		float staticFriction  = 0.5f;
		float dynamicFriction = 0.4f;
		float restitution     = 0.1f;

		// Local offset of the shape center relative to the bone origin (bone local space)
		glm::vec3 offsetPosition = glm::vec3(0.0f);
		glm::vec3 offsetRotation = glm::vec3(0.0f);   // Euler degrees, applied ZYX

		// Collision layer name (resolved at runtime to layerIndex)
		std::string layerName = "Default";
		int         layerIndex = 0;           // resolved during CreateRagdoll
	};

	// ════════════════════════════════════════════════════════════════
	//  RagdollJointConfig — per-joint D6 constraint configuration (from JSON)
	// ════════════════════════════════════════════════════════════════

	struct RagdollJointConfig
	{
		// The child bone whose body is constrained to its parent body
		std::string childBoneName;

		// Angular swing limits (degrees) — Y and Z axes of the child frame
		float swingYLimit = 45.0f;
		float swingZLimit = 45.0f;

		// Twist limits around the X axis (degrees)
		float twistLowLimit  = -30.0f;
		float twistHighLimit =  30.0f;

		// Spring properties applied to the angular limit surfaces
		float limitStiffness = 20.0f;
		float limitDamping   =  2.0f;

		// Joint projection tolerance (meters): bodies further apart than this will be projected
		float projectionTolerance = 0.1f;

		// Optional position-target drive (used in Blend mode to pull bodies toward animation pose)
		bool  enableDrive       = false;
		float driveStiffness    = 0.0f;
		float driveDamping      = 20.0f;
		float driveForceLimit   = PX_MAX_F32;
	};

	// ════════════════════════════════════════════════════════════════
	//  RagdollProfile — full character ragdoll asset loaded from JSON
	// ════════════════════════════════════════════════════════════════

	struct RagdollProfile
	{
		std::string name;
		std::vector<RagdollBodyConfig>  bodies;
		std::vector<RagdollJointConfig> joints;

		// Load from a .vragdoll JSON file path
		static bool LoadFromFile(const std::string& filePath, RagdollProfile& out);

		// Parse from an already-loaded nlohmann::json object
		static bool LoadFromJson(const nlohmann::json& j, RagdollProfile& out);
	};

	// ════════════════════════════════════════════════════════════════
	//  RagdollBoneEntry — runtime entry for one bone body
	// ════════════════════════════════════════════════════════════════

	struct RagdollBoneEntry
	{
		std::string      boneName;
		int              boneIndex = -1;     // index in Skeleton::bones[]

		// PhysX objects (owned by VansRagdollSystem, released on destroy)
		PxRigidDynamic*  body      = nullptr;
		PxD6Joint*       joint     = nullptr;  // joint connecting this body to parent (null for root)
		PxMaterial*      material  = nullptr;

		// Shape-offset relative to bone origin, stored as bone-local glm::mat4.
		// During kinematic driving: bodyWorld = boneWorld * shapeOffset
		// During readback:         boneWorld = bodyWorld * shapeOffsetInverse
		glm::mat4 shapeOffset        = glm::mat4(1.0f);
		glm::mat4 shapeOffsetInverse = glm::mat4(1.0f);
	};

	// ════════════════════════════════════════════════════════════════
	//  RagdollInstance — runtime ragdoll state for one VansAnimationNode
	// ════════════════════════════════════════════════════════════════

	struct RagdollInstance
	{
		VansGraphics::VansAnimationNode* animNode   = nullptr;
		RagdollDriveMode                 driveMode  = RagdollDriveMode::Animation;
		float                            blendWeight = 0.0f;  // 0 = full anim, 1 = full physics

		std::vector<RagdollBoneEntry> boneEntries;

		// Fast lookup: boneName → index in boneEntries
		std::unordered_map<std::string, int> boneNameToEntryIndex;
	};

}  // namespace VansEngine
