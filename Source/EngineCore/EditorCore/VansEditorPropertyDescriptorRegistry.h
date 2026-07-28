#pragma once

#include "VansEditorObjectReference.h"

#include <string>

namespace Vans
{
struct LuaScriptFieldDescriptor;

enum class EditorPropertyKind
{
    Value,
    ObjectReference
};

enum class EditorPropertyDescriptorSource
{
    None,
    Declared
};

struct EditorPropertyDescriptor
{
    EditorPropertyKind kind = EditorPropertyKind::Value;
    EditorPropertyDescriptorSource source = EditorPropertyDescriptorSource::None;
    ObjectReferenceSlotDescriptor objectReferenceSlot;

    bool IsObjectReference() const { return kind == EditorPropertyKind::ObjectReference; }
    bool IsDeclared() const { return source == EditorPropertyDescriptorSource::Declared; }
};

class VansEditorPropertyDescriptorRegistry
{
public:
    static EditorPropertyDescriptor Resolve(
        const std::string& componentType,
        const std::string& parentKey,
        const std::string& fieldKey);

    static ObjectReferenceSlotDescriptor ProjectAssetReferenceSlot(
        EditorAPI::AssetType expectedType,
        ObjectReferenceStoragePolicy storagePolicy = ObjectReferenceStoragePolicy::GuidObject);

    static bool TryResolveLuaScriptFieldObjectReferenceSlot(
        const LuaScriptFieldDescriptor& descriptor,
        ObjectReferenceSlotDescriptor& slot);
};
}
