#include "VansSceneEditService.h"

#include "VansSceneObjectReferenceResolver.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneParentReference.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

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
    explicit VansRemoveScenePropertyCommand(std::string propertyPointer,
        SceneEditLifecycleHooks hooks = {});

private:
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

    std::string m_PropertyPointer;
    VansSerializedValue m_OldValue;
    SceneEditLifecycleHooks m_Hooks;
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

class VansReparentSceneEntityCommand final : public VansSceneEditCommand
{
public:
    VansReparentSceneEntityCommand(
        std::string childEntityGuid,
        std::optional<VansSceneParentReference> newParent,
        ReparentTransformPolicy transformPolicy,
        std::optional<EditorAPI::RuntimeTransformSnapshot> resolvedLocalTransform);

private:
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

    std::string m_ChildEntityGuid;
    std::optional<VansSceneParentReference> m_NewParent;
    std::optional<VansSceneParentReference> m_OldParent;
    ReparentTransformPolicy m_TransformPolicy = ReparentTransformPolicy::KeepWorld;
    std::optional<EditorAPI::RuntimeTransformSnapshot> m_ResolvedLocalTransform;
    VansSerializedValue m_BeforeRoot;
    VansSerializedValue m_AfterRoot;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

class VansSetSceneEntityTransformCommand final : public VansSceneEditCommand
{
public:
    VansSetSceneEntityTransformCommand(
        std::string entityGuid,
        EditorAPI::RuntimeTransformSnapshot transform);

private:
    SceneEditResult Execute(VansSceneDocument& document) override;
    SceneEditResult Undo(VansSceneDocument& document) override;
    SceneEditResult Redo(VansSceneDocument& document) override;

    std::string m_EntityGuid;
    EditorAPI::RuntimeTransformSnapshot m_Transform;
    VansSerializedValue m_BeforeRoot;
    VansSerializedValue m_AfterRoot;
    SceneStateId m_BeforeState = 0;
    SceneStateId m_AfterState = 0;
};

namespace
{
struct EntityHierarchyRecord
{
    std::size_t index = 0;
    std::string parent;
    std::optional<VansSceneParentReference> parentReference;
};

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

std::string TryReadEntityGuidForPointer(
    const VansSerializedValue& root,
    const std::string& pointer)
{
    const std::vector<std::string> tokens = SplitSerializedPointer(pointer);
    if (tokens.size() < 2 || tokens[0] != "entities")
        return {};
    std::size_t entityIndex = 0;
    try
    {
        entityIndex = static_cast<std::size_t>(std::stoull(tokens[1]));
    }
    catch (...)
    {
        return {};
    }

    const VansSerializedValue* entities = FindObjectField(root, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array ||
        entityIndex >= entities->arrayItems.size())
    {
        return {};
    }
    return ReadSerializedStringField(entities->arrayItems[entityIndex], "id");
}

SceneEditResult RuntimePreviewEditResult(
    std::string entityGuid,
    std::string message = {})
{
    SceneEditResult result{ true, std::move(message) };
    result.changedEntityGuid = std::move(entityGuid);
    result.runtimePreviewSupported = !result.changedEntityGuid.empty();
    return result;
}

SceneEditResult RuntimeParentPreviewEditResult(
    std::string entityGuid,
    const std::optional<VansSceneParentReference>& parent,
    std::string message = {})
{
    SceneEditResult result{ true, std::move(message) };
    result.changedEntityGuid = std::move(entityGuid);
    if (parent)
    {
        result.changedParent.entityGuid = parent->entityGuid.ToString();
        if (parent->IsEntity())
            result.changedParent.kind = EditorAPI::RuntimeParentKind::Entity;
        else
        {
            result.changedParent.kind = parent->kind == VansSceneParentKind::Bone
                ? EditorAPI::RuntimeParentKind::Bone : EditorAPI::RuntimeParentKind::Socket;
            result.changedParent.animationComponentGuid = parent->animationComponentGuid.ToString();
            result.changedParent.anchorGuid = parent->anchorGuid.ToString();
        }
    }
    result.runtimeParentPreviewSupported = !result.changedEntityGuid.empty();
    result.runtimePreviewSupported = result.runtimeParentPreviewSupported;
    return result;
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

bool TryBuildEntityHierarchy(
    const VansSerializedValue& root,
    std::unordered_map<std::string, EntityHierarchyRecord>& outEntities,
    std::string& error)
{
    const VansSerializedValue* entities = FindObjectField(root, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
    {
        error = "Scene document has no entities array";
        return false;
    }

    for (std::size_t index = 0; index < entities->arrayItems.size(); ++index)
    {
        const VansSerializedValue& entity = entities->arrayItems[index];
        if (entity.kind != VansSerializedValue::Kind::Object)
        {
            error = "Scene entity is not an object";
            return false;
        }

        const std::string entityId = ReadSerializedStringField(entity, "id");
        if (entityId.empty())
        {
            error = "Scene entity is missing an id";
            return false;
        }

        const VansSerializedValue* parentField = FindObjectField(entity, "parent");
        EntityHierarchyRecord record;
        record.index = index;
		if (parentField && !parentField->IsNull())
		{
			VansSceneParentReference parent;
			std::string parentError;
			if (!TryReadSceneParentReference(*parentField, parent, parentError))
			{
				error = parentError;
				return false;
			}
			record.parent = parent.entityGuid.ToString();
			record.parentReference = std::move(parent);
		}
        outEntities[entityId] = std::move(record);
    }

    return true;
}

bool IsDescendantOf(
    const std::unordered_map<std::string, EntityHierarchyRecord>& entities,
    const std::string& candidateChild,
    const std::string& candidateAncestor)
{
    std::string cursor = candidateChild;
    for (;;)
    {
        const auto found = entities.find(cursor);
        if (found == entities.end() || found->second.parent.empty())
            return false;
        if (found->second.parent == candidateAncestor)
            return true;
        cursor = found->second.parent;
    }
}

VansSerializedValue TransformVec3(float x, float y, float z)
{
    return VansSerializedValue::Array({
        VansSerializedValue::Float(x),
        VansSerializedValue::Float(y),
        VansSerializedValue::Float(z)
    });
}

VansSerializedValue TransformQuat(const glm::quat& value)
{
    const glm::quat normalized = glm::normalize(value);
    return VansSerializedValue::Array({
        VansSerializedValue::Float(normalized.x),
        VansSerializedValue::Float(normalized.y),
        VansSerializedValue::Float(normalized.z),
        VansSerializedValue::Float(normalized.w)
    });
}

SceneEditResult ApplyEntityTransform(
    VansSerializedValue& root,
    const std::string& entityGuid,
    const EditorAPI::RuntimeTransformSnapshot& transform);

SceneEditResult ApplyEntityParent(
    VansSerializedValue& root,
    const std::string& childEntityGuid,
    const std::optional<VansSceneParentReference>& newParent,
    const std::optional<EditorAPI::RuntimeTransformSnapshot>& resolvedLocalTransform)
{
	const std::string newParentEntityGuid = newParent
		? newParent->entityGuid.ToString() : std::string{};
    if (childEntityGuid.empty())
        return { false, "Child entity id must not be empty" };
    if (childEntityGuid == newParentEntityGuid)
        return { false, "An entity cannot be parented to itself" };

    std::unordered_map<std::string, EntityHierarchyRecord> entitiesById;
    std::string error;
    if (!TryBuildEntityHierarchy(root, entitiesById, error))
        return { false, error };

    const auto child = entitiesById.find(childEntityGuid);
    if (child == entitiesById.end())
        return { false, "Child entity does not exist" };
    if (!newParentEntityGuid.empty() && entitiesById.find(newParentEntityGuid) == entitiesById.end())
        return { false, "Parent entity does not exist" };
    if (!newParentEntityGuid.empty() &&
        IsDescendantOf(entitiesById, newParentEntityGuid, childEntityGuid))
    {
        return { false, "Cannot parent an entity to one of its descendants" };
    }
    VansSerializedValue* entities = FindObjectField(root, "entities");
    VansSerializedValue& childEntity = entities->arrayItems[child->second.index];
	const VansSerializedValue* currentParent = FindObjectField(childEntity, "parent");
	const VansSerializedValue nextParent = newParent
		? WriteSceneParentReference(*newParent) : VansSerializedValue::Null();
	if (currentParent && SerializedValuesEqual(*currentParent, nextParent)
		&& !resolvedLocalTransform)
		return { false, "Scene entity parent is unchanged" };
    SetSerializedObjectField(
        childEntity,
        "parent",
		nextParent);
	if (resolvedLocalTransform)
	{
		EditorAPI::RuntimeTransformSnapshot local = *resolvedLocalTransform;
		local.space = EditorAPI::RuntimeTransformSpace::Local;
		if (SceneEditResult transformResult = ApplyEntityTransform(root, childEntityGuid, local);
			!transformResult)
			return transformResult;
	}
    return { true, {} };
}

SceneEditResult ApplyEntityTransform(
    VansSerializedValue& root,
    const std::string& entityGuid,
    const EditorAPI::RuntimeTransformSnapshot& transform)
{
    if (entityGuid.empty())
        return { false, "Scene entity id must not be empty" };

    VansSerializedValue* entities = FindObjectField(root, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
        return { false, "Scene document has no entities array" };

    for (VansSerializedValue& entity : entities->arrayItems)
    {
        if (ReadSerializedStringField(entity, "id") != entityGuid)
            continue;

        VansSerializedValue* components = FindObjectField(entity, "components");
        if (!components || components->kind != VansSerializedValue::Kind::Array)
            return { false, "Scene entity has no components array" };

        for (VansSerializedValue& component : components->arrayItems)
        {
            if (ReadSerializedStringField(component, "type") != "Transform")
                continue;

            VansSerializedValue* data = FindObjectField(component, "data");
            if (!data || data->kind != VansSerializedValue::Kind::Object)
            {
                SetSerializedObjectField(component, "data", VansSerializedValue::Object({}));
                data = FindObjectField(component, "data");
            }
            if (!data)
                return { false, "Could not create Transform component data" };

            SetSerializedObjectField(*data, "position",
                TransformVec3(transform.position.x, transform.position.y, transform.position.z));
            const glm::quat rotation = glm::quat(glm::radians(glm::vec3(
                transform.rotationDegrees.x,
                transform.rotationDegrees.y,
                transform.rotationDegrees.z)));
            SetSerializedObjectField(*data, "rotation", TransformQuat(rotation));
            SetSerializedObjectField(*data, "scale",
                TransformVec3(transform.scale.x, transform.scale.y, transform.scale.z));
            return { true, {} };
        }
        return { false, "Scene entity has no Transform component" };
    }

    return { false, "Scene entity does not exist" };
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
    const std::string changedEntityGuid = TryReadEntityGuidForPointer(candidate, m_PropertyPointer);
    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    return RuntimePreviewEditResult(changedEntityGuid);
}

SceneEditResult VansSetScenePropertyCommand::Undo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = m_HadOldValue
        ? WriteAt(candidate, m_PropertyPointer, m_OldValue)
        : RemoveAt(candidate, m_PropertyPointer);
    if (result)
    {
        const std::string changedEntityGuid = TryReadEntityGuidForPointer(candidate, m_PropertyPointer);
        document.RestoreEditedSerializedRoot(std::move(candidate), m_BeforeState);
        return RuntimePreviewEditResult(changedEntityGuid);
    }
    return result;
}

SceneEditResult VansSetScenePropertyCommand::Redo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = WriteAt(candidate, m_PropertyPointer, m_NewValue);
    if (result)
    {
        const std::string changedEntityGuid = TryReadEntityGuidForPointer(candidate, m_PropertyPointer);
        document.RestoreEditedSerializedRoot(std::move(candidate), m_AfterState);
        return RuntimePreviewEditResult(changedEntityGuid);
    }
    return result;
}

VansRemoveScenePropertyCommand::VansRemoveScenePropertyCommand(
    std::string propertyPointer,
    SceneEditLifecycleHooks hooks)
    : m_PropertyPointer(std::move(propertyPointer))
    , m_Hooks(std::move(hooks))
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
    const std::string changedEntityGuid = TryReadEntityGuidForPointer(candidate, m_PropertyPointer);
    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    if (m_Hooks.afterExecute)
    {
        SceneEditResult result = RuntimePreviewEditResult(changedEntityGuid);
        result.runtimeChangeApplied = m_Hooks.afterExecute();
        return result;
    }
    return RuntimePreviewEditResult(changedEntityGuid);
}

SceneEditResult VansRemoveScenePropertyCommand::Undo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = WriteAt(candidate, m_PropertyPointer, m_OldValue);
    if (result)
    {
        const std::string changedEntityGuid = TryReadEntityGuidForPointer(candidate, m_PropertyPointer);
        document.RestoreEditedSerializedRoot(std::move(candidate), m_BeforeState);
        if (m_Hooks.afterUndo)
        {
            SceneEditResult hookResult = RuntimePreviewEditResult(changedEntityGuid);
            hookResult.runtimeChangeApplied = m_Hooks.afterUndo();
            return hookResult;
        }
        return RuntimePreviewEditResult(changedEntityGuid);
    }
    return result;
}

SceneEditResult VansRemoveScenePropertyCommand::Redo(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    SceneEditResult result = RemoveAt(candidate, m_PropertyPointer);
    if (result)
    {
        const std::string changedEntityGuid = TryReadEntityGuidForPointer(candidate, m_PropertyPointer);
        document.RestoreEditedSerializedRoot(std::move(candidate), m_AfterState);
        if (m_Hooks.afterRedo)
        {
            SceneEditResult hookResult = RuntimePreviewEditResult(changedEntityGuid);
            hookResult.runtimeChangeApplied = m_Hooks.afterRedo();
            return hookResult;
        }
        return RuntimePreviewEditResult(changedEntityGuid);
    }
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
    if (m_Hooks.afterExecute)
    {
        SceneEditResult result{ true, {} };
        result.runtimeChangeApplied = m_Hooks.afterExecute();
        return result;
    }
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
    {
        SceneEditResult result{ true, {} };
        result.runtimeChangeApplied = m_Hooks.afterUndo();
        return result;
    }
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
    {
        SceneEditResult result{ true, {} };
        result.runtimeChangeApplied = m_Hooks.afterRedo();
        return result;
    }
    return { true, {} };
}

VansReparentSceneEntityCommand::VansReparentSceneEntityCommand(
    std::string childEntityGuid,
    std::optional<VansSceneParentReference> newParent,
    ReparentTransformPolicy transformPolicy,
    std::optional<EditorAPI::RuntimeTransformSnapshot> resolvedLocalTransform)
    : m_ChildEntityGuid(std::move(childEntityGuid))
    , m_NewParent(std::move(newParent))
    , m_TransformPolicy(transformPolicy)
    , m_ResolvedLocalTransform(std::move(resolvedLocalTransform))
{
}

SceneEditResult VansReparentSceneEntityCommand::Execute(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    m_BeforeRoot = candidate;
    m_BeforeState = document.m_CurrentStateId;
    std::unordered_map<std::string, EntityHierarchyRecord> entitiesById;
    std::string hierarchyError;
    if (!TryBuildEntityHierarchy(m_BeforeRoot, entitiesById, hierarchyError))
        return { false, hierarchyError };
    const auto childIt = entitiesById.find(m_ChildEntityGuid);
    if (childIt == entitiesById.end())
        return { false, "Child entity does not exist" };
	m_OldParent = childIt->second.parentReference;
	if (m_TransformPolicy == ReparentTransformPolicy::KeepWorld && !m_ResolvedLocalTransform)
		return { false, "KeepWorld reparent requires the resolved runtime-local transform" };
	std::optional<EditorAPI::RuntimeTransformSnapshot> serializedLocalTransform =
		m_ResolvedLocalTransform;
	if (m_TransformPolicy == ReparentTransformPolicy::Snap)
	{
		EditorAPI::RuntimeTransformSnapshot identity;
		identity.available = true;
		identity.entityGuid = m_ChildEntityGuid;
		identity.space = EditorAPI::RuntimeTransformSpace::Local;
		identity.scale = { 1.0f, 1.0f, 1.0f };
		serializedLocalTransform = std::move(identity);
	}

    if (SceneEditResult result = ApplyEntityParent(
        candidate,
        m_ChildEntityGuid,
		m_NewParent,
		serializedLocalTransform); !result)
    {
        return result;
    }

    m_AfterRoot = candidate;
    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    return RuntimeParentPreviewEditResult(m_ChildEntityGuid, m_NewParent);
}

SceneEditResult VansReparentSceneEntityCommand::Undo(VansSceneDocument& document)
{
    document.RestoreEditedSerializedRoot(m_BeforeRoot, m_BeforeState);
    return RuntimeParentPreviewEditResult(m_ChildEntityGuid, m_OldParent);
}

SceneEditResult VansReparentSceneEntityCommand::Redo(VansSceneDocument& document)
{
    document.RestoreEditedSerializedRoot(m_AfterRoot, m_AfterState);
    return RuntimeParentPreviewEditResult(m_ChildEntityGuid, m_NewParent);
}

VansSetSceneEntityTransformCommand::VansSetSceneEntityTransformCommand(
    std::string entityGuid,
    EditorAPI::RuntimeTransformSnapshot transform)
    : m_EntityGuid(std::move(entityGuid))
    , m_Transform(std::move(transform))
{
}

SceneEditResult VansSetSceneEntityTransformCommand::Execute(VansSceneDocument& document)
{
    VansSerializedValue candidate = document.SerializedRootSnapshot();
    m_BeforeRoot = candidate;
    m_BeforeState = document.m_CurrentStateId;

    if (SceneEditResult result = ApplyEntityTransform(candidate, m_EntityGuid, m_Transform); !result)
        return result;
    if (SerializedValuesEqual(candidate, m_BeforeRoot))
        return { false, "Scene entity transform is unchanged" };

    m_AfterRoot = candidate;
    m_AfterState = document.ApplyEditedSerializedRoot(std::move(candidate));
    return RuntimePreviewEditResult(m_EntityGuid);
}

SceneEditResult VansSetSceneEntityTransformCommand::Undo(VansSceneDocument& document)
{
    document.RestoreEditedSerializedRoot(m_BeforeRoot, m_BeforeState);
    return RuntimePreviewEditResult(m_EntityGuid);
}

SceneEditResult VansSetSceneEntityTransformCommand::Redo(VansSceneDocument& document)
{
    document.RestoreEditedSerializedRoot(m_AfterRoot, m_AfterState);
    return RuntimePreviewEditResult(m_EntityGuid);
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

SceneEditResult VansSceneEditService::ReparentEntity(
    const std::string& childEntityGuid,
    std::optional<VansSceneParentReference> newParent,
    ReparentTransformPolicy transformPolicy,
    std::optional<EditorAPI::RuntimeTransformSnapshot> resolvedLocalTransform)
{
    return Execute(std::make_unique<VansReparentSceneEntityCommand>(
        childEntityGuid,
		std::move(newParent),
		transformPolicy,
		std::move(resolvedLocalTransform)));
}

SceneEditResult VansSceneEditService::SetEntityTransform(
    const std::string& entityGuid,
    const EditorAPI::RuntimeTransformSnapshot& transform)
{
    return Execute(std::make_unique<VansSetSceneEntityTransformCommand>(
        entityGuid,
        transform));
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

SceneEditResult VansSceneEditService::Remove(const std::string& propertyPointer,
    SceneEditLifecycleHooks hooks)
{
    return Execute(std::make_unique<VansRemoveScenePropertyCommand>(
        propertyPointer,
        std::move(hooks)));
}

SceneEditResult VansSceneEditService::Remove(const DocumentPropertyPath& path,
    SceneEditLifecycleHooks hooks)
{
    if (path.space != DocumentPropertySpace::Scene)
        return { false, "Scene remove target is not a scene document path" };
    std::string pathError;
    if (!ValidateDocumentPropertyPath(path, &pathError))
        return { false, pathError };
    return Remove(ToDocumentPropertyPointer(path), std::move(hooks));
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
