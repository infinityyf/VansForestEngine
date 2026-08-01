#pragma once

#include "../AssetCore/VansAssetDatabase.h"
#include "VansSceneResourcePlan.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
	struct VansSceneAssetDependencyBuildResult
	{
		bool success = false;
		VansSceneResourceBuildPlan resourcePlan;
		std::unordered_set<std::string> requiredModels;
		std::unordered_set<std::string> requiredMaterials;
		std::unordered_set<std::string> requiredTextures;
		std::unordered_set<std::string> requiredShaders;
	};

	class VansSceneAssetDependencyBuilder
	{
	public:
		static VansSceneAssetDependencyBuildResult BuildResourcePlan(
			VansAssetDatabase& database,
			const std::filesystem::path& scenePath,
			const std::unordered_map<std::string, std::string>& runtimeAssetBindings,
			VansAssetDatabase* builtInAssetDatabase = nullptr);
	};
}
