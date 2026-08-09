#include "VansPosePayloadMixer.h"

#include "VansPoseMath.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	namespace
	{
		std::string_view NodeTransformKey(const SampledNodeTransform& transform)
		{
			return !transform.nodePath.empty() ? transform.nodePath : transform.nodeName;
		}

		VansAnimationFrameVector<SampledNodeTransform> BlendNodeTransforms(
			const VansAnimationFrameVector<SampledNodeTransform>& first,
			const VansAnimationFrameVector<SampledNodeTransform>& second,
			float alpha)
		{
			if (first.empty()) return second;
			if (second.empty()) return first;
			VansAnimationFrameVector<bool> consumed(second.size(), false);
			VansAnimationFrameVector<SampledNodeTransform> result;
			result.reserve(std::max(first.size(), second.size()));
			for (const SampledNodeTransform& transform : first)
			{
				std::size_t found = second.size();
				for (std::size_t index = 0; index < second.size(); ++index)
					if (!consumed[index] && NodeTransformKey(second[index]) == NodeTransformKey(transform))
					{ found = index; break; }
				if (found == second.size())
				{
					result.push_back(transform);
					continue;
				}
				SampledNodeTransform blended = transform;
				blended.modelTransform = VansPoseMath::BlendTransforms(
					transform.modelTransform, second[found].modelTransform, alpha);
				result.push_back(std::move(blended));
				consumed[found] = true;
			}
			for (std::size_t index = 0; index < second.size(); ++index)
				if (!consumed[index]) result.push_back(second[index]);
			return result;
		}

		bool SameEventOccurrence(const VansAnimationEventSample& first,
		                         const VansAnimationEventSample& second)
		{
			return first.id == second.id && first.clipId == second.clipId
				&& first.sourceNodeId == second.sourceNodeId
				&& first.sourceLayerId == second.sourceLayerId
				&& first.loopIndex == second.loopIndex
				&& std::abs(first.sourceTime - second.sourceTime) <= 1.0e-6f;
		}

		void AppendWeightedEvents(VansAnimationFrameVector<VansAnimationEventSample>& destination,
		                          const VansAnimationFrameVector<VansAnimationEventSample>& source,
		                          float weight)
		{
			if (weight <= 0.0f)
				return;
			for (const VansAnimationEventSample& event : source)
			{
				VansAnimationEventSample weighted = event;
				weighted.weight *= weight;
				auto duplicate = std::find_if(destination.begin(), destination.end(),
					[&](const VansAnimationEventSample& existing)
					{
						return SameEventOccurrence(existing, weighted);
					});
				if (duplicate == destination.end())
					destination.push_back(std::move(weighted));
				else
					duplicate->weight = std::clamp(duplicate->weight + weighted.weight, 0.0f, 1.0f);
			}
		}

		VansRootMotionDelta BlendRootMotion(const VansRootMotionDelta& first,
		                                         const VansRootMotionDelta& second,
		                                         float alpha)
		{
			if (!first.valid) return second;
			if (!second.valid) return first;
			const VansBoneTransform blended = VansPoseMath::BlendTransforms(
				{ first.translation, first.rotation, first.scale },
				{ second.translation, second.rotation, second.scale }, alpha);
			return { blended.translation, blended.rotation, blended.scale, true };
		}

		VansAnimationFrameVector<VansAnimationCurveSample> BlendCurves(
			const VansAnimationFrameVector<VansAnimationCurveSample>& first,
			const VansAnimationFrameVector<VansAnimationCurveSample>& second,
			float alpha)
		{
			VansAnimationFrameVector<bool> consumed(second.size(), false);
			VansAnimationFrameVector<VansAnimationCurveSample> result;
			result.reserve(first.size() + second.size());
			for (const VansAnimationCurveSample& curve : first)
			{
				if (!curve.present)
					continue;
				std::size_t found = second.size();
				for (std::size_t index = 0; index < second.size(); ++index)
					if (!consumed[index] && second[index].present && second[index].id == curve.id)
					{ found = index; break; }
				if (found == second.size())
				{
					result.push_back(curve);
					continue;
				}
				VansAnimationCurveSample blended = curve;
				blended.value = glm::mix(curve.value, second[found].value, alpha);
				result.push_back(std::move(blended));
				consumed[found] = true;
			}
			for (std::size_t index = 0; index < second.size(); ++index)
				if (!consumed[index] && second[index].present) result.push_back(second[index]);
			return result;
		}

		VansAnimationSyncState BlendSync(const VansAnimationSyncState& first,
		                                 const VansAnimationSyncState& second,
		                                 float alpha)
		{
			if (!first.valid) return second;
			if (!second.valid) return first;
			if (first.groupId == second.groupId && first.markerId == second.markerId
			    && first.nextMarkerId == second.nextMarkerId)
			{
				VansAnimationSyncState result = first;
				result.normalizedTime = glm::mix(first.normalizedTime, second.normalizedTime, alpha);
				result.phase = glm::mix(first.phase, second.phase, alpha);
				return result;
			}
			return alpha < 0.5f ? first : second;
		}
	}

	VansPosePayload VansPosePayloadMixer::BlendOverride(const VansPosePayload& first,
	                                                   const VansPosePayload& second,
	                                                   float alpha)
	{
		if (!first.valid) return second;
		if (!second.valid) return first;
		const float weight = std::clamp(alpha, 0.0f, 1.0f);
		VansPosePayload result;
		VansPoseMath::BlendPoses(first.localPose, second.localPose, weight, result.localPose);
		result.rootMotion = BlendRootMotion(first.rootMotion, second.rootMotion, weight);
		result.curves = BlendCurves(first.curves, second.curves, weight);
		AppendWeightedEvents(result.events, first.events, 1.0f - weight);
		AppendWeightedEvents(result.events, second.events, weight);
		result.nodeTransforms = BlendNodeTransforms(first.nodeTransforms, second.nodeTransforms, weight);
		result.sync = BlendSync(first.sync, second.sync, weight);
		result.footPlacement = weight < 0.5f ? first.footPlacement : second.footPlacement;
		if (!result.footPlacement.valid)
			result.footPlacement = first.footPlacement.valid ? first.footPlacement : second.footPlacement;
		result.sourceWeight = glm::mix(first.sourceWeight, second.sourceWeight, weight);
		result.valid = true;
		return result;
	}

	VansPosePayload VansPosePayloadMixer::ApplyAdditive(const VansPosePayload& base,
	                                                    const VansPosePayload& additive,
	                                                    float weight)
	{
		if (!base.valid || !additive.valid)
			return base;
		const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
		VansPosePayload result = base;
		VansPoseMath::ApplyAdditivePose(base.localPose, additive.localPose,
		                                clampedWeight, result.localPose);
		for (const VansAnimationCurveSample& curve : additive.curves)
		{
			if (!curve.present)
				continue;
			auto found = std::find_if(result.curves.begin(), result.curves.end(),
				[&](const VansAnimationCurveSample& candidate)
				{ return candidate.present && candidate.id == curve.id; });
			if (found == result.curves.end())
			{
				VansAnimationCurveSample added = curve;
				added.value *= clampedWeight;
				result.curves.push_back(std::move(added));
			}
			else
				found->value += curve.value * clampedWeight;
		}
		AppendWeightedEvents(result.events, additive.events, clampedWeight);
		result.valid = true;
		return result;
	}
}
