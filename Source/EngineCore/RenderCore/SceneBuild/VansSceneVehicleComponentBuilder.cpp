#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "VansSceneVehicleComponentBuilder.h"

#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansPhysicsVehicle.h"

#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansMesh.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <optional>

using namespace physx;
using namespace physx::vehicle2;


namespace VansGraphics
{
namespace
{
uint32_t GetObjectTransformID(VansScriptObject* sceneObject)
{
	if (!sceneObject)
		return UINT32_MAX;
	if (auto* renderComp = sceneObject->GetComponent<VansScriptRenderComponent>())
	{
		if (renderComp->m_RenderNode)
			return renderComp->m_RenderNode->m_TransformID;
	}
	return sceneObject->m_TransformID;
}

PxVec3 ToPxVec3(const std::array<float, 3>& value)
{
	return PxVec3(value[0], value[1], value[2]);
}

void ApplyPxVec3(const std::optional<std::array<float, 3>>& value, PxVec3& out)
{
	if (value)
		out = ToPxVec3(*value);
}

void ApplyPxTransformPosition(const std::optional<std::array<float, 3>>& value, PxTransform& out)
{
	if (value)
		out.p = ToPxVec3(*value);
}

void ApplyFloatArray4(const std::optional<std::array<float, 4>>& value, std::array<PxReal, 4>& out)
{
	if (!value)
		return;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = (*value)[i];
}

void ApplyVec3Array4(
	const std::optional<std::array<std::array<float, 3>, 4>>& value,
	std::array<PxVec3, 4>& out)
{
	if (!value)
		return;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = ToPxVec3((*value)[i]);
}

std::string NormalizeToken(std::string token)
{
	std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) {
		return c == '_' || c == '-' || c == ' ';
	}), token.end());
	return token;
}

bool ParseVehicleAxis(
	const Vans::VansSceneVehicleTokenConfig& value,
	PxVehicleAxes::Enum& axis)
{
	if (value.index)
	{
		const int index = *value.index;
		if (index < 0 || index >= static_cast<int>(PxVehicleAxes::eMAX_NB_AXES))
			return false;
		axis = static_cast<PxVehicleAxes::Enum>(index);
		return true;
	}
	if (!value.name)
		return false;

	const std::string token = NormalizeToken(*value.name);
	if (token == "posx") axis = PxVehicleAxes::ePosX;
	else if (token == "negx") axis = PxVehicleAxes::eNegX;
	else if (token == "posy") axis = PxVehicleAxes::ePosY;
	else if (token == "negy") axis = PxVehicleAxes::eNegY;
	else if (token == "posz") axis = PxVehicleAxes::ePosZ;
	else if (token == "negz") axis = PxVehicleAxes::eNegZ;
	else return false;
	return true;
}

float VehicleAxisCoordinate(const PxVec3& value, const PxVec3& axis)
{
	return value.dot(axis);
}

bool AccumulateMeshBounds(VansMesh* mesh, PxVec3& outMin, PxVec3& outMax)
{
	if (!mesh)
		return false;
	if (mesh->HasLocalBounds())
	{
		const glm::vec3 bmin = mesh->GetLocalBoundsMin();
		const glm::vec3 bmax = mesh->GetLocalBoundsMax();
		outMin = PxVec3(bmin.x, bmin.y, bmin.z);
		outMax = PxVec3(bmax.x, bmax.y, bmax.z);
		return true;
	}

	const std::vector<float>& raw = mesh->GetMeshRawPositionData();
	if (raw.empty())
		return false;

	const uint32_t vertexCount = mesh->GetMeshVertexCount();
	size_t stride = 3;
	if (vertexCount > 0 && raw.size() >= static_cast<size_t>(vertexCount) * 8)
		stride = 8;
	else if (vertexCount > 0 && raw.size() >= static_cast<size_t>(vertexCount) * 4)
		stride = 4;

	bool any = false;
	for (size_t i = 0; i + 2 < raw.size(); i += stride)
	{
		const PxVec3 p(raw[i], raw[i + 1], raw[i + 2]);
		if (!any)
		{
			outMin = p;
			outMax = p;
			any = true;
		}
		else
		{
			outMin.x = std::min(outMin.x, p.x);
			outMin.y = std::min(outMin.y, p.y);
			outMin.z = std::min(outMin.z, p.z);
			outMax.x = std::max(outMax.x, p.x);
			outMax.y = std::max(outMax.y, p.y);
			outMax.z = std::max(outMax.z, p.z);
		}
	}
	return any;
}

bool CalculateBoundsFromRenderNodes(const std::vector<VansRenderNode*>& renderNodes, PxVec3& boundsMin, PxVec3& boundsMax)
{
	boundsMin = PxVec3(FLT_MAX, FLT_MAX, FLT_MAX);
	boundsMax = PxVec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	bool any = false;
	for (VansRenderNode* node : renderNodes)
	{
		PxVec3 meshMin, meshMax;
		if (!node || !AccumulateMeshBounds(node->m_Mesh, meshMin, meshMax))
			continue;
		if (!any)
		{
			boundsMin = meshMin;
			boundsMax = meshMax;
			any = true;
		}
		else
		{
			boundsMin.x = std::min(boundsMin.x, meshMin.x);
			boundsMin.y = std::min(boundsMin.y, meshMin.y);
			boundsMin.z = std::min(boundsMin.z, meshMin.z);
			boundsMax.x = std::max(boundsMax.x, meshMax.x);
			boundsMax.y = std::max(boundsMax.y, meshMax.y);
			boundsMax.z = std::max(boundsMax.z, meshMax.z);
		}
	}
	return any;
}

float VehicleAxisExtent(const PxVec3& boundsMin, const PxVec3& boundsMax, const PxVec3& axis)
{
	return (boundsMax - boundsMin).dot(PxVec3(std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)));
}

bool ParseVehicleTuning(
	const Vans::VansSceneVehicleComponentConfig& vehicleConfig,
	VansEngine::VansVehicleTuning& tuning,
	std::string& error)
{
	const Vans::VansSceneVehicleTuningConfig& t = vehicleConfig.tuning;

	if (t.bodyMass) tuning.bodyMass = *t.bodyMass;
	ApplyPxVec3(t.bodyMoi, tuning.bodyMoi);
	ApplyPxTransformPosition(t.centerOfMass, tuning.centerOfMassLocalPose);
	ApplyPxVec3(t.bodyBoxHalfExtents, tuning.bodyBoxHalfExtents);
	ApplyPxTransformPosition(t.bodyBoxLocalPosition, tuning.bodyBoxLocalPose);
	if (t.autoBodyGeometry) tuning.autoBodyGeometry = *t.autoBodyGeometry;
	ApplyPxVec3(t.bodyGeometryPadding, tuning.bodyGeometryPadding);
	ApplyPxVec3(t.bodyGeometryHalfExtentsScale, tuning.bodyGeometryHalfExtentsScale);
	ApplyPxVec3(t.bodyGeometryCenterOffset, tuning.bodyGeometryCenterOffset);
	if (t.longitudinalAxis && !ParseVehicleAxis(*t.longitudinalAxis, tuning.longitudinalAxis))
	{
		error = "Vehicle tuning has an invalid longitudinalAxis";
		return false;
	}
	if (t.lateralAxis && !ParseVehicleAxis(*t.lateralAxis, tuning.lateralAxis))
	{
		error = "Vehicle tuning has an invalid lateralAxis";
		return false;
	}
	if (t.verticalAxis && !ParseVehicleAxis(*t.verticalAxis, tuning.verticalAxis))
	{
		error = "Vehicle tuning has an invalid verticalAxis";
		return false;
	}

	if (t.wheelRadius) tuning.wheelRadius = *t.wheelRadius;
	if (t.wheelHalfWidth) tuning.wheelHalfWidth = *t.wheelHalfWidth;
	if (t.wheelMass) tuning.wheelMass = *t.wheelMass;
	if (t.wheelMoi) tuning.wheelMoi = *t.wheelMoi;
	if (t.wheelDampingRate) tuning.wheelDampingRate = *t.wheelDampingRate;
	if (t.visualWheelRollSign) tuning.visualWheelRollSign = *t.visualWheelRollSign;
	if (t.wheelVisualGroundClearance) tuning.wheelVisualGroundClearance = *t.wheelVisualGroundClearance;
	if (t.collisionLayer) tuning.collisionLayerName = *t.collisionLayer;
	if (t.useRoadQueryLayerFilter) tuning.useRoadQueryLayerFilter = *t.useRoadQueryLayerFilter;
	if (t.physxActorUpdateMode)
	{
		const std::string mode = NormalizeToken(*t.physxActorUpdateMode);
		if (mode == "acceleration")
			tuning.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_ACCELERATION;
		else if (mode == "velocity")
			tuning.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_VELOCITY;
		else
		{
			error = "Vehicle tuning physxActorUpdateMode must be 'velocity' or 'acceleration'";
			return false;
		}
	}
	if (t.roadQueryMask)
	{
		tuning.roadQueryMask = *t.roadQueryMask;
		tuning.useCustomRoadQueryMask = true;
	}
	if (!t.roadQueryLayers.empty())
	{
		PxU32 roadMask = 0u;
		auto& layerMgr = VansEngine::VansCollisionLayerManager::Get();
		for (const std::string& layerName : t.roadQueryLayers)
		{
			int layerIndex = 0;
			if (!layerMgr.TryGetLayerIndex(layerName, layerIndex))
			{
				error = "Vehicle tuning roadQueryLayers contains an unknown layer: " + layerName;
				return false;
			}
			roadMask |= (1u << static_cast<PxU32>(layerIndex));
		}
		tuning.roadQueryMask = roadMask;
		tuning.useCustomRoadQueryMask = true;
	}
	if (t.autoAlignToGround) tuning.autoAlignToGround = *t.autoAlignToGround;
	if (t.groundHeight) tuning.groundHeight = *t.groundHeight;
	if (t.groundClearance) tuning.groundClearance = *t.groundClearance;
	if (t.startHeightOffset) tuning.startHeightOffset = *t.startHeightOffset;
	if (t.wheelCollisionMode)
	{
		const std::string mode = NormalizeToken(*t.wheelCollisionMode);
		if (mode == "simulation")
			tuning.enableWheelSimulationCollision = true;
		else if (mode == "query")
			tuning.enableWheelSimulationCollision = false;
		else
		{
			error = "Vehicle tuning wheelCollisionMode must be 'query' or 'simulation'";
			return false;
		}
	}

	ApplyVec3Array4(t.suspensionAttachmentPositions, tuning.suspensionAttachmentPositions);
	if (t.suspensionTravelDist) tuning.suspensionTravelDist = *t.suspensionTravelDist;
	ApplyFloatArray4(t.suspensionStiffness, tuning.suspensionStiffness);
	ApplyFloatArray4(t.suspensionDamping, tuning.suspensionDamping);
	ApplyFloatArray4(t.sprungMass, tuning.sprungMass);

	if (t.brakeMaxTorque) tuning.brakeMaxTorque = *t.brakeMaxTorque;
	if (t.handbrakeMaxTorque) tuning.handbrakeMaxTorque = *t.handbrakeMaxTorque;
	if (t.maxSteerAngleDegrees) tuning.maxSteerAngleRadians = glm::radians(*t.maxSteerAngleDegrees);
	if (t.ackermannWheelBase) tuning.ackermannWheelBase = *t.ackermannWheelBase;
	if (t.ackermannTrackWidth) tuning.ackermannTrackWidth = *t.ackermannTrackWidth;
	if (t.ackermannStrength) tuning.ackermannStrength = *t.ackermannStrength;

	if (t.enginePeakTorque) tuning.enginePeakTorque = *t.enginePeakTorque;
	if (t.engineMaxOmega) tuning.engineMaxOmega = *t.engineMaxOmega;
	if (t.engineMoi) tuning.engineMoi = *t.engineMoi;
	if (t.engineIdleOmega) tuning.engineIdleOmega = *t.engineIdleOmega;
	if (t.engineDampingFullThrottle) tuning.engineDampingFullThrottle = *t.engineDampingFullThrottle;
	if (t.engineDampingZeroThrottleClutchEngaged)
		tuning.engineDampingZeroThrottleClutchEngaged = *t.engineDampingZeroThrottleClutchEngaged;
	if (t.engineDampingZeroThrottleClutchDisengaged)
		tuning.engineDampingZeroThrottleClutchDisengaged = *t.engineDampingZeroThrottleClutchDisengaged;
	if (!t.engineTorqueCurve.empty())
	{
		tuning.engineTorqueCurve.clear();
		for (const std::array<float, 2>& point : t.engineTorqueCurve)
			tuning.engineTorqueCurve.push_back({ point[0], point[1] });
	}
	if (!t.gearRatios.empty()) tuning.gearRatios.assign(t.gearRatios.begin(), t.gearRatios.end());
	if (t.neutralGear) tuning.neutralGear = *t.neutralGear;
	if (t.gearboxFinalRatio) tuning.gearboxFinalRatio = *t.gearboxFinalRatio;
	if (t.gearboxSwitchTime) tuning.gearboxSwitchTime = *t.gearboxSwitchTime;
	if (!t.autoboxUpRatios.empty()) tuning.autoboxUpRatios.assign(t.autoboxUpRatios.begin(), t.autoboxUpRatios.end());
	if (!t.autoboxDownRatios.empty()) tuning.autoboxDownRatios.assign(t.autoboxDownRatios.begin(), t.autoboxDownRatios.end());
	if (t.autoboxLatency) tuning.autoboxLatency = *t.autoboxLatency;
	if (t.clutchStrength) tuning.clutchStrength = *t.clutchStrength;
	if (t.clutchEstimateIterations) tuning.clutchEstimateIterations = *t.clutchEstimateIterations;

	if (t.tireLongitudinalStiffness) tuning.tireLongitudinalStiffness = *t.tireLongitudinalStiffness;
	if (t.tireLateralStiffnessX) tuning.tireLateralStiffnessX = *t.tireLateralStiffnessX;
	ApplyFloatArray4(t.tireLateralStiffnessY, tuning.tireLateralStiffnessY);
	if (t.tireCamberStiffness) tuning.tireCamberStiffness = *t.tireCamberStiffness;
	ApplyFloatArray4(t.tireRestLoad, tuning.tireRestLoad);
	if (t.tireFrictionVsSlip)
	{
		for (size_t i = 0; i < tuning.tireFrictionVsSlip.size(); ++i)
			tuning.tireFrictionVsSlip[i] = { (*t.tireFrictionVsSlip)[i][0], (*t.tireFrictionVsSlip)[i][1] };
	}
	if (t.tireLoadFilter)
	{
		for (size_t i = 0; i < tuning.tireLoadFilter.size(); ++i)
			tuning.tireLoadFilter[i] = { (*t.tireLoadFilter)[i][0], (*t.tireLoadFilter)[i][1] };
	}

	ApplyFloatArray4(t.differentialTorqueRatios, tuning.differentialTorqueRatios);
	ApplyFloatArray4(t.differentialAverageWheelSpeedRatios, tuning.differentialAverageWheelSpeedRatios);
	if (t.differentialCenterBias) tuning.differentialCenterBias = *t.differentialCenterBias;
	if (t.differentialCenterTarget) tuning.differentialCenterTarget = *t.differentialCenterTarget;
	if (t.differentialFrontBias) tuning.differentialFrontBias = *t.differentialFrontBias;
	if (t.differentialFrontTarget) tuning.differentialFrontTarget = *t.differentialFrontTarget;
	if (t.differentialRearBias) tuning.differentialRearBias = *t.differentialRearBias;
	if (t.differentialRearTarget) tuning.differentialRearTarget = *t.differentialRearTarget;
	if (t.differentialRate) tuning.differentialRate = *t.differentialRate;

	if (t.materialStaticFriction) tuning.materialStaticFriction = *t.materialStaticFriction;
	if (t.materialDynamicFriction) tuning.materialDynamicFriction = *t.materialDynamicFriction;
	if (t.materialRestitution) tuning.materialRestitution = *t.materialRestitution;
	if (t.tireFriction) tuning.tireFriction = *t.tireFriction;
	if (t.suspensionLimitRestitution) tuning.suspensionLimitRestitution = *t.suspensionLimitRestitution;
	if (t.drivetrainSubsteps) tuning.drivetrainSubsteps = *t.drivetrainSubsteps;

	int collisionLayerIndex = 0;
	if (!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(
		tuning.collisionLayerName, collisionLayerIndex))
	{
		error = "Vehicle tuning collisionLayer is unknown: " + tuning.collisionLayerName;
		return false;
	}
	return tuning.IsValid(error);
}

int ParseWheelSlot(const Vans::VansSceneVehicleTokenConfig& value)
{
	if (value.index)
	{
		const int slot = *value.index;
		return (slot >= 0 && slot < 4) ? slot : -1;
	}
	if (!value.name)
		return -1;

	std::string slot = NormalizeToken(*value.name);

	if (slot == "frontleft") return 0;
	if (slot == "frontright") return 1;
	if (slot == "rearleft") return 2;
	if (slot == "rearright") return 3;
	return -1;
}

bool ParseConfiguredWheelOrder(
	const std::array<Vans::VansSceneVehicleTokenConfig, 4>& configuredOrder,
	const std::string& objectName,
	size_t wheelCount,
	std::vector<size_t>& outOrder,
	std::string& error)
{
	if (wheelCount < configuredOrder.size())
	{
		error = "Vehicle '" + objectName + "' wheelOrder requires four resolved tire groups";
		return false;
	}

	outOrder.assign(wheelCount, 0);
	for (size_t i = 0; i < outOrder.size(); ++i)
		outOrder[i] = i;

	std::array<bool, 4> seen = { false, false, false, false };
	for (size_t sourceIndex = 0; sourceIndex < configuredOrder.size(); ++sourceIndex)
	{
		const int slot = ParseWheelSlot(configuredOrder[sourceIndex]);
		if (slot < 0 || seen[slot])
		{
			error = "Vehicle '" + objectName + "' has an invalid or duplicate wheelOrder entry at index " +
				std::to_string(sourceIndex);
			return false;
		}
		seen[slot] = true;
		outOrder[slot] = sourceIndex;
	}

	return true;
}
}

VansSceneVehicleBuildResult VansSceneVehicleComponentBuilder::BuildVehicles(
	VansScene& scene,
	const std::vector<VansSceneVehicleBuildRequest>& requests)
{
	VansSceneVehicleBuildResult result;
	const auto& sceneObjects = scene.GetSceneObjects();
	result.builtVehicles.reserve(requests.size());
	for (const VansSceneVehicleBuildRequest& request : requests)
	{
		if (request.ownerEntityGuid.empty() || request.componentGuid.empty())
		{
			result.error = "Vehicle build request is missing a stable owner or component GUID";
			return result;
		}
		VansScriptObject* obj = scene.FindObjectByGuid(request.ownerEntityGuid);
		if (!obj)
		{
			result.error = "Vehicle owner is unavailable for entity '" +
				request.ownerEntityGuid + "'";
			return result;
		}
		if (obj->GetComponent<VansScriptVehicleComponent>())
		{
			result.error = "Vehicle component is already present for entity '" +
				request.ownerEntityGuid + "'";
			return result;
		}
		const Vans::VansSceneVehicleComponentConfig& vehicleConfig = request.config;

		std::string bodyNodeName;
			uint32_t bodyTransformID = UINT32_MAX;
			VansScriptObject* bodyObj = nullptr;
			std::vector<VansRenderNode*> bodyRenderNodesForBounds;
			if (vehicleConfig.bodyObject)
			{
				const std::string& bodyObjName = *vehicleConfig.bodyObject;
				bodyObj = scene.FindSceneObjectByName(bodyObjName);
				if (bodyObj)
				{
					bodyTransformID = GetObjectTransformID(bodyObj);
					auto* rc = bodyObj->GetComponent<VansScriptRenderComponent>();
					if (rc && rc->m_RenderNode)
						bodyNodeName = rc->m_RenderNode->m_NodeName;
				}
				else
				{
					result.error = "Vehicle body object was not found: " + bodyObjName;
					return result;
				}
			}
			if (bodyObj && !bodyObj->m_EntityGuid.empty())
			{
				for (VansScriptObject* candidate : sceneObjects)
				{
					if (!candidate)
						continue;
					auto* rc = candidate->GetComponent<VansScriptRenderComponent>();
					VansRenderNode* rn = rc ? rc->m_RenderNode : nullptr;
					if (!rn)
						continue;

					if (rn->m_ParentEntityGuid == bodyObj->m_EntityGuid ||
						rn->m_ParentGroupKey == bodyObj->m_EntityGuid ||
						candidate == bodyObj)
					{
						bodyRenderNodesForBounds.push_back(rn);
					}
				}
			}

			std::vector<std::string> tireNodeNames;
			std::vector<uint32_t> tireTransformIDs;
			std::vector<std::vector<VansEngine::VansVehicleVisualBinding>> wheelVisualBindings;
			std::vector<PxVec3> wheelGroupPivots;
			std::vector<PxVec3> wheelGroupBoundsMin;
			std::vector<PxVec3> wheelGroupBoundsMax;
			std::unordered_set<VansRenderNode*> tireRenderNodesForBodyExclusion;
			if (!vehicleConfig.tireObjects.empty())
			{
				for (const Vans::VansSceneVehicleTireGroupConfig& tireConfig : vehicleConfig.tireObjects)
				{
					const std::vector<std::string>& groupObjectNames = tireConfig.objectNames;
					if (groupObjectNames.empty())
						continue;

					struct PendingWheelVisual
					{
						uint32_t transformID = UINT32_MAX;
						VansRenderNode* renderNode = nullptr;
					};
					std::vector<PendingWheelVisual> pendingVisuals;
					std::vector<VansRenderNode*> renderNodesForPivot;

					for (const std::string& tireObjName : groupObjectNames)
					{
						VansScriptObject* tireObj = scene.FindSceneObjectByName(tireObjName);
						if (!tireObj)
						{
							result.error = "Vehicle tire object was not found: " + tireObjName;
							return result;
						}

						PendingWheelVisual visual;
						visual.transformID = GetObjectTransformID(tireObj);
						if (visual.transformID != UINT32_MAX)
							result.drivenTransformIds.insert(visual.transformID);

						auto* rc = tireObj->GetComponent<VansScriptRenderComponent>();
						if (rc && rc->m_RenderNode)
						{
							visual.renderNode = rc->m_RenderNode;
							renderNodesForPivot.push_back(rc->m_RenderNode);
							tireRenderNodesForBodyExclusion.insert(rc->m_RenderNode);
						}

						if (visual.transformID != UINT32_MAX)
							pendingVisuals.push_back(visual);
					}

					if (pendingVisuals.empty())
						continue;

					PxVec3 groupBoundsMin, groupBoundsMax;
					const bool hasGroupBounds = CalculateBoundsFromRenderNodes(renderNodesForPivot, groupBoundsMin, groupBoundsMax);
					PxVec3 groupPivot = hasGroupBounds ? (groupBoundsMin + groupBoundsMax) * 0.5f : PxVec3(0.0f);
					ApplyPxVec3(tireConfig.pivot, groupPivot);

					PxVec3 visualPivot = groupPivot;
					ApplyPxVec3(tireConfig.visualPivot, visualPivot);

					PxVec3 wheelCenter = groupPivot;
					if (tireConfig.wheelCenter)
						wheelCenter = ToPxVec3(*tireConfig.wheelCenter);
					else if (tireConfig.suspensionPivot)
						wheelCenter = ToPxVec3(*tireConfig.suspensionPivot);

					wheelGroupPivots.push_back(wheelCenter);
					wheelGroupBoundsMin.push_back(hasGroupBounds ? groupBoundsMin : groupPivot);
					wheelGroupBoundsMax.push_back(hasGroupBounds ? groupBoundsMax : groupPivot);

					std::vector<VansEngine::VansVehicleVisualBinding> groupBindings;
					groupBindings.reserve(pendingVisuals.size());
					for (const PendingWheelVisual& visual : pendingVisuals)
					{
						VansEngine::VansVehicleVisualBinding binding;
						binding.transformID = visual.transformID;
						binding.pivotLocal = visualPivot;
						groupBindings.push_back(binding);
					}
					wheelVisualBindings.push_back(std::move(groupBindings));

					tireTransformIDs.push_back(pendingVisuals.front().transformID);
					tireNodeNames.push_back(pendingVisuals.front().renderNode ? pendingVisuals.front().renderNode->m_NodeName : std::string());
				}
			}

			glm::vec3 spawnPos(0.0f, 5.0f, 0.0f);
			if (vehicleConfig.position)
			{
				spawnPos = glm::vec3(
					(*vehicleConfig.position)[0],
					(*vehicleConfig.position)[1],
					(*vehicleConfig.position)[2]);
			}
			else if (bodyTransformID != UINT32_MAX &&
					 Vans::VansTransformStore::IsAllocated(bodyTransformID))
			{
				spawnPos = Vans::VansTransformStore::Read(bodyTransformID).m_Position;
			}

			VansEngine::VansVehicleTuning tuning;
			std::string tuningError;
			if (!ParseVehicleTuning(vehicleConfig, tuning, tuningError))
			{
				result.error = "Vehicle tuning is invalid for object '" + obj->m_ObjectName + "': " + tuningError;
				return result;
			}

			std::vector<size_t> wheelOrder;
			if (vehicleConfig.wheelOrder)
			{
				std::string wheelOrderError;
				if (!ParseConfiguredWheelOrder(
					*vehicleConfig.wheelOrder,
					obj->m_ObjectName,
					wheelGroupPivots.size(),
					wheelOrder,
					wheelOrderError))
				{
					result.error = wheelOrderError;
					return result;
				}
				auto applyWheelOrder = [&](auto& values)
				{
					auto ordered = values;
					const size_t count = std::min(values.size(), wheelOrder.size());
					for (size_t i = 0; i < count; ++i)
						ordered[i] = values[wheelOrder[i]];
					values = std::move(ordered);
				};

				applyWheelOrder(wheelGroupPivots);
				applyWheelOrder(wheelGroupBoundsMin);
				applyWheelOrder(wheelGroupBoundsMax);
				applyWheelOrder(wheelVisualBindings);
				applyWheelOrder(tireTransformIDs);
				applyWheelOrder(tireNodeNames);
			}

			const bool autoWheelGeometry = vehicleConfig.tuning.autoWheelGeometry.value_or(false);
			const PxVehicleFrame vehicleFrame = tuning.BuildFrame();
			if (!vehicleConfig.tuning.bodyGeometryExcludeObjects.empty())
			{
				for (const std::string& excludeName : vehicleConfig.tuning.bodyGeometryExcludeObjects)
				{
					VansScriptObject* excludedObj = scene.FindSceneObjectByName(excludeName);
					auto* rc = excludedObj ? excludedObj->GetComponent<VansScriptRenderComponent>() : nullptr;
					if (rc && rc->m_RenderNode)
						tireRenderNodesForBodyExclusion.insert(rc->m_RenderNode);
				}
			}
			if (tuning.autoBodyGeometry && !bodyRenderNodesForBounds.empty())
			{
				std::vector<VansRenderNode*> filteredBodyRenderNodes;
				filteredBodyRenderNodes.reserve(bodyRenderNodesForBounds.size());
				for (VansRenderNode* rn : bodyRenderNodesForBounds)
				{
					if (rn && tireRenderNodesForBodyExclusion.find(rn) == tireRenderNodesForBodyExclusion.end())
						filteredBodyRenderNodes.push_back(rn);
				}

				PxVec3 bodyBoundsMin, bodyBoundsMax;
				if (CalculateBoundsFromRenderNodes(filteredBodyRenderNodes, bodyBoundsMin, bodyBoundsMax))
				{
					const PxVec3 unscaledHalfExtents = (bodyBoundsMax - bodyBoundsMin) * 0.5f + tuning.bodyGeometryPadding;
					const PxVec3 halfExtents(
						unscaledHalfExtents.x * tuning.bodyGeometryHalfExtentsScale.x,
						unscaledHalfExtents.y * tuning.bodyGeometryHalfExtentsScale.y,
						unscaledHalfExtents.z * tuning.bodyGeometryHalfExtentsScale.z);
					const PxVec3 center = (bodyBoundsMin + bodyBoundsMax) * 0.5f + tuning.bodyGeometryCenterOffset;
					tuning.bodyBoxHalfExtents = PxVec3(
						std::max(halfExtents.x, 0.01f),
						std::max(halfExtents.y, 0.01f),
						std::max(halfExtents.z, 0.01f));
					tuning.bodyBoxLocalPose = PxTransform(center, PxQuat(PxIdentity));
				}
			}
			if (autoWheelGeometry && wheelGroupBoundsMin.size() >= 4 && wheelGroupBoundsMax.size() >= 4)
			{
				float radiusSum = 0.0f;
				float halfWidthSum = 0.0f;
				for (size_t wi = 0; wi < 4; ++wi)
				{
					const float verticalExtent = VehicleAxisExtent(wheelGroupBoundsMin[wi], wheelGroupBoundsMax[wi], vehicleFrame.getVrtAxis());
					const float longitudinalExtent = VehicleAxisExtent(wheelGroupBoundsMin[wi], wheelGroupBoundsMax[wi], vehicleFrame.getLngAxis());
					const float lateralExtent = VehicleAxisExtent(wheelGroupBoundsMin[wi], wheelGroupBoundsMax[wi], vehicleFrame.getLatAxis());
					radiusSum += 0.5f * std::max(verticalExtent, longitudinalExtent);
					halfWidthSum += 0.5f * lateralExtent;
				}
				tuning.wheelRadius = std::max(0.01f, radiusSum * 0.25f);
				tuning.wheelHalfWidth = std::max(0.01f, halfWidthSum * 0.25f);
			}
			if (!vehicleConfig.tuning.suspensionAttachmentPositions && wheelGroupPivots.size() >= 4)
			{
				const PxVec3 upAxis = vehicleFrame.getVrtAxis();
				constexpr float kGravityMagnitude = 9.81f;
				for (size_t wi = 0; wi < 4; ++wi)
				{
					const float stiffness = std::max(1.0f, tuning.suspensionStiffness[wi]);
					const float staticJounce = std::clamp(
						tuning.sprungMass[wi] * kGravityMagnitude / stiffness,
						0.0f,
						tuning.suspensionTravelDist);
					const float visualRestOffset = tuning.suspensionTravelDist - staticJounce + tuning.wheelVisualGroundClearance;
					tuning.suspensionAttachmentPositions[wi] = wheelGroupPivots[wi] + upAxis * visualRestOffset;
				}
			}
			if (tuning.autoAlignToGround && wheelGroupPivots.size() >= 4)
			{
				const PxVec3 upAxis = vehicleFrame.getVrtAxis();
				const PxVec3 suspensionTravelDir = -upAxis;
				float averageWheelCenterHeight = 0.0f;
				for (size_t wi = 0; wi < 4; ++wi)
				{
					const float stiffness = std::max(1.0f, tuning.suspensionStiffness[wi]);
					const float staticJounce = std::clamp(
						tuning.sprungMass[wi] * 9.81f / stiffness,
						0.0f,
						tuning.suspensionTravelDist);
					const PxVec3 wheelCenterLocal =
						tuning.suspensionAttachmentPositions[wi] +
						suspensionTravelDir * (tuning.suspensionTravelDist - staticJounce);
					averageWheelCenterHeight += VehicleAxisCoordinate(wheelCenterLocal, upAxis);
				}
				averageWheelCenterHeight *= 0.25f;

				const PxVec3 spawnPx(spawnPos.x, spawnPos.y, spawnPos.z);
				const float spawnHeight = VehicleAxisCoordinate(spawnPx, upAxis);
				const float desiredWheelCenterHeight =
					tuning.groundHeight + tuning.wheelRadius + tuning.groundClearance;
				const float currentWheelCenterHeight = spawnHeight + averageWheelCenterHeight;
				const float deltaHeight = desiredWheelCenterHeight - currentWheelCenterHeight;
				spawnPos += glm::vec3(upAxis.x, upAxis.y, upAxis.z) * deltaHeight;
			}

			std::string runtimeError;
			VansEngine::VansPhysicsVehicle* vehicle = scene.BuildVehicleRuntime(
				&VansEngine::VansPhysicsSystem::GetInstance(), spawnPos,
				bodyNodeName, tireNodeNames, bodyTransformID, tireTransformIDs,
				tuning, wheelVisualBindings, runtimeError);
			if (!vehicle)
			{
				result.error = "Vehicle runtime could not be created for object '" +
					obj->m_ObjectName + "': " + runtimeError;
				return result;
			}
		auto* component = new VansScriptVehicleComponent();
		component->m_ComponentName = "vehicle";
		component->m_ComponentGuid = request.componentGuid;
		component->m_Vehicle = vehicle;
		obj->AddComponent(component);
		result.builtVehicles.push_back({ request.ownerEntityGuid, component });
	}

	result.success = true;
	return result;
}
}
