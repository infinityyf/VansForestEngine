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

		const char* debugViews[] = {
			"None", "Influence", "Probe Color", "SSR Confidence",
			"Region Id", "Parallax", "Fallback Only", "SSR Only"
		};
		int debugIndex = std::clamp(settings.editor.debugView, 0, 7);
		if (ImGui::Combo("Debug View", &debugIndex, debugViews, IM_ARRAYSIZE(debugViews)))
		{
			settings.editor.debugView = debugIndex;
			changed = true;
		}
	}

	if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= ImGui::Checkbox("Auto Placement Enabled", &settings.placement.enabled);
		changed |= EditVec3("Volume Min", settings.placement.volumeMin, 0.25f);
		changed |= EditVec3("Volume Max", settings.placement.volumeMax, 0.25f);
		changed |= ImGui::DragFloat("Uniform Spacing", &settings.placement.uniformSpacing, 0.1f, 0.5f, 100.0f, "%.2f");
		changed |= ImGui::DragFloat("Uniform Box Scale", &settings.placement.uniformBoxSizeScale, 0.01f, 0.05f, 1.0f, "%.2f");

		int uniformResolution = static_cast<int>(settings.placement.uniformProbeResolution);
		if (ImGui::InputInt("Uniform Resolution", &uniformResolution))
		{
			settings.placement.uniformProbeResolution = static_cast<std::uint32_t>(std::clamp(uniformResolution, 32, 512));
			changed = true;
		}

		int maxProbeCount = static_cast<int>(settings.placement.maxProbeCount);
		if (ImGui::InputInt("Max Probe Count", &maxProbeCount))
		{
			settings.placement.maxProbeCount = static_cast<std::uint32_t>(std::clamp(maxProbeCount, 1, 4096));
			changed = true;
		}

		if (ImGui::Button("Generate Auto Probes"))
		{
			if (changed)
			{
				editorAPI.ApplyReflectionProbeSettings(settings);
				changed = false;
			}
			editorAPI.GenerateAutoReflectionProbes();
			settings = editorAPI.GetReflectionProbeSettings();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Auto Probes"))
		{
			editorAPI.ClearAutoReflectionProbes();
			settings = editorAPI.GetReflectionProbeSettings();
		}
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
		changed |= ImGui::DragFloat("Sky Intensity", &settings.lighting.skyIntensity, 0.01f, 0.0f, 20.0f, "%.2f");
	}

	if (ImGui::CollapsingHeader("Bake", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Array Resolution: %u", settings.arrayResolution);
		ImGui::Text("Mip Count: %u", settings.mipCount);
		if (ImGui::Button("Request Bake All"))
			editorAPI.RequestReflectionProbeBakeAll();
		ImGui::SameLine();
		if (ImGui::Button("Bake Queue Now"))
			editorAPI.BakeQueuedReflectionProbesNow();
		ImGui::SameLine();
		if (ImGui::Button("Save Config"))
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
					changed |= ImGui::Checkbox("##enabled", &probe.enabled);
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
					const std::uint32_t mipCount = std::max(1u, settings.mipCount);
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
