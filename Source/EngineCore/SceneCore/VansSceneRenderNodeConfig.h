#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansSceneTransformConfig
{
	std::array<float, 3> position{ 0.0f, 0.0f, 0.0f };
	std::array<float, 3> rotation{ 0.0f, 0.0f, 0.0f };
	std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
};

struct VansSceneRenderNodeConfig
{
	std::string name;
	std::string type;
	std::string mesh;
	std::string material;
	std::string entityGuid;
	std::string parentEntityGuid;
	std::string parent;
	std::string submeshSlotName;
	std::string rayTracingMode = "auto";
	std::optional<uint32_t> submesh;
	std::optional<VansSceneTransformConfig> transform;
	bool supportShadow = false;
	uint32_t shadowCasterMask = 0xffffffffu;
	std::unordered_map<std::string, std::string> submeshMaterialOverrides;
};

using VansSceneRenderNodeConfigs = std::vector<VansSceneRenderNodeConfig>;
}
