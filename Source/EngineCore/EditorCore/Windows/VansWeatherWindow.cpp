#include "VansWeatherWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../../EngineAPILayer/Public/ISceneSettingsEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
void TrackItem(bool itemChanged, bool& changed, bool& commitAfterEdit)
{
	changed |= itemChanged;
	commitAfterEdit |= ImGui::IsItemDeactivatedAfterEdit();
}

void DrawRuntimeState(const Vans::EditorAPI::RainWeatherSettingsSnapshot& settings)
{
	ImGui::SeparatorText("Runtime Preview");
	ImGui::TextDisabled("GPU presentation: rain layers %u, splash quads %u",
		settings.rainLayerCount, settings.maximumSplashCount);
	ImGui::ProgressBar(
		std::clamp(settings.normalizedIntensity, 0.0f, 1.0f),
		ImVec2(-1.0f, 0.0f), "Rain intensity");
	ImGui::ProgressBar(
		std::clamp(settings.filmWetness, 0.0f, 1.0f),
		ImVec2(-1.0f, 0.0f), "Surface film");
}
}

void VansWeatherWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::IsWindowOpen(VansEditorWindowId::Weather))
		return;

	Vans::EditorAPI::ISceneSettingsEditorAPI& settingsAPI = editorAPI;
	Vans::EditorAPI::RainWeatherSettingsSnapshot settings =
		settingsAPI.GetRainWeatherSettings();

	ImGui::SetNextWindowSize(ImVec2(430.0f, 680.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Weather", VansEditorWindow::WindowOpenState(VansEditorWindowId::Weather)))
	{
		ImGui::End();
		return;
	}

	if (!settings.available)
	{
		ImGui::TextDisabled("Load a Scene to edit and preview rain weather.");
		ImGui::End();
		return;
	}

	DrawRuntimeState(settings);
	bool changed = false;
	bool puddleFillPreviewChanged = false;
	bool commitAfterEdit = false;

	ImGui::SeparatorText("Rainfall");
	TrackItem(ImGui::Checkbox("Enabled", &settings.enabled), changed, commitAfterEdit);
	TrackItem(ImGui::DragFloat("Rain rate", &settings.rainRateMmPerHour,
		0.25f, 0.0f, 500.0f, "%.2f mm/h"), changed, commitAfterEdit);
	TrackItem(ImGui::DragFloat("Full intensity rate", &settings.fullIntensityRateMmPerHour,
		0.25f, 0.1f, 500.0f, "%.2f mm/h"), changed, commitAfterEdit);

	if (ImGui::CollapsingHeader("Accumulation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		TrackItem(ImGui::DragFloat("Wetting half-life", &settings.wettingHalfLifeSeconds,
			0.1f, 0.05f, 3600.0f, "%.2f s"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Drying half-life", &settings.dryingHalfLifeSeconds,
			1.0f, 0.05f, 14400.0f, "%.1f s"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Puddle fill half-life", &settings.puddleFillHalfLifeSeconds,
			0.5f, 0.05f, 7200.0f, "%.1f s"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Puddle drain half-life", &settings.puddleDrainHalfLifeSeconds,
			1.0f, 0.05f, 28800.0f, "%.1f s"), changed, commitAfterEdit);
		puddleFillPreviewChanged = ImGui::SliderFloat(
			"Puddle fill preview", &settings.puddleFill, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Sets the current runtime water level once. It is not saved and then resumes normal rain accumulation.");
		}
	}

	if (ImGui::CollapsingHeader("Wind and Rain Layers", ImGuiTreeNodeFlags_DefaultOpen))
	{
		float windDirection[2] = { settings.windDirectionX, settings.windDirectionZ };
		const bool windChanged = ImGui::DragFloat2(
			"Wind direction XZ", windDirection, 0.01f, -1.0f, 1.0f, "%.3f");
		if (windChanged)
		{
			const float length = std::sqrt(
				windDirection[0] * windDirection[0] + windDirection[1] * windDirection[1]);
			if (length > 1.0e-4f)
			{
				settings.windDirectionX = windDirection[0] / length;
				settings.windDirectionZ = windDirection[1] / length;
			}
		}
		TrackItem(windChanged, changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Wind speed", &settings.windSpeedMetersPerSecond,
			0.05f, 0.0f, 100.0f, "%.2f m/s"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Fall speed", &settings.fallSpeedMetersPerSecond,
			0.05f, 0.1f, 50.0f, "%.2f m/s"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Visible distance", &settings.maximumVisibleDistanceMeters,
			0.5f, 1.0f, 500.0f, "%.1f m"), changed, commitAfterEdit);
	}

	if (ImGui::CollapsingHeader("Splash and Ripple", ImGuiTreeNodeFlags_DefaultOpen))
	{
		TrackItem(ImGui::DragFloat("Splash lifetime", &settings.splashLifetimeSeconds,
			0.01f, 0.02f, 5.0f, "%.2f s"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Splash radius", &settings.splashRadiusMeters,
			0.005f, 0.01f, 2.0f, "%.3f m"), changed, commitAfterEdit);
		TrackItem(ImGui::DragFloat("Ripple scale", &settings.rippleScaleMeters,
			0.02f, 0.05f, 50.0f, "%.2f m"), changed, commitAfterEdit);
		TrackItem(ImGui::SliderFloat("Ripple strength", &settings.rippleStrength,
			0.0f, 1.0f, "%.3f"), changed, commitAfterEdit);
	}

	bool previewAccepted = true;
	if (changed || puddleFillPreviewChanged)
	{
		settings.applyPuddleFillPreview = puddleFillPreviewChanged;
		const Vans::EditorAPI::RainWeatherApplyResult result =
			settingsAPI.ApplyRainWeatherSettings(settings);
		previewAccepted = result.accepted;
		m_Status = result.message;
		m_StatusIsError = !result.accepted;
	}

	ImGui::Separator();
	const bool applyToDocument = ImGui::Button("Apply to Scene Document");
	ImGui::SameLine();
	ImGui::TextDisabled("Disk write requires Save / Save All");
	if ((commitAfterEdit || applyToDocument) && previewAccepted)
	{
		const bool committed = settingsAPI.CommitRainWeatherSettings();
		m_Status = committed
			? "Scene document updated; use Save or Save All to write disk."
			: "Unable to update the Scene document.";
		m_StatusIsError = !committed;
	}

	if (!m_Status.empty())
	{
		const ImVec4 color = m_StatusIsError
			? ImVec4(0.95f, 0.35f, 0.30f, 1.0f)
			: ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
		ImGui::TextColored(color, "%s", m_Status.c_str());
	}
	ImGui::TextWrapped(
		"Rain settings preview the current Scene runtime immediately. Puddle fill preview changes only the current accumulated water level; Terrain > Layers owns the persistent puddle Noise shape.");

	ImGui::End();
}
}
