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

            // ── Displacement Strength ──────────────────────────────────
            float dispStr = terrain->GetTessDisplacementStrength();
            if (ImGui::DragFloat("Micro-Displacement (m)", &dispStr, 0.005f, 0.0f, 0.0f, "%.3f"))
                terrain->SetTessDisplacementStrength(dispStr);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Normal-map Y displacement in world meters. 0=off, try 0.5~2.0 for visible effect.");

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
