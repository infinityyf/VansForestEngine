#include "VansSceneCameraMediaComponentBuilder.h"

#include "../../AudioCore/VansAudioManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"
#include "../VansCamera.h"
#include "../VansVideoManager.h"
#include "../VulkanCore/VansVideoTexture.h"

namespace VansGraphics
{
VansSceneCameraMediaDependencies VansSceneCameraMediaComponentBuilder::ResolveDependencies(
	VansScene& scene,
	const Vans::VansSceneCameraMediaComponentConfig& components)
{
	VansSceneCameraMediaDependencies dependencies;
	if (components.camera)
	{
		dependencies.camera = scene.GetCamera();
		if (!dependencies.camera)
		{
			dependencies.success = false;
			dependencies.error = "Camera component requires the scene camera";
			return dependencies;
		}
	}

	if (components.audio)
	{
		const std::string& assetGuid = components.audio->assetGuid;
		dependencies.audioManager = scene.GetAudioManager();
		dependencies.audioNode = dependencies.audioManager
			? dependencies.audioManager->Get(assetGuid) : nullptr;
		if (assetGuid.empty() || !dependencies.audioNode)
		{
			dependencies.success = false;
			dependencies.error = "Audio asset is unavailable: '" + assetGuid + "'";
			return dependencies;
		}
	}

	if (components.video)
	{
		const std::string& assetGuid = components.video->assetGuid;
		dependencies.videoManager = scene.GetVideoManager();
		dependencies.videoTexture = dependencies.videoManager
			? dependencies.videoManager->GetByAssetGuid(assetGuid) : nullptr;
		if (assetGuid.empty() || !dependencies.videoTexture)
		{
			dependencies.success = false;
			dependencies.error = "Video asset is unavailable: '" + assetGuid + "'";
			return dependencies;
		}
	}
	return dependencies;
}

VansSceneCameraMediaBuildResult VansSceneCameraMediaComponentBuilder::Build(
	VansScriptObject& object,
	const Vans::VansSceneCameraMediaComponentConfig& components,
	const VansSceneCameraMediaDependencies& dependencies,
	const std::function<void()>& ensureObjectTransform)
{
	VansSceneCameraMediaBuildResult result;
	if (components.camera)
	{
		ensureObjectTransform();
		const Vans::VansSceneCameraComponentConfig& cameraConfig = *components.camera;
		if (cameraConfig.fov) dependencies.camera->SetFov(*cameraConfig.fov);
		if (cameraConfig.nearClip) dependencies.camera->SetNearClip(*cameraConfig.nearClip);
		if (cameraConfig.farClip) dependencies.camera->SetFarClip(*cameraConfig.farClip);

		dependencies.camera->SetTransformID(object.m_TransformID);

		auto* cameraComp = new VansScriptCameraComponent();
		cameraComp->m_Camera = dependencies.camera;
		cameraComp->m_TransformID = object.m_TransformID;
		object.AddComponent(cameraComp);
		result.camera = cameraComp;

		VANS_LOG("[LoadSceneObjects] Camera component attached to object: "
			<< object.m_ObjectName << ", TransformID=" << object.m_TransformID);
	}

	if (components.audio)
	{
		const std::string& assetGuid = components.audio->assetGuid;
		ensureObjectTransform();
		auto* audioComp = new VansScriptAudioComponent();
		const bool bound = audioComp->m_Source.Bind(
			dependencies.audioManager, dependencies.audioNode, assetGuid);
		auto* voice = audioComp->m_Source.GetVoice();
		if (voice)
			voice->SetLowpassHighFrequencyGain(components.audio->lowpassHighFrequencyGain);
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
		if (voice && dependencies.audioNode->IsAutoPlay())
			voice->Play();
		object.AddComponent(audioComp);
		result.audio = audioComp;
		if (bound)
			VANS_LOG("[LoadSceneObjects] Audio component '" << assetGuid
				<< "' attached to object: " << object.m_ObjectName);
		else
			VANS_LOG_ERROR("[LoadSceneObjects] Audio component '" << assetGuid
				<< "' could not create an independent voice for object: " << object.m_ObjectName);
	}

	if (components.video)
	{
		const std::string& assetGuid = components.video->assetGuid;
		auto* videoComp = new VansScriptVideoComponent();
		videoComp->m_VideoAssetGuid = assetGuid;
		videoComp->m_VideoTex = dependencies.videoTexture;
		videoComp->m_VideoManager = dependencies.videoManager;
		object.AddComponent(videoComp);
		result.video = videoComp;
		VANS_LOG("[LoadSceneObjects] Video component '" << assetGuid
			<< "' attached to object: " << object.m_ObjectName);
	}
	return result;
}

}
