#include "VansPcgMaskAsset.h"

namespace Vans
{
std::vector<VansAssetGuid> VansPcgMaskAsset::Dependencies() const
{
	return pixelAsset.IsValid() ? std::vector<VansAssetGuid>{ pixelAsset } : std::vector<VansAssetGuid>{};
}

std::vector<std::string> ValidatePcgMaskAsset(const VansPcgMaskAsset& asset, bool requirePixels)
{
	std::vector<std::string> errors;
	VansAssetGuid maskGuid;
	if (!asset.mask.target.IsValid() || !VansAssetGuid::TryParse(asset.mask.target.maskId, maskGuid) || !maskGuid.IsValid())
		errors.push_back("PCG Mask needs a region, layer and valid mask asset GUID");
	if (!asset.pixelAsset.IsValid() || asset.pixelAsset == maskGuid)
		errors.push_back("PCG Mask needs a separate owned pixel texture");
	if (!asset.mask.bounds.IsValid() || !asset.mask.width || !asset.mask.height ||
		asset.mask.width > 8192 || asset.mask.height > 8192)
		errors.push_back("PCG Mask requires valid world bounds and dimensions in [1, 8192]");
	if (requirePixels && !asset.mask.IsValid()) errors.push_back("PCG Mask pixels are unavailable or have incorrect dimensions");
	if (!asset.brush.IsValid())
		errors.push_back("PCG Mask brush has invalid radius, strength, hardness, target or spacing");
	return errors;
}
}
