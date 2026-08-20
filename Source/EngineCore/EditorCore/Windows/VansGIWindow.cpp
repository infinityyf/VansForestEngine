#include "VansGIWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
	if (draftSettings.regions.empty() || settings.regions.empty())
	{
		ImGui::TextDisabled("No GI Region is configured.");
		ImGui::End();
		return;
	}
	const std::uint32_t selectedIndex = std::min<std::uint32_t>(
		draftSettings.selectedRegionIndex,
		static_cast<std::uint32_t>(draftSettings.regions.size() - 1u));
	draftSettings.selectedRegionIndex = selectedIndex;
	auto& draftRegion = draftSettings.regions[selectedIndex];
	const auto& selectedRuntimeRegion = settings.regions[std::min<std::uint32_t>(
		settings.selectedRegionIndex,
		static_cast<std::uint32_t>(settings.regions.size() - 1u))];

	const auto drawGIRTPreview = [&]()
	{
		if (ImGui::CollapsingHeader("GI / SSGI Probe Cache Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static int zSlice = 0;
			static int rayIndex = -1;
			static bool followActiveSlice = true;
			static float previewExposure = 1.0f;
			static float positionScale = 0.05f;
			static bool livePreview = true;
			static std::vector<Vans::EditorAPI::RenderTexturePreview> previews;
			static double lastPreviewRequestTime = -1.0;
			static bool lastFollowActiveSlice = false;
			static int lastZSlice = -1;
			static int lastRayIndex = -1;
			static float lastPreviewExposure = -1.0f;
			static float lastPositionScale = -1.0f;

			const int maxZSlice = std::max(0, static_cast<int>(selectedRuntimeRegion.gridDimensions.z) - 1);
			const int raysPerActiveProbe = std::max(1, static_cast<int>(
				(selectedRuntimeRegion.raysPerProbe + selectedRuntimeRegion.directionUpdateSlices - 1u) /
				std::max(selectedRuntimeRegion.directionUpdateSlices, 1u)));
			const int maxRayIndex = raysPerActiveProbe - 1;
			zSlice = std::clamp(zSlice, 0, maxZSlice);
			if (rayIndex < 0)
				rayIndex = std::min(2, maxRayIndex);
			rayIndex = std::clamp(rayIndex, 0, maxRayIndex);
			ImGui::Checkbox("Follow Current Active Z Slice", &followActiveSlice);
			if (!followActiveSlice)
				ImGui::SliderInt("Probe Z Slice", &zSlice, 0, maxZSlice);
			ImGui::SliderInt("Active Slice Ray", &rayIndex, 0, maxRayIndex);
			ImGui::DragFloat("Position Display Scale", &positionScale, 0.001f, 0.0001f, 10.0f, "%.4f");
			ImGui::DragFloat("RT Preview Exposure", &previewExposure, 0.05f, 0.001f, 128.0f, "%.3f");

			ImGui::Checkbox("Live RT Preview", &livePreview);
			ImGui::SameLine();
			const bool refreshRequested = ImGui::Button("Refresh RT Preview");
			const bool previewParamsChanged =
				lastFollowActiveSlice != followActiveSlice ||
				lastZSlice != zSlice ||
				lastRayIndex != rayIndex ||
				lastPreviewExposure != previewExposure ||
				lastPositionScale != positionScale;
			const double now = ImGui::GetTime();
			const bool livePreviewDue =
				livePreview &&
				(previewParamsChanged || previews.empty() || lastPreviewRequestTime < 0.0 || (now - lastPreviewRequestTime) >= 0.25);
			if (refreshRequested || livePreviewDue)
			{
				previews = editorAPI.RequestGIRTPreviews(
					followActiveSlice ? 0xffffffffu : static_cast<std::uint32_t>(zSlice),
					static_cast<std::uint32_t>(rayIndex),
					previewExposure,
					positionScale);
				lastPreviewRequestTime = now;
				lastFollowActiveSlice = followActiveSlice;
				lastZSlice = zSlice;
				lastRayIndex = rayIndex;
				lastPreviewExposure = previewExposure;
				lastPositionScale = positionScale;
			}

			const std::string sliceLabel = followActiveSlice
				? "current active Z"
				: "Z=" + std::to_string(zSlice) + "/" + std::to_string(maxZSlice);
			ImGui::Text("Grid slice: %u x %u, %s",
				static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.x),
				static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.y),
				sliceLabel.c_str());
			ImGui::TextDisabled("All RT, DDGI and SSGI Probe Cache diagnostic targets refresh together. Active Slice Ray addresses the current 16-ray update slice.");
			ImGui::TextDisabled("Screen Probe Cache Radiance is the 1/4-resolution Hi-Z screen query blended with DDGI/sky fallback; Surface stores geometric normal.xyz and linear depth.a for reconstruction validation.");
			if (previews.empty())
			{
				ImGui::TextDisabled("RT preview is unavailable until the scene has ray-tracing geometry and GI resources are ready.");
			}
			else if (ImGui::BeginTable("AllGIRTPreviews", 3, ImGuiTableFlags_SizingStretchSame))
			{
				for (const Vans::EditorAPI::RenderTexturePreview& preview : previews)
				{
					if (!preview.texture || preview.width == 0 || preview.height == 0)
						continue;
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(preview.name.c_str());
					const float width = std::max(64.0f, ImGui::GetContentRegionAvail().x);
					const float aspect = static_cast<float>(preview.width) / static_cast<float>(preview.height);
					ImGui::Image(preview.texture, ImVec2(width, width / std::max(aspect, 0.001f)));
				}
				ImGui::EndTable();
			}
		}
	};

	drawGIRTPreview();

	if (ImGui::CollapsingHeader("Probe Volume", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int gridDimensions[3] = {
			static_cast<int>(draftRegion.gridDimensions.x),
			static_cast<int>(draftRegion.gridDimensions.y),
			static_cast<int>(draftRegion.gridDimensions.z) };
		if (ImGui::DragInt3("Grid Dimensions XYZ", gridDimensions, 1.0f, 1, 256))
		{
			draftRegion.gridDimensions = {
				static_cast<float>(std::clamp(gridDimensions[0], 1, 256)),
				static_cast<float>(std::clamp(gridDimensions[1], 1, 256)),
				static_cast<float>(std::clamp(gridDimensions[2], 1, 256)) };
		}

		if (ImGui::DragFloat("Probe Spacing", &draftRegion.probeSpacing, 0.01f, 0.001f, 100.0f, "%.3f"))
		{
			draftRegion.probeSpacing = std::max(draftRegion.probeSpacing, 0.001f);
		}
		ImGui::DragFloat3("Region Center", &draftRegion.regionCenter.x, 0.05f);
		ImGui::DragFloat("Normal Bias", &draftRegion.normalBias, 0.005f, 0.0f, 10.0f, "%.3f");
		ImGui::DragFloat("Max Ray Distance", &draftRegion.maxRayDistance, 0.1f, 0.001f, 10000.0f, "%.2f");
		ImGui::DragFloat("Volume Fade Distance", &draftRegion.volumeFadeDistance, 0.05f, 0.0f, 1000.0f, "%.2f");

		const float draftVolumeSize[3] = {
			draftRegion.gridDimensions.x * draftRegion.probeSpacing,
			draftRegion.gridDimensions.y * draftRegion.probeSpacing,
			draftRegion.gridDimensions.z * draftRegion.probeSpacing };
		const std::uint32_t draftTotalProbeCount =
			static_cast<std::uint32_t>(draftRegion.gridDimensions.x) *
			static_cast<std::uint32_t>(draftRegion.gridDimensions.y) *
			static_cast<std::uint32_t>(draftRegion.gridDimensions.z);
		ImGui::Text("Runtime Grid: %u x %u x %u",
			static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.x),
			static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.y),
			static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.z));
		ImGui::Text("GI Regions: %u, Active Probes: %u",
			static_cast<unsigned>(settings.regions.size()),
			static_cast<unsigned>(settings.totalProbeCount));
		ImGui::Text("Active Ray Working Set: %llu, Estimated GI Memory: %.1f MB",
			static_cast<unsigned long long>(settings.totalRayCacheEntries),
			settings.totalEstimatedMemoryMB);
		ImGui::Text("Draft Total Probes: %u", draftTotalProbeCount);
		ImGui::Text("Draft Volume Size: %.2f x %.2f x %.2f", draftVolumeSize[0], draftVolumeSize[1], draftVolumeSize[2]);
		ImGui::Text("Runtime Min: %.2f, %.2f, %.2f", selectedRuntimeRegion.volumeMin.x, selectedRuntimeRegion.volumeMin.y, selectedRuntimeRegion.volumeMin.z);
		ImGui::Text("Runtime Max: %.2f, %.2f, %.2f", selectedRuntimeRegion.volumeMax.x, selectedRuntimeRegion.volumeMax.y, selectedRuntimeRegion.volumeMax.z);
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
		int raysPerProbe = static_cast<int>(draftRegion.raysPerProbe);
		if (ImGui::DragInt("Rays Per Probe", &raysPerProbe, 1.0f, 1, 4096))
			draftRegion.raysPerProbe = static_cast<std::uint32_t>(std::clamp(raysPerProbe, 1, 4096));

		int spatialUpdateDivisor = static_cast<int>(draftRegion.spatialUpdateDivisor);
		const int minGridDimension = std::max(1, static_cast<int>(std::min({
			draftRegion.gridDimensions.x, draftRegion.gridDimensions.y, draftRegion.gridDimensions.z })));
		if (ImGui::DragInt("Spatial Update Divisor", &spatialUpdateDivisor, 1.0f, 1, minGridDimension))
			draftRegion.spatialUpdateDivisor = static_cast<std::uint32_t>(std::clamp(spatialUpdateDivisor, 1, minGridDimension));

		int directionUpdateSlices = static_cast<int>(draftRegion.directionUpdateSlices);
		if (ImGui::DragInt("Direction Update Slices", &directionUpdateSlices, 1.0f, 1, std::max(1, static_cast<int>(draftRegion.raysPerProbe))))
			draftRegion.directionUpdateSlices = static_cast<std::uint32_t>(std::clamp(directionUpdateSlices, 1, std::max(1, static_cast<int>(draftRegion.raysPerProbe))));

		ImGui::DragFloat("Environment Intensity", &draftSettings.environmentIntensity, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Max Indirect Radiance", &draftSettings.maxIndirectRadiance, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Max Probe Radiance", &draftSettings.maxProbeRadiance, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Irradiance Hysteresis", &draftSettings.irradianceHysteresis, 0.001f, 0.0f, 0.999f, "%.3f");
		ImGui::DragFloat("Distance Hysteresis", &draftSettings.distanceHysteresis, 0.001f, 0.0f, 0.999f, "%.3f");
		ImGui::DragFloat("Distance Sharpness", &draftSettings.distanceSharpness, 0.1f, 8.0f, 16.0f, "%.2f");
		ImGui::DragFloat("Brightness Change Threshold", &draftSettings.brightnessChangeThreshold, 0.05f, 0.001f, 1000.0f, "%.3f");

		const std::uint64_t divisor = std::max(1u, draftRegion.spatialUpdateDivisor);
		const std::uint64_t spatialPhaseCount = divisor * divisor * divisor;
		const std::uint64_t fullRefreshFrames = spatialPhaseCount * std::max(1u, draftRegion.directionUpdateSlices);
		ImGui::Text("DDGI Full Probe/Direction Cycle: %llu frames", static_cast<unsigned long long>(fullRefreshFrames));
		ImGui::TextDisabled("Default 256 rays / spatial divisor 2 / 16 slices = 128 frames.");
	}

	if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Show GI Probe Positions", &draftSettings.showProbeGizmos);
		ImGui::Checkbox("Show GI Volume Bounds", &draftSettings.showProbeVolume);

		int stride = static_cast<int>(draftSettings.gizmoStride);
		const int maxGridDimension = std::max(1, static_cast<int>(std::max({
			draftRegion.gridDimensions.x, draftRegion.gridDimensions.y, draftRegion.gridDimensions.z })));
		if (ImGui::SliderInt("Probe Gizmo Stride", &stride, 1, maxGridDimension))
			draftSettings.gizmoStride = static_cast<std::uint32_t>(std::max(1, stride));

		ImGui::DragFloat("DDGI Atlas Exposure", &draftSettings.debugExposure, 0.05f, 0.001f, 64.0f, "%.3f");
		ImGui::Separator();
		ImGui::Checkbox("Deferred: DDGI Probe Irradiance Only", &draftSettings.probeOnlyDeferredOutput);
		ImGui::DragFloat("Deferred Probe Display Exposure", &draftSettings.probeOnlyDeferredExposure, 0.05f, 0.001f, 64.0f, "%.3f");
		ImGui::TextDisabled("Directly displays the per-pixel DDGI atlas sample. SSGI, sky, direct lighting and BRDF are skipped.");

		ImGui::Separator();
		if (ImGui::Button("Capture DDGI Probe State"))
		{
			editorAPI.CaptureGIProbeDebugSnapshot(draftSettings.gizmoStride, draftSettings.debugExposure);
		}
		const Vans::EditorAPI::GIProbeDebugSnapshot debugSnapshot = editorAPI.GetGIProbeDebugSnapshot();
		ImGui::Text("Captured Probes: %u", static_cast<unsigned>(debugSnapshot.probes.size()));
		if (!debugSnapshot.status.empty())
			ImGui::TextWrapped("%s", debugSnapshot.status.c_str());
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
