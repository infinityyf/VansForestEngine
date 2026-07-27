#include "VansPcgWindow.h"

#include "../VansPcgDebugDataService.h"
#include "../VansEditorWindow.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace VansGraphics
{
namespace
{
std::string FormatVec2(const glm::vec2& value)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f, %.2f", value.x, value.y);
    return buffer;
}

float PreviewValue(const PcgPlacementMask& mask, uint32_t x, uint32_t y)
{
    const size_t index = static_cast<size_t>(y) * mask.width + x;
    if (index >= mask.values.size())
        return 0.0f;

    float value = mask.values[index];
    if (mask.invert)
        value = 1.0f - value;
    value = std::clamp(value * std::max(mask.densityScale, 0.0f), 0.0f, 1.0f);
    return value >= mask.threshold ? value : 0.0f;
}

void DrawMaskPreview(const PcgPlacementMask& mask)
{
    if (!mask.enabled || mask.width == 0 || mask.height == 0 || mask.values.empty())
    {
        ImGui::TextDisabled("No preview data");
        return;
    }

    const float maxSide = 160.0f;
    const float aspect = static_cast<float>(mask.width) / static_cast<float>(mask.height);
    const float width = aspect >= 1.0f ? maxSide : maxSide * aspect;
    const float height = aspect >= 1.0f ? maxSide / aspect : maxSide;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, height);

    ImGui::InvisibleButton(("##pcgMaskPreview_" + mask.name).c_str(), size);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(start, ImVec2(start.x + width, start.y + height), IM_COL32(20, 20, 22, 255));

    const uint32_t columns = std::min<uint32_t>(mask.width, 72u);
    const uint32_t rows = std::min<uint32_t>(mask.height, 72u);
    for (uint32_t y = 0; y < rows; ++y)
    {
        const uint32_t sourceY = std::min<uint32_t>(
            mask.height - 1,
            static_cast<uint32_t>((static_cast<float>(y) + 0.5f) * static_cast<float>(mask.height) / static_cast<float>(rows)));
        const float y0 = start.y + height * static_cast<float>(y) / static_cast<float>(rows);
        const float y1 = start.y + height * static_cast<float>(y + 1) / static_cast<float>(rows);
        for (uint32_t x = 0; x < columns; ++x)
        {
            const uint32_t sourceX = std::min<uint32_t>(
                mask.width - 1,
                static_cast<uint32_t>((static_cast<float>(x) + 0.5f) * static_cast<float>(mask.width) / static_cast<float>(columns)));
            const float value = PreviewValue(mask, sourceX, sourceY);
            const int intensity = static_cast<int>(std::round(value * 255.0f));
            const ImU32 color = IM_COL32(intensity, intensity, intensity, 255);
            const float x0 = start.x + width * static_cast<float>(x) / static_cast<float>(columns);
            const float x1 = start.x + width * static_cast<float>(x + 1) / static_cast<float>(columns);
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
        }
    }
    drawList->AddRect(start, ImVec2(start.x + width, start.y + height), IM_COL32(90, 90, 96, 255));
}

void DrawMaskDetails(const PcgPlacementMask& mask)
{
    ImGui::Text("Name: %s", mask.name.c_str());
    if (!mask.assetGuid.empty())
        ImGui::TextWrapped("Asset GUID: %s", mask.assetGuid.c_str());
    ImGui::Text("Size: %u x %u", mask.width, mask.height);
    ImGui::Text("Channel: %s", mask.channel.c_str());
    ImGui::Text("Bounds: [%s] -> [%s]",
        FormatVec2(mask.worldMinXZ).c_str(),
        FormatVec2(mask.worldMaxXZ).c_str());
    ImGui::Text("Threshold: %.3f  Density: %.3f  Invert: %s",
        mask.threshold,
        mask.densityScale,
        mask.invert ? "true" : "false");
    if (!mask.sourcePath.empty())
        ImGui::TextWrapped("Source: %s", mask.sourcePath.c_str());
    if (!mask.resolvedPath.empty())
        ImGui::TextWrapped("Resolved: %s", mask.resolvedPath.c_str());
}

void DrawMaskBlock(const char* title, const PcgPlacementMask& mask)
{
    if (!mask.enabled)
        return;

    if (ImGui::TreeNode(title))
    {
        DrawMaskPreview(mask);
        ImGui::SameLine();
        ImGui::BeginGroup();
        DrawMaskDetails(mask);
        ImGui::EndGroup();
        ImGui::TreePop();
    }
}
}

void VansPcgWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!VansEditorWindow::m_PcgWindowOpen)
        return;

    if (!ImGui::Begin("PCG", &VansEditorWindow::m_PcgWindowOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Module: Source/EngineCore/RenderCore/PcgCore");
    ImGui::Text("Masks: pcg.masks or legacy masks; use texture guid references; consumers: placement.mask, trees.randomInstances.mask");
    ImGui::Separator();

    const std::vector<PcgVegetationDebugEntry> entries =
        VansPcgDebugDataService::Collect(editorAPI.GetProjectRootPath(), VansEditorWindow::GetSceneDocument());
    if (entries.empty())
    {
        ImGui::TextDisabled("No vegetation PCG configuration found in the current scene or Assets/Vegetation.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("PcgVegetationTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Instances");
        ImGui::TableSetupColumn("Placement");
        ImGui::TableSetupColumn("Configured Masks");
        ImGui::TableSetupColumn("References");
        ImGui::TableHeadersRow();

        for (const PcgVegetationDebugEntry& entry : entries)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", entry.label.c_str());
            ImGui::TextDisabled("%s", entry.sourcePath.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", entry.instanceCount);
            ImGui::TableSetColumnIndex(2);
            if (entry.hasPlacement)
                ImGui::Text("[%s] -> [%s]",
                    FormatVec2(entry.placementMinXZ).c_str(),
                    FormatVec2(entry.placementMaxXZ).c_str());
            else
                ImGui::TextDisabled("default");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%zu", entry.configuredMasks.size());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("grass: %s", entry.grassMaskRef.empty() ? "<none>" : entry.grassMaskRef.c_str());
            ImGui::Text("trees: %s", entry.treeMaskRef.empty() ? "<none>" : entry.treeMaskRef.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    for (const PcgVegetationDebugEntry& entry : entries)
    {
        if (!ImGui::TreeNode(entry.label.c_str()))
            continue;

        ImGui::TextWrapped("Source: %s", entry.sourcePath.c_str());
        ImGui::TextWrapped("JSON: %s", entry.jsonPath.c_str());
        ImGui::Text("Placement Bounds: [%s] -> [%s]",
            FormatVec2(entry.placementMinXZ).c_str(),
            FormatVec2(entry.placementMaxXZ).c_str());

        if (entry.configuredMasks.empty())
        {
            ImGui::TextDisabled("No configured masks in pcg.masks / masks.");
        }
        else
        {
            for (const PcgPlacementMask& mask : entry.configuredMasks)
                DrawMaskBlock(mask.name.c_str(), mask);
        }

        DrawMaskBlock("Resolved Grass Mask", entry.grassMask);
        DrawMaskBlock("Resolved Tree Mask", entry.treeMask);
        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace VansGraphics
