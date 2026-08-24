#include "VansSceneHierarchyService.h"

#include "VansEditorObjectReference.h"
#include "VansSceneEditService.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneParentReference.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace Vans
{
namespace
{
	struct EntityHierarchyRecord
	{
		std::size_t index = 0;
		std::string parent;
	};

	bool TryBuildEntityMap(
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
			const std::string parent = parentField
				? ReadSceneParentEntityGuid(*parentField) : std::string{};

			EntityHierarchyRecord record;
			record.index = index;
			record.parent = parent;
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
}

SceneHierarchyEditResult VansSceneHierarchyService::Reparent(
	const VansSceneDocument& document,
	VansSceneEditService& editService,
	const SceneReparentRequest& request)
{
	if (request.childEntityGuid.empty())
		return { false, false, "Child entity id must not be empty" };
	const std::string newParentEntityGuid = request.newParent
		? request.newParent->entityGuid.ToString() : std::string{};
	if (request.newParent && !request.newParent->IsValid())
		return { false, false, "Parent reference is invalid" };
	if (request.childEntityGuid == newParentEntityGuid)
		return { false, false, "An entity cannot be parented to itself" };

	const VansSerializedValue root = document.SerializedRootSnapshot();
	std::unordered_map<std::string, EntityHierarchyRecord> entities;
	std::string error;
	if (!TryBuildEntityMap(root, entities, error))
		return { false, false, error };

	const auto child = entities.find(request.childEntityGuid);
	if (child == entities.end())
		return { false, false, "Child entity does not exist" };

	if (!newParentEntityGuid.empty() &&
		entities.find(newParentEntityGuid) == entities.end())
	{
		return { false, false, "Parent entity does not exist" };
	}

	if (!newParentEntityGuid.empty() &&
		IsDescendantOf(entities, newParentEntityGuid, request.childEntityGuid))
	{
		return { false, false, "Cannot parent an entity to one of its descendants" };
	}

	const SceneEditResult editResult = editService.ReparentEntity(
		request.childEntityGuid,
		request.newParent,
		request.transformPolicy,
		request.resolvedLocalTransform);
	if (!editResult)
		return { false, false, editResult.message };

	return { true, true, "Entity reparented" };
}
}
