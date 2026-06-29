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

// ════════════════════════════════════════════════════════════════
//  Construction & Destruction
// ════════════════════════════════════════════════════════════════

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
}

void VansAnimationController::ConfigureMotionMatching(const MotionMatchingSettings& settings)
{
	if (!m_MotionMatching)
		m_MotionMatching = std::make_unique<VansMotionMatchingRuntime>();
	m_MotionMatching->Configure(settings);
}

const MotionMatchingDebugData* VansAnimationController::GetMotionMatchingDebugData() const
{
	return m_MotionMatching ? &m_MotionMatching->GetDebugData() : nullptr;
}

const FootPlacementDebugData* VansAnimationController::GetFootPlacementDebugData() const
{
	return m_FootPlacement ? &m_FootPlacement->GetDebugData() : nullptr;
}

void VansAnimationController::ConfigureFootPlacement(const FootPlacementSettings& settings, const Skeleton& skeleton)
{
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
	m_FootPlacementSettings.enabled = enabled;
	if (m_FootPlacement)
		m_FootPlacement->SetEnabled(enabled);
}

void VansAnimationController::SetFootPlacementDebugVisualization(bool enabled)
{
	m_FootPlacementSettings.debugVisualization = enabled;
	if (m_FootPlacement)
		m_FootPlacement->SetDebugVisualization(enabled);
}

void VansAnimationController::SetFootPlacementRuntimeState(const FootPlacementRuntimeState& state)
{
	m_FootPlacementState = state;
}

// ════════════════════════════════════════════════════════════════
//  参数管理
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  State 管理
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  Transition 管理
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  Clip 管理
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  播放控制
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  状态查询
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  Root Motion
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  核心更新（每帧调用）
// ════════════════════════════════════════════════════════════════

void VansAnimationController::Update(float deltaTime, const Skeleton& skeleton)
{
	if (m_PlaybackState == AnimationState::Stopped || m_PlaybackState == AnimationState::Paused)
		return;

	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
	if (boneCount == 0)
		return;

	// ════════════════════════════════════════════════════════════
	//  v2 路径: AnimGraph 求值
	// ════════════════════════════════════════════════════════════
	if (m_MotionMatching)
	{
		std::vector<glm::mat4> localTransforms;
		if (m_MotionMatching->Update(deltaTime, skeleton, m_Clips, m_Parameters, localTransforms))
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

	if (m_Graph)
	{
		// 推进节点内部时间（乘以 GlobalSpeed，使速度滑条生效）
		m_Graph->AdvanceTime(deltaTime * m_GlobalSpeed);

		// 构建求值上下文
		AnimGraphContext ctx;
		ctx.deltaTime  = deltaTime;
		ctx.skeleton   = &skeleton;
		ctx.parameters = &m_Parameters;
		ctx.clips      = &m_Clips;

		// pull 求值: Output → 上游节点递归采样
		AnimGraphPose pose = m_Graph->Evaluate(ctx);
		if (!pose.valid || pose.localTransforms.size() != boneCount)
			return;

		std::vector<glm::mat4> localTransforms = std::move(pose.localTransforms);

		// 骨骼覆盖 / Root Motion / 层级 / 最终矩阵 与 v1 共用
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

// ════════════════════════════════════════════════════════════════
//  内部方法: AdvanceStateTime
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  内部方法: EvaluateTransitions
// ════════════════════════════════════════════════════════════════

void VansAnimationController::EvaluateTransitions()
{
	// 正在混合中时，只有 Any State ("*") 过渡可以打断
	bool currentlyBlending = (m_BlendState == ControllerBlendState::Blending);

	for (const auto& transition : m_Transitions)
	{
		bool isAnyState = (transition.fromState == "*");
		bool isFromCurrent = (transition.fromState == m_CurrentStateName);

		if (!isAnyState && !isFromCurrent)
			continue;

		// Any State 过渡的目标不能是当前状态（防止无限循环）
		if (isAnyState && transition.toState == m_CurrentStateName)
			continue;

		// 非 Any State 过渡不打断正在进行的混合
		if (currentlyBlending && !isAnyState)
			continue;

		// 检查目标状态是否存在
		if (m_States.find(transition.toState) == m_States.end())
			continue;

		// 检查 exitTime 条件
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

		// 检查参数条件
		if (!CheckConditions(transition))
			continue;

		// 满足所有条件，触发过渡
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

// 显式实例化模板
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

	// 重置目标状态的播放时间
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

// ════════════════════════════════════════════════════════════════
//  内部方法: ComputeBoneTransforms
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  内部方法: BlendTransforms
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  内部方法: ApplyBoneOverrides
// ════════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════════
//  内部方法: ExtractRootMotion
// ════════════════════════════════════════════════════════════════

void VansAnimationController::ApplyFootPlacement(float deltaTime,
                                                 const Skeleton& skeleton,
                                                 std::vector<glm::mat4>& localTransforms)
{
	if (!m_FootPlacement || !m_FootPlacementSettings.enabled)
		return;

	FootPlacementRuntimeState state = m_FootPlacementState;
	auto airborneIt = m_Parameters.find("IsAirborne");
	if (airborneIt != m_Parameters.end())
	{
		const AnimatorParameter& param = airborneIt->second;
		if (param.type == AnimatorParamType::Bool)
			state.airborne = param.boolVal;
		else if (param.type == AnimatorParamType::Float)
			state.airborne = param.floatVal > 0.5f;
	}

	auto crouchIt = m_Parameters.find("IsCrouching");
	if (crouchIt != m_Parameters.end())
	{
		const AnimatorParameter& param = crouchIt->second;
		if (param.type == AnimatorParamType::Bool)
			state.crouching = param.boolVal;
		else if (param.type == AnimatorParamType::Float)
			state.crouching = param.floatVal > 0.5f;
		else if (param.type == AnimatorParamType::Int)
			state.crouching = param.intVal != 0;
	}
	auto moveStateIt = m_Parameters.find("MoveState");
	if (moveStateIt != m_Parameters.end() &&
	    moveStateIt->second.type == AnimatorParamType::Int &&
	    moveStateIt->second.intVal == 4)
	{
		state.crouching = true;
	}

	if (!m_HasFootPlacementStanceState)
	{
		m_HasFootPlacementStanceState = true;
		m_LastFootPlacementCrouching = state.crouching;
	}
	else if (m_LastFootPlacementCrouching != state.crouching)
	{
		m_LastFootPlacementCrouching = state.crouching;
		m_FootPlacementStanceSuppressTimer = std::max(m_FootPlacementStanceSuppressTimer,
		                                              m_FootPlacementSettings.stanceChangeSuppressionTime);
		m_FootPlacement->ResetTransientState();
	}
	if (m_FootPlacementStanceSuppressTimer > 0.0f)
	{
		m_FootPlacementStanceSuppressTimer = std::max(0.0f, m_FootPlacementStanceSuppressTimer - std::max(deltaTime, 0.0f));
		state.stanceChanging = true;
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
	glm::mat4 R = glm::toMat4(rot);
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

		// 诊断: 前 20 帧输出 delta
		static int s_DbgFrameCount = 0;
		if (s_DbgFrameCount < 20)
		{
			VANS_LOG("[RootMotion] Frame " << s_DbgFrameCount
			         << " rootPos=(" << rootPos.x << ", " << rootPos.y << ", " << rootPos.z
			         << ") delta=(" << deltaPos.x << ", " << deltaPos.y << ", " << deltaPos.z << ")");
			s_DbgFrameCount++;
		}
	}

	// 将 root bone 的水平位移归零，保留垂直分量
	glm::vec3 skeletonPos = glm::vec3(0.0f, rootPos.y, 0.0f);
	glm::mat4 T = glm::translate(glm::mat4(1.0f), skeletonPos);
	glm::mat4 R = glm::toMat4(rootRot);
	glm::mat4 S = glm::scale(glm::mat4(1.0f), rootScale);
	localTransforms[m_RootBoneIndex] = T * R * S;
}

// ════════════════════════════════════════════════════════════════
//  内部方法: UpdateHierarchy
// ════════════════════════════════════════════════════════════════

void VansAnimationController::UpdateHierarchy(std::vector<glm::mat4>& localTransforms,
                                               const Skeleton& skeleton)
{
	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());

	// 按拓扑顺序遍历（父骨骼保证在子骨骼之前），解决 parentIndex > childIndex 时
	// 单遍遍历结果错误的问题
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
		// 回退：若无拓扑顺序则按索引遍历（仅当 parentIndex < childIndex 时正确）
		for (uint32_t b = 0; b < boneCount; b++)
		{
			const BoneInfo& bone = skeleton.bones[b];
			if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(boneCount))
				localTransforms[b] = localTransforms[bone.parentIndex] * localTransforms[b];
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  内部方法: BuildFinalMatrices
// ════════════════════════════════════════════════════════════════

void VansAnimationController::BuildFinalMatrices(const std::vector<glm::mat4>& globalTransforms,
                                                   const Skeleton& skeleton)
{
	// 缓存模型空间全局骨骼矩阵，供骨骼附着点系统读取。
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

// ════════════════════════════════════════════════════════════════
//  内部方法: InterpolateKeyframes
// ════════════════════════════════════════════════════════════════

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
	// 找到骨架根节点（parentIndex == -1）
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

	// 获取当前片段，判断哪些骨骼有动画关键帧
	const AnimatorState* current = GetState(m_CurrentStateName);
	const VansAnimationClip* clip = current ? current->clip : nullptr;

	// 如果骨架根自身就有关键帧位移数据，直接使用
	if (clip && skeletonRoot < static_cast<int>(clip->boneKeyframes.size())
	    && !clip->boneKeyframes[skeletonRoot].empty())
		return skeletonRoot;

	// 否则 BFS 查找第一个带关键帧的子孙骨骼作为 root motion 源
	// （典型情况：场景根 "Adam_Reference" 无动画，运动数据在 "Bip01"）
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

	// 回退：没有找到带关键帧的骨骼，使用骨架根
	return skeletonRoot;
}
