#include "VansProjectWindow.h"
#include "../VansEditorWindow.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../Util/VansLog.h"
#include "../VansEditorSelection.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

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
                        if (entry.path().extension() == ".meta")
                            continue;
                        ImGui::TableNextColumn();
                        ImGui::PushID(entry.path().string().c_str());

                        std::string filename = entry.path().filename().string();
                        const bool isSceneCandidate = entry.path().extension() == ".json";

                        if (ImGui::Button(filename.c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                            Vans::VansEditorSelection::SelectAsset(entry.path());
                        }

                        if (projectMgr.IsProjectLoaded() && ImGui::BeginDragDropSource())
                        {
                            if (Vans::VansAssetDatabase* database = projectMgr.GetAssetDatabase())
                            {
                                std::string registrationError;
                                if (!database->Find(entry.path()))
                                    database->RegisterOrRefresh(entry.path(), true, registrationError);
                                if (const auto record = database->Find(entry.path()))
                                {
                                    const std::string guid = record->guid.ToString();
                                    ImGui::SetDragDropPayload("VANS_ASSET_GUID", guid.c_str(), guid.size() + 1);
                                    ImGui::TextUnformatted(filename.c_str());
                                }
                                else if (!registrationError.empty())
                                    ImGui::TextUnformatted(registrationError.c_str());
                            }
                            ImGui::EndDragDropSource();
                        }

                        if (isSceneCandidate && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
							std::ifstream input(entry.path());
							nlohmann::json document = nlohmann::json::parse(input, nullptr, false);
							if (document.is_object() && document.value("schemaVersion", 0u) == 2u)
							{
								std::string scenePath = entry.path().string();
								VANS_LOG("[Project] Deferring Scene v2 load: " << scenePath);
								VansEditorWindow::m_PendingScenePath = scenePath;
							}
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
