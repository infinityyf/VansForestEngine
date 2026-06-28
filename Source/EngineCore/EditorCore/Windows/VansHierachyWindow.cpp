#include "VansHierachyWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
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
            }

            // ── 右键菜单：Delete ───────────────────────────────────────────
            const bool isSelected = (Vans::VansEditorSelection::EntityGuid() == id);
            if (ImGui::BeginPopupContextItem(id.c_str()))
            {
                if (ImGui::MenuItem("Delete"))
                {
                    // ⚠️ 必须在 DestroyEntity 前收集全部信息，destroy 后指针/引用全部失效
                    VansScriptObject* obj = m_Scene ? m_Scene->FindObjectByName(name) : nullptr;
                    std::string entityGuid = obj ? obj->m_EntityGuid : "";

                    // 1. 从 JSON 文档删除（先于 DestroyEntity，因为 DestroyEntity
                    //    不依赖 JSON；反过来如果先 Destroy 再 Remove，entities 引用已悬垂）
                    if (!entityGuid.empty())
                    {
                        if (auto* doc = VansEditorWindow::GetSceneDocument())
                        {
                            const auto& entities = doc->Root()["entities"];
                            for (int i = 0; i < static_cast<int>(entities.size()); ++i)
                            {
                                if (entities[i].value("id", "") == entityGuid)
                                {
                                    if (auto* editService = VansEditorWindow::GetSceneEditService())
                                        editService->Remove("/entities/" + std::to_string(i));
                                    break;
                                }
                            }
                        }
                    }

                    // 2. 从运行时场景删除
                    if (m_Scene)
                        m_Scene->DestroyEntity(name);

                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    // ⚠️ Remove 已 swap m_Root → 外部 entities 引用失效，必须立即退出 lambda
                    if (open && hasChildren) ImGui::TreePop();
                    return;
                }
                ImGui::EndPopup();
            }

            // ── Delete 键快捷方式 ──────────────────────────────────────────
            if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                VansScriptObject* obj = m_Scene ? m_Scene->FindObjectByName(name) : nullptr;
                std::string entityGuid = obj ? obj->m_EntityGuid : "";

                // 1. 先从 JSON 文档删除
                if (!entityGuid.empty())
                {
                    if (auto* doc = VansEditorWindow::GetSceneDocument())
                    {
                        const auto& entities = doc->Root()["entities"];
                        for (int i = 0; i < static_cast<int>(entities.size()); ++i)
                        {
                            if (entities[i].value("id", "") == entityGuid)
                            {
                                if (auto* editService = VansEditorWindow::GetSceneEditService())
                                    editService->Remove("/entities/" + std::to_string(i));
                                break;
                            }
                        }
                    }
                }

                // 2. 从运行时场景删除
                if (m_Scene)
                    m_Scene->DestroyEntity(name);

                // ⚠️ 外部 entities 引用已失效，立即退出
                if (open && hasChildren) ImGui::TreePop();
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
