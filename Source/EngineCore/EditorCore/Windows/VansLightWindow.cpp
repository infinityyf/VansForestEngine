#include "VansLightWindow.h"
#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool g_CommandMergeBoundaryReached = false;

    void TrackCommandMergeBoundary()
    {
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            g_CommandMergeBoundaryReached = true;
        }
    }

    bool DragFloatTracked(
        const char* label,
        float* value,
        float speed,
        float minValue,
        float maxValue,
        const char* format)
    {
        const bool changed = ImGui::DragFloat(label, value, speed, minValue, maxValue, format);
        TrackCommandMergeBoundary();
        return changed;
    }

    bool DragFloat3Tracked(
        const char* label,
        float value[3],
        float speed,
        float minValue,
        float maxValue,
        const char* format)
    {
        const bool changed = ImGui::DragFloat3(label, value, speed, minValue, maxValue, format);
        TrackCommandMergeBoundary();
        return changed;
    }

    bool InputFloatTracked(const char* label, float* value)
    {
        const bool changed = ImGui::InputFloat(label, value);
        TrackCommandMergeBoundary();
        return changed;
    }

    float Dot(const Vans::EditorAPI::Vec3& a, const Vans::EditorAPI::Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vans::EditorAPI::Vec3 Normalize(const Vans::EditorAPI::Vec3& value)
    {
        const float lengthSq = Dot(value, value);
        if (lengthSq <= 1e-6f)
        {
            return { 0.0f, -1.0f, 0.0f };
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        return { value.x * invLength, value.y * invLength, value.z * invLength };
    }

    Vans::EditorAPI::Vec3 NormalizeLightDirection(
        const Vans::EditorAPI::Vec3& direction,
        const Vans::EditorAPI::Vec3& fallbackDirection)
    {
        constexpr float MIN_DIRECTION_LENGTH_SQ = 1e-6f;

        if (Dot(direction, direction) > MIN_DIRECTION_LENGTH_SQ)
        {
            return Normalize(direction);
        }

        if (Dot(fallbackDirection, fallbackDirection) > MIN_DIRECTION_LENGTH_SQ)
        {
            return Normalize(fallbackDirection);
        }

        return { 0.0f, -1.0f, 0.0f };
    }

    bool EditColor3(const char* label, Vans::EditorAPI::Vec3& color)
    {
        float colorValue[3] = { color.x, color.y, color.z };
        const bool changed = ImGui::ColorEdit3(label, colorValue);
        TrackCommandMergeBoundary();
        if (!changed)
        {
            return false;
        }

        color = { colorValue[0], colorValue[1], colorValue[2] };
        return true;
    }

    bool EditFloat3(
        const char* label,
        Vans::EditorAPI::Vec3& value,
        float speed,
        float minValue,
        float maxValue,
        const char* format)
    {
        float valueArray[3] = { value.x, value.y, value.z };
        if (!DragFloat3Tracked(label, valueArray, speed, minValue, maxValue, format))
        {
            return false;
        }

        value = { valueArray[0], valueArray[1], valueArray[2] };
        return true;
    }

    bool EditDirection3(const char* label, Vans::EditorAPI::Vec3& direction)
    {
        const Vans::EditorAPI::Vec3 previousDirection = direction;
        if (!EditFloat3(label, direction, 0.01f, -1.0f, 1.0f, "%.3f"))
        {
            return false;
        }

        direction = NormalizeLightDirection(direction, previousDirection);
        return true;
    }

    float ToDegrees(float radians)
    {
        constexpr float RAD_TO_DEG = 57.29577951308232f;
        return radians * RAD_TO_DEG;
    }

    float ToRadians(float degrees)
    {
        constexpr float DEG_TO_RAD = 0.017453292519943295f;
        return degrees * DEG_TO_RAD;
    }
}

void VansGraphics::VansLightWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!VansEditorWindow::m_LightWindowOpen)
        return;

    g_CommandMergeBoundaryReached = false;
    ImGui::Begin("Light Info");

    Vans::EditorAPI::LightingSettingsSnapshot lightingSettings = editorAPI.GetLightingSettings();

    bool lightChanged = false;
    lightChanged |= DrawDirectionalLights(lightingSettings.directionalLights);
    lightChanged |= DrawPointLights(lightingSettings.pointLights);
    lightChanged |= DrawSpotLights(lightingSettings.spotLights);

    if (lightChanged)
    {
        editorAPI.ApplyLightingSettings(lightingSettings);
    }

    ImGui::Separator();
    DrawFogParameters(editorAPI);
    DrawFogVolumeParameters(editorAPI);
    DrawCloudParameters(editorAPI);

    if (g_CommandMergeBoundaryReached)
    {
        editorAPI.BreakCommandMergeGroup();
    }

    ImGui::End();
}

bool VansGraphics::VansLightWindow::DrawDirectionalLights(std::vector<Vans::EditorAPI::DirectionalLightSettings>& directionLights)
{
    if (!ImGui::CollapsingHeader("Directional Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(directionLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Directional Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditDirection3("Direction", directionLights[lightIndex].direction);
            changed |= EditColor3("Color", directionLights[lightIndex].color);
            changed |= DragFloatTracked("Intensity", &directionLights[lightIndex].intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawPointLights(std::vector<Vans::EditorAPI::PointLightSettings>& pointLights)
{
    if (!ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(pointLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Point Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditFloat3("Position", pointLights[lightIndex].position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditColor3("Color", pointLights[lightIndex].color);
            changed |= DragFloatTracked("Intensity", &pointLights[lightIndex].intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= DragFloatTracked("Radius", &pointLights[lightIndex].radius, 0.1f, 0.01f, 10000.0f, "%.2f");
            pointLights[lightIndex].radius = std::max(pointLights[lightIndex].radius, 0.01f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawSpotLights(std::vector<Vans::EditorAPI::SpotLightSettings>& spotLights)
{
    if (!ImGui::CollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(spotLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Spot Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditFloat3("Position", spotLights[lightIndex].position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditDirection3("Direction", spotLights[lightIndex].direction);
            changed |= EditColor3("Color", spotLights[lightIndex].color);
            changed |= DragFloatTracked("Intensity", &spotLights[lightIndex].intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= DragFloatTracked("Radius", &spotLights[lightIndex].radius, 0.1f, 0.01f, 10000.0f, "%.2f");

            float innerCutoffDegrees = ToDegrees(spotLights[lightIndex].innerCutoffRadians);
            float outerCutoffDegrees = ToDegrees(spotLights[lightIndex].outerCutoffRadians);
            bool cutoffChanged = false;
            cutoffChanged |= DragFloatTracked("Inner Cutoff", &innerCutoffDegrees, 0.1f, 0.1f, 89.0f, "%.2f deg");
            cutoffChanged |= DragFloatTracked("Outer Cutoff", &outerCutoffDegrees, 0.1f, 0.1f, 89.5f, "%.2f deg");
            if (cutoffChanged)
            {
                innerCutoffDegrees = std::clamp(innerCutoffDegrees, 0.1f, 89.0f);
                outerCutoffDegrees = std::clamp(outerCutoffDegrees, innerCutoffDegrees, 89.5f);
                spotLights[lightIndex].innerCutoffRadians = ToRadians(innerCutoffDegrees);
                spotLights[lightIndex].outerCutoffRadians = ToRadians(outerCutoffDegrees);
                changed = true;
            }

            spotLights[lightIndex].radius = std::max(spotLights[lightIndex].radius, 0.01f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

void VansGraphics::VansLightWindow::DrawFogParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!ImGui::CollapsingHeader("Volumetric Fog"))
        return;

    Vans::EditorAPI::FogSettings fogParams = editorAPI.GetFogSettings();
    bool changed = false;

    changed |= DragFloatTracked("Fog Density",        &fogParams.fogDensity,      0.0001f, 0.0f,    0.1f,    "%.5f");
    changed |= DragFloatTracked("Height Falloff",     &fogParams.heightFalloff,   0.001f,  0.0f,    1.0f,    "%.4f");
    changed |= DragFloatTracked("Sun Scatter Scale",  &fogParams.sunScatterScale, 0.01f,   0.0f,    5.0f,    "%.3f");
    changed |= DragFloatTracked("Ambient Scale",      &fogParams.ambientScale,    0.01f,   0.0f,    5.0f,    "%.3f");
    changed |= DragFloatTracked("Fog Min Height",     &fogParams.fogMinHeight,    1.0f,   -10000.0f, 10000.0f, "%.1f");
    changed |= InputFloatTracked("Sky Fog Distance",  &fogParams.skyFogDistance);

    if (changed)
    {
        editorAPI.ApplyFogSettings(fogParams);
    }
}

void VansGraphics::VansLightWindow::DrawFogVolumeParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!ImGui::CollapsingHeader("Volumetric Fog Volume"))
        return;

    Vans::EditorAPI::FogVolumeSettings fogVolumeParams = editorAPI.GetFogVolumeSettings();
    bool changed = false;

    changed |= DragFloatTracked("Volume Density",      &fogVolumeParams.density,      0.01f,  0.0f,  10.0f,  "%.3f");
    changed |= DragFloatTracked("Anisotropy (g)",      &fogVolumeParams.anisotropy,   0.01f, -1.0f,   1.0f,  "%.3f");
    changed |= DragFloatTracked("Scatter Scale",       &fogVolumeParams.scatterScale, 0.01f,  0.0f,  10.0f,  "%.3f");
    changed |= DragFloatTracked("Volume Ambient Scale", &fogVolumeParams.ambientScale, 0.005f, 0.0f,   5.0f,  "%.4f");
    changed |= DragFloatTracked("Volume Near",         &fogVolumeParams.volumeNear,   0.1f,   0.01f, 100.0f, "%.2f");
    changed |= DragFloatTracked("Volume Far",          &fogVolumeParams.volumeFar,    1.0f,   1.0f,  2000.0f, "%.1f");
    changed |= DragFloatTracked("Slice Power",         &fogVolumeParams.slicePower,   0.05f,  0.1f,  10.0f,  "%.2f");

    ImGui::Separator();
    changed |= DragFloat3Tracked("Fog Box Min", fogVolumeParams.fogBoxMin, 0.5f, -10000.0f, 10000.0f, "%.1f");
    changed |= DragFloat3Tracked("Fog Box Max", fogVolumeParams.fogBoxMax, 0.5f, -10000.0f, 10000.0f, "%.1f");

    if (changed)
    {
        editorAPI.ApplyFogVolumeSettings(fogVolumeParams);
    }
}

void VansGraphics::VansLightWindow::DrawCloudParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!ImGui::CollapsingHeader("Volumetric Clouds", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    Vans::EditorAPI::CloudSettings cloudParams = editorAPI.GetCloudSettings();
    bool changed = false;

    float cloudBaseHeight = cloudParams.cloudMinHeight;
    float cloudThickness = std::max(cloudParams.cloudMaxHeight - cloudParams.cloudMinHeight, 100.0f);
    bool heightChanged = false;
    heightChanged |= DragFloatTracked("Cloud Base Height", &cloudBaseHeight, 10.0f, 0.0f, 20000.0f, "%.0f m");
    heightChanged |= DragFloatTracked("Cloud Thickness", &cloudThickness, 10.0f, 100.0f, 30000.0f, "%.0f m");
    if (heightChanged)
    {
        cloudBaseHeight = std::clamp(cloudBaseHeight, 0.0f, 20000.0f);
        cloudThickness = std::clamp(cloudThickness, 100.0f, 30000.0f);
        cloudParams.cloudMinHeight = cloudBaseHeight;
        cloudParams.cloudMaxHeight = cloudBaseHeight + cloudThickness;
        changed = true;
    }

    changed |= DragFloatTracked("Density", &cloudParams.density, 0.001f, 0.0f, 0.5f, "%.4f");
    changed |= DragFloatTracked("Coverage", &cloudParams.coverage, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Sun Brightness", &cloudParams.sunBrightness, 0.01f, 0.0f, 10.0f, "%.3f");
    changed |= DragFloatTracked("Phase G", &cloudParams.phaseG, 0.005f, -0.5f, 1.0f, "%.3f");

    ImGui::Separator();
    changed |= DragFloatTracked("Main Tile", &cloudParams.mainTileMeters, 100.0f, 5000.0f, 200000.0f, "%.0f m");
    changed |= DragFloatTracked("Detail Tile", &cloudParams.detailTileMeters, 50.0f, 1000.0f, 50000.0f, "%.0f m");
    changed |= DragFloatTracked("Main Height Scale", &cloudParams.mainHeightScale, 0.01f, 0.0f, 8.0f, "%.2f");
    changed |= DragFloatTracked("Detail Height Scale", &cloudParams.detailHeightScale, 0.01f, 0.0f, 16.0f, "%.2f");

    ImGui::Separator();
    changed |= DragFloatTracked("Overcast Threshold", &cloudParams.thresholdLowCoverage, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Clear Threshold", &cloudParams.thresholdHighCoverage, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Density Smooth Low", &cloudParams.densityRemapLow, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Density Smooth High", &cloudParams.densityRemapHigh, 0.005f, 0.01f, 1.0f, "%.3f");

    ImGui::Separator();
    changed |= DragFloatTracked("Main Erosion", &cloudParams.mainErosionStrength, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Detail Erosion", &cloudParams.detailErosionStrength, 0.01f, 0.0f, 3.0f, "%.3f");
    changed |= DragFloatTracked("Edge Erosion", &cloudParams.edgeErosionStrength, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Vertical Shape Power", &cloudParams.verticalShapePower, 0.01f, 0.1f, 4.0f, "%.3f");
    changed |= DragFloatTracked("Detail Erosion Low", &cloudParams.detailErosionLow, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Detail Erosion High", &cloudParams.detailErosionHigh, 0.005f, 0.01f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Detail Edge Strength", &cloudParams.detailEdgeStrength, 0.01f, 0.0f, 3.0f, "%.3f");
    changed |= DragFloatTracked("Shadow Density", &cloudParams.shadowDensityScale, 0.01f, 0.0f, 5.0f, "%.3f");

    if (ImGui::Button("Reset Cloud Defaults"))
    {
        editorAPI.ResetCloudSettings();
        editorAPI.BreakCommandMergeGroup();
        return;
    }

    cloudParams.mainTileMeters = std::max(cloudParams.mainTileMeters, 1000.0f);
    cloudParams.detailTileMeters = std::max(cloudParams.detailTileMeters, 500.0f);
    cloudParams.densityRemapHigh = std::max(cloudParams.densityRemapHigh, cloudParams.densityRemapLow + 0.01f);
    cloudParams.detailErosionHigh = std::max(cloudParams.detailErosionHigh, cloudParams.detailErosionLow + 0.01f);

    if (changed)
    {
        editorAPI.ApplyCloudSettings(cloudParams);
    }
}
