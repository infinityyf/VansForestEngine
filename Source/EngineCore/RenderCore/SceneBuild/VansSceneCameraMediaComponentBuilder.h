#pragma once

#include "../VansScene.h"

#include <functional>
#include <string>
#include "../../SceneCore/VansSceneCameraMediaComponentConfig.h"

class VansScriptAudioComponent;
class VansScriptCameraComponent;
class VansScriptVideoComponent;

namespace VansGraphics
{
	struct VansSceneCameraMediaDependencies
	{
		bool success = true;
		std::string error;
		VansCamera* camera = nullptr;
		VansEngine::VansAudioManager* audioManager = nullptr;
		VansEngine::VansAudioNode* audioNode = nullptr;
		VansVideoManager* videoManager = nullptr;
		VansVideoTexture* videoTexture = nullptr;
	};

	struct VansSceneCameraMediaBuildResult
	{
		VansScriptCameraComponent* camera = nullptr;
		VansScriptAudioComponent* audio = nullptr;
		VansScriptVideoComponent* video = nullptr;
	};

	class VansSceneCameraMediaComponentBuilder
	{
	public:
		static VansSceneCameraMediaDependencies ResolveDependencies(
			VansScene& scene,
			const Vans::VansSceneCameraMediaComponentConfig& components);

		static VansSceneCameraMediaBuildResult Build(
			VansScriptObject& object,
			const Vans::VansSceneCameraMediaComponentConfig& components,
			const VansSceneCameraMediaDependencies& dependencies,
			const std::function<void()>& ensureObjectTransform);
	};
}
