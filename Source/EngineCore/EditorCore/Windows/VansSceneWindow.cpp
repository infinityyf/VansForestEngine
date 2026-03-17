#include "VansSceneWindow.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <fstream>
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../VansTimer.h"

void VansGraphics::VansSceneWindow::ShowWindow(VansVKDevice& device)
{
    // -------------------------------------------------------------------------
    // 3. Scene 窗口 (游戏视图)
    // -------------------------------------------------------------------------
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene");

        // ImGuizmo must be told a new frame is starting once per ImGui frame.
        ImGuizmo::BeginFrame();

        // ── Gizmo mode toolbar ────────────────────────────────────────────────
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
            ImGui::Spacing();
            ImGui::Indent(4.0f);

            if (ImGui::RadioButton("T (Translate)##gizmo",
                m_Gizmos.m_Mode == GizmoMode::Translate))
                m_Gizmos.m_Mode = GizmoMode::Translate;
            ImGui::SameLine();

            if (ImGui::RadioButton("R (Rotate)##gizmo",
                m_Gizmos.m_Mode == GizmoMode::Rotate))
                m_Gizmos.m_Mode = GizmoMode::Rotate;
            ImGui::SameLine();

            if (ImGui::RadioButton("S (Scale)##gizmo",
                m_Gizmos.m_Mode == GizmoMode::Scale))
                m_Gizmos.m_Mode = GizmoMode::Scale;
            ImGui::SameLine();

            ImGui::Text("|");
            ImGui::SameLine();

            bool isWorld = (m_Gizmos.m_Space == GizmoSpace::World);
            if (ImGui::Checkbox("World##gizmo", &isWorld))
                m_Gizmos.m_Space = isWorld ? GizmoSpace::World : GizmoSpace::Local;

            ImGui::Unindent(4.0f);
            ImGui::Spacing();
            ImGui::PopStyleVar();
        }

        // 获取当前窗口可用区域大小
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();

        // TODO: 检测 viewportSize 是否变化，如果变化则通知渲染器调整 RenderTarget (Framebuffer) 的大小
        // if (viewportSize.x != m_LastWidth || viewportSize.y != m_LastHeight) { ResizeRenderTarget(...); }

       // 获取渲染结果图像 (假设使用 PostProcess 后的结果)
        auto renderPassManager = VansGraphics::VansRenderPassManager::GetInstance();
        VansVKImage& sceneImage = renderPassManager->GetColorAfterPostProcess();
        // 如果 PostProcess 未初始化，可回退到: renderPassManager->GetColor();

        // 静态缓存，防止每帧重复创建 DescriptorSet
        static VkDescriptorSet cachedSceneDS = VK_NULL_HANDLE;
        static VkImageView cachedImageView = VK_NULL_HANDLE;
        static VkSampler cachedSampler = VK_NULL_HANDLE;

        // 检测图像资源是否发生变化 (例如窗口大小改变导致 ImageView 重建)
        if (cachedImageView != sceneImage.GetImageView() || cachedSampler != sceneImage.GetSampler() || cachedSceneDS == VK_NULL_HANDLE)
        {
            cachedImageView = sceneImage.GetImageView();
            cachedSampler = sceneImage.GetSampler();

            // 使用 ImGui Vulkan 后端提供的辅助函数创建 DescriptorSet
            // 注意：ImGui 期望图像处于 SHADER_READ_ONLY_OPTIMAL 布局
            cachedSceneDS = ImGui_ImplVulkan_AddTexture(
                cachedSampler,
                cachedImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }

        VkDescriptorSet sceneTextureDS = cachedSceneDS;

        if (sceneTextureDS != VK_NULL_HANDLE)
        {
            // Preserve aspect ratio: fit the texture into the available viewport without stretching.
            // Query the actual image extent (fallback to swapchain extent if needed).
            ImVec2 avail = viewportSize;
            // Assume VansVKImage exposes image extent; if not, adapt to your API (e.g., swapchain extent).
            VkExtent3D imgExtent = sceneImage.GetImageDimension(); // width/height/depth
            float texW = (float)imgExtent.width;
            float texH = (float)imgExtent.height;

            // Compute aspect-fit size
            ImVec2 drawSize = avail;
            if (texW > 0.0f && texH > 0.0f)
            {
                float texAspect = texW / texH;
                float availAspect = avail.x / (avail.y > 0.0f ? avail.y : 1.0f);
                if (availAspect > texAspect)
                {
                    // Limit by height
                    drawSize.y = avail.y;
                    drawSize.x = drawSize.y * texAspect;
                }
                else
                {
                    // Limit by width
                    drawSize.x = avail.x;
                    drawSize.y = drawSize.x / texAspect;
                }
            }

            // Center the image within the window content region
            ImVec2 cursor = ImGui::GetCursorPos();
            ImVec2 offset = ImVec2((avail.x - drawSize.x) * 0.5f, (avail.y - drawSize.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(cursor.x + offset.x, cursor.y + offset.y));

            ImGui::Image((ImTextureID)sceneTextureDS, drawSize);

            // ── Gizmo overlay (drawn on top of the scene image) ───────────────
            // Use the exact screen-space rect of the rendered texture, not the
            // whole panel window, so gizmo clipping and NDC unprojection are accurate.
            ImVec2 imageScreenPos = ImGui::GetItemRectMin();
            m_Gizmos.HandleHotkeys(m_Scene);
            m_Gizmos.Draw(m_Scene, m_Camera, imageScreenPos, drawSize);

            // ── Object picking: LMB click when gizmo is not being dragged ─────
            if (ImGui::IsWindowHovered()
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGuizmo::IsOver())
            {
                m_Gizmos.TryPickObject(m_Scene, m_Camera,
                    ImGui::GetMousePos(), imageScreenPos, drawSize);
            }
        }

        // Input Handling
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
            float deltaTime = VansGraphics::VansTimer::GetDeltaTime();

            m_Camera->SetRightMouseDown(true);
            m_Camera->HandleMouseMovement(mouseDelta.x, mouseDelta.y);

            if (ImGui::IsKeyDown(ImGuiKey_W)) m_Camera->HandleKeyboardInput(GLFW_KEY_W, 0, GLFW_PRESS, 0, deltaTime);
            if (ImGui::IsKeyDown(ImGuiKey_S)) m_Camera->HandleKeyboardInput(GLFW_KEY_S, 0, GLFW_PRESS, 0, deltaTime);
            if (ImGui::IsKeyDown(ImGuiKey_A)) m_Camera->HandleKeyboardInput(GLFW_KEY_A, 0, GLFW_PRESS, 0, deltaTime);
            if (ImGui::IsKeyDown(ImGuiKey_D)) m_Camera->HandleKeyboardInput(GLFW_KEY_D, 0, GLFW_PRESS, 0, deltaTime);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) m_Camera->HandleKeyboardInput(GLFW_KEY_Q, 0, GLFW_PRESS, 0, deltaTime);
            if (ImGui::IsKeyDown(ImGuiKey_E)) m_Camera->HandleKeyboardInput(GLFW_KEY_E, 0, GLFW_PRESS, 0, deltaTime);
        }
        else
        {
            m_Camera->SetRightMouseDown(false);
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
