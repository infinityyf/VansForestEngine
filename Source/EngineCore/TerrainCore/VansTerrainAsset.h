#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/VansAssetGuid.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
constexpr std::size_t VANS_TERRAIN_LAYER_COUNT = 8;

struct VansTerrainLayerAsset
{
	std::string id;
	std::string name;
	VansAssetGuid albedo;
	VansAssetGuid normal;
	VansAssetGuid roughness;
	float tiling = 64.0f;
};

struct VansTerrainAssetSettings
{
	float terrainSize = 1024.0f;
	float maxHeight = 500.0f;
	float heightOffset = -23.0f;
	float lodBaseDistance = 64.0f;
	float lodRangeRatio = 2.0f;
	float morphStartRatio = 0.70f;
	bool tessellationEnabled = true;
	float tessellationDistance = 300.0f;
	float maxTessellationLevel = 32.0f;
	float tessellationTargetPixels = 12.0f;
	bool noiseDetailEnabled = true;
	float noiseStrength = 0.03f;
	float noiseFrequency = 0.8f;
	float noiseLacunarity = 2.0f;
	float noiseGain = 0.52f;
	int noiseOctaves = 4;
	float noiseWarpStrength = 0.0f;
	float noiseFadeStart = 0.70f;
};

// 地形资产是编辑、运行时渲染和 Play 初始化共享的不可变数据快照。
// 高度和权重始终保留原始量化值，避免编辑和保存之间发生精度往返。
struct VansTerrainAsset
{
	std::filesystem::path sourcePath;
	VansSerializedValue sourceRoot;
	VansAssetGuid heightmap;
	std::array<VansAssetGuid, 2> splatmaps{};
	VansTerrainAssetSettings settings;
	std::vector<VansTerrainLayerAsset> layers;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<std::uint16_t> heights;
	std::array<std::vector<std::uint8_t>, 2> splatPixels;

	bool HasPixelData() const;
	std::vector<VansAssetGuid> Dependencies() const;
};

std::vector<std::string> ValidateTerrainAsset(const VansTerrainAsset& asset, bool requirePixelData);
std::uint64_t HashTerrainAssetContent(const VansTerrainAsset& asset);
}
