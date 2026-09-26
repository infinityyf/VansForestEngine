#pragma once

#include "VansPcgBatchPlan.h"

namespace Vans
{
enum class VansPcgUpdatePartition
{
	PerLayer,
	PerCellWhenCovered
};

struct VansPcgUpdatePlan
{
	std::vector<VansPcgBatchUpdate> updates;
	std::uint64_t candidates = 0;
};

class VansPcgUpdatePlanner
{
public:
	static bool PlanRecipe(const VansPcgRecipeAsset& recipe,
		const VansAssetObjectRepository& repository,
		const VansPcgSurfaceResolver& resolveSurface,
		const std::optional<VansPcgBounds>& coverage,
		std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField,
		VansPcgUpdatePartition partition,
		VansPcgUpdatePlan& output,
		std::string& error);

	static bool PlanLayer(const std::string& recipeName,
		const VansPcgRegion& region,
		const VansPcgLayer& layer,
		const VansAssetObjectRepository& repository,
		const VansPcgSurfaceResolver& resolveSurface,
		const std::optional<VansPcgBounds>& coverage,
		std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField,
		VansPcgUpdatePartition partition,
		bool clearWhenDisabled,
		VansPcgUpdatePlan& output,
		std::string& error);
};
}
