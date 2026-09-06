#include "VansAnimationClipLoader.h"
#include "VansSkinnedMeshLoader.h"
#include "../Util/VansLog.h"

using namespace VansGraphics;

namespace
{
bool IsSkeletonHierarchyValid(const Skeleton& skeleton, std::string* reason = nullptr)
{
	if (skeleton.bones.empty())
	{
		if (reason) *reason = "empty skeleton";
		return false;
	}

	int rootCount = 0;
	for (size_t i = 0; i < skeleton.bones.size(); ++i)
	{
		const BoneInfo& bone = skeleton.bones[i];
		if (bone.parentIndex < 0)
		{
			++rootCount;
		}
		else if (bone.parentIndex == static_cast<int>(i))
		{
			if (reason) *reason = "self-parent bone: " + bone.name;
			return false;
		}
		else if (bone.parentIndex >= static_cast<int>(skeleton.bones.size()))
		{
			if (reason) *reason = "out-of-range parent for bone: " + bone.name;
			return false;
		}

		for (int child : bone.children)
		{
			if (child == static_cast<int>(i))
			{
				if (reason) *reason = "self-child bone: " + bone.name;
				return false;
			}
			if (child < 0 || child >= static_cast<int>(skeleton.bones.size()))
			{
				if (reason) *reason = "out-of-range child for bone: " + bone.name;
				return false;
			}
		}
	}

	if (rootCount == 0)
	{
		if (reason) *reason = "no root bone";
		return false;
	}
	return true;
}
}

bool VansAnimationClipLoader::LoadClipsFromRefs(
	const std::vector<AnimatorClipRef>& clipRefs,
	const AnimatorClipAssetResolver& assetResolver,
	const Skeleton* originSkeleton,
	std::unordered_map<std::string, VansAnimationClip>& outClips,
	std::string& error)
{
	outClips.clear();
	error.clear();
	if (!assetResolver)
	{
		error = "Animator Clip memory-asset resolver is not configured";
		return false;
	}

	for (const AnimatorClipRef& ref : clipRefs)
	{
		std::shared_ptr<const VansAnimationClipAsset> asset;
		if (!assetResolver(ref, asset, error) || !asset)
		{
			if (error.empty())
				error = "Cannot resolve Animation Clip memory object '" + ref.name
					+ "' (" + ref.assetGuid + ")";
			outClips.clear();
			return false;
		}

		std::string layoutError;
		if (!IsSkeletonHierarchyValid(asset->skeleton, &layoutError) ||
			(originSkeleton && !asset->skeleton.MatchesAnimationLayout(*originSkeleton, &layoutError)))
		{
			error = "Animation Clip '" + ref.name + "' (" + ref.assetGuid
				+ ") has an incompatible Skeleton: " + layoutError;
			outClips.clear();
			return false;
		}

		const std::string key = ref.name.empty() ? asset->clip.clipName : ref.name;
		outClips[key] = asset->clip;
	}

	VANS_LOG("[ClipLoader] Loaded " << outClips.size() << "/" << clipRefs.size()
		<< " clips from memory asset refs");
	return true;
}

bool VansAnimationClipLoader::ExtractClipsFromFBX(
	const std::string& fbxPath,
	const Skeleton& skeleton,
	std::vector<VansAnimationClip>& outClips)
{
	return VansSkinnedMeshLoader::ExtractExternAnimationClips(fbxPath, skeleton, outClips);
}
