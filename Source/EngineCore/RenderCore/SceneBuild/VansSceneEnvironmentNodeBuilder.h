#pragma once

#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

#include <string>

using json = nlohmann::json;

namespace VansGraphics
{
class VansScene;
class VansVKDevice;

class VansSceneEnvironmentNodeBuilder
{
public:
    static void AddTerrainNode(VansScene& scene, VansVKDevice* device, json& terrainData);
    static void AddWaterNode(VansScene& scene, VkDevice& device, json& waterData);
    static void AddVegetationNode(VansScene& scene, VkDevice& device, json& vegetationData, const std::string& projectRoot);
};
}
