#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct VansScenePhysicsMaterialConfig
{
	std::optional<float> staticFriction;
	std::optional<float> dynamicFriction;
	std::optional<float> restitution;
};

struct VansScenePhysicsNodeConfig
{
	std::optional<bool> enabled;
	std::optional<std::string> bodyType;
	std::optional<std::string> colliderType;
	std::optional<float> mass;
	std::optional<bool> useMeshCollider;
	std::optional<bool> useConvexDecomposition;
	std::optional<VansScenePhysicsMaterialConfig> material;
	std::optional<std::array<float, 3>> boxExtents;
	std::optional<std::array<float, 3>> shapeOffset;
	std::optional<std::array<float, 3>> colliderOffset;
	std::optional<float> sphereRadius;
	std::optional<float> capsuleRadius;
	std::optional<float> capsuleHalfHeight;
	std::optional<std::string> layer;
	std::optional<bool> isTrigger;
	std::optional<std::string> hitRegion;
	std::optional<std::string> mesh;
	std::optional<std::string> name;
};

struct VansSceneClothCollisionSphereConfig
{
	std::string objectRef;
	std::optional<float> radius;
};

struct VansSceneClothNodeConfig
{
	std::optional<std::string> profileGuid;
	std::optional<float> physicsAttachOffsetY;
	std::vector<VansSceneClothCollisionSphereConfig> collisionSpheres;
};

struct VansSceneCharacterControllerConfig
{
	std::optional<float> radius;
	std::optional<float> height;
	std::optional<float> slopeLimit;
	std::optional<float> stepOffset;
	std::optional<float> contactOffset;
	std::optional<std::string> layer;
	std::optional<std::string> climbingMode;
	std::optional<std::array<float, 3>> positionOffset;
	std::optional<bool> followRagdoll;
	std::optional<std::string> followRagdollBone;
};

struct VansScenePhysicsComponentsConfig
{
	std::optional<VansScenePhysicsNodeConfig> physics;
	std::optional<VansSceneClothNodeConfig> cloth;
	std::optional<VansSceneCharacterControllerConfig> characterController;
};
}
