#pragma once

#include "VansAnimationTypes.h"
#include "VansAnimationClip.h"
#include "VansAnimatorIO.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace VansGraphics
{
	// ────────────────────────────────────────────────────────────────
	//  VansAnimationClipLoader
	//
	//  高层 clip 内存资产解析与原始 FBX 解码入口。
	// ────────────────────────────────────────────────────────────────

	class VansAnimationClipLoader
	{
	public:
		using AnimatorClipAssetResolver = std::function<bool(
			const AnimatorClipRef&,
			std::shared_ptr<const VansAnimationClipAsset>&,
			std::string&)>;

		// 从已经发布的内存资产批量解析 Animator Clip；任一依赖失败则整体失败。
		static bool LoadClipsFromRefs(
			const std::vector<AnimatorClipRef>& clipRefs,
			const AnimatorClipAssetResolver& assetResolver,
			const Skeleton* originSkeleton,
			std::unordered_map<std::string, VansAnimationClip>& outClips,
			std::string& error);

		// 从外部 FBX 源资源解码动画 clip 到内存（利用已有骨骼）。
		// 该入口不会创建或更新 .vclip 文件。
		static bool ExtractClipsFromFBX(const std::string& fbxPath,
		                                const Skeleton& skeleton,
		                                std::vector<VansAnimationClip>& outClips);
	};

}  // namespace VansGraphics
