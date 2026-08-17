#include "VansAnimationSampler.h"

#include "VansPoseMath.h"

#include <../../GLM/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace VansGraphics
{
	namespace
	{
		constexpr double kTimeEpsilon = 1.0e-6;

		VansBoneTransform InterpolateBoneKeys(const std::vector<BoneKeyframe>& keys,
		                                           float time,
		                                           const VansBoneTransform& fallback)
		{
			if (keys.empty())
				return fallback;
			if (keys.size() == 1 || time <= keys.front().time)
				return { keys.front().position, glm::normalize(keys.front().rotation), keys.front().scale };
			if (time >= keys.back().time)
				return { keys.back().position, glm::normalize(keys.back().rotation), keys.back().scale };

			auto upper = std::upper_bound(keys.begin(), keys.end(), time,
				[](float sampleTime, const BoneKeyframe& key) { return sampleTime < key.time; });
			const BoneKeyframe& right = *upper;
			const BoneKeyframe& left = *(upper - 1);
			const float span = right.time - left.time;
			const float alpha = span > 0.0f ? std::clamp((time - left.time) / span, 0.0f, 1.0f) : 0.0f;
			return {
				glm::mix(left.position, right.position, alpha),
				glm::normalize(glm::slerp(left.rotation, right.rotation, alpha)),
				glm::mix(left.scale, right.scale, alpha)
			};
		}

		VansBoneTransform InterpolateTransformKeys(const std::vector<TransformKeyframe>& keys,
		                                                float time,
		                                                const VansBoneTransform& fallback)
		{
			if (keys.empty())
				return fallback;
			if (keys.size() == 1 || time <= keys.front().time)
				return { keys.front().position, glm::normalize(keys.front().rotation), keys.front().scale };
			if (time >= keys.back().time)
				return { keys.back().position, glm::normalize(keys.back().rotation), keys.back().scale };

			auto upper = std::upper_bound(keys.begin(), keys.end(), time,
				[](float sampleTime, const TransformKeyframe& key) { return sampleTime < key.time; });
			const TransformKeyframe& right = *upper;
			const TransformKeyframe& left = *(upper - 1);
			const float span = right.time - left.time;
			const float alpha = span > 0.0f ? std::clamp((time - left.time) / span, 0.0f, 1.0f) : 0.0f;
			return {
				glm::mix(left.position, right.position, alpha),
				glm::normalize(glm::slerp(left.rotation, right.rotation, alpha)),
				glm::mix(left.scale, right.scale, alpha)
			};
		}

		float InterpolateCurve(const std::vector<AnimationCurveKey>& keys, float time)
		{
			if (keys.empty())
				return 0.0f;
			if (keys.size() == 1 || time <= keys.front().time)
				return keys.front().value;
			if (time >= keys.back().time)
				return keys.back().value;
			auto upper = std::upper_bound(keys.begin(), keys.end(), time,
				[](float sampleTime, const AnimationCurveKey& key) { return sampleTime < key.time; });
			const AnimationCurveKey& right = *upper;
			const AnimationCurveKey& left = *(upper - 1);
			const float span = right.time - left.time;
			const float alpha = span > 0.0f ? std::clamp((time - left.time) / span, 0.0f, 1.0f) : 0.0f;
			return glm::mix(left.value, right.value, alpha);
		}

		int ResolveRootBoneIndex(
			const VansAnimationClip& clip, const Skeleton& skeleton, int requestedBoneIndex)
		{
			if (requestedBoneIndex >= 0 &&
			    requestedBoneIndex < static_cast<int>(skeleton.bones.size()))
				return requestedBoneIndex;
			if (!clip.rootMotion.boneName.empty())
			{
				auto found = skeleton.boneNameToIndex.find(clip.rootMotion.boneName);
				if (found != skeleton.boneNameToIndex.end()
				    && found->second >= 0 && found->second < static_cast<int>(skeleton.bones.size()))
					return found->second;
				return -1;
			}
			for (size_t index = 0; index < skeleton.bones.size(); ++index)
				if (skeleton.bones[index].parentIndex < 0)
					return static_cast<int>(index);
			return skeleton.bones.empty() ? -1 : 0;
		}

		glm::mat4 SampleRootMatrix(const VansAnimationClip& clip, const Skeleton& skeleton,
		                           int boneIndex, float time)
		{
			VansBoneTransform fallback;
			VansPoseMath::TryDecompose(skeleton.bones[boneIndex].localTransform, fallback);
			if (boneIndex >= 0 && boneIndex < static_cast<int>(clip.boneKeyframes.size()))
				return VansPoseMath::Compose(InterpolateBoneKeys(clip.boneKeyframes[boneIndex], time, fallback));
			return VansPoseMath::Compose(fallback);
		}

		glm::mat4 MatrixPower(glm::mat4 value, std::int64_t exponent)
		{
			glm::mat4 result(1.0f);
			while (exponent > 0)
			{
				if ((exponent & 1) != 0)
					result *= value;
				value *= value;
				exponent >>= 1;
			}
			return result;
		}

		glm::mat4 ForwardLoopDelta(const VansAnimationClip& clip, const Skeleton& skeleton,
		                           int boneIndex, double from, double to,
		                           float start, float end)
		{
			const double range = static_cast<double>(end - start);
			const auto cycleAt = [&](double raw)
			{
				return static_cast<std::int64_t>(std::floor((raw - start) / range));
			};
			const auto phaseAt = [&](double raw, std::int64_t cycle)
			{
				return std::clamp(static_cast<float>(raw - static_cast<double>(cycle) * range), start, end);
			};
			const auto segment = [&](float segmentStart, float segmentEnd)
			{
				const glm::mat4 first = SampleRootMatrix(clip, skeleton, boneIndex, segmentStart);
				const glm::mat4 second = SampleRootMatrix(clip, skeleton, boneIndex, segmentEnd);
				return glm::inverse(first) * second;
			};

			const std::int64_t fromCycle = cycleAt(from);
			const std::int64_t toCycle = cycleAt(to);
			const float fromPhase = phaseAt(from, fromCycle);
			const float toPhase = phaseAt(to, toCycle);
			if (fromCycle == toCycle)
				return segment(fromPhase, toPhase);

			glm::mat4 result = segment(fromPhase, end);
			const std::int64_t completeCycles = toCycle - fromCycle - 1;
			if (completeCycles > 0)
				result *= MatrixPower(segment(start, end), completeCycles);
			result *= segment(start, toPhase);
			return result;
		}

		void SampleRootMotion(const VansAnimationClip& clip, const Skeleton& skeleton,
		                      const VansAnimationSampleRequest& request,
		                      float start, float end, VansRootMotionDelta& outDelta)
		{
			outDelta = {};
			if (!clip.rootMotion.enabled || end <= start)
				return;
			const int rootBoneIndex = ResolveRootBoneIndex(
				clip, skeleton, request.rootMotionBoneIndex);
			if (rootBoneIndex < 0)
				return;
			if (rootBoneIndex >= static_cast<int>(clip.boneKeyframes.size()) ||
			    clip.boneKeyframes[static_cast<std::size_t>(rootBoneIndex)].empty())
				return;

			double previous = request.previousTime;
			double current = request.currentTime;
			glm::mat4 delta(1.0f);
			if (request.loop)
			{
				if (current >= previous)
					delta = ForwardLoopDelta(clip, skeleton, rootBoneIndex, previous, current, start, end);
				else
					delta = glm::inverse(ForwardLoopDelta(clip, skeleton, rootBoneIndex, current, previous, start, end));
			}
			else
			{
				const float from = std::clamp(static_cast<float>(previous), start, end);
				const float to = std::clamp(static_cast<float>(current), start, end);
				delta = glm::inverse(SampleRootMatrix(clip, skeleton, rootBoneIndex, from))
				      * SampleRootMatrix(clip, skeleton, rootBoneIndex, to);
			}

			VansBoneTransform transform;
			if (!VansPoseMath::TryDecompose(delta, transform))
				return;
			outDelta.translation = clip.rootMotion.extractTranslation ? transform.translation : glm::vec3(0.0f);
			outDelta.rotation = clip.rootMotion.extractRotation ? transform.rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			outDelta.scale = clip.rootMotion.extractScale ? transform.scale : glm::vec3(1.0f);
			outDelta.valid = true;
			outDelta.sourceClipId = clip.stableId != 0 ? clip.stableId : VansAnimationStableId(clip.clipName);
			outDelta.sourceNodeId = request.sourceNodeId;
			outDelta.sourceLayerId = request.sourceLayerId;
		}

		void SampleEvents(const VansAnimationClip& clip, const VansAnimationSampleRequest& request,
		                  float start, float end, VansAnimationFrameVector<VansAnimationEventSample>& outEvents)
		{
			struct Occurrence
			{
				double traversalTime = 0.0;
				const AnimationClipEvent* event = nullptr;
				std::int64_t loopIndex = 0;
			};
			VansAnimationFrameVector<Occurrence> occurrences;
			const double previous = request.previousTime;
			const double current = request.currentTime;
			if (std::abs(current - previous) <= kTimeEpsilon || end <= start)
				return;

			for (const AnimationClipEvent& event : clip.events)
			{
				if (event.time < start - kTimeEpsilon || event.time > end + kTimeEpsilon)
					continue;
				if (!request.loop)
				{
					const double eventTime = std::clamp(static_cast<double>(event.time),
					                                  static_cast<double>(start), static_cast<double>(end));
					const bool crossed = current > previous
						? eventTime > previous + kTimeEpsilon && eventTime <= current + kTimeEpsilon
						: eventTime < previous - kTimeEpsilon && eventTime >= current - kTimeEpsilon;
					if (crossed)
						occurrences.push_back({ eventTime, &event, 0 });
					continue;
				}

				const double range = static_cast<double>(end - start);
				double phase = event.time >= end - kTimeEpsilon ? start : std::max(event.time, start);
				const double lower = std::min(previous, current);
				const double upper = std::max(previous, current);
				const std::int64_t firstCycle = static_cast<std::int64_t>(std::floor((lower - phase) / range)) - 1;
				const std::int64_t lastCycle = static_cast<std::int64_t>(std::floor((upper - phase) / range)) + 1;
				for (std::int64_t cycle = firstCycle; cycle <= lastCycle; ++cycle)
				{
					const double traversalTime = phase + static_cast<double>(cycle) * range;
					const bool crossed = current > previous
						? traversalTime > previous + kTimeEpsilon && traversalTime <= current + kTimeEpsilon
						: traversalTime < previous - kTimeEpsilon && traversalTime >= current - kTimeEpsilon;
					if (crossed)
						occurrences.push_back({ traversalTime, &event, cycle });
				}
			}

			std::stable_sort(occurrences.begin(), occurrences.end(), [&](const Occurrence& a, const Occurrence& b)
			{
				return current > previous ? a.traversalTime < b.traversalTime : a.traversalTime > b.traversalTime;
			});
			const std::uint64_t clipId = clip.stableId != 0 ? clip.stableId : VansAnimationStableId(clip.clipName);
			for (const Occurrence& occurrence : occurrences)
			{
				VansAnimationEventSample sample;
				sample.id = occurrence.event->id != 0
					? occurrence.event->id : VansAnimationStableId(occurrence.event->name);
				sample.clipId = clipId;
				sample.sourceNodeId = request.sourceNodeId;
				sample.sourceLayerId = request.sourceLayerId;
				sample.name = occurrence.event->name;
				sample.sourceTime = occurrence.event->time;
				sample.loopIndex = occurrence.loopIndex;
				sample.payload = occurrence.event->payload;
				outEvents.push_back(std::move(sample));
			}
		}

		void SampleSync(const VansAnimationClip& clip, float sampleTime,
		                float start, float end, bool loop, VansAnimationSyncState& outSync)
		{
			outSync = {};
			const float range = end - start;
			if (range <= 0.0f)
				return;
			outSync.groupId = clip.syncGroupName.empty() ? 0 : VansAnimationStableId(clip.syncGroupName);
			outSync.normalizedTime = std::clamp((sampleTime - start) / range, 0.0f, 1.0f);
			outSync.phase = outSync.normalizedTime;
			outSync.valid = true;

			VansAnimationFrameVector<const AnimationSyncMarker*> markers;
			for (const AnimationSyncMarker& marker : clip.syncMarkers)
				if (marker.time >= start && marker.time <= end)
					markers.push_back(&marker);
			if (markers.empty())
				return;
			std::stable_sort(markers.begin(), markers.end(), [](const auto* a, const auto* b)
			{
				return a->time < b->time;
			});

			const AnimationSyncMarker* previous = nullptr;
			const AnimationSyncMarker* next = nullptr;
			for (const AnimationSyncMarker* marker : markers)
			{
				if (marker->time <= sampleTime)
					previous = marker;
				else
				{
					next = marker;
					break;
				}
			}
			float previousTime = 0.0f;
			float nextTime = 0.0f;
			if (loop)
			{
				if (!previous)
				{
					previous = markers.back();
					previousTime = previous->time - range;
				}
				else
					previousTime = previous->time;
				if (!next)
				{
					next = markers.front();
					nextTime = next->time + range;
				}
				else
					nextTime = next->time;
			}
			else
			{
				if (!previous)
					previous = markers.front();
				if (!next)
					next = markers.back();
				previousTime = previous->time;
				nextTime = next->time;
			}
			outSync.markerId = previous->id != 0 ? previous->id : VansAnimationStableId(previous->name);
			outSync.nextMarkerId = next->id != 0 ? next->id : VansAnimationStableId(next->name);
			const float markerRange = nextTime - previousTime;
			outSync.phase = markerRange > 0.0f
				? std::clamp((sampleTime - previousTime) / markerRange, 0.0f, 1.0f)
				: 0.0f;
		}

		void SampleNodeTransforms(const VansAnimationClip& clip, float time,
		                          VansAnimationFrameVector<SampledNodeTransform>& outTransforms)
		{
			const size_t count = clip.nodeTransformChannels.size();
			if (count == 0)
				return;
			VansAnimationFrameVector<glm::mat4> local(count, glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> model(count, glm::mat4(1.0f));
			VansAnimationFrameVector<std::uint8_t> state(count, 0);
			for (size_t index = 0; index < count; ++index)
			{
				VansBoneTransform fallback;
				VansPoseMath::TryDecompose(clip.nodeTransformChannels[index].bindLocalTransform, fallback);
				local[index] = VansPoseMath::Compose(
					InterpolateTransformKeys(clip.nodeTransformChannels[index].keyframes, time, fallback));
			}
			auto resolve = [&](auto&& self, size_t index) -> glm::mat4
			{
				if (state[index] == 2)
					return model[index];
				const NodeTransformChannel& channel = clip.nodeTransformChannels[index];
				if (state[index] == 1)
					return channel.bindModelTransform;
				state[index] = 1;
				glm::mat4 parent(1.0f);
				if (channel.parentChannelIndex >= 0
				    && channel.parentChannelIndex < static_cast<int>(count))
					parent = self(self, static_cast<size_t>(channel.parentChannelIndex));
				else
					parent = channel.bindModelTransform * glm::inverse(channel.bindLocalTransform);
				model[index] = parent * local[index];
				state[index] = 2;
				return model[index];
			};
			outTransforms.reserve(count);
			for (size_t index = 0; index < count; ++index)
			{
				const NodeTransformChannel& channel = clip.nodeTransformChannels[index];
				outTransforms.push_back({ static_cast<std::uint32_t>(index), channel.nodeName,
					                          channel.nodePath, resolve(resolve, index) });
			}
		}
	}

	float VansAnimationSampler::ResolveSampleTime(float rawTime, float startTime,
	                                              float endTime, bool loop)
	{
		if (endTime <= startTime)
			return startTime;
		if (!loop)
			return std::clamp(rawTime, startTime, endTime);
		const float range = endTime - startTime;
		float wrapped = startTime + std::fmod(rawTime - startTime, range);
		if (wrapped < startTime)
			wrapped += range;
		return wrapped;
	}

	bool VansAnimationSampler::Sample(const VansAnimationClip& clip,
	                                  const Skeleton& skeleton,
	                                  const VansAnimationSampleRequest& request,
	                                  VansPosePayload& outPayload)
	{
		outPayload = {};
		const float start = std::clamp(request.startTime, 0.0f, clip.duration);
		const float requestedEnd = request.endTime < 0.0f ? clip.duration : request.endTime;
		const float end = std::clamp(requestedEnd, start, clip.duration);
		if (end <= start)
			return false;
		const float sampleTime = ResolveSampleTime(request.currentTime, start, end, request.loop);

		outPayload.localPose.resize(skeleton.bones.size());
		for (size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex)
		{
			VansPoseMath::TryDecompose(skeleton.bones[boneIndex].localTransform,
			                          outPayload.localPose[boneIndex]);
			if (boneIndex < clip.boneKeyframes.size())
				outPayload.localPose[boneIndex] = InterpolateBoneKeys(
					clip.boneKeyframes[boneIndex], sampleTime, outPayload.localPose[boneIndex]);
		}

		SampleNodeTransforms(clip, sampleTime, outPayload.nodeTransforms);
		outPayload.curves.reserve(clip.curves.size());
		for (const AnimationCurveTrack& curve : clip.curves)
		{
			if (curve.keys.empty())
				continue;
			outPayload.curves.push_back({
				curve.id != 0 ? curve.id : VansAnimationStableId(curve.name),
				curve.name,
				InterpolateCurve(curve.keys, sampleTime),
				true
			});
		}
		SampleEvents(clip, request, start, end, outPayload.events);
		SampleRootMotion(clip, skeleton, request, start, end, outPayload.rootMotion);
		SampleSync(clip, sampleTime, start, end, request.loop, outPayload.sync);
		outPayload.valid = !outPayload.localPose.empty() || !outPayload.nodeTransforms.empty()
			|| !outPayload.curves.empty() || !outPayload.events.empty() || outPayload.rootMotion.valid;
		return outPayload.valid;
	}
}
