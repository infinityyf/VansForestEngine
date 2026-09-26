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
    static bool BuildTerrainNode(
        VansScene& scene,
        VansVKDevice& device,
        const Vans::VansSceneTerrainNodeConfig& terrainConfig,
        std::string& error);
    static bool BuildVegetationNode(
        VansScene& scene,
        VkDevice& device,
        const Vans::VansPcgRecipeAsset& vegetationConfig,
        std::string& error);
    static bool BuildWaterNode(
        VansScene& scene,
        VansVKDevice& device,
        const Vans::VansSceneWaterNodeConfig& waterConfig,
        std::shared_ptr<const Vans::VansTerrainAsset> effectiveTerrain,
        std::string& error);
};
}
