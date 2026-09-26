#include "VansAssetDocumentEditService.h"
#include "VansAssetDocumentRegistry.h"

#include "../AssetCore/VansAssetDocument.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace Vans
{
namespace
{
AssetDocumentEditResult ValidateAssetDocumentPath(const DocumentPropertyPath& path)
{
    if (path.space != DocumentPropertySpace::AssetSource &&
        path.space != DocumentPropertySpace::AssetMeta)
    {
        return { false, "Asset edit target is not an asset document path" };
    }
    std::string error;
    if (!ValidateDocumentPropertyPath(path, &error))
        return { false, error };
    return { true, {} };
}

bool TryRead(
    const VansSerializedValue& root,
    const std::string& pointer,
    VansSerializedValue& value)
{
    const VansSerializedValue* found = FindSerializedPointer(root, pointer);
    if (!found)
        return false;
    value = *found;
    return true;
}

AssetDocumentEditResult RemoveAt(VansSerializedValue& root, const std::string& pointer)
{
    std::string error;
    if (EraseSerializedPointer(root, pointer, &error))
        return { true, {} };
    return { false, error.empty() ? "Asset property does not exist" : error };
}

AssetDocumentEditResult WriteAt(
    VansSerializedValue& root,
    const std::string& pointer,
    VansSerializedValue value)
{
    std::string error;
    if (SetSerializedPointer(root, pointer, std::move(value), &error))
        return { true, {} };
    return { false, error };
}

class AssetDocumentEditCommand
{
public:
    virtual ~AssetDocumentEditCommand() = default;
    virtual AssetDocumentEditResult Execute(VansAssetDocument& document) = 0;
    virtual AssetDocumentEditResult Undo(VansAssetDocument& document) = 0;
    virtual AssetDocumentEditResult Redo(VansAssetDocument& document) = 0;
};
}

namespace EditorInternal
{
class SetAssetPropertyCommand final : public AssetDocumentEditCommand
{
public:
    SetAssetPropertyCommand(std::string propertyPointer, VansSerializedValue value)
        : m_PropertyPointer(std::move(propertyPointer))
        , m_NewValue(std::move(value))
    {
    }

    AssetDocumentEditResult Execute(VansAssetDocument& document) override
    {
        if (!document.IsLoaded())
            return { false, "Asset document is not loaded" };
        std::string pointerError;
        if (!ValidateDocumentPropertyPointer(
                m_PropertyPointer, true, &pointerError, "Asset property address"))
        {
            return { false, std::move(pointerError) };
        }

        VansSerializedValue candidate = document.SerializedRootSnapshot();
        VansSerializedValue oldValue;
        m_HadOldValue = TryRead(candidate, m_PropertyPointer, oldValue);
        m_BeforeState = document.CurrentStateId();
        if (m_HadOldValue && SerializedValuesEqual(oldValue, m_NewValue))
            return { false, "Asset property is unchanged" };
        if (m_HadOldValue)
            m_OldValue = std::move(oldValue);

        if (AssetDocumentEditResult result = WriteAt(candidate, m_PropertyPointer, m_NewValue); !result)
            return result;
        m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
        return { true, {} };
    }

    AssetDocumentEditResult Undo(VansAssetDocument& document) override
    {
        VansSerializedValue candidate = document.SerializedRootSnapshot();
        AssetDocumentEditResult result = m_HadOldValue
            ? WriteAt(candidate, m_PropertyPointer, m_OldValue)
            : RemoveAt(candidate, m_PropertyPointer);
        if (result)
            document.RestoreEditedSerializedRoot(std::move(candidate), m_BeforeState);
        return result;
    }

    AssetDocumentEditResult Redo(VansAssetDocument& document) override
    {
        VansSerializedValue candidate = document.SerializedRootSnapshot();
        AssetDocumentEditResult result = WriteAt(candidate, m_PropertyPointer, m_NewValue);
        if (result)
            document.RestoreEditedSerializedRoot(std::move(candidate), m_AfterState);
        return result;
    }

private:
    std::string m_PropertyPointer;
    VansSerializedValue m_NewValue;
    VansSerializedValue m_OldValue;
    bool m_HadOldValue = false;
    VansAssetDocumentStateId m_BeforeState = 0;
    VansAssetDocumentStateId m_AfterState = 0;
};

class ExternalAssetEditCommand final : public AssetDocumentEditCommand
{
public:
	ExternalAssetEditCommand(VansSerializedValue root, std::function<bool()> undo, std::function<bool()> redo)
		: m_Undo(std::move(undo)), m_Redo(std::move(redo)), m_AfterRoot(std::move(root))
	{
	}

	AssetDocumentEditResult Execute(VansAssetDocument& document) override
	{
		if (!document.IsLoaded())
			return { false, "Asset document is not loaded" };
		m_BeforeState = document.CurrentStateId();
		m_BeforeRoot = document.SerializedRootSnapshot();
		m_AfterState = document.ApplyEditedSerializedRoot(m_AfterRoot);
		return { true, {} };
	}

	AssetDocumentEditResult Undo(VansAssetDocument& document) override
	{
		if (!m_Undo || !m_Undo())
			return { false, "External asset payload undo failed" };
		document.RestoreEditedSerializedRoot(m_BeforeRoot, m_BeforeState);
		return { true, {} };
	}

	AssetDocumentEditResult Redo(VansAssetDocument& document) override
	{
		if (!m_Redo || !m_Redo())
			return { false, "External asset payload redo failed" };
		document.RestoreEditedSerializedRoot(m_AfterRoot, m_AfterState);
		return { true, {} };
	}

private:
	std::function<bool()> m_Undo;
	std::function<bool()> m_Redo;
	VansSerializedValue m_BeforeRoot, m_AfterRoot;
	VansAssetDocumentStateId m_BeforeState = 0;
	VansAssetDocumentStateId m_AfterState = 0;
};
}

class VansAssetDocumentEditHistory
{
public:
	struct Entry
	{
		std::unique_ptr<AssetDocumentEditCommand> command;
		VansHistorySequence sequence = 0;
	};

	void DiscardStaleRedo() const
	{
		if (!redo.empty() && redoRevision != VansAuthoringHistory::CurrentRevision())
		{
			redo.clear();
			redoRevision = 0;
		}
	}

    std::vector<Entry> undo;
    mutable std::vector<Entry> redo;
	mutable VansHistorySequence redoRevision = 0;
};

VansOpenAssetDocument::VansOpenAssetDocument() = default;
VansOpenAssetDocument::~VansOpenAssetDocument() = default;

class VansAssetDocumentEditAccess
{
public:
	static VansOpenAssetDocument* FindOwner(VansAssetDocument& document)
	{
		auto& registry = VansAssetDocumentRegistry::Get();
		for (auto& [path, candidate] : registry.m_Documents)
		{
			(void)path;
			if (candidate &&
				(&candidate->sourceDocument == &document || &candidate->metaDocument == &document))
				return candidate.get();
		}
		return nullptr;
	}

	static const VansOpenAssetDocument* FindOwner(const VansAssetDocument& document)
	{
		const auto& registry = VansAssetDocumentRegistry::Get();
		for (const auto& [path, candidate] : registry.m_Documents)
		{
			(void)path;
			if (candidate &&
				(&candidate->sourceDocument == &document || &candidate->metaDocument == &document))
				return candidate.get();
		}
		return nullptr;
	}

	static VansAssetDocumentEditHistory* FindHistory(VansAssetDocument& document, bool create)
	{
		VansOpenAssetDocument* owner = FindOwner(document);
		if (!owner)
			return nullptr;
		std::unique_ptr<VansAssetDocumentEditHistory>& history =
			&owner->sourceDocument == &document
			? owner->m_SourceEditHistory
			: owner->m_MetaEditHistory;
		if (!history && create)
			history = std::make_unique<VansAssetDocumentEditHistory>();
		return history.get();
	}

	static const VansAssetDocumentEditHistory* FindHistory(const VansAssetDocument& document)
	{
		const VansOpenAssetDocument* owner = FindOwner(document);
		if (!owner)
			return nullptr;
		return (&owner->sourceDocument == &document
			? owner->m_SourceEditHistory
			: owner->m_MetaEditHistory).get();
	}
};

namespace
{

AssetDocumentEditResult ExecuteCommand(
    VansAssetDocument& document,
    std::unique_ptr<AssetDocumentEditCommand> command)
{
    if (!command)
        return { false, "Asset edit command is null" };
    AssetDocumentEditResult result = command->Execute(document);
    if (!result)
        return result;

	VansAssetDocumentEditHistory* history = VansAssetDocumentEditAccess::FindHistory(document, true);
	if (!history)
	{
		const AssetDocumentEditResult rollback = command->Undo(document);
		return rollback
			? AssetDocumentEditResult{ false, "Asset document is not open in the authoring registry" }
			: AssetDocumentEditResult{ false, "Asset document is not open in the authoring registry and edit rollback failed" };
	}
	history->undo.push_back({ std::move(command), VansAuthoringHistory::IssueEditSequence() });
    history->redo.clear();
	history->redoRevision = 0;
	VansAssetDocumentRegistry::Get().PublishWorkingCopy(document);
    return result;
}
}

AssetDocumentEditResult VansAssetDocumentEditService::Set(
    VansAssetDocument& document,
    const std::string& propertyPointer,
    VansSerializedValue value)
{
    return ExecuteCommand(document, std::make_unique<EditorInternal::SetAssetPropertyCommand>(
        propertyPointer, std::move(value)));
}

AssetDocumentEditResult VansAssetDocumentEditService::ReplaceRoot(
	VansAssetDocument& document,
	VansSerializedValue value)
{
	return ExecuteCommand(document, std::make_unique<EditorInternal::SetAssetPropertyCommand>(
		std::string{}, std::move(value)));
}

AssetDocumentEditResult VansAssetDocumentEditService::RecordExternalEdit(
	VansAssetDocument& document,
	std::function<bool()> undo,
	std::function<bool()> redo)
{
	return RecordExternalEdit(document, document.SerializedRootSnapshot(), std::move(undo), std::move(redo));
}

AssetDocumentEditResult VansAssetDocumentEditService::RecordExternalEdit(
	VansAssetDocument& document, VansSerializedValue root,
	std::function<bool()> undo, std::function<bool()> redo)
{
	return ExecuteCommand(document,
		std::make_unique<EditorInternal::ExternalAssetEditCommand>(
			std::move(root), std::move(undo), std::move(redo)));
}

AssetDocumentEditResult VansAssetDocumentEditService::Set(
    VansAssetDocument& document,
    const DocumentPropertyPath& path,
    VansSerializedValue value)
{
    if (AssetDocumentEditResult validation = ValidateAssetDocumentPath(path); !validation)
        return validation;
    return Set(document, ToDocumentPropertyPointer(path), std::move(value));
}

AssetDocumentEditResult VansAssetDocumentEditService::SetAndAssignObjectReference(
    VansAssetDocument& document,
    const DocumentPropertyPath& path,
    VansSerializedValue value,
    const ObjectReferenceAssignment& assignment)
{
    if (AssetDocumentEditResult validation = ValidateAssetDocumentPath(path); !validation)
        return validation;
    if (AssetDocumentEditResult validation = ValidateAssetDocumentPath(assignment.targetPath); !validation)
        return validation;
    if (path.space != assignment.targetPath.space)
        return { false, "Asset object reference transaction targets must be in the same document" };

    VansSerializedValue referenceValue;
    std::string assignmentError;
    if (!TryEncodeProjectAssetReferenceAssignment(assignment, referenceValue, &assignmentError))
        return { false, assignmentError };

    std::string pathError;
    std::string relativePointer;
    if (!TryMakeRelativeDocumentPropertyPointer(path, assignment.targetPath, relativePointer, &pathError))
        return { false, pathError };

    if (relativePointer.empty())
    {
        value = std::move(referenceValue);
    }
    else if (!SetSerializedPointer(value, relativePointer, std::move(referenceValue), &pathError))
    {
        return { false, pathError };
    }

    return ExecuteCommand(document, std::make_unique<EditorInternal::SetAssetPropertyCommand>(
        ToDocumentPropertyPointer(path), std::move(value)));
}

AssetDocumentEditResult VansAssetDocumentEditService::AssignObjectReference(
    VansAssetDocument& document,
    const ObjectReferenceAssignment& assignment)
{
    if (AssetDocumentEditResult validation = ValidateAssetDocumentPath(assignment.targetPath); !validation)
        return validation;

    VansSerializedValue referenceValue;
    std::string assignmentError;
    if (!TryEncodeProjectAssetReferenceAssignment(assignment, referenceValue, &assignmentError))
        return { false, assignmentError };
    return ExecuteCommand(document, std::make_unique<EditorInternal::SetAssetPropertyCommand>(
        ToDocumentPropertyPointer(assignment.targetPath),
        std::move(referenceValue)));
}

bool VansAssetDocumentEditService::CanUndo(const VansAssetDocument& document)
{
	return HistorySnapshot(document).CanUndo();
}

bool VansAssetDocumentEditService::CanRedo(const VansAssetDocument& document)
{
	return HistorySnapshot(document).CanRedo();
}

VansAuthoringHistorySnapshot VansAssetDocumentEditService::HistorySnapshot(
	const VansAssetDocument& document)
{
	const VansAssetDocumentEditHistory* history = VansAssetDocumentEditAccess::FindHistory(document);
	if (!history)
		return {};
	history->DiscardStaleRedo();
	return {
		history->undo.empty() ? 0 : history->undo.back().sequence,
		history->redo.empty() ? 0 : history->redo.back().sequence
	};
}

AssetDocumentEditResult VansAssetDocumentEditService::Undo(VansAssetDocument& document)
{
    VansAssetDocumentEditHistory* history = VansAssetDocumentEditAccess::FindHistory(document, false);
    if (!history || history->undo.empty())
        return { false, "No asset edit to undo" };
	history->DiscardStaleRedo();
	VansAssetDocumentEditHistory::Entry entry = std::move(history->undo.back());
    history->undo.pop_back();
	AssetDocumentEditResult result = entry.command->Undo(document);
    if (result)
	{
		if (history->redo.empty())
			history->redoRevision = VansAuthoringHistory::CurrentRevision();
		history->redo.push_back(std::move(entry));
		VansAssetDocumentRegistry::Get().PublishWorkingCopy(document);
	}
    else
		history->undo.push_back(std::move(entry));
    return result;
}

AssetDocumentEditResult VansAssetDocumentEditService::Redo(VansAssetDocument& document)
{
    VansAssetDocumentEditHistory* history = VansAssetDocumentEditAccess::FindHistory(document, false);
    if (!history)
		return { false, "No asset edit to redo" };
	history->DiscardStaleRedo();
	if (history->redo.empty())
        return { false, "No asset edit to redo" };
	VansAssetDocumentEditHistory::Entry entry = std::move(history->redo.back());
    history->redo.pop_back();
	AssetDocumentEditResult result = entry.command->Redo(document);
    if (result)
	{
		history->undo.push_back(std::move(entry));
		if (history->redo.empty())
			history->redoRevision = 0;
		VansAssetDocumentRegistry::Get().PublishWorkingCopy(document);
	}
    else
		history->redo.push_back(std::move(entry));
    return result;
}

AssetDocumentEditResult VansAssetDocumentEditService::RevertToSaved(VansAssetDocument& document)
{
    std::size_t guard = 0;
    while (document.IsDirty())
    {
        if (!CanUndo(document))
            return { false, "Cannot discard asset edits because the saved document state is not in history" };
        AssetDocumentEditResult result = Undo(document);
        if (!result)
            return result;
        if (++guard > 100000)
            return { false, "Asset discard exceeded the edit-history safety limit" };
    }
    ClearHistory(document);
    return { true, {} };
}

void VansAssetDocumentEditService::ClearHistory(const VansAssetDocument& document)
{
    VansOpenAssetDocument* owner = VansAssetDocumentEditAccess::FindOwner(
		const_cast<VansAssetDocument&>(document));
	if (!owner)
		return;
	(&owner->sourceDocument == &document
		? owner->m_SourceEditHistory
		: owner->m_MetaEditHistory).reset();
}

void VansAssetDocumentEditService::ClearAllHistories()
{
	auto& registry = VansAssetDocumentRegistry::Get();
	for (auto& [path, document] : registry.m_Documents)
	{
		(void)path;
		if (!document)
			continue;
		document->m_SourceEditHistory.reset();
		document->m_MetaEditHistory.reset();
	}
}
}
