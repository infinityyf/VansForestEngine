#pragma once

#include "../VansTerrainAsset.h"

#include <string>

namespace Vans
{
class VansTerrainAssetCodec
{
public:
	static bool DecodeDefinition(
		const VansSerializedValue& root,
		VansTerrainAsset& asset,
		std::string& error);
	static bool EncodeDefinition(
		const VansTerrainAsset& asset,
		VansSerializedValue& root,
		std::string& error);
};
}
