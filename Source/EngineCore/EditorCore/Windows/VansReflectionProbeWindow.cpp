#include "VansReflectionProbeWindow.h"
#include "../VansEditorWindow.h"
#include "../../RenderCore/ReflectionProbeCore/VansReflectionProbeSystem.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
	const char* ProbeTypeName(ReflectionProbeType type)
	{
		switch (type)
		{
		case ReflectionProbeType::Realtime: return "Realtime";
		case ReflectionProbeType::Sky: return "Sky";
		case ReflectionProbeType::Baked:
		default: return "Baked";
		}
	}

	void RebuildProbeResources(VansScene* scene, VansVKDevice& device, VansReflectionProbeSystem* probes)
	{
		if (!scene || !probes) return;
		probes->CreateGPUResources(device, device.GetImmediateGraphicsCommandBuffer());
		probes->UpdateGlobalDescriptors(scene->m_GlobalDescriptorSet);
	}

	VkDescriptorSet GetPreviewDescriptor(VansReflectionProbeSystem* probes, VkImageView view)
	{
		if (!probes || view == VK_NULL_HANDLE || !probes->GetSpecularArray())
			return VK_NULL_HANDLE;

		static VkImageView cachedView = VK_NULL_HANDLE;
		static VkSampler cachedSampler = VK_NULL_HANDLE;
		static VkDescriptorSet cachedDescriptor = VK_NULL_HANDLE;
		static VansTexture* cachedTexture = nullptr;

		VansTexture* texture = probes->GetSpecularArray();
		VkSampler sampler = texture->GetImage().GetSampler();
		if (cachedTexture != texture || cachedView != view || cachedSampler != sampler || cachedDescriptor == VK_NULL_HANDLE)
		{
			cachedTexture = texture;
			cachedView = view;
			cachedSampler = sampler;
			cachedDescriptor = ImGui_ImplVulkan_AddTexture(
				cachedSampler,
				cachedView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		return cachedDescriptor;
	}
}

void VansReflectionProbeWindow::ShowWindow(VansVKDevice& device)
{
	if (!VansEditorWindow::m_ReflectionProbeWindowOpen)
		return;

	if (!ImGui::Begin("Reflection Probe Inspector", &VansEditorWindow::m_ReflectionProbeWindowOpen))
	{
		ImGui::End();
		return;
	}

	if (!m_Scene)
	{
		ImGui::TextDisabled("No scene loaded.");
		ImGui::End();
		return;
	}

	VansReflectionProbeSystem* probes = m_Scene->GetReflectionProbeSystem();
	if (!probes)
	{
		ImGui::TextDisabled("Reflection probe system is not available.");
		ImGui::End();
		return;
	}
	auto& editor = probes->GetEditorState();
	auto& placement = probes->GetPlacementSettings();
	auto& lighting = probes->GetLightingSettings();
	auto& probeList = probes->GetProbes();
	auto& bakeResults = probes->GetBakeResults();

	if (ImGui::CollapsingHeader("Scene Gizmos", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Show Probe Gizmos", &editor.showProbeGizmos);
		ImGui::Checkbox("Show Influence Volumes", &editor.showInfluenceVolumes);
		ImGui::Checkbox("Show Blend Volumes", &editor.showBlendVolumes);
		ImGui::Checkbox("Show Placement Grid", &editor.showPlacementGrid);
		ImGui::Checkbox("Show Regions", &editor.showRegions);

		const char* debugViews[] = {
			"None", "Influence", "Probe Color", "SSR Confidence",
			"Region Id", "Parallax", "Fallback Only", "SSR Only"
		};
		int debugIndex = static_cast<int>(editor.debugView);
		if (ImGui::Combo("Debug View", &debugIndex, debugViews, IM_ARRAYSIZE(debugViews)))
		{
			editor.debugView = static_cast<ReflectionProbeDebugView>(debugIndex);
			probes->UploadMetadata();
		}
	}

	if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Auto Placement Enabled", &placement.enabled);
		ImGui::DragFloat3("Volume Min", glm::value_ptr(placement.volumeMin), 0.25f);
		ImGui::DragFloat3("Volume Max", glm::value_ptr(placement.volumeMax), 0.25f);
		ImGui::DragFloat("Uniform Spacing", &placement.uniformSpacing, 0.1f, 0.5f, 100.0f, "%.2f");
		ImGui::DragFloat("Uniform Box Scale", &placement.uniformBoxSizeScale, 0.01f, 0.05f, 1.0f, "%.2f");
		int uniformResolution = static_cast<int>(placement.uniformProbeResolution);
		if (ImGui::InputInt("Uniform Resolution", &uniformResolution))
			placement.uniformProbeResolution = static_cast<uint32_t>(std::clamp(uniformResolution, 32, 512));
		int maxProbeCount = static_cast<int>(placement.maxProbeCount);
		if (ImGui::InputInt("Max Probe Count", &maxProbeCount))
			placement.maxProbeCount = static_cast<uint32_t>(std::clamp(maxProbeCount, 1, 4096));

		if (ImGui::Button("Generate Auto Probes"))
		{
			probes->GenerateAutoProbes(*m_Scene, true);
			RebuildProbeResources(m_Scene, device, probes);
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Auto Probes"))
		{
			probes->ClearAutoProbes();
			RebuildProbeResources(m_Scene, device, probes);
		}
	}

	if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int maxBlend = static_cast<int>(lighting.maxBlendCount);
		if (ImGui::SliderInt("Max Blend Count", &maxBlend, 1, 4))
		{
			lighting.maxBlendCount = static_cast<uint32_t>(maxBlend);
			probes->UploadMetadata();
		}
		bool lightingDirty = false;
		lightingDirty |= ImGui::DragFloat("SSR Roughness Fade Start", &lighting.ssrRoughnessFadeStart, 0.01f, 0.0f, 1.0f, "%.2f");
		lightingDirty |= ImGui::DragFloat("SSR Roughness Fade End", &lighting.ssrRoughnessFadeEnd, 0.01f, 0.0f, 1.0f, "%.2f");
		lightingDirty |= ImGui::DragFloat("Sky Intensity", &lighting.skyIntensity, 0.01f, 0.0f, 20.0f, "%.2f");
		if (lightingDirty)
			probes->UploadMetadata();
	}

	if (ImGui::CollapsingHeader("Bake", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Array Resolution: %u", probes->GetArrayResolution());
		ImGui::Text("Mip Count: %u", probes->GetMipCount());
		if (ImGui::Button("Request Bake All"))
			probes->RequestBakeAll();
		ImGui::SameLine();
		if (ImGui::Button("Bake Queue Now"))
			probes->BakeQueuedProbesNow(*m_Scene, device, device.GetImmediateGraphicsCommandBuffer());
		ImGui::SameLine();
		if (ImGui::Button("Save Config"))
			probes->SaveConfiguration();
	}

	if (ImGui::CollapsingHeader("Probes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Probe Count: %d", static_cast<int>(probeList.size()));
		if (ImGui::BeginTable("ReflectionProbeTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 45.0f);
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 65.0f);
			ImGui::TableHeadersRow();
			for (int i = 0; i < static_cast<int>(probeList.size()); ++i)
			{
				auto& probe = probeList[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%d", i);
				ImGui::TableNextColumn();
				const bool selected = editor.selectedProbeIndex == i;
				if (ImGui::Selectable(probe.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					editor.selectedProbeIndex = i;
				ImGui::TableNextColumn();
				ImGui::Text("%s", ProbeTypeName(probe.type));
				ImGui::TableNextColumn();
				const char* status = i < static_cast<int>(bakeResults.size()) ? bakeResults[i].status.c_str() : "No bake result";
				ImGui::TextWrapped("%s", status);
				ImGui::TableNextColumn();
				if (probe.type == ReflectionProbeType::Sky)
				{
					ImGui::TextDisabled("-");
				}
				else
				{
					ImGui::PushID(i);
					if (ImGui::Checkbox("##enabled", &probe.enabled))
						probes->UploadMetadata();
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}

		if (editor.selectedProbeIndex >= 0 && editor.selectedProbeIndex < static_cast<int>(probeList.size()))
		{
			ImGui::Separator();
			auto& probe = probeList[editor.selectedProbeIndex];
			ImGui::Text("Selected: %s", probe.name.c_str());
			bool dirty = false;
			dirty |= ImGui::DragFloat3("Position", glm::value_ptr(probe.position), 0.1f);
			dirty |= ImGui::DragFloat3("Capture Position", glm::value_ptr(probe.capturePosition), 0.1f);
			if (probe.shape == ReflectionProbeShape::Box)
			{
				dirty |= ImGui::DragFloat3("Box Min", glm::value_ptr(probe.boxMin), 0.1f);
				dirty |= ImGui::DragFloat3("Box Max", glm::value_ptr(probe.boxMax), 0.1f);
				dirty |= ImGui::Checkbox("Box Projection", &probe.boxProjection);
			}
			else
			{
				dirty |= ImGui::DragFloat("Radius", &probe.radius, 0.1f, 0.01f, 10000.0f, "%.2f");
			}
			dirty |= ImGui::DragFloat("Blend Distance", &probe.blendDistance, 0.05f, 0.001f, 1000.0f, "%.2f");
			dirty |= ImGui::DragFloat("Intensity", &probe.intensity, 0.05f, 0.0f, 100.0f, "%.2f");
			dirty |= ImGui::DragFloat("Specular Intensity", &probe.specularIntensity, 0.05f, 0.0f, 100.0f, "%.2f");
			dirty |= ImGui::DragFloat("Priority", &probe.priority, 0.05f, -1000.0f, 1000.0f, "%.2f");
			if (dirty)
			{
				probes->MarkDirty(static_cast<size_t>(editor.selectedProbeIndex));
				probes->UploadMetadata();
			}

			if (probe.type != ReflectionProbeType::Sky)
			{
				if (ImGui::Button("Request Bake Selected"))
					probes->RequestBake(static_cast<size_t>(editor.selectedProbeIndex));
				ImGui::SameLine();
				if (probe.autoGenerated && ImGui::Button("Convert To Manual"))
					probes->ConvertToManual(static_cast<size_t>(editor.selectedProbeIndex));
			}

			if (probe.type != ReflectionProbeType::Sky && ImGui::CollapsingHeader("Cubemap Preview", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Preview Cubemap", &editor.previewCubemap);
				const char* faces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
				editor.previewFace = std::clamp(editor.previewFace, 0, 5);
				ImGui::Combo("Face", &editor.previewFace, faces, IM_ARRAYSIZE(faces));
				ImGui::SliderFloat("Roughness", &editor.previewRoughness, 0.0f, 1.0f, "%.2f");

				if (editor.previewCubemap)
				{
					const uint32_t mipCount = std::max(1u, probes->GetMipCount());
					const uint32_t mip = static_cast<uint32_t>(
						std::round(std::clamp(editor.previewRoughness, 0.0f, 1.0f) * float(mipCount - 1)));
					VkImageView previewView = probes->GetPreviewFaceView(
						static_cast<size_t>(editor.selectedProbeIndex),
						static_cast<uint32_t>(editor.previewFace),
						mip);
					VkDescriptorSet previewDescriptor = GetPreviewDescriptor(probes, previewView);
					if (previewDescriptor != VK_NULL_HANDLE)
					{
						const float mipScale = 1.0f / float(1u << mip);
						const float textureSize = std::max(1.0f, float(probes->GetArrayResolution()) * mipScale);
						const float maxPreviewSize = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
						const float previewSize = std::max(96.0f, std::min(maxPreviewSize, textureSize));
						ImGui::Text("Mip %u / %u", mip, mipCount - 1);
						ImGui::Image((ImTextureID)previewDescriptor, ImVec2(previewSize, previewSize));
					}
					else
					{
						ImGui::TextDisabled("Preview texture is not available.");
					}
				}
			}
		}
	}

	const auto errors = probes->ValidatePlacement();
	if (!errors.empty() && ImGui::CollapsingHeader("Validation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const std::string& error : errors)
			ImGui::TextWrapped("%s", error.c_str());
	}

	ImGui::End();
}

}



