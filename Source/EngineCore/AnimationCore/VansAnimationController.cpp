#include "VansAnimationController.h"
#include "VansAnimGraph.h"
#include "VansAnimationSampler.h"
#include "VansPoseMath.h"
#include "MotionMatching/VansMotionMatching.h"
#include "FootPlacement/VansFootPlacementSolver.h"
#include "../Util/VansLog.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <unordered_set>

using namespace VansGraphics;

namespace
{
	std::uint64_t ResolveMarkerId(const AnimationSyncMarker& marker)
	{
		return marker.id != 0 ? marker.id : VansAnimationStableId(marker.name);
	}

	bool ResolveMarkerSyncedTime(const VansAnimationSyncState& leaderSync,
	                           float leaderRawTime,
	                           float leaderDuration,
	                           const VansAnimationClip& followerClip,
	                           float& outTime)
	{
		if (!leaderSync.valid || leaderSync.markerId == 0 || leaderSync.nextMarkerId == 0
			|| leaderDuration <= 0.0f || followerClip.duration <= 0.0f)
			return false;
		const AnimationSyncMarker* previous = nullptr;
		const AnimationSyncMarker* next = nullptr;
		for (const AnimationSyncMarker& marker : followerClip.syncMarkers)
		{
			const std::uint64_t id = ResolveMarkerId(marker);
			if (id == leaderSync.markerId) previous = &marker;
			if (id == leaderSync.nextMarkerId) next = &marker;
		}
		if (!previous || !next)
			return false;
		float previousTime = previous->time;
		float nextTime = next->time;
		if (nextTime <= previousTime)
			nextTime += followerClip.duration;
		float localTime = previousTime + std::clamp(leaderSync.phase, 0.0f, 1.0f)
			* (nextTime - previousTime);
		const float cycle = std::floor(leaderRawTime / leaderDuration);
		if (localTime >= followerClip.duration)
			localTime -= followerClip.duration;
		outTime = cycle * followerClip.duration + localTime;
		return true;
	}

	const AnimGraphStateMachineNode* FindPrimaryStateMachine(const VansAnimGraph& graph)
	{
		std::vector<int> executionPlan;
		std::string error;
		if (!graph.BuildExecutionPlan(executionPlan, error))
			return nullptr;
		for (int nodeId : executionPlan)
		{
			const VansAnimGraphNode* node = graph.GetNode(nodeId);
			if (node && node->GetType() == AnimGraphNodeType::StateMachine)
				return static_cast<const AnimGraphStateMachineNode*>(node);
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
//  Construction & Destruction
// ---------------------------------------------------------------------------

VansAnimationController::VansAnimationController()
{
	std::memset(&m_BoneMatricesSSBO, 0, sizeof(BoneMatricesSSBO));
	for (uint32_t i = 0; i < MAX_BONES; i++)
		m_BoneMatricesSSBO.boneMatrices[i] = glm::mat4(1.0f);
}

VansAnimationController::~VansAnimationController()
{
}

bool VansAnimationController::SetLayerStack(
	std::vector<VansAnimationLayerGraphSetup> layers,
	std::string& error)
{
	error.clear();
	if (layers.empty())
	{
		error = "Animator requires one Base layer";
		return false;
	}
	std::unordered_set<std::string> layerIds;
	int baseCount = 0;
	for (size_t index = 0; index < layers.size(); ++index)
	{
		const VansAnimationLayerGraphSetup& layer = layers[index];
		if (layer.definition.id.empty() || layer.definition.name.empty()
		    || layer.definition.graphId.empty() || !layer.graph)
		{
			error = "Animation layers require non-empty IDs, names and graph bindings";
			return false;
		}
		if (!layerIds.insert(layer.definition.id).second)
		{
			error = "Animation layer IDs must be unique";
			return false;
		}
		if (layer.definition.kind == VansAnimationLayerKind::Base)
		{
			++baseCount;
			if (index != 0)
			{
				error = "The Base animation layer must be at index zero";
				return false;
			}
		}
		else if (!layer.mask)
		{
			error = "Overlay animation layer '" + layer.definition.name + "' is missing its Bone Mask asset";
			return false;
		}
		VansAnimGraphInstance validation(*layer.graph);
		if (!validation.IsCompiled())
		{
			error = "Animation layer graph '" + layer.definition.graphId
				+ "' failed compilation: " + validation.GetCompileError();
			return false;
		}
	}
	if (baseCount != 1)
	{
		error = "Animator must contain exactly one Base layer";
		return false;
	}
	for (size_t index = 0; index < layers.size(); ++index)
	{
		const VansAnimationLayerDefinition& definition = layers[index].definition;
		if (definition.sync == VansLayerSyncMode::Independent)
			continue;
		bool foundEarlierLeader = false;
		size_t leaderIndex = 0;
		for (size_t leader = 0; leader < index; ++leader)
			if (layers[leader].definition.id == definition.syncLeaderLayerId)
			{
				foundEarlierLeader = true;
				leaderIndex = leader;
				break;
			}
		if (!foundEarlierLeader)
		{
			error = "Synced Layer '" + definition.name + "' requires an earlier leader Layer";
			return false;
		}
		if (definition.sync == VansLayerSyncMode::SyncedGraph)
		{
			const AnimGraphStateMachineNode* leaderStateMachine =
				FindPrimaryStateMachine(*layers[leaderIndex].graph);
			const AnimGraphStateMachineNode* followerStateMachine =
				FindPrimaryStateMachine(*layers[index].graph);
			if (!leaderStateMachine || !followerStateMachine)
			{
				error = "Synced Graph Layer '" + definition.name
					+ "' requires primary State Machine nodes on both Layers";
				return false;
			}
			std::unordered_set<std::string> leaderStates;
			std::unordered_set<std::string> followerStates;
			for (const AnimatorState& state : leaderStateMachine->m_States)
				leaderStates.insert(state.name);
			for (const AnimatorState& state : followerStateMachine->m_States)
				followerStates.insert(state.name);
			if (leaderStates != followerStates)
			{
				error = "Synced Graph Layer '" + definition.name
					+ "' must expose the same logical State names as its leader";
				return false;
			}
		}
	}

	std::vector<LayerRuntime> runtimes;
	runtimes.reserve(layers.size());
	for (VansAnimationLayerGraphSetup& setup : layers)
	{
		LayerRuntime runtime;
		runtime.definition = std::move(setup.definition);
		runtime.graph = std::move(setup.graph);
		runtime.instance = std::make_unique<VansAnimGraphInstance>(*runtime.graph);
		runtime.maskAsset = std::move(setup.mask);
		runtime.parameterScratch = m_Parameters;
		runtimes.push_back(std::move(runtime));
	}
	for (size_t index = 0; index < runtimes.size(); ++index)
	{
		if (runtimes[index].definition.sync == VansLayerSyncMode::Independent)
			continue;
		for (size_t leader = 0; leader < index; ++leader)
		{
			if (runtimes[leader].definition.id == runtimes[index].definition.syncLeaderLayerId)
			{
				runtimes[index].syncLeaderIndex = static_cast<int>(leader);
				break;
			}
		}
	}
	m_LayerRuntimes = std::move(runtimes);
	std::string slotResetError;
	m_SlotRuntime.Configure({}, slotResetError);
	m_SlotPayloads.clear();
	if (m_MotionMatching)
	{
		EnsureMotionMatchingGraphNode();
		RebuildLayerInstances();
	}
	return true;
}

bool VansAnimationController::SetTargetPostProcessGraph(
	std::unique_ptr<VansAnimGraph> graph,
	std::string& error)
{
	error.clear();
	if (!graph)
	{
		error = "Target Post Process Graph definition is missing";
		return false;
	}

	VansAnimGraphInstance validation(*graph);
	if (!validation.IsCompiled())
	{
		error = "Target Post Process Graph failed compilation: " + validation.GetCompileError();
		return false;
	}

	std::size_t targetInputCount = 0;
	bool targetInputReachable = false;
	for (const auto& [nodeId, node] : graph->GetNodes())
	{
		if (!node)
			continue;
		if (node->GetType() == AnimGraphNodeType::TargetPoseInput)
		{
			++targetInputCount;
			targetInputReachable = std::find(validation.GetExecutionPlan().begin(),
				validation.GetExecutionPlan().end(), nodeId) != validation.GetExecutionPlan().end();
		}
		switch (node->GetType())
		{
		case AnimGraphNodeType::Entry:
		case AnimGraphNodeType::Clip:
		case AnimGraphNodeType::SpeedScale:
		case AnimGraphNodeType::StateMachine:
		case AnimGraphNodeType::MotionMatching:
		case AnimGraphNodeType::Slot:
			error = "Target Post Process Graph cannot contain pose-source or playback nodes";
			return false;
		default:
			break;
		}
	}
	if (targetInputCount != 1 || !targetInputReachable)
	{
		error = "Target Post Process Graph requires exactly one reachable Target Pose Input";
		return false;
	}

	m_TargetPostProcessGraph = std::move(graph);
	m_TargetPostProcessInstance = std::make_unique<VansAnimGraphInstance>(*m_TargetPostProcessGraph);
	return true;
}

void VansAnimationController::ClearTargetPostProcessGraph()
{
	m_TargetPostProcessInstance.reset();
	m_TargetPostProcessGraph.reset();
}

bool VansAnimationController::SetSlots(
	std::vector<VansAnimationSlotDefinition> slots,
	std::string& error)
{
	std::vector<std::string> slotIds;
	slotIds.reserve(slots.size());
	std::unordered_set<std::string> layerIds;
	for (const LayerRuntime& layer : m_LayerRuntimes)
		layerIds.insert(layer.definition.id);
	for (const VansAnimationSlotDefinition& slot : slots)
	{
		if (layerIds.find(slot.layerId) == layerIds.end())
		{
			error = "Slot '" + slot.name + "' references an unknown Layer";
			return false;
		}
		const LayerRuntime* owner = nullptr;
		for (const LayerRuntime& layer : m_LayerRuntimes)
			if (layer.definition.id == slot.layerId) { owner = &layer; break; }
		const VansAnimGraphNode* node = owner ? owner->graph->GetNode(slot.slotNodeId) : nullptr;
		if (!node || node->GetType() != AnimGraphNodeType::Slot)
		{
			error = "Slot '" + slot.name + "' does not bind a Slot node in its Layer Graph";
			return false;
		}
		const auto* slotNode = static_cast<const AnimGraphSlotNode*>(node);
		if (slotNode->m_SlotId != slot.id)
		{
			error = "Slot '" + slot.name + "' ID does not match its bound Graph node";
			return false;
		}
		slotIds.push_back(slot.id);
	}
	if (!m_SlotRuntime.Configure(std::move(slots), error))
		return false;
	// Slot IDs are definition data. Allocate the lookup nodes at configuration
	// time so a stable frame only refreshes the existing payload storage.
	m_SlotPayloads.clear();
	m_SlotPayloads.reserve(slotIds.size());
	for (const std::string& slotId : slotIds)
		m_SlotPayloads.try_emplace(slotId);
	return true;
}

bool VansAnimationController::TransferRuntimeStateFrom(
	const VansAnimationController& previous,
	const Skeleton& skeleton,
	std::string& diagnostic)
{
	diagnostic.clear();
	bool fullyCompatible = true;

	// Reapply scene-owned runtime configuration before restoring graph state,
	// because Motion Matching installation may rebuild Layer instances.
	if (previous.m_MotionMatching)
		ConfigureMotionMatching(previous.m_MotionMatching->GetSettings());
	if (previous.m_HasExternalFootPlacementSettings)
	{
		ConfigureFootPlacement(previous.m_ExternalFootPlacementSettings, skeleton);
		SetFootPlacementRuntimeState(previous.m_FootPlacementState);
	}

	for (auto& [name, parameter] : m_Parameters)
	{
		const auto source = previous.m_Parameters.find(name);
		if (source != previous.m_Parameters.end() && source->second.type == parameter.type)
			parameter = source->second;
	}

	std::unordered_set<std::string> restoredLayerIds;
	for (LayerRuntime& layer : m_LayerRuntimes)
	{
		const auto source = std::find_if(previous.m_LayerRuntimes.begin(), previous.m_LayerRuntimes.end(),
			[&](const LayerRuntime& candidate) { return candidate.definition.id == layer.definition.id; });
		if (source == previous.m_LayerRuntimes.end())
		{
			fullyCompatible = false;
			continue;
		}
		restoredLayerIds.insert(layer.definition.id);
		if (source->definition.kind == layer.definition.kind
			&& source->definition.blendMode == layer.definition.blendMode)
			layer.state = source->state;
		else
			fullyCompatible = false;
		if (!layer.instance->RestoreRuntimeState(source->instance->CaptureRuntimeState()))
			fullyCompatible = false;
	}
	for (const LayerRuntime& previousLayer : previous.m_LayerRuntimes)
		if (restoredLayerIds.find(previousLayer.definition.id) == restoredLayerIds.end())
			fullyCompatible = false;

	m_SlotRuntime.TransferRuntimeStateFrom(previous.m_SlotRuntime, m_Clips);
	m_PlaybackState = previous.m_PlaybackState;
	m_GlobalSpeed = previous.m_GlobalSpeed;
	m_RootMotionEnabled = previous.m_RootMotionEnabled;
	m_RootMotionApplyToOwner = previous.m_RootMotionApplyToOwner;
	m_RootBoneIndex = previous.m_RootBoneIndex;
	m_OwnerWorldTransform = previous.m_OwnerWorldTransform;
	m_LastRootMotionDelta = glm::vec3(0.0f);
	m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	m_SampledEvents.clear();
	m_SampledCurves.clear();
	m_SyncState = {};
	if (!fullyCompatible)
		diagnostic = "Some Layer or Graph runtime state was reset because stable IDs or structure changed";
	return fullyCompatible;
}

VansSlotPlaybackHandle VansAnimationController::PlaySlot(
	const std::string& slotId,
	const VansSlotPlayRequest& request)
{
	return m_SlotRuntime.Play(slotId, request);
}

bool VansAnimationController::StopSlot(VansSlotPlaybackHandle handle, float blendOut, bool force)
{
	return m_SlotRuntime.Stop(handle, blendOut, force);
}

bool VansAnimationController::DriveSlot(
	VansSlotPlaybackHandle handle,
	float playbackTime,
	float weight)
{
	return m_SlotRuntime.Drive(handle, playbackTime, weight);
}

VansSlotPlaybackStatus VansAnimationController::GetSlotStatus(VansSlotPlaybackHandle handle) const
{
	return m_SlotRuntime.GetStatus(handle);
}

bool VansAnimationController::IsSlotActive(const std::string& slotId) const
{
	return m_SlotRuntime.IsSlotActive(slotId);
}

const VansAnimationSlotDefinition* VansAnimationController::FindSlotDefinition(const std::string& slotId) const
{
	const auto& definitions = m_SlotRuntime.GetDefinitions();
	const auto found = std::find_if(definitions.begin(), definitions.end(),
		[&](const VansAnimationSlotDefinition& definition) { return definition.id == slotId; });
	return found == definitions.end() ? nullptr : &*found;
}

const std::vector<VansSlotLifecycleEvent>& VansAnimationController::GetSlotLifecycleEvents() const
{
	return m_SlotRuntime.GetLifecycleEvents();
}

void VansAnimationController::ConfigureMotionMatching(const MotionMatchingSettings& settings)
{
	if (!m_MotionMatching)
		m_MotionMatching = std::make_unique<VansMotionMatchingRuntime>();
	m_MotionMatching->Configure(settings);
	EnsureMotionMatchingGraphNode();
	RebuildLayerInstances();
}

void VansAnimationController::RebuildLayerInstances()
{
	for (LayerRuntime& layer : m_LayerRuntimes)
	{
		layer.instance = std::make_unique<VansAnimGraphInstance>(*layer.graph);
		if (!layer.instance->IsCompiled())
			VANS_LOG_ERROR("[AnimController] Layer Graph compile failed for '" << layer.definition.name
			               << "': " << layer.instance->GetCompileError());
	}
}

const MotionMatchingDebugData* VansAnimationController::GetMotionMatchingDebugData() const
{
	return m_MotionMatching ? &m_MotionMatching->GetDebugData() : nullptr;
}

const MotionMatchingSettings* VansAnimationController::GetMotionMatchingSettings() const
{
	return m_MotionMatching ? &m_MotionMatching->GetSettings() : nullptr;
}

bool VansAnimationController::MotionMatchingPrefersRootMotion() const
{
	return m_MotionMatching && m_MotionMatching->PrefersRootMotionThisFrame();
}

void VansAnimationController::EnsureMotionMatchingGraphNode()
{
	if (!m_MotionMatching)
		return;
	VansAnimGraph* graph = !m_LayerRuntimes.empty() ? m_LayerRuntimes.front().graph.get() : nullptr;
	if (!graph)
		return;

	for (const auto& [id, node] : graph->GetNodes())
	{
		if (node && node->GetType() == AnimGraphNodeType::MotionMatching)
			return;
	}

	const int outputId = graph->GetOutputNodeId();
	if (outputId < 0)
		return;

	int sourceNodeId = -1;
	int sourcePinIndex = 0;
	int outputLinkId = -1;
	for (const AnimGraphLink& link : graph->GetLinks())
	{
		if (link.toNodeId == outputId && link.toPinIndex == 0)
		{
			sourceNodeId = link.fromNodeId;
			sourcePinIndex = link.fromPinIndex;
			outputLinkId = link.linkId;
			break;
		}
	}
	if (sourceNodeId < 0 || outputLinkId < 0)
		return;

	auto mmNode = std::make_unique<AnimGraphMotionMatchingNode>();
	const int mmNodeId = graph->AddNode(std::move(mmNode));
	if (mmNodeId < 0)
		return;

	graph->RemoveLink(outputLinkId);
	graph->AddLink(sourceNodeId, sourcePinIndex, mmNodeId, 0);
	graph->AddLink(mmNodeId, 0, outputId, 0);
}

const FootPlacementDebugData* VansAnimationController::GetFootPlacementDebugData() const
{
	return m_FootPlacement ? &m_FootPlacement->GetDebugData() : nullptr;
}

void VansAnimationController::ConfigureFootPlacement(const FootPlacementSettings& settings, const Skeleton& skeleton)
{
	m_ExternalFootPlacementSettings = settings;
	m_HasExternalFootPlacementSettings = true;
	m_FootPlacementSourceNodeId = -1;
	m_FootPlacementSettings = settings;
	if (!m_FootPlacement)
		m_FootPlacement = std::make_unique<VansFootPlacementSolver>();
	if (!m_FootPlacement->Configure(settings, skeleton))
	{
		VANS_LOG_WARN("FootPlacement configure failed for controller '" << m_Name << "': missing humanoid leg bones");
		m_FootPlacement.reset();
	}
}

void VansAnimationController::SetFootPlacementEnabled(bool enabled)
{
	m_ExternalFootPlacementSettings.enabled = enabled;
	m_FootPlacementSettings.enabled = enabled;
	if (m_FootPlacement)
		m_FootPlacement->SetEnabled(enabled);
}

void VansAnimationController::SetFootPlacementDebugVisualization(bool enabled)
{
	m_ExternalFootPlacementSettings.debugVisualization = enabled;
	m_FootPlacementSettings.debugVisualization = enabled;
	if (m_FootPlacement)
		m_FootPlacement->SetDebugVisualization(enabled);
}

void VansAnimationController::SetFootPlacementRuntimeState(const FootPlacementRuntimeState& state)
{
	m_FootPlacementState = state;
}

// ---------------------------------------------------------------------------
// Parameter management.
// ---------------------------------------------------------------------------

void VansAnimationController::AddParameter(const std::string& name, AnimatorParamType type)
{
	AnimatorParameter param;
	param.name = name;
	param.type = type;
	m_Parameters[name] = param;
}

void VansAnimationController::RemoveParameter(const std::string& name)
{
	m_Parameters.erase(name);
}

bool VansAnimationController::HasParameter(const std::string& name) const
{
	return m_Parameters.find(name) != m_Parameters.end();
}

void VansAnimationController::SetFloat(const std::string& name, float value)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Float)
		it->second.floatVal = value;
}

void VansAnimationController::SetBool(const std::string& name, bool value)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Bool)
		it->second.boolVal = value;
}

void VansAnimationController::SetInt(const std::string& name, int value)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Int)
		it->second.intVal = value;
}

void VansAnimationController::SetTrigger(const std::string& name)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Trigger)
		it->second.boolVal = true;
}

void VansAnimationController::ResetTrigger(const std::string& name)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Trigger)
		it->second.boolVal = false;
}

float VansAnimationController::GetFloat(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Float)
		return it->second.floatVal;
	return 0.0f;
}

bool VansAnimationController::GetBool(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Bool)
		return it->second.boolVal;
	return false;
}

int VansAnimationController::GetInt(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Int)
		return it->second.intVal;
	return 0;
}

bool VansAnimationController::IsTriggerSet(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Trigger)
		return it->second.boolVal;
	return false;
}

void VansAnimationController::SetVector3(const std::string& name, const glm::vec3& value)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Vector3)
		it->second.vec3Val = value;
}

void VansAnimationController::SetQuaternion(const std::string& name, const glm::quat& value)
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Quaternion)
		it->second.quatVal = value;
}

glm::vec3 VansAnimationController::GetVector3(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Vector3)
		return it->second.vec3Val;
	return glm::vec3(0.0f);
}

glm::quat VansAnimationController::GetQuaternion(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Quaternion)
		return it->second.quatVal;
	return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

const std::unordered_map<std::string, AnimatorParameter>& VansAnimationController::GetParameters() const
{
	return m_Parameters;
}

const glm::mat4& VansAnimationController::GetCachedGlobalTransform(int boneIndex) const
{
	static const glm::mat4 identity(1.0f);
	if (boneIndex < 0 || boneIndex >= static_cast<int>(m_CachedGlobalTransforms.size()))
		return identity;
	return m_CachedGlobalTransforms[boneIndex];
}

bool VansAnimationController::SubmitExternalModelPose(
	const std::vector<glm::mat4>& modelSpaceTransforms,
	const Skeleton& skeleton,
	float deltaTime,
	VansExternalPoseEvaluationMode mode)
{
	m_FramePool.BeginFrame();
	VansAnimationFrameMemory::Scope frameMemoryScope(m_FramePool.Resource());
	struct FramePoolCompletion
	{
		VansAnimationFramePool& pool;
		~FramePoolCompletion() { pool.EndFrame(); }
	} framePoolCompletion{ m_FramePool };

	if (modelSpaceTransforms.size() != skeleton.bones.size())
	{
		VANS_LOG_WARN("[AnimController] SubmitExternalModelPose bone count mismatch: input="
			<< modelSpaceTransforms.size() << " skeleton=" << skeleton.bones.size());
		return false;
	}

	if (mode == VansExternalPoseEvaluationMode::DirectFinalPose)
	{
		BuildFinalMatrices(modelSpaceTransforms, skeleton);
		return true;
	}

	VansPosePayload pose;
	if (!ConvertModelPoseToLocalPayload(modelSpaceTransforms, skeleton, pose))
		return false;
	VansPosePayload processed;
	if (!EvaluateTargetPostProcess(deltaTime, skeleton, pose, processed))
		return false;
	return FinalizeLocalPose(deltaTime, skeleton, std::move(processed), false, false, false);
}

// ---------------------------------------------------------------------------
// Clip management.
// ---------------------------------------------------------------------------

void VansAnimationController::AddClip(const std::string& name, VansAnimationClip&& clip)
{
	m_Clips[name] = std::move(clip);
	if (m_MotionMatching)
		m_MotionMatching->MarkDatabaseDirty();
}

void VansAnimationController::AddClip(const std::string& name, const VansAnimationClip& clip)
{
	m_Clips[name] = clip;
	if (m_MotionMatching)
		m_MotionMatching->MarkDatabaseDirty();
}

void VansAnimationController::RemoveClip(const std::string& name)
{
	m_Clips.erase(name);
	if (m_MotionMatching)
		m_MotionMatching->MarkDatabaseDirty();
}

VansAnimationClip* VansAnimationController::GetClip(const std::string& name)
{
	auto it = m_Clips.find(name);
	return (it != m_Clips.end()) ? &it->second : nullptr;
}

const VansAnimationClip* VansAnimationController::GetClip(const std::string& name) const
{
	auto it = m_Clips.find(name);
	return (it != m_Clips.end()) ? &it->second : nullptr;
}

const std::unordered_map<std::string, VansAnimationClip>& VansAnimationController::GetClipsMap() const
{
	return m_Clips;
}

std::vector<std::string> VansAnimationController::GetClipNames() const
{
	std::vector<std::string> names;
	names.reserve(m_Clips.size());
	for (const auto& [name, clip] : m_Clips)
		names.push_back(name);
	return names;
}

// ---------------------------------------------------------------------------
// Playback control.
// ---------------------------------------------------------------------------

void VansAnimationController::Play()
{
	m_PlaybackState = AnimationState::Playing;
	m_SlotRuntime.Reset();
	for (auto& [slotId, payload] : m_SlotPayloads)
		payload = {};
	for (LayerRuntime& layer : m_LayerRuntimes)
	{
		layer.instance->Reset();
		layer.state = {};
	}
	if (m_TargetPostProcessInstance)
		m_TargetPostProcessInstance->Reset();
}

void VansAnimationController::Play(const std::string& stateName)
{
	bool found = false;
	for (LayerRuntime& layer : m_LayerRuntimes)
		found = layer.instance->PlayState(stateName) || found;
	if (!found)
	{
		VANS_LOG_WARN("[AnimController] " << m_Name << ": state '" << stateName << "' not found");
		return;
	}
	m_PlaybackState = AnimationState::Playing;
}

void VansAnimationController::Pause()
{
	if (m_PlaybackState == AnimationState::Playing || m_PlaybackState == AnimationState::Blending)
		m_PlaybackState = AnimationState::Paused;
}

void VansAnimationController::Resume()
{
	if (m_PlaybackState == AnimationState::Paused)
		m_PlaybackState = AnimationState::Playing;
}

void VansAnimationController::Stop()
{
	m_PlaybackState = AnimationState::Stopped;
	m_SlotRuntime.Reset();
	for (auto& [slotId, payload] : m_SlotPayloads)
		payload = {};
	for (LayerRuntime& layer : m_LayerRuntimes)
	{
		layer.instance->Reset();
		layer.state = {};
	}
	if (m_TargetPostProcessInstance)
		m_TargetPostProcessInstance->Reset();

	m_LastRootMotionDelta   = glm::vec3(0.0f);
	m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	m_SampledEvents.clear();
	m_SampledCurves.clear();
	m_SyncState = {};
}

void VansAnimationController::Reset()
{
	Stop();
	Play();
}

void VansAnimationController::SetSpeed(float speed)
{
	m_GlobalSpeed = speed;
}

float VansAnimationController::GetSpeed() const
{
	return m_GlobalSpeed;
}

// ---------------------------------------------------------------------------
// State query.
// ---------------------------------------------------------------------------

std::string VansAnimationController::GetCurrentStateName() const
{
	if (!m_LayerRuntimes.empty())
		return m_LayerRuntimes.front().instance->GetCurrentStateName();
	return {};
}

AnimationState VansAnimationController::GetPlaybackState() const
{
	return m_PlaybackState;
}

float VansAnimationController::GetCurrentPlayTime() const
{
	if (!m_LayerRuntimes.empty())
		return m_LayerRuntimes.front().instance->GetPrimaryPlaybackTime();
	return 0.0f;
}

float VansAnimationController::GetCurrentDuration() const
{
	if (m_LayerRuntimes.empty())
		return 0.0f;
	auto it = m_Clips.find(m_LayerRuntimes.front().instance->GetPrimaryClipName());
	return it == m_Clips.end() ? 0.0f : it->second.duration;
}

float VansAnimationController::GetNormalizedTime() const
{
	float dur = GetCurrentDuration();
	if (dur <= 0.0f) return 0.0f;
	return GetCurrentPlayTime() / dur;
}

// ---------------------------------------------------------------------------
//  Root Motion
// ---------------------------------------------------------------------------

void VansAnimationController::EnableRootMotion(bool enable)
{
	m_RootMotionEnabled = enable;
	m_LastRootMotionDelta = glm::vec3(0.0f);
	m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

bool VansAnimationController::IsRootMotionEnabled() const
{
	return m_RootMotionEnabled;
}

glm::vec3 VansAnimationController::GetRootMotionDelta() const
{
	return m_LastRootMotionDelta;
}

glm::quat VansAnimationController::GetRootRotationDelta() const
{
	return m_LastRootRotationDelta;
}

void VansAnimationController::SetBoneOverrides(const std::unordered_map<std::string, glm::mat4>* overrides)
{
	m_BoneOverrides = overrides;
}

bool VansAnimationController::SeekNormalizedTime(float normalizedTime)
{
	normalizedTime = glm::clamp(normalizedTime, 0.0f, 1.0f);
	bool changed = false;
	for (LayerRuntime& layer : m_LayerRuntimes)
	{
		if (!layer.instance)
			continue;
		const std::string clipName = layer.instance->GetPrimaryClipName();
		const VansAnimationClip* clip = GetClip(clipName);
		if (!clip || clip->duration <= 0.0f)
			continue;
		changed = layer.instance->SetPrimaryPlaybackTime(normalizedTime * clip->duration) || changed;
	}
	return changed;
}

std::vector<VansAnimationController::LayerRuntimeDebugInfo>
VansAnimationController::GetLayerRuntimeDebugInfo() const
{
	std::vector<LayerRuntimeDebugInfo> result;
	result.reserve(m_LayerRuntimes.size());
	for (const LayerRuntime& layer : m_LayerRuntimes)
	{
		LayerRuntimeDebugInfo info;
		info.id = layer.definition.id;
		info.name = layer.definition.name;
		info.weight = layer.definition.kind == VansAnimationLayerKind::Base
			? 1.0f : layer.state.currentWeight;
		info.enabled = layer.definition.enabled;
		info.kind = layer.definition.kind;
		info.blendMode = layer.definition.blendMode;
		info.evaluationMilliseconds = layer.lastEvaluationMilliseconds;
		if (layer.definition.kind == VansAnimationLayerKind::Base)
			info.boneWeights.assign(layer.compiledMask.weights.size(), 1.0f);
		else
		{
			info.boneWeights.reserve(layer.compiledMask.weights.size());
			for (float maskWeight : layer.compiledMask.weights)
				info.boneWeights.push_back(glm::clamp(maskWeight * info.weight, 0.0f, 1.0f));
		}
		if (layer.instance)
		{
			info.state = layer.instance->GetCurrentStateName();
			info.clip = layer.instance->GetPrimaryClipName();
			info.playbackTime = layer.instance->GetPrimaryPlaybackTime();
			if (const VansAnimationClip* clip = GetClip(info.clip); clip && clip->duration > 0.0f)
				info.normalizedTime = glm::clamp(info.playbackTime / clip->duration, 0.0f, 1.0f);
		}
		result.push_back(std::move(info));
	}
	return result;
}

void VansAnimationController::ResolveLayerReferencePose(
	const LayerRuntime& layer,
	const Skeleton& skeleton,
	VansAnimationFrameVector<VansBoneTransform>& outPose) const
{
	VansAnimationLayerMixer::BuildBindPose(skeleton, outPose);
	if (layer.definition.additiveReference == VansAdditiveReferenceMode::BindPose)
		return;

	const std::string* clipName = &layer.definition.referenceClipName;
	if (layer.definition.additiveReference != VansAdditiveReferenceMode::ReferenceClip)
		clipName = layer.instance ? &layer.instance->GetPrimaryClipName() : nullptr;
	if (!clipName)
		return;
	auto clip = m_Clips.find(*clipName);
	if (clip == m_Clips.end())
		return;

	VansAnimationSampleRequest request;
	request.loop = false;
	request.currentTime = layer.definition.additiveReference == VansAdditiveReferenceMode::FirstFrame
		? 0.0f : layer.definition.referenceTime;
	request.previousTime = request.currentTime;
	VansPosePayload sampled;
	if (VansAnimationSampler::Sample(clip->second, skeleton, request, sampled)
	    && sampled.localPose.size() == outPose.size())
		outPose = std::move(sampled.localPose);
}

bool VansAnimationController::EvaluateLayerStack(
	float deltaTime,
	const Skeleton& skeleton,
	VansPosePayload& outPayload)
{
	outPayload = {};
	if (m_LayerRuntimes.empty())
		return false;

	const std::uint64_t skeletonSignature = VansBoneMaskCompiler::ComputeSkeletonSignature(skeleton);
	VansAnimationFrameVector<VansBoneTransform> bindPose;
	VansAnimationLayerMixer::BuildBindPose(skeleton, bindPose);
	m_EvaluatedSyncScratch.clear();
	m_EvaluatedSyncScratch.resize(m_LayerRuntimes.size());
	auto& evaluatedSync = m_EvaluatedSyncScratch;

	for (size_t layerIndex = 0; layerIndex < m_LayerRuntimes.size(); ++layerIndex)
	{
		LayerRuntime& layer = m_LayerRuntimes[layerIndex];
		if (layer.compiledMask.skeletonSignature != skeletonSignature)
		{
			if (layer.definition.kind == VansAnimationLayerKind::Base)
			{
				layer.compiledMask = {};
				layer.compiledMask.assetId = "__full_body__";
				layer.compiledMask.skeletonSignature = skeletonSignature;
				layer.compiledMask.weights.assign(skeleton.bones.size(), 1.0f);
				layer.compiledMask.activeBones.reserve(skeleton.bones.size());
				for (size_t bone = 0; bone < skeleton.bones.size(); ++bone)
					layer.compiledMask.activeBones.push_back(static_cast<std::uint32_t>(bone));
				layer.compiledMask.rootWeight = skeleton.bones.empty() ? 0.0f : 1.0f;
				layer.compiledMask.allZero = skeleton.bones.empty();
				layer.compiledMask.allOne = !skeleton.bones.empty();
				layer.compiledMask.valid = true;
			}
			else if (layer.maskAsset)
				layer.compiledMask = VansBoneMaskCompiler::Compile(*layer.maskAsset, skeleton);
		}
		if (!layer.compiledMask.valid)
		{
			if (layerIndex == 0)
				return false;
			continue;
		}

		float targetWeight = layer.definition.fixedWeight;
		if (layer.definition.kind == VansAnimationLayerKind::Base)
			targetWeight = 1.0f;
		else if (layer.definition.useWeightParameter)
		{
			auto parameter = m_Parameters.find(layer.definition.weightParameter);
			targetWeight = parameter != m_Parameters.end()
			    && parameter->second.type == AnimatorParamType::Float
				? parameter->second.floatVal : 0.0f;
		}
		if (!layer.definition.enabled)
			targetWeight = 0.0f;
		targetWeight = std::clamp(targetWeight, 0.0f, 1.0f);
		if (!layer.state.initialized)
		{
			layer.state.currentWeight = targetWeight;
			layer.state.initialized = true;
		}
		else if (layer.definition.weightSmoothingTime > 0.0f)
		{
			const float alpha = 1.0f - std::exp(-std::max(0.0f, deltaTime)
				/ layer.definition.weightSmoothingTime);
			layer.state.currentWeight = glm::mix(layer.state.currentWeight, targetWeight, alpha);
		}
		else
			layer.state.currentWeight = targetWeight;

		const bool shouldUpdate = layer.definition.updateWhenWeightIsZero
			|| layer.state.currentWeight > 1.0e-6f
			|| layer.definition.kind == VansAnimationLayerKind::Base;
		if (!shouldUpdate)
		{
			layer.lastEvaluationMilliseconds = 0.0f;
			continue;
		}

		if (layer.parameterScratch.size() != m_Parameters.size())
			layer.parameterScratch = m_Parameters;
		else
			for (const auto& [name, parameter] : m_Parameters)
			{
				auto target = layer.parameterScratch.find(name);
				if (target == layer.parameterScratch.end())
				{
					layer.parameterScratch = m_Parameters;
					break;
				}
				target->second = parameter;
			}
		AnimGraphContext context;
		context.deltaTime = deltaTime * m_GlobalSpeed;
		context.skeleton = &skeleton;
		context.parameters = &layer.parameterScratch;
		context.clips = &m_Clips;
		context.motionMatching = m_MotionMatching.get();
		context.characterTrajectory = m_CharacterTrajectory;
		context.slotPayloads = &m_SlotPayloads;
		context.ownerWorldTransform = m_OwnerWorldTransform;
		if (layer.definition.sync == VansLayerSyncMode::Independent)
		{
			layer.instance->AdvanceTime(context.deltaTime, context);
		}
		else if (layer.definition.sync == VansLayerSyncMode::SyncedGraph)
		{
			const LayerRuntime& leader = m_LayerRuntimes[static_cast<size_t>(layer.syncLeaderIndex)];
			if (!layer.instance->SynchronizePrimaryStateMachineFrom(*leader.instance, m_Clips))
				return false;
			context.synchronizedStateFollower = true;
		}
		else
		{
			const LayerRuntime& leader = m_LayerRuntimes[static_cast<size_t>(layer.syncLeaderIndex)];
			if (layer.instance->GetPrimaryClipName().empty())
				layer.instance->SetPrimaryPlaybackTime(0.0f);

			const std::string& leaderClipName = leader.instance->GetPrimaryClipName();
			const std::string& followerClipName = layer.instance->GetPrimaryClipName();
			auto leaderClip = m_Clips.find(leaderClipName);
			auto followerClip = m_Clips.find(followerClipName);
			if (leaderClip != m_Clips.end() && followerClip != m_Clips.end()
				&& leaderClip->second.duration > 0.0f && followerClip->second.duration > 0.0f)
			{
				const float leaderRawTime = leader.instance->GetPrimaryPlaybackTime();
				float followerTime = leaderRawTime / leaderClip->second.duration
					* followerClip->second.duration;
				if (layer.definition.sync == VansLayerSyncMode::MarkerSync)
					ResolveMarkerSyncedTime(evaluatedSync[static_cast<size_t>(layer.syncLeaderIndex)],
						leaderRawTime, leaderClip->second.duration, followerClip->second, followerTime);
				layer.instance->SetPrimaryPlaybackTime(followerTime);
			}
		}
		const auto evaluationBegin = m_DebugMetricsEnabled
			? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		VansPosePayload sampled = layer.instance->Evaluate(context);
		if (m_DebugMetricsEnabled)
			layer.lastEvaluationMilliseconds = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - evaluationBegin).count();
		if (!sampled.valid)
		{
			if (layerIndex == 0)
				return false;
			continue;
		}

		const std::uint64_t stableLayerId = VansAnimationStableId(layer.definition.id);
		for (VansAnimationEventSample& event : sampled.events)
			event.sourceLayerId = stableLayerId;
		if (sampled.rootMotion.valid)
			sampled.rootMotion.sourceLayerId = stableLayerId;
		evaluatedSync[layerIndex] = sampled.sync;

		if (layerIndex == 0)
		{
			outPayload = std::move(sampled);
			continue;
		}
		VansAnimationFrameVector<VansBoneTransform> referencePose = bindPose;
		ResolveLayerReferencePose(layer, skeleton, referencePose);
		outPayload = VansAnimationLayerMixer::ApplyLayer(
			outPayload, sampled, layer.definition, layer.compiledMask,
			skeleton, referencePose, layer.state.currentWeight);
	}

	for (auto& [name, parameter] : m_Parameters)
		if (parameter.type == AnimatorParamType::Trigger)
			parameter.boolVal = false;
	return outPayload.valid;
}

bool VansAnimationController::EvaluateTargetPostProcess(
	float deltaTime,
	const Skeleton& skeleton,
	const VansPosePayload& input,
	VansPosePayload& output)
{
	if (!input.valid || input.localPose.size() != skeleton.bones.size())
		return false;
	if (!m_TargetPostProcessInstance)
	{
		output = input;
		return true;
	}

	AnimGraphContext context;
	context.deltaTime = deltaTime * m_GlobalSpeed;
	context.skeleton = &skeleton;
	context.parameters = &m_Parameters;
	context.clips = &m_Clips;
	context.motionMatching = nullptr;
	context.characterTrajectory = m_CharacterTrajectory;
	context.slotPayloads = nullptr;
	context.targetPoseInput = &input;
	context.ownerWorldTransform = m_OwnerWorldTransform;
	output = m_TargetPostProcessInstance->Evaluate(context);
	return output.valid && output.localPose.size() == skeleton.bones.size();
}

bool VansAnimationController::ConvertModelPoseToLocalPayload(
	const std::vector<glm::mat4>& modelSpaceTransforms,
	const Skeleton& skeleton,
	VansPosePayload& outPayload) const
{
	outPayload = {};
	if (modelSpaceTransforms.size() != skeleton.bones.size())
		return false;

	auto& localTransforms = m_LocalTransformScratch;
	localTransforms.assign(modelSpaceTransforms.size(), glm::mat4(1.0f));
	for (std::size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex)
	{
		const int parentIndex = skeleton.bones[boneIndex].parentIndex;
		if (parentIndex >= 0 && parentIndex < static_cast<int>(modelSpaceTransforms.size()))
			localTransforms[boneIndex] = glm::inverse(modelSpaceTransforms[static_cast<std::size_t>(parentIndex)])
				* modelSpaceTransforms[boneIndex];
		else
			localTransforms[boneIndex] = modelSpaceTransforms[boneIndex];
	}
	if (!VansPoseMath::FromMatrices(localTransforms, outPayload.localPose))
		return false;
	outPayload.valid = outPayload.localPose.size() == skeleton.bones.size();
	return outPayload.valid;
}

bool VansAnimationController::FinalizeLocalPose(
	float deltaTime,
	const Skeleton& skeleton,
	VansPosePayload pose,
	bool normalizeRoot,
	bool publishAnimationOutputs,
	bool deferWorldSpacePostProcess)
{
	const std::uint32_t boneCount = static_cast<std::uint32_t>(skeleton.bones.size());
	if (!pose.valid)
		return false;

	if (publishAnimationOutputs)
	{
		m_SampledNodeTransforms = std::move(pose.nodeTransforms);
		m_SampledEvents = std::move(pose.events);
		m_SampledCurves = std::move(pose.curves);
		m_SyncState = pose.sync;
		if (m_RootBoneIndex < 0)
			m_RootBoneIndex = DetectRootBoneIndex(skeleton);
		if (m_RootMotionEnabled && pose.rootMotion.valid)
		{
			m_LastRootMotionDelta = pose.rootMotion.translation;
			m_LastRootRotationDelta = pose.rootMotion.rotation;
			m_LastRootMotionValid = true;
		}
	}
	// Node Transform Animation is a first-class output even for clips without a
	// skeleton. Preserve it independently from skinning-pose finalization.
	if (boneCount == 0)
		return true;
	if (pose.localPose.size() != boneCount)
		return false;

	auto& localTransforms = m_LocalTransformScratch;
	VansPoseMath::ToMatrices(pose.localPose, localTransforms);
	ApplyBoneOverrides(localTransforms, skeleton);
	if (normalizeRoot)
	{
		if (m_RootBoneIndex < 0)
			m_RootBoneIndex = DetectRootBoneIndex(skeleton);
		NormalizeRootTransform(localTransforms, skeleton);
	}

	if (pose.footPlacement.valid && pose.footPlacement.settings)
	{
		if (m_FootPlacementSourceNodeId != pose.footPlacement.sourceNodeId)
		{
			m_FootPlacementSettings = *pose.footPlacement.settings;
			if (!m_FootPlacement)
				m_FootPlacement = std::make_unique<VansFootPlacementSolver>();
			if (!m_FootPlacement->Configure(m_FootPlacementSettings, skeleton))
				m_FootPlacement.reset();
			m_FootPlacementSourceNodeId = pose.footPlacement.sourceNodeId;
		}
	}
	else if (m_FootPlacementSourceNodeId >= 0)
	{
		m_FootPlacementSourceNodeId = -1;
		if (m_HasExternalFootPlacementSettings)
		{
			m_FootPlacementSettings = m_ExternalFootPlacementSettings;
			if (!m_FootPlacement)
				m_FootPlacement = std::make_unique<VansFootPlacementSolver>();
			if (!m_FootPlacement->Configure(m_FootPlacementSettings, skeleton))
				m_FootPlacement.reset();
		}
		else
			m_FootPlacement.reset();
	}

	if (deferWorldSpacePostProcess)
	{
		m_PreparedLocalTransforms = localTransforms;
		m_PreparedDeltaTime = deltaTime;
		m_HasPreparedFrame = true;
		return true;
	}
	ApplyFootPlacement(deltaTime, skeleton, localTransforms);
	UpdateHierarchy(localTransforms, skeleton);
	BuildFinalMatrices(localTransforms, skeleton);
	return true;
}

// ---------------------------------------------------------------------------
// Core per-frame update.
// ---------------------------------------------------------------------------

void VansAnimationController::Update(float deltaTime, const Skeleton& skeleton)

{
	UpdateInternal(deltaTime, skeleton, false);
}

void VansAnimationController::UpdateForMovement(float deltaTime, const Skeleton& skeleton)

{
	UpdateInternal(deltaTime, skeleton, true);
}

bool VansAnimationController::FinalizePreparedFrame(const Skeleton& skeleton)

{
	if (!m_HasPreparedFrame)
		return false;
	m_LocalTransformScratch = m_PreparedLocalTransforms;
	ApplyFootPlacement(m_PreparedDeltaTime, skeleton, m_LocalTransformScratch);
	UpdateHierarchy(m_LocalTransformScratch, skeleton);
	BuildFinalMatrices(m_LocalTransformScratch, skeleton);
	m_PreparedLocalTransforms.clear();
	m_HasPreparedFrame = false;
	return true;
}

void VansAnimationController::UpdateInternal(
	float deltaTime, const Skeleton& skeleton, bool deferWorldSpacePostProcess)
{
	m_FramePool.BeginFrame();
	VansAnimationFrameMemory::Scope frameMemoryScope(m_FramePool.Resource());
	struct FramePoolCompletion
	{
		VansAnimationFramePool& pool;
		~FramePoolCompletion() { pool.EndFrame(); }
	} framePoolCompletion{ m_FramePool };

	m_SampledNodeTransforms.clear();
	m_SampledEvents.clear();
	m_SampledCurves.clear();
	m_SyncState = {};
	m_LastRootMotionDelta = glm::vec3(0.0f);
	m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	m_LastRootMotionValid = false;

	if (m_PlaybackState == AnimationState::Stopped || m_PlaybackState == AnimationState::Paused)
		return;
	m_SlotRuntime.Update(deltaTime * m_GlobalSpeed, m_Clips, skeleton, m_SlotPayloads);

	if (!m_LayerRuntimes.empty())
	{
		VansPosePayload pose;
		if (!EvaluateLayerStack(deltaTime, skeleton, pose))
			return;
		VansPosePayload processed;
		if (!EvaluateTargetPostProcess(deltaTime, skeleton, pose, processed))
			return;
		FinalizeLocalPose(
			deltaTime, skeleton, std::move(processed), true, true,
			deferWorldSpacePostProcess);
		return;
	}

}

// ---------------------------------------------------------------------------
// Internal method: ApplyBoneOverrides
// ---------------------------------------------------------------------------

void VansAnimationController::ApplyBoneOverrides(std::vector<glm::mat4>& localTransforms,
                                                  const Skeleton& skeleton)
{
	if (!m_BoneOverrides || m_BoneOverrides->empty())
		return;

	for (const auto& [boneName, overrideTransform] : *m_BoneOverrides)
	{
		auto it = skeleton.boneNameToIndex.find(boneName);
		if (it != skeleton.boneNameToIndex.end())
		{
			int idx = it->second;
			if (idx >= 0 && idx < static_cast<int>(localTransforms.size()))
				localTransforms[idx] = overrideTransform;
		}
	}
}

// ---------------------------------------------------------------------------
// Internal method: ExtractRootMotion
// ---------------------------------------------------------------------------

void VansAnimationController::ApplyFootPlacement(float deltaTime,
                                                 const Skeleton& skeleton,
                                                 std::vector<glm::mat4>& localTransforms)
{
	if (!m_FootPlacement || !m_FootPlacementSettings.enabled)
		return;

	FootPlacementRuntimeState state = m_FootPlacementState;
	auto findParameter = [this](const std::string& name) -> const AnimatorParameter*
	{
		if (name.empty())
			return nullptr;
		const auto it = m_Parameters.find(name);
		return it != m_Parameters.end() ? &it->second : nullptr;
	};
	const AnimatorParameter* airborneParameter = findParameter(m_FootPlacementSettings.airborneParameter);
	if (airborneParameter)
	{
		const AnimatorParameter& param = *airborneParameter;
		if (param.type == AnimatorParamType::Bool)
			state.airborne = param.boolVal;
		else if (param.type == AnimatorParamType::Float)
			state.airborne = param.floatVal > 0.5f;
		else if (param.type == AnimatorParamType::Int)
			state.airborne = param.intVal != 0;
	}
	if (m_MotionMatching && m_MotionMatching->WasUsedThisFrame())
	{
		state.hasAnimationPlantWeights = true;
		state.leftPlantWeight = m_MotionMatching->GetLeftFootPlantWeight();
		state.rightPlantWeight = m_MotionMatching->GetRightFootPlantWeight();
	}
	else
	{
		state.hasAnimationPlantWeights = false;
		state.leftPlantWeight = 0.0f;
		state.rightPlantWeight = 0.0f;
	}
	m_FootPlacement->SetRuntimeState(state);
	m_FootPlacement->Solve(deltaTime, skeleton, m_OwnerWorldTransform, localTransforms);
}

void VansAnimationController::NormalizeRootTransform(std::vector<glm::mat4>& localTransforms,
                                                     const Skeleton& skeleton)
{
	if (m_RootBoneIndex < 0 || m_RootBoneIndex >= static_cast<int>(localTransforms.size()))
		return;

	glm::vec3 pos, scale, skew;
	glm::quat rot;
	glm::vec4 perspective;
	glm::decompose(localTransforms[m_RootBoneIndex], scale, rot, pos, skew, perspective);

	glm::vec3 bindPos, bindScale, bindSkew;
	glm::quat bindRot;
	glm::vec4 bindPerspective;
	glm::decompose(skeleton.bones[m_RootBoneIndex].localTransform,
	               bindScale, bindRot, bindPos, bindSkew, bindPerspective);

	glm::mat4 T = glm::translate(glm::mat4(1.0f), bindPos);
	// In-place/external locomotion owns both translation and facing. Preserving
	// sampled root rotation here double-applies authored turns on top of the CCT
	// owner rotation and drags the feet around the character.
	glm::mat4 R = glm::toMat4(glm::normalize(bindRot));
	glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
	localTransforms[m_RootBoneIndex] = T * R * S;
}

// ---------------------------------------------------------------------------
// Internal method: UpdateHierarchy
// ---------------------------------------------------------------------------

void VansAnimationController::UpdateHierarchy(std::vector<glm::mat4>& localTransforms,
                                               const Skeleton& skeleton)
{
	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());

	// Iterate in topological order so parents are processed before children.
	// Iterate in topological order to avoid incorrect results when parent indices
	// are greater than child indices.
	if (!skeleton.topologicalOrder.empty())
	{
		for (int b : skeleton.topologicalOrder)
		{
			const BoneInfo& bone = skeleton.bones[b];
			if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(boneCount))
				localTransforms[b] = localTransforms[bone.parentIndex] * localTransforms[b];
		}
	}
	else
	{
		// Fallback: index order only works when parentIndex < childIndex.
		for (uint32_t b = 0; b < boneCount; b++)
		{
			const BoneInfo& bone = skeleton.bones[b];
			if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(boneCount))
				localTransforms[b] = localTransforms[bone.parentIndex] * localTransforms[b];
		}
	}
}

// ---------------------------------------------------------------------------
// Internal method: BuildFinalMatrices
// ---------------------------------------------------------------------------

void VansAnimationController::BuildFinalMatrices(const std::vector<glm::mat4>& globalTransforms,
                                                   const Skeleton& skeleton)
{
	// Cache model-space global bone matrices for the bone attachment system.
	m_CachedGlobalTransforms = globalTransforms;

	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
	uint32_t limit = (std::min)(boneCount, MAX_BONES);

	for (uint32_t i = 0; i < limit; i++)
	{
		const BoneInfo& bone = skeleton.bones[i];
		// Skeleton globals start at the first imported bone, while skinned mesh
		// vertices are kept in Assimp mesh/model space. Applying the Assimp scene
		// root inverse here would rotate the final skinned result a second time.
		m_BoneMatricesSSBO.boneMatrices[i] = globalTransforms[i] * bone.offsetMatrix;
	}

	for (uint32_t i = limit; i < MAX_BONES; i++)
		m_BoneMatricesSSBO.boneMatrices[i] = glm::mat4(1.0f);
}

int VansAnimationController::DetectRootBoneIndex(const Skeleton& skeleton) const
{
	auto rootIt = skeleton.boneNameToIndex.find("root");
	if (rootIt != skeleton.boneNameToIndex.end())
		return rootIt->second;
	// Find the skeleton root node (parentIndex == -1).
	int skeletonRoot = -1;
	for (uint32_t i = 0; i < static_cast<uint32_t>(skeleton.bones.size()); i++)
	{
		if (skeleton.bones[i].parentIndex < 0)
		{
			skeletonRoot = static_cast<int>(i);
			break;
		}
	}
	if (skeletonRoot < 0)
		return -1;

	// Inspect the current clip to find bones that have animation keyframes.
	const VansAnimationClip* clip = nullptr;
	if (!m_LayerRuntimes.empty() && m_LayerRuntimes.front().instance)
	{
		auto clipIt = m_Clips.find(m_LayerRuntimes.front().instance->GetPrimaryClipName());
		if (clipIt != m_Clips.end())
			clip = &clipIt->second;
	}
	// If the skeleton root itself has keyed translation data, use it directly.
	if (clip && skeletonRoot < static_cast<int>(clip->boneKeyframes.size())
	    && !clip->boneKeyframes[skeletonRoot].empty())
		return skeletonRoot;

	// Otherwise, BFS to find the first descendant bone with keyed translation as the root-motion source.
	// Typical case: scene root has no animation, while motion data lives on a child root bone.
	std::queue<int> bfsQueue;
	for (int child : skeleton.bones[skeletonRoot].children)
		bfsQueue.push(child);

	while (!bfsQueue.empty())
	{
		int idx = bfsQueue.front();
		bfsQueue.pop();

		if (clip && idx < static_cast<int>(clip->boneKeyframes.size())
		    && !clip->boneKeyframes[idx].empty())
			return idx;

		for (int child : skeleton.bones[idx].children)
			bfsQueue.push(child);
	}

	// Fallback: no keyed bone was found, so use the skeleton root.
	return skeletonRoot;
}
