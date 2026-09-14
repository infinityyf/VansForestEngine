#include "VansTerrainWindow.h"
#include "../VansEditorWindow.h"

#include "imgui.h"

#include <algorithm>
#include <iterator>

namespace VansGraphics
{
namespace
{
bool IsSculptTool(Vans::EditorAPI::TerrainBrushTool tool)
{
	using Tool = Vans::EditorAPI::TerrainBrushTool;
	return tool == Tool::Raise || tool == Tool::Lower || tool == Tool::SmoothHeight ||
		tool == Tool::Flatten || tool == Tool::Noise;
}

bool IsPaintTool(Vans::EditorAPI::TerrainBrushTool tool)
{
	using Tool = Vans::EditorAPI::TerrainBrushTool;
	return tool == Tool::PaintLayer || tool == Tool::EraseLayer ||
		tool == Tool::SmoothWeights;
}

const char* BrushPatternName(Vans::EditorAPI::TerrainBrushPattern pattern)
{
	using Pattern = Vans::EditorAPI::TerrainBrushPattern;
	switch (pattern)
	{
	case Pattern::SmoothCircle: return "Smooth Circle";
	case Pattern::LinearCircle: return "Linear Circle";
	case Pattern::Sphere: return "Sphere";
	case Pattern::Tip: return "Tip";
	case Pattern::SoftSquare: return "Soft Square";
	case Pattern::Ridge: return "Ridge";
	case Pattern::Crater: return "Crater Rim";
	case Pattern::Rocky: return "Rocky Noise";
	}
	return "Smooth Circle";
}
}

void VansTerrainWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!VansEditorWindow::m_TerrainWindowOpen)
        return;

    const Vans::EditorAPI::TerrainEditorSnapshot terrain =
		editorAPI.GetTerrainEditorSnapshot();
    const Vans::EditorAPI::TerrainSettingsSnapshot currentSettings =
		editorAPI.GetTerrainSettings();

    ImGui::Begin("Terrain");
    if (!terrain.available || !currentSettings.available)
    {
		ImGui::TextDisabled("No editable terrain asset is loaded.");
		if (!terrain.message.empty())
			ImGui::TextWrapped("%s", terrain.message.c_str());
        ImGui::End();
        return;
    }

	if (!m_HasSettingsDraft || m_SettingsAssetGuid != terrain.assetGuid ||
		!ImGui::IsAnyItemActive())
	{
		m_SettingsDraft = currentSettings;
		m_SettingsAssetGuid = terrain.assetGuid;
		m_HasSettingsDraft = true;
	}

	Vans::EditorAPI::TerrainBrushConfiguration brush;
	brush.enabled = terrain.brushEnabled;
	brush.tool = terrain.tool;
	brush.radius = terrain.radius;
	brush.strength = terrain.strength;
	brush.hardness = terrain.hardness;
	brush.pattern = terrain.pattern;
	brush.rotationRadians = terrain.rotationRadians;
	brush.flattenHeight = terrain.flattenHeight;
	brush.selectedLayer = terrain.selectedLayer;
	if (m_BrushStateAssetGuid != terrain.assetGuid)
	{
		m_BrushStateAssetGuid = terrain.assetGuid;
		m_ActiveBrushMode = BrushMode::None;
		m_LastSculptTool = IsSculptTool(brush.tool)
			? brush.tool : Vans::EditorAPI::TerrainBrushTool::Raise;
		m_LastPaintTool = IsPaintTool(brush.tool)
			? brush.tool : Vans::EditorAPI::TerrainBrushTool::PaintLayer;
	}

	const auto configureBrush = [&]()
	{
		const auto result = editorAPI.ConfigureTerrainBrush(brush);
		m_StatusMessage = result.success ? std::string{} : result.message;
		return result.success;
	};
	const auto activateBrushMode = [&](BrushMode mode)
	{
		if (!terrain.editable || m_ActiveBrushMode == mode)
			return;
		brush.tool = mode == BrushMode::Sculpt ? m_LastSculptTool : m_LastPaintTool;
		if (configureBrush())
			m_ActiveBrushMode = mode;
	};
	const auto drawBrushControls = [&]()
	{
		using Pattern = Vans::EditorAPI::TerrainBrushPattern;
		constexpr Pattern patterns[] = {
			Pattern::SmoothCircle,
			Pattern::LinearCircle,
			Pattern::Sphere,
			Pattern::Tip,
			Pattern::SoftSquare,
			Pattern::Ridge,
			Pattern::Crater,
			Pattern::Rocky
		};
		ImGui::SeparatorText("Brush");
		if (ImGui::BeginCombo("Pattern", BrushPatternName(brush.pattern)))
		{
			for (const Pattern pattern : patterns)
			{
				const bool selected = brush.pattern == pattern;
				if (ImGui::Selectable(BrushPatternName(pattern), selected))
				{
					brush.pattern = pattern;
					configureBrush();
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::DragFloat("Radius", &brush.radius, 0.25f, 0.25f,
			std::max(1.0f, currentSettings.terrainSize * 0.25f), "%.2f m")) configureBrush();
		if (ImGui::SliderFloat("Strength", &brush.strength, 0.001f, 1.0f, "%.3f")) configureBrush();
		if (ImGui::SliderFloat("Hardness", &brush.hardness, 0.0f, 1.0f, "%.2f")) configureBrush();
		if (ImGui::SliderAngle("Rotation", &brush.rotationRadians, -180.0f, 180.0f, "%.0f deg"))
			configureBrush();
	};
	const auto applySettings = [&]()
	{
		const auto result = editorAPI.ApplyTerrainSettings(m_SettingsDraft);
		m_StatusMessage = result.success ? std::string{} : result.message;
	};
	const auto operation = [&](const Vans::EditorAPI::TerrainEditorOperationResult& result)
	{
		m_StatusMessage = result.success ? std::string{} : result.message;
		m_HasSettingsDraft = false;
	};

	ImGui::BeginDisabled(!terrain.editable);
	if (ImGui::Checkbox("Edit Terrain", &brush.enabled)) configureBrush();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("%ux%u  %s", terrain.width, terrain.height,
		terrain.dirty ? "Modified" : "Saved");

	ImGui::BeginDisabled(!terrain.editable || !terrain.canUndo);
	if (ImGui::Button("Undo")) operation(editorAPI.UndoTerrainEdit());
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!terrain.editable || !terrain.canRedo);
	if (ImGui::Button("Redo")) operation(editorAPI.RedoTerrainEdit());
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!terrain.editable || !terrain.dirty);
	if (ImGui::Button("Revert")) operation(editorAPI.RevertTerrainEdits());
	ImGui::SameLine();
	if (ImGui::Button("Save")) operation(editorAPI.SaveTerrainAsset());
	ImGui::EndDisabled();
	if (!terrain.editable)
		ImGui::TextDisabled("Terrain authoring is available in Edit mode.");
	if (!m_StatusMessage.empty())
		ImGui::TextWrapped("%s", m_StatusMessage.c_str());
	ImGui::TextDisabled("Ctrl+Z / Ctrl+Y undo or redo one completed terrain stroke.");
	ImGui::Separator();

    if (ImGui::BeginTabBar("TerrainTabs"))
    {
		if (ImGui::BeginTabItem("Sculpt"))
		{
			activateBrushMode(BrushMode::Sculpt);
			ImGui::BeginDisabled(!terrain.editable);
			const std::pair<const char*, Vans::EditorAPI::TerrainBrushTool> tools[] = {
				{ "Raise", Vans::EditorAPI::TerrainBrushTool::Raise },
				{ "Lower", Vans::EditorAPI::TerrainBrushTool::Lower },
				{ "Smooth", Vans::EditorAPI::TerrainBrushTool::SmoothHeight },
				{ "Flatten", Vans::EditorAPI::TerrainBrushTool::Flatten },
				{ "Noise", Vans::EditorAPI::TerrainBrushTool::Noise }
			};
			for (std::size_t index = 0; index < std::size(tools); ++index)
			{
				if (index != 0) ImGui::SameLine();
				if (ImGui::RadioButton(tools[index].first, brush.tool == tools[index].second))
				{
					brush.tool = tools[index].second;
					if (configureBrush()) m_LastSculptTool = brush.tool;
				}
			}
			drawBrushControls();
			if (brush.tool == Vans::EditorAPI::TerrainBrushTool::Flatten &&
				ImGui::SliderFloat("Target Height", &brush.flattenHeight, 0.0f, 1.0f, "%.3f"))
				configureBrush();
			ImGui::TextDisabled("LMB sculpts. Hold Shift to invert Raise/Lower.");
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Paint"))
		{
			activateBrushMode(BrushMode::Paint);
			ImGui::BeginDisabled(!terrain.editable);
			const std::pair<const char*, Vans::EditorAPI::TerrainBrushTool> tools[] = {
				{ "Paint", Vans::EditorAPI::TerrainBrushTool::PaintLayer },
				{ "Erase", Vans::EditorAPI::TerrainBrushTool::EraseLayer },
				{ "Smooth", Vans::EditorAPI::TerrainBrushTool::SmoothWeights }
			};
			for (std::size_t index = 0; index < std::size(tools); ++index)
			{
				if (index != 0) ImGui::SameLine();
				if (ImGui::RadioButton(tools[index].first, brush.tool == tools[index].second))
				{
					brush.tool = tools[index].second;
					if (configureBrush()) m_LastPaintTool = brush.tool;
				}
			}
			ImGui::SeparatorText("Layer");
			for (const auto& layer : terrain.layers)
			{
				const std::string label = std::to_string(layer.index + 1u) + ". " + layer.name +
					"##terrainLayer" + std::to_string(layer.index);
				if (ImGui::Selectable(label.c_str(), brush.selectedLayer == layer.index))
				{
					brush.selectedLayer = layer.index;
					configureBrush();
				}
			}
			drawBrushControls();
			ImGui::TextDisabled("Eight normalized channels are stored across Splat0 RGBA and Splat1 RGBA.");
			ImGui::TextDisabled("Hold Shift to invert Paint/Erase.");
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Layers"))
		{
			for (const auto& layer : terrain.layers)
			{
				const char* mapName = layer.index < 4u ? "Splat0" : "Splat1";
				const char channel = "RGBA"[layer.index % 4u];
				ImGui::Text("%u  %s", layer.index + 1u, layer.name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s.%c  %s", mapName, channel, layer.id.c_str());
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("LOD & Detail"))
        {
			ImGui::BeginDisabled(!terrain.editable);
            if (ImGui::Checkbox("Enable Tessellation", &m_SettingsDraft.tessellationEnabled))
				applySettings();
            ImGui::Separator();

            if (m_SettingsDraft.tessellationEnabled)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Tessellation ACTIVE");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Tessellation OFF (using VS path)");
            ImGui::Separator();

            ImGui::DragFloat("Tess Distance (m)", &m_SettingsDraft.tessellationDistance, 1.0f, 1.0f, 2000.0f, "%.0f");
			if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Patches within this distance use GPU tessellation. Beyond: VS-only path.");

            ImGui::SliderFloat("Max Tess Level", &m_SettingsDraft.maxTessellationLevel, 1.0f, 64.0f, "%.0f");
			if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum triangle subdivision level. Higher = more detail but more GPU cost.");

            ImGui::DragFloat("Target Edge Pixels", &m_SettingsDraft.tessellationTargetPixels, 0.5f, 2.0f, 64.0f, "%.1f");
			if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Target screen-space length per tessellated edge. Lower values add detail and GPU cost.");

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Procedural Noise Detail");

            if (ImGui::Checkbox("Enable Noise Detail", &m_SettingsDraft.noiseDetailEnabled))
				applySettings();

            if (m_SettingsDraft.noiseDetailEnabled)
            {
                ImGui::DragFloat("Strength (m)", &m_SettingsDraft.noiseStrength, 0.001f, 0.0f, 0.5f, "%.3f");
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Noise displacement in world meters. 0.03 = 3cm micro-detail.");

                ImGui::DragFloat("Frequency", &m_SettingsDraft.noiseFrequency, 0.01f, 0.01f, 10.0f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base noise frequency. Higher = finer detail pattern.");

                ImGui::SliderInt("Octaves", &m_SettingsDraft.noiseOctaves, 1, 4);
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Number of noise layers. More = richer detail but more GPU cost.");

                ImGui::DragFloat("Gain", &m_SettingsDraft.noiseGain, 0.01f, 0.01f, 1.0f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Amplitude falloff per octave. 0.52 = original ShaderToy hill().");

                ImGui::DragFloat("Lacunarity", &m_SettingsDraft.noiseLacunarity, 0.1f, 1.0f, 4.0f, "%.1f");
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frequency multiplier per octave. 2.0 = standard.");

                ImGui::DragFloat("Warp Strength", &m_SettingsDraft.noiseWarpStrength, 0.01f, 0.0f, 1.0f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Domain warping. 0=off, 0.2~0.3 = visible ridge-like distortion (HIGH/ULTRA quality).");

                ImGui::SliderFloat("Fade Start", &m_SettingsDraft.noiseFadeStart, 0.0f, 1.0f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance ratio where noise begins fading out. 0.7 = fade starts at 70%% of tess distance.");
            }

            ImGui::Separator();
            ImGui::Text("Terrain Size: %.0f m", m_SettingsDraft.terrainSize);
            ImGui::DragFloat("Finest LOD Distance", &m_SettingsDraft.lodBaseDistance, 1.0f, 1.0f, 4096.0f, "%.0f m");
			if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outer distance of the finest regular-grid LOD.");

            ImGui::DragFloat("LOD Range Ratio", &m_SettingsDraft.lodRangeRatio, 0.1f, 2.0f, 8.0f, "%.1f");
			if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance growth between adjacent CDLOD levels. Values below 2 cannot maintain a balanced quadtree.");

            ImGui::SliderFloat("Morph Start Ratio", &m_SettingsDraft.morphStartRatio, 0.05f, 0.95f, "%.2f");
			if (ImGui::IsItemDeactivatedAfterEdit()) applySettings();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fraction of each LOD range where geometry starts morphing to the next coarser surface.");
			ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace VansGraphics
