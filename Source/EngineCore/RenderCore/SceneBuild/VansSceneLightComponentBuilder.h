#pragma once

#include "../VansScene.h"

#include <functional>
#include <string>
#include "../../SceneCore/VansSceneLightComponentConfig.h"

class VansScriptDirectionalLightComponent;
class VansScriptPointLightComponent;
class VansScriptRectLightComponent;
class VansScriptSpotLightComponent;

namespace Vans
{
	class VansAssetObjectRepository;
}

namespace VansGraphics
{
	class VansTexture;

	struct VansSceneLightDependencies
	{
		int pointIesProfileIndex = -1;
		int spotIesProfileIndex = -1;
		VansTexture* rectEmissiveTexture = nullptr;
	};

	struct VansSceneLightBuildResult
	{
		VansScriptDirectionalLightComponent* directionalLight = nullptr;
		VansScriptPointLightComponent* pointLight = nullptr;
		VansScriptSpotLightComponent* spotLight = nullptr;
		VansScriptRectLightComponent* rectLight = nullptr;
	};

	class VansSceneLightComponentBuilder
	{
	public:
		static VansSceneLightDependencies ResolveDependencies(
			VansScene& scene,
			const Vans::VansSceneLightComponentConfig& config,
			const Vans::VansAssetObjectRepository& repository,
			VansIESProfileManager& iesProfileManager,
			const std::string& objectName);

		static VansSceneLightBuildResult BuildLights(
			VansScene& scene,
			VansScriptObject& object,
			const Vans::VansSceneLightComponentConfig& config,
			const VansSceneLightDependencies& dependencies,
			const std::function<void()>& ensureObjectTransform);

		static void BindVideo(
			VansScene& scene,
			VansScriptObject& object);
	};
}
