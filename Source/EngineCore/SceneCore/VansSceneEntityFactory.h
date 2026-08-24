#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansSceneParentReference.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct SceneSubmeshEntityDescriptor
{
    std::string sourceNodeName;
    std::string materialName;
    std::string materialGuid;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

struct SceneEmptyEntityFactoryRequest
{
    std::string entityName = "Empty Object";
    std::optional<VansSceneParentReference> parent;
    std::string transformComponentGuid;
    std::array<float, 3> position = { 0.0f, 0.0f, 0.0f };
    std::array<float, 4> rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> scale = { 1.0f, 1.0f, 1.0f };
};

struct SceneModelEntityFactoryRequest
{
    std::string entityName;
    std::string modelGuid;
    std::string defaultMaterialGuid;
    std::string transformComponentGuid;
    std::string modelRendererComponentGuid;
    std::array<float, 3> position = { 0.0f, 0.0f, 0.0f };
    std::array<float, 4> rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> scale = { 1.0f, 1.0f, 1.0f };
    std::vector<SceneSubmeshEntityDescriptor> submeshes;
};

struct SceneModelEntityFactoryResult
{
    std::string rootEntityId;
    std::vector<VansSerializedValue> entities;
};

class VansSceneEntityFactory
{
public:
    static VansSerializedValue BuildEmptyEntity(
        const SceneEmptyEntityFactoryRequest& request,
        const std::string& entityId = {});

    static SceneModelEntityFactoryResult BuildSingleModelEntity(
        const SceneModelEntityFactoryRequest& request,
        const std::string& entityId = {});

    static SceneModelEntityFactoryResult BuildMultiMeshEntityHierarchy(
        const SceneModelEntityFactoryRequest& request,
        const std::string& rootEntityId = {});
};
}
