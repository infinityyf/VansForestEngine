#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneContentBuildPlan.h"
#include "../../SceneCore/VansSceneRenderSettingsConfig.h"

namespace Vans
{
	struct VansProjectMainCameraHiZCullSettings;
}

namespace VansGraphics
{
	class VansVKDevice;

	class VansSceneContentBuildExecutor
	{
	public:
		static bool BuildFromDocument(
			VansScene& scene,
			const Vans::VansSerializedValue& sceneDocument,
			const std::filesystem::path& sceneSourcePath);

	private:
		static bool BuildFromPlan(
			VansScene& scene,
			VkDevice& nativeDevice,
			VansVKDevice* vkDevice,
			const Vans::VansSceneContentBuildPlan& buildPlan,
			const std::filesystem::path& sceneSourcePath,
			const std::string& projectRoot);

		static void ApplyPostProcessSettings(
			VansMaterialManager& materialManager,
			const std::optional<Vans::VansScenePostProcessSettingsConfig>& config);
		static void ApplyMainCameraHiZCullSettings(
			VansScene& scene,
			const std::optional<Vans::VansSceneMainCameraHiZCullSettingsConfig>& config);
		static void ApplyProjectMainCameraHiZCullSettings(
			VansScene& scene,
			const Vans::VansProjectMainCameraHiZCullSettings& projectSettings);
		static void ApplyGISettings(
			VansScene& scene,
			const std::optional<Vans::VansSceneGISettingsConfig>& config);
		static std::string ResolveProjectRootFromScenePath(
			const std::filesystem::path& sceneSourcePath);
	};
}
