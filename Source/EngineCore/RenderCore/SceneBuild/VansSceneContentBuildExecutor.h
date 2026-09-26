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

	enum class VansSceneContentBuildFailure
	{
		None,
		ProjectionFailed,
		ObjectBuildFailed,
		ConfiguredRenderNodeBuildFailed,
		ProjectCameraSettingsFailed,
		SplineFieldBuildFailed,
		TerrainBuildFailed,
		VegetationBuildFailed,
		WaterBuildFailed,
		DeferredNodeBuildFailed,
		ScreenSpaceNodeBuildFailed
	};

	struct VansSceneContentBuildResult
	{
		bool m_Built = false;
		VansSceneContentBuildFailure m_Failure = VansSceneContentBuildFailure::None;
		std::string m_Error;
	};

	class VansSceneContentBuildExecutor
	{
	public:
		static VansSceneContentBuildResult BuildFromDocument(
			VansScene& scene,
			const Vans::VansSerializedValue& sceneDocument,
			const std::filesystem::path& sceneSourcePath,
			VansVKDevice& device);

	private:
		static VansSceneContentBuildResult BuildFromPlan(
			VansScene& scene,
			VkDevice& nativeDevice,
			VansVKDevice& device,
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
