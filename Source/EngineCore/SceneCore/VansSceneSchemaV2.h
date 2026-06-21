#pragma once

#include "../AssetCore/VansAssetGuid.h"
#include "VansSceneDiagnostics.h"

#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
constexpr std::uint32_t VansSceneSchemaVersion = 2;

struct VansSceneTransform
{
    std::array<float, 3> position{ 0.0f, 0.0f, 0.0f };
    std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
};

struct VansSceneComponentData
{
    VansComponentGuid id;
    std::string type;
    std::uint32_t version = 1;
    bool enabled = true;
    nlohmann::ordered_json data = nlohmann::ordered_json::object();
};

struct VansSceneEntityData
{
    VansEntityGuid id;
    std::string name;
    std::optional<VansEntityGuid> parent;
    std::vector<VansSceneComponentData> components;
};

struct VansSceneData
{
    VansAssetGuid sceneGuid;
    std::vector<VansSceneEntityData> entities;
    nlohmann::ordered_json settings = nlohmann::ordered_json::object();
};

class VansSceneSchemaV2
{
public:
    static SceneDiagnostics Validate(const nlohmann::ordered_json& root);
    static bool Deserialize(const nlohmann::ordered_json& root, VansSceneData& scene, SceneDiagnostics& diagnostics);
    static nlohmann::ordered_json Serialize(const VansSceneData& scene);

    static VansSceneComponentData MakeTransform(const VansSceneTransform& transform = {});
    static VansSceneComponentData MakeModelRenderer(VansAssetGuid model);
};
}
