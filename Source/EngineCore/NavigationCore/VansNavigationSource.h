#pragma once

#include "VansNavigationTypes.h"

#include <cstdint>
#include <string>

namespace Vans
{
struct VansNavigationSource
{
	std::string scene;
	std::uint64_t inputHash = 0;

	bool IsValid() const { return !scene.empty(); }
};

std::uint64_t HashNavigationBakeSettings(const VansNavigationBakeSettings& settings);
bool ValidateNavigationSourceIdentity(const std::string& scene, std::string& error);
bool ComputeNavigationGeometryHash(const VansNavigationGeometry& geometry,
	std::uint64_t& hash, std::string& error);
}
