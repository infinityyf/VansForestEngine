#include "VansPostProcessWindow.h"

#include "../VansEditorWindow.h"

#include "imgui.h"

namespace
{
	bool g_CommandMergeBoundaryReached = false;

	void TrackCommandMergeBoundary()
	{
		if (ImGui::IsItemDeactivatedAfterEdit())
			g_CommandMergeBoundaryReached = true;
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

	bool CheckboxTracked(const char* label, bool* value)
	{
		const bool changed = ImGui::Checkbox(label, value);
		TrackCommandMergeBoundary();
		return changed;
	}

	bool ComboTracked(const char* label, int* value, const char* const items[], int itemCount)
	{
		const bool changed = ImGui::Combo(label, value, items, itemCount);
		TrackCommandMergeBoundary();
		return changed;
	}

	void BeginOptionalSection(bool enabled)
	{
		if (!enabled)
			ImGui::BeginDisabled();
	}

	void EndOptionalSection(bool enabled)
	{
		if (!enabled)
			ImGui::EndDisabled();
	}
}

void VansGraphics::VansPostProcessWindow::ShowWindow(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_PostProcessWindowOpen)
		return;

	g_CommandMergeBoundaryReached = false;
	if (!ImGui::Begin("Post Process", &VansEditorWindow::m_PostProcessWindowOpen))
	{
		ImGui::End();
		return;
	}

	Vans::EditorAPI::PostProcessSettingsSnapshot settings = editorAPI.GetPostProcessSettings();
	if (!settings.available)
	{
		ImGui::TextDisabled("Post-process runtime is not available. Load a scene first.");
		ImGui::End();
		return;
	}

	ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "LIVE");
	ImGui::SameLine();
	const bool canPersist = editorAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Edit;
	ImGui::TextDisabled(canPersist
		? "Changes are applied immediately and stored in the scene document. Use Ctrl+S to save."
		: "Play mode changes affect the active runtime only and are not saved.");
	ImGui::Separator();

	bool changed = false;

	if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= CheckboxTracked("Auto Exposure", &settings.enableAutoExposure);
		changed |= DragFloatTracked("Compensation (EV)", &settings.exposureCompensation, 0.05f, -16.0f, 16.0f, "%+.2f EV");
		BeginOptionalSection(settings.enableAutoExposure);
		changed |= DragFloatTracked("Minimum EV100", &settings.minEV100, 0.1f, -24.0f, 24.0f, "%.1f");
		changed |= DragFloatTracked("Maximum EV100", &settings.maxEV100, 0.1f, settings.minEV100, 24.0f, "%.1f");
		changed |= DragFloatTracked("Adapt Speed Up", &settings.adaptationSpeedUp, 0.05f, 0.0f, 20.0f, "%.2f");
		changed |= DragFloatTracked("Adapt Speed Down", &settings.adaptationSpeedDown, 0.05f, 0.0f, 20.0f, "%.2f");
		EndOptionalSection(settings.enableAutoExposure);
	}

	if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= CheckboxTracked("Enable Bloom", &settings.enableBloom);
		BeginOptionalSection(settings.enableBloom);
		changed |= DragFloatTracked("Threshold", &settings.bloomThreshold, 0.02f, 0.0f, 64.0f, "%.2f");
		changed |= DragFloatTracked("Soft Knee", &settings.bloomKnee, 0.01f, 0.0f, 1.0f, "%.2f");
		changed |= DragFloatTracked("Intensity", &settings.bloomIntensity, 0.01f, 0.0f, 10.0f, "%.3f");
		changed |= DragFloatTracked("Scatter", &settings.bloomScatter, 0.01f, 0.0f, 1.0f, "%.2f");
		EndOptionalSection(settings.enableBloom);
	}

	if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const char* toneMapperNames[] = { "Linear", "ACES", "Reinhard" };
		changed |= ComboTracked("Operator", &settings.toneMapperType, toneMapperNames, 3);
		if (settings.toneMapperType == 2)
			changed |= DragFloatTracked("White Point", &settings.whitePoint, 0.1f, 0.1f, 64.0f, "%.2f");
	}

	if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= CheckboxTracked("Enable Color Grading", &settings.enableColorGrading);
		BeginOptionalSection(settings.enableColorGrading);
		changed |= DragFloatTracked("Contrast", &settings.contrast, 0.01f, 0.0f, 4.0f, "%.2f");
		changed |= DragFloatTracked("Saturation", &settings.saturation, 0.01f, 0.0f, 4.0f, "%.2f");
		changed |= DragFloatTracked("Hue Shift", &settings.hueShift, 0.005f, -1.0f, 1.0f, "%+.3f");
		changed |= DragFloatTracked("Temperature", &settings.temperature, 0.005f, -1.0f, 1.0f, "%+.3f");
		changed |= DragFloatTracked("Tint", &settings.tint, 0.005f, -1.0f, 1.0f, "%+.3f");
		EndOptionalSection(settings.enableColorGrading);
	}

	if (changed)
		editorAPI.ApplyPostProcessSettings(settings);
	if (g_CommandMergeBoundaryReached && canPersist)
		editorAPI.CommitPostProcessSettings();

	ImGui::Separator();
	if (ImGui::Button("Reset to Defaults"))
	{
		Vans::EditorAPI::PostProcessSettingsSnapshot defaults;
		defaults.available = true;
		editorAPI.BreakCommandMergeGroup();
		editorAPI.ApplyPostProcessSettings(defaults);
		if (canPersist)
			editorAPI.CommitPostProcessSettings();
		editorAPI.BreakCommandMergeGroup();
	}

	if (g_CommandMergeBoundaryReached)
		editorAPI.BreakCommandMergeGroup();

	ImGui::End();
}
