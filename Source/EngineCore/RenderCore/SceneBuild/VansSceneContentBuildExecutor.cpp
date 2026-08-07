#include "VansSceneContentBuildExecutor.h"

#include "../../SceneCore/VansSceneDocumentLoader.h"
#include "../../SceneCore/VansSceneRuntimeProjection.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../Util/VansLog.h"
#include "VansSceneEnvironmentNodeBuilder.h"
#include "VansSceneMaterialBuilder.h"
#include "VansSceneRenderNodeBuilder.h"
#include "../VulkanCore/VansVKDevice.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace VansGraphics
{
namespace
{
template <typename T>
void ApplyOptionalValue(const std::optional<T>& source, T& destination)
{
	if (source.has_value())
	{
		destination = *source;
	}
}

void ApplyOptionalFloat3(const std::optional<std::array<float, 3>>& source, float destination[4])
{
	if (!source.has_value())
	{
		return;
	}

	for (size_t index = 0; index < 3; ++index)
	{
		destination[index] = (*source)[index];
	}
}

float ResolveOptionalFloat(const std::optional<float>& source, float fallback)
{
	return source.has_value() ? *source : fallback;
}

void LogSceneDocumentLoadDiagnostics(
	const char* path,
	const Vans::SceneDiagnostics& diagnostics)
{
	if (diagnostics.empty())
	{
		VANS_LOG_ERROR("[VansScene] Cannot load scene document: " << path);
		return;
	}

	for (const Vans::SceneDiagnostic& diagnostic : diagnostics)
	{
		const char* severity = diagnostic.severity == Vans::SceneDiagnosticSeverity::Error
			? "error"
			: "warning";
		VANS_LOG_ERROR("[VansScene] Scene document " << severity << " "
			<< diagnostic.propertyPointer << ": " << diagnostic.message);
	}
}
}

bool VansSceneContentBuildExecutor::BuildFromFile(VansScene& scene, const char* path)
{
	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VkDevice nativeDevice = vkDevice->GetLogicDevice();

	Vans::SceneDocumentLoadResult loadResult = Vans::VansSceneDocumentLoader::Load(path);
	if (!loadResult)
	{
		LogSceneDocumentLoadDiagnostics(path, loadResult.diagnostics);
		return false;
	}

	const std::string projectRoot = ResolveProjectRootFromScenePath(path);
	Vans::VansSceneContentBuildPlan buildPlan;
	std::string planError;
	if (!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
		loadResult.document->SerializedRootSnapshot(),
		projectRoot,
		buildPlan,
		planError))
	{
		VANS_LOG_ERROR("[VansScene] " << planError << ": " << path);
		return false;
	}

	return BuildFromPlan(scene, nativeDevice, vkDevice, buildPlan, path, projectRoot);
}

bool VansSceneContentBuildExecutor::BuildFromPlan(
	VansScene& scene,
	VkDevice& nativeDevice,
	VansVKDevice* vkDevice,
	const Vans::VansSceneContentBuildPlan& buildPlan,
	const char* path,
	const std::string& projectRoot)
{
	const Vans::VansSceneRenderSettingsConfig& renderSettings = buildPlan.renderSettings;
	ApplyHeightFogSettings(*scene.GetMaterialManager(), renderSettings.heightFog);
	ApplyVolumetricFogSettings(*scene.GetMaterialManager(), renderSettings.volumetricFog);
	ApplyVolumetricCloudSettings(*scene.GetMaterialManager(), renderSettings.volumetricClouds);
	ApplyPostProcessSettings(*scene.GetMaterialManager(), renderSettings.postProcess);
	ApplyMainCameraHiZCullSettings(scene, renderSettings.mainCameraHiZCulling);
	if (Vans::VansProjectManager::Get().IsProjectLoaded())
	{
		ApplyProjectMainCameraHiZCullSettings(
			scene,
			Vans::VansProjectManager::Get().GetProjectSettings().GetMainCameraHiZCullSettings());
	}
	scene.GetReflectionProbeSystem()->LoadFromSceneConfig(buildPlan.reflectionProbes, path);
	ApplyGISettings(scene, renderSettings.globalIllumination);

	if (!buildPlan.materials.empty())
		VansSceneMaterialBuilder::LoadMaterials(scene, buildPlan.materials);

	scene.LoadSceneObjects(nativeDevice, buildPlan.objects, projectRoot);

	if (!buildPlan.renderNodes.empty())
		VansSceneRenderNodeBuilder::LoadRenderNodes(scene, nativeDevice, buildPlan.renderNodes);

	scene.GetLightManager()->CreateLightUniformData(nativeDevice);

	if (buildPlan.terrain)
		VansSceneEnvironmentNodeBuilder::AddTerrainNode(scene, vkDevice, *buildPlan.terrain);

	if (buildPlan.vegetation)
		VansSceneEnvironmentNodeBuilder::AddVegetationNode(scene, nativeDevice, *buildPlan.vegetation, projectRoot);

	if (buildPlan.water)
		VansSceneEnvironmentNodeBuilder::AddWaterNode(scene, nativeDevice, *buildPlan.water);

	VansSceneRenderNodeBuilder::AddDeferredNode(scene, nativeDevice);
	VansSceneRenderNodeBuilder::AddScreenSpaceFeatureNode(scene, nativeDevice);

	VANS_LOG("[VansScene] Scene content loaded from: " << path);
	return true;
}

void VansSceneContentBuildExecutor::ApplyHeightFogSettings(
	VansMaterialManager& materialManager,
	const std::optional<Vans::VansSceneHeightFogSettingsConfig>& config)
{
	if (!config.has_value())
	{
		return;
	}

	VansFogSettings settings = materialManager.GetFogSettings();
	ApplyOptionalValue(config->fogDensity, settings.fogDensity);
	ApplyOptionalValue(config->heightFalloff, settings.heightFalloff);
	ApplyOptionalValue(config->sunScatterScale, settings.sunScatterScale);
	ApplyOptionalValue(config->ambientScale, settings.ambientScale);
	ApplyOptionalValue(config->fogMinHeight, settings.fogMinHeight);
	ApplyOptionalValue(config->skyFogDistance, settings.skyFogDistance);
	materialManager.ApplyFogSettings(settings);
}

void VansSceneContentBuildExecutor::ApplyVolumetricFogSettings(
	VansMaterialManager& materialManager,
	const std::optional<Vans::VansSceneVolumetricFogSettingsConfig>& config)
{
	if (!config.has_value())
	{
		return;
	}

	VansFogVolumeSettings settings = materialManager.GetFogVolumeSettings();
	ApplyOptionalValue(config->density, settings.density);
	ApplyOptionalValue(config->anisotropy, settings.anisotropy);
	ApplyOptionalValue(config->scatterScale, settings.scatterScale);
	ApplyOptionalValue(config->ambientScale, settings.ambientScale);
	ApplyOptionalValue(config->volumeNear, settings.volumeNear);
	ApplyOptionalValue(config->volumeFar, settings.volumeFar);
	ApplyOptionalValue(config->slicePower, settings.slicePower);
	ApplyOptionalFloat3(config->fogBoxMin, settings.fogBoxMin);
	ApplyOptionalFloat3(config->fogBoxMax, settings.fogBoxMax);
	materialManager.ApplyFogVolumeSettings(settings);
}

void VansSceneContentBuildExecutor::ApplyVolumetricCloudSettings(
	VansMaterialManager& materialManager,
	const std::optional<Vans::VansSceneVolumetricCloudSettingsConfig>& config)
{
	if (!config.has_value())
	{
		return;
	}

	VansCloudParamsGPU& params = materialManager.m_CloudParams;

	ApplyOptionalValue(config->planetRadius, params.planetRadius);
	ApplyOptionalValue(config->seaLevel, params.seaLevel);

	const float baseHeight = ResolveOptionalFloat(config->cloudBaseHeight,
		ResolveOptionalFloat(config->cloudMinHeight, params.cloudMinHeight));
	const float thickness = ResolveOptionalFloat(config->cloudThickness,
		ResolveOptionalFloat(config->cloudMaxHeight, params.cloudMaxHeight) - baseHeight);
	params.cloudMinHeight = baseHeight;
	params.cloudMaxHeight = baseHeight + std::max(thickness, 100.0f);

	ApplyOptionalValue(config->density, params.density);
	ApplyOptionalValue(config->coverage, params.coverage);
	ApplyOptionalValue(config->sunBrightness, params.sunBrightness);
	ApplyOptionalValue(config->phaseG, params.phaseG);

	ApplyOptionalValue(config->mainTileMeters, params.mainTileMeters);
	ApplyOptionalValue(config->detailTileMeters, params.detailTileMeters);
	ApplyOptionalValue(config->mainHeightScale, params.mainHeightScale);
	ApplyOptionalValue(config->detailHeightScale, params.detailHeightScale);

	ApplyOptionalValue(config->thresholdLowCoverage, params.thresholdLowCoverage);
	ApplyOptionalValue(config->thresholdHighCoverage, params.thresholdHighCoverage);
	ApplyOptionalValue(config->densityRemapLow, params.densityRemapLow);
	ApplyOptionalValue(config->densityRemapHigh, params.densityRemapHigh);

	ApplyOptionalValue(config->mainErosionStrength, params.mainErosionStrength);
	ApplyOptionalValue(config->detailErosionStrength, params.detailErosionStrength);
	ApplyOptionalValue(config->edgeErosionStrength, params.edgeErosionStrength);
	ApplyOptionalValue(config->verticalShapePower, params.verticalShapePower);

	ApplyOptionalValue(config->detailErosionLow, params.detailErosionLow);
	ApplyOptionalValue(config->detailErosionHigh, params.detailErosionHigh);
	ApplyOptionalValue(config->detailEdgeStrength, params.detailEdgeStrength);
	ApplyOptionalValue(config->shadowDensityScale, params.shadowDensityScale);
	ApplyOptionalValue(config->sigmaTRef, params.sigmaTRef);
	ApplyOptionalValue(config->viewAbsorption, params.viewAbsorption);
	ApplyOptionalValue(config->lightAbsorption, params.lightAbsorption);
	ApplyOptionalValue(config->singleScatteringAlbedo, params.singleScatteringAlbedo);
	ApplyOptionalValue(config->forwardEccentricity, params.forwardEccentricity);
	ApplyOptionalValue(config->backwardEccentricity, params.backwardEccentricity);
	ApplyOptionalValue(config->msAttenuation, params.msAttenuation);
	ApplyOptionalValue(config->msContribution, params.msContribution);
	ApplyOptionalValue(config->msEccentricity, params.msEccentricity);
	ApplyOptionalValue(config->scatteringTintR, params.scatteringTintR);
	ApplyOptionalValue(config->scatteringTintG, params.scatteringTintG);
	ApplyOptionalValue(config->scatteringTintB, params.scatteringTintB);
	ApplyOptionalValue(config->scatterSourceODScale, params.scatterSourceODScale);
	ApplyOptionalValue(config->scatterSourceCurvePow, params.scatterSourceCurvePow);
	ApplyOptionalValue(config->aoUpwardScale, params.aoUpwardScale);
	ApplyOptionalValue(config->ambientBottomStrength, params.ambientBottomStrength);
	ApplyOptionalValue(config->ambientTopStrength, params.ambientTopStrength);
	ApplyOptionalValue(config->ambientDuskWarmth, params.ambientDuskWarmth);
	ApplyOptionalValue(config->boundaryConfidence, params.boundaryConfidence);
	ApplyOptionalValue(config->boundaryWrap, params.boundaryWrap);
	ApplyOptionalValue(config->phiFwdIntensity, params.phiFwdIntensity);
	ApplyOptionalValue(config->phiFwdDepthPow, params.phiFwdDepthPow);
	ApplyOptionalValue(config->phiFwdDepthBias, params.phiFwdDepthBias);
	ApplyOptionalValue(config->phiFwdMSBuildScale, params.phiFwdMSBuildScale);
	ApplyOptionalValue(config->phiFwdCompress, params.phiFwdCompress);
	ApplyOptionalValue(config->phiFwdMaxDistance, params.phiFwdMaxDistance);
	ApplyOptionalValue(config->phiFwdConeRatio, params.phiFwdConeRatio);
	ApplyOptionalValue(config->phiFwdMinStep, params.phiFwdMinStep);
	ApplyOptionalValue(config->lightStepCount, params.lightStepCount);
	ApplyOptionalValue(config->boundaryGradientStep, params.boundaryGradientStep);
	ApplyOptionalValue(config->boundaryGradientStrength, params.boundaryGradientStrength);
	ApplyOptionalValue(config->shadingDebugMode, params.shadingDebugMode);

	params.mainTileMeters = std::max(params.mainTileMeters, 1000.0f);
	params.detailTileMeters = std::max(params.detailTileMeters, 500.0f);
	params.densityRemapHigh = std::max(params.densityRemapHigh, params.densityRemapLow + 0.01f);
	params.detailErosionHigh = std::max(params.detailErosionHigh, params.detailErosionLow + 0.01f);
	params.sigmaTRef = std::max(params.sigmaTRef, 0.0f);
	params.viewAbsorption = std::max(params.viewAbsorption, 0.0f);
	params.lightAbsorption = std::max(params.lightAbsorption, 0.0f);
	params.singleScatteringAlbedo = std::clamp(params.singleScatteringAlbedo, 0.0f, 0.9999f);
	params.forwardEccentricity = std::clamp(params.forwardEccentricity, 0.0f, 0.95f);
	params.backwardEccentricity = std::clamp(params.backwardEccentricity, 0.0f, 0.95f);
	params.msAttenuation = std::clamp(params.msAttenuation, 0.0f, 1.0f);
	params.msContribution = std::clamp(params.msContribution, 0.0f, 1.0f);
	params.msEccentricity = std::clamp(params.msEccentricity, 0.0f, 1.0f);
	params.scatterSourceODScale = std::max(params.scatterSourceODScale, 0.001f);
	params.scatterSourceCurvePow = std::max(params.scatterSourceCurvePow, 0.01f);
	params.phiFwdConeRatio = std::max(params.phiFwdConeRatio, 1.01f);
	params.phiFwdMinStep = std::max(params.phiFwdMinStep, 1.0f);
	params.lightStepCount = std::clamp(params.lightStepCount, 1.0f, 16.0f);
	params.boundaryGradientStrength = std::clamp(params.boundaryGradientStrength, 0.0f, 1.0f);
	materialManager.UploadCloudParamsToGPU();
}

void VansSceneContentBuildExecutor::ApplyPostProcessSettings(
	VansMaterialManager& materialManager,
	const std::optional<Vans::VansScenePostProcessSettingsConfig>& config)
{
	VansPostProcessProfile& profile = materialManager.m_PostProcessProfile;
	profile.ResetToDefaults();
	if (!config.has_value())
	{
		return;
	}

	ApplyOptionalValue(config->enableAutoExposure, profile.m_EnableAutoExposure);
	ApplyOptionalValue(config->exposureCompensation, profile.m_ExposureCompensation);
	ApplyOptionalValue(config->minEV100, profile.m_MinEV100);
	ApplyOptionalValue(config->maxEV100, profile.m_MaxEV100);
	ApplyOptionalValue(config->adaptationSpeedUp, profile.m_AdaptationSpeedUp);
	ApplyOptionalValue(config->adaptationSpeedDown, profile.m_AdaptationSpeedDown);
	ApplyOptionalValue(config->enableBloom, profile.m_EnableBloom);
	ApplyOptionalValue(config->bloomThreshold, profile.m_BloomThreshold);
	ApplyOptionalValue(config->bloomKnee, profile.m_BloomKnee);
	ApplyOptionalValue(config->bloomIntensity, profile.m_BloomIntensity);
	ApplyOptionalValue(config->bloomScatter, profile.m_BloomScatter);
	ApplyOptionalValue(config->toneMapperType, profile.m_ToneMapperType);
	ApplyOptionalValue(config->whitePoint, profile.m_WhitePoint);
	ApplyOptionalValue(config->enableColorGrading, profile.m_EnableColorGrading);
	ApplyOptionalValue(config->contrast, profile.m_Contrast);
	ApplyOptionalValue(config->saturation, profile.m_Saturation);
	ApplyOptionalValue(config->hueShift, profile.m_HueShift);
	ApplyOptionalValue(config->temperature, profile.m_Temperature);
	ApplyOptionalValue(config->tint, profile.m_Tint);

	profile.m_ExposureCompensation = std::clamp(profile.m_ExposureCompensation, -16.0f, 16.0f);
	profile.m_MinEV100 = std::clamp(profile.m_MinEV100, -24.0f, 24.0f);
	profile.m_MaxEV100 = std::clamp(profile.m_MaxEV100, profile.m_MinEV100, 24.0f);
	profile.m_AdaptationSpeedUp = std::clamp(profile.m_AdaptationSpeedUp, 0.0f, 20.0f);
	profile.m_AdaptationSpeedDown = std::clamp(profile.m_AdaptationSpeedDown, 0.0f, 20.0f);
	profile.m_BloomThreshold = std::clamp(profile.m_BloomThreshold, 0.0f, 64.0f);
	profile.m_BloomKnee = std::clamp(profile.m_BloomKnee, 0.0f, 1.0f);
	profile.m_BloomIntensity = std::clamp(profile.m_BloomIntensity, 0.0f, 10.0f);
	profile.m_BloomScatter = std::clamp(profile.m_BloomScatter, 0.0f, 1.0f);
	profile.m_ToneMapperType = std::clamp(profile.m_ToneMapperType, 0, 2);
	profile.m_WhitePoint = std::clamp(profile.m_WhitePoint, 0.1f, 64.0f);
	profile.m_Contrast = std::clamp(profile.m_Contrast, 0.0f, 4.0f);
	profile.m_Saturation = std::clamp(profile.m_Saturation, 0.0f, 4.0f);
	profile.m_HueShift = std::clamp(profile.m_HueShift, -1.0f, 1.0f);
	profile.m_Temperature = std::clamp(profile.m_Temperature, -1.0f, 1.0f);
	profile.m_Tint = std::clamp(profile.m_Tint, -1.0f, 1.0f);
	profile.m_IsDirty = true;
}

void VansSceneContentBuildExecutor::ApplyGISettings(
	VansScene& scene,
	const std::optional<Vans::VansSceneGISettingsConfig>& config)
{
	VansGISettings giSettings{};
	if (config.has_value())
	{
		if (config->gridDimensions.has_value())
		{
			giSettings.gridDimensions = glm::uvec3(
				std::clamp((*config->gridDimensions)[0], 1u, 256u),
				std::clamp((*config->gridDimensions)[1], 1u, 256u),
				std::clamp((*config->gridDimensions)[2], 1u, 256u));
		}
		else if (config->gridSize.has_value())
		{
			const uint32_t gridSize = std::clamp(*config->gridSize, 1u, 256u);
			giSettings.gridDimensions = glm::uvec3(gridSize);
		}
		if (config->probeSpacingAxes.has_value())
		{
			giSettings.probeSpacingAxes = glm::vec3(
				std::max((*config->probeSpacingAxes)[0], 0.001f),
				std::max((*config->probeSpacingAxes)[1], 0.001f),
				std::max((*config->probeSpacingAxes)[2], 0.001f));
		}
		else if (config->probeSpacing.has_value())
		{
			giSettings.probeSpacingAxes = glm::vec3(std::max(*config->probeSpacing, 0.001f));
		}
		if (config->raysPerProbe.has_value())
		{
			giSettings.raysPerProbe = std::clamp(*config->raysPerProbe, 1u, 4096u);
		}
		if (config->spatialUpdateDivisor.has_value())
		{
			giSettings.spatialUpdateDivisor = std::clamp(
				*config->spatialUpdateDivisor, 1u,
				std::min({ giSettings.gridDimensions.x, giSettings.gridDimensions.y, giSettings.gridDimensions.z }));
		}
		if (config->directionUpdateSlices.has_value())
		{
			giSettings.directionUpdateSlices = std::clamp(
				*config->directionUpdateSlices, 1u, giSettings.raysPerProbe);
		}
		if (config->maxRayDistance.has_value())
		{
			giSettings.maxRayDistance = std::max(*config->maxRayDistance, 0.001f);
		}
		if (config->normalBias.has_value())
		{
			giSettings.normalBias = std::max(*config->normalBias, 0.0f);
		}
		if (config->environmentIntensity.has_value())
		{
			giSettings.environmentIntensity = std::max(*config->environmentIntensity, 0.0f);
		}
		if (config->maxIndirectRadiance.has_value())
		{
			giSettings.maxIndirectRadiance = std::max(*config->maxIndirectRadiance, 0.0f);
		}
		if (config->maxSHL0.has_value())
		{
			giSettings.maxSHL0 = std::max(*config->maxSHL0, 0.0f);
		}
		if (config->volumeFadeDistance.has_value())
		{
			giSettings.volumeFadeDistance = std::max(*config->volumeFadeDistance, 0.0f);
		}
		if (config->gizmoStride.has_value())
		{
			giSettings.gizmoStride = std::clamp(
				*config->gizmoStride, 1u,
				std::max({ giSettings.gridDimensions.x, giSettings.gridDimensions.y, giSettings.gridDimensions.z }));
		}
		ApplyOptionalValue(config->showProbeGizmos, giSettings.showProbeGizmos);
		ApplyOptionalValue(config->showProbeVolume, giSettings.showProbeVolume);

		if (config->regionCenter.has_value())
		{
			giSettings.regionCenter = glm::vec3(
				(*config->regionCenter)[0],
				(*config->regionCenter)[1],
				(*config->regionCenter)[2]);
		}

		GIProbeRegionDesc baseRegion = BuildLegacyGIRegionDesc(giSettings);
		if (!config->regions.empty())
		{
			giSettings.regions.clear();
			giSettings.regions.reserve(config->regions.size());
			for (size_t index = 0; index < config->regions.size(); ++index)
			{
				const Vans::VansSceneGIRegionSettingsConfig& regionConfig = config->regions[index];
				GIProbeRegionDesc region = baseRegion;
				region.stableId = regionConfig.stableId.value_or(static_cast<uint32_t>(index + 1u));
				region.name = regionConfig.name.value_or(index == 0 ? std::string("Default") : ("GI Region " + std::to_string(index + 1u)));
				ApplyOptionalValue(regionConfig.enabled, region.enabled);
				if (regionConfig.center.has_value())
				{
					region.center = glm::vec3(
						(*regionConfig.center)[0],
						(*regionConfig.center)[1],
						(*regionConfig.center)[2]);
				}
				if (regionConfig.size.has_value())
				{
					region.size = glm::vec3(
						std::max((*regionConfig.size)[0], 0.001f),
						std::max((*regionConfig.size)[1], 0.001f),
						std::max((*regionConfig.size)[2], 0.001f));
					region.overrideGridDimensions = false;
				}
				if (regionConfig.probeSpacingAxes.has_value())
				{
					region.probeSpacingAxes = glm::vec3(
						std::max((*regionConfig.probeSpacingAxes)[0], 0.001f),
						std::max((*regionConfig.probeSpacingAxes)[1], 0.001f),
						std::max((*regionConfig.probeSpacingAxes)[2], 0.001f));
				}
				if (regionConfig.gridDimensions.has_value())
				{
					region.gridDimensions = glm::uvec3(
						std::clamp((*regionConfig.gridDimensions)[0], 1u, 256u),
						std::clamp((*regionConfig.gridDimensions)[1], 1u, 256u),
						std::clamp((*regionConfig.gridDimensions)[2], 1u, 256u));
					region.overrideGridDimensions = true;
				}
				ApplyOptionalValue(regionConfig.raysPerProbe, region.raysPerProbe);
				ApplyOptionalValue(regionConfig.spatialUpdateDivisor, region.spatialUpdateDivisor);
				ApplyOptionalValue(regionConfig.directionUpdateSlices, region.directionUpdateSlices);
				ApplyOptionalValue(regionConfig.maxRayDistance, region.maxRayDistance);
				ApplyOptionalValue(regionConfig.normalBias, region.normalBias);
				ApplyOptionalValue(regionConfig.volumeFadeDistance, region.volumeFadeDistance);
				ApplyOptionalValue(regionConfig.priority, region.priority);
				giSettings.regions.push_back(region);
			}
		}
		else
		{
			giSettings.regions = { baseRegion };
		}
	}

	NormalizeGISettings(giSettings);
	scene.SetGISettings(giSettings);

	uint64_t totalProbeCount = 0;
	uint64_t totalRayCacheEntries = 0;
	for (size_t index = 0; index < giSettings.regions.size(); ++index)
	{
		const GIResolvedRegion region = ResolveGIRegion(giSettings.regions[index]);
		totalProbeCount += region.enabled ? region.probeCount : 0u;
		totalRayCacheEntries += region.enabled ? region.probeCount * region.raysPerProbe : 0u;
		VANS_LOG("[GISettings] region[" << index << "] name='" << region.name
			<< "' enabled=" << (region.enabled ? 1 : 0)
			<< " grid=" << region.gridDimensions.x << "x"
			<< region.gridDimensions.y << "x"
			<< region.gridDimensions.z
			<< " spacing=(" << region.probeSpacingAxes.x << ","
			<< region.probeSpacingAxes.y << ","
			<< region.probeSpacingAxes.z << ")"
			<< " center=(" << region.center.x << ","
			<< region.center.y << ","
			<< region.center.z << ")"
			<< " volume=(" << region.volumeSize.x << ","
			<< region.volumeSize.y << "," << region.volumeSize.z << ")"
			<< " probes=" << region.probeCount
			<< " rayCacheEntries=" << (region.probeCount * region.raysPerProbe));
	}
	VANS_LOG("[GISettings] regions=" << giSettings.regions.size()
		<< " activeProbes=" << totalProbeCount
		<< " activeRayCacheEntries=" << totalRayCacheEntries);

	const GIResolvedRegion primaryRegion = ResolveGIRegion(GetPrimaryGIRegionDesc(giSettings));
	SSGIParamsGPU volumeData{};
	volumeData.giVolumeMin = glm::vec4(primaryRegion.volumeMin, 0.0f);
	volumeData.giVolumeSizeAndBias = glm::vec4(primaryRegion.volumeSize, primaryRegion.normalBias);
	volumeData.traceParams = glm::vec4(primaryRegion.maxRayDistance, 0.75f, primaryRegion.volumeFadeDistance, 0.0f);
	VansMaterialManager* materialManager = scene.GetMaterialManager();
	if (materialManager->m_SSGICBBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
		materialManager->m_SSGICBBuffer.SetBufferData(
			&volumeData.giVolumeMin, sizeof(glm::vec4), sizeof(glm::vec4) * 3);
}

void VansSceneContentBuildExecutor::ApplyMainCameraHiZCullSettings(
	VansScene& scene,
	const std::optional<Vans::VansSceneMainCameraHiZCullSettingsConfig>& config)
{
	VansMainCameraHiZCullSettings settings = scene.GetMainCameraHiZCullSettings();
	if (config.has_value())
	{
		ApplyOptionalValue(config->enabled, settings.enabled);
		ApplyOptionalValue(config->enableOpaque, settings.enableOpaque);
		ApplyOptionalValue(config->enableHair, settings.enableHair);
		ApplyOptionalValue(config->enableTransparent, settings.enableTransparent);
		ApplyOptionalValue(config->enableDecal, settings.enableDecal);
		ApplyOptionalValue(config->enableForwardOpaqueAfterDeferred, settings.enableForwardOpaqueAfterDeferred);
		ApplyOptionalValue(config->depthBiasMeters, settings.depthBiasMeters);
		ApplyOptionalValue(config->cameraMotionDisableDistance, settings.cameraMotionDisableDistance);
		ApplyOptionalValue(config->cameraMotionDisableAngleRadians, settings.cameraMotionDisableAngleRadians);
		ApplyOptionalValue(config->forceVisibleFramesAfterChange, settings.forceVisibleFramesAfterChange);
		ApplyOptionalValue(config->refreshCulledEveryNFrames, settings.refreshCulledEveryNFrames);
		ApplyOptionalValue(config->maxScreenCoverageForCull, settings.maxScreenCoverageForCull);
	}

	settings.depthBiasMeters = std::max(settings.depthBiasMeters, 0.0f);
	settings.cameraMotionDisableDistance = std::max(settings.cameraMotionDisableDistance, 0.0f);
	settings.cameraMotionDisableAngleRadians = std::max(settings.cameraMotionDisableAngleRadians, 0.0f);
	settings.forceVisibleFramesAfterChange = std::max(settings.forceVisibleFramesAfterChange, 1u);
	settings.refreshCulledEveryNFrames = std::max(settings.refreshCulledEveryNFrames, 1u);
	settings.maxScreenCoverageForCull = std::clamp(settings.maxScreenCoverageForCull, 0.05f, 1.0f);
	scene.SetMainCameraHiZCullSettings(settings);
}

void VansSceneContentBuildExecutor::ApplyProjectMainCameraHiZCullSettings(
	VansScene& scene,
	const Vans::VansProjectMainCameraHiZCullSettings& projectSettings)
{
	VansMainCameraHiZCullSettings settings = scene.GetMainCameraHiZCullSettings();
	settings.enabled = projectSettings.enabled;
	settings.enableOpaque = projectSettings.enableOpaque;
	settings.enableHair = projectSettings.enableHair;
	settings.enableTransparent = projectSettings.enableTransparent;
	settings.enableDecal = projectSettings.enableDecal;
	settings.enableForwardOpaqueAfterDeferred = projectSettings.enableForwardOpaqueAfterDeferred;
	settings.depthBiasMeters = projectSettings.depthBiasMeters;
	settings.cameraMotionDisableDistance = projectSettings.cameraMotionDisableDistance;
	settings.cameraMotionDisableAngleRadians = projectSettings.cameraMotionDisableAngleRadians;
	settings.forceVisibleFramesAfterChange = projectSettings.forceVisibleFramesAfterChange;
	settings.refreshCulledEveryNFrames = projectSettings.refreshCulledEveryNFrames;
	settings.maxScreenCoverageForCull = projectSettings.maxScreenCoverageForCull;
	scene.SetMainCameraHiZCullSettings(settings);
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
