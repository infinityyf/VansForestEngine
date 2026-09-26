#pragma once

#include "VansPcgRecipeAsset.h"
#include "VansPcgMaskAsset.h"
#include <memory>
#include <optional>

namespace Vans
{
class VansAssetObjectRepository;
struct VansPcgSplineFieldSnapshot;
using VansPcgSurfaceResolver = std::function<VansPcgSurfaceSampler(const VansPcgSurfaceBinding&, std::string&)>;

struct VansPcgLayerResult
{
	std::string regionId;
	std::string layerId;
	VansAssetGuid plantGuid;
	std::shared_ptr<const VansPlantTypeAsset> plant;
	std::vector<std::string> variantIds;
	std::vector<VansPcgPoint> points;
	VansPcgGenerationStats stats;
	std::vector<std::string> orphanOverrides;
};

struct VansPcgExecutionResult
{
	std::vector<VansPcgLayerResult> layers;
	std::string error;
	explicit operator bool() const { return error.empty(); }
};

class VansPcgExecutor
{
public:
	// 只解析已经发布的内存资产，不允许回读作者文件或隐式选取场景地形。
	static VansPcgExecutionResult Generate(const VansPcgRecipeAsset& recipe,
		const VansAssetObjectRepository& repository, const VansPcgSurfaceResolver& resolveSurface,
		const std::optional<VansPcgBounds>& outputBounds = std::nullopt,
        std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField = {});
};
}
