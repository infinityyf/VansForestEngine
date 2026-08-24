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

	if (!scene.LoadSceneObjects(nativeDevice, buildPlan.objects, projectRoot))
		return false;

	if (!buildPlan.renderNodes.empty())
		VansSceneRenderNodeBuilder::LoadRenderNodes(scene, nativeDevice, buildPlan.renderNodes);

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
	ApplyOptionalValue(config->bloomClamp, profile.m_BloomClamp);
	ApplyOptionalValue(config->bloomTintR, profile.m_BloomTintR);
	ApplyOptionalValue(config->bloomTintG, profile.m_BloomTintG);
	ApplyOptionalValue(config->bloomTintB, profile.m_BloomTintB);
	ApplyOptionalValue(config->bloomShapeMode, profile.m_BloomShapeMode);
	ApplyOptionalValue(config->bloomShapeIntensity, profile.m_BloomShapeIntensity);
	ApplyOptionalValue(config->bloomShapeBlend, profile.m_BloomShapeBlend);
	ApplyOptionalValue(config->bloomShapeAngleDeg, profile.m_BloomShapeAngleDeg);
	ApplyOptionalValue(config->bloomAnamorphicStretch, profile.m_BloomAnamorphicStretch);
	ApplyOptionalValue(config->bloomStreakCount, profile.m_BloomStreakCount);
	ApplyOptionalValue(config->bloomStreakLength, profile.m_BloomStreakLength);
	ApplyOptionalValue(config->bloomStreakAttenuation, profile.m_BloomStreakAttenuation);
	ApplyOptionalValue(config->enableDOF, profile.m_EnableDOF);
	ApplyOptionalValue(config->focusDistance, profile.m_FocusDistance);
	ApplyOptionalValue(config->focalLengthMm, profile.m_FocalLengthMm);
	ApplyOptionalValue(config->fStop, profile.m_FStop);
	ApplyOptionalValue(config->sensorHeightMm, profile.m_SensorHeightMm);
	ApplyOptionalValue(config->maxCoC, profile.m_MaxCoC);
	ApplyOptionalValue(config->dofBlurTransmissionBackground, profile.m_DOFBlurTransmissionBackground);
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
	profile.m_BloomClamp = std::clamp(profile.m_BloomClamp, 0.0f, 1024.0f);
	profile.m_BloomTintR = std::clamp(profile.m_BloomTintR, 0.0f, 8.0f);
	profile.m_BloomTintG = std::clamp(profile.m_BloomTintG, 0.0f, 8.0f);
	profile.m_BloomTintB = std::clamp(profile.m_BloomTintB, 0.0f, 8.0f);
	profile.m_BloomShapeMode = std::clamp(profile.m_BloomShapeMode, 0, 2);
	profile.m_BloomShapeIntensity = std::clamp(profile.m_BloomShapeIntensity, 0.0f, 4.0f);
	profile.m_BloomShapeBlend = std::clamp(profile.m_BloomShapeBlend, 0.0f, 1.0f);
	profile.m_BloomShapeAngleDeg = std::clamp(profile.m_BloomShapeAngleDeg, -360.0f, 360.0f);
	profile.m_BloomAnamorphicStretch = std::clamp(profile.m_BloomAnamorphicStretch, 0.0f, 16.0f);
	profile.m_BloomStreakCount = std::clamp(profile.m_BloomStreakCount, 2, 8);
	profile.m_BloomStreakLength = std::clamp(profile.m_BloomStreakLength, 0.0f, 128.0f);
	profile.m_BloomStreakAttenuation = std::clamp(profile.m_BloomStreakAttenuation, 0.0f, 0.98f);
	profile.m_FocusDistance = std::clamp(profile.m_FocusDistance, 0.01f, 100000.0f);
	profile.m_FocalLengthMm = std::clamp(profile.m_FocalLengthMm, 8.0f, 300.0f);
	profile.m_FStop = std::clamp(profile.m_FStop, 0.7f, 32.0f);
	profile.m_SensorHeightMm = std::clamp(profile.m_SensorHeightMm, 1.0f, 80.0f);
	profile.m_MaxCoC = std::clamp(profile.m_MaxCoC, 0.0f, 64.0f);
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
		if (!config->regions.empty())
		{
			giSettings.regions.clear();
			giSettings.regions.reserve(config->regions.size());
			for (size_t index = 0; index < config->regions.size(); ++index)
			{
				const Vans::VansSceneGIRegionSettingsConfig& sourceRegion = config->regions[index];
				GIProbeRegionDesc region{};
				region.stableId = sourceRegion.stableId.value_or(static_cast<uint32_t>(index + 1u));
				region.name = sourceRegion.name.value_or(index == 0 ? std::string("Default") : ("GI Region " + std::to_string(index + 1u)));
				ApplyOptionalValue(sourceRegion.enabled, region.enabled);
				if (sourceRegion.center.has_value())
					region.center = glm::vec3((*sourceRegion.center)[0], (*sourceRegion.center)[1], (*sourceRegion.center)[2]);
				if (sourceRegion.size.has_value())
				{
					region.size = glm::vec3(std::max((*sourceRegion.size)[0], 0.001f), std::max((*sourceRegion.size)[1], 0.001f), std::max((*sourceRegion.size)[2], 0.001f));
					region.overrideGridDimensions = false;
				}
				if (sourceRegion.probeSpacing.has_value())
				{
					region.probeSpacing = std::max(*sourceRegion.probeSpacing, 0.001f);
				}
				else if (sourceRegion.probeSpacingAxes.has_value())
				{
					const auto& spacingAxes = *sourceRegion.probeSpacingAxes;
					region.probeSpacing = std::max(std::max(spacingAxes[0], spacingAxes[1]), spacingAxes[2]);
					region.probeSpacing = std::max(region.probeSpacing, 0.001f);
				}
				if (sourceRegion.gridDimensions.has_value())
				{
					region.gridDimensions = glm::uvec3(std::clamp((*sourceRegion.gridDimensions)[0], 1u, 256u), std::clamp((*sourceRegion.gridDimensions)[1], 1u, 256u), std::clamp((*sourceRegion.gridDimensions)[2], 1u, 256u));
					region.overrideGridDimensions = true;
				}
				ApplyOptionalValue(sourceRegion.raysPerProbe, region.raysPerProbe);
				ApplyOptionalValue(sourceRegion.spatialUpdateDivisor, region.spatialUpdateDivisor);
				ApplyOptionalValue(sourceRegion.directionUpdateSlices, region.directionUpdateSlices);
				ApplyOptionalValue(sourceRegion.maxRayDistance, region.maxRayDistance);
				ApplyOptionalValue(sourceRegion.normalBias, region.normalBias);
				ApplyOptionalValue(sourceRegion.volumeFadeDistance, region.volumeFadeDistance);
				ApplyOptionalValue(sourceRegion.priority, region.priority);
				giSettings.regions.push_back(region);
			}
		}
		if (config->environmentIntensity.has_value())
			giSettings.environmentIntensity = std::max(*config->environmentIntensity, 0.0f);
		if (config->maxIndirectRadiance.has_value())
			giSettings.maxIndirectRadiance = std::max(*config->maxIndirectRadiance, 0.0f);
		if (config->maxProbeRadiance.has_value())
			giSettings.maxProbeRadiance = std::max(*config->maxProbeRadiance, 0.0f);
		if (config->irradianceHysteresis.has_value())
			giSettings.irradianceHysteresis = *config->irradianceHysteresis;
		if (config->distanceHysteresis.has_value())
			giSettings.distanceHysteresis = *config->distanceHysteresis;
		if (config->distanceSharpness.has_value())
			giSettings.distanceSharpness = *config->distanceSharpness;
		if (config->brightnessChangeThreshold.has_value())
			giSettings.brightnessChangeThreshold = *config->brightnessChangeThreshold;
		ApplyOptionalValue(config->showProbeGizmos, giSettings.showProbeGizmos);
		ApplyOptionalValue(config->showProbeVolume, giSettings.showProbeVolume);
		if (config->gizmoStride.has_value())
			giSettings.gizmoStride = std::max(*config->gizmoStride, 1u);
	}

	NormalizeGISettings(giSettings);
	scene.SetGISettings(giSettings);

	uint64_t totalProbeCount = 0;
	uint64_t totalRayCacheEntries = 0;
	for (const GIProbeRegionDesc& desc : giSettings.regions)
	{
		const GIResolvedRegion region = ResolveGIRegion(desc);
		const GIProbeUpdateBatch batch = BuildGIProbeUpdateBatch(region, 0u);
		totalProbeCount += region.enabled ? region.probeCount : 0u;
		totalRayCacheEntries += region.enabled ? batch.activeRayCount : 0u;
	}
	VANS_LOG("[GISettings] regions=" << giSettings.regions.size()
		<< " activeProbes=" << totalProbeCount
		<< " activeRayWorkingSet=" << totalRayCacheEntries);
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
