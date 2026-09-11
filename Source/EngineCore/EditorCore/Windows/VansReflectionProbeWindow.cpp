#include "VansReflectionProbeWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace VansGraphics
{
namespace
{
	const char* ProbeTypeName(int type)
	{
		switch (type)
		{
		case 1: return "Realtime";
		case 2: return "Sky";
		case 0:
		default: return "Baked";
		}
	}

	bool EditVec3(const char* label, Vans::EditorAPI::Vec3& value, float speed)
	{
		float values[3] = { value.x, value.y, value.z };
		if (!ImGui::DragFloat3(label, values, speed))
			return false;

		value = { values[0], values[1], values[2] };
		return true;
	}
}

void VansReflectionProbeWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_ReflectionProbeWindowOpen)
		return;

	if (!ImGui::Begin("Reflection Probe Inspector", &VansEditorWindow::m_ReflectionProbeWindowOpen))
	{
		ImGui::End();
		return;
	}

	Vans::EditorAPI::ReflectionProbeSettingsSnapshot settings = editorAPI.GetReflectionProbeSettings();
	if (!settings.available)
	{
		ImGui::TextDisabled("Reflection probe system is not available.");
		ImGui::End();
		return;
	}

	bool changed = false;

	if (ImGui::CollapsingHeader("Scene Gizmos", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= ImGui::Checkbox("Show Probe Gizmos", &settings.editor.showProbeGizmos);
		changed |= ImGui::Checkbox("Show Influence Volumes", &settings.editor.showInfluenceVolumes);
		changed |= ImGui::Checkbox("Show Blend Volumes", &settings.editor.showBlendVolumes);
		changed |= ImGui::Checkbox("Show Placement Grid", &settings.editor.showPlacementGrid);
		changed |= ImGui::Checkbox("Show Regions", &settings.editor.showRegions);

	}

	if (ImGui::CollapsingHeader("Viewport Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const char* debugViews[] = {
			"None", "Influence", "Probe Color", "SSR Confidence",
			"Region Id", "Parallax", "Fallback Only", "SSR Only",
			"Local Probe Radiance (No Material)", "Probe + Sky Radiance (No Material)",
			"Sky Fallback Contribution (No Material)", "Local Probe Coverage",
			"Indirect Specular (Material + SSR)", "Indirect Diffuse (Material + GI)", "Direct Lighting (Material)"
		};
		int debugIndex = std::clamp(settings.editor.debugView, 0, int(IM_ARRAYSIZE(debugViews)) - 1);
		if (ImGui::Combo("Debug View", &debugIndex, debugViews, IM_ARRAYSIZE(debugViews)))
		{
			settings.editor.debugView = debugIndex;
			changed = true;
		}
		if (settings.editor.debugView >= 8)
		{
			if (settings.editor.debugView <= 10) changed |= ImGui::SliderFloat("Diagnostic Roughness", &settings.editor.debugRoughness, 0.0f, 1.0f, "%.2f");
			if (settings.editor.debugView != 11)
				changed |= ImGui::SliderFloat("Diagnostic Exposure (EV)", &settings.editor.debugExposureEV, -10.0f, 10.0f, "%.1f");
			if (settings.editor.debugView <= 11) ImGui::TextWrapped("Uses geometric normals and fixed roughness. No surface textures, BRDF, AO, SSR, fog or bloom. Captured scene lighting remains in the cubemap.");
			if (settings.editor.debugView >= 12)
				ImGui::TextWrapped("Actual opaque lighting contribution, including material properties. Compare all three at the same EV. No auto exposure, fog or bloom.");
			if (settings.editor.debugView == 8)
				ImGui::TextWrapped("Normalized local probe radiance before coverage fade. Black: no valid local probe.");
			else if (settings.editor.debugView == 11)
				ImGui::TextWrapped("White: full local coverage. Black: full sky fallback. Gray: blended coverage.");
			ImGui::Text("Pending captures: %u", settings.pendingCaptureCount);
		}
	}

	static std::string placementScene;
	static Vans::EditorAPI::ReflectionProbePlacementSettingsSnapshot placementDraft;
	static bool placementDirty = false;
	if (placementScene != settings.scenePath || !placementDirty)
	{
		placementScene = settings.scenePath; placementDraft = settings.placement; placementDirty = false;
	}
	if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
	{
		placementDirty |= ImGui::Checkbox("Automatic Reflection Placement", &placementDraft.enabled);
		placementDirty |= EditVec3("Volume Min", placementDraft.volumeMin, 0.25f);
		placementDirty |= EditVec3("Volume Max", placementDraft.volumeMax, 0.25f);
		placementDirty |= ImGui::DragFloat("Surface Analysis Cell (m)", &placementDraft.cellSize, 0.1f, 0.05f, 100.0f, "%.2f");
		placementDirty |= ImGui::DragFloat("Surface Clearance (m)", &placementDraft.minCaptureClearance, 0.01f, 0.001f, 10.0f, "%.3f");
		placementDirty |= ImGui::DragFloat("Indoor Spacing (m)", &placementDraft.indoorSpacing, 0.1f, 0.05f, 1000.0f, "%.2f");
		placementDirty |= ImGui::DragFloat("Corridor Spacing (m)", &placementDraft.corridorSpacing, 0.1f, 0.05f, 1000.0f, "%.2f");
		placementDirty |= ImGui::DragFloat("Outdoor Spacing (m)", &placementDraft.outdoorSpacing, 0.1f, 0.05f, 1000.0f, "%.2f");
		float targetCoverage = 100.0f * (1.0f - placementDraft.refinementThreshold);
		if (ImGui::SliderFloat("Coverage Target (%)", &targetCoverage, 90.0f, 100.0f, "%.1f"))
		{ placementDraft.refinementThreshold = 1.0f - targetCoverage * 0.01f; placementDirty = true; }
		int resolution = static_cast<int>(placementDraft.uniformProbeResolution);
		if (ImGui::InputInt("Capture Resolution", &resolution))
		{ placementDraft.uniformProbeResolution = static_cast<std::uint32_t>(std::clamp(resolution, 32, 512)); placementDirty = true; }
		int count = static_cast<int>(placementDraft.maxProbeCount);
		if (ImGui::InputInt("Probe Budget", &count))
		{ placementDraft.maxProbeCount = static_cast<std::uint32_t>(std::clamp(count, 1, 4096)); placementDirty = true; }
		ImGui::BeginDisabled(!placementDirty);
		if (ImGui::Button("Apply Placement"))
		{
			settings.placement = placementDraft;
			const bool applied = editorAPI.ApplyReflectionProbeSettings(settings); changed = false;
			settings = editorAPI.GetReflectionProbeSettings();
			if (applied) { placementDraft = settings.placement; placementDirty = false; }
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(placementDirty || !settings.placement.enabled);
		if (ImGui::Button("Regenerate"))
		{
			editorAPI.GenerateAutoReflectionProbes();
			settings = editorAPI.GetReflectionProbeSettings();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!settings.placement.enabled);
		if (ImGui::Button("Restore Authored Layout"))
		{
			editorAPI.ClearAutoReflectionProbes(); settings = editorAPI.GetReflectionProbeSettings();
			placementDraft = settings.placement; placementDirty = false;
		}
		ImGui::EndDisabled();
		if (settings.placement.enabled)
		{
			ImGui::Text("Covered receiver samples: %u / %u", settings.coveredReceiverCount, settings.receiverCount);
			ImGui::Text("Visible surface area: %.1f%%", settings.coveredSurfaceFraction * 100.0f);
			ImGui::Text("Area reachable by candidates: %.1f%%", settings.reachableSurfaceFraction * 100.0f);
			ImGui::TextWrapped("Coverage requires a visible capture inside its full influence box. Hidden and inaccessible geometry remains in the total area. Influence colors alone do not prove visibility.");
		}
		ImGui::TextDisabled("Apply updates the preview. Store Runtime Config in Scene stages the applied settings; Save Scene writes the file.");
	}

	if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int maxBlend = static_cast<int>(settings.lighting.maxBlendCount);
		if (ImGui::SliderInt("Max Blend Count", &maxBlend, 1, 4))
		{
			settings.lighting.maxBlendCount = static_cast<std::uint32_t>(maxBlend);
			changed = true;
		}
		changed |= ImGui::DragFloat("SSR Roughness Fade Start", &settings.lighting.ssrRoughnessFadeStart, 0.01f, 0.0f, 1.0f, "%.2f");
		changed |= ImGui::DragFloat("SSR Roughness Fade End", &settings.lighting.ssrRoughnessFadeEnd, 0.01f, 0.0f, 1.0f, "%.2f");
	}

	if (ImGui::CollapsingHeader("Bake", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Texture Pages: %u", settings.texturePageCount);
		ImGui::Text("Texture Data: %.2f MiB", double(settings.residentTextureBytes) / (1024.0 * 1024.0));
		if (ImGui::Button("Request Bake All"))
			editorAPI.RequestReflectionProbeBakeAll();
		ImGui::SameLine();
		if (ImGui::Button("Bake Queue Now"))
			editorAPI.BakeQueuedReflectionProbesNow();
		ImGui::SameLine();
		if (ImGui::Button("Store Runtime Config in Scene"))
			editorAPI.SaveReflectionProbeConfiguration();
	}

	if (ImGui::CollapsingHeader("Probes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Probe Count: %d", static_cast<int>(settings.probes.size()));
		if (ImGui::BeginTable("ReflectionProbeTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 45.0f);
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 65.0f);
			ImGui::TableHeadersRow();
			for (int i = 0; i < static_cast<int>(settings.probes.size()); ++i)
			{
				auto& probe = settings.probes[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%d", i);
				ImGui::TableNextColumn();
				const bool selected = settings.editor.selectedProbeIndex == i;
				if (ImGui::Selectable(probe.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					settings.editor.selectedProbeIndex = i;
					changed = true;
				}
				ImGui::TableNextColumn();
				ImGui::Text("%s", ProbeTypeName(probe.type));
				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", probe.bakeStatus.empty() ? "No bake result" : probe.bakeStatus.c_str());
				ImGui::TableNextColumn();
				if (probe.type == 2)
				{
					ImGui::TextDisabled("-");
				}
				else
				{
					ImGui::PushID(i);
					ImGui::BeginDisabled(!probe.editable);
					changed |= ImGui::Checkbox("##enabled", &probe.enabled);
					ImGui::EndDisabled();
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}

		if (settings.editor.selectedProbeIndex >= 0
			&& settings.editor.selectedProbeIndex < static_cast<int>(settings.probes.size()))
		{
			ImGui::Separator();
			const int selectedIndex = settings.editor.selectedProbeIndex;
			auto& probe = settings.probes[selectedIndex];
			ImGui::Text("Selected: %s", probe.name.c_str());

			if (!probe.editable) ImGui::TextDisabled("Convert to manual to edit this generated probe.");
			ImGui::BeginDisabled(!probe.editable);
			changed |= EditVec3("Position", probe.position, 0.1f);
			changed |= EditVec3("Capture Position", probe.capturePosition, 0.1f);
			if (probe.shape == 1)
			{
				changed |= EditVec3("Box Min", probe.boxMin, 0.1f);
				changed |= EditVec3("Box Max", probe.boxMax, 0.1f);
				changed |= ImGui::Checkbox("Box Projection", &probe.boxProjection);
			}
			else
			{
				changed |= ImGui::DragFloat("Radius", &probe.radius, 0.1f, 0.01f, 10000.0f, "%.2f");
			}
			changed |= ImGui::DragFloat("Blend Distance", &probe.blendDistance, 0.05f, 0.001f, 1000.0f, "%.2f");
			changed |= ImGui::DragFloat("Intensity", &probe.intensity, 0.05f, 0.0f, 100.0f, "%.2f");
			changed |= ImGui::DragFloat("Specular Intensity", &probe.specularIntensity, 0.05f, 0.0f, 100.0f, "%.2f");
			changed |= ImGui::DragFloat("Priority", &probe.priority, 0.05f, -1000.0f, 1000.0f, "%.2f");
			ImGui::EndDisabled();

			if (probe.type != 2)
			{
				if (ImGui::Button("Request Bake Selected"))
				{
					if (changed)
					{
						editorAPI.ApplyReflectionProbeSettings(settings);
						changed = false;
					}
					editorAPI.RequestReflectionProbeBake(static_cast<std::uint32_t>(selectedIndex));
				}
				ImGui::SameLine();
				if (probe.autoGenerated && ImGui::Button("Convert To Manual"))
				{
					if (changed)
					{
						editorAPI.ApplyReflectionProbeSettings(settings);
						changed = false;
					}
					editorAPI.ConvertReflectionProbeToManual(static_cast<std::uint32_t>(selectedIndex));
					settings = editorAPI.GetReflectionProbeSettings();
				}
			}

			if (probe.type != 2 && ImGui::CollapsingHeader("Cubemap Preview", ImGuiTreeNodeFlags_DefaultOpen))
			{
				changed |= ImGui::Checkbox("Preview Cubemap", &settings.editor.previewCubemap);
				const char* faces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
				settings.editor.previewFace = std::clamp(settings.editor.previewFace, 0, 5);
				if (ImGui::Combo("Face", &settings.editor.previewFace, faces, IM_ARRAYSIZE(faces)))
					changed = true;
				changed |= ImGui::SliderFloat("Roughness", &settings.editor.previewRoughness, 0.0f, 1.0f, "%.2f");

				if (settings.editor.previewCubemap)
				{
					const std::uint32_t mipCount = std::max(1u, probe.mipCount);
					const std::uint32_t mip = static_cast<std::uint32_t>(
						std::round(std::clamp(settings.editor.previewRoughness, 0.0f, 1.0f) * float(mipCount - 1)));
					Vans::EditorAPI::RenderTextureFilter filter;
					filter.category = "reflection_probe";
					filter.probeIndex = static_cast<std::uint32_t>(selectedIndex);
					filter.face = static_cast<std::uint32_t>(settings.editor.previewFace);
					filter.roughness = settings.editor.previewRoughness;
					std::vector<Vans::EditorAPI::RenderTexturePreview> previews =
						editorAPI.QueryRenderTexturePreviews(filter);
					const Vans::EditorAPI::RenderTexturePreview preview =
						previews.empty() ? Vans::EditorAPI::RenderTexturePreview{} : previews.front();
					if (preview.texture)
					{
						const float maxPreviewSize = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
						const float previewSize = std::max(96.0f, std::min(maxPreviewSize, static_cast<float>(preview.width)));
						ImGui::Text("Mip %u / %u", mip, mipCount - 1);
						ImGui::Image(preview.texture, ImVec2(previewSize, previewSize));
					}
					else
					{
						ImGui::TextDisabled("Preview texture is not available.");
					}
				}
			}
		}
	}

	if (!settings.validationErrors.empty() && ImGui::CollapsingHeader("Validation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const std::string& error : settings.validationErrors)
			ImGui::TextWrapped("%s", error.c_str());
	}

	if (changed)
		editorAPI.ApplyReflectionProbeSettings(settings);

	ImGui::End();
}

} // namespace VansGraphics
