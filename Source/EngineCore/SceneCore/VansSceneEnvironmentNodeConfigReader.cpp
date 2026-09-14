#include "VansSceneEnvironmentNodeConfigReader.h"

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace Vans
{
namespace
{
const VansSerializedValue* ReadObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

std::optional<std::string> ReadOptionalStringField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::String
		? std::optional<std::string>(field->stringValue)
		: std::nullopt;
}

std::optional<float> ReadOptionalFloatField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field)
		return std::nullopt;
	if (field->kind == VansSerializedValue::Kind::Float || field->kind == VansSerializedValue::Kind::Int)
		return static_cast<float>(ReadSerializedNumber(*field));
	return std::nullopt;
}

std::optional<int> ReadOptionalIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Int)
		return std::nullopt;
	return static_cast<int>(field->intValue);
}

std::optional<std::int32_t> ReadOptionalInt32Field(const VansSerializedValue& object, const char* key)
{
	std::optional<int> value = ReadOptionalIntField(object, key);
	return value ? std::optional<std::int32_t>(static_cast<std::int32_t>(*value)) : std::nullopt;
}

std::optional<unsigned int> ReadOptionalUIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Int || field->intValue < 0)
		return std::nullopt;
	return static_cast<unsigned int>(field->intValue);
}

std::optional<std::uint32_t> ReadOptionalUInt32Field(const VansSerializedValue& object, const char* key)
{
	std::optional<unsigned int> value = ReadOptionalUIntField(object, key);
	return value ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(*value)) : std::nullopt;
}

std::optional<bool> ReadOptionalBoolField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Bool
		? std::optional<bool>(field->boolValue)
		: std::nullopt;
}

template <typename T>
void AssignIfPresent(std::optional<T>& target, std::optional<T> value)
{
	if (value)
		target = *value;
}

std::optional<VansSceneFloat2> ReadOptionalFloat2Field(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* value = FindObjectField(object, key);
	if (!value)
		return std::nullopt;

	if (value->kind == VansSerializedValue::Kind::Array && value->arrayItems.size() >= 2)
	{
		return VansSceneFloat2{
			static_cast<float>(ReadSerializedNumber(value->arrayItems[0])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[1]))
		};
	}
	if (value->kind == VansSerializedValue::Kind::Object)
	{
		return VansSceneFloat2{
			ReadOptionalFloatField(*value, "x").value_or(0.0f),
			ReadOptionalFloatField(*value, "y").value_or(0.0f)
		};
	}
	return std::nullopt;
}

std::optional<VansSceneFloat3> ReadOptionalFloat3Field(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* value = FindObjectField(object, key);
	if (!value)
		return std::nullopt;

	if (value->kind == VansSerializedValue::Kind::Array && value->arrayItems.size() >= 3)
	{
		return VansSceneFloat3{
			static_cast<float>(ReadSerializedNumber(value->arrayItems[0])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[1])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[2]))
		};
	}
	if (value->kind == VansSerializedValue::Kind::Object)
	{
		return VansSceneFloat3{
			ReadOptionalFloatField(*value, "x").value_or(0.0f),
			ReadOptionalFloatField(*value, "y").value_or(0.0f),
			ReadOptionalFloatField(*value, "z").value_or(0.0f)
		};
	}
	return std::nullopt;
}

std::optional<VansSceneFloat3> ReadOptionalColor3Field(
	const VansSerializedValue& object,
	const char* key,
	VansSceneFloat3 objectFallback = { 0.0f, 0.0f, 0.0f })
{
	const VansSerializedValue* value = FindObjectField(object, key);
	if (!value)
		return std::nullopt;

	if (value->kind == VansSerializedValue::Kind::Array && value->arrayItems.size() >= 3)
	{
		return VansSceneFloat3{
			static_cast<float>(ReadSerializedNumber(value->arrayItems[0])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[1])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[2]))
		};
	}
	if (value->kind == VansSerializedValue::Kind::Object)
	{
		return VansSceneFloat3{
			ReadOptionalFloatField(*value, "r").value_or(objectFallback[0]),
			ReadOptionalFloatField(*value, "g").value_or(objectFallback[1]),
			ReadOptionalFloatField(*value, "b").value_or(objectFallback[2])
		};
	}
	return std::nullopt;
}

std::optional<VansSceneFloat4> ReadOptionalColor4Field(
	const VansSerializedValue& object,
	const char* key,
	VansSceneFloat3 objectFallback = { 0.0f, 0.0f, 0.0f })
{
	std::optional<VansSceneFloat3> color = ReadOptionalColor3Field(object, key, objectFallback);
	if (!color)
		return std::nullopt;
	return VansSceneFloat4{ (*color)[0], (*color)[1], (*color)[2], 1.0f };
}

std::optional<VansSceneWaterWaveMode> ReadOptionalWaterWaveMode(const VansSerializedValue& object, const char* key)
{
	std::optional<std::string> mode = ReadOptionalStringField(object, key);
	if (!mode)
		return std::nullopt;

	std::transform(mode->begin(), mode->end(), mode->begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	if (*mode == "fft") return VansSceneWaterWaveMode::FFT;
	if (*mode == "gerstner") return VansSceneWaterWaveMode::Gerstner;
	if (*mode == "waveparticle" || *mode == "wave_particle" || *mode == "particle")
		return VansSceneWaterWaveMode::WaveParticle;
	if (*mode == "hybrid") return VansSceneWaterWaveMode::FFT;
	return std::nullopt;
}

std::vector<std::string> DecodeStringArrayField(const VansSerializedValue& object, const char* key)
{
	std::vector<std::string> values;
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Array)
		return values;

	values.reserve(found->arrayItems.size());
	for (const VansSerializedValue& value : found->arrayItems)
		if (value.kind == VansSerializedValue::Kind::String)
			values.push_back(value.stringValue);
	return values;
}

VansSceneTerrainPhysicsMaterialConfig DecodeTerrainPhysicsMaterial(const VansSerializedValue& materialNode)
{
	VansSceneTerrainPhysicsMaterialConfig config;
	config.staticFriction = ReadOptionalFloatField(materialNode, "staticFriction");
	config.dynamicFriction = ReadOptionalFloatField(materialNode, "dynamicFriction");
	config.restitution = ReadOptionalFloatField(materialNode, "restitution");
	return config;
}

VansSceneTerrainCollisionConfig DecodeTerrainCollision(const VansSerializedValue& collisionNode)
{
	VansSceneTerrainCollisionConfig config;
	config.enabled = ReadOptionalBoolField(collisionNode, "enabled");
	config.layer = ReadOptionalStringField(collisionNode, "layer");
	if (const VansSerializedValue* material = ReadObjectField(collisionNode, "material"))
		config.material = DecodeTerrainPhysicsMaterial(*material);
	return config;
}

VansSceneWaterMediumConfig DecodeWaterMedium(const VansSerializedValue& mediumNode)
{
	VansSceneWaterMediumConfig config;
	config.absorptionCoeff = ReadOptionalColor3Field(mediumNode, "absorption", { 0.05f, 0.08f, 0.20f });
	config.scatteringCoeff = ReadOptionalColor3Field(mediumNode, "scattering", { 0.03f, 0.05f, 0.08f });
	config.ior = ReadOptionalFloatField(mediumNode, "ior");
	config.anisotropy = ReadOptionalFloatField(mediumNode, "anisotropy");
	config.waterRoughness = ReadOptionalFloatField(mediumNode, "roughness");
	return config;
}

VansSceneWaterOpticsConfig DecodeWaterOptics(const VansSerializedValue& opticsNode)
{
	VansSceneWaterOpticsConfig config;
	config.maxCrossDistance = ReadOptionalFloatField(opticsNode, "maxCrossDistance");
	config.maxRefractionCrossDistance = ReadOptionalFloatField(opticsNode, "maxRefractionCrossDistance");
	config.multiScatterScale = ReadOptionalFloatField(opticsNode, "multiScatterScale");
	config.waterDispersionStrength = ReadOptionalFloatField(opticsNode, "waterDispersionStrength");
	config.sssPathScale = ReadOptionalFloatField(opticsNode, "sssPathScale");
	config.sssNonlinearStrength = ReadOptionalFloatField(opticsNode, "sssNonlinearStrength");
	config.sssScatterBoost = ReadOptionalFloatField(opticsNode, "sssScatterBoost");
	config.backlitPathScale = ReadOptionalFloatField(opticsNode, "backlitPathScale");
	config.backlitPhaseG = ReadOptionalFloatField(opticsNode, "backlitPhaseG");
	return config;
}

VansSceneWaterVolumeConfig DecodeWaterVolume(const VansSerializedValue& volumeNode)
{
	VansSceneWaterVolumeConfig config;
	config.resolutionScale = ReadOptionalFloatField(volumeNode, "resolutionScale");
	config.sampleCount = ReadOptionalIntField(volumeNode, "sampleCount");
	config.spatialFilterIterations = ReadOptionalIntField(volumeNode, "spatialFilterIterations");
	config.spatialDepthSensitivity = ReadOptionalFloatField(volumeNode, "spatialDepthSensitivity");
	return config;
}

void DecodeWaterSpectrumFields(const VansSerializedValue& spectrumNode, VansSceneWaterSpectrumConfig& config)
{
	if (std::optional<VansSceneWaterWaveMode> mode = ReadOptionalWaterWaveMode(spectrumNode, "mode"))
		config.mode = *mode;
	AssignIfPresent(config.baseCoverage, ReadOptionalFloatField(spectrumNode, "baseCoverage"));
	AssignIfPresent(config.cascadeScale, ReadOptionalFloatField(spectrumNode, "cascadeScale"));
	AssignIfPresent(config.cascadeCount, ReadOptionalIntField(spectrumNode, "cascadeCount"));
	AssignIfPresent(config.windSpeed, ReadOptionalFloatField(spectrumNode, "windSpeed"));
	AssignIfPresent(config.swellAmplitude, ReadOptionalFloatField(spectrumNode, "swellAmplitude"));
	AssignIfPresent(config.choppiness, ReadOptionalFloatField(spectrumNode, "choppiness"));
	AssignIfPresent(config.gerstnerWaveCount, ReadOptionalIntField(spectrumNode, "gerstnerWaveCount"));
	AssignIfPresent(config.windDirection, ReadOptionalFloat2Field(spectrumNode, "windDirection"));
	AssignIfPresent(config.spectrumAmplitude, ReadOptionalFloatField(spectrumNode, "spectrumAmplitude"));
	AssignIfPresent(config.minWavelength, ReadOptionalFloatField(spectrumNode, "minWavelength"));
	AssignIfPresent(config.smallWaveDamping, ReadOptionalFloatField(spectrumNode, "smallWaveDamping"));
	AssignIfPresent(config.windDependency, ReadOptionalFloatField(spectrumNode, "windDependency"));
	AssignIfPresent(config.depth, ReadOptionalFloatField(spectrumNode, "depth"));
	AssignIfPresent(config.repeatPeriod, ReadOptionalFloatField(spectrumNode, "repeatPeriod"));
	AssignIfPresent(config.randomSeed, ReadOptionalUIntField(spectrumNode, "randomSeed"));
}

VansSceneWaterWaveParticleConfig DecodeWaterWaveParticles(
	const VansSerializedValue& waveParticleNode)
{
	VansSceneWaterWaveParticleConfig config;
	config.particlesPerCascade = ReadOptionalIntField(waveParticleNode, "particlesPerCascade");
	config.rmsAmplitude = ReadOptionalFloatField(waveParticleNode, "rmsAmplitude");
	config.packetWidth = ReadOptionalFloatField(waveParticleNode, "packetWidth");
	config.dispersionScale = ReadOptionalFloatField(waveParticleNode, "dispersionScale");
	config.directionSpread = ReadOptionalFloatField(waveParticleNode, "directionSpread");
	config.cascadeAmplitudeFalloff = ReadOptionalFloatField(waveParticleNode, "cascadeAmplitudeFalloff");
	config.foamThreshold = ReadOptionalFloatField(waveParticleNode, "foamThreshold");
	config.foamSoftness = ReadOptionalFloatField(waveParticleNode, "foamSoftness");
	config.randomSeed = ReadOptionalUIntField(waveParticleNode, "randomSeed");
	return config;
}

VansSceneWaterFlowMapConfig DecodeWaterFlowMap(const VansSerializedValue& flowMapNode)
{
	VansSceneWaterFlowMapConfig config;
	config.enabled = ReadOptionalBoolField(flowMapNode, "enabled");
	config.strength = ReadOptionalFloatField(flowMapNode, "strength");
	config.speed = ReadOptionalFloatField(flowMapNode, "speed");
	config.phaseLength = ReadOptionalFloatField(flowMapNode, "phaseLength");
	config.noiseAmount = ReadOptionalFloatField(flowMapNode, "noiseAmount");
	config.worldOrigin = ReadOptionalFloat2Field(flowMapNode, "worldOrigin");
	config.worldSize = ReadOptionalFloat2Field(flowMapNode, "worldSize");
	config.fallbackDirection = ReadOptionalFloat2Field(flowMapNode, "fallbackDirection");
	return config;
}


VansSceneWaterRefractionConfig DecodeWaterRefraction(const VansSerializedValue& refractionNode)
{
	VansSceneWaterRefractionConfig config;
	config.enabled = ReadOptionalBoolField(refractionNode, "enabled");
	config.distortionStrength = ReadOptionalFloatField(refractionNode, "distortionStrength");
	return config;
}

VansSceneWaterDetailNormalConfig DecodeWaterDetailNormal(
	const VansSerializedValue& detailNode,
	bool& valid)
{
	VansSceneWaterDetailNormalConfig config;
	config.enabled = ReadOptionalBoolField(detailNode, "enabled");
	config.flipGreen = ReadOptionalBoolField(detailNode, "flipGreen");
	config.globalStrength = ReadOptionalFloatField(detailNode, "globalStrength");
	config.maxSlope = ReadOptionalFloatField(detailNode, "maxSlope");
	config.mipBias = ReadOptionalFloatField(detailNode, "mipBias");
	config.anisotropy = ReadOptionalFloatField(detailNode, "anisotropy");

	if (std::optional<std::string> decodeMode = ReadOptionalStringField(detailNode, "decodeMode"))
	{
		if (*decodeMode == "rgReconstructZ")
			config.decodeMode = VansSceneWaterNormalDecodeMode::RGReconstructZ;
		else
		{
			valid = false;
			VANS_LOG_ERROR("[SceneLoader] water.detailNormal.decodeMode must be 'rgReconstructZ'.");
		}
	}

	if (const VansSerializedValue* layers = FindObjectField(detailNode, "layers"))
	{
		if (layers->kind != VansSerializedValue::Kind::Array || layers->arrayItems.size() > 4)
		{
			valid = false;
			VANS_LOG_ERROR("[SceneLoader] water.detailNormal.layers must be an array with at most 4 entries.");
			return config;
		}
		for (const VansSerializedValue& layerNode : layers->arrayItems)
		{
			if (layerNode.kind != VansSerializedValue::Kind::Object)
			{
				valid = false;
				VANS_LOG_ERROR("[SceneLoader] Every water.detailNormal.layers entry must be an object.");
				return config;
			}
			VansSceneWaterDetailNormalLayerConfig layer;
			layer.enabled = ReadOptionalBoolField(layerNode, "enabled");
			layer.tileSizeMeters = ReadOptionalFloatField(layerNode, "tileSizeMeters");
			layer.direction = ReadOptionalFloat2Field(layerNode, "direction");
			layer.speedMetersPerSecond = ReadOptionalFloatField(layerNode, "speedMetersPerSecond");
			layer.phase = ReadOptionalFloatField(layerNode, "phase");
			layer.strength = ReadOptionalFloatField(layerNode, "strength");
			layer.fadeStartMeters = ReadOptionalFloatField(layerNode, "fadeStartMeters");
			layer.fadeEndMeters = ReadOptionalFloatField(layerNode, "fadeEndMeters");
			config.layers.push_back(std::move(layer));
		}
	}
	return config;
}

VansSceneWaterEffectiveRoughnessConfig DecodeWaterEffectiveRoughness(
	const VansSerializedValue& roughnessNode,
	bool& valid)
{
	VansSceneWaterEffectiveRoughnessConfig config;
	if (std::optional<std::string> mode = ReadOptionalStringField(roughnessNode, "mode"))
	{
		if (*mode == "baseOnly")
			config.mode = VansSceneWaterEffectiveRoughnessMode::BaseOnly;
		else if (*mode == "distanceHeuristic")
			config.mode = VansSceneWaterEffectiveRoughnessMode::DistanceHeuristic;
		else
		{
			valid = false;
			VANS_LOG_ERROR("[SceneLoader] water.effectiveRoughness.mode is invalid.");
		}
	}
	config.distanceStartMeters = ReadOptionalFloatField(roughnessNode, "distanceStartMeters");
	config.distanceEndMeters = ReadOptionalFloatField(roughnessNode, "distanceEndMeters");
	config.distanceStrength = ReadOptionalFloatField(roughnessNode, "distanceStrength");
	return config;
}

VansSceneWaterColorMipConfig DecodeWaterColorMip(const VansSerializedValue& colorMipNode)
{
	VansSceneWaterColorMipConfig config;
	config.refractionScatterScale = ReadOptionalFloatField(colorMipNode, "refractionScatterScale");
	config.refractionRoughnessScale = ReadOptionalFloatField(colorMipNode, "refractionRoughnessScale");
	config.forwardScatterMipScale = ReadOptionalFloatField(colorMipNode, "forwardScatterMipScale");
	config.backgroundScatterScale = ReadOptionalFloatField(colorMipNode, "backgroundScatterScale");
	config.lodBias = ReadOptionalFloatField(colorMipNode, "lodBias");
	return config;
}

VansSceneWaterShadowConfig DecodeWaterShadow(const VansSerializedValue& shadowNode)
{
	VansSceneWaterShadowConfig config;
	config.enabled = ReadOptionalBoolField(shadowNode, "enabled");
	config.quality = ReadOptionalIntField(shadowNode, "quality");
	config.depthBias = ReadOptionalFloatField(shadowNode, "depthBias");
	config.normalBias = ReadOptionalFloatField(shadowNode, "normalBias");
	config.volumeStepStride = ReadOptionalIntField(shadowNode, "volumeStepStride");
	return config;
}

VansSceneWaterSSRConfig DecodeWaterSSR(const VansSerializedValue& ssrNode)
{
	VansSceneWaterSSRConfig config;
	config.enabled = ReadOptionalBoolField(ssrNode, "enabled");
	config.maxDistance = ReadOptionalFloatField(ssrNode, "maxDistance");
	config.maxRoughness = ReadOptionalFloatField(ssrNode, "maxRoughness");
	config.roughnessFadeStart = ReadOptionalFloatField(ssrNode, "roughnessFadeStart");
	config.colorMipConeScale = ReadOptionalFloatField(ssrNode, "colorMipConeScale");
	config.colorMipBias = ReadOptionalFloatField(ssrNode, "colorMipBias");
	config.edgeFadePixels = ReadOptionalFloatField(ssrNode, "edgeFadePixels");
	return config;
}

VansSceneWaterSSSConfig DecodeWaterSSS(const VansSerializedValue& sssNode)
{
	VansSceneWaterSSSConfig config;
	config.enabled = ReadOptionalBoolField(sssNode, "enabled");
	config.maxThickness = ReadOptionalFloatField(sssNode, "maxThickness");
	config.deepFallback = ReadOptionalFloatField(sssNode, "deepFallback");
	return config;
}

VansSceneWaterGeometryConfig DecodeWaterGeometry(const VansSerializedValue& geometryNode)
{
	VansSceneWaterGeometryConfig config;
	config.lodCount = ReadOptionalIntField(geometryNode, "lodCount");
	config.basePatchSize = ReadOptionalFloatField(geometryNode, "basePatchSize");
	config.meshDim = ReadOptionalIntField(geometryNode, "meshDim");
	config.morphStartRatio = ReadOptionalFloatField(geometryNode, "morphStartRatio");
	return config;
}

}

VansSceneTerrainNodeConfig VansSceneEnvironmentNodeConfigReader::ReadTerrain(
	const VansSerializedValue& terrainNode)
{
	VansSceneTerrainNodeConfig config;
	if (terrainNode.kind != VansSerializedValue::Kind::Object)
		return config;

	const VansSerializedValue* assetValue = FindObjectField(terrainNode, "asset");
	SerializedObjectReferenceValue reference;
	if (assetValue && TryReadSerializedObjectReference(*assetValue, reference) &&
		reference.domain == "ProjectAsset" && reference.assetType == "terrain")
	{
		config.assetGuid = reference.guid;
		config.valid = true;
	}
	config.name = ReadOptionalStringField(terrainNode, "name");
	if (const VansSerializedValue* collision = ReadObjectField(terrainNode, "collision"))
		config.collision = DecodeTerrainCollision(*collision);
	return config;
}

VansSceneWaterNodeConfig VansSceneEnvironmentNodeConfigReader::ReadWater(
	const VansSerializedValue& waterNode)
{
	VansSceneWaterNodeConfig config;
	if (waterNode.kind != VansSerializedValue::Kind::Object)
	{
		config.valid = false;
		return config;
	}

	config.level = ReadOptionalFloatField(waterNode, "level");
	config.specularIntensity = ReadOptionalFloatField(waterNode, "specularIntensity");
	config.name = ReadOptionalStringField(waterNode, "name");

	if (const VansSerializedValue* medium = ReadObjectField(waterNode, "medium"))
		config.medium = DecodeWaterMedium(*medium);
	if (const VansSerializedValue* spectrum = ReadObjectField(waterNode, "spectrum"))
		DecodeWaterSpectrumFields(*spectrum, config.spectrum);
	if (const VansSerializedValue* waveParticle = ReadObjectField(waterNode, "waveParticle"))
		config.waveParticle = DecodeWaterWaveParticles(*waveParticle);
	if (const VansSerializedValue* flowMap = ReadObjectField(waterNode, "flowMap"))
		config.flowMap = DecodeWaterFlowMap(*flowMap);
	if (const VansSerializedValue* refraction = ReadObjectField(waterNode, "refraction"))
		config.refraction = DecodeWaterRefraction(*refraction);
	if (const VansSerializedValue* detailNormal = ReadObjectField(waterNode, "detailNormal"))
		config.detailNormal = DecodeWaterDetailNormal(*detailNormal, config.valid);
	if (const VansSerializedValue* effectiveRoughness = ReadObjectField(waterNode, "effectiveRoughness"))
		config.effectiveRoughness = DecodeWaterEffectiveRoughness(*effectiveRoughness, config.valid);
	if (const VansSerializedValue* colorMip = ReadObjectField(waterNode, "colorMip"))
		config.colorMip = DecodeWaterColorMip(*colorMip);
	if (const VansSerializedValue* shadow = ReadObjectField(waterNode, "shadow"))
		config.shadow = DecodeWaterShadow(*shadow);
	if (const VansSerializedValue* ssr = ReadObjectField(waterNode, "ssr"))
		config.ssr = DecodeWaterSSR(*ssr);
	if (const VansSerializedValue* sss = ReadObjectField(waterNode, "sss"))
		config.sss = DecodeWaterSSS(*sss);
	if (const VansSerializedValue* optics = ReadObjectField(waterNode, "optics"))
		config.optics = DecodeWaterOptics(*optics);
	if (const VansSerializedValue* volume = ReadObjectField(waterNode, "volume"))
		config.volume = DecodeWaterVolume(*volume);
	if (const VansSerializedValue* geometry = ReadObjectField(waterNode, "geometry"))
		config.geometry = DecodeWaterGeometry(*geometry);
	return config;
}

}
