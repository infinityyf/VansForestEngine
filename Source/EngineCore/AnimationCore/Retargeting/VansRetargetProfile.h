#pragma once

#include <string>
#include <vector>

namespace VansGraphics
{
	enum class VansRetargetTranslationScaleMode
	{
		AutoPelvis,
		CompatibleSkeleton,
		Explicit
	};

	enum class VansRetargetRootAlignment
	{
		None,
		FeetToOwner
	};

	enum class VansRetargetModelSpaceAlignment
	{
		None,
		SourceBindPose
	};

	struct VansRetargetLimbChainDesc
	{
		std::string name;
		std::string sourceRoot;
		std::string sourceMid;
		std::string sourceTip;
		std::string targetChainId;
		float positionWeight = 1.0f;
	};

	struct VansRetargetControlBoneDesc
	{
		std::string bone;
		std::string referenceBone;
	};

	struct VansRetargetProfileAsset
	{
		std::string name;
		VansRetargetTranslationScaleMode translationScaleMode =
			VansRetargetTranslationScaleMode::AutoPelvis;
		float explicitTranslationScale = 1.0f;
		VansRetargetRootAlignment rootAlignment = VansRetargetRootAlignment::None;
		VansRetargetModelSpaceAlignment targetModelSpaceAlignment =
			VansRetargetModelSpaceAlignment::None;
		std::vector<VansRetargetLimbChainDesc> limbChains;
		// 重映射结束后从目标参考骨重建控制帧，供目标程序化图使用。
		std::vector<VansRetargetControlBoneDesc> controlBones;
	};
}
