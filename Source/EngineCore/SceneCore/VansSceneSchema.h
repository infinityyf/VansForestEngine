#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/VansAssetGuid.h"
#include "VansSceneDiagnostics.h"
#include "VansSceneJson.h"

#include <array>
#include <cstdint>
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
    VansSerializedValue data = VansSerializedValue::Object({});
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
    VansSerializedValue settings = VansSerializedValue::Object({});
};

class VansSceneSchema
{
public:
    static SceneDiagnostics ValidateLegacyJson(const SceneJson& root);
    static bool DeserializeLegacyJson(const SceneJson& root, VansSceneData& scene, SceneDiagnostics& diagnostics);
    static SceneJson SerializeLegacyJson(const VansSceneData& scene);

    static VansSceneComponentData MakeTransform(const VansSceneTransform& transform = {});
    static VansSceneComponentData MakeModelRenderer(VansAssetGuid model);
    static VansSceneComponentData MakeSubmeshModelRenderer(VansAssetGuid model,
        std::uint32_t submeshIndex,
        const std::string& sourceNode = {},
        const std::string& sourceMaterial = {},
        const std::string& slotName = {});
    static VansSceneComponentData MakeMultiMeshRoot(VansAssetGuid model, std::uint32_t submeshCount);
};
}
