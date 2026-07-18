#include "VansHierachyWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../../SceneCore/VansSceneDocument.h"

#include "imgui.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
void VansHierachuWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    ImGui::Begin("Hierarchy");

    const Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    if (!document || !document->Root().contains("entities") || !document->Root()["entities"].is_array())
    {
        ImGui::TextDisabled("No Scene document loaded");
        ImGui::End();
        return;
    }

    const auto& entities = document->Root()["entities"];
    if (ImGui::Selectable("Scene Settings", Vans::VansEditorSelection::IsSceneSelected()))
        Vans::VansEditorSelection::SelectScene();
    ImGui::Separator();

    std::unordered_map<std::string, std::vector<std::size_t>> children;
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        const auto& entity = entities[index];
        const std::string parent = entity.contains("parent") && entity["parent"].is_string()
            ? entity["parent"].get<std::string>()
            : std::string{};
        children[parent].push_back(index);
    }

    std::function<void(const std::string&)> drawChildren = [&](const std::string& parent)
    {
        const auto found = children.find(parent);
        if (found == children.end())
            return;

        for (const std::size_t index : found->second)
        {
            const auto& entity = entities[index];
            const std::string id = entity.value("id", "");
            const std::string name = entity.value("name", "Unnamed Entity");
            const bool hasChildren = children.find(id) != children.end();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (Vans::VansEditorSelection::EntityGuid() == id)
                flags |= ImGuiTreeNodeFlags_Selected;

            const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", name.c_str());
            if (ImGui::IsItemClicked())
                Vans::VansEditorSelection::SelectEntity(id);

            auto deleteEntity = [&editorAPI, &entities, &id, &name]()
            {
                if (!id.empty())
                {
                    for (int i = 0; i < static_cast<int>(entities.size()); ++i)
                    {
                        if (entities[i].value("id", "") == id)
                        {
                            if (auto* editService = VansEditorWindow::GetSceneEditService())
                                editService->Remove("/entities/" + std::to_string(i));
                            break;
                        }
                    }
                }

                Vans::EditorAPI::RuntimeEntityDestroyRequest request;
                request.entityName = name;
                editorAPI.DestroyRuntimeEntityByName(request);
            };

            const bool isSelected = (Vans::VansEditorSelection::EntityGuid() == id);
            if (ImGui::BeginPopupContextItem(id.c_str()))
            {
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
