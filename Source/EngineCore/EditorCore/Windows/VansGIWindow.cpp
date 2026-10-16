#include "VansGIWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IGIEditorAPI.h"
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
	Vans::EditorAPI::IGIEditorAPI& giAPI = editorAPI;
	if (!VansEditorWindow::IsWindowOpen(VansEditorWindowId::GI))
		return;

	if (!ImGui::Begin("GI Inspector", VansEditorWindow::WindowOpenState(VansEditorWindowId::GI)))
	{
		ImGui::End();
		return;
	}

	Vans::EditorAPI::GIInspectorSettingsSnapshot settings = giAPI.GetGISettings();
	static Vans::EditorAPI::GIInspectorSettingsSnapshot draftSettings;
	static bool draftInitialized = false;
	static bool applyFailed = false;
	static bool refreshGIResources = true;
	if (!settings.available)
	{
		draftInitialized = false;
		applyFailed = false;
		refreshGIResources = true;
		ImGui::TextDisabled("GI system is not available.");
		ImGui::End();
		return;
	}
	if (!draftInitialized)
	{
		draftSettings = settings;
		draftInitialized = true;
	}
	if (ImGui::CollapsingHeader("GI Measured Cost"))
	{
		const auto cost = giAPI.GetGIFrameCost();
		if (!cost.capturing) ImGui::TextDisabled("Capture is stopped. Enable capture in Profiler for new samples.");
		if (!cost.hasFrame) ImGui::TextDisabled("No profiler frame is available.");
		else
		{
			ImGui::Text("Last profiler frame: %llu", static_cast<unsigned long long>(cost.frameIndex));
			if (cost.gpuValid) ImGui::Text("GPU scoped intervals: %.3f ms", cost.gpuScopedMilliseconds);
			else ImGui::TextDisabled("GPU cost unavailable: no usable GI samples in this frame.");
			if (cost.cpuValid) ImGui::Text("CPU scoped intervals: %.3f ms", cost.cpuScopedMilliseconds);
			else ImGui::TextDisabled("CPU cost unavailable: no usable GI samples in this frame.");
			ImGui::TextDisabled("Union within each queue/thread, summed across queues/threads.");
			ImGui::TextDisabled("Nested rows are included. These intervals are not frame critical-path time.");
			if (ImGui::BeginTable("GICostStages", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Stage"); ImGui::TableSetupColumn("CPU / GPU"); ImGui::TableSetupColumn("ms / samples");
				ImGui::TableHeadersRow();
				for (size_t index = 0; index < GICostStages.size(); ++index)
				{
					const auto& stage = GICostStages[index]; const auto& sample = cost.stages[index];
					ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.label);
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.gpu ? "GPU" : "CPU");
					ImGui::TableNextColumn();
					if (sample.samples) ImGui::Text("%.3f / %u", sample.milliseconds, sample.samples);
					else ImGui::TextDisabled("--");
				}
				ImGui::EndTable();
			}
			ImGui::TextDisabled("-- means no usable sample for this stage, not zero cost.");
		}
	}
	if (ImGui::CollapsingHeader("GI Completed Work"))
	{
		const auto completed = giAPI.GetGIFrameWork();
        const auto& work = completed.probes;
		if (!work.available) ImGui::TextDisabled("No completed probe batch is available.");
		else
		{
			ImGui::Text("Last completed render frame: %llu", static_cast<unsigned long long>(work.renderFrameNumber));
			if (work.profilerFrameIndex != UINT64_MAX)
				ImGui::Text("Profiler frame: %llu", static_cast<unsigned long long>(work.profilerFrameIndex));
			const auto bytes = [](const char* label, uint64_t value)
			{ ImGui::Text("%s: %.3f KiB (%llu bytes)", label, double(value)/1024.0, static_cast<unsigned long long>(value)); };
			bytes("Probe / layout / material uploads", work.probeUploadBytes);
			bytes("Feedback readback", work.feedbackReadbackBytes);
			bytes("Diagnostic state readback", work.diagnosticReadbackBytes);
			if (work.worldRecorded)
			{
				const auto& world=work.world;
				ImGui::Text("World bricks: %u ready / %u mapped / %u capacity", world.readyBricks,world.mappedBricks,world.brickCapacity);
				ImGui::Text("Uploaded bricks: %u; pending CPU build: %u",world.uploadedBricks,world.pendingBuildBricks);
				bytes("World buffer capacity",world.bufferCapacityBytes);
				bytes("World uploads",world.UploadBytes());
				bytes("  Height / hierarchy",world.heightBytes+world.rangeBytes);
				bytes("  Surface color",world.colorBytes);
				bytes("  Page table / parameters",world.pageTableBytes+world.parameterBytes);
				bytes("  Voxels",world.voxelBytes);
				bytes("  Voxel materials",world.materialBytes);
				ImGui::Text("World buffer update commands: %u",world.updateCommands);
			}
			if (ImGui::BeginTable("GICompletedRegions", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
			{
				for (const auto* label : {"Region", "Updates / slots", "Prewarm", "Primary rays", "HW / world primary", "Rejected feedback"})
					ImGui::TableSetupColumn(label);
				ImGui::TableHeadersRow();
				for(uint32_t index=0;index<work.regionCount;++index)
				{
					const auto& region=work.regions[index];
					ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%u",region.stableId);
					ImGui::TableNextColumn();ImGui::Text("%u / %u",region.probeUpdates,region.physicalProbes);
					ImGui::TableNextColumn();ImGui::Text("%u",region.prewarmUpdates);
					ImGui::TableNextColumn();ImGui::Text("%llu",static_cast<unsigned long long>(region.primaryRays));
					ImGui::TableNextColumn();ImGui::Text("%llu / %llu",static_cast<unsigned long long>(region.hardwareRays),static_cast<unsigned long long>(region.worldRays));
					ImGui::TableNextColumn();ImGui::Text("%u",region.rejectedFeedback);
				}
				ImGui::EndTable();
			}
			ImGui::TextDisabled("Completed attempts; probe publication can still fail. Primary rays may query both backends.");
			ImGui::TextDisabled("World buffer capacity is not physical VRAM residency. Timing and work may refer to different frames.");
		}
        const auto& consumers = completed.consumers;
        if (!consumers.available) ImGui::TextDisabled("No completed screen GI frame is available.");
        else
        {
            ImGui::Separator();
            ImGui::Text("Screen GI completed render frame: %llu", static_cast<unsigned long long>(consumers.renderFrameNumber));
            if (consumers.profilerFrameIndex != UINT64_MAX)
                ImGui::Text("Screen GI profiler frame: %llu", static_cast<unsigned long long>(consumers.profilerFrameIndex));
            const auto count = [](const char* label, uint64_t value)
            { ImGui::Text("%s: %llu", label, static_cast<unsigned long long>(value)); };
            const auto& receiver = consumers.receiver;
            count("Receiver stable anchors", receiver.stableAnchors);
            count("Receiver prepared anchors", receiver.preparedAnchors);
            count("Receiver maintenance records", receiver.maintainedRecords);
            count("Receiver claim anchors", receiver.claimedAnchors);
            count("Receiver publication records", receiver.publishedRecords);
            count("Bias invocations / hardware", receiver.biasHardwareInvocations);
            count("Bias invocations / world", receiver.biasWorldInvocations);
            count("Visibility invocations / hardware", receiver.visibilityHardwareInvocations);
            count("Visibility invocations / world", receiver.visibilityWorldInvocations);
            count("Receiver buffer clear bytes", receiver.clearBytes);
            count("Receiver and probe history copy bytes", receiver.historyCopyBytes);
            count("SSGI cache texels", consumers.ssgi.cacheTexels);
            count("SSGI reconstruction pixels", consumers.ssgi.reconstructedPixels);
            count("SSGI temporal pixels", consumers.ssgi.temporalPixels);
            count("SSGI filter pixels / all passes", consumers.ssgi.filteredPixels);
            count("Sky updated cells / six directions each", consumers.ambientSky.updatedCells);
            count("Sky clear-test cells", consumers.ambientSky.clearTestCells);
            ImGui::TextDisabled("Submitted work domains, excluding padding; shaders may exit early or issue multiple queries.");
            ImGui::TextDisabled("These are not executed ray counts. Probe and screen work retain independent completion identities.");
        }
	}
    const auto memory = giAPI.GetGIMemorySnapshot();
    if (ImGui::CollapsingHeader("GI Resource Memory"))
    {
        if (!memory.Available()) ImGui::TextDisabled("No published GI resources.");
        const auto mib = [](uint64_t bytes) { return double(bytes) / (1024.0 * 1024.0); };
        ImGui::Text("Active allocations: %.3f MiB; pending retirement: %.3f MiB",
            mib(memory.ActiveAllocationBytes()), mib(memory.retiredAllocationBytes));
        ImGui::Text("Published + retired peak: %.3f MiB", mib(memory.publishedRetainedPeakBytes));
        if (ImGui::BeginTable("GIResourceOwners", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            for (const char* label : {"Owner", "Requested buffer MiB", "Buffer allocation MiB", "Image allocation MiB"})
                ImGui::TableSetupColumn(label);
            ImGui::TableHeadersRow();
            const auto row = [&](const char* name, const VansGraphics::VansGIMemoryUsage& usage)
            {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(usage.bufferCapacityBytes));
                ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(usage.bufferAllocationBytes));
                ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(usage.imageAllocationBytes));
            };
            row("World", memory.probes.world);
            row("Shared probe data", memory.probes.shared);
            row("Probe previews", memory.probes.previews);
            row("Receiver visibility", memory.receiver);
            row("SSGI", memory.ssgi);
            row("Ambient specular sky", memory.ambientSky);
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("GIResourceRegions", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            for (const auto* label : {"Region", "Physical probes", "Ray capacity", "Requested buffer MiB", "Buffer allocation MiB", "Image allocation MiB"})
                ImGui::TableSetupColumn(label);
            ImGui::TableHeadersRow();
            for (uint32_t i = 0; i < memory.probes.regionCount; ++i)
            {
                const auto& region = memory.probes.regions[i];
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%u", region.stableId);
                ImGui::TableNextColumn(); ImGui::Text("%u", region.physicalProbes);
                ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(region.rayCapacity));
                ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(region.storage.bufferCapacityBytes));
                ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(region.storage.bufferAllocationBytes));
                ImGui::TableNextColumn(); ImGui::Text("%.3f", mib(region.storage.imageAllocationBytes));
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("VMA allocation sizes; not heap reservation or OS VRAM residency.");
        ImGui::TextDisabled("Scope: probe/world, previews, receiver, SSGI and sky-cache storage; excludes shared scene geometry and pipelines.");
        ImGui::TextDisabled("Peak covers published resources and pending retirement on this device; excludes candidate creation and staging.");
    }
	if (draftSettings.regions.empty() || settings.regions.empty())
	{
		ImGui::TextDisabled("No GI Region is configured.");
		ImGui::End();
		return;
	}
	if (ImGui::BeginCombo("Region", draftSettings.regions[std::min<std::size_t>(
        draftSettings.selectedRegionIndex, draftSettings.regions.size() - 1u)].name.c_str()))
    {
        for (std::uint32_t index = 0; index < draftSettings.regions.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(draftSettings.regions[index].name.c_str(), index == draftSettings.selectedRegionIndex))
                draftSettings.selectedRegionIndex = index;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(draftSettings.regions.size() >= settings.maximumRegionCount);
    if (ImGui::Button("Add Region"))
    {
        auto region = draftSettings.regions[std::min<std::size_t>(draftSettings.selectedRegionIndex, draftSettings.regions.size() - 1u)];
        region.stableId = 1u;
        for (const auto& existing : draftSettings.regions) region.stableId = std::max(region.stableId, existing.stableId + 1u);
        region.name = "GI Region " + std::to_string(region.stableId);
        draftSettings.regions.push_back(std::move(region));
        draftSettings.selectedRegionIndex = static_cast<std::uint32_t>(draftSettings.regions.size() - 1u);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(draftSettings.regions.size() <= 1u);
    if (ImGui::Button("Remove Region"))
    {
        const auto index = std::min<std::size_t>(draftSettings.selectedRegionIndex, draftSettings.regions.size() - 1u);
        draftSettings.regions.erase(draftSettings.regions.begin() + index);
        draftSettings.selectedRegionIndex = static_cast<std::uint32_t>(std::min(index, draftSettings.regions.size() - 1u));
    }
    ImGui::EndDisabled();
	const std::uint32_t selectedIndex = std::min<std::uint32_t>(
		draftSettings.selectedRegionIndex,
		static_cast<std::uint32_t>(draftSettings.regions.size() - 1u));
	draftSettings.selectedRegionIndex = selectedIndex;
	auto& draftRegion = draftSettings.regions[selectedIndex];
	const auto& selectedRuntimeRegion = settings.regions[std::min<std::uint32_t>(
		settings.selectedRegionIndex,
		static_cast<std::uint32_t>(settings.regions.size() - 1u))];
    glm::uvec3 storageDimensions{0u};
    for (uint32_t i = 0; i < memory.probes.regionCount; ++i)
        if (memory.probes.regions[i].stableId == selectedRuntimeRegion.stableId)
        {
            storageDimensions = memory.probes.regions[i].storageDimensions;
            break;
        }

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

			const int maxZSlice = std::max(0, static_cast<int>(storageDimensions.z) - 1);
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
			if (refreshGIResources || refreshRequested || livePreviewDue)
			{
				previews = giAPI.RequestGIRTPreviews(
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
				refreshGIResources = false;
			}

			const std::string sliceLabel = followActiveSlice
				? "current active Z"
				: "Z=" + std::to_string(zSlice) + "/" + std::to_string(maxZSlice);
			ImGui::Text("Storage slice: %u x %u, %s",
				storageDimensions.x,
				storageDimensions.y,
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
	if (ImGui::CollapsingHeader("Terrain and PCG GI"))
	{
		auto& world = draftSettings.world;
		ImGui::Checkbox("Enable Terrain and PCG GI", &world.enabled);
		ImGui::BeginDisabled(!world.enabled);
		ImGui::DragFloat("Finest Voxel Size (m)", &world.voxelSize, 0.025f, 0.125f, 4.0f);
		ImGui::DragFloat("Detail Coverage Radius (m)", &world.coverageDistance, 1.0f, 16.0f, 2048.0f);
		ImGui::DragFloat("Canopy Extinction", &world.extinctionScale, 0.01f, 0.0f, 16.0f);
		ImGui::InputScalar("Minimum Voxel Levels", ImGuiDataType_U32, &world.levelCount);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Far voxels become coarser when needed to keep half the brick budget available for nearby detail.");
		ImGui::InputScalar("World Brick Budget", ImGuiDataType_U32, &world.maxBricks);
		ImGui::InputScalar("Brick Updates Per Frame", ImGuiDataType_U32, &world.bricksPerFrame);
		ImGui::InputScalar("World Trace Step Budget", ImGuiDataType_U32, &world.maxTraceSteps);
		ImGui::EndDisabled();
		ImGui::TextDisabled("Trees and shrubs use whole-model voxels. Grass only receives GI.");
		ImGui::TextDisabled("Disabled: no world GI resources or updates. Apply uses the safe resource transaction.");
	}
	if (ImGui::CollapsingHeader("Ambient Sky Reflection Visibility"))
	{
		auto& sky = draftSettings.ambientSkyCache;
		ImGui::Checkbox("Enable Ambient Sky Cache", &sky.enabled);
		ImGui::BeginDisabled(!sky.enabled);
		ImGui::DragFloat("Cache Cell Spacing (m)", &sky.gridSpacing, 0.1f, 2.0f, 16.0f, "%.1f");
		ImGui::InputScalar("Cell Updates Per Frame", ImGuiDataType_U32, &sky.cellUpdatesPerFrame);
		sky.cellUpdatesPerFrame = std::clamp(sky.cellUpdatesPerFrame, 1u, settings.maximumAmbientSkyCellUpdatesPerFrame);
		ImGui::TextDisabled("Six direction queries per cell (%u queries per frame).", sky.cellUpdatesPerFrame * 6u);
		ImGui::EndDisabled();
		ImGui::TextDisabled("Only the uncovered sky part of ambient specular is attenuated; SSR, local probes, SSGI and direct lighting remain unchanged.");
		ImGui::TextDisabled("The cache traces the terrain and vegetation GIWorld and falls back to full sky visibility while coverage is unavailable.");
	}

	if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto& placement = draftSettings.placement;
		ImGui::TextDisabled("Refinement limits are shared by adaptive regions; frame budgets cover all regions.");
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
		int placementMode = static_cast<int>(draftRegion.mode);
        if (ImGui::Combo("Placement Mode", &placementMode, "Uniform\0Adaptive\0"))
        {
            draftRegion.mode = static_cast<Vans::EditorAPI::GIRegionPlacementMode>(placementMode);
            if (draftRegion.mode == Vans::EditorAPI::GIRegionPlacementMode::Adaptive) draftRegion.followView = false;
        }
        ImGui::Checkbox("Enabled", &draftRegion.enabled);
		ImGui::Checkbox("Terrain / PCG Region", &draftRegion.worldOnly);
        ImGui::BeginDisabled(!draftRegion.worldOnly || draftRegion.mode == Vans::EditorAPI::GIRegionPlacementMode::Adaptive);
        ImGui::Checkbox("Follow View", &draftRegion.followView);
        ImGui::EndDisabled();
		ImGui::TextDisabled("Active only with Terrain and PCG GI. Ordinary regions keep their original lighting.");
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
        ImGui::BeginDisabled(draftRegion.worldOnly && draftRegion.followView);
		ImGui::DragFloat3("Region Center", &draftRegion.regionCenter.x, 0.05f);
        ImGui::EndDisabled();
		ImGui::DragFloat("Normal Bias", &draftRegion.normalBias, 0.005f, 0.0f, 10.0f, "%.3f");
		ImGui::DragFloat("Max Ray Distance", &draftRegion.trace.maxDistance, 0.1f, 0.001f, 10000.0f, "%.2f");
        ImGui::DragFloat("Trace Min Voxel Size", &draftRegion.trace.minVoxelSize, 0.01f, 0.0f, 128.0f, "%.3f m");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum voxel size for geometry queries. 0 uses the finest resident level. Independent of probe spacing.");
        ImGui::DragFloat("Scatter Min Voxel Size", &draftRegion.trace.scatterMinVoxelSize, 0.01f, 0.0f, 128.0f, "%.3f m");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum voxel size for incoming scattering queries; also bounded by the world field's scattering floor.");
		ImGui::DragFloat("Volume Fade Distance", &draftRegion.volumeFadeDistance, 0.05f, 0.0f, 1000.0f, "%.2f");

		const float draftVolumeSize[3] = {
			draftRegion.gridDimensions.x * draftRegion.probeSpacing,
			draftRegion.gridDimensions.y * draftRegion.probeSpacing,
			draftRegion.gridDimensions.z * draftRegion.probeSpacing };
		const std::uint32_t draftTotalProbeCount =
			static_cast<std::uint32_t>(draftRegion.gridDimensions.x) *
			static_cast<std::uint32_t>(draftRegion.gridDimensions.y) *
			static_cast<std::uint32_t>(draftRegion.gridDimensions.z);
		ImGui::Text("Applied Author Grid: %u x %u x %u",
			static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.x),
			static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.y),
			static_cast<unsigned>(selectedRuntimeRegion.gridDimensions.z));
        ImGui::Text("Probe Storage: %u x %u x %u", storageDimensions.x, storageDimensions.y, storageDimensions.z);
		ImGui::Text("GI Regions: %u, Regular Layout Probes: %u",
			static_cast<unsigned>(settings.regions.size()),
			static_cast<unsigned>(settings.totalProbeCount));
		ImGui::Text("Regular Layout Ray Capacity Bound: %llu",
			static_cast<unsigned long long>(settings.totalRayCacheEntries));
		ImGui::Text("Draft Regular Grid Probes: %u", draftTotalProbeCount);
		ImGui::Text("Draft Volume Size: %.2f x %.2f x %.2f", draftVolumeSize[0], draftVolumeSize[1], draftVolumeSize[2]);
		ImGui::Text("Runtime Min: %.2f, %.2f, %.2f", selectedRuntimeRegion.volumeMin.x, selectedRuntimeRegion.volumeMin.y, selectedRuntimeRegion.volumeMin.z);
		ImGui::Text("Runtime Max: %.2f, %.2f, %.2f", selectedRuntimeRegion.volumeMax.x, selectedRuntimeRegion.volumeMax.y, selectedRuntimeRegion.volumeMax.z);
		if (!settings.regions.empty() && ImGui::TreeNode("Configured Regions"))
		{
			for (size_t index = 0; index < settings.regions.size(); ++index)
			{
				const auto& region = settings.regions[index];
				ImGui::Text("%u: %s %s | %u regular probes | %llu ray capacity bound",
					static_cast<unsigned>(index),
					region.name.c_str(),
					region.enabled ? "enabled" : "disabled",
					static_cast<unsigned>(region.totalProbeCount),
					static_cast<unsigned long long>(region.rayCacheEntries));
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
			giAPI.SetGIProbeVisualization(draftSettings.showProbeGizmos, draftSettings.showProbeVolume, draftSettings.gizmoStride);
		ImGui::TextDisabled("Display changes apply immediately. Stride 1 shows every probe.");

		ImGui::DragFloat("DDGI Atlas Exposure", &draftSettings.debugExposure, 0.05f, 0.001f, 64.0f, "%.3f");
		ImGui::Separator();
		ImGui::Checkbox("Deferred: DDGI Probe Irradiance Only", &draftSettings.probeOnlyDeferredOutput);
		ImGui::DragFloat("Deferred Probe Display Exposure", &draftSettings.probeOnlyDeferredExposure, 0.05f, 0.001f, 64.0f, "%.3f");
		ImGui::TextDisabled("Directly displays the per-pixel DDGI atlas sample. SSGI, sky, direct lighting and BRDF are skipped.");

		ImGui::Separator();
		const auto debugSnapshot = giAPI.GetGIProbeDebugSnapshot();
		if (debugSnapshot && debugSnapshot->available)
		{
			ImGui::Text("Physical probes: %u; displayed: %u", debugSnapshot->physicalProbeCount, static_cast<unsigned>(debugSnapshot->probes.size()));
			ImGui::TextDisabled("Generated positions refresh with the layout. Relocation and lighting are shown in RT Preview.");
		}
		else if (draftSettings.showProbeGizmos && selectedRuntimeRegion.mode == Vans::EditorAPI::GIRegionPlacementMode::Adaptive)
			ImGui::TextDisabled("No generated positions are available for the selected GI region.");
	}

	if (ImGui::Button("Apply Runtime GI Settings"))
	{
		draftSettings.available = true;
		applyFailed = !giAPI.ApplyGISettings(draftSettings);
		if (!applyFailed) { draftInitialized = false; refreshGIResources = true; }
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
		giAPI.SaveGIConfiguration();

	ImGui::End();
}

} // namespace VansGraphics
