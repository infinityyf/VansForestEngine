#include "VansAnimationController.h"
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

const std::unordered_map<std::string, AnimatorParameter>& VansAnimationController::GetParameters() const
{
	return m_Parameters;
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
}

void VansAnimationController::AddClip(const std::string& name, const VansAnimationClip& clip)
{
	m_Clips[name] = clip;
}

void VansAnimationController::RemoveClip(const std::string& name)
{
	m_Clips.erase(name);
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
	if (m_DefaultStateName.empty() || m_States.find(m_DefaultStateName) == m_States.end())
	{
		// 没有默认状态，尝试用第一个 state
		if (!m_States.empty())
			m_DefaultStateName = m_States.begin()->first;
		else
			return;
	}

	Play(m_DefaultStateName);
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
	auto it = m_States.find(m_CurrentStateName);
	if (it == m_States.end()) return 0.0f;
	return it->second.currentTime;
}

float VansAnimationController::GetCurrentDuration() const
{
	auto it = m_States.find(m_CurrentStateName);
	if (it == m_States.end()) return 0.0f;

	const AnimatorState& state = it->second;
	if (!state.clip) return 0.0f;

	float end = (state.endTime < 0.0f) ? state.clip->duration : state.endTime;
	return end - state.startTime;
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

	// 1. 求值过渡条件
	EvaluateTransitions();

	// 2. 推进当前状态时间
	AnimatorState* currentState = GetState(m_CurrentStateName);
	if (!currentState || !currentState->clip)
		return;

	// 自动检测 root bone（需要在 clip 确认有效后调用，以便通过关键帧判断运动骨骼）
	if (m_RootBoneIndex < 0)
		m_RootBoneIndex = DetectRootBoneIndex(skeleton);

	AdvanceStateTime(*currentState, deltaTime * m_GlobalSpeed);

	// 如果正在混合，也推进上一个状态的时间
	AnimatorState* prevState = nullptr;
	if (m_BlendState == ControllerBlendState::Blending)
	{
		prevState = GetState(m_PrevStateName);
		if (prevState && prevState->clip)
			AdvanceStateTime(*prevState, deltaTime * m_GlobalSpeed);
	}

	// 3. 计算当前状态的骨骼变换
	std::vector<glm::mat4> localTransforms(boneCount, glm::mat4(1.0f));
	ComputeBoneTransforms(*currentState, skeleton, localTransforms);

	// 4. 如果正在混合
	if (m_BlendState == ControllerBlendState::Blending)
	{
		m_BlendAlpha += deltaTime / m_BlendDuration;

		if (m_BlendAlpha >= 1.0f)
		{
			m_BlendAlpha = 1.0f;
			m_BlendState = ControllerBlendState::Idle;
			m_PlaybackState = AnimationState::Playing;
			m_PrevStateName.clear();
		}
		else if (prevState && prevState->clip)
		{
			std::vector<glm::mat4> prevLocalTransforms(boneCount, glm::mat4(1.0f));
			ComputeBoneTransforms(*prevState, skeleton, prevLocalTransforms);
			BlendTransforms(prevLocalTransforms, localTransforms, m_BlendAlpha, localTransforms);
		}
	}

	// 5. 应用骨骼覆盖 (IK / 程序化动画)
	ApplyBoneOverrides(localTransforms, skeleton);

	// 6. 提取 root motion（或在关闭时清除残留标记）
	if (m_RootMotionEnabled)
		ExtractRootMotion(localTransforms, skeleton);
	else
		m_LoopJustWrapped = false;  // root motion 关闭时也要清除，防止累积

	// 7. 更新骨骼层级（local → global）
	UpdateHierarchy(localTransforms, skeleton);

	// 8. 构建最终矩阵（localTransforms 此时已经是 globalTransforms）
	BuildFinalMatrices(localTransforms, skeleton);
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
			outLocalTransforms[b] = glm::mat4(1.0f);
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

	// 如果 root motion 关闭，也要把 root bone 水平位移归零
	if (!m_RootMotionEnabled && m_RootBoneIndex >= 0 && m_RootBoneIndex < static_cast<int>(boneCount))
	{
		glm::vec3 pos, scale, skew;
		glm::quat rot;
		glm::vec4 perspective;
		glm::decompose(localTransforms[m_RootBoneIndex], scale, rot, pos, skew, perspective);

		glm::vec3 clampedPos = glm::vec3(0.0f, pos.y, 0.0f);
		glm::mat4 T = glm::translate(glm::mat4(1.0f), clampedPos);
		glm::mat4 R = glm::toMat4(rot);
		glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
		localTransforms[m_RootBoneIndex] = T * R * S;
	}

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
	uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
	uint32_t limit = (std::min)(boneCount, MAX_BONES);

	for (uint32_t i = 0; i < limit; i++)
	{
		const BoneInfo& bone = skeleton.bones[i];
		m_BoneMatricesSSBO.boneMatrices[i] =
			skeleton.globalInverseTransform * globalTransforms[i] * bone.offsetMatrix;
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
