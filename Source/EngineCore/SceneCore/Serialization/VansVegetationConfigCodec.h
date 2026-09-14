#pragma once

#include "../../PcgCore/VansPcgRecipeAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <string>
#include <vector>

namespace Vans
{
struct VansVegetationConfigAsset
{
	VansPcgRecipeAsset config;
};

class VansVegetationConfigCodec
{
public:
	static bool Decode(
		const VansSerializedValue& root,
		VansVegetationConfigAsset& asset,
		std::string& error);
	static bool Encode(
		const VansPcgRecipeAsset& config,
		VansSerializedValue& root,
		std::string& error);
	static std::vector<std::string> Validate(
		const VansPcgRecipeAsset& config);
	static std::string ReadReferenceGuid(const VansSerializedValue& reference);
	static bool ResolveReference(
		const VansSerializedValue& reference,
		const VansVegetationConfigAsset& asset,
		VansPcgRecipeAsset& config,
		std::string& error);
};
}
