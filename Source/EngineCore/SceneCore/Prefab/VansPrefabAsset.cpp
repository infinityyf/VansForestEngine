#include "VansPrefabAsset.h"

#include "../VansSceneSchema.h"
#include "../VansSceneParentReference.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace Vans
{
namespace
{
using Json = nlohmann::ordered_json;
Json ToJson(const VansSerializedValue& value) { return EncodeSerializedValueJson<Json>(value); }
std::string Id(const Json& object) { return object.at("id").get<std::string>(); }
bool Guid(const std::string& text) { VansAssetGuid guid; return VansAssetGuid::TryParse(text, guid); }
void Require(bool condition, const std::string& error) { if (!condition) throw std::runtime_error(error); }

Json* FindId(Json& objects, const std::string& id)
{
    for (auto& object : objects) if (Id(object) == id) return &object;
    return nullptr;
}

Json& Transform(Json& entity)
{
    for (auto& component : entity.at("components"))
        if (component.at("type") == "Transform") return component.at("data");
    throw std::runtime_error("Prefab entity has no Transform");
}

void EraseId(Json& objects, const std::string& id)
{
    const auto it = std::find_if(objects.begin(), objects.end(), [&](const Json& object) { return Id(object) == id; });
    Require(it != objects.end(), "Override target no longer exists: " + id);
    objects.erase(it);
}

void ApplyOverrides(Json& entities, const Json& overrides)
{
    Require(overrides.is_array(), "Prefab overrides must be an array");
    for (const auto& change : overrides)
    {
        const std::string op = change.at("op");
        const std::string entityId = change.at("entity");
        if (op == "addEntity")
        {
            Require(!FindId(entities, entityId), "Duplicate added entity: " + entityId);
            Require(Id(change.at("value")) == entityId, "Added entity identity mismatch");
            entities.push_back(change.at("value"));
            continue;
        }
        if (op == "removeEntity") { EraseId(entities, entityId); continue; }
        auto* entity = FindId(entities, entityId);
        Require(entity != nullptr, "Override entity no longer exists: " + entityId);
        if (op == "addComponent")
        {
            const auto& component = change.at("value");
            Require(!FindId(entity->at("components"), Id(component)), "Duplicate added component");
            entity->at("components").push_back(component);
            continue;
        }
        const std::string componentId = change.value("component", "");
        if (op == "removeComponent") { EraseId(entity->at("components"), componentId); continue; }
        Json* target = componentId.empty() ? entity : FindId(entity->at("components"), componentId);
        Require(target != nullptr, "Override component no longer exists: " + componentId);
        const std::string path = change.at("path");
        Require(!path.empty() && path[0] == '/' && path != "/id" && path != "/type" &&
            !(componentId.empty() && (path == "/components" || path.rfind("/components/", 0) == 0)),
            "Invalid override property path: " + path);
        if (op == "set") (*target)[Json::json_pointer(path)] = change.at("value");
        else if (op == "removeField")
            *target = target->patch(Json::array({ Json{ { "op", "remove" }, { "path", path } } }));
        else throw std::runtime_error("Unknown prefab override operation: " + op);
    }
}

void DiffValue(const Json& before, const Json& after, const std::string& path,
    const std::string& entity, const std::string& component, Json& changes)
{
    if (before == after) return;
    const auto append = [&](const char* op, const std::string& pointer, const Json* value)
    {
        Json change{ { "op", op }, { "entity", entity }, { "path", pointer } };
        if (!component.empty()) change["component"] = component;
        if (value) change["value"] = *value;
        changes.push_back(std::move(change));
    };
    const auto escape = [](const std::string& text)
    {
        std::string result;
        for (const char ch : text) result += ch == '~' ? "~0" : ch == '/' ? "~1" : std::string(1, ch);
        return result;
    };
    if (before.is_object() && after.is_object())
    {
        for (auto it = before.begin(); it != before.end(); ++it)
            if (!after.contains(it.key())) append("removeField", path + "/" + escape(it.key()), nullptr);
        for (auto it = after.begin(); it != after.end(); ++it)
        {
            const auto pointer = path + "/" + escape(it.key());
            if (before.contains(it.key())) DiffValue(before.at(it.key()), it.value(), pointer, entity, component, changes);
            else append("set", pointer, &it.value());
        }
    }
    else append("set", path, &after); // 集合按整体覆盖，禁止数组索引跨模板重排漂移。
}

Json DiffEntities(Json before, Json after)
{
    Json changes = Json::array();
    for (auto& entity : before)
        if (!FindId(after, Id(entity))) changes.push_back({ { "op", "removeEntity" }, { "entity", Id(entity) } });
    for (auto& entity : after)
    {
        const auto id = Id(entity);
        auto* original = FindId(before, id);
        if (!original)
        { changes.push_back({ { "op", "addEntity" }, { "entity", id }, { "value", entity } }); continue; }
        auto oldComponents = original->at("components");
        auto newComponents = entity.at("components");
        original->erase("components"); entity.erase("components");
        DiffValue(*original, entity, "", id, "", changes);
        for (const auto& component : oldComponents)
            if (!FindId(newComponents, Id(component)))
                changes.push_back({ { "op", "removeComponent" }, { "entity", id }, { "component", Id(component) } });
        for (const auto& component : newComponents)
        {
            const auto* old = FindId(oldComponents, Id(component));
            if (old) DiffValue(*old, component, "", id, Id(component), changes);
            else changes.push_back({ { "op", "addComponent" }, { "entity", id }, { "value", component } });
        }
    }
    return changes;
}

Json LocalEntities(const VansPrefabAsset& asset, const Json& instance)
{
    auto entities = ToJson(asset.entities);
    ApplyOverrides(entities, instance.at("overrides"));
    Require(FindId(entities, asset.rootEntity), "Prefab root cannot be removed");
    const auto diagnostics = VansSceneSchema::ValidateEntityGraph(entities);
    Require(diagnostics.empty(), diagnostics.empty() ? "" : diagnostics.front().message);
    return entities;
}

VansSceneObjectIdentityMap IdentityMap(const Json& entities, const VansSerializedValue& instance)
{
    VansSceneObjectIdentityMap map;
    std::unordered_set<std::string> used;
    for (const auto& entity : entities)
    {
        const auto id = VansPrefabResolver::InstanceObjectGuid(instance, Id(entity), false);
        Require(used.insert(id).second, "Prefab instance entity identity collision");
        map.entities.emplace(Id(entity), id);
        for (const auto& component : entity.at("components"))
        {
            const auto componentId = VansPrefabResolver::InstanceObjectGuid(instance, Id(component), true);
            Require(used.insert(componentId).second, "Prefab instance component identity collision");
            map.components.emplace(Id(component), componentId);
        }
    }
    return map;
}

VansSceneObjectIdentityMap CompleteIdentityMap(const VansPrefabAsset& asset, const Json& current, const VansSerializedValue& instance)
{
    auto all = ToJson(asset.entities);
    for (const auto& entity : current)
    {
        auto* original = FindId(all, Id(entity));
        if (!original) { all.push_back(entity); continue; }
        for (const auto& component : entity.at("components"))
            if (!FindId(original->at("components"), Id(component))) original->at("components").push_back(component);
    }
    return IdentityMap(all, instance);
}

void ValidateInstance(const Json& instance)
{
    Require(Guid(instance.at("instanceId")) && Guid(instance.at("asset")), "Invalid prefab instance or asset GUID");
    const auto& placement = instance.at("placement");
    Require(placement.is_object() && placement.contains("parent"), "Prefab placement requires a parent field");
    if (!placement.at("parent").is_null())
    {
        VansSceneParentReference parent; std::string reason;
        Require(TryReadSceneParentReference(DecodeSerializedValueJson(placement.at("parent")), parent, reason), reason);
    }
    const auto vector = [&](const char* field, std::size_t size)
    {
        const auto& values = placement.at(field);
        Require(values.is_array() && values.size() == size, std::string("Invalid placement ") + field);
        for (const auto& value : values) Require(value.is_number() && std::isfinite(value.get<double>()), "Non-finite prefab placement");
    };
    vector("position", 3); vector("rotation", 4);
    double rotationLength = 0;
    for (const auto& value : placement.at("rotation")) rotationLength += value.get<double>() * value.get<double>();
    Require(rotationLength > 0.000001, "Prefab placement rotation is zero");
    Require(instance.at("identityOverrides").is_object() && instance.at("placement").is_object(),
        "Invalid prefab identity map or placement");
    for (auto it = instance.at("identityOverrides").begin(); it != instance.at("identityOverrides").end(); ++it)
        Require(((it.key().rfind("entity/", 0) == 0 && Guid(it.key().substr(7))) ||
            (it.key().rfind("component/", 0) == 0 && Guid(it.key().substr(10)))) &&
            it.value().is_string() && Guid(it.value()), "Invalid adopted prefab identity");
}

bool Try(const std::function<void()>& operation, std::string& error)
{
    error.clear();
    try { operation(); return true; }
    catch (const std::exception& e) { error = e.what(); return false; }
}
}

bool VansPrefabCodec::Decode(const VansSerializedValue& root, VansPrefabAsset& asset, std::string& error)
{
    VansPrefabAsset candidate;
    if (!Try([&]
    {
        auto json = ToJson(root);
        Require(json.is_object() && json.size() == 2 && json.contains("rootEntity") && json.contains("entities"),
            "Prefab requires exactly rootEntity and entities");
        candidate.rootEntity = json.at("rootEntity");
        candidate.entities = DecodeSerializedValueJson(json.at("entities"));
    }, error) || !Validate(candidate, error)) return false;
    asset = std::move(candidate);
    return true;
}

VansSerializedValue VansPrefabCodec::Encode(const VansPrefabAsset& asset)
{
    return VansSerializedValue::Object({ { "rootEntity", VansSerializedValue::String(asset.rootEntity) },
        { "entities", asset.entities } });
}

bool VansPrefabCodec::Validate(const VansPrefabAsset& asset, std::string& error)
{
    return Try([&]
    {
        auto entities = ToJson(asset.entities);
        const auto diagnostics = VansSceneSchema::ValidateEntityGraph(entities);
        Require(diagnostics.empty(), diagnostics.empty() ? "" : diagnostics.front().propertyPointer + ": " + diagnostics.front().message);
        Require(Guid(asset.rootEntity) && FindId(entities, asset.rootEntity), "Prefab root does not exist");
        std::unordered_set<std::string> entityIds;
        std::unordered_map<std::string, std::string> componentOwners;
        for (const auto& entity : entities)
        {
            entityIds.insert(Id(entity));
            Require(entity.at("parent").is_null() == (Id(entity) == asset.rootEntity), "Prefab must have exactly one root");
            for (const auto& component : entity.at("components")) componentOwners.emplace(Id(component), Id(entity));
        }
        auto references = asset.entities;
        std::string referenceError;
        Require(VisitSceneObjectReferences(references, [&](VansSceneObjectReference& ref, std::string& reason)
        {
            if ((!ref.entityGuid.empty() && !entityIds.count(ref.entityGuid)) ||
                (!ref.componentGuid.empty() && !componentOwners.count(ref.componentGuid)))
            { reason = ref.propertyPath + ": prefab contains an external scene reference"; return false; }
            if (!ref.entityGuid.empty() && !ref.componentGuid.empty() && componentOwners.at(ref.componentGuid) != ref.entityGuid)
            { reason = ref.propertyPath + ": component reference belongs to a different entity"; return false; }
            return true;
        }, referenceError), referenceError);
    }, error);
}

VansPrefabLookup VansPrefabResolver::FromRepository(const VansAssetObjectRepository& repository)
{
    return [&repository](const std::string& text)
    {
        VansAssetGuid guid;
        return VansAssetGuid::TryParse(text, guid) ? repository.ResolveLatest<VansPrefabAsset>(guid) : nullptr;
    };
}

VansSerializedValue VansPrefabResolver::MakeInstance(VansAssetGuid assetGuid)
{
    return DecodeSerializedValueJson(Json{
        { "instanceId", VansAssetGuid::New().ToString() }, { "asset", assetGuid.ToString() },
        { "placement", { { "parent", nullptr }, { "position", { 0.0, 0.0, 0.0 } }, { "rotation", { 0.0, 0.0, 0.0, 1.0 } } } },
        { "identityOverrides", Json::object() }, { "overrides", Json::array() } });
}

std::string VansPrefabResolver::InstanceObjectGuid(const VansSerializedValue& instance,
    const std::string& localId, bool component)
{
    if (const auto* mapping = FindObjectField(instance, "identityOverrides"))
        if (const auto* adopted = FindObjectField(*mapping, (component ? "component/" : "entity/") + localId))
            return adopted->stringValue;
    return VansAssetGuid::FromStableName(ReadSerializedStringField(instance, "instanceId"),
        (component ? "component/" : "entity/") + localId).ToString();
}

bool VansPrefabResolver::Instantiate(const VansPrefabAsset& asset, const VansSerializedValue& instance,
    VansSerializedValue& entities, std::string& error)
{
    return Try([&]
    {
        std::string validation;
        Require(VansPrefabCodec::Validate(asset, validation), validation);
        auto record = ToJson(instance); ValidateInstance(record);
        const auto local = LocalEntities(asset, record);
        auto candidate = DecodeSerializedValueJson(local);
        const auto identities = CompleteIdentityMap(asset, local, instance);
        Require(RemapSceneObjectGraph(candidate, identities, validation), validation);
        auto json = ToJson(candidate);
        auto* root = FindId(json, identities.entities.at(asset.rootEntity));
        root->at("parent") = record.at("placement").at("parent");
        auto& transform = Transform(*root);
        transform["position"] = record.at("placement").at("position");
        transform["rotation"] = record.at("placement").at("rotation");
        entities = DecodeSerializedValueJson(json);
    }, error);
}

bool VansPrefabResolver::ResolveScene(const VansSerializedValue& authoring, const VansPrefabLookup& lookup,
    VansSerializedValue& resolved, std::string& error)
{
    return Try([&]
    {
        auto scene = ToJson(authoring);
        if (scene.contains("prefabInstances"))
        {
            Require(scene.at("prefabInstances").is_array(), "prefabInstances must be an array");
            std::unordered_set<std::string> instanceIds;
            for (const auto& record : scene.at("prefabInstances"))
            {
                ValidateInstance(record);
                Require(instanceIds.insert(record.at("instanceId")).second, "Duplicate prefab instance identity");
                Require(static_cast<bool>(lookup), "Prefab asset lookup unavailable");
                const auto asset = lookup(record.at("asset"));
                Require(asset != nullptr, "Prefab asset unavailable: " + record.at("asset").get<std::string>());
                VansSerializedValue objects; std::string reason;
                Require(Instantiate(*asset, DecodeSerializedValueJson(record), objects, reason), reason);
                for (auto& entity : ToJson(objects)) scene["entities"].push_back(entity);
            }
        }
        const auto diagnostics = VansSceneSchema::ValidateEntityGraph(scene.at("entities"));
        Require(diagnostics.empty(), diagnostics.empty() ? "" : diagnostics.front().message);
        resolved = DecodeSerializedValueJson(scene);
    }, error);
}

bool VansPrefabResolver::CaptureScene(const VansSerializedValue& resolved, const VansPrefabLookup& lookup,
    VansSerializedValue& authoring, std::string& error)
{
    return Try([&]
    {
        auto scene = ToJson(resolved);
        if (!scene.contains("prefabInstances") || scene.at("prefabInstances").empty())
        { authoring = resolved; return; }
        Json remaining = scene.at("entities");
        Json records = Json::array();
        for (auto record : scene.at("prefabInstances"))
        {
            ValidateInstance(record);
            Require(static_cast<bool>(lookup), "Prefab asset lookup unavailable");
                const auto asset = lookup(record.at("asset"));
            Require(asset != nullptr, "Prefab source unavailable while editing");
            auto instance = DecodeSerializedValueJson(record);
            const auto known = LocalEntities(*asset, record);
            const auto map = CompleteIdentityMap(*asset, known, instance);
            const auto rootGuid = map.entities.at(asset->rootEntity);
            if (!FindId(remaining, rootGuid)) continue; // 删除实例根就是删除该实例记录。
            VansSerializedValue subtree; std::string reason;
            Require(ExtractSceneObjectSubtree(DecodeSerializedValueJson(remaining), rootGuid, subtree, reason), reason);
            auto captured = ToJson(subtree);
            for (const auto& other : scene.at("prefabInstances"))
            {
                if (other.at("instanceId") == record.at("instanceId")) continue;
                const auto source = lookup(other.at("asset"));
                Require(source != nullptr, "Prefab asset unavailable");
                const auto otherRoot = InstanceObjectGuid(DecodeSerializedValueJson(other), source->rootEntity, false);
                Require(!FindId(captured, otherRoot), "Nested prefab instances are not supported; unpack before reparenting");
            }
            for (const auto& pair : map.entities)
                Require(!FindId(remaining, pair.second) || FindId(captured, pair.second),
                    "Unpack the instance before moving an inherited object outside its root");
            VansSceneObjectIdentityMap inverse;
            for (const auto& pair : map.entities) inverse.entities.emplace(pair.second, pair.first);
            for (const auto& pair : map.components) inverse.components.emplace(pair.second, pair.first);
            for (const auto& entity : captured)
            {
                if (!inverse.entities.count(Id(entity)))
                {
                    const auto local = VansAssetGuid::New().ToString();
                    inverse.entities.emplace(Id(entity), local); record["identityOverrides"]["entity/" + local] = Id(entity);
                }
                for (const auto& component : entity.at("components"))
                    if (!inverse.components.count(Id(component)))
                    {
                        const auto local = VansAssetGuid::New().ToString();
                        inverse.components.emplace(Id(component), local); record["identityOverrides"]["component/" + local] = Id(component);
                    }
                EraseId(remaining, Id(entity));
            }
            auto* root = FindId(captured, rootGuid);
            record["placement"] = { { "parent", root->at("parent") },
                { "position", Transform(*root).at("position") }, { "rotation", Transform(*root).at("rotation") } };
            root->at("parent") = nullptr;
            Transform(*root)["position"] = { 0.0, 0.0, 0.0 };
            Transform(*root)["rotation"] = { 0.0, 0.0, 0.0, 1.0 };
            auto local = DecodeSerializedValueJson(captured);
            Require(RemapSceneObjectGraph(local, inverse, reason), reason);
            record["overrides"] = DiffEntities(ToJson(asset->entities), ToJson(local));
            records.push_back(std::move(record));
        }
        scene["entities"] = std::move(remaining);
        scene["prefabInstances"] = std::move(records);
        auto candidate = DecodeSerializedValueJson(scene);
        VansSerializedValue checked; std::string reason;
        Require(ResolveScene(candidate, lookup, checked, reason), reason);
        authoring = std::move(candidate);
    }, error);
}

bool VansPrefabResolver::Extract(const VansSerializedValue& resolvedScene, const std::string& rootEntity,
    VansAssetGuid assetGuid, VansPrefabExtraction& result, std::string& error, const VansPrefabLookup& lookup)
{
    return Try([&]
    {
        Require(assetGuid.IsValid(), "New prefab requires a valid asset GUID");
        auto scene = ToJson(resolvedScene);
        VansSerializedValue subtree; std::string reason;
        Require(ExtractSceneObjectSubtree(DecodeSerializedValueJson(scene.at("entities")), rootEntity, subtree, reason), reason);
        std::unordered_set<std::string> selected;
        for (const auto& entity : ToJson(subtree)) selected.insert(Id(entity));
        if (scene.contains("prefabInstances")) for (const auto& record : scene.at("prefabInstances"))
        {
            Require(static_cast<bool>(lookup), "Prefab asset lookup unavailable");
            const auto source = lookup(record.at("asset"));
            Require(source != nullptr, "Prefab asset unavailable");
            const auto map = CompleteIdentityMap(*source, LocalEntities(*source, record), DecodeSerializedValueJson(record));
            for (const auto& pair : map.entities)
                Require(!selected.count(pair.second), "Unpack the selected prefab instance before creating a new template");
        }
        auto instance = MakeInstance(assetGuid);
        auto record = ToJson(instance);
        auto source = ToJson(subtree);
        auto* root = FindId(source, rootEntity);
        record["placement"] = { { "parent", root->at("parent") },
            { "position", Transform(*root).at("position") }, { "rotation", Transform(*root).at("rotation") } };
        root->at("parent") = nullptr;
        Transform(*root)["position"] = { 0.0, 0.0, 0.0 };
        Transform(*root)["rotation"] = { 0.0, 0.0, 0.0, 1.0 };
        VansSceneObjectIdentityMap map;
        for (const auto& entity : source)
        {
            const auto local = VansAssetGuid::New().ToString();
            map.entities.emplace(Id(entity), local); record["identityOverrides"]["entity/" + local] = Id(entity);
            for (const auto& component : entity.at("components"))
            {
                const auto id = VansAssetGuid::New().ToString();
                map.components.emplace(Id(component), id); record["identityOverrides"]["component/" + id] = Id(component);
            }
        }
        auto local = DecodeSerializedValueJson(source);
        Require(RemapSceneObjectGraph(local, map, reason), reason);
        const auto bound = ToJson(local);
        // 外部场景引用留在实例覆盖中；模板只保留可重新赋值的空引用。
        Require(VisitSceneObjectReferences(local, [&](VansSceneObjectReference& ref, std::string&)
        {
            const auto isLocal = [](const auto& identities, const auto& id)
            { return std::any_of(identities.begin(), identities.end(), [&](const auto& pair) { return pair.second == id; }); };
            if (!ref.entityGuid.empty() && !isLocal(map.entities, ref.entityGuid)) ref.entityGuid.clear();
            if (!ref.componentGuid.empty() && !isLocal(map.components, ref.componentGuid)) ref.componentGuid.clear();
            return true;
        }, reason), reason);
        VansPrefabExtraction candidate;
        candidate.asset = { map.entities.at(rootEntity), local };
        Require(VansPrefabCodec::Validate(candidate.asset, reason), reason);
        record["overrides"] = DiffEntities(ToJson(local), bound);
        candidate.instance = DecodeSerializedValueJson(record);
        if (!scene.contains("prefabInstances")) scene["prefabInstances"] = Json::array();
        scene["prefabInstances"].push_back(record);
        candidate.scene = DecodeSerializedValueJson(scene); // 返回编辑视图；文档提交负责压缩。
        result = std::move(candidate);
    }, error);
}

bool VansPrefabResolver::Apply(const VansPrefabAsset& asset, const VansSerializedValue& instance,
    VansPrefabAsset& updatedAsset, VansSerializedValue& updatedInstance, std::string& error)
{
    return Try([&]
    {
        auto record = ToJson(instance); ValidateInstance(record);
        const auto bound = LocalEntities(asset, record);
        auto local = DecodeSerializedValueJson(bound);
        std::unordered_set<std::string> entities, components;
        for (const auto& entity : bound)
        {
            entities.insert(Id(entity));
            for (const auto& component : entity.at("components")) components.insert(Id(component));
        }
        std::string reason;
        Require(VisitSceneObjectReferences(local, [&](VansSceneObjectReference& ref, std::string&)
        {
            if (!ref.entityGuid.empty() && !entities.count(ref.entityGuid)) ref.entityGuid.clear();
            if (!ref.componentGuid.empty() && !components.count(ref.componentGuid)) ref.componentGuid.clear();
            return true;
        }, reason), reason);
        VansPrefabAsset candidate{asset.rootEntity, local};
        Require(VansPrefabCodec::Validate(candidate, reason), reason);
        record["overrides"] = DiffEntities(ToJson(local), bound);
        updatedAsset = std::move(candidate);
        updatedInstance = DecodeSerializedValueJson(record);
    }, error);
}

bool VansPrefabResolver::DuplicateSubtree(const VansSerializedValue& resolvedScene, const VansPrefabLookup& lookup,
    const std::string& root, VansSerializedValue& duplicatedScene, std::string& duplicatedRoot, std::string& error)
{
    return Try([&]
    {
        auto scene = ToJson(resolvedScene);
        VansSerializedValue subtree; std::string reason;
        Require(ExtractSceneObjectSubtree(DecodeSerializedValueJson(scene.at("entities")), root, subtree, reason), reason);
        VansSceneObjectIdentityMap map;
        for (const auto& entity : ToJson(subtree))
        {
            map.entities.emplace(Id(entity), VansAssetGuid::New().ToString());
            for (const auto& component : entity.at("components")) map.components.emplace(Id(component), VansAssetGuid::New().ToString());
        }
        auto records = scene.value("prefabInstances", Json::array());
        for (auto record : records)
        {
            const auto asset = lookup(record.at("asset")); Require(asset != nullptr, "Prefab source unavailable");
            const auto identities = CompleteIdentityMap(*asset, LocalEntities(*asset, record), DecodeSerializedValueJson(record));
            const auto instanceRoot = identities.entities.at(asset->rootEntity);
            if (!map.entities.count(instanceRoot)) continue;
            record["instanceId"] = VansAssetGuid::New().ToString();
            record["identityOverrides"] = Json::object();
            for (const auto& pair : identities.entities) if (map.entities.count(pair.second)) record["identityOverrides"]["entity/" + pair.first] = map.entities.at(pair.second);
            for (const auto& pair : identities.components) if (map.components.count(pair.second)) record["identityOverrides"]["component/" + pair.first] = map.components.at(pair.second);
            scene["prefabInstances"].push_back(record);
        }
        Require(RemapSceneObjectGraph(subtree, map, reason), reason);
        auto objects = ToJson(subtree);
        objects[0]["name"] = objects[0].value("name", std::string{}) + " Copy";
        for (auto& object : objects) scene["entities"].push_back(std::move(object));
        VansSerializedValue authoring;
        Require(CaptureScene(DecodeSerializedValueJson(scene), lookup, authoring, reason), reason);
        Require(ResolveScene(authoring, lookup, duplicatedScene, reason), reason);
        duplicatedRoot = map.entities.at(root);
    }, error);
}
}
