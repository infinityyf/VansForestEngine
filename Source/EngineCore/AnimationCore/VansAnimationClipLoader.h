#pragma once

#include "VansAnimationTypes.h"
#include "VansAnimatorIO.h"
#include <string>
#include <vector>
#include <unordered_map>

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
		// 从 .vclip 文件加载单个 clip（不含骨骼信息，仅关键帧数据）
		static bool LoadClip(const std::string& filePath,
		                     VansAnimationClip& outClip);

		// 从 .vclip 文件加载单个 clip 及其骨骼信息
		static bool LoadClipWithSkeleton(const std::string& filePath,
		                                  VansAnimationClip& outClip,
		                                  Skeleton& outSkeleton);

		// 根据 AnimatorClipRef 列表批量加载 clips
		// 返回值: name → clip 映射表
		static std::unordered_map<std::string, VansAnimationClip>
		LoadClipsFromRefs(const std::vector<AnimatorClipRef>& clipRefs);

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
	};

}  // namespace VansGraphics
