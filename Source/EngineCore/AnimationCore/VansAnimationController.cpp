#include "VansAnimationController.h"
#include "VansAnimGraph.h"
#include "VansAnimationSampler.h"
#include "VansPoseMath.h"
#include "MotionMatching/VansMotionMatching.h"
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

	float EvaluateGraphSetBlendCurve(VansGraphSetBlendCurve curve, float progress)
	{
		const float t = std::clamp(progress, 0.0f, 1.0f);
		return curve == VansGraphSetBlendCurve::SmoothStep
			? t * t * (3.0f - 2.0f * t) : t;
	}

	VansPosePayload BlendGraphSetPayloads(
		const VansPosePayload& source,
		const VansPosePayload& incoming,
		float alpha,
		const VansGraphSetTransitionPolicy& policy)
	{
		VansPosePayload result = VansPosePayloadMixer::BlendOverride(source, incoming, alpha);
		if (!result.valid)
			return result;
		if (policy.events == VansGraphSetEventPolicy::DominantSource)
			result.events = alpha < 0.5f ? source.events : incoming.events;
		switch (policy.rootMotion)
		{
		case VansGraphSetRootMotionPolicy::Blend:
			break;
		case VansGraphSetRootMotionPolicy::DominantSource:
			result.rootMotion = alpha < 0.5f ? source.rootMotion : incoming.rootMotion;
			break;
		case VansGraphSetRootMotionPolicy::IncomingOnly:
			result.rootMotion = incoming.rootMotion;
			break;
		}
		return result;
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

	bool ReadProceduralFloat(const void* context, const std::string& name, float& value)
	{
		const auto* parameters = static_cast<const std::unordered_map<std::string, AnimatorParameter>*>(context);
		if (!parameters) return false;
		const auto found = parameters->find(name);
		if (found == parameters->end() || found->second.type != AnimatorParamType::Float) return false;
		value = found->second.floatVal;
		return std::isfinite(value);
	}

	bool ReadProceduralVector3(const void* context, const std::string& name, glm::vec3& value)
	{
		const auto* parameters = static_cast<const std::unordered_map<std::string, AnimatorParameter>*>(context);
		if (!parameters) return false;
		const auto found = parameters->find(name);
		if (found == parameters->end() || found->second.type != AnimatorParamType::Vector3) return false;
		value = found->second.vec3Val;
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool ReadProceduralQuaternion(const void* context, const std::string& name, glm::quat& value)
	{
		const auto* parameters = static_cast<const std::unordered_map<std::string, AnimatorParameter>*>(context);
		if (!parameters) return false;
		const auto found = parameters->find(name);
		if (found == parameters->end() || found->second.type != AnimatorParamType::Quaternion) return false;
		value = found->second.quatVal;
		const float lengthSquared = glm::dot(value, value);
		if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f) return false;
		value = glm::normalize(value);
		return true;
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

bool VansAnimationController::SetAnimationGraphSets(
	std::vector<VansAnimationLayerSetup> layers,
	std::vector<VansAnimationGraphSetSetup> graphSets,
	std::string defaultGraphSetId,
	VansGraphSetTransitionPolicy defaultTransition,
	std::vector<VansGraphSetTransitionRule> transitionRules,
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
		const VansAnimationLayerSetup& layer = layers[index];
		if (layer.definition.id.empty() || layer.definition.name.empty())
		{
			error = "Animation layers require non-empty IDs and names";
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
	}
	if (baseCount != 1)
	{
		error = "Animator must contain exactly one Base layer";
		return false;
	}
	if (graphSets.empty() || defaultGraphSetId.empty())
	{
		error = "Animator requires at least one Graph Set and a default Graph Set ID";
		return false;
	}
	if (!std::isfinite(defaultTransition.duration) || defaultTransition.duration < 0.0f)
	{
		error = "Default Graph Set transition duration must be finite and non-negative";
		return false;
	}

	std::unordered_set<std::string> graphSetIds;
	bool hasDefaultGraphSet = false;
	for (const VansAnimationGraphSetSetup& graphSet : graphSets)
	{
		if (graphSet.definition.id.empty() || graphSet.definition.name.empty()
			|| !graphSetIds.insert(graphSet.definition.id).second)
		{
			error = "Graph Sets require unique non-empty IDs and non-empty names";
			return false;
		}
		hasDefaultGraphSet = hasDefaultGraphSet || graphSet.definition.id == defaultGraphSetId;
		if (graphSet.bindings.size() != layers.size()
			|| graphSet.definition.bindings.size() != layers.size())
		{
			error = "Graph Set '" + graphSet.definition.name
				+ "' must explicitly bind every Layer exactly once";
			return false;
		}
		for (size_t index = 0; index < layers.size(); ++index)
		{
			const VansAnimationGraphBindingSetup& binding = graphSet.bindings[index];
			const VansAnimationGraphBindingDefinition& authored = graphSet.definition.bindings[index];
			const VansAnimationLayerDefinition& layer = layers[index].definition;
			if (binding.definition.layerId != layer.id || authored.layerId != layer.id
				|| binding.definition.graphId != authored.graphId
				|| binding.definition.enabled != authored.enabled)
			{
				error = "Graph Set '" + graphSet.definition.name
					+ "' bindings must follow Layer Stack order and match their definitions";
				return false;
			}
			if (layer.kind == VansAnimationLayerKind::Base && !binding.definition.enabled)
			{
				error = "The Base Layer must be enabled in every Graph Set";
				return false;
			}
			if (!binding.definition.enabled)
			{
				if (!binding.definition.graphId.empty() || binding.graph)
				{
					error = "Disabled Graph Set bindings must not retain a Graph reference";
					return false;
				}
				continue;
			}
			if (binding.definition.graphId.empty() || !binding.graph)
			{
				error = "Enabled Graph Set bindings require a Graph ID and Graph definition";
				return false;
			}
			VansAnimGraphInstance validation(*binding.graph);
			if (!validation.IsCompiled())
			{
				error = "Graph '" + binding.definition.graphId + "' in Graph Set '"
					+ graphSet.definition.name + "' failed compilation: "
					+ validation.GetCompileError();
				return false;
			}
		}

		for (size_t index = 0; index < layers.size(); ++index)
		{
			const VansAnimationLayerDefinition& layer = layers[index].definition;
			if (layer.sync == VansLayerSyncMode::Independent
				|| !graphSet.bindings[index].definition.enabled)
				continue;
			size_t leaderIndex = layers.size();
			for (size_t leader = 0; leader < index; ++leader)
				if (layers[leader].definition.id == layer.syncLeaderLayerId)
				{
					leaderIndex = leader;
					break;
				}
			if (leaderIndex == layers.size()
				|| !graphSet.bindings[leaderIndex].definition.enabled)
			{
				error = "Synced Layer '" + layer.name
					+ "' requires an enabled earlier leader in every Graph Set";
				return false;
			}
			if (layer.sync != VansLayerSyncMode::SyncedGraph)
				continue;
			const AnimGraphStateMachineNode* leaderStateMachine =
				FindPrimaryStateMachine(*graphSet.bindings[leaderIndex].graph);
			const AnimGraphStateMachineNode* followerStateMachine =
				FindPrimaryStateMachine(*graphSet.bindings[index].graph);
			if (!leaderStateMachine || !followerStateMachine)
			{
				error = "Synced Graph Layer '" + layer.name
					+ "' requires primary State Machine nodes on both bindings";
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
				error = "Synced Graph Layer '" + layer.name
					+ "' must expose the same logical State names as its leader";
				return false;
			}
		}
	}
	if (!hasDefaultGraphSet)
	{
		error = "Default Graph Set ID does not reference a Graph Set";
		return false;
	}
	std::unordered_set<std::string> rulePairs;
	for (const VansGraphSetTransitionRule& rule : transitionRules)
	{
		if (graphSetIds.find(rule.fromGraphSetId) == graphSetIds.end()
			|| graphSetIds.find(rule.toGraphSetId) == graphSetIds.end()
			|| rule.fromGraphSetId == rule.toGraphSetId
			|| !std::isfinite(rule.policy.duration) || rule.policy.duration < 0.0f
			|| !rulePairs.insert(rule.fromGraphSetId + "\n" + rule.toGraphSetId).second)
		{
			error = "Graph Set transition rules require unique valid source/target pairs and durations";
			return false;
		}
	}

	std::vector<LayerRuntime> layerRuntimes;
	layerRuntimes.reserve(layers.size());
	for (VansAnimationLayerSetup& setup : layers)
	{
		LayerRuntime runtime;
		runtime.definition = std::move(setup.definition);
		runtime.maskAsset = std::move(setup.mask);
		layerRuntimes.push_back(std::move(runtime));
	}

	std::vector<GraphSetRuntime> graphSetRuntimes;
	graphSetRuntimes.reserve(graphSets.size());
	for (VansAnimationGraphSetSetup& setup : graphSets)
	{
		GraphSetRuntime runtime;
		runtime.definition = std::move(setup.definition);
		runtime.bindings.reserve(setup.bindings.size());
		for (VansAnimationGraphBindingSetup& bindingSetup : setup.bindings)
		{
			GraphBindingRuntime binding;
			binding.definition = std::move(bindingSetup.definition);
			binding.graph = std::move(bindingSetup.graph);
			if (binding.graph)
				binding.instance = std::make_unique<VansAnimGraphInstance>(*binding.graph);
			binding.parameterScratch = m_Parameters;
			runtime.bindings.push_back(std::move(binding));
		}
		for (size_t index = 0; index < runtime.bindings.size(); ++index)
		{
			if (!runtime.bindings[index].definition.enabled
				|| layerRuntimes[index].definition.sync == VansLayerSyncMode::Independent)
				continue;
			for (size_t leader = 0; leader < index; ++leader)
			{
				if (layerRuntimes[leader].definition.id
					== layerRuntimes[index].definition.syncLeaderLayerId)
				{
					runtime.bindings[index].syncLeaderIndex = static_cast<int>(leader);
					break;
				}
			}
		}
		graphSetRuntimes.push_back(std::move(runtime));
	}

	m_LayerRuntimes = std::move(layerRuntimes);
	m_GraphSetRuntimes = std::move(graphSetRuntimes);
	m_GraphSetById.clear();
	for (size_t index = 0; index < m_GraphSetRuntimes.size(); ++index)
		m_GraphSetById.emplace(m_GraphSetRuntimes[index].definition.id, index);
	m_ActiveGraphSetIndex = m_GraphSetById.at(defaultGraphSetId);
	m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
	m_DefaultGraphSetTransition = defaultTransition;
	m_GraphSetTransitionRules = std::move(transitionRules);
	m_CurrentGraphSetTransition = {};
	m_GraphSetTransitionElapsed = 0.0f;
	m_QueuedGraphSetId.clear();
	m_IncomingMotionMatching.reset();
	std::string slotResetError;
	m_SlotRuntime.Configure({}, slotResetError);
	m_SlotPayloads.clear();
	if (m_MotionMatching)
	{
		EnsureMotionMatchingGraphNode();
		RebuildGraphSetInstances();
	}
	return true;
}

const VansGraphSetTransitionPolicy& VansAnimationController::ResolveGraphSetTransitionPolicy(
	const std::string& fromGraphSetId,
	const std::string& toGraphSetId) const
{
	for (const VansGraphSetTransitionRule& rule : m_GraphSetTransitionRules)
		if (rule.fromGraphSetId == fromGraphSetId && rule.toGraphSetId == toGraphSetId)
			return rule.policy;
	return m_DefaultGraphSetTransition;
}

VansAnimationController::GraphSetRuntime* VansAnimationController::GetActiveGraphSetRuntime()
{
	return m_ActiveGraphSetIndex < m_GraphSetRuntimes.size()
		? &m_GraphSetRuntimes[m_ActiveGraphSetIndex] : nullptr;
}

const VansAnimationController::GraphSetRuntime* VansAnimationController::GetActiveGraphSetRuntime() const
{
	return m_ActiveGraphSetIndex < m_GraphSetRuntimes.size()
		? &m_GraphSetRuntimes[m_ActiveGraphSetIndex] : nullptr;
}

const std::string& VansAnimationController::GetActiveGraphSetId() const
{
	const GraphSetRuntime* active = GetActiveGraphSetRuntime();
	static const std::string empty;
	return active ? active->definition.id : empty;
}

const std::string& VansAnimationController::GetIncomingGraphSetId() const
{
	static const std::string empty;
	return m_IncomingGraphSetIndex < m_GraphSetRuntimes.size()
		? m_GraphSetRuntimes[m_IncomingGraphSetIndex].definition.id : empty;
}

bool VansAnimationController::IsGraphSetTransitioning() const
{
	return m_IncomingGraphSetIndex < m_GraphSetRuntimes.size();
}

float VansAnimationController::GetGraphSetTransitionProgress() const
{
	if (!IsGraphSetTransitioning())
		return 1.0f;
	if (m_CurrentGraphSetTransition.duration <= 0.0f)
		return 1.0f;
	return std::clamp(
		m_GraphSetTransitionElapsed / m_CurrentGraphSetTransition.duration, 0.0f, 1.0f);
}

bool VansAnimationController::ApplyGraphSetPhaseHandoff(
	const GraphSetRuntime& source,
	GraphSetRuntime& target,
	const VansGraphSetTransitionPolicy& policy)
{
	bool fullyMatched = true;
	for (size_t index = 0; index < target.bindings.size(); ++index)
	{
		GraphBindingRuntime& targetBinding = target.bindings[index];
		if (!targetBinding.definition.enabled || !targetBinding.instance)
			continue;
		if (policy.phase == VansGraphSetPhasePolicy::Restart)
		{
			targetBinding.instance->Reset();
			continue;
		}
		const GraphBindingRuntime& sourceBinding = source.bindings[index];
		if (!sourceBinding.definition.enabled || !sourceBinding.instance)
		{
			targetBinding.instance->Reset();
			fullyMatched = false;
			continue;
		}

		if (targetBinding.instance->SynchronizePrimaryStateMachineFrom(
			*sourceBinding.instance, m_Clips))
		{
			if (policy.phase == VansGraphSetPhasePolicy::MatchMarker
				&& index < source.evaluatedSync.size())
			{
				const std::string sourceClipName = sourceBinding.instance->GetPrimaryClipName();
				const std::string targetClipName = targetBinding.instance->GetPrimaryClipName();
				const auto sourceClip = m_Clips.find(sourceClipName);
				const auto targetClip = m_Clips.find(targetClipName);
				float markerTime = 0.0f;
				if (sourceClip == m_Clips.end() || targetClip == m_Clips.end()
					|| !ResolveMarkerSyncedTime(source.evaluatedSync[index],
						sourceBinding.instance->GetPrimaryPlaybackTime(),
						sourceClip->second.duration, targetClip->second, markerTime)
					|| !targetBinding.instance->SetPrimaryPlaybackTime(markerTime))
				{
					fullyMatched = false;
				}
			}
			continue;
		}

		// 无 State Machine 的 Clip Graph 仍可按归一化时间交接。
		targetBinding.instance->SetPrimaryPlaybackTime(0.0f);
		const auto sourceClip = m_Clips.find(sourceBinding.instance->GetPrimaryClipName());
		const auto targetClip = m_Clips.find(targetBinding.instance->GetPrimaryClipName());
		if (sourceClip == m_Clips.end() || targetClip == m_Clips.end()
			|| sourceClip->second.duration <= 0.0f || targetClip->second.duration <= 0.0f)
		{
			targetBinding.instance->Reset();
			fullyMatched = false;
			continue;
		}
		float targetTime = sourceBinding.instance->GetPrimaryPlaybackTime()
			/ sourceClip->second.duration * targetClip->second.duration;
		if (policy.phase == VansGraphSetPhasePolicy::MatchMarker)
		{
			if (index >= source.evaluatedSync.size()
				|| !ResolveMarkerSyncedTime(source.evaluatedSync[index],
					sourceBinding.instance->GetPrimaryPlaybackTime(),
					sourceClip->second.duration, targetClip->second, targetTime))
			{
				fullyMatched = false;
			}
		}
		if (!targetBinding.instance->SetPrimaryPlaybackTime(targetTime))
			fullyMatched = false;
	}
	return fullyMatched || !policy.requireStateMatch;
}

VansGraphSetSwitchResult VansAnimationController::SwitchGraphSet(
	const std::string& graphSetId)
{
	const auto target = m_GraphSetById.find(graphSetId);
	if (target == m_GraphSetById.end())
		return VansGraphSetSwitchResult::UnknownGraphSet;
	if (!IsGraphSetTransitioning() && target->second == m_ActiveGraphSetIndex)
		return VansGraphSetSwitchResult::AlreadyActive;
	if (IsGraphSetTransitioning())
	{
		if (target->second == m_IncomingGraphSetIndex)
			return VansGraphSetSwitchResult::AlreadyActive;
		switch (m_CurrentGraphSetTransition.interruption)
		{
		case VansGraphSetInterruptionPolicy::QueueLatest:
			m_QueuedGraphSetId = graphSetId;
			return VansGraphSetSwitchResult::Queued;
		case VansGraphSetInterruptionPolicy::Reject:
			return VansGraphSetSwitchResult::Rejected;
		case VansGraphSetInterruptionPolicy::Force:
			if (GetGraphSetTransitionProgress() >= 0.5f)
				CompleteGraphSetTransition();
			else
			{
				m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
				m_IncomingMotionMatching.reset();
				m_GraphSetTransitionElapsed = 0.0f;
			}
			break;
		}
		if (target->second == m_ActiveGraphSetIndex)
		{
			m_QueuedGraphSetId.clear();
			return VansGraphSetSwitchResult::AlreadyActive;
		}
	}

	GraphSetRuntime& source = m_GraphSetRuntimes[m_ActiveGraphSetIndex];
	GraphSetRuntime& incoming = m_GraphSetRuntimes[target->second];
	const VansGraphSetTransitionPolicy policy = ResolveGraphSetTransitionPolicy(
		source.definition.id, incoming.definition.id);
	if (!ApplyGraphSetPhaseHandoff(source, incoming, policy))
		return VansGraphSetSwitchResult::StateHandoffFailed;

	m_CurrentGraphSetTransition = policy;
	m_GraphSetTransitionElapsed = 0.0f;
	m_IncomingGraphSetIndex = target->second;
	m_QueuedGraphSetId.clear();
	if (m_MotionMatching)
		m_IncomingMotionMatching = std::make_unique<VansMotionMatchingRuntime>(*m_MotionMatching);
	if (policy.duration <= 0.0f)
	{
		CompleteGraphSetTransition();
		return VansGraphSetSwitchResult::Completed;
	}
	return VansGraphSetSwitchResult::Started;
}

void VansAnimationController::CompleteGraphSetTransition()
{
	if (!IsGraphSetTransitioning())
		return;
	m_ActiveGraphSetIndex = m_IncomingGraphSetIndex;
	m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
	m_GraphSetTransitionElapsed = 0.0f;
	if (m_IncomingMotionMatching)
		m_MotionMatching = std::move(m_IncomingMotionMatching);
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
	if (!m_AnimationRig)
	{
		error = "Target Post Process Graph requires a compiled Animation Rig";
		return false;
	}
	auto proceduralRuntime = std::make_unique<VansProceduralGraphRuntime>();
	if (!proceduralRuntime->Configure(*graph, *m_AnimationRig, m_QueryProfileResolver, error))
	{
		error = "Target Procedural Graph failed compilation: " + error;
		return false;
	}

	m_TargetPostProcessGraph = std::move(graph);
	m_TargetPostProcessInstance = std::make_unique<VansAnimGraphInstance>(*m_TargetPostProcessGraph);
	m_ProceduralRuntime = std::move(proceduralRuntime);
	return true;
}

bool VansAnimationController::SetAnimationRig(
	VansCompiledAnimationRig rig,
	VansGroundQueryProfileResolver queryProfileResolver,
	std::string& error)
{
	error.clear();
	if (!rig.skeleton || rig.skeleton->bones.empty())
	{
		error = "Animation Controller requires a compiled Rig bound to a Skeleton";
		return false;
	}
	const Skeleton* compiledSkeleton = rig.skeleton;
	if (!rig.BindSkeleton(*compiledSkeleton, error))
		return false;
	m_AnimationRig = std::move(rig);
	m_QueryProfileResolver = std::move(queryProfileResolver);
	if (m_TargetPostProcessGraph)
	{
		auto runtime = std::make_unique<VansProceduralGraphRuntime>();
		if (!runtime->Configure(*m_TargetPostProcessGraph, *m_AnimationRig,
			m_QueryProfileResolver, error)) return false;
		m_ProceduralRuntime = std::move(runtime);
	}
	return true;
}

bool VansAnimationController::BindAnimationRigSkeleton(
	const Skeleton& skeleton,
	std::string& error)
{
	if (!m_AnimationRig)
	{
		error.clear();
		return true;
	}
	return m_AnimationRig->BindSkeleton(skeleton, error);
}

void VansAnimationController::ClearTargetPostProcessGraph()
{
	m_ProceduralRuntime.reset();
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
		const size_t layerIndex = static_cast<size_t>(std::distance(
			m_LayerRuntimes.begin(), std::find_if(m_LayerRuntimes.begin(), m_LayerRuntimes.end(),
				[&](const LayerRuntime& layer) { return layer.definition.id == slot.layerId; })));
		for (const GraphSetRuntime& graphSet : m_GraphSetRuntimes)
		{
			const GraphBindingRuntime& binding = graphSet.bindings[layerIndex];
			if (!binding.definition.enabled)
				continue;
			std::size_t matchingNodeCount = 0;
			for (const auto& [nodeId, node] : binding.graph->GetNodes())
				if (node && node->GetType() == AnimGraphNodeType::Slot
					&& static_cast<const AnimGraphSlotNode*>(node.get())->m_SlotId == slot.id)
					++matchingNodeCount;
			if (matchingNodeCount != 1)
			{
				error = "Slot '" + slot.name + "' requires exactly one matching Slot node in enabled binding '"
					+ binding.definition.graphId + "' of Graph Set '" + graphSet.definition.name + "'";
				return false;
			}
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
	{
		std::string motionMatchingError;
		if (!ConfigureMotionMatching(previous.m_MotionMatching->GetSettings(), motionMatchingError))
		{
			fullyCompatible = false;
			diagnostic = motionMatchingError;
		}
	}
	m_ExternalInput = previous.m_ExternalInput;
	if (m_ProceduralRuntime)
		m_ProceduralRuntime->Reset(m_ExternalInput.resetToken);

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
	}
	for (const LayerRuntime& previousLayer : previous.m_LayerRuntimes)
		if (restoredLayerIds.find(previousLayer.definition.id) == restoredLayerIds.end())
			fullyCompatible = false;

	for (GraphSetRuntime& graphSet : m_GraphSetRuntimes)
	{
		const auto sourceSet = std::find_if(
			previous.m_GraphSetRuntimes.begin(), previous.m_GraphSetRuntimes.end(),
			[&](const GraphSetRuntime& candidate)
			{ return candidate.definition.id == graphSet.definition.id; });
		if (sourceSet == previous.m_GraphSetRuntimes.end())
		{
			fullyCompatible = false;
			continue;
		}
		for (std::size_t index = 0; index < graphSet.bindings.size(); ++index)
		{
			GraphBindingRuntime& binding = graphSet.bindings[index];
			if (!binding.definition.enabled || !binding.instance)
				continue;
			const auto sourceBinding = std::find_if(
				sourceSet->bindings.begin(), sourceSet->bindings.end(),
				[&](const GraphBindingRuntime& candidate)
				{ return candidate.definition.layerId == binding.definition.layerId; });
			if (sourceBinding == sourceSet->bindings.end()
				|| !sourceBinding->definition.enabled || !sourceBinding->instance
				|| sourceBinding->definition.graphId != binding.definition.graphId
				|| !binding.instance->RestoreRuntimeState(
					sourceBinding->instance->CaptureRuntimeState()))
			{
				fullyCompatible = false;
			}
		}
	}
	const auto previousActive = m_GraphSetById.find(previous.GetActiveGraphSetId());
	if (previousActive != m_GraphSetById.end())
		m_ActiveGraphSetIndex = previousActive->second;
	else
		fullyCompatible = false;
	m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
	m_IncomingMotionMatching.reset();
	m_GraphSetTransitionElapsed = 0.0f;
	m_QueuedGraphSetId.clear();

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

bool VansAnimationController::ConfigureMotionMatching(
	const MotionMatchingSettings& settings,
	std::string& error)
{
	error.clear();
	if (settings.motionModel.driveMode != Vans::VansLocomotionDriveMode::Capsule &&
		settings.motionModel.driveMode != Vans::VansLocomotionDriveMode::RootMotion &&
		settings.motionModel.driveMode != Vans::VansLocomotionDriveMode::Hybrid)
	{
		error = "Motion Matching drive mode is invalid";
		return false;
	}
	if (settings.contactProvider.empty() != settings.contactChannels.empty())
	{
		error = "Motion Matching contact provider and channels must either both be authored or both be empty";
		return false;
	}
	std::unordered_set<std::string> contactIds;
	std::unordered_set<int> contactSources;
	for (const MotionMatchingContactChannel& channel : settings.contactChannels)
	{
		if (channel.id.empty() || !contactIds.insert(channel.id).second)
		{
			error = "Motion Matching contact channel IDs must be non-empty and unique";
			return false;
		}
		if (channel.source != MotionMatchingContactSource::LeftFoot &&
			channel.source != MotionMatchingContactSource::RightFoot)
		{
			error = "Motion Matching contact source is invalid";
			return false;
		}
		if (!contactSources.insert(static_cast<int>(channel.source)).second)
		{
			error = "Motion Matching contact sources must be unique";
			return false;
		}
	}
	if (!m_MotionMatching)
		m_MotionMatching = std::make_unique<VansMotionMatchingRuntime>();
	m_MotionMatching->Configure(settings);
	EnsureMotionMatchingGraphNode();
	RebuildGraphSetInstances();
	return true;
}

void VansAnimationController::RebuildGraphSetInstances()
{
	for (GraphSetRuntime& graphSet : m_GraphSetRuntimes)
		for (GraphBindingRuntime& binding : graphSet.bindings)
		{
			if (!binding.graph)
			{
				binding.instance.reset();
				continue;
			}
			binding.instance = std::make_unique<VansAnimGraphInstance>(*binding.graph);
			if (!binding.instance->IsCompiled())
				VANS_LOG_ERROR("[AnimController] Graph Set binding compile failed for '"
					<< binding.definition.graphId << "': " << binding.instance->GetCompileError());
		}
}

const MotionMatchingDebugData* VansAnimationController::GetMotionMatchingDebugData() const
{
	const VansMotionMatchingRuntime* runtime = GetOutputMotionMatchingRuntime();
	return runtime ? &runtime->GetDebugData() : nullptr;
}

const MotionMatchingSettings* VansAnimationController::GetMotionMatchingSettings() const
{
	return m_MotionMatching ? &m_MotionMatching->GetSettings() : nullptr;
}

const std::vector<VansProceduralDebugRecord>*
VansAnimationController::GetProceduralDebugRecords() const
{
	return m_ProceduralRuntime ? &m_ProceduralRuntime->GetDebugRecords() : nullptr;
}

bool VansAnimationController::MotionMatchingPrefersRootMotion() const
{
	const VansMotionMatchingRuntime* runtime = GetOutputMotionMatchingRuntime();
	return runtime && runtime->PrefersRootMotionThisFrame();
}

VansMotionMatchingRuntime* VansAnimationController::GetOutputMotionMatchingRuntime()
{
	if (m_IncomingMotionMatching && GetGraphSetTransitionProgress() >= 0.5f)
		return m_IncomingMotionMatching.get();
	return m_MotionMatching.get();
}

const VansMotionMatchingRuntime* VansAnimationController::GetOutputMotionMatchingRuntime() const
{
	if (m_IncomingMotionMatching && GetGraphSetTransitionProgress() >= 0.5f)
		return m_IncomingMotionMatching.get();
	return m_MotionMatching.get();
}

void VansAnimationController::EnsureMotionMatchingGraphNode()
{
	if (!m_MotionMatching)
		return;
	for (GraphSetRuntime& graphSet : m_GraphSetRuntimes)
	{
		if (graphSet.bindings.empty())
			continue;
		VansAnimGraph* graph = graphSet.bindings.front().graph.get();
		if (!graph)
			continue;
		bool hasMotionMatching = false;
		for (const auto& [id, node] : graph->GetNodes())
			if (node && node->GetType() == AnimGraphNodeType::MotionMatching)
			{
				hasMotionMatching = true;
				break;
			}
		if (hasMotionMatching)
			continue;
		const int outputId = graph->GetOutputNodeId();
		if (outputId < 0)
			continue;

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
			continue;

		auto mmNode = std::make_unique<AnimGraphMotionMatchingNode>();
		const int mmNodeId = graph->AddNode(std::move(mmNode));
		if (mmNodeId < 0)
			continue;

		graph->RemoveLink(outputLinkId);
		graph->AddLink(sourceNodeId, sourcePinIndex, mmNodeId, 0);
		graph->AddLink(mmNodeId, 0, outputId, 0);
	}
}

void VansAnimationController::RefreshExternalMotionState()
{
	if (m_CharacterTrajectory && m_CharacterTrajectory->valid
		&& m_CharacterTrajectory->hasGrounding)
	{
		m_ExternalInput.grounded = m_CharacterTrajectory->grounded;
		m_ExternalInput.airborne = !m_CharacterTrajectory->grounded;
		return;
	}
	if (!m_MotionMatching)
		return;
	const std::string& airborneParameter = m_MotionMatching->GetSettings().parameters.airborne;
	const auto found = m_Parameters.find(airborneParameter);
	if (found == m_Parameters.end())
		return;
	bool airborne = false;
	switch (found->second.type)
	{
	case AnimatorParamType::Bool:
	case AnimatorParamType::Trigger:
		airborne = found->second.boolVal;
		break;
	case AnimatorParamType::Float:
		airborne = found->second.floatVal > 0.5f;
		break;
	case AnimatorParamType::Int:
		airborne = found->second.intVal != 0;
		break;
	default:
		return;
	}
	m_ExternalInput.airborne = airborne;
	m_ExternalInput.grounded = !airborne;
}

void VansAnimationController::PublishMotionMatchingContacts()
{
	VansMotionMatchingRuntime* runtime = GetOutputMotionMatchingRuntime();
	if (!runtime)
		return;
	const MotionMatchingSettings& settings = runtime->GetSettings();
	if (settings.contactProvider.empty())
		return;
	m_ExternalInput.contacts.erase(
		std::remove_if(m_ExternalInput.contacts.begin(), m_ExternalInput.contacts.end(),
			[&settings](const VansContactAttribute& value)
			{
				return value.provider == settings.contactProvider;
			}),
		m_ExternalInput.contacts.end());
	if (!runtime->WasUsedThisFrame())
		return;
	m_ExternalInput.contacts.reserve(
		m_ExternalInput.contacts.size() + settings.contactChannels.size());
	for (const MotionMatchingContactChannel& channel : settings.contactChannels)
	{
		const float weight = channel.source == MotionMatchingContactSource::LeftFoot
			? runtime->GetLeftFootPlantWeight()
			: runtime->GetRightFootPlantWeight();
		VansContactAttribute attribute;
		attribute.provider = settings.contactProvider;
		attribute.id = channel.id;
		attribute.phase = std::clamp(weight, 0.0f, 1.0f);
		// Motion Matching's baked contact curve already combines normalized foot
		// height with velocity confidence. Grounding consumes phase for plant-lock
		// hysteresis and confidence for continuous placement strength.
		attribute.confidence = attribute.phase;
		attribute.present = true;
		m_ExternalInput.contacts.push_back(std::move(attribute));
	}
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
	VansExternalPoseEvaluationMode mode,
	bool prepareWorldQueries)
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
	return FinalizeLocalPose(
		deltaTime, skeleton, std::move(processed), false, false, prepareWorldQueries);
}

// ---------------------------------------------------------------------------
// Clip management.
// ---------------------------------------------------------------------------

void VansAnimationController::AddClip(const std::string& name, VansAnimationClip&& clip)
{
	m_Clips[name] = std::move(clip);
	if (m_MotionMatching)
		m_MotionMatching->MarkDatabaseDirty();
	if (m_IncomingMotionMatching)
		m_IncomingMotionMatching->MarkDatabaseDirty();
}

void VansAnimationController::AddClip(const std::string& name, const VansAnimationClip& clip)
{
	m_Clips[name] = clip;
	if (m_MotionMatching)
		m_MotionMatching->MarkDatabaseDirty();
	if (m_IncomingMotionMatching)
		m_IncomingMotionMatching->MarkDatabaseDirty();
}

void VansAnimationController::RemoveClip(const std::string& name)
{
	m_Clips.erase(name);
	if (m_MotionMatching)
		m_MotionMatching->MarkDatabaseDirty();
	if (m_IncomingMotionMatching)
		m_IncomingMotionMatching->MarkDatabaseDirty();
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
		layer.state = {};
	for (GraphSetRuntime& graphSet : m_GraphSetRuntimes)
		for (GraphBindingRuntime& binding : graphSet.bindings)
			if (binding.instance) binding.instance->Reset();
	m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
	m_IncomingMotionMatching.reset();
	m_GraphSetTransitionElapsed = 0.0f;
	m_QueuedGraphSetId.clear();
	if (m_TargetPostProcessInstance)
		m_TargetPostProcessInstance->Reset();
	++m_ExternalInput.resetToken;
	if (m_ExternalInput.resetToken == 0) ++m_ExternalInput.resetToken;
	if (m_ProceduralRuntime) m_ProceduralRuntime->Reset(m_ExternalInput.resetToken);
}

void VansAnimationController::Play(const std::string& stateName)
{
	bool found = false;
	if (GraphSetRuntime* graphSet = GetActiveGraphSetRuntime())
		for (GraphBindingRuntime& binding : graphSet->bindings)
			if (binding.instance)
				found = binding.instance->PlayState(stateName) || found;
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
		layer.state = {};
	for (GraphSetRuntime& graphSet : m_GraphSetRuntimes)
		for (GraphBindingRuntime& binding : graphSet.bindings)
			if (binding.instance) binding.instance->Reset();
	m_IncomingGraphSetIndex = static_cast<std::size_t>(-1);
	m_IncomingMotionMatching.reset();
	m_GraphSetTransitionElapsed = 0.0f;
	m_QueuedGraphSetId.clear();
	if (m_TargetPostProcessInstance)
		m_TargetPostProcessInstance->Reset();
	++m_ExternalInput.resetToken;
	if (m_ExternalInput.resetToken == 0) ++m_ExternalInput.resetToken;
	if (m_ProceduralRuntime) m_ProceduralRuntime->Reset(m_ExternalInput.resetToken);

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
	const GraphSetRuntime* graphSet = GetActiveGraphSetRuntime();
	if (graphSet && !graphSet->bindings.empty() && graphSet->bindings.front().instance)
		return graphSet->bindings.front().instance->GetCurrentStateName();
	return {};
}

AnimationState VansAnimationController::GetPlaybackState() const
{
	return m_PlaybackState;
}

float VansAnimationController::GetCurrentPlayTime() const
{
	const GraphSetRuntime* graphSet = GetActiveGraphSetRuntime();
	if (graphSet && !graphSet->bindings.empty() && graphSet->bindings.front().instance)
		return graphSet->bindings.front().instance->GetPrimaryPlaybackTime();
	return 0.0f;
}

float VansAnimationController::GetCurrentDuration() const
{
	const GraphSetRuntime* graphSet = GetActiveGraphSetRuntime();
	if (!graphSet || graphSet->bindings.empty() || !graphSet->bindings.front().instance)
		return 0.0f;
	auto it = m_Clips.find(graphSet->bindings.front().instance->GetPrimaryClipName());
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
	GraphSetRuntime* graphSet = GetActiveGraphSetRuntime();
	if (!graphSet)
		return false;
	for (GraphBindingRuntime& binding : graphSet->bindings)
	{
		if (!binding.instance)
			continue;
		const std::string clipName = binding.instance->GetPrimaryClipName();
		const VansAnimationClip* clip = GetClip(clipName);
		if (!clip || clip->duration <= 0.0f)
			continue;
		changed = binding.instance->SetPrimaryPlaybackTime(normalizedTime * clip->duration) || changed;
	}
	return changed;
}

std::vector<VansAnimationController::LayerRuntimeDebugInfo>
VansAnimationController::GetLayerRuntimeDebugInfo() const
{
	std::vector<LayerRuntimeDebugInfo> result;
	result.reserve(m_LayerRuntimes.size());
	const GraphSetRuntime* graphSet = GetActiveGraphSetRuntime();
	for (std::size_t index = 0; index < m_LayerRuntimes.size(); ++index)
	{
		const LayerRuntime& layer = m_LayerRuntimes[index];
		const GraphBindingRuntime* binding = graphSet && index < graphSet->bindings.size()
			? &graphSet->bindings[index] : nullptr;
		LayerRuntimeDebugInfo info;
		info.id = layer.definition.id;
		info.name = layer.definition.name;
		info.weight = layer.definition.kind == VansAnimationLayerKind::Base
			? 1.0f : layer.state.currentWeight;
		info.enabled = binding && binding->definition.enabled;
		info.kind = layer.definition.kind;
		info.blendMode = layer.definition.blendMode;
		info.evaluationMilliseconds = binding ? binding->lastEvaluationMilliseconds : 0.0f;
		if (layer.definition.kind == VansAnimationLayerKind::Base)
			info.boneWeights.assign(layer.compiledMask.weights.size(), 1.0f);
		else
		{
			info.boneWeights.reserve(layer.compiledMask.weights.size());
			for (float maskWeight : layer.compiledMask.weights)
				info.boneWeights.push_back(glm::clamp(maskWeight * info.weight, 0.0f, 1.0f));
		}
		if (binding && binding->instance)
		{
			info.state = binding->instance->GetCurrentStateName();
			info.clip = binding->instance->GetPrimaryClipName();
			info.playbackTime = binding->instance->GetPrimaryPlaybackTime();
			if (const VansAnimationClip* clip = GetClip(info.clip); clip && clip->duration > 0.0f)
				info.normalizedTime = glm::clamp(info.playbackTime / clip->duration, 0.0f, 1.0f);
		}
		result.push_back(std::move(info));
	}
	return result;
}

void VansAnimationController::ResolveLayerReferencePose(
	const LayerRuntime& layer,
	const GraphBindingRuntime& binding,
	const Skeleton& skeleton,
	VansAnimationFrameVector<VansBoneTransform>& outPose) const
{
	VansAnimationLayerMixer::BuildBindPose(skeleton, outPose);
	if (layer.definition.additiveReference == VansAdditiveReferenceMode::BindPose)
		return;

	const std::string* clipName = &layer.definition.referenceClipName;
	if (layer.definition.additiveReference != VansAdditiveReferenceMode::ReferenceClip)
		clipName = binding.instance ? &binding.instance->GetPrimaryClipName() : nullptr;
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

bool VansAnimationController::PrepareLayerStack(
	float deltaTime,
	const Skeleton& skeleton)
{
	if (m_LayerRuntimes.empty())
		return false;
	const std::uint64_t skeletonSignature = VansBoneMaskCompiler::ComputeSkeletonSignature(skeleton);
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
	}
	return true;
}

bool VansAnimationController::EvaluateGraphSet(
	GraphSetRuntime& graphSet,
	VansMotionMatchingRuntime* motionMatching,
	float deltaTime,
	const Skeleton& skeleton,
	VansPosePayload& outPayload)
{
	outPayload = {};
	if (graphSet.bindings.size() != m_LayerRuntimes.size())
		return false;
	VansAnimationFrameVector<VansBoneTransform> bindPose;
	VansAnimationLayerMixer::BuildBindPose(skeleton, bindPose);
	graphSet.evaluatedSync.clear();
	graphSet.evaluatedSync.resize(m_LayerRuntimes.size());
	auto& evaluatedSync = graphSet.evaluatedSync;

	for (size_t layerIndex = 0; layerIndex < m_LayerRuntimes.size(); ++layerIndex)
	{
		LayerRuntime& layer = m_LayerRuntimes[layerIndex];
		GraphBindingRuntime& binding = graphSet.bindings[layerIndex];
		if (!binding.definition.enabled)
		{
			binding.lastEvaluationMilliseconds = 0.0f;
			continue;
		}

		const bool shouldUpdate = layer.definition.updateWhenWeightIsZero
			|| layer.state.currentWeight > 1.0e-6f
			|| layer.definition.kind == VansAnimationLayerKind::Base;
		if (!shouldUpdate)
		{
			binding.lastEvaluationMilliseconds = 0.0f;
			continue;
		}

		if (binding.parameterScratch.size() != m_Parameters.size())
			binding.parameterScratch = m_Parameters;
		else
			for (const auto& [name, parameter] : m_Parameters)
			{
				auto target = binding.parameterScratch.find(name);
				if (target == binding.parameterScratch.end())
				{
					binding.parameterScratch = m_Parameters;
					break;
				}
				target->second = parameter;
			}
		AnimGraphContext context;
		context.deltaTime = deltaTime * m_GlobalSpeed;
		context.skeleton = &skeleton;
		context.parameters = &binding.parameterScratch;
		context.clips = &m_Clips;
		context.motionMatching = motionMatching;
		context.characterTrajectory = m_CharacterTrajectory;
		context.slotPayloads = &m_SlotPayloads;
		context.ownerWorldTransform = m_OwnerWorldTransform;
		if (layer.definition.sync == VansLayerSyncMode::Independent)
		{
			binding.instance->AdvanceTime(context.deltaTime, context);
		}
		else if (layer.definition.sync == VansLayerSyncMode::SyncedGraph)
		{
			const GraphBindingRuntime& leader = graphSet.bindings[
				static_cast<size_t>(binding.syncLeaderIndex)];
			if (!binding.instance->SynchronizePrimaryStateMachineFrom(*leader.instance, m_Clips))
				return false;
			context.synchronizedStateFollower = true;
		}
		else
		{
			const GraphBindingRuntime& leader = graphSet.bindings[
				static_cast<size_t>(binding.syncLeaderIndex)];
			if (binding.instance->GetPrimaryClipName().empty())
				binding.instance->SetPrimaryPlaybackTime(0.0f);

			const std::string& leaderClipName = leader.instance->GetPrimaryClipName();
			const std::string& followerClipName = binding.instance->GetPrimaryClipName();
			auto leaderClip = m_Clips.find(leaderClipName);
			auto followerClip = m_Clips.find(followerClipName);
			if (leaderClip != m_Clips.end() && followerClip != m_Clips.end()
				&& leaderClip->second.duration > 0.0f && followerClip->second.duration > 0.0f)
			{
				const float leaderRawTime = leader.instance->GetPrimaryPlaybackTime();
				float followerTime = leaderRawTime / leaderClip->second.duration
					* followerClip->second.duration;
				if (layer.definition.sync == VansLayerSyncMode::MarkerSync)
					ResolveMarkerSyncedTime(evaluatedSync[static_cast<size_t>(binding.syncLeaderIndex)],
						leaderRawTime, leaderClip->second.duration, followerClip->second, followerTime);
				binding.instance->SetPrimaryPlaybackTime(followerTime);
			}
		}
		const auto evaluationBegin = m_DebugMetricsEnabled
			? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		VansPosePayload sampled = binding.instance->Evaluate(context);
		if (m_DebugMetricsEnabled)
			binding.lastEvaluationMilliseconds = std::chrono::duration<float, std::milli>(
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
		ResolveLayerReferencePose(layer, binding, skeleton, referencePose);
		outPayload = VansAnimationLayerMixer::ApplyLayer(
			outPayload, sampled, layer.definition, layer.compiledMask,
			skeleton, referencePose, layer.state.currentWeight);
	}
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

	m_PreparedWorldQueries.clear();
	m_HasPreparedFrame = false;
	if (m_ProceduralRuntime)
	{
		if (!m_AnimationRig)
			return false;
		if (!VansPoseMath::FromMatrices(localTransforms, pose.localPose))
			return false;
		m_PreparedLocalTransforms = localTransforms;
		m_PreparedProceduralPose.assign(pose.localPose.begin(), pose.localPose.end());
		m_PreparedProceduralNodeIds.assign(
			pose.proceduralNodeIds.begin(), pose.proceduralNodeIds.end());
		m_PreparedDeltaTime = deltaTime;
		m_HasPreparedFrame = true;
		if (deferWorldSpacePostProcess)
			return true;
		GatherPreparedWorldQueries(skeleton);
		if (HasPreparedWorldQueries())
			ResolvePreparedWorldQueries({}, skeleton);
		return true;
	}
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

void VansAnimationController::PrepareFrame(float deltaTime, const Skeleton& skeleton)

{
	UpdateInternal(deltaTime, skeleton, true);
}

bool VansAnimationController::GatherPreparedWorldQueries(const Skeleton& skeleton)
{
	if (!m_HasPreparedFrame || !m_ProceduralRuntime || !m_AnimationRig)
		return false;
	m_AnimationRig->skeleton = &skeleton;
	VansProceduralParameterAccessor accessor;
	accessor.context = &m_Parameters;
	accessor.readFloat = ReadProceduralFloat;
	accessor.readVector3 = ReadProceduralVector3;
	accessor.readQuaternion = ReadProceduralQuaternion;
	m_ExternalInput.ownerWorld = m_OwnerWorldTransform;
	auto& completedPose = m_ProceduralCompletedPoseScratch;
	bool needsResolve = false;
	auto& error = m_ProceduralErrorScratch;
	error.clear();
	if (!m_ProceduralRuntime->Prepare(
		m_PreparedDeltaTime, m_PreparedProceduralPose, m_PreparedProceduralNodeIds,
		accessor, m_ExternalInput, m_PreparedWorldQueries, completedPose, needsResolve, error))
	{
		VANS_LOG_WARN("[AnimController] Target Procedural transaction rejected: " << error);
		m_LocalTransformScratch = m_PreparedLocalTransforms;
		UpdateHierarchy(m_LocalTransformScratch, skeleton);
		BuildFinalMatrices(m_LocalTransformScratch, skeleton);
		m_PreparedWorldQueries.clear();
		m_HasPreparedFrame = false;
		return false;
	}
	if (!needsResolve)
	{
		m_LocalTransformScratch.resize(completedPose.size());
		for (std::size_t index = 0; index < completedPose.size(); ++index)
			m_LocalTransformScratch[index] = VansPoseMath::Compose(completedPose[index]);
		UpdateHierarchy(m_LocalTransformScratch, skeleton);
		BuildFinalMatrices(m_LocalTransformScratch, skeleton);
		m_PreparedLocalTransforms.clear();
		m_PreparedProceduralPose.clear();
		m_PreparedProceduralNodeIds.clear();
		m_HasPreparedFrame = false;
	}
	return true;
}

bool VansAnimationController::ResolvePreparedWorldQueries(
	const std::vector<VansWorldQueryResult>& results,
	const Skeleton& skeleton)

{
	if (!HasPreparedWorldQueries())
		return false;
	m_LocalTransformScratch = m_PreparedLocalTransforms;
	if (m_ProceduralRuntime && m_AnimationRig)
	{
		m_AnimationRig->skeleton = &skeleton;
		auto& completedPose = m_ProceduralCompletedPoseScratch;
		auto& error = m_ProceduralErrorScratch;
		error.clear();
		if (m_ProceduralRuntime->Resolve(results, completedPose, error))
		{
			m_LocalTransformScratch.resize(completedPose.size());
			for (std::size_t index = 0; index < completedPose.size(); ++index)
				m_LocalTransformScratch[index] = VansPoseMath::Compose(completedPose[index]);
		}
		else
		{
			VANS_LOG_WARN("[AnimController] Target Procedural transaction rolled back: " << error);
		}
	}
	UpdateHierarchy(m_LocalTransformScratch, skeleton);
	BuildFinalMatrices(m_LocalTransformScratch, skeleton);
	m_PreparedLocalTransforms.clear();
	m_PreparedProceduralPose.clear();
	m_PreparedProceduralNodeIds.clear();
	m_PreparedWorldQueries.clear();
	m_HasPreparedFrame = false;
	return true;
}

bool VansAnimationController::HasPreparedWorldQueries() const
{
	return m_HasPreparedFrame && m_ProceduralRuntime
		&& m_ProceduralRuntime->HasPreparedQueries();
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
	m_HasPreparedFrame = false;
	m_PreparedWorldQueries.clear();

	if (m_PlaybackState == AnimationState::Stopped || m_PlaybackState == AnimationState::Paused)
		return;
	m_SlotRuntime.Update(deltaTime * m_GlobalSpeed, m_Clips, skeleton, m_SlotPayloads);

	if (GraphSetRuntime* activeGraphSet = GetActiveGraphSetRuntime())
	{
		if (!PrepareLayerStack(deltaTime, skeleton))
			return;
		VansPosePayload pose;
		if (!EvaluateGraphSet(*activeGraphSet, m_MotionMatching.get(), deltaTime, skeleton, pose))
			return;
		if (IsGraphSetTransitioning())
		{
			GraphSetRuntime& incoming = m_GraphSetRuntimes[m_IncomingGraphSetIndex];
			VansPosePayload incomingPose;
			if (!EvaluateGraphSet(
				incoming, m_IncomingMotionMatching.get(), deltaTime, skeleton, incomingPose))
				return;
			m_GraphSetTransitionElapsed += std::max(0.0f, deltaTime);
			const float alpha = EvaluateGraphSetBlendCurve(
				m_CurrentGraphSetTransition.curve, GetGraphSetTransitionProgress());
			pose = BlendGraphSetPayloads(
				pose, incomingPose, alpha, m_CurrentGraphSetTransition);
			if (GetGraphSetTransitionProgress() >= 1.0f)
			{
				const std::string queuedGraphSetId = std::move(m_QueuedGraphSetId);
				CompleteGraphSetTransition();
				m_QueuedGraphSetId.clear();
				if (!queuedGraphSetId.empty()
					&& queuedGraphSetId != GetActiveGraphSetId())
					SwitchGraphSet(queuedGraphSetId);
			}
		}
		for (auto& [name, parameter] : m_Parameters)
			if (parameter.type == AnimatorParamType::Trigger)
				parameter.boolVal = false;
		RefreshExternalMotionState();
		PublishMotionMatchingContacts();
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
	if (globalTransforms.size() != skeleton.bones.size())
	{
		m_CachedLocalTransforms.clear();
		m_CachedGlobalTransforms.clear();
		return;
	}

	// 最终姿态是动画、程序化处理和布娃娃写回之后的统一只读快照。
	m_CachedGlobalTransforms = globalTransforms;
	m_CachedLocalTransforms.resize(globalTransforms.size());
	for (std::size_t boneIndex = 0; boneIndex < globalTransforms.size(); ++boneIndex)
	{
		const int parentIndex = skeleton.bones[boneIndex].parentIndex;
		m_CachedLocalTransforms[boneIndex] = parentIndex >= 0
			&& parentIndex < static_cast<int>(globalTransforms.size())
			? glm::inverse(globalTransforms[static_cast<std::size_t>(parentIndex)])
				* globalTransforms[boneIndex]
			: globalTransforms[boneIndex];
	}
	if (++m_FinalPoseRevision == 0)
		++m_FinalPoseRevision;

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
	const GraphSetRuntime* graphSet = GetActiveGraphSetRuntime();
	if (graphSet && !graphSet->bindings.empty() && graphSet->bindings.front().instance)
	{
		auto clipIt = m_Clips.find(graphSet->bindings.front().instance->GetPrimaryClipName());
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
