#include "VansLightWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansMaterial.h"

#include "imgui.h"

#include <algorithm>

namespace
{
    glm::vec3 NormalizeLightDirection(const glm::vec3& direction, const glm::vec3& fallbackDirection)
    {
        constexpr float MIN_DIRECTION_LENGTH_SQ = 1e-6f;

        if (glm::dot(direction, direction) > MIN_DIRECTION_LENGTH_SQ)
        {
            return glm::normalize(direction);
        }

        if (glm::dot(fallbackDirection, fallbackDirection) > MIN_DIRECTION_LENGTH_SQ)
        {
            return glm::normalize(fallbackDirection);
        }

        return glm::vec3(0.0f, -1.0f, 0.0f);
    }

    bool EditColor3(const char* label, glm::vec3& color)
    {
        float colorValue[3] = { color.x, color.y, color.z };
        if (!ImGui::ColorEdit3(label, colorValue))
        {
            return false;
        }

        color = glm::vec3(colorValue[0], colorValue[1], colorValue[2]);
        return true;
    }

    bool EditFloat3(const char* label, glm::vec3& value, float speed, float minValue, float maxValue, const char* format)
    {
        float valueArray[3] = { value.x, value.y, value.z };
        if (!ImGui::DragFloat3(label, valueArray, speed, minValue, maxValue, format))
        {
            return false;
        }

        value = glm::vec3(valueArray[0], valueArray[1], valueArray[2]);
        return true;
    }

    bool EditDirection3(const char* label, glm::vec3& direction)
    {
        const glm::vec3 previousDirection = direction;
        if (!EditFloat3(label, direction, 0.01f, -1.0f, 1.0f, "%.3f"))
        {
            return false;
        }

        direction = NormalizeLightDirection(direction, previousDirection);
        return true;
    }
}

void VansGraphics::VansLightWindow::ShowWindow(VansVKDevice& device)
{
    ImGui::Begin("Light Info");

    bool lightChanged = false;
    lightChanged |= DrawDirectionalLights();
    lightChanged |= DrawPointLights();
    lightChanged |= DrawSpotLights();

    if (lightChanged)
    {
        SyncLightDataToGPU();
    }

    ImGui::Separator();
    DrawFogParameters(device);
    DrawFogVolumeParameters(device);

    ImGui::End();
}

bool VansGraphics::VansLightWindow::DrawDirectionalLights()
{
    if (!ImGui::CollapsingHeader("Directional Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    auto& directionLights = m_Scene->GetLightManager()->GetDirectionLights();
    for (int lightIndex = 0; lightIndex < static_cast<int>(directionLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Directional Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditDirection3("Direction", directionLights[lightIndex].m_Direction);
            changed |= EditColor3("Color", directionLights[lightIndex].m_Color);
            changed |= ImGui::DragFloat("Intensity", &directionLights[lightIndex].m_Intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawPointLights()
{
    if (!ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    auto& pointLights = m_Scene->GetLightManager()->GetPointLights();
    for (int lightIndex = 0; lightIndex < static_cast<int>(pointLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Point Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditFloat3("Position", pointLights[lightIndex].m_Position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditColor3("Color", pointLights[lightIndex].m_Color);
            changed |= ImGui::DragFloat("Intensity", &pointLights[lightIndex].m_Intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= ImGui::DragFloat("Radius", &pointLights[lightIndex].m_Radius, 0.1f, 0.01f, 10000.0f, "%.2f");
            pointLights[lightIndex].m_Radius = std::max(pointLights[lightIndex].m_Radius, 0.01f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawSpotLights()
{
    if (!ImGui::CollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    auto& spotLights = m_Scene->GetLightManager()->GetSpotLight();
    for (int lightIndex = 0; lightIndex < static_cast<int>(spotLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Spot Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditFloat3("Position", spotLights[lightIndex].m_Position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditDirection3("Direction", spotLights[lightIndex].m_Direction);
            changed |= EditColor3("Color", spotLights[lightIndex].m_Color);
            changed |= ImGui::DragFloat("Intensity", &spotLights[lightIndex].m_Intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= ImGui::DragFloat("Radius", &spotLights[lightIndex].m_Radius, 0.1f, 0.01f, 10000.0f, "%.2f");

            float innerCutoffDegrees = glm::degrees(spotLights[lightIndex].m_InnerCutOff);
            float outerCutoffDegrees = glm::degrees(spotLights[lightIndex].m_OuterCutOff);
            bool cutoffChanged = false;
            cutoffChanged |= ImGui::DragFloat("Inner Cutoff", &innerCutoffDegrees, 0.1f, 0.1f, 89.0f, "%.2f deg");
            cutoffChanged |= ImGui::DragFloat("Outer Cutoff", &outerCutoffDegrees, 0.1f, 0.1f, 89.5f, "%.2f deg");
            if (cutoffChanged)
            {
                innerCutoffDegrees = std::clamp(innerCutoffDegrees, 0.1f, 89.0f);
                outerCutoffDegrees = std::clamp(outerCutoffDegrees, innerCutoffDegrees, 89.5f);
                spotLights[lightIndex].m_InnerCutOff = glm::radians(innerCutoffDegrees);
                spotLights[lightIndex].m_OuterCutOff = glm::radians(outerCutoffDegrees);
                changed = true;
            }

            spotLights[lightIndex].m_Radius = std::max(spotLights[lightIndex].m_Radius, 0.01f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

void VansGraphics::VansLightWindow::SyncLightDataToGPU()
{
    VansCamera* camera = m_Scene->GetCamera();
    if (camera == nullptr)
    {
        return;
    }

    m_Scene->GetLightManager()->SyncLightGPUData(glm::vec3(camera->GetPosition()));
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
