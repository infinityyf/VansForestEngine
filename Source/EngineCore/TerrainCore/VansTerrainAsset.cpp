#include "VansTerrainAsset.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
namespace
{
bool FinitePositive(float value)
{
	return std::isfinite(value) && value > 0.0f;
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
{
	const auto* bytes = static_cast<const std::uint8_t*>(data);
	for (std::size_t index = 0; index < size; ++index)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ull;
	}
}
}

bool VansTerrainAsset::HasPixelData() const
{
	if (width < 2 || height < 2)
		return false;
	const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
	return heights.size() == pixelCount &&
		splatPixels[0].size() == pixelCount * 4u &&
		splatPixels[1].size() == pixelCount * 4u;
}

std::vector<VansAssetGuid> VansTerrainAsset::Dependencies() const
{
	std::vector<VansAssetGuid> dependencies;
	dependencies.reserve(3 + layers.size() * 3);
	const auto append = [&dependencies](VansAssetGuid guid)
	{
		if (guid.IsValid() && std::find(dependencies.begin(), dependencies.end(), guid) == dependencies.end())
			dependencies.push_back(guid);
	};
	append(heightmap);
	append(splatmaps[0]);
	append(splatmaps[1]);
	for (const VansTerrainLayerAsset& layer : layers)
	{
		append(layer.albedo);
		append(layer.normal);
		append(layer.roughness);
	}
	return dependencies;
}

std::vector<std::string> ValidateTerrainAsset(const VansTerrainAsset& asset, bool requirePixelData)
{
	std::vector<std::string> diagnostics;
	if (!asset.heightmap.IsValid() || !asset.splatmaps[0].IsValid() || !asset.splatmaps[1].IsValid())
		diagnostics.emplace_back("Terrain requires one heightmap and exactly two splatmap asset references");
	if (!FinitePositive(asset.settings.terrainSize) || !FinitePositive(asset.settings.maxHeight) ||
		!std::isfinite(asset.settings.heightOffset))
		diagnostics.emplace_back("Terrain dimensions and height range are invalid");
	if (!FinitePositive(asset.settings.lodBaseDistance) || asset.settings.lodRangeRatio < 2.0f ||
		asset.settings.morphStartRatio <= 0.0f || asset.settings.morphStartRatio >= 1.0f)
		diagnostics.emplace_back("Terrain CDLOD settings are invalid");
	if (!FinitePositive(asset.settings.tessellationDistance) ||
		!FinitePositive(asset.settings.maxTessellationLevel) ||
		asset.settings.maxTessellationLevel > 64.0f ||
		!FinitePositive(asset.settings.tessellationTargetPixels))
		diagnostics.emplace_back("Terrain tessellation settings are invalid");
	if (asset.settings.noiseStrength < 0.0f || !FinitePositive(asset.settings.noiseFrequency) ||
		asset.settings.noiseLacunarity < 1.0f || asset.settings.noiseGain <= 0.0f ||
		asset.settings.noiseGain > 1.0f || asset.settings.noiseOctaves < 1 ||
		asset.settings.noiseOctaves > 4 || asset.settings.noiseWarpStrength < 0.0f ||
		asset.settings.noiseFadeStart < 0.0f || asset.settings.noiseFadeStart >= 1.0f)
		diagnostics.emplace_back("Terrain procedural detail settings are invalid");
	if (asset.layers.empty() || asset.layers.size() > VANS_TERRAIN_LAYER_COUNT)
		diagnostics.emplace_back("Terrain requires between one and eight material layers");
	std::unordered_set<std::string> layerIds;
	for (const VansTerrainLayerAsset& layer : asset.layers)
	{
		if (layer.id.empty() || !layerIds.insert(layer.id).second)
			diagnostics.emplace_back("Terrain layer ids must be non-empty and unique");
		if (!layer.albedo.IsValid() || !layer.normal.IsValid() || !layer.roughness.IsValid() ||
			!FinitePositive(layer.tiling))
			diagnostics.emplace_back("Each terrain layer requires complete texture references and positive tiling");
	}
	if (requirePixelData && !asset.HasPixelData())
		diagnostics.emplace_back("Terrain pixel buffers are missing or have inconsistent dimensions");
	return diagnostics;
}

std::uint64_t HashTerrainAssetContent(const VansTerrainAsset& asset)
{
	std::uint64_t hash = 14695981039346656037ull;
	HashBytes(hash, &asset.width, sizeof(asset.width));
	HashBytes(hash, &asset.height, sizeof(asset.height));
	if (!asset.heights.empty())
		HashBytes(hash, asset.heights.data(), asset.heights.size() * sizeof(std::uint16_t));
	for (const auto& splat : asset.splatPixels)
		if (!splat.empty()) HashBytes(hash, splat.data(), splat.size());
	return hash == 0 ? 1 : hash;
}
}
