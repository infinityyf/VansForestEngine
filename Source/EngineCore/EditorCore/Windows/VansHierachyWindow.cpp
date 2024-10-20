#include "VansHierachyWindow.h"
#include "../../RenderCore/VansScene.h"

#include "imgui.h"

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
        for (auto& node : m_Scene->m_OpaqueRenderNodes)
        {
            if (ImGui::Selectable(node->m_NodeName.c_str(), m_Scene->m_SelectedNode == node))
            {
                m_Scene->m_SelectedNode = node;
            }
        }
    }

    if (ImGui::CollapsingHeader("Transparent Nodes"))
    {
        for (auto& node : m_Scene->m_TransParentRenderNodes)
        {
            if (ImGui::Selectable(node->m_NodeName.c_str(), m_Scene->m_SelectedNode == node))
            {
                m_Scene->m_SelectedNode = node;
            }
        }
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

        ImGui::Text("Asset Name: %s", m_Scene->m_SelectedNode->m_NodeName.c_str());
        
        if (ImGui::CollapsingHeader("Material"))
        {
            if (m_Scene->m_SelectedNode->m_Material != nullptr)
            {
				DrawMaterialDetail(*m_Scene->m_SelectedNode->m_Material);
			}
		}

        ImGui::End();
    }
}

void VansGraphics::VansHierachuWindow::DrawMaterialDetail(VansMaterial& material)
{
	switch (material.m_MaterialType)
	{
	case VansMaterialType::VAN_PBR:
		DrawPBRMaterialParameters(material.m_BasePBRParam);
		break;
	case VansMaterialType::VAN_SKY_BOX:
        DrawAtmosphereParameters(material.m_AtmospherePBRParam);
		break;
	default:
		break;
	}
}

void VansGraphics::VansHierachuWindow::DrawPBRMaterialParameters(VansBasePBRParam& param)
{
    ImGui::SliderFloat("metalic", &param.m_metallic, 0, 1);
    ImGui::SliderFloat("roughness", &param.m_roughness, 0, 1);
}

void VansGraphics::VansHierachuWindow::DrawAtmosphereParameters(VansAtmospherePBRParam& param)
{
    ImGui::InputFloat("planet radius", &param.m_PlanetRadius);
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
    //ªÊ÷∆menu bar
    ImGui::Begin("Another Window", &m_TestButton);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
    ImGui::Text("Hello from another window!");
    if (ImGui::Button("Close Me"))
    {
        m_TestButton = false;
    }

    ImGui::End();
    
    DrawRenderNodeList();

    DrawRenderNodeDetail();
}
