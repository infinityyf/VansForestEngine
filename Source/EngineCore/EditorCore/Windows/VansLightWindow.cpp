#include "VansLightWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansMaterial.h"

#include "imgui.h"

void VansGraphics::VansLightWindow::ShowWindow(VansVKDevice& device)
{
    ImGui::Begin("Light Info");
    
    for (auto& node : m_Scene->GetLightManager()->GetDirectionLights())
    {
        float direction[3] = { node.m_Direction.x, node.m_Direction.y, node.m_Direction.z };
        ImGui::SliderFloat3("direct light direction", direction, -1, 1);
        node.m_Direction = glm::vec3(direction[0], direction[1], direction[2]);
    }

    for (auto& node : m_Scene->GetLightManager()->GetPointLights())
    {
    }

    for (auto& node : m_Scene->GetLightManager()->GetSpotLight())
    {
    }

    ImGui::Separator();
    DrawFogParameters(device);

    ImGui::End();
}

void VansGraphics::VansLightWindow::DrawFogParameters(VansVKDevice& device)
{
    if (!ImGui::CollapsingHeader("Volumetric Fog"))
        return;

    bool changed = false;

    changed |= ImGui::DragFloat("Fog Density",        &m_FogParams.fogDensity,      0.0001f, 0.0f,    0.1f,    "%.5f");
    changed |= ImGui::DragFloat("Height Falloff",     &m_FogParams.heightFalloff,   0.001f,  0.0f,    1.0f,    "%.4f");
    changed |= ImGui::DragFloat("Sun Scatter Scale",  &m_FogParams.sunScatterScale, 0.01f,   0.0f,    5.0f,    "%.3f");
    changed |= ImGui::DragFloat("Ambient Scale",      &m_FogParams.ambientScale,    0.01f,   0.0f,    5.0f,    "%.3f");
    changed |= ImGui::DragFloat("Fog Min Height",     &m_FogParams.fogMinHeight,    1.0f,   -10000.0f, 10000.0f, "%.1f");
    ImGui::InputFloat("Sky Fog Distance",   &m_FogParams.skyFogDistance);

    if (changed)
    {
        m_Scene->GetMaterialManager()->m_FogParamsCBBuffer.SetBufferData(
            &m_FogParams, 0, sizeof(FogParamsData));
    }
}
