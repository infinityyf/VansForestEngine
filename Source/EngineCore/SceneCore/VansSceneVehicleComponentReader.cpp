#include "VansSceneVehicleComponentReader.h"

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

std::optional<uint32_t> ReadOptionalUIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Int || found->intValue < 0)
		return std::nullopt;
	return static_cast<uint32_t>(found->intValue);
}

std::optional<std::array<float, 3>> ReadOptionalFloat3Field(
	const VansSerializedValue& object,
	const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Array || found->arrayItems.size() < 3)
		return std::nullopt;

	std::array<float, 3> result{};
	for (std::size_t index = 0; index < result.size(); ++index)
	{
		const VansSerializedValue& value = found->arrayItems[index];
		if (value.kind != VansSerializedValue::Kind::Float && value.kind != VansSerializedValue::Kind::Int)
			return std::nullopt;
		result[index] = static_cast<float>(ReadSerializedNumber(value));
	}
	return result;
}

std::optional<std::array<float, 4>> ReadOptionalFloat4Field(
	const VansSerializedValue& object,
	const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Array || found->arrayItems.size() < 4)
		return std::nullopt;

	std::array<float, 4> values{};
	for (std::size_t index = 0; index < values.size(); ++index)
	{
		const VansSerializedValue& value = found->arrayItems[index];
		if (value.kind != VansSerializedValue::Kind::Float && value.kind != VansSerializedValue::Kind::Int)
			return std::nullopt;
		values[index] = static_cast<float>(ReadSerializedNumber(value));
	}
	return values;
}

std::optional<std::array<std::array<float, 3>, 4>> ReadOptionalFloat3Array4Field(
	const VansSerializedValue& object,
	const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Array || found->arrayItems.size() < 4)
		return std::nullopt;

	std::array<std::array<float, 3>, 4> result{};
	for (std::size_t index = 0; index < result.size(); ++index)
	{
		const VansSerializedValue& value = found->arrayItems[index];
		if (value.kind != VansSerializedValue::Kind::Array || value.arrayItems.size() < 3)
			return std::nullopt;
		for (std::size_t axis = 0; axis < 3; ++axis)
		{
			const VansSerializedValue& item = value.arrayItems[axis];
			if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int)
				return std::nullopt;
			result[index][axis] = static_cast<float>(ReadSerializedNumber(item));
		}
	}
	return result;
}

std::optional<VansSceneVehicleTokenConfig> ReadOptionalTokenField(
	const VansSerializedValue& object,
	const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found)
		return std::nullopt;

	VansSceneVehicleTokenConfig token;
	if (found->kind == VansSerializedValue::Kind::Int)
	{
		token.index = static_cast<int>(found->intValue);
		return token;
	}
	if (found->kind == VansSerializedValue::Kind::String)
	{
		token.name = found->stringValue;
		return token;
	}
	return std::nullopt;
}

std::optional<std::array<VansSceneVehicleTokenConfig, 4>> ReadOptionalWheelOrder(
	const VansSerializedValue& object)
{
	const VansSerializedValue* found = FindObjectField(object, "wheelOrder");
	if (!found || found->kind != VansSerializedValue::Kind::Array || found->arrayItems.size() < 4)
		return std::nullopt;

	std::array<VansSceneVehicleTokenConfig, 4> order;
	for (std::size_t index = 0; index < order.size(); ++index)
	{
		const VansSerializedValue& entry = found->arrayItems[index];
		if (entry.kind == VansSerializedValue::Kind::Int)
		{
			order[index].index = static_cast<int>(entry.intValue);
		}
		else if (entry.kind == VansSerializedValue::Kind::String)
		{
			order[index].name = entry.stringValue;
		}
		else
		{
			return std::nullopt;
		}
	}
	return order;
}

std::vector<std::string> ReadStringArrayField(const VansSerializedValue& object, const char* key)
{
	std::vector<std::string> result;
	const VansSerializedValue* found = ReadArrayField(object, key);
	if (!found)
		return result;

	result.reserve(found->arrayItems.size());
	for (const VansSerializedValue& item : found->arrayItems)
	{
		if (item.kind == VansSerializedValue::Kind::String)
			result.push_back(item.stringValue);
	}
	return result;
}

std::vector<std::string> CollectVehicleObjectNames(const VansSerializedValue& tireNode)
{
	std::vector<std::string> names;
	if (tireNode.kind == VansSerializedValue::Kind::String)
	{
		names.push_back(tireNode.stringValue);
	}
	else if (tireNode.kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : tireNode.arrayItems)
			if (item.kind == VansSerializedValue::Kind::String)
				names.push_back(item.stringValue);
	}
	else if (tireNode.kind == VansSerializedValue::Kind::Object)
	{
		if (auto objectName = ReadOptionalStringField(tireNode, "object"))
			names.push_back(*objectName);
		std::vector<std::string> nested = ReadStringArrayField(tireNode, "objects");
		names.insert(names.end(), nested.begin(), nested.end());
	}
	return names;
}

VansSceneVehicleTireGroupConfig DecodeTireGroup(const VansSerializedValue& tireNode)
{
	VansSceneVehicleTireGroupConfig config;
	config.objectNames = CollectVehicleObjectNames(tireNode);
	if (tireNode.kind == VansSerializedValue::Kind::Object)
	{
		config.pivot = ReadOptionalFloat3Field(tireNode, "pivot");
		config.visualPivot = ReadOptionalFloat3Field(tireNode, "visualPivot");
		config.wheelCenter = ReadOptionalFloat3Field(tireNode, "wheelCenter");
		config.suspensionPivot = ReadOptionalFloat3Field(tireNode, "suspensionPivot");
	}
	return config;
}

std::vector<VansSceneVehicleTireGroupConfig> DecodeTireGroups(const VansSerializedValue& vehicleNode)
{
	std::vector<VansSceneVehicleTireGroupConfig> result;
	const VansSerializedValue* found = ReadArrayField(vehicleNode, "tireObjects");
	if (!found)
		return result;

	result.reserve(found->arrayItems.size());
	for (const VansSerializedValue& tireNode : found->arrayItems)
		result.push_back(DecodeTireGroup(tireNode));
	return result;
}

VansSceneVehicleTuningConfig DecodeTuning(const VansSerializedValue& vehicleNode)
{
	VansSceneVehicleTuningConfig config;
	const VansSerializedValue* tuningObject = ReadObjectField(vehicleNode, "tuning");
	const VansSerializedValue& tuningNode = tuningObject ? *tuningObject : vehicleNode;

	config.bodyMass = ReadOptionalFloatField(tuningNode, "bodyMass");
	config.bodyMoi = ReadOptionalFloat3Field(tuningNode, "bodyMoi");
	config.centerOfMass = ReadOptionalFloat3Field(tuningNode, "centerOfMass");
	config.bodyBoxHalfExtents = ReadOptionalFloat3Field(tuningNode, "bodyBoxHalfExtents");
	config.bodyBoxLocalPosition = ReadOptionalFloat3Field(tuningNode, "bodyBoxLocalPosition");
	config.autoBodyGeometry = ReadOptionalBoolField(tuningNode, "autoBodyGeometry");
	config.bodyGeometryPadding = ReadOptionalFloat3Field(tuningNode, "bodyGeometryPadding");
	config.bodyGeometryHalfExtentsScale = ReadOptionalFloat3Field(tuningNode, "bodyGeometryHalfExtentsScale");
	config.bodyGeometryCenterOffset = ReadOptionalFloat3Field(tuningNode, "bodyGeometryCenterOffset");
	config.longitudinalAxis = ReadOptionalTokenField(tuningNode, "longitudinalAxis");
	config.forwardAxis = ReadOptionalTokenField(tuningNode, "forwardAxis");
	config.lateralAxis = ReadOptionalTokenField(tuningNode, "lateralAxis");
	config.verticalAxis = ReadOptionalTokenField(tuningNode, "verticalAxis");

	config.wheelRadius = ReadOptionalFloatField(tuningNode, "wheelRadius");
	config.wheelHalfWidth = ReadOptionalFloatField(tuningNode, "wheelHalfWidth");
	config.wheelMass = ReadOptionalFloatField(tuningNode, "wheelMass");
	config.wheelMoi = ReadOptionalFloatField(tuningNode, "wheelMoi");
	config.wheelDampingRate = ReadOptionalFloatField(tuningNode, "wheelDampingRate");
	config.visualWheelRollSign = ReadOptionalFloatField(tuningNode, "visualWheelRollSign");
	config.wheelVisualGroundClearance = ReadOptionalFloatField(tuningNode, "wheelVisualGroundClearance");
	config.enableWheelSimulationCollision = ReadOptionalBoolField(tuningNode, "enableWheelSimulationCollision");
	config.collisionLayer = ReadOptionalStringField(tuningNode, "collisionLayer");
	config.layer = ReadOptionalStringField(tuningNode, "layer");
	config.useRoadQueryLayerFilter = ReadOptionalBoolField(tuningNode, "useRoadQueryLayerFilter");
	config.roadQueryLayerFilter = ReadOptionalBoolField(tuningNode, "roadQueryLayerFilter");
	config.physxActorUpdateMode = ReadOptionalStringField(tuningNode, "physxActorUpdateMode");
	config.roadQueryMask = ReadOptionalUIntField(tuningNode, "roadQueryMask");
	config.roadQueryLayers = ReadStringArrayField(tuningNode, "roadQueryLayers");
	config.autoAlignToGround = ReadOptionalBoolField(tuningNode, "autoAlignToGround");
	config.groundHeight = ReadOptionalFloatField(tuningNode, "groundHeight");
	config.groundClearance = ReadOptionalFloatField(tuningNode, "groundClearance");
	config.startHeightOffset = ReadOptionalFloatField(tuningNode, "startHeightOffset");
	config.wheelCollisionMode = ReadOptionalStringField(tuningNode, "wheelCollisionMode");

	config.suspensionAttachmentPositions = ReadOptionalFloat3Array4Field(tuningNode, "suspensionAttachmentPositions");
	config.suspensionTravelDist = ReadOptionalFloatField(tuningNode, "suspensionTravelDist");
	config.suspensionStiffness = ReadOptionalFloat4Field(tuningNode, "suspensionStiffness");
	config.suspensionDamping = ReadOptionalFloat4Field(tuningNode, "suspensionDamping");
	config.sprungMass = ReadOptionalFloat4Field(tuningNode, "sprungMass");

	config.brakeMaxTorque = ReadOptionalFloatField(tuningNode, "brakeMaxTorque");
	config.handbrakeMaxTorque = ReadOptionalFloatField(tuningNode, "handbrakeMaxTorque");
	config.maxSteerAngleDeg = ReadOptionalFloatField(tuningNode, "maxSteerAngleDeg");
	config.maxSteerAngleRad = ReadOptionalFloatField(tuningNode, "maxSteerAngleRad");
	config.ackermannWheelBase = ReadOptionalFloatField(tuningNode, "ackermannWheelBase");
	config.ackermannTrackWidth = ReadOptionalFloatField(tuningNode, "ackermannTrackWidth");
	config.ackermannStrength = ReadOptionalFloatField(tuningNode, "ackermannStrength");

	config.enginePeakTorque = ReadOptionalFloatField(tuningNode, "enginePeakTorque");
	config.engineMaxOmega = ReadOptionalFloatField(tuningNode, "engineMaxOmega");
	config.gearboxFinalRatio = ReadOptionalFloatField(tuningNode, "gearboxFinalRatio");
	config.gearboxSwitchTime = ReadOptionalFloatField(tuningNode, "gearboxSwitchTime");
	config.autoboxLatency = ReadOptionalFloatField(tuningNode, "autoboxLatency");
	config.clutchStrength = ReadOptionalFloatField(tuningNode, "clutchStrength");

	config.autoWheelGeometry = ReadOptionalBoolField(tuningNode, "autoWheelGeometry");
	config.bodyGeometryExcludeObjects = ReadStringArrayField(tuningNode, "bodyGeometryExcludeObjects");
	config.wheelOrder = ReadOptionalWheelOrder(tuningNode);
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

VansSceneVehicleObjectConfigs VansSceneVehicleComponentReader::ReadObjects(
	const VansSerializedValue& objectsArray)
{
	VansSceneVehicleObjectConfigs configs;
	if (objectsArray.kind != VansSerializedValue::Kind::Array)
		return configs;

	configs.reserve(objectsArray.arrayItems.size());
	for (const VansSerializedValue& objectNode : objectsArray.arrayItems)
		configs.push_back(ReadObject(objectNode));
	return configs;
}

VansSceneVehicleObjectConfig VansSceneVehicleComponentReader::ReadObject(
	const VansSerializedValue& objectNode)
{
	VansSceneVehicleObjectConfig config;
	if (objectNode.kind != VansSerializedValue::Kind::Object)
		return config;

	if (const VansSerializedValue* components = ReadObjectField(objectNode, "components"))
		config = ReadComponents(*components);
	return config;
}

VansSceneVehicleObjectConfig VansSceneVehicleComponentReader::ReadComponents(
	const VansSerializedValue& components)
{
	VansSceneVehicleObjectConfig config;
	if (const VansSerializedValue* vehicle = ReadObjectField(components, "vehicle"))
		config.vehicle = ReadVehicle(*vehicle);
	return config;
}

VansSceneVehicleObjectConfig VansSceneVehicleComponentReader::ReadAuthoringComponents(
	const VansSerializedValue& entity)
{
	VansSceneVehicleObjectConfig config;
	const VansSerializedValue* vehicleComponent = FindAuthoringComponent(entity, "Vehicle");
	if (!vehicleComponent)
		return config;

	if (const VansSerializedValue* data = ReadObjectField(*vehicleComponent, "data"))
		config.vehicle = ReadVehicle(*data);
	else
		config.vehicle = VansSceneVehicleComponentConfig{};
	return config;
}

VansSceneVehicleComponentConfig VansSceneVehicleComponentReader::ReadVehicle(
	const VansSerializedValue& vehicleNode)
{
	VansSceneVehicleComponentConfig config;
	if (vehicleNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.bodyObject = ReadOptionalStringField(vehicleNode, "bodyObject");
	config.tireObjects = DecodeTireGroups(vehicleNode);
	config.position = ReadOptionalFloat3Field(vehicleNode, "position");
	config.wheelOrder = ReadOptionalWheelOrder(vehicleNode);
	config.tuning = DecodeTuning(vehicleNode);
	return config;
}
}
