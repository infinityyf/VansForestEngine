#include "VansSceneLightComponentReader.h"

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

std::optional<std::array<float, 3>> ReadOptionalColorField(const VansSerializedValue& object)
{
	const VansSerializedValue* found = FindObjectField(object, "color");
	if (!found || found->kind != VansSerializedValue::Kind::Array || found->arrayItems.size() < 3)
		return std::nullopt;

	for (std::size_t index = 0; index < 3; ++index)
	{
		const VansSerializedValue& item = found->arrayItems[index];
		if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int)
			return std::nullopt;
	}

	return std::array<float, 3>{
		static_cast<float>(ReadSerializedNumber(found->arrayItems[0])),
		static_cast<float>(ReadSerializedNumber(found->arrayItems[1])),
		static_cast<float>(ReadSerializedNumber(found->arrayItems[2]))
	};
}

std::optional<float> ReadOptionalFloatField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found)
		return std::nullopt;
	if (found->kind == VansSerializedValue::Kind::Float || found->kind == VansSerializedValue::Kind::Int)
		return static_cast<float>(ReadSerializedNumber(*found));
	return std::nullopt;
}

std::optional<int> ReadOptionalIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Int)
		return std::nullopt;
	return static_cast<int>(found->intValue);
}

std::optional<uint32_t> ReadOptionalUIntField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found || found->kind != VansSerializedValue::Kind::Int || found->intValue < 0)
		return std::nullopt;
	return static_cast<uint32_t>(found->intValue);
}

std::optional<bool> ReadOptionalBoolField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	return found && found->kind == VansSerializedValue::Kind::Bool
		? std::optional<bool>(found->boolValue)
		: std::nullopt;
}

std::optional<std::string> ReadOptionalStringField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	return found && found->kind == VansSerializedValue::Kind::String
		? std::optional<std::string>(found->stringValue)
		: std::nullopt;
}

std::optional<std::string> ReadOptionalResolutionField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* found = FindObjectField(object, key);
	if (!found)
		return std::nullopt;
	if (found->kind == VansSerializedValue::Kind::String)
		return found->stringValue;
	if (found->kind == VansSerializedValue::Kind::Int)
		return std::to_string(found->intValue);
	return std::nullopt;
}

VansSceneLightShadowConfig DecodeShadowConfig(const VansSerializedValue& lightNode)
{
	VansSceneLightShadowConfig config;
	config.castShadows = ReadOptionalBoolField(lightNode, "castShadows");
	config.legacyShadow = ReadOptionalBoolField(lightNode, "shadow");
	config.policy = ReadOptionalStringField(lightNode, "shadowPolicy");
	config.priority = ReadOptionalIntField(lightNode, "shadowPriority");
	config.resolution = ReadOptionalResolutionField(lightNode, "shadowResolution");
	config.updateMode = ReadOptionalStringField(lightNode, "shadowUpdateMode");
	config.fallback = ReadOptionalStringField(lightNode, "shadowFallback");
	config.maxShadowDistance = ReadOptionalFloatField(lightNode, "shadowMaxDistance");
	config.nearPlaneOverride = ReadOptionalFloatField(lightNode, "shadowNearPlane");
	config.depthBiasTexels = ReadOptionalFloatField(lightNode, "shadowDepthBiasTexels");
	config.normalBiasTexels = ReadOptionalFloatField(lightNode, "shadowNormalBiasTexels");
	config.sourceRadius = ReadOptionalFloatField(lightNode, "shadowSourceRadius");
	config.affectsVolumetricFog = ReadOptionalBoolField(lightNode, "shadowAffectsFog");
	config.affectsGI = ReadOptionalBoolField(lightNode, "shadowAffectsGI");
	config.shadowCasterMask = ReadOptionalUIntField(lightNode, "shadowCasterMask");
	return config;
}
}

VansSceneDirectionalLightComponentConfig VansSceneLightComponentReader::ReadDirectionalLight(
	const VansSerializedValue& lightNode)
{
	VansSceneDirectionalLightComponentConfig config;
	config.color = ReadOptionalColorField(lightNode);
	config.intensity = ReadOptionalFloatField(lightNode, "intensity");
	return config;
}

VansScenePointLightComponentConfig VansSceneLightComponentReader::ReadPointLight(
	const VansSerializedValue& lightNode)
{
	VansScenePointLightComponentConfig config;
	config.color = ReadOptionalColorField(lightNode);
	config.intensity = ReadOptionalFloatField(lightNode, "intensity");
	config.radius = ReadOptionalFloatField(lightNode, "radius");
	config.iesProfile = ReadOptionalStringField(lightNode, "ies_profile");
	config.shadow = DecodeShadowConfig(lightNode);
	return config;
}

VansSceneSpotLightComponentConfig VansSceneLightComponentReader::ReadSpotLight(
	const VansSerializedValue& lightNode)
{
	VansSceneSpotLightComponentConfig config;
	config.color = ReadOptionalColorField(lightNode);
	config.intensity = ReadOptionalFloatField(lightNode, "intensity");
	config.radius = ReadOptionalFloatField(lightNode, "radius");
	config.innerCutoffDegrees = ReadOptionalFloatField(lightNode, "innercutoff");
	config.outerCutoffDegrees = ReadOptionalFloatField(lightNode, "outerCutoff");
	config.iesProfile = ReadOptionalStringField(lightNode, "ies_profile");
	config.iesIntensityScale = ReadOptionalFloatField(lightNode, "ies_intensity_scale");
	config.shadow = DecodeShadowConfig(lightNode);
	return config;
}

VansSceneRectLightComponentConfig VansSceneLightComponentReader::ReadRectLight(
	const VansSerializedValue& lightNode)
{
	VansSceneRectLightComponentConfig config;
	config.color = ReadOptionalColorField(lightNode);
	config.intensity = ReadOptionalFloatField(lightNode, "intensity");
	config.width = ReadOptionalFloatField(lightNode, "width");
	config.height = ReadOptionalFloatField(lightNode, "height");
	config.range = ReadOptionalFloatField(lightNode, "range");
	config.twoSided = ReadOptionalBoolField(lightNode, "two_sided");
	config.attenuationExp = ReadOptionalFloatField(lightNode, "attenuation_exp");
	config.textureLodBias = ReadOptionalFloatField(lightNode, "texture_lod_bias");
	config.emissiveTexture = ReadOptionalStringField(lightNode, "emissive_texture");
	config.emissiveVideo = ReadOptionalStringField(lightNode, "emissive_video");
	config.shadow = DecodeShadowConfig(lightNode);
	return config;
}

VansSceneLightComponentConfig VansSceneLightComponentReader::ReadComponents(
	const VansSerializedValue& components)
{
	VansSceneLightComponentConfig config;
	if (components.kind != VansSerializedValue::Kind::Object)
		return config;

	if (const VansSerializedValue* lightNode = ReadObjectField(components, "directional_light"))
		config.directionalLight = ReadDirectionalLight(*lightNode);
	if (const VansSerializedValue* lightNode = ReadObjectField(components, "point_light"))
		config.pointLight = ReadPointLight(*lightNode);
	if (const VansSerializedValue* lightNode = ReadObjectField(components, "spot_light"))
		config.spotLight = ReadSpotLight(*lightNode);
	if (const VansSerializedValue* lightNode = ReadObjectField(components, "rect_light"))
		config.rectLight = ReadRectLight(*lightNode);
	return config;
}
}
