#include "VansPostProcessWindow.h"

#include "../VansEditorWindow.h"

#include "imgui.h"

namespace
{
	bool g_CommandMergeBoundaryReached = false;
	bool g_PendingSceneCommit = false;

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

	bool DragIntTracked(
		const char* label,
		int* value,
		float speed,
		int minValue,
		int maxValue,
		const char* format)
	{
		const bool changed = ImGui::DragInt(label, value, speed, minValue, maxValue, format);
		TrackCommandMergeBoundary();
		return changed;
	}

	bool ComboTracked(const char* label, int* value, const char* const items[], int itemCount)
	{
		const bool changed = ImGui::Combo(label, value, items, itemCount);
		TrackCommandMergeBoundary();
		return changed;
	}

	bool ColorEdit3Tracked(const char* label, float* value)
	{
		const bool changed = ImGui::ColorEdit3(
			label,
			value,
			ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
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

	void FlushPendingSceneCommit(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		bool canPersist)
	{
		if (!g_PendingSceneCommit)
			return;

		if (!canPersist)
		{
			g_PendingSceneCommit = false;
			return;
		}

		editorAPI.CommitPostProcessSettings();
		editorAPI.BreakCommandMergeGroup();
		g_PendingSceneCommit = false;
	}
}

void VansGraphics::VansPostProcessWindow::ShowWindow(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	const bool canPersist = editorAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Edit;
	if (!VansEditorWindow::m_PostProcessWindowOpen)
	{
		FlushPendingSceneCommit(editorAPI, canPersist);
		return;
	}

	g_CommandMergeBoundaryReached = false;
	if (!ImGui::Begin("Post Process", &VansEditorWindow::m_PostProcessWindowOpen))
	{
		FlushPendingSceneCommit(editorAPI, canPersist);
		ImGui::End();
		return;
	}

	Vans::EditorAPI::PostProcessSettingsSnapshot settings = editorAPI.GetPostProcessSettings();
	if (!settings.available)
	{
		FlushPendingSceneCommit(editorAPI, canPersist);
		ImGui::TextDisabled("Post-process runtime is not available. Load a scene first.");
		ImGui::End();
		return;
	}

	ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "LIVE");
	ImGui::SameLine();
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
		changed |= DragFloatTracked("Clamp", &settings.bloomClamp, 0.1f, 0.0f, 1024.0f, "%.1f");
		float bloomTint[3] = { settings.bloomTintR, settings.bloomTintG, settings.bloomTintB };
		if (ColorEdit3Tracked("Tint", bloomTint))
		{
			settings.bloomTintR = bloomTint[0];
			settings.bloomTintG = bloomTint[1];
			settings.bloomTintB = bloomTint[2];
			changed = true;
		}
		const char* bloomShapeNames[] = { "Standard", "Anamorphic", "Star" };
		changed |= ComboTracked("Shape", &settings.bloomShapeMode, bloomShapeNames, 3);
		const bool shapeEnabled = settings.enableBloom && settings.bloomShapeMode != 0;
		BeginOptionalSection(shapeEnabled);
		changed |= DragFloatTracked("Shape Intensity", &settings.bloomShapeIntensity, 0.01f, 0.0f, 4.0f, "%.2f");
		changed |= DragFloatTracked("Shape Blend", &settings.bloomShapeBlend, 0.01f, 0.0f, 1.0f, "%.2f");
		changed |= DragFloatTracked("Shape Angle", &settings.bloomShapeAngleDeg, 0.25f, -360.0f, 360.0f, "%.1f deg");
		changed |= DragFloatTracked("Streak Length", &settings.bloomStreakLength, 0.25f, 0.0f, 128.0f, "%.1f px");
		changed |= DragFloatTracked("Streak Attenuation", &settings.bloomStreakAttenuation, 0.005f, 0.0f, 0.98f, "%.3f");
		if (settings.bloomShapeMode == 1)
		{
			changed |= DragFloatTracked("Anamorphic Stretch", &settings.bloomAnamorphicStretch, 0.05f, 0.0f, 16.0f, "%.2f");
		}
		else if (settings.bloomShapeMode == 2)
		{
			changed |= DragIntTracked("Star Arms", &settings.bloomStreakCount, 0.05f, 2, 8, "%d");
		}
		EndOptionalSection(shapeEnabled);
		EndOptionalSection(settings.enableBloom);
	}

	if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= CheckboxTracked("Enable DOF", &settings.enableDOF);
		BeginOptionalSection(settings.enableDOF);
		changed |= DragFloatTracked("Focus Distance", &settings.focusDistance, 0.05f, 0.01f, 100000.0f, "%.2f m");
		changed |= DragFloatTracked("Focal Length", &settings.focalLengthMm, 0.25f, 8.0f, 300.0f, "%.1f mm");
		changed |= DragFloatTracked("F-Stop", &settings.fStop, 0.05f, 0.7f, 32.0f, "f/%.1f");
		changed |= DragFloatTracked("Sensor Height", &settings.sensorHeightMm, 0.1f, 1.0f, 80.0f, "%.1f mm");
		changed |= DragFloatTracked("Max CoC", &settings.maxCoC, 0.25f, 0.0f, 64.0f, "%.1f px");
		changed |= CheckboxTracked("Blur Transmission Background", &settings.dofBlurTransmissionBackground);
		EndOptionalSection(settings.enableDOF);
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
	{
		editorAPI.ApplyPostProcessSettings(settings);
		if (canPersist)
			g_PendingSceneCommit = true;
		else
			g_PendingSceneCommit = false;
	}
	if (g_CommandMergeBoundaryReached || !ImGui::IsAnyItemActive())
		FlushPendingSceneCommit(editorAPI, canPersist);

	ImGui::Separator();
	if (ImGui::Button("Reset to Defaults"))
	{
		Vans::EditorAPI::PostProcessSettingsSnapshot defaults;
		defaults.available = true;
		editorAPI.BreakCommandMergeGroup();
		editorAPI.ApplyPostProcessSettings(defaults);
		g_PendingSceneCommit = false;
		if (canPersist)
			editorAPI.CommitPostProcessSettings();
		editorAPI.BreakCommandMergeGroup();
	}

	if (g_CommandMergeBoundaryReached)
		editorAPI.BreakCommandMergeGroup();

	ImGui::End();
}
