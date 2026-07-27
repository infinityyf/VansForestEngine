#pragma once

#include "../RenderCore/PcgCore/VansPcgMask.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
	class VansSceneDocument;
}

namespace VansGraphics
{
	struct PcgVegetationDebugEntry
	{
		std::string label;
		std::string sourcePath;
		std::string jsonPath;
		uint32_t instanceCount = 0;
		bool hasPlacement = false;
		bool hasRandomTrees = false;
		glm::vec2 placementMinXZ = glm::vec2(-100.0f);
		glm::vec2 placementMaxXZ = glm::vec2(100.0f);
		std::string grassMaskRef;
		std::string treeMaskRef;
		std::vector<PcgPlacementMask> configuredMasks;
		PcgPlacementMask grassMask;
		PcgPlacementMask treeMask;
	};

	class VansPcgDebugDataService
	{
	public:
		static std::vector<PcgVegetationDebugEntry> Collect(
			const std::string& projectRootPath,
			const Vans::VansSceneDocument* document);
	};
}
