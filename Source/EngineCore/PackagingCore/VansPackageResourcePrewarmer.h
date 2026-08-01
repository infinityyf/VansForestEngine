#pragma once

#include "../AssetCore/VansAssetDatabase.h"
#include "../SceneCore/VansSceneResourcePlan.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
	struct VansPackageResourcePrewarmResult
	{
		std::uint32_t meshChecked = 0;
		std::uint32_t meshCooked = 0;
		std::uint32_t meshUpToDate = 0;
		std::uint32_t meshNotEligible = 0;
		std::uint32_t meshFailed = 0;
		std::uint32_t textureChecked = 0;
		std::uint32_t textureCooked = 0;
		std::uint32_t textureUpToDate = 0;
		std::uint32_t textureNotEligible = 0;
		std::uint32_t textureFailed = 0;
		std::vector<std::string> errors;

		bool ChangedArtifacts() const
		{
			return meshCooked > 0 || textureCooked > 0;
		}
	};

	class VansPackageResourcePrewarmer
	{
	public:
		static VansPackageResourcePrewarmResult Prewarm(
			const std::filesystem::path& projectRoot,
			const VansAssetDatabase& database,
			const VansAssetDatabase& builtInAssetDatabase,
			const VansSceneResourceBuildPlan& resourcePlan);
	};
}
