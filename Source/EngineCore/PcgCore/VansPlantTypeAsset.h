#pragma once

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/VansModelLod.h"
#include "VansPcgConfigurationField.h"

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

struct VansPlantTreeRuntimeBounds
{
	std::array<float, 3> center{};
	float radius = 0;
};

bool ResolvePlantTreeRuntimeBounds(
	const VansPlantVariant& variant,
	VansPlantTreeRuntimeBounds& result,
	std::string& error);

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
	std::vector<float> lodDistances{60.f, 180.f};
	float lodHysteresis = 0.1f;

};

inline const std::array<VansPcgConfigurationFieldDescriptor<VansPlantGrassSettings>, 21> VansPlantGrassConfigurationFields{
	VansPcgConfigurationFieldDescriptor<VansPlantGrassSettings>{ "boneCount", "Bones", "", &VansPlantGrassSettings::boneCount, .01f, 0, 64, true, true, false, 0, 0, 0 },
	{ "subBladeCount", "Blades per instance", "", &VansPlantGrassSettings::subBladeCount, .01f, 1, 32, true, true, false, 0, 0, 1 },
	{ "scatterSeed", "Sub-blade scatter seed", "", &VansPlantGrassSettings::scatterSeed, .01f, 0, 0, false, false, false, 0, 0, 7 },
	{ "windDirection", "Wind direction", "", &VansPlantGrassSettings::windDirection, .01f, 0, 0, false, false, false, 0, 0, 2 },
	{ "bladeHeight", "Blade height (m)", "", &VansPlantGrassSettings::bladeHeight, .01f, 0, 0, true, false, false, 0, 0, 3 },
	{ "leanDeviation", "Lean deviation (degrees)", "", &VansPlantGrassSettings::leanDeviation, .01f, 0, 0, true, false, false, 0, 0, 4 },
	{ "restTipBendDegrees", "Rest tip bend (degrees)", "", &VansPlantGrassSettings::restTipBendDegrees, .01f, 0, 90, true, true, false, 0, 0, 5 },
	{ "restRootBendDegrees", "Rest root bend (degrees)", "", &VansPlantGrassSettings::restRootBendDegrees, .01f, 0, 90, true, true, false, 0, 0, 6 },
	{ "scatterRadiusMin", "Scatter radius minimum (m)", "", &VansPlantGrassSettings::scatterRadiusMin, .01f, 0, 0, true, false, false, 0, 0, 8 },
	{ "scatterRadiusMax", "Scatter radius maximum (m)", "", &VansPlantGrassSettings::scatterRadiusMax, .01f, 0, 0, true, false, false, 0, 0, 9 },
	{ "windStrength", "Wind strength", "", &VansPlantGrassSettings::windStrength, .01f, 0, 0, true, false, false, 0, 0, 10 },
	{ "windFrequency", "Wind frequency", "", &VansPlantGrassSettings::windFrequency, .01f, 0, 0, true, false, false, 0, 0, 11 },
	{ "windSpeed", "Wind speed", "", &VansPlantGrassSettings::windSpeed, .01f, 0, 0, true, false, false, 0, 0, 12 },
	{ "windBendMultiplier", "Wind bend multiplier", "", &VansPlantGrassSettings::windBendMultiplier, .01f, 0, 0, true, false, false, 0, 0, 13 },
	{ "stiffness", "Stiffness", "", &VansPlantGrassSettings::stiffness, .01f, 0, 0, true, false, false, 0, 0, 14 },
	{ "damping", "Damping", "", &VansPlantGrassSettings::damping, .01f, 0, 1, true, true, false, 0, 0, 15 },
	{ "softness", "Softness", "", &VansPlantGrassSettings::softness, .01f, 0, 1, true, true, false, 0, 0, 16 },
	{ "simulationFullDistance", "Full simulation distance", "", &VansPlantGrassSettings::simulationFullDistance, .01f, 0, 0, true, false, false, 0, 0, 17 },
	{ "simulationFadeDistance", "Simulation fade distance", "", &VansPlantGrassSettings::simulationFadeDistance, .01f, 0, 0, true, false, false, 0, 0, 18 },
	{ "subBladeLodMidDistance", "Sub-blade middle LOD distance", "", &VansPlantGrassSettings::subBladeLodMidDistance, .01f, 0, 0, true, false, false, 0, 0, 19 },
	{ "subBladeLodFarDistance", "Sub-blade far LOD distance", "", &VansPlantGrassSettings::subBladeLodFarDistance, .01f, 0, 0, true, false, false, 0, 0, 20 }
};

inline const std::array<VansPcgConfigurationFieldDescriptor<VansPlantRenderSettings>, 7> VansPlantRenderConfigurationFields{
	VansPcgConfigurationFieldDescriptor<VansPlantRenderSettings>{ "cullingEnabled", "Culling", "", &VansPlantRenderSettings::cullingEnabled, .01f, 0, 0, false, false, false, 0, 0, 0 },
	{ "hizEnabled", "Hi-Z", "", &VansPlantRenderSettings::hizEnabled, .01f, 0, 0, false, false, false, 0, 0, 4 },
	{ "cullDistance", "Cull distance", "Shadow distance", &VansPlantRenderSettings::cullDistance, .01f, 0, 0, true, false, false, 0, 0, 1 },
	{ "hizBias", "Hi-Z bias", "", &VansPlantRenderSettings::hizBias, .01f, 0, 0, true, false, false, 0, 0, 5 },
	{ "castShadows", "Cast directional shadows (first 2 cascades)", "", &VansPlantRenderSettings::castShadows, .01f, 0, 0, false, false, false, 0, 0, 6 },
	{ "lodDistances", "LOD distance (m)", "", &VansPlantRenderSettings::lodDistances, 1.f, 1, 100000, true, true, true,
		MinimumModelLodLevelCount, MaximumModelLodLevelCount, 2, VansPcgConfigurationFieldPersistence::TreeOnly },
	{ "lodHysteresis", "Tree LOD hysteresis", "", &VansPlantRenderSettings::lodHysteresis, .01f, 0, .3f, true, true, false,
		0, 0, 3, VansPcgConfigurationFieldPersistence::TreeOnly }
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
