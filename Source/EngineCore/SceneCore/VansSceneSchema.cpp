#include "VansSceneEnvironmentAuthoring.h"
#include "VansComponentTypeCatalog.h"
#include "VansSceneRenderSettingsConfig.h"
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

void ValidateOptionalAssetReferenceMap(
    const Json& data,
    const char* fieldName,
    const std::string& dataPointer,
    SceneDiagnostics& diagnostics)
{
    if (!data.contains(fieldName))
        return;
    const Json& references = data[fieldName];
    const std::string fieldPointer = dataPointer + "/" + fieldName;
    if (!references.is_object())
    {
        Error(diagnostics, fieldPointer, std::string(fieldName) + " must be an object");
        return;
    }
    for (auto reference = references.begin(); reference != references.end(); ++reference)
    {
        const Json& value = reference.value();
        const std::string referencePointer = fieldPointer + "/" + reference.key();
        if (value.is_string())
        {
            if (value.get_ref<const std::string&>().empty())
                continue;
            VansAssetGuid guid;
            if (!ReadGuid(value, guid))
                Error(diagnostics, referencePointer, "Material override must contain a valid asset guid");
            continue;
        }
        if (value.is_object())
        {
            if (value.empty())
                continue;
            if (value.contains("guid") && value["guid"].is_string() &&
                value["guid"].get_ref<const std::string&>().empty())
                continue;
            VansAssetGuid guid;
            if (!value.contains("guid") || !ReadGuid(value["guid"], guid))
                Error(diagnostics, referencePointer + "/guid", "Material override must contain a valid asset guid");
            continue;
        }
        Error(diagnostics, referencePointer, "Material override must be an empty binding or an asset guid reference");
    }
}

void ValidateEntityComponentSemantics(
    const Json& entity,
    std::size_t entityIndex,
    SceneDiagnostics& diagnostics)
{
    const std::string entityPointer = "/entities/" + std::to_string(entityIndex);
    const Json& components = entity["components"];
    bool hasModelRenderer = false;
    bool hasMultiMeshRoot = false;
    for (std::size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex)
    {
        const Json& component = components[componentIndex];
        if (!component.is_object())
            continue;
        const std::string componentPointer =
            entityPointer + "/components/" + std::to_string(componentIndex);
        const std::string type = component.value("type", "");
        if (type.empty())
            continue;
        if (!VansComponentTypeCatalog::IsSceneAuthoringType(type))
        {
            Error(diagnostics, componentPointer + "/type",
                "Unsupported runtime component '" + type + "' on entity '" +
                entity.value("name", "") + "'");
            continue;
        }

        const Json* data = component.contains("data") && component["data"].is_object()
            ? &component["data"] : nullptr;
        if (type == "ModelRenderer")
        {
            hasModelRenderer = true;
            VansAssetGuid model;
            if (!data || !data->contains("model") || !(*data)["model"].is_object() ||
                !(*data)["model"].contains("guid") || !ReadGuid((*data)["model"]["guid"], model))
            {
                Error(diagnostics, componentPointer + "/data/model/guid",
                    "ModelRenderer requires a valid model asset guid");
            }
            if (data)
            {
                ValidateOptionalAssetReferenceMap(
                    *data, "materialOverrides", componentPointer + "/data", diagnostics);
                ValidateOptionalAssetReferenceMap(
                    *data, "submeshMaterialOverrides", componentPointer + "/data", diagnostics);
            }
            continue;
        }
        if (type == "MultiMeshRoot")
        {
            hasMultiMeshRoot = true;
            VansAssetGuid model;
            if (!data || !data->contains("model") || !(*data)["model"].is_object() ||
                !(*data)["model"].contains("guid") || !ReadGuid((*data)["model"]["guid"], model))
            {
                Error(diagnostics, componentPointer + "/data/model/guid",
                    "MultiMeshRoot requires a valid model asset guid");
            }
            if (!data || !data->contains("submeshCount") ||
                !IsNonNegativeInteger((*data)["submeshCount"]) ||
                (*data)["submeshCount"].get<std::uint64_t>() == 0 ||
                (*data)["submeshCount"].get<std::uint64_t>() > UINT32_MAX)
            {
                Error(diagnostics, componentPointer + "/data/submeshCount",
                    "MultiMeshRoot submeshCount must be a positive unsigned integer");
            }
            continue;
        }
        if (type != "LODGroup")
            continue;
        if (!data)
        {
            Error(diagnostics, componentPointer + "/data",
                "LODGroup component data must be an object");
            continue;
        }
        const std::string mode = data->value("mode", "autoScreenError");
        if (mode != "autoScreenError" && mode != "screenRelativeHeight")
        {
            Error(diagnostics, componentPointer + "/data/mode",
                "LODGroup mode must be autoScreenError or screenRelativeHeight");
        }
        if (!data->contains("levels"))
            continue;
        const Json& levels = (*data)["levels"];
        if (!levels.is_array())
        {
            Error(diagnostics, componentPointer + "/data/levels",
                "LODGroup levels must be an array");
            continue;
        }
        for (std::size_t levelIndex = 0; levelIndex < levels.size(); ++levelIndex)
        {
            const Json& level = levels[levelIndex];
            const std::string levelPointer = componentPointer + "/data/levels/" +
                std::to_string(levelIndex);
            if (!level.is_object())
            {
                Error(diagnostics, levelPointer,
                    "LODGroup level " + std::to_string(levelIndex + 1) + " must be an object");
                continue;
            }
            if (level.contains("meshes"))
            {
                const Json& meshes = level["meshes"];
                if (!meshes.is_array())
                {
                    Error(diagnostics, levelPointer + "/meshes",
                        "LODGroup level " + std::to_string(levelIndex + 1) + " meshes must be an array");
                }
                else
                {
                    for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
                    {
                        if (!meshes[meshIndex].is_string())
                        {
                            Error(diagnostics, levelPointer + "/meshes/" + std::to_string(meshIndex),
                                "LODGroup level " + std::to_string(levelIndex + 1) + " mesh " +
                                std::to_string(meshIndex) + " must be a GUID string");
                            continue;
                        }
                        const std::string guidText = meshes[meshIndex].get<std::string>();
                        VansAssetGuid guid;
                        if (!guidText.empty() && !VansAssetGuid::TryParse(guidText, guid))
                        {
                            Error(diagnostics, levelPointer + "/meshes/" + std::to_string(meshIndex),
                                "LODGroup level " + std::to_string(levelIndex + 1) + " mesh " +
                                std::to_string(meshIndex) + " has an invalid GUID");
                        }
                    }
                }
            }
            if (level.contains("errors"))
            {
                const Json& errors = level["errors"];
                if (!errors.is_array())
                {
                    Error(diagnostics, levelPointer + "/errors",
                        "LODGroup level " + std::to_string(levelIndex + 1) + " errors must be an array");
                }
                else
                {
                    for (std::size_t errorIndex = 0; errorIndex < errors.size(); ++errorIndex)
                    {
                        if (!errors[errorIndex].is_number())
                        {
                            Error(diagnostics, levelPointer + "/errors/" + std::to_string(errorIndex),
                                "LODGroup level " + std::to_string(levelIndex + 1) +
                                " errors must contain numbers");
                        }
                    }
                }
            }
        }
    }
    if (hasModelRenderer && hasMultiMeshRoot)
    {
        Error(diagnostics, entityPointer + "/components",
            "MultiMeshRoot and ModelRenderer cannot be declared on the same entity");
    }
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

VansSerializedValue VansSceneSchema::MakeDefaultSettings()
{
    VansSceneEnvironmentSettingsConfig environment;
    // 空场景尚无天体光源；天气由用户显式配置后启用。
    environment.physicalAtmosphere.enabled = false;
    environment.heightFog.enabled = false;
    environment.volumetricClouds.enabled = false;
    return VansSerializedValue::Object({{"environment", WriteSceneEnvironmentSettings(environment)}});
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

    const auto graphDiagnostics = ValidateEntityGraph(root["entities"]);
    diagnostics.insert(diagnostics.end(), graphDiagnostics.begin(), graphDiagnostics.end());
    return diagnostics;
}

SceneDiagnostics VansSceneSchema::ValidateEntityGraph(const Json& entities)
{
    SceneDiagnostics diagnostics;
    if (!entities.is_array())
    {
        Error(diagnostics, "/entities", "Object graph requires an entities array");
        return diagnostics;
    }
    std::unordered_set<VansEntityGuid> entityIds;
    std::unordered_set<VansComponentGuid> componentIds;
	std::vector<std::pair<std::string, VansSceneParentReference>> parents;
    std::unordered_map<VansEntityGuid, VansEntityGuid> parentByEntity;
	std::unordered_map<VansComponentGuid, std::pair<VansEntityGuid, std::string>> componentOwners;
    for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
    {
        const Json& entity = entities[entityIndex];
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
			VansSceneParentReference parent;
			std::string parentError;
			if (!entity["parent"].is_object()
				|| !TryReadSceneParentReference(
					DecodeSerializedValueJson(entity["parent"]), parent, parentError))
				Error(diagnostics, pointer + "/parent", parentError.empty()
					? "Entity parent must be null or a canonical parent object" : parentError);
			else
			{
				parents.emplace_back(pointer + "/parent", parent);
				if (entityId.IsValid())
					parentByEntity[entityId] = parent.entityGuid;
			}
        }
        if (!entity.contains("components") || !entity["components"].is_array())
        {
            Error(diagnostics, pointer + "/components", "Entity requires a components array");
            continue;
        }

        ValidateEntityComponentSemantics(entity, entityIndex, diagnostics);

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
			if (componentId.IsValid() && entityId.IsValid() && !type.empty())
				componentOwners.emplace(componentId, std::make_pair(entityId, type));
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
                const auto& data = component["data"];
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
            if ((type == "Transform" || type == "ModelRenderer" || type == "Physics" || type == "MultiMeshRoot") && !singletonTypes.insert(type).second)
                Error(diagnostics, componentPointer + "/type", type + " is a singleton component");
        }
        if (singletonTypes.find("Transform") == singletonTypes.end())
            Error(diagnostics, pointer + "/components", "Every entity requires a Transform component");
    }
	for (const auto& [pointer, parent] : parents)
	{
		if (entityIds.find(parent.entityGuid) == entityIds.end())
            Error(diagnostics, pointer, "Entity parent does not exist");
		if (parent.IsAnchor())
		{
			const auto component = componentOwners.find(parent.animationComponentGuid);
			if (component == componentOwners.end()
				|| component->second.first != parent.entityGuid
				|| component->second.second != "Animation")
				Error(diagnostics, pointer,
					"Bone/socket parent must reference an Animation component owned by entityGuid");
		}
	}
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

SceneDiagnostics VansSceneSchema::ValidateEntityComponents(const VansSerializedValue& entities)
{
    const Json encoded = EncodeSerializedValueJson<Json>(entities);
    SceneDiagnostics diagnostics;
    if (!encoded.is_array())
    {
        Error(diagnostics, "/entities", "Entities must be an array");
        return diagnostics;
    }
    for (std::size_t entityIndex = 0; entityIndex < encoded.size(); ++entityIndex)
    {
        const Json& entity = encoded[entityIndex];
        const std::string pointer = "/entities/" + std::to_string(entityIndex);
        if (!entity.is_object())
        {
            Error(diagnostics, pointer, "Entity must be an object");
            continue;
        }
        if (!entity.contains("components") || !entity["components"].is_array())
        {
            Error(diagnostics, pointer + "/components", "Entity requires a components array");
            continue;
        }
        ValidateEntityComponentSemantics(entity, entityIndex, diagnostics);
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
			VansSceneParentReference parent;
			std::string error;
			TryReadSceneParentReference(
				DecodeSerializedValueJson(entityJson["parent"]), parent, error);
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
			{ "parent", entity.parent
				? EncodeSerializedValueJson<Json>(WriteSceneParentReference(*entity.parent))
				: Json(nullptr) },
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
