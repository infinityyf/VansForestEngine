#include "VansAnimationClipLoader.h"
#include "VansAnimationClip.h"
#include "VansSkinnedMeshLoader.h"
#include "../Util/VansLog.h"

#include <filesystem>

using namespace VansGraphics;

// ════════════════════════════════════════════════════════════════
//  LoadClip (不含骨骼)
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::LoadClip(const std::string& filePath,
                                        VansAnimationClip& outClip)
{
	Skeleton tempSkeleton;
	bool ok = VansAnimationClipIO::Load(filePath, outClip, tempSkeleton);
	if (!ok)
		VANS_LOG_WARN("[ClipLoader] Failed to load clip: " << filePath);
	return ok;
}

// ════════════════════════════════════════════════════════════════
//  LoadClipWithSkeleton
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::LoadClipWithSkeleton(const std::string& filePath,
                                                    VansAnimationClip& outClip,
                                                    Skeleton& outSkeleton)
{
	bool ok = VansAnimationClipIO::Load(filePath, outClip, outSkeleton);
	if (!ok)
		VANS_LOG_WARN("[ClipLoader] Failed to load clip+skeleton: " << filePath);
	return ok;
}

// ════════════════════════════════════════════════════════════════
//  LoadClipsFromRefs (批量加载)
// ════════════════════════════════════════════════════════════════

std::unordered_map<std::string, VansAnimationClip>
VansAnimationClipLoader::LoadClipsFromRefs(const std::vector<AnimatorClipRef>& clipRefs)
{
	std::unordered_map<std::string, VansAnimationClip> result;

	for (const auto& ref : clipRefs)
	{
		VansAnimationClip clip;
		if (LoadClip(ref.path, clip))
		{
			// 优先用 ref.name 作为 key；如果为空用 clip 自身的名称
			std::string key = ref.name.empty() ? clip.clipName : ref.name;
			result[key] = std::move(clip);
		}
		else
		{
			VANS_LOG_WARN("[ClipLoader] Skipping clip ref: name='" << ref.name
			             << "' path='" << ref.path << "'");
		}
	}

	VANS_LOG("[ClipLoader] Loaded " << result.size() << "/" << clipRefs.size() << " clips from refs");
	return result;
}

// ════════════════════════════════════════════════════════════════
//  LoadClipsFromDirectory (扫描目录)
// ════════════════════════════════════════════════════════════════

std::vector<VansAnimationClip>
VansAnimationClipLoader::LoadClipsFromDirectory(const std::string& directoryPath)
{
	std::vector<VansAnimationClip> clips;

	if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath))
	{
		VANS_LOG_WARN("[ClipLoader] Directory not found: " << directoryPath);
		return clips;
	}

	for (const auto& entry : std::filesystem::directory_iterator(directoryPath))
	{
		if (!entry.is_regular_file())
			continue;

		if (entry.path().extension() != ".vclip")
			continue;

		VansAnimationClip clip;
		if (LoadClip(entry.path().string(), clip))
			clips.push_back(std::move(clip));
	}

	VANS_LOG("[ClipLoader] Loaded " << clips.size() << " clips from directory: " << directoryPath);
	return clips;
}

// ════════════════════════════════════════════════════════════════
//  ExtractClipsFromFBX (从外部 FBX 提取)
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::ExtractClipsFromFBX(const std::string& fbxPath,
                                                   const Skeleton& skeleton,
                                                   std::vector<VansAnimationClip>& outClips)
{
	return VansSkinnedMeshLoader::ExtractExternAnimationClips(fbxPath, skeleton, outClips);
}

// ════════════════════════════════════════════════════════════════
//  PeekClipInfo (元信息)
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::PeekClipInfo(const std::string& filePath,
                                            VansAnimationClipInfo& outInfo)
{
	return VansAnimationClipIO::Peek(filePath, outInfo);
}
