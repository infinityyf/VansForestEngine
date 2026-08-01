#include "VansShadowDebuggerWindow.h"

#include "../VansEditorWindow.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace
{
	using namespace Vans::EditorAPI;

	const char* LightKindName(PunctualShadowLightKind kind)
	{
		switch (kind)
		{
		case PunctualShadowLightKind::Point: return "Point";
		case PunctualShadowLightKind::Spot: return "Spot";
		case PunctualShadowLightKind::Rect: return "Rect";
		default: return "Unknown";
		}
	}

	const char* DisplayModeName(PunctualShadowDisplayMode mode)
	{
		switch (mode)
		{
		case PunctualShadowDisplayMode::Disabled: return "Disabled";
		case PunctualShadowDisplayMode::HeroAtlas: return "Hero Atlas";
		case PunctualShadowDisplayMode::CachedAtlas: return "Cached Atlas";
		case PunctualShadowDisplayMode::AtlasTransition: return "Atlas Transition";
		case PunctualShadowDisplayMode::ScreenSpaceFallback: return "Screen-space Fallback";
		case PunctualShadowDisplayMode::Unshadowed: return "Unshadowed";
		default: return "Unknown";
		}
	}

	ImU32 DisplayModeColor(PunctualShadowDisplayMode mode)
	{
		switch (mode)
		{
		case PunctualShadowDisplayMode::HeroAtlas: return IM_COL32(255, 205, 64, 255);
		case PunctualShadowDisplayMode::CachedAtlas: return IM_COL32(75, 210, 125, 255);
		case PunctualShadowDisplayMode::AtlasTransition: return IM_COL32(75, 185, 255, 255);
		case PunctualShadowDisplayMode::ScreenSpaceFallback: return IM_COL32(220, 130, 255, 255);
		case PunctualShadowDisplayMode::Unshadowed: return IM_COL32(255, 130, 80, 255);
		default: return IM_COL32(135, 135, 135, 255);
		}
	}

	void DrawPreview(const RenderTexturePreview& preview, float maximumHeight = 560.0f)
	{
		if (!preview.texture || preview.width == 0 || preview.height == 0)
		{
			ImGui::TextDisabled("Preview is not available yet.");
			return;
		}

		const float aspect = static_cast<float>(preview.width) / static_cast<float>(preview.height);
		float width = ImGui::GetContentRegionAvail().x;
		float height = width / std::max(aspect, 0.001f);
		if (height > maximumHeight)
		{
			height = maximumHeight;
			width = height * aspect;
		}
		ImGui::Image(preview.texture, ImVec2(width, height));
	}

	void DrawAtlas(
		const PunctualShadowDebugSnapshot& snapshot,
		int selectedLight,
		bool showLabels)
	{
		const RenderTexturePreview& preview = snapshot.atlasPreview;
		if (!preview.texture || snapshot.atlasSize == 0)
		{
			ImGui::TextDisabled("Punctual shadow Atlas is not available yet.");
			return;
		}

		const uint32_t atlasCount = std::max(snapshot.atlasCount, 1u);
		const float width = std::min(ImGui::GetContentRegionAvail().x, 900.0f);
		const float atlasDisplaySize = width / static_cast<float>(atlasCount);
		ImGui::Image(preview.texture, ImVec2(width, atlasDisplaySize));
		const ImVec2 imageMin = ImGui::GetItemRectMin();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float scale = atlasDisplaySize / static_cast<float>(snapshot.atlasSize);

		for (std::size_t lightIndex = 0; lightIndex < snapshot.lights.size(); ++lightIndex)
		{
			const auto& light = snapshot.lights[lightIndex];
			const ImU32 color = DisplayModeColor(light.displayMode);
			for (const auto& view : light.atlasViews)
			{
				const ImVec2 rectMin(
					imageMin.x + static_cast<float>(view.atlasIndex) * atlasDisplaySize + static_cast<float>(view.x) * scale,
					imageMin.y + static_cast<float>(view.y) * scale);
				const ImVec2 rectMax(
					rectMin.x + static_cast<float>(view.resolution) * scale,
					rectMin.y + static_cast<float>(view.resolution) * scale);
				const float thickness = static_cast<int>(lightIndex) == selectedLight ? 3.0f : 1.5f;
				drawList->AddRect(rectMin, rectMax, color, 0.0f, 0, thickness);

				if (showLabels && (rectMax.x - rectMin.x) >= 28.0f)
				{
					char label[48] = {};
					std::snprintf(label, sizeof(label), "%c%u/F%u",
						light.lightKind == PunctualShadowLightKind::Point ? 'P' :
						(light.lightKind == PunctualShadowLightKind::Spot ? 'S' : 'R'),
						light.gpuLightIndex,
						view.faceIndex);
					drawList->AddRectFilled(
						rectMin,
						ImVec2(std::min(rectMax.x, rectMin.x + ImGui::CalcTextSize(label).x + 6.0f), rectMin.y + 17.0f),
						IM_COL32(0, 0, 0, 180));
					drawList->AddText(ImVec2(rectMin.x + 3.0f, rectMin.y + 1.0f), color, label);
				}
			}
		}
	}

	void DrawSelectedFaces(
		const PunctualShadowDebugSnapshot& snapshot,
		int selectedLight)
	{
		if (selectedLight < 0 || selectedLight >= static_cast<int>(snapshot.lights.size()))
		{
			ImGui::TextDisabled("Select an Atlas-resident light from the table to inspect its faces.");
			return;
		}

		const auto& light = snapshot.lights[static_cast<std::size_t>(selectedLight)];
		if (!snapshot.atlasPreview.texture || snapshot.atlasSize == 0 || light.atlasViews.empty())
		{
			ImGui::TextDisabled("The selected light has no valid Atlas views.");
			return;
		}

		const int columns = light.lightKind == PunctualShadowLightKind::Point ? 3 : 1;
		if (!ImGui::BeginTable("ShadowFacePreviews", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
			return;

		for (const auto& view : light.atlasViews)
		{
			ImGui::TableNextColumn();
			ImGui::Text("Atlas %u | Face %u | %ux%u | (%u, %u)",
				view.atlasIndex, view.faceIndex, view.resolution, view.resolution, view.x, view.y);
			const float atlasSize = static_cast<float>(snapshot.atlasSize);
			const float atlasCount = static_cast<float>(std::max(snapshot.atlasCount, 1u));
			const ImVec2 uv0(
				(static_cast<float>(view.atlasIndex) + static_cast<float>(view.x) / atlasSize) / atlasCount,
				static_cast<float>(view.y) / atlasSize);
			const ImVec2 uv1(
				(static_cast<float>(view.atlasIndex) + static_cast<float>(view.x + view.resolution) / atlasSize) / atlasCount,
				static_cast<float>(view.y + view.resolution) / atlasSize);
			const float width = ImGui::GetContentRegionAvail().x;
			ImGui::Image(snapshot.atlasPreview.texture, ImVec2(width, width), uv0, uv1);
		}
		ImGui::EndTable();
	}
}

void VansGraphics::VansShadowDebuggerWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_ShadowDebuggerWindowOpen)
		return;

	if (!ImGui::Begin("Shadow Debugger", &VansEditorWindow::m_ShadowDebuggerWindowOpen))
	{
		ImGui::End();
		return;
	}

	const double now = ImGui::GetTime();
	const bool refreshSnapshot =
		!m_HasCachedSnapshot ||
		m_LastSnapshotTime < 0.0 ||
		(now - m_LastSnapshotTime) >= 0.10;
	if (refreshSnapshot)
	{
		if (m_RequestPreviewNextFrame)
			editorAPI.RequestPunctualShadowDebugPreview();
		m_CachedSnapshot = editorAPI.GetPunctualShadowDebugSnapshot();
		m_HasCachedSnapshot = true;
		m_LastSnapshotTime = now;
	}
	m_RequestPreviewNextFrame = false;
	const Vans::EditorAPI::PunctualShadowDebugSnapshot& snapshot = m_CachedSnapshot;
	if (!snapshot.available)
	{
		m_DraftInitialized = false;
		ImGui::TextDisabled("Load a scene to inspect punctual shadows.");
		ImGui::End();
		return;
	}

	if (!m_DraftInitialized)
	{
		m_DraftSettings = snapshot.screenSpaceSettings;
		m_DraftInitialized = true;
	}

	const float pageUsage = snapshot.totalPages > 0
		? static_cast<float>(snapshot.usedPages) / static_cast<float>(snapshot.totalPages)
		: 0.0f;
	ImGui::Text("Atlas %ux%u x %u | Pages %u/%u | Resident %u lights / %u views",
		snapshot.atlasSize, snapshot.atlasSize, snapshot.atlasCount, snapshot.usedPages, snapshot.totalPages,
		snapshot.residentLights, snapshot.residentViews);
	ImGui::ProgressBar(pageUsage, ImVec2(-1.0f, 0.0f));
	ImGui::Text("This frame: %u rendered views, %llu dirty texels | Fallback lights: %u | Allocation failures: %u",
		snapshot.renderedViewsThisFrame,
		static_cast<unsigned long long>(snapshot.dirtyTexelsThisFrame),
		snapshot.fallbackLights,
		snapshot.allocationFailures);

	if (ImGui::BeginTabBar("ShadowDebuggerTabs"))
	{
		if (ImGui::BeginTabItem("Atlas"))
		{
			m_RequestPreviewNextFrame = true;
			ImGui::Checkbox("Overlay light/face labels", &m_ShowAtlasLabels);
			DrawAtlas(snapshot, m_SelectedLight, m_ShowAtlasLabels);
			ImGui::Separator();
			DrawSelectedFaces(snapshot, m_SelectedLight);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Lights"))
		{
			if (ImGui::BeginTable("PunctualShadowLights", 11,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
				ImVec2(0.0f, 420.0f)))
			{
				ImGui::TableSetupColumn("Light");
				ImGui::TableSetupColumn("Shadow Type");
				ImGui::TableSetupColumn("Runtime");
				ImGui::TableSetupColumn("Policy/Priority");
				ImGui::TableSetupColumn("Resolution");
				ImGui::TableSetupColumn("Weight");
				ImGui::TableSetupColumn("Importance");
				ImGui::TableSetupColumn("Camera Dist");
				ImGui::TableSetupColumn("Distance Weight");
				ImGui::TableSetupColumn("Faces");
				ImGui::TableSetupColumn("Consumers");
				ImGui::TableHeadersRow();

				for (std::size_t index = 0; index < snapshot.lights.size(); ++index)
				{
					const auto& light = snapshot.lights[index];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					char lightLabel[64] = {};
					std::snprintf(lightLabel, sizeof(lightLabel), "%s #%u (id %u)",
						LightKindName(light.lightKind), light.gpuLightIndex, light.stableLightId);
					if (ImGui::Selectable(lightLabel, m_SelectedLight == static_cast<int>(index), ImGuiSelectableFlags_SpanAllColumns))
						m_SelectedLight = static_cast<int>(index);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(DisplayModeColor(light.displayMode)), "%s", DisplayModeName(light.displayMode));
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(light.runtimeState.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%s / %u", light.policy.c_str(), light.priority);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%u -> %u", light.activeResolution, light.targetResolution);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%.2f", light.atlasWeight);
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%.2f", light.importance);
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%.2f", light.cameraDistance);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%.3f", light.distancePriority);
					ImGui::TableSetColumnIndex(9);
					ImGui::Text("V:%02X D:%02X", light.validFaceMask, light.dirtyFaceMask);
					ImGui::TableSetColumnIndex(10);
					ImGui::Text("Fog:%s GI:%s", light.affectsFog ? "Y" : "N", light.affectsGI ? "Y" : "N");
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Screen-space Fallback"))
		{
			m_RequestPreviewNextFrame = true;
			ImGui::TextWrapped(
				"Punctual fallback has no explicit camera-distance cutoff. The maximum distance below is the world-space ray length from a receiver toward its light. Camera distance still changes projected sampling density and screen visibility; the shader now preserves a world-space step floor to reduce distance-dependent disappearance.");
			ImGui::Separator();
			ImGui::DragFloat("Max ray distance (world units)", &m_DraftSettings.maxTraceDistance, 0.25f, 0.25f, 50.0f, "%.2f");
			ImGui::DragFloat("Depth thickness", &m_DraftSettings.thickness, 0.005f, 0.005f, 1.0f, "%.3f");
			ImGui::DragFloat("Normal bias", &m_DraftSettings.normalBias, 0.001f, 0.001f, 0.25f, "%.3f");
			int maxSteps = static_cast<int>(m_DraftSettings.maxSteps);
			if (ImGui::DragInt("Maximum HZB iterations", &maxSteps, 1.0f, 8, 128))
				m_DraftSettings.maxSteps = static_cast<std::uint32_t>(std::clamp(maxSteps, 8, 128));
			ImGui::SliderFloat("Strength", &m_DraftSettings.strength, 0.0f, 1.0f, "%.2f");

			if (ImGui::Button("Apply"))
			{
				editorAPI.ApplyPunctualScreenSpaceShadowSettings(m_DraftSettings);
				m_LastSnapshotTime = -1.0;
			}
			ImGui::SameLine();
			if (ImGui::Button("Long-range preset (20 / 96)"))
			{
				m_DraftSettings.maxTraceDistance = 20.0f;
				m_DraftSettings.thickness = 0.12f;
				m_DraftSettings.normalBias = 0.020f;
				m_DraftSettings.maxSteps = 96;
				m_DraftSettings.strength = 1.0f;
				editorAPI.ApplyPunctualScreenSpaceShadowSettings(m_DraftSettings);
				m_LastSnapshotTime = -1.0;
			}
			ImGui::SameLine();
			if (ImGui::Button("Use runtime values"))
				m_DraftSettings = snapshot.screenSpaceSettings;

			ImGui::Separator();
			ImGui::Text("Runtime: %.2f units, %.3f thickness, %.3f bias, %u steps, %.2f strength",
				snapshot.screenSpaceSettings.maxTraceDistance,
				snapshot.screenSpaceSettings.thickness,
				snapshot.screenSpaceSettings.normalBias,
				snapshot.screenSpaceSettings.maxSteps,
				snapshot.screenSpaceSettings.strength);
			DrawPreview(snapshot.screenSpacePreview);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}
