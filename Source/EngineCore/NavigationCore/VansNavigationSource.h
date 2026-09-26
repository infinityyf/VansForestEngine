#pragma once

#include "VansNavigationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
struct VansNavigationColliderSource
{
	std::string guid;
	std::uint64_t sourceHash = 0;
	std::uint64_t metaHash = 0;
};

struct VansNavigationSource
{
	std::string scene;
	std::uint64_t sceneHash = 0;
	std::uint64_t colliderHash = 0;
	std::uint64_t settingsHash = 0;

	bool IsValid() const { return !scene.empty(); }
};

std::uint64_t HashNavigationSettings(const VansNavigationSettings& settings);
std::uint64_t HashNavigationColliders(
	std::vector<VansNavigationColliderSource> sources);
}
