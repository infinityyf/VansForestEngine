#pragma once

#include "../VansScene.h"

#include <functional>
#include "../../SceneCore/VansSceneCameraMediaComponentConfig.h"

class VansScriptAudioComponent;
class VansScriptCameraComponent;
class VansScriptVideoComponent;

namespace VansGraphics
{
	struct VansSceneCameraMediaBuildResult
	{
		VansScriptCameraComponent* camera = nullptr;
		VansScriptAudioComponent* audio = nullptr;
		VansScriptVideoComponent* video = nullptr;
	};

	class VansSceneCameraMediaComponentBuilder
	{
	public:
		static VansSceneCameraMediaBuildResult BuildCameraAudioVideo(
			VansScene& scene,
			VansScriptObject& object,
			const Vans::VansSceneCameraMediaComponentConfig& components,
			const std::function<void()>& ensureObjectTransform);

	};
}
