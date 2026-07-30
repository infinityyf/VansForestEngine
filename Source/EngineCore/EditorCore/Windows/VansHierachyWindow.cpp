#include "VansHierachyWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansEditorObjectReference.h"
#include "../VansSceneHierarchyService.h"
#include "../VansSceneEditService.h"
#include "../VansSceneObjectReferenceRemapper.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../Util/VansLog.h"

#include "imgui.h"

#include <functional>
#include <string>
#include <unordered_map>
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

void ReparentDroppedEntity(
    Vans::EditorAPI::IEngineEditorAPI& editorAPI,
    const std::string& childGuid,
    const std::string& parentGuid)
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editService = VansEditorWindow::GetSceneEditService();
    if (!document || !editService)
        return;

    Vans::SceneReparentRequest request;
    request.childEntityGuid = childGuid;
    request.newParentEntityGuid = parentGuid;
    request.transformPolicy = Vans::ReparentTransformPolicy::KeepWorld;
    const Vans::SceneHierarchyEditResult result =
        Vans::VansSceneHierarchyService::Reparent(*document, *editService, request);
    if (!result)
    {
        VANS_LOG_WARN("[Hierarchy] Reparent failed: " << result.message);
        return;
    }
    if (result.changed)
    {
        Vans::EditorAPI::RuntimeEntityReparentRequest runtimeRequest;
        runtimeRequest.childEntityGuid = childGuid;
        runtimeRequest.newParentEntityGuid = parentGuid;
        const Vans::EditorAPI::RuntimeEntityReparentResult runtimeResult =
            editorAPI.ReparentRuntimeEntity(runtimeRequest);
        if (!runtimeResult.applied)
            VANS_LOG_WARN("[Hierarchy] Runtime reparent preview failed: " << runtimeResult.message);

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

    if (ImGui::Selectable("Scene Settings", Vans::VansEditorSelection::IsSceneSelected()))
        Vans::VansEditorSelection::SelectScene();
    if (ImGui::BeginDragDropTarget())
    {
        std::string droppedEntityGuid;
        if (AcceptSceneEntityDrop(droppedEntityGuid))
            ReparentDroppedEntity(editorAPI, droppedEntityGuid, {});
        ImGui::EndDragDropTarget();
    }
    ImGui::Separator();

    std::unordered_map<std::string, std::vector<std::size_t>> children;
    for (std::size_t index = 0; index < entities->arrayItems.size(); ++index)
    {
        const Vans::VansSerializedValue& entity = entities->arrayItems[index];
        const std::string parent = Vans::ReadSerializedStringField(entity, "parent");
        children[parent].push_back(index);
    }

    std::function<void(const std::string&)> drawChildren = [&](const std::string& parent)
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
            const bool hasChildren = children.find(id) != children.end();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (Vans::VansEditorSelection::EntityGuid() == id)
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
                    ReparentDroppedEntity(editorAPI, droppedEntityGuid, id);
                ImGui::EndDragDropTarget();
            }

            auto deleteEntity = [&editorAPI, entities, &id, &name]()
            {
                if (!id.empty())
                {
                    for (std::size_t i = 0; i < entities->arrayItems.size(); ++i)
                    {
                        const Vans::VansSerializedValue& entity = entities->arrayItems[i];
                        if (Vans::ReadSerializedStringField(entity, "id") == id)
                        {
                            if (auto* editService = VansEditorWindow::GetSceneEditService())
                            {
                                editService->Remove(Vans::MakeDocumentPropertyPath(
                                    Vans::DocumentPropertySpace::Scene,
                                    "/entities/" + std::to_string(i)));
                            }
                            break;
                        }
                    }
                }

                Vans::EditorAPI::RuntimeEntityDestroyRequest request;
                request.entityName = name;
                editorAPI.DestroyRuntimeEntityByName(request);
            };

            auto duplicateEntity = [&document, &id]()
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
                Vans::SceneEditResult editResult =
                    editService->AppendEntities(std::move(duplicate.entities));
                if (!editResult)
                {
                    VANS_LOG_ERROR("[Hierarchy] Duplicate failed: " << editResult.message);
                    return;
                }
                if (!duplicatedRootGuid.empty())
                    Vans::VansEditorSelection::SelectEntity(duplicatedRootGuid);
            };

            const bool isSelected = (Vans::VansEditorSelection::EntityGuid() == id);
            if (ImGui::BeginPopupContextItem(id.c_str()))
            {
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
                drawChildren(id);
                ImGui::TreePop();
            }
        }
    };

    drawChildren({});
    ImGui::End();
}
}
