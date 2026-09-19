#include "VansScenePickingService.h"

#include "VansEditorSelectionService.h"
#include "VansEditorWindow.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"

namespace Vans
{
EditorAPI::EditorScenePickResult VansScenePickingService::Pick(
	EditorAPI::IEngineEditorAPI& editorAPI,
	const EditorAPI::Ray& ray,
	float maxDistance, bool toggle, bool additive)
{
	const auto* document = VansGraphics::VansEditorWindow::GetSceneDocument();
	if (!document) return {};
	const auto snapshot = document->CreateSnapshot();
	const auto* entities = FindObjectField(snapshot.Root(), "entities");
	if (!entities || entities->kind != VansSerializedValue::Kind::Array) return {};
	EditorAPI::EditorScenePickRequest request;
	request.ray = ray; request.maxDistance = maxDistance;
	for (const auto& entity : entities->arrayItems)
		request.selectableEntities.push_back(ReadSerializedStringField(entity, "id"));
	auto result = editorAPI.PickEditorScene(request);
	if (!result.success) return result;
	auto& selection = VansEditorSelectionService::Get();
	if (!result.entityGuid.empty())
	{
		EditorObjectHandle handle;
		handle.domain = EditorObjectDomain::SceneEntity;
		handle.guid = handle.entityGuid = result.entityGuid;
		selection.Apply(toggle ? EditorSelectionOperation::Toggle : additive ? EditorSelectionOperation::Add :
			EditorSelectionOperation::Replace, {handle}, handle, "SceneViewport");
	}
	else if (!toggle && !additive) selection.Clear("SceneViewport");
	return result;
}
}
