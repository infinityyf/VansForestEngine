#include "VansSceneEditService.h"

#include "../AssetCore/VansAssetGuid.h"

namespace Vans
{
namespace
{
using JsonPointer = SceneJson::json_pointer;

SceneEditResult ValidatePointer(const std::string& path)
{
    if (path.empty() || path.front() != '/')
        return { false, "Scene property address must be a non-root JSON Pointer" };
    try
    {
        JsonPointer pointer(path);
        (void)pointer;
    }
    catch (const SceneJson::exception& error)
    {
        return { false, error.what() };
    }
    return { true, {} };
}

bool TryRead(const SceneJson& root, const JsonPointer& pointer, SceneJson& value)
{
    try
    {
        value = root.at(pointer);
        return true;
    }
    catch (const SceneJson::out_of_range&)
    {
        return false;
    }
}

SceneEditResult RemoveAt(SceneJson& root, const JsonPointer& pointer)
{
    const JsonPointer parent = pointer.parent_pointer();
    const std::string token = pointer.back();
    try
    {
        SceneJson& container = root.at(parent);
        if (container.is_object())
        {
            if (container.erase(token) == 0)
                return { false, "Scene property does not exist" };
            return { true, {} };
        }
        if (container.is_array())
        {
            std::size_t consumed = 0;
            const std::size_t index = std::stoull(token, &consumed);
            if (consumed != token.size() || index >= container.size())
                return { false, "Invalid scene array index" };
            container.erase(container.begin() + static_cast<SceneJson::difference_type>(index));
            return { true, {} };
        }
        return { false, "Scene property parent is not a container" };
    }
    catch (const std::exception& error)
    {
        return { false, error.what() };
    }
}

SceneEditResult WriteAt(SceneJson& root, const JsonPointer& pointer, const SceneJson& value)
{
    try
    {
        root[pointer] = value;
        return { true, {} };
    }
    catch (const SceneJson::exception& error)
    {
        return { false, error.what() };
    }
}
}

VansSetScenePropertyCommand::VansSetScenePropertyCommand(std::string jsonPointer, SceneJson value)
    : m_JsonPointer(std::move(jsonPointer)), m_NewValue(std::move(value))
{
}

SceneEditResult VansSetScenePropertyCommand::Execute(VansSceneDocument& document)
{
    if (auto validation = ValidatePointer(m_JsonPointer); !validation)
        return validation;
    const JsonPointer pointer(m_JsonPointer);
    m_HadOldValue = TryRead(document.m_Root, pointer, m_OldValue);
    m_BeforeState = document.m_CurrentStateId;
    if (m_HadOldValue && m_OldValue == m_NewValue)
        return { false, "Scene property is unchanged" };
    SceneJson candidate = document.m_Root;
    if (auto result = WriteAt(candidate, pointer, m_NewValue); !result)
        return result;
    document.m_Root.swap(candidate);
    m_AfterState = document.AllocateStateId();
    document.m_CurrentStateId = m_AfterState;
    return { true, {} };
}

SceneEditResult VansSetScenePropertyCommand::Undo(VansSceneDocument& document)
{
    const JsonPointer pointer(m_JsonPointer);
    SceneJson candidate = document.m_Root;
    SceneEditResult result = m_HadOldValue ? WriteAt(candidate, pointer, m_OldValue)
                                           : RemoveAt(candidate, pointer);
    if (result)
    {
        document.m_Root.swap(candidate);
        document.m_CurrentStateId = m_BeforeState;
    }
    return result;
}

SceneEditResult VansSetScenePropertyCommand::Redo(VansSceneDocument& document)
{
    SceneJson candidate = document.m_Root;
    SceneEditResult result = WriteAt(candidate, JsonPointer(m_JsonPointer), m_NewValue);
    if (result)
    {
        document.m_Root.swap(candidate);
        document.m_CurrentStateId = m_AfterState;
    }
    return result;
}

VansRemoveScenePropertyCommand::VansRemoveScenePropertyCommand(std::string jsonPointer)
    : m_JsonPointer(std::move(jsonPointer))
{
}

SceneEditResult VansRemoveScenePropertyCommand::Execute(VansSceneDocument& document)
{
    if (auto validation = ValidatePointer(m_JsonPointer); !validation)
        return validation;
    const JsonPointer pointer(m_JsonPointer);
    if (!TryRead(document.m_Root, pointer, m_OldValue))
        return { false, "Scene property does not exist" };
    m_BeforeState = document.m_CurrentStateId;
    SceneJson candidate = document.m_Root;
    if (auto result = RemoveAt(candidate, pointer); !result)
        return result;
    document.m_Root.swap(candidate);
    m_AfterState = document.AllocateStateId();
    document.m_CurrentStateId = m_AfterState;
    return { true, {} };
}

SceneEditResult VansRemoveScenePropertyCommand::Undo(VansSceneDocument& document)
{
    SceneJson candidate = document.m_Root;
    SceneEditResult result = WriteAt(candidate, JsonPointer(m_JsonPointer), m_OldValue);
    if (result)
    {
        document.m_Root.swap(candidate);
        document.m_CurrentStateId = m_BeforeState;
    }
    return result;
}

SceneEditResult VansRemoveScenePropertyCommand::Redo(VansSceneDocument& document)
{
    SceneJson candidate = document.m_Root;
    SceneEditResult result = RemoveAt(candidate, JsonPointer(m_JsonPointer));
    if (result)
    {
        document.m_Root.swap(candidate);
        document.m_CurrentStateId = m_AfterState;
    }
    return result;
}

VansAssignAssetReferenceCommand::VansAssignAssetReferenceCommand(std::string jsonPointer,
    std::string assetGuid,
    EditorAPI::AssetType expectedType,
    bool writeObjectReference)
    : m_JsonPointer(std::move(jsonPointer))
    , m_AssetGuid(std::move(assetGuid))
    , m_ExpectedType(expectedType)
    , m_WriteObjectReference(writeObjectReference)
{
}

SceneJson VansAssignAssetReferenceCommand::BuildReferenceValue() const
{
    if (m_WriteObjectReference)
        return SceneJson{ { "guid", m_AssetGuid } };
    return m_AssetGuid;
}

SceneEditResult VansAssignAssetReferenceCommand::Execute(VansSceneDocument& document)
{
    if (auto validation = ValidatePointer(m_JsonPointer); !validation)
        return validation;
    if (m_ExpectedType == EditorAPI::AssetType::Unknown)
        return { false, "Asset reference slot has no expected asset type" };
    if (!m_AssetGuid.empty())
    {
        VansAssetGuid parsedGuid;
        if (!VansAssetGuid::TryParse(m_AssetGuid, parsedGuid))
            return { false, "Asset reference value is not a valid asset GUID" };
    }

    const JsonPointer pointer(m_JsonPointer);
    const SceneJson newValue = BuildReferenceValue();
    m_HadOldValue = TryRead(document.m_Root, pointer, m_OldValue);
    m_BeforeState = document.m_CurrentStateId;
    if (m_HadOldValue && m_OldValue == newValue)
        return { false, "Asset reference is unchanged" };

    SceneJson candidate = document.m_Root;
    if (auto result = WriteAt(candidate, pointer, newValue); !result)
        return result;
    document.m_Root.swap(candidate);
    m_AfterState = document.AllocateStateId();
    document.m_CurrentStateId = m_AfterState;
    return { true, {} };
}

SceneEditResult VansAssignAssetReferenceCommand::Undo(VansSceneDocument& document)
{
    const JsonPointer pointer(m_JsonPointer);
    SceneJson candidate = document.m_Root;
    SceneEditResult result = m_HadOldValue ? WriteAt(candidate, pointer, m_OldValue)
                                           : RemoveAt(candidate, pointer);
    if (result)
    {
        document.m_Root.swap(candidate);
        document.m_CurrentStateId = m_BeforeState;
    }
    return result;
}

SceneEditResult VansAssignAssetReferenceCommand::Redo(VansSceneDocument& document)
{
    SceneJson candidate = document.m_Root;
    SceneEditResult result = WriteAt(candidate, JsonPointer(m_JsonPointer), BuildReferenceValue());
    if (result)
    {
        document.m_Root.swap(candidate);
        document.m_CurrentStateId = m_AfterState;
    }
    return result;
}

VansAppendSceneEntitiesCommand::VansAppendSceneEntitiesCommand(
    std::vector<SceneJson> entities,
    SceneEditLifecycleHooks hooks)
    : m_Entities(std::move(entities))
    , m_Hooks(std::move(hooks))
{
}

SceneEditResult VansAppendSceneEntitiesCommand::Execute(VansSceneDocument& document)
{
    if (m_Entities.empty())
        return { false, "No scene entities to append" };
    if (!document.m_Root.contains("entities") || !document.m_Root["entities"].is_array())
        return { false, "Scene document has no entities array" };

    SceneJson candidate = document.m_Root;
    SceneJson& entities = candidate["entities"];
    m_InsertIndex = entities.size();
    m_BeforeState = document.m_CurrentStateId;
    for (const SceneJson& entity : m_Entities)
        entities.push_back(entity);

    document.m_Root.swap(candidate);
    m_AfterState = document.AllocateStateId();
    document.m_CurrentStateId = m_AfterState;
    return { true, {} };
}

SceneEditResult VansAppendSceneEntitiesCommand::Undo(VansSceneDocument& document)
{
    if (!document.m_Root.contains("entities") || !document.m_Root["entities"].is_array())
        return { false, "Scene document has no entities array" };

    SceneJson candidate = document.m_Root;
    SceneJson& entities = candidate["entities"];
    if (m_InsertIndex > entities.size() || entities.size() - m_InsertIndex < m_Entities.size())
        return { false, "Scene entity append range is no longer valid" };

    entities.erase(
        entities.begin() + static_cast<SceneJson::difference_type>(m_InsertIndex),
        entities.begin() + static_cast<SceneJson::difference_type>(m_InsertIndex + m_Entities.size()));

    document.m_Root.swap(candidate);
    document.m_CurrentStateId = m_BeforeState;
    if (m_Hooks.afterUndo)
        m_Hooks.afterUndo();
    return { true, {} };
}

SceneEditResult VansAppendSceneEntitiesCommand::Redo(VansSceneDocument& document)
{
    if (!document.m_Root.contains("entities") || !document.m_Root["entities"].is_array())
        return { false, "Scene document has no entities array" };

    SceneJson candidate = document.m_Root;
    SceneJson& entities = candidate["entities"];
    if (m_InsertIndex > entities.size())
        return { false, "Scene entity append index is no longer valid" };

    auto insertIt = entities.begin() + static_cast<SceneJson::difference_type>(m_InsertIndex);
    for (const SceneJson& entity : m_Entities)
    {
        insertIt = entities.insert(insertIt, entity);
        ++insertIt;
    }

    document.m_Root.swap(candidate);
    document.m_CurrentStateId = m_AfterState;
    if (m_Hooks.afterRedo)
        m_Hooks.afterRedo();
    return { true, {} };
}

SceneEditResult VansSceneEditService::Execute(std::unique_ptr<VansSceneEditCommand> command)
{
    if (!command)
        return { false, "Scene edit command is null" };
    SceneEditResult result = command->Execute(m_Document);
    if (!result)
        return result;
    m_Undo.push_back(std::move(command));
    m_Redo.clear();
    return result;
}

SceneEditResult VansSceneEditService::Set(const std::string& jsonPointer, SceneJson value)
{
    return Execute(std::make_unique<VansSetScenePropertyCommand>(jsonPointer, std::move(value)));
}

SceneEditResult VansSceneEditService::AssignAssetReference(const std::string& jsonPointer,
    std::string assetGuid,
    EditorAPI::AssetType expectedType,
    bool writeObjectReference)
{
    return Execute(std::make_unique<VansAssignAssetReferenceCommand>(
        jsonPointer, std::move(assetGuid), expectedType, writeObjectReference));
}

SceneEditResult VansSceneEditService::AppendEntities(std::vector<SceneJson> entities,
    SceneEditLifecycleHooks hooks)
{
    return Execute(std::make_unique<VansAppendSceneEntitiesCommand>(
        std::move(entities), std::move(hooks)));
}

SceneEditResult VansSceneEditService::Remove(const std::string& jsonPointer)
{
    return Execute(std::make_unique<VansRemoveScenePropertyCommand>(jsonPointer));
}

SceneEditResult VansSceneEditService::Undo()
{
    if (m_Undo.empty())
        return { false, "No scene edit to undo" };
    std::unique_ptr<VansSceneEditCommand> command = std::move(m_Undo.back());
    m_Undo.pop_back();
    SceneEditResult result = command->Undo(m_Document);
    if (result)
        m_Redo.push_back(std::move(command));
    else
        m_Undo.push_back(std::move(command));
    return result;
}

SceneEditResult VansSceneEditService::Redo()
{
    if (m_Redo.empty())
        return { false, "No scene edit to redo" };
    std::unique_ptr<VansSceneEditCommand> command = std::move(m_Redo.back());
    m_Redo.pop_back();
    SceneEditResult result = command->Redo(m_Document);
    if (result)
        m_Undo.push_back(std::move(command));
    else
        m_Redo.push_back(std::move(command));
    return result;
}

void VansSceneEditService::ClearHistory()
{
    m_Undo.clear();
    m_Redo.clear();
}
}
