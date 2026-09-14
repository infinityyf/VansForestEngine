#pragma once

#include "../VansPcgMaskAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
class VansPcgMaskAssetCodec
{
public:
	static bool DecodeDefinition(const VansSerializedValue& root, VansPcgMaskAsset& asset, std::string& error);
	static bool EncodeDefinition(const VansPcgMaskAsset& asset, VansSerializedValue& root, std::string& error);
	// 正式像素资源必须为单通道 16 位；普通图片只能经用户显式导入转换。
	static bool DecodePixels(const std::string& bytes, VansPcgMaskAsset& asset, std::string& error);
	static bool EncodePixels(const VansPcgMaskAsset& asset, std::string& bytes, std::string& error);
	static bool ImportPixels(const std::string& bytes, std::uint32_t channel, VansPcgMaskAsset& asset, std::string& error);
};
}
