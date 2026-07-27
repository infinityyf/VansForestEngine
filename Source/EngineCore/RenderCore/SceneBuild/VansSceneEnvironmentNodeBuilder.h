#pragma once

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
    static void AddVegetationNode(VansScene& scene, VkDevice& device, const Vans::VansSceneVegetationNodeConfig& vegetationConfig, const std::string& projectRoot);
};
}
