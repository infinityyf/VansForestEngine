#include "VansPcgMaskAssetStorage.h"
#include "../Serialization/VansPcgMaskAssetCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/Storage/VansFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansPcgMaskAssetStorage::Load(const std::filesystem::path& path, const PixelPathResolver& resolvePixels,
	VansPcgMaskAsset& asset, std::string& error)
{
	error.clear();
	if (!resolvePixels) { error = "PCG Mask load requires an indexed pixel resolver"; return false; }
	VansScopedIOContext io(VansIODomain::SourceResource, "PcgMaskAsset.Load", false);
	nlohmann::ordered_json root;
	VansPcgMaskAsset decoded;
	if (!VansJsonFileStorage::Read(path, root, error) ||
		!VansPcgMaskAssetCodec::DecodeDefinition(DecodeSerializedValueJson(root), decoded, error)) return false;
	const auto pixelPath = resolvePixels(decoded.pixelAsset);
	if (!pixelPath) { error = "PCG Mask pixel reference does not resolve to an indexed texture"; return false; }
	std::string bytes;
	if (!VansFileStorage::ReadAllBytes(*pixelPath, bytes, error) ||
		!VansPcgMaskAssetCodec::DecodePixels(bytes, decoded, error)) return false;
	asset = std::move(decoded);
	return true;
}
}
