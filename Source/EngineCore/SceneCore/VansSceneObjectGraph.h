#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansSceneObjectReference
{
    std::string entityGuid;
    std::string componentGuid;
    std::string propertyPath;
};

using VansSceneObjectReferenceVisitor =
    std::function<bool(VansSceneObjectReference&, std::string&)>;

struct VansSceneObjectIdentityMap
{
    std::unordered_map<std::string, std::string> entities;
    std::unordered_map<std::string, std::string> components;
};

// 场景复制、Prefab 提取和实例化共用引用语义；不替换资产 GUID 或普通字符串。
bool VisitSceneObjectReferences(VansSerializedValue& entities,
    const VansSceneObjectReferenceVisitor& visitor, std::string& error);
bool RemapSceneObjectGraph(VansSerializedValue& entities,
    const VansSceneObjectIdentityMap& identities, std::string& error);
bool ExtractSceneObjectSubtree(const VansSerializedValue& entities,
    const std::string& rootGuid, VansSerializedValue& subtree, std::string& error);
}
