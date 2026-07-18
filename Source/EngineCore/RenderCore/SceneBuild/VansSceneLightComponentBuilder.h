#pragma once

#include "../VansScene.h"

#include <functional>

namespace VansGraphics
{
	class VansSceneLightComponentBuilder
	{
	public:
		static void BuildLights(
			VansScene& scene,
			VansScriptObject& object,
			const json& components,
			const std::string& projectRoot,
			const std::function<void()>& ensureObjectTransform);

		static void BindExplicitVideoComponentToRectLight(
			VansScene& scene,
			VansScriptObject& object);
	};
}
