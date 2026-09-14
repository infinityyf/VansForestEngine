#pragma once

#include "../../PcgCore/VansPcgRecipeAsset.h"

#include "../../SceneCore/VansSceneEnvironmentNodeConfig.h"
#include <vulkan/vulkan.h>

#include <string>

namespace VansGraphics
{
class VansScene;
class VansVKDevice;

class VansSceneEnvironmentNodeBuilder
{
public:
    static void AddTerrainNode(VansScene& scene, VansVKDevice* device, const Vans::VansSceneTerrainNodeConfig& terrainConfig);
    static void AddWaterNode(VansScene& scene, VkDevice& device, const Vans::VansSceneWaterNodeConfig& waterConfig);
    static bool AddVegetationNode(VansScene& scene, VkDevice& device, const Vans::VansPcgRecipeAsset& vegetationConfig);
};
}
