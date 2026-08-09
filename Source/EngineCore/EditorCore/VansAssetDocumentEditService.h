#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansEditorObjectReference.h"

#include <string>

namespace Vans
{
class VansAssetDocument;

struct AssetDocumentEditResult
{
    bool success = false;
    std::string message;

    explicit operator bool() const { return success; }
};

class VansAssetDocumentEditService
{
public:
	// Replaces one complete authoring document as a single undoable command.
	// Structured editors use this after validating their typed working copy.
	static AssetDocumentEditResult ReplaceRoot(
		VansAssetDocument& document,
		VansSerializedValue value);

    static AssetDocumentEditResult Set(
        VansAssetDocument& document,
        const DocumentPropertyPath& path,
        VansSerializedValue value);

    static AssetDocumentEditResult SetAndAssignObjectReference(
        VansAssetDocument& document,
        const DocumentPropertyPath& path,
        VansSerializedValue value,
        const ObjectReferenceAssignment& assignment);

    static AssetDocumentEditResult AssignObjectReference(
        VansAssetDocument& document,
        const ObjectReferenceAssignment& assignment);

    static bool CanUndo(const VansAssetDocument& document);
    static bool CanRedo(const VansAssetDocument& document);
    static AssetDocumentEditResult Undo(VansAssetDocument& document);
    static AssetDocumentEditResult Redo(VansAssetDocument& document);
	// Undo all edits after the last adopted save and drop their history. This is
	// the shared-document implementation of an explicit editor Discard action.
	static AssetDocumentEditResult RevertToSaved(VansAssetDocument& document);
    static void ClearHistory(const VansAssetDocument& document);
    static void ClearAllHistories();

private:
    static AssetDocumentEditResult Set(
        VansAssetDocument& document,
        const std::string& propertyPointer,
        VansSerializedValue value);
};
}
