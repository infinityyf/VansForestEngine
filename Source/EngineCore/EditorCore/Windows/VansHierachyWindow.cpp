#include "VansHierachyWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansEditorObjectReference.h"
#include "../VansSceneHierarchyService.h"
#include "../VansSceneEditService.h"
#include "../VansSceneEntityCreationService.h"
#include "../VansSceneObjectReferenceRemapper.h"
#include "../VansScenePropertyValueAdapter.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneParentReference.h"
#include "../../Util/VansLog.h"

#include "imgui.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VansGraphics
{
namespace
{
bool AcceptSceneEntityDrop(std::string& entityGuid)
{
    const ImGuiPayload* payload =
        ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType);
    if (!payload || payload->DataSize <= 0 || !payload->Data)
        return false;

    Vans::EditorObjectHandle handle;
    if (!Vans::TryDeserializeEditorObjectHandle(
        payload->Data,
        static_cast<std::size_t>(payload->DataSize),
        handle))
    {
        return false;
    }

    if (handle.domain != Vans::EditorObjectDomain::SceneEntity)
        return false;

    entityGuid = handle.entityGuid.empty() ? handle.guid : handle.entityGuid;
    return !entityGuid.empty();
}

std::string AnchorHierarchyKey(
	const std::string& animationComponentGuid,
	const std::string& anchorGuid)
{
	return "@anchor/" + animationComponentGuid + "/" + anchorGuid;
}

bool IsSelectedHandle(const Vans::EditorObjectHandle& handle)
{
	const Vans::EditorObjectHandle& active =
		Vans::VansEditorSelectionService::Get().Snapshot().active;
	return active.domain == handle.domain
		&& active.entityGuid == handle.entityGuid
		&& active.componentGuid == handle.componentGuid
		&& active.subObjectKind == handle.subObjectKind
		&& active.subObjectGuid == handle.subObjectGuid;
}

std::optional<Vans::VansSceneParentReference> ParentReferenceFromHandle(
	const Vans::EditorObjectHandle& handle)
{
	Vans::VansSceneParentReference parent;
	if (handle.domain == Vans::EditorObjectDomain::SceneEntity)
	{
		parent.kind = Vans::VansSceneParentKind::Entity;
		const std::string& entityGuid = handle.entityGuid.empty() ? handle.guid : handle.entityGuid;
		if (Vans::VansAssetGuid::TryParse(entityGuid, parent.entityGuid))
			return parent;
		return std::nullopt;
	}
	if (handle.domain != Vans::EditorObjectDomain::SceneSubObject
		|| (handle.subObjectKind != Vans::SceneSubObjectKind::Bone
			&& handle.subObjectKind != Vans::SceneSubObjectKind::Socket))
	{
		return std::nullopt;
	}
	parent.kind = handle.subObjectKind == Vans::SceneSubObjectKind::Bone
		? Vans::VansSceneParentKind::Bone
		: Vans::VansSceneParentKind::Socket;
	if (!Vans::VansAssetGuid::TryParse(handle.entityGuid, parent.entityGuid)
		|| !Vans::VansAssetGuid::TryParse(handle.componentGuid, parent.animationComponentGuid)
		|| !Vans::VansAssetGuid::TryParse(handle.subObjectGuid, parent.anchorGuid))
	{
		return std::nullopt;
	}
	return parent;
}

void ReparentDroppedEntity(
    Vans::EditorAPI::IEngineEditorAPI& editorAPI,
    const std::string& childGuid,
    std::optional<Vans::VansSceneParentReference> parent,
    Vans::ReparentTransformPolicy policy = Vans::ReparentTransformPolicy::KeepWorld)
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editService = VansEditorWindow::GetSceneEditService();
    if (!document || !editService)
        return;

	Vans::EditorAPI::RuntimeEntityReparentRequest runtimeRequest;
	runtimeRequest.childEntityGuid = childGuid;
	switch (policy)
	{
	case Vans::ReparentTransformPolicy::KeepLocal:
		runtimeRequest.transformPolicy =
			Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepLocal;
		break;
	case Vans::ReparentTransformPolicy::Snap:
		runtimeRequest.transformPolicy =
			Vans::EditorAPI::RuntimeReparentTransformPolicy::Snap;
		break;
	case Vans::ReparentTransformPolicy::KeepWorld:
	default:
		runtimeRequest.transformPolicy =
			Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepWorld;
		break;
	}
	if (parent)
	{
		runtimeRequest.newParent.entityGuid = parent->entityGuid.ToString();
		if (parent->IsEntity())
			runtimeRequest.newParent.kind = Vans::EditorAPI::RuntimeParentKind::Entity;
		else
		{
			runtimeRequest.newParent.kind = parent->kind == Vans::VansSceneParentKind::Bone
				? Vans::EditorAPI::RuntimeParentKind::Bone
				: Vans::EditorAPI::RuntimeParentKind::Socket;
			runtimeRequest.newParent.animationComponentGuid = parent->animationComponentGuid.ToString();
			runtimeRequest.newParent.anchorGuid = parent->anchorGuid.ToString();
		}
	}
	const Vans::EditorAPI::RuntimeEntityReparentResult runtimeResult =
		editorAPI.ReparentRuntimeEntity(runtimeRequest);
	if (!runtimeResult.applied)
	{
		VANS_LOG_WARN("[Hierarchy] Runtime reparent failed: " << runtimeResult.message);
		return;
	}

	Vans::SceneReparentRequest request;
	request.childEntityGuid = childGuid;
	request.newParent = parent;
	request.transformPolicy = policy;
	if (policy == Vans::ReparentTransformPolicy::KeepWorld)
		request.resolvedLocalTransform = runtimeResult.localTransform;
    const Vans::SceneHierarchyEditResult result =
        Vans::VansSceneHierarchyService::Reparent(*document, *editService, request);
    if (!result)
    {
        VANS_LOG_WARN("[Hierarchy] Reparent failed: " << result.message);
		VansEditorWindow::ReloadCurrentSceneForEditing();
		return;
    }
    if (result.changed)
    {
        Vans::VansEditorSelection::SelectEntity(childGuid);
    }
}
}

void VansHierachuWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    ImGui::Begin("Hierarchy");

    const Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    const Vans::VansSerializedValue sceneRoot =
        document ? document->SerializedRootSnapshot() : Vans::VansSerializedValue::Null();
    const Vans::VansSerializedValue* entities = Vans::FindObjectField(sceneRoot, "entities");
    if (!entities || entities->kind != Vans::VansSerializedValue::Kind::Array)
    {
        ImGui::TextDisabled("No Scene document loaded");
        ImGui::End();
        return;
    }

    auto createEmptyObject = [&editorAPI, document](
		std::optional<Vans::VansSceneParentReference> parent)
    {
        Vans::VansSceneEditService* editService = VansEditorWindow::GetSceneEditService();
        if (!document || !editService)
            return;

        VansSceneEntityCreationService::EmptyObjectRequest request;
        request.parent = std::move(parent);
        const VansSceneEntityCreationService::Result result =
            VansSceneEntityCreationService::CreateEmptyObject(
                editorAPI,
                *document,
                *editService,
                request);
        if (!result)
        {
            VANS_LOG_ERROR("[Hierarchy] Create empty object failed: " << result.message);
            return;
        }
        if (!result.runtimeChangeApplied)
        {
            if (!result.message.empty())
                VANS_LOG_WARN("[Hierarchy] " << result.message);
            VansEditorWindow::ReloadCurrentSceneForEditing();
        }
        Vans::VansEditorSelection::SelectEntity(result.entityGuid);
    };

    auto createLocalVolumetricFog = [&editorAPI, document]()
    {
        Vans::VansSceneEditService* editService =
            VansEditorWindow::GetSceneEditService();
        if (!document || !editService)
            return;

        VansSceneEntityCreationService::LocalVolumetricFogRequest request;
        request.dimensions = { 20.0f, 2.0f, 20.0f };
        request.settings.visibilityDistanceMeters = 150.0f;
        request.settings.singleScatteringAlbedo = { 0.96f, 0.98f, 1.0f };
        request.settings.anisotropy = 0.25f;
        request.settings.edgeFadeDistanceMeters = 0.5f;
        request.settings.distanceFadeEndMeters = 1000.0f;
        request.settings.directLightingScale = 1.0f;
        request.settings.skyLightingScale = 0.75f;
        request.settings.receiveCloudShadows = true;
        const VansSceneEntityCreationService::Result result =
            VansSceneEntityCreationService::CreateLocalVolumetricFog(
                editorAPI, *document, *editService, request);
        if (!result)
        {
            VANS_LOG_ERROR("[Hierarchy] Create local volumetric fog failed: "
                << result.message);
            return;
        }
        if (!result.runtimeChangeApplied)
        {
            if (!result.message.empty())
                VANS_LOG_WARN("[Hierarchy] " << result.message);
            VansEditorWindow::ReloadCurrentSceneForEditing();
        }
        Vans::VansEditorSelection::SelectEntity(result.entityGuid);
    };

    if (ImGui::Button("+ Create"))
        ImGui::OpenPopup("HierarchyCreateMenu");
	static int reparentPolicyIndex = 0;
	static constexpr const char* reparentPolicyLabels[] = {
		"Keep World", "Keep Local", "Snap"
	};
	ImGui::SameLine();
	ImGui::SetNextItemWidth(116.0f);
	ImGui::Combo("##HierarchyReparentPolicy", &reparentPolicyIndex,
		reparentPolicyLabels, IM_ARRAYSIZE(reparentPolicyLabels));
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Transform policy used when dropping an entity onto a parent");
	const Vans::ReparentTransformPolicy reparentPolicy = reparentPolicyIndex == 1
		? Vans::ReparentTransformPolicy::KeepLocal
		: (reparentPolicyIndex == 2 ? Vans::ReparentTransformPolicy::Snap
			: Vans::ReparentTransformPolicy::KeepWorld);
    if (ImGui::BeginPopup("HierarchyCreateMenu"))
    {
        if (ImGui::MenuItem("Empty Object"))
            createEmptyObject(std::nullopt);
		if (ImGui::BeginMenu("Visual Effects"))
		{
			if (ImGui::MenuItem("Local Volumetric Fog"))
				createLocalVolumetricFog();
			ImGui::EndMenu();
		}

		const std::optional<Vans::VansSceneParentReference> selectedParent =
			ParentReferenceFromHandle(
				Vans::VansEditorSelectionService::Get().Snapshot().active);
        ImGui::BeginDisabled(!selectedParent);
        if (ImGui::MenuItem("Empty Child"))
            createEmptyObject(selectedParent);
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::Separator();

    if (ImGui::Selectable("Scene Settings", Vans::VansEditorSelection::IsSceneSelected()))
        Vans::VansEditorSelection::SelectScene();
    if (ImGui::BeginDragDropTarget())
    {
        std::string droppedEntityGuid;
        if (AcceptSceneEntityDrop(droppedEntityGuid))
			ReparentDroppedEntity(editorAPI, droppedEntityGuid, std::nullopt, reparentPolicy);
        ImGui::EndDragDropTarget();
    }
    ImGui::Separator();

	std::unordered_map<std::string, std::vector<std::size_t>> children;
	std::unordered_set<std::string> entitiesWithAnimation;
    for (std::size_t index = 0; index < entities->arrayItems.size(); ++index)
    {
        const Vans::VansSerializedValue& entity = entities->arrayItems[index];
		const std::string entityGuid = Vans::ReadSerializedStringField(entity, "id");
		const Vans::VansSerializedValue* components = Vans::FindObjectField(entity, "components");
		if (components && components->kind == Vans::VansSerializedValue::Kind::Array)
		{
			for (const Vans::VansSerializedValue& component : components->arrayItems)
			{
				if (Vans::ReadSerializedStringField(component, "type") == "Animation")
				{
					entitiesWithAnimation.insert(entityGuid);
					break;
				}
			}
		}
		const Vans::VansSerializedValue* parentValue = Vans::FindObjectField(entity, "parent");
		std::string hierarchyParent;
		if (parentValue && !parentValue->IsNull())
		{
			Vans::VansSceneParentReference parent;
			std::string error;
			if (Vans::TryReadSceneParentReference(*parentValue, parent, error))
			{
				hierarchyParent = parent.IsAnchor()
					? AnchorHierarchyKey(parent.animationComponentGuid.ToString(), parent.anchorGuid.ToString())
					: parent.entityGuid.ToString();
			}
		}
		children[hierarchyParent].push_back(index);
    }

	std::function<void(const std::string&)> drawChildren;
	std::function<void(const Vans::EditorAPI::SceneSkeletonHierarchyRig&, int)> drawBone;
	drawBone = [&](const Vans::EditorAPI::SceneSkeletonHierarchyRig& rig, int boneIndex)
	{
		if (boneIndex < 0 || boneIndex >= static_cast<int>(rig.bones.size()))
			return;
		const auto& bone = rig.bones[static_cast<std::size_t>(boneIndex)];
		if (bone.guid.empty())
			return;
		std::vector<int> childBones;
		for (int index = 0; index < static_cast<int>(rig.bones.size()); ++index)
			if (rig.bones[static_cast<std::size_t>(index)].parentIndex == boneIndex)
				childBones.push_back(index);
		std::vector<const Vans::EditorAPI::SceneSkeletonHierarchySocket*> sockets;
		for (const auto& socket : rig.sockets)
			if (socket.parentBoneIndex == boneIndex)
				sockets.push_back(&socket);
		const std::string boneKey = AnchorHierarchyKey(rig.animationComponentGuid, bone.guid);
		const bool hasChildren = !childBones.empty() || !sockets.empty()
			|| children.find(boneKey) != children.end();

		Vans::EditorObjectHandle handle;
		handle.domain = Vans::EditorObjectDomain::SceneSubObject;
		handle.guid = bone.guid;
		handle.entityGuid = rig.entityGuid;
		handle.componentGuid = rig.animationComponentGuid;
		handle.componentType = "Animation";
		handle.subObjectKind = Vans::SceneSubObjectKind::Bone;
		handle.subObjectGuid = bone.guid;
		handle.subObjectName = bone.name;
		handle.displayName = bone.name;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (IsSelectedHandle(handle))
			flags |= ImGuiTreeNodeFlags_Selected;
		const std::string nodeId = "bone/" + rig.animationComponentGuid + "/" + bone.guid;
		const bool open = ImGui::TreeNodeEx(nodeId.c_str(), flags, "%s", bone.name.c_str());
		if (ImGui::IsItemClicked())
			Vans::VansEditorSelection::SelectSceneSubObject(handle);
		if (ImGui::BeginPopupContextItem((nodeId + "/context").c_str()))
		{
			if (ImGui::MenuItem("Create Empty Child"))
				createEmptyObject(ParentReferenceFromHandle(handle));
			ImGui::EndPopup();
		}
		if (ImGui::BeginDragDropTarget())
		{
			std::string droppedEntityGuid;
			if (AcceptSceneEntityDrop(droppedEntityGuid))
			{
				Vans::VansSceneParentReference parent;
				parent.kind = Vans::VansSceneParentKind::Bone;
				if (Vans::VansAssetGuid::TryParse(rig.entityGuid, parent.entityGuid)
					&& Vans::VansAssetGuid::TryParse(rig.animationComponentGuid, parent.animationComponentGuid)
					&& Vans::VansAssetGuid::TryParse(bone.guid, parent.anchorGuid))
				{
					ReparentDroppedEntity(editorAPI, droppedEntityGuid, std::move(parent), reparentPolicy);
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (!open || !hasChildren)
			return;
		for (const int childBone : childBones)
			drawBone(rig, childBone);
		for (const auto* socket : sockets)
		{
			const std::string socketKey = AnchorHierarchyKey(rig.animationComponentGuid, socket->guid);
			const bool socketHasChildren = children.find(socketKey) != children.end();
			Vans::EditorObjectHandle socketHandle;
			socketHandle.domain = Vans::EditorObjectDomain::SceneSubObject;
			socketHandle.guid = socket->guid;
			socketHandle.entityGuid = rig.entityGuid;
			socketHandle.componentGuid = rig.animationComponentGuid;
			socketHandle.componentType = "Animation";
			socketHandle.subObjectKind = Vans::SceneSubObjectKind::Socket;
			socketHandle.subObjectGuid = socket->guid;
			socketHandle.subObjectName = socket->name;
			socketHandle.displayName = socket->name;
			ImGuiTreeNodeFlags socketFlags = ImGuiTreeNodeFlags_SpanAvailWidth
				| ImGuiTreeNodeFlags_OpenOnArrow;
			if (!socketHasChildren)
				socketFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (IsSelectedHandle(socketHandle))
				socketFlags |= ImGuiTreeNodeFlags_Selected;
			const std::string socketId = "socket/" + rig.animationComponentGuid + "/" + socket->guid;
			const bool socketOpen = ImGui::TreeNodeEx(
				socketId.c_str(), socketFlags, "[Socket] %s", socket->name.c_str());
			if (ImGui::IsItemClicked())
				Vans::VansEditorSelection::SelectSceneSubObject(socketHandle);
			if (ImGui::BeginPopupContextItem((socketId + "/context").c_str()))
			{
				if (ImGui::MenuItem("Create Empty Child"))
					createEmptyObject(ParentReferenceFromHandle(socketHandle));
				ImGui::EndPopup();
			}
			if (ImGui::BeginDragDropTarget())
			{
				std::string droppedEntityGuid;
				if (AcceptSceneEntityDrop(droppedEntityGuid))
				{
					Vans::VansSceneParentReference parent;
					parent.kind = Vans::VansSceneParentKind::Socket;
					if (Vans::VansAssetGuid::TryParse(rig.entityGuid, parent.entityGuid)
						&& Vans::VansAssetGuid::TryParse(rig.animationComponentGuid, parent.animationComponentGuid)
						&& Vans::VansAssetGuid::TryParse(socket->guid, parent.anchorGuid))
					{
						ReparentDroppedEntity(editorAPI, droppedEntityGuid, std::move(parent), reparentPolicy);
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (socketOpen && socketHasChildren)
			{
				drawChildren(socketKey);
				ImGui::TreePop();
			}
		}
		drawChildren(boneKey);
		ImGui::TreePop();
	};

    drawChildren = [&](const std::string& parent)
    {
        const auto found = children.find(parent);
        if (found == children.end())
            return;

        for (const std::size_t index : found->second)
        {
            const Vans::VansSerializedValue& entity = entities->arrayItems[index];
            const std::string id = Vans::ReadSerializedStringField(entity, "id");
            const std::string name =
                Vans::ReadSerializedStringField(entity, "name", "Unnamed Entity");
			const bool hasRig = entitiesWithAnimation.find(id) != entitiesWithAnimation.end();
            const bool hasChildren = children.find(id) != children.end() || hasRig;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			const Vans::EditorObjectHandle& active =
				Vans::VansEditorSelectionService::Get().Snapshot().active;
            if (active.domain == Vans::EditorObjectDomain::SceneEntity
				&& Vans::VansEditorSelection::EntityGuid() == id)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", name.c_str());
            if (ImGui::IsItemClicked())
                Vans::VansEditorSelection::SelectEntity(id);

            if (!id.empty() && ImGui::BeginDragDropSource())
            {
                Vans::EditorObjectHandle handle;
                handle.domain = Vans::EditorObjectDomain::SceneEntity;
                handle.guid = id;
                handle.entityGuid = id;
                handle.displayName = name;
                const std::string payload = Vans::SerializeEditorObjectHandle(handle);
                ImGui::SetDragDropPayload(Vans::VansObjectReferenceDragPayloadType,
                    payload.c_str(),
                    payload.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            if (!id.empty() && ImGui::BeginDragDropTarget())
            {
                std::string droppedEntityGuid;
                if (AcceptSceneEntityDrop(droppedEntityGuid))
				{
					Vans::VansSceneParentReference parent;
					parent.kind = Vans::VansSceneParentKind::Entity;
					if (Vans::VansAssetGuid::TryParse(id, parent.entityGuid))
						ReparentDroppedEntity(editorAPI, droppedEntityGuid, std::move(parent), reparentPolicy);
				}
                ImGui::EndDragDropTarget();
            }

            auto deleteEntity = [&editorAPI, entities, &id]()
            {
                if (id.empty())
                    return;

                for (std::size_t i = 0; i < entities->arrayItems.size(); ++i)
                {
                    const Vans::VansSerializedValue& entity = entities->arrayItems[i];
                    if (Vans::ReadSerializedStringField(entity, "id") != id)
                        continue;

                    Vans::VansSerializedValue removedEntity = entity;
                    std::vector<Vans::EditorAPI::ScenePropertyValue> runtimeEntities;
                    runtimeEntities.push_back(Vans::FromSerializedValue(removedEntity));

                    std::vector<std::string> childGuids;
                    for (const Vans::VansSerializedValue& candidateChild : entities->arrayItems)
                    {
						const Vans::VansSerializedValue* candidateParent =
							Vans::FindObjectField(candidateChild, "parent");
						if (candidateParent
							&& Vans::ReadSceneParentEntityGuid(*candidateParent) == id)
                            childGuids.push_back(Vans::ReadSerializedStringField(candidateChild, "id"));
                    }

                    auto destroyRuntimeEntity = [&editorAPI, entityGuid = id]()
                    {
                        Vans::EditorAPI::RuntimeEntityDestroyRequest request;
                        request.entityGuid = entityGuid;
                        return editorAPI.DestroyRuntimeEntity(request).destroyed;
                    };
                    auto createRuntimeEntity = [&editorAPI, runtimeEntities, childGuids, entityGuid = id]()
                    {
                        Vans::EditorAPI::RuntimeSceneEntitiesCreateRequest request;
                        request.sceneEntities = runtimeEntities;
                        const Vans::EditorAPI::RuntimeSceneEntitiesCreateResult createResult =
                            editorAPI.CreateRuntimeSceneEntities(request);
                        if (!createResult.created)
                        {
                            if (!createResult.message.empty())
                                VANS_LOG_WARN("[Hierarchy] Runtime delete undo create failed: "
                                    << createResult.message);
                            return false;
                        }

                        bool reparentedAll = true;
                        for (const std::string& childGuid : childGuids)
                        {
                            if (childGuid.empty())
                                continue;
                            Vans::EditorAPI::RuntimeEntityReparentRequest reparentRequest;
                            reparentRequest.childEntityGuid = childGuid;
							reparentRequest.newParent.kind = Vans::EditorAPI::RuntimeParentKind::Entity;
							reparentRequest.newParent.entityGuid = entityGuid;
							reparentRequest.transformPolicy =
								Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepLocal;
                            const Vans::EditorAPI::RuntimeEntityReparentResult reparentResult =
                                editorAPI.ReparentRuntimeEntity(reparentRequest);
                            if (!reparentResult.applied && !reparentResult.message.empty())
                            {
                                VANS_LOG_WARN("[Hierarchy] Runtime delete undo child reparent failed: "
                                    << reparentResult.message);
                            }
                            reparentedAll = reparentResult.applied && reparentedAll;
                        }
                        return reparentedAll;
                    };

                    Vans::SceneEditLifecycleHooks hooks;
                    hooks.afterExecute = destroyRuntimeEntity;
                    hooks.afterUndo = createRuntimeEntity;
                    hooks.afterRedo = destroyRuntimeEntity;

                    if (auto* editService = VansEditorWindow::GetSceneEditService())
                    {
                        Vans::SceneEditResult result = editService->Remove(
                            Vans::MakeDocumentPropertyPath(
                                Vans::DocumentPropertySpace::Scene,
                                "/entities/" + std::to_string(i)),
                            std::move(hooks));
                        if (!result)
                            VANS_LOG_ERROR("[Hierarchy] Delete failed: " << result.message);
                        else if (!result.runtimeChangeApplied)
                            VansEditorWindow::ReloadCurrentSceneForEditing();
                    }
                    return;
                }
            };

            auto duplicateEntity = [&editorAPI, &document, &id]()
            {
                if (id.empty())
                    return;
                auto* editService = VansEditorWindow::GetSceneEditService();
                if (!editService)
                    return;

                Vans::SceneEntityDuplicateResult duplicate =
                    Vans::DuplicateSceneEntitySubtree(*document, id);
                if (!duplicate)
                {
                    VANS_LOG_ERROR("[Hierarchy] Duplicate failed: " << duplicate.message);
                    return;
                }

                const std::string duplicatedRootGuid = duplicate.duplicatedRootGuid;
                std::vector<std::string> duplicatedEntityGuids;
                std::vector<Vans::EditorAPI::ScenePropertyValue> runtimeEntities;
                duplicatedEntityGuids.reserve(duplicate.entities.size());
                runtimeEntities.reserve(duplicate.entities.size());
                for (const Vans::VansSerializedValue& entity : duplicate.entities)
                {
                    duplicatedEntityGuids.push_back(Vans::ReadSerializedStringField(entity, "id"));
                    runtimeEntities.push_back(Vans::FromSerializedValue(entity));
                }

                auto createRuntimeEntities = [&editorAPI, runtimeEntities]()
                {
                    Vans::EditorAPI::RuntimeSceneEntitiesCreateRequest request;
                    request.sceneEntities = runtimeEntities;
                    const Vans::EditorAPI::RuntimeSceneEntitiesCreateResult result =
                        editorAPI.CreateRuntimeSceneEntities(request);
                    if (!result.created && !result.message.empty())
                        VANS_LOG_WARN("[Hierarchy] Runtime duplicate create failed: " << result.message);
                    return result.created;
                };

                Vans::SceneEditLifecycleHooks hooks;
                hooks.afterExecute = createRuntimeEntities;
                hooks.afterUndo = [&editorAPI, duplicatedEntityGuids]()
                {
                    bool destroyedAll = true;
                    for (auto it = duplicatedEntityGuids.rbegin(); it != duplicatedEntityGuids.rend(); ++it)
                    {
                        if (it->empty())
                            continue;
                        Vans::EditorAPI::RuntimeEntityDestroyRequest request;
                        request.entityGuid = *it;
                        destroyedAll = editorAPI.DestroyRuntimeEntity(request).destroyed && destroyedAll;
                    }
                    return destroyedAll;
                };
                hooks.afterRedo = createRuntimeEntities;

                Vans::SceneEditResult editResult =
                    editService->AppendEntities(std::move(duplicate.entities), std::move(hooks));
                if (!editResult)
                {
                    VANS_LOG_ERROR("[Hierarchy] Duplicate failed: " << editResult.message);
                    return;
                }
                if (!editResult.runtimeChangeApplied)
                    VansEditorWindow::ReloadCurrentSceneForEditing();
                if (!duplicatedRootGuid.empty())
                    Vans::VansEditorSelection::SelectEntity(duplicatedRootGuid);
            };

            const bool isSelected = (Vans::VansEditorSelection::EntityGuid() == id);
            if (ImGui::BeginPopupContextItem(id.c_str()))
            {
                if (ImGui::MenuItem("Create Empty Child"))
                {
					Vans::VansSceneParentReference parent;
					parent.kind = Vans::VansSceneParentKind::Entity;
					if (Vans::VansAssetGuid::TryParse(id, parent.entityGuid))
						createEmptyObject(parent);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate"))
                {
                    duplicateEntity();
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    if (open && hasChildren)
                        ImGui::TreePop();
                    return;
                }
                if (ImGui::MenuItem("Delete"))
                {
                    deleteEntity();
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    if (open && hasChildren)
                        ImGui::TreePop();
                    return;
                }
                ImGui::EndPopup();
            }

            if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                deleteEntity();
                if (open && hasChildren)
                    ImGui::TreePop();
                return;
            }

            if (open && hasChildren)
            {
				if (hasRig)
				{
					const Vans::EditorAPI::SceneSkeletonHierarchySnapshot skeletonHierarchy =
						editorAPI.GetSceneSkeletonHierarchy(id);
					for (const auto& rig : skeletonHierarchy.rigs)
						for (int boneIndex = 0; boneIndex < static_cast<int>(rig.bones.size()); ++boneIndex)
							if (rig.bones[static_cast<std::size_t>(boneIndex)].parentIndex < 0)
								drawBone(rig, boneIndex);
				}
                drawChildren(id);
                ImGui::TreePop();
            }
        }
    };

    drawChildren({});
    ImGui::End();
}
}
