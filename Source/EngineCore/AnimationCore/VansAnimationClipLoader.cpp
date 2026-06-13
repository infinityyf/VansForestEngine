#include "VansAnimationClipLoader.h"
#include "VansAnimationClip.h"
#include "VansSkinnedMeshLoader.h"
#include "../Util/VansLog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/anim.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <algorithm>
#include <cmath>

using namespace VansGraphics;

static bool IsSkeletonHierarchyValid(const Skeleton& skeleton, std::string* reason = nullptr)
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
			rootCount++;
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

static bool MatchesOriginSkeleton(const Skeleton& cached, const Skeleton& origin, std::string* reason = nullptr)
{
	if (cached.bones.size() != origin.bones.size())
	{
		if (reason) *reason = "bone count mismatch";
		return false;
	}

	for (size_t i = 0; i < origin.bones.size(); ++i)
	{
		if (cached.bones[i].name != origin.bones[i].name)
		{
			if (reason) *reason = "bone name mismatch at index " + std::to_string(i);
			return false;
		}

		if (cached.bones[i].parentIndex != origin.bones[i].parentIndex)
		{
			if (reason) *reason = "parent mismatch for bone: " + origin.bones[i].name;
			return false;
		}

		const float* cachedLocal = &cached.bones[i].localTransform[0][0];
		const float* originLocal = &origin.bones[i].localTransform[0][0];
		for (int e = 0; e < 16; ++e)
		{
			if (std::abs(cachedLocal[e] - originLocal[e]) > 0.0001f)
			{
				if (reason) *reason = "bind-pose local transform mismatch for bone: " + origin.bones[i].name;
				return false;
			}
		}
	}

	return true;
}

// ════════════════════════════════════════════════════════════════
//  LoadClip (不含骨骼)
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::LoadClip(const std::string& filePath,
                                        VansAnimationClip& outClip,
                                        const Skeleton* originSkeleton)
{
	Skeleton tempSkeleton;
	bool ok = VansAnimationClipIO::Load(filePath, outClip, tempSkeleton);
	if (ok)
	{
		std::string reason;
		bool cacheValid = IsSkeletonHierarchyValid(tempSkeleton, &reason);
		if (cacheValid && originSkeleton)
			cacheValid = MatchesOriginSkeleton(tempSkeleton, *originSkeleton, &reason);

		if (!cacheValid)
		{
			VANS_LOG_WARN("[ClipLoader] Cached .vclip skeleton is stale or invalid ("
			              << reason << "), recreating from FBX: " << filePath);
			ok = TryCreateFromFBX(filePath, outClip, originSkeleton);
			if (!ok)
				VANS_LOG_WARN("[ClipLoader] Failed to recreate invalid cached clip: " << filePath);
		}
	}
	if (!ok)
	{
		// .vclip 文件不存在或损坏，尝试从 FBX 提取并创建
		VANS_LOG_WARN("[ClipLoader] .vclip not found or corrupt, trying to create from FBX: " << filePath);
		ok = TryCreateFromFBX(filePath, outClip, originSkeleton);
		if (!ok)
			VANS_LOG_WARN("[ClipLoader] Failed to load or create clip: " << filePath);
	}
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
	if (ok)
	{
		std::string reason;
		if (!IsSkeletonHierarchyValid(outSkeleton, &reason))
		{
			VANS_LOG_WARN("[ClipLoader] Cached .vclip skeleton is invalid ("
			              << reason << "), recreating from FBX: " << filePath);
			ok = TryCreateFromFBX(filePath, outClip);
			if (ok)
				ok = VansAnimationClipIO::Load(filePath, outClip, outSkeleton);
		}
	}
	if (!ok)
	{
		// .vclip 文件不存在或损坏，尝试从 FBX 提取并创建
		VANS_LOG_WARN("[ClipLoader] .vclip not found or corrupt, trying to create from FBX: " << filePath);
		ok = TryCreateFromFBX(filePath, outClip);
		if (ok)
		{
			// 重新加载以获取骨骼信息
			ok = VansAnimationClipIO::Load(filePath, outClip, outSkeleton);
		}
		if (!ok)
			VANS_LOG_WARN("[ClipLoader] Failed to load or create clip+skeleton: " << filePath);
	}
	return ok;
}

// ════════════════════════════════════════════════════════════════
//  LoadClipsFromRefs (批量加载)
// ════════════════════════════════════════════════════════════════

std::unordered_map<std::string, VansAnimationClip>
VansAnimationClipLoader::LoadClipsFromRefs(const std::vector<AnimatorClipRef>& clipRefs, const std::string& pathPrefix,
                                           const Skeleton* originSkeleton)
{
	std::unordered_map<std::string, VansAnimationClip> result;

	for (const auto& ref : clipRefs)
	{
		// 将相对路径拼接为完整路径
		std::string fullPath = pathPrefix + ref.path;
		VansAnimationClip clip;
		if (LoadClip(fullPath, clip, originSkeleton))
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

// ════════════════════════════════════════════════════════════════
//  TryCreateFromFBX — 从同目录 FBX 提取 clip 并保存为 .vclip
//
//  命名约定 (两种):
//    1. {fbxBaseName}_{clipName}.vclip   (例: character_Idle.vclip)
//    2. {fbxBaseName}.vclip              (例: M_Neutral_Walk_Loop_F.vclip)
//  从 FBX 中提取动画 clip，保存为 .vclip 文件
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::TryCreateFromFBX(const std::string& vclipPath,
                                                VansAnimationClip& outClip,
                                                const Skeleton* originSkeleton)
{
	namespace fs = std::filesystem;

	fs::path clipFilePath(vclipPath);
	std::string clipDir  = clipFilePath.parent_path().string();
	std::string fileName = clipFilePath.stem().string(); // 不含扩展名

	if (clipDir.empty() || fileName.empty())
		return false;

	// 扫描同目录下的 .fbx 文件，找到匹配的 FBX 源文件
	// 匹配条件:
	//   1. vclip 文件名以 "{fbxBaseName}_" 开头 (子剪辑)
	//   2. vclip 文件名完全等于 fbxBaseName (完整动画 FBX 的单剪辑)
	std::string matchedFbxPath;
	std::string matchedClipName;
	bool        exactMatch = false;

	if (!fs::exists(clipDir) || !fs::is_directory(clipDir))
		return false;

	for (const auto& entry : fs::directory_iterator(clipDir))
	{
		if (!entry.is_regular_file())
			continue;

		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (ext != ".fbx")
			continue;

		std::string fbxBaseName = entry.path().stem().string();

		// 优先: 前缀匹配 {fbxBaseName}_{clipName}.vclip
		std::string prefix = fbxBaseName + "_";
		if (fileName.size() > prefix.size() && fileName.substr(0, prefix.size()) == prefix)
		{
			matchedFbxPath = entry.path().string();
			matchedClipName = fileName.substr(prefix.size());
			exactMatch = false;
			break;
		}

		// 其次: 完全匹配 {fbxBaseName}.vclip (整个 FBX 作为一个 clip)
		if (fileName == fbxBaseName)
		{
			matchedFbxPath = entry.path().string();
			matchedClipName = fbxBaseName;
			exactMatch = true;
			break;
		}
	}

	if (matchedFbxPath.empty())
	{
		VANS_LOG_WARN("[ClipLoader] No matching FBX found for: " << vclipPath);
		return false;
	}

	VANS_LOG("[ClipLoader] Found source FBX: " << matchedFbxPath
	         << ", extracting clip '" << matchedClipName << "'");

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(matchedFbxPath,
		aiProcess_Triangulate | aiProcess_FlipUVs);

	if (!scene || !scene->HasAnimations())
	{
		VANS_LOG_WARN("[ClipLoader] Failed to load FBX or no animations: " << matchedFbxPath);
		return false;
	}

	Skeleton extractedSkeleton;
	const Skeleton* skeletonToUse = originSkeleton;
	if (skeletonToUse == nullptr)
	{
		VANS_LOG_WARN("[ClipLoader] No originSkeleton provided, extracting from FBX (bone indices may mismatch): " << matchedFbxPath);
		VansSkinnedMeshLoader::ExtractSkeleton(scene, extractedSkeleton);
		if (extractedSkeleton.bones.empty())
		{
			VANS_LOG_WARN("[ClipLoader] No skeleton in FBX: " << matchedFbxPath);
			return false;
		}
		skeletonToUse = &extractedSkeleton;
	}

	std::string fbxBaseName = fs::path(matchedFbxPath).stem().string();
	bool found = false;

	for (uint32_t i = 0; i < scene->mNumAnimations; i++)
	{
		aiAnimation* anim = scene->mAnimations[i];
		std::string clipName = anim->mName.C_Str();
		if (clipName.empty())
			clipName = fbxBaseName + "_clip" + std::to_string(i);

		for (char& c : clipName)
		{
			if (c == ' ' || c == '/' || c == '\\' || c == ':')
				c = '_';
		}

		VansAnimationClip clip;
		VansSkinnedMeshLoader::ExtractClipFromAssimp(anim, *skeletonToUse, clip);
		clip.clipName = clipName;

		// 总是保存前缀格式: {fbxBaseName}_{clipName}.vclip
		std::string prefixVclipPath = clipDir + "/" + fbxBaseName + "_" + clipName + ".vclip";
		{
			VansAnimationClipIO::Save(prefixVclipPath, clip, *skeletonToUse);
			VANS_LOG("[ClipLoader] Auto-created .vclip: " << prefixVclipPath);
		}

		// 如果是完全匹配模式，也保存为 {fbxBaseName}.vclip (兼容现有命名)
		if (exactMatch && clipName == matchedClipName)
		{
			VansAnimationClipIO::Save(vclipPath, clip, *skeletonToUse);
			VANS_LOG("[ClipLoader] Also saved as exact-match: " << vclipPath);
		}

		if (clipName == matchedClipName)
		{
			outClip = clip;
			found = true;
		}
	}

	if (!found)
	{
		VANS_LOG_WARN("[ClipLoader] Clip '" << matchedClipName
		             << "' not found in FBX: " << matchedFbxPath);
	}
	return found;
}
