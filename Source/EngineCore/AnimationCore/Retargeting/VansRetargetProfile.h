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
	};
}
