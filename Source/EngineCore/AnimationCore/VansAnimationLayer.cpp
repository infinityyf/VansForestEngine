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
		constexpr float kLinearPoseWeightEpsilon = 1.0e-5f;

		VansBoneTransform ApplyRelativeAdditive(const VansBoneTransform& base,
		                                          const VansBoneTransform& source,
		                                          const VansBoneTransform& reference,
		                                          float weight, bool linearRotationBlend = false)
		{
			VansBoneTransform delta;
			delta.translation = source.translation - reference.translation;
			delta.rotation = glm::normalize(linearRotationBlend
				? source.rotation * glm::inverse(reference.rotation)
				: glm::inverse(reference.rotation) * source.rotation);
			delta.scale = glm::vec3(1.0f);
			for (int axis = 0; axis < 3; ++axis)
			{
				const float divisor = reference.scale[axis];
				const float tolerance=linearRotationBlend ? 1.e-8f : 1.e-6f;
				delta.scale[axis] = std::abs(divisor) > tolerance
					? source.scale[axis] / divisor : (linearRotationBlend ? 0.f : 1.f);
			}
			if (!linearRotationBlend) return VansPoseMath::ApplyAdditiveTransform(base, delta, weight);
			const float sign=delta.rotation.w>=0 ? 1.f : -1.f;
			VansBoneTransform result;
			result.translation = base.translation + delta.translation * weight;
			result.scale = base.scale * glm::mix(glm::vec3(1), delta.scale, weight);
			result.rotation = glm::normalize((glm::quat(1,0,0,0)*(sign*(1-weight))+delta.rotation*weight)*base.rotation);
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
				float boneWeight = std::clamp(weight * mask.weights[index], 0.0f, 1.0f);
				if (boneWeight <= (definition.linearRotationBlend ? kLinearPoseWeightEpsilon : kLayerEpsilon))
					continue;
				if (definition.linearRotationBlend && definition.blendMode==VansLayerBlendMode::Override && boneWeight>=1-kLinearPoseWeightEpsilon)
					boneWeight=1;
				if (definition.blendMode == VansLayerBlendMode::Override && definition.linearRotationBlend)
				{
					const auto& source=layer.localPose[index];auto& target=result[index];
					const float sign=glm::dot(target.rotation,source.rotation)<0 ? -1.f : 1.f;
					target.translation=glm::mix(target.translation,source.translation,boneWeight);
					target.scale=glm::mix(target.scale,source.scale,boneWeight);
					target.rotation=glm::normalize(target.rotation*(sign*(1-boneWeight))+source.rotation*boneWeight);
				}
				else if (definition.blendMode == VansLayerBlendMode::Override)
					result[index] = VansPoseMath::BlendTransforms(result[index], layer.localPose[index], boneWeight);
				else
					result[index] = ApplyRelativeAdditive(result[index], layer.localPose[index],
					                                      reference[index], boneWeight,definition.linearRotationBlend);
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
			// UE's mesh-space-rotation flag changes only the rotation blend. Translation
			// and scale remain local; blending complete component-space matrices moves
			// children when their parents rotate and also introduces scale/shear drift.
			VansAnimationFrameVector<glm::quat> baseRotation(result.size());
			VansAnimationFrameVector<glm::quat> layerRotation(result.size());
			VansAnimationFrameVector<glm::quat> referenceRotation(result.size());
			VansAnimationFrameVector<glm::quat> blendedRotation(result.size());
			if (!skeleton.ValidateTopology())
				return;
			for (int boneIndex : skeleton.topologicalOrder)
			{
				const std::size_t index = static_cast<std::size_t>(boneIndex);
				const int parent = skeleton.bones[index].parentIndex;
				const glm::quat parentBase = parent < 0 ? glm::quat(1, 0, 0, 0) : baseRotation[parent];
				const glm::quat parentLayer = parent < 0 ? glm::quat(1, 0, 0, 0) : layerRotation[parent];
				const glm::quat parentReference = parent < 0 ? glm::quat(1, 0, 0, 0) : referenceRotation[parent];
				baseRotation[index] = glm::normalize(parentBase * result[index].rotation);
				layerRotation[index] = glm::normalize(parentLayer * layer.localPose[index].rotation);
				referenceRotation[index] = glm::normalize(parentReference * reference[index].rotation);
			}
			for (int boneIndex : skeleton.topologicalOrder)
			{
				const std::size_t index = static_cast<std::size_t>(boneIndex);
				const int parent = skeleton.bones[index].parentIndex;
				float boneWeight = std::clamp(weight * mask.weights[index], 0.0f, 1.0f);
				if (definition.linearRotationBlend && boneWeight<=kLinearPoseWeightEpsilon) boneWeight=0;
				if (definition.linearRotationBlend && definition.blendMode==VansLayerBlendMode::Override && boneWeight>=1-kLinearPoseWeightEpsilon)
					boneWeight=1;
				VansBoneTransform mixed = result[index];
				glm::quat meshRotation = baseRotation[index];
				if (boneWeight > (definition.linearRotationBlend ? kLinearPoseWeightEpsilon : kLayerEpsilon))
				{
					if (definition.blendMode == VansLayerBlendMode::Override)
					{
						mixed.translation = glm::mix(mixed.translation, layer.localPose[index].translation, boneWeight);
						mixed.scale = glm::mix(mixed.scale, layer.localPose[index].scale, boneWeight);
						if (definition.linearRotationBlend && boneWeight == 1.0f)
							meshRotation = layerRotation[index];
						else
					{
							const float sign = glm::dot(meshRotation, layerRotation[index]) < 0.0f ? -1.0f : 1.0f;
							meshRotation = definition.linearRotationBlend
								? glm::normalize(meshRotation * (sign * (1.0f - boneWeight))
									+ layerRotation[index] * boneWeight)
								: glm::normalize(meshRotation * (1.0f - boneWeight)
									+ layerRotation[index] * (sign * boneWeight));
						}
					}
					else
					{
						mixed = ApplyRelativeAdditive(mixed, layer.localPose[index], reference[index], boneWeight,definition.linearRotationBlend);
						glm::quat delta = glm::normalize(layerRotation[index] * glm::inverse(referenceRotation[index]));
						const float sign = definition.linearRotationBlend && delta.w < 0.0f ? -1.0f : 1.0f;
						if (!definition.linearRotationBlend && delta.w < 0.0f) delta = -delta;
						meshRotation = glm::normalize((glm::quat(1, 0, 0, 0) * (sign * (1.0f - boneWeight))
							+ delta * boneWeight)
							* baseRotation[index]);
					}
				}
				blendedRotation[index] = meshRotation;
				mixed.rotation = glm::normalize(
					(parent < 0 ? glm::quat(1, 0, 0, 0) : glm::inverse(blendedRotation[parent]))
					* meshRotation);
				result[index] = mixed;
			}
		}

		void ApplyCurves(VansPosePayload& result, const VansPosePayload& layer,
		                 VansLayerCurveMode mode, float weight, const VansCompiledBoneMask& mask)
		{
			if (mode == VansLayerCurveMode::BaseOnly || weight <= kLinearPoseWeightEpsilon)
				return;
			if (mode == VansLayerCurveMode::Blend)
			{
				result.curves = VansPosePayloadMixer::BlendCurveSamples(result.curves, layer.curves, weight);
				return;
			}
			float curveWeight = 0.0f;
			for (float boneMask : mask.weights)
				curveWeight = std::max(curveWeight, std::clamp(weight * boneMask, 0.0f, 1.0f));
			if (curveWeight <= kLinearPoseWeightEpsilon) curveWeight = 0.0f;
			if ((mode == VansLayerCurveMode::Accumulate || mode == VansLayerCurveMode::Normalize) && curveWeight == 0.0f)
				return;
			const float normalization = mode == VansLayerCurveMode::Normalize ? 1.0f / (1.0f + curveWeight) : 1.0f;
			if (mode == VansLayerCurveMode::Normalize)
				for (auto& curve : result.curves)
					if (curve.present) curve.value *= normalization;
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
					if (mode == VansLayerCurveMode::Accumulate || mode == VansLayerCurveMode::Normalize)
						result.curves.back().value *= curveWeight * normalization;
					continue;
				}
				float& value = found->value;
				switch (mode)
				{
				case VansLayerCurveMode::BaseOnly: break;
				case VansLayerCurveMode::Override: value = curve.value; break;
				case VansLayerCurveMode::Blend: break;
				case VansLayerCurveMode::Normalize:
				case VansLayerCurveMode::Accumulate: value += curve.value * (curveWeight * normalization); break;
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

	const char* VansLayerCurveModeName(VansLayerCurveMode mode)
	{
		switch (mode)
		{
		case VansLayerCurveMode::BaseOnly: return "baseOnly";
		case VansLayerCurveMode::Override: return "override";
		case VansLayerCurveMode::Blend: return "blend";
		case VansLayerCurveMode::Normalize: return "normalize";
		case VansLayerCurveMode::Min: return "min";
		case VansLayerCurveMode::Max: return "max";
		case VansLayerCurveMode::Accumulate: return "accumulate";
		}
		return "";
	}

	bool VansParseLayerCurveMode(const std::string& name, VansLayerCurveMode& mode)
	{
		for (int value = static_cast<int>(VansLayerCurveMode::BaseOnly);
			value <= static_cast<int>(VansLayerCurveMode::Accumulate); ++value)
			if (name == VansLayerCurveModeName(static_cast<VansLayerCurveMode>(value)))
			{ mode = static_cast<VansLayerCurveMode>(value); return true; }
		return false;
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
					std::clamp(definition.dynamicAdditiveWeight, 0.0f, 1.0f), definition.rotationSpace,definition.linearRotationBlend);
		if (!mask.allZero && weight > kLayerEpsilon)
		{
			if (definition.rotationSpace == VansRotationBlendSpace::Mesh)
				ApplyMeshSpaceMix(result.localPose, adjustedLayer, definition, mask, skeleton, referencePose, weight);
			else
				ApplyLocalBoneMix(result.localPose, adjustedLayer, definition, mask, referencePose, weight);
		}

		ApplyCurves(result, layer, definition.curves, weight, mask);
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
		VansRotationBlendSpace rotationSpace,bool linearRotationBlend)
	{
		VansPosePayload result = layer;
		if (!base.valid || !layer.valid || base.localPose.size() != layer.localPose.size()
			|| base.localPose.size() != baseReference.size()
			|| base.localPose.size() != mask.weights.size())
			return result;
		const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
		if (rotationSpace == VansRotationBlendSpace::Mesh)
		{
			if (!skeleton.ValidateTopology())
				return result;
			VansAnimationFrameVector<glm::quat> baseRotation(layer.localPose.size());
			VansAnimationFrameVector<glm::quat> layerRotation(layer.localPose.size());
			VansAnimationFrameVector<glm::quat> referenceRotation(layer.localPose.size());
			VansAnimationFrameVector<glm::quat> blendedRotation(layer.localPose.size());
			for (int boneIndex : skeleton.topologicalOrder)
			{
				const std::size_t index = static_cast<std::size_t>(boneIndex);
				const int parent = skeleton.bones[index].parentIndex;
				baseRotation[index] = glm::normalize((parent < 0 ? glm::quat(1, 0, 0, 0)
					: baseRotation[parent]) * base.localPose[index].rotation);
				layerRotation[index] = glm::normalize((parent < 0 ? glm::quat(1, 0, 0, 0)
					: layerRotation[parent]) * layer.localPose[index].rotation);
				referenceRotation[index] = glm::normalize((parent < 0 ? glm::quat(1, 0, 0, 0)
					: referenceRotation[parent]) * baseReference[index].rotation);
			}
			for (int boneIndex : skeleton.topologicalOrder)
			{
				const std::size_t index = static_cast<std::size_t>(boneIndex);
				const int parent = skeleton.bones[index].parentIndex;
				const float boneWeight = std::clamp(mask.weights[index] * clampedWeight, 0.0f, 1.0f);
				glm::quat meshRotation = layerRotation[index];
				if (boneWeight > (linearRotationBlend ? kLinearPoseWeightEpsilon : kLayerEpsilon))
				{
					result.localPose[index] = ApplyRelativeAdditive(
						layer.localPose[index], base.localPose[index], baseReference[index], boneWeight,linearRotationBlend);
					glm::quat delta = glm::normalize(baseRotation[index] * glm::inverse(referenceRotation[index]));
					const float sign = linearRotationBlend && delta.w < 0.0f ? -1.0f : 1.0f;
					if (!linearRotationBlend && delta.w < 0.0f) delta = -delta;
					meshRotation = glm::normalize((glm::quat(1, 0, 0, 0) * (sign * (1.0f - boneWeight))
						+ delta * boneWeight) * layerRotation[index]);
				}
				blendedRotation[index] = meshRotation;
				result.localPose[index].rotation = glm::normalize(
					(parent < 0 ? glm::quat(1, 0, 0, 0) : glm::inverse(blendedRotation[parent]))
					* meshRotation);
			}
			return result;
		}
		for (std::size_t index = 0; index < layer.localPose.size(); ++index)
		{
			const float boneWeight = std::clamp(mask.weights[index] * clampedWeight, 0.0f, 1.0f);
			if (boneWeight <= (linearRotationBlend ? kLinearPoseWeightEpsilon : kLayerEpsilon))
				continue;
			// Keep the overlay's authored pose and add only the Base movement
			// delta relative to the selected reference pose.  Root/pelvis/legs
			// are naturally excluded by the compiled upper-body mask.
			result.localPose[index] = ApplyRelativeAdditive(
				layer.localPose[index], base.localPose[index], baseReference[index], boneWeight,linearRotationBlend);
		}
		return result;
	}
}
