#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct VansSceneVehicleTokenConfig
{
	std::optional<int> index;
	std::optional<std::string> name;
};

struct VansSceneVehicleTireGroupConfig
{
	std::vector<std::string> objectNames;
	std::optional<std::array<float, 3>> pivot;
	std::optional<std::array<float, 3>> visualPivot;
	std::optional<std::array<float, 3>> wheelCenter;
	std::optional<std::array<float, 3>> suspensionPivot;
};

struct VansSceneVehicleTuningConfig
{
	std::optional<float> bodyMass;
	std::optional<std::array<float, 3>> bodyMoi;
	std::optional<std::array<float, 3>> centerOfMass;
	std::optional<std::array<float, 3>> bodyBoxHalfExtents;
	std::optional<std::array<float, 3>> bodyBoxLocalPosition;
	std::optional<bool> autoBodyGeometry;
	std::optional<std::array<float, 3>> bodyGeometryPadding;
	std::optional<std::array<float, 3>> bodyGeometryHalfExtentsScale;
	std::optional<std::array<float, 3>> bodyGeometryCenterOffset;
	std::optional<VansSceneVehicleTokenConfig> longitudinalAxis;
	std::optional<VansSceneVehicleTokenConfig> forwardAxis;
	std::optional<VansSceneVehicleTokenConfig> lateralAxis;
	std::optional<VansSceneVehicleTokenConfig> verticalAxis;

	std::optional<float> wheelRadius;
	std::optional<float> wheelHalfWidth;
	std::optional<float> wheelMass;
	std::optional<float> wheelMoi;
	std::optional<float> wheelDampingRate;
	std::optional<float> visualWheelRollSign;
	std::optional<float> wheelVisualGroundClearance;
	std::optional<bool> enableWheelSimulationCollision;
	std::optional<std::string> collisionLayer;
	std::optional<std::string> layer;
	std::optional<bool> useRoadQueryLayerFilter;
	std::optional<bool> roadQueryLayerFilter;
	std::optional<std::string> physxActorUpdateMode;
	std::optional<uint32_t> roadQueryMask;
	std::vector<std::string> roadQueryLayers;
	std::optional<bool> autoAlignToGround;
	std::optional<float> groundHeight;
	std::optional<float> groundClearance;
	std::optional<float> startHeightOffset;
	std::optional<std::string> wheelCollisionMode;

	std::optional<std::array<std::array<float, 3>, 4>> suspensionAttachmentPositions;
	std::optional<float> suspensionTravelDist;
	std::optional<std::array<float, 4>> suspensionStiffness;
	std::optional<std::array<float, 4>> suspensionDamping;
	std::optional<std::array<float, 4>> sprungMass;

	std::optional<float> brakeMaxTorque;
	std::optional<float> handbrakeMaxTorque;
	std::optional<float> maxSteerAngleDeg;
	std::optional<float> maxSteerAngleRad;
	std::optional<float> ackermannWheelBase;
	std::optional<float> ackermannTrackWidth;
	std::optional<float> ackermannStrength;

	std::optional<float> enginePeakTorque;
	std::optional<float> engineMaxOmega;
	std::optional<float> gearboxFinalRatio;
	std::optional<float> gearboxSwitchTime;
	std::optional<float> autoboxLatency;
	std::optional<float> clutchStrength;

	std::optional<bool> autoWheelGeometry;
	std::vector<std::string> bodyGeometryExcludeObjects;
	std::optional<std::array<VansSceneVehicleTokenConfig, 4>> wheelOrder;
};

struct VansSceneVehicleComponentConfig
{
	std::optional<std::string> bodyObject;
	std::vector<VansSceneVehicleTireGroupConfig> tireObjects;
	std::optional<std::array<float, 3>> position;
	std::optional<std::array<VansSceneVehicleTokenConfig, 4>> wheelOrder;
	VansSceneVehicleTuningConfig tuning;
};

struct VansSceneVehicleObjectConfig
{
	std::optional<VansSceneVehicleComponentConfig> vehicle;
};

using VansSceneVehicleObjectConfigs = std::vector<VansSceneVehicleObjectConfig>;
}
