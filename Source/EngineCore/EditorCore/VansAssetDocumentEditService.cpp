#include "VansAssetDocumentEditService.h"

#include "../AssetCore/VansAssetGuid.h"

#include <exception>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
namespace
{
using JsonPointer = VansAssetDocument::Json::json_pointer;

std::wstring HistoryKey(const VansAssetDocument& document)
{
    return document.Path().generic_wstring();
}

AssetDocumentEditResult ValidatePointer(const std::string& path)
{
    if (path.empty() || path.front() != '/')
        return { false, "Asset property address must be a non-root JSON Pointer" };
    try
    {
        JsonPointer pointer(path);
        (void)pointer;
    }
    catch (const VansAssetDocument::Json::exception& error)
    {
        return { false, error.what() };
    }
    return { true, {} };
}

VansAssetDocument::Json BuildReferenceValue(const std::string& assetGuid, bool writeObjectReference)
{
    if (writeObjectReference)
        return VansAssetDocument::Json{ { "guid", assetGuid } };
    return assetGuid;
}

bool TryRead(const VansAssetDocument::Json& root, const JsonPointer& pointer, VansAssetDocument::Json& value)
{
    try
    {
        value = root.at(pointer);
        return true;
    }
    catch (const VansAssetDocument::Json::out_of_range&)
    {
        return false;
    }
}

AssetDocumentEditResult RemoveAt(VansAssetDocument::Json& root, const JsonPointer& pointer)
{
    const JsonPointer parent = pointer.parent_pointer();
    const std::string token = pointer.back();
    try
    {
        VansAssetDocument::Json& container = root.at(parent);
        if (container.is_object())
        {
            if (container.erase(token) == 0)
                return { false, "Asset property does not exist" };
            return { true, {} };
        }
        if (container.is_array())
        {
            std::size_t consumed = 0;
            const std::size_t index = std::stoull(token, &consumed);
            if (consumed != token.size() || index >= container.size())
                return { false, "Invalid asset array index" };
            container.erase(container.begin() + static_cast<VansAssetDocument::Json::difference_type>(index));
            return { true, {} };
        }
        return { false, "Asset property parent is not a container" };
    }
    catch (const std::exception& error)
    {
        return { false, error.what() };
    }
}

AssetDocumentEditResult WriteAt(
    VansAssetDocument& document,
    const JsonPointer& pointer,
    const VansAssetDocument::Json& value)
{
    try
    {
        document.Root()[pointer] = value;
        document.MarkDirty();
        return { true, {} };
    }
    catch (const VansAssetDocument::Json::exception& error)
    {
        return { false, error.what() };
    }
}

class AssetDocumentEditCommand
{
public:
    virtual ~AssetDocumentEditCommand() = default;
    virtual AssetDocumentEditResult Execute(VansAssetDocument& document) = 0;
    virtual AssetDocumentEditResult Undo(VansAssetDocument& document) = 0;
    virtual AssetDocumentEditResult Redo(VansAssetDocument& document) = 0;
};

class SetAssetReferenceCommand final : public AssetDocumentEditCommand
{
public:
    SetAssetReferenceCommand(std::string jsonPointer, std::string assetGuid, bool writeObjectReference)
        : m_JsonPointer(std::move(jsonPointer))
        , m_AssetGuid(std::move(assetGuid))
        , m_WriteObjectReference(writeObjectReference)
    {
    }

    AssetDocumentEditResult Execute(VansAssetDocument& document) override
    {
        if (!document.IsLoaded())
            return { false, "Asset document is not loaded" };
        if (auto validation = ValidatePointer(m_JsonPointer); !validation)
            return validation;
        if (!m_AssetGuid.empty())
        {
            VansAssetGuid parsedGuid;
            if (!VansAssetGuid::TryParse(m_AssetGuid, parsedGuid))
                return { false, "Asset reference value is not a valid asset GUID" };
        }

        const JsonPointer pointer(m_JsonPointer);
        m_NewValue = BuildReferenceValue(m_AssetGuid, m_WriteObjectReference);
        m_HadOldValue = TryRead(document.Root(), pointer, m_OldValue);
        if (m_HadOldValue && m_OldValue == m_NewValue)
            return { false, "Asset reference is unchanged" };
        return WriteAt(document, pointer, m_NewValue);
    }

    AssetDocumentEditResult Undo(VansAssetDocument& document) override
    {
        const JsonPointer pointer(m_JsonPointer);
        if (m_HadOldValue)
            return WriteAt(document, pointer, m_OldValue);

        VansAssetDocument::Json candidate = document.Root();
        AssetDocumentEditResult result = RemoveAt(candidate, pointer);
        if (result)
        {
            document.Root().swap(candidate);
            document.MarkDirty();
        }
        return result;
    }

    AssetDocumentEditResult Redo(VansAssetDocument& document) override
    {
        return WriteAt(document, JsonPointer(m_JsonPointer), m_NewValue);
    }

private:
    std::string m_JsonPointer;
    std::string m_AssetGuid;
    bool m_WriteObjectReference = true;
    VansAssetDocument::Json m_NewValue;
    VansAssetDocument::Json m_OldValue;
    bool m_HadOldValue = false;
};

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
    return result;
}
}

AssetDocumentEditResult VansAssetDocumentEditService::SetAssetReference(
    VansAssetDocument& document,
    const std::string& jsonPointer,
    const std::string& assetGuid,
    bool writeObjectReference)
{
    return ExecuteCommand(document, std::make_unique<SetAssetReferenceCommand>(
        jsonPointer, assetGuid, writeObjectReference));
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
        history.redo.push_back(std::move(command));
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
        history.undo.push_back(std::move(command));
    else
        history.redo.push_back(std::move(command));
    return result;
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
