#include "VansAnimationLayer.h"

#include "VansPoseMath.h"
#include "VansPosePayloadMixer.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace VansGraphics
{
	namespace
	{
		constexpr float kLayerEpsilon = 1.0e-6f;

		VansBoneTransform ApplyRelativeAdditive(const VansBoneTransform& base,
		                                          const VansBoneTransform& source,
		                                          const VansBoneTransform& reference,
		                                          float weight)
		{
			VansBoneTransform delta;
			delta.translation = source.translation - reference.translation;
			delta.rotation = glm::normalize(glm::inverse(reference.rotation) * source.rotation);
			delta.scale = glm::vec3(1.0f);
			for (int axis = 0; axis < 3; ++axis)
			{
				const float divisor = reference.scale[axis];
				delta.scale[axis] = std::abs(divisor) > 1.0e-6f
					? source.scale[axis] / divisor : 1.0f;
			}
			return VansPoseMath::ApplyAdditiveTransform(base, delta, weight);
		}

		VansAnimationFrameVector<glm::quat> BuildModelRotations(const VansAnimationFrameVector<VansBoneTransform>& pose,
		                                           const Skeleton& skeleton)
		{
			VansAnimationFrameVector<glm::quat> result(pose.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
			auto apply = [&](int index)
			{
				if (index < 0 || index >= static_cast<int>(pose.size()))
					return;
				const int parent = skeleton.bones[index].parentIndex;
				result[index] = parent >= 0 && parent < static_cast<int>(pose.size())
					? glm::normalize(result[parent] * pose[index].rotation)
					: glm::normalize(pose[index].rotation);
			};
			if (!skeleton.topologicalOrder.empty())
				for (int index : skeleton.topologicalOrder) apply(index);
			else
				for (size_t index = 0; index < pose.size(); ++index) apply(static_cast<int>(index));
			return result;
		}

		void ApplyLocalBoneMix(VansAnimationFrameVector<VansBoneTransform>& result,
		                       const VansPosePayload& layer,
		                       const VansAnimationLayerDefinition& definition,
		                       const VansCompiledBoneMask& mask,
		                       const VansAnimationFrameVector<VansBoneTransform>& reference,
		                       float weight)
		{
			for (size_t index = 0; index < result.size(); ++index)
			{
				const float boneWeight = std::clamp(weight * mask.weights[index], 0.0f, 1.0f);
				if (boneWeight <= kLayerEpsilon)
					continue;
				if (definition.blendMode == VansLayerBlendMode::Override)
					result[index] = VansPoseMath::BlendTransforms(result[index], layer.localPose[index], boneWeight);
				else
					result[index] = ApplyRelativeAdditive(result[index], layer.localPose[index],
					                                      reference[index], boneWeight);
			}
		}

		void ApplyMeshRotationMix(VansAnimationFrameVector<VansBoneTransform>& result,
		                          const VansPosePayload& layer,
		                          const VansAnimationLayerDefinition& definition,
		                          const VansCompiledBoneMask& mask,
		                          const Skeleton& skeleton,
		                          const VansAnimationFrameVector<VansBoneTransform>& reference,
		                          float weight)
		{
			const VansAnimationFrameVector<glm::quat> baseModel = BuildModelRotations(result, skeleton);
			const VansAnimationFrameVector<glm::quat> layerModel = BuildModelRotations(layer.localPose, skeleton);
			const VansAnimationFrameVector<glm::quat> referenceModel = BuildModelRotations(reference, skeleton);
			VansAnimationFrameVector<glm::quat> finalModel(result.size());
			auto apply = [&](int index)
			{
				if (index < 0 || index >= static_cast<int>(result.size()))
					return;
				const float boneWeight = std::clamp(weight * mask.weights[index], 0.0f, 1.0f);
				result[index].translation = definition.blendMode == VansLayerBlendMode::Override
					? glm::mix(result[index].translation, layer.localPose[index].translation, boneWeight)
					: result[index].translation
					  + (layer.localPose[index].translation - reference[index].translation) * boneWeight;
				if (definition.blendMode == VansLayerBlendMode::Override)
					finalModel[index] = glm::normalize(glm::slerp(baseModel[index], layerModel[index], boneWeight));
				else
				{
					const glm::quat delta = glm::normalize(glm::inverse(referenceModel[index]) * layerModel[index]);
					finalModel[index] = glm::normalize(baseModel[index] * glm::slerp(
						glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, boneWeight));
				}
				const int parent = skeleton.bones[index].parentIndex;
				result[index].rotation = parent >= 0 && parent < static_cast<int>(result.size())
					? glm::normalize(glm::inverse(finalModel[parent]) * finalModel[index])
					: finalModel[index];
				if (definition.blendMode == VansLayerBlendMode::Override)
					result[index].scale = glm::mix(result[index].scale, layer.localPose[index].scale, boneWeight);
				else
				{
					for (int axis = 0; axis < 3; ++axis)
					{
						const float divisor = reference[index].scale[axis];
						const float ratio = std::abs(divisor) > 1.0e-6f
							? layer.localPose[index].scale[axis] / divisor : 1.0f;
						result[index].scale[axis] *= glm::mix(1.0f, ratio, boneWeight);
					}
				}
			};
			if (!skeleton.topologicalOrder.empty())
				for (int index : skeleton.topologicalOrder) apply(index);
			else
				for (size_t index = 0; index < result.size(); ++index) apply(static_cast<int>(index));
		}

		void ApplyCurves(VansPosePayload& result, const VansPosePayload& layer,
		                 VansLayerCurveMode mode, float weight)
		{
			if (mode == VansLayerCurveMode::BaseOnly)
				return;
			for (const VansAnimationCurveSample& curve : layer.curves)
			{
				if (!curve.present)
					continue;
				auto found = std::find_if(result.curves.begin(), result.curves.end(),
					[&](const VansAnimationCurveSample& candidate)
					{ return candidate.present && candidate.id == curve.id; });
				if (found == result.curves.end())
				{
					result.curves.push_back(curve);
					continue;
				}
				float& value = found->value;
				switch (mode)
				{
				case VansLayerCurveMode::BaseOnly: break;
				case VansLayerCurveMode::Override:
				case VansLayerCurveMode::Blend:
				case VansLayerCurveMode::Normalize: value = glm::mix(value, curve.value, weight); break;
				case VansLayerCurveMode::Min: value = std::min(value, curve.value); break;
				case VansLayerCurveMode::Max: value = std::max(value, curve.value); break;
				}
			}
		}

		VansRootMotionDelta BlendRoot(const VansRootMotionDelta& base,
		                              const VansRootMotionDelta& layer, float weight)
		{
			if (!base.valid) return layer;
			if (!layer.valid) return base;
			const VansBoneTransform transform = VansPoseMath::BlendTransforms(
				{ base.translation, base.rotation, base.scale },
				{ layer.translation, layer.rotation, layer.scale }, weight);
			VansRootMotionDelta result = weight < 0.5f ? base : layer;
			result.translation = transform.translation;
			result.rotation = transform.rotation;
			result.scale = transform.scale;
			result.valid = true;
			return result;
		}
	}

	void VansAnimationLayerMixer::BuildBindPose(const Skeleton& skeleton,
	                                           VansAnimationFrameVector<VansBoneTransform>& outPose)
	{
		outPose.resize(skeleton.bones.size());
		for (size_t index = 0; index < skeleton.bones.size(); ++index)
			VansPoseMath::TryDecompose(skeleton.bones[index].localTransform, outPose[index]);
	}

	VansPosePayload VansAnimationLayerMixer::ApplyLayer(
		const VansPosePayload& base,
		const VansPosePayload& layer,
		const VansAnimationLayerDefinition& definition,
		const VansCompiledBoneMask& mask,
		const Skeleton& skeleton,
		const VansAnimationFrameVector<VansBoneTransform>& referencePose,
		float layerWeight)
	{
		if (!base.valid || !layer.valid || !definition.enabled)
			return base;
		const float weight = std::clamp(layerWeight * layer.sourceWeight, 0.0f, 1.0f);
		if (base.localPose.size() != layer.localPose.size()
		    || base.localPose.size() != mask.weights.size()
		    || base.localPose.size() != referencePose.size()
		    || base.localPose.size() != skeleton.bones.size())
			return base;

		VansPosePayload result = base;
		if (!mask.allZero && weight > kLayerEpsilon)
		{
			if (definition.rotationSpace == VansRotationBlendSpace::Mesh)
				ApplyMeshRotationMix(result.localPose, layer, definition, mask, skeleton, referencePose, weight);
			else
				ApplyLocalBoneMix(result.localPose, layer, definition, mask, referencePose, weight);
		}

		ApplyCurves(result, layer, definition.curves, weight);
		if (definition.events == VansLayerEventMode::Always
		    || (definition.events == VansLayerEventMode::ActiveOnly
		        && weight >= definition.eventWeightThreshold))
		{
			const std::uint64_t layerId = VansAnimationStableId(definition.id);
			for (VansAnimationEventSample event : layer.events)
			{
				event.sourceLayerId = layerId;
				event.weight *= weight;
				result.events.push_back(std::move(event));
			}
		}

		switch (definition.rootMotion)
		{
		case VansLayerRootMotionMode::Ignore:
		case VansLayerRootMotionMode::Base:
			break;
		case VansLayerRootMotionMode::BlendByRootWeight:
			result.rootMotion = BlendRoot(result.rootMotion, layer.rootMotion,
				std::clamp(weight * mask.rootWeight, 0.0f, 1.0f));
			break;
		case VansLayerRootMotionMode::Override:
			if (layer.rootMotion.valid && weight >= definition.eventWeightThreshold)
				result.rootMotion = layer.rootMotion;
			break;
		}
		if (result.rootMotion.valid && result.rootMotion.sourceLayerId == 0
		    && definition.rootMotion != VansLayerRootMotionMode::Ignore)
			result.rootMotion.sourceLayerId = VansAnimationStableId(definition.id);

		if (definition.nodeTracks == VansLayerNodeTrackMode::Override && weight > kLayerEpsilon)
		{
			VansPosePayload metadata = VansPosePayloadMixer::BlendOverride(base, layer, weight);
			result.nodeTransforms = std::move(metadata.nodeTransforms);
		}
		if (layer.sync.valid && weight >= 0.5f)
			result.sync = layer.sync;
		result.valid = true;
		return result;
	}
}
