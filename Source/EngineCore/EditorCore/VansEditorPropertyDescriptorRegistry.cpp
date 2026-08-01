#include "VansEditorPropertyDescriptorRegistry.h"

#include "../ScriptCore/VansLuaScriptInspectorService.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Vans
{
namespace
{
struct AssetReferenceRule
{
    const char* component;
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

const AssetReferenceRule* ResolveDeclaredAssetReferenceRule(
    const std::string& component,
    const std::string& parent,
    const std::string& field)
{
    static constexpr AssetReferenceRule rules[] = {
        { "modelrenderer", "data", "model", EditorAPI::AssetType::Model, ObjectReferenceStoragePolicy::GuidObject },
        { "modelrenderer", "materialoverrides", "", EditorAPI::AssetType::Material, ObjectReferenceStoragePolicy::GuidObject },
        { "audio", "data", "source", EditorAPI::AssetType::Audio, ObjectReferenceStoragePolicy::GuidObject },
        { "video", "data", "source", EditorAPI::AssetType::Video, ObjectReferenceStoragePolicy::GuidObject },
        { "particle", "data", "asset", EditorAPI::AssetType::Particle, ObjectReferenceStoragePolicy::GuidString },
        { "animation", "data", "animator", EditorAPI::AssetType::AnimatorController, ObjectReferenceStoragePolicy::GuidString },
        { "animation", "ragdoll", "profile", EditorAPI::AssetType::RagdollProfile, ObjectReferenceStoragePolicy::GuidString },
        { "cloth", "data", "profilepath", EditorAPI::AssetType::ClothProfile, ObjectReferenceStoragePolicy::GuidString },
        { "", "", "shader", EditorAPI::AssetType::Shader, ObjectReferenceStoragePolicy::GuidObject },
        { "", "textures", "", EditorAPI::AssetType::Texture, ObjectReferenceStoragePolicy::GuidObject },
        { "", "customTextures", "", EditorAPI::AssetType::Texture, ObjectReferenceStoragePolicy::GuidObject },
    };

    for (const AssetReferenceRule& rule : rules)
    {
        if (MatchesRuleToken(component, rule.component) &&
            MatchesRuleToken(parent, rule.parent) &&
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
    const AssetReferenceRule* rule = ResolveDeclaredAssetReferenceRule(component, parent, field);
    if (rule && rule->assetType != EditorAPI::AssetType::Unknown)
    {
        descriptor.kind = EditorPropertyKind::ObjectReference;
        descriptor.source = EditorPropertyDescriptorSource::Declared;
        descriptor.objectReferenceSlot = ProjectAssetReferenceSlot(rule->assetType, rule->storagePolicy);
    }
    return descriptor;
}
}
