#pragma once

#include "../AssetCore/VansAssetDatabase.h"
#include "VansSceneResourcePlan.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
	class VansAssetObjectRepository;

	struct VansSceneAssetDependencyBuildResult
	{
		bool success = false;
		VansSceneResourceBuildPlan resourcePlan;
		std::unordered_set<std::string> requiredModels;
		std::unordered_set<std::string> requiredMaterials;
		std::unordered_set<std::string> requiredTextures;
		std::unordered_set<std::string> requiredShaders;
		std::unordered_set<std::string> requiredSkinProfiles;
		std::unordered_set<std::string> requiredAssets;
		std::vector<std::string> errors;
	};

	class VansSceneAssetDependencyBuilder
	{
	public:
		static VansSceneAssetDependencyBuildResult BuildResourcePlan(
			VansAssetDatabase& database,
			const VansSerializedValue& sceneDocument,
			const std::filesystem::path& sceneSourcePath,
			const std::unordered_map<std::string, std::string>& runtimeAssetBindings,
			const VansAssetObjectRepository& objectRepository,
			VansAssetDatabase* builtInAssetDatabase = nullptr);
	};
}
