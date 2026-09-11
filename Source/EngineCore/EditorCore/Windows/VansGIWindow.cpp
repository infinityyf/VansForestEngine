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
	static bool applyFailed = false;
	if (!settings.available)
	{
		draftInitialized = false;
		applyFailed = false;
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
			const int raysPerActiveProbe = static_cast<int>(selectedRuntimeRegion.raysPerProbe);
			const int maxRayIndex = raysPerActiveProbe - 1;
			zSlice = std::clamp(zSlice, 0, maxZSlice);
			if (rayIndex < 0)
				rayIndex = std::min(2, maxRayIndex);
			rayIndex = std::clamp(rayIndex, 0, maxRayIndex);
			ImGui::Checkbox("Follow Current Active Z Slice", &followActiveSlice);
			if (!followActiveSlice)
				ImGui::SliderInt("Probe Z Slice", &zSlice, 0, maxZSlice);
			ImGui::SliderInt("Probe Ray", &rayIndex, 0, maxRayIndex);
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
			ImGui::TextDisabled("All RT, DDGI and SSGI Probe Cache diagnostic targets refresh together. Probe Ray addresses the current complete probe update.");
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

	if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto& placement = draftSettings.placement;
		ImGui::Checkbox("Automatic GI Placement", &placement.enabled);
		ImGui::DragFloat("Minimum Probe Spacing (m)", &placement.minProbeSpacing, 0.01f, 0.001f, 100.0f, "%.3f");
		ImGui::DragFloat("Maximum Probe Spacing (m)", &placement.maxProbeSpacing, 0.05f, 0.001f, 1000.0f, "%.3f");
		ImGui::DragFloat("Parent GI Maximum Cell Size (m)", &placement.parentProbeMaxSize, 0.05f, 0.001f, 1000.0f, "%.3f");
		ImGui::InputScalar("Probe Budget", ImGuiDataType_U32, &placement.maxProbeCount);
		ImGui::InputScalar("Probe Updates Per Frame", ImGuiDataType_U32, &placement.maxProbeUpdatesPerFrame);
		ImGui::InputScalar("Rays Per Frame", ImGuiDataType_U32, &placement.maxRaysPerFrame);
		ImGui::TextDisabled("GI placement is independent of reflection placement. 0.5 m is a configurable default.");
		ImGui::TextDisabled("Apply changes the runtime preview. Store Runtime GI in Scene stages the applied settings; Save Scene writes the file.");
	}

	if (ImGui::CollapsingHeader("Probe Volume", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int gridDimensions[3] = {
			static_cast<int>(draftRegion.gridDimensions.x),
			static_cast<int>(draftRegion.gridDimensions.y),
			static_cast<int>(draftRegion.gridDimensions.z) };
		if (ImGui::DragInt3("Grid Dimensions XYZ", gridDimensions, 1.0f, 1, 256))
		{
			draftRegion.overrideGridDimensions = true;
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
		ImGui::Text("GI Regions: %u, Regular Layout Probes: %u",
			static_cast<unsigned>(settings.regions.size()),
			static_cast<unsigned>(settings.totalProbeCount));
		ImGui::Text("Ray Capacity Bound: %llu, Estimated Memory: %.1f MB",
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
		if (ImGui::DragInt("Rays Per Probe", &raysPerProbe, 1.0f, 2, 4096))
			draftRegion.raysPerProbe = static_cast<std::uint32_t>(std::clamp(raysPerProbe, 2, 4096));

		ImGui::DragFloat("Max Indirect Radiance", &draftSettings.maxIndirectRadiance, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Max Probe Radiance", &draftSettings.maxProbeRadiance, 0.05f, 0.0f, 1000.0f, "%.3f");
		ImGui::DragFloat("Irradiance Hysteresis", &draftSettings.irradianceHysteresis, 0.001f, 0.0f, 0.999f, "%.3f");
		ImGui::DragFloat("Distance Hysteresis", &draftSettings.distanceHysteresis, 0.001f, 0.0f, 0.999f, "%.3f");
		ImGui::DragFloat("Distance Sharpness", &draftSettings.distanceSharpness, 0.1f, 8.0f, 16.0f, "%.2f");

        ImGui::TextDisabled("Each scheduled probe traces and integrates all directions. Frame budgets apply to every layout.");
        ImGui::TextDisabled("Hysteresis is history retained per complete probe update.");
	}

	if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
	{
		draftSettings.showProbeGizmos = settings.showProbeGizmos;
		draftSettings.showProbeVolume = settings.showProbeVolume;
		draftSettings.gizmoStride = settings.gizmoStride;
		bool visualizationChanged = ImGui::Checkbox("Show GI Probe Positions", &draftSettings.showProbeGizmos);
		visualizationChanged |= ImGui::Checkbox("Show GI Volume Bounds", &draftSettings.showProbeVolume);

		int stride = static_cast<int>(draftSettings.gizmoStride);
		const int maxGridDimension = std::max(1, static_cast<int>(std::max({
			selectedRuntimeRegion.gridDimensions.x, selectedRuntimeRegion.gridDimensions.y, selectedRuntimeRegion.gridDimensions.z })));
		if (ImGui::SliderInt("Probe Gizmo Stride", &stride, 1, maxGridDimension))
		{
			draftSettings.gizmoStride = static_cast<std::uint32_t>(std::max(1, stride));
			visualizationChanged = true;
		}
		if (visualizationChanged)
			editorAPI.SetGIProbeVisualization(draftSettings.showProbeGizmos, draftSettings.showProbeVolume, draftSettings.gizmoStride);
		ImGui::TextDisabled("Display changes apply immediately. Stride 1 shows every probe.");

		ImGui::DragFloat("DDGI Atlas Exposure", &draftSettings.debugExposure, 0.05f, 0.001f, 64.0f, "%.3f");
		ImGui::Separator();
		ImGui::Checkbox("Deferred: DDGI Probe Irradiance Only", &draftSettings.probeOnlyDeferredOutput);
		ImGui::DragFloat("Deferred Probe Display Exposure", &draftSettings.probeOnlyDeferredExposure, 0.05f, 0.001f, 64.0f, "%.3f");
		ImGui::TextDisabled("Directly displays the per-pixel DDGI atlas sample. SSGI, sky, direct lighting and BRDF are skipped.");

		ImGui::Separator();
		const auto debugSnapshot = editorAPI.GetGIProbeDebugSnapshot();
		if (debugSnapshot && debugSnapshot->available)
		{
			ImGui::Text("Physical probes: %u; displayed: %u", debugSnapshot->physicalProbeCount, static_cast<unsigned>(debugSnapshot->probes.size()));
			ImGui::TextDisabled("Generated positions refresh with the layout. Relocation and lighting are shown in RT Preview.");
		}
		else if (draftSettings.showProbeGizmos && settings.placement.enabled)
			ImGui::TextDisabled("No generated positions are available for the selected GI region.");
	}

	if (ImGui::Button("Apply Runtime GI Settings"))
	{
		draftSettings.available = true;
		applyFailed = !editorAPI.ApplyGISettings(draftSettings);
		if (!applyFailed) draftInitialized = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Draft From Runtime"))
	{
		draftSettings = settings;
		applyFailed = false;
	}
	if (applyFailed)
		ImGui::TextWrapped("GI settings could not be applied. Previous lighting is retained; see the log for details.");
	if (ImGui::Button("Store Runtime GI in Scene"))
		editorAPI.SaveGIConfiguration();

	ImGui::End();
}

} // namespace VansGraphics
