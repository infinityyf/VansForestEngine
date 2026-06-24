#include "VansTerrainWindow.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../RenderCore/TerrainCore/VansTerrain.h"

#include "imgui.h"

namespace VansGraphics
{

void VansTerrainWindow::ShowWindow(VansVKDevice& device)
{
    if (!m_Scene)
    {
        ImGui::Begin("Terrain");
        ImGui::TextDisabled("No scene loaded.");
        ImGui::End();
        return;
    }

    VansRenderNode* terrainRenderNode = m_Scene->m_TerrainRenderNode;
    if (!terrainRenderNode)
    {
        ImGui::Begin("Terrain");
        ImGui::TextDisabled("Scene has no terrain. Add \"terrain\" block to Scene JSON and reload.");
        ImGui::End();
        return;
    }

    VansTerrainRenderNode* terrainNode = dynamic_cast<VansTerrainRenderNode*>(terrainRenderNode);
    if (!terrainNode)
    {
        ImGui::Begin("Terrain");
        ImGui::TextDisabled("Terrain render node type mismatch.");
        ImGui::End();
        return;
    }

    VansTerrain* terrain = terrainNode->GetTerrain();
    if (!terrain)
    {
        ImGui::Begin("Terrain");
        ImGui::TextDisabled("Terrain not initialized.");
        ImGui::End();
        return;
    }

    ImGui::Begin("Terrain");

    if (ImGui::BeginTabBar("TerrainTabs"))
    {
        // ================================================================
        // Tab 1: Tessellation
        // ================================================================
        if (ImGui::BeginTabItem("Tessellation"))
        {
            // ── Enable/Disable ─────────────────────────────────────────
            bool tessEnabled = terrain->IsTessellationEnabled();
            if (ImGui::Checkbox("Enable Tessellation", &tessEnabled))
                terrain->SetTessellationEnabled(tessEnabled);
            ImGui::Separator();

            if (tessEnabled)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Tessellation ACTIVE");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Tessellation OFF (using VS path)");
            }
            ImGui::Separator();

            // ── Tessellation Distance ──────────────────────────────────
            float tessDist = terrain->GetTessellationDistance();
            if (ImGui::DragFloat("Tess Distance (m)", &tessDist, 1.0f, 1.0f, 2000.0f, "%.0f"))
                terrain->SetTessellationDistance(tessDist);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Patches within this distance use GPU tessellation. Beyond: VS-only path.");

            // ── Max Tessellation Level ─────────────────────────────────
            float maxLevel = terrain->GetMaxTessellationLevel();
            if (ImGui::SliderFloat("Max Tess Level", &maxLevel, 1.0f, 64.0f, "%.0f"))
                terrain->SetMaxTessellationLevel(maxLevel);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum triangle subdivision level. Higher = more detail but more GPU cost.");

            // ── Tessellation Power ─────────────────────────────────────
            float tessPower = terrain->GetTessellationPower();
            if (ImGui::DragFloat("Falloff Power", &tessPower, 0.1f, 0.1f, 10.0f, "%.1f"))
                terrain->SetTessellationPower(tessPower);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exponent for distance falloff. 1.0=linear, 2.0=quadratic (smoother falloff).");

            // ── LOD Bias ───────────────────────────────────────────────
            float lodBias = terrain->GetTessLodBias();
            if (ImGui::SliderFloat("LOD Bias", &lodBias, 0.1f, 3.0f, "%.2f"))
                terrain->SetTessLodBias(lodBias);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Coarser CPU patches within tess range (lower = fewer larger patches). <1 reduces instance count.");

            ImGui::Separator();

            // ── 程序化噪声细节 ──────────────────────────────────────────
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Procedural Noise Detail");

            bool noiseEnabled = terrain->IsNoiseDetailEnabled();
            if (ImGui::Checkbox("Enable Noise Detail", &noiseEnabled))
                terrain->SetNoiseDetailEnabled(noiseEnabled);

            if (noiseEnabled)
            {
                float noiseStr = terrain->GetNoiseStrength();
                if (ImGui::DragFloat("Strength (m)", &noiseStr, 0.001f, 0.0f, 0.5f, "%.3f"))
                    terrain->SetNoiseStrength(noiseStr);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Noise displacement in world meters. 0.03 = 3cm micro-detail.");

                float noiseFreq = terrain->GetNoiseFrequency();
                if (ImGui::DragFloat("Frequency", &noiseFreq, 0.01f, 0.01f, 10.0f, "%.2f"))
                    terrain->SetNoiseFrequency(noiseFreq);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base noise frequency. Higher = finer detail pattern.");

                int noiseOct = terrain->GetNoiseOctaves();
                if (ImGui::SliderInt("Octaves", &noiseOct, 1, 8))
                    terrain->SetNoiseOctaves(noiseOct);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Number of noise layers. More = richer detail but more GPU cost.");

                float noiseGain = terrain->GetNoiseGain();
                if (ImGui::DragFloat("Gain", &noiseGain, 0.01f, 0.01f, 1.0f, "%.2f"))
                    terrain->SetNoiseGain(noiseGain);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Amplitude falloff per octave. 0.52 = original ShaderToy hill().");

                float noiseLac = terrain->GetNoiseLacunarity();
                if (ImGui::DragFloat("Lacunarity", &noiseLac, 0.1f, 1.0f, 4.0f, "%.1f"))
                    terrain->SetNoiseLacunarity(noiseLac);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frequency multiplier per octave. 2.0 = standard.");

                float noiseWarp = terrain->GetNoiseWarpStrength();
                if (ImGui::DragFloat("Warp Strength", &noiseWarp, 0.01f, 0.0f, 1.0f, "%.2f"))
                    terrain->SetNoiseWarpStrength(noiseWarp);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Domain warping. 0=off, 0.2~0.3 = visible ridge-like distortion (HIGH/ULTRA quality).");

                float noiseFade = terrain->GetNoiseFadeStart();
                if (ImGui::SliderFloat("Fade Start", &noiseFade, 0.0f, 1.0f, "%.2f"))
                    terrain->SetNoiseFadeStart(noiseFade);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance ratio where noise begins fading out. 0.7 = fade starts at 70%% of tess distance.");
            }

            // ── Patch stats ────────────────────────────────────────────
            ImGui::Separator();
            ImGui::Text("Terrain Size: %.0f m", terrain->GetTerrainSize());

            ImGui::EndTabItem();
        }

        // ================================================================
        // Tab 2: LOD
        // ================================================================
        if (ImGui::BeginTabItem("LOD"))
        {
            float splitDist = terrain->GetSplitDistMult();
            if (ImGui::DragFloat("Split Distance Mult", &splitDist, 0.1f, 0.1f, 10.0f, "%.1f"))
                terrain->SetSplitDistMult(splitDist);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Patch splits when dist < nodeSize * this. Higher = more splits = finer patches.");

            float lodDistRatio = terrain->GetLodDistanceRatio();
            if (ImGui::DragFloat("LOD Distance Ratio", &lodDistRatio, 0.1f, 1.0f, 10.0f, "%.1f"))
                terrain->SetLodDistanceRatio(lodDistRatio);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("CDLOD visible range growth ratio. Reserved for future CDLOD morph.");

            float tessLodBias = terrain->GetTessLodBias();
            ImGui::Text("Tess LOD Bias: %.2f", tessLodBias);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace VansGraphics
