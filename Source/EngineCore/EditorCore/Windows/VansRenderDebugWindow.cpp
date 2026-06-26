#include "VansRenderDebugWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VansScene.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"

void VansGraphics::VansRenderDebugWindow::ShowWindow(VansVKDevice& device)
{
    if (!VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen)
    {
        return;
    }
    ImGui::Begin("Render Debug");

    auto renderPassManager = VansRenderPassManager::GetInstance();
    VansMaterialManager* materialManager = m_Scene ? m_Scene->GetMaterialManager() : nullptr;

    if (!renderPassManager)
    {
        ImGui::Text("RenderPassManager not initialized.");
        ImGui::End();
        return;
    }

    // Helper lambda to display an image with caching
    auto DisplayImage = [](const char* label, VansVKImage& image, VkDescriptorSet& cachedDS,
        VkImageView& cachedImageView, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        ImGui::Text("%s", label);

        VkImageView currentImageView = image.GetImageView();
        if (cachedDS == VK_NULL_HANDLE || cachedImageView != currentImageView)
        {
            if (cachedDS != VK_NULL_HANDLE)
            {
                ImGui_ImplVulkan_RemoveTexture(cachedDS);
            }
            cachedDS = ImGui_ImplVulkan_AddTexture(image.GetSampler(), currentImageView, layout);
            cachedImageView = currentImageView;
        }

        if (cachedDS != VK_NULL_HANDLE)
        {
            float width = ImGui::GetContentRegionAvail().x;
            float aspect = (float)image.GetImageDimension().width / (float)image.GetImageDimension().height;
            ImGui::Image((ImTextureID)cachedDS, ImVec2(width, width / aspect));
        }
    };

    // Static cache for each slot
    static VkDescriptorSet dsMV = VK_NULL_HANDLE;  static VkImageView ivMV = VK_NULL_HANDLE;
    static VkDescriptorSet dsSSR = VK_NULL_HANDLE;  static VkImageView ivSSR = VK_NULL_HANDLE;
    static VkDescriptorSet dsSSGI = VK_NULL_HANDLE; static VkImageView ivSSGI = VK_NULL_HANDLE;
    static VkDescriptorSet dsFog = VK_NULL_HANDLE;  static VkImageView ivFog = VK_NULL_HANDLE;
    static VkDescriptorSet dsSSS = VK_NULL_HANDLE;  static VkImageView ivSSS = VK_NULL_HANDLE;

    if (ImGui::BeginTable("RenderDebugTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
    {
        // Motion Vector
        ImGui::TableNextColumn();
        DisplayImage("Motion Vector", renderPassManager->GetMotionVector(), dsMV, ivMV);

        // SSR Resolve Result
        ImGui::TableNextColumn();
        if (materialManager)
        {
            VansTexture* ssrTex = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSR_RESULT);
            if (ssrTex)
            {
                DisplayImage("SSR Resolve Result", ssrTex->GetImage(), dsSSR, ivSSR, VK_IMAGE_LAYOUT_GENERAL);
            }
            else
            {
                ImGui::Text("SSR Resolve Result: N/A");
            }
        }
        else
        {
            ImGui::Text("SSR Resolve Result: No MaterialManager");
        }

        // SSGI Result
        ImGui::TableNextColumn();
        if (materialManager)
        {
            VansTexture* ssgiTex = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_RESULT);
            if (ssgiTex)
            {
                DisplayImage("SSGI Result", ssgiTex->GetImage(), dsSSGI, ivSSGI, VK_IMAGE_LAYOUT_GENERAL);
            }
            else
            {
                ImGui::Text("SSGI Result: N/A");
            }
        }
        else
        {
            ImGui::Text("SSGI Result: No MaterialManager");
        }

        // Fog Blend Result
        ImGui::TableNextColumn();
        if (materialManager)
        {
            VansTexture* fogTex = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);
            if (fogTex)
            {
                DisplayImage("Fog Blend Result", fogTex->GetImage(), dsFog, ivFog, VK_IMAGE_LAYOUT_GENERAL);
            }
            else
            {
                ImGui::Text("Fog Blend Result: N/A");
            }
        }
        else
        {
            ImGui::Text("Fog Blend Result: No MaterialManager");
        }

        ImGui::TableNextColumn();
        if (materialManager)
        {
            VansTexture* sssTex = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT);
            if (sssTex)
            {
                DisplayImage("Screen Space Shadow", sssTex->GetImage(), dsSSS, ivSSS, VK_IMAGE_LAYOUT_GENERAL);
            }
            else
            {
                ImGui::Text("Screen Space Shadow: N/A");
            }
        }
        else
        {
            ImGui::Text("Screen Space Shadow: No MaterialManager");
        }

        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Motion Matching", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!m_Scene || m_Scene->m_AnimationNodes.empty())
        {
            ImGui::TextDisabled("No animation nodes in current scene.");
        }
        else
        {
            for (auto* animNode : m_Scene->m_AnimationNodes)
            {
                if (!animNode || !animNode->GetController())
                    continue;

                const auto* debug = animNode->GetController()->GetMotionMatchingDebugData();
                if (!debug)
                    continue;

                ImGui::SeparatorText(animNode->GetName().c_str());
                ImGui::Text("Enabled: %s", debug->enabled ? "true" : "false");
                ImGui::Text("Database: %s | Clips: %d | Samples: %d",
                            debug->databaseReady ? "ready" : "not ready",
                            debug->clipCount,
                            debug->sampleCount);
                ImGui::Text("Active: %s @ %.3fs",
                            debug->activeClip.empty() ? "<none>" : debug->activeClip.c_str(),
                            debug->activeTime);
                ImGui::Text("Selected: %s @ %.3fs",
                            debug->selectedClip.empty() ? "<none>" : debug->selectedClip.c_str(),
                            debug->selectedTime);
                ImGui::Text("Cost: total %.3f | trajectory %.3f | pose %.3f | bias %.3f",
                            debug->currentCost,
                            debug->trajectoryCost,
                            debug->poseCost,
                            debug->biasCost);
                ImGui::Text("Query: speed %.2f | direction %.2f rad | switches %d",
                            debug->querySpeed,
                            debug->queryDirection,
                            debug->switches);

                if (ImGui::BeginTable("MMTopCandidates", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("Clip");
                    ImGui::TableSetupColumn("Time");
                    ImGui::TableSetupColumn("Total");
                    ImGui::TableSetupColumn("Trajectory");
                    ImGui::TableSetupColumn("Pose");
                    ImGui::TableHeadersRow();
                    for (const auto& candidate : debug->topCandidates)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(candidate.clipName.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.3f", candidate.time);
                        ImGui::TableNextColumn(); ImGui::Text("%.3f", candidate.totalCost);
                        ImGui::TableNextColumn(); ImGui::Text("%.3f", candidate.trajectoryCost);
                        ImGui::TableNextColumn(); ImGui::Text("%.3f", candidate.poseCost);
                    }
                    ImGui::EndTable();
                }
            }
        }
    }

    ImGui::End();
}
