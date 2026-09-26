#pragma once

#include "VansPcgPointGenerator.h"
#include "VansPlantTypeAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vans
{
enum class VansPcgVariantInfluence
{
	AllConfigured,
	PositiveWeight
};

inline double PcgInfluenceRadius(const VansPcgPlacementSettings& placement,
	const std::vector<VansPcgVariantChoice>& variants, VansPcgVariantInfluence selection)
{
	double radius = 0;
	const double maximumHorizontalScale = std::max(placement.scaleMax[0], placement.scaleMax[2]);
	for (const auto& variant : variants)
	{
		if (selection == VansPcgVariantInfluence::PositiveWeight && variant.weight <= 0)
			continue;
		radius = std::max(radius, static_cast<double>(variant.footprintRadius) * maximumHorizontalScale);
	}
	return radius;
}

inline double PcgInfluenceRadius(const VansPcgPlacementSettings& placement,
	const VansPlantTypeAsset& plant)
{
	double radius = 0;
	const double maximumHorizontalScale = std::max(placement.scaleMax[0], placement.scaleMax[2]);
	for (const auto& variant : plant.variants)
		radius = std::max(radius, static_cast<double>(variant.footprintRadius) * maximumHorizontalScale);
	return radius;
}

inline double PcgSpacingHalo(const VansPcgPlacementSettings& placement, double influenceRadius)
{
	return std::max(static_cast<double>(placement.minimumSpacing), influenceRadius * 2);
}

inline double PcgMaskHalo(const VansPcgPlacementSettings& placement, const VansPlantTypeAsset& plant)
{
	return PcgSpacingHalo(placement, PcgInfluenceRadius(placement, plant));
}

inline double PcgGenerationHalo(const VansPcgPlacementSettings& placement, double influenceRadius)
{
	return PcgSpacingHalo(placement, influenceRadius) +
		std::abs(static_cast<double>(placement.rootOffset)) * 2;
}

inline float PcgSplineFieldChangeHalo(float fieldTexelSize, float worldSize, std::uint32_t terrainWidth)
{
	return std::max(fieldTexelSize, worldSize / terrainWidth);
}
}
