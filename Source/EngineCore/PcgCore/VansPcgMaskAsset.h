#pragma once

#include "VansPcgMaskBrush.h"
#include "../AssetCore/VansAssetGuid.h"

namespace Vans
{
// Mask 资产及其像素纹理由一个场景分布项独占；模型资产的复用不改变这个归属。
struct VansPcgMaskAsset
{
	std::string name;
	VansAssetGuid pixelAsset;
	VansPcgMask mask;
	VansPcgBrushSettings brush;
	std::vector<VansAssetGuid> Dependencies() const;
};

std::vector<std::string> ValidatePcgMaskAsset(const VansPcgMaskAsset& asset, bool requirePixels);
}
