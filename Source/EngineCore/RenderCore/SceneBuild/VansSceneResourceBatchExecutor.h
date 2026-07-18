#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace VansGraphics
{
	class VansScene;
	class VansVKDevice;
}

namespace Vans
{
	struct VansSceneResourceBuildPlan;
}

namespace VansGraphics
{
	class VansSceneResourceBatchExecutor
	{
	public:
		static void Execute(VansScene& scene, const Vans::VansSceneResourceBuildPlan& resourcePlan);
		static void Execute(VansScene& scene, nlohmann::json& resourceData);

	private:
		static void LoadEngineMeshes(VansScene& scene, const std::string& enginePrefix, VansVKDevice* vkDevice);
		static void LoadLegacyAudioVideoShaderBatches(
			VansScene& scene,
			nlohmann::json& resourceData,
			const std::string& assetPrefix);
		static void FinalizeResourceBatch(VansScene& scene);
	};
}
