#include "VansAnimationRetargetBaker.h"
#include "../VansAnimationSampler.h"
#include "../VansPoseMath.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	bool VansAnimationRetargetBaker::Bake(const VansAnimationClip& sourceClip,
		const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton,
		const VansCompiledAnimationRig& targetRig, const VansRetargetRuntimeDesc& profile,
		VansAnimationClip& output, std::string& error, const VansRetargetBakeSettings& settings)
	{
		output = {}; error.clear();
		VansRetargetProcessor processor;
		if (!std::isfinite(sourceClip.duration) || sourceClip.duration < 0
			|| !processor.Build(sourceSkeleton, targetSkeleton, targetRig, profile))
		{ error = "Retarget bake rejected the clip or skeleton/profile binding"; return false; }
		if (settings.referenceClip && (settings.nativeDeltaChannels.size() != sourceSkeleton.bones.size()
			|| settings.nativeDeltaWeights.size() != sourceSkeleton.bones.size()
			|| !std::isfinite(settings.referenceTime) || settings.referenceTime < 0
			|| settings.referenceTime > settings.referenceClip->duration))
		{ error = "Native delta retarget requires a valid reference pose and complete channel mask"; return false; }
		for (std::size_t bone = 0; bone < settings.nativeDeltaChannels.size(); ++bone)
			if ((settings.nativeDeltaChannels[bone] & ~7) || !std::isfinite(settings.nativeDeltaWeights[bone])
				|| settings.nativeDeltaWeights[bone] < 0 || settings.nativeDeltaWeights[bone] > 1)
			{ error = "Invalid native delta channel mask or weight"; return false; }
		std::vector<float> times{0.0f, sourceClip.duration};
		for (const auto& channel : sourceClip.boneKeyframes)
			for (const auto& key : channel)
			{
				if (!std::isfinite(key.time) || key.time < 0 || key.time > sourceClip.duration + 0.0001f)
				{ error = "Retarget source has an invalid key time"; return false; }
				times.push_back(std::min(key.time, sourceClip.duration));
			}
		std::sort(times.begin(), times.end());
		times.erase(std::unique(times.begin(), times.end()), times.end());
		VansAnimationClip baked = sourceClip;
		baked.stableId = 0;
		baked.boneKeyframes.assign(targetSkeleton.bones.size(), {});
		// 源模型节点不是目标 Skeleton 的节点，不向目标泄漏场景动画轨道。
		baked.nodeTransformChannels.clear();
		if (!sourceClip.rootMotion.boneName.empty())
		{
			if (profile.boneMappings.empty()) baked.rootMotion.boneName = sourceClip.rootMotion.boneName;
			else
			{
				const auto root = std::find_if(profile.boneMappings.begin(), profile.boneMappings.end(),
					[&](const auto& mapping) { return sourceSkeleton.FindBoneIndex(mapping.sourceBone)
						== sourceSkeleton.FindBoneIndex(sourceClip.rootMotion.boneName) && mapping.copyTranslationDelta; });
				if (root == profile.boneMappings.end())
				{ error = "Retarget profile does not map the declared motion root"; return false; }
				baked.rootMotion.boneName = root->targetBone;
			}
		}
		VansAnimationSampleRequest request; request.loop = false; request.extractRootMotion = false;
		// A runtime VCLIP can force-lock the rendered root while retaining authored
		// root tracks for CCT extraction. Offline baking must sample those raw tracks.
		auto samplingClip=sourceClip;
		samplingClip.rootMotion.enabled=false;samplingClip.rootMotion.forceLock=false;
		samplingClip.rootMotion.lockMode=AnimationRootLockMode::None;
		VansPosePayload source;
		VansPosePayload reference;
		if (settings.referenceClip)
		{
			request.currentTime = settings.referenceTime;
			if (!VansAnimationSampler::Sample(*settings.referenceClip, sourceSkeleton, request, reference))
			{ error = "Could not sample native delta reference"; return false; }
		}
		VansAnimationFrameVector<glm::mat4> sourceFrameModel;
		std::vector<glm::mat4> sourceModel, targetModel, targetLocal;
		std::vector<glm::mat4> sourceBindLocal;
		for (const auto& bone : sourceSkeleton.bones) sourceBindLocal.push_back(bone.localTransform);
		std::vector<glm::mat4> referenceModel, referenceTargetModel;
		if (!VansPoseMath::BuildModelTransforms(sourceBindLocal, sourceSkeleton, referenceModel)
			|| !processor.Process(referenceModel, sourceSkeleton, targetSkeleton, referenceTargetModel))
		{ error = "Could not retarget the motion root binding"; return false; }
		const int rootIndex = targetSkeleton.FindBoneIndex(baked.rootMotion.boneName);
		VansBoneTransform rootReference, firstRoot;
		if (settings.normalizeMotionRoot && (rootIndex < 0
			|| !VansPoseMath::TryDecompose(referenceTargetModel[rootIndex], rootReference)))
		{ error = "Retarget bake requires an explicit valid target motion root"; return false; }
		bool rootCaptured = false;
		for (float time : times)
		{
			request.currentTime = request.previousTime = time;
			if (!VansAnimationSampler::Sample(samplingClip, sourceSkeleton, request, source))
			{ error = "Retarget source sample failed at " + std::to_string(time); return false; }
			if (settings.referenceClip)
			{
				for (std::size_t bone = 0; bone < source.localPose.size(); ++bone)
				{
					const auto raw = source.localPose[bone];
					const auto flags = settings.nativeDeltaChannels[bone];
					const float weight = settings.nativeDeltaWeights[bone];
					auto& pose = source.localPose[bone]; pose = reference.localPose[bone];
					if (flags & 1) pose.translation += raw.translation * weight;
					if (flags & 2) pose.rotation = glm::normalize(glm::slerp(glm::quat(1,0,0,0), raw.rotation, weight) * pose.rotation);
					if (flags & 4) pose.scale *= glm::mix(glm::vec3(1), raw.scale, weight);
				}
			}
			if (!VansPoseMath::BuildModelTransforms(source.localPose, sourceSkeleton, sourceFrameModel))
			{ error = "Could not propagate the source pose"; return false; }
			sourceModel.assign(sourceFrameModel.begin(), sourceFrameModel.end());
			if (!processor.Process(sourceModel, sourceSkeleton, targetSkeleton, targetModel)
				|| !VansPoseMath::BuildLocalTransforms(targetModel, targetSkeleton, targetLocal))
			{ error = "Retarget sample failed at " + std::to_string(time); return false; }
			if (settings.normalizeMotionRoot)
			{
				VansBoneTransform root;
				if (!VansPoseMath::TryDecompose(targetLocal[rootIndex], root)) return false;
				if (!rootCaptured) { firstRoot = root; rootCaptured = true; }
				root.translation = root.translation - firstRoot.translation + rootReference.translation;
				root.rotation = glm::normalize(root.rotation * glm::inverse(firstRoot.rotation) * rootReference.rotation);
				targetLocal[rootIndex] = VansPoseMath::Compose(root);
			}
			for (std::size_t bone = 0; bone < targetLocal.size(); ++bone)
			{
				VansBoneTransform pose;
				if (!VansPoseMath::TryDecompose(targetLocal[bone], pose))
				{ error = "Retarget produced a non-decomposable target transform"; return false; }
				baked.boneKeyframes[bone].push_back({time, pose.translation, pose.rotation, pose.scale});
			}
		}
		if (settings.normalizeMotionRoot)
		{
			baked.rootMotion.lockMode = AnimationRootLockMode::FirstFrame;
			baked.rootMotion.forceLock = true;
		}
		if (settings.referenceClip) baked.rootMotion.enabled = false;
		// Collapse only constant tracks; retain every authored changing sample.
		for(auto& keys:baked.boneKeyframes)
		{
			if(keys.size()<3)continue;
			const auto first=keys.front();
			const bool constant=std::all_of(keys.begin(),keys.end(),[&](const auto& key){
				return glm::length(key.position-first.position)<1.e-5f
					&& glm::length(key.scale-first.scale)<1.e-6f
					&& std::abs(glm::dot(key.rotation,first.rotation))>1-1.e-7f;});
			if(constant){const auto last=keys.back();keys={first,last};}
		}
		output = std::move(baked);
		return true;
	}
}
