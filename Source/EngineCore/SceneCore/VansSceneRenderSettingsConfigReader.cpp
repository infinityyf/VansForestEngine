#include "VansSceneRenderSettingsConfigReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
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

std::optional<std::array<double, 3>> ReadOptionalDouble3Field(
	const VansSerializedValue& object,
	const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Array || field->arrayItems.size() != 3)
	{
		return std::nullopt;
	}

	std::array<double, 3> values{};
	for (size_t index = 0; index < values.size(); ++index)
	{
		const VansSerializedValue& item = field->arrayItems[index];
		if (item.kind != VansSerializedValue::Kind::Float &&
			item.kind != VansSerializedValue::Kind::Int)
		{
			return std::nullopt;
		}
		values[index] = ReadSerializedNumber(item);
	}
	return values;
}

bool RequireObjectField(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	const VansSerializedValue*& value,
	std::string& error)
{
	value = ReadObjectField(object, key);
	if (value)
	{
		return true;
	}
	error = std::string(path) + "/" + key + " must be an object";
	return false;
}

bool RequireArrayField(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	const VansSerializedValue*& value,
	std::string& error)
{
	value = ReadArrayField(object, key);
	if (value)
	{
		return true;
	}
	error = std::string(path) + "/" + key + " must be an array";
	return false;
}

bool RequireBoolField(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	bool& value,
	std::string& error)
{
	const std::optional<bool> parsed = ReadOptionalBoolField(object, key);
	if (parsed)
	{
		value = *parsed;
		return true;
	}
	error = std::string(path) + "/" + key + " must be a bool";
	return false;
}

bool RequireFloatField(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	float& value,
	std::string& error)
{
	const std::optional<float> parsed = ReadOptionalFloatField(object, key);
	if (parsed)
	{
		value = *parsed;
		return true;
	}
	error = std::string(path) + "/" + key + " must be numeric";
	return false;
}

bool RequireDoubleField(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	double& value,
	std::string& error)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (field && (field->kind == VansSerializedValue::Kind::Float ||
		field->kind == VansSerializedValue::Kind::Int))
	{
		value = ReadSerializedNumber(*field);
		return true;
	}
	error = std::string(path) + "/" + key + " must be numeric";
	return false;
}

bool RequireStringField(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	std::string& value,
	std::string& error)
{
	const std::optional<std::string> parsed = ReadOptionalStringField(object, key);
	if (parsed && !parsed->empty())
	{
		value = *parsed;
		return true;
	}
	error = std::string(path) + "/" + key + " must be a non-empty string";
	return false;
}

bool RequireFloat3Field(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	std::array<float, 3>& value,
	std::string& error)
{
	const std::optional<std::array<float, 3>> parsed = ReadOptionalFloat3Field(object, key);
	if (parsed)
	{
		value = *parsed;
		return true;
	}
	error = std::string(path) + "/" + key + " must contain exactly three numbers";
	return false;
}

bool RequireDouble3Field(
	const VansSerializedValue& object,
	const char* key,
	const char* path,
	std::array<double, 3>& value,
	std::string& error)
{
	const std::optional<std::array<double, 3>> parsed = ReadOptionalDouble3Field(object, key);
	if (parsed)
	{
		value = *parsed;
		return true;
	}
	error = std::string(path) + "/" + key + " must contain exactly three numbers";
	return false;
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

bool DecodeVolumetricClouds(
	const VansSerializedValue& cloud,
	VansSceneVolumetricCloudSettingsConfig& config,
	std::string& error)
{
	const char* path = "/settings/environment/volumetricClouds";
#define VANS_REQUIRE_CLOUD_FLOAT(field) \
	if (!RequireFloatField(cloud, #field, path, config.field, error)) return false
	VANS_REQUIRE_CLOUD_FLOAT(cloudMinHeight);
	VANS_REQUIRE_CLOUD_FLOAT(cloudMaxHeight);
	VANS_REQUIRE_CLOUD_FLOAT(density);
	VANS_REQUIRE_CLOUD_FLOAT(coverage);
	VANS_REQUIRE_CLOUD_FLOAT(sunBrightness);
	VANS_REQUIRE_CLOUD_FLOAT(mainTileMeters);
	VANS_REQUIRE_CLOUD_FLOAT(detailTileMeters);
	VANS_REQUIRE_CLOUD_FLOAT(mainHeightScale);
	VANS_REQUIRE_CLOUD_FLOAT(detailHeightScale);
	VANS_REQUIRE_CLOUD_FLOAT(thresholdLowCoverage);
	VANS_REQUIRE_CLOUD_FLOAT(thresholdHighCoverage);
	VANS_REQUIRE_CLOUD_FLOAT(densityRemapLow);
	VANS_REQUIRE_CLOUD_FLOAT(densityRemapHigh);
	VANS_REQUIRE_CLOUD_FLOAT(mainErosionStrength);
	VANS_REQUIRE_CLOUD_FLOAT(detailErosionStrength);
	VANS_REQUIRE_CLOUD_FLOAT(edgeErosionStrength);
	VANS_REQUIRE_CLOUD_FLOAT(verticalShapePower);
	VANS_REQUIRE_CLOUD_FLOAT(detailErosionLow);
	VANS_REQUIRE_CLOUD_FLOAT(detailErosionHigh);
	VANS_REQUIRE_CLOUD_FLOAT(detailEdgeStrength);
	VANS_REQUIRE_CLOUD_FLOAT(sigmaTRef);
	VANS_REQUIRE_CLOUD_FLOAT(viewAbsorption);
	VANS_REQUIRE_CLOUD_FLOAT(lightAbsorption);
	VANS_REQUIRE_CLOUD_FLOAT(singleScatteringAlbedo);
	VANS_REQUIRE_CLOUD_FLOAT(forwardEccentricity);
	VANS_REQUIRE_CLOUD_FLOAT(backwardEccentricity);
	VANS_REQUIRE_CLOUD_FLOAT(msAttenuation);
	VANS_REQUIRE_CLOUD_FLOAT(msContribution);
	VANS_REQUIRE_CLOUD_FLOAT(msEccentricity);
	VANS_REQUIRE_CLOUD_FLOAT(scatteringTintR);
	VANS_REQUIRE_CLOUD_FLOAT(scatteringTintG);
	VANS_REQUIRE_CLOUD_FLOAT(scatteringTintB);
	VANS_REQUIRE_CLOUD_FLOAT(scatterSourceODScale);
	VANS_REQUIRE_CLOUD_FLOAT(scatterSourceCurvePow);
	VANS_REQUIRE_CLOUD_FLOAT(aoUpwardScale);
	VANS_REQUIRE_CLOUD_FLOAT(ambientBottomStrength);
	VANS_REQUIRE_CLOUD_FLOAT(ambientTopStrength);
	VANS_REQUIRE_CLOUD_FLOAT(ambientDuskWarmth);
	VANS_REQUIRE_CLOUD_FLOAT(boundaryConfidence);
	VANS_REQUIRE_CLOUD_FLOAT(boundaryWrap);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdIntensity);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdDepthPow);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdDepthBias);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdMSBuildScale);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdCompress);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdMaxDistance);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdConeRatio);
	VANS_REQUIRE_CLOUD_FLOAT(phiFwdMinStep);
	VANS_REQUIRE_CLOUD_FLOAT(lightStepCount);
	VANS_REQUIRE_CLOUD_FLOAT(boundaryGradientStep);
	VANS_REQUIRE_CLOUD_FLOAT(boundaryGradientStrength);
	VANS_REQUIRE_CLOUD_FLOAT(shadingDebugMode);
#undef VANS_REQUIRE_CLOUD_FLOAT
	const VansSerializedValue* shadow = nullptr;
	if (!RequireObjectField(cloud, "shadow", path, shadow, error) ||
		!RequireBoolField(*shadow, "enabled",
			"/settings/environment/volumetricClouds/shadow",
			config.shadow.enabled, error) ||
		!RequireFloatField(*shadow, "atmosphereStrength",
			"/settings/environment/volumetricClouds/shadow",
			config.shadow.atmosphereStrength, error) ||
		!RequireFloatField(*shadow, "ambientOcclusionStrength",
			"/settings/environment/volumetricClouds/shadow",
			config.shadow.ambientOcclusionStrength, error))
	{
		return false;
	}
	const float* finiteValues[] = {
		&config.cloudMinHeight, &config.cloudMaxHeight, &config.density, &config.coverage,
		&config.sunBrightness, &config.mainTileMeters, &config.detailTileMeters,
		&config.mainHeightScale, &config.detailHeightScale, &config.thresholdLowCoverage,
		&config.thresholdHighCoverage, &config.densityRemapLow, &config.densityRemapHigh,
		&config.mainErosionStrength, &config.detailErosionStrength, &config.edgeErosionStrength,
		&config.verticalShapePower, &config.detailErosionLow, &config.detailErosionHigh,
		&config.detailEdgeStrength, &config.sigmaTRef,
		&config.viewAbsorption, &config.lightAbsorption, &config.singleScatteringAlbedo,
		&config.forwardEccentricity, &config.backwardEccentricity, &config.msAttenuation,
		&config.msContribution, &config.msEccentricity, &config.scatteringTintR,
		&config.scatteringTintG, &config.scatteringTintB, &config.scatterSourceODScale,
		&config.scatterSourceCurvePow, &config.aoUpwardScale, &config.ambientBottomStrength,
		&config.ambientTopStrength, &config.ambientDuskWarmth, &config.boundaryConfidence,
		&config.boundaryWrap, &config.phiFwdIntensity, &config.phiFwdDepthPow,
		&config.phiFwdDepthBias, &config.phiFwdMSBuildScale, &config.phiFwdCompress,
		&config.phiFwdMaxDistance, &config.phiFwdConeRatio, &config.phiFwdMinStep,
		&config.lightStepCount, &config.boundaryGradientStep, &config.boundaryGradientStrength,
		&config.shadingDebugMode, &config.shadow.atmosphereStrength,
		&config.shadow.ambientOcclusionStrength
	};
	for (const float* value : finiteValues)
	{
		if (!std::isfinite(*value))
		{
			error = std::string(path) + " contains a non-finite number";
			return false;
		}
	}
	if (config.msEccentricity < 0.0f || config.msEccentricity > 1.0f)
	{
		error = std::string(path) + "/msEccentricity must be in [0, 1]";
		return false;
	}
	if (config.cloudMinHeight >= config.cloudMaxHeight ||
		config.density < 0.0f || config.coverage < 0.0f || config.coverage > 1.0f ||
		config.sunBrightness < 0.0f ||
		config.mainTileMeters <= 0.0f || config.detailTileMeters <= 0.0f ||
		config.mainHeightScale <= 0.0f || config.detailHeightScale <= 0.0f ||
		config.thresholdLowCoverage > config.thresholdHighCoverage ||
		config.densityRemapLow > config.densityRemapHigh ||
		config.detailErosionLow > config.detailErosionHigh ||
		config.sigmaTRef < 0.0f || config.viewAbsorption < 0.0f ||
		config.lightAbsorption < 0.0f || config.singleScatteringAlbedo < 0.0f ||
		config.singleScatteringAlbedo > 1.0f || std::abs(config.forwardEccentricity) >= 1.0f ||
		std::abs(config.backwardEccentricity) >= 1.0f ||
		config.scatteringTintR < 0.0f || config.scatteringTintG < 0.0f ||
		config.scatteringTintB < 0.0f || config.phiFwdMaxDistance <= 0.0f ||
		config.phiFwdMinStep <= 0.0f || config.lightStepCount < 1.0f ||
		config.boundaryGradientStep <= 0.0f ||
		config.shadow.atmosphereStrength < 0.0f ||
		config.shadow.atmosphereStrength > 1.0f ||
		config.shadow.ambientOcclusionStrength < 0.0f ||
		config.shadow.ambientOcclusionStrength > 1.0f)
	{
		error = std::string(path) + " contains values outside the physical cloud domain";
		return false;
	}
	return true;
}

bool DecodeEnvironment(
	const VansSerializedValue& sceneSettings,
	VansSceneEnvironmentSettingsConfig& config,
	std::string& error)
{
	const VansSerializedValue* environment = nullptr;
	const VansSerializedValue* planet = nullptr;
	const VansSerializedValue* atmosphere = nullptr;
	const VansSerializedValue* rayleigh = nullptr;
	const VansSerializedValue* mie = nullptr;
	const VansSerializedValue* ozone = nullptr;
	const VansSerializedValue* aerialPerspective = nullptr;
	const VansSerializedValue* celestialBodies = nullptr;
	const VansSerializedValue* heightFog = nullptr;
	const VansSerializedValue* clouds = nullptr;

	if (!RequireObjectField(sceneSettings, "environment", "/settings", environment, error) ||
		!RequireObjectField(*environment, "planet", "/settings/environment", planet, error) ||
		!RequireDouble3Field(*planet, "centerWorldMeters", "/settings/environment/planet",
			config.planet.centerWorldMeters, error) ||
		!RequireDoubleField(*planet, "bottomRadiusMeters", "/settings/environment/planet",
			config.planet.bottomRadiusMeters, error) ||
		!RequireDoubleField(*planet, "atmosphereHeightMeters", "/settings/environment/planet",
			config.planet.atmosphereHeightMeters, error))
	{
		return false;
	}

	if (!std::isfinite(config.planet.bottomRadiusMeters) ||
		config.planet.bottomRadiusMeters <= 0.0 ||
		!std::isfinite(config.planet.atmosphereHeightMeters) ||
		config.planet.atmosphereHeightMeters <= 0.0 ||
		!std::all_of(config.planet.centerWorldMeters.begin(),
			config.planet.centerWorldMeters.end(),
			[](double value) { return std::isfinite(value); }))
	{
		error = "/settings/environment/planet contains invalid radii or center";
		return false;
	}

	auto& physical = config.physicalAtmosphere;
	const char* physicalPath = "/settings/environment/physicalAtmosphere";
	if (!RequireObjectField(*environment, "physicalAtmosphere",
			"/settings/environment", atmosphere, error) ||
		!RequireBoolField(*atmosphere, "enabled", physicalPath, physical.enabled, error) ||
		!RequireFloat3Field(*atmosphere, "groundAlbedo", physicalPath,
			physical.groundAlbedo, error) ||
		!RequireObjectField(*atmosphere, "rayleigh", physicalPath, rayleigh, error) ||
		!RequireFloat3Field(*rayleigh, "scatteringPerMeterAtGround",
			"/settings/environment/physicalAtmosphere/rayleigh",
			physical.rayleigh.scatteringPerMeterAtGround, error) ||
		!RequireFloatField(*rayleigh, "densityScaleHeightMeters",
			"/settings/environment/physicalAtmosphere/rayleigh",
			physical.rayleigh.densityScaleHeightMeters, error) ||
		!RequireObjectField(*atmosphere, "mie", physicalPath, mie, error) ||
		!RequireFloat3Field(*mie, "scatteringPerMeterAtGround",
			"/settings/environment/physicalAtmosphere/mie",
			physical.mie.scatteringPerMeterAtGround, error) ||
		!RequireFloat3Field(*mie, "absorptionPerMeterAtGround",
			"/settings/environment/physicalAtmosphere/mie",
			physical.mie.absorptionPerMeterAtGround, error) ||
		!RequireFloatField(*mie, "densityScaleHeightMeters",
			"/settings/environment/physicalAtmosphere/mie",
			physical.mie.densityScaleHeightMeters, error) ||
		!RequireFloatField(*mie, "anisotropy",
			"/settings/environment/physicalAtmosphere/mie",
			physical.mie.anisotropy, error) ||
		!RequireObjectField(*atmosphere, "ozone", physicalPath, ozone, error) ||
		!RequireFloat3Field(*ozone, "absorptionPerMeter",
			"/settings/environment/physicalAtmosphere/ozone",
			physical.ozone.absorptionPerMeter, error) ||
		!RequireFloatField(*ozone, "centerAltitudeMeters",
			"/settings/environment/physicalAtmosphere/ozone",
			physical.ozone.centerAltitudeMeters, error) ||
		!RequireFloatField(*ozone, "halfWidthMeters",
			"/settings/environment/physicalAtmosphere/ozone",
			physical.ozone.halfWidthMeters, error) ||
		!RequireObjectField(*atmosphere, "aerialPerspective", physicalPath,
			aerialPerspective, error) ||
		!RequireFloatField(*aerialPerspective, "distanceScale",
			"/settings/environment/physicalAtmosphere/aerialPerspective",
			physical.aerialPerspective.distanceScale, error) ||
		!RequireFloatField(*atmosphere, "mainLightVolumetricScatteringScale",
			physicalPath,
			physical.mainLightVolumetricScatteringScale, error) ||
		!RequireArrayField(*atmosphere, "celestialBodies", physicalPath,
			celestialBodies, error))
	{
		return false;
	}

	auto allFiniteInRange = [](const std::array<float, 3>& values,
		float minimum, float maximum)
	{
		return std::all_of(values.begin(), values.end(), [&](float value)
		{
			return std::isfinite(value) && value >= minimum && value <= maximum;
		});
	};
	if (!allFiniteInRange(physical.groundAlbedo, 0.0f, 1.0f) ||
		!allFiniteInRange(physical.rayleigh.scatteringPerMeterAtGround, 0.0f, 1.0f) ||
		!allFiniteInRange(physical.mie.scatteringPerMeterAtGround, 0.0f, 1.0f) ||
		!allFiniteInRange(physical.mie.absorptionPerMeterAtGround, 0.0f, 1.0f) ||
		!allFiniteInRange(physical.ozone.absorptionPerMeter, 0.0f, 1.0f) ||
		!std::isfinite(physical.rayleigh.densityScaleHeightMeters) ||
		physical.rayleigh.densityScaleHeightMeters <= 0.0f ||
		!std::isfinite(physical.mie.densityScaleHeightMeters) ||
		physical.mie.densityScaleHeightMeters <= 0.0f ||
		!std::isfinite(physical.mie.anisotropy) ||
		std::abs(physical.mie.anisotropy) >= 0.99f ||
		!std::isfinite(physical.ozone.centerAltitudeMeters) ||
		physical.ozone.centerAltitudeMeters < 0.0f ||
		!std::isfinite(physical.ozone.halfWidthMeters) ||
		physical.ozone.halfWidthMeters <= 0.0f ||
		!std::isfinite(physical.aerialPerspective.distanceScale) ||
		physical.aerialPerspective.distanceScale <= 0.0f ||
		!std::isfinite(physical.mainLightVolumetricScatteringScale) ||
		physical.mainLightVolumetricScatteringScale < 0.0f)
	{
		error = std::string(physicalPath) + " contains invalid physical coefficients";
		return false;
	}

	if (celestialBodies->arrayItems.size() > 2)
	{
		error = std::string(physicalPath) + "/celestialBodies supports at most two bodies";
		return false;
	}
	physical.celestialBodies.clear();
	physical.celestialBodies.reserve(celestialBodies->arrayItems.size());
	for (size_t index = 0; index < celestialBodies->arrayItems.size(); ++index)
	{
		const VansSerializedValue& bodyNode = celestialBodies->arrayItems[index];
		const std::string bodyPath = std::string(physicalPath) +
			"/celestialBodies/" + std::to_string(index);
		if (bodyNode.kind != VansSerializedValue::Kind::Object)
		{
			error = bodyPath + " must be an object";
			return false;
		}
		VansSceneCelestialBodySettingsConfig body;
		const VansSerializedValue* disk = nullptr;
		if (!RequireStringField(bodyNode, "name", bodyPath.c_str(), body.name, error) ||
			!RequireStringField(bodyNode, "lightEntityId", bodyPath.c_str(),
				body.lightEntityId, error) ||
			!RequireObjectField(bodyNode, "disk", bodyPath.c_str(), disk, error))
		{
			return false;
		}
		const std::string diskPath = bodyPath + "/disk";
		if (!RequireBoolField(*disk, "enabled", diskPath.c_str(), body.disk.enabled, error) ||
			!RequireFloatField(*disk, "angularRadiusRadians", diskPath.c_str(),
				body.disk.angularRadiusRadians, error) ||
			!RequireFloatField(*disk, "featherRadians", diskPath.c_str(),
				body.disk.featherRadians, error) ||
			!RequireFloatField(*disk, "radianceScale", diskPath.c_str(),
				body.disk.radianceScale, error) ||
			!RequireFloatField(*disk, "occlusionStrength", diskPath.c_str(),
				body.disk.occlusionStrength, error))
		{
			return false;
		}
		if (!std::isfinite(body.disk.angularRadiusRadians) ||
			body.disk.angularRadiusRadians <= 0.0f ||
			body.disk.angularRadiusRadians >= 0.25f ||
			!std::isfinite(body.disk.featherRadians) || body.disk.featherRadians < 0.0f ||
			!std::isfinite(body.disk.radianceScale) || body.disk.radianceScale < 0.0f ||
			!std::isfinite(body.disk.occlusionStrength) || body.disk.occlusionStrength < 0.0f)
		{
			error = diskPath + " contains invalid values";
			return false;
		}
		for (const auto& existing : physical.celestialBodies)
		{
			if (existing.lightEntityId == body.lightEntityId)
			{
				error = bodyPath + "/lightEntityId must be unique";
				return false;
			}
		}
		physical.celestialBodies.push_back(std::move(body));
	}
	if (physical.enabled && physical.celestialBodies.empty())
	{
		error = std::string(physicalPath) + " requires a celestial body when enabled";
		return false;
	}

	auto& fog = config.heightFog;
	const char* fogPath = "/settings/environment/heightFog";
	if (!RequireObjectField(*environment, "heightFog", "/settings/environment",
			heightFog, error) ||
		!RequireBoolField(*heightFog, "enabled", fogPath, fog.enabled, error) ||
		!RequireFloatField(*heightFog, "groundHeightWorldMeters", fogPath,
			fog.groundHeightWorldMeters, error) ||
		!RequireFloatField(*heightFog, "visibilityAtGroundMeters", fogPath,
			fog.visibilityAtGroundMeters, error) ||
		!RequireFloatField(*heightFog, "densityFalloffHeightMeters", fogPath,
			fog.densityFalloffHeightMeters, error) ||
		!RequireFloatField(*heightFog, "startDistanceMeters", fogPath,
			fog.startDistanceMeters, error) ||
		!RequireFloatField(*heightFog, "nearFadeDistanceMeters", fogPath,
			fog.nearFadeDistanceMeters, error) ||
		!RequireFloatField(*heightFog, "maximumDistanceMeters", fogPath,
			fog.maximumDistanceMeters, error) ||
		!RequireFloatField(*heightFog, "farFadeDistanceMeters", fogPath,
			fog.farFadeDistanceMeters, error) ||
		!RequireFloat3Field(*heightFog, "singleScatteringAlbedo", fogPath,
			fog.singleScatteringAlbedo, error) ||
		!RequireFloatField(*heightFog, "anisotropy", fogPath,
			fog.anisotropy, error) ||
		!RequireFloat3Field(*heightFog, "emissivePerMeter", fogPath,
			fog.emissivePerMeter, error) ||
		!RequireFloatField(*heightFog, "skyLightingScale", fogPath,
			fog.skyLightingScale, error) ||
		!RequireFloatField(*heightFog, "mainLightVolumetricScale", fogPath,
			fog.mainLightVolumetricScale, error) ||
		!RequireBoolField(*heightFog, "receiveCloudShadows", fogPath,
			fog.receiveCloudShadows, error))
	{
		return false;
	}

	if (!std::isfinite(fog.groundHeightWorldMeters) ||
		!std::isfinite(fog.visibilityAtGroundMeters) || fog.visibilityAtGroundMeters <= 0.0f ||
		!std::isfinite(fog.densityFalloffHeightMeters) || fog.densityFalloffHeightMeters <= 0.0f ||
		!std::isfinite(fog.startDistanceMeters) || fog.startDistanceMeters < 0.0f ||
		!std::isfinite(fog.nearFadeDistanceMeters) || fog.nearFadeDistanceMeters < 0.0f ||
		!std::isfinite(fog.maximumDistanceMeters) ||
		fog.maximumDistanceMeters <= fog.startDistanceMeters ||
		!std::isfinite(fog.farFadeDistanceMeters) || fog.farFadeDistanceMeters < 0.0f ||
		fog.nearFadeDistanceMeters + fog.farFadeDistanceMeters >
			fog.maximumDistanceMeters - fog.startDistanceMeters ||
		!allFiniteInRange(fog.singleScatteringAlbedo, 0.0f, 1.0f) ||
		!std::isfinite(fog.anisotropy) || std::abs(fog.anisotropy) >= 0.99f ||
		!allFiniteInRange(fog.emissivePerMeter, 0.0f, 100000.0f) ||
		!std::isfinite(fog.skyLightingScale) || fog.skyLightingScale < 0.0f ||
		!std::isfinite(fog.mainLightVolumetricScale) ||
		fog.mainLightVolumetricScale < 0.0f)
	{
		error = std::string(fogPath) + " contains invalid near-ground fog values";
		return false;
	}

	if (!RequireObjectField(*environment, "volumetricClouds",
			"/settings/environment", clouds, error) ||
		!RequireBoolField(*clouds, "enabled",
			"/settings/environment/volumetricClouds",
			config.volumetricClouds.enabled, error) ||
		!DecodeVolumetricClouds(*clouds, config.volumetricClouds, error))
	{
		return false;
	}
	return true;
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
		config.bloomClamp = ReadOptionalFloatField(*bloom, "clamp");
		config.bloomTintR = ReadOptionalFloatField(*bloom, "tintR");
		config.bloomTintG = ReadOptionalFloatField(*bloom, "tintG");
		config.bloomTintB = ReadOptionalFloatField(*bloom, "tintB");
		config.bloomShapeMode = ReadOptionalIntField(*bloom, "shapeMode");
		config.bloomShapeIntensity = ReadOptionalFloatField(*bloom, "shapeIntensity");
		config.bloomShapeBlend = ReadOptionalFloatField(*bloom, "shapeBlend");
		config.bloomShapeAngleDeg = ReadOptionalFloatField(*bloom, "shapeAngleDeg");
		config.bloomAnamorphicStretch = ReadOptionalFloatField(*bloom, "anamorphicStretch");
		config.bloomStreakCount = ReadOptionalIntField(*bloom, "streakCount");
		config.bloomStreakLength = ReadOptionalFloatField(*bloom, "streakLength");
		config.bloomStreakAttenuation = ReadOptionalFloatField(*bloom, "streakAttenuation");
	}
	if (const VansSerializedValue* dof = ReadObjectField(*postProcess, "dof"))
	{
		config.enableDOF = ReadOptionalBoolField(*dof, "enable");
		config.focusDistance = ReadOptionalFloatField(*dof, "focusDistance");
		config.focalLengthMm = ReadOptionalFloatField(*dof, "focalLengthMm");
		config.fStop = ReadOptionalFloatField(*dof, "fStop");
		if (!config.fStop.has_value())
			config.fStop = ReadOptionalFloatField(*dof, "aperture");
		config.sensorHeightMm = ReadOptionalFloatField(*dof, "sensorHeightMm");
		config.maxCoC = ReadOptionalFloatField(*dof, "maxCoC");
		config.dofBlurTransmissionBackground = ReadOptionalBoolField(*dof, "blurTransmissionBackground");
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
	config.enableForwardOpaquePreAtmosphere = ReadOptionalBoolField(*hiz, "enableForwardOpaquePreAtmosphere");
	config.depthBiasMeters = ReadOptionalFloatField(*hiz, "depthBiasMeters");
	config.cameraMotionDisableDistance = ReadOptionalFloatField(*hiz, "cameraMotionDisableDistance");
	config.cameraMotionDisableAngleRadians = ReadOptionalFloatField(*hiz, "cameraMotionDisableAngleRadians");
	config.forceVisibleFramesAfterChange = ReadOptionalUIntField(*hiz, "forceVisibleFramesAfterChange");
	config.refreshCulledEveryNFrames = ReadOptionalUIntField(*hiz, "refreshCulledEveryNFrames");
	config.maxScreenCoverageForCull = ReadOptionalFloatField(*hiz, "maxScreenCoverageForCull");
	return config;
}
}

bool VansSceneRenderSettingsConfigReader::Read(
	const VansSerializedValue& sceneSettings,
	VansSceneRenderSettingsConfig& config,
	std::string& error)
{
	config = VansSceneRenderSettingsConfig{};
	error.clear();
	if (sceneSettings.kind != VansSerializedValue::Kind::Object)
	{
		error = "/settings must be an object";
		return false;
	}

	if (!DecodeEnvironment(sceneSettings, config.environment, error))
	{
		return false;
	}
	config.postProcess = DecodePostProcess(sceneSettings);
	config.globalIllumination = DecodeGISettings(sceneSettings);
	config.mainCameraHiZCulling = DecodeMainCameraHiZCulling(sceneSettings);
	return true;
}
}
