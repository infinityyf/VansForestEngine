#include "VansSceneResourceBatchExecutor.h"

#include "../VansScene.h"
#include "VansSceneProjectResourceBuilder.h"
#include "VansSceneResourceArtifactPrewarmer.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/VansSceneResourceLoadContext.h"
#include "../../SceneCore/VansSceneResourcePlan.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"

#include <chrono>
#include <vector>

namespace VansGraphics
{
namespace
{
	using SceneLoadClock = std::chrono::steady_clock;

	double SceneLoadMsSince(SceneLoadClock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(SceneLoadClock::now() - start).count();
	}

	void LogSceneLoadPhase(const char* phase, SceneLoadClock::time_point start)
	{
		VANS_LOG("[SceneLoadProfile] " << phase << "=" << SceneLoadMsSince(start) << "ms");
	}
}

void VansSceneResourceBatchExecutor::FinalizeResourceBatch(VansScene& scene)
{
	VANS_PROFILE_SCOPE("SceneLoad.FinalizeResourceBatch", Vans::ProfileCategory::RenderPrepare);
	scene.FinalizeProjectResourceBatch();
}

bool VansSceneResourceBatchExecutor::Execute(VansScene& scene, const Vans::VansSceneResourceBuildPlan& resourcePlan)
{
	auto vansConfigration = VansConfigration::GetInstance();
	std::string enginePrefix = vansConfigration->GetProjectRootPath();

	auto& projectMgr = Vans::VansProjectManager::Get();
	std::string assetPrefix = projectMgr.IsProjectLoaded()
		? projectMgr.GetProjectRootPath()
		: enginePrefix;
	if (Vans::VansAssetDatabase* projectDatabase = projectMgr.GetAssetDatabase())
	{
		if (Vans::VansAssetDatabase* builtInDatabase = projectMgr.GetBuiltInAssetDatabase())
		{
			const auto prewarmStart = SceneLoadClock::now();
			const VansSceneResourceArtifactPrewarmResult prewarm =
				VansSceneResourceArtifactPrewarmer::Prewarm(
					assetPrefix,
					*projectDatabase,
					*builtInDatabase,
					resourcePlan);
			LogSceneLoadPhase("resource.artifactPrewarm", prewarmStart);
			if (!prewarm.Succeeded())
			{
				VANS_LOG_WARN("[ResourceArtifactPrewarm] Some resources could not be cached; "
					"Editor source fallback remains enabled");
			}
		}
	}
	const Vans::VansSceneResourceLoadContext loadContext =
		Vans::VansSceneResourceLoadContext::ForEditor(
			assetPrefix,
			enginePrefix,
			projectMgr.EnumerateAssetRecords());
	return Execute(scene, resourcePlan, loadContext);
}

bool VansSceneResourceBatchExecutor::Execute(
	VansScene& scene,
	const Vans::VansSceneResourceBuildPlan& resourcePlan,
	const Vans::VansSceneResourceLoadContext& loadContext)
{
	VANS_PROFILE_SCOPE("SceneLoad.ResourceBatch", Vans::ProfileCategory::IO);
	const auto totalStart = SceneLoadClock::now();

	VansVKDevice* vkDevice = scene.GetRuntimeResourceDevice();
	if (!vkDevice)
	{
		VANS_LOG_ERROR("[VansScene] Cannot load resources without a runtime Vulkan device");
		return false;
	}
	VkDevice nativeDevice = vkDevice->GetLogicDevice();

	auto phaseStart = SceneLoadClock::now();
	if (!VansSceneProjectResourceBuilder::LoadMeshes(scene, resourcePlan.meshes, loadContext, nativeDevice, vkDevice))
	{
		VANS_LOG_ERROR("[SceneResource] Required mesh batch failed; resource finalization aborted.");
		return false;
	}
	LogSceneLoadPhase("resource.meshes", phaseStart);

	if (resourcePlan.includeDefaultTextureSet || !resourcePlan.textures.empty())
	{
		phaseStart = SceneLoadClock::now();
		if (!VansSceneProjectResourceBuilder::LoadTextures(
			scene,
			resourcePlan.textures,
			loadContext,
			vkDevice,
			resourcePlan.includeDefaultTextureSet))
			return false;
		LogSceneLoadPhase("resource.textures", phaseStart);
	}

	phaseStart = SceneLoadClock::now();
	std::vector<Vans::VansSceneAudioResourceRequest> resolvedAudios = resourcePlan.audios;
	for (auto& audio : resolvedAudios)
	{
		const Vans::VansResolvedSceneResourcePath resolved = loadContext.ResolveAudio(audio);
		if (!resolved.valid)
		{
			VANS_LOG_ERROR("[SceneResource] Audio '" << audio.name
				<< "' cannot be resolved from asset index: " << resolved.error);
			return false;
		}
		audio.path = resolved.sourcePath.string();
	}
	scene.LoadProjectAudioResources(resolvedAudios);
	LogSceneLoadPhase("resource.audio", phaseStart);

	phaseStart = SceneLoadClock::now();
	std::vector<Vans::VansSceneVideoResourceRequest> resolvedVideos = resourcePlan.videos;
	for (auto& video : resolvedVideos)
	{
		const Vans::VansResolvedSceneResourcePath resolved = loadContext.ResolveVideo(video);
		if (!resolved.valid)
		{
			VANS_LOG_ERROR("[SceneResource] Video '" << video.name
				<< "' cannot be resolved from asset index: " << resolved.error);
			return false;
		}
		video.path = resolved.sourcePath.string();
	}
	scene.LoadProjectVideoResources(resolvedVideos);
	LogSceneLoadPhase("resource.video", phaseStart);

	if (resourcePlan.loadRegisteredShaders || !resourcePlan.shaders.empty())
	{
		phaseStart = SceneLoadClock::now();
		VansSceneProjectResourceBuilder::RegisterShaders(
			scene,
			resourcePlan.shaders,
			loadContext,
			nativeDevice,
			resourcePlan.loadRegisteredShaders);
		LogSceneLoadPhase("resource.shaders", phaseStart);
	}

	phaseStart = SceneLoadClock::now();
	FinalizeResourceBatch(scene);
	LogSceneLoadPhase("resource.finalize", phaseStart);
	LogSceneLoadPhase("resource.total", totalStart);
	return true;
}

}
