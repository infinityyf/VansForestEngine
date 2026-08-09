#include "VansSceneCameraMediaComponentBuilder.h"

#include "../../AudioCore/VansAudioManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VansCamera.h"
#include "../VansVideoManager.h"
#include "../VulkanCore/VansVideoTexture.h"

namespace VansGraphics
{
VansSceneCameraMediaBuildResult VansSceneCameraMediaComponentBuilder::BuildCameraAudioVideo(
	VansScene& scene,
	VansScriptObject& object,
	const Vans::VansSceneCameraMediaComponentConfig& components,
	const std::function<void()>& ensureObjectTransform)
{
	VansSceneCameraMediaBuildResult result;
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
			result.camera = cameraComp;

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
			audioComp->m_Source.Bind(audioManager, audioNode, audioName);
			audioComp->m_Source.SetLowpassHighFrequencyGain(
				components.audio->lowpassHighFrequencyGain);
			audioComp->m_OcclusionSettings.enabled = components.audio->occlusionEnabled;
			audioComp->m_OcclusionSettings.blockedGain = components.audio->occlusionGain;
			audioComp->m_OcclusionSettings.blockedHighFrequencyGain =
				components.audio->occlusionHighFrequencyGain;
			audioComp->m_OcclusionSettings.material = components.audio->occlusionMaterial;
			audioComp->m_OcclusionSettings.materialThickness =
				components.audio->occlusionMaterialThickness;
			audioComp->m_OcclusionSettings.attackSeconds = components.audio->occlusionAttack;
			audioComp->m_OcclusionSettings.releaseSeconds = components.audio->occlusionRelease;
			audioComp->m_OcclusionSettings.queryIntervalSeconds =
				components.audio->occlusionQueryInterval;
			audioComp->m_OcclusionSettings.maxQueryDistance =
				components.audio->occlusionMaxDistance;
			audioComp->m_OcclusionSettings.maxQueriesPerFrame =
				components.audio->occlusionMaxQueriesPerFrame;
			audioComp->m_OcclusionSettings.Normalize();
			audioComp->m_ConeSettings.enabled = components.audio->coneEnabled;
			audioComp->m_ConeSettings.innerAngleDegrees = components.audio->coneInnerAngle;
			audioComp->m_ConeSettings.outerAngleDegrees = components.audio->coneOuterAngle;
			audioComp->m_ConeSettings.outerGain = components.audio->coneOuterGain;
			audioComp->m_ConeSettings.Normalize();
			audioComp->m_DopplerEnabled = components.audio->dopplerEnabled;
			if (audioComp->m_Source.UsesIndependentPlayback() && audioNode->IsAutoPlay())
				audioComp->m_Source.Play();
			object.AddComponent(audioComp);
			result.audio = audioComp;
			VANS_LOG("[LoadSceneObjects] Audio component '" << audioName
				<< "' attached to object: " << object.m_ObjectName);
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
			return result;
		}

		VansVideoManager* videoManager = scene.GetVideoManager();
		VansVideoTexture* videoTex = videoManager ? videoManager->Get(videoName) : nullptr;
		if (!videoTex && videoManager)
			videoTex = videoManager->GetByAssetGuid(videoName);
		if (videoTex)
		{
			auto* videoComp = new VansScriptVideoComponent();
			videoComp->m_VideoName = videoName;
			videoComp->m_VideoTex = videoTex;
			videoComp->m_VideoManager = videoManager;
			object.AddComponent(videoComp);
			result.video = videoComp;
			VANS_LOG("[LoadSceneObjects] Video component '" << videoName
				<< "' attached to object: " << object.m_ObjectName);
		}
		else
		{
			VANS_LOG_WARN("[LoadSceneObjects] Video resource not found '" << videoName
			<< "' for object: " << object.m_ObjectName);
		}
	}
	return result;
}

}
