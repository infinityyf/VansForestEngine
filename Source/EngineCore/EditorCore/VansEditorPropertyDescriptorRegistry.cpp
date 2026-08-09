#include "VansEditorPropertyDescriptorRegistry.h"

#include "../ScriptCore/VansLuaScriptInspectorService.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"

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
        { "audiovolume", "data", "presetasset", EditorAPI::AssetType::AudioReverbPreset, ObjectReferenceStoragePolicy::GuidObject },
        { "audioreverbzone", "data", "presetasset", EditorAPI::AssetType::AudioReverbPreset, ObjectReferenceStoragePolicy::GuidObject },
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

const std::vector<EditorAnimatablePropertyDescriptor>& VansEditorPropertyDescriptorRegistry::AllAnimatable()
{
	static const std::vector<EditorAnimatablePropertyDescriptor> descriptors{
		{ "Transform.Position", "Position", VansRuntimeComponentType_Transform, VansTimelineChannelType::Vec3, "m", -100000.0, 100000.0, 0.01 },
		{ "Transform.Rotation", "Rotation", VansRuntimeComponentType_Transform, VansTimelineChannelType::Quaternion, "", -1.0, 1.0, 0.001 },
		{ "Transform.Scale", "Scale", VansRuntimeComponentType_Transform, VansTimelineChannelType::Vec3, "", 0.0, 1000.0, 0.01 },
		{ "Camera.FieldOfView", "Field of View", VansRuntimeComponentType_Camera, VansTimelineChannelType::Float, "deg", 1.0, 179.0, 0.1 },
		{ "Camera.NearClip", "Near Clip", VansRuntimeComponentType_Camera, VansTimelineChannelType::Float, "m", 0.001, 1000.0, 0.001 },
		{ "Camera.FarClip", "Far Clip", VansRuntimeComponentType_Camera, VansTimelineChannelType::Float, "m", 0.01, 1000000.0, 0.1 },
		{ "Audio.Volume", "Volume", VansRuntimeComponentType_Audio, VansTimelineChannelType::Float, "", 0.0, 4.0, 0.01 },
		{ "Audio.Pitch", "Pitch", VansRuntimeComponentType_Audio, VansTimelineChannelType::Float, "", 0.01, 4.0, 0.01 },
		{ "Audio.ReferenceDistance", "Reference Distance", VansRuntimeComponentType_Audio, VansTimelineChannelType::Float, "m", 0.0, 100000.0, 0.01 },
		{ "Audio.MaxDistance", "Max Distance", VansRuntimeComponentType_Audio, VansTimelineChannelType::Float, "m", 0.0, 1000000.0, 0.1 },
		{ "Audio.Rolloff", "Rolloff", VansRuntimeComponentType_Audio, VansTimelineChannelType::Float, "", 0.0, 16.0, 0.01 },
		{ "Audio.ReverbSend", "Reverb Send", VansRuntimeComponentType_Audio, VansTimelineChannelType::Float, "", 0.0, 1.0, 0.01 },
		{ "Audio.Loop", "Loop", VansRuntimeComponentType_Audio, VansTimelineChannelType::Bool },
		{ "Audio.Spatial", "Spatial", VansRuntimeComponentType_Audio, VansTimelineChannelType::Bool },
		{ "Audio.Bus", "Bus", VansRuntimeComponentType_Audio, VansTimelineChannelType::String }
	};
	return descriptors;
}

const EditorAnimatablePropertyDescriptor* VansEditorPropertyDescriptorRegistry::FindAnimatable(
	const std::string& stableId)
{
	const auto& descriptors = AllAnimatable();
	const auto found = std::find_if(descriptors.begin(), descriptors.end(),
		[&](const auto& descriptor) { return descriptor.stableId == stableId; });
	return found == descriptors.end() ? nullptr : &*found;
}

std::vector<EditorAnimatablePropertyDescriptor>
VansEditorPropertyDescriptorRegistry::AnimatableForComponent(std::uint16_t componentTypeId)
{
	std::vector<EditorAnimatablePropertyDescriptor> result;
	for (const auto& descriptor : AllAnimatable())
		if (descriptor.componentTypeId == componentTypeId) result.push_back(descriptor);
	return result;
}
}
