#include "VansTerrainWindow.h"
#include "../VansEditorWindow.h"

#include "imgui.h"

namespace VansGraphics
{

void VansTerrainWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!VansEditorWindow::m_TerrainWindowOpen)
        return;

    Vans::EditorAPI::TerrainSettingsSnapshot settings = editorAPI.GetTerrainSettings();

    ImGui::Begin("Terrain");
    if (!settings.available)
    {
        ImGui::TextDisabled("Scene has no terrain. Add \"terrain\" block to Scene JSON and reload.");
        ImGui::End();
        return;
    }

    auto apply = [&]()
    {
        editorAPI.ApplyTerrainSettings(settings);
    };

    if (ImGui::BeginTabBar("TerrainTabs"))
    {
        if (ImGui::BeginTabItem("Tessellation"))
        {
            if (ImGui::Checkbox("Enable Tessellation", &settings.tessellationEnabled))
                apply();
            ImGui::Separator();

            if (settings.tessellationEnabled)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Tessellation ACTIVE");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Tessellation OFF (using VS path)");
            ImGui::Separator();

            if (ImGui::DragFloat("Tess Distance (m)", &settings.tessellationDistance, 1.0f, 1.0f, 2000.0f, "%.0f"))
                apply();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Patches within this distance use GPU tessellation. Beyond: VS-only path.");

            if (ImGui::SliderFloat("Max Tess Level", &settings.maxTessellationLevel, 1.0f, 64.0f, "%.0f"))
                apply();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum triangle subdivision level. Higher = more detail but more GPU cost.");

            if (ImGui::DragFloat("Falloff Power", &settings.tessellationPower, 0.1f, 0.1f, 10.0f, "%.1f"))
                apply();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exponent for distance falloff. 1.0=linear, 2.0=quadratic (smoother falloff).");

            if (ImGui::SliderFloat("LOD Bias", &settings.tessLodBias, 0.1f, 3.0f, "%.2f"))
                apply();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Coarser CPU patches within tess range (lower = fewer larger patches). <1 reduces instance count.");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Procedural Noise Detail");

            if (ImGui::Checkbox("Enable Noise Detail", &settings.noiseDetailEnabled))
                apply();

            if (settings.noiseDetailEnabled)
            {
                if (ImGui::DragFloat("Strength (m)", &settings.noiseStrength, 0.001f, 0.0f, 0.5f, "%.3f"))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Noise displacement in world meters. 0.03 = 3cm micro-detail.");

                if (ImGui::DragFloat("Frequency", &settings.noiseFrequency, 0.01f, 0.01f, 10.0f, "%.2f"))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base noise frequency. Higher = finer detail pattern.");

                if (ImGui::SliderInt("Octaves", &settings.noiseOctaves, 1, 8))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Number of noise layers. More = richer detail but more GPU cost.");

                if (ImGui::DragFloat("Gain", &settings.noiseGain, 0.01f, 0.01f, 1.0f, "%.2f"))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Amplitude falloff per octave. 0.52 = original ShaderToy hill().");

                if (ImGui::DragFloat("Lacunarity", &settings.noiseLacunarity, 0.1f, 1.0f, 4.0f, "%.1f"))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frequency multiplier per octave. 2.0 = standard.");

                if (ImGui::DragFloat("Warp Strength", &settings.noiseWarpStrength, 0.01f, 0.0f, 1.0f, "%.2f"))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Domain warping. 0=off, 0.2~0.3 = visible ridge-like distortion (HIGH/ULTRA quality).");

                if (ImGui::SliderFloat("Fade Start", &settings.noiseFadeStart, 0.0f, 1.0f, "%.2f"))
                    apply();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance ratio where noise begins fading out. 0.7 = fade starts at 70%% of tess distance.");
            }

            ImGui::Separator();
            ImGui::Text("Terrain Size: %.0f m", settings.terrainSize);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("LOD"))
        {
            if (ImGui::DragFloat("Split Distance Mult", &settings.splitDistMult, 0.1f, 0.1f, 10.0f, "%.1f"))
                apply();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Patch splits when dist < nodeSize * this. Higher = more splits = finer patches.");

            if (ImGui::DragFloat("LOD Distance Ratio", &settings.lodDistanceRatio, 0.1f, 1.0f, 10.0f, "%.1f"))
                apply();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("CDLOD visible range growth ratio. Reserved for future CDLOD morph.");

            ImGui::Text("Tess LOD Bias: %.2f", settings.tessLodBias);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace VansGraphics
