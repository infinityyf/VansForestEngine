#include "VansEditorPropertyDescriptorRegistry.h"

#include "../ScriptCore/VansLuaScriptInspectorService.h"
#include "../SceneCore/VansComponentTypeCatalog.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Vans
{
namespace
{
struct GlobalAssetReferenceRule
{
    const char* parent;
    const char* field;
    EditorAPI::AssetType assetType;
    ObjectReferenceStoragePolicy storagePolicy;
};

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool MatchesRuleToken(const std::string& actual, const char* expected)
{
    return expected == nullptr || *expected == '\0' || actual == expected;
}

const GlobalAssetReferenceRule* ResolveGlobalAssetReferenceRule(
    const std::string& parent,
    const std::string& field)
{
    static constexpr GlobalAssetReferenceRule rules[] = {
        { "", "shader", EditorAPI::AssetType::Shader, ObjectReferenceStoragePolicy::GuidObject },
        { "parameters", "skinprofile", EditorAPI::AssetType::SkinProfile, ObjectReferenceStoragePolicy::GuidObject },
        { "textures", "", EditorAPI::AssetType::Texture, ObjectReferenceStoragePolicy::GuidObject },
    };

    for (const GlobalAssetReferenceRule& rule : rules)
    {
        if (MatchesRuleToken(parent, rule.parent) &&
            MatchesRuleToken(field, rule.field))
        {
            return &rule;
        }
    }
    return nullptr;
}
}

ObjectReferenceSlotDescriptor VansEditorPropertyDescriptorRegistry::ProjectAssetReferenceSlot(
    EditorAPI::AssetType expectedType,
    ObjectReferenceStoragePolicy storagePolicy)
{
    ObjectReferenceSlotDescriptor descriptor;
    descriptor.expectedDomain = EditorObjectDomain::ProjectAsset;
    descriptor.expectedAssetType = expectedType;
    descriptor.storagePolicy = storagePolicy;
    return descriptor;
}

bool VansEditorPropertyDescriptorRegistry::TryResolveLuaScriptFieldObjectReferenceSlot(
    const LuaScriptFieldDescriptor& descriptor,
    ObjectReferenceSlotDescriptor& slot)
{
    ObjectReferenceSlotDescriptor resolvedSlot;
    resolvedSlot.storagePolicy = ObjectReferenceStoragePolicy::EditorObjectReference;

    if (descriptor.kind == LuaScriptInspectableFieldKind::SceneEntityReference)
    {
        resolvedSlot.expectedDomain = EditorObjectDomain::SceneEntity;
        slot = std::move(resolvedSlot);
        return true;
    }

    if (descriptor.kind == LuaScriptInspectableFieldKind::SceneComponentReference)
    {
        resolvedSlot.expectedDomain = EditorObjectDomain::SceneComponent;
        resolvedSlot.expectedComponentType = descriptor.componentType;
        slot = std::move(resolvedSlot);
        return true;
    }

    if (descriptor.kind == LuaScriptInspectableFieldKind::ProjectAssetReference)
    {
        slot = ProjectAssetReferenceSlot(
            EditorAssetTypeFromString(descriptor.assetType),
            ObjectReferenceStoragePolicy::EditorObjectReference);
        return true;
    }

    return false;
}

EditorPropertyDescriptor VansEditorPropertyDescriptorRegistry::Resolve(
    const std::string& componentType,
    const std::string& parentKey,
    const std::string& fieldKey)
{
    const std::string field = Lower(fieldKey);
    const std::string parent = Lower(parentKey);
    const std::string component = Lower(componentType);

    EditorPropertyDescriptor descriptor;
    if (const VansComponentAssetReferenceRule* rule =
        VansComponentTypeCatalog::FindAssetReferenceRule(component, parent, field))
    {
        const EditorAPI::AssetType assetType = EditorAssetTypeFromString(std::string(rule->assetType));
        if (assetType == EditorAPI::AssetType::Unknown) return descriptor;
        descriptor.kind = EditorPropertyKind::ObjectReference;
        descriptor.source = EditorPropertyDescriptorSource::Declared;
        descriptor.objectReferenceSlot = ProjectAssetReferenceSlot(
            assetType,
            rule->storage == VansComponentAssetReferenceStorage::GuidString
                ? ObjectReferenceStoragePolicy::GuidString
                : ObjectReferenceStoragePolicy::GuidObject);
        return descriptor;
    }
    const GlobalAssetReferenceRule* rule = ResolveGlobalAssetReferenceRule(parent, field);
    if (!rule || rule->assetType == EditorAPI::AssetType::Unknown) return descriptor;
    descriptor.kind = EditorPropertyKind::ObjectReference;
    descriptor.source = EditorPropertyDescriptorSource::Declared;
    descriptor.objectReferenceSlot = ProjectAssetReferenceSlot(rule->assetType, rule->storagePolicy);
    return descriptor;
}
}
