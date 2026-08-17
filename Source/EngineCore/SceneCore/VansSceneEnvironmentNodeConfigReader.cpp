#include "VansSceneEnvironmentNodeConfigReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
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

VansSceneTerrainNoiseDetailConfig DecodeTerrainNoiseDetail(const VansSerializedValue& noiseNode)
{
	VansSceneTerrainNoiseDetailConfig config;
	config.enabled = ReadOptionalBoolField(noiseNode, "enabled");
	config.strength = ReadOptionalFloatField(noiseNode, "strength");
	config.frequency = ReadOptionalFloatField(noiseNode, "frequency");
	config.lacunarity = ReadOptionalFloatField(noiseNode, "lacunarity");
	config.gain = ReadOptionalFloatField(noiseNode, "gain");
	config.octaves = ReadOptionalIntField(noiseNode, "octaves");
	config.warpStrength = ReadOptionalFloatField(noiseNode, "warpStrength");
	config.fadeStart = ReadOptionalFloatField(noiseNode, "fadeStart");
	return config;
}

VansSceneTerrainTessellationConfig DecodeTerrainTessellation(const VansSerializedValue& tessellationNode)
{
	VansSceneTerrainTessellationConfig config;
	config.enabled = ReadOptionalBoolField(tessellationNode, "enabled");
	config.distance = ReadOptionalFloatField(tessellationNode, "distance");
	config.maxLevel = ReadOptionalFloatField(tessellationNode, "maxLevel");
	config.power = ReadOptionalFloatField(tessellationNode, "power");
	config.lodBias = ReadOptionalFloatField(tessellationNode, "lodBias");
	config.displacementStrength = ReadOptionalFloatField(tessellationNode, "displacementStrength");
	if (const VansSerializedValue* noiseDetail = ReadObjectField(tessellationNode, "noiseDetail"))
		config.noiseDetail = DecodeTerrainNoiseDetail(*noiseDetail);
	return config;
}

VansSceneTerrainLayerConfig DecodeTerrainLayer(const VansSerializedValue& layerNode)
{
	VansSceneTerrainLayerConfig config;
	config.albedoTexture = ReadOptionalStringField(layerNode, "albedo_texture");
	config.albedoPath = ReadOptionalStringField(layerNode, "albedo");
	config.normalTexture = ReadOptionalStringField(layerNode, "normal_texture");
	config.normalPath = ReadOptionalStringField(layerNode, "normal");
	config.roughnessTexture = ReadOptionalStringField(layerNode, "roughness_texture");
	config.roughnessPath = ReadOptionalStringField(layerNode, "roughness");
	config.tiling = ReadOptionalFloatField(layerNode, "tiling");
	return config;
}

std::vector<VansSceneTerrainLayerConfig> DecodeTerrainLayers(const VansSerializedValue& terrainNode)
{
	std::vector<VansSceneTerrainLayerConfig> layers;
	const VansSerializedValue* found = FindObjectField(terrainNode, "layers");
	if (!found || found->kind != VansSerializedValue::Kind::Array)
		return layers;

	layers.reserve(found->arrayItems.size());
	for (const VansSerializedValue& layerNode : found->arrayItems)
		if (layerNode.kind == VansSerializedValue::Kind::Object)
			layers.push_back(DecodeTerrainLayer(layerNode));
	return layers;
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
	config.terrainSize = ReadOptionalFloatField(collisionNode, "terrainSize");
	config.maxHeight = ReadOptionalFloatField(collisionNode, "maxHeight");
	config.heightOffset = ReadOptionalFloatField(collisionNode, "heightOffset");
	config.layer = ReadOptionalStringField(collisionNode, "layer");
	config.flipX = ReadOptionalBoolField(collisionNode, "flipX");
	config.flipZ = ReadOptionalBoolField(collisionNode, "flipZ");
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

VansSceneWaterCausticsConfig DecodeWaterCaustics(const VansSerializedValue& causticsNode)
{
	VansSceneWaterCausticsConfig config;
	config.enabled = ReadOptionalBoolField(causticsNode, "enabled");
	config.intensity = ReadOptionalFloatField(causticsNode, "intensity");
	config.maxDistance = ReadOptionalFloatField(causticsNode, "maxDistance");
	config.maxGain = ReadOptionalFloatField(causticsNode, "maxGain");
	config.filterRadius = ReadOptionalFloatField(causticsNode, "filterRadius");
	return config;
}

VansSceneWaterRefractionConfig DecodeWaterRefraction(const VansSerializedValue& refractionNode)
{
	VansSceneWaterRefractionConfig config;
	config.enabled = ReadOptionalBoolField(refractionNode, "enabled");
	config.distortionStrength = ReadOptionalFloatField(refractionNode, "distortionStrength");
	return config;
}

VansSceneWaterSSRConfig DecodeWaterSSR(const VansSerializedValue& ssrNode)
{
	VansSceneWaterSSRConfig config;
	config.enabled = ReadOptionalBoolField(ssrNode, "enabled");
	config.maxDistance = ReadOptionalFloatField(ssrNode, "maxDistance");
	config.maxRoughness = ReadOptionalFloatField(ssrNode, "maxRoughness");
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

std::string ReadVegetationConfigPath(const VansSerializedValue& vegetationNode)
{
	if (vegetationNode.kind == VansSerializedValue::Kind::String)
		return vegetationNode.stringValue;
	if (vegetationNode.kind != VansSerializedValue::Kind::Object)
		return {};

	std::string path = ReadOptionalStringField(vegetationNode, "config").value_or(std::string());
	if (path.empty()) path = ReadOptionalStringField(vegetationNode, "configPath").value_or(std::string());
	if (path.empty()) path = ReadOptionalStringField(vegetationNode, "path").value_or(std::string());
	return path;
}

VansSerializedValue LoadVegetationConfigFromReference(
	const VansSerializedValue& vegetationNode,
	const std::string& projectRoot)
{
	const std::string configPath = ReadVegetationConfigPath(vegetationNode);
	if (configPath.empty())
		return vegetationNode;

	std::filesystem::path resolved(configPath);
	if (resolved.is_relative())
		resolved = std::filesystem::path(projectRoot) / resolved;

	nlohmann::json loaded;
	std::string error;
	if (!VansJsonFileStorage::Read(resolved, loaded, error))
	{
		VANS_LOG_ERROR("[VegetationConfig] Cannot read config file: " << resolved.string() << " (" << error << ")");
		return VansSerializedValue::Object({});
	}

	if (!loaded.is_object())
	{
		VANS_LOG_ERROR("[VegetationConfig] Invalid JSON config file: " << resolved.string());
		return VansSerializedValue::Object({});
	}
	VANS_LOG("[VegetationConfig] Loaded config file: " << resolved.string());

	VansSerializedValue resolvedNode = DecodeSerializedValueJson(loaded);
	if (vegetationNode.kind == VansSerializedValue::Kind::Object)
	{
		for (const auto& [key, value] : vegetationNode.objectFields)
		{
			if (key == "config" || key == "configPath" || key == "path")
				continue;
			SetSerializedObjectField(resolvedNode, key, value);
		}
	}
	return resolvedNode;
}

std::optional<std::int32_t> ReadSubmeshIndex(const VansSerializedValue& node)
{
	if (node.kind != VansSerializedValue::Kind::Object)
		return std::nullopt;
	if (std::optional<std::int32_t> index = ReadOptionalInt32Field(node, "submeshIndex"))
		return index;

	const VansSerializedValue* submesh = FindObjectField(node, "submesh");
	if (!submesh)
		return std::nullopt;
	if (submesh->kind == VansSerializedValue::Kind::Int)
		return static_cast<std::int32_t>(submesh->intValue);
	if (submesh->kind == VansSerializedValue::Kind::Object)
		return ReadOptionalInt32Field(*submesh, "index");
	return std::nullopt;
}

bool HasPcgMaskSource(const VansSerializedValue& maskNode)
{
	return FindObjectField(maskNode, "path") != nullptr
		|| FindObjectField(maskNode, "texture") != nullptr
		|| FindObjectField(maskNode, "guid") != nullptr
		|| FindObjectField(maskNode, "textureGuid") != nullptr
		|| FindObjectField(maskNode, "assetGuid") != nullptr
		|| FindObjectField(maskNode, "asset") != nullptr;
}

VansScenePcgMaskConfig DecodePcgMaskConfig(
	const VansSerializedValue& maskNode,
	const std::string& fallbackId = {})
{
	VansScenePcgMaskConfig config;
	if (maskNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.id = ReadOptionalStringField(maskNode, "id");
	if (!config.id && !fallbackId.empty())
		config.id = fallbackId;
	config.path = ReadOptionalStringField(maskNode, "path");
	config.assetGuid = ReadOptionalStringField(maskNode, "guid");
	if (!config.assetGuid) config.assetGuid = ReadOptionalStringField(maskNode, "textureGuid");
	if (!config.assetGuid) config.assetGuid = ReadOptionalStringField(maskNode, "assetGuid");
	config.channel = ReadOptionalStringField(maskNode, "channel");
	config.boundsMin = ReadOptionalFloat2Field(maskNode, "boundsMin");
	config.boundsMax = ReadOptionalFloat2Field(maskNode, "boundsMax");
	config.worldMin = ReadOptionalFloat2Field(maskNode, "worldMin");
	config.worldMax = ReadOptionalFloat2Field(maskNode, "worldMax");
	config.threshold = ReadOptionalFloatField(maskNode, "threshold");
	config.densityScale = ReadOptionalFloatField(maskNode, "densityScale");
	config.invert = ReadOptionalBoolField(maskNode, "invert");

	if (const VansSerializedValue* texture = FindObjectField(maskNode, "texture"))
	{
		if (texture->kind == VansSerializedValue::Kind::Object)
		{
			if (!config.assetGuid) config.assetGuid = ReadOptionalStringField(*texture, "guid");
		}
		else if (texture->kind == VansSerializedValue::Kind::String)
		{
			config.textureValue = texture->stringValue;
		}
	}

	if (const VansSerializedValue* asset = FindObjectField(maskNode, "asset"))
	{
		if (asset->kind == VansSerializedValue::Kind::Object)
		{
			if (!config.assetGuid) config.assetGuid = ReadOptionalStringField(*asset, "guid");
		}
		else if (asset->kind == VansSerializedValue::Kind::String)
		{
			config.assetGuid = asset->stringValue;
		}
	}

	return config;
}

std::optional<VansScenePcgMaskReferenceConfig> DecodePcgMaskReference(const VansSerializedValue& ownerNode)
{
	if (ownerNode.kind != VansSerializedValue::Kind::Object)
		return std::nullopt;

	const VansSerializedValue* mask = FindObjectField(ownerNode, "mask");
	if (!mask)
		return std::nullopt;

	VansScenePcgMaskReferenceConfig config;
	if (mask->kind == VansSerializedValue::Kind::String)
	{
		config.ref = mask->stringValue;
		return config;
	}

	if (mask->kind != VansSerializedValue::Kind::Object)
		return std::nullopt;

	const std::string ref = ReadOptionalStringField(*mask, "ref")
		.value_or(ReadOptionalStringField(*mask, "id").value_or(std::string()));
	if (!ref.empty() && !HasPcgMaskSource(*mask))
	{
		config.ref = ref;
		return config;
	}

	config.inlineMask = DecodePcgMaskConfig(*mask, ref);
	return config;
}

std::vector<VansScenePcgMaskConfig> DecodePcgMasks(const VansSerializedValue& vegetationNode)
{
	std::vector<VansScenePcgMaskConfig> masks;
	const VansSerializedValue* masksNode = nullptr;
	if (const VansSerializedValue* pcg = ReadObjectField(vegetationNode, "pcg"))
	{
		const VansSerializedValue* pcgMasks = FindObjectField(*pcg, "masks");
		if (pcgMasks && (pcgMasks->kind == VansSerializedValue::Kind::Array ||
			pcgMasks->kind == VansSerializedValue::Kind::Object))
		{
			masksNode = pcgMasks;
		}
	}
	if (masksNode == nullptr)
	{
		const VansSerializedValue* found = FindObjectField(vegetationNode, "masks");
		if (found && (found->kind == VansSerializedValue::Kind::Array ||
			found->kind == VansSerializedValue::Kind::Object))
		{
			masksNode = found;
		}
	}
	if (masksNode == nullptr)
		return masks;

	if (masksNode->kind == VansSerializedValue::Kind::Array)
	{
		masks.reserve(masksNode->arrayItems.size());
		for (const VansSerializedValue& maskNode : masksNode->arrayItems)
			if (maskNode.kind == VansSerializedValue::Kind::Object)
				masks.push_back(DecodePcgMaskConfig(maskNode));
	}
	else if (masksNode->kind == VansSerializedValue::Kind::Object)
	{
		masks.reserve(masksNode->objectFields.size());
		for (const auto& [id, maskNode] : masksNode->objectFields)
			if (maskNode.kind == VansSerializedValue::Kind::Object)
				masks.push_back(DecodePcgMaskConfig(maskNode, id));
	}
	return masks;
}

VansSceneVegetationPlacementConfig DecodeVegetationPlacement(const VansSerializedValue& placementNode)
{
	VansSceneVegetationPlacementConfig config;
	config.boundsMin = ReadOptionalFloat2Field(placementNode, "boundsMin");
	config.boundsMax = ReadOptionalFloat2Field(placementNode, "boundsMax");
	config.grassScaleMin = ReadOptionalFloatField(placementNode, "grassScaleMin");
	config.grassScaleMax = ReadOptionalFloatField(placementNode, "grassScaleMax");
	config.mask = DecodePcgMaskReference(placementNode);
	return config;
}

VansSceneVegetationTreePartType DecodeTreePartType(const std::string& type)
{
	std::string lower = type;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	if (lower == "trunk") return VansSceneVegetationTreePartType::Trunk;
	if (lower == "leaves" || lower == "leaf") return VansSceneVegetationTreePartType::Leaves;
	return VansSceneVegetationTreePartType::Custom;
}

VansSceneVegetationTreePartConfig DecodeTreePart(const VansSerializedValue& partNode)
{
	VansSceneVegetationTreePartConfig config;
	config.type = DecodeTreePartType(ReadOptionalStringField(partNode, "type").value_or("custom"));
	config.mesh = ReadOptionalStringField(partNode, "mesh").value_or(std::string());
	config.material = ReadOptionalStringField(partNode, "material").value_or(std::string());
	config.submeshIndex = ReadSubmeshIndex(partNode);
	return config;
}

VansSceneVegetationTreeSpeciesConfig DecodeTreeSpecies(const VansSerializedValue& speciesNode)
{
	VansSceneVegetationTreeSpeciesConfig config;
	config.name = ReadOptionalStringField(speciesNode, "name").value_or("TreeSpecies");
	config.boundsRadius = ReadOptionalFloatField(speciesNode, "boundsRadius");
	const VansSerializedValue* parts = FindObjectField(speciesNode, "parts");
	if (parts && parts->kind == VansSerializedValue::Kind::Array)
	{
		config.parts.reserve(parts->arrayItems.size());
		for (const VansSerializedValue& partNode : parts->arrayItems)
			if (partNode.kind == VansSerializedValue::Kind::Object)
				config.parts.push_back(DecodeTreePart(partNode));
	}
	return config;
}

VansSceneVegetationTreeInstanceConfig DecodeTreeInstance(const VansSerializedValue& instanceNode)
{
	VansSceneVegetationTreeInstanceConfig config;
	config.species = ReadOptionalStringField(instanceNode, "species");
	config.position = ReadOptionalFloat3Field(instanceNode, "position");
	config.yaw = ReadOptionalFloatField(instanceNode, "yaw");
	config.scale = ReadOptionalFloatField(instanceNode, "scale");
	config.submeshIndex = ReadSubmeshIndex(instanceNode);
	return config;
}

VansSceneVegetationRandomTreeConfig DecodeRandomTreeConfig(const VansSerializedValue& randomNode)
{
	VansSceneVegetationRandomTreeConfig config;
	config.count = ReadOptionalUInt32Field(randomNode, "count");
	config.seed = ReadOptionalUInt32Field(randomNode, "seed");
	config.scaleMin = ReadOptionalFloatField(randomNode, "scaleMin");
	config.scaleMax = ReadOptionalFloatField(randomNode, "scaleMax");
	config.boundsMin = ReadOptionalFloat2Field(randomNode, "boundsMin");
	config.boundsMax = ReadOptionalFloat2Field(randomNode, "boundsMax");
	config.species = ReadOptionalStringField(randomNode, "species");
	config.submeshIndex = ReadSubmeshIndex(randomNode);
	config.mask = DecodePcgMaskReference(randomNode);
	return config;
}

VansSceneVegetationTreesConfig DecodeVegetationTrees(const VansSerializedValue& treesNode)
{
	VansSceneVegetationTreesConfig config;
	config.enabled = ReadOptionalBoolField(treesNode, "enabled");
	config.cullDistance = ReadOptionalFloatField(treesNode, "cullDistance");
	config.cullEnabled = ReadOptionalBoolField(treesNode, "cullEnabled");
	config.hizEnabled = ReadOptionalBoolField(treesNode, "hizEnabled");
	config.fallbackCount = ReadOptionalUInt32Field(treesNode, "count");
	config.placementRadius = ReadOptionalFloatField(treesNode, "placementRadius");
	config.center = ReadOptionalFloat3Field(treesNode, "center");

	const VansSerializedValue* species = FindObjectField(treesNode, "species");
	if (species && species->kind == VansSerializedValue::Kind::Array)
	{
		config.species.reserve(species->arrayItems.size());
		for (const VansSerializedValue& speciesNode : species->arrayItems)
			if (speciesNode.kind == VansSerializedValue::Kind::Object)
				config.species.push_back(DecodeTreeSpecies(speciesNode));
	}

	const VansSerializedValue* instances = FindObjectField(treesNode, "instances");
	if (instances && instances->kind == VansSerializedValue::Kind::Array)
	{
		config.instances.reserve(instances->arrayItems.size());
		for (const VansSerializedValue& instanceNode : instances->arrayItems)
			if (instanceNode.kind == VansSerializedValue::Kind::Object)
				config.instances.push_back(DecodeTreeInstance(instanceNode));
	}

	if (const VansSerializedValue* randomInstances = ReadObjectField(treesNode, "randomInstances"))
		config.randomInstances = DecodeRandomTreeConfig(*randomInstances);
	return config;
}

VansSceneVegetationRenderConfig DecodeVegetationRenderConfig(const VansSerializedValue& renderNode)
{
	VansSceneVegetationRenderConfig config;
	config.mesh = ReadOptionalStringField(renderNode, "mesh");
	config.material = ReadOptionalStringField(renderNode, "material");
	config.percent = ReadOptionalFloatField(renderNode, "percent");
	return config;
}

VansSceneVegetationNodeConfig DecodeVegetationObject(const VansSerializedValue& vegetationNode)
{
	VansSceneVegetationNodeConfig config;
	if (vegetationNode.kind != VansSerializedValue::Kind::Object)
	{
		config.valid = false;
		return config;
	}

	config.instanceCount = ReadOptionalUInt32Field(vegetationNode, "instanceCount");
	config.boneCount = ReadOptionalUInt32Field(vegetationNode, "boneCount");
	config.bladeHeight = ReadOptionalFloatField(vegetationNode, "bladeHeight");
	config.windDirX = ReadOptionalFloatField(vegetationNode, "windDirX");
	config.windDirZ = ReadOptionalFloatField(vegetationNode, "windDirZ");
	config.leanDeviation = ReadOptionalFloatField(vegetationNode, "leanDeviation");
	config.material = ReadOptionalStringField(vegetationNode, "material");
	config.name = ReadOptionalStringField(vegetationNode, "name");
	config.subBladeCount = ReadOptionalUInt32Field(vegetationNode, "subBladeCount");
	config.subBladeScatterRadiusMin = ReadOptionalFloatField(vegetationNode, "subBladeScatterRadiusMin");
	config.subBladeScatterRadiusMax = ReadOptionalFloatField(vegetationNode, "subBladeScatterRadiusMax");
	config.windStrength = ReadOptionalFloatField(vegetationNode, "windStrength");
	config.windFrequency = ReadOptionalFloatField(vegetationNode, "windFrequency");
	config.windSpeed = ReadOptionalFloatField(vegetationNode, "windSpeed");
	config.windBendMult = ReadOptionalFloatField(vegetationNode, "windBendMult");
	config.stiffness = ReadOptionalFloatField(vegetationNode, "stiffness");
	config.damping = ReadOptionalFloatField(vegetationNode, "damping");
	config.softness = ReadOptionalFloatField(vegetationNode, "softness");
	config.lodFullDist = ReadOptionalFloatField(vegetationNode, "lodFullDist");
	config.lodFadeDist = ReadOptionalFloatField(vegetationNode, "lodFadeDist");
	config.terrainMaxHeight = ReadOptionalFloatField(vegetationNode, "terrainMaxHeight");
	config.terrainHeightOffset = ReadOptionalFloatField(vegetationNode, "terrainHeightOffset");
	config.hizSampleBias = ReadOptionalFloatField(vegetationNode, "hizSampleBias");
	config.grassScaleMin = ReadOptionalFloatField(vegetationNode, "grassScaleMin");
	config.grassScaleMax = ReadOptionalFloatField(vegetationNode, "grassScaleMax");
	config.pcgMasks = DecodePcgMasks(vegetationNode);

	if (const VansSerializedValue* placement = ReadObjectField(vegetationNode, "placement"))
		config.placement = DecodeVegetationPlacement(*placement);
	if (const VansSerializedValue* trees = ReadObjectField(vegetationNode, "trees"))
		config.trees = DecodeVegetationTrees(*trees);

	const VansSerializedValue* renderConfigs = FindObjectField(vegetationNode, "renderConfigs");
	if (renderConfigs && renderConfigs->kind == VansSerializedValue::Kind::Array)
	{
		config.renderConfigs.reserve(renderConfigs->arrayItems.size());
		for (const VansSerializedValue& renderNode : renderConfigs->arrayItems)
			if (renderNode.kind == VansSerializedValue::Kind::Object)
				config.renderConfigs.push_back(DecodeVegetationRenderConfig(renderNode));
	}

	return config;
}
}

VansSceneTerrainNodeConfig VansSceneEnvironmentNodeConfigReader::ReadTerrain(
	const VansSerializedValue& terrainNode)
{
	VansSceneTerrainNodeConfig config;
	if (terrainNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.heightmap = ReadOptionalStringField(terrainNode, "heightmap");
	config.terrainSize = ReadOptionalFloatField(terrainNode, "terrainSize");
	config.maxHeight = ReadOptionalFloatField(terrainNode, "maxHeight");
	config.heightOffset = ReadOptionalFloatField(terrainNode, "heightOffset");
	config.splitDistMult = ReadOptionalFloatField(terrainNode, "splitDistMult");
	config.lodDistanceRatio = ReadOptionalFloatField(terrainNode, "lodDistanceRatio");
	config.morphStartRatio = ReadOptionalFloatField(terrainNode, "morphStartRatio");
	config.maxPatchInstances = ReadOptionalUIntField(terrainNode, "maxPatchInstances");
	config.splatmaps = DecodeStringArrayField(terrainNode, "splatmaps");
	config.layers = DecodeTerrainLayers(terrainNode);
	config.name = ReadOptionalStringField(terrainNode, "name");
	if (const VansSerializedValue* tessellation = ReadObjectField(terrainNode, "tessellation"))
		config.tessellation = DecodeTerrainTessellation(*tessellation);
	if (const VansSerializedValue* collision = ReadObjectField(terrainNode, "collision"))
		config.collision = DecodeTerrainCollision(*collision);
	return config;
}

VansSceneWaterNodeConfig VansSceneEnvironmentNodeConfigReader::ReadWater(
	const VansSerializedValue& waterNode)
{
	VansSceneWaterNodeConfig config;
	if (waterNode.kind != VansSerializedValue::Kind::Object)
		return config;

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
	if (const VansSerializedValue* caustics = ReadObjectField(waterNode, "caustics"))
		config.caustics = DecodeWaterCaustics(*caustics);
	if (const VansSerializedValue* refraction = ReadObjectField(waterNode, "refraction"))
		config.refraction = DecodeWaterRefraction(*refraction);
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

VansSceneVegetationNodeConfig VansSceneEnvironmentNodeConfigReader::ReadVegetation(
	const VansSerializedValue& vegetationNode,
	const std::string& projectRoot)
{
	VansSerializedValue resolvedVegetationNode =
		LoadVegetationConfigFromReference(vegetationNode, projectRoot);
	if (resolvedVegetationNode.kind != VansSerializedValue::Kind::Object)
	{
		VANS_LOG_ERROR("[SceneLoader] Vegetation config must be an object or config path.");
		VansSceneVegetationNodeConfig config;
		config.valid = false;
		return config;
	}

	return DecodeVegetationObject(resolvedVegetationNode);
}
}
