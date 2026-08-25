#include "VansRootMotionYaw.h"

#include "../VansAnimationSampler.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kEpsilon = 0.0001f;
}

namespace VansGraphics
{
	float TurnYawProfile::SampleCumulativeYaw(float timeSeconds) const
	{
		if (!valid || sampleTimesSeconds.empty() ||
			cumulativeYawDegrees.size() != sampleTimesSeconds.size())
		{
			return 0.0f;
		}
		if (timeSeconds <= sampleTimesSeconds.front())
			return cumulativeYawDegrees.front();
		if (timeSeconds >= sampleTimesSeconds.back())
			return cumulativeYawDegrees.back();

		const auto upper = std::upper_bound(
			sampleTimesSeconds.begin(), sampleTimesSeconds.end(), timeSeconds);
		const size_t rightIndex = static_cast<size_t>(
			std::distance(sampleTimesSeconds.begin(), upper));
		const size_t leftIndex = rightIndex - 1;
		const float span = sampleTimesSeconds[rightIndex] - sampleTimesSeconds[leftIndex];
		const float alpha = span > kEpsilon
			? glm::clamp(
				(timeSeconds - sampleTimesSeconds[leftIndex]) / span, 0.0f, 1.0f)
			: 0.0f;
		return glm::mix(
			cumulativeYawDegrees[leftIndex], cumulativeYawDegrees[rightIndex], alpha);
	}

	float TurnYawProfile::RemainingYaw(float timeSeconds) const
	{
		return valid ? totalYawDegrees - SampleCumulativeYaw(timeSeconds) : 0.0f;
	}

	float TurnYawProfile::RemainingMotionTime(float timeSeconds) const
	{
		return valid ? (std::max)(0.0f, motionEndTimeSeconds - timeSeconds) : 0.0f;
	}

	float ExtractRootMotionYawDegrees(const glm::quat& rotation)
	{
		const glm::quat normalized = glm::normalize(rotation);
		const float length = std::sqrt(
			normalized.w * normalized.w + normalized.z * normalized.z);
		if (!std::isfinite(length) || length <= kEpsilon)
			return 0.0f;
		const float twistW = normalized.w / length;
		const float twistZ = normalized.z / length;
		const float yaw = glm::degrees(2.0f * std::atan2(twistZ, twistW));
		return std::remainder(yaw, 360.0f);
	}

	void ApplyRootMotionYawCorrection(float correctionDegrees, glm::quat& inOutRotation)
	{
		if (!std::isfinite(correctionDegrees) || std::abs(correctionDegrees) <= kEpsilon)
			return;
		const glm::quat correction = glm::angleAxis(
			glm::radians(correctionDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
		inOutRotation = glm::normalize(correction * inOutRotation);
	}

	bool BuildTurnYawProfile(
		const VansAnimationClip& clip,
		const Skeleton& skeleton,
		int rootMotionBoneIndex,
		float sampleRate,
		float playbackEndMarginSeconds,
		float motionEndThresholdDegrees,
		TurnYawProfile& outProfile)
	{
		outProfile = {};
		if (clip.duration <= kEpsilon || skeleton.bones.empty() ||
			rootMotionBoneIndex < 0 ||
			rootMotionBoneIndex >= static_cast<int>(skeleton.bones.size()) ||
			!clip.rootMotion.enabled || !clip.rootMotion.extractRotation)
		{
			return false;
		}

		const float effectiveSampleRate = (std::max)(30.0f, sampleRate);
		const float step = 1.0f / effectiveSampleRate;
		// MM 的非循环 transition 会在文件末尾前一个 completion window 切出。
		// Profile 必须以运行时真正会消费的端点积分，否则最后几帧的 authored
		// yaw 会被错误计入可达性，切出时留下确定性残差。
		const float playbackEndTime = glm::clamp(
			clip.duration - (std::max)(0.0f, playbackEndMarginSeconds),
			(std::min)(step, clip.duration),
			clip.duration);
		outProfile.durationSeconds = playbackEndTime;
		outProfile.sampleTimesSeconds.push_back(0.0f);
		outProfile.cumulativeYawDegrees.push_back(0.0f);

		float cumulativeYaw = 0.0f;
		float previousTime = 0.0f;
		for (float currentTime = (std::min)(step, playbackEndTime);;
			 currentTime = (std::min)(currentTime + step, playbackEndTime))
		{
			VansAnimationSampleRequest request;
			request.previousTime = previousTime;
			request.currentTime = currentTime;
			request.startTime = 0.0f;
			request.endTime = clip.duration;
			request.loop = false;
			request.rootMotionBoneIndex = rootMotionBoneIndex;
			VansPosePayload payload;
			if (!VansAnimationSampler::Sample(clip, skeleton, request, payload) ||
				!payload.rootMotion.valid)
			{
				outProfile = {};
				return false;
			}

			const float deltaYaw = ExtractRootMotionYawDegrees(payload.rootMotion.rotation);
			if (!std::isfinite(deltaYaw))
			{
				outProfile = {};
				return false;
			}
			cumulativeYaw += deltaYaw;
			outProfile.sampleTimesSeconds.push_back(currentTime);
			outProfile.cumulativeYawDegrees.push_back(cumulativeYaw);
			previousTime = currentTime;
			if (currentTime >= playbackEndTime - kEpsilon)
				break;
		}

		outProfile.totalYawDegrees = cumulativeYaw;
		const float yawThreshold = (std::max)(motionEndThresholdDegrees, kEpsilon);
		if (!std::isfinite(cumulativeYaw) || std::abs(cumulativeYaw) <= yawThreshold)
		{
			outProfile = {};
			return false;
		}

		size_t motionEndIndex = outProfile.sampleTimesSeconds.size() - 1;
		for (size_t index = outProfile.sampleTimesSeconds.size(); index-- > 0;)
		{
			const float remaining = std::abs(
				outProfile.totalYawDegrees - outProfile.cumulativeYawDegrees[index]);
			if (remaining <= yawThreshold)
				motionEndIndex = index;
			else
				break;
		}
		outProfile.motionEndTimeSeconds =
			outProfile.sampleTimesSeconds[motionEndIndex];
		outProfile.directionSign = cumulativeYaw >= 0.0f ? 1 : -1;
		outProfile.valid = true;
		return true;
	}
}
