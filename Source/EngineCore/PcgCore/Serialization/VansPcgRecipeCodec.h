#pragma once

#include "../VansPcgRecipeAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
class VansPcgRecipeCodec
{
public:
	static bool Decode(const VansSerializedValue& root, VansPcgRecipeAsset& recipe, std::string& error);
	static bool Encode(const VansPcgRecipeAsset& recipe, VansSerializedValue& root, std::string& error);
};
}
