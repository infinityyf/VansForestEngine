#pragma once

#include "EngineDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class IAnimationEditorAPI
	{
	public:
		virtual ~IAnimationEditorAPI() = default;
		virtual AnimationAssetBindingSnapshot GetAnimationAssetBinding(
			const std::string& entityGuid) const = 0;
		virtual SceneSkeletonHierarchySnapshot GetSceneSkeletonHierarchy(
			const std::string& entityGuidFilter) const = 0;
		virtual SceneSkeletonNodePoseSnapshot GetSceneSkeletonNodePose(
			const SceneSkeletonNodePoseRequest& request) const = 0;
		virtual SkeletonDebugSnapshot GetSkeletonDebugSnapshot(
			const std::string& entityGuidFilter) const = 0;
		virtual AssetSkeletonSnapshot GetAssetSkeletonSnapshot(
			const std::string& assetGuid) const = 0;
		virtual AnimatorDocumentDecodeResult DecodeAnimatorDocument(
			const std::string& canonicalJson) const = 0;
		virtual AnimatorDocumentEncodeResult EncodeAnimatorDocument(
			const AnimatorDocumentDTO& document) const = 0;
		virtual BoneMaskDocumentDecodeResult DecodeBoneMaskDocument(
			const std::string& canonicalJson) const = 0;
		virtual BoneMaskDocumentEncodeResult EncodeBoneMaskDocument(
			const VansBoneMaskDocumentDTO& document) const = 0;
		virtual AnimationRigDocumentDecodeResult DecodeAnimationRigDocument(
			const std::string& canonicalJson) const = 0;
		virtual AnimationRigDocumentEncodeResult EncodeAnimationRigDocument(
			const AnimationRigDocumentDTO& document) const = 0;
		virtual BoneMaskCompileResult CompileBoneMaskDocument(
			const VansBoneMaskDocumentDTO& document,
			const AssetSkeletonSnapshot& skeleton) const = 0;
	};
}
