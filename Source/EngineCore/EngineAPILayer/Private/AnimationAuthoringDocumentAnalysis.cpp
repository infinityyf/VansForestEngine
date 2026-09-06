#include "../Public/AnimationAuthoringDocumentAnalysis.h"

#include "../../AnimationCore/Serialization/VansRetargetProfileJsonCodec.h"
#include "../../AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../../AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AssetCore/VansAssetDatabase.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Vans::EditorAPI
{
	AnimationAuthoringDocumentAnalysis AnalyzeAnimationAuthoringDocument(
		const std::string& sourcePath,
		const std::string& canonicalJson)
	{
		AnimationAuthoringDocumentAnalysis result;
		nlohmann::json root;
		try
		{
			root = nlohmann::json::parse(canonicalJson);
		}
		catch (const std::exception& exception)
		{
			result.message = exception.what();
			return result;
		}

		std::string error;
		switch (VansAssetDatabase::Classify(std::filesystem::path(sourcePath)))
		{
		case VansAssetType::AnimationRig:
		{
			VansGraphics::VansAnimationRigAsset asset;
			if (!VansGraphics::VansAnimationRigStorage::DeserializeFromJsonObject(
				root, asset, error)) break;
			if (!asset.skeletonGuid.empty())
				result.dependencies.push_back(asset.skeletonGuid);
			for (const auto& profile : asset.attachmentProfiles)
				if (!profile.modelGuid.empty() &&
					std::find(result.dependencies.begin(), result.dependencies.end(),
						profile.modelGuid) == result.dependencies.end())
					result.dependencies.push_back(profile.modelGuid);
			result.success = true;
			return result;
		}
		case VansAssetType::RetargetProfile:
		{
			VansGraphics::VansRetargetProfileAsset asset;
			if (VansGraphics::VansRetargetProfileJsonCodec::Decode(root, asset, error))
			{
				result.success = true;
				return result;
			}
			break;
		}
		case VansAssetType::AnimatorController:
		{
			VansGraphics::AnimatorAssetData asset;
			if (VansGraphics::VansAnimatorIO::DeserializeFromJsonObject(root, asset, error))
			{
				result.success = true;
				return result;
			}
			break;
		}
		case VansAssetType::BoneMask:
		{
			VansGraphics::VansBoneMaskAsset asset;
			if (VansGraphics::VansBoneMaskStorage::DeserializeFromJsonObject(root, asset, error))
			{
				result.success = true;
				return result;
			}
			break;
		}
		default:
			error = "Document is not an Animation authoring asset";
			break;
		}
		result.message = error.empty() ? "Animation authoring document is invalid" : error;
		return result;
	}
}
