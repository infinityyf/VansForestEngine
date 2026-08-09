#pragma once

#include "../Public/EngineDTOs.h"

namespace Vans::EditorAPI
{
	enum class AnimationAuthoringAssetKind { Animator, BoneMask };

	struct AnimationAuthoringAssetCreateRequest
	{
		std::string directoryPath;
		AnimationAuthoringAssetKind kind = AnimationAuthoringAssetKind::Animator;
	};

	struct AnimationAuthoringAssetCreateResult
	{
		bool success = false;
		std::string message;
		std::string assetPath;
	};

	class AnimationAuthoringBridge final
	{
	public:
		static AnimatorDocumentDecodeResult DecodeAnimator(const std::string& canonicalJson);
		static AnimatorDocumentEncodeResult EncodeAnimator(const AnimatorDocumentDTO& document);
		static BoneMaskDocumentDecodeResult DecodeBoneMask(const std::string& canonicalJson);
		static BoneMaskDocumentEncodeResult EncodeBoneMask(const BoneMaskDocumentDTO& document);
		static BoneMaskCompileResult CompileBoneMask(
			const BoneMaskDocumentDTO& document,
			const AssetSkeletonSnapshot& skeleton);
		static AnimationAuthoringAssetCreateResult CreateAsset(
			const AnimationAuthoringAssetCreateRequest& request);
	};
}
