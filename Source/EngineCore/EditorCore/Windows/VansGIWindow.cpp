#include "VansGIWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace VansGraphics
{
void VansGIWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_GIWindowOpen)
		return;

	if (!ImGui::Begin("GI Inspector", &VansEditorWindow::m_GIWindowOpen))
	{
		ImGui::End();
		return;
	}

	Vans::EditorAPI::GIInspectorSettingsSnapshot settings = editorAPI.GetGISettings();
	static Vans::EditorAPI::GIInspectorSettingsSnapshot draftSettings;
	static bool draftInitialized = false;
	if (!settings.available)
	{
		draftInitialized = false;
		ImGui::TextDisabled("GI system is not available.");
		ImGui::End();
		return;
	}
	if (!draftInitialized)
	{
		draftSettings = settings;
		draftInitialized = true;
	}

	if (ImGui::CollapsingHeader("Probe Volume", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int gridDimensions[3] = {
			static_cast<int>(draftSettings.gridDimensions.x),
			static_cast<int>(draftSettings.gridDimensions.y),
			static_cast<int>(draftSettings.gridDimensions.z) };
		if (ImGui::DragInt3("Grid Dimensions XYZ", gridDimensions, 1.0f, 1, 256))
		{
			draftSettings.gridDimensions = {
				static_cast<float>(std::clamp(gridDimensions[0], 1, 256)),
				static_cast<float>(std::clamp(gridDimensions[1], 1, 256)),
				static_cast<float>(std::clamp(gridDimensions[2], 1, 256)) };
		}

		float probeSpacingAxes[3] = {
			draftSettings.probeSpacingAxes.x,
			draftSettings.probeSpacingAxes.y,
			draftSettings.probeSpacingAxes.z };
		if (ImGui::DragFloat3("Probe Spacing XYZ", probeSpacingAxes, 0.01f, 0.001f, 100.0f, "%.3f"))
		{
			draftSettings.probeSpacingAxes = {
				std::max(probeSpacingAxes[0], 0.001f),
				std::max(probeSpacingAxes[1], 0.001f),
				std::max(probeSpacingAxes[2], 0.001f) };
		}
		ImGui::DragFloat3("Region Center", &draftSettings.regionCenter.x, 0.05f);
		ImGui::DragFloat("Normal Bias", &draftSettings.normalBias, 0.005f, 0.0f, 10.0f, "%.3f");
		ImGui::DragFloat("Max Ray Distance", &draftSettings.maxRayDistance, 0.1f, 0.001f, 10000.0f, "%.2f");
		ImGui::DragFloat("Volume Fade Distance", &draftSettings.volumeFadeDistance, 0.05f, 0.0f, 1000.0f, "%.2f");

		const float draftVolumeSize[3] = {
			draftSettings.gridDimensions.x * draftSettings.probeSpacingAxes.x,
			draftSettings.gridDimensions.y * draftSettings.probeSpacingAxes.y,
			draftSettings.gridDimensions.z * draftSettings.probeSpacingAxes.z };
		const std::uint32_t draftTotalProbeCount =
			static_cast<std::uint32_t>(draftSettings.gridDimensions.x) *
			static_cast<std::uint32_t>(draftSettings.gridDimensions.y) *
			static_cast<std::uint32_t>(draftSettings.gridDimensions.z);
		ImGui::Text("Runtime Grid: %u x %u x %u",
			static_cast<unsigned>(settings.gridDimensions.x),
			static_cast<unsigned>(settings.gridDimensions.y),
			static_cast<unsigned>(settings.gridDimensions.z));
		ImGui::Text("GI Regions: %u, Active Probes: %u",
			static_cast<unsigned>(settings.regions.size()),
			static_cast<unsigned>(settings.totalProbeCount));
		ImGui::Text("Ray Cache Entries: %llu, Estimated GI Memory: %.1f MB",
			static_cast<unsigned long long>(settings.totalRayCacheEntries),
			settings.totalEstimatedMemoryMB);
		ImGui::Text("Draft Total Probes: %u", draftTotalProbeCount);
		ImGui::Text("Draft Volume Size: %.2f x %.2f x %.2f", draftVolumeSize[0], draftVolumeSize[1], draftVolumeSize[2]);
		ImGui::Text("Runtime Min: %.2f, %.2f, %.2f", settings.volumeMin.x, settings.volumeMin.y, settings.volumeMin.z);
		ImGui::Text("Runtime Max: %.2f, %.2f, %.2f", settings.volumeMax.x, settings.volumeMax.y, settings.volumeMax.z);
		if (!settings.regions.empty() && ImGui::TreeNode("Region Cost"))
		{
			for (size_t index = 0; index < settings.regions.size(); ++index)
			{
				const auto& region = settings.regions[index];
				ImGui::Text("%u: %s %s | %u probes | %llu rays | %.1f MB",
					static_cast<unsigned>(index),
					region.name.c_str(),
					region.enabled ? "enabled" : "disabled",
					static_cast<unsigned>(region.totalProbeCount),
					static_cast<unsigned long long>(region.rayCacheEntries),
					region.estimatedMemoryMB);
			}
			ImGui::TreePop();
		}
	}

	if (ImGui::CollapsingHeader("Update", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int raysPerProbe = static_cast<int>(draftSettings.raysPerProbe);
		if (ImGui::DragInt("Rays Per Probe", &raysPerProbe, 1.0f, 1, 4096))
			draftSettings.raysPerProbe = static_cast<std::uint32_t>(std::clamp(raysPerProbe, 1, 4096));

		int spatialUpdateDivisor = static_cast<int>(draftSettings.spatialUpdateDivisor);
		const int minGridDimension = std::max(1, static_cast<int>(std::min({
			draftSettings.gridDimensions.x, draftSettings.gridDimensions.y, draftSettings.gridDimensions.z })));
		if (ImGui::DragInt("Spatial Update Divisor", &spatialUpdateDivisor, 1.0f, 1, minGridDimension))
			draftSettings.spatialUpdateDivisor = static_cast<std::uint32_t>(std::clamp(spatialUpdateDivisor, 1, minGridDimension));

		int directionUpdateSlices = static_cast<int>(draftSettings.directionUpdateSlices);
		if (ImGui::DragInt("Direction Update Slices", &directionUpdateSlices, 1.0f, 1, std::max(1, static_cast<int>(draftSettings.raysPerProbe))))
			draftSettings.directionUpdateSlices = static_cast<std::uint32_t>(std::clamp(directionUpdateSlices, 1, std::max(1, static_cast<int>(draftSettings.raysPerProbe))));

		ImGui::DragFloat("Environment Intensity", &draftSettings.environmentIntensity, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Max Indirect Radiance", &draftSettings.maxIndirectRadiance, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Max SH L0", &draftSettings.maxSHL0, 0.05f, 0.0f, 1000.0f, "%.3f");

		const std::uint64_t divisor = std::max(1u, draftSettings.spatialUpdateDivisor);
		const std::uint64_t spatialPhaseCount = divisor * divisor * divisor;
		const std::uint64_t fullRefreshFrames = spatialPhaseCount * std::max(1u, draftSettings.directionUpdateSlices);
		const std::uint64_t responseDivisor = std::min<std::uint64_t>(divisor, 2u);
		const std::uint64_t responseFrames = responseDivisor * responseDivisor * responseDivisor;
		ImGui::Text("Draft Full Probe Refresh: %llu frames", static_cast<unsigned long long>(fullRefreshFrames));
		ImGui::Text("Main Light Response Refresh: %llu frames", static_cast<unsigned long long>(responseFrames));
	}

	if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Show GI Probe Positions", &draftSettings.showProbeGizmos);
		ImGui::Checkbox("Show GI Volume Bounds", &draftSettings.showProbeVolume);

		int stride = static_cast<int>(draftSettings.gizmoStride);
		const int maxGridDimension = std::max(1, static_cast<int>(std::max({
			draftSettings.gridDimensions.x, draftSettings.gridDimensions.y, draftSettings.gridDimensions.z })));
		if (ImGui::SliderInt("Probe Gizmo Stride", &stride, 1, maxGridDimension))
			draftSettings.gizmoStride = static_cast<std::uint32_t>(std::max(1, stride));

		ImGui::DragFloat("Probe SH Exposure", &draftSettings.debugExposure, 0.05f, 0.001f, 64.0f, "%.3f");

		ImGui::Separator();
		if (ImGui::Button("Capture Probe SH"))
		{
			editorAPI.CaptureGIProbeDebugSnapshot(draftSettings.gizmoStride, draftSettings.debugExposure);
		}
		const Vans::EditorAPI::GIProbeDebugSnapshot debugSnapshot = editorAPI.GetGIProbeDebugSnapshot();
		ImGui::Text("Captured Probes: %u", static_cast<unsigned>(debugSnapshot.probes.size()));
		if (!debugSnapshot.status.empty())
			ImGui::TextWrapped("%s", debugSnapshot.status.c_str());
	}

	if (ImGui::CollapsingHeader("GI Ray Tracing Preview", ImGuiTreeNodeFlags_DefaultOpen))
	{
		static int previewMode = 0;
		static int zSlice = 0;
		static int rayIndex = 0;
		static float previewExposure = 1.0f;
		static float positionScale = 0.05f;
		static bool livePreview = false;
		static Vans::EditorAPI::RenderTexturePreview preview;
		static double lastPreviewRequestTime = -1.0;
		static int lastPreviewMode = -1;
		static int lastZSlice = -1;
		static int lastRayIndex = -1;
		static float lastPreviewExposure = -1.0f;
		static float lastPositionScale = -1.0f;

		static constexpr const char* previewModes[] = {
			"RT Miss Ratio",
			"RT Hit Mask",
			"RT Hit Position (Signed)",
			"RT Hit Normal",
			"RT Hit Albedo",
			"RT Hit Roughness",
			"GI Direct Radiance",
			"GI SH L0",
			"GI SH L1 Magnitude",
			"GI SH R L1 (Signed)",
			"GI SH G L1 (Signed)",
			"GI SH B L1 (Signed)"
		};
		ImGui::Combo("Preview Source", &previewMode, previewModes, IM_ARRAYSIZE(previewModes));

		const int maxZSlice = std::max(0, static_cast<int>(settings.gridDimensions.z) - 1);
		const int maxRayIndex = std::max(0, static_cast<int>(settings.raysPerProbe) - 1);
		zSlice = std::clamp(zSlice, 0, maxZSlice);
		rayIndex = std::clamp(rayIndex, 0, maxRayIndex);
		ImGui::SliderInt("Probe Z Slice", &zSlice, 0, maxZSlice);
		if (previewMode >= 1 && previewMode <= 6)
			ImGui::SliderInt("Ray Index", &rayIndex, 0, maxRayIndex);
		if (previewMode == 2)
			ImGui::DragFloat("Position Display Scale", &positionScale, 0.001f, 0.0001f, 10.0f, "%.4f");
		if (previewMode >= 6)
			ImGui::DragFloat("RT Preview Exposure", &previewExposure, 0.05f, 0.001f, 128.0f, "%.3f");

		ImGui::Checkbox("Live RT Preview", &livePreview);
		ImGui::SameLine();
		const bool refreshRequested = ImGui::Button("Refresh RT Preview");
		const bool previewParamsChanged =
			lastPreviewMode != previewMode ||
			lastZSlice != zSlice ||
			lastRayIndex != rayIndex ||
			lastPreviewExposure != previewExposure ||
			lastPositionScale != positionScale;
		const double now = ImGui::GetTime();
		const bool livePreviewDue =
			livePreview &&
			(previewParamsChanged || !preview.texture || lastPreviewRequestTime < 0.0 || (now - lastPreviewRequestTime) >= 0.25);
		if (refreshRequested || livePreviewDue)
		{
			preview = editorAPI.RequestGIRTPreview(
				static_cast<std::uint32_t>(previewMode),
				static_cast<std::uint32_t>(zSlice),
				static_cast<std::uint32_t>(rayIndex),
				previewExposure,
				positionScale);
			lastPreviewRequestTime = now;
			lastPreviewMode = previewMode;
			lastZSlice = zSlice;
			lastRayIndex = rayIndex;
			lastPreviewExposure = previewExposure;
			lastPositionScale = positionScale;
		}

		ImGui::Text("Grid slice: %u x %u, Z=%d/%d",
			static_cast<unsigned>(settings.gridDimensions.x),
			static_cast<unsigned>(settings.gridDimensions.y),
			zSlice, maxZSlice);
		ImGui::TextDisabled("Ray selection applies to hit/PBR/direct-light views; SH and miss ratio are per probe.");
		if (preview.texture && preview.width > 0 && preview.height > 0)
		{
			const float width = std::max(64.0f, ImGui::GetContentRegionAvail().x);
			const float aspect = static_cast<float>(preview.width) / static_cast<float>(preview.height);
			ImGui::Image(preview.texture, ImVec2(width, width / std::max(aspect, 0.001f)));
		}
		else
		{
			ImGui::TextDisabled("RT preview is unavailable until the scene has ray-tracing geometry and GI resources are ready.");
		}
	}

	if (ImGui::Button("Apply Runtime GI Settings"))
	{
		draftSettings.available = true;
		editorAPI.ApplyGISettings(draftSettings);
		draftInitialized = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Draft From Runtime"))
	{
		draftSettings = settings;
	}

	ImGui::End();
}

} // namespace VansGraphics
