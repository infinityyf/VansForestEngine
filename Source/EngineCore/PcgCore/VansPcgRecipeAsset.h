#pragma once

#include "VansPcgPointGenerator.h"
#include "VansPlantTypeAsset.h"

namespace Vans
{
enum class VansPcgSourceMode { Density, Count, Fixed };
enum class VansPcgSurfaceKind { Unassigned, Plane, Terrain };
enum class VansPcgOverrideKind { Remove, Transform, Lock };

struct VansPcgAuthoredInstance
{
	std::string id;
	std::string variant;
	std::array<float, 3> position{};
	std::array<float, 4> rotation{ 0, 0, 0, 1 };
	std::array<float, 3> scale{ 1, 1, 1 };
};

struct VansPcgInstanceOverride
{
	// 生成点的稳定 64 位身份以 16 个十六进制字符保存，避免 JSON 数字精度损失。
	std::string target;
	VansPcgOverrideKind kind = VansPcgOverrideKind::Remove;
	std::array<float, 3> position{};
	std::array<float, 4> rotation{ 0, 0, 0, 1 };
	std::array<float, 3> scale{ 1, 1, 1 };
	// Lock 保存显式变体及变换，即使候选点被 Mask/密度移除也保留此株。
	std::string variant;
};

struct VansPcgLayer
{
	std::string id;
	std::string name;
	VansPlantCategory category = VansPlantCategory::Grass;
	bool enabled = false;
	bool locked = false;
	VansAssetGuid plant;
	VansAssetGuid densityMask;
	VansAssetGuid exclusionMask;
	std::uint32_t seed = 0;
	VansPcgSourceMode source = VansPcgSourceMode::Density;
	VansPcgPlacementSettings placement;
	std::uint32_t targetCount = 0;
	VansPcgGenerationBudget budget;
	std::vector<VansPcgAuthoredInstance> fixedInstances;
	std::vector<VansPcgAuthoredInstance> addedInstances;
	std::vector<VansPcgInstanceOverride> overrides;
};

struct VansPcgSurfaceBinding
{
	VansPcgSurfaceKind kind = VansPcgSurfaceKind::Unassigned;
	float planeHeight = 0;
	VansAssetGuid terrain;
};

struct VansPcgRegion
{
	std::string id;
	std::string name;
	bool enabled = false;
	VansPcgBounds bounds;
	VansPcgSurfaceBinding surface;
	std::uint32_t seed = 0;
	float cellSize = 0;
	std::vector<VansPcgLayer> layers;
};

// 唯一的场景植被配方；空配方不创建区域、不绑定地表，也不生成任何植物。
struct VansPcgRecipeAsset
{
	std::string name;
	std::vector<VansPcgRegion> regions;
	std::vector<VansAssetGuid> Dependencies() const;
};

std::vector<std::string> ValidatePcgRecipe(const VansPcgRecipeAsset& recipe, bool requireReady);
}
