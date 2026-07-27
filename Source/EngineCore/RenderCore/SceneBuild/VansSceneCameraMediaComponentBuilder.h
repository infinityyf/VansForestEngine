#pragma once

#include "../VansScene.h"

#include <functional>
#include "../../SceneCore/VansSceneCameraMediaComponentConfig.h"

namespace VansGraphics
{
	class VansSceneCameraMediaComponentBuilder
	{
	public:
		static void BuildCameraAudioVideo(
			VansScene& scene,
			VansScriptObject& object,
			const Vans::VansSceneCameraMediaComponentConfig& components,
			const std::function<void()>& ensureObjectTransform);

	};
}
