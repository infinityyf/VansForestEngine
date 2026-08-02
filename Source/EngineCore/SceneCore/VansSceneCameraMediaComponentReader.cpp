#include "VansSceneCameraMediaComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cstdint>

namespace Vans
{
namespace
{
const VansSerializedValue* ReadObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
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

float ReadFloatFieldClamped(
	const VansSerializedValue& object,
	const char* key,
	float fallback,
	float minValue,
	float maxValue)
{
	const std::optional<float> value = ReadOptionalFloatField(object, key);
	return std::clamp(value.value_or(fallback), minValue, maxValue);
}

int ReadIntFieldClamped(
	const VansSerializedValue& object,
	const char* key,
	int fallback,
	int minValue,
	int maxValue)
{
	const std::int64_t value = ReadSerializedIntField(object, key, fallback);
	return std::clamp(static_cast<int>(value), minValue, maxValue);
}

std::string DefaultSourceNameResolver(const VansSerializedValue& source)
{
	return source.kind == VansSerializedValue::Kind::String ? source.stringValue : std::string{};
}

std::optional<std::string> ReadSourceName(
	const VansSerializedValue& object,
	const VansSceneSourceNameResolver& sourceResolver)
{
	const VansSerializedValue* source = FindObjectField(object, "source");
	if (!source)
		return std::nullopt;

	const std::string sourceName = sourceResolver ? sourceResolver(*source) : DefaultSourceNameResolver(*source);
	return sourceName.empty() ? std::nullopt : std::optional<std::string>(sourceName);
}
}

VansSceneCameraComponentConfig VansSceneCameraMediaComponentReader::ReadCamera(
	const VansSerializedValue& cameraNode)
{
	VansSceneCameraComponentConfig config;
	if (cameraNode.kind == VansSerializedValue::Kind::Object)
	{
		config.fov = ReadOptionalFloatField(cameraNode, "fov");
		config.nearClip = ReadOptionalFloatField(cameraNode, "nearClip");
		config.farClip = ReadOptionalFloatField(cameraNode, "farClip");
	}
	return config;
}

std::optional<VansSceneAudioComponentConfig> VansSceneCameraMediaComponentReader::ReadAudio(
	const VansSerializedValue& audioNode,
	const VansSceneSourceNameResolver& sourceResolver)
{
	if (audioNode.kind != VansSerializedValue::Kind::Object)
		return std::nullopt;

	if (std::optional<std::string> sourceName = ReadSourceName(audioNode, sourceResolver))
	{
		VansSceneAudioComponentConfig config;
		config.sourceName = *sourceName;
		config.occlusionEnabled = ReadSerializedBoolField(audioNode, "occlusionEnabled", false);
		config.occlusionGain = ReadFloatFieldClamped(audioNode, "occlusionGain", config.occlusionGain, 0.0f, 1.0f);
		config.occlusionHighFrequencyGain = ReadFloatFieldClamped(
			audioNode,
			"occlusionHighFrequencyGain",
			config.occlusionHighFrequencyGain,
			0.0f,
			1.0f);
		config.occlusionMaterial = VansEngine::NormalizeAudioOcclusionMaterialName(
			ReadSerializedStringField(audioNode, "occlusionMaterial", config.occlusionMaterial));
		config.occlusionMaterialThickness = ReadFloatFieldClamped(
			audioNode,
			"occlusionMaterialThickness",
			config.occlusionMaterialThickness,
			0.0f,
			4.0f);
		config.occlusionAttack = ReadFloatFieldClamped(audioNode, "occlusionAttack", config.occlusionAttack, 0.001f, 10.0f);
		config.occlusionRelease = ReadFloatFieldClamped(audioNode, "occlusionRelease", config.occlusionRelease, 0.001f, 10.0f);
		config.occlusionQueryInterval = ReadFloatFieldClamped(
			audioNode,
			"occlusionQueryInterval",
			config.occlusionQueryInterval,
			0.016f,
			10.0f);
		config.occlusionMaxDistance = ReadFloatFieldClamped(
			audioNode,
			"occlusionMaxDistance",
			config.occlusionMaxDistance,
			0.01f,
			100000.0f);
		config.occlusionMaxQueriesPerFrame = ReadIntFieldClamped(
			audioNode,
			"occlusionMaxQueriesPerFrame",
			config.occlusionMaxQueriesPerFrame,
			1,
			64);
		config.lowpassHighFrequencyGain = ReadFloatFieldClamped(
			audioNode,
			"lowpassHighFrequencyGain",
			config.lowpassHighFrequencyGain,
			0.0f,
			1.0f);
		config.coneEnabled = ReadSerializedBoolField(audioNode, "coneEnabled", false);
		config.coneInnerAngle = ReadFloatFieldClamped(
			audioNode,
			"coneInnerAngle",
			config.coneInnerAngle,
			0.0f,
			360.0f);
		config.coneOuterAngle = ReadFloatFieldClamped(
			audioNode,
			"coneOuterAngle",
			config.coneOuterAngle,
			config.coneInnerAngle,
			360.0f);
		config.coneOuterGain = ReadFloatFieldClamped(
			audioNode,
			"coneOuterGain",
			config.coneOuterGain,
			0.0f,
			1.0f);
		config.dopplerEnabled = ReadSerializedBoolField(audioNode, "dopplerEnabled", false);
		return config;
	}
	return std::nullopt;
}

std::optional<VansSceneVideoComponentConfig> VansSceneCameraMediaComponentReader::ReadVideo(
	const VansSerializedValue& videoNode,
	const VansSceneSourceNameResolver& sourceResolver)
{
	if (videoNode.kind != VansSerializedValue::Kind::Object)
		return std::nullopt;

	if (std::optional<std::string> sourceName = ReadSourceName(videoNode, sourceResolver))
		return VansSceneVideoComponentConfig{ *sourceName };
	return std::nullopt;
}

VansSceneCameraMediaComponentConfig VansSceneCameraMediaComponentReader::ReadComponents(
	const VansSerializedValue& components)
{
	return ReadComponents(components, DefaultSourceNameResolver);
}

VansSceneCameraMediaComponentConfig VansSceneCameraMediaComponentReader::ReadComponents(
	const VansSerializedValue& components,
	const VansSceneSourceNameResolver& sourceResolver)
{
	VansSceneCameraMediaComponentConfig config;
	if (components.kind != VansSerializedValue::Kind::Object)
		return config;

	if (const VansSerializedValue* camera = FindObjectField(components, "camera"))
		config.camera = ReadCamera(*camera);
	if (const VansSerializedValue* audio = ReadObjectField(components, "audio"))
		config.audio = ReadAudio(*audio, sourceResolver);
	if (const VansSerializedValue* video = ReadObjectField(components, "video"))
		config.video = ReadVideo(*video, sourceResolver);
	return config;
}
}
