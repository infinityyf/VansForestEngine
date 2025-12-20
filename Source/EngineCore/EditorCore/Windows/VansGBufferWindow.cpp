#include "VansGBufferWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"

void VansGraphics::VansGBufferWindow::ShowWindow(VansVKDevice& device)
{
    if (!VansGraphics::VansEditorWindow::m_GBufferWindowOpen)
    {
        return;
    }
    ImGui::Begin("GBuffer Visualization");

    auto renderPassManager = VansRenderPassManager::GetInstance();
    if (renderPassManager)
    {
        // Get GBuffer images
        VansVKImage& gbuffer0 = renderPassManager->GetGbuffer0(); // Albedo + Roughness
        VansVKImage& gbuffer1 = renderPassManager->GetGbuffer1(); // Metallic + AO + MaterialID
        VansVKImage& gbuffer2 = renderPassManager->GetGbuffer2(); // WorldPosition + LinearDepth
        VansVKImage& normal = renderPassManager->GetNormal();     // Normal

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
                // Ensure the image is in a readable layout (SHADER_READ_ONLY_OPTIMAL)
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

        // Static cache for each slot to avoid recreating descriptor sets every frame
        static VkDescriptorSet ds0 = VK_NULL_HANDLE; static VkImageView iv0 = VK_NULL_HANDLE;
        static VkDescriptorSet ds1 = VK_NULL_HANDLE; static VkImageView iv1 = VK_NULL_HANDLE;
        static VkDescriptorSet ds2 = VK_NULL_HANDLE; static VkImageView iv2 = VK_NULL_HANDLE;
        static VkDescriptorSet dsN = VK_NULL_HANDLE; static VkImageView ivN = VK_NULL_HANDLE;

        if (ImGui::BeginTable("GBufferTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
        {
            ImGui::TableNextColumn();
            DisplayImage("GBuffer 0 (Albedo + Roughness)", gbuffer0, ds0, iv0);

            ImGui::TableNextColumn();
            DisplayImage("GBuffer 1 (Metallic + AO + MatID)", gbuffer1, ds1, iv1);

            ImGui::TableNextColumn();
            DisplayImage("GBuffer 2 (WorldPos + LinearDepth)", gbuffer2, ds2, iv2);

            ImGui::TableNextColumn();
            DisplayImage("Normal", normal, dsN, ivN);

            ImGui::EndTable();
        }
    }
    else
    {
        ImGui::Text("RenderPassManager not initialized.");
    }

    ImGui::End();
}
