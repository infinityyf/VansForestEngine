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
	const json& components,
	const std::function<void()>& ensureObjectTransform)
{
	if (components.contains("camera"))
	{
		ensureObjectTransform();
		VansCamera* camera = scene.GetCamera();

		if (camera == nullptr)
		{
			VANS_LOG_WARN("[LoadSceneObjects] 找到 camera component 但 m_Camera 为 nullptr，跳过");
		}
		else
		{
			const auto& camJson = components["camera"];
			if (camJson.contains("fov"))
				camera->SetFov(camJson["fov"].get<float>());
			if (camJson.contains("nearClip"))
				camera->SetNearClip(camJson["nearClip"].get<float>());
			if (camJson.contains("farClip"))
				camera->SetFarClip(camJson["farClip"].get<float>());

			camera->SetTransformID(object.m_TransformID);

			auto* cameraComp = new VansScriptCameraComponent();
			cameraComp->m_Camera = camera;
			object.AddComponent(cameraComp);

			VANS_LOG("[LoadSceneObjects] Camera component 已挂载到 object: "
				<< object.m_ObjectName << "，TransformID=" << object.m_TransformID);
		}
	}

	if (components.contains("audio"))
	{
		std::string audioName = components["audio"]["source"].get<std::string>();
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
			VANS_LOG_WARN("[LoadSceneObjects] 找不到音频节点 '" << audioName
				<< "'，对象: " << object.m_ObjectName);
		}
	}

	if (components.contains("video"))
	{
		std::string videoName = components["video"]["source"].get<std::string>();
		if (object.GetComponent<VansScriptVideoComponent>() != nullptr)
			return;

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
				<< "' 已挂载到 object: " << object.m_ObjectName);
		}
		else
		{
			VANS_LOG_WARN("[LoadSceneObjects] 找不到视频资源 '" << videoName
				<< "'，对象: " << object.m_ObjectName);
		}
	}
}
}
