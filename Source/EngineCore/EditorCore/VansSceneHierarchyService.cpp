#include "VansSceneHierarchyService.h"

#include "../AuthoringCore/VansEditorObjectReference.h"
#include "VansSceneEditService.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneParentReference.h"

#include <string>
#include "../SceneCore/VansSceneObjectGraph.h"
#include <utility>

namespace Vans
{
SceneHierarchyEditResult VansSceneHierarchyService::ValidateReparent(
	const VansSceneDocument& document,
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

	const auto snapshot = document.CreateSnapshot();
	const auto* entities = FindObjectField(snapshot.Root(), "entities");
	std::string error;
	if (!entities || !ValidateSceneObjectReparent(*entities, request.childEntityGuid, newParentEntityGuid, error))
		return {false, false, entities ? error : "Scene document has no entities array"};
	return {true, false, {}};
}

SceneHierarchyEditResult VansSceneHierarchyService::Reparent(
	const VansSceneDocument& document,
	VansSceneEditService& editService,
	const SceneReparentRequest& request)
{
	const auto validation = ValidateReparent(document, request);
	if (!validation) return validation;

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
