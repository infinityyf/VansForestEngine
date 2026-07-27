#include "VansSceneEntityFactory.h"

#include "../AssetCore/VansAssetGuid.h"

#include <unordered_set>
#include <utility>

namespace Vans
{
namespace
{
using SerializedField = std::pair<std::string, VansSerializedValue>;

std::string NewGuidString()
{
    return VansAssetGuid::New().ToString();
}

VansSerializedValue Object(std::initializer_list<SerializedField> fields)
{
    return VansSerializedValue::Object(std::vector<SerializedField>(fields));
}

VansSerializedValue String(const std::string& value)
{
    return VansSerializedValue::String(value);
}

VansSerializedValue Int(std::int64_t value)
{
    return VansSerializedValue::Int(value);
}

VansSerializedValue Bool(bool value)
{
    return VansSerializedValue::Bool(value);
}

VansSerializedValue FloatArray(
    float x,
    float y,
    float z)
{
    return VansSerializedValue::Array({
        VansSerializedValue::Float(x),
        VansSerializedValue::Float(y),
        VansSerializedValue::Float(z)
    });
}

VansSerializedValue FloatArray(
    float x,
    float y,
    float z,
    float w)
{
    return VansSerializedValue::Array({
        VansSerializedValue::Float(x),
        VansSerializedValue::Float(y),
        VansSerializedValue::Float(z),
        VansSerializedValue::Float(w)
    });
}

VansSerializedValue GuidReference(const std::string& guid)
{
    return Object({ { "guid", String(guid) } });
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

VansSerializedValue BuildMaterialOverrides(const std::string& materialGuid)
{
    if (materialGuid.empty())
        return VansSerializedValue::Object({});
    return Object({ { "default", GuidReference(materialGuid) } });
}

VansSerializedValue BuildTransformComponent(
    const std::array<float, 3>& position,
    const std::array<float, 4>& rotation,
    const std::array<float, 3>& scale)
{
    return Object({
        { "id", String(NewGuidString()) },
        { "type", String("Transform") },
        { "version", Int(1) },
        { "enabled", Bool(true) },
        { "data", Object({
            { "position", FloatArray(position[0], position[1], position[2]) },
            { "rotation", FloatArray(rotation[0], rotation[1], rotation[2], rotation[3]) },
            { "scale", FloatArray(scale[0], scale[1], scale[2]) }
        }) }
    });
}

VansSerializedValue BuildModelRendererComponent(VansSerializedValue data)
{
    return Object({
        { "id", String(NewGuidString()) },
        { "type", String("ModelRenderer") },
        { "version", Int(1) },
        { "enabled", Bool(true) },
        { "data", std::move(data) }
    });
}

void AppendSceneEntity(
    SceneModelEntityFactoryResult& result,
    VansSerializedValue entity)
{
    result.entities.push_back(std::move(entity));
}
}

SceneModelEntityFactoryResult VansSceneEntityFactory::BuildSingleModelEntity(
    const SceneModelEntityFactoryRequest& request,
    const std::string& entityId)
{
    SceneModelEntityFactoryResult result;
    result.rootEntityId = entityId.empty() ? NewGuidString() : entityId;

    VansSerializedValue modelData = Object({
        { "model", GuidReference(request.modelGuid) },
        { "castShadows", Bool(true) },
        { "receiveShadows", Bool(true) },
        { "rayTracingMode", String("auto") },
        { "visibilityMask", Int(0xffffffffll) },
        { "shadowCasterMask", Int(0xffffffffll) },
        { "materialOverrides", BuildMaterialOverrides(request.defaultMaterialGuid) },
        { "orphanOverrides", VansSerializedValue::Object({}) }
    });

    VansSerializedValue entity = Object({
        { "id", String(result.rootEntityId) },
        { "name", String(request.entityName) },
        { "parent", VansSerializedValue::Null() },
        { "components", VansSerializedValue::Array({
        BuildTransformComponent(request.position, request.rotation, request.scale),
        BuildModelRendererComponent(std::move(modelData))
        }) }
    });

    AppendSceneEntity(result, std::move(entity));
    return result;
}

SceneModelEntityFactoryResult VansSceneEntityFactory::BuildMultiMeshEntityHierarchy(
    const SceneModelEntityFactoryRequest& request,
    const std::string& rootEntityId)
{
    SceneModelEntityFactoryResult result;
    result.rootEntityId = rootEntityId.empty() ? NewGuidString() : rootEntityId;

    VansSerializedValue rootComp = Object({
        { "id", String(NewGuidString()) },
        { "type", String("MultiMeshRoot") },
        { "version", Int(1) },
        { "enabled", Bool(true) },
        { "data", Object({
            { "model", GuidReference(request.modelGuid) },
            { "submeshCount", Int(static_cast<std::int64_t>(request.submeshes.size())) },
            { "generation", String("object-hierarchy") }
        }) }
    });

    VansSerializedValue parentEntity = Object({
        { "id", String(result.rootEntityId) },
        { "name", String(request.entityName) },
        { "parent", VansSerializedValue::Null() },
        { "components", VansSerializedValue::Array({
        BuildTransformComponent(request.position, request.rotation, request.scale),
        std::move(rootComp)
        }) }
    });
    AppendSceneEntity(result, std::move(parentEntity));

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

        VansSerializedValue modelData = Object({
            { "model", GuidReference(request.modelGuid) },
            { "submesh", Object({
                { "index", Int(index) },
                { "sourceNode", String(submesh.sourceNodeName) },
                { "sourceMaterial", String(submesh.materialName) },
                { "slotName", String(slotName) }
            }) },
            { "castShadows", Bool(true) },
            { "receiveShadows", Bool(true) },
            { "rayTracingMode", String("auto") },
            { "visibilityMask", Int(0xffffffffll) },
            { "shadowCasterMask", Int(0xffffffffll) },
            { "materialOverrides", BuildMaterialOverrides(submesh.materialGuid) },
            { "orphanOverrides", VansSerializedValue::Object({}) }
        });

        const std::string childName = request.entityName + "_" +
            (!submesh.sourceNodeName.empty()
                ? submesh.sourceNodeName
                : ("Submesh_" + std::to_string(index)));

        VansSerializedValue childEntity = Object({
            { "id", String(NewGuidString()) },
            { "name", String(childName) },
            { "parent", String(result.rootEntityId) },
            { "components", VansSerializedValue::Array({
            BuildTransformComponent({ 0.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f }),
            BuildModelRendererComponent(std::move(modelData))
            }) }
        });
        AppendSceneEntity(result, std::move(childEntity));
    }

    return result;
}
}
