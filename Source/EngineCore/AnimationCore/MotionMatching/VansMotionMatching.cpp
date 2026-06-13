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
	constexpr float kTwoPi = 6.28318530718f;

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

	float ReadFloatParam(const std::unordered_map<std::string, AnimatorParameter>& parameters,
	                     const char* name,
	                     float fallback = 0.0f)
	{
		auto it = parameters.find(name);
		if (it == parameters.end() || it->second.type != AnimatorParamType::Float)
			return fallback;
		return it->second.floatVal;
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
			int mid = (lo + hi) / 2;
			if (keyframes[mid].time <= time)
				lo = mid + 1;
			else
			{
				next = mid;
				hi = mid - 1;
			}
		}

		int prev = (std::max)(0, next - 1);
		const BoneKeyframe& a = keyframes[prev];
		const BoneKeyframe& b = keyframes[next];
		float span = b.time - a.time;
		float alpha = span > kEpsilon ? (time - a.time) / span : 0.0f;
		alpha = glm::clamp(alpha, 0.0f, 1.0f);

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

	float DirectionFromClipName(const std::string& clipName)
	{
		const std::string n = ToLower(clipName);
		if (n.find("_loop_b") != std::string::npos || n.find("_start_b") != std::string::npos || n.find("_stop_b") != std::string::npos)
			return 3.14159265f;
		if (n.find("_loop_fl") != std::string::npos || n.find("_start_fl") != std::string::npos || n.find("_stop_fl") != std::string::npos)
			return 0.78539816f;
		if (n.find("_loop_fr") != std::string::npos || n.find("_start_fr") != std::string::npos || n.find("_stop_fr") != std::string::npos)
			return -0.78539816f;
		if (n.find("_loop_bl") != std::string::npos || n.find("_start_bl") != std::string::npos || n.find("_stop_bl") != std::string::npos)
			return 2.35619449f;
		if (n.find("_loop_br") != std::string::npos || n.find("_start_br") != std::string::npos || n.find("_stop_br") != std::string::npos)
			return -2.35619449f;
		if (n.find("_loop_ll") != std::string::npos || n.find("_start_ll") != std::string::npos || n.find("_stop_ll") != std::string::npos)
			return 1.57079633f;
		if (n.find("_loop_rr") != std::string::npos || n.find("_start_rr") != std::string::npos || n.find("_stop_rr") != std::string::npos)
			return -1.57079633f;
		return 0.0f;
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

VansMotionMatchingRuntime::BoneMap VansMotionMatchingRuntime::DetectBones(const Skeleton& skeleton) const
{
	BoneMap map;
	for (int i = 0; i < static_cast<int>(skeleton.bones.size()); ++i)
	{
		const std::string n = ToLower(skeleton.bones[i].name);
		if (map.root < 0 && (n == "root" || n.find("root") != std::string::npos))
			map.root = i;
		if (map.pelvis < 0 && (n.find("pelvis") != std::string::npos || n.find("hips") != std::string::npos))
			map.pelvis = i;
		if (map.leftFoot < 0 && (n.find("foot_l") != std::string::npos || n.find("l_foot") != std::string::npos || n.find("leftfoot") != std::string::npos))
			map.leftFoot = i;
		if (map.rightFoot < 0 && (n.find("foot_r") != std::string::npos || n.find("r_foot") != std::string::npos || n.find("rightfoot") != std::string::npos))
			map.rightFoot = i;
		if (map.head < 0 && n.find("head") != std::string::npos)
			map.head = i;
	}
	if (map.pelvis < 0) map.pelvis = map.root;
	if (map.root < 0) map.root = map.pelvis >= 0 ? map.pelvis : 0;
	return map;
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

VansMotionMatchingRuntime::FeatureVector VansMotionMatchingRuntime::ExtractFeature(
	const VansAnimationClip& clip,
	float time,
	const Skeleton& skeleton,
	const BoneMap& bones) const
{
	FeatureVector f{};
	const float horizon = 0.25f;
	const float t0 = glm::clamp(time, 0.0f, (std::max)(clip.duration, 0.0f));
	const float t1 = glm::clamp(time + horizon, 0.0f, (std::max)(clip.duration, 0.0f));
	const float tm = glm::clamp(time - horizon, 0.0f, (std::max)(clip.duration, 0.0f));

	std::vector<glm::mat4> p0;
	std::vector<glm::mat4> p1;
	std::vector<glm::mat4> pm;
	SamplePose(clip, t0, skeleton, p0);
	SamplePose(clip, t1, skeleton, p1);
	SamplePose(clip, tm, skeleton, pm);

	auto posAt = [&](const std::vector<glm::mat4>& pose, int idx) -> glm::vec3
	{
		if (idx < 0 || idx >= static_cast<int>(pose.size()))
			return glm::vec3(0.0f);
		return ExtractTranslation(pose[idx]);
	};

	const glm::vec3 root0 = posAt(p0, bones.root);
	const glm::vec3 root1 = posAt(p1, bones.root);
	const glm::vec3 rootM = posAt(pm, bones.root);
	const glm::vec3 pelvis = posAt(p0, bones.pelvis);
	const glm::vec3 lf = posAt(p0, bones.leftFoot) - pelvis;
	const glm::vec3 rf = posAt(p0, bones.rightFoot) - pelvis;
	const glm::vec3 head = posAt(p0, bones.head) - pelvis;
	const glm::vec3 lfv = (posAt(p1, bones.leftFoot) - posAt(pm, bones.leftFoot)) / (2.0f * horizon);
	const glm::vec3 rfv = (posAt(p1, bones.rightFoot) - posAt(pm, bones.rightFoot)) / (2.0f * horizon);
	const glm::vec3 rootVel = (root1 - rootM) / (2.0f * horizon);
	const float dir = DirectionFromClipName(clip.clipName);

	f[0] = rootVel.x;
	f[1] = rootVel.z;
	f[2] = glm::length(glm::vec2(rootVel.x, rootVel.z));
	f[3] = std::sin(dir);
	f[4] = std::cos(dir);
	f[5] = lf.x;  f[6] = lf.y;  f[7] = lf.z;
	f[8] = rf.x;  f[9] = rf.y;  f[10] = rf.z;
	f[11] = glm::length(lfv);
	f[12] = glm::length(rfv);
	f[13] = head.y;
	f[14] = root1.x - root0.x;
	f[15] = root1.z - root0.z;
	return f;
}

bool VansMotionMatchingRuntime::BuildDatabase(const std::unordered_map<std::string, VansAnimationClip>& clips,
                                              const Skeleton& skeleton)
{
	m_Samples.clear();
	m_Bones = DetectBones(skeleton);
	if (clips.empty() || skeleton.bones.empty())
		return false;

	const float sampleStep = 1.0f / (std::max)(1.0f, m_Settings.sampleRate);
	for (const auto& [name, clip] : clips)
	{
		if (!ShouldIncludeClip(name) || clip.duration <= kEpsilon)
			continue;

		for (float t = 0.0f; t < clip.duration; t += sampleStep)
		{
			Sample sample;
			sample.clipName = name;
			sample.clip = &clip;
			sample.time = t;
			sample.feature = ExtractFeature(clip, t, skeleton, m_Bones);
			const std::string lowered = ToLower(name);
			sample.loopLike = lowered.find("loop") != std::string::npos || lowered.find("idle") != std::string::npos;
			m_Samples.push_back(sample);
		}
	}

	if (m_Samples.empty())
		return false;

	m_Mean.fill(0.0f);
	m_Std.fill(0.0f);
	for (const Sample& sample : m_Samples)
		for (int i = 0; i < FeatureDim; ++i)
			m_Mean[i] += sample.feature[i];
	for (float& mean : m_Mean)
		mean /= static_cast<float>(m_Samples.size());
	for (const Sample& sample : m_Samples)
		for (int i = 0; i < FeatureDim; ++i)
		{
			const float d = sample.feature[i] - m_Mean[i];
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
	m_CurrentTime = m_Samples[0].time;
	m_CurrentCost = 1.0e30f;
	m_DebugData.sampleCount = static_cast<int>(m_Samples.size());
	m_DebugData.clipCount = static_cast<int>(clips.size());
	m_DebugData.databaseReady = true;
	VANS_LOG("[MotionMatching] Built database: samples=" << m_Samples.size() << " clips=" << clips.size());
	return true;
}

void VansMotionMatchingRuntime::NormalizeFeature(FeatureVector& feature) const
{
	for (int i = 0; i < FeatureDim; ++i)
		feature[i] = (feature[i] - m_Mean[i]) / m_Std[i];
}

VansMotionMatchingRuntime::FeatureVector VansMotionMatchingRuntime::BuildQueryFeature(
	const std::unordered_map<std::string, AnimatorParameter>& parameters) const
{
	FeatureVector f{};
	const float speed01 = ReadFloatParam(parameters, "Speed", 0.0f);
	const float direction = ReadFloatParam(parameters, "Direction", 0.0f);
	const bool crouch = ReadFloatParam(parameters, "IsCrouching", 0.0f) > 0.5f || ReadBoolParam(parameters, "IsCrouching", false);
	const bool airborne = ReadFloatParam(parameters, "IsAirborne", 0.0f) > 0.5f || ReadBoolParam(parameters, "IsAirborne", false);
	const int moveState = ReadIntParam(parameters, "MoveState", 0);

	const float desiredSpeed = speed01 * m_Settings.desiredSpeedScale;
	const float signedDir = -direction;
	const glm::vec2 velocity(std::sin(signedDir) * desiredSpeed, std::cos(signedDir) * desiredSpeed);

	f[0] = velocity.x;
	f[1] = velocity.y;
	f[2] = desiredSpeed;
	f[3] = std::sin(signedDir);
	f[4] = std::cos(signedDir);

	const float stanceY = crouch ? -12.0f : 0.0f;
	f[5] = -10.0f; f[6] = stanceY; f[7] = 0.0f;
	f[8] = 10.0f;  f[9] = stanceY; f[10] = 0.0f;
	f[11] = desiredSpeed > 10.0f ? desiredSpeed * 0.35f : 0.0f;
	f[12] = desiredSpeed > 10.0f ? desiredSpeed * 0.35f : 0.0f;
	f[13] = crouch ? 75.0f : 105.0f;
	f[14] = velocity.x * 0.25f;
	f[15] = velocity.y * 0.25f;

	if (airborne || moveState == 5)
	{
		f[11] += 200.0f;
		f[12] += 200.0f;
	}
	return f;
}

VansMotionMatchingRuntime::MatchResult VansMotionMatchingRuntime::FindBestMatch(const FeatureVector& query)
{
	MatchResult best;
	best.totalCost = std::numeric_limits<float>::max();
	m_DebugData.topCandidates.clear();

	for (int i = 0; i < static_cast<int>(m_Samples.size()); ++i)
	{
		const Sample& sample = m_Samples[i];
		float trajectory = 0.0f;
		float pose = 0.0f;
		for (int d = 0; d <= 4; ++d)
		{
			const float diff = query[d] - sample.feature[d];
			trajectory += diff * diff;
		}
		for (int d = 5; d < FeatureDim; ++d)
		{
			const float diff = query[d] - sample.feature[d];
			pose += diff * diff;
		}

		float bias = 0.0f;
		if (m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size()))
		{
			const Sample& current = m_Samples[m_CurrentSample];
			if (sample.clipName == current.clipName && sample.time >= m_CurrentTime)
				bias -= m_Settings.continuationBias;
			if (sample.loopLike && current.loopLike)
				bias -= m_Settings.loopBias;
		}

		MatchResult result;
		result.sampleIndex = i;
		result.trajectoryCost = trajectory * 0.55f;
		result.poseCost = pose * 0.35f;
		result.biasCost = bias;
		result.totalCost = result.trajectoryCost + result.poseCost + result.biasCost;

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
	item.biasCost = result.biasCost;

	auto& list = m_DebugData.topCandidates;
	list.push_back(item);
	std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.totalCost < b.totalCost; });
	const int limit = (std::max)(1, m_Settings.topCandidateCount);
	if (static_cast<int>(list.size()) > limit)
		list.resize(static_cast<size_t>(limit));
}

void VansMotionMatchingRuntime::BlendPose(const std::vector<glm::mat4>& from,
                                          const std::vector<glm::mat4>& to,
                                          float alpha,
                                          std::vector<glm::mat4>& out) const
{
	const size_t count = (std::min)(from.size(), to.size());
	out.resize(count);
	for (size_t i = 0; i < count; ++i)
	{
		glm::vec3 scaleA, posA, skewA;
		glm::quat rotA;
		glm::vec4 perspA;
		glm::decompose(from[i], scaleA, rotA, posA, skewA, perspA);

		glm::vec3 scaleB, posB, skewB;
		glm::quat rotB;
		glm::vec4 perspB;
		glm::decompose(to[i], scaleB, rotB, posB, skewB, perspB);

		const glm::vec3 pos = glm::mix(posA, posB, alpha);
		const glm::quat rot = glm::normalize(glm::slerp(rotA, rotB, alpha));
		const glm::vec3 scale = glm::mix(scaleA, scaleB, alpha);
		out[i] = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), scale);
	}
}

bool VansMotionMatchingRuntime::Update(float deltaTime,
                                       const Skeleton& skeleton,
                                       const std::unordered_map<std::string, VansAnimationClip>& clips,
                                       const std::unordered_map<std::string, AnimatorParameter>& parameters,
                                       std::vector<glm::mat4>& outLocalTransforms)
{
	m_DebugData.enabled = m_Settings.enabled;
	m_DebugData.usedThisFrame = false;
	if (!m_Settings.enabled)
		return false;
	const bool parameterWantsMotionMatching = ReadBoolParam(parameters, "UseMotionMatching", true);
	m_DebugData.enabled = parameterWantsMotionMatching;
	if (!parameterWantsMotionMatching)
		return false;

	if ((m_DatabaseDirty || !m_DatabaseReady) && m_Settings.autoBuild)
		BuildDatabase(clips, skeleton);
	if (!m_DatabaseReady || m_Samples.empty())
		return false;

	FeatureVector query = BuildQueryFeature(parameters);
	m_DebugData.querySpeed = query[2];
	m_DebugData.queryDirection = std::atan2(query[0], query[1]);
	NormalizeFeature(query);

	m_TimeSinceSearch += deltaTime;
	if (m_CurrentSample < 0)
		m_CurrentSample = 0;

	if (m_TimeSinceSearch >= m_Settings.searchThrottle)
	{
		m_TimeSinceSearch = 0.0f;
		MatchResult best = FindBestMatch(query);
		if (best.sampleIndex >= 0 &&
		    (best.totalCost + m_Settings.minSwitchCostImprovement < m_CurrentCost || m_CurrentSample < 0))
		{
			if (m_CurrentSample >= 0 && m_CurrentSample < static_cast<int>(m_Samples.size()))
				SamplePose(*m_Samples[m_CurrentSample].clip, m_CurrentTime, skeleton, m_BlendSource);
			m_CurrentSample = best.sampleIndex;
			m_CurrentTime = m_Samples[m_CurrentSample].time;
			m_CurrentCost = best.totalCost;
			m_Blending = !m_BlendSource.empty() && m_Settings.blendDuration > kEpsilon;
			m_BlendElapsed = 0.0f;
			++m_SwitchCount;
		}

		m_DebugData.currentCost = best.totalCost;
		m_DebugData.trajectoryCost = best.trajectoryCost;
		m_DebugData.poseCost = best.poseCost;
		m_DebugData.biasCost = best.biasCost;
		if (best.sampleIndex >= 0)
		{
			m_DebugData.selectedClip = m_Samples[best.sampleIndex].clipName;
			m_DebugData.selectedTime = m_Samples[best.sampleIndex].time;
		}
	}

	Sample& active = m_Samples[m_CurrentSample];
	m_CurrentTime += deltaTime;
	if (active.clip && active.clip->duration > kEpsilon)
		m_CurrentTime = std::fmod(m_CurrentTime, active.clip->duration);

	std::vector<glm::mat4> target;
	SamplePose(*active.clip, m_CurrentTime, skeleton, target);
	if (m_Blending)
	{
		m_BlendElapsed += deltaTime;
		const float t = glm::clamp(m_BlendElapsed / (std::max)(m_Settings.blendDuration, kEpsilon), 0.0f, 1.0f);
		const float alpha = 1.0f - std::pow(1.0f - t, 3.0f);
		BlendPose(m_BlendSource, target, alpha, outLocalTransforms);
		if (t >= 1.0f)
		{
			m_Blending = false;
			m_BlendSource.clear();
		}
	}
	else
	{
		outLocalTransforms = std::move(target);
	}

	m_DebugData.usedThisFrame = true;
	m_DebugData.databaseReady = true;
	m_DebugData.sampleCount = static_cast<int>(m_Samples.size());
	m_DebugData.switches = m_SwitchCount;
	m_DebugData.activeClip = active.clipName;
	m_DebugData.activeTime = m_CurrentTime;
	return true;
}
