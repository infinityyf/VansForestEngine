#include "VansPlantTypeAsset.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
namespace
{
bool ValidLodRatios(const std::vector<float>& values)
{
	if (values.size() < MinimumModelLodLevelCount || values.size() > MaximumModelLodLevelCount)
		return false;
	float previous = 1;
	for (float value : values)
	{
		if (!std::isfinite(value) || value <= 0 || value >= previous) return false;
		previous = value;
	}
	return true;
}

bool ValidLodDistances(const std::vector<float>& values)
{
	if (values.size() < MinimumModelLodLevelCount || values.size() > MaximumModelLodLevelCount)
		return false;
	float previous = 0;
	for (float value : values)
	{
		if (!std::isfinite(value) || value <= previous) return false;
		previous = value;
	}
	return true;
}
}

bool ResolvePlantTreeRuntimeBounds(
	const VansPlantVariant& variant,
	VansPlantTreeRuntimeBounds& result,
	std::string& error)
{
	VansPlantTreeRuntimeBounds candidate;
	if (variant.lod.levels.empty())
	{
		error = "requires baked LOD resources";
		return false;
	}
	for (std::size_t axis = 0; axis < candidate.center.size(); ++axis)
	{
		candidate.center[axis] = variant.lod.centerRadius[axis];
		if (!std::isfinite(candidate.center[axis]))
		{
			error = "has invalid baked LOD bounds";
			return false;
		}
	}
	candidate.radius = variant.lod.centerRadius[3];
	if (!std::isfinite(candidate.radius) || candidate.radius <= 0)
	{
		error = "has invalid baked LOD bounds";
		return false;
	}
	result = candidate;
	error.clear();
	return true;
}

std::vector<VansAssetGuid> VansPlantTypeAsset::Dependencies() const
{
	std::vector<VansAssetGuid> result;
	for (const auto& variant : variants) for (const auto& part : variant.parts)
	{
		if (part.mesh.IsValid()) result.push_back(part.mesh);
		if (part.material.IsValid()) result.push_back(part.material);
	}
    for (const auto& variant:variants) for(const auto& level:variant.lod.levels) for(const auto& part:level.parts) {
        if(part.model.IsValid())result.push_back(part.model);
        if(part.material.IsValid())result.push_back(part.material);
    }
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

std::vector<std::string> ValidatePlantTypeAsset(const VansPlantTypeAsset& asset, bool requireReady)
{
	std::vector<std::string> errors;
	const auto finiteNonnegative = [&](float value, const std::string& field) {
		if (!std::isfinite(value) || value < 0) errors.push_back(field + " must be finite and nonnegative");
	};
	if (asset.category != VansPlantCategory::Grass && asset.category != VansPlantCategory::Tree)
		errors.push_back("Plant category is invalid");
	if (requireReady && asset.name.empty()) errors.push_back("Plant name is required");
	finiteNonnegative(asset.render.cullDistance, "render.cullDistance");
	finiteNonnegative(asset.render.hizBias, "render.hizBias");
	if (asset.render.cullingEnabled && asset.render.cullDistance <= 0)
		errors.push_back("Enabled plant culling requires a positive distance");
    if(asset.category==VansPlantCategory::Tree) {
            if(!ValidLodDistances(asset.render.lodDistances)||!std::isfinite(asset.render.lodHysteresis)||
                asset.render.lodHysteresis<0||asset.render.lodHysteresis>.3f)
                errors.push_back("Tree render has invalid LOD distances or hysteresis");
    }
	std::unordered_set<std::string> variantIds;
	double totalWeight = 0;
	bool procedural = false;
	for (const auto& variant : asset.variants)
	{
		const std::string label = "Plant variant '" + variant.id + "'";
		if (variant.id.empty() || !variantIds.insert(variant.id).second) errors.push_back("Plant variant IDs must be nonempty and unique");
		if (variant.geometry < VansPlantGeometry::Unassigned || variant.geometry > VansPlantGeometry::ProceduralBlade)
			errors.push_back(label + " has invalid geometry mode");
		finiteNonnegative(variant.weight, label + ".weight");
		finiteNonnegative(variant.footprintRadius, label + ".footprintRadius");
		finiteNonnegative(variant.cullingRadius, label + ".cullingRadius");
		finiteNonnegative(variant.bladeWidth, label + ".bladeWidth");
		totalWeight += variant.weight;
		double rotationLength = 0;
		for (float component : variant.rotation) rotationLength += static_cast<double>(component) * component;
		if (!std::isfinite(rotationLength) || std::abs(rotationLength - 1.0) > 0.001)
			errors.push_back(label + " requires a normalized calibration quaternion");
		for (std::size_t axis = 0; axis < 3; ++axis)
			if (!std::isfinite(variant.offset[axis]) || !std::isfinite(variant.scale[axis]) || variant.scale[axis] <= 0)
				errors.push_back(label + " has invalid calibration transform");
		if (requireReady && (variant.geometry == VansPlantGeometry::Unassigned || variant.parts.empty()))
			errors.push_back(label + " needs an explicit geometry choice and material parts");
		if (variant.geometry == VansPlantGeometry::ProceduralBlade)
		{
			procedural = true;
			if (asset.category != VansPlantCategory::Grass) errors.push_back("Procedural blades are only valid for grass");
			if (requireReady && (variant.bladeWidth <= 0 || variant.parts.size() != 1))
				errors.push_back(label + " requires a blade width and exactly one material part");
		}
        if(asset.category==VansPlantCategory::Tree) {
            const auto& settings=variant.lodSettings;
            if(!ValidLodRatios(settings.ratios)||settings.ratios.size()!=asset.render.lodDistances.size()||
                !std::isfinite(settings.maximumError)||settings.maximumError<=0||settings.maximumError>.25f)
                errors.push_back(label+" has invalid LOD build settings");
            VansPlantTreeRuntimeBounds runtimeBounds;
            std::string boundsError;
            if((requireReady && variant.geometry==VansPlantGeometry::Mesh) || !variant.lod.levels.empty())
                if(!ResolvePlantTreeRuntimeBounds(variant,runtimeBounds,boundsError))
                    errors.push_back(label+" "+boundsError);
            if(!variant.lod.levels.empty()) {
                if(variant.lod.levels.size()!=settings.ratios.size()||variant.lod.buildKey.empty())
                    errors.push_back(label+" has incomplete LOD resources");
                for(const auto& level:variant.lod.levels) {
                    if(level.parts.size()!=variant.parts.size())errors.push_back(label+" LOD parts do not match source parts");
                    std::unordered_set<uint32_t> sources;
                    for(const auto& part:level.parts) if(!part.model.IsValid()||!part.material.IsValid()||part.submesh<0||part.sourcePart>=variant.parts.size()||
                        !sources.insert(part.sourcePart).second||part.triangleCount==0||!std::isfinite(part.error)||part.error<0)
                        errors.push_back(label+" has invalid LOD part resources");
                }
            }
        }
		std::unordered_set<std::string> partIds;
		for (const auto& part : variant.parts)
		{
			if (part.id.empty() || !partIds.insert(part.id).second) errors.push_back(label + " part IDs must be nonempty and unique");
			if (part.kind < VansPlantPartKind::Surface || part.kind > VansPlantPartKind::Leaves || part.submesh < -1)
				errors.push_back(label + " has an invalid part");
			if (variant.geometry != VansPlantGeometry::Mesh && part.mesh.IsValid())
				errors.push_back(label + " assigns a mesh outside mesh geometry mode");
			if (requireReady && variant.geometry == VansPlantGeometry::Mesh && !part.mesh.IsValid())
				errors.push_back(label + " is missing a model reference");
			if (requireReady && !part.material.IsValid())
				errors.push_back(label + " is missing an explicit material selection");
			if (variant.geometry == VansPlantGeometry::ProceduralBlade && part.submesh != -1)
				errors.push_back(label + " procedural geometry has no imported submesh");
		}
	}
	if (requireReady && (asset.variants.empty() || totalWeight <= 0 || !std::isfinite(totalWeight)))
		errors.push_back("Plant generation requires user-created variants with positive total weight");
	if (asset.category == VansPlantCategory::Grass)
	{
		const auto& grass = asset.grass;
		if (!std::isfinite(grass.restTipBendDegrees) || !std::isfinite(grass.restRootBendDegrees) ||
			grass.restTipBendDegrees<0 || grass.restTipBendDegrees>90 || grass.restRootBendDegrees<0 || grass.restRootBendDegrees>90)
			errors.push_back("Grass rest bend angles must be in [0, 90] degrees");
		for (const auto& field : std::vector<std::pair<const char*, float>>{
			{ "bladeHeight", grass.bladeHeight }, { "leanDeviation", grass.leanDeviation },
			{ "scatterRadiusMin", grass.scatterRadiusMin }, { "scatterRadiusMax", grass.scatterRadiusMax },
			{ "windStrength", grass.windStrength }, { "windFrequency", grass.windFrequency },
			{ "windSpeed", grass.windSpeed }, { "windBendMultiplier", grass.windBendMultiplier },
			{ "stiffness", grass.stiffness }, { "damping", grass.damping }, { "softness", grass.softness },
			{ "simulationFullDistance", grass.simulationFullDistance }, { "simulationFadeDistance", grass.simulationFadeDistance },
			{ "subBladeLodMidDistance", grass.subBladeLodMidDistance }, { "subBladeLodFarDistance", grass.subBladeLodFarDistance } })
			finiteNonnegative(field.second, std::string("grass.") + field.first);
		if (!std::isfinite(grass.windDirection[0]) || !std::isfinite(grass.windDirection[1])) errors.push_back("Grass wind direction must be finite");
		if (grass.boneCount > 64 || grass.subBladeCount == 0 || grass.subBladeCount > 32)
			errors.push_back("Grass bone/sub-blade counts exceed supported geometry limits");
		if (grass.scatterRadiusMax < grass.scatterRadiusMin || grass.simulationFadeDistance < grass.simulationFullDistance ||
			grass.subBladeLodFarDistance < grass.subBladeLodMidDistance || grass.damping > 1 || grass.softness > 1)
			errors.push_back("Grass ranges or response coefficients are invalid");
		if (requireReady && (grass.boneCount < 2 || grass.bladeHeight <= 0))
			errors.push_back("Grass requires at least two bones and a positive simulation height");
		if (requireReady && procedural && grass.boneCount < 2)
			errors.push_back("Procedural grass requires at least two blade segments");
	}
	return errors;
}
}
