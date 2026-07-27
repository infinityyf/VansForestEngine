#include "VansProjectWindow.h"
#include "../VansEditorWindow.h"
#include "../VansEditorObjectReference.h"
#include "../../SceneCore/VansSceneDocumentLoader.h"
#include "../../Util/VansLog.h"
#include "../VansEditorSelection.h"
#include "imgui.h"
#include <filesystem>
#include <functional>
#include <string>

void VansGraphics::VansProjectWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    DrawProjectContents(editorAPI);
}

void VansGraphics::VansProjectWindow::DrawProjectContents(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    // -------------------------------------------------------------------------
    // Project 窗口 (资源浏览器)
    // -------------------------------------------------------------------------
    {
        ImGui::Begin("Project");

        // Determine the browsing root: project directory when loaded,
        // otherwise fall back to the engine's EngineAssets directory.
        const Vans::EditorAPI::ProjectBrowserRootSnapshot root = editorAPI.GetProjectBrowserRoot();
        const std::string& rootPath = root.rootPath;
        const std::string& rootLabel = root.rootLabel;

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
                        if (ImGui::Button(filename.c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                            Vans::VansEditorSelection::SelectAsset(entry.path());
                        }

                        if (root.projectLoaded && ImGui::BeginDragDropSource())
                        {
                            const Vans::EditorAPI::AssetDragPayload payload =
                                editorAPI.CreateAssetDragPayload(entry.path().string());
                            if (payload.available)
                            {
                                Vans::EditorObjectHandle handle;
                                handle.domain = Vans::EditorObjectDomain::ProjectAsset;
                                handle.guid = payload.guid;
                                handle.path = payload.sourcePath.empty()
                                    ? entry.path().string()
                                    : payload.sourcePath;
                                handle.displayName = payload.displayName.empty()
                                    ? filename
                                    : payload.displayName;
                                handle.assetType = payload.assetType;
                                const std::string objectRefPayload = Vans::SerializeEditorObjectHandle(handle);
                                ImGui::SetDragDropPayload(Vans::VansObjectReferenceDragPayloadType,
                                    objectRefPayload.c_str(),
                                    objectRefPayload.size() + 1);
                                ImGui::TextUnformatted(filename.c_str());
                            }
                            else if (!payload.error.empty())
                                ImGui::TextUnformatted(payload.error.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
							std::string readError;
							if (Vans::VansSceneDocumentLoader::IsSceneDocumentFile(entry.path(), &readError))
							{
								std::string scenePath = entry.path().string();
								VANS_LOG("[Project] Deferring Scene load: " << scenePath);
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
