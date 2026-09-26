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
	return config;
}
}
