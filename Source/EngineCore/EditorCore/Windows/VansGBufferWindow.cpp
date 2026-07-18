#include "VansGBufferWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"

#include <vector>

namespace
{
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
}

void VansGraphics::VansGBufferWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansGraphics::VansEditorWindow::m_GBufferWindowOpen &&
		!VansGraphics::VansEditorWindow::m_WaterGBufferWindowOpen)
	{
		return;
	}

	if (VansGraphics::VansEditorWindow::m_GBufferWindowOpen)
	{
		ImGui::Begin("GBuffer Visualization");
		Vans::EditorAPI::RenderTextureFilter filter;
		filter.category = "gbuffer";
		DrawPreviewTable("GBufferTable", editorAPI.QueryRenderTexturePreviews(filter));
		ImGui::End();
	}

	if (!VansGraphics::VansEditorWindow::m_WaterGBufferWindowOpen)
		return;

	ImGui::Begin("Water GBuffer Visualization");
	ImGui::TextWrapped("Water-specific GBuffer outputs used to confirm water pixels are isolated from the scene GBuffer.");
	Vans::EditorAPI::RenderTextureFilter filter;
	filter.category = "water_gbuffer";
	DrawPreviewTable("WaterGBufferTable", editorAPI.QueryRenderTexturePreviews(filter));
	ImGui::End();
}
