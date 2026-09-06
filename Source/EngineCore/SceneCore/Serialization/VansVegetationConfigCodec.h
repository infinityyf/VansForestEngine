#pragma once

#include "../VansSceneEnvironmentNodeConfig.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <string>
#include <vector>

namespace Vans
{
struct VansVegetationConfigAsset
{
	VansSerializedValue sourceRoot;
	VansSceneVegetationNodeConfig config;
};

class VansVegetationConfigCodec
{
public:
	static bool Decode(
		const VansSerializedValue& root,
		VansVegetationConfigAsset& asset,
		std::string& error);
	static bool Encode(
		const VansSceneVegetationNodeConfig& config,
		VansSerializedValue& root,
		std::string& error);
	static std::vector<std::string> Validate(
		const VansSceneVegetationNodeConfig& config);
	static std::string ReadReferenceGuid(const VansSerializedValue& reference);
	static bool ResolveReference(
		const VansSerializedValue& reference,
		const VansVegetationConfigAsset& asset,
		VansSceneVegetationNodeConfig& config,
		std::string& error);
};
}
