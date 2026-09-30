#pragma once

#include "VansNavigationTypes.h"

#include <cstdint>
#include <string>

namespace Vans
{
struct VansNavigationSource
{
	std::string scene;
	std::uint64_t geometryHash = 0;
	std::uint64_t bakeSettingsHash = 0;

	bool IsValid() const { return !scene.empty(); }
	bool MatchesGeometry(std::uint64_t hash) const
	{
		return IsValid() && geometryHash != 0 && geometryHash == hash;
	}
};

std::uint64_t HashNavigationBakeSettings(const VansNavigationBakeSettings& settings);
bool ComputeNavigationGeometryHash(const VansNavigationGeometry& geometry,
	std::uint64_t& hash, std::string& error);
}
