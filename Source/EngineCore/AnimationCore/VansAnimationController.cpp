#include "VansAnimationController.h"
#include "VansAnimGraph.h"
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
#include <cstring>
#include <cmath>

using namespace VansGraphics;

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

void VansAnimationController::SetGraph(std::unique_ptr<VansAnimGraph> graph)
{
	m_Graph = std::move(graph);
	EnsureMotionMatchingGraphNode();
}

void VansAnimationController::ConfigureMotionMatching(const MotionMatchingSettings& settings)
{
	if (!m_MotionMatching)
		m_MotionMatching = std::make_unique<VansMotionMatchingRuntime>();
	m_MotionMatching->Configure(settings);
	EnsureMotionMatchingGraphNode();
}

const MotionMatchingDebugData* VansAnimationController::GetMotionMatchingDebugData() const
{
	return m_MotionMatching ? &m_MotionMatching->GetDebugData() : nullptr;
}

void VansAnimationController::EnsureMotionMatchingGraphNode()
{
	if (!m_Graph || !m_MotionMatching)
		return;

	for (const auto& [id, node] : m_Graph->GetNodes())
	{
		if (node && node->GetType() == AnimGraphNodeType::MotionMatching)
			return;
	}

	const int outputId = m_Graph->GetOutputNodeId();
	if (outputId < 0)
		return;

	int sourceNodeId = -1;
	int sourcePinIndex = 0;
	int outputLinkId = -1;
	for (const AnimGraphLink& link : m_Graph->GetLinks())
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
	const int mmNodeId = m_Graph->AddNode(std::move(mmNode));
	if (mmNodeId < 0)
		return;

	m_Graph->RemoveLink(outputLinkId);
	m_Graph->AddLink(sourceNodeId, sourcePinIndex, mmNodeId, 0);
	m_Graph->AddLink(mmNodeId, 0, outputId, 0);
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

void VansAnimationController::FeedExternalBoneWorldTransforms(
	const std::vector<glm::mat4>& modelSpaceTransforms,
	const Skeleton& skeleton)
{
	if (modelSpaceTransforms.size() != skeleton.bones.size())
	{
		VANS_LOG_WARN("[AnimController] FeedExternalBoneWorldTransforms bone count mismatch: input="
			<< modelSpaceTransforms.size() << " skeleton=" << skeleton.bones.size());
		return;
	}

	BuildFinalMatrices(modelSpaceTransforms, skeleton);
}

// ---------------------------------------------------------------------------
// State management.
// ---------------------------------------------------------------------------

void VansAnimationController::AddState(const AnimatorState& state)
{
	m_States[state.name] = state;
}

void VansAnimationController::RemoveState(const std::string& stateName)
{
	m_States.erase(stateName);
}

AnimatorState* VansAnimationController::GetState(const std::string& stateName)
{
	auto it = m_States.find(stateName);
	return (it != m_States.end()) ? &it->second : nullptr;
}

const AnimatorState* VansAnimationController::GetState(const std::string& stateName) const
{
	auto it = m_States.find(stateName);
	return (it != m_States.end()) ? &it->second : nullptr;
}

std::vector<std::string> VansAnimationController::GetStateNames() const
{
	std::vector<std::string> names;
	names.reserve(m_States.size());
	for (const auto& [name, state] : m_States)
		names.push_back(name);
	return names;
}

void VansAnimationController::SetDefaultState(const std::string& stateName)
{
	m_DefaultStateName = stateName;
}

// ---------------------------------------------------------------------------
// Transition management.
// ---------------------------------------------------------------------------

void VansAnimationController::AddTransition(const AnimatorTransition& transition)
{
	m_Transitions.push_back(transition);
}

void VansAnimationController::RemoveTransition(const std::string& fromState, const std::string& toState)
{
	m_Transitions.erase(
		std::remove_if(m_Transitions.begin(), m_Transitions.end(),
			[&](const AnimatorTransition& t)
			{
				return t.fromState == fromState && t.toState == toState;
			}),
		m_Transitions.end());
}

const std::vector<AnimatorTransition>& VansAnimationController::GetTransitions() const
{
	return m_Transitions;
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

void VansAnimationController::BindStateClips()
{
	for (auto& [stateName, state] : m_States)
	{
		auto it = m_Clips.find(state.clipName);
		if (it != m_Clips.end())
			state.clip = &it->second;
		else
			state.clip = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Playback control.
// ---------------------------------------------------------------------------

void VansAnimationController::Play()
{
	if (!m_Graph) return;
	m_PlaybackState = AnimationState::Playing;
	m_Graph->ResetAll();
}

void VansAnimationController::Play(const std::string& stateName)
{
	auto it = m_States.find(stateName);
	if (it == m_States.end())
	{
		VANS_LOG_WARN("[AnimController] " << m_Name << ": state '" << stateName << "' not found");
		return;
	}

	m_CurrentStateName = stateName;
	it->second.currentTime = it->second.startTime;

	m_PlaybackState = AnimationState::Playing;
	m_BlendState    = ControllerBlendState::Idle;
	m_BlendAlpha    = 0.0f;
	m_PrevStateName.clear();

	BindStateClips();
}

void VansAnimationController::Pause()
{
	if (m_PlaybackState == AnimationState::Playing || m_PlaybackState == AnimationState::Blending)
		m_PlaybackState = AnimationState::Paused;
}

void VansAnimationController::Resume()
{
	if (m_PlaybackState == AnimationState::Paused)
		m_PlaybackState = (m_BlendState == ControllerBlendState::Blending)
			? AnimationState::Blending
			: AnimationState::Playing;
}

void VansAnimationController::Stop()
{
	m_PlaybackState = AnimationState::Stopped;
	m_BlendState    = ControllerBlendState::Idle;
	m_BlendAlpha    = 0.0f;
}

void VansAnimationController::Reset()
{
	Stop();
	for (auto& [name, state] : m_States)
		state.currentTime = state.startTime;

	m_RootMotionInitialized = false;
	m_LastRootMotionDelta   = glm::vec3(0.0f);
	m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

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
	return m_CurrentStateName;
}

AnimationState VansAnimationController::GetPlaybackState() const
{
	return m_PlaybackState;
}

float VansAnimationController::GetCurrentPlayTime() const
{
	if (!m_Graph) return 0.0f;
	for (const auto& [id, node] : m_Graph->GetNodes())
	{
		if (node->GetType() == AnimGraphNodeType::Clip)
		{
			return static_cast<const AnimGraphClipNode*>(node.get())->m_CurrentTime;
		}
	}
	return 0.0f;
}

float VansAnimationController::GetCurrentDuration() const
{
	if (!m_Graph) return 0.0f;
	for (const auto& [id, node] : m_Graph->GetNodes())
	{
		if (node->GetType() == AnimGraphNodeType::Clip)
		{
			const std::string& clipName =
				static_cast<const AnimGraphClipNode*>(node.get())->m_ClipName;
			auto it = m_Clips.find(clipName);
			if (it != m_Clips.end()) return it->second.duration;
		}
	}
	return 0.0f;
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
	m_RootMotionInitialized = false;
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

// ---------------------------------------------------------------------------
// Core per-frame update.
// ---------------------------------------------------------------------------

void VansAnimationController::Update(float deltaTime, const Skeleton& skeleton)
{
	m_SampledNodeTransforms.clear();

	if (m_PlaybackState == AnimationState::Stopped || m_PlaybackState == AnimationState::Paused)
		return;

	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());

// ---------------------------------------------------------------------------
	// AnimGraph evaluation path.
// ---------------------------------------------------------------------------
	if (m_Graph)
	{
		// Advance node-local time, scaled by GlobalSpeed.
		m_Graph->AdvanceTime(deltaTime * m_GlobalSpeed);

		// Build evaluation context.
		AnimGraphContext ctx;
		ctx.deltaTime  = deltaTime;
		ctx.skeleton   = &skeleton;
		ctx.parameters = &m_Parameters;
		ctx.clips      = &m_Clips;
		ctx.motionMatching = m_MotionMatching.get();
		ctx.ownerWorldTransform = m_OwnerWorldTransform;

		// Pull the Output node, recursively sampling upstream graph nodes.
		AnimGraphPose pose = m_Graph->Evaluate(ctx);
		if (!pose.valid)
			return;

		m_SampledNodeTransforms = std::move(pose.sampledNodeTransforms);
		if (boneCount == 0)
			return;
		if (pose.localTransforms.size() != boneCount)
			return;

		std::vector<glm::mat4> localTransforms = std::move(pose.localTransforms);

		// Bone overrides, root motion, layers, and final matrices share the skeletal path.
		ApplyBoneOverrides(localTransforms, skeleton);

		if (m_RootBoneIndex < 0)
			m_RootBoneIndex = DetectRootBoneIndex(skeleton);

		if (m_RootMotionEnabled)
		{
			ExtractRootMotion(localTransforms, skeleton);
		}
		else
		{
			m_LoopJustWrapped = false;
			NormalizeRootTransform(localTransforms, skeleton);
		}

		if (pose.hasFootPlacement)
		{
			if (m_FootPlacementSourceNodeId != pose.footPlacementNodeId)
			{
				m_FootPlacementSettings = pose.footPlacementSettings;
				if (!m_FootPlacement)
					m_FootPlacement = std::make_unique<VansFootPlacementSolver>();
				if (!m_FootPlacement->Configure(m_FootPlacementSettings, skeleton))
				{
					VANS_LOG_WARN("FootPlacement node configure failed for controller '" << m_Name << "': missing configured bones");
					m_FootPlacement.reset();
				}
				m_FootPlacementSourceNodeId = pose.footPlacementNodeId;
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
			{
				m_FootPlacement.reset();
			}
		}

		ApplyFootPlacement(deltaTime, skeleton, localTransforms);
		UpdateHierarchy(localTransforms, skeleton);
		BuildFinalMatrices(localTransforms, skeleton);
		return;
	}

	if (m_MotionMatching)
	{
		std::vector<glm::mat4> localTransforms;
		if (m_MotionMatching->Update(deltaTime, skeleton, m_Clips, m_Parameters,
		                             m_OwnerWorldTransform, localTransforms))
		{
			ApplyBoneOverrides(localTransforms, skeleton);

			if (m_RootBoneIndex < 0)
				m_RootBoneIndex = DetectRootBoneIndex(skeleton);

			if (m_RootMotionEnabled)
			{
				ExtractRootMotion(localTransforms, skeleton);
			}
			else
			{
				m_LoopJustWrapped = false;
				NormalizeRootTransform(localTransforms, skeleton);
			}

			ApplyFootPlacement(deltaTime, skeleton, localTransforms);
			UpdateHierarchy(localTransforms, skeleton);
			BuildFinalMatrices(localTransforms, skeleton);
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Internal method: AdvanceStateTime
// ---------------------------------------------------------------------------

void VansAnimationController::AdvanceStateTime(AnimatorState& state, float dt)
{
	if (!state.clip) return;

	float effectiveDuration = (state.endTime < 0.0f) ? state.clip->duration : state.endTime;
	float start = state.startTime;
	float delta = dt * state.speed;

	state.currentTime += delta;

	if (state.currentTime >= effectiveDuration)
	{
		if (state.loop)
		{
			float range = effectiveDuration - start;
			if (range > 0.0f)
				state.currentTime = start + std::fmod(state.currentTime - start, range);
			else
				state.currentTime = start;

			m_LoopJustWrapped = true;
		}
		else
		{
			state.currentTime = effectiveDuration;
		}
	}
}

// ---------------------------------------------------------------------------
// Internal method: EvaluateTransitions
// ---------------------------------------------------------------------------

void VansAnimationController::EvaluateTransitions()
{
	// During blending, only Any State ('*') transitions may interrupt.
	bool currentlyBlending = (m_BlendState == ControllerBlendState::Blending);

	for (const auto& transition : m_Transitions)
	{
		bool isAnyState = (transition.fromState == "*");
		bool isFromCurrent = (transition.fromState == m_CurrentStateName);

		if (!isAnyState && !isFromCurrent)
			continue;

		// An Any State target cannot be the current state; prevents infinite loops.
		if (isAnyState && transition.toState == m_CurrentStateName)
			continue;

		// Non-Any-State transitions do not interrupt an active blend.
		if (currentlyBlending && !isAnyState)
			continue;

		// Check whether the target state exists.
		if (m_States.find(transition.toState) == m_States.end())
			continue;

		// Check exitTime conditions.
		if (transition.hasExitTime)
		{
			AnimatorState* fromState = GetState(isAnyState ? m_CurrentStateName : transition.fromState);
			if (fromState)
			{
				float normalizedTime = GetStateNormalizedTime(*fromState);
				if (normalizedTime < transition.exitTime)
					continue;
			}
		}

		// Check parameter conditions.
		if (!CheckConditions(transition))
			continue;

		// All conditions passed; trigger the transition.
		StartTransition(transition);
		return;
	}
}

bool VansAnimationController::CheckConditions(const AnimatorTransition& t) const
{
	for (const auto& cond : t.conditions)
	{
		auto it = m_Parameters.find(cond.paramName);
		if (it == m_Parameters.end()) return false;

		if (!CompareParam(it->second, cond))
			return false;
	}
	return true;
}

bool VansAnimationController::CompareParam(const AnimatorParameter& param, const TransitionCondition& cond) const
{
	switch (param.type)
	{
	case AnimatorParamType::Float:
		return CompareValue(param.floatVal, cond.op, cond.floatVal);
	case AnimatorParamType::Int:
		return CompareValue(param.intVal, cond.op, cond.intVal);
	case AnimatorParamType::Bool:
		return (cond.op == CompareOp::Equal)
			? (param.boolVal == cond.boolVal)
			: (param.boolVal != cond.boolVal);
	case AnimatorParamType::Trigger:
		return (cond.op == CompareOp::Equal)
			? (param.boolVal == cond.boolVal)
			: (param.boolVal != cond.boolVal);
	}
	return false;
}

template<typename T>
bool VansAnimationController::CompareValue(T a, CompareOp op, T b)
{
	switch (op)
	{
	case CompareOp::Greater:      return a > b;
	case CompareOp::Less:         return a < b;
	case CompareOp::Equal:        return a == b;
	case CompareOp::NotEqual:     return a != b;
	case CompareOp::GreaterEqual: return a >= b;
	case CompareOp::LessEqual:    return a <= b;
	}
	return false;
}

// Explicit template instantiations.
template bool VansAnimationController::CompareValue<float>(float, CompareOp, float);
template bool VansAnimationController::CompareValue<int>(int, CompareOp, int);

void VansAnimationController::ConsumeTriggers(const AnimatorTransition& t)
{
	for (const auto& cond : t.conditions)
	{
		auto it = m_Parameters.find(cond.paramName);
		if (it != m_Parameters.end() && it->second.type == AnimatorParamType::Trigger)
			it->second.boolVal = false;
	}
}

void VansAnimationController::StartTransition(const AnimatorTransition& transition)
{
	m_PrevStateName    = m_CurrentStateName;
	m_CurrentStateName = transition.toState;
	m_BlendAlpha       = 0.0f;
	m_BlendDuration    = transition.blendDuration;
	m_BlendState       = ControllerBlendState::Blending;
	m_PlaybackState    = AnimationState::Blending;

	// Reset the target state's playback time.
	AnimatorState* targetState = GetState(transition.toState);
	if (targetState)
		targetState->currentTime = targetState->startTime;

	ConsumeTriggers(transition);
}

float VansAnimationController::GetStateNormalizedTime(const AnimatorState& state) const
{
	if (!state.clip) return 0.0f;
	float end = (state.endTime < 0.0f) ? state.clip->duration : state.endTime;
	float range = end - state.startTime;
	if (range <= 0.0f) return 0.0f;
	return (state.currentTime - state.startTime) / range;
}

// ---------------------------------------------------------------------------
// Internal method: ComputeBoneTransforms
// ---------------------------------------------------------------------------

void VansAnimationController::ComputeBoneTransforms(const AnimatorState& state,
                                                     const Skeleton& skeleton,
                                                     std::vector<glm::mat4>& outLocalTransforms)
{
	if (!state.clip) return;

	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
	const VansAnimationClip* clip = state.clip;

	for (uint32_t b = 0; b < boneCount; b++)
	{
		if (b >= clip->boneKeyframes.size() || clip->boneKeyframes[b].empty())
		{
			outLocalTransforms[b] = skeleton.bones[b].localTransform;
			continue;
		}

		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scl;
		InterpolateKeyframes(clip->boneKeyframes[b], state.currentTime, pos, rot, scl);

		glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
		glm::mat4 R = glm::toMat4(rot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
		outLocalTransforms[b] = T * R * S;
	}
}

// ---------------------------------------------------------------------------
// Internal method: BlendTransforms
// ---------------------------------------------------------------------------

void VansAnimationController::BlendTransforms(const std::vector<glm::mat4>& a,
                                               const std::vector<glm::mat4>& b,
                                               float alpha,
                                               std::vector<glm::mat4>& outBlended)
{
	uint32_t count = static_cast<uint32_t>((std::min)(a.size(), b.size()));
	outBlended.resize(count);

	for (uint32_t i = 0; i < count; i++)
	{
		glm::vec3 scaleA, posA, skewA;
		glm::quat rotA;
		glm::vec4 perspA;
		glm::decompose(a[i], scaleA, rotA, posA, skewA, perspA);

		glm::vec3 scaleB, posB, skewB;
		glm::quat rotB;
		glm::vec4 perspB;
		glm::decompose(b[i], scaleB, rotB, posB, skewB, perspB);

		glm::vec3 blendedPos   = glm::mix(posA, posB, alpha);
		glm::quat blendedRot   = glm::slerp(rotA, rotB, alpha);
		glm::vec3 blendedScale = glm::mix(scaleA, scaleB, alpha);

		glm::mat4 T = glm::translate(glm::mat4(1.0f), blendedPos);
		glm::mat4 R = glm::toMat4(blendedRot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), blendedScale);
		outBlended[i] = T * R * S;
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

void VansAnimationController::ExtractRootMotion(std::vector<glm::mat4>& localTransforms,
                                                 const Skeleton& skeleton)
{
	if (m_RootBoneIndex < 0 || m_RootBoneIndex >= static_cast<int>(localTransforms.size()))
		return;

	glm::vec3 rootPos, rootScale, skew;
	glm::quat rootRot;
	glm::vec4 perspective;
	glm::decompose(localTransforms[m_RootBoneIndex], rootScale, rootRot, rootPos, skew, perspective);

	m_LastRootMotionDelta   = glm::vec3(0.0f);
	m_LastRootRotationDelta = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	if (!m_RootMotionInitialized)
	{
		m_PrevRootPosition      = rootPos;
		m_PrevRootRotation      = rootRot;
		m_RootMotionInitialized = true;
		m_LoopJustWrapped       = false;

		VANS_LOG("[RootMotion] Init: boneIdx=" << m_RootBoneIndex
		         << " name=\"" << skeleton.bones[m_RootBoneIndex].name
		         << "\" rootPos=(" << rootPos.x << ", " << rootPos.y << ", " << rootPos.z << ")");
	}
	else if (m_LoopJustWrapped)
	{
		m_PrevRootPosition = rootPos;
		m_PrevRootRotation = rootRot;
		m_LoopJustWrapped  = false;

		VANS_LOG("[RootMotion] LoopWrap: reset prevPos=(" << rootPos.x << ", " << rootPos.y << ", " << rootPos.z << ")");
	}
	else
	{
		glm::vec3 deltaPos = rootPos - m_PrevRootPosition;
		glm::quat deltaRot = rootRot * glm::inverse(m_PrevRootRotation);

		m_PrevRootPosition = rootPos;
		m_PrevRootRotation = rootRot;

		m_LastRootMotionDelta   = deltaPos;
		m_LastRootRotationDelta = deltaRot;

		// Diagnostics: print delta for the first 20 frames.
		static int s_DbgFrameCount = 0;
		if (s_DbgFrameCount < 20)
		{
			VANS_LOG("[RootMotion] Frame " << s_DbgFrameCount
			         << " rootPos=(" << rootPos.x << ", " << rootPos.y << ", " << rootPos.z
			         << ") delta=(" << deltaPos.x << ", " << deltaPos.y << ", " << deltaPos.z << ")");
			s_DbgFrameCount++;
		}
	}

	// 将 root bone 的水平位移归零，保留垂直分量。
	glm::vec3 skeletonPos = glm::vec3(0.0f, rootPos.y, 0.0f);
	glm::mat4 T = glm::translate(glm::mat4(1.0f), skeletonPos);
	glm::mat4 R = glm::toMat4(rootRot);
	glm::mat4 S = glm::scale(glm::mat4(1.0f), rootScale);
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

// ---------------------------------------------------------------------------
// Internal method: InterpolateKeyframes
// ---------------------------------------------------------------------------

void VansAnimationController::InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
                                                    float time,
                                                    glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
{
	if (keyframes.empty())
	{
		outPos   = glm::vec3(0.0f);
		outRot   = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		outScale = glm::vec3(1.0f);
		return;
	}

	if (time <= keyframes.front().time || keyframes.size() == 1)
	{
		outPos   = keyframes.front().position;
		outRot   = keyframes.front().rotation;
		outScale = keyframes.front().scale;
		return;
	}

	if (time >= keyframes.back().time)
	{
		outPos   = keyframes.back().position;
		outRot   = keyframes.back().rotation;
		outScale = keyframes.back().scale;
		return;
	}

	int lo = 0;
	int hi = static_cast<int>(keyframes.size()) - 1;
	int nextIdx = hi;

	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (keyframes[mid].time <= time)
			lo = mid + 1;
		else
		{
			nextIdx = mid;
			hi = mid - 1;
		}
	}

	int prevIdx = nextIdx - 1;
	if (prevIdx < 0) prevIdx = 0;

	const BoneKeyframe& kfA = keyframes[prevIdx];
	const BoneKeyframe& kfB = keyframes[nextIdx];

	float segmentDuration = kfB.time - kfA.time;
	float alpha = (segmentDuration > 0.0001f) ? (time - kfA.time) / segmentDuration : 0.0f;
	alpha = glm::clamp(alpha, 0.0f, 1.0f);

	outPos   = glm::mix(kfA.position, kfB.position, alpha);
	outRot   = glm::slerp(kfA.rotation, kfB.rotation, alpha);
	outScale = glm::mix(kfA.scale, kfB.scale, alpha);
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
	const AnimatorState* current = GetState(m_CurrentStateName);
	const VansAnimationClip* clip = current ? current->clip : nullptr;

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
