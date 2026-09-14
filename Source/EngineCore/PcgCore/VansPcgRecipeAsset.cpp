#include "VansPcgRecipeAsset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Vans
{
std::vector<VansAssetGuid> VansPcgRecipeAsset::Dependencies() const
{
	std::vector<VansAssetGuid> result;
	for (const auto& region : regions)
	{
		if (region.surface.terrain.IsValid()) result.push_back(region.surface.terrain);
		for (const auto& layer : region.layers)
			for (const auto guid : { layer.plant, layer.densityMask, layer.exclusionMask })
				if (guid.IsValid()) result.push_back(guid);
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}


namespace
{
bool ValidTransform(const std::array<float, 3>& position, const std::array<float, 4>& rotation, const std::array<float, 3>& scale)
{
	double length = 0;
	for (float value : rotation) length += static_cast<double>(value) * value;
	if (!std::isfinite(length) || std::abs(length - 1.0) > 0.001) return false;
	for (std::size_t axis = 0; axis < 3; ++axis)
		if (!std::isfinite(position[axis]) || !std::isfinite(scale[axis]) || scale[axis] <= 0) return false;
	return true;
}

bool StablePointId(const std::string& text)
{
	return text.size() == 16 && std::all_of(text.begin(), text.end(), [](char ch) {
		return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
	}) && text != "0000000000000000";
}
}

std::vector<std::string> ValidatePcgRecipe(const VansPcgRecipeAsset& recipe, bool requireReady)
{
	std::vector<std::string> errors;
	std::unordered_set<std::string> regionIds;
	std::unordered_set<VansAssetGuid> maskIds;
	for (const auto& region : recipe.regions)
	{
		const auto fail = [&](const std::string& message) { errors.push_back("PCG region '" + region.id + "': " + message); };
		if (region.id.empty() || !regionIds.insert(region.id).second) fail("region IDs must be nonempty and unique");
		if (!region.bounds.IsValid()) fail("world bounds must be explicitly configured");
		for (std::size_t axis = 0; axis < 2; ++axis)
			if (std::abs(static_cast<double>(region.bounds.min[axis])) >= 1073741824.0 ||
				std::abs(static_cast<double>(region.bounds.max[axis])) >= 1073741824.0)
				fail("bounds exceed supported stable world coordinates");
		if (!std::isfinite(region.cellSize) || region.cellSize <= 0) fail("cell size must be positive");
		if (region.surface.kind < VansPcgSurfaceKind::Unassigned || region.surface.kind > VansPcgSurfaceKind::Terrain ||
			!std::isfinite(region.surface.planeHeight)) fail("surface binding is invalid");
		if (region.surface.kind != VansPcgSurfaceKind::Terrain && region.surface.terrain.IsValid())
			fail("terrain reference is only valid for a terrain surface binding");
		std::unordered_set<std::string> layerIds;
		for (const auto& layer : region.layers)
		{
			const std::string label = "layer '" + layer.id + "': ";
			if (layer.id.empty() || !layerIds.insert(layer.id).second) fail("layer IDs must be nonempty and unique within the region");
			if (layer.category != VansPlantCategory::Grass && layer.category != VansPlantCategory::Tree) fail(label + "unknown plant category");
			if (layer.source < VansPcgSourceMode::Density || layer.source > VansPcgSourceMode::Fixed) fail(label + "unknown generation source");
			if (layer.category == VansPlantCategory::Grass && (layer.source == VansPcgSourceMode::Count || layer.targetCount != 0))
				fail(label + "grass distribution uses density, not a target count");
			for (const auto& error : ValidatePcgPlacement(layer.placement)) fail(label + error);
			for (const auto mask : { layer.densityMask, layer.exclusionMask })
				if (mask.IsValid() && !maskIds.insert(mask).second) fail(label + "writable Masks cannot be shared by different bindings");
			if (layer.budget.maxCandidates > (std::numeric_limits<std::uint32_t>::max)() ||
				layer.budget.maxInstances > (std::numeric_limits<std::uint32_t>::max)()) fail(label + "generation budget exceeds supported instance addressing");
			const bool active = requireReady && region.enabled && layer.enabled;
			if (active)
			{
				if (!layer.plant.IsValid()) fail(label + "plant asset is unassigned");
				if (!layer.densityMask.IsValid()) fail(label + "every layer requires its own density Mask");
				if (!layer.budget.maxInstances || (layer.source != VansPcgSourceMode::Fixed && !layer.budget.maxCandidates))
					fail(label + "work and instance budgets must be explicitly configured");
				if (layer.source != VansPcgSourceMode::Fixed && (region.surface.kind == VansPcgSurfaceKind::Unassigned ||
					(region.surface.kind == VansPcgSurfaceKind::Terrain && !region.surface.terrain.IsValid())))
					fail(label + "a generation surface must be explicitly bound");
			}
			std::unordered_set<std::string> instanceIds;
			for (const auto* instances : { &layer.fixedInstances, &layer.addedInstances })
				for (const auto& instance : *instances)
				{
					if (instance.id.empty() || !instanceIds.insert(instance.id).second) fail(label + "authored instance IDs must be nonempty and unique");
					if (instance.variant.empty() || !ValidTransform(instance.position, instance.rotation, instance.scale))
						fail(label + "authored instances require an explicit variant and a valid transform");
				}
			std::unordered_set<std::string> overrideIds;
			for (const auto& edit : layer.overrides)
			{
				if (!StablePointId(edit.target) || !overrideIds.insert(edit.target).second) fail(label + "override targets must be unique stable point IDs");
				if (edit.kind != VansPcgOverrideKind::Remove && edit.kind != VansPcgOverrideKind::Transform && edit.kind != VansPcgOverrideKind::Lock) fail(label + "unknown override kind");
				if ((edit.kind==VansPcgOverrideKind::Lock)!=!edit.variant.empty()) fail(label + "only locked instances require an explicit variant");
				if (!ValidTransform(edit.position, edit.rotation, edit.scale)) fail(label + "override transform is invalid");
			}
		}
	}
	return errors;
}
}
