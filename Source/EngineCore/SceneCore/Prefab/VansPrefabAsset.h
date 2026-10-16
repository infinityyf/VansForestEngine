#pragma once

#include "../../AssetCore/Serialization/VansSerializedValue.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../VansSceneObjectGraph.h"

#include <functional>
#include <memory>
#include <string>

namespace Vans
{
class VansAssetObjectRepository;

struct VansPrefabAsset
{
    std::string rootEntity;
    VansSerializedValue entities = VansSerializedValue::Array({});
};

using VansPrefabLookup = std::function<std::shared_ptr<const VansPrefabAsset>(const std::string&)>;

// Derived from a resolved immutable scene; indices address its authoring instance records.
struct VansPrefabSceneIndex
{
    std::unordered_map<std::string, std::size_t> instanceByEntity;
};

struct VansPrefabExtraction
{
    VansPrefabAsset asset;
    VansSerializedValue instance;
    VansSerializedValue scene;
};

class VansPrefabCodec
{
public:
    static bool Decode(const VansSerializedValue& root, VansPrefabAsset& asset, std::string& error);
    static VansSerializedValue Encode(const VansPrefabAsset& asset);
    static bool Validate(const VansPrefabAsset& asset, std::string& error);
};

// 纯内存操作；磁盘 I/O 只属于资产导入/保存/打包入口。
class VansPrefabResolver
{
public:
    static VansPrefabLookup FromRepository(const VansAssetObjectRepository& repository);
    static bool ResolveScene(const VansSerializedValue& authoring, const VansPrefabLookup& lookup,
        VansSerializedValue& resolved, std::string& error, VansPrefabSceneIndex* index = nullptr);
    static bool CaptureScene(const VansSerializedValue& resolved, const VansPrefabLookup& lookup,
        VansSerializedValue& authoring, std::string& error);
    static bool CaptureInstance(const VansPrefabAsset& asset, const VansSerializedValue& instance,
        const VansSerializedValue& entities, VansSerializedValue& capturedInstance, std::string& error);
    static bool Instantiate(const VansPrefabAsset& asset, const VansSerializedValue& instance,
        VansSerializedValue& entities, std::string& error);
    static bool Extract(const VansSerializedValue& resolvedScene, const std::string& rootEntity,
        VansAssetGuid assetGuid, VansPrefabExtraction& result, std::string& error, const VansPrefabLookup& lookup = {});
    static VansSerializedValue MakeInstance(VansAssetGuid assetGuid);
    static bool Apply(const VansPrefabAsset& asset, const VansSerializedValue& instance,
        VansPrefabAsset& updatedAsset, VansSerializedValue& updatedInstance, std::string& error);
    static bool DuplicateSubtree(const VansSerializedValue& scene, const VansPrefabLookup& lookup,
        const std::string& root, VansSerializedValue& duplicatedScene, std::string& duplicatedRoot, std::string& error);
    static std::string InstanceObjectGuid(const VansSerializedValue& instance,
        const std::string& localId, bool component);
};
}
