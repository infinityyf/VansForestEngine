#pragma once
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include <array>

namespace Vans
{
// 场景配置与编辑器 DTO 使用同一个作者字段写出器，避免环境 schema 分叉。
template <typename EnvironmentSettings>
VansSerializedValue WriteSceneEnvironmentSettings(const EnvironmentSettings& settings)
{
		auto float3 = [](const std::array<float, 3>& value)
		{
			return VansSerializedValue::Array({
				VansSerializedValue::Float(value[0]),
				VansSerializedValue::Float(value[1]),
				VansSerializedValue::Float(value[2])
			});
		};
		auto double3 = [](const std::array<double, 3>& value)
		{
			return VansSerializedValue::Array({
				VansSerializedValue::Float(value[0]),
				VansSerializedValue::Float(value[1]),
				VansSerializedValue::Float(value[2])
			});
		};
		std::vector<VansSerializedValue> celestialBodies;
		celestialBodies.reserve(settings.physicalAtmosphere.celestialBodies.size());
		for (const auto& body :
			settings.physicalAtmosphere.celestialBodies)
		{
			celestialBodies.push_back(VansSerializedValue::Object({
				{ "name", VansSerializedValue::String(body.name) },
				{ "lightEntityId", VansSerializedValue::String(body.lightEntityId) },
				{ "disk", VansSerializedValue::Object({
					{ "enabled", VansSerializedValue::Bool(body.disk.enabled) },
					{ "angularRadiusRadians", VansSerializedValue::Float(body.disk.angularRadiusRadians) },
					{ "featherRadians", VansSerializedValue::Float(body.disk.featherRadians) },
					{ "radianceScale", VansSerializedValue::Float(body.disk.radianceScale) },
					{ "occlusionStrength", VansSerializedValue::Float(body.disk.occlusionStrength) }
				}) }
			}));
		}
		const auto& cloud = settings.volumetricClouds;
        return VansSerializedValue::Object({
                { "skyLighting", VansSerializedValue::Object({ { "intensity", VansSerializedValue::Float(settings.skyLighting.intensity) } }) },
				{ "planet", VansSerializedValue::Object({
					{ "centerWorldMeters", double3(settings.planet.centerWorldMeters) },
					{ "bottomRadiusMeters", VansSerializedValue::Float(settings.planet.bottomRadiusMeters) },
					{ "atmosphereHeightMeters", VansSerializedValue::Float(settings.planet.atmosphereHeightMeters) }
				}) },
				{ "physicalAtmosphere", VansSerializedValue::Object({
					{ "enabled", VansSerializedValue::Bool(settings.physicalAtmosphere.enabled) },
					{ "groundAlbedo", float3(settings.physicalAtmosphere.groundAlbedo) },
					{ "rayleigh", VansSerializedValue::Object({
						{ "scatteringPerMeterAtGround", float3(settings.physicalAtmosphere.rayleigh.scatteringPerMeterAtGround) },
						{ "densityScaleHeightMeters", VansSerializedValue::Float(settings.physicalAtmosphere.rayleigh.densityScaleHeightMeters) }
					}) },
					{ "mie", VansSerializedValue::Object({
						{ "scatteringPerMeterAtGround", float3(settings.physicalAtmosphere.mie.scatteringPerMeterAtGround) },
						{ "absorptionPerMeterAtGround", float3(settings.physicalAtmosphere.mie.absorptionPerMeterAtGround) },
						{ "densityScaleHeightMeters", VansSerializedValue::Float(settings.physicalAtmosphere.mie.densityScaleHeightMeters) },
						{ "anisotropy", VansSerializedValue::Float(settings.physicalAtmosphere.mie.anisotropy) }
					}) },
					{ "ozone", VansSerializedValue::Object({
						{ "absorptionPerMeter", float3(settings.physicalAtmosphere.ozone.absorptionPerMeter) },
						{ "centerAltitudeMeters", VansSerializedValue::Float(settings.physicalAtmosphere.ozone.centerAltitudeMeters) },
						{ "halfWidthMeters", VansSerializedValue::Float(settings.physicalAtmosphere.ozone.halfWidthMeters) }
					}) },
					{ "aerialPerspective", VansSerializedValue::Object({
						{ "distanceScale", VansSerializedValue::Float(settings.physicalAtmosphere.aerialPerspective.distanceScale) }
					}) },
					{ "mainLightVolumetricScatteringScale", VansSerializedValue::Float(
						settings.physicalAtmosphere.mainLightVolumetricScatteringScale) },
					{ "celestialBodies", VansSerializedValue::Array(std::move(celestialBodies)) }
				}) },
				{ "heightFog", VansSerializedValue::Object({
					{ "enabled", VansSerializedValue::Bool(settings.heightFog.enabled) },
					{ "groundHeightWorldMeters", VansSerializedValue::Float(settings.heightFog.groundHeightWorldMeters) },
					{ "visibilityAtGroundMeters", VansSerializedValue::Float(settings.heightFog.visibilityAtGroundMeters) },
					{ "densityFalloffHeightMeters", VansSerializedValue::Float(settings.heightFog.densityFalloffHeightMeters) },
					{ "startDistanceMeters", VansSerializedValue::Float(settings.heightFog.startDistanceMeters) },
					{ "nearFadeDistanceMeters", VansSerializedValue::Float(settings.heightFog.nearFadeDistanceMeters) },
					{ "maximumDistanceMeters", VansSerializedValue::Float(settings.heightFog.maximumDistanceMeters) },
					{ "farFadeDistanceMeters", VansSerializedValue::Float(settings.heightFog.farFadeDistanceMeters) },
					{ "singleScatteringAlbedo", float3(settings.heightFog.singleScatteringAlbedo) },
					{ "anisotropy", VansSerializedValue::Float(settings.heightFog.anisotropy) },
					{ "emissivePerMeter", float3(settings.heightFog.emissivePerMeter) },
					{ "skyLightingScale", VansSerializedValue::Float(settings.heightFog.skyLightingScale) },
					{ "mainLightVolumetricScale", VansSerializedValue::Float(settings.heightFog.mainLightVolumetricScale) },
					{ "receiveCloudShadows", VansSerializedValue::Bool(settings.heightFog.receiveCloudShadows) }
				}) },
				{ "volumetricClouds", VansSerializedValue::Object({
					{ "enabled", VansSerializedValue::Bool(cloud.enabled) },
					{ "cloudMinHeight", VansSerializedValue::Float(cloud.cloudMinHeight) },
					{ "cloudMaxHeight", VansSerializedValue::Float(cloud.cloudMaxHeight) },
					{ "density", VansSerializedValue::Float(cloud.density) },
					{ "coverage", VansSerializedValue::Float(cloud.coverage) },
					{ "sunBrightness", VansSerializedValue::Float(cloud.sunBrightness) },
					{ "mainTileMeters", VansSerializedValue::Float(cloud.mainTileMeters) },
					{ "detailTileMeters", VansSerializedValue::Float(cloud.detailTileMeters) },
					{ "mainHeightScale", VansSerializedValue::Float(cloud.mainHeightScale) },
					{ "detailHeightScale", VansSerializedValue::Float(cloud.detailHeightScale) },
					{ "thresholdLowCoverage", VansSerializedValue::Float(cloud.thresholdLowCoverage) },
					{ "thresholdHighCoverage", VansSerializedValue::Float(cloud.thresholdHighCoverage) },
					{ "densityRemapLow", VansSerializedValue::Float(cloud.densityRemapLow) },
					{ "densityRemapHigh", VansSerializedValue::Float(cloud.densityRemapHigh) },
					{ "mainErosionStrength", VansSerializedValue::Float(cloud.mainErosionStrength) },
					{ "detailErosionStrength", VansSerializedValue::Float(cloud.detailErosionStrength) },
					{ "edgeErosionStrength", VansSerializedValue::Float(cloud.edgeErosionStrength) },
					{ "verticalShapePower", VansSerializedValue::Float(cloud.verticalShapePower) },
					{ "detailErosionLow", VansSerializedValue::Float(cloud.detailErosionLow) },
					{ "detailErosionHigh", VansSerializedValue::Float(cloud.detailErosionHigh) },
					{ "detailEdgeStrength", VansSerializedValue::Float(cloud.detailEdgeStrength) },
					{ "sigmaTRef", VansSerializedValue::Float(cloud.sigmaTRef) },
					{ "viewAbsorption", VansSerializedValue::Float(cloud.viewAbsorption) },
					{ "lightAbsorption", VansSerializedValue::Float(cloud.lightAbsorption) },
					{ "singleScatteringAlbedo", VansSerializedValue::Float(cloud.singleScatteringAlbedo) },
					{ "forwardEccentricity", VansSerializedValue::Float(cloud.forwardEccentricity) },
					{ "backwardEccentricity", VansSerializedValue::Float(cloud.backwardEccentricity) },
					{ "msAttenuation", VansSerializedValue::Float(cloud.msAttenuation) },
					{ "msContribution", VansSerializedValue::Float(cloud.msContribution) },
					{ "msEccentricity", VansSerializedValue::Float(cloud.msEccentricity) },
					{ "scatteringTintR", VansSerializedValue::Float(cloud.scatteringTintR) },
					{ "scatteringTintG", VansSerializedValue::Float(cloud.scatteringTintG) },
					{ "scatteringTintB", VansSerializedValue::Float(cloud.scatteringTintB) },
					{ "scatterSourceODScale", VansSerializedValue::Float(cloud.scatterSourceODScale) },
					{ "scatterSourceCurvePow", VansSerializedValue::Float(cloud.scatterSourceCurvePow) },
					{ "aoUpwardScale", VansSerializedValue::Float(cloud.aoUpwardScale) },
					{ "ambientBottomStrength", VansSerializedValue::Float(cloud.ambientBottomStrength) },
					{ "ambientTopStrength", VansSerializedValue::Float(cloud.ambientTopStrength) },
					{ "ambientDuskWarmth", VansSerializedValue::Float(cloud.ambientDuskWarmth) },
					{ "boundaryConfidence", VansSerializedValue::Float(cloud.boundaryConfidence) },
					{ "boundaryWrap", VansSerializedValue::Float(cloud.boundaryWrap) },
					{ "phiFwdIntensity", VansSerializedValue::Float(cloud.phiFwdIntensity) },
					{ "phiFwdDepthPow", VansSerializedValue::Float(cloud.phiFwdDepthPow) },
					{ "phiFwdDepthBias", VansSerializedValue::Float(cloud.phiFwdDepthBias) },
					{ "phiFwdMSBuildScale", VansSerializedValue::Float(cloud.phiFwdMSBuildScale) },
					{ "phiFwdCompress", VansSerializedValue::Float(cloud.phiFwdCompress) },
					{ "phiFwdMaxDistance", VansSerializedValue::Float(cloud.phiFwdMaxDistance) },
					{ "phiFwdConeRatio", VansSerializedValue::Float(cloud.phiFwdConeRatio) },
					{ "phiFwdMinStep", VansSerializedValue::Float(cloud.phiFwdMinStep) },
					{ "lightStepCount", VansSerializedValue::Float(cloud.lightStepCount) },
					{ "boundaryGradientStep", VansSerializedValue::Float(cloud.boundaryGradientStep) },
					{ "boundaryGradientStrength", VansSerializedValue::Float(cloud.boundaryGradientStrength) },
					{ "shadingDebugMode", VansSerializedValue::Float(cloud.shadingDebugMode) },
					{ "shadow", VansSerializedValue::Object({
						{ "enabled", VansSerializedValue::Bool(cloud.shadow.enabled) },
						{ "atmosphereStrength", VansSerializedValue::Float(cloud.shadow.atmosphereStrength) },
						{ "ambientOcclusionStrength", VansSerializedValue::Float(cloud.shadow.ambientOcclusionStrength) }
					}) }
				}) }
			});
}
}
