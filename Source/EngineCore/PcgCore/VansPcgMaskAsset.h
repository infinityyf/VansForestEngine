#pragma once

#include "VansPcgMask.h"
#include "../AssetCore/VansAssetGuid.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace Vans
{
class VansAssetObjectRepository;
struct VansPcgLayer;
struct VansPcgRegion;

inline constexpr std::uint32_t MaximumPcgMaskDimension = 8192;
inline constexpr std::size_t MaximumPcgMaskImportBytes = 256ull * 1024ull * 1024ull;

// Mask 资产及其像素纹理由一个场景分布项独占；模型资产的复用不改变这个归属。
struct VansPcgMaskAsset
{
	std::string name;
	VansAssetGuid pixelAsset;
	VansPcgMask mask;
	std::vector<VansAssetGuid> Dependencies() const;
};

std::vector<std::string> ValidatePcgMaskAsset(const VansPcgMaskAsset& asset, bool requirePixels);

struct VansPcgMaskBinding
{
	std::shared_ptr<const VansPcgMaskAsset> density;
	std::shared_ptr<const VansPcgMaskAsset> exclusion;
};

// 执行、资源闭包和作者预览共用同一条归属规则；claimedPixels 覆盖整个配方事务。
bool ResolvePcgMaskBinding(const VansPcgRegion& region, const VansPcgLayer& layer,
	const VansAssetObjectRepository& repository, bool requirePixels,
	std::unordered_set<VansAssetGuid>& claimedPixels, VansPcgMaskBinding& binding,
	std::string& error);
}
