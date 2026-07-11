#include "VansRenderDebugWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VansScene.h"

void VansGraphics::VansRenderDebugWindow::ShowWindow(VansVKDevice& device)
{
    if (!VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen &&
        !VansGraphics::VansEditorWindow::m_HairDebugWindowOpen)
    {
        return;
    }

    auto renderPassManager = VansRenderPassManager::GetInstance();
    VansMaterialManager* materialManager = m_Scene ? m_Scene->GetMaterialManager() : nullptr;

    if (!renderPassManager)
    {
        if (VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen)
        {
            ImGui::Begin("Render Debug", &VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen);
            ImGui::Text("RenderPassManager not initialized.");
            ImGui::End();
        }
        if (VansGraphics::VansEditorWindow::m_HairDebugWindowOpen)
        {
            ImGui::Begin("Hair Debug", &VansGraphics::VansEditorWindow::m_HairDebugWindowOpen);
            ImGui::Text("RenderPassManager not initialized.");
            ImGui::End();
        }
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
    static VkDescriptorSet dsHairVis0 = VK_NULL_HANDLE;      static VkImageView ivHairVis0 = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairVis1 = VK_NULL_HANDLE;      static VkImageView ivHairVis1 = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairVis2 = VK_NULL_HANDLE;      static VkImageView ivHairVis2 = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairVis3 = VK_NULL_HANDLE;      static VkImageView ivHairVis3 = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairDepth = VK_NULL_HANDLE;     static VkImageView ivHairDepth = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairCoverage = VK_NULL_HANDLE;  static VkImageView ivHairCoverage = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairColor = VK_NULL_HANDLE;     static VkImageView ivHairColor = VK_NULL_HANDLE;
    static VkDescriptorSet dsHairDeepOpacity = VK_NULL_HANDLE; static VkImageView ivHairDeepOpacity = VK_NULL_HANDLE;

    if (VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen)
    {
        ImGui::Begin("Render Debug", &VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen);
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

        if (ImGui::BeginTable("HairDebugTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
        {
            ImGui::TableNextColumn();
            DisplayImage("Hair Color", renderPassManager->GetHairColor(), dsHairColor, ivHairColor);

            ImGui::TableNextColumn();
            DisplayImage("Hair Deep Opacity", renderPassManager->GetHairDeepOpacity(), dsHairDeepOpacity, ivHairDeepOpacity);

            ImGui::EndTable();
        }
        ImGui::End();
    }
}
