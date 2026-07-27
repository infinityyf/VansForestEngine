#pragma once

#include "VansSceneEnvironmentNodeConfig.h"

#include <string>

namespace Vans
{
struct VansSerializedValue;

class VansSceneEnvironmentNodeConfigReader
{
public:
	static VansSceneTerrainNodeConfig ReadTerrain(const VansSerializedValue& terrainNode);
	static VansSceneWaterNodeConfig ReadWater(const VansSerializedValue& waterNode);
	static VansSceneVegetationNodeConfig ReadVegetation(
		const VansSerializedValue& vegetationNode,
		const std::string& projectRoot);
};
}
