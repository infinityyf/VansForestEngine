#include "VansPcgExecutor.h"
#include "VansPcgDeterminism.h"
#include "VansPcgSplineField.h"
#include "../AssetCore/VansAssetObjectRepository.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
VansPcgExecutionResult VansPcgExecutor::Generate(const VansPcgRecipeAsset& recipe,
	const VansAssetObjectRepository& repository, const VansPcgSurfaceResolver& resolveSurface,
	const std::optional<VansPcgBounds>& outputBounds,
    std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField)
{
	VansPcgExecutionResult result;
	const auto fail = [&](const std::string& error) {
		result.error = error;
		result.layers.clear();
		return result;
	};
	const auto diagnostics = ValidatePcgRecipe(recipe, true);
	if (!diagnostics.empty()) return fail(diagnostics.front());
	std::unordered_set<VansAssetGuid> pixelOwners;
	for (const auto& region : recipe.regions)
	{
		if (!region.enabled) continue;
		VansPcgSurfaceSampler surface;
		bool surfaceResolved = false;
		for (const auto& layer : region.layers)
		{
			if (!layer.enabled) continue;
			const std::string label = "PCG region '" + region.id + "', layer '" + layer.id + "': ";
			VansPcgLayerResult output;
			output.regionId = region.id;
			output.layerId = layer.id;
			output.plantGuid = layer.plant;
			output.plant = repository.ResolveLatest<VansPlantTypeAsset>(layer.plant);
			if (!output.plant) return fail(label + "plant asset is not available in memory");
			const auto plantErrors = ValidatePlantTypeAsset(*output.plant, true);
			if (!plantErrors.empty()) return fail(label + plantErrors.front());
			if (output.plant->category != layer.category) return fail(label + "plant category does not match the layer panel");
			VansPcgMaskBinding masks;
			std::string maskError;
			if (!ResolvePcgMaskBinding(region, layer, repository, true, pixelOwners, masks, maskError))
				return fail(label + maskError);
			const auto& density = masks.density;
			const auto& exclusion = masks.exclusion;
			VansPcgDistributionSettings settings;
			static_cast<VansPcgPlacementSettings&>(settings) = layer.placement;
			settings.regionId = region.id;
			settings.layerId = layer.id;
			settings.bounds = region.bounds;
			settings.seed = (static_cast<std::uint64_t>(region.seed) << 32) | layer.seed;
			for (const auto& variant : output.plant->variants)
				settings.variants.push_back({ variant.id, variant.weight, variant.footprintRadius });
			VansPcgGenerationResult generated;
			if (layer.source == VansPcgSourceMode::Fixed)
			{
				for (const auto& variant : settings.variants) generated.variantIds.push_back(variant.id);
				std::sort(generated.variantIds.begin(), generated.variantIds.end());
			}
			else
			{
				if (!surfaceResolved)
				{
					if (region.surface.kind == VansPcgSurfaceKind::Plane)
						surface = [height = region.surface.planeHeight](float, float, VansPcgSurfacePoint& point) {
							point.height = height; point.normal = { 0, 1, 0 }; return true;
						};
					else if (resolveSurface) surface = resolveSurface(region.surface, result.error);
					if (!surface || !result.error.empty()) return fail(label + (result.error.empty() ? "bound surface is unavailable" : result.error));
					surfaceResolved = true;
				}
				generated = layer.source == VansPcgSourceMode::Density
					? VansPcgPointGenerator::GenerateDensity(settings, density->mask, exclusion ? &exclusion->mask : nullptr,
						outputBounds.value_or(region.bounds), surface, layer.budget)
					: VansPcgPointGenerator::GenerateCount(settings, density->mask, exclusion ? &exclusion->mask : nullptr,
						layer.targetCount, region.bounds, surface, layer.budget);
				if (!generated) return fail(label + generated.error);
			}
			output.variantIds = std::move(generated.variantIds);
			output.points = std::move(generated.points);
			output.stats = generated.stats;
			const auto addAuthored = [&](const std::vector<VansPcgAuthoredInstance>& instances) {
				for (const auto& instance : instances)
				{
					const auto variant = std::lower_bound(output.variantIds.begin(), output.variantIds.end(), instance.variant);
					if (variant == output.variantIds.end() || *variant != instance.variant) { result.error = "authored instance references an unknown model variant"; return false; }
					if (output.points.size() >= layer.budget.maxInstances) { result.error = "authored instances exceed the hard instance budget"; return false; }
					VansPcgPoint point;
					point.id = VansPcgPointGenerator::AuthoredInstanceId(region.id, layer.id, instance.id);
					point.variantIndex = static_cast<std::uint32_t>(variant - output.variantIds.begin());
					point.position = instance.position;
					point.rotation = instance.rotation;
					point.scale = instance.scale;
					output.points.push_back(std::move(point));
				}
				return true;
			};
			if (layer.source == VansPcgSourceMode::Fixed && !addAuthored(layer.fixedInstances)) return fail(label + result.error);
			const auto maskedFixedCount = layer.source == VansPcgSourceMode::Fixed && layer.category == VansPlantCategory::Tree
				? output.points.size() : 0;
			if (!addAuthored(layer.addedInstances)) return fail(label + result.error);
			std::unordered_map<std::uint64_t, std::size_t> pointIndices;
			if (!layer.overrides.empty()) for (std::size_t i = 0; i < output.points.size(); ++i)
				if (!pointIndices.emplace(output.points[i].id, i).second) return fail(label + "stable point identity collision");
			std::unordered_set<std::uint64_t> removed;
			std::unordered_set<std::uint64_t> locked;
			for (const auto& edit : layer.overrides)
			{
				std::uint64_t id = 0;
				if (!VansPcgPointGenerator::ReadPointIdText(edit.target, id)) return fail(label + "invalid override identity");
				const auto found = pointIndices.find(id);
				if (edit.kind==VansPcgOverrideKind::Lock) {
					locked.insert(id);
					const auto variant=std::lower_bound(output.variantIds.begin(),output.variantIds.end(),edit.variant);
					if (variant==output.variantIds.end() || *variant!=edit.variant) return fail(label+"locked instance references an unknown variant");
					VansPcgPoint point;point.id=id;point.variantIndex=static_cast<std::uint32_t>(variant-output.variantIds.begin());
					point.position=edit.position;point.rotation=edit.rotation;point.scale=edit.scale;
					if (found!=pointIndices.end()) output.points[found->second]=point;
					else {
						if (output.points.size()>=layer.budget.maxInstances) return fail(label+"locked instances exceed the hard instance budget");
						output.points.push_back(point);
					}
					continue;
				}
				if (found == pointIndices.end()) { output.orphanOverrides.push_back(edit.target); continue; }
				if (edit.kind == VansPcgOverrideKind::Remove) removed.insert(id);
				else
				{
					auto& point = output.points[found->second];
					point.position = edit.position;
					point.rotation = edit.rotation;
					point.scale = edit.scale;
				}
			}
			// 仅过滤固定树木的输出，保留作者数据供涂回/撤销恢复；使用最终根部位置。
			// 手工新增及显式 Lock 仍遵守原有的独立编辑语义。
			for (std::size_t i = 0; i < maskedFixedCount; ++i)
			{
				const auto& point = output.points[i];
				if (removed.count(point.id) || locked.count(point.id)) continue;
				++output.stats.candidates;
				if (!VansPcgPointGenerator::PassesMask(layer.placement, density->mask,
					exclusion ? &exclusion->mask : nullptr, point.position[0], point.position[2], point.id))
				{
					removed.insert(point.id);
					++output.stats.maskRejected;
				}
			}
            std::sort(output.points.begin(),output.points.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            if (std::adjacent_find(output.points.begin(),output.points.end(),[](const auto& a,const auto& b){return a.id==b.id;})!=output.points.end())
                return fail(label+"stable point identity collision");
			output.points.erase(std::remove_if(output.points.begin(), output.points.end(), [&](const VansPcgPoint& point) {
                if (removed.count(point.id)) return true;
                // 在最终根部位置叠加派生场，包括固定/手工实例；源 Mask 和作者数据保持不变。
                // 独立稳定随机通道使保留密度为原密度*(1-exclusion)，拖动时不重新抽签。
                if (splineField && splineField->hasVegetationExclusion && region.surface.terrain==splineField->terrainGuid)
                {
                    const float exclusion=splineField->SampleVegetationExclusion(point.position[0],point.position[2]);
					const auto hash=PcgMix64(point.id ^ 0x93a9dc78ab1ce745ull);
                    if (double(hash>>11)*(1.0/9007199254740992.0)<exclusion) {++output.stats.maskRejected;return true;}
                }
                return false;
			}), output.points.end());

			output.stats.generatedCount = output.points.size();
			result.layers.push_back(std::move(output));
		}
	}
	return result;
}
}
