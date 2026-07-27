#include "VansSceneVehicleComponentBuilder.h"

#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansMesh.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <optional>

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

PxVehicleAxes::Enum ParseVehicleAxis(const Vans::VansSceneVehicleTokenConfig& value, PxVehicleAxes::Enum fallback)
{
	if (value.index)
	{
		const int axis = *value.index;
		if (axis >= 0 && axis < static_cast<int>(PxVehicleAxes::eMAX_NB_AXES))
			return static_cast<PxVehicleAxes::Enum>(axis);
		return fallback;
	}
	if (!value.name)
		return fallback;

	std::string axis = NormalizeToken(*value.name);
	if (axis == "posx" || axis == "+x" || axis == "x") return PxVehicleAxes::ePosX;
	if (axis == "negx" || axis == "-x") return PxVehicleAxes::eNegX;
	if (axis == "posy" || axis == "+y" || axis == "y") return PxVehicleAxes::ePosY;
	if (axis == "negy" || axis == "-y") return PxVehicleAxes::eNegY;
	if (axis == "posz" || axis == "+z" || axis == "z") return PxVehicleAxes::ePosZ;
	if (axis == "negz" || axis == "-z") return PxVehicleAxes::eNegZ;
	return fallback;
}

PxVec3 VehicleAxisToVec3(PxVehicleAxes::Enum axis)
{
	switch (axis)
	{
	case PxVehicleAxes::ePosX: return PxVec3(1.0f, 0.0f, 0.0f);
	case PxVehicleAxes::eNegX: return PxVec3(-1.0f, 0.0f, 0.0f);
	case PxVehicleAxes::ePosY: return PxVec3(0.0f, 1.0f, 0.0f);
	case PxVehicleAxes::eNegY: return PxVec3(0.0f, -1.0f, 0.0f);
	case PxVehicleAxes::ePosZ: return PxVec3(0.0f, 0.0f, 1.0f);
	case PxVehicleAxes::eNegZ: return PxVec3(0.0f, 0.0f, -1.0f);
	default: return PxVec3(0.0f, 1.0f, 0.0f);
	}
}

float VehicleAxisCoordinate(const PxVec3& value, PxVehicleAxes::Enum axis)
{
	switch (axis)
	{
	case PxVehicleAxes::ePosX: return value.x;
	case PxVehicleAxes::eNegX: return -value.x;
	case PxVehicleAxes::ePosY: return value.y;
	case PxVehicleAxes::eNegY: return -value.y;
	case PxVehicleAxes::ePosZ: return value.z;
	case PxVehicleAxes::eNegZ: return -value.z;
	default: return value.y;
	}
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

float VehicleAxisExtent(const PxVec3& boundsMin, const PxVec3& boundsMax, PxVehicleAxes::Enum axis)
{
	switch (axis)
	{
	case PxVehicleAxes::ePosX:
	case PxVehicleAxes::eNegX:
		return boundsMax.x - boundsMin.x;
	case PxVehicleAxes::ePosY:
	case PxVehicleAxes::eNegY:
		return boundsMax.y - boundsMin.y;
	case PxVehicleAxes::ePosZ:
	case PxVehicleAxes::eNegZ:
		return boundsMax.z - boundsMin.z;
	default:
		return 0.0f;
	}
}

VansEngine::VansVehicleTuning ParseVehicleTuning(const Vans::VansSceneVehicleComponentConfig& vehicleConfig)
{
	VansEngine::VansVehicleTuning tuning;
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
	if (t.longitudinalAxis) tuning.longitudinalAxis = ParseVehicleAxis(*t.longitudinalAxis, tuning.longitudinalAxis);
	if (t.forwardAxis) tuning.longitudinalAxis = ParseVehicleAxis(*t.forwardAxis, tuning.longitudinalAxis);
	if (t.lateralAxis) tuning.lateralAxis = ParseVehicleAxis(*t.lateralAxis, tuning.lateralAxis);
	if (t.verticalAxis) tuning.verticalAxis = ParseVehicleAxis(*t.verticalAxis, tuning.verticalAxis);

	if (t.wheelRadius) tuning.wheelRadius = *t.wheelRadius;
	if (t.wheelHalfWidth) tuning.wheelHalfWidth = *t.wheelHalfWidth;
	tuning.wheelRadius = std::clamp(tuning.wheelRadius, 0.01f, 2.0f);
	tuning.wheelHalfWidth = std::clamp(tuning.wheelHalfWidth, 0.01f, 2.0f);
	if (t.wheelMass) tuning.wheelMass = *t.wheelMass;
	if (t.wheelMoi) tuning.wheelMoi = *t.wheelMoi;
	if (t.wheelDampingRate) tuning.wheelDampingRate = *t.wheelDampingRate;
	if (t.visualWheelRollSign) tuning.visualWheelRollSign = *t.visualWheelRollSign;
	if (t.wheelVisualGroundClearance) tuning.wheelVisualGroundClearance = *t.wheelVisualGroundClearance;
	if (t.enableWheelSimulationCollision) tuning.enableWheelSimulationCollision = *t.enableWheelSimulationCollision;
	if (t.collisionLayer) tuning.collisionLayerName = *t.collisionLayer;
	if (t.layer) tuning.collisionLayerName = *t.layer;
	if (t.useRoadQueryLayerFilter) tuning.useRoadQueryLayerFilter = *t.useRoadQueryLayerFilter;
	if (t.roadQueryLayerFilter) tuning.useRoadQueryLayerFilter = *t.roadQueryLayerFilter;
	if (t.physxActorUpdateMode)
	{
		std::string mode = NormalizeToken(*t.physxActorUpdateMode);
		if (mode == "acceleration" || mode == "applyacceleration")
			tuning.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_ACCELERATION;
		else if (mode == "velocity" || mode == "applyvelocity")
			tuning.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_VELOCITY;
		else
			VANS_LOG_WARN("[VehicleTuning] Unknown physxActorUpdateMode '" << mode << "'; using default velocity mode.");
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
			const int layerIndex = layerMgr.GetLayerIndex(layerName);
			if (layerIndex >= 0 && layerIndex < 32)
				roadMask |= (1u << static_cast<PxU32>(layerIndex));
		}
		if (roadMask != 0u)
		{
			tuning.roadQueryMask = roadMask;
			tuning.useCustomRoadQueryMask = true;
		}
		else
		{
			VANS_LOG_WARN("[VehicleTuning] roadQueryLayers resolved to an empty mask; using the collision layer mask.");
		}
	}
	if (t.autoAlignToGround) tuning.autoAlignToGround = *t.autoAlignToGround;
	if (t.groundHeight) tuning.groundHeight = *t.groundHeight;
	if (t.groundClearance) tuning.groundClearance = *t.groundClearance;
	tuning.groundClearance = std::clamp(tuning.groundClearance, -0.5f, 1.0f);
	if (t.startHeightOffset) tuning.startHeightOffset = *t.startHeightOffset;
	if (t.wheelCollisionMode)
	{
		std::string mode = NormalizeToken(*t.wheelCollisionMode);
		tuning.enableWheelSimulationCollision =
			(mode == "simulation" || mode == "simulationandquery" ||
			 mode == "simulation_and_query" || mode == "collider");
	}

	ApplyVec3Array4(t.suspensionAttachmentPositions, tuning.suspensionAttachmentPositions);
	if (t.suspensionTravelDist) tuning.suspensionTravelDist = *t.suspensionTravelDist;
	ApplyFloatArray4(t.suspensionStiffness, tuning.suspensionStiffness);
	ApplyFloatArray4(t.suspensionDamping, tuning.suspensionDamping);
	ApplyFloatArray4(t.sprungMass, tuning.sprungMass);

	if (t.brakeMaxTorque) tuning.brakeMaxTorque = *t.brakeMaxTorque;
	if (t.handbrakeMaxTorque) tuning.handbrakeMaxTorque = *t.handbrakeMaxTorque;
	if (t.maxSteerAngleDeg) tuning.maxSteerAngleRad = glm::radians(*t.maxSteerAngleDeg);
	if (t.maxSteerAngleRad) tuning.maxSteerAngleRad = *t.maxSteerAngleRad;
	if (t.ackermannWheelBase) tuning.ackermannWheelBase = *t.ackermannWheelBase;
	if (t.ackermannTrackWidth) tuning.ackermannTrackWidth = *t.ackermannTrackWidth;
	if (t.ackermannStrength) tuning.ackermannStrength = *t.ackermannStrength;

	if (t.enginePeakTorque) tuning.enginePeakTorque = *t.enginePeakTorque;
	if (t.engineMaxOmega) tuning.engineMaxOmega = *t.engineMaxOmega;
	if (t.gearboxFinalRatio) tuning.gearboxFinalRatio = *t.gearboxFinalRatio;
	if (t.gearboxSwitchTime) tuning.gearboxSwitchTime = *t.gearboxSwitchTime;
	if (t.autoboxLatency) tuning.autoboxLatency = *t.autoboxLatency;
	if (t.clutchStrength) tuning.clutchStrength = *t.clutchStrength;

	return tuning;
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

	if (slot == "frontleft" || slot == "leftfront" || slot == "fl" || slot == "lf") return 0;
	if (slot == "frontright" || slot == "rightfront" || slot == "fr" || slot == "rf") return 1;
	if (slot == "rearleft" || slot == "leftrear" || slot == "rl" || slot == "lr" ||
		slot == "backleft" || slot == "leftback") return 2;
	if (slot == "rearright" || slot == "rightrear" || slot == "rr" || slot == "rightr" ||
		slot == "backright" || slot == "rightback") return 3;
	return -1;
}

bool ParseConfiguredWheelOrder(
	const std::optional<std::array<Vans::VansSceneVehicleTokenConfig, 4>>& configuredOrder,
	const std::string& objectName,
	size_t wheelCount,
	std::vector<size_t>& outOrder)
{
	if (!configuredOrder)
		return false;

	if (configuredOrder->size() < 4)
	{
		VANS_LOG_WARN("[VehicleWheelOrder] object='" << objectName
			<< "' wheelOrder must contain four wheel slots; keeping tireObjects order.");
		return false;
	}

	outOrder.assign(wheelCount, 0);
	for (size_t i = 0; i < outOrder.size(); ++i)
		outOrder[i] = i;

	std::array<bool, 4> seen = { false, false, false, false };
	for (size_t sourceIndex = 0; sourceIndex < configuredOrder->size(); ++sourceIndex)
	{
		const int slot = ParseWheelSlot((*configuredOrder)[sourceIndex]);
		if (slot < 0 || seen[slot])
		{
			VANS_LOG_WARN("[VehicleWheelOrder] object='" << objectName
				<< "' invalid wheelOrder entry at index " << sourceIndex
				<< "; keeping tireObjects order.");
			return false;
		}
		seen[slot] = true;
		outOrder[slot] = sourceIndex;
	}

	return seen[0] && seen[1] && seen[2] && seen[3];
}
}

void VansSceneVehicleComponentBuilder::AddVehiclePlaceholder(
	VansScriptObject& object,
	const Vans::VansSceneVehicleObjectConfig& objectConfig)
{
	if (!objectConfig.vehicle)
		return;
	auto* vehicleComp = new VansScriptVehicleComponent();
	vehicleComp->m_ComponentName = "vehicle";
	vehicleComp->m_Vehicle = nullptr;
	object.AddComponent(vehicleComp);
}

std::unordered_set<uint32_t> VansSceneVehicleComponentBuilder::ResolveVehicles(
	VansScene& scene,
	const Vans::VansSceneVehicleObjectConfigs& objectConfigs)
{
	std::unordered_set<uint32_t> vehicleDrivenTransformIDs;
	const auto& sceneObjects = scene.GetSceneObjects();
	int objIndex = 0;
	for (const Vans::VansSceneVehicleObjectConfig& objectConfig : objectConfigs)
	{
		if (objectConfig.vehicle)
		{
			const Vans::VansSceneVehicleComponentConfig& vehicleConfig = *objectConfig.vehicle;
			VansScriptObject* obj = sceneObjects[sceneObjects.size() - objectConfigs.size() + objIndex];
			auto* vc = obj->GetComponent<VansScriptVehicleComponent>();

			std::string bodyNodeName;
			uint32_t bodyTransformID = UINT32_MAX;
			VansScriptObject* bodyObj = nullptr;
			std::vector<VansRenderNode*> bodyRenderNodesForBounds;
			if (vehicleConfig.bodyObject)
			{
				std::string bodyObjName = *vehicleConfig.bodyObject;
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
					VANS_LOG_WARN("[LoadSceneObjects] Vehicle body object not found: " << bodyObjName);
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
						rn->m_ParentGroupName == bodyObj->m_ObjectName ||
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
							VANS_LOG_WARN("[LoadSceneObjects] Vehicle tire object not found: " << tireObjName);
							continue;
						}

						PendingWheelVisual visual;
						visual.transformID = GetObjectTransformID(tireObj);
						if (visual.transformID != UINT32_MAX)
							vehicleDrivenTransformIDs.insert(visual.transformID);

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
					 bodyTransformID < static_cast<uint32_t>(VansTransformStore::GlobalTransforms.size()))
			{
				spawnPos = VansTransformStore::GetTransform(bodyTransformID).m_Position;
			}

			VansEngine::VansVehicleTuning tuning = ParseVehicleTuning(vehicleConfig);

			std::vector<size_t> wheelOrder;
			if (wheelGroupPivots.size() >= 4 &&
				(ParseConfiguredWheelOrder(vehicleConfig.wheelOrder, obj->m_ObjectName, wheelGroupPivots.size(), wheelOrder) ||
				 ParseConfiguredWheelOrder(vehicleConfig.tuning.wheelOrder, obj->m_ObjectName, wheelGroupPivots.size(), wheelOrder)))
			{
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

				VANS_LOG("[VehicleWheelOrder] object='" << obj->m_ObjectName
					<< "' configured wheelOrder applied: frontLeft=(" << wheelGroupPivots[0].x << ", "
					<< wheelGroupPivots[0].y << ", " << wheelGroupPivots[0].z << ")"
					<< " frontRight=(" << wheelGroupPivots[1].x << ", "
					<< wheelGroupPivots[1].y << ", " << wheelGroupPivots[1].z << ")"
					<< " rearLeft=(" << wheelGroupPivots[2].x << ", "
					<< wheelGroupPivots[2].y << ", " << wheelGroupPivots[2].z << ")"
					<< " rearRight=(" << wheelGroupPivots[3].x << ", "
					<< wheelGroupPivots[3].y << ", " << wheelGroupPivots[3].z << ")");
			}

			const bool autoWheelGeometry = vehicleConfig.tuning.autoWheelGeometry.value_or(false);
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
					VANS_LOG("[VehicleAutoBodyGeometry] object='" << obj->m_ObjectName
						<< "' bodyNodes=" << filteredBodyRenderNodes.size()
						<< " excludedTireNodes=" << tireRenderNodesForBodyExclusion.size()
						<< " bodyBoundsMin=(" << bodyBoundsMin.x << ", " << bodyBoundsMin.y << ", " << bodyBoundsMin.z << ")"
						<< " bodyBoundsMax=(" << bodyBoundsMax.x << ", " << bodyBoundsMax.y << ", " << bodyBoundsMax.z << ")"
						<< " bodyHalfExtentsScale=(" << tuning.bodyGeometryHalfExtentsScale.x << ", "
						<< tuning.bodyGeometryHalfExtentsScale.y << ", " << tuning.bodyGeometryHalfExtentsScale.z << ")"
						<< " bodyCenterOffset=(" << tuning.bodyGeometryCenterOffset.x << ", "
						<< tuning.bodyGeometryCenterOffset.y << ", " << tuning.bodyGeometryCenterOffset.z << ")"
						<< " bodyBoxHalfExtents=(" << tuning.bodyBoxHalfExtents.x << ", "
						<< tuning.bodyBoxHalfExtents.y << ", " << tuning.bodyBoxHalfExtents.z << ")"
						<< " bodyBoxLocalPosition=(" << tuning.bodyBoxLocalPose.p.x << ", "
						<< tuning.bodyBoxLocalPose.p.y << ", " << tuning.bodyBoxLocalPose.p.z << ")");
				}
			}
			if (autoWheelGeometry && wheelGroupBoundsMin.size() >= 4 && wheelGroupBoundsMax.size() >= 4)
			{
				float radiusSum = 0.0f;
				float halfWidthSum = 0.0f;
				for (size_t wi = 0; wi < 4; ++wi)
				{
					const float verticalExtent = VehicleAxisExtent(wheelGroupBoundsMin[wi], wheelGroupBoundsMax[wi], tuning.verticalAxis);
					const float longitudinalExtent = VehicleAxisExtent(wheelGroupBoundsMin[wi], wheelGroupBoundsMax[wi], tuning.longitudinalAxis);
					const float lateralExtent = VehicleAxisExtent(wheelGroupBoundsMin[wi], wheelGroupBoundsMax[wi], tuning.lateralAxis);
					radiusSum += 0.5f * std::max(verticalExtent, longitudinalExtent);
					halfWidthSum += 0.5f * lateralExtent;
					VANS_LOG("[VehicleAutoWheelGeometry] object='" << obj->m_ObjectName
						<< "' wheel=" << wi
						<< " boundsMin=(" << wheelGroupBoundsMin[wi].x << ", " << wheelGroupBoundsMin[wi].y << ", " << wheelGroupBoundsMin[wi].z << ")"
						<< " boundsMax=(" << wheelGroupBoundsMax[wi].x << ", " << wheelGroupBoundsMax[wi].y << ", " << wheelGroupBoundsMax[wi].z << ")"
						<< " verticalExtent=" << verticalExtent
						<< " longitudinalExtent=" << longitudinalExtent
						<< " lateralExtent=" << lateralExtent
						<< " renderRadius=" << (0.5f * std::max(verticalExtent, longitudinalExtent))
						<< " renderHalfWidth=" << (0.5f * lateralExtent));
				}
				tuning.wheelRadius = std::max(0.01f, radiusSum * 0.25f);
				tuning.wheelHalfWidth = std::max(0.01f, halfWidthSum * 0.25f);
				VANS_LOG("[VehicleAutoWheelGeometry] object='" << obj->m_ObjectName
					<< "' final wheelRadius=" << tuning.wheelRadius
					<< " wheelHalfWidth=" << tuning.wheelHalfWidth);
			}
			if (!vehicleConfig.tuning.suspensionAttachmentPositions && wheelGroupPivots.size() >= 4)
			{
				const PxVec3 upAxis = VehicleAxisToVec3(tuning.verticalAxis);
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
					VANS_LOG("[VehicleAutoSuspension] object='" << obj->m_ObjectName
						<< "' wheel=" << wi
						<< " pivot=(" << wheelGroupPivots[wi].x << ", " << wheelGroupPivots[wi].y << ", " << wheelGroupPivots[wi].z << ")"
						<< " staticJounce=" << staticJounce
						<< " clearance=" << tuning.wheelVisualGroundClearance
						<< " attachment=(" << tuning.suspensionAttachmentPositions[wi].x << ", "
						<< tuning.suspensionAttachmentPositions[wi].y << ", "
						<< tuning.suspensionAttachmentPositions[wi].z << ")");
				}
			}
			if (tuning.autoAlignToGround && wheelGroupPivots.size() >= 4)
			{
				const PxVec3 upAxis = VehicleAxisToVec3(tuning.verticalAxis);
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
					averageWheelCenterHeight += VehicleAxisCoordinate(wheelCenterLocal, tuning.verticalAxis);
				}
				averageWheelCenterHeight *= 0.25f;

				const PxVec3 spawnPx(spawnPos.x, spawnPos.y, spawnPos.z);
				const float spawnHeight = VehicleAxisCoordinate(spawnPx, tuning.verticalAxis);
				const float desiredWheelCenterHeight =
					tuning.groundHeight + tuning.wheelRadius + tuning.groundClearance;
				const float currentWheelCenterHeight = spawnHeight + averageWheelCenterHeight;
				const float deltaHeight = desiredWheelCenterHeight - currentWheelCenterHeight;
				spawnPos += glm::vec3(upAxis.x, upAxis.y, upAxis.z) * deltaHeight;

				VANS_LOG("[VehicleAutoGroundAlign] object='" << obj->m_ObjectName
					<< "' groundHeight=" << tuning.groundHeight
					<< " wheelRadius=" << tuning.wheelRadius
					<< " groundClearance=" << tuning.groundClearance
					<< " averageWheelCenterHeight=" << averageWheelCenterHeight
					<< " oldSpawnHeight=" << spawnHeight
					<< " newSpawn=(" << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << ")");
			}

			VansEngine::VansPhysicsVehicle* vehicle = scene.BuildVehicleRuntime(&VansEngine::VansPhysicsSystem::GetInstance(), spawnPos,
				bodyNodeName, tireNodeNames, bodyTransformID, tireTransformIDs, tuning, wheelVisualBindings);
			if (vc)
				vc->m_Vehicle = vehicle;
		}
		++objIndex;
	}

	return vehicleDrivenTransformIDs;
}
}
