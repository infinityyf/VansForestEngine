#include "VansAssetDocumentEditService.h"
#include "VansAssetDocumentRegistry.h"

#include "../AssetCore/VansAssetDocument.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <exception>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
namespace
{
std::wstring HistoryKey(const VansAssetDocument& document)
{
    return document.Path().generic_wstring();
}

AssetDocumentEditResult ValidatePointer(const std::string& path)
{
	if (path.empty())
		return { true, {} };
	if (path.front() != '/')
        return { false, "Asset property address must be a non-root JSON Pointer" };
    for (std::size_t index = 0; index < path.size(); ++index)
    {
        if (path[index] == '~' &&
            (index + 1 >= path.size() || (path[index + 1] != '0' && path[index + 1] != '1')))
        {
            return { false, "Asset property address contains an invalid JSON Pointer escape" };
        }
    }
    return { true, {} };
}

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
        if (auto validation = ValidatePointer(m_PropertyPointer); !validation)
            return validation;

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
}

namespace
{
struct AssetDocumentEditHistory
{
    std::vector<std::unique_ptr<AssetDocumentEditCommand>> undo;
    std::vector<std::unique_ptr<AssetDocumentEditCommand>> redo;
};

std::unordered_map<std::wstring, AssetDocumentEditHistory>& Histories()
{
    static std::unordered_map<std::wstring, AssetDocumentEditHistory> histories;
    return histories;
}

AssetDocumentEditResult ExecuteCommand(
    VansAssetDocument& document,
    std::unique_ptr<AssetDocumentEditCommand> command)
{
    if (!command)
        return { false, "Asset edit command is null" };
    AssetDocumentEditResult result = command->Execute(document);
    if (!result)
        return result;

    AssetDocumentEditHistory& history = Histories()[HistoryKey(document)];
    history.undo.push_back(std::move(command));
    history.redo.clear();
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
    const auto found = Histories().find(HistoryKey(document));
    return found != Histories().end() && !found->second.undo.empty();
}

bool VansAssetDocumentEditService::CanRedo(const VansAssetDocument& document)
{
    const auto found = Histories().find(HistoryKey(document));
    return found != Histories().end() && !found->second.redo.empty();
}

AssetDocumentEditResult VansAssetDocumentEditService::Undo(VansAssetDocument& document)
{
    AssetDocumentEditHistory& history = Histories()[HistoryKey(document)];
    if (history.undo.empty())
        return { false, "No asset edit to undo" };
    std::unique_ptr<AssetDocumentEditCommand> command = std::move(history.undo.back());
    history.undo.pop_back();
    AssetDocumentEditResult result = command->Undo(document);
    if (result)
	{
        history.redo.push_back(std::move(command));
		VansAssetDocumentRegistry::Get().PublishWorkingCopy(document);
	}
    else
        history.undo.push_back(std::move(command));
    return result;
}

AssetDocumentEditResult VansAssetDocumentEditService::Redo(VansAssetDocument& document)
{
    AssetDocumentEditHistory& history = Histories()[HistoryKey(document)];
    if (history.redo.empty())
        return { false, "No asset edit to redo" };
    std::unique_ptr<AssetDocumentEditCommand> command = std::move(history.redo.back());
    history.redo.pop_back();
    AssetDocumentEditResult result = command->Redo(document);
    if (result)
	{
        history.undo.push_back(std::move(command));
		VansAssetDocumentRegistry::Get().PublishWorkingCopy(document);
	}
    else
        history.redo.push_back(std::move(command));
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
    Histories().erase(HistoryKey(document));
}

void VansAssetDocumentEditService::ClearAllHistories()
{
    Histories().clear();
}
}
