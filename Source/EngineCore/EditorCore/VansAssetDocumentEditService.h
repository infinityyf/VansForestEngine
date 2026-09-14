#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansEditorObjectReference.h"

#include <functional>
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
	// Records a sidecar/binary edit that has already been applied. Undo and redo
	// callbacks restore the external payload while the document state id keeps
	// save/discard semantics unified with ordinary JSON property edits.
	static AssetDocumentEditResult RecordExternalEdit(
		VansAssetDocument& document,
		std::function<bool()> undo,
		std::function<bool()> redo);
	// 将映射定义和伴随像素作为同一个历史操作恢复。
	static AssetDocumentEditResult RecordExternalEdit(
		VansAssetDocument& document, VansSerializedValue root,
		std::function<bool()> undo, std::function<bool()> redo);

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
