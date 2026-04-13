#include "VansRenderDebugWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VansScene.h"

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
    auto DisplayImage = [](const char* label, VansVKImage& image, VkDescriptorSet& cachedDS, VkImageView& cachedImageView)
    {
        ImGui::Text("%s", label);

        VkImageView currentImageView = image.GetImageView();
        if (cachedDS == VK_NULL_HANDLE || cachedImageView != currentImageView)
        {
            if (cachedDS != VK_NULL_HANDLE)
            {
                ImGui_ImplVulkan_RemoveTexture(cachedDS);
            }
            cachedDS = ImGui_ImplVulkan_AddTexture(image.GetSampler(), currentImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
                DisplayImage("SSR Resolve Result", ssrTex->GetImage(), dsSSR, ivSSR);
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
                DisplayImage("SSGI Result", ssgiTex->GetImage(), dsSSGI, ivSSGI);
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
                DisplayImage("Fog Blend Result", fogTex->GetImage(), dsFog, ivFog);
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

        ImGui::EndTable();
    }

    ImGui::End();
}
