#include "VansProjectWindow.h"
#include "../VansEditorWindow.h"
#include "../VansEditorObjectReference.h"
#include "../../AudioCore/Storage/VansAudioBusSnapshotAssetStorage.h"
#include "../../AudioCore/Storage/VansAudioDuckingRulesAssetStorage.h"
#include "../../AudioCore/Storage/VansAudioReverbPresetAssetStorage.h"
#include "../../AudioCore/VansAudioBusSnapshotAsset.h"
#include "../../AudioCore/VansAudioDuckingRulesAsset.h"
#include "../../AudioCore/VansAudioReverbPresetAsset.h"
#include "../../AudioCore/VansAudioReverbPreset.h"
#include "../../SceneCore/VansSceneDocumentLoader.h"
#include "../../Util/VansLog.h"
#include "../VansEditorSelection.h"
#include "imgui.h"
#include <filesystem>
#include <functional>
#include <string>

namespace
{
enum class AudioControlAssetTemplate
{
    ReverbPreset,
    BusSnapshot,
    DuckingRules
};

std::filesystem::path MakeUniqueAssetPath(
    const std::filesystem::path& directory,
    const std::string& baseName,
    const std::string& extension)
{
    std::filesystem::path candidate = directory / (baseName + extension);
    if (!std::filesystem::exists(candidate))
        return candidate;

    for (int index = 1; index < 1000; ++index)
    {
        candidate = directory / (baseName + " " + std::to_string(index) + extension);
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
    return {};
}

std::filesystem::path CreateAudioControlAsset(
    const std::filesystem::path& directory,
    AudioControlAssetTemplate assetTemplate,
    std::string& error)
{
    error.clear();
    if (directory.empty())
    {
        error = "No target folder selected";
        return {};
    }

    switch (assetTemplate)
    {
    case AudioControlAssetTemplate::ReverbPreset:
    {
        const std::filesystem::path path = MakeUniqueAssetPath(directory, "Room Reverb", ".vreverb");
        if (path.empty())
        {
            error = "Cannot allocate a unique reverb preset name";
            return {};
        }

        Vans::VansAudioReverbPresetAsset asset;
        asset.displayName = "Room Reverb";
        asset.parameters = VansEngine::GetAudioReverbPresetParameters(VansEngine::AudioReverbPreset::Room);
        return Vans::VansAudioReverbPresetAssetStorage::SaveAtomic(path, asset, error) ? path : std::filesystem::path{};
    }
    case AudioControlAssetTemplate::BusSnapshot:
    {
        const std::filesystem::path path = MakeUniqueAssetPath(directory, "Gameplay Mix", ".vaudiosnapshot");
        if (path.empty())
        {
            error = "Cannot allocate a unique bus snapshot name";
            return {};
        }

        Vans::VansAudioBusSnapshotAsset asset;
        asset.displayName = "Gameplay Mix";
        asset.snapshot.fadeSeconds = 0.25f;
        asset.snapshot.buses = {
            VansEngine::AudioBusSnapshotEntry{ "Music", 0.8f },
            VansEngine::AudioBusSnapshotEntry{ "SFX", 1.0f },
            VansEngine::AudioBusSnapshotEntry{ "Voice", 1.0f }
        };
        return Vans::VansAudioBusSnapshotAssetStorage::SaveAtomic(path, asset, error) ? path : std::filesystem::path{};
    }
    case AudioControlAssetTemplate::DuckingRules:
    {
        const std::filesystem::path path = MakeUniqueAssetPath(directory, "Voice Ducking", ".vducking");
        if (path.empty())
        {
            error = "Cannot allocate a unique ducking rules name";
            return {};
        }

        Vans::VansAudioDuckingRulesAsset asset;
        asset.displayName = "Voice Ducking";
        asset.rules = { VansEngine::AudioDuckingRule{} };
        return Vans::VansAudioDuckingRulesAssetStorage::SaveAtomic(path, asset, error) ? path : std::filesystem::path{};
    }
    default:
        error = "Unsupported audio control asset template";
        return {};
    }
}
}

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

        static std::string s_AudioAssetCreateStatus;
        auto createAndSelectAudioAsset = [&](AudioControlAssetTemplate assetTemplate)
        {
            std::string error;
            const std::filesystem::path createdPath = CreateAudioControlAsset(currentPath, assetTemplate, error);
            if (createdPath.empty())
            {
                s_AudioAssetCreateStatus = error.empty() ? "Audio asset creation failed" : error;
                VANS_LOG_ERROR("[Project] " << s_AudioAssetCreateStatus);
                return;
            }

            const Vans::EditorAPI::AssetRefreshResult refresh =
                editorAPI.RefreshProjectAsset(createdPath.string(), true);
            if (!refresh.success)
            {
                s_AudioAssetCreateStatus = "Created, but asset refresh failed: " + refresh.message;
                VANS_LOG_ERROR("[Project] " << s_AudioAssetCreateStatus);
                return;
            }

            Vans::VansEditorSelection::SelectAsset(createdPath);
            s_AudioAssetCreateStatus = "Created " + createdPath.filename().string();
            VANS_LOG("[Project] " << s_AudioAssetCreateStatus);
        };

        if (root.projectLoaded)
        {
            if (ImGui::Button("Create Audio"))
                ImGui::OpenPopup("CreateAudioAssetPopup");
            if (ImGui::BeginPopup("CreateAudioAssetPopup"))
            {
                if (ImGui::MenuItem("Reverb Preset"))
                    createAndSelectAudioAsset(AudioControlAssetTemplate::ReverbPreset);
                if (ImGui::MenuItem("Bus Snapshot"))
                    createAndSelectAudioAsset(AudioControlAssetTemplate::BusSnapshot);
                if (ImGui::MenuItem("Ducking Rules"))
                    createAndSelectAudioAsset(AudioControlAssetTemplate::DuckingRules);
                ImGui::EndPopup();
            }
            if (!s_AudioAssetCreateStatus.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", s_AudioAssetCreateStatus.c_str());
            }
        }

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
