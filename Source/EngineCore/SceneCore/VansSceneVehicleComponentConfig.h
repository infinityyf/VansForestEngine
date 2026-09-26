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
	std::optional<VansSceneVehicleTokenConfig> lateralAxis;
	std::optional<VansSceneVehicleTokenConfig> verticalAxis;

	std::optional<float> wheelRadius;
	std::optional<float> wheelHalfWidth;
	std::optional<float> wheelMass;
	std::optional<float> wheelMoi;
	std::optional<float> wheelDampingRate;
	std::optional<float> visualWheelRollSign;
	std::optional<float> wheelVisualGroundClearance;
	std::optional<std::string> collisionLayer;
	std::optional<bool> useRoadQueryLayerFilter;
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
	std::optional<float> maxSteerAngleDegrees;
	std::optional<float> ackermannWheelBase;
	std::optional<float> ackermannTrackWidth;
	std::optional<float> ackermannStrength;

	std::optional<float> enginePeakTorque;
	std::optional<float> engineMaxOmega;
	std::optional<float> engineMoi;
	std::optional<float> engineIdleOmega;
	std::optional<float> engineDampingFullThrottle;
	std::optional<float> engineDampingZeroThrottleClutchEngaged;
	std::optional<float> engineDampingZeroThrottleClutchDisengaged;
	std::vector<std::array<float, 2>> engineTorqueCurve;
	std::vector<float> gearRatios;
	std::optional<uint32_t> neutralGear;
	std::optional<float> gearboxFinalRatio;
	std::optional<float> gearboxSwitchTime;
	std::vector<float> autoboxUpRatios;
	std::vector<float> autoboxDownRatios;
	std::optional<float> autoboxLatency;
	std::optional<float> clutchStrength;
	std::optional<uint32_t> clutchEstimateIterations;

	std::optional<float> tireLongitudinalStiffness;
	std::optional<float> tireLateralStiffnessX;
	std::optional<std::array<float, 4>> tireLateralStiffnessY;
	std::optional<float> tireCamberStiffness;
	std::optional<std::array<float, 4>> tireRestLoad;
	std::optional<std::array<std::array<float, 2>, 3>> tireFrictionVsSlip;
	std::optional<std::array<std::array<float, 2>, 2>> tireLoadFilter;

	std::optional<std::array<float, 4>> differentialTorqueRatios;
	std::optional<std::array<float, 4>> differentialAverageWheelSpeedRatios;
	std::optional<float> differentialCenterBias;
	std::optional<float> differentialCenterTarget;
	std::optional<float> differentialFrontBias;
	std::optional<float> differentialFrontTarget;
	std::optional<float> differentialRearBias;
	std::optional<float> differentialRearTarget;
	std::optional<float> differentialRate;

	std::optional<float> materialStaticFriction;
	std::optional<float> materialDynamicFriction;
	std::optional<float> materialRestitution;
	std::optional<float> tireFriction;
	std::optional<float> suspensionLimitRestitution;
	std::optional<uint32_t> drivetrainSubsteps;

	std::optional<bool> autoWheelGeometry;
	std::vector<std::string> bodyGeometryExcludeObjects;
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
