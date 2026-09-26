#include "VansPcgMaskAsset.h"
#include "VansPcgRecipeAsset.h"
#include "../AssetCore/VansAssetObjectRepository.h"

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
		asset.mask.width > MaximumPcgMaskDimension || asset.mask.height > MaximumPcgMaskDimension)
		errors.push_back("PCG Mask requires valid world bounds and dimensions in [1, 8192]");
	if (requirePixels && !asset.mask.IsValid()) errors.push_back("PCG Mask pixels are unavailable or have incorrect dimensions");
	return errors;
}

bool ResolvePcgMaskBinding(const VansPcgRegion& region, const VansPcgLayer& layer,
	const VansAssetObjectRepository& repository, bool requirePixels,
	std::unordered_set<VansAssetGuid>& claimedPixels, VansPcgMaskBinding& binding,
	std::string& error)
{
	error.clear();
	binding = {};
	binding.density = repository.ResolveLatest<VansPcgMaskAsset>(layer.densityMask);
	binding.exclusion = layer.exclusionMask.IsValid()
		? repository.ResolveLatest<VansPcgMaskAsset>(layer.exclusionMask) : nullptr;
	if (!binding.density || (layer.exclusionMask.IsValid() && !binding.exclusion))
	{
		error = "Mask asset is not available in memory";
		return false;
	}

	const std::pair<const VansPcgMaskAsset*, VansAssetGuid> masks[] = {
		{ binding.density.get(), layer.densityMask },
		{ binding.exclusion.get(), layer.exclusionMask }
	};
	for (const auto& [mask, expectedGuid] : masks)
	{
		if (!mask) continue;
		const auto diagnostics = ValidatePcgMaskAsset(*mask, requirePixels);
		if (!diagnostics.empty())
		{
			error = diagnostics.front();
			return false;
		}
		if (mask->mask.target.regionId != region.id || mask->mask.target.layerId != layer.id ||
			mask->mask.target.maskId != expectedGuid.ToString())
		{
			error = "Mask ownership does not match the selected region/layer";
			return false;
		}
		if (!claimedPixels.insert(mask->pixelAsset).second)
		{
			error = "writable Mask pixel textures are shared";
			return false;
		}
	}
	return true;
}
}
