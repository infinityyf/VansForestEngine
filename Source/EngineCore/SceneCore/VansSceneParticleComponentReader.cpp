#include "VansSceneParticleComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
namespace
{
std::string ReadAssetReference(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Object)
		return {};
	return ReadSerializedStringField(*field, "guid");
}
}

std::optional<VansSceneParticleComponentConfig> VansSceneParticleComponentReader::ReadParticle(
	const VansSerializedValue& particleNode)
{
	if (particleNode.kind != VansSerializedValue::Kind::Object)
		return std::nullopt;

	VansSceneParticleComponentConfig config;
	config.assetGuid = ReadAssetReference(particleNode, "asset");
	config.playOnAwake = ReadSerializedBoolField(particleNode, "play_on_awake", true);
	if (config.assetGuid.empty())
		return std::nullopt;

	return config;
}

std::optional<VansSceneParticleComponentConfig> VansSceneParticleComponentReader::ReadComponents(
	const VansSerializedValue& components)
{
	if (components.kind != VansSerializedValue::Kind::Object)
		return std::nullopt;

	const VansSerializedValue* particle = FindObjectField(components, "particle");
	return particle ? ReadParticle(*particle) : std::nullopt;
}
}
