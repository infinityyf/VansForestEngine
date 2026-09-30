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
			float alpha, bool preserveMissing)
		{
			if (!preserveMissing && alpha <= 1.0e-5f) return first;
			if (!preserveMissing && std::abs(alpha - 1.0f) <= 1.0e-5f) return second;
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
					if (!preserveMissing) result.back().value *= 1.0f - alpha;
					continue;
				}
				VansAnimationCurveSample blended = curve;
				blended.value = glm::mix(curve.value, second[found].value, alpha);
				result.push_back(std::move(blended));
				consumed[found] = true;
			}
			for (std::size_t index = 0; index < second.size(); ++index)
				if (!consumed[index] && second[index].present)
				{ result.push_back(second[index]); if (!preserveMissing) result.back().value *= alpha; }
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

	VansAnimationFrameVector<VansAnimationCurveSample> VansPosePayloadMixer::BlendCurveSamples(
		const VansAnimationFrameVector<VansAnimationCurveSample>& first,
		const VansAnimationFrameVector<VansAnimationCurveSample>& second, float alpha)
	{
		return BlendCurves(first, second, std::clamp(alpha, 0.0f, 1.0f), false);
	}

	VansPosePayload VansPosePayloadMixer::BlendWeighted(
		const VansAnimationFrameVector<VansPosePayload>& poses,
		const VansAnimationFrameVector<float>& weights)
	{
		VansPosePayload result;
		if (poses.empty() || poses.size() != weights.size()) return result;
		const size_t boneCount = poses.front().localPose.size();
		for (const auto& pose : poses)
			if (!pose.valid || pose.localPose.size() != boneCount) return result;
		result.localPose.resize(boneCount);
		for (auto& bone : result.localPose)
			bone = { glm::vec3(0.0f), glm::quat(0.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.0f) };
		result.rootMotion.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
		result.rootMotion.scale = glm::vec3(0.0f);
		result.sourceWeight = 0.0f;
		size_t dominant = 0;
		for (size_t i = 0; i < poses.size(); ++i)
		{
			const auto& pose = poses[i];
			const float weight = weights[i];
			if (!std::isfinite(weight) || weight < 0.0f) return {};
			if (weight <= 0.0f) continue;
			if (weight > weights[dominant]) dominant = i;
			for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
			{
				auto& target = result.localPose[boneIndex];
				const auto& source = pose.localPose[boneIndex];
				target.translation += source.translation * weight;
				target.scale += source.scale * weight;
				// Match shortest-hemisphere accumulation against the partial sum.
				target.rotation += source.rotation * (glm::dot(target.rotation, source.rotation) < 0.0f ? -weight : weight);
			}
			const auto& root = pose.rootMotion;
			const glm::quat rootRotation = root.valid ? root.rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			result.rootMotion.translation += (root.valid ? root.translation : glm::vec3(0.0f)) * weight;
			result.rootMotion.scale += (root.valid ? root.scale : glm::vec3(1.0f)) * weight;
			result.rootMotion.rotation += rootRotation * (glm::dot(result.rootMotion.rotation, rootRotation) < 0.0f ? -weight : weight);
			result.rootMotion.valid |= root.valid;
			for (const auto& curve : pose.curves)
			{
				if (!curve.present) continue;
				auto found = std::find_if(result.curves.begin(), result.curves.end(),
					[&](const auto& candidate) { return candidate.id == curve.id; });
				if (found == result.curves.end())
				{
					result.curves.push_back(curve);
					result.curves.back().value *= weight;
				}
				else found->value += curve.value * weight;
			}
			AppendWeightedEvents(result.events, pose.events, weight);
			result.sourceWeight += pose.sourceWeight * weight;
		}
		for (auto& bone : result.localPose)
			bone.rotation = glm::dot(bone.rotation, bone.rotation) > 1.0e-12f
				? glm::normalize(bone.rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		result.rootMotion.rotation = glm::dot(result.rootMotion.rotation, result.rootMotion.rotation) > 1.0e-12f
			? glm::normalize(result.rootMotion.rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		result.sync = poses[dominant].sync;
		result.sourceAdditive = poses[dominant].sourceAdditive;
		result.sourceBoneMask = poses[dominant].sourceBoneMask;
		// Scene tracks blend only over sources containing that track. Accumulate
		// quaternion components once, just as for skeleton bones above.
		VansAnimationFrameVector<float> nodeWeights;
		VansAnimationFrameVector<VansBoneTransform> nodeTransforms;
		float accumulated = 0.0f;
		for (size_t i = 0; i < poses.size(); ++i)
		{
			if (weights[i] <= 0.0f) continue;
			for (const auto& source : poses[i].nodeTransforms)
			{
				VansBoneTransform sourceTransform;
				if (!VansPoseMath::TryDecompose(source.modelTransform, sourceTransform)) return {};
				auto found = std::find_if(result.nodeTransforms.begin(), result.nodeTransforms.end(),
					[&](const auto& candidate) { return NodeTransformKey(candidate) == NodeTransformKey(source); });
				if (found == result.nodeTransforms.end())
				{
					result.nodeTransforms.push_back(source);
					found = result.nodeTransforms.end() - 1;
					nodeTransforms.push_back({ glm::vec3(0), glm::quat(0, 0, 0, 0), glm::vec3(0) });
					nodeWeights.push_back(0.0f);
				}
				auto& target = nodeTransforms[found - result.nodeTransforms.begin()];
				target.translation += sourceTransform.translation * weights[i];
				target.scale += sourceTransform.scale * weights[i];
				target.rotation += sourceTransform.rotation *
					(glm::dot(target.rotation, sourceTransform.rotation) < 0 ? -weights[i] : weights[i]);
				nodeWeights[found - result.nodeTransforms.begin()] += weights[i];
			}
			accumulated += weights[i];
		}
		for (size_t i = 0; i < result.nodeTransforms.size(); ++i)
		{
			auto& target = nodeTransforms[i];
			target.translation /= nodeWeights[i];
			target.scale /= nodeWeights[i];
			target.rotation = glm::dot(target.rotation, target.rotation) > 1.0e-12f
				? glm::normalize(target.rotation) : glm::quat(1, 0, 0, 0);
			result.nodeTransforms[i].modelTransform = VansPoseMath::Compose(target);
		}
		result.valid = accumulated > 0.0f;
		return result;
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
		// 普通有效姿态混合保留单侧曲线；按零参与的线性曲线混合由独立入口提供。
		result.curves = BlendCurves(first.curves, second.curves, weight, true);
		AppendWeightedEvents(result.events, first.events, 1.0f - weight);
		AppendWeightedEvents(result.events, second.events, weight);
		result.nodeTransforms = BlendNodeTransforms(first.nodeTransforms, second.nodeTransforms, weight);
		result.sync = BlendSync(first.sync, second.sync, weight);
		// 程序化命令属于唯一 Target Procedural Graph，不允许跨 Pose Layer 混合。
		result.proceduralNodeIds = weight < 0.5f
			? first.proceduralNodeIds : second.proceduralNodeIds;
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
