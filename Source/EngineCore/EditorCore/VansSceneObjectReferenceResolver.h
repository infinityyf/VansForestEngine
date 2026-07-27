#pragma once

#include "VansEditorObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <optional>
#include <string>

namespace Vans
{
class VansSceneDocument;

struct SceneObjectReferenceResolution
{
    bool success = false;
    std::string message;
    EditorObjectHandle handle;
    std::optional<VansSerializedValue> encodedValue;
    std::string entityDisplayName;
    std::string componentDisplayType;

    explicit operator bool() const { return success; }
};

SceneObjectReferenceResolution ResolveSceneObjectReference(
    const VansSceneDocument& document,
    const ObjectReferenceSlotDescriptor& slot,
    const EditorObjectHandle& source);
bool TryEncodeSceneDocumentObjectReferenceAssignment(
    const VansSceneDocument& document,
    const ObjectReferenceAssignment& assignment,
    VansSerializedValue& encodedValue,
    std::string* error = nullptr);
}
