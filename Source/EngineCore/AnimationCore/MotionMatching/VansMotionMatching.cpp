#include "VansMotionMatching.h"
#include "../VansAnimationSampler.h"
#include "../VansPoseMath.h"
#include "../../Util/VansLog.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/matrix_decompose.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
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

	bool ContainsAnyToken(const std::string& text, const std::vector<std::string>& tokens)
	{
		if (tokens.empty())
			return false;
		const std::string loweredText = ToLower(text);
		for (const std::string& token : tokens)
		{
			if (!token.empty() && loweredText.find(ToLower(token)) != std::string::npos)
				return true;
		}
		return false;
	}

	bool EndsWithToken(const std::string& text, const std::string& suffix)
	{
		return text.size() >= suffix.size() &&
			text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	std::vector<std::string> MergeTokens(const std::vector<std::string>& a,
	                                     const std::vector<std::string>& b)
	{
		std::vector<std::string> merged = a;
		merged.insert(merged.end(), b.begin(), b.end());
		return merged;
	}

	bool ShouldIncludeAutoDatabaseClip(
		const std::string& clipName,
		const std::vector<std::string>& includeTokens,
		const std::vector<std::string>& excludeTokens)
	{
		if (!includeTokens.empty() && !ContainsAnyToken(clipName, includeTokens))
			return false;
		if (ContainsAnyToken(clipName, excludeTokens))
			return false;
		return true;
	}

	std::string InferAutoDatabasePhase(const std::string& clipName)
	{
		const std::string name = ToLower(clipName);
		if (name.find("start") != std::string::npos)
			return "Start";
		if (name.find("stop") != std::string::npos)
			return "Stop";
		if (name.find("pivot") != std::string::npos)
			return "Pivot";
		if (name.find("turn") != std::string::npos)
			return "Turn";
		if (name.find("transition") != std::string::npos)
			return "Transition";
		if (name.find("to") != std::string::npos)
			return "Transition";
		if (name.find("idle") != std::string::npos)
			return "Idle";
		return "Move";
	}

	bool InferAutoDatabaseLoop(const std::string& clipName)
	{
		const std::string name = ToLower(clipName);
		const std::string phase = InferAutoDatabasePhase(clipName);
		const std::string loweredPhase = ToLower(phase);
		if (loweredPhase == "start" ||
		    loweredPhase == "stop" ||
		    loweredPhase == "pivot" ||
		    loweredPhase == "turn" ||
		    loweredPhase == "transition")
		{
			return false;
		}
		return name.find("loop") != std::string::npos ||
			name.find("idle") != std::string::npos ||
			name.find("walk") != std::string::npos ||
			name.find("run") != std::string::npos ||
			name.find("sprint") != std::string::npos ||
			name.find("crouch") != std::string::npos;
	}

	int InferAutoMoveStateFromName(const std::string& name,
	                               const MotionMatchingStateSemantics& states)
	{
		const std::string lowered = ToLower(name);
		if (lowered.find("sprint") != std::string::npos)
			return 3;
		if (lowered.find("run") != std::string::npos)
			return 2;
		if (lowered.find("walk") != std::string::npos)
			return 1;
		if (lowered.find("crouch") != std::string::npos)
			return states.crouchState;
		return states.idleState;
	}

	void InferAutoMoveStateTransition(const std::string& clipName,
	                                  const std::string& phase,
	                                  const MotionMatchingStateSemantics& states,
	                                  int& sourceMoveState,
	                                  int& targetMoveState)
	{
		const std::string lowered = ToLower(clipName);
		const std::string loweredPhase = ToLower(phase);
		const size_t toPos = lowered.find("to");
		if (toPos != std::string::npos && toPos > 0 && toPos + 2 < lowered.size())
		{
			sourceMoveState = InferAutoMoveStateFromName(lowered.substr(0, toPos), states);
			targetMoveState = InferAutoMoveStateFromName(lowered.substr(toPos + 2), states);
			return;
		}

		targetMoveState = InferAutoMoveStateFromName(lowered, states);
		sourceMoveState = targetMoveState;
		if (loweredPhase == "idle")
		{
			if (lowered.find("crouch") == std::string::npos)
				targetMoveState = states.idleState;
			sourceMoveState = targetMoveState;
		}
		else if (loweredPhase == "start")
		{
			sourceMoveState = targetMoveState == states.crouchState ? states.crouchState : states.idleState;
		}
		else if (loweredPhase == "stop")
		{
			sourceMoveState = targetMoveState;
			targetMoveState = sourceMoveState == states.crouchState ? states.crouchState : states.idleState;
		}
	}

	int InferAutoDirectionBucket(const std::string& clipName)
	{
		const std::string name = ToLower(clipName);
		auto hasDirectionToken = [&](const std::string& token) -> bool
		{
			return name.find("_" + token + "_") != std::string::npos ||
				EndsWithToken(name, "_" + token);
		};
		if (hasDirectionToken("fl")) return 1;
		if (hasDirectionToken("l")) return 2;
		if (hasDirectionToken("bl")) return 3;
		if (hasDirectionToken("b")) return 4;
		if (hasDirectionToken("br")) return 5;
		if (hasDirectionToken("r")) return 6;
		if (hasDirectionToken("fr")) return 7;
		if (hasDirectionToken("f")) return 0;
		return -1;
	}

	int DirectionTokenToBucket(const std::string& token)
	{
		if (token == "f") return 0;
		if (token == "fl") return 1;
		if (token == "l" || token == "ll") return 2;
		if (token == "bl") return 3;
		if (token == "b") return 4;
		if (token == "br") return 5;
		if (token == "r" || token == "rr") return 6;
		if (token == "fr") return 7;
		// UE 的 strafing 命名区分领先脚：LR/RL 仍分别代表左/右方向。
		if (token == "lr") return 2;
		if (token == "rl") return 6;
		return -1;
	}

	void InferAutoPivotDirectionBuckets(
		const std::string& clipName, MotionMatchingDatabaseClip& clip)
	{
		const std::string name = ToLower(clipName);
		const size_t pivot = name.find("pivot_");
		if (pivot == std::string::npos)
			return;
		const size_t sourceBegin = pivot + 6;
		const size_t sourceEnd = name.find('_', sourceBegin);
		if (sourceEnd == std::string::npos)
			return;
		const size_t targetEnd = name.find('_', sourceEnd + 1);
		const std::string source = name.substr(
			sourceBegin, sourceEnd - sourceBegin);
		const std::string target = name.substr(
			sourceEnd + 1,
			(targetEnd == std::string::npos ? name.size() : targetEnd) - sourceEnd - 1);
		if (clip.sourceDirectionBucket < 0)
			clip.sourceDirectionBucket = DirectionTokenToBucket(source);
		if (clip.directionBucket < 0)
			clip.directionBucket = DirectionTokenToBucket(target);
	}

	void InferAutoTurnMetadata(const std::string& clipName, MotionMatchingDatabaseClip& clip)
	{
		const std::string name = ToLower(clipName);
		if (name.find("turn") == std::string::npos)
			return;

		if (name.find("_l_") != std::string::npos)
			clip.turnDirectionSign = 1;
		else if (name.find("_r_") != std::string::npos)
			clip.turnDirectionSign = -1;

		if (name.find("180") != std::string::npos)
			clip.turnBucketDelta = 4;
		else if (name.find("135") != std::string::npos)
			clip.turnBucketDelta = 3;
		else if (name.find("090") != std::string::npos || name.find("_90") != std::string::npos)
			clip.turnBucketDelta = 2;
		else if (name.find("045") != std::string::npos || name.find("_45") != std::string::npos)
			clip.turnBucketDelta = 1;
	}

	void FillAutoDatabaseClipMetadata(MotionMatchingDatabaseClip& databaseClip,
	                                  const MotionMatchingDatabase& database,
	                                  const MotionMatchingSettings& settings)
	{
		if (databaseClip.phase.empty() || ToLower(databaseClip.phase) == "any" || databaseClip.phase == "*")
			databaseClip.phase = InferAutoDatabasePhase(databaseClip.name);
		const std::string effectivePhase = databaseClip.phase.empty() ? database.phase : databaseClip.phase;
		InferAutoMoveStateTransition(
			databaseClip.name,
			effectivePhase,
			settings.states,
			databaseClip.sourceMoveState,
			databaseClip.targetMoveState);
		if (ToLower(effectivePhase) == "pivot")
			InferAutoPivotDirectionBuckets(databaseClip.name, databaseClip);
		else if (databaseClip.directionBucket < 0)
			databaseClip.directionBucket = InferAutoDirectionBucket(databaseClip.name);
		InferAutoTurnMetadata(databaseClip.name, databaseClip);
	}

	int PopulateAutoDatabaseClips(MotionMatchingDatabase& database,
	                              const MotionMatchingSettings& settings,
	                              const std::unordered_map<std::string, VansAnimationClip>& clips)
	{
		if (!database.clips.empty())
		{
			for (MotionMatchingDatabaseClip& databaseClip : database.clips)
				FillAutoDatabaseClipMetadata(databaseClip, database, settings);
			return static_cast<int>(database.clips.size());
		}

		const std::vector<std::string> includeTokens =
			database.includeTokens.empty() ? settings.includeClipTokens : database.includeTokens;
		const std::vector<std::string> excludeTokens =
			MergeTokens(settings.excludeClipTokens, database.excludeTokens);

		for (const auto& [clipName, clip] : clips)
		{
			if (clip.duration <= kEpsilon)
				continue;
			if (!ShouldIncludeAutoDatabaseClip(clipName, includeTokens, excludeTokens))
				continue;
			const std::string inferredPhase = InferAutoDatabasePhase(clipName);
			const std::string databasePhase = ToLower(database.phase);
			if (!databasePhase.empty() && databasePhase != "any" && databasePhase != "*" &&
			    databasePhase != ToLower(inferredPhase))
			{
				continue;
			}

			MotionMatchingDatabaseClip databaseClip;
			databaseClip.name = clipName;
			databaseClip.loop = InferAutoDatabaseLoop(clipName);
			databaseClip.phase = (database.phase.empty() || ToLower(database.phase) == "any" || database.phase == "*")
				? inferredPhase
				: database.phase;
			FillAutoDatabaseClipMetadata(databaseClip, database, settings);
			database.clips.push_back(std::move(databaseClip));
		}

		return static_cast<int>(database.clips.size());
	}

	bool EnsureAutoDatabase(
		MotionMatchingSettings& settings,
		const std::unordered_map<std::string, VansAnimationClip>& clips)
	{
		if (!settings.autoBuild)
			return true;

		if (settings.databases.empty())
		{
			MotionMatchingDatabase database;
			database.name = "AutoPoseSearch";
			database.schema = "Default";
			database.normalizationSet = "Auto";
			database.stance = "Any";
			database.phase = "Any";
			database.enabled = true;
			settings.databases.push_back(std::move(database));
		}

		int generatedClipCount = 0;
		for (MotionMatchingDatabase& database : settings.databases)
			generatedClipCount += PopulateAutoDatabaseClips(database, settings, clips);

		if (generatedClipCount <= 0)
			return false;

		if (settings.selectorRows.empty())
		{
			MotionMatchingSelectorRow row;
			row.name = "Auto";
			row.stance = "Any";
			row.phase = "Any";
			for (const MotionMatchingDatabase& database : settings.databases)
				if (database.enabled)
					row.databases.push_back(database.name);
			settings.selectorRows.push_back(std::move(row));
		}

		VANS_LOG("[MotionMatching] Auto-built PoseSearch database config from clip tokens: clips="
			<< generatedClipCount
			<< " databases=" << settings.databases.size()
			<< " selectorRows=" << settings.selectorRows.size()
			<< " includeTokens=" << settings.includeClipTokens.size()
			<< " excludeTokens=" << settings.excludeClipTokens.size());
		return true;
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

	float SignedPlanarAngleFromForward(const glm::vec3& direction,
	                                  const glm::vec3& configuredForward)
	{
		glm::vec3 forward(configuredForward.x, configuredForward.y, 0.0f);
		glm::vec3 planar(direction.x, direction.y, 0.0f);
		if (glm::length(glm::vec2(forward.x, forward.y)) <= kEpsilon ||
		    glm::length(glm::vec2(planar.x, planar.y)) <= kEpsilon)
			return 0.0f;
		forward = glm::normalize(forward);
		planar = glm::normalize(planar);
		const glm::vec3 left(-forward.y, forward.x, 0.0f);
		return std::atan2(glm::dot(planar, left), glm::dot(planar, forward));
	}

	float LerpAngle(float from, float to, float t)
	{
		return from + NormalizeAngle(to - from) * glm::clamp(t, 0.0f, 1.0f);
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
	m_RootMotionSteering.Configure(settings.steering);
	m_RootMotionReconciler.Configure(settings.rootMotionReconciliation);
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
	m_LastPivotRequested = false;
	m_LastFacingTurnRequested = false;
	m_LastFacingTurnDirectionSign = 0;
	m_LastFacingTurnBucketDelta = 0;
	m_CurrentPlaybackRate = 1.0f;
	m_CurrentTrajectoryVelocityRoot = glm::vec3(0.0f);
	m_LeftFootPlantWeight = 0.0f;
	m_RightFootPlantWeight = 0.0f;
	m_LeftContactOffset = 0.0f;
	m_RightContactOffset = 0.0f;
	m_ContactTransitionActive = false;
	m_PrefersRootMotionThisFrame = false;
	m_QueryIntentSpeed01 = 0.0f;
	m_QueryIntentDirection = 0.0f;
	m_QueryDesiredVelocityRoot = glm::vec3(0.0f);
	m_QueryFacingDeltaDegrees = 0.0f;
	m_PivotRequested = false;
	m_PivotDatabaseAvailable = false;
	m_UrgentDirectionChange = false;
	m_RequestedMoveState = 0;
	m_EffectiveMoveState = 0;
	m_DirectionalStateFallback = false;
	m_FacingTurnRequested = false;
	m_FacingTurnDirectionSign = 0;
	m_FacingTurnBucketDelta = 0;
	m_RootMotionSteering.Reset();
	m_RootMotionReconciler.Reset();
	m_ActiveDatabaseIndices.clear();
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
	const bool wantsIdle = m_QueryIntentSpeed01 < m_Settings.states.idleSpeedThreshold ||
	                       moveState == m_Settings.states.idleState;
	if (ReadCrouchingParam(parameters) || moveState == m_Settings.states.crouchState)
		return m_Settings.states.crouchState;
	return wantsIdle ? m_Settings.states.idleState : moveState;
}

int VansMotionMatchingRuntime::ResolveDirectionalFallbackMoveState(int requestedMoveState) const
{
	if (!IsMovingState(requestedMoveState) ||
	    requestedMoveState == m_Settings.states.idleState ||
	    glm::length(glm::vec2(m_QueryDesiredVelocityRoot.x, m_QueryDesiredVelocityRoot.y)) <= kEpsilon)
	{
		return requestedMoveState;
	}

	const bool requestedCrouch = requestedMoveState == m_Settings.states.crouchState;
	const int tolerance = glm::clamp(m_Settings.directionBucketTolerance, 0, 4);
	auto directionBucket = [&](const glm::vec2& direction)
	{
		constexpr float pi = 3.14159265358979323846f;
		glm::vec3 forward3(m_Rig.forwardAxis.x, m_Rig.forwardAxis.y, 0.0f);
		if (glm::length(glm::vec2(forward3.x, forward3.y)) <= kEpsilon)
			forward3 = glm::vec3(0.0f, -1.0f, 0.0f);
		else
			forward3 = glm::normalize(forward3);
		const glm::vec2 forwardAxis(forward3.x, forward3.y);
		const glm::vec2 leftAxis(-forwardAxis.y, forwardAxis.x);
		float angle = std::atan2(glm::dot(direction, leftAxis), glm::dot(direction, forwardAxis));
		constexpr float twoPi = pi * 2.0f;
		angle = std::fmod(angle, twoPi);
		if (angle < 0.0f)
			angle += twoPi;
		return static_cast<int>((angle + pi * 0.125f) / (pi * 0.25f)) & 7;
	};
	const glm::vec3 intentDirectionRoot = BuildIntentDirectionRoot(m_Rig);
	const int desiredBucket = directionBucket(
		glm::vec2(intentDirectionRoot.x, intentDirectionRoot.y));
	auto sampleDirectionBucket = [&](const Sample& sample)
	{
		if (sample.directionBucketFromName >= 0)
			return sample.directionBucketFromName;
		const glm::vec2 direction(sample.rawFeature[4], sample.rawFeature[5]);
		return glm::length(direction) > kEpsilon ? directionBucket(direction) : -1;
	};
	auto databaseMatchesStance = [&](const Sample& sample)
	{
		if (sample.databaseIndex < 0 ||
		    sample.databaseIndex >= static_cast<int>(m_Settings.databases.size()))
			return false;
		const std::string stance = ToLower(m_Settings.databases[sample.databaseIndex].stance);
		const bool databaseCrouch = stance == "crouch";
		if (requestedCrouch)
			return (databaseCrouch || stance.empty() || stance == "any" || stance == "*") &&
			       sample.targetMoveState == m_Settings.states.crouchState;
		return !databaseCrouch && sample.targetMoveState != m_Settings.states.crouchState;
	};
	auto sampleMatchesDirection = [&](const Sample& sample)
	{
		const int bucket = sampleDirectionBucket(sample);
		return bucket < 0 ||
			std::abs(SignedBucketDelta(desiredBucket, bucket)) <= tolerance;
	};

	for (const Sample& sample : m_Samples)
	{
		if (sample.loopLike && sample.targetMoveState == requestedMoveState &&
		    databaseMatchesStance(sample) && sampleMatchesDirection(sample))
		{
			return requestedMoveState;
		}
	}

	// 素材库某个速度档缺少目标方向时，不应停在旧动作末帧。保持同一站姿和
	// 目标方向，从其余移动档中选根速度最接近的循环；CCT 仍只消费该动画的
	// Root Motion，因此不会用错误方向的胶囊位移掩盖素材缺口。
	const float desiredSpeed = glm::length(
		glm::vec2(m_QueryDesiredVelocityRoot.x, m_QueryDesiredVelocityRoot.y));
	int bestState = requestedMoveState;
	float bestSpeedError = std::numeric_limits<float>::max();
	for (const Sample& sample : m_Samples)
	{
		if (!sample.loopLike || !IsMovingState(sample.targetMoveState) ||
		    !databaseMatchesStance(sample) || !sampleMatchesDirection(sample))
		{
			continue;
		}
		const float speedError = std::abs(sample.trajectorySpeed - desiredSpeed);
		if (speedError < bestSpeedError)
		{
			bestSpeedError = speedError;
			bestState = sample.targetMoveState;
		}
	}
	return bestState;
}

void VansMotionMatchingRuntime::ResolveActiveDatabases(
	const std::unordered_map<std::string, AnimatorParameter>& parameters,
	bool forceFinishedTransitionExit)
{
	const int moveState = ReadMoveStateParam(parameters);
	const float speed01 = m_QueryIntentSpeed01;
	const bool wantsIdle = speed01 < m_Settings.states.idleSpeedThreshold || moveState == m_Settings.states.idleState;
	const int desiredMoveState = m_EffectiveMoveState;
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
	const bool desiredCrouchStance = desiredMoveState == m_Settings.states.crouchState;
	const std::string desiredStance = desiredCrouchStance ? "crouch" : "stand";
	std::string desiredPhase = "Move";
	if (startingFromIdle)
		desiredPhase = "Start";
	else if (stoppingToIdle)
		desiredPhase = "Stop";
	else if (changingPace || changingStance)
		desiredPhase = "Transition";
	else if (m_PivotRequested && m_PivotDatabaseAvailable && currentMoveState == desiredMoveState)
		desiredPhase = "Pivot";
	else if (m_FacingTurnRequested && currentMoveState == desiredMoveState)
		desiredPhase = "Turn";
	else if (!desiredMoving)
		desiredPhase = "Idle";
	if (forceFinishedTransitionExit)
		desiredPhase = desiredMoving ? "Move" : "Idle";

	m_ActiveDatabaseIndices.clear();
	const std::string desiredPhaseLower = ToLower(desiredPhase);
	auto phaseMatches = [&](const std::string& phase)
	{
		const std::string lowered = ToLower(phase);
		// Pivot 是对连续 Move 搜索的语义提示，不是互斥状态。让 Pivot 与
		// Move 同时竞争，轨迹不再需要等一个长 one-shot 播放完才能恢复。
		return lowered.empty() || lowered == "any" || lowered == "*" ||
			lowered == desiredPhaseLower ||
			(desiredPhaseLower == "pivot" && lowered == "move");
	};
	auto addDatabase = [&](int index)
	{
		if (std::find(m_ActiveDatabaseIndices.begin(), m_ActiveDatabaseIndices.end(), index) ==
			m_ActiveDatabaseIndices.end())
		{
			m_ActiveDatabaseIndices.push_back(index);
		}
	};
	for (const MotionMatchingSelectorRow& row : m_Settings.selectorRows)
	{
		const std::string stance = ToLower(row.stance);
		if (!stance.empty() && stance != "any" && stance != "*" && stance != desiredStance)
			continue;
		if (!phaseMatches(row.phase))
			continue;
		if (!row.moveStates.empty() &&
		    std::find(row.moveStates.begin(), row.moveStates.end(), desiredMoveState) == row.moveStates.end())
			continue;

		for (const std::string& databaseName : row.databases)
		{
			const std::string loweredDatabaseName = ToLower(databaseName);
			for (int i = 0; i < static_cast<int>(m_Settings.databases.size()); ++i)
			{
				const MotionMatchingDatabase& database = m_Settings.databases[i];
				if (database.enabled && ToLower(database.name) == loweredDatabaseName)
					addDatabase(i);
			}
		}
	}

	if (m_ActiveDatabaseIndices.empty())
	{
		for (int i = 0; i < static_cast<int>(m_Settings.databases.size()); ++i)
		{
			const MotionMatchingDatabase& database = m_Settings.databases[i];
			if (!database.enabled)
				continue;
			if (ToLower(database.stance) != desiredStance)
				continue;
			if (!phaseMatches(database.phase))
				continue;
			if (!database.moveStates.empty() &&
			    std::find(database.moveStates.begin(), database.moveStates.end(), desiredMoveState) == database.moveStates.end())
				continue;
			addDatabase(i);
		}
	}
}

int VansMotionMatchingRuntime::ResolveFacingTurnBucket(
	int moveState, int directionSign, float absoluteAngleDegrees) const
{
	int smallestCoveringBucket = 0;
	int largestBucket = 0;
	for (const MotionMatchingDatabase& database : m_Settings.databases)
	{
		if (!database.enabled)
			continue;
		for (const MotionMatchingDatabaseClip& clip : database.clips)
		{
			const std::string phase = ToLower(clip.phase.empty() ? database.phase : clip.phase);
			if (phase != "turn" || clip.turnBucketDelta <= 0 ||
			    clip.sourceMoveState != moveState || clip.targetMoveState != moveState)
				continue;
			if (clip.turnDirectionSign != 0 && clip.turnDirectionSign != directionSign)
				continue;
			const float authoredAngle = static_cast<float>(clip.turnBucketDelta) * 45.0f;
			largestBucket = (std::max)(largestBucket, clip.turnBucketDelta);
			if (authoredAngle + kEpsilon >= absoluteAngleDegrees &&
			    (smallestCoveringBucket == 0 || clip.turnBucketDelta < smallestCoveringBucket))
				smallestCoveringBucket = clip.turnBucketDelta;
		}
	}
	// Pose Search may enter a turn clip after its first frame. Select the
	// smallest authored arc that can cover the remaining error, then let facing
	// features choose the correct entry time inside that arc. Choosing the
	// numerically nearest clip can under-rotate and strand a non-looping turn at
	// its end while the error is still above threshold.
	return smallestCoveringBucket > 0 ? smallestCoveringBucket : largestBucket;
}

bool VansMotionMatchingRuntime::HasPivotDatabaseForState(int moveState) const
{
	for (const MotionMatchingDatabase& database : m_Settings.databases)
	{
		if (!database.enabled)
			continue;
		for (const MotionMatchingDatabaseClip& clip : database.clips)
		{
			const std::string phase = ToLower(clip.phase.empty() ? database.phase : clip.phase);
			if (phase == "pivot" && clip.sourceMoveState == moveState &&
			    clip.targetMoveState == moveState)
			{
				return true;
			}
		}
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

	m_DebugData.rigStatus = "missing explicit rig";
	return MotionMatchingResolvedRig{};
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

glm::vec3 VansMotionMatchingRuntime::BuildIntentDirectionRoot(
	const MotionMatchingResolvedRig& rig) const
{
	glm::vec3 forwardAxis = rig.forwardAxis;
	forwardAxis.z = 0.0f;
	if (glm::length(glm::vec2(forwardAxis.x, forwardAxis.y)) <= kEpsilon)
		forwardAxis = glm::vec3(0.0f, -1.0f, 0.0f);
	else
		forwardAxis = glm::normalize(forwardAxis);
	const glm::vec3 leftAxis(-forwardAxis.y, forwardAxis.x, 0.0f);
	return leftAxis * std::sin(m_QueryIntentDirection) +
		forwardAxis * std::cos(m_QueryIntentDirection);
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
		constexpr float velocityWindow = 1.0f / 30.0f;
		std::vector<glm::mat4> localBefore, modelBefore;
		std::vector<glm::mat4> localAfter, modelAfter;
		sampleUnwrappedModel(time + futureTime - velocityWindow, localBefore, modelBefore);
		sampleUnwrappedModel(time + futureTime + velocityWindow, localAfter, modelAfter);
		const glm::vec3 beforeRoot = ExtractTranslation(modelBefore[rig.trajectoryRoot]);
		const glm::vec3 afterRoot = ExtractTranslation(modelAfter[rig.trajectoryRoot]);
		const glm::vec3 velocityRoot = TransformVectorToRootSpace(
			rootModel0, (afterRoot - beforeRoot) / (2.0f * velocityWindow));
		f[offset++] = velocityRoot.x;
		f[offset++] = velocityRoot.y;
	}

	for (float futureTime : m_Settings.schema.futureTimes)
	{
		std::vector<glm::mat4> localFuture;
		std::vector<glm::mat4> modelFuture;
		sampleUnwrappedModel(time + futureTime, localFuture, modelFuture);

		// Facing and travel direction are independent PoseSearch channels. A
		// strafe clip can move left while continuing to face forward, while a
		// turn-in-place clip has no translation but changes facing substantially.
		const glm::vec3 futureForward = ExtractRootForward(modelFuture[rig.root], rig);
		const glm::vec3 relativeForward = TransformVectorToRootSpace(rootModel0, futureForward);
		const float facing = SignedPlanarAngleFromForward(relativeForward, rig.forwardAxis);
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
	const MotionMatchingResolvedRig& rig,
	const Vans::VansCharacterTrajectory* trajectory) const
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
	const float currentFacing = 0.0f;
	const float desiredFacing = glm::length(desiredVelXY) > kEpsilon
		? SignedPlanarAngleFromForward(desiredVelRoot, rig.forwardAxis)
		: currentFacing;
	const float blendHorizon = (std::max)(m_Settings.schema.futureTimes.back(), kEpsilon);
	const float response = (std::max)(m_Settings.trajectoryResponsiveness, kEpsilon);
	const glm::vec3 currentTrajectoryVelocity = m_CurrentTrajectoryVelocityRoot;

	auto trajectoryAt = [trajectory](float time)
	{
		Vans::VansCharacterTrajectorySample result;
		if (!trajectory || !trajectory->valid)
			return result;
		Vans::VansCharacterTrajectorySample previous;
		previous.positionWorld = trajectory->originWorld;
		previous.velocityWorld = trajectory->currentVelocityWorld;
		previous.facingYaw = trajectory->currentFacingYaw;
		for (const auto& next : trajectory->future)
		{
			if (time <= next.time)
			{
				const float span = (std::max)(next.time - previous.time, kEpsilon);
				const float alpha = glm::clamp((time - previous.time) / span, 0.0f, 1.0f);
				result.time = time;
				result.positionWorld = glm::mix(previous.positionWorld, next.positionWorld, alpha);
				result.velocityWorld = glm::mix(previous.velocityWorld, next.velocityWorld, alpha);
				result.facingYaw = LerpAngle(previous.facingYaw, next.facingYaw, alpha);
				return result;
			}
			previous = next;
		}
		return previous;
	};

	int offset = 0;
	for (float futureTime : m_Settings.schema.futureTimes)
	{
		glm::vec3 deltaRoot;
		if (trajectory && trajectory->valid)
		{
			const auto sample = trajectoryAt(futureTime);
			deltaRoot = Vans::WorldToAnimationPlanar(
				sample.positionWorld - trajectory->originWorld,
				trajectory->currentFacingYaw) *
				(std::max)(m_Settings.worldToAnimationScale, kEpsilon);
		}
		else
		{
			const float responseIntegral = (1.0f - std::exp(-response * futureTime)) / response;
			deltaRoot = desiredVelRoot * futureTime +
				(currentTrajectoryVelocity - desiredVelRoot) * responseIntegral;
		}
		f[offset++] = deltaRoot.x;
		f[offset++] = deltaRoot.y;
	}
	for (float futureTime : m_Settings.schema.futureTimes)
	{
		glm::vec3 velocityRoot;
		if (trajectory && trajectory->valid)
		{
			const auto sample = trajectoryAt(futureTime);
			velocityRoot = Vans::WorldToAnimationPlanar(
				sample.velocityWorld, trajectory->currentFacingYaw) *
				(std::max)(m_Settings.worldToAnimationScale, kEpsilon);
		}
		else
		{
			velocityRoot = desiredVelRoot +
				(currentTrajectoryVelocity - desiredVelRoot) * std::exp(-response * futureTime);
		}
		f[offset++] = velocityRoot.x;
		f[offset++] = velocityRoot.y;
	}
	for (float futureTime : m_Settings.schema.futureTimes)
	{
		float facing = 0.0f;
		if (trajectory && trajectory->valid)
		{
			const auto sample = trajectoryAt(futureTime);
			facing = glm::radians(std::remainder(
				sample.facingYaw - trajectory->currentFacingYaw, 360.0f));
		}
		else
		{
			const float t = futureTime / blendHorizon;
			facing = LerpAngle(currentFacing, desiredFacing, t);
		}
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
	const auto buildStarted = std::chrono::steady_clock::now();
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
	EnsureAutoDatabase(m_Settings, clips);
	if (m_Settings.databases.empty())
	{
		VANS_LOG_WARN("[MotionMatching] Cannot build database: no explicit PoseSearch databases configured.");
		return false;
	}

	int includedClipCount = 0;
	const float sampleStep = 1.0f / (std::max)(1.0f, m_Settings.sampleRate);
	for (int databaseIndex = 0; databaseIndex < static_cast<int>(m_Settings.databases.size()); ++databaseIndex)
	{
		const MotionMatchingDatabase& database = m_Settings.databases[databaseIndex];
		if (!database.enabled)
			continue;
		for (const MotionMatchingDatabaseClip& databaseClip : database.clips)
		{
			auto clipIt = clips.find(databaseClip.name);
			if (clipIt == clips.end() || clipIt->second.duration <= kEpsilon)
				continue;

			const VansAnimationClip& clip = clipIt->second;
			const float samplingStart = glm::clamp(databaseClip.samplingStart, 0.0f, clip.duration);
			const float minimumSamplingEnd = (std::min)(
				clip.duration, samplingStart + sampleStep);
			const float defaultSamplingEnd = databaseClip.loop
				? clip.duration
				: glm::clamp(
					clip.duration - (std::max)(0.0f, m_Settings.nonLoopSamplingEndMargin),
					minimumSamplingEnd,
					clip.duration);
			const float samplingEnd = databaseClip.samplingEnd > samplingStart
				? glm::clamp(databaseClip.samplingEnd, samplingStart, clip.duration)
				: defaultSamplingEnd;
			bool contributedSamples = false;
			for (float t = samplingStart; t < samplingEnd; t += sampleStep)
			{
				Sample sample;
				sample.clipName = databaseClip.name;
				sample.time = t;
				sample.rawFeature = ExtractDatabaseFeature(clip, t, databaseClip.loop, skeleton, m_Rig);
				sample.feature = sample.rawFeature;
				const float speedHorizon = m_Settings.schema.futureTimes[0];
				if (speedHorizon > kEpsilon)
					sample.trajectorySpeed = glm::length(glm::vec2(sample.rawFeature[0], sample.rawFeature[1])) / speedHorizon;
				const std::string phase = ToLower(databaseClip.phase.empty() ? database.phase : databaseClip.phase);
				sample.loopLike = databaseClip.loop;
				sample.idleLike = phase == "idle";
				sample.startLike = phase == "start";
				sample.stopLike = phase == "stop";
				sample.pivotLike = phase == "pivot";
				sample.turnLike = phase == "turn";
				sample.paceTransitionLike = phase == "transition";
				sample.transitionLike = sample.startLike || sample.stopLike ||
				                        sample.pivotLike || sample.turnLike || sample.paceTransitionLike;
				sample.sourceMoveState = databaseClip.sourceMoveState;
				sample.targetMoveState = databaseClip.targetMoveState;
				sample.sourceDirectionBucket = databaseClip.sourceDirectionBucket;
				sample.directionBucketFromName = databaseClip.directionBucket;
				sample.turnDirectionSign = databaseClip.turnDirectionSign;
				sample.turnBucketDelta = databaseClip.turnBucketDelta;
				sample.databaseIndex = databaseIndex;
				const int sampleIndex = static_cast<int>(m_Samples.size());
				m_Samples.push_back(sample);
				m_ClipSampleIndices[databaseClip.name].push_back(sampleIndex);
				contributedSamples = true;
			}
			if (contributedSamples)
			{
				++includedClipCount;
				BuildFootContactPhases(m_ClipSampleIndices[databaseClip.name]);
			}
		}
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
	const auto buildMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - buildStarted).count();
	VANS_LOG("[MotionMatching] Built database: samples=" << m_Samples.size()
	         << " clips=" << includedClipCount << " timeMs=" << buildMilliseconds);
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
	for (int d = kTrajectoryBegin; d < kTrajectoryPositionEnd; ++d)
	{
		const float diff = query[d] - candidate[d];
		outTrajectory += diff * diff * m_Settings.trajectoryPositionWeight;
	}
	for (int d = kTrajectoryVelocityBegin; d < kTrajectoryVelocityEnd; ++d)
	{
		const float diff = query[d] - candidate[d];
		outTrajectory += diff * diff * m_Settings.trajectoryVelocityWeight;
	}
	for (int d = kTrajectoryFacingBegin; d < kTrajectoryEnd; ++d)
	{
		const float diff = query[d] - candidate[d];
		outTrajectory += diff * diff * m_Settings.trajectoryFacingWeight;
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
	const std::unordered_map<std::string, AnimatorParameter>& parameters,
	bool forceFinishedTransitionExit) const
{
	if (!m_ActiveDatabaseIndices.empty() &&
	    std::find(m_ActiveDatabaseIndices.begin(), m_ActiveDatabaseIndices.end(), sample.databaseIndex) ==
	    m_ActiveDatabaseIndices.end())
		return false;

	const int moveState = ReadMoveStateParam(parameters);
	const float speed01 = m_QueryIntentSpeed01;
	const bool wantsIdle = speed01 < m_Settings.states.idleSpeedThreshold || moveState == m_Settings.states.idleState;
	const bool currentValid = m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size());
	const Sample* currentSample = currentValid ? &m_Samples[m_CurrentSample] : nullptr;
	const int currentMoveState = currentSample ? currentSample->targetMoveState : 0;
	const int desiredMoveState = m_EffectiveMoveState;
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
	const glm::vec3 desiredVelRoot = m_QueryDesiredVelocityRoot;
	const glm::vec3 intentDirectionRoot = BuildIntentDirectionRoot(m_Rig);
	const glm::vec2 desiredDir = glm::length(glm::vec2(
		desiredVelRoot.x, desiredVelRoot.y)) > kEpsilon
		? glm::vec2(intentDirectionRoot.x, intentDirectionRoot.y)
		: glm::vec2(0.0f);
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
		return delta <= glm::clamp(m_Settings.directionBucketTolerance, 0, 4);
	};
	auto sampleNameMatchesDesiredDirection = [&]() -> bool
	{
		if (sample.directionBucketFromName < 0 || desiredDirLen <= kEpsilon)
			return sampleMatchesDesiredDirection();
		return std::abs(SignedBucketDelta(directionBucket(desiredDir), sample.directionBucketFromName)) <=
			glm::clamp(m_Settings.directionBucketTolerance, 0, 4);
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
	// A completed one-shot must hand control back to a stable loop before any
	// further transition is considered. This guarantees forward playback and
	// prevents Pose Search from repeatedly selecting the final frame of the same
	// non-loop clip.
	if (forceFinishedTransitionExit)
		return sample.loopLike &&
		       sample.targetMoveState == desiredMoveState;

	if (sample.transitionLike)
	{
		if (sample.startLike)
			return startingFromIdle && sample.targetMoveState == desiredMoveState && sampleNameMatchesDesiredDirection();
		if (sample.stopLike)
			return stoppingToIdle &&
			       sample.sourceMoveState == currentMoveState &&
			       sample.targetMoveState == desiredMoveState &&
			       sampleNameMatchesCurrentDirection();
		if (sample.pivotLike)
			return (!currentSample || !currentSample->pivotLike) &&
			       m_PivotRequested &&
			       sample.sourceMoveState == currentMoveState &&
			       sample.targetMoveState == desiredMoveState &&
			       currentMoveState == desiredMoveState;
		if (sample.paceTransitionLike)
			return (changingPace || changingStance) &&
			       sample.sourceMoveState == currentMoveState &&
			       sample.targetMoveState == desiredMoveState;
		if (sample.turnLike)
		{
			if (!m_FacingTurnRequested)
				return false;
			if (sample.sourceMoveState != currentMoveState || sample.targetMoveState != desiredMoveState)
				return false;
			if (currentMoveState != desiredMoveState)
				return false;
			if (sample.turnBucketDelta != m_FacingTurnBucketDelta)
				return false;
			if (sample.turnDirectionSign != 0 &&
			    sample.turnDirectionSign != m_FacingTurnDirectionSign)
				return false;
			return true;
		}
		return false;
	}

	if (!sample.loopLike)
		return false;
	// A resolved facing request guarantees that an authored turn exists for the
	// current locomotion state. Keeping ordinary loops in the candidate set can
	// let their lower pose cost suppress the requested root-rotating animation.
	if (m_FacingTurnRequested)
		return false;

	if (startingFromIdle || stoppingToIdle || changingPace || changingStance)
		return false;
	// 普通移动方向由连续轨迹特征决定。方向桶只用于 Pivot 的起点/目标语义，
	// 不能再作为循环动作的硬门槛，否则镜头驱动的轨迹跨桶时会整组候选跳变。
	return sample.targetMoveState == desiredMoveState;
}

VansMotionMatchingRuntime::MatchResult VansMotionMatchingRuntime::FindBestMatch(
	const FeatureVector& query,
	const std::unordered_map<std::string, AnimatorParameter>& parameters,
	bool forceFinishedTransitionExit,
	bool allowReplayCurrentClip)
{
	MatchResult best;
	best.totalCost = std::numeric_limits<float>::max();
	m_DebugData.topCandidates.clear();

	for (int i = 0; i < static_cast<int>(m_Samples.size()); ++i)
	{
		const Sample& sample = m_Samples[i];
		if (!ShouldConsiderSampleForParameters(
			sample, parameters, forceFinishedTransitionExit))
			continue;

		// Continuing the active slice is evaluated explicitly from m_CurrentTime in
		// Update().  Searching another frame of the same clip would be a time
		// teleport, not continuation, and produces the characteristic loop jitter
		// when that frame wins again at the next search tick.
		if (!allowReplayCurrentClip &&
		    m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size()) &&
		    sample.clipName == m_Samples[m_CurrentSample].clipName)
		{
			continue;
		}

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
	                                   const Vans::VansCharacterTrajectory* trajectory,
	                                   VansPosePayload& outPayload)
{
	m_DebugData.enabled = m_Settings.enabled;
	m_DebugData.usedThisFrame = false;
	m_DebugData.queryFacingDeltaDegrees = 0.0f;
	m_DebugData.currentFacingYawDegrees = 0.0f;
	m_DebugData.desiredFacingYawDegrees = 0.0f;
	m_DebugData.desiredFacingYawRateDegreesPerSecond = 0.0f;
	m_DebugData.facingTurnState = "Unavailable";
	m_DebugData.facingTurnGateReason = "MissingTrajectory";
	m_DebugData.facingTurnRequested = false;
	m_DebugData.facingTurnDirectionSign = 0;
	m_DebugData.facingTurnBucketDelta = 0;
	m_DebugData.steeringTargetFacingDeltaDegrees = 0.0f;
	m_DebugData.steeringAuthoredFacingDeltaDegrees = 0.0f;
	m_DebugData.steeringRequestedCorrectionDegrees = 0.0f;
	m_DebugData.steeringAppliedCorrectionDegrees = 0.0f;
	m_DebugData.steeringAppliedYawRateDegreesPerSecond = 0.0f;
	m_DebugData.steeringActive = false;
	m_DebugData.steeringLimited = false;
	m_DebugData.rootMotionReconciliationActive = false;
	m_DebugData.rootMotionTargetVelocityWorld = glm::vec3(0.0f);
	m_DebugData.rootMotionReconciledVelocityWorld = glm::vec3(0.0f);
	m_DebugData.rootMotionTargetYawRateDegreesPerSecond = 0.0f;
	m_DebugData.rootMotionReconciledYawRateDegreesPerSecond = 0.0f;
	m_DebugData.authoredRootYawDeltaDegrees = 0.0f;
	m_DebugData.appliedRootYawDeltaDegrees = 0.0f;
	m_DebugData.appliedRootMotionVelocityWorld = glm::vec3(0.0f);
	m_PrefersRootMotionThisFrame = false;
	if (!m_Settings.enabled)
		return false;

	m_DebugData.querySpeed = trajectory && trajectory->valid
		? glm::length(glm::vec2(trajectory->currentVelocityWorld.x,
			trajectory->currentVelocityWorld.z)) *
			(std::max)(m_Settings.worldToAnimationScale, kEpsilon)
		: ReadSpeedParam(parameters) * m_Settings.desiredSpeedScale;
	m_DebugData.queryDirection = ReadDirectionParam(parameters);
	const bool parameterWantsMotionMatching = ReadBoolParam(parameters, m_Settings.parameters.enabled, true);
	if (!parameterWantsMotionMatching)
	{
		m_HasLastSearchContext = false;
		m_FacingTurnRequested = false;
		m_FacingTurnDirectionSign = 0;
		m_FacingTurnBucketDelta = 0;
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
	const std::string playbackClipName = activeSample->clipName;
	const bool playbackLoopLike = activeSample->loopLike;
	const bool playbackWasTurnLike = activeSample->turnLike;

	float wantedPlaybackRate = 1.0f;
	if (m_Settings.enableSpeedMatching)
	{
		float desiredSpeed = glm::length(glm::vec2(
			BuildDesiredVelocityRoot(parameters, m_Rig)));
		if (trajectory && trajectory->valid)
		{
			// 播放率跟随运动计划；碰撞时再按 CCT 实际消费比例回落到真实速度。
			// 直接使用上一帧实际速度会形成“动画慢 -> Root Motion 慢 -> 动画更慢”
			// 的闭环，表现为持续滑步和方向改变后的响应滞后。
			const glm::vec2 actualVelocity(
				trajectory->currentVelocityWorld.x,
				trajectory->currentVelocityWorld.z);
			const glm::vec2 plannedVelocity(
				trajectory->plannedVelocityWorld.x,
				trajectory->plannedVelocityWorld.z);
			const float consumption = glm::clamp(
				trajectory->motionConsumptionRatio, 0.0f, 1.0f);
			const glm::vec2 consumedPlan = glm::mix(
				actualVelocity, plannedVelocity, consumption);
			desiredSpeed = glm::length(consumedPlan) *
				(std::max)(m_Settings.worldToAnimationScale, kEpsilon);
		}
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
	// Keep the interval end unwrapped for payload sampling.  Pose time is
	// wrapped, but Root Motion/events need the monotonic time to integrate the
	// authored loop seam in the forward direction.
	const float outgoingPlaybackTime = activeSample->loopLike
		? previousPlaybackTime + playbackDelta : m_CurrentTime;
	bool switchedThisFrame = false;
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
	if (trajectory && trajectory->valid)
	{
		m_CurrentTrajectoryVelocityRoot = Vans::WorldToAnimationPlanar(
			trajectory->currentVelocityWorld, trajectory->currentFacingYaw) *
			(std::max)(m_Settings.worldToAnimationScale, kEpsilon);
	}
	else
		m_CurrentTrajectoryVelocityRoot = glm::mix(
			m_CurrentTrajectoryVelocityRoot, sampledTrajectoryVelocity, trajectoryAlpha);
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
	const float transitionCompletionWindow =
		2.0f / (std::max)(1.0f, m_Settings.sampleRate);
	const bool activeTransitionComplete =
		!activeSample->loopLike &&
		m_CurrentTime >= (std::max)(
			0.0f, activeClipIt->second.duration - transitionCompletionWindow);
	const bool activeFacingTurnInProgress =
		activeSample->turnLike && !activeTransitionComplete;
	const bool activePivotInProgress =
		activeSample->pivotLike && !activeTransitionComplete;

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

	FeatureVector query = BuildQueryFeature(
		parameters, queryLocal, skeleton, m_Rig, trajectory);
	m_QueryIntentSpeed01 = ReadSpeedParam(parameters);
	m_QueryIntentDirection = ReadDirectionParam(parameters);
	m_QueryDesiredVelocityRoot = BuildDesiredVelocityRoot(parameters, m_Rig);
	if (trajectory && trajectory->valid)
	{
		const glm::vec3 desiredLocalVelocity = Vans::WorldToLocomotionLocalPlanar(
			trajectory->desiredVelocityWorld, trajectory->currentFacingYaw);
		m_QueryDesiredVelocityRoot = Vans::EngineLocalToAnimationPlanar(desiredLocalVelocity) *
			(std::max)(m_Settings.worldToAnimationScale, kEpsilon);
		const float desiredAnimationSpeed = glm::length(glm::vec2(
			m_QueryDesiredVelocityRoot.x, m_QueryDesiredVelocityRoot.y));
		m_QueryIntentSpeed01 = glm::clamp(desiredAnimationSpeed /
			(std::max)(m_Settings.desiredSpeedScale, kEpsilon), 0.0f, 1.0f);
		// 方向语义来自玩家在移动参考系中的输入，而不是已经被相机旋转后的
		// 世界速度。持续按 W 转动相机应改变未来世界轨迹，但仍属于 Forward
		// 数据库方向；否则每个相机采样都会触发一次方向桶切换。
		if (glm::length(trajectory->moveInputLocal) > kEpsilon)
			m_QueryIntentDirection = std::atan2(
				trajectory->moveInputLocal.x, trajectory->moveInputLocal.y);
	}
	const float speed01 = m_QueryIntentSpeed01;
	const float direction = m_QueryIntentDirection;
	const int moveState = ReadMoveStateParam(parameters);
	const bool isCrouching = ReadCrouchingParam(parameters);
	const bool isAirborne = trajectory && trajectory->valid && trajectory->hasGrounding
		? !trajectory->grounded
		: ReadAirborneParam(parameters);
	const bool isMoving = speed01 >= m_Settings.states.idleSpeedThreshold;
	m_RequestedMoveState = ResolveDesiredMoveState(parameters);
	m_EffectiveMoveState = ResolveDirectionalFallbackMoveState(m_RequestedMoveState);
	m_DirectionalStateFallback = m_EffectiveMoveState != m_RequestedMoveState;
	const int desiredMoveState = m_EffectiveMoveState;
	m_PivotDatabaseAvailable = HasPivotDatabaseForState(desiredMoveState);
	m_UrgentDirectionChange = false;
	// Pivot 来自预测轨迹的速度过零点，而不是单帧输入边沿。相机连续转动会
	// 改变未来轨迹，但只有真实换向需要 Pivot；同时请求可在片段中途退出，
	// 避免 3~6 秒的 Pivot 素材变成互斥状态锁。
	if (m_PivotDatabaseAvailable && trajectory && trajectory->valid &&
	    trajectory->hasDirectionChange && !isAirborne && isMoving)
	{
		const float actualSpeed = glm::length(glm::vec2(
			trajectory->currentVelocityWorld.x, trajectory->currentVelocityWorld.z));
		const float enterAngle = glm::clamp(m_Settings.pivotEnterAngleDegrees, 0.0f, 180.0f);
		const float exitAngle = glm::clamp(m_Settings.pivotExitAngleDegrees, 0.0f, enterAngle);
		const bool predictedPivotIsRelevant =
			trajectory->hasPredictedPivot &&
			trajectory->predictedPivotTime <=
				(std::max)(0.0f, m_Settings.pivotPredictionLeadTime);
		if (!m_PivotRequested)
			m_PivotRequested = actualSpeed >= m_Settings.pivotMinSpeed &&
				predictedPivotIsRelevant &&
				trajectory->directionChangeDegrees >= enterAngle;
		const bool pivotMinimumPlaybackSatisfied =
			!activeSample->pivotLike ||
			m_CurrentTime >= (std::max)(0.0f, m_Settings.pivotMinimumPlaybackTime);
		if (m_PivotRequested && pivotMinimumPlaybackSatisfied &&
			(actualSpeed < m_Settings.pivotMinSpeed ||
			 !predictedPivotIsRelevant ||
			 trajectory->directionChangeDegrees <= exitAngle))
			m_PivotRequested = false;
		m_UrgentDirectionChange = m_PivotRequested &&
			trajectory->predictedPivotTime <=
				(std::max)(0.0f, m_Settings.pivotUrgentPredictionTime) &&
			trajectory->directionChangeDegrees >=
				m_Settings.urgentDirectionChangeDegrees;
	}
	else if (!activeSample->pivotLike ||
		m_CurrentTime >= (std::max)(0.0f, m_Settings.pivotMinimumPlaybackTime))
	{
		m_PivotRequested = false;
	}
	m_QueryFacingDeltaDegrees = 0.0f;
	if (trajectory && trajectory->valid && trajectory->hasFacing && !isAirborne)
	{
		m_QueryFacingDeltaDegrees = std::remainder(
			trajectory->desiredFacingYaw - trajectory->currentFacingYaw, 360.0f);
		const float enterThreshold = (std::max)(
			0.0f, m_Settings.facingTurnEnterThresholdDegrees);
		const float exitThreshold = glm::clamp(
			m_Settings.facingTurnExitThresholdDegrees, 0.0f, enterThreshold);
		const float exitYawRate = (std::max)(
			0.0f, m_Settings.facingTurnExitYawRateDegreesPerSecond);
		const float absoluteFacingError = std::abs(m_QueryFacingDeltaDegrees);
		const float farFutureError = std::remainder(
			trajectory->future.back().facingYaw - trajectory->currentFacingYaw, 360.0f);
		// Moving turns use angular-velocity look-ahead. Turn-in-place selects its arc
		// from the instantaneous error, but may finish an already playing one-shot
		// while the camera continues in the same direction. The completed clip still
		// exits through a loop; angular velocity never authorizes a time teleport.
		const bool useMovingFacingPolicy = isMoving;
		const bool cameraFacingStillMoving =
			std::abs(trajectory->desiredFacingYawRate) > exitYawRate;
		const int cameraFacingDirectionSign = trajectory->desiredFacingYawRate >= 0.0f ? 1 : -1;
		const bool cameraContinuesActiveIdleTurn =
			!useMovingFacingPolicy &&
			activeFacingTurnInProgress &&
			cameraFacingStillMoving &&
			activeSample->turnDirectionSign == cameraFacingDirectionSign;
		// 移动中由未来轨迹搜索负责选择步态，再由 Root Motion Steering 连续
		// 修正剩余朝向误差。离散 Turn 只用于停步状态，否则镜头每次采样都会
		// 使 Turn one-shot 与 Move loop 互相争抢。
		const bool shouldEnterFacingTurn =
			!useMovingFacingPolicy && absoluteFacingError >= enterThreshold;
		if (useMovingFacingPolicy)
			m_FacingTurnRequested = false;

		if (!m_FacingTurnRequested)
			m_FacingTurnRequested = shouldEnterFacingTurn;
		else if (useMovingFacingPolicy &&
			absoluteFacingError <= exitThreshold && !cameraFacingStillMoving)
			m_FacingTurnRequested = false;
		else if (!useMovingFacingPolicy && activeFacingTurnInProgress &&
			absoluteFacingError <= exitThreshold && !cameraContinuesActiveIdleTurn)
			m_FacingTurnRequested = false;
		else if (!useMovingFacingPolicy && !activeFacingTurnInProgress &&
			absoluteFacingError < enterThreshold)
			m_FacingTurnRequested = false;

		if (m_FacingTurnRequested)
		{
			const int desiredMoveState = m_EffectiveMoveState;
			float directionSignal = useMovingFacingPolicy
				? farFutureError : m_QueryFacingDeltaDegrees;
			// At the +/-180 degree wrap boundary the shortest signed error changes
			// sign even though the camera is still rotating continuously. Prefer the
			// measured view yaw rate there so left/right Turn clips do not ping-pong.
			if (!useMovingFacingPolicy && absoluteFacingError >= 170.0f &&
				cameraFacingStillMoving)
			{
				directionSignal = trajectory->desiredFacingYawRate;
			}
			if (std::abs(directionSignal) <= exitThreshold && cameraFacingStillMoving)
				directionSignal = trajectory->desiredFacingYawRate;
			if (std::abs(directionSignal) <= kEpsilon)
				directionSignal = m_QueryFacingDeltaDegrees;
			int directionSign = directionSignal >= 0.0f ? 1 : -1;
			const bool keepActiveTurnSelection =
				activeFacingTurnInProgress &&
				(activeSample->turnDirectionSign == directionSign ||
				 absoluteFacingError < enterThreshold);
			if (keepActiveTurnSelection)
				directionSign = activeSample->turnDirectionSign;
			const float requestedTurnArc = glm::clamp(
				useMovingFacingPolicy
					? (std::max)(absoluteFacingError, std::abs(farFutureError))
					: absoluteFacingError,
				0.0f,
				180.0f);
			const int turnBucket = keepActiveTurnSelection
				? activeSample->turnBucketDelta
				: ResolveFacingTurnBucket(desiredMoveState, directionSign, requestedTurnArc);
			if (turnBucket > 0)
			{
				m_FacingTurnDirectionSign = directionSign;
				m_FacingTurnBucketDelta = turnBucket;
			}
			else
				m_FacingTurnRequested = false;
		}
	}
	else
		m_FacingTurnRequested = false;
	// 大角度速度换向优先保证行进方向；相机朝向仍保留在轨迹中，待 Pivot/换向
	// 稳定后继续由 Facing Turn 匹配，避免 Turn 数据库排除所有目标方向循环动作。
	if (m_UrgentDirectionChange)
		m_FacingTurnRequested = false;
	if (!m_FacingTurnRequested)
	{
		m_FacingTurnDirectionSign = 0;
		m_FacingTurnBucketDelta = 0;
	}
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kTwoPi = kPi * 2.0f;
	float wrappedDirection = std::fmod(direction, kTwoPi);
	if (wrappedDirection < 0.0f)
		wrappedDirection += kTwoPi;
	const int directionBucket = static_cast<int>((wrappedDirection + kPi * 0.125f) / (kPi * 0.25f)) & 7;
	const bool directionChanged =
		m_HasLastSearchContext &&
		m_LastDirectionBucket != directionBucket;
	const bool locomotionDirectionContextChanged =
		directionChanged && !m_FacingTurnRequested;
	const bool facingTurnContextChanged =
		!m_HasLastSearchContext ||
		m_LastFacingTurnRequested != m_FacingTurnRequested ||
		(m_FacingTurnRequested &&
		 (m_LastFacingTurnDirectionSign != m_FacingTurnDirectionSign ||
		  m_LastFacingTurnBucketDelta != m_FacingTurnBucketDelta));
	const bool pivotContextChanged =
		!m_HasLastSearchContext || m_LastPivotRequested != m_PivotRequested;
	const bool searchContextChanged =
		!m_HasLastSearchContext ||
		m_LastMoveState != moveState ||
		locomotionDirectionContextChanged ||
		pivotContextChanged ||
		facingTurnContextChanged ||
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
	m_LastPivotRequested = m_PivotRequested;
	m_LastFacingTurnRequested = m_FacingTurnRequested;
	m_LastFacingTurnDirectionSign = m_FacingTurnDirectionSign;
	m_LastFacingTurnBucketDelta = m_FacingTurnBucketDelta;
	const bool continueCompletedFacingTurn =
		activeTransitionComplete && activeSample->turnLike && m_FacingTurnRequested;
	const bool forceFinishedTransitionExit =
		activeTransitionComplete && !continueCompletedFacingTurn;
	ResolveActiveDatabases(parameters, forceFinishedTransitionExit);
	m_DebugData.activeDatabases.clear();
	for (const int databaseIndex : m_ActiveDatabaseIndices)
	{
		if (databaseIndex >= 0 && databaseIndex < static_cast<int>(m_Settings.databases.size()))
			m_DebugData.activeDatabases.push_back(m_Settings.databases[databaseIndex].name);
	}
	m_DebugData.querySpeed = speed01 * m_Settings.desiredSpeedScale;
	m_DebugData.queryDirection = direction;
	m_DebugData.queryFacingDeltaDegrees = m_QueryFacingDeltaDegrees;
	if (trajectory && trajectory->valid)
	{
		m_DebugData.currentFacingYawDegrees = trajectory->currentFacingYaw;
		m_DebugData.desiredFacingYawDegrees = trajectory->desiredFacingYaw;
		m_DebugData.desiredFacingYawRateDegreesPerSecond = trajectory->desiredFacingYawRate;
		if (!trajectory->hasFacing)
		{
			m_DebugData.facingTurnState = "Unavailable";
			m_DebugData.facingTurnGateReason = "MissingFacingIntent";
		}
		else if (isAirborne)
		{
			m_DebugData.facingTurnState = "Unavailable";
			m_DebugData.facingTurnGateReason = "Airborne";
		}
		else if (isMoving)
		{
			m_DebugData.facingTurnState = "MovingSteering";
			m_DebugData.facingTurnGateReason = "Moving";
		}
		else if (m_FacingTurnRequested)
		{
			m_DebugData.facingTurnState = continueCompletedFacingTurn ? "Replanning" : "Turning";
			m_DebugData.facingTurnGateReason = "None";
		}
		else
		{
			m_DebugData.facingTurnState = "Aligned";
			m_DebugData.facingTurnGateReason = "BelowThreshold";
		}
	}
	m_DebugData.pivotRequested = m_PivotRequested;
	m_DebugData.pivotDatabaseAvailable = m_PivotDatabaseAvailable;
	m_DebugData.urgentDirectionChange = m_UrgentDirectionChange;
	m_DebugData.requestedMoveState = m_RequestedMoveState;
	m_DebugData.effectiveMoveState = m_EffectiveMoveState;
	m_DebugData.directionalStateFallback = m_DirectionalStateFallback;
	if (trajectory && trajectory->valid)
	{
		m_DebugData.trajectoryOriginWorld = trajectory->originWorld;
		m_DebugData.actualVelocityWorld = trajectory->currentVelocityWorld;
		m_DebugData.plannedVelocityWorld = trajectory->plannedVelocityWorld;
		m_DebugData.desiredVelocityWorld = trajectory->desiredVelocityWorld;
		m_DebugData.moveInputLocal = trajectory->moveInputLocal;
		m_DebugData.movementReferenceYaw = trajectory->movementReferenceYaw;
		m_DebugData.movementReferenceYawRate = trajectory->movementReferenceYawRate;
		m_DebugData.plannedFacingYaw = trajectory->plannedFacingYaw;
		m_DebugData.trajectoryHistory = trajectory->history;
		m_DebugData.trajectoryFuture = trajectory->future;
		m_DebugData.directionChangeDegrees = trajectory->directionChangeDegrees;
		m_DebugData.inputDirectionChangeDegrees = trajectory->inputDirectionChangeDegrees;
		m_DebugData.predictedPivotPositionWorld = trajectory->predictedPivotPositionWorld;
		m_DebugData.predictedPivotTime = trajectory->predictedPivotTime;
		m_DebugData.hasPredictedPivot = trajectory->hasPredictedPivot;
		m_DebugData.motionConsumptionRatio = trajectory->motionConsumptionRatio;
		m_DebugData.movementBlocked = trajectory->movementBlocked;
	}
	else
	{
		m_DebugData.trajectoryOriginWorld = glm::vec3(0.0f);
		m_DebugData.actualVelocityWorld = glm::vec3(0.0f);
		m_DebugData.plannedVelocityWorld = glm::vec3(0.0f);
		m_DebugData.desiredVelocityWorld = glm::vec3(0.0f);
		m_DebugData.moveInputLocal = glm::vec2(0.0f);
		m_DebugData.movementReferenceYaw = 0.0f;
		m_DebugData.movementReferenceYawRate = 0.0f;
		m_DebugData.plannedFacingYaw = 0.0f;
		m_DebugData.directionChangeDegrees = 0.0f;
		m_DebugData.inputDirectionChangeDegrees = 0.0f;
		m_DebugData.predictedPivotTime = 0.0f;
		m_DebugData.hasPredictedPivot = false;
		m_DebugData.motionConsumptionRatio = 1.0f;
		m_DebugData.movementBlocked = false;
	}
	m_DebugData.facingTurnRequested = m_FacingTurnRequested;
	m_DebugData.facingTurnDirectionSign = m_FacingTurnDirectionSign;
	m_DebugData.facingTurnBucketDelta = m_FacingTurnBucketDelta;
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
		MatchResult best = FindBestMatch(
			query, parameters, forceFinishedTransitionExit, continueCompletedFacingTurn);
		const bool bestIsTargetLoop =
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].loopLike;
		const bool shouldExitFinishedTransition = activeTransitionComplete && bestIsTargetLoop;
		const bool activeMovingSample = activeSample && IsMovingPlaybackSample(*activeSample);
		const bool shouldEnterStartTransition =
			searchContextChanged &&
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].startLike &&
			isMoving &&
			!activeMovingSample;
		const bool bestIsFacingTurn =
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].turnLike &&
			m_FacingTurnRequested;
		const bool activeMatchesFacingTurn =
			activeSample->turnLike && !activeTransitionComplete &&
			activeSample->turnDirectionSign == m_FacingTurnDirectionSign &&
			activeSample->turnBucketDelta == m_FacingTurnBucketDelta;
		const bool shouldEnterFacingTurn =
			bestIsFacingTurn &&
			!activeMatchesFacingTurn &&
			(facingTurnContextChanged || activeSample->loopLike || activeTransitionComplete);
		const bool shouldEnterPivot =
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].pivotLike &&
			m_PivotRequested &&
			!activePivotInProgress;
		const bool bestIsContextTransition =
			best.sampleIndex >= 0 &&
			m_Samples[best.sampleIndex].transitionLike &&
			(m_Samples[best.sampleIndex].startLike ||
			 m_Samples[best.sampleIndex].stopLike ||
			 m_Samples[best.sampleIndex].pivotLike ||
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
			 m_Samples[best.sampleIndex].pivotLike != m_Samples[m_CurrentSample].pivotLike ||
			 m_Samples[best.sampleIndex].idleLike != m_Samples[m_CurrentSample].idleLike ||
			 m_Samples[best.sampleIndex].loopLike != m_Samples[m_CurrentSample].loopLike);
		const bool shouldEnterSelectedContextTransition =
			shouldEnterStartTransition ||
			(searchContextChanged &&
			 contextTransitionHasValidSource &&
			 (m_Samples[best.sampleIndex].targetMoveState != m_Samples[m_CurrentSample].targetMoveState ||
			  contextTransitionChangesRole));
		const bool shouldEnterContextTransition =
			shouldEnterSelectedContextTransition ||
			shouldEnterPivot ||
			shouldEnterFacingTurn;
		// Search context changes may request a new search immediately, but they do
		// not all authorize an immediate clip switch. Only true state transitions
		// and the first entry into a facing turn can bypass the dwell interval.
		const bool shouldBypassSwitchInterval =
			shouldExitFinishedTransition ||
			shouldEnterStartTransition ||
			shouldEnterPivot ||
			(shouldEnterFacingTurn && (!activeSample->turnLike || activeTransitionComplete)) ||
			m_CurrentSample < 0;
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
			 (m_UrgentDirectionChange && !activePivotInProgress) ||
			 shouldEnterContextTransition ||
			 shouldExitFinishedTransition ||
			 m_CurrentSample < 0);
		const bool canInterruptBlend =
			!m_Blending ||
			(m_UrgentDirectionChange && !activePivotInProgress) ||
			shouldEnterContextTransition ||
			shouldExitFinishedTransition ||
			m_BlendElapsed >= m_Settings.inertializationMaxDuration *
				glm::clamp(m_Settings.blendInterruptFraction, 0.0f, 1.0f);
		const bool switchIntervalReady =
			shouldBypassSwitchInterval ||
			m_TimeSinceSwitch >= m_Settings.minSwitchInterval;
		const bool canSwitchNow =
			best.sampleIndex >= 0 &&
			improvesEnough &&
			canInterruptBlend &&
			switchIntervalReady;
		if (best.sampleIndex >= 0 &&
		    canSwitchNow)
		{
			switchedThisFrame = true;
			const float sourceLeftContact = m_LeftFootPlantWeight;
			const float sourceRightContact = m_RightFootPlantWeight;
			m_CurrentSample = best.sampleIndex;
			m_CurrentTime = m_Samples[m_CurrentSample].time;
			m_CurrentCost = best.totalCost;
			m_TimeSinceSwitch = 0.0f;
			if (shouldExitFinishedTransition && m_FacingTurnRequested)
				m_TimeSinceSearch = m_Settings.searchThrottle;
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
			const Sample& selectedSample = m_Samples[best.sampleIndex];
			m_DebugData.selectedClip = selectedSample.clipName;
			m_DebugData.selectedTime = selectedSample.time;
			const glm::vec3 selectedCandidateVelocityAnimation(
				selectedSample.rawFeature[kTrajectoryVelocityBegin],
				selectedSample.rawFeature[kTrajectoryVelocityBegin + 1],
				0.0f);
			m_DebugData.selectedCandidateVelocityWorld = Vans::AnimationToWorldPlanar(
				selectedCandidateVelocityAnimation /
					(std::max)(m_Settings.worldToAnimationScale, kEpsilon),
				trajectory && trajectory->valid ? trajectory->currentFacingYaw : 0.0f);
		}
		else
		{
			m_DebugData.selectedClip = activeSample->clipName;
			m_DebugData.selectedTime = m_CurrentTime;
			m_DebugData.selectedCandidateVelocityWorld = glm::vec3(0.0f);
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
	std::vector<glm::mat4> outputLocalTransforms;
	ApplyInertialization(deltaTime, target, outputLocalTransforms);

	VansAnimationSampleRequest request;
	// The pose switch happens after this frame's source clip was advanced.  A
	// destination interval would either be zero or run backwards when matching
	// an earlier point in the same clip, so movement for the switch frame must
	// come from the outgoing playback interval.
	request.previousTime = switchedThisFrame ? m_CurrentTime : previousPlaybackTime;
	request.currentTime = switchedThisFrame ? m_CurrentTime : outgoingPlaybackTime;
	request.startTime = 0.0f;
	request.endTime = activeClipIt->second.duration;
	request.loop = activeSample->loopLike;
	request.rootMotionBoneIndex = m_Rig.trajectoryRoot;
	request.sourceNodeId = VansAnimationStableId("MotionMatching");
	if (!VansAnimationSampler::Sample(activeClipIt->second, skeleton, request, outPayload) ||
	    !VansPoseMath::FromMatrices(outputLocalTransforms, outPayload.localPose))
		return false;
	if (switchedThisFrame)
	{
		const auto outgoingClipIt = clips.find(playbackClipName);
		if (outgoingClipIt != clips.end())
		{
			VansAnimationSampleRequest outgoingRequest;
			outgoingRequest.previousTime = previousPlaybackTime;
			outgoingRequest.currentTime = outgoingPlaybackTime;
			outgoingRequest.startTime = 0.0f;
			outgoingRequest.endTime = outgoingClipIt->second.duration;
			outgoingRequest.loop = playbackLoopLike;
			outgoingRequest.rootMotionBoneIndex = m_Rig.trajectoryRoot;
			outgoingRequest.sourceNodeId = request.sourceNodeId;
			VansPosePayload outgoingPayload;
			if (VansAnimationSampler::Sample(
				outgoingClipIt->second, skeleton, outgoingRequest, outgoingPayload) &&
			    outgoingPayload.rootMotion.valid)
			{
				outPayload.rootMotion = outgoingPayload.rootMotion;
			}
		}
	}
	if (outPayload.rootMotion.valid && deltaTime > kEpsilon)
	{
		const float outgoingYawRate = glm::degrees(
			glm::eulerAngles(outPayload.rootMotion.rotation)).z / deltaTime;
		m_DebugData.authoredRootYawDeltaDegrees = outgoingYawRate * deltaTime;
		if (switchedThisFrame)
		{
			// 切换帧仍消费源片段的完整 Root Motion 区间；从下一帧开始把
			// 该线/角速度连续地收敛到目标片段，而不是修改 CCT Transform。
			// Turn-in-place Root Rotation is the authoritative CCT rotation. Angular
			// velocity reconciliation changes its integral and leaves a residual
			// facing error, so turn boundaries rely on pose inertialization instead.
			if (playbackWasTurnLike || activeSample->turnLike)
				m_RootMotionReconciler.Reset();
			else
				m_RootMotionReconciler.RequestTransition(
					outPayload.rootMotion.translation / deltaTime,
					outgoingYawRate);
		}
		else
		{
			const RootMotionReconciliationResult reconciliation =
				m_RootMotionReconciler.Apply(
					deltaTime,
					outPayload.rootMotion.translation,
					outPayload.rootMotion.rotation);
			m_DebugData.rootMotionReconciliationActive = reconciliation.active;
			m_DebugData.rootMotionTargetYawRateDegreesPerSecond =
				reconciliation.targetYawRateDegreesPerSecond;
			m_DebugData.rootMotionReconciledYawRateDegreesPerSecond =
				reconciliation.appliedYawRateDegreesPerSecond;
			const float rootVelocityToWorld =
				m_Settings.motionModel.rootMotionToWorldScale;
			const float facingYaw = trajectory && trajectory->valid
				? trajectory->currentFacingYaw : 0.0f;
			m_DebugData.rootMotionTargetVelocityWorld = Vans::AnimationToWorldPlanar(
				reconciliation.targetVelocityAnimation * rootVelocityToWorld,
				facingYaw);
			m_DebugData.rootMotionReconciledVelocityWorld = Vans::AnimationToWorldPlanar(
				reconciliation.appliedVelocityAnimation * rootVelocityToWorld,
				facingYaw);
		}
	}
	if (outPayload.rootMotion.valid && trajectory && trajectory->valid &&
		isMoving && !switchedThisFrame)
	{
		const float predictionTime = (std::max)(
			m_Settings.steering.predictionTime, 0.0001f);
		float targetFacingYaw = trajectory->currentFacingYaw;
		float previousFacingYaw = trajectory->currentFacingYaw;
		float previousTime = 0.0f;
		for (const auto& future : trajectory->future)
		{
			if (predictionTime <= future.time)
			{
				const float span = (std::max)(future.time - previousTime, 0.0001f);
				const float alpha = glm::clamp(
					(predictionTime - previousTime) / span, 0.0f, 1.0f);
				targetFacingYaw = previousFacingYaw + std::remainder(
					future.facingYaw - previousFacingYaw, 360.0f) * alpha;
				break;
			}
			targetFacingYaw = future.facingYaw;
			previousFacingYaw = future.facingYaw;
			previousTime = future.time;
		}

		float authoredFutureYaw = 0.0f;
		VansAnimationSampleRequest steeringRequest;
		steeringRequest.previousTime = m_CurrentTime;
		steeringRequest.currentTime = m_CurrentTime +
			predictionTime * m_CurrentPlaybackRate;
		steeringRequest.startTime = 0.0f;
		steeringRequest.endTime = activeClipIt->second.duration;
		steeringRequest.loop = activeSample->loopLike;
		steeringRequest.rootMotionBoneIndex = m_Rig.trajectoryRoot;
		steeringRequest.sourceNodeId = request.sourceNodeId;
		VansPosePayload steeringPayload;
		if (VansAnimationSampler::Sample(
			activeClipIt->second, skeleton, steeringRequest, steeringPayload) &&
			steeringPayload.rootMotion.valid)
		{
			authoredFutureYaw = glm::degrees(
				glm::eulerAngles(steeringPayload.rootMotion.rotation)).z;
		}
		const float plannedMovementSpeed = glm::length(glm::vec2(
			trajectory->plannedVelocityWorld.x,
			trajectory->plannedVelocityWorld.z));
		const float desiredMovementSpeed = glm::length(glm::vec2(
			trajectory->desiredVelocityWorld.x,
			trajectory->desiredVelocityWorld.z));
		const float movementSpeed = (std::max)(
			plannedMovementSpeed, desiredMovementSpeed);
		const RootMotionSteeringResult steering = m_RootMotionSteering.Apply(
			deltaTime,
			movementSpeed,
			trajectory->currentFacingYaw,
			targetFacingYaw,
			authoredFutureYaw,
			outPayload.rootMotion.rotation);
		m_DebugData.steeringTargetFacingDeltaDegrees =
			steering.targetFacingDeltaDegrees;
		m_DebugData.steeringAuthoredFacingDeltaDegrees =
			steering.authoredFacingDeltaDegrees;
		m_DebugData.steeringRequestedCorrectionDegrees =
			steering.requestedCorrectionDegrees;
		m_DebugData.steeringAppliedCorrectionDegrees =
			steering.appliedCorrectionDegrees;
		m_DebugData.steeringAppliedYawRateDegreesPerSecond =
			steering.appliedYawRateDegreesPerSecond;
		m_DebugData.steeringActive = steering.active;
		m_DebugData.steeringLimited = steering.limited;
	}
	else if (!isMoving)
	{
		m_RootMotionSteering.Reset();
	}
	if (outPayload.rootMotion.valid)
	{
		m_DebugData.appliedRootYawDeltaDegrees = glm::degrees(
			glm::eulerAngles(outPayload.rootMotion.rotation)).z;
	}
	outPayload.valid = outPayload.localPose.size() == skeleton.bones.size();
	m_PrefersRootMotionThisFrame = activeSample->transitionLike || activeSample->turnLike;
	m_PreviousOutputLocalPose = m_LastOutputLocalPose;
	m_LastOutputLocalPose = outputLocalTransforms;

	m_DebugData.usedThisFrame = true;
	m_DebugData.databaseReady = true;
	m_DebugData.rigReady = m_Rig.IsValid();
	m_DebugData.sampleCount = static_cast<int>(m_Samples.size());
	m_DebugData.switches = m_SwitchCount;
	m_DebugData.activeClip = activeSample->clipName;
	m_DebugData.activeTime = m_CurrentTime;
	const glm::vec3 selectedVelocityAnimation(
		activeSample->rawFeature[kTrajectoryVelocityBegin],
		activeSample->rawFeature[kTrajectoryVelocityBegin + 1],
		0.0f);
	const float animationToWorldScale = 1.0f /
		(std::max)(m_Settings.worldToAnimationScale, kEpsilon);
	m_DebugData.activeClipVelocityWorld = Vans::AnimationToWorldPlanar(
		selectedVelocityAnimation * animationToWorldScale,
		trajectory && trajectory->valid ? trajectory->currentFacingYaw : 0.0f);
	if (outPayload.rootMotion.valid && deltaTime > kEpsilon)
	{
		m_DebugData.appliedRootMotionVelocityWorld = Vans::AnimationToWorldPlanar(
			outPayload.rootMotion.translation *
				(m_Settings.motionModel.rootMotionToWorldScale / deltaTime),
			trajectory && trajectory->valid ? trajectory->currentFacingYaw : 0.0f);
	}
	return true;
}
