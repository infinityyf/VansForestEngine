#include "VansProjectWindow.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

std::filesystem::path VansGraphics::VansProjectWindow::m_CurrentSelectedFile = "";

void VansGraphics::VansProjectWindow::ShowWindow(VansVKDevice& device)
{
    // -------------------------------------------------------------------------
    // Project 窗口 (资源浏览器)
    // -------------------------------------------------------------------------
    {
        ImGui::Begin("Project");

        static std::filesystem::path currentPath = "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets";

        // Left Panel: Directory Tree
        ImGui::BeginChild("LeftPanel", ImVec2(200, 0), true);

        std::function<void(const std::filesystem::path&)> renderTree = [&](const std::filesystem::path& path) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_directory()) {
                    bool open = ImGui::TreeNode(entry.path().filename().string().c_str());
                    if (ImGui::IsItemClicked()) {
                        currentPath = entry.path();
                    }
                    if (open) {
                        renderTree(entry.path());
                        ImGui::TreePop();
                    }
                }
            }
            };

        if (std::filesystem::exists("D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets")) {
            if (ImGui::TreeNode("EngineAssets")) {
                if (ImGui::IsItemClicked()) {
                    currentPath = "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets";
                }
                renderTree("D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets");
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right Panel: File List
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);

        static float padding = 10.0f;
        static float thumbnailSize = 64.0f;
        float cellSize = thumbnailSize + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        if (ImGui::BeginTable("ProjectAssets", columnCount))
        {
            if (std::filesystem::exists(currentPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                    if (!entry.is_directory()) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(entry.path().string().c_str());

                        std::string filename = entry.path().filename().string();
                        if (ImGui::Button(filename.c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                            m_CurrentSelectedFile = entry.path();
                        }

                        ImGui::TextWrapped("%s", filename.c_str());
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::End();
    }
}
