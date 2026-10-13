#pragma once
#include "../VansNavigationSourceSnapshot.h"

namespace Vans
{
bool ValidateNavigationHeightfieldInput(const VansNavigationHeightfieldInput& input, std::string& error);
bool CollectNavigationHeightfieldTile(const std::vector<VansNavigationHeightfieldInput>& inputs,
    const glm::vec3& minimum, const glm::vec3& maximum, std::uint64_t maximumBytes,
    VansNavigationGeometry& output, std::string& error);
}
