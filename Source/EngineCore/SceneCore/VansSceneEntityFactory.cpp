#include "VansSceneEntityFactory.h"

#include "../AssetCore/VansAssetGuid.h"

#include <unordered_set>

namespace Vans
{
namespace
{
std::string NewGuidString()
{
    return VansAssetGuid::New().ToString();
}

std::string MakeSubmeshSlotName(
    const std::string& nodeName,
    const std::string& materialName,
    std::uint32_t index,
    std::unordered_set<std::string>& usedSlots)
{
    std::string base = (!nodeName.empty() || !materialName.empty())
        ? nodeName + "/" + materialName
        : "Submesh_" + std::to_string(index);
    if (base == "/")
        base = "Submesh_" + std::to_string(index);

    std::string candidate = base;
    std::uint32_t suffix = 1;
    while (!usedSlots.insert(candidate).second)
        candidate = base + "_" + std::to_string(suffix++);
    return candidate;
}

SceneJson BuildMaterialOverrides(const std::string& materialGuid)
{
    if (materialGuid.empty())
        return SceneJson::object();
    return SceneJson{ { "default", SceneJson{ { "guid", materialGuid } } } };
}
}

SceneJson VansSceneEntityFactory::BuildTransformComponent(
    const std::array<float, 3>& position,
    const std::array<float, 4>& rotation,
    const std::array<float, 3>& scale)
{
    SceneJson transformComp;
    transformComp["id"] = NewGuidString();
    transformComp["type"] = "Transform";
    transformComp["version"] = 1u;
    transformComp["enabled"] = true;
    transformComp["data"] = {
        { "position", { position[0], position[1], position[2] } },
        { "rotation", { rotation[0], rotation[1], rotation[2], rotation[3] } },
        { "scale", { scale[0], scale[1], scale[2] } }
    };
    return transformComp;
}

SceneModelEntityFactoryResult VansSceneEntityFactory::BuildSingleModelEntity(
    const SceneModelEntityFactoryRequest& request,
    const std::string& entityId)
{
    SceneModelEntityFactoryResult result;
    result.rootEntityId = entityId.empty() ? NewGuidString() : entityId;

    SceneJson modelData;
    modelData["model"] = { { "guid", request.modelGuid } };
    modelData["materialOverrides"] = BuildMaterialOverrides(request.defaultMaterialGuid);

    SceneJson rendererComp;
    rendererComp["id"] = NewGuidString();
    rendererComp["type"] = "ModelRenderer";
    rendererComp["version"] = 1u;
    rendererComp["enabled"] = true;
    rendererComp["data"] = std::move(modelData);

    SceneJson entity;
    entity["id"] = result.rootEntityId;
    entity["name"] = request.entityName;
    entity["parent"] = nullptr;
    entity["components"] = SceneJson::array({
        BuildTransformComponent(request.position, request.rotation, request.scale),
        std::move(rendererComp)
    });

    result.entities.push_back(std::move(entity));
    return result;
}

SceneModelEntityFactoryResult VansSceneEntityFactory::BuildMultiMeshEntityHierarchy(
    const SceneModelEntityFactoryRequest& request,
    const std::string& rootEntityId)
{
    SceneModelEntityFactoryResult result;
    result.rootEntityId = rootEntityId.empty() ? NewGuidString() : rootEntityId;

    SceneJson rootComp;
    rootComp["id"] = NewGuidString();
    rootComp["type"] = "MultiMeshRoot";
    rootComp["version"] = 1u;
    rootComp["enabled"] = true;
    rootComp["data"] = {
        { "model", { { "guid", request.modelGuid } } },
        { "submeshCount", static_cast<std::uint32_t>(request.submeshes.size()) },
        { "generation", "object-hierarchy" }
    };

    SceneJson parentEntity;
    parentEntity["id"] = result.rootEntityId;
    parentEntity["name"] = request.entityName;
    parentEntity["parent"] = nullptr;
    parentEntity["components"] = SceneJson::array({
        BuildTransformComponent(request.position, request.rotation, request.scale),
        std::move(rootComp)
    });
    result.entities.push_back(std::move(parentEntity));

    std::unordered_set<std::string> usedSlots;
    for (std::uint32_t index = 0; index < request.submeshes.size(); ++index)
    {
        const SceneSubmeshEntityDescriptor& submesh = request.submeshes[index];
        if (submesh.vertexCount == 0 || submesh.indexCount == 0)
            continue;

        const std::string slotName = MakeSubmeshSlotName(
            submesh.sourceNodeName,
            submesh.materialName,
            index,
            usedSlots);

        SceneJson modelData;
        modelData["model"] = { { "guid", request.modelGuid } };
        modelData["submesh"] = {
            { "index", index },
            { "sourceNode", submesh.sourceNodeName },
            { "sourceMaterial", submesh.materialName },
            { "slotName", slotName }
        };
        modelData["castShadows"] = true;
        modelData["receiveShadows"] = true;
        modelData["rayTracingMode"] = "auto";
        modelData["visibilityMask"] = 0xffffffffu;
        modelData["materialOverrides"] = BuildMaterialOverrides(submesh.materialGuid);
        modelData["orphanOverrides"] = SceneJson::object();

        SceneJson rendererComp;
        rendererComp["id"] = NewGuidString();
        rendererComp["type"] = "ModelRenderer";
        rendererComp["version"] = 1u;
        rendererComp["enabled"] = true;
        rendererComp["data"] = std::move(modelData);

        const std::string childName = request.entityName + "_" +
            (!submesh.sourceNodeName.empty()
                ? submesh.sourceNodeName
                : ("Submesh_" + std::to_string(index)));

        SceneJson childEntity;
        childEntity["id"] = NewGuidString();
        childEntity["name"] = childName;
        childEntity["parent"] = result.rootEntityId;
        childEntity["components"] = SceneJson::array({
            BuildTransformComponent({ 0.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f }),
            std::move(rendererComp)
        });
        result.entities.push_back(std::move(childEntity));
    }

    return result;
}
}
