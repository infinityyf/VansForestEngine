#pragma once

#include "../VansScene.h"

#include <functional>
#include "../../SceneCore/VansSceneLightComponentConfig.h"

class VansScriptDirectionalLightComponent;
class VansScriptPointLightComponent;
class VansScriptRectLightComponent;
class VansScriptSpotLightComponent;

namespace VansGraphics
{
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
		static VansSceneLightBuildResult BuildLights(
			VansScene& scene,
			VansScriptObject& object,
			const Vans::VansSceneLightComponentConfig& config,
			const std::string& projectRoot,
			const std::function<void()>& ensureObjectTransform);

		static void BindExplicitVideoComponentToRectLight(
			VansScene& scene,
			VansScriptObject& object);
	};
}
