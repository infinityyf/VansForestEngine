#include "VansPcgUpdatePlanner.h"
#include "../AssetCore/VansAssetObjectRepository.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
bool AppendUpdate(const VansPcgRegion& region, VansPcgBatchUpdate update,
	VansPcgUpdatePartition partition, VansPcgUpdatePlan& plan, std::string& error)
{
	if (partition != VansPcgUpdatePartition::PerCellWhenCovered || !update.coverage)
	{
		plan.updates.push_back(std::move(update));
		return true;
	}
	const auto& coverage = *update.coverage;
	if (!std::isfinite(region.cellSize) || region.cellSize <= 0 || !coverage.IsValid())
	{
		error = "Invalid PCG cell partition target or coverage.";
		return false;
	}
	const auto minX = static_cast<std::int64_t>(std::floor(coverage.min[0] / region.cellSize));
	const auto maxX = static_cast<std::int64_t>(std::ceil(coverage.max[0] / region.cellSize));
	const auto minZ = static_cast<std::int64_t>(std::floor(coverage.min[1] / region.cellSize));
	const auto maxZ = static_cast<std::int64_t>(std::ceil(coverage.max[1] / region.cellSize));
	for (auto z = minZ; z < maxZ; ++z)
	{
		for (auto x = minX; x < maxX; ++x)
		{
			VansPcgBatchUpdate cell;
			cell.region = update.region;
			cell.layer = update.layer;
			cell.cellSize = update.cellSize;
			cell.coverage = VansPcgBounds{
				{ static_cast<float>(x * region.cellSize), static_cast<float>(z * region.cellSize) },
				{ static_cast<float>((x + 1) * region.cellSize), static_cast<float>((z + 1) * region.cellSize) }
			};
			for (const auto& [key, batch] : update.batches)
				if (key.x == x && key.z == z) cell.batches.emplace(key, batch);
			plan.updates.push_back(std::move(cell));
		}
	}
	return true;
}
}

bool VansPcgUpdatePlanner::PlanRecipe(const VansPcgRecipeAsset& recipe,
	const VansAssetObjectRepository& repository,
	const VansPcgSurfaceResolver& resolveSurface,
	const std::optional<VansPcgBounds>& coverage,
	std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField,
	VansPcgUpdatePartition partition,
	VansPcgUpdatePlan& output,
	std::string& error)
{
	error.clear();
	const auto generated = VansPcgExecutor::Generate(
		recipe, repository, resolveSurface, coverage, std::move(splineField));
	if (!generated)
	{
		error = generated.error;
		return false;
	}
	VansPcgUpdatePlan plan;
	for (const auto& layer : generated.layers)
	{
		const auto region = std::find_if(recipe.regions.begin(), recipe.regions.end(),
			[&](const auto& value) { return value.id == layer.regionId; });
		if (region == recipe.regions.end())
		{
			error = "PCG generation returned an unknown region '" + layer.regionId + "'.";
			return false;
		}
		VansPcgBatchUpdate update;
		if (!BuildPcgBatchUpdate(*region, layer, coverage, update, error) ||
			!AppendUpdate(*region, std::move(update), partition, plan, error))
			return false;
		plan.candidates += layer.stats.candidates;
	}
	output = std::move(plan);
	return true;
}

bool VansPcgUpdatePlanner::PlanLayer(const std::string& recipeName,
	const VansPcgRegion& region,
	const VansPcgLayer& layer,
	const VansAssetObjectRepository& repository,
	const VansPcgSurfaceResolver& resolveSurface,
	const std::optional<VansPcgBounds>& coverage,
	std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField,
	VansPcgUpdatePartition partition,
	bool clearWhenDisabled,
	VansPcgUpdatePlan& output,
	std::string& error)
{
	VansPcgRecipeAsset selected;
	selected.name = recipeName;
	selected.regions = { region };
	selected.regions.front().layers = { layer };
	VansPcgUpdatePlan plan;
	if (!PlanRecipe(selected, repository, resolveSurface, coverage, std::move(splineField), partition, plan, error))
		return false;
	if (plan.updates.empty() && clearWhenDisabled)
	{
		VansPcgLayerResult empty;
		empty.regionId = region.id;
		empty.layerId = layer.id;
		empty.plantGuid = layer.plant;
		empty.plant = repository.ResolveLatest<VansPlantTypeAsset>(layer.plant);
		if (!empty.plant)
		{
			error = "PCG layer plant asset is not available in memory.";
			return false;
		}
		VansPcgBatchUpdate update;
		if (!BuildPcgBatchUpdate(region, empty, std::nullopt, update, error) ||
			!AppendUpdate(region, std::move(update), VansPcgUpdatePartition::PerLayer, plan, error))
			return false;
	}
	output = std::move(plan);
	return true;
}
}
