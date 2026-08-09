#pragma once

#include "VansAnimationTypes.h"
#include "VansAnimatorIO.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>

namespace VansGraphics
{
	// ────────────────────────────────────────────────────────────────
	//  VansAnimationClipLoader
	//
	//  高层 clip 加载工具类。封装 VansAnimationClipIO 和
	//  VansSkinnedMeshLoader，提供统一的 clip 加载接口。
	//  Controller / SceneLoader 调用此类获取 clip 数据。
	// ────────────────────────────────────────────────────────────────

	class VansAnimationClipLoader
	{
	public:
		using AnimatorClipPathResolver = std::function<bool(
			const AnimatorClipRef&, std::filesystem::path&, std::string&)>;

		// 从 .vclip 文件加载单个 clip（不含骨骼信息，仅关键帧数据）
		// originSkeleton: mesh 的原始骨骼，当 .vclip 不存在需回退到 FBX 时使用，确保骨骼索引一致
		static bool LoadClip(const std::string& filePath,
		                     VansAnimationClip& outClip,
		                     const Skeleton* originSkeleton = nullptr);

		// 从 .vclip 文件加载单个 clip 及其骨骼信息
		static bool LoadClipWithSkeleton(const std::string& filePath,
		                                  VansAnimationClip& outClip,
		                                  Skeleton& outSkeleton);

		// 批量加载 Animator Clip。路径解析由资产数据库边界提供；任一依赖失败则整体失败。
		static bool LoadClipsFromRefs(
			const std::vector<AnimatorClipRef>& clipRefs,
			const AnimatorClipPathResolver& pathResolver,
			const Skeleton* originSkeleton,
			std::unordered_map<std::string, VansAnimationClip>& outClips,
			std::string& error);

		// 扫描指定目录下所有 .vclip 文件并加载
		static std::vector<VansAnimationClip>
		LoadClipsFromDirectory(const std::string& directoryPath);

		// 从外部 FBX 文件提取动画 clip（利用已有骨骼）
		static bool ExtractClipsFromFBX(const std::string& fbxPath,
		                                const Skeleton& skeleton,
		                                std::vector<VansAnimationClip>& outClips);

		// 快速读取 .vclip 文件的元信息（不加载关键帧）
		static bool PeekClipInfo(const std::string& filePath,
		                          VansAnimationClipInfo& outInfo);

	private:
		// 当 .vclip 文件不存在时，尝试从同目录下的 FBX 提取并创建
		// 命名约定: {fbxBaseName}_{clipName}.vclip → {fbxBaseName}.fbx
		// originSkeleton: 必须传入 mesh 的原始骨骼，以保证 clip 的骨骼索引与 mesh 一致
		static bool TryCreateFromFBX(const std::string& vclipPath,
		                              VansAnimationClip& outClip,
		                              const Skeleton* originSkeleton = nullptr);
	};

}  // namespace VansGraphics
