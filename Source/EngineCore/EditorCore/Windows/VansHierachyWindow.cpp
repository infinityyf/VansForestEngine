#include "VansHierachyWindow.h"
#include "../../RenderCore/VansScene.h"

#include "imgui.h"

// ── Helper: draw a node list, grouping multi-mesh children under a tree node ──
void VansGraphics::VansHierachuWindow::DrawNodeListWithGroups(const std::vector<VansRenderNode*>& nodes)
{
    // Track which groups we have already drawn in this pass
    std::set<std::string> drawnGroups;

    for (auto& node : nodes)
    {
        if (node->m_ParentGroupName.empty())
        {
            // Regular (non-grouped) node – use pointer as unique ID
            ImGui::PushID(node);
            if (ImGui::Selectable(node->m_NodeName.c_str(), m_Scene->m_SelectedNode == node))
            {
                m_Scene->m_SelectedNode = node;
            }
            ImGui::PopID();
        }
        else
        {
            // This node belongs to a multi-mesh group.
            // Only draw the tree once per group.
            const std::string& groupName = node->m_ParentGroupName;
            if (drawnGroups.count(groupName))
                continue;
            drawnGroups.insert(groupName);

            auto groupIt = m_Scene->m_MultiMeshGroups.find(groupName);
            if (groupIt == m_Scene->m_MultiMeshGroups.end())
                continue;

            const MultiMeshGroup& group = groupIt->second;

            // Tree node for the parent group – use group name pointer as stable ID
            ImGui::PushID(groupName.c_str());
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            bool treeOpen = ImGui::TreeNodeEx(groupName.c_str(), flags);

            if (treeOpen)
            {
                for (auto* child : group.childNodes)
                {
                    ImGui::PushID(child);
                    if (ImGui::Selectable(child->m_NodeName.c_str(), m_Scene->m_SelectedNode == child))
                    {
                        m_Scene->m_SelectedNode = child;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

void VansGraphics::VansHierachuWindow::DrawRenderNodeList()
{
    ImGui::Begin("Render Nodes");

    if (ImGui::CollapsingHeader("Sky Node"))
    {
        if (ImGui::Selectable(m_Scene->m_SkyBoxNode->m_NodeName.c_str(), m_Scene->m_SelectedNode == m_Scene->m_SkyBoxNode))
        {
            m_Scene->m_SelectedNode = m_Scene->m_SkyBoxNode;
        }
    }

    if (ImGui::CollapsingHeader("Opaque Nodes"))
    {
        DrawNodeListWithGroups(m_Scene->m_OpaqueRenderNodes);
    }

    if (ImGui::CollapsingHeader("Transparent Nodes"))
    {
        DrawNodeListWithGroups(m_Scene->m_TransParentRenderNodes);
    }

    if (ImGui::CollapsingHeader("Post Process Nodes"))
    {
        for (auto& node : m_Scene->m_PostProcessRenderNodes)
        {
            if (ImGui::Selectable(node->m_NodeName.c_str(), m_Scene->m_SelectedNode == node))
            {
                m_Scene->m_SelectedNode = node;
            }
        }
    }

    ImGui::End();
}

void VansGraphics::VansHierachuWindow::DrawRenderNodeDetail()
{
    if (m_Scene->m_SelectedNode!=nullptr)
    {
        ImGui::Begin("Render Node Inspector");

        VansRenderNode* node = m_Scene->m_SelectedNode;
        ImGui::Text("Node: %s", node->m_NodeName.c_str());
        if (!node->m_ParentGroupName.empty())
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Parent Group: %s", node->m_ParentGroupName.c_str());
        }
        ImGui::Separator();

        // --- Transform ---
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawTransformDetail(*node);
        }

        // --- Material ---
        if (node->m_Material != nullptr)
        {
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
            {
                DrawMaterialDetail(*node->m_Material);
            }
        }

        ImGui::End();
    }
}

void VansGraphics::VansHierachuWindow::DrawTransformDetail(VansRenderNode& node)
{
    glm::vec3 pos = node.GetTransformPosition();
    glm::vec3 rot = node.GetTransformRotation();
    glm::vec3 scl = node.GetTransformScale();

    bool changed = false;

    if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
        changed = true;
    if (ImGui::DragFloat3("Rotation", &rot.x, 0.5f))
        changed = true;
    if (ImGui::DragFloat3("Scale", &scl.x, 0.01f, 0.001f, 100.0f))
        changed = true;

    if (changed)
    {
        node.SetTransformData(pos, rot, scl);
    }
}

void VansGraphics::VansHierachuWindow::DrawMaterialDetail(VansMaterial& material, int index)
{
    // Show material type label
    const char* typeNames[] = { "PBR", "Coat", "Transparent", "PostProcess", "SkyBox", "Deferred", "SSAO", "SSR", "Shadow" };
    int typeIdx = (int)material.m_MaterialType;
    if (typeIdx >= 0 && typeIdx < 9)
        ImGui::Text("Type: %s", typeNames[typeIdx]);

    // Show texture info
    auto showTex = [](const char* label, VansTexture* tex) {
        if (tex != nullptr)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "  %s: bound", label);
        else
            ImGui::TextDisabled("  %s: (none)", label);
    };

    if (material.m_MaterialType == VansMaterialType::VAN_PBR)
    {
        if (ImGui::TreeNode("Textures"))
        {
            showTex("BaseColor", material.m_BaseColorTexture);
            showTex("Normal",    material.m_NormalTexture);
            showTex("Metallic",  material.m_MetalTexture);
            showTex("Roughness", material.m_RoughnessTexture);
            showTex("AO",        material.m_AoTexture);
            ImGui::TreePop();
        }
    }

    ImGui::Separator();

	switch (material.m_MaterialType)
	{
	case VansMaterialType::VAN_PBR:
		DrawPBRMaterialParameters(material.m_BasePBRParam, index);
		break;
	case VansMaterialType::VAN_SKY_BOX:
        DrawAtmosphereParameters(material.m_AtmospherePBRParam);
		break;
	default:
		break;
	}
}

void VansGraphics::VansHierachuWindow::DrawPBRMaterialParameters(VansBasePBRParam& param, int id)
{
    // Use unique ImGui IDs when multiple materials exist on one node
    ImGui::PushID(id);
    ImGui::ColorEdit3("Albedo", &param.m_albedo.x);
    ImGui::SliderFloat("Metallic", &param.m_metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &param.m_roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("AO", &param.m_ao, 0.0f, 1.0f);
    ImGui::PopID();
}

void VansGraphics::VansHierachuWindow::DrawAtmosphereParameters(VansAtmospherePBRParam& param)
{
    ImGui::InputFloat("planet radius", &param.m_PlanetRadius);
    ImGui::InputFloat("init sea level", &param.m_InitSeaLevel);
    ImGui::InputFloat("sun luminace", &param.m_SunLuminance);
    ImGui::InputFloat("atmosphere width", &param.m_AtmosphereWidth);
    ImGui::InputFloat("rayleigh scalar height", &param.m_RayleighScalarHeight);
    ImGui::InputFloat("mie scalar height", &param.m_MieScalarHeight);
    ImGui::InputFloat("mie anisotropy", &param.m_MieAnisotropy);
    ImGui::InputFloat("ozone center height", &param.m_OzoneLevelCenterHeight);
    ImGui::InputFloat("ozone width", &param.m_OzoneLevelWidth);
}

void VansGraphics::VansHierachuWindow::ShowWindow(VansVKDevice& device)
{
    DrawRenderNodeList();

    DrawRenderNodeDetail();
}
