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
    DrawFogVolumeParameters(device);

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

void VansGraphics::VansLightWindow::DrawFogVolumeParameters(VansVKDevice& device)
{
    if (!ImGui::CollapsingHeader("Volumetric Fog Volume"))
        return;

    bool changed = false;

    changed |= ImGui::DragFloat("Volume Density",      &m_FogVolumeParams.density,      0.01f,  0.0f,  10.0f,  "%.3f");
    changed |= ImGui::DragFloat("Anisotropy (g)",      &m_FogVolumeParams.anisotropy,   0.01f, -1.0f,   1.0f,  "%.3f");
    changed |= ImGui::DragFloat("Scatter Scale",       &m_FogVolumeParams.scatterScale, 0.01f,  0.0f,  10.0f,  "%.3f");
    changed |= ImGui::DragFloat("Volume Ambient Scale", &m_FogVolumeParams.ambientScale, 0.005f, 0.0f,   5.0f,  "%.4f");
    changed |= ImGui::DragFloat("Volume Near",         &m_FogVolumeParams.volumeNear,   0.1f,   0.01f, 100.0f, "%.2f");
    changed |= ImGui::DragFloat("Volume Far",          &m_FogVolumeParams.volumeFar,    1.0f,   1.0f,  2000.0f, "%.1f");
    changed |= ImGui::DragFloat("Slice Power",         &m_FogVolumeParams.slicePower,   0.05f,  0.1f,  10.0f,  "%.2f");

    ImGui::Separator();
    changed |= ImGui::DragFloat3("Fog Box Min", m_FogVolumeParams.fogBoxMin, 0.5f, -10000.0f, 10000.0f, "%.1f");
    changed |= ImGui::DragFloat3("Fog Box Max", m_FogVolumeParams.fogBoxMax, 0.5f, -10000.0f, 10000.0f, "%.1f");

    if (changed)
    {
        m_Scene->GetMaterialManager()->m_FogVolumeParamsCBBuffer.SetBufferData(
            &m_FogVolumeParams, 0, sizeof(FogVolumeParamsData));
    }
}
