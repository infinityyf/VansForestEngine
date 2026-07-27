#include "VansSceneCameraMediaComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

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
		return VansSceneAudioComponentConfig{ *sourceName };
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
