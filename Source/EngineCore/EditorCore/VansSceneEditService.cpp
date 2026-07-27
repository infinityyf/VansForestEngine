#include "VansSceneEditService.h"

#include "VansSceneObjectReferenceResolver.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace Vans
{
class VansSceneEditCommand
{
    friend class VansSceneEditService;

public:
    virtual ~VansSceneEditCommand() = default;

private:
    virtual SceneEditResult Execute(VansSceneDocument& document) = 0;
    virtual SceneEditResult Undo(VansSceneDocument& document) = 0;
    virtual SceneEditResult Redo(VansSceneDocument& document) = 0;
};

class VansSetScenePropertyCommand final : public VansSceneEditCommand
{
public:
    VansSetScenePropertyCommand(std::string propertyPointer, VansSerializedValue value);

private:
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

    std::string m_PropertyPointer;
    VansSerializedValue m_NewValue;
    VansSerializedValue m_OldValue;
    bool m_HadOldValue = false;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansRemoveScenePropertyCommand final : public VansSceneEditCommand
{
public:
    explicit VansRemoveScenePropertyCommand(std::string propertyPointer);

private:
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

    std::string m_PropertyPointer;
    VansSerializedValue m_OldValue;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansAppendSceneEntitiesCommand final : public VansSceneEditCommand
{
public:
    explicit VansAppendSceneEntitiesCommand(std::vector<VansSerializedValue> entities,
        SceneEditLifecycleHooks hooks = {});

private:
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

    std::vector<VansSerializedValue> m_Entities;
    SceneEditLifecycleHooks m_Hooks;
    std::size_t m_InsertIndex = 0;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

namespace
{
SceneEditResult ValidatePointer(const std::string& path)
{
    if (path.empty() || path.front() != '/')
        return { false, "Scene property address must be a non-root JSON Pointer" };
    for (std::size_t index = 0; index < path.size(); ++index)
    {
        if (path[index] == '~' &&
            (index + 1 >= path.size() || (path[index + 1] != '0' && path[index + 1] != '1')))
        {
            return { false, "Scene property address contains an invalid JSON Pointer escape" };
        }
    }
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

SceneEditResult RemoveAt(VansSerializedValue& root, const std::string& pointer)
{
    std::string error;
    if (EraseSerializedPointer(root, pointer, &error))
        return { true, {} };
    return { false, error.empty() ? "Scene property does not exist" : error };
}

SceneEditResult WriteAt(
    VansSerializedValue& root,
    const std::string& pointer,
    VansSerializedValue value)
{
    std::string error;
    if (SetSerializedPointer(root, pointer, std::move(value), &error))
        return { true, {} };
    return { false, error };
}

}

VansSceneEditService::VansSceneEditService(VansSceneDocument& document)
    : m_Document(document)
{
}

VansSceneEditService::~VansSceneEditService() = default;

VansSetScenePropertyCommand::VansSetScenePropertyCommand(std::string propertyPointer, VansSerializedValue value)
    : m_PropertyPointer(std::move(propertyPointer)), m_NewValue(std::move(value))
{
}

SceneEditResult VansSetScenePropertyCommand::Execute(VansSceneDocument& document)
{
    if (auto validation = ValidatePointer(m_PropertyPointer); !validation)
        return validation;
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    VansSerializedValue oldValue;
    m_HadOldValue = TryRead(candidate, m_PropertyPointer, oldValue);
    m_BeforeState = document.m_CurrentStateId;
    if (m_HadOldValue && SerializedValuesEqual(oldValue, m_NewValue))
        return { false, "Scene property is unchanged" };
    if (m_HadOldValue)
        m_OldValue = std::move(oldValue);
    if (auto result = WriteAt(candidate, m_PropertyPointer, m_NewValue); !result)
        return result;
    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    return { true, {} };
}

SceneEditResult VansSetScenePropertyCommand::Undo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = m_HadOldValue
        ? WriteAt(candidate, m_PropertyPointer, m_OldValue)
        : RemoveAt(candidate, m_PropertyPointer);
    if (result)
        document.RestoreEditedSerializedRoot(std::move(candidate), m_BeforeState);
    return result;
}

SceneEditResult VansSetScenePropertyCommand::Redo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = WriteAt(candidate, m_PropertyPointer, m_NewValue);
    if (result)
        document.RestoreEditedSerializedRoot(std::move(candidate), m_AfterState);
    return result;
}

VansRemoveScenePropertyCommand::VansRemoveScenePropertyCommand(std::string propertyPointer)
    : m_PropertyPointer(std::move(propertyPointer))
{
}

SceneEditResult VansRemoveScenePropertyCommand::Execute(VansSceneDocument& document)
{
    if (auto validation = ValidatePointer(m_PropertyPointer); !validation)
        return validation;
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    VansSerializedValue oldValue;
    if (!TryRead(candidate, m_PropertyPointer, oldValue))
        return { false, "Scene property does not exist" };
    m_OldValue = std::move(oldValue);
    m_BeforeState = document.m_CurrentStateId;
    if (auto result = RemoveAt(candidate, m_PropertyPointer); !result)
        return result;
    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    return { true, {} };
}

SceneEditResult VansRemoveScenePropertyCommand::Undo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = WriteAt(candidate, m_PropertyPointer, m_OldValue);
    if (result)
        document.RestoreEditedSerializedRoot(std::move(candidate), m_BeforeState);
    return result;
}

SceneEditResult VansRemoveScenePropertyCommand::Redo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = RemoveAt(candidate, m_PropertyPointer);
    if (result)
        document.RestoreEditedSerializedRoot(std::move(candidate), m_AfterState);
    return result;
}

VansAppendSceneEntitiesCommand::VansAppendSceneEntitiesCommand(
    std::vector<VansSerializedValue> entities,
    SceneEditLifecycleHooks hooks)
    : m_Entities(std::move(entities))
    , m_Hooks(std::move(hooks))
{
}

SceneEditResult VansAppendSceneEntitiesCommand::Execute(VansSceneDocument& document)
{
    if (m_Entities.empty())
        return { false, "No scene entities to append" };
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    VansSerializedValue* entities = FindObjectField(candidate, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
        return { false, "Scene document has no entities array" };

    m_InsertIndex = entities->arrayItems.size();
    m_BeforeState = document.m_CurrentStateId;
    for (const VansSerializedValue& entity : m_Entities)
        entities->arrayItems.push_back(entity);

    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    return { true, {} };
}

SceneEditResult VansAppendSceneEntitiesCommand::Undo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    VansSerializedValue* entities = FindObjectField(candidate, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
        return { false, "Scene document has no entities array" };

    if (m_InsertIndex > entities->arrayItems.size() ||
        entities->arrayItems.size() - m_InsertIndex < m_Entities.size())
    {
        return { false, "Scene entity append range is no longer valid" };
    }

    entities->arrayItems.erase(
        entities->arrayItems.begin() + static_cast<std::ptrdiff_t>(m_InsertIndex),
        entities->arrayItems.begin() + static_cast<std::ptrdiff_t>(m_InsertIndex + m_Entities.size()));

    document.RestoreEditedSerializedRoot(std::move(candidate), m_BeforeState);
    if (m_Hooks.afterUndo)
        m_Hooks.afterUndo();
    return { true, {} };
}

SceneEditResult VansAppendSceneEntitiesCommand::Redo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    VansSerializedValue* entities = FindObjectField(candidate, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
        return { false, "Scene document has no entities array" };

    if (m_InsertIndex > entities->arrayItems.size())
        return { false, "Scene entity append index is no longer valid" };

    auto insertIt = entities->arrayItems.begin() + static_cast<std::ptrdiff_t>(m_InsertIndex);
    for (const VansSerializedValue& entity : m_Entities)
    {
        insertIt = entities->arrayItems.insert(insertIt, entity);
        ++insertIt;
    }

    document.RestoreEditedSerializedRoot(std::move(candidate), m_AfterState);
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

SceneEditResult VansSceneEditService::Set(const std::string& propertyPointer, VansSerializedValue value)
{
    return Execute(std::make_unique<VansSetScenePropertyCommand>(propertyPointer, std::move(value)));
}

SceneEditResult VansSceneEditService::Set(const DocumentPropertyPath& path, VansSerializedValue value)
{
    if (path.space != DocumentPropertySpace::Scene)
        return { false, "Scene edit target is not a scene document path" };
    std::string pathError;
    if (!ValidateDocumentPropertyPath(path, &pathError))
        return { false, pathError };
    return Set(ToDocumentPropertyPointer(path), std::move(value));
}

SceneEditResult VansSceneEditService::SetAndAssignObjectReference(
    const DocumentPropertyPath& path,
    VansSerializedValue value,
    const ObjectReferenceAssignment& assignment)
{
    if (path.space != DocumentPropertySpace::Scene ||
        assignment.targetPath.space != DocumentPropertySpace::Scene)
    {
        return { false, "Scene object reference transaction targets must be scene document paths" };
    }
    std::string pathError;
    if (!ValidateDocumentPropertyPath(path, &pathError) ||
        !ValidateDocumentPropertyPath(assignment.targetPath, &pathError))
    {
        return { false, pathError };
    }
    VansSerializedValue referenceValue;
    std::string assignmentError;
    if (!TryEncodeSceneDocumentObjectReferenceAssignment(
        m_Document,
        assignment,
        referenceValue,
        &assignmentError))
    {
        return { false, assignmentError };
    }

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

    return Execute(std::make_unique<VansSetScenePropertyCommand>(
        ToDocumentPropertyPointer(path), std::move(value)));
}

SceneEditResult VansSceneEditService::AssignObjectReference(const ObjectReferenceAssignment& assignment)
{
    if (assignment.targetPath.space != DocumentPropertySpace::Scene)
        return { false, "Object reference assignment target is not a scene document path" };
    std::string pathError;
    if (!ValidateDocumentPropertyPath(assignment.targetPath, &pathError))
        return { false, pathError };
    const std::string propertyPointer = ToDocumentPropertyPointer(assignment.targetPath);
    if (auto validation = ValidatePointer(propertyPointer); !validation)
        return validation;

    VansSerializedValue referenceValue;
    std::string assignmentError;
    if (!TryEncodeSceneDocumentObjectReferenceAssignment(
        m_Document,
        assignment,
        referenceValue,
        &assignmentError))
    {
        return { false, assignmentError };
    }

    return Set(propertyPointer, std::move(referenceValue));
}

SceneEditResult VansSceneEditService::AppendEntities(std::vector<VansSerializedValue> entities,
    SceneEditLifecycleHooks hooks)
{
    for (const VansSerializedValue& entity : entities)
    {
        if (entity.kind != VansSerializedValue::Kind::Object)
            return { false, "Scene entity append payload must contain objects" };
    }
    return Execute(std::make_unique<VansAppendSceneEntitiesCommand>(
        std::move(entities), std::move(hooks)));
}

SceneEditResult VansSceneEditService::Remove(const std::string& propertyPointer)
{
    return Execute(std::make_unique<VansRemoveScenePropertyCommand>(propertyPointer));
}

SceneEditResult VansSceneEditService::Remove(const DocumentPropertyPath& path)
{
    if (path.space != DocumentPropertySpace::Scene)
        return { false, "Scene remove target is not a scene document path" };
    std::string pathError;
    if (!ValidateDocumentPropertyPath(path, &pathError))
        return { false, pathError };
    return Remove(ToDocumentPropertyPointer(path));
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
