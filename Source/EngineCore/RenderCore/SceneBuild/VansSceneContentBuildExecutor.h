#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneContentBuildPlan.h"
#include "../../SceneCore/VansSceneRenderSettingsConfig.h"

namespace VansGraphics
{
	class VansVKDevice;

	class VansSceneContentBuildExecutor
	{
	public:
		static bool BuildFromFile(VansScene& scene, const char* path);

	private:
		static bool BuildFromPlan(
			VansScene& scene,
			VkDevice& nativeDevice,
			VansVKDevice* vkDevice,
			const Vans::VansSceneContentBuildPlan& buildPlan,
			const char* path,
			const std::string& projectRoot);

		static void ApplyHeightFogSettings(
			VansMaterialManager& materialManager,
			const std::optional<Vans::VansSceneHeightFogSettingsConfig>& config);
		static void ApplyVolumetricFogSettings(
			VansMaterialManager& materialManager,
			const std::optional<Vans::VansSceneVolumetricFogSettingsConfig>& config);
		static void ApplyVolumetricCloudSettings(
			VansMaterialManager& materialManager,
			const std::optional<Vans::VansSceneVolumetricCloudSettingsConfig>& config);
		static void ApplyGISettings(
			VansScene& scene,
			const std::optional<Vans::VansSceneGISettingsConfig>& config);
		static std::string ResolveProjectRootFromScenePath(const char* path);
	};
}
