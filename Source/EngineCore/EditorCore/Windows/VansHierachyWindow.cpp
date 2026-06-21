#include "VansHierachyWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../RenderCore/VansScene.h"
#include "../../ScriptCore/VansScriptContext.h"

#include "imgui.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
void VansHierachuWindow::ShowWindow(VansVKDevice& device)
{
    (void)device;
    ImGui::Begin("Hierarchy");

    const Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    if (!document || !document->Root().contains("entities") || !document->Root()["entities"].is_array())
    {
        ImGui::TextDisabled("No Scene v2 document loaded");
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
            ? entity["parent"].get<std::string>() : std::string{};
        children[parent].push_back(index);
    }

    std::function<void(const std::string&)> drawChildren = [&](const std::string& parent)
    {
        const auto found = children.find(parent);
        if (found == children.end()) return;
        for (const std::size_t index : found->second)
        {
            const auto& entity = entities[index];
            const std::string id = entity.value("id", "");
            const std::string name = entity.value("name", "Unnamed Entity");
            const bool hasChildren = children.find(id) != children.end();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (Vans::VansEditorSelection::EntityGuid() == id) flags |= ImGuiTreeNodeFlags_Selected;

            const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "%s", name.c_str());
            if (ImGui::IsItemClicked())
            {
                Vans::VansEditorSelection::SelectEntity(id);
                if (m_Scene)
                {
                    m_Scene->m_SelectedObject = nullptr;
                    m_Scene->m_SelectedNode = nullptr;
                    for (VansScriptObject* object : m_Scene->m_SceneObjects)
                    {
                        if (!object || object->m_EntityGuid != id) continue;
                        m_Scene->m_SelectedObject = object;
                        if (auto* render = object->GetComponent<VansScriptRenderComponent>())
                            m_Scene->m_SelectedNode = render->m_RenderNode;
                        break;
                    }
                }
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
