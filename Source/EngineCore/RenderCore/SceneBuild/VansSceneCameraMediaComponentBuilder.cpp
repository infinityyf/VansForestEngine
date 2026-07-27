#include "VansSceneCameraMediaComponentBuilder.h"

#include "../../AudioCore/VansAudioManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VansCamera.h"
#include "../VansVideoManager.h"
#include "../VulkanCore/VansVideoTexture.h"

namespace VansGraphics
{
void VansSceneCameraMediaComponentBuilder::BuildCameraAudioVideo(
	VansScene& scene,
	VansScriptObject& object,
	const Vans::VansSceneCameraMediaComponentConfig& components,
	const std::function<void()>& ensureObjectTransform)
{
	if (components.camera)
	{
		ensureObjectTransform();
		VansCamera* camera = scene.GetCamera();

		if (camera == nullptr)
		{
			VANS_LOG_WARN("[LoadSceneObjects] Camera component found but scene camera is null, skipped");
		}
		else
		{
			const Vans::VansSceneCameraComponentConfig& cameraConfig = *components.camera;
			if (cameraConfig.fov) camera->SetFov(*cameraConfig.fov);
			if (cameraConfig.nearClip) camera->SetNearClip(*cameraConfig.nearClip);
			if (cameraConfig.farClip) camera->SetFarClip(*cameraConfig.farClip);

			camera->SetTransformID(object.m_TransformID);

			auto* cameraComp = new VansScriptCameraComponent();
			cameraComp->m_Camera = camera;
			object.AddComponent(cameraComp);

			VANS_LOG("[LoadSceneObjects] Camera component attached to object: "
				<< object.m_ObjectName << ", TransformID=" << object.m_TransformID);
		}
	}

	if (components.audio)
	{
		const std::string& audioName = components.audio->sourceName;
		VansEngine::VansAudioManager* audioManager = scene.GetAudioManager();
		VansEngine::VansAudioNode* audioNode = audioManager ? audioManager->Get(audioName) : nullptr;
		if (audioNode)
		{
			ensureObjectTransform();
			auto* audioComp = new VansScriptAudioComponent();
			audioComp->m_AudioNode = audioNode;
			audioComp->m_AudioManager = audioManager;
			object.AddComponent(audioComp);
		}
		else
		{
			VANS_LOG_WARN("[LoadSceneObjects] Audio node not found '" << audioName
				<< "' for object: " << object.m_ObjectName);
		}
	}

	if (components.video)
	{
		const std::string& videoName = components.video->sourceName;
		if (object.GetComponent<VansScriptVideoComponent>() != nullptr)
		{
			return;
		}

		VansVideoManager* videoManager = scene.GetVideoManager();
		VansVideoTexture* videoTex = videoManager ? videoManager->Get(videoName) : nullptr;
		if (videoTex)
		{
			auto* videoComp = new VansScriptVideoComponent();
			videoComp->m_VideoName = videoName;
			videoComp->m_VideoTex = videoTex;
			videoComp->m_VideoManager = videoManager;
			object.AddComponent(videoComp);
			VANS_LOG("[LoadSceneObjects] Video component '" << videoName
				<< "' attached to object: " << object.m_ObjectName);
		}
		else
		{
			VANS_LOG_WARN("[LoadSceneObjects] Video resource not found '" << videoName
				<< "' for object: " << object.m_ObjectName);
		}
	}
}

}
