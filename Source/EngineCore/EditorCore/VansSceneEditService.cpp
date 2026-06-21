#include "VansSceneEditService.h"

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
