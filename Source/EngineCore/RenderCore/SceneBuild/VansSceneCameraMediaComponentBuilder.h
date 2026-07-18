#pragma once

#include "../VansScene.h"

#include <functional>

namespace VansGraphics
{
	class VansSceneCameraMediaComponentBuilder
	{
	public:
		static void BuildCameraAudioVideo(
			VansScene& scene,
			VansScriptObject& object,
			const json& components,
			const std::function<void()>& ensureObjectTransform);
	};
}
