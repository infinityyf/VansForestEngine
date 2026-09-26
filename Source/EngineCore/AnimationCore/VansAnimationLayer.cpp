#include "VansAnimationLayer.h"

#include "VansPoseMath.h"
#include "VansPosePayloadMixer.h"
#include "../Util/VansLog.h"

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

		void ApplyMeshSpaceMix(VansAnimationFrameVector<VansBoneTransform>& result,
		                          const VansPosePayload& layer,
		                          const VansAnimationLayerDefinition& definition,
		                          const VansCompiledBoneMask& mask,
		                          const Skeleton& skeleton,
		                          const VansAnimationFrameVector<VansBoneTransform>& reference,
		                          float weight)
		{
			VansAnimationFrameVector<glm::mat4> baseModel(result.size(), glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> layerModel(result.size(), glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> referenceModel(result.size(), glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> finalModel(result.size(), glm::mat4(1.0f));
			std::string topologyError;
			if (!VansPoseMath::BuildModelTransforms(result, skeleton, baseModel, &topologyError)
				|| !VansPoseMath::BuildModelTransforms(layer.localPose, skeleton, layerModel, &topologyError)
				|| !VansPoseMath::BuildModelTransforms(reference, skeleton, referenceModel, &topologyError))
			{
				VANS_LOG_WARN("[AnimationLayer] Mesh-space mix rejected: " << topologyError);
				return;
			}
			finalModel = baseModel;
			for (std::size_t index = 0; index < result.size(); ++index)
			{
				const float boneWeight = std::clamp(weight * mask.weights[index], 0.0f, 1.0f);
				if (definition.blendMode == VansLayerBlendMode::Override)
					finalModel[index] = VansPoseMath::BlendTransforms(
						baseModel[index], layerModel[index], boneWeight);
				else
					finalModel[index] = VansPoseMath::ApplyMeshSpaceAdditiveTransform(
						baseModel[index], layerModel[index], referenceModel[index], boneWeight);
			}
			VansAnimationFrameVector<glm::mat4> finalLocal;
			if (!VansPoseMath::BuildLocalTransforms(
				finalModel, skeleton, finalLocal, &topologyError))
			{
				VANS_LOG_WARN("[AnimationLayer] Mesh-space result rejected: " << topologyError);
				return;
			}
			for (std::size_t index = 0; index < result.size(); ++index)
			{
				const float boneWeight = std::clamp(weight * mask.weights[index], 0.0f, 1.0f);
				if (!VansPoseMath::TryDecompose(finalLocal[index], result[index]))
					result[index] = definition.blendMode == VansLayerBlendMode::Override
						? VansPoseMath::BlendTransforms(result[index], layer.localPose[index], boneWeight)
						: result[index];
			}
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
		if (!base.valid || !layer.valid)
			return base;
		const float weight = std::clamp(layerWeight * layer.sourceWeight, 0.0f, 1.0f);
		if (base.localPose.size() != layer.localPose.size()
		    || base.localPose.size() != mask.weights.size()
		    || base.localPose.size() != referencePose.size()
		    || base.localPose.size() != skeleton.bones.size())
			return base;

		VansPosePayload result = base;
		VansPosePayload adjustedLayer = layer;
		if (definition.dynamicAdditive && definition.dynamicAdditiveWeight > kLayerEpsilon)
				adjustedLayer = ApplyDynamicAdditive(base, layer, referencePose, skeleton, mask,
					std::clamp(definition.dynamicAdditiveWeight, 0.0f, 1.0f), definition.rotationSpace);
		if (!mask.allZero && weight > kLayerEpsilon)
		{
			if (definition.rotationSpace == VansRotationBlendSpace::Mesh)
				ApplyMeshSpaceMix(result.localPose, adjustedLayer, definition, mask, skeleton, referencePose, weight);
			else
				ApplyLocalBoneMix(result.localPose, adjustedLayer, definition, mask, referencePose, weight);
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

	VansPosePayload VansAnimationLayerMixer::ApplyDynamicAdditive(
		const VansPosePayload& base,
		const VansPosePayload& layer,
		const VansAnimationFrameVector<VansBoneTransform>& baseReference,
		const Skeleton& skeleton,
		const VansCompiledBoneMask& mask,
		float weight,
		VansRotationBlendSpace rotationSpace)
	{
		VansPosePayload result = layer;
		if (!base.valid || !layer.valid || base.localPose.size() != layer.localPose.size()
			|| base.localPose.size() != baseReference.size()
			|| base.localPose.size() != mask.weights.size())
			return result;
		const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
		if (rotationSpace == VansRotationBlendSpace::Mesh)
		{
			VansAnimationFrameVector<glm::mat4> baseModel(base.localPose.size(), glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> layerModel(layer.localPose.size(), glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> referenceModel(baseReference.size(), glm::mat4(1.0f));
			VansAnimationFrameVector<glm::mat4> resultModel(layer.localPose.size(), glm::mat4(1.0f));
			std::string topologyError;
			if (!VansPoseMath::BuildModelTransforms(base.localPose, skeleton, baseModel, &topologyError)
				|| !VansPoseMath::BuildModelTransforms(layer.localPose, skeleton, layerModel, &topologyError)
				|| !VansPoseMath::BuildModelTransforms(baseReference, skeleton, referenceModel, &topologyError))
			{
				VANS_LOG_WARN("[AnimationLayer] Dynamic additive rejected: " << topologyError);
				return result;
			}
			resultModel = layerModel;
			for (size_t index = 0; index < layer.localPose.size(); ++index)
			{
				const float boneWeight = std::clamp(mask.weights[index] * clampedWeight, 0.0f, 1.0f);
				if (boneWeight <= kLayerEpsilon) continue;
				resultModel[index] = VansPoseMath::ApplyMeshSpaceAdditiveTransform(
					layerModel[index], baseModel[index], referenceModel[index], boneWeight);
			}
			VansAnimationFrameVector<glm::mat4> resultLocal;
			if (!VansPoseMath::BuildLocalTransforms(
				resultModel, skeleton, resultLocal, &topologyError))
			{
				VANS_LOG_WARN("[AnimationLayer] Dynamic additive result rejected: " << topologyError);
				return result;
			}
			for (std::size_t index = 0; index < result.localPose.size(); ++index)
				VansPoseMath::TryDecompose(resultLocal[index], result.localPose[index]);
			return result;
		}
		for (std::size_t index = 0; index < layer.localPose.size(); ++index)
		{
			const float boneWeight = std::clamp(mask.weights[index] * clampedWeight, 0.0f, 1.0f);
			if (boneWeight <= kLayerEpsilon)
				continue;
			// Keep the overlay's authored pose and add only the Base movement
			// delta relative to the selected reference pose.  Root/pelvis/legs
			// are naturally excluded by the compiled upper-body mask.
			result.localPose[index] = ApplyRelativeAdditive(
				layer.localPose[index], base.localPose[index], baseReference[index], boneWeight);
		}
		return result;
	}
}
