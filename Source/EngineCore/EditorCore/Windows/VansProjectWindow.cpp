#include "VansProjectWindow.h"
#include "../VansEditorWindow.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../Util/VansLog.h"
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

        // Determine the browsing root: project directory when loaded,
        // otherwise fall back to the engine's EngineAssets directory.
        auto& projectMgr = Vans::VansProjectManager::Get();
        std::string rootPath;
        std::string rootLabel;

        if (projectMgr.IsProjectLoaded())
        {
            rootPath  = projectMgr.GetProjectRootPath();
            rootLabel = projectMgr.GetProjectName();
        }
        else
        {
            auto vansConfigration = VansConfigration::GetInstance();
            rootPath  = vansConfigration->GetProjectRootPath() + "EngineAssets";
            rootLabel = "EngineAssets";
        }

        static std::filesystem::path currentPath = "";
        // Reset currentPath when the root changes (e.g. project just opened)
        static std::string cachedRoot;
        if (cachedRoot != rootPath)
        {
            cachedRoot  = rootPath;
            currentPath = rootPath;
        }

        // Left Panel: Directory Tree
        ImGui::BeginChild("LeftPanel", ImVec2(200, 0), true);

        std::function<void(const std::filesystem::path&)> renderTree = [&](const std::filesystem::path& path) {
            if (!std::filesystem::exists(path)) return;
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

        if (std::filesystem::exists(rootPath)) {
            if (ImGui::TreeNode(rootLabel.c_str())) {
                if (ImGui::IsItemClicked()) {
                    currentPath = rootPath;
                }
                renderTree(rootPath);
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
                        bool isJson = entry.path().extension() == ".json";

                        if (ImGui::Button(filename.c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                            m_CurrentSelectedFile = entry.path();
                        }

                        // Double-click a .json file to defer-load it as a scene
                        if (isJson && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            std::string scenePath = entry.path().string();
                            VANS_LOG("[Project] Deferring scene load: " << scenePath);
                            VansEditorWindow::m_PendingScenePath = scenePath;
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
