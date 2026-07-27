#pragma once

#include "../VansScene.h"

#include <functional>
#include "../../SceneCore/VansSceneLightComponentConfig.h"

namespace VansGraphics
{
	class VansSceneLightComponentBuilder
	{
	public:
		static void BuildLights(
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
