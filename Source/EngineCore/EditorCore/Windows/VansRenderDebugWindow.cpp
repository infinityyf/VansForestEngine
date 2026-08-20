#include "VansRenderDebugWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"

#include <cfloat>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	unsigned long long ToImGuiCount(std::uint64_t value)
	{
		return static_cast<unsigned long long>(value);
	}

	void DrawPreviewTable(
		const char* tableId,
		const std::vector<Vans::EditorAPI::RenderTexturePreview>& previews)
	{
		if (previews.empty())
		{
			ImGui::TextDisabled("No render texture previews are available.");
			return;
		}

		if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
		{
			for (const auto& preview : previews)
			{
				ImGui::TableNextColumn();
				ImGui::Text("%s", preview.name.c_str());
				if (!preview.texture || preview.width == 0 || preview.height == 0)
				{
					ImGui::TextDisabled("(image not created)");
					continue;
				}

				const float width = ImGui::GetContentRegionAvail().x;
				const float aspect = static_cast<float>(preview.width) / static_cast<float>(preview.height);
				ImGui::Image(preview.texture, ImVec2(width, width / aspect));
			}
			ImGui::EndTable();
		}
	}

	void DrawPipelineRegistryRow(
		const char* label,
		const Vans::EditorAPI::PipelineRegistryMapStatsSnapshot& stats)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
		ImGui::Text("%llu", ToImGuiCount(stats.bucketCount));
		ImGui::TableNextColumn();
		ImGui::Text("%llu", ToImGuiCount(stats.activeCount));
		ImGui::TableNextColumn();
		ImGui::Text("%llu", ToImGuiCount(stats.expiredCount));
	}

	void DrawPipelineRegistryStats(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!ImGui::CollapsingHeader("Pipeline Registry", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		const Vans::EditorAPI::PipelineRegistryStatsSnapshot stats =
			editorAPI.GetPipelineRegistryStats();

		ImGui::Text("Total Active: %llu", ToImGuiCount(stats.totalActiveCount));
		ImGui::Text("Total Expired: %llu", ToImGuiCount(stats.totalExpiredCount));

		if (ImGui::BeginTable("PipelineRegistryStatsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Buckets");
			ImGui::TableSetupColumn("Active");
			ImGui::TableSetupColumn("Expired");
			ImGui::TableHeadersRow();

			DrawPipelineRegistryRow("Graphics", stats.graphics);
			DrawPipelineRegistryRow("Compute", stats.compute);
			DrawPipelineRegistryRow("Ray Tracing", stats.rayTracing);
			ImGui::EndTable();
		}
	}

	void DrawRenderDocControls(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!ImGui::CollapsingHeader("RenderDoc Capture", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		Vans::EditorAPI::RenderDocStatusSnapshot status = editorAPI.GetRenderDocStatus();
		const ImVec4 statusColor = status.available
			? ImVec4(0.30f, 0.85f, 0.45f, 1.0f)
			: ImVec4(0.95f, 0.65f, 0.20f, 1.0f);
		ImGui::TextColored(statusColor, "%s", status.available ? "Injected / ready" : "Not injected");
		ImGui::TextWrapped("%s", status.message.c_str());

		if (!status.available)
		{
			ImGui::TextDisabled("Open RenderDoc, choose ForestEngine.exe as the executable, then Launch Application.");
			return;
		}

		ImGui::Text("API: %d.%d.%d", status.apiMajor, status.apiMinor, status.apiPatch);
		ImGui::Text("Target control: %s", status.targetControlConnected ? "connected" : "not connected");
		ImGui::Text("Captures: %u", status.captureCount);
		ImGui::TextWrapped("Capture template: %s", status.capturePathTemplate.c_str());
		if (!status.lastCapturePath.empty())
		{
			ImGui::TextWrapped("Last capture: %s", status.lastCapturePath.c_str());
		}

		bool apiValidation = status.apiValidationEnabled;
		if (ImGui::Checkbox("Capture API validation messages", &apiValidation))
		{
			editorAPI.SetRenderDocAPIValidationEnabled(apiValidation);
		}

		bool referenceAllResources = status.referenceAllResources;
		if (ImGui::Checkbox("Reference all live resources (larger capture)", &referenceAllResources))
		{
			editorAPI.SetRenderDocReferenceAllResources(referenceAllResources);
		}

		if (status.frameCapturing)
		{
			ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.25f, 1.0f), "Capturing frame...");
		}
		else if (ImGui::Button("Capture Next Frame"))
		{
			editorAPI.CaptureNextRenderDocFrame();
		}

		ImGui::SameLine();
		if (ImGui::Button("Open RenderDoc UI"))
		{
			editorAPI.OpenRenderDocUI();
		}
	}

	void DrawRenderBackendDiagnostics(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!ImGui::CollapsingHeader("Render Backend Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		const Vans::EditorAPI::RenderBackendDiagnostics diagnostics =
			editorAPI.GetRenderBackendDiagnostics(false);

		if (ImGui::BeginTable("RenderBackendDiagnosticsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Metric");
			ImGui::TableSetupColumn("Value");
			ImGui::TableHeadersRow();

			auto drawBoolRow = [](const char* label, bool value)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(label);
				ImGui::TableNextColumn();
				ImGui::TextColored(
					value ? ImVec4(0.30f, 0.85f, 0.45f, 1.0f) : ImVec4(0.95f, 0.35f, 0.25f, 1.0f),
					"%s",
					value ? "true" : "false");
			};

			auto drawCountRow = [](const char* label, std::uint32_t value)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(label);
				ImGui::TableNextColumn();
				ImGui::Text("%u", value);
			};

			auto drawCount64Row = [](const char* label, std::uint64_t value)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(label);
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(value));
			};

			drawBoolRow("Graph Diagnostics Available", diagnostics.available);
			drawBoolRow("Frame Submit Succeeded", diagnostics.frameSubmitSucceeded);
			drawBoolRow("Shadow Submitted", diagnostics.shadowSubmitted);
			drawBoolRow("GBuffer Submitted", diagnostics.gbufferSubmitted);
			drawBoolRow("Async Early Submitted", diagnostics.asyncEarlySubmitted);
			drawCount64Row("Frame Number", diagnostics.frameNumber);
			drawCountRow("Swapchain Image", diagnostics.swapchainImageIndex);
			drawCountRow("Descriptor Standard Pools", diagnostics.descriptorStandardPoolCount);
			drawCountRow("Descriptor Update-After-Bind Pools", diagnostics.descriptorUpdateAfterBindPoolCount);
			drawCountRow("Descriptor Tracked Sets", diagnostics.descriptorTrackedSetCount);
			drawCountRow("Descriptor UAB Layouts", diagnostics.descriptorUpdateAfterBindLayoutCount);
			drawCountRow("Descriptor Global Persistent Sets", diagnostics.descriptorGlobalPersistentSetCount);
			drawCountRow("Descriptor Scene Persistent Sets", diagnostics.descriptorScenePersistentSetCount);
			drawCountRow("Descriptor Frame Transient Sets", diagnostics.descriptorFrameTransientSetCount);
			drawCountRow("Descriptor Pass Persistent Sets", diagnostics.descriptorPassPersistentSetCount);
			drawCountRow("Descriptor Upload Scratch Sets", diagnostics.descriptorUploadScratchSetCount);
			drawCountRow("Descriptor Ray Tracing Sets", diagnostics.descriptorRayTracingPersistentSetCount);
			drawCount64Row("Deferred Deletes Last Flush", diagnostics.deferredDeleteLastFlushCount);
			drawCount64Row("Deferred Deletes Pending", diagnostics.deferredDeletePendingCount);
			drawCount64Row("RenderNode Descriptor Failures", diagnostics.renderNodeDescriptorValidationFailureCount);
			drawCount64Row("Texture Upload Failures", diagnostics.textureUploadFailureCount);
			drawBoolRow("Compiled Graph Valid", diagnostics.compiledGraphValid);
			drawBoolRow("Feature Audit Passed", diagnostics.featureAuditPassed);
			drawCountRow("Frame Passes", diagnostics.framePlanPassCount);
			drawCountRow("Compiled Resources", diagnostics.compiledResourceCount);
			drawCountRow("Barrier Dependencies", diagnostics.barrierDependencyCount);
			drawCount64Row("Graph Topology Revision", diagnostics.renderGraphTopologyRevision);
			drawCount64Row("Graph Topology Hash", diagnostics.renderGraphTopologyHash);
			drawCount64Row("Graph Compiled Frame", diagnostics.renderGraphCompiledFrameNumber);
			ImGui::EndTable();
		}

		if (ImGui::CollapsingHeader("RenderGraph Summary"))
		{
			const Vans::EditorAPI::RenderBackendDiagnostics summaryDiagnostics =
				editorAPI.GetRenderBackendDiagnostics(true);
			if (!summaryDiagnostics.available)
			{
				ImGui::TextDisabled("No render graph diagnostics are available yet.");
			}
			else
			{
				if (ImGui::BeginChild(
					"RenderGraphSummaryText",
					ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 18.0f),
					true,
					ImGuiWindowFlags_HorizontalScrollbar))
				{
					ImGui::TextUnformatted(summaryDiagnostics.renderGraphSummary.c_str());
				}
				ImGui::EndChild();
			}
		}
	}
}

void VansGraphics::VansRenderDebugWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen &&
		!VansGraphics::VansEditorWindow::m_HairDebugWindowOpen)
	{
		return;
	}

	if (VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen)
	{
		ImGui::Begin("Render Debug", &VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen);
		Vans::EditorAPI::RenderTextureFilter filter;
		filter.category = "render_debug";
		DrawPreviewTable("RenderDebugTable", editorAPI.QueryRenderTexturePreviews(filter));
		ImGui::Separator();
		DrawRenderDocControls(editorAPI);
		ImGui::Separator();
		DrawRenderBackendDiagnostics(editorAPI);
		ImGui::Separator();
		DrawPipelineRegistryStats(editorAPI);
		ImGui::End();
	}

	if (VansGraphics::VansEditorWindow::m_HairDebugWindowOpen)
	{
		ImGui::Begin("Hair Debug", &VansGraphics::VansEditorWindow::m_HairDebugWindowOpen);
		ImGui::Text("Hair PPLL OIT");
		ImGui::Text("Visibility pass writes per-pixel linked-list storage buffers.");
		ImGui::Text("HairColor: RGB lit hair, A resolved coverage");
		ImGui::Text("HairDeepOpacity: RGBA four opacity slices");
		ImGui::Separator();

		Vans::EditorAPI::RenderTextureFilter filter;
		filter.category = "hair_debug";
		DrawPreviewTable("HairDebugTable", editorAPI.QueryRenderTexturePreviews(filter));
		ImGui::End();
	}
}
