#include "VansSceneRenderSettingsConfigReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <cstddef>

namespace Vans
{
namespace
{
const VansSerializedValue* ReadObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* ReadArrayField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

std::optional<std::string> ReadOptionalStringField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::String)
	{
		return std::nullopt;
	}
	return field->stringValue;
}

std::optional<float> ReadOptionalFloatField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field)
	{
		return std::nullopt;
	}
	if (field->kind == VansSerializedValue::Kind::Float || field->kind == VansSerializedValue::Kind::Int)
	{
		return static_cast<float>(ReadSerializedNumber(*field));
	}
	return std::nullopt;
}

std::optional<uint32_t> ReadOptionalUIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field)
	{
		return std::nullopt;
	}
	if (field->kind == VansSerializedValue::Kind::Int && field->intValue >= 0)
	{
		return static_cast<uint32_t>(field->intValue);
	}
	return std::nullopt;
}

std::optional<int32_t> ReadOptionalIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Int)
	{
		return std::nullopt;
	}
	return static_cast<int32_t>(field->intValue);
}

std::optional<bool> ReadOptionalBoolField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Bool
		? std::optional<bool>(field->boolValue)
		: std::nullopt;
}

std::optional<std::array<float, 3>> ReadOptionalFloat3Field(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Array || field->arrayItems.size() < 3)
	{
		return std::nullopt;
	}

	for (size_t index = 0; index < 3; ++index)
	{
		const VansSerializedValue& item = field->arrayItems[index];
		if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int)
		{
			return std::nullopt;
		}
	}

	return std::array<float, 3>{
		static_cast<float>(ReadSerializedNumber(field->arrayItems[0])),
		static_cast<float>(ReadSerializedNumber(field->arrayItems[1])),
		static_cast<float>(ReadSerializedNumber(field->arrayItems[2]))
	};
}

std::optional<std::array<uint32_t, 3>> ReadOptionalUInt3Field(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Array || field->arrayItems.size() < 3)
	{
		return std::nullopt;
	}

	std::array<uint32_t, 3> values{};
	for (size_t index = 0; index < values.size(); ++index)
	{
		const VansSerializedValue& item = field->arrayItems[index];
		if (item.kind != VansSerializedValue::Kind::Int || item.intValue < 0)
		{
			return std::nullopt;
		}
		values[index] = static_cast<uint32_t>(item.intValue);
	}
	return values;
}

std::optional<VansSceneGIRegionSettingsConfig> DecodeGIRegionSettings(const VansSerializedValue& regionNode)
{
	if (regionNode.kind != VansSerializedValue::Kind::Object)
	{
		return std::nullopt;
	}

	VansSceneGIRegionSettingsConfig config;
	config.stableId = ReadOptionalUIntField(regionNode, "stableId");
	config.name = ReadOptionalStringField(regionNode, "name");
	config.enabled = ReadOptionalBoolField(regionNode, "enabled");
	config.center = ReadOptionalFloat3Field(regionNode, "center");
	config.size = ReadOptionalFloat3Field(regionNode, "size");
	config.gridDimensions = ReadOptionalUInt3Field(regionNode, "gridDimensions");
	config.probeSpacing = ReadOptionalFloatField(regionNode, "probeSpacing");
	config.probeSpacingAxes = ReadOptionalFloat3Field(regionNode, "probeSpacingAxes");
	config.raysPerProbe = ReadOptionalUIntField(regionNode, "raysPerProbe");
	config.spatialUpdateDivisor = ReadOptionalUIntField(regionNode, "spatialUpdateDivisor");
	config.directionUpdateSlices = ReadOptionalUIntField(regionNode, "directionUpdateSlices");
	config.maxRayDistance = ReadOptionalFloatField(regionNode, "maxRayDistance");
	config.normalBias = ReadOptionalFloatField(regionNode, "normalBias");
	config.volumeFadeDistance = ReadOptionalFloatField(regionNode, "volumeFadeDistance");
	config.priority = ReadOptionalFloatField(regionNode, "priority");
	return config;
}

std::optional<VansSceneHeightFogSettingsConfig> DecodeHeightFog(const VansSerializedValue& sceneSettings)
{
	const VansSerializedValue* fog = ReadObjectField(sceneSettings, "heightFog");
	if (!fog)
	{
		return std::nullopt;
	}

	VansSceneHeightFogSettingsConfig config;
	config.fogDensity = ReadOptionalFloatField(*fog, "fogDensity");
	config.heightFalloff = ReadOptionalFloatField(*fog, "heightFalloff");
	config.sunScatterScale = ReadOptionalFloatField(*fog, "sunScatterScale");
	config.ambientScale = ReadOptionalFloatField(*fog, "ambientScale");
	config.fogMinHeight = ReadOptionalFloatField(*fog, "fogMinHeight");
	config.skyFogDistance = ReadOptionalFloatField(*fog, "skyFogDistance");
	return config;
}

std::optional<VansSceneVolumetricFogSettingsConfig> DecodeVolumetricFog(const VansSerializedValue& sceneSettings)
{
	const VansSerializedValue* fog = ReadObjectField(sceneSettings, "volumetricFog");
	if (!fog)
	{
		return std::nullopt;
	}

	VansSceneVolumetricFogSettingsConfig config;
	config.density = ReadOptionalFloatField(*fog, "density");
	config.anisotropy = ReadOptionalFloatField(*fog, "anisotropy");
	config.scatterScale = ReadOptionalFloatField(*fog, "scatterScale");
	config.ambientScale = ReadOptionalFloatField(*fog, "ambientScale");
	config.volumeNear = ReadOptionalFloatField(*fog, "volumeNear");
	config.volumeFar = ReadOptionalFloatField(*fog, "volumeFar");
	config.slicePower = ReadOptionalFloatField(*fog, "slicePower");
	config.fogBoxMin = ReadOptionalFloat3Field(*fog, "fogBoxMin");
	config.fogBoxMax = ReadOptionalFloat3Field(*fog, "fogBoxMax");
	return config;
}

std::optional<VansSceneVolumetricCloudSettingsConfig> DecodeVolumetricClouds(const VansSerializedValue& sceneSettings)
{
	const VansSerializedValue* cloud = ReadObjectField(sceneSettings, "volumetricClouds");
	if (!cloud)
	{
		return std::nullopt;
	}

	VansSceneVolumetricCloudSettingsConfig config;
	config.planetRadius = ReadOptionalFloatField(*cloud, "planetRadius");
	config.seaLevel = ReadOptionalFloatField(*cloud, "seaLevel");
	config.cloudBaseHeight = ReadOptionalFloatField(*cloud, "cloudBaseHeight");
	config.cloudMinHeight = ReadOptionalFloatField(*cloud, "cloudMinHeight");
	config.cloudThickness = ReadOptionalFloatField(*cloud, "cloudThickness");
	config.cloudMaxHeight = ReadOptionalFloatField(*cloud, "cloudMaxHeight");
	config.density = ReadOptionalFloatField(*cloud, "density");
	config.coverage = ReadOptionalFloatField(*cloud, "coverage");
	config.sunBrightness = ReadOptionalFloatField(*cloud, "sunBrightness");
	config.phaseG = ReadOptionalFloatField(*cloud, "phaseG");
	config.mainTileMeters = ReadOptionalFloatField(*cloud, "mainTileMeters");
	config.detailTileMeters = ReadOptionalFloatField(*cloud, "detailTileMeters");
	config.mainHeightScale = ReadOptionalFloatField(*cloud, "mainHeightScale");
	config.detailHeightScale = ReadOptionalFloatField(*cloud, "detailHeightScale");
	config.thresholdLowCoverage = ReadOptionalFloatField(*cloud, "thresholdLowCoverage");
	config.thresholdHighCoverage = ReadOptionalFloatField(*cloud, "thresholdHighCoverage");
	config.densityRemapLow = ReadOptionalFloatField(*cloud, "densityRemapLow");
	config.densityRemapHigh = ReadOptionalFloatField(*cloud, "densityRemapHigh");
	config.mainErosionStrength = ReadOptionalFloatField(*cloud, "mainErosionStrength");
	config.detailErosionStrength = ReadOptionalFloatField(*cloud, "detailErosionStrength");
	config.edgeErosionStrength = ReadOptionalFloatField(*cloud, "edgeErosionStrength");
	config.verticalShapePower = ReadOptionalFloatField(*cloud, "verticalShapePower");
	config.detailErosionLow = ReadOptionalFloatField(*cloud, "detailErosionLow");
	config.detailErosionHigh = ReadOptionalFloatField(*cloud, "detailErosionHigh");
	config.detailEdgeStrength = ReadOptionalFloatField(*cloud, "detailEdgeStrength");
	config.shadowDensityScale = ReadOptionalFloatField(*cloud, "shadowDensityScale");
	config.sigmaTRef = ReadOptionalFloatField(*cloud, "sigmaTRef");
	config.viewAbsorption = ReadOptionalFloatField(*cloud, "viewAbsorption");
	config.lightAbsorption = ReadOptionalFloatField(*cloud, "lightAbsorption");
	config.singleScatteringAlbedo = ReadOptionalFloatField(*cloud, "singleScatteringAlbedo");
	config.forwardEccentricity = ReadOptionalFloatField(*cloud, "forwardEccentricity");
	config.backwardEccentricity = ReadOptionalFloatField(*cloud, "backwardEccentricity");
	config.msAttenuation = ReadOptionalFloatField(*cloud, "msAttenuation");
	config.msContribution = ReadOptionalFloatField(*cloud, "msContribution");
	config.msEccentricity = ReadOptionalFloatField(*cloud, "msEccentricity");
	config.scatteringTintR = ReadOptionalFloatField(*cloud, "scatteringTintR");
	config.scatteringTintG = ReadOptionalFloatField(*cloud, "scatteringTintG");
	config.scatteringTintB = ReadOptionalFloatField(*cloud, "scatteringTintB");
	config.scatterSourceODScale = ReadOptionalFloatField(*cloud, "scatterSourceODScale");
	config.scatterSourceCurvePow = ReadOptionalFloatField(*cloud, "scatterSourceCurvePow");
	config.aoUpwardScale = ReadOptionalFloatField(*cloud, "aoUpwardScale");
	config.ambientBottomStrength = ReadOptionalFloatField(*cloud, "ambientBottomStrength");
	config.ambientTopStrength = ReadOptionalFloatField(*cloud, "ambientTopStrength");
	config.ambientDuskWarmth = ReadOptionalFloatField(*cloud, "ambientDuskWarmth");
	config.boundaryConfidence = ReadOptionalFloatField(*cloud, "boundaryConfidence");
	config.boundaryWrap = ReadOptionalFloatField(*cloud, "boundaryWrap");
	config.phiFwdIntensity = ReadOptionalFloatField(*cloud, "phiFwdIntensity");
	config.phiFwdDepthPow = ReadOptionalFloatField(*cloud, "phiFwdDepthPow");
	config.phiFwdDepthBias = ReadOptionalFloatField(*cloud, "phiFwdDepthBias");
	config.phiFwdMSBuildScale = ReadOptionalFloatField(*cloud, "phiFwdMSBuildScale");
	config.phiFwdCompress = ReadOptionalFloatField(*cloud, "phiFwdCompress");
	config.phiFwdMaxDistance = ReadOptionalFloatField(*cloud, "phiFwdMaxDistance");
	config.phiFwdConeRatio = ReadOptionalFloatField(*cloud, "phiFwdConeRatio");
	config.phiFwdMinStep = ReadOptionalFloatField(*cloud, "phiFwdMinStep");
	config.lightStepCount = ReadOptionalFloatField(*cloud, "lightStepCount");
	config.boundaryGradientStep = ReadOptionalFloatField(*cloud, "boundaryGradientStep");
	config.boundaryGradientStrength = ReadOptionalFloatField(*cloud, "boundaryGradientStrength");
	config.shadingDebugMode = ReadOptionalFloatField(*cloud, "shadingDebugMode");
	return config;
}

std::optional<VansScenePostProcessSettingsConfig> DecodePostProcess(
	const VansSerializedValue& sceneSettings)
{
	const VansSerializedValue* postProcess = ReadObjectField(sceneSettings, "postProcess");
	if (!postProcess)
	{
		return std::nullopt;
	}

	VansScenePostProcessSettingsConfig config;
	if (const VansSerializedValue* exposure = ReadObjectField(*postProcess, "exposure"))
	{
		config.enableAutoExposure = ReadOptionalBoolField(*exposure, "enableAutoExposure");
		config.exposureCompensation = ReadOptionalFloatField(*exposure, "exposureCompensation");
		config.minEV100 = ReadOptionalFloatField(*exposure, "minEV100");
		config.maxEV100 = ReadOptionalFloatField(*exposure, "maxEV100");
		config.adaptationSpeedUp = ReadOptionalFloatField(*exposure, "adaptationSpeedUp");
		config.adaptationSpeedDown = ReadOptionalFloatField(*exposure, "adaptationSpeedDown");
	}
	if (const VansSerializedValue* bloom = ReadObjectField(*postProcess, "bloom"))
	{
		config.enableBloom = ReadOptionalBoolField(*bloom, "enable");
		config.bloomThreshold = ReadOptionalFloatField(*bloom, "threshold");
		config.bloomKnee = ReadOptionalFloatField(*bloom, "knee");
		config.bloomIntensity = ReadOptionalFloatField(*bloom, "intensity");
		config.bloomScatter = ReadOptionalFloatField(*bloom, "scatter");
	}
	if (const VansSerializedValue* toneMapping = ReadObjectField(*postProcess, "toneMapping"))
	{
		config.toneMapperType = ReadOptionalIntField(*toneMapping, "type");
		config.whitePoint = ReadOptionalFloatField(*toneMapping, "whitePoint");
	}
	if (const VansSerializedValue* colorGrading = ReadObjectField(*postProcess, "colorGrading"))
	{
		config.enableColorGrading = ReadOptionalBoolField(*colorGrading, "enable");
		config.contrast = ReadOptionalFloatField(*colorGrading, "contrast");
		config.saturation = ReadOptionalFloatField(*colorGrading, "saturation");
		config.hueShift = ReadOptionalFloatField(*colorGrading, "hueShift");
		config.temperature = ReadOptionalFloatField(*colorGrading, "temperature");
		config.tint = ReadOptionalFloatField(*colorGrading, "tint");
	}
	return config;
}

std::optional<VansSceneGISettingsConfig> DecodeGISettings(const VansSerializedValue& sceneSettings)
{
	const VansSerializedValue* gi = ReadObjectField(sceneSettings, "globalIllumination");
	if (!gi)
	{
		return std::nullopt;
	}

	VansSceneGISettingsConfig config;
	if (const VansSerializedValue* regions = ReadArrayField(*gi, "regions"))
	{
		for (const VansSerializedValue& regionNode : regions->arrayItems)
		{
			if (std::optional<VansSceneGIRegionSettingsConfig> region = DecodeGIRegionSettings(regionNode))
			{
				config.regions.push_back(*region);
			}
		}
	}
	config.environmentIntensity = ReadOptionalFloatField(*gi, "environmentIntensity");
	config.maxIndirectRadiance = ReadOptionalFloatField(*gi, "maxIndirectRadiance");
	config.maxProbeRadiance = ReadOptionalFloatField(*gi, "maxProbeRadiance");
	config.irradianceHysteresis = ReadOptionalFloatField(*gi, "irradianceHysteresis");
	config.distanceHysteresis = ReadOptionalFloatField(*gi, "distanceHysteresis");
	config.distanceSharpness = ReadOptionalFloatField(*gi, "distanceSharpness");
	config.brightnessChangeThreshold = ReadOptionalFloatField(*gi, "brightnessChangeThreshold");
	config.showProbeGizmos = ReadOptionalBoolField(*gi, "showProbeGizmos");
	config.showProbeVolume = ReadOptionalBoolField(*gi, "showProbeVolume");
	config.gizmoStride = ReadOptionalUIntField(*gi, "gizmoStride");
	return config;
}

std::optional<VansSceneMainCameraHiZCullSettingsConfig> DecodeMainCameraHiZCulling(
	const VansSerializedValue& sceneSettings)
{
	const VansSerializedValue* hiz = ReadObjectField(sceneSettings, "mainCameraHiZCulling");
	if (!hiz)
	{
		return std::nullopt;
	}

	VansSceneMainCameraHiZCullSettingsConfig config;
	config.enabled = ReadOptionalBoolField(*hiz, "enabled");
	config.enableOpaque = ReadOptionalBoolField(*hiz, "enableOpaque");
	config.enableHair = ReadOptionalBoolField(*hiz, "enableHair");
	config.enableTransparent = ReadOptionalBoolField(*hiz, "enableTransparent");
	config.enableDecal = ReadOptionalBoolField(*hiz, "enableDecal");
	config.enableForwardOpaqueAfterDeferred = ReadOptionalBoolField(*hiz, "enableForwardOpaqueAfterDeferred");
	config.depthBiasMeters = ReadOptionalFloatField(*hiz, "depthBiasMeters");
	config.cameraMotionDisableDistance = ReadOptionalFloatField(*hiz, "cameraMotionDisableDistance");
	config.cameraMotionDisableAngleRadians = ReadOptionalFloatField(*hiz, "cameraMotionDisableAngleRadians");
	config.forceVisibleFramesAfterChange = ReadOptionalUIntField(*hiz, "forceVisibleFramesAfterChange");
	config.refreshCulledEveryNFrames = ReadOptionalUIntField(*hiz, "refreshCulledEveryNFrames");
	config.maxScreenCoverageForCull = ReadOptionalFloatField(*hiz, "maxScreenCoverageForCull");
	return config;
}
}

VansSceneRenderSettingsConfig VansSceneRenderSettingsConfigReader::Read(
	const VansSerializedValue& sceneSettings)
{
	VansSceneRenderSettingsConfig config;
	if (sceneSettings.kind != VansSerializedValue::Kind::Object)
	{
		return config;
	}

	config.heightFog = DecodeHeightFog(sceneSettings);
	config.volumetricFog = DecodeVolumetricFog(sceneSettings);
	config.volumetricClouds = DecodeVolumetricClouds(sceneSettings);
	config.postProcess = DecodePostProcess(sceneSettings);
	config.globalIllumination = DecodeGISettings(sceneSettings);
	config.mainCameraHiZCulling = DecodeMainCameraHiZCulling(sceneSettings);
	return config;
}
}
