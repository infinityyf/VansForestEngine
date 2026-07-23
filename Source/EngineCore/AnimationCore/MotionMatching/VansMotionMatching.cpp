#include "VansMotionMatching.h"
#include "../../Util/VansLog.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/matrix_decompose.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

using namespace VansGraphics;

namespace
{
	constexpr float kEpsilon = 0.0001f;
	constexpr float kLn2 = 0.6931471805599453f;

	void DecomposeTransform(const glm::mat4& transform,
	                        glm::vec3& position,
	                        glm::quat& rotation,
	                        glm::vec3& scale)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		if (!glm::decompose(transform, scale, rotation, position, skew, perspective))
		{
			position = glm::vec3(transform[3]);
			rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			scale = glm::vec3(1.0f);
		}
		rotation = glm::normalize(rotation);
	}

	glm::vec3 QuaternionLogVector(glm::quat rotation)
	{
		rotation = glm::normalize(rotation);
		if (rotation.w < 0.0f)
			rotation = -rotation;
		const float sinHalf = glm::length(glm::vec3(rotation.x, rotation.y, rotation.z));
		if (sinHalf <= kEpsilon)
			return glm::vec3(0.0f);
		const float angle = 2.0f * std::atan2(sinHalf, glm::clamp(rotation.w, -1.0f, 1.0f));
		return glm::vec3(rotation.x, rotation.y, rotation.z) * (angle / sinHalf);
	}

	glm::quat QuaternionExpVector(const glm::vec3& vector)
	{
		const float angle = glm::length(vector);
		if (angle <= kEpsilon)
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		return glm::angleAxis(angle, vector / angle);
	}

	void DecayCriticalSpring(glm::vec3& value, glm::vec3& velocity, float halfLife, float deltaTime)
	{
		if (deltaTime <= 0.0f)
			return;
		if (halfLife <= kEpsilon)
		{
			value = glm::vec3(0.0f);
			velocity = glm::vec3(0.0f);
			return;
		}
		const float damping = 4.0f * kLn2 / halfLife;
		const glm::vec3 initial = value;
		const glm::vec3 linear = velocity + initial * damping;
		const float decay = std::exp(-damping * deltaTime);
		value = (initial + linear * deltaTime) * decay;
		velocity = (velocity - linear * damping * deltaTime) * decay;
	}

	float SmoothStep(float edge0, float edge1, float value)
	{
		if (edge1 <= edge0 + kEpsilon)
			return value >= edge1 ? 1.0f : 0.0f;
		const float x = glm::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return x * x * (3.0f - 2.0f * x);
	}

	std::string ToLower(std::string value)
	{
		for (char& c : value)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return value;
	}

	bool ContainsToken(const std::string& loweredName, const std::vector<std::string>& tokens)
	{
		for (const std::string& token : tokens)
		{
			if (!token.empty() && loweredName.find(ToLower(token)) != std::string::npos)
				return true;
		}
		return false;
	}

	float NormalizeAngle(float angle)
	{
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kTwoPi = kPi * 2.0f;
		angle = std::fmod(angle + kPi, kTwoPi);
		if (angle < 0.0f)
			angle += kTwoPi;
		return angle - kPi;
	}

	float LerpAngle(float from, float to, float t)
	{
		return from + NormalizeAngle(to - from) * glm::clamp(t, 0.0f, 1.0f);
	}

	bool IsLoopSearchClipName(const std::string& clipName)
	{
		const std::string lowered = ToLower(clipName);
		if (lowered.find("start") != std::string::npos ||
		    lowered.find("stop") != std::string::npos ||
		    lowered.find("transition") != std::string::npos ||
		    lowered.find("towalk") != std::string::npos ||
		    lowered.find("torun") != std::string::npos ||
		    lowered.find("tosprint") != std::string::npos ||
		    lowered.find("tocrouch") != std::string::npos ||
		    lowered.find("tostand") != std::string::npos ||
		    lowered.find("turn") != std::string::npos ||
		    lowered.find("break") != std::string::npos ||
		    lowered.find("jump") != std::string::npos ||
		    lowered.find("land") != std::string::npos ||
		    lowered.find("fall") != std::string::npos)
		{
			return false;
		}
		return lowered.find("idle") != std::string::npos ||
		       lowered.find("walk") != std::string::npos ||
		       lowered.find("run") != std::string::npos ||
		       lowered.find("sprint") != std::string::npos ||
		       lowered.find("crouch") != std::string::npos ||
		       lowered.find("loop") != std::string::npos;
	}

	bool IsTransitionSearchClipName(const std::string& clipName)
	{
		const std::string lowered = ToLower(clipName);
		if (lowered.find("jump") != std::string::npos ||
		    lowered.find("land") != std::string::npos ||
		    lowered.find("fall") != std::string::npos ||
		    lowered.find("break") != std::string::npos)
			return false;
		return lowered.find("start") != std::string::npos ||
		       lowered.find("stop") != std::string::npos ||
		       lowered.find("transition") != std::string::npos ||
		       lowered.find("towalk") != std::string::npos ||
		       lowered.find("torun") != std::string::npos ||
		       lowered.find("tosprint") != std::string::npos ||
		       lowered.find("tocrouch") != std::string::npos ||
		       lowered.find("tostand") != std::string::npos ||
		       lowered.find("turn") != std::string::npos;
	}

	bool IsMotionSearchClipName(const std::string& clipName)
	{
		return IsLoopSearchClipName(clipName) || IsTransitionSearchClipName(clipName);
	}

	int MoveStateFromFamilyName(const std::string& lowered)
	{
		if (lowered.find("crouch") != std::string::npos)
			return 4;
		if (lowered.find("sprint") != std::string::npos)
			return 3;
		if (lowered.find("run") != std::string::npos)
			return 2;
		if (lowered.find("walk") != std::string::npos)
			return 1;
		return 0;
	}

	int TransitionTargetMoveStateFromName(const std::string& lowered)
	{
		if (lowered.find("tocrouch") != std::string::npos)
			return 4;
		if (lowered.find("tostand") != std::string::npos)
			return 0;
		if (lowered.find("towalk") != std::string::npos)
			return 1;
		if (lowered.find("torun") != std::string::npos)
			return 2;
		if (lowered.find("tosprint") != std::string::npos)
			return 3;
		if (lowered.find("stop") != std::string::npos)
			return lowered.find("crouch") != std::string::npos ? 4 : 0;
		return MoveStateFromFamilyName(lowered);
	}

	int TransitionSourceMoveStateFromName(const std::string& lowered)
	{
		const size_t toPos = lowered.find("to");
		if (toPos != std::string::npos)
			return MoveStateFromFamilyName(lowered.substr(0, toPos));
		if (lowered.find("start") != std::string::npos)
			return 0;
		return MoveStateFromFamilyName(lowered);
	}

	int TurnDirectionSignFromName(const std::string& lowered)
	{
		if (lowered.find("turn_l") != std::string::npos ||
		    lowered.find("turnleft") != std::string::npos ||
		    lowered.find("_l_") != std::string::npos)
			return 1;
		if (lowered.find("turn_r") != std::string::npos ||
		    lowered.find("turnright") != std::string::npos ||
		    lowered.find("_r_") != std::string::npos)
			return -1;
		return 0;
	}

	int TurnBucketDeltaFromName(const std::string& lowered)
	{
		if (lowered.find("045") != std::string::npos)
			return 1;
		if (lowered.find("090") != std::string::npos)
			return 2;
		if (lowered.find("135") != std::string::npos)
			return 3;
		if (lowered.find("180") != std::string::npos)
			return 4;
		return 0;
	}

	bool ContainsDirectionToken(const std::string& lowered, const char* token)
	{
		const std::string needle = std::string("_") + token;
		size_t pos = lowered.find(needle);
		while (pos != std::string::npos)
		{
			const size_t end = pos + needle.size();
			if (end == lowered.size() || lowered[end] == '_' || std::isdigit(static_cast<unsigned char>(lowered[end])))
				return true;
			pos = lowered.find(needle, pos + 1);
		}
		return false;
	}

	int DirectionBucketFromName(const std::string& lowered)
	{
		if (ContainsDirectionToken(lowered, "fl")) return 1;
		if (ContainsDirectionToken(lowered, "bl")) return 3;
		if (ContainsDirectionToken(lowered, "br")) return 5;
		if (ContainsDirectionToken(lowered, "fr")) return 7;
		if (ContainsDirectionToken(lowered, "ll") || ContainsDirectionToken(lowered, "l")) return 2;
		if (ContainsDirectionToken(lowered, "rr") || ContainsDirectionToken(lowered, "r")) return 6;
		if (ContainsDirectionToken(lowered, "b")) return 4;
		if (ContainsDirectionToken(lowered, "f")) return 0;
		return -1;
	}

	int SignedBucketDelta(int fromBucket, int toBucket)
	{
		int delta = (toBucket - fromBucket) & 7;
		if (delta > 4)
			delta -= 8;
		return delta;
	}

	float ReadFloatParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                     const char* name,
	                     float fallback = 0.0f)
	{
		auto it = parameters.find(name);
		if (it == parameters.end() || it->second.type != AnimatorParamType::Float)
			return fallback;
		return it->second.floatVal;
	}

	float ReadFloatParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                     const std::string& name,
	                     float fallback = 0.0f)
	{
		return ReadFloatParam(parameters, name.c_str(), fallback);
	}

	int ReadIntParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                 const char* name,
	                 int fallback = 0)
	{
		auto it = parameters.find(name);
		if (it == parameters.end() || it->second.type != AnimatorParamType::Int)
			return fallback;
		return it->second.intVal;
	}

	int ReadIntParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                 const std::string& name,
	                 int fallback = 0)
	{
		return ReadIntParam(parameters, name.c_str(), fallback);
	}

	bool ReadBoolParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                   const char* name,
	                   bool fallback = false)
	{
		auto it = parameters.find(name);
		if (it == parameters.end())
			return fallback;
		if (it->second.type == AnimatorParamType::Bool)
			return it->second.boolVal;
		if (it->second.type == AnimatorParamType::Float)
			return it->second.floatVal > 0.5f;
		if (it->second.type == AnimatorParamType::Int)
			return it->second.intVal != 0;
		return fallback;
	}

	bool ReadBoolParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                   const std::string& name,
	                   bool fallback = false)
	{
		return ReadBoolParam(parameters, name.c_str(), fallback);
	}

	void InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
	                          float time,
	                          glm::vec3& outPos,
	                          glm::quat& outRot,
	                          glm::vec3& outScale)
	{
		if (keyframes.empty())
		{
			outPos = glm::vec3(0.0f);
			outRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			outScale = glm::vec3(1.0f);
			return;
		}

		if (time <= keyframes.front().time || keyframes.size() == 1)
		{
			outPos = keyframes.front().position;
			outRot = keyframes.front().rotation;
			outScale = keyframes.front().scale;
			return;
		}

		if (time >= keyframes.back().time)
		{
			outPos = keyframes.back().position;
			outRot = keyframes.back().rotation;
			outScale = keyframes.back().scale;
			return;
		}

		int lo = 0;
		int hi = static_cast<int>(keyframes.size()) - 1;
		int next = hi;
		while (lo <= hi)
		{
			const int mid = (lo + hi) / 2;
			if (keyframes[mid].time <= time)
				lo = mid + 1;
			else
			{
				next = mid;
				hi = mid - 1;
			}
		}

		const int prev = (std::max)(0, next - 1);
		const BoneKeyframe& a = keyframes[prev];
		const BoneKeyframe& b = keyframes[next];
		const float span = b.time - a.time;
		const float alpha = span > kEpsilon ? glm::clamp((time - a.time) / span, 0.0f, 1.0f) : 0.0f;

		outPos = glm::mix(a.position, b.position, alpha);
		outRot = glm::normalize(glm::slerp(a.rotation, b.rotation, alpha));
		outScale = glm::mix(a.scale, b.scale, alpha);
	}

	glm::vec3 ExtractTranslation(const glm::mat4& m)
	{
		glm::vec3 scale, pos, skew;
		glm::quat rot;
		glm::vec4 perspective;
		glm::decompose(m, scale, rot, pos, skew, perspective);
		return pos;
	}
}

void VansMotionMatchingRuntime::Configure(const MotionMatchingSettings& settings)
{
	m_Settings = settings;
	MarkDatabaseDirty();
	m_DebugData.enabled = settings.enabled;
}

void VansMotionMatchingRuntime::MarkDatabaseDirty()
{
	m_DatabaseDirty = true;
	m_DatabaseReady = false;
	m_DebugData.databaseReady = false;
	m_PreviousQueryModelPose.clear();
	m_LastOutputLocalPose.clear();
	m_PreviousOutputLocalPose.clear();
	m_InertialState.clear();
	m_Blending = false;
	m_CurrentLeftFootVelocity = glm::vec3(0.0f);
	m_CurrentRightFootVelocity = glm::vec3(0.0f);
	m_CurrentPelvisVelocity = glm::vec3(0.0f);
	m_HasQueryVelocity = false;
	m_HasLastSearchContext = false;
	m_DirectionChangedForSearch = false;
	m_CurrentPlaybackRate = 1.0f;
	m_CurrentTrajectoryVelocityRoot = glm::vec3(0.0f);
	m_LeftFootPlantWeight = 0.0f;
	m_RightFootPlantWeight = 0.0f;
	m_LeftContactOffset = 0.0f;
	m_RightContactOffset = 0.0f;
	m_ContactTransitionActive = false;
	m_OwnerTransformInitialized = false;
	m_ActualOwnerVelocityRoot = glm::vec3(0.0f);
	m_HasActualOwnerVelocity = false;
}

bool VansMotionMatchingRuntime::ShouldIncludeClip(const std::string& clipName) const
{
	const std::string lowered = ToLower(clipName);
	if (!m_Settings.includeClipNameTokens.empty() &&
	    !ContainsToken(lowered, m_Settings.includeClipNameTokens))
		return false;
	if (!m_Settings.excludeClipNameTokens.empty() &&
	    ContainsToken(lowered, m_Settings.excludeClipNameTokens))
		return false;
	return true;
}

const MotionMatchingClipMetadata* VansMotionMatchingRuntime::FindClipMetadata(const std::string& clipName) const
{
	const std::string lowered = ToLower(clipName);
	for (const MotionMatchingClipMetadata& metadata : m_Settings.clipMetadata)
	{
		if (!metadata.name.empty() && ToLower(metadata.name) == lowered)
			return &metadata;
		if (!metadata.matchTokens.empty() && ContainsToken(lowered, metadata.matchTokens))
			return &metadata;
	}
	return nullptr;
}

float VansMotionMatchingRuntime::ReadSpeedParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	return ReadFloatParam(parameters, m_Settings.parameters.speed, 0.0f);
}

float VansMotionMatchingRuntime::ReadDirectionParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	return ReadFloatParam(parameters, m_Settings.parameters.direction, 0.0f);
}

bool VansMotionMatchingRuntime::ReadCrouchingParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	return ReadFloatParam(parameters, m_Settings.parameters.crouching, 0.0f) > 0.5f ||
	       ReadBoolParam(parameters, m_Settings.parameters.crouching, false);
}

bool VansMotionMatchingRuntime::ReadAirborneParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	return ReadFloatParam(parameters, m_Settings.parameters.airborne, 0.0f) > 0.5f ||
	       ReadBoolParam(parameters, m_Settings.parameters.airborne, false);
}

int VansMotionMatchingRuntime::ReadMoveStateParam(const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	return ReadIntParam(parameters, m_Settings.parameters.moveState, m_Settings.states.idleState);
}

bool VansMotionMatchingRuntime::IsMovingState(int state) const
{
	return std::find(m_Settings.states.movingStates.begin(), m_Settings.states.movingStates.end(), state) !=
	       m_Settings.states.movingStates.end();
}

bool VansMotionMatchingRuntime::IsMovingPlaybackSample(const Sample& sample) const
{
	// A transition to a moving state is already moving playback. Treating pace
	// transitions as idle makes the next search enter a Start clip instead of the
	// target locomotion loop, adding an entire extra stride behind actor motion.
	return !sample.idleLike && !sample.stopLike && IsMovingState(sample.targetMoveState);
}

bool VansMotionMatchingRuntime::IsPaceTransitionState(int state) const
{
	return std::find(m_Settings.states.paceTransitionStates.begin(), m_Settings.states.paceTransitionStates.end(), state) !=
	       m_Settings.states.paceTransitionStates.end();
}

bool VansMotionMatchingRuntime::IsStanceState(int state) const
{
	return std::find(m_Settings.states.stanceStates.begin(), m_Settings.states.stanceStates.end(), state) !=
	       m_Settings.states.stanceStates.end();
}

int VansMotionMatchingRuntime::ResolveDesiredMoveState(const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	const int moveState = ReadMoveStateParam(parameters);
	const bool wantsIdle = ReadSpeedParam(parameters) < m_Settings.states.idleSpeedThreshold ||
	                       moveState == m_Settings.states.idleState;
	if (ReadCrouchingParam(parameters) || moveState == m_Settings.states.crouchState)
		return m_Settings.states.crouchState;
	return wantsIdle ? m_Settings.states.idleState : moveState;
}

bool VansMotionMatchingRuntime::SearchGroupAllowsSample(
	const Sample& sample,
	const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	if (m_Settings.externallyDriven && sample.transitionLike)
		return false;
	if (m_Settings.searchGroups.empty())
		return true;

	const int moveState = ReadMoveStateParam(parameters);
	const float speed01 = ReadSpeedParam(parameters);
	const bool wantsIdle = speed01 < m_Settings.states.idleSpeedThreshold || moveState == m_Settings.states.idleState;
	const int desiredMoveState = ResolveDesiredMoveState(parameters);
	const bool desiredMoving = !wantsIdle;

	const bool currentValid = m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size());
	const Sample* currentSample = currentValid ? &m_Samples[m_CurrentSample] : nullptr;
	const int currentMoveState = currentSample ? currentSample->targetMoveState : 0;
	const bool currentMoving = currentSample && IsMovingPlaybackSample(*currentSample);
	const bool startingFromIdle = !currentMoving && desiredMoving;
	const bool stoppingToIdle = currentMoving && !desiredMoving;
	const bool changingPace =
		IsPaceTransitionState(currentMoveState) &&
		IsPaceTransitionState(desiredMoveState) &&
		currentMoveState != desiredMoveState;
	const bool changingStance =
		IsStanceState(currentMoveState) &&
		IsStanceState(desiredMoveState) &&
		currentMoveState != desiredMoveState;

	const std::string loweredClip = ToLower(sample.clipName);
	const bool desiredCrouchStance = desiredMoveState == m_Settings.states.crouchState;

	for (const MotionMatchingSearchGroup& group : m_Settings.searchGroups)
	{
		const std::string stance = ToLower(group.stance);
		if (stance == "crouch" && !desiredCrouchStance)
			continue;
		if ((stance == "stand" || stance == "standing") && desiredCrouchStance)
			continue;

		const std::string phase = ToLower(group.phase);
		bool queryPhaseMatches = false;
		if (phase.empty() || phase == "any" || phase == "*")
			queryPhaseMatches = true;
		else if (phase == "idle")
			queryPhaseMatches = !desiredMoving;
		else if (phase == "move" || phase == "moving" || phase == "locomotion")
			queryPhaseMatches = desiredMoving;
		else if (phase == "start")
			queryPhaseMatches = startingFromIdle;
		else if (phase == "stop")
			queryPhaseMatches = stoppingToIdle;
		else if (phase == "transition")
			queryPhaseMatches = changingPace || changingStance;
		else if (phase == "turn")
			queryPhaseMatches = m_DirectionChangedForSearch && currentMoveState == desiredMoveState;
		if (!queryPhaseMatches)
			continue;

		bool samplePhaseMatches = false;
		if (phase.empty() || phase == "any" || phase == "*")
			samplePhaseMatches = true;
		else if (phase == "idle")
			samplePhaseMatches = sample.idleLike;
		else if (phase == "move" || phase == "moving" || phase == "locomotion")
			samplePhaseMatches = sample.loopLike && !sample.idleLike;
		else if (phase == "start")
			samplePhaseMatches = sample.startLike;
		else if (phase == "stop")
			samplePhaseMatches = sample.stopLike;
		else if (phase == "transition")
			samplePhaseMatches = sample.paceTransitionLike;
		else if (phase == "turn")
			samplePhaseMatches = sample.turnLike;
		if (!samplePhaseMatches)
			continue;

		if (!group.moveStates.empty() &&
		    std::find(group.moveStates.begin(), group.moveStates.end(), sample.targetMoveState) == group.moveStates.end())
			continue;
		if (!group.includeClipNameTokens.empty() &&
		    !ContainsToken(loweredClip, group.includeClipNameTokens))
			continue;
		if (!group.excludeClipNameTokens.empty() &&
		    ContainsToken(loweredClip, group.excludeClipNameTokens))
			continue;
		return true;
	}

	return false;
}

int VansMotionMatchingRuntime::ResolveBoneIndex(const Skeleton& skeleton, const std::string& name) const
{
	if (name.empty())
		return -1;
	auto it = skeleton.boneNameToIndex.find(name);
	return it != skeleton.boneNameToIndex.end() ? it->second : -1;
}

MotionMatchingResolvedRig VansMotionMatchingRuntime::ResolveRig(const Skeleton& skeleton)
{
	if (m_Settings.rig.HasExplicitMapping())
	{
		MotionMatchingResolvedRig rig;
		const std::string trajectoryRoot = m_Settings.rig.trajectoryRoot.empty()
			? m_Settings.rig.root
			: m_Settings.rig.trajectoryRoot;
		rig.root = ResolveBoneIndex(skeleton, m_Settings.rig.root);
		rig.trajectoryRoot = ResolveBoneIndex(skeleton, trajectoryRoot);
		rig.pelvis = ResolveBoneIndex(skeleton, m_Settings.rig.pelvis);
		rig.leftFoot = ResolveBoneIndex(skeleton, m_Settings.rig.leftFoot);
		rig.rightFoot = ResolveBoneIndex(skeleton, m_Settings.rig.rightFoot);
		rig.head = ResolveBoneIndex(skeleton, m_Settings.rig.head);
		rig.forwardAxis = m_Settings.rig.forwardAxis;
		m_DebugData.rigStatus = "explicit";
		return rig;
	}

	if (m_Settings.allowLegacyBoneDetection)
	{
		m_DebugData.rigStatus = "legacy fallback";
		VANS_LOG_WARN("[MotionMatching] No explicit rig map configured; using legacy bone detection.");
		return DetectLegacyRig(skeleton);
	}

	m_DebugData.rigStatus = "missing explicit rig";
	return MotionMatchingResolvedRig{};
}

MotionMatchingResolvedRig VansMotionMatchingRuntime::DetectLegacyRig(const Skeleton& skeleton) const
{
	MotionMatchingResolvedRig rig;
	rig.forwardAxis = m_Settings.rig.forwardAxis;
	for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
	{
		const std::string n = ToLower(skeleton.bones[i].name);
		if (rig.root < 0 && (n == "root" || n.find("root") != std::string::npos || n == "bip01"))
			rig.root = i;
		if (rig.pelvis < 0 && (n.find("pelvis") != std::string::npos || n.find("hips") != std::string::npos))
			rig.pelvis = i;
		if (rig.leftFoot < 0 && (n.find("foot_l") != std::string::npos || n.find("l foot") != std::string::npos ||
		                         n.find("l_foot") != std::string::npos || n.find("leftfoot") != std::string::npos))
			rig.leftFoot = i;
		if (rig.rightFoot < 0 && (n.find("foot_r") != std::string::npos || n.find("r foot") != std::string::npos ||
		                          n.find("r_foot") != std::string::npos || n.find("rightfoot") != std::string::npos))
			rig.rightFoot = i;
		if (rig.head < 0 && n.find("head") != std::string::npos)
			rig.head = i;
	}

	if (rig.root < 0)
	{
		for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
		{
			if (skeleton.bones[i].parentIndex < 0)
			{
				rig.root = i;
				break;
			}
		}
	}
	if (rig.pelvis < 0)
		rig.pelvis = rig.root;
	rig.trajectoryRoot = rig.root;
	return rig;
}

bool VansMotionMatchingRuntime::ValidateRig(const MotionMatchingResolvedRig& rig, std::string& outReason) const
{
	if (rig.root < 0) { outReason = "missing root"; return false; }
	if (rig.trajectoryRoot < 0) { outReason = "missing trajectory_root"; return false; }
	if (rig.pelvis < 0) { outReason = "missing pelvis"; return false; }
	if (rig.leftFoot < 0) { outReason = "missing left_foot"; return false; }
	if (rig.rightFoot < 0) { outReason = "missing right_foot"; return false; }
	return true;
}

void VansMotionMatchingRuntime::SamplePose(const VansAnimationClip& clip,
                                           float time,
                                           const Skeleton& skeleton,
                                           std::vector<glm::mat4>& outLocalTransforms) const
{
	const uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
	outLocalTransforms.resize(boneCount);
	for (uint32_t b = 0; b < boneCount; ++b)
	{
		if (b >= clip.boneKeyframes.size() || clip.boneKeyframes[b].empty())
		{
			outLocalTransforms[b] = skeleton.bones[b].localTransform;
			continue;
		}

		glm::vec3 pos;
		glm::quat rot;
		glm::vec3 scale;
		InterpolateKeyframes(clip.boneKeyframes[b], time, pos, rot, scale);
		outLocalTransforms[b] = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), scale);
	}
}

void VansMotionMatchingRuntime::BuildModelSpacePose(const std::vector<glm::mat4>& localTransforms,
                                                    const Skeleton& skeleton,
                                                    std::vector<glm::mat4>& outModelTransforms) const
{
	outModelTransforms = localTransforms;
	const int boneCount = static_cast<int>(skeleton.bones.size());
	if (outModelTransforms.size() != skeleton.bones.size())
		outModelTransforms.resize(skeleton.bones.size(), glm::mat4(1.0f));

	if (!skeleton.topologicalOrder.empty())
	{
		for (int b : skeleton.topologicalOrder)
		{
			if (b < 0 || b >= boneCount)
				continue;
			const BoneInfo& bone = skeleton.bones[b];
			if (bone.parentIndex >= 0 && bone.parentIndex < boneCount)
				outModelTransforms[b] = outModelTransforms[bone.parentIndex] * outModelTransforms[b];
		}
	}
	else
	{
		for (int b = 0; b < boneCount; ++b)
		{
			const BoneInfo& bone = skeleton.bones[b];
			if (bone.parentIndex >= 0 && bone.parentIndex < boneCount)
				outModelTransforms[b] = outModelTransforms[bone.parentIndex] * outModelTransforms[b];
		}
	}
}

glm::vec3 VansMotionMatchingRuntime::TransformPointToRootSpace(const glm::mat4& rootModel, const glm::vec3& point) const
{
	return glm::vec3(glm::inverse(rootModel) * glm::vec4(point, 1.0f));
}

glm::vec3 VansMotionMatchingRuntime::TransformVectorToRootSpace(const glm::mat4& rootModel, const glm::vec3& vector) const
{
	return glm::vec3(glm::inverse(rootModel) * glm::vec4(vector, 0.0f));
}

glm::vec3 VansMotionMatchingRuntime::ExtractRootForward(const glm::mat4& rootModel, const MotionMatchingResolvedRig& rig) const
{
	glm::vec3 forward = glm::vec3(rootModel * glm::vec4(rig.forwardAxis, 0.0f));
	forward.z = 0.0f;
	if (glm::length(glm::vec2(forward.x, forward.y)) <= kEpsilon)
		return glm::vec3(0.0f, 1.0f, 0.0f);
	return glm::normalize(forward);
}

glm::vec3 VansMotionMatchingRuntime::BuildDesiredVelocityRoot(
	const std::unordered_map<std::string, AnimatorParameter>& parameters,
	const MotionMatchingResolvedRig& rig) const
{
	const float speed01 = ReadSpeedParam(parameters);
	const float direction = ReadDirectionParam(parameters);
	const float desiredSpeed = speed01 * m_Settings.desiredSpeedScale;
	glm::vec3 forwardAxis = rig.forwardAxis;
	forwardAxis.z = 0.0f;
	if (glm::length(glm::vec2(forwardAxis.x, forwardAxis.y)) <= kEpsilon)
		forwardAxis = glm::vec3(0.0f, -1.0f, 0.0f);
	else
		forwardAxis = glm::normalize(forwardAxis);
	const glm::vec3 leftAxis(-forwardAxis.y, forwardAxis.x, 0.0f);
	return (leftAxis * std::sin(direction) + forwardAxis * std::cos(direction)) * desiredSpeed;
}

float VansMotionMatchingRuntime::WrapClipTime(const VansAnimationClip& clip, float time) const
{
	if (clip.duration <= kEpsilon)
		return 0.0f;
	float wrapped = std::fmod(time, clip.duration);
	if (wrapped < 0.0f)
		wrapped += clip.duration;
	return wrapped;
}

float VansMotionMatchingRuntime::ResolveClipTime(const VansAnimationClip& clip, float time, bool loopLike) const
{
	if (loopLike)
		return WrapClipTime(clip, time);
	return glm::clamp(time, 0.0f, (std::max)(0.0f, clip.duration));
}

void VansMotionMatchingRuntime::WriteVec3(FeatureVector& feature, int& offset, const glm::vec3& value) const
{
	feature[offset++] = value.x;
	feature[offset++] = value.y;
	feature[offset++] = value.z;
}

bool VansMotionMatchingRuntime::SampleContactWeights(int sampleIndex,
	                                                  float time,
	                                                  float& outLeft,
	                                                  float& outRight) const
{
	outLeft = 0.0f;
	outRight = 0.0f;
	if (sampleIndex < 0 || sampleIndex >= static_cast<int>(m_Samples.size()))
		return false;
	const Sample& active = m_Samples[sampleIndex];
	const auto sampleIt = m_ClipSampleIndices.find(active.clipName);
	if (sampleIt == m_ClipSampleIndices.end() || sampleIt->second.empty())
		return false;

	const std::vector<int>& clipSamples = sampleIt->second;
	const float samplePosition = (std::max)(0.0f, time * m_Settings.sampleRate);
	const size_t lower = (std::min)(clipSamples.size() - 1,
		static_cast<size_t>(std::floor(samplePosition)));
	size_t upper = lower + 1;
	if (upper >= clipSamples.size())
		upper = active.loopLike ? 0 : lower;
	const float alpha = upper == lower ? 0.0f : samplePosition - std::floor(samplePosition);
	outLeft = glm::clamp(glm::mix(
		m_Samples[clipSamples[lower]].rawFeature[kContactBegin],
		m_Samples[clipSamples[upper]].rawFeature[kContactBegin], alpha), 0.0f, 1.0f);
	outRight = glm::clamp(glm::mix(
		m_Samples[clipSamples[lower]].rawFeature[kContactBegin + 1],
		m_Samples[clipSamples[upper]].rawFeature[kContactBegin + 1], alpha), 0.0f, 1.0f);
	return true;
}

void VansMotionMatchingRuntime::AdvanceContactWeights(float deltaTime,
	                                                   float targetLeft,
	                                                   float targetRight)
{
	if (m_ContactTransitionActive)
	{
		const float halfLife = (std::max)(m_Settings.inertializationHalfLife, 0.001f);
		const float decay = std::exp(-0.69314718056f * (std::max)(deltaTime, 0.0f) / halfLife);
		m_LeftContactOffset *= decay;
		m_RightContactOffset *= decay;
		if ((std::max)(std::abs(m_LeftContactOffset), std::abs(m_RightContactOffset)) < 0.001f)
		{
			m_LeftContactOffset = 0.0f;
			m_RightContactOffset = 0.0f;
			m_ContactTransitionActive = false;
		}
	}
	m_LeftFootPlantWeight = glm::clamp(targetLeft + m_LeftContactOffset, 0.0f, 1.0f);
	m_RightFootPlantWeight = glm::clamp(targetRight + m_RightContactOffset, 0.0f, 1.0f);
}

void VansMotionMatchingRuntime::BeginContactTransition(float sourceLeft,
	                                                    float sourceRight,
	                                                    float targetLeft,
	                                                    float targetRight)
{
	m_LeftContactOffset = glm::clamp(sourceLeft, 0.0f, 1.0f) - glm::clamp(targetLeft, 0.0f, 1.0f);
	m_RightContactOffset = glm::clamp(sourceRight, 0.0f, 1.0f) - glm::clamp(targetRight, 0.0f, 1.0f);
	m_ContactTransitionActive = std::abs(m_LeftContactOffset) > 0.001f ||
	                            std::abs(m_RightContactOffset) > 0.001f;
	m_LeftFootPlantWeight = glm::clamp(targetLeft + m_LeftContactOffset, 0.0f, 1.0f);
	m_RightFootPlantWeight = glm::clamp(targetRight + m_RightContactOffset, 0.0f, 1.0f);
}

void VansMotionMatchingRuntime::UpdateActualOwnerVelocity(float deltaTime,
	                                                       const glm::mat4& ownerWorldTransform)
{
	const glm::vec3 ownerWorldPosition = ExtractTranslation(ownerWorldTransform);
	m_HasActualOwnerVelocity = false;
	if (m_OwnerTransformInitialized && deltaTime > kEpsilon)
	{
		const glm::vec3 worldVelocity = (ownerWorldPosition - m_PreviousOwnerWorldPosition) / deltaTime;
		const glm::vec3 rootVelocity = glm::vec3(glm::inverse(ownerWorldTransform) * glm::vec4(worldVelocity, 0.0f));
		const float sanityLimit = (std::max)(m_Settings.desiredSpeedScale * 4.0f, 100.0f);
		if (std::isfinite(rootVelocity.x) && std::isfinite(rootVelocity.y) && std::isfinite(rootVelocity.z) &&
		    glm::length(rootVelocity) <= sanityLimit)
		{
			m_ActualOwnerVelocityRoot = rootVelocity;
			m_HasActualOwnerVelocity = true;
		}
	}
	m_PreviousOwnerWorldPosition = ownerWorldPosition;
	m_OwnerTransformInitialized = true;
}

VansMotionMatchingRuntime::FeatureVector VansMotionMatchingRuntime::ExtractDatabaseFeature(
	const VansAnimationClip& clip,
	float time,
	bool loopLike,
	const Skeleton& skeleton,
	const MotionMatchingResolvedRig& rig) const
{
	FeatureVector f{};
	std::vector<glm::mat4> local0;
	std::vector<glm::mat4> model0;
	SamplePose(clip, ResolveClipTime(clip, time, loopLike), skeleton, local0);
	BuildModelSpacePose(local0, skeleton, model0);

	glm::vec3 loopCycleDelta(0.0f);
	if (loopLike && clip.duration > kEpsilon)
	{
		std::vector<glm::mat4> localStart, modelStart;
		std::vector<glm::mat4> localEnd, modelEnd;
		SamplePose(clip, 0.0f, skeleton, localStart);
		SamplePose(clip, clip.duration, skeleton, localEnd);
		BuildModelSpacePose(localStart, skeleton, modelStart);
		BuildModelSpacePose(localEnd, skeleton, modelEnd);
		loopCycleDelta = ExtractTranslation(modelEnd[rig.trajectoryRoot]) -
		                 ExtractTranslation(modelStart[rig.trajectoryRoot]);
	}

	auto sampleUnwrappedModel = [&](float absoluteTime,
	                                std::vector<glm::mat4>& outLocal,
	                                std::vector<glm::mat4>& outModel)
	{
		float sampleTime = ResolveClipTime(clip, absoluteTime, loopLike);
		int cycle = 0;
		if (loopLike && clip.duration > kEpsilon)
		{
			const float cycleF = std::floor(absoluteTime / clip.duration);
			cycle = static_cast<int>(cycleF);
			sampleTime = absoluteTime - cycleF * clip.duration;
			if (sampleTime < 0.0f)
			{
				sampleTime += clip.duration;
				--cycle;
			}
			if (sampleTime >= clip.duration)
				sampleTime = 0.0f;
		}

		SamplePose(clip, sampleTime, skeleton, outLocal);
		BuildModelSpacePose(outLocal, skeleton, outModel);
		if (cycle != 0)
		{
			const glm::vec3 offset = loopCycleDelta * static_cast<float>(cycle);
			for (glm::mat4& model : outModel)
				model[3] += glm::vec4(offset, 0.0f);
		}
	};

	const glm::mat4 rootModel0 = model0[rig.root];
	const glm::vec3 trajectoryRoot0 = ExtractTranslation(model0[rig.trajectoryRoot]);
	int offset = 0;

	for (float futureTime : m_Settings.schema.futureTimes)
	{
		std::vector<glm::mat4> localFuture;
		std::vector<glm::mat4> modelFuture;
		sampleUnwrappedModel(time + futureTime, localFuture, modelFuture);

		const glm::vec3 futureRoot = ExtractTranslation(modelFuture[rig.trajectoryRoot]);
		const glm::vec3 deltaRoot = TransformVectorToRootSpace(rootModel0, futureRoot - trajectoryRoot0);
		f[offset++] = deltaRoot.x;
		f[offset++] = deltaRoot.y;
	}

	for (float futureTime : m_Settings.schema.futureTimes)
	{
		std::vector<glm::mat4> localFuture;
		std::vector<glm::mat4> modelFuture;
		sampleUnwrappedModel(time + futureTime, localFuture, modelFuture);

		const glm::vec3 futureRoot = ExtractTranslation(modelFuture[rig.trajectoryRoot]);
		const glm::vec3 deltaRoot = TransformVectorToRootSpace(rootModel0, futureRoot - trajectoryRoot0);
		const glm::vec2 deltaXY(deltaRoot.x, deltaRoot.y);
		float facing = 0.0f;
		if (glm::length(deltaXY) > kEpsilon)
			facing = std::atan2(deltaRoot.x, deltaRoot.y);
		else
		{
			const glm::vec3 forward = ExtractRootForward(rootModel0, rig);
			facing = std::atan2(forward.x, forward.y);
		}
		f[offset++] = std::sin(facing);
		f[offset++] = std::cos(facing);
	}

	const float velocityDt = 0.10f;
	std::vector<glm::mat4> localPrev, modelPrev;
	std::vector<glm::mat4> localNext, modelNext;
	sampleUnwrappedModel(time - velocityDt, localPrev, modelPrev);
	sampleUnwrappedModel(time + velocityDt, localNext, modelNext);

	const glm::vec3 pelvis0 = ExtractTranslation(model0[rig.pelvis]);
	const glm::vec3 leftFoot0 = ExtractTranslation(model0[rig.leftFoot]);
	const glm::vec3 rightFoot0 = ExtractTranslation(model0[rig.rightFoot]);
	const glm::vec3 head0 = rig.head >= 0 ? ExtractTranslation(model0[rig.head]) : pelvis0;

	const glm::vec3 leftFootRel = TransformPointToRootSpace(rootModel0, leftFoot0);
	const glm::vec3 rightFootRel = TransformPointToRootSpace(rootModel0, rightFoot0);
	const glm::vec3 leftFootVel = TransformVectorToRootSpace(rootModel0,
		(ExtractTranslation(modelNext[rig.leftFoot]) - ExtractTranslation(modelPrev[rig.leftFoot])) / (2.0f * velocityDt));
	const glm::vec3 rightFootVel = TransformVectorToRootSpace(rootModel0,
		(ExtractTranslation(modelNext[rig.rightFoot]) - ExtractTranslation(modelPrev[rig.rightFoot])) / (2.0f * velocityDt));
	const glm::vec3 pelvisVel = TransformVectorToRootSpace(rootModel0,
		(ExtractTranslation(modelNext[rig.pelvis]) - ExtractTranslation(modelPrev[rig.pelvis])) / (2.0f * velocityDt));
	const float legScale = (std::max)(
		0.01f,
		0.5f * (glm::distance(pelvis0, leftFoot0) + glm::distance(pelvis0, rightFoot0)));
	const float contactReleaseSpeed = legScale * 0.65f;
	const float contactPlantSpeed = legScale * 0.15f;

	WriteVec3(f, offset, leftFootRel);
	WriteVec3(f, offset, rightFootRel);
	WriteVec3(f, offset, leftFootVel);
	WriteVec3(f, offset, rightFootVel);
	WriteVec3(f, offset, pelvisVel);
	f[offset++] = head0.z - pelvis0.z;
	f[offset++] = 1.0f - SmoothStep(contactPlantSpeed, contactReleaseSpeed, glm::length(leftFootVel));
	f[offset++] = 1.0f - SmoothStep(contactPlantSpeed, contactReleaseSpeed, glm::length(rightFootVel));
	return f;
}

void VansMotionMatchingRuntime::BuildFootContactPhases(const std::vector<int>& clipSampleIndices)
{
	if (clipSampleIndices.empty())
		return;

	const Sample& firstSample = m_Samples[clipSampleIndices.front()];
	if (firstSample.idleLike)
	{
		// Idle clips are a stable two-foot support pose.  Treating tiny mocap
		// noise as a gait phase makes one foot randomly unlock while standing.
		for (const int sampleIndex : clipSampleIndices)
		{
			Sample& sample = m_Samples[sampleIndex];
			sample.rawFeature[kContactBegin] = 1.0f;
			sample.rawFeature[kContactBegin + 1] = 1.0f;
			sample.feature = sample.rawFeature;
		}
		return;
	}

	const float fullFraction = glm::clamp(m_Settings.contactHeightFullFraction, 0.0f, 0.95f);
	const float fadeFraction = glm::clamp(
		(std::max)(m_Settings.contactHeightFadeFraction, fullFraction + 0.01f),
		fullFraction + 0.01f,
		1.0f);
	const float velocityFloor = glm::clamp(m_Settings.contactVelocityConfidenceFloor, 0.55f, 1.0f);

	const auto buildFootPhase = [&](int heightChannel, int contactChannel)
	{
		float minHeight = std::numeric_limits<float>::max();
		float maxHeight = std::numeric_limits<float>::lowest();
		for (const int sampleIndex : clipSampleIndices)
		{
			const float height = m_Samples[sampleIndex].rawFeature[heightChannel];
			minHeight = (std::min)(minHeight, height);
			maxHeight = (std::max)(maxHeight, height);
		}

		const float heightSpan = (std::max)(maxHeight - minHeight, kEpsilon);
		const float fullHeight = minHeight + heightSpan * fullFraction;
		const float fadeHeight = minHeight + heightSpan * fadeFraction;
		for (const int sampleIndex : clipSampleIndices)
		{
			Sample& sample = m_Samples[sampleIndex];
			const float heightWeight = 1.0f - SmoothStep(
				fullHeight, fadeHeight, sample.rawFeature[heightChannel]);
			const float velocityConfidence = glm::clamp(
				sample.rawFeature[contactChannel], 0.0f, 1.0f);
			// Height defines the gait phase.  Velocity can strengthen confidence,
			// but cannot erase an otherwise valid low-foot contact.
			sample.rawFeature[contactChannel] = heightWeight * glm::mix(
				velocityFloor, 1.0f, velocityConfidence);
			sample.feature = sample.rawFeature;
		}
	};

	buildFootPhase(kPoseBegin + 2, kContactBegin);
	buildFootPhase(kPoseBegin + 5, kContactBegin + 1);
}

VansMotionMatchingRuntime::FeatureVector VansMotionMatchingRuntime::BuildQueryFeature(
	const std::unordered_map<std::string, AnimatorParameter>& parameters,
	const std::vector<glm::mat4>& currentLocalPose,
	const Skeleton& skeleton,
	const MotionMatchingResolvedRig& rig) const
{
	FeatureVector f{};
	std::vector<glm::mat4> currentModel;
	BuildModelSpacePose(currentLocalPose, skeleton, currentModel);

	const glm::mat4 rootModel = currentModel[rig.root];
	const float speed01 = ReadSpeedParam(parameters);
	const float direction = ReadDirectionParam(parameters);
	const bool airborne = ReadAirborneParam(parameters);
	const int moveState = ReadMoveStateParam(parameters);
	const glm::vec3 desiredVelRoot = BuildDesiredVelocityRoot(parameters, rig);
	const glm::vec2 desiredVelXY(desiredVelRoot.x, desiredVelRoot.y);
	const glm::vec3 currentForward = ExtractRootForward(rootModel, rig);
	const float currentFacing = std::atan2(currentForward.x, currentForward.y);
	const float desiredFacing = glm::length(desiredVelXY) > kEpsilon
		? std::atan2(desiredVelRoot.x, desiredVelRoot.y)
		: currentFacing;
	const float blendHorizon = (std::max)(m_Settings.schema.futureTimes.back(), kEpsilon);
	const float response = (std::max)(m_Settings.trajectoryResponsiveness, kEpsilon);
	const glm::vec3 currentTrajectoryVelocity = m_CurrentTrajectoryVelocityRoot;

	int offset = 0;
	for (float futureTime : m_Settings.schema.futureTimes)
	{
		// Integrate a velocity that exponentially converges from the currently
		// playing motion to the desired controller velocity.  Linear extrapolation
		// produces impossible starts/stops and is a common source of MM jitter.
		const float responseIntegral = (1.0f - std::exp(-response * futureTime)) / response;
		const glm::vec3 deltaRoot = desiredVelRoot * futureTime +
			(currentTrajectoryVelocity - desiredVelRoot) * responseIntegral;
		f[offset++] = deltaRoot.x;
		f[offset++] = deltaRoot.y;
	}
	for (float futureTime : m_Settings.schema.futureTimes)
	{
		const float t = futureTime / blendHorizon;
		const float facing = LerpAngle(currentFacing, desiredFacing, t);
		f[offset++] = std::sin(facing);
		f[offset++] = std::cos(facing);
	}

	WriteVec3(f, offset, TransformPointToRootSpace(rootModel, ExtractTranslation(currentModel[rig.leftFoot])));
	WriteVec3(f, offset, TransformPointToRootSpace(rootModel, ExtractTranslation(currentModel[rig.rightFoot])));

	glm::vec3 leftVelocity = m_CurrentLeftFootVelocity;
	glm::vec3 rightVelocity = m_CurrentRightFootVelocity;
	if (airborne || moveState == 5)
	{
		leftVelocity += desiredVelRoot;
		rightVelocity += desiredVelRoot;
	}
	WriteVec3(f, offset, leftVelocity);
	WriteVec3(f, offset, rightVelocity);
	const glm::vec3 pelvisVelocity = m_HasQueryVelocity ? m_CurrentPelvisVelocity : desiredVelRoot;
	WriteVec3(f, offset, pelvisVelocity);

	const glm::vec3 pelvis = ExtractTranslation(currentModel[rig.pelvis]);
	const glm::vec3 head = rig.head >= 0 ? ExtractTranslation(currentModel[rig.head]) : pelvis;
	f[offset++] = head.z - pelvis.z;
	// The current database phase is authoritative during inertialization.  A
	// finite-difference contact test here would see the intentional pose offset
	// decay as foot motion and destabilize the next search.
	f[offset++] = m_LeftFootPlantWeight;
	f[offset++] = m_RightFootPlantWeight;
	return f;
}

bool VansMotionMatchingRuntime::BuildDatabase(const std::unordered_map<std::string, VansAnimationClip>& clips,
                                              const Skeleton& skeleton)
{
	m_Samples.clear();
	m_ClipSampleIndices.clear();
	m_DebugData.topCandidates.clear();
	m_DebugData.databaseReady = false;
	m_DebugData.rigReady = false;
	if (clips.empty() || skeleton.bones.empty())
		return false;

	m_Rig = ResolveRig(skeleton);
	std::string rigReason;
	if (!ValidateRig(m_Rig, rigReason))
	{
		m_DebugData.rigStatus = "Rig error: " + rigReason;
		VANS_LOG_WARN("[MotionMatching] Cannot build database: " << rigReason);
		return false;
	}
	if (m_Rig.head < 0)
		VANS_LOG_WARN("[MotionMatching] Head bone not found; height feature will be 0.");
	m_DebugData.rigReady = true;

	int includedClipCount = 0;
	const float sampleStep = 1.0f / (std::max)(1.0f, m_Settings.sampleRate);
	for (const auto& [name, clip] : clips)
	{
		if (!ShouldIncludeClip(name) || clip.duration <= kEpsilon)
			continue;
		const MotionMatchingClipMetadata* metadata = FindClipMetadata(name);
		if (!metadata && !IsMotionSearchClipName(name))
			continue;

		++includedClipCount;
		const std::string lowered = ToLower(name);
		const bool legacyLoopLike = IsLoopSearchClipName(name);
		const bool loopLike = metadata && metadata->hasLoopLike ? metadata->loopLike : legacyLoopLike;
		for (float t = 0.0f; t < clip.duration; t += sampleStep)
		{
			Sample sample;
			sample.clipName = name;
			sample.time = t;
			sample.rawFeature = ExtractDatabaseFeature(clip, t, loopLike, skeleton, m_Rig);
			sample.feature = sample.rawFeature;
			const float speedHorizon = m_Settings.schema.futureTimes[0];
			if (speedHorizon > kEpsilon)
				sample.trajectorySpeed = glm::length(glm::vec2(sample.rawFeature[0], sample.rawFeature[1])) / speedHorizon;
			sample.loopLike = loopLike;
			sample.idleLike = metadata && metadata->hasIdleLike
				? metadata->idleLike
				: sample.loopLike && lowered.find("idle") != std::string::npos;
			sample.transitionLike = metadata && metadata->hasTransitionLike
				? metadata->transitionLike
				: !loopLike;
			sample.startLike = metadata && metadata->hasStartLike
				? metadata->startLike
				: sample.transitionLike && lowered.find("start") != std::string::npos;
			sample.stopLike = metadata && metadata->hasStopLike
				? metadata->stopLike
				: sample.transitionLike && lowered.find("stop") != std::string::npos;
			sample.turnLike = metadata && metadata->hasTurnLike
				? metadata->turnLike
				: sample.transitionLike && lowered.find("turn") != std::string::npos;
			sample.turnDirectionSign = metadata && metadata->hasTurnDirectionSign
				? metadata->turnDirectionSign
				: (sample.turnLike ? TurnDirectionSignFromName(lowered) : 0);
			sample.turnBucketDelta = metadata && metadata->hasTurnBucketDelta
				? metadata->turnBucketDelta
				: (sample.turnLike ? TurnBucketDeltaFromName(lowered) : 0);
			sample.paceTransitionLike = metadata && metadata->hasPaceTransitionLike
				? metadata->paceTransitionLike
				: sample.transitionLike &&
				(lowered.find("transition") != std::string::npos ||
				 lowered.find("towalk") != std::string::npos ||
				 lowered.find("torun") != std::string::npos ||
				 lowered.find("tosprint") != std::string::npos ||
				 lowered.find("tocrouch") != std::string::npos ||
				 lowered.find("tostand") != std::string::npos);
			sample.sourceMoveState = metadata && metadata->hasSourceMoveState
				? metadata->sourceMoveState
				: (sample.transitionLike
				? TransitionSourceMoveStateFromName(lowered)
				: MoveStateFromFamilyName(lowered));
			sample.targetMoveState = metadata && metadata->hasTargetMoveState
				? metadata->targetMoveState
				: (sample.transitionLike
				? TransitionTargetMoveStateFromName(lowered)
				: MoveStateFromFamilyName(lowered));
			sample.directionBucketFromName = metadata && metadata->hasDirectionBucket
				? metadata->directionBucket
				: DirectionBucketFromName(lowered);
			const int sampleIndex = static_cast<int>(m_Samples.size());
			m_Samples.push_back(sample);
			m_ClipSampleIndices[name].push_back(sampleIndex);
		}
		BuildFootContactPhases(m_ClipSampleIndices[name]);
	}

	if (m_Samples.size() < 2)
	{
		VANS_LOG_WARN("[MotionMatching] Database build skipped: not enough valid samples.");
		return false;
	}

	m_Mean.fill(0.0f);
	m_Std.fill(0.0f);
	for (const Sample& sample : m_Samples)
		for (int i = 0; i < FeatureDim; ++i)
			m_Mean[i] += sample.rawFeature[i];
	for (float& mean : m_Mean)
		mean /= static_cast<float>(m_Samples.size());
	for (const Sample& sample : m_Samples)
		for (int i = 0; i < FeatureDim; ++i)
		{
			const float d = sample.rawFeature[i] - m_Mean[i];
			m_Std[i] += d * d;
		}
	for (float& stdev : m_Std)
	{
		stdev = std::sqrt(stdev / static_cast<float>(m_Samples.size()));
		if (stdev < kEpsilon)
			stdev = 1.0f;
	}

	for (Sample& sample : m_Samples)
		NormalizeFeature(sample.feature);

	m_DatabaseReady = true;
	m_DatabaseDirty = false;
	m_CurrentSample = 0;
	for (int i = 0; i < static_cast<int>(m_Samples.size()); ++i)
	{
		if (m_Samples[i].loopLike && m_Samples[i].targetMoveState == m_Settings.states.idleState)
		{
			m_CurrentSample = i;
			break;
		}
	}
	m_CurrentTime = m_Samples[m_CurrentSample].time;
	m_InertialState.clear();
	m_LastOutputLocalPose.clear();
	m_PreviousOutputLocalPose.clear();
	m_CurrentTrajectoryVelocityRoot = glm::vec3(0.0f);
	m_LeftFootPlantWeight = 0.0f;
	m_RightFootPlantWeight = 0.0f;
	m_LeftContactOffset = 0.0f;
	m_RightContactOffset = 0.0f;
	m_ContactTransitionActive = false;
	m_CurrentCost = std::numeric_limits<float>::max();
	m_TimeSinceSearch = m_Settings.searchThrottle;
	m_TimeSinceSwitch = m_Settings.minSwitchInterval;
	m_PreviousQueryModelPose.clear();
	m_LastOutputLocalPose.clear();
	m_CurrentLeftFootVelocity = glm::vec3(0.0f);
	m_CurrentRightFootVelocity = glm::vec3(0.0f);
	m_CurrentPelvisVelocity = glm::vec3(0.0f);
	m_HasQueryVelocity = false;
	m_CurrentPlaybackRate = 1.0f;
	m_DebugData.sampleCount = static_cast<int>(m_Samples.size());
	m_DebugData.clipCount = includedClipCount;
	m_DebugData.databaseReady = true;
	VANS_LOG("[MotionMatching] Built database: samples=" << m_Samples.size() << " clips=" << includedClipCount);
	return true;
}

void VansMotionMatchingRuntime::NormalizeFeature(FeatureVector& feature) const
{
	for (int i = 0; i < FeatureDim; ++i)
		feature[i] = (feature[i] - m_Mean[i]) / m_Std[i];
}

float VansMotionMatchingRuntime::ComputeCost(const FeatureVector& query,
                                             const FeatureVector& candidate,
                                             float& outTrajectory,
	                                             float& outPose,
	                                             float& outContact) const
{
	outTrajectory = 0.0f;
	outPose = 0.0f;
	for (int d = kTrajectoryBegin; d < kTrajectoryEnd; ++d)
	{
		const float diff = query[d] - candidate[d];
		outTrajectory += diff * diff;
	}
	for (int d = kPoseBegin; d < kPoseEnd; ++d)
	{
		const float diff = query[d] - candidate[d];
		outPose += diff * diff;
	}
	outTrajectory *= m_Settings.trajectoryWeight;
	outPose *= m_Settings.poseWeight;
	outContact = 0.0f;
	for (int d = kContactBegin; d < kContactEnd; ++d)
	{
		const float diff = query[d] - candidate[d];
		outContact += diff * diff;
	}
	outContact *= m_Settings.contactWeight;
	return outTrajectory + outPose + outContact;
}

bool VansMotionMatchingRuntime::ShouldConsiderSampleForParameters(
	const Sample& sample,
	const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	if (!SearchGroupAllowsSample(sample, parameters))
		return false;

	const int moveState = ReadMoveStateParam(parameters);
	const float speed01 = ReadSpeedParam(parameters);
	const bool wantsIdle = speed01 < m_Settings.states.idleSpeedThreshold || moveState == m_Settings.states.idleState;
	const bool currentValid = m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size());
	const Sample* currentSample = currentValid ? &m_Samples[m_CurrentSample] : nullptr;
	const int currentMoveState = currentSample ? currentSample->targetMoveState : 0;
	const int desiredMoveState = ResolveDesiredMoveState(parameters);
	const bool currentMoving = currentSample && IsMovingPlaybackSample(*currentSample);
	const bool desiredMoving = !wantsIdle;
	const bool startingFromIdle = !currentMoving && desiredMoving;
	const bool stoppingToIdle = currentMoving && !desiredMoving;
	const bool changingPace =
		IsPaceTransitionState(currentMoveState) &&
		IsPaceTransitionState(desiredMoveState) &&
		currentMoveState != desiredMoveState;
	const bool changingStance =
		IsStanceState(currentMoveState) &&
		IsStanceState(desiredMoveState) &&
		currentMoveState != desiredMoveState;
	const glm::vec3 desiredVelRoot = BuildDesiredVelocityRoot(parameters, m_Rig);
	const glm::vec2 desiredDir(desiredVelRoot.x, desiredVelRoot.y);
	const float desiredDirLen = glm::length(desiredDir);
	glm::vec3 forwardAxis = m_Rig.forwardAxis;
	forwardAxis.z = 0.0f;
	if (glm::length(glm::vec2(forwardAxis.x, forwardAxis.y)) <= kEpsilon)
		forwardAxis = glm::vec3(0.0f, -1.0f, 0.0f);
	else
		forwardAxis = glm::normalize(forwardAxis);
	const glm::vec3 leftAxis(-forwardAxis.y, forwardAxis.x, 0.0f);
	auto directionBucket = [&](const glm::vec2& dir) -> int
	{
		const glm::vec3 dir3(dir.x, dir.y, 0.0f);
		float angle = std::atan2(glm::dot(dir3, leftAxis), glm::dot(dir3, forwardAxis));
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kTwoPi = kPi * 2.0f;
		angle = std::fmod(angle, kTwoPi);
		if (angle < 0.0f)
			angle += kTwoPi;
		return static_cast<int>((angle + kPi * 0.125f) / (kPi * 0.25f)) & 7;
	};
	auto sampleMatchesDesiredDirection = [&]() -> bool
	{
		if (desiredDirLen <= kEpsilon)
			return true;
		const glm::vec2 sampleDir(sample.rawFeature[4], sample.rawFeature[5]);
		const float sampleDirLen = glm::length(sampleDir);
		if (sampleDirLen <= kEpsilon)
			return true;
		const int delta = std::abs(SignedBucketDelta(directionBucket(desiredDir), directionBucket(sampleDir)));
		return delta <= 1;
	};
	auto sampleNameMatchesDesiredDirection = [&]() -> bool
	{
		if (sample.directionBucketFromName < 0 || desiredDirLen <= kEpsilon)
			return sampleMatchesDesiredDirection();
		return std::abs(SignedBucketDelta(directionBucket(desiredDir), sample.directionBucketFromName)) <= 1;
	};
	auto sampleNameMatchesCurrentDirection = [&]() -> bool
	{
		if (sample.directionBucketFromName < 0 || !currentSample)
			return true;
		if (currentSample->directionBucketFromName >= 0)
			return sample.directionBucketFromName == currentSample->directionBucketFromName;
		const glm::vec2 currentDir(currentSample->rawFeature[4], currentSample->rawFeature[5]);
		if (glm::length(currentDir) <= kEpsilon)
			return true;
		return sample.directionBucketFromName == directionBucket(currentDir);
	};

	if (m_Settings.externallyDriven)
	{
		// Gameplay already applied the complete owner displacement for this
		// frame. Start/stop/turn clips carry an acceleration/rotation trajectory
		// that will not be consumed, so select the desired idle/loop slice and let
		// inertialization provide visual continuity.
		if (!sample.loopLike || sample.transitionLike)
			return false;
		return sample.targetMoveState == desiredMoveState &&
		       (desiredMoving ? sampleMatchesDesiredDirection() : sample.idleLike);
	}

	if (sample.transitionLike)
	{
		if (sample.startLike)
			return startingFromIdle && sample.targetMoveState == desiredMoveState && sampleNameMatchesDesiredDirection();
		if (sample.stopLike)
			return stoppingToIdle &&
			       sample.sourceMoveState == currentMoveState &&
			       sample.targetMoveState == desiredMoveState &&
			       sampleNameMatchesCurrentDirection();
		if (sample.paceTransitionLike)
			return (changingPace || changingStance) &&
			       sample.sourceMoveState == currentMoveState &&
			       sample.targetMoveState == desiredMoveState;
		if (sample.turnLike)
		{
			if (!m_DirectionChangedForSearch)
				return false;
			if (m_SourceDirectionBucketForSearch < 0)
				return false;
			if (sample.sourceMoveState != currentMoveState || sample.targetMoveState != desiredMoveState)
				return false;
			if (currentMoveState != desiredMoveState)
				return false;
			const int signedDelta = SignedBucketDelta(m_SourceDirectionBucketForSearch, m_LastDirectionBucket);
			const int absDelta = std::abs(signedDelta);
			if (absDelta == 0 || sample.turnBucketDelta != absDelta)
				return false;
			if (absDelta != 4 && sample.turnDirectionSign != 0 &&
			    sample.turnDirectionSign != (signedDelta > 0 ? 1 : -1))
				return false;
			return true;
		}
		return false;
	}

	if (!sample.loopLike)
		return false;

	if (startingFromIdle || stoppingToIdle || changingPace || changingStance)
		return false;
	return sample.targetMoveState == desiredMoveState && sampleMatchesDesiredDirection();
}

VansMotionMatchingRuntime::MatchResult VansMotionMatchingRuntime::FindBestMatch(
	const FeatureVector& query,
	const std::unordered_map<std::string, AnimatorParameter>& parameters)
{
	MatchResult best;
	best.totalCost = std::numeric_limits<float>::max();
	m_DebugData.topCandidates.clear();

	for (int i = 0; i < static_cast<int>(m_Samples.size()); ++i)
	{
		const Sample& sample = m_Samples[i];
		if (!ShouldConsiderSampleForParameters(sample, parameters))
			continue;

		// Continuing the active slice is evaluated explicitly from m_CurrentTime in
		// Update().  Searching another frame of the same clip would be a time
		// teleport, not continuation, and produces the characteristic loop jitter
		// when that frame wins again at the next search tick.
		if (m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size()) &&
		    sample.clipName == m_Samples[m_CurrentSample].clipName)
			continue;

		float trajectory = 0.0f;
		float pose = 0.0f;
		float contact = 0.0f;
		float total = ComputeCost(query, sample.feature, trajectory, pose, contact);

		float bias = 0.0f;
		if (m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size()))
		{
			const Sample& current = m_Samples[m_CurrentSample];
			// A loop is useful as the destination of a completed authored transition.
			// Do not reward loop-to-loop switching: that defeats continuation-first
			// playback and causes unnecessary clip churn under a stable query.
			if (sample.loopLike && !current.loopLike)
				bias -= m_Settings.loopBias;
			if (sample.transitionLike && !current.transitionLike)
				bias -= m_Settings.transitionBias;
		}

		MatchResult result;
		result.sampleIndex = i;
		result.trajectoryCost = trajectory;
		result.poseCost = pose;
		result.contactCost = contact;
		result.biasCost = bias;
		result.totalCost = total + bias;
		PushCandidateDebug(result);
		if (result.totalCost < best.totalCost)
			best = result;
	}

	return best;
}

void VansMotionMatchingRuntime::PushCandidateDebug(const MatchResult& result)
{
	if (result.sampleIndex < 0 || result.sampleIndex >= static_cast<int>(m_Samples.size()))
		return;

	MotionMatchingCandidateDebug item;
	const Sample& sample = m_Samples[result.sampleIndex];
	item.clipName = sample.clipName;
	item.time = sample.time;
	item.totalCost = result.totalCost;
	item.trajectoryCost = result.trajectoryCost;
	item.poseCost = result.poseCost;
	item.contactCost = result.contactCost;
	item.biasCost = result.biasCost;

	auto& list = m_DebugData.topCandidates;
	list.push_back(item);
	std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.totalCost < b.totalCost; });
	const int limit = (std::max)(1, m_Settings.topCandidateCount);
	if (static_cast<int>(list.size()) > limit)
		list.resize(static_cast<size_t>(limit));
}

void VansMotionMatchingRuntime::BeginInertialTransition(
	const std::vector<glm::mat4>& target,
	const std::vector<glm::mat4>& targetFuture,
	float velocityDeltaTime)
{
	if (target.empty() || targetFuture.size() != target.size() ||
	    m_LastOutputLocalPose.size() != target.size())
	{
		m_InertialState.clear();
		m_Blending = false;
		return;
	}

	const float invDt = velocityDeltaTime > kEpsilon ? 1.0f / velocityDeltaTime : 0.0f;
	const bool hasPrevious = m_PreviousOutputLocalPose.size() == target.size() && invDt > 0.0f;
	m_InertialState.assign(target.size(), InertialBoneState{});
	bool hasMeaningfulOffset = false;

	for (size_t i = 0; i < target.size(); ++i)
	{
		glm::vec3 sourcePos, sourceScale;
		glm::quat sourceRot;
		DecomposeTransform(m_LastOutputLocalPose[i], sourcePos, sourceRot, sourceScale);
		glm::vec3 previousPos = sourcePos, previousScale;
		glm::quat previousRot = sourceRot;
		if (hasPrevious)
			DecomposeTransform(m_PreviousOutputLocalPose[i], previousPos, previousRot, previousScale);

		glm::vec3 targetPos, targetScale;
		glm::quat targetRot;
		DecomposeTransform(target[i], targetPos, targetRot, targetScale);
		glm::vec3 futurePos, futureScale;
		glm::quat futureRot;
		DecomposeTransform(targetFuture[i], futurePos, futureRot, futureScale);

		InertialBoneState& state = m_InertialState[i];
		state.positionOffset = sourcePos - targetPos;
		state.positionVelocity = (sourcePos - previousPos) * invDt - (futurePos - targetPos) * invDt;
		state.rotationOffset = QuaternionLogVector(sourceRot * glm::conjugate(targetRot));
		const glm::vec3 sourceAngular = hasPrevious
			? QuaternionLogVector(sourceRot * glm::conjugate(previousRot)) * invDt
			: glm::vec3(0.0f);
		const glm::vec3 targetAngular = QuaternionLogVector(futureRot * glm::conjugate(targetRot)) * invDt;
		state.angularVelocity = sourceAngular - targetAngular;
		hasMeaningfulOffset |= glm::dot(state.positionOffset, state.positionOffset) > 1e-8f ||
		                       glm::dot(state.rotationOffset, state.rotationOffset) > 1e-8f;
	}

	m_Blending = hasMeaningfulOffset && m_Settings.inertializationHalfLife > kEpsilon;
	m_BlendElapsed = 0.0f;
}

void VansMotionMatchingRuntime::ApplyInertialization(
	float deltaTime,
	const std::vector<glm::mat4>& target,
	std::vector<glm::mat4>& out)
{
	if (!m_Blending || m_InertialState.size() != target.size())
	{
		out = target;
		return;
	}

	out.resize(target.size());
	float maximumResidual = 0.0f;
	const float halfLife = (std::max)(m_Settings.inertializationHalfLife, kEpsilon);
	m_BlendElapsed += (std::max)(deltaTime, 0.0f);

	for (size_t i = 0; i < target.size(); ++i)
	{
		InertialBoneState& state = m_InertialState[i];
		DecayCriticalSpring(state.positionOffset, state.positionVelocity, halfLife, deltaTime);
		DecayCriticalSpring(state.rotationOffset, state.angularVelocity, halfLife, deltaTime);

		glm::vec3 targetPos, targetScale;
		glm::quat targetRot;
		DecomposeTransform(target[i], targetPos, targetRot, targetScale);
		const glm::vec3 position = targetPos + state.positionOffset;
		const glm::quat rotation = glm::normalize(QuaternionExpVector(state.rotationOffset) * targetRot);
		out[i] = glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation) *
		         glm::scale(glm::mat4(1.0f), targetScale);
		maximumResidual = (std::max)(maximumResidual,
			glm::length(state.positionOffset) + glm::length(state.rotationOffset));
	}

	if (m_BlendElapsed >= (std::max)(m_Settings.inertializationMaxDuration, halfLife) ||
	    maximumResidual < 1e-4f)
	{
		m_Blending = false;
		m_InertialState.clear();
	}
}

bool VansMotionMatchingRuntime::Update(float deltaTime,
                                       const Skeleton& skeleton,
                                       const std::unordered_map<std::string, VansAnimationClip>& clips,
                                       const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                                   const glm::mat4& ownerWorldTransform,
                                       std::vector<glm::mat4>& outLocalTransforms)
{
	m_DebugData.enabled = m_Settings.enabled;
	m_DebugData.usedThisFrame = false;
	if (m_Settings.externallyDriven)
		UpdateActualOwnerVelocity(deltaTime, ownerWorldTransform);
	if (!m_Settings.enabled)
		return false;

	m_DebugData.querySpeed = ReadSpeedParam(parameters) * m_Settings.desiredSpeedScale;
	m_DebugData.queryDirection = ReadDirectionParam(parameters);
	const bool parameterWantsMotionMatching = ReadBoolParam(parameters, m_Settings.parameters.enabled, true);
	if (!parameterWantsMotionMatching)
	{
		m_HasLastSearchContext = false;
		return false;
	}

	if ((m_DatabaseDirty || !m_DatabaseReady) && m_Settings.autoBuild)
		BuildDatabase(clips, skeleton);
	if (!m_DatabaseReady || m_Samples.empty())
		return false;

	if (m_CurrentSample < 0 || m_CurrentSample >= static_cast<int>(m_Samples.size()))
		m_CurrentSample = 0;

	const Sample* activeSample = &m_Samples[m_CurrentSample];
	auto activeClipIt = clips.find(activeSample->clipName);
	if (activeClipIt == clips.end())
	{
		MarkDatabaseDirty();
		return false;
	}

	float wantedPlaybackRate = 1.0f;
	if (m_Settings.enableSpeedMatching)
	{
		const float desiredSpeed = m_Settings.externallyDriven && m_HasActualOwnerVelocity
			? glm::length(glm::vec2(m_ActualOwnerVelocityRoot))
			: glm::length(glm::vec2(BuildDesiredVelocityRoot(parameters, m_Rig)));
		if (desiredSpeed > kEpsilon && activeSample->trajectorySpeed > kEpsilon)
		{
			const float minRate = std::max(0.01f, std::min(m_Settings.minPlaybackRate, m_Settings.maxPlaybackRate));
			const float maxRate = std::max(minRate, std::max(m_Settings.minPlaybackRate, m_Settings.maxPlaybackRate));
			wantedPlaybackRate = glm::clamp(desiredSpeed / activeSample->trajectorySpeed, minRate, maxRate);
		}
	}
	const float rateAlpha = m_Settings.playbackRateSmoothing <= 0.0f
		? 1.0f
		: glm::clamp(1.0f - std::exp(-m_Settings.playbackRateSmoothing * std::max(deltaTime, 0.0f)), 0.0f, 1.0f);
	m_CurrentPlaybackRate = glm::mix(m_CurrentPlaybackRate, wantedPlaybackRate, rateAlpha);
	m_DebugData.playbackRate = m_CurrentPlaybackRate;
	const float playbackDelta = deltaTime * m_CurrentPlaybackRate;
	const float previousPlaybackTime = m_CurrentTime;
	bool loopWrapped = false;
	if (activeSample->loopLike)
	{
		m_CurrentTime = WrapClipTime(activeClipIt->second, m_CurrentTime + playbackDelta);
		loopWrapped = activeClipIt->second.duration > kEpsilon &&
		              m_CurrentTime + kEpsilon < previousPlaybackTime;
	}
	else
		m_CurrentTime = glm::clamp(m_CurrentTime + playbackDelta, 0.0f, (std::max)(0.0f, activeClipIt->second.duration));
	const float outgoingLeftContact = m_LeftFootPlantWeight;
	const float outgoingRightContact = m_RightFootPlantWeight;
	// Keep the logical current sample synchronized with playback.  Holding the
	// sample index at the original transition frame makes continuation, contact
	// phase and current-cost comparisons stale after the first frame.
	if (const auto sampleIt = m_ClipSampleIndices.find(activeSample->clipName);
	    sampleIt != m_ClipSampleIndices.end() && !sampleIt->second.empty())
	{
		const std::vector<int>& clipSamples = sampleIt->second;
		const float samplePosition = (std::max)(0.0f, m_CurrentTime * m_Settings.sampleRate);
		const size_t lower = (std::min)(
			clipSamples.size() - 1,
			static_cast<size_t>(std::floor(samplePosition)));
		size_t upper = lower + 1;
		if (upper >= clipSamples.size())
			upper = activeSample->loopLike ? 0 : lower;
		const float phaseAlpha = upper == lower ? 0.0f : samplePosition - std::floor(samplePosition);
		const size_t nearest = phaseAlpha < 0.5f ? lower : upper;
		m_CurrentSample = sampleIt->second[nearest];
		activeSample = &m_Samples[m_CurrentSample];
	}
	float targetLeftContact = 0.0f;
	float targetRightContact = 0.0f;
	SampleContactWeights(m_CurrentSample, m_CurrentTime, targetLeftContact, targetRightContact);
	if (loopWrapped)
		BeginContactTransition(outgoingLeftContact, outgoingRightContact, targetLeftContact, targetRightContact);
	else
		AdvanceContactWeights(deltaTime, targetLeftContact, targetRightContact);
	const float trajectoryHorizon = (std::max)(m_Settings.schema.futureTimes[0], kEpsilon);
	const glm::vec3 sampledTrajectoryVelocity(
		activeSample->rawFeature[0] / trajectoryHorizon,
		activeSample->rawFeature[1] / trajectoryHorizon,
		0.0f);
	const float trajectoryAlpha = 1.0f - std::exp(
		-(std::max)(m_Settings.trajectoryResponsiveness, 0.0f) * (std::max)(deltaTime, 0.0f));
	m_CurrentTrajectoryVelocityRoot = m_Settings.externallyDriven && m_HasActualOwnerVelocity
		? m_ActualOwnerVelocityRoot
		: glm::mix(m_CurrentTrajectoryVelocityRoot, sampledTrajectoryVelocity, trajectoryAlpha);
	std::vector<glm::mat4> currentLocal;
	SamplePose(activeClipIt->second, m_CurrentTime, skeleton, currentLocal);
	if (loopWrapped)
	{
		// Crossing an authored loop seam is still continuous playback, not an MM
		// switch.  Preserve the outgoing pose and velocity while the first frames
		// of the new cycle settle, which hides small non-seamless authoring errors.
		std::vector<glm::mat4> seamFuture;
		const float velocityDt = 1.0f / (std::max)(m_Settings.sampleRate, 1.0f);
		SamplePose(activeClipIt->second,
		           ResolveClipTime(activeClipIt->second, m_CurrentTime + velocityDt, true),
		           skeleton,
		           seamFuture);
		BeginInertialTransition(currentLocal, seamFuture, velocityDt);
	}

	const std::vector<glm::mat4>& queryLocal =
		(m_LastOutputLocalPose.size() == skeleton.bones.size()) ? m_LastOutputLocalPose : currentLocal;
	std::vector<glm::mat4> currentModel;
	BuildModelSpacePose(queryLocal, skeleton, currentModel);
	if (!m_PreviousQueryModelPose.empty() && deltaTime > kEpsilon &&
	    m_PreviousQueryModelPose.size() == currentModel.size())
	{
		const glm::mat4 rootModel = currentModel[m_Rig.root];
		const glm::vec3 rawLeftFootVelocity = TransformVectorToRootSpace(rootModel,
			(ExtractTranslation(currentModel[m_Rig.leftFoot]) - ExtractTranslation(m_PreviousQueryModelPose[m_Rig.leftFoot])) / deltaTime);
		const glm::vec3 rawRightFootVelocity = TransformVectorToRootSpace(rootModel,
			(ExtractTranslation(currentModel[m_Rig.rightFoot]) - ExtractTranslation(m_PreviousQueryModelPose[m_Rig.rightFoot])) / deltaTime);
		const glm::vec3 rawPelvisVelocity = TransformVectorToRootSpace(rootModel,
			(ExtractTranslation(currentModel[m_Rig.pelvis]) - ExtractTranslation(m_PreviousQueryModelPose[m_Rig.pelvis])) / deltaTime);
		const float alpha = 1.0f - std::exp(-deltaTime / 0.10f);
		if (m_HasQueryVelocity)
		{
			m_CurrentLeftFootVelocity = glm::mix(m_CurrentLeftFootVelocity, rawLeftFootVelocity, alpha);
			m_CurrentRightFootVelocity = glm::mix(m_CurrentRightFootVelocity, rawRightFootVelocity, alpha);
			m_CurrentPelvisVelocity = glm::mix(m_CurrentPelvisVelocity, rawPelvisVelocity, alpha);
		}
		else
		{
			m_CurrentLeftFootVelocity = rawLeftFootVelocity;
			m_CurrentRightFootVelocity = rawRightFootVelocity;
			m_CurrentPelvisVelocity = rawPelvisVelocity;
			m_HasQueryVelocity = true;
		}
	}
	m_PreviousQueryModelPose = currentModel;

	FeatureVector query = BuildQueryFeature(parameters, queryLocal, skeleton, m_Rig);
	const float speed01 = ReadSpeedParam(parameters);
	const float direction = ReadDirectionParam(parameters);
	const int moveState = ReadMoveStateParam(parameters);
	const bool isCrouching = ReadCrouchingParam(parameters);
	const bool isAirborne = ReadAirborneParam(parameters);
	const bool isMoving = speed01 >= m_Settings.states.idleSpeedThreshold;
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kTwoPi = kPi * 2.0f;
	float wrappedDirection = std::fmod(direction, kTwoPi);
	if (wrappedDirection < 0.0f)
		wrappedDirection += kTwoPi;
	const int directionBucket = static_cast<int>((wrappedDirection + kPi * 0.125f) / (kPi * 0.25f)) & 7;
	const bool directionChanged =
		m_HasLastSearchContext &&
		m_LastDirectionBucket != directionBucket;
	m_SourceDirectionBucketForSearch = directionChanged ? m_LastDirectionBucket : directionBucket;
	const bool searchContextChanged =
		!m_HasLastSearchContext ||
		m_LastMoveState != moveState ||
		directionChanged ||
		m_LastCrouching != isCrouching ||
		m_LastAirborne != isAirborne ||
		m_LastMoving != isMoving;
	if (searchContextChanged)
		m_TimeSinceSearch = (std::max)(m_TimeSinceSearch, m_Settings.searchThrottle);
	m_HasLastSearchContext = true;
	m_LastMoveState = moveState;
	m_LastDirectionBucket = directionBucket;
	m_LastCrouching = isCrouching;
	m_LastAirborne = isAirborne;
	m_LastMoving = isMoving;
	m_DirectionChangedForSearch = directionChanged;
	m_DebugData.querySpeed = speed01 * m_Settings.desiredSpeedScale;
	m_DebugData.queryDirection = direction;
	NormalizeFeature(query);

	m_TimeSinceSearch += deltaTime;
	m_TimeSinceSwitch += deltaTime;
	if (m_TimeSinceSearch >= m_Settings.searchThrottle)
	{
		m_TimeSinceSearch = 0.0f;

		FeatureVector currentFeature = ExtractDatabaseFeature(activeClipIt->second, m_CurrentTime, activeSample->loopLike, skeleton, m_Rig);
		// Runtime phase is interpolated from the database contact curves.  Keep
		// current-cost comparison in the same feature domain as the candidates.
		currentFeature[kContactBegin] = m_LeftFootPlantWeight;
		currentFeature[kContactBegin + 1] = m_RightFootPlantWeight;
		NormalizeFeature(currentFeature);
		float currentTrajectory = 0.0f;
		float currentPose = 0.0f;
		float currentContact = 0.0f;
		const float currentCost = ComputeCost(query, currentFeature, currentTrajectory, currentPose, currentContact);
		const float transitionCompletionWindow = 2.0f / (std::max)(1.0f, m_Settings.sampleRate);
		const bool activeTransitionComplete =
			!activeSample->loopLike &&
			m_CurrentTime >= (std::max)(0.0f, activeClipIt->second.duration - transitionCompletionWindow);

		MatchResult best = FindBestMatch(query, parameters);
		const bool bestIsTargetLoop =
			best.sampleIndex >= 0 &&
			m_CurrentSample >= 0 &&
			m_CurrentSample < static_cast<int>(m_Samples.size()) &&
			m_Samples[best.sampleIndex].loopLike &&
			m_Samples[best.sampleIndex].targetMoveState == m_Samples[m_CurrentSample].targetMoveState;
		const bool shouldExitFinishedTransition = activeTransitionComplete && bestIsTargetLoop;
		const bool activeMovingSample = activeSample && IsMovingPlaybackSample(*activeSample);
		const bool shouldEnterStartTransition =
			searchContextChanged &&
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].startLike &&
			isMoving &&
			!activeMovingSample;
		const bool bestIsContextTransition =
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].transitionLike &&
			(m_Samples[best.sampleIndex].startLike ||
			 m_Samples[best.sampleIndex].stopLike ||
			 m_Samples[best.sampleIndex].paceTransitionLike);
		const bool contextTransitionHasValidSource =
			bestIsContextTransition &&
			m_CurrentSample >= 0 &&
			m_CurrentSample < static_cast<int>(m_Samples.size()) &&
			(m_Samples[best.sampleIndex].sourceMoveState == m_Samples[m_CurrentSample].targetMoveState ||
			 m_Samples[m_CurrentSample].idleLike);
		const bool contextTransitionChangesRole =
			bestIsContextTransition &&
			m_CurrentSample >= 0 &&
			m_CurrentSample < static_cast<int>(m_Samples.size()) &&
			(m_Samples[best.sampleIndex].startLike != m_Samples[m_CurrentSample].startLike ||
			 m_Samples[best.sampleIndex].stopLike != m_Samples[m_CurrentSample].stopLike ||
			 m_Samples[best.sampleIndex].idleLike != m_Samples[m_CurrentSample].idleLike ||
			 m_Samples[best.sampleIndex].loopLike != m_Samples[m_CurrentSample].loopLike);
		const bool legacyShouldEnterContextTransition =
			shouldEnterStartTransition ||
			(searchContextChanged &&
			 best.sampleIndex >= 0 &&
			 m_CurrentSample >= 0 &&
			 m_CurrentSample < static_cast<int>(m_Samples.size()) &&
			 m_Samples[best.sampleIndex].transitionLike &&
			 (m_Samples[best.sampleIndex].startLike ||
			  m_Samples[best.sampleIndex].stopLike ||
			  m_Samples[best.sampleIndex].paceTransitionLike) &&
			 (m_Samples[best.sampleIndex].sourceMoveState == m_Samples[m_CurrentSample].targetMoveState ||
			  m_Samples[m_CurrentSample].idleLike) &&
			 m_Samples[best.sampleIndex].targetMoveState != m_Samples[m_CurrentSample].targetMoveState);
		const bool groupedShouldEnterContextTransition =
			shouldEnterStartTransition ||
			(searchContextChanged &&
			 contextTransitionHasValidSource &&
			 (m_Samples[best.sampleIndex].targetMoveState != m_Samples[m_CurrentSample].targetMoveState ||
			  contextTransitionChangesRole));
		const bool shouldEnterContextTransition = m_Settings.searchGroups.empty()
			? legacyShouldEnterContextTransition
			: groupedShouldEnterContextTransition;
		float requiredImprovement = searchContextChanged
			? m_Settings.minSwitchCostImprovement * 0.5f
			: m_Settings.minSwitchCostImprovement;
		const float continuationCost = (std::max)(
			0.0f, currentCost - (std::max)(0.0f, m_Settings.continuationBias));
		if (!searchContextChanged)
		{
			const float relativeImprovement = continuationCost *
				glm::clamp(m_Settings.minSwitchCostRatio, 0.0f, 0.95f);
			requiredImprovement = (std::max)(requiredImprovement, relativeImprovement);
		}
		const bool improvesEnough =
			best.sampleIndex >= 0 &&
			(best.totalCost + requiredImprovement < continuationCost ||
			 shouldEnterContextTransition ||
			 shouldExitFinishedTransition ||
			 m_CurrentSample < 0);
		const bool canInterruptBlend =
			!m_Blending ||
			shouldEnterContextTransition ||
			shouldExitFinishedTransition ||
			m_BlendElapsed >= m_Settings.inertializationMaxDuration *
				glm::clamp(m_Settings.blendInterruptFraction, 0.0f, 1.0f);
		const bool switchIntervalReady =
			searchContextChanged ||
			shouldEnterContextTransition ||
			shouldExitFinishedTransition ||
			m_TimeSinceSwitch >= m_Settings.minSwitchInterval;
		const bool canSwitchNow =
			best.sampleIndex >= 0 &&
			improvesEnough &&
			canInterruptBlend &&
			switchIntervalReady;
		if (best.sampleIndex >= 0 &&
		    canSwitchNow)
		{
			const float sourceLeftContact = m_LeftFootPlantWeight;
			const float sourceRightContact = m_RightFootPlantWeight;
			m_CurrentSample = best.sampleIndex;
			m_CurrentTime = m_Samples[m_CurrentSample].time;
			m_CurrentCost = best.totalCost;
			m_TimeSinceSwitch = 0.0f;
			m_PreviousQueryModelPose.clear();
			m_CurrentLeftFootVelocity = glm::vec3(0.0f);
			m_CurrentRightFootVelocity = glm::vec3(0.0f);
			m_CurrentPelvisVelocity = glm::vec3(0.0f);
			m_HasQueryVelocity = false;
			++m_SwitchCount;

			activeSample = &m_Samples[m_CurrentSample];
			activeClipIt = clips.find(activeSample->clipName);
			if (activeClipIt == clips.end())
			{
				MarkDatabaseDirty();
				return false;
			}
			SamplePose(activeClipIt->second, m_CurrentTime, skeleton, currentLocal);
			std::vector<glm::mat4> targetFuture;
			const float velocityDt = 1.0f / (std::max)(m_Settings.sampleRate, 1.0f);
			SamplePose(activeClipIt->second,
			           ResolveClipTime(activeClipIt->second, m_CurrentTime + velocityDt, activeSample->loopLike),
			           skeleton,
			           targetFuture);
			BeginInertialTransition(currentLocal, targetFuture, velocityDt);
			float switchedLeftContact = 0.0f;
			float switchedRightContact = 0.0f;
			SampleContactWeights(m_CurrentSample, m_CurrentTime, switchedLeftContact, switchedRightContact);
			BeginContactTransition(sourceLeftContact, sourceRightContact,
			                       switchedLeftContact, switchedRightContact);
		}
		else
		{
			m_CurrentCost = currentCost;
		}

		m_DebugData.currentCost = currentCost;
		m_DebugData.trajectoryCost = currentTrajectory;
		m_DebugData.poseCost = currentPose;
		m_DebugData.contactCost = currentContact;
		m_DebugData.biasCost = 0.0f;
		if (best.sampleIndex >= 0)
		{
			m_DebugData.selectedClip = m_Samples[best.sampleIndex].clipName;
			m_DebugData.selectedTime = m_Samples[best.sampleIndex].time;
		}
		else
		{
			m_DebugData.selectedClip = activeSample->clipName;
			m_DebugData.selectedTime = m_CurrentTime;
		}
	}

	activeSample = &m_Samples[m_CurrentSample];
	activeClipIt = clips.find(activeSample->clipName);
	if (activeClipIt == clips.end())
	{
		MarkDatabaseDirty();
		return false;
	}

	std::vector<glm::mat4> target;
	SamplePose(activeClipIt->second, m_CurrentTime, skeleton, target);
	ApplyInertialization(deltaTime, target, outLocalTransforms);
	m_PreviousOutputLocalPose = m_LastOutputLocalPose;
	m_LastOutputLocalPose = outLocalTransforms;

	m_DebugData.usedThisFrame = true;
	m_DebugData.databaseReady = true;
	m_DebugData.rigReady = m_Rig.IsValid();
	m_DebugData.sampleCount = static_cast<int>(m_Samples.size());
	m_DebugData.switches = m_SwitchCount;
	m_DebugData.activeClip = activeSample->clipName;
	m_DebugData.activeTime = m_CurrentTime;
	return true;
}
