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

struct VansTerrainPuddleSettings
{
	float scaleMeters = 18.0f;
	float detailScale = 3.0f;
	float threshold = 0.58f;
	float softness = 0.12f;
	float strength = 1.0f;
	float seed = 0.0f;
};

struct VansTerrainWetSurfaceSettings
{
	float albedoScale = 0.72f;
	float roughness = 0.18f;
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
	bool heightDetailEnabled = true;
	float heightDetailStrength = 0.03f;
	float heightDetailFadeStart = 0.70f;
	VansTerrainWetSurfaceSettings wetSurface;
	VansTerrainPuddleSettings puddle;
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
