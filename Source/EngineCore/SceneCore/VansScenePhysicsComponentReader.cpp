#include "VansScenePhysicsComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <cstddef>

namespace Vans
{
namespace
{
const VansSerializedValue* ReadObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* ReadArrayField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

std::optional<std::string> ReadOptionalStringField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	return found && found->kind == VansSerializedValue::Kind::String
		? std::optional<std::string>(found->stringValue)
		: std::nullopt;
}

std::optional<std::string> ReadOptionalAssetReferenceField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found)
		return std::nullopt;
	if (found->kind == VansSerializedValue::Kind::String)
		return found->stringValue;
	if (found->kind == VansSerializedValue::Kind::Object)
	{
		const std::string guid = ReadSerializedStringField(*found, "guid");
		if (!guid.empty())
			return guid;
	}
	return std::nullopt;
}

std::optional<float> ReadOptionalFloatField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found)
		return std::nullopt;
	if (found->kind == VansSerializedValue::Kind::Float || found->kind == VansSerializedValue::Kind::Int)
		return static_cast<float>(ReadSerializedNumber(*found));
	return std::nullopt;
}

std::optional<bool> ReadOptionalBoolField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	return found && found->kind == VansSerializedValue::Kind::Bool
		? std::optional<bool>(found->boolValue)
		: std::nullopt;
}

std::optional<std::array<float, 3>> ReadOptionalFloat3Field(
	const VansSerializedValue& object,
	const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Array || found->arrayItems.size() < 3)
		return std::nullopt;

	for (std::size_t index = 0; index < 3; ++index)
	{
		const VansSerializedValue& item = found->arrayItems[index];
		if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int)
			return std::nullopt;
	}

	return std::array<float, 3>{
		static_cast<float>(ReadSerializedNumber(found->arrayItems[0])),
		static_cast<float>(ReadSerializedNumber(found->arrayItems[1])),
		static_cast<float>(ReadSerializedNumber(found->arrayItems[2]))
	};
}

std::vector<VansSceneClothCollisionSphereConfig> ReadCollisionSpheres(
	const VansSerializedValue& clothNode)
{
	std::vector<VansSceneClothCollisionSphereConfig> result;
	const VansSerializedValue* found = ReadArrayField(clothNode, "collisionSpheres");
	if (!found)
		return result;

	result.reserve(found->arrayItems.size());
	for (const VansSerializedValue& sphere : found->arrayItems)
	{
		if (sphere.kind != VansSerializedValue::Kind::Object)
			continue;

		VansSceneClothCollisionSphereConfig config;
		config.objectRef = ReadOptionalStringField(sphere, "objectRef").value_or("");
		config.radius = ReadOptionalFloatField(sphere, "radius");
		result.push_back(config);
	}
	return result;
}

std::optional<VansScenePhysicsMaterialConfig> ReadMaterial(const VansSerializedValue& physicsNode)
{
	const VansSerializedValue* material = ReadObjectField(physicsNode, "material");
	if (!material)
		return std::nullopt;

	VansScenePhysicsMaterialConfig config;
	config.staticFriction = ReadOptionalFloatField(*material, "staticFriction");
	config.dynamicFriction = ReadOptionalFloatField(*material, "dynamicFriction");
	config.restitution = ReadOptionalFloatField(*material, "restitution");
	return config;
}

const VansSerializedValue* FindAuthoringComponent(const VansSerializedValue& entity, const char* type)
{
	const VansSerializedValue* components = ReadArrayField(entity, "components");
	if (!components)
		return nullptr;

	for (const VansSerializedValue& component : components->arrayItems)
	{
		if (ReadSerializedStringField(component, "type") == type)
			return &component;
	}
	return nullptr;
}
}

VansScenePhysicsComponentsConfig VansScenePhysicsComponentReader::ReadComponents(
	const VansSerializedValue& components)
{
	VansScenePhysicsComponentsConfig config;
	if (components.kind != VansSerializedValue::Kind::Object)
		return config;

	if (const VansSerializedValue* physicsNode = ReadObjectField(components, "physics"))
		config.physics = ReadPhysicsNode(*physicsNode);
	if (const VansSerializedValue* clothNode = ReadObjectField(components, "cloth"))
		config.cloth = ReadClothNode(*clothNode);
	if (const VansSerializedValue* characterController = ReadObjectField(components, "charController"))
		config.characterController = ReadCharacterController(*characterController);
	return config;
}

VansScenePhysicsComponentsConfig VansScenePhysicsComponentReader::ReadAuthoringComponents(
	const VansSerializedValue& entity)
{
	VansScenePhysicsComponentsConfig config;
	if (const VansSerializedValue* physics = FindAuthoringComponent(entity, "Physics"))
		config.physics = ReadAuthoringPhysicsComponent(*physics);
	if (const VansSerializedValue* cloth = FindAuthoringComponent(entity, "Cloth"))
	{
		if (const VansSerializedValue* data = ReadObjectField(*cloth, "data"))
			config.cloth = ReadClothNode(*data);
	}
	if (const VansSerializedValue* controller = FindAuthoringComponent(entity, "CharacterController"))
	{
		if (const VansSerializedValue* data = ReadObjectField(*controller, "data"))
			config.characterController = ReadCharacterController(*data);
	}
	return config;
}

VansScenePhysicsNodeConfig VansScenePhysicsComponentReader::ReadAuthoringPhysicsComponent(
	const VansSerializedValue& component)
{
	VansScenePhysicsNodeConfig config;
	if (const VansSerializedValue* data = ReadObjectField(component, "data"))
		config = ReadPhysicsNode(*data);

	config.enabled = ReadSerializedBoolField(component, "enabled", true);
	return config;
}

VansScenePhysicsNodeConfig VansScenePhysicsComponentReader::ReadPhysicsNode(
	const VansSerializedValue& physicsNode)
{
	VansScenePhysicsNodeConfig config;
	if (physicsNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.enabled = ReadOptionalBoolField(physicsNode, "enabled");
	config.bodyType = ReadOptionalStringField(physicsNode, "bodyType");
	config.colliderType = ReadOptionalStringField(physicsNode, "colliderType");
	config.mass = ReadOptionalFloatField(physicsNode, "mass");
	config.useMeshCollider = ReadOptionalBoolField(physicsNode, "useMeshCollider");
	config.useConvexDecomposition = ReadOptionalBoolField(physicsNode, "useConvexDecomposition");
	config.material = ReadMaterial(physicsNode);
	config.boxExtents = ReadOptionalFloat3Field(physicsNode, "boxExtents");
	config.shapeOffset = ReadOptionalFloat3Field(physicsNode, "shapeOffset");
	config.colliderOffset = ReadOptionalFloat3Field(physicsNode, "colliderOffset");
	config.sphereRadius = ReadOptionalFloatField(physicsNode, "sphereRadius");
	config.capsuleRadius = ReadOptionalFloatField(physicsNode, "capsuleRadius");
	config.capsuleHalfHeight = ReadOptionalFloatField(physicsNode, "capsuleHalfHeight");
	config.layer = ReadOptionalStringField(physicsNode, "layer");
	config.isTrigger = ReadOptionalBoolField(physicsNode, "isTrigger");
	config.mesh = ReadOptionalStringField(physicsNode, "mesh");
	config.name = ReadOptionalStringField(physicsNode, "name");
	return config;
}

VansSceneClothNodeConfig VansScenePhysicsComponentReader::ReadClothNode(
	const VansSerializedValue& clothNode)
{
	VansSceneClothNodeConfig config;
	if (clothNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.profilePath = ReadOptionalAssetReferenceField(clothNode, "profilePath");
	config.physicsAttachOffsetY = ReadOptionalFloatField(clothNode, "physicsAttachOffsetY");
	config.collisionSpheres = ReadCollisionSpheres(clothNode);
	return config;
}

VansSceneCharacterControllerConfig VansScenePhysicsComponentReader::ReadCharacterController(
	const VansSerializedValue& characterController)
{
	VansSceneCharacterControllerConfig config;
	if (characterController.kind != VansSerializedValue::Kind::Object)
		return config;

	config.radius = ReadOptionalFloatField(characterController, "radius");
	config.height = ReadOptionalFloatField(characterController, "height");
	config.slopeLimit = ReadOptionalFloatField(characterController, "slopeLimit");
	config.stepOffset = ReadOptionalFloatField(characterController, "stepOffset");
	config.contactOffset = ReadOptionalFloatField(characterController, "contactOffset");
	config.layer = ReadOptionalStringField(characterController, "layer");
	config.climbingMode = ReadOptionalStringField(characterController, "climbingMode");
	config.positionOffset = ReadOptionalFloat3Field(characterController, "positionOffset");
	config.followRagdoll = ReadOptionalBoolField(characterController, "followRagdoll");
	config.followRagdollBone = ReadOptionalStringField(characterController, "followRagdollBone");
	return config;
}
}
