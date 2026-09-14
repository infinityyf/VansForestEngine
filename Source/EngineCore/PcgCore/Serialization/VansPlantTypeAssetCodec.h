#pragma once

#include "../VansPlantTypeAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
class VansPlantTypeAssetCodec
{
public:
	static bool Decode(const VansSerializedValue& root, VansPlantTypeAsset& asset, std::string& error);
	static bool Encode(const VansPlantTypeAsset& asset, VansSerializedValue& root, std::string& error);
};
}
