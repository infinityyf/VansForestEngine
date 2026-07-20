#include "VansSceneContentBuildExecutor.h"

#include "../../SceneCore/VansSceneRuntimeProjection.h"
#include "../../Util/VansLog.h"
#include "VansSceneEnvironmentNodeBuilder.h"
#include "VansSceneMaterialBuilder.h"
#include "VansSceneRenderNodeBuilder.h"
#include "../VulkanCore/VansVKDevice.h"

#include <algorithm>
#include <fstream>

namespace VansGraphics
{
namespace
{
float ReadFloatField(const json& object, const char* key, float fallback)
{
	if (!object.is_object())
		return fallback;
	const auto found = object.find(key);
	return found != object.end() && found->is_number() ? found->get<float>() : fallback;
}
}

bool VansSceneContentBuildExecutor::BuildFromFile(VansScene& scene, const char* path)
{
	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VkDevice nativeDevice = vkDevice->GetLogicDevice();

	std::ifstream jsonFile(path);
	if (!jsonFile.is_open())
	{
		VANS_LOG_ERROR("[VansScene] Cannot open scene file: " << path);
		return false;
	}

	json sceneData = json::parse(jsonFile);
	if (!Vans::VansSceneRuntimeProjection::BuildRuntimeScene(sceneData))
	{
		VANS_LOG_ERROR("[VansScene] Invalid Scene document: " << path);
		return false;
	}

	ApplyVolumetricCloudSettings(*scene.GetMaterialManager(), sceneData);
	scene.GetReflectionProbeSystem()->LoadFromSceneJson(sceneData, path);
	ApplyGISettings(scene, sceneData);

	const std::string projectRoot = ResolveProjectRootFromScenePath(path);

	if (sceneData.contains("material") && sceneData["material"].is_array())
		VansSceneMaterialBuilder::LoadMaterialsFromJson(scene, sceneData["material"]);

	if (!sceneData.contains("scene") || !sceneData["scene"].is_array() || sceneData["scene"].empty())
	{
		VANS_LOG_ERROR("[VansScene] Scene file has no valid scene array: " << path);
		return false;
	}

	json& sceneNode = sceneData["scene"][0];
	if (!sceneNode.contains("objects") || !sceneNode["objects"].is_array())
	{
		VANS_LOG_ERROR("[VansScene] Scene file must contain scene[0].objects array: " << path);
		return false;
	}

	scene.LoadSceneObjects(nativeDevice, sceneNode["objects"], projectRoot);

	if (sceneNode.contains("rendernode") && sceneNode["rendernode"].is_array() &&
		!sceneNode["rendernode"].empty())
	{
		VansSceneRenderNodeBuilder::LoadRenderNodes(scene, nativeDevice, sceneNode["rendernode"]);
	}

	scene.GetLightManager()->CreateLightUniformData(nativeDevice);

	if (sceneData.contains("terrain"))
		VansSceneEnvironmentNodeBuilder::AddTerrainNode(scene, vkDevice, sceneData["terrain"]);

	if (sceneData.contains("vegetation"))
		VansSceneEnvironmentNodeBuilder::AddVegetationNode(scene, nativeDevice, sceneData["vegetation"], projectRoot);

	if (sceneData.contains("water"))
		VansSceneEnvironmentNodeBuilder::AddWaterNode(scene, nativeDevice, sceneData["water"]);

	VansSceneRenderNodeBuilder::AddDeferredNode(scene, nativeDevice);
	VansSceneRenderNodeBuilder::AddScreenSpaceFeatureNode(scene, nativeDevice);

	VANS_LOG("[VansScene] Scene content loaded from: " << path);
	return true;
}

void VansSceneContentBuildExecutor::ApplyVolumetricCloudSettings(VansMaterialManager& materialManager, const json& sceneData)
{
	const auto cloudIt = sceneData.find("volumetricClouds");
	if (cloudIt == sceneData.end() || !cloudIt->is_object())
		return;

	const json& cloud = *cloudIt;
	VansCloudParamsGPU& params = materialManager.m_CloudParams;

	params.planetRadius = ReadFloatField(cloud, "planetRadius", params.planetRadius);
	params.seaLevel = ReadFloatField(cloud, "seaLevel", params.seaLevel);

	const float baseHeight = ReadFloatField(cloud, "cloudBaseHeight",
		ReadFloatField(cloud, "cloudMinHeight", params.cloudMinHeight));
	const float thickness = ReadFloatField(cloud, "cloudThickness",
		ReadFloatField(cloud, "cloudMaxHeight", params.cloudMaxHeight) - baseHeight);
	params.cloudMinHeight = baseHeight;
	params.cloudMaxHeight = baseHeight + std::max(thickness, 100.0f);

	params.density = ReadFloatField(cloud, "density", params.density);
	params.coverage = ReadFloatField(cloud, "coverage", params.coverage);
	params.sunBrightness = ReadFloatField(cloud, "sunBrightness", params.sunBrightness);
	params.phaseG = ReadFloatField(cloud, "phaseG", params.phaseG);

	params.mainTileMeters = ReadFloatField(cloud, "mainTileMeters", params.mainTileMeters);
	params.detailTileMeters = ReadFloatField(cloud, "detailTileMeters", params.detailTileMeters);
	params.mainHeightScale = ReadFloatField(cloud, "mainHeightScale", params.mainHeightScale);
	params.detailHeightScale = ReadFloatField(cloud, "detailHeightScale", params.detailHeightScale);

	params.thresholdLowCoverage = ReadFloatField(cloud, "thresholdLowCoverage", params.thresholdLowCoverage);
	params.thresholdHighCoverage = ReadFloatField(cloud, "thresholdHighCoverage", params.thresholdHighCoverage);
	params.densityRemapLow = ReadFloatField(cloud, "densityRemapLow", params.densityRemapLow);
	params.densityRemapHigh = ReadFloatField(cloud, "densityRemapHigh", params.densityRemapHigh);

	params.mainErosionStrength = ReadFloatField(cloud, "mainErosionStrength", params.mainErosionStrength);
	params.detailErosionStrength = ReadFloatField(cloud, "detailErosionStrength", params.detailErosionStrength);
	params.edgeErosionStrength = ReadFloatField(cloud, "edgeErosionStrength", params.edgeErosionStrength);
	params.verticalShapePower = ReadFloatField(cloud, "verticalShapePower", params.verticalShapePower);

	params.detailErosionLow = ReadFloatField(cloud, "detailErosionLow", params.detailErosionLow);
	params.detailErosionHigh = ReadFloatField(cloud, "detailErosionHigh", params.detailErosionHigh);
	params.detailEdgeStrength = ReadFloatField(cloud, "detailEdgeStrength", params.detailEdgeStrength);
	params.shadowDensityScale = ReadFloatField(cloud, "shadowDensityScale", params.shadowDensityScale);

	params.mainTileMeters = std::max(params.mainTileMeters, 1000.0f);
	params.detailTileMeters = std::max(params.detailTileMeters, 500.0f);
	params.densityRemapHigh = std::max(params.densityRemapHigh, params.densityRemapLow + 0.01f);
	params.detailErosionHigh = std::max(params.detailErosionHigh, params.detailErosionLow + 0.01f);
	materialManager.UploadCloudParamsToGPU();
}

void VansSceneContentBuildExecutor::ApplyGISettings(VansScene& scene, const json& sceneData)
{
	VansGISettings giSettings{};
	if (sceneData.contains("globalIllumination") && sceneData["globalIllumination"].is_object())
	{
		const json& gi = sceneData["globalIllumination"];
		giSettings.gridSize = std::clamp(gi.value("gridSize", giSettings.gridSize), 1u, 256u);
		giSettings.probeSpacing = std::max(gi.value("probeSpacing", giSettings.probeSpacing), 0.001f);
		giSettings.gridDimensions = glm::uvec3(giSettings.gridSize);
		giSettings.probeSpacingAxes = glm::vec3(giSettings.probeSpacing);
		if (gi.contains("gridDimensions") && gi["gridDimensions"].is_array() && gi["gridDimensions"].size() == 3)
		{
			giSettings.gridDimensions = glm::uvec3(
				std::clamp(gi["gridDimensions"][0].get<uint32_t>(), 1u, 256u),
				std::clamp(gi["gridDimensions"][1].get<uint32_t>(), 1u, 256u),
				std::clamp(gi["gridDimensions"][2].get<uint32_t>(), 1u, 256u));
		}
		if (gi.contains("probeSpacingAxes") && gi["probeSpacingAxes"].is_array() && gi["probeSpacingAxes"].size() == 3)
		{
			giSettings.probeSpacingAxes = glm::vec3(
				std::max(gi["probeSpacingAxes"][0].get<float>(), 0.001f),
				std::max(gi["probeSpacingAxes"][1].get<float>(), 0.001f),
				std::max(gi["probeSpacingAxes"][2].get<float>(), 0.001f));
		}
		giSettings.raysPerProbe = std::clamp(gi.value("raysPerProbe", giSettings.raysPerProbe), 1u, 4096u);
		giSettings.spatialUpdateDivisor = std::clamp(
			gi.value("spatialUpdateDivisor", giSettings.spatialUpdateDivisor), 1u,
			std::min({ giSettings.gridDimensions.x, giSettings.gridDimensions.y, giSettings.gridDimensions.z }));
		giSettings.directionUpdateSlices = std::clamp(
			gi.value("directionUpdateSlices", giSettings.directionUpdateSlices), 1u, giSettings.raysPerProbe);
		giSettings.maxRayDistance = std::max(gi.value("maxRayDistance", giSettings.maxRayDistance), 0.001f);
		giSettings.normalBias = std::max(gi.value("normalBias", giSettings.normalBias), 0.0f);
		giSettings.environmentIntensity = std::max(gi.value("environmentIntensity", giSettings.environmentIntensity), 0.0f);
		giSettings.maxIndirectRadiance = std::max(gi.value("maxIndirectRadiance", giSettings.maxIndirectRadiance), 0.0f);
		giSettings.maxSHL0 = std::max(gi.value("maxSHL0", giSettings.maxSHL0), 0.0f);
		giSettings.volumeFadeDistance = std::max(gi.value("volumeFadeDistance", giSettings.volumeFadeDistance), 0.0f);
		giSettings.gizmoStride = std::clamp(
			gi.value("gizmoStride", giSettings.gizmoStride), 1u,
			std::max({ giSettings.gridDimensions.x, giSettings.gridDimensions.y, giSettings.gridDimensions.z }));
		giSettings.showProbeGizmos = gi.value("showProbeGizmos", giSettings.showProbeGizmos);
		giSettings.showProbeVolume = gi.value("showProbeVolume", giSettings.showProbeVolume);

		if (gi.contains("regionCenter") && gi["regionCenter"].is_array() && gi["regionCenter"].size() == 3)
		{
			giSettings.regionCenter = glm::vec3(
				gi["regionCenter"][0].get<float>(),
				gi["regionCenter"][1].get<float>(),
				gi["regionCenter"][2].get<float>());
		}
	}

	scene.SetGISettings(giSettings);

	const glm::vec3 volumeSize = glm::vec3(giSettings.gridDimensions) * giSettings.probeSpacingAxes;
	const glm::vec3 volumeMin = giSettings.regionCenter - volumeSize * 0.5f;
	VANS_LOG("[GISettings] grid="
		<< giSettings.gridDimensions.x << "x"
		<< giSettings.gridDimensions.y << "x"
		<< giSettings.gridDimensions.z
		<< " spacing=(" << giSettings.probeSpacingAxes.x << ","
		<< giSettings.probeSpacingAxes.y << ","
		<< giSettings.probeSpacingAxes.z << ")"
		<< " center=(" << giSettings.regionCenter.x << ","
		<< giSettings.regionCenter.y << ","
		<< giSettings.regionCenter.z << ")"
		<< " volume=(" << volumeSize.x << ","
		<< volumeSize.y << "," << volumeSize.z << ")");
	SSGIParamsGPU volumeData{};
	volumeData.giVolumeMin = glm::vec4(volumeMin, 0.0f);
	volumeData.giVolumeSizeAndBias = glm::vec4(volumeSize, giSettings.normalBias);
	volumeData.traceParams = glm::vec4(giSettings.maxRayDistance, 0.75f, giSettings.volumeFadeDistance, 0.0f);
	VansMaterialManager* materialManager = scene.GetMaterialManager();
	if (materialManager->m_SSGICBBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
		materialManager->m_SSGICBBuffer.SetBufferData(
			&volumeData.giVolumeMin, sizeof(glm::vec4), sizeof(glm::vec4) * 3);
}

std::string VansSceneContentBuildExecutor::ResolveProjectRootFromScenePath(const char* path)
{
	std::string scenePath(path);
	std::string projectRoot = scenePath.substr(0, scenePath.find_last_of("/\\") + 1);
	if (!projectRoot.empty())
	{
		size_t pos = projectRoot.substr(0, projectRoot.size() - 1).find_last_of("/\\");
		if (pos != std::string::npos)
			projectRoot = projectRoot.substr(0, pos + 1);
	}
	return projectRoot;
}
}
