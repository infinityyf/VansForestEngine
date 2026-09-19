#pragma once

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/VansModelLod.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
enum class VansPlantCategory { Grass, Tree };
enum class VansPlantGeometry { Unassigned, Mesh, ProceduralBlade };
enum class VansPlantPartKind { Surface, Trunk, Leaves };

struct VansPlantPart
{
	std::string id;
	VansPlantPartKind kind = VansPlantPartKind::Surface;
	VansAssetGuid mesh;
	std::int32_t submesh = -1;
	VansAssetGuid material;
};

struct VansPlantVariant
{
	std::string id;
	std::string name;
	VansPlantGeometry geometry = VansPlantGeometry::Unassigned;
	float weight = 0;
	float footprintRadius = 0;
	float cullingRadius = 0;
	float bladeWidth = 0;
	std::array<float, 3> offset{};
	std::array<float, 4> rotation{ 0, 0, 0, 1 };
	std::array<float, 3> scale{ 1, 1, 1 };
	std::vector<VansPlantPart> parts;
	VansModelLodSettings lodSettings;
	VansModelLodAsset lod;
};

struct VansPlantGrassSettings
{
	std::uint32_t boneCount = 0;
	std::uint32_t subBladeCount = 1;
	float bladeHeight = 0;
	float leanDeviation = 0;
	float restTipBendDegrees = 0;
	float restRootBendDegrees = 0;
	std::uint32_t scatterSeed = 0;
	float scatterRadiusMin = 0;
	float scatterRadiusMax = 0;
	std::array<float, 2> windDirection{};
	float windStrength = 0;
	float windFrequency = 0;
	float windSpeed = 0;
	float windBendMultiplier = 0;
	float stiffness = 0;
	float damping = 0;
	float softness = 0;
	float simulationFullDistance = 0;
	float simulationFadeDistance = 0;
	float subBladeLodMidDistance = 0;
	float subBladeLodFarDistance = 0;
};

struct VansPlantRenderSettings
{
	bool cullingEnabled = false;
	bool hizEnabled = false;
	float cullDistance = 0;
	float hizBias = 0;
	bool castShadows = false;
	std::array<float, 2> lodDistances{60.f, 180.f};
	float lodHysteresis = 0.1f;

};

// 植物外观是独立用户资产，不包含场景范围、分布点、Mask 或内置植物配方。
struct VansPlantTypeAsset
{
	std::string name;
	VansPlantCategory category = VansPlantCategory::Grass;
	std::vector<VansPlantVariant> variants;
	VansPlantRenderSettings render;
	VansPlantGrassSettings grass;
	std::vector<VansAssetGuid> Dependencies() const;
};

// 草稿可以保存未绑定资源；生成前必须验证 ready，禁止用默认模型/材质补齐。
std::vector<std::string> ValidatePlantTypeAsset(const VansPlantTypeAsset& asset, bool requireReady);
}
