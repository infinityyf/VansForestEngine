#pragma once

#include <cmath>
#include <limits>

namespace Vans
{
// IEEE-754 double 在该范围内仍能逐整数精确表示；cell key 不得静默丢失相邻格。
inline constexpr double MaximumExactPcgCellCoordinate = 4503599627370495.0;

// Grass 进入距离仍由 Plant cullDistance 决定，退出时多保留一个完整 cell 防抖。
inline constexpr float PcgGrassResidencyHysteresisCells = 1.0f;

inline bool ResolvePcgGrassResidencyExitDistance(float cullDistance, float cellSize, float& distance)
{
	if (!std::isfinite(cullDistance) || cullDistance < 0 ||
		!std::isfinite(cellSize) || cellSize <= 0)
		return false;
	const double resolved = static_cast<double>(cullDistance) +
		static_cast<double>(cellSize) * PcgGrassResidencyHysteresisCells;
	if (!std::isfinite(resolved) || resolved > static_cast<double>((std::numeric_limits<float>::max)()))
		return false;
	distance = static_cast<float>(resolved);
	return true;
}
}
