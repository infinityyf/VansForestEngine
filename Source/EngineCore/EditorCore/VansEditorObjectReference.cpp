#include "VansEditorObjectReference.h"

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <utility>

namespace Vans
{
namespace
{
constexpr const char* kEditorObjectHandlePayloadHeader = "VANS_OBJECT_REF_HANDLE_V1\n";

struct ProjectAssetReferenceValue
{
    std::string assetGuid;
    EditorAPI::AssetType assetType = EditorAPI::AssetType::Unknown;
    ObjectReferenceStoragePolicy storagePolicy = ObjectReferenceStoragePolicy::GuidObject;
};

void AppendLengthPrefixed(std::string& output, const std::string& value)
{
    output += std::to_string(value.size());
    output.push_back(':');
    output += value;
    output.push_back('\n');
}

bool ReadLengthPrefixed(const std::string& input, std::size_t& offset, std::string& value)
{
    if (offset >= input.size())
        return false;

    std::size_t length = 0;
    bool hasDigits = false;
    while (offset < input.size() && input[offset] >= '0' && input[offset] <= '9')
    {
        hasDigits = true;
        const std::size_t digit = static_cast<std::size_t>(input[offset] - '0');
        if (length > ((std::numeric_limits<std::size_t>::max)() - digit) / 10)
            return false;
        length = length * 10 + digit;
        ++offset;
    }

    if (!hasDigits || offset >= input.size() || input[offset] != ':')
        return false;
    ++offset;

    if (length > input.size() - offset)
        return false;
    value.assign(input, offset, length);
    offset += length;

    if (offset < input.size() && input[offset] == '\n')
        ++offset;
    return true;
}

bool ParseInt(const std::string& text, int& value)
{
    if (text.empty())
        return false;

    bool negative = false;
    std::size_t offset = 0;
    if (text[0] == '-' || text[0] == '+')
    {
        negative = text[0] == '-';
        offset = 1;
        if (offset == text.size())
            return false;
    }

    long long parsed = 0;
    const long long limit = negative
        ? static_cast<long long>((std::numeric_limits<int>::max)()) + 1ll
        : static_cast<long long>((std::numeric_limits<int>::max)());
    for (; offset < text.size(); ++offset)
    {
        const char ch = text[offset];
        if (ch < '0' || ch > '9')
            return false;
        const long long digit = static_cast<long long>(ch - '0');
        if (parsed > (limit - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }

    value = negative ? static_cast<int>(-parsed) : static_cast<int>(parsed);
    return true;
}

bool IsValidPropertyPointerSyntax(const std::string& pointer)
{
    if (pointer.empty() || pointer.front() != '/')
        return false;
    for (std::size_t index = 0; index < pointer.size(); ++index)
    {
        if (pointer[index] == '~' &&
            (index + 1 >= pointer.size() || (pointer[index + 1] != '0' && pointer[index + 1] != '1')))
        {
            return false;
        }
    }
    return true;
}

std::string NormalizeAssetTypeToken(std::string value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char c : value)
    {
        if (std::isalnum(c))
            normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return normalized;
}

bool HasInspectorPrefix(const std::string& pointer, const char* prefix)
{
    const std::size_t prefixLength = std::char_traits<char>::length(prefix);
    return pointer.size() >= prefixLength &&
        pointer.compare(0, prefixLength, prefix) == 0 &&
        (pointer.size() == prefixLength || pointer[prefixLength] == '/');
}

std::string StripInspectorPrefix(const std::string& pointer, const char* prefix)
{
    const std::size_t prefixLength = std::char_traits<char>::length(prefix);
    std::string stripped = pointer.substr(prefixLength);
    return stripped.empty() ? std::string("/") : stripped;
}
}

bool ObjectReferenceSlotDescriptor::Accepts(const EditorObjectHandle& handle) const
{
    const bool entityToComponent =
        expectedDomain == EditorObjectDomain::SceneComponent &&
        handle.domain == EditorObjectDomain::SceneEntity &&
        !expectedComponentType.empty();
    if (expectedDomain != EditorObjectDomain::Unknown &&
        handle.domain != expectedDomain &&
        !entityToComponent)
    {
        return false;
    }

    if (expectedDomain == EditorObjectDomain::ProjectAsset ||
        handle.domain == EditorObjectDomain::ProjectAsset)
    {
        if (expectedAssetType == EditorAPI::AssetType::Unknown)
            return handle.domain == EditorObjectDomain::ProjectAsset;
        return handle.assetType == expectedAssetType;
    }

    if (handle.domain == EditorObjectDomain::SceneComponent)
    {
        if (!expectedComponentType.empty() && handle.componentType != expectedComponentType)
            return false;
    }

    return handle.domain != EditorObjectDomain::Unknown;
}

namespace
{
const char* ToString(EditorObjectDomain domain)
{
    switch (domain)
    {
    case EditorObjectDomain::ProjectAsset: return "ProjectAsset";
    case EditorObjectDomain::SceneEntity: return "SceneEntity";
    case EditorObjectDomain::SceneComponent: return "SceneComponent";
    case EditorObjectDomain::SubAsset: return "SubAsset";
    case EditorObjectDomain::ScriptClass: return "ScriptClass";
    default: return "Unknown";
    }
}

EditorObjectDomain EditorObjectDomainFromString(const std::string& value)
{
    if (value == "ProjectAsset") return EditorObjectDomain::ProjectAsset;
    if (value == "SceneEntity") return EditorObjectDomain::SceneEntity;
    if (value == "SceneComponent") return EditorObjectDomain::SceneComponent;
    if (value == "SubAsset") return EditorObjectDomain::SubAsset;
    if (value == "ScriptClass") return EditorObjectDomain::ScriptClass;
    return EditorObjectDomain::Unknown;
}

const char* ToString(EditorAPI::AssetType assetType)
{
    switch (assetType)
    {
    case EditorAPI::AssetType::Model: return "Model";
    case EditorAPI::AssetType::Texture: return "Texture";
    case EditorAPI::AssetType::Material: return "Material";
    case EditorAPI::AssetType::Shader: return "Shader";
    case EditorAPI::AssetType::Audio: return "Audio";
    case EditorAPI::AssetType::Video: return "Video";
    case EditorAPI::AssetType::Scene: return "Scene";
    case EditorAPI::AssetType::Particle: return "Particle";
    case EditorAPI::AssetType::AnimationClip: return "AnimationClip";
    case EditorAPI::AssetType::AnimatorController: return "AnimatorController";
    case EditorAPI::AssetType::BoneMask: return "BoneMask";
	case EditorAPI::AssetType::Timeline: return "Timeline";
	case EditorAPI::AssetType::ActionDefinition: return "ActionDefinition";
	case EditorAPI::AssetType::ActionSet: return "ActionSet";
	case EditorAPI::AssetType::GameplayEffect: return "GameplayEffect";
	case EditorAPI::AssetType::GameplayCue: return "GameplayCue";
	case EditorAPI::AssetType::AttributeSet: return "AttributeSet";
	case EditorAPI::AssetType::TargetingPolicy: return "TargetingPolicy";
	case EditorAPI::AssetType::GameplayTagTree: return "GameplayTagTree";
	case EditorAPI::AssetType::PayloadSchema: return "PayloadSchema";
	case EditorAPI::AssetType::ActionGraph: return "ActionGraph";
	case EditorAPI::AssetType::CameraRigProfile: return "CameraRigProfile";
	case EditorAPI::AssetType::CameraShakeProfile: return "CameraShakeProfile";
	case EditorAPI::AssetType::GAFEditorLayout: return "GAFEditorLayout";
    case EditorAPI::AssetType::ClothProfile: return "ClothProfile";
    case EditorAPI::AssetType::SkinProfile: return "SkinProfile";
    case EditorAPI::AssetType::PostProcessProfile: return "PostProcessProfile";
    case EditorAPI::AssetType::RagdollProfile: return "RagdollProfile";
    case EditorAPI::AssetType::AudioReverbPreset: return "AudioReverbPreset";
    case EditorAPI::AssetType::AudioBusSnapshot: return "AudioBusSnapshot";
    case EditorAPI::AssetType::AudioDuckingRules: return "AudioDuckingRules";
    default: return "Unknown";
    }
}
}

EditorAPI::AssetType EditorAssetTypeFromString(const std::string& value)
{
    const std::string type = NormalizeAssetTypeToken(value);
    if (type == "model") return EditorAPI::AssetType::Model;
    if (type == "texture") return EditorAPI::AssetType::Texture;
    if (type == "material") return EditorAPI::AssetType::Material;
    if (type == "shader") return EditorAPI::AssetType::Shader;
    if (type == "audio") return EditorAPI::AssetType::Audio;
    if (type == "video") return EditorAPI::AssetType::Video;
    if (type == "scene") return EditorAPI::AssetType::Scene;
    if (type == "particle") return EditorAPI::AssetType::Particle;
    if (type == "animationclip") return EditorAPI::AssetType::AnimationClip;
    if (type == "animatorcontroller") return EditorAPI::AssetType::AnimatorController;
    if (type == "bonemask") return EditorAPI::AssetType::BoneMask;
	if (type == "timeline") return EditorAPI::AssetType::Timeline;
	if (type == "actiondefinition") return EditorAPI::AssetType::ActionDefinition;
	if (type == "actionset") return EditorAPI::AssetType::ActionSet;
	if (type == "gameplayeffect") return EditorAPI::AssetType::GameplayEffect;
	if (type == "gameplaycue") return EditorAPI::AssetType::GameplayCue;
	if (type == "attributeset") return EditorAPI::AssetType::AttributeSet;
	if (type == "targetingpolicy") return EditorAPI::AssetType::TargetingPolicy;
	if (type == "gameplaytagtree") return EditorAPI::AssetType::GameplayTagTree;
	if (type == "payloadschema") return EditorAPI::AssetType::PayloadSchema;
	if (type == "actiongraph") return EditorAPI::AssetType::ActionGraph;
	if (type == "camerarigprofile") return EditorAPI::AssetType::CameraRigProfile;
	if (type == "camerashakeprofile") return EditorAPI::AssetType::CameraShakeProfile;
	if (type == "gafeditorlayout") return EditorAPI::AssetType::GAFEditorLayout;
    if (type == "clothprofile") return EditorAPI::AssetType::ClothProfile;
    if (type == "skinprofile") return EditorAPI::AssetType::SkinProfile;
    if (type == "postprocessprofile") return EditorAPI::AssetType::PostProcessProfile;
    if (type == "ragdollprofile") return EditorAPI::AssetType::RagdollProfile;
    if (type == "audioreverbpreset") return EditorAPI::AssetType::AudioReverbPreset;
    if (type == "audiobussnapshot") return EditorAPI::AssetType::AudioBusSnapshot;
    if (type == "audioduckingrules") return EditorAPI::AssetType::AudioDuckingRules;
    return EditorAPI::AssetType::Unknown;
}

DocumentPropertyPath MakeDocumentPropertyPath(DocumentPropertySpace space, std::string documentPropertyPointer)
{
    DocumentPropertyPath path;
    path.space = space;
    path.propertyPointer = std::move(documentPropertyPointer);
    return path;
}

DocumentPropertyPath MakeInspectorDocumentPropertyPath(std::string inspectorPropertyPointer)
{
    constexpr const char* kAssetPrefix = "/asset";
    constexpr const char* kMetaPrefix = "/meta";

    if (HasInspectorPrefix(inspectorPropertyPointer, kAssetPrefix))
        return MakeDocumentPropertyPath(
            DocumentPropertySpace::AssetSource,
            StripInspectorPrefix(inspectorPropertyPointer, kAssetPrefix));
    if (HasInspectorPrefix(inspectorPropertyPointer, kMetaPrefix))
        return MakeDocumentPropertyPath(
            DocumentPropertySpace::AssetMeta,
            StripInspectorPrefix(inspectorPropertyPointer, kMetaPrefix));
    return MakeDocumentPropertyPath(DocumentPropertySpace::Scene, std::move(inspectorPropertyPointer));
}

ObjectReferenceAssignment MakeObjectReferenceAssignment(
    DocumentPropertyPath targetPath,
    ObjectReferenceSlotDescriptor slot,
    EditorObjectHandle value)
{
    ObjectReferenceAssignment assignment;
    assignment.targetPath = std::move(targetPath);
    assignment.slot = std::move(slot);
    assignment.value = std::move(value);
    return assignment;
}

bool ValidateDocumentPropertyPath(const DocumentPropertyPath& path, std::string* error)
{
    if (path.space == DocumentPropertySpace::Unknown)
    {
        if (error)
            *error = "Document property path has no document space";
        return false;
    }
    if (path.propertyPointer.empty() || path.propertyPointer.front() != '/')
    {
        if (error)
            *error = "Document property path must be an absolute property pointer";
        return false;
    }
    if (!IsValidPropertyPointerSyntax(path.propertyPointer))
    {
        if (error)
            *error = "Document property path contains an invalid pointer escape";
        return false;
    }
    return true;
}

std::string ToDocumentPropertyPointer(const DocumentPropertyPath& path)
{
    return path.propertyPointer;
}

bool TryMakeRelativeDocumentPropertyPointer(
    const DocumentPropertyPath& rootPath,
    const DocumentPropertyPath& childPath,
    std::string& relativePropertyPointer,
    std::string* error)
{
    relativePropertyPointer.clear();
    if (rootPath.space != childPath.space)
    {
        if (error)
            *error = "Document property paths are in different document spaces";
        return false;
    }
    if (!ValidateDocumentPropertyPath(rootPath, error) ||
        !ValidateDocumentPropertyPath(childPath, error))
    {
        return false;
    }

    const std::string& rootPointer = rootPath.propertyPointer;
    const std::string& childPointer = childPath.propertyPointer;
    if (rootPointer == childPointer)
        return true;

    if (rootPointer == "/")
    {
        relativePropertyPointer = childPointer;
        return true;
    }

    if (childPointer.size() > rootPointer.size() &&
        childPointer.compare(0, rootPointer.size(), rootPointer) == 0 &&
        childPointer[rootPointer.size()] == '/')
    {
        relativePropertyPointer = childPointer.substr(rootPointer.size());
        return true;
    }

    if (error)
        *error = "Object reference target is outside the edited property";
    return false;
}

namespace
{
ProjectAssetReferenceValue BuildProjectAssetReferenceValue(
    const std::string& assetGuid,
    EditorAPI::AssetType assetType,
    ObjectReferenceStoragePolicy storagePolicy)
{
    ProjectAssetReferenceValue value;
    value.assetGuid = assetGuid;
    value.assetType = assetType;
    value.storagePolicy = storagePolicy;
    return value;
}

ProjectAssetReferenceValue ReadProjectAssetReferenceValue(
    const VansSerializedValue& value,
    EditorAPI::AssetType fallbackAssetType,
    ObjectReferenceStoragePolicy storagePolicy)
{
    ProjectAssetReferenceValue reference;
    reference.assetType = fallbackAssetType;
    reference.storagePolicy = storagePolicy;

    if (storagePolicy == ObjectReferenceStoragePolicy::GuidString)
    {
        reference.assetGuid = ReadSerializedString(value);
        return reference;
    }

    if (storagePolicy == ObjectReferenceStoragePolicy::EditorObjectReference)
    {
        SerializedObjectReferenceValue serializedReference;
        if (TryReadSerializedObjectReference(value, serializedReference))
        {
            reference.assetGuid = serializedReference.guid;
            const EditorAPI::AssetType encodedType =
                EditorAssetTypeFromString(serializedReference.assetType);
            if (encodedType != EditorAPI::AssetType::Unknown)
                reference.assetType = encodedType;
        }
        return reference;
    }

    reference.assetGuid = ReadSerializedStringField(value, "guid");
    return reference;
}

VansSerializedValue EncodeProjectAssetReferenceValue(const ProjectAssetReferenceValue& value)
{
    if (value.storagePolicy == ObjectReferenceStoragePolicy::GuidString)
        return VansSerializedValue::String(value.assetGuid);
    if (value.storagePolicy == ObjectReferenceStoragePolicy::EditorObjectReference)
        return MakeSerializedProjectAssetObjectReference(value.assetGuid, ToString(value.assetType));
    if (value.storagePolicy == ObjectReferenceStoragePolicy::GuidObject)
    {
        return VansSerializedValue::Object({
            { "guid", VansSerializedValue::String(value.assetGuid) }
        });
    }
    return VansSerializedValue::String(value.assetGuid);
}

bool NormalizeProjectAssetReferenceValue(
    VansSerializedValue& value,
    EditorAPI::AssetType assetType,
    ObjectReferenceStoragePolicy storagePolicy)
{
    if (storagePolicy == ObjectReferenceStoragePolicy::GuidString)
    {
        if (value.kind == VansSerializedValue::Kind::String)
            return false;
        const std::string guid = ReadSerializedStringField(value, "guid");
        value = VansSerializedValue::String(guid);
        return true;
    }

    if (storagePolicy == ObjectReferenceStoragePolicy::EditorObjectReference)
    {
        SerializedObjectReferenceValue expected;
        expected.domain = ToString(EditorObjectDomain::ProjectAsset);
        expected.assetType = ToString(assetType);
        return NormalizeSerializedObjectReference(value, expected);
    }

    if (storagePolicy == ObjectReferenceStoragePolicy::GuidObject)
    {
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            const std::string guid = ReadSerializedString(value);
            value = VansSerializedValue::Object({
                { "guid", VansSerializedValue::String(guid) }
            });
            return true;
        }

        bool changed = false;
        VansSerializedValue* guid = FindObjectField(value, "guid");
        if (!guid || guid->kind != VansSerializedValue::Kind::String)
        {
            SetSerializedObjectField(value, "guid", VansSerializedValue::String(""));
            changed = true;
        }
        changed |= EraseSerializedObjectField(value, "domain");
        changed |= EraseSerializedObjectField(value, "assetType");
        changed |= EraseSerializedObjectField(value, "entityGuid");
        changed |= EraseSerializedObjectField(value, "componentGuid");
        changed |= EraseSerializedObjectField(value, "componentType");
        return changed;
    }

    return false;
}

EditorObjectHandle ReadSceneObjectReferenceHandle(const VansSerializedValue& reference)
{
    EditorObjectHandle handle;
    SerializedObjectReferenceValue serializedReference;
    if (!TryReadSerializedObjectReference(reference, serializedReference))
        return handle;

    handle.domain = EditorObjectDomainFromString(serializedReference.domain);
    handle.guid = serializedReference.guid;
    handle.entityGuid = serializedReference.entityGuid;
    handle.componentGuid = serializedReference.componentGuid;
    handle.componentType = serializedReference.componentType;
    return handle;
}
}

bool NormalizeObjectReferenceSlotValue(
    VansSerializedValue& value,
    const ObjectReferenceSlotDescriptor& slot)
{
    if (slot.expectedDomain == EditorObjectDomain::ProjectAsset)
    {
        return NormalizeProjectAssetReferenceValue(
            value,
            slot.expectedAssetType,
            slot.storagePolicy);
    }

    if (slot.expectedDomain == EditorObjectDomain::SceneEntity ||
        slot.expectedDomain == EditorObjectDomain::SceneComponent)
    {
        SerializedObjectReferenceValue expected;
        expected.domain = ToString(slot.expectedDomain);
        expected.componentType = slot.expectedComponentType;
        return NormalizeSerializedObjectReference(value, expected);
    }

    return false;
}

EditorObjectHandle ReadObjectReferenceSlotHandle(
    const VansSerializedValue& value,
    const ObjectReferenceSlotDescriptor& slot)
{
    if (slot.expectedDomain == EditorObjectDomain::ProjectAsset)
    {
        const ProjectAssetReferenceValue reference =
            ReadProjectAssetReferenceValue(
                value,
                slot.expectedAssetType,
                slot.storagePolicy);

        EditorObjectHandle handle;
        handle.domain = EditorObjectDomain::ProjectAsset;
        handle.guid = reference.assetGuid;
        handle.assetType = reference.assetType;
        return handle;
    }

    EditorObjectHandle handle = ReadSceneObjectReferenceHandle(value);
    if (slot.expectedDomain == EditorObjectDomain::SceneEntity ||
        slot.expectedDomain == EditorObjectDomain::SceneComponent)
    {
        handle.domain = slot.expectedDomain;
    }
    if (handle.componentType.empty())
        handle.componentType = slot.expectedComponentType;
    return handle;
}

bool TryEncodeProjectAssetReferenceAssignment(
    const ObjectReferenceAssignment& assignment,
    VansSerializedValue& encodedValue,
    std::string* error)
{
    if (!assignment.slot.Accepts(assignment.value))
    {
        if (error)
            *error = "Dragged object is not accepted by this project asset reference slot";
        return false;
    }
    if (assignment.value.domain != EditorObjectDomain::ProjectAsset)
    {
        if (error)
            *error = "Object reference assignment value is not a project asset";
        return false;
    }
    if (!assignment.value.guid.empty())
    {
        VansAssetGuid parsedGuid;
        if (!VansAssetGuid::TryParse(assignment.value.guid, parsedGuid))
        {
            if (error)
                *error = "Asset reference value is not a valid asset GUID";
            return false;
        }
    }

    const EditorAPI::AssetType writtenAssetType =
        assignment.value.assetType != EditorAPI::AssetType::Unknown
            ? assignment.value.assetType
            : assignment.slot.expectedAssetType;
    encodedValue = EncodeProjectAssetReferenceValue(
        BuildProjectAssetReferenceValue(
            assignment.value.guid,
            writtenAssetType,
            assignment.slot.storagePolicy));
    return true;
}

std::string SerializeEditorObjectHandle(const EditorObjectHandle& handle)
{
    std::string payload = kEditorObjectHandlePayloadHeader;
    AppendLengthPrefixed(payload, ToString(handle.domain));
    AppendLengthPrefixed(payload, handle.guid);
    AppendLengthPrefixed(payload, handle.path);
    AppendLengthPrefixed(payload, handle.displayName);
    AppendLengthPrefixed(payload, std::to_string(static_cast<int>(handle.assetType)));
    AppendLengthPrefixed(payload, handle.entityGuid);
    AppendLengthPrefixed(payload, handle.componentGuid);
    AppendLengthPrefixed(payload, handle.componentType);
    AppendLengthPrefixed(payload, handle.subObjectName);
    return payload;
}

bool TryDeserializeEditorObjectHandle(const void* data, std::size_t size, EditorObjectHandle& handle)
{
    handle = {};
    if (!data || size == 0)
        return false;

    const char* bytes = static_cast<const char*>(data);
    std::string text(bytes, bytes + size);
    if (!text.empty() && text.back() == '\0')
        text.pop_back();

    const std::string header = kEditorObjectHandlePayloadHeader;
    if (text.compare(0, header.size(), header) != 0)
        return false;
    std::size_t offset = header.size();

    std::string domain;
    std::string assetType;
    if (!ReadLengthPrefixed(text, offset, domain) ||
        !ReadLengthPrefixed(text, offset, handle.guid) ||
        !ReadLengthPrefixed(text, offset, handle.path) ||
        !ReadLengthPrefixed(text, offset, handle.displayName) ||
        !ReadLengthPrefixed(text, offset, assetType) ||
        !ReadLengthPrefixed(text, offset, handle.entityGuid) ||
        !ReadLengthPrefixed(text, offset, handle.componentGuid) ||
        !ReadLengthPrefixed(text, offset, handle.componentType) ||
        !ReadLengthPrefixed(text, offset, handle.subObjectName))
    {
        handle = {};
        return false;
    }

    int assetTypeValue = 0;
    if (!ParseInt(assetType, assetTypeValue))
        assetTypeValue = 0;
    handle.domain = EditorObjectDomainFromString(domain);
    handle.assetType = static_cast<EditorAPI::AssetType>(assetTypeValue);
    return handle.domain != EditorObjectDomain::Unknown;
}
}
