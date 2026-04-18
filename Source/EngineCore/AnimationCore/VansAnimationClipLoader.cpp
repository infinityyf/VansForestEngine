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

using namespace VansGraphics;

// ════════════════════════════════════════════════════════════════
//  LoadClip (不含骨骼)
// ════════════════════════════════════════════════════════════════

bool VansAnimationClipLoader::LoadClip(const std::string& filePath,
                                        VansAnimationClip& outClip,
                                        const Skeleton* originSkeleton)
{
	Skeleton tempSkeleton;
	bool ok = VansAnimationClipIO::Load(filePath, outClip, tempSkeleton);
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
//  命名约定: {fbxBaseName}_{clipName}.vclip
//  例: Models/character_Idle.vclip → 查找 Models/character.fbx
//  从 FBX 中提取名为 "Idle" 的动画 clip，保存为 .vclip 文件
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
	// 匹配条件: vclip 文件名以 "{fbxBaseName}_" 开头
	std::string matchedFbxPath;
	std::string matchedClipName;

	if (!fs::exists(clipDir) || !fs::is_directory(clipDir))
		return false;

	for (const auto& entry : fs::directory_iterator(clipDir))
	{
		if (!entry.is_regular_file())
			continue;

		std::string ext = entry.path().extension().string();
		// 不区分大小写比较扩展名
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (ext != ".fbx")
			continue;

		std::string fbxBaseName = entry.path().stem().string();
		std::string prefix = fbxBaseName + "_";

		// 检查 vclip 文件名是否以 "{fbxBaseName}_" 开头
		if (fileName.size() > prefix.size() && fileName.substr(0, prefix.size()) == prefix)
		{
			matchedFbxPath = entry.path().string();
			matchedClipName = fileName.substr(prefix.size());
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

	// 直接从 FBX 文件提取骨骼和动画 clip
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(matchedFbxPath,
		aiProcess_Triangulate | aiProcess_FlipUVs);

	if (!scene || !scene->HasAnimations())
	{
		VANS_LOG_WARN("[ClipLoader] Failed to load FBX or no animations: " << matchedFbxPath);
		return false;
	}

	// 优先使用 mesh 原始骨骼（保证骨骼索引与 mesh 完全一致）
	// 若未提供，则从 FBX 自行提取（骨骼顺序可能不同，导致 clip 骨骼索引错位）
	Skeleton extractedSkeleton;
	const Skeleton* skeletonToUse = originSkeleton;
	if (skeletonToUse == nullptr)
	{
		VANS_LOG_WARN("[ClipLoader] No originSkeleton provided for FBX fallback, extracting from FBX (bone indices may mismatch): " << matchedFbxPath);
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

		// 与 ProcessAnimatedMesh 保持一致的清理逻辑
		for (char& c : clipName)
		{
			if (c == ' ' || c == '/' || c == '\\' || c == ':')
				c = '_';
		}

		VansAnimationClip clip;
		VansSkinnedMeshLoader::ExtractClipFromAssimp(anim, *skeletonToUse, clip);
		clip.clipName = clipName;

		// 保存所有提取的 clip 到 .vclip（顺便缓存同 FBX 中的其他 clip）
		std::string currentVclipPath = clipDir + "/" + fbxBaseName + "_" + clipName + ".vclip";
		// 若已有缓存文件则覆盖，确保骨骼索引使用的是 mesh 原始骨骼
		{
			VansAnimationClipIO::Save(currentVclipPath, clip, *skeletonToUse);
			VANS_LOG("[ClipLoader] Auto-created/updated .vclip: " << currentVclipPath);
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
