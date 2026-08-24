#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <cstddef>
#include <string>

namespace Vans
{
enum class EditorObjectDomain
{
    Unknown,
    ProjectAsset,
    SceneEntity,
    SceneComponent,
	SceneSubObject,
    SubAsset,
    ScriptClass
};

enum class SceneSubObjectKind
{
	None,
	Bone,
	Socket
};

enum class DocumentPropertySpace
{
    Unknown,
    Scene,
    AssetSource,
    AssetMeta
};

enum class ObjectReferenceStoragePolicy
{
    GuidString,
    GuidObject,
    EditorObjectReference
};

struct DocumentPropertyPath
{
    DocumentPropertySpace space = DocumentPropertySpace::Unknown;
    std::string propertyPointer;
};

struct EditorObjectHandle
{
    EditorObjectDomain domain = EditorObjectDomain::Unknown;
    std::string guid;
    std::string path;
    std::string displayName;
    EditorAPI::AssetType assetType = EditorAPI::AssetType::Unknown;
    std::string entityGuid;
    std::string componentGuid;
    std::string componentType;
	SceneSubObjectKind subObjectKind = SceneSubObjectKind::None;
	std::string subObjectGuid;
    std::string subObjectName;
};

struct ObjectReferenceSlotDescriptor
{
    EditorObjectDomain expectedDomain = EditorObjectDomain::Unknown;
    EditorAPI::AssetType expectedAssetType = EditorAPI::AssetType::Unknown;
    std::string expectedComponentType;
    ObjectReferenceStoragePolicy storagePolicy = ObjectReferenceStoragePolicy::GuidObject;

    bool Accepts(const EditorObjectHandle& handle) const;
};

struct ObjectReferenceAssignment
{
    DocumentPropertyPath targetPath;
    ObjectReferenceSlotDescriptor slot;
    EditorObjectHandle value;
};

constexpr const char* VansObjectReferenceDragPayloadType = "VANS_OBJECT_REF";

EditorAPI::AssetType EditorAssetTypeFromString(const std::string& value);
DocumentPropertyPath MakeDocumentPropertyPath(DocumentPropertySpace space, std::string documentPropertyPointer);
DocumentPropertyPath MakeInspectorDocumentPropertyPath(std::string inspectorPropertyPointer);
ObjectReferenceAssignment MakeObjectReferenceAssignment(
    DocumentPropertyPath targetPath,
    ObjectReferenceSlotDescriptor slot,
    EditorObjectHandle value);
bool ValidateDocumentPropertyPath(const DocumentPropertyPath& path, std::string* error = nullptr);
std::string ToDocumentPropertyPointer(const DocumentPropertyPath& path);
bool TryMakeRelativeDocumentPropertyPointer(
    const DocumentPropertyPath& rootPath,
    const DocumentPropertyPath& childPath,
    std::string& relativePropertyPointer,
    std::string* error = nullptr);
bool NormalizeObjectReferenceSlotValue(
    VansSerializedValue& value,
    const ObjectReferenceSlotDescriptor& slot);
EditorObjectHandle ReadObjectReferenceSlotHandle(
    const VansSerializedValue& value,
    const ObjectReferenceSlotDescriptor& slot);
bool TryEncodeProjectAssetReferenceAssignment(
    const ObjectReferenceAssignment& assignment,
    VansSerializedValue& encodedValue,
    std::string* error = nullptr);

std::string SerializeEditorObjectHandle(const EditorObjectHandle& handle);
bool TryDeserializeEditorObjectHandle(const void* data, std::size_t size, EditorObjectHandle& handle);
}
