#include "VansSceneSchema.h"

#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"

#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <unordered_map>

namespace Vans
{
namespace
{
using Json = nlohmann::ordered_json;
using SerializedField = std::pair<std::string, VansSerializedValue>;

void Error(SceneDiagnostics& diagnostics, std::string pointer, std::string message)
{
    diagnostics.push_back({ SceneDiagnosticSeverity::Error, std::move(pointer), std::move(message) });
}

bool ReadGuid(const Json& value, VansAssetGuid& guid)
{
    return value.is_string() && VansAssetGuid::TryParse(value.get<std::string>(), guid);
}

bool IsNonNegativeInteger(const Json& value)
{
    if (value.is_number_unsigned())
        return true;
    if (!value.is_number_integer())
        return false;
    return value.get<std::int64_t>() >= 0;
}

Json GuidJson(const VansAssetGuid& guid)
{
    return guid.ToString();
}

VansSerializedValue SerializedArray(std::initializer_list<VansSerializedValue> items)
{
    return VansSerializedValue::Array(std::vector<VansSerializedValue>(items));
}

VansSerializedValue SerializedObject(std::initializer_list<SerializedField> fields)
{
    return VansSerializedValue::Object(std::vector<SerializedField>(fields));
}
}

SceneDiagnostics VansSceneSchema::ValidateSceneJson(const Json& root)
{
    SceneDiagnostics diagnostics;
    if (!root.is_object())
    {
        Error(diagnostics, "", "Scene root must be an object");
        return diagnostics;
    }
    if (root.value("schemaVersion", 0u) != VansSceneSchemaVersion)
        Error(diagnostics, "/schemaVersion", "Unsupported scene schema version");

    VansAssetGuid sceneGuid;
    if (!root.contains("sceneGuid") || !ReadGuid(root["sceneGuid"], sceneGuid))
        Error(diagnostics, "/sceneGuid", "Scene requires a valid sceneGuid");
    if (!root.contains("entities") || !root["entities"].is_array())
    {
        Error(diagnostics, "/entities", "Scene requires an entities array");
        return diagnostics;
    }

    std::unordered_set<VansEntityGuid> entityIds;
    std::unordered_set<VansComponentGuid> componentIds;
    std::vector<std::pair<std::string, VansEntityGuid>> parents;
    std::unordered_map<VansEntityGuid, VansEntityGuid> parentByEntity;
    for (std::size_t entityIndex = 0; entityIndex < root["entities"].size(); ++entityIndex)
    {
        const Json& entity = root["entities"][entityIndex];
        const std::string pointer = "/entities/" + std::to_string(entityIndex);
        if (!entity.is_object())
        {
            Error(diagnostics, pointer, "Entity must be an object");
            continue;
        }
        VansEntityGuid entityId;
        if (!entity.contains("id") || !ReadGuid(entity["id"], entityId))
            Error(diagnostics, pointer + "/id", "Entity requires a valid id");
        else if (!entityIds.insert(entityId).second)
            Error(diagnostics, pointer + "/id", "Entity id must be unique");

        if (!entity.contains("name") || !entity["name"].is_string())
            Error(diagnostics, pointer + "/name", "Entity requires a string name");
        if (!entity.contains("parent"))
            Error(diagnostics, pointer + "/parent", "Entity requires a parent field");
        else if (!entity["parent"].is_null())
        {
            VansEntityGuid parent;
            if (!ReadGuid(entity["parent"], parent))
                Error(diagnostics, pointer + "/parent", "Entity parent must be null or a valid id");
            else
            {
                parents.emplace_back(pointer + "/parent", parent);
                if (entityId.IsValid())
                    parentByEntity[entityId] = parent;
            }
        }
        if (!entity.contains("components") || !entity["components"].is_array())
        {
            Error(diagnostics, pointer + "/components", "Entity requires a components array");
            continue;
        }

        std::unordered_set<std::string> singletonTypes;
        for (std::size_t componentIndex = 0; componentIndex < entity["components"].size(); ++componentIndex)
        {
            const Json& component = entity["components"][componentIndex];
            const std::string componentPointer = pointer + "/components/" + std::to_string(componentIndex);
            if (!component.is_object())
            {
                Error(diagnostics, componentPointer, "Component must be an object");
                continue;
            }
            VansComponentGuid componentId;
            if (!component.contains("id") || !ReadGuid(component["id"], componentId))
                Error(diagnostics, componentPointer + "/id", "Component requires a valid id");
            else if (!componentIds.insert(componentId).second)
                Error(diagnostics, componentPointer + "/id", "Component id must be unique across the scene");
            const std::string type = component.value("type", "");
            if (type.empty())
                Error(diagnostics, componentPointer + "/type", "Component requires a type");
            if (!component.contains("version") || !IsNonNegativeInteger(component["version"]))
                Error(diagnostics, componentPointer + "/version", "Component requires an unsigned version");
            if (!component.contains("enabled") || !component["enabled"].is_boolean())
                Error(diagnostics, componentPointer + "/enabled", "Component requires enabled state");
            if (!component.contains("data") || !component["data"].is_object())
                Error(diagnostics, componentPointer + "/data", "Component data must be an object");
            if (type == "ModelRenderer" && component.contains("data") && component["data"].is_object())
            {
                VansAssetGuid model;
                const auto& data = component["data"];
                if (!data.contains("model") || !data["model"].is_object() || !data["model"].contains("guid") ||
                    !ReadGuid(data["model"]["guid"], model))
                    Error(diagnostics, componentPointer + "/data/model/guid", "ModelRenderer requires a valid model asset guid");
                if (data.contains("submesh"))
                {
                    const Json& submesh = data["submesh"];
                    if (!submesh.is_object())
                    {
                        Error(diagnostics, componentPointer + "/data/submesh", "ModelRenderer submesh must be an object");
                    }
                    else
                    {
                        if (!submesh.contains("index") || !IsNonNegativeInteger(submesh["index"]))
                            Error(diagnostics, componentPointer + "/data/submesh/index", "ModelRenderer submesh.index must be an unsigned integer");
                        const char* stringFields[] = { "sourceNode", "sourceMaterial", "slotName" };
                        for (const char* field : stringFields)
                        {
                            if (submesh.contains(field) && !submesh[field].is_string())
                                Error(diagnostics, componentPointer + "/data/submesh/" + std::string(field), "ModelRenderer submesh field must be a string");
                        }
                    }
                }
            }
            if (type == "MultiMeshRoot" && component.contains("data") && component["data"].is_object())
            {
                VansAssetGuid model;
                const auto& data = component["data"];
                if (!data.contains("model") || !data["model"].is_object() || !data["model"].contains("guid") ||
                    !ReadGuid(data["model"]["guid"], model))
                    Error(diagnostics, componentPointer + "/data/model/guid", "MultiMeshRoot requires a valid model asset guid");
                if (data.contains("submeshCount") && !IsNonNegativeInteger(data["submeshCount"]))
                    Error(diagnostics, componentPointer + "/data/submeshCount", "MultiMeshRoot submeshCount must be an unsigned integer");
            }
            if ((type == "Transform" || type == "ModelRenderer" || type == "Physics" || type == "MultiMeshRoot") && !singletonTypes.insert(type).second)
                Error(diagnostics, componentPointer + "/type", type + " is a singleton component");
        }
        if (singletonTypes.find("Transform") == singletonTypes.end())
            Error(diagnostics, pointer + "/components", "Every entity requires a Transform component");
    }
    for (const auto& [pointer, parent] : parents)
        if (entityIds.find(parent) == entityIds.end())
            Error(diagnostics, pointer, "Entity parent does not exist");
    for (const VansEntityGuid& entity : entityIds)
    {
        std::unordered_set<VansEntityGuid> chain;
        VansEntityGuid cursor = entity;
        for (;;)
        {
            const auto parent = parentByEntity.find(cursor);
            if (parent == parentByEntity.end())
                break;
            if (!chain.insert(cursor).second || parent->second == entity)
            {
                Error(diagnostics, "/entities", "Entity hierarchy contains a cycle");
                break;
            }
            cursor = parent->second;
        }
    }
    return diagnostics;
}

bool VansSceneSchema::DeserializeSceneJson(const Json& root, VansSceneData& scene, SceneDiagnostics& diagnostics)
{
    diagnostics = ValidateSceneJson(root);
    if (!diagnostics.empty())
        return false;

    scene = {};
    ReadGuid(root["sceneGuid"], scene.sceneGuid);
    scene.settings = DecodeSerializedValueJson(root.value("settings", Json::object()));
    for (const Json& entityJson : root["entities"])
    {
        VansSceneEntityData entity;
        ReadGuid(entityJson["id"], entity.id);
        entity.name = entityJson["name"].get<std::string>();
        if (!entityJson["parent"].is_null())
        {
            VansEntityGuid parent;
            ReadGuid(entityJson["parent"], parent);
            entity.parent = parent;
        }
        for (const Json& componentJson : entityJson["components"])
        {
            VansSceneComponentData component;
            ReadGuid(componentJson["id"], component.id);
            component.type = componentJson["type"].get<std::string>();
            component.version = componentJson["version"].get<std::uint32_t>();
            component.enabled = componentJson["enabled"].get<bool>();
            component.data = DecodeSerializedValueJson(componentJson["data"]);
            entity.components.push_back(std::move(component));
        }
        scene.entities.push_back(std::move(entity));
    }
    return true;
}

Json VansSceneSchema::SerializeSceneJson(const VansSceneData& scene)
{
    Json root = {
        { "schemaVersion", VansSceneSchemaVersion },
        { "sceneGuid", GuidJson(scene.sceneGuid) },
        { "entities", Json::array() },
        { "settings", EncodeSerializedValueJson<Json>(scene.settings) }
    };
    for (const VansSceneEntityData& entity : scene.entities)
    {
        Json entityJson = {
            { "id", GuidJson(entity.id) },
            { "name", entity.name },
            { "parent", entity.parent ? GuidJson(*entity.parent) : Json(nullptr) },
            { "components", Json::array() }
        };
        for (const VansSceneComponentData& component : entity.components)
        {
            entityJson["components"].push_back({
                { "id", GuidJson(component.id) },
                { "type", component.type },
                { "version", component.version },
                { "enabled", component.enabled },
                { "data", EncodeSerializedValueJson<Json>(component.data) }
            });
        }
        root["entities"].push_back(std::move(entityJson));
    }
    return root;
}

VansSceneComponentData VansSceneSchema::MakeTransform(const VansSceneTransform& transform)
{
    VansSceneComponentData result;
    result.id = VansComponentGuid::New();
    result.type = "Transform";
    result.data = SerializedObject({
        { "position", SerializedArray({
            VansSerializedValue::Float(transform.position[0]),
            VansSerializedValue::Float(transform.position[1]),
            VansSerializedValue::Float(transform.position[2])
        }) },
        { "rotation", SerializedArray({
            VansSerializedValue::Float(transform.rotation[0]),
            VansSerializedValue::Float(transform.rotation[1]),
            VansSerializedValue::Float(transform.rotation[2]),
            VansSerializedValue::Float(transform.rotation[3])
        }) },
        { "scale", SerializedArray({
            VansSerializedValue::Float(transform.scale[0]),
            VansSerializedValue::Float(transform.scale[1]),
            VansSerializedValue::Float(transform.scale[2])
        }) }
    });
    return result;
}

VansSceneComponentData VansSceneSchema::MakeModelRenderer(VansAssetGuid model)
{
    VansSceneComponentData result;
    result.id = VansComponentGuid::New();
    result.type = "ModelRenderer";
    result.data = SerializedObject({
        { "model", SerializedObject({
            { "guid", VansSerializedValue::String(model.ToString()) }
        }) },
        { "castShadows", VansSerializedValue::Bool(true) },
        { "receiveShadows", VansSerializedValue::Bool(true) },
        { "rayTracingMode", VansSerializedValue::String("auto") },
        { "visibilityMask", VansSerializedValue::Int(0xffffffffll) },
        { "shadowCasterMask", VansSerializedValue::Int(0xffffffffll) },
        { "materialOverrides", VansSerializedValue::Object({}) },
        { "orphanOverrides", VansSerializedValue::Object({}) }
    });
    return result;
}

VansSceneComponentData VansSceneSchema::MakeSubmeshModelRenderer(VansAssetGuid model,
    std::uint32_t submeshIndex,
    const std::string& sourceNode,
    const std::string& sourceMaterial,
    const std::string& slotName)
{
    VansSceneComponentData result = MakeModelRenderer(model);
    result.data = SerializedObject({
        { "model", SerializedObject({
            { "guid", VansSerializedValue::String(model.ToString()) }
        }) },
        { "castShadows", VansSerializedValue::Bool(true) },
        { "receiveShadows", VansSerializedValue::Bool(true) },
        { "rayTracingMode", VansSerializedValue::String("auto") },
        { "visibilityMask", VansSerializedValue::Int(0xffffffffll) },
        { "shadowCasterMask", VansSerializedValue::Int(0xffffffffll) },
        { "materialOverrides", SerializedObject({
            { "default", VansSerializedValue::Object({}) }
        }) },
        { "orphanOverrides", VansSerializedValue::Object({}) },
        { "submesh", SerializedObject({
            { "index", VansSerializedValue::Int(submeshIndex) },
            { "sourceNode", VansSerializedValue::String(sourceNode) },
            { "sourceMaterial", VansSerializedValue::String(sourceMaterial) },
            { "slotName", VansSerializedValue::String(slotName) }
        }) }
    });
    return result;
}

VansSceneComponentData VansSceneSchema::MakeMultiMeshRoot(VansAssetGuid model, std::uint32_t submeshCount)
{
    VansSceneComponentData result;
    result.id = VansComponentGuid::New();
    result.type = "MultiMeshRoot";
    result.data = SerializedObject({
        { "model", SerializedObject({
            { "guid", VansSerializedValue::String(model.ToString()) }
        }) },
        { "submeshCount", VansSerializedValue::Int(submeshCount) },
        { "generation", VansSerializedValue::String("object-hierarchy") }
    });
    return result;
}
}
