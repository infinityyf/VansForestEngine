#include "VansProjectWindow.h"
#include "../VansEditorWindow.h"
#include "../../AuthoringCore/VansEditorObjectReference.h"
#include "../../SceneCore/VansSceneDocumentLoader.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../../EngineAPILayer/Public/IAssetAuthoringEditorAPI.h"
#include "../../EngineAPILayer/Public/IAssetEditorAPI.h"
#include "../../Util/VansLog.h"
#include "../VansEditorSelectionService.h"
#include "imgui.h"
#include <filesystem>
#include <functional>
#include <string>

namespace
{
bool IsDirectoryInsideRoot(const std::filesystem::path& directory,
                           const std::filesystem::path& root)
{
    std::error_code directoryError;
    std::error_code rootError;
    const std::filesystem::path canonicalDirectory =
        std::filesystem::weakly_canonical(directory, directoryError);
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(root, rootError);
    if (directoryError || rootError || !std::filesystem::is_directory(canonicalDirectory))
        return false;
    if (canonicalDirectory == canonicalRoot)
        return true;

    const std::filesystem::path relative = canonicalDirectory.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute())
        return false;
    for (const std::filesystem::path& part : relative)
        if (part == "..")
            return false;
    return true;
}

}

void VansGraphics::VansProjectWindow::RequestAssetCreation(
    Vans::EditorAPI::ProjectAssetCreationKind kind)
{
    m_PendingAssetCreation = kind;
    m_HasPendingAssetCreation = true;
}

void VansGraphics::VansProjectWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI = editorAPI;
	Vans::EditorAPI::IAssetEditorAPI& assetAPI = editorAPI;
    DrawProjectContents(assetAuthoringAPI, assetAPI);
}

std::filesystem::path VansGraphics::VansProjectWindow::ResolveAssetCreationDirectory(
    const Vans::EditorAPI::ProjectBrowserRootSnapshot& root) const
{
    if (!root.projectLoaded || root.rootPath.empty())
        return {};

    const std::filesystem::path projectRoot(root.rootPath);
    if (IsDirectoryInsideRoot(m_CurrentDirectory, projectRoot))
        return m_CurrentDirectory;
    if (IsDirectoryInsideRoot(projectRoot, projectRoot))
        return projectRoot;
    return {};
}

void VansGraphics::VansProjectWindow::ProcessAssetCreation(
    Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI,
    const Vans::EditorAPI::ProjectBrowserRootSnapshot& root)
{
    if (!m_HasPendingAssetCreation)
        return;
    const Vans::EditorAPI::ProjectAssetCreationKind request = m_PendingAssetCreation;
    m_HasPendingAssetCreation = false;

    const std::filesystem::path targetDirectory = ResolveAssetCreationDirectory(root);
    if (targetDirectory.empty())
    {
        VANS_LOG_ERROR("[Asset] Open a project and select a valid asset folder before creating an asset");
        return;
    }

    if (request == Vans::EditorAPI::ProjectAssetCreationKind::Timeline)
    {
        m_TimelineCreationDirectory = targetDirectory;
        m_TimelineAssetCreateStatus.clear();
        m_OpenTimelineCreationPopup = true;
        return;
    }

    Vans::EditorAPI::ProjectAssetCreateRequest createRequest;
    createRequest.directoryPath = targetDirectory.string();
    createRequest.kind = request;
    const Vans::EditorAPI::ProjectAssetCreateResult creation =
        assetAuthoringAPI.CreateProjectAsset(createRequest);
    if (!creation.success)
    {
        VANS_LOG_ERROR("[Asset] " << (creation.message.empty()
            ? "Project asset creation failed" : creation.message));
        return;
    }

    const std::filesystem::path createdPath(creation.assetPath);
    const Vans::EditorAPI::AssetRefreshResult refresh =
        assetAuthoringAPI.RefreshProjectAsset(createdPath.string(), true);
    if (!refresh.success)
    {
        VANS_LOG_ERROR("[Asset] Created " << createdPath.string()
            << ", but project refresh failed: " << refresh.message);
        return;
    }

    Vans::VansEditorSelectionService::Get().SelectAsset(createdPath, "ProjectWindow");
    if (request == Vans::EditorAPI::ProjectAssetCreationKind::AnimatorController ||
        request == Vans::EditorAPI::ProjectAssetCreationKind::BoneMask)
        VansEditorWindow::OpenAnimationAsset(createdPath.string());
	else if (Vans::VansGameplayAssetSchemaRegistry::IsGameplayAssetType(
		Vans::VansAssetDatabase::Classify(createdPath)))
		VansEditorWindow::OpenAssetForAuthoring(createdPath.string());
    VANS_LOG("[Asset] Created " << createdPath.string());
}

void VansGraphics::VansProjectWindow::DrawTimelineCreationPopup(
    Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI)
{
    if (m_OpenTimelineCreationPopup)
    {
        ImGui::OpenPopup("Create Timeline");
        m_OpenTimelineCreationPopup = false;
    }

    if (!ImGui::BeginPopupModal("Create Timeline", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", m_TimelineName, sizeof(m_TimelineName));
    if (!m_TimelineAssetCreateStatus.empty())
        ImGui::TextUnformatted(m_TimelineAssetCreateStatus.c_str());

    if (ImGui::Button("Create"))
    {
        const std::string timelineName(m_TimelineName);
        if (timelineName.empty())
        {
            m_TimelineAssetCreateStatus = "Timeline name is required";
        }
        else
        {
            Vans::EditorAPI::ProjectAssetCreateRequest createRequest;
            createRequest.directoryPath = m_TimelineCreationDirectory.string();
            createRequest.kind = Vans::EditorAPI::ProjectAssetCreationKind::Timeline;
            createRequest.name = timelineName;
            const Vans::EditorAPI::ProjectAssetCreateResult creation =
                assetAuthoringAPI.CreateProjectAsset(createRequest);
            if (!creation.success)
                m_TimelineAssetCreateStatus = creation.message.empty()
                    ? "Timeline creation failed" : creation.message;
            else
            {
                const std::filesystem::path createdPath(creation.assetPath);
                const Vans::EditorAPI::AssetRefreshResult refresh =
                    assetAuthoringAPI.RefreshProjectAsset(createdPath.string(), true);
                if (!refresh.success)
                    m_TimelineAssetCreateStatus = refresh.message;
                else
                {
                    Vans::VansEditorSelectionService::Get().SelectAsset(createdPath, "ProjectWindow");
                    VansEditorWindow::OpenAssetForAuthoring(createdPath.string());
                    VANS_LOG("[Asset] Created " << createdPath.string());
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void VansGraphics::VansProjectWindow::DrawProjectContents(
	Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI,
	Vans::EditorAPI::IAssetEditorAPI& assetAPI)
{
    // -------------------------------------------------------------------------
    // Project 窗口 (资源浏览器)
    // -------------------------------------------------------------------------
    {
        ImGui::Begin("Project");

        // Determine the browsing root: project directory when loaded,
        // otherwise fall back to the engine's EngineAssets directory.
        const Vans::EditorAPI::ProjectBrowserRootSnapshot root = assetAuthoringAPI.GetProjectBrowserRoot();
        const std::string& rootPath = root.rootPath;
        const std::string& rootLabel = root.rootLabel;

        // 项目切换后立即丢弃旧目录，避免全局 Asset 菜单在旧项目中创建资产。
        if (m_CachedRootPath != rootPath)
        {
            m_CachedRootPath = rootPath;
            m_CurrentDirectory = root.projectLoaded ? root.assetsRootPath : rootPath;
        }

        auto acceptPrefabCreation = [&](const std::filesystem::path& directory)
        {
            if (!root.projectLoaded || !ImGui::BeginDragDropTarget()) return;
            if (const auto* payload = ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType))
            {
                Vans::EditorObjectHandle object;
                if (Vans::TryDeserializeEditorObjectHandle(payload->Data, static_cast<std::size_t>(payload->DataSize), object) &&
                    object.domain == Vans::EditorObjectDomain::SceneEntity)
                    VansEditorWindow::QueuePrefabCreation(object.entityGuid, directory.string(), object.path);
            }
            ImGui::EndDragDropTarget();
        };

        // Left Panel: Directory Tree
        ImGui::BeginChild("LeftPanel", ImVec2(200, 0), true);

        std::function<void(const std::filesystem::path&)> renderTree = [&](const std::filesystem::path& path) {
            if (!std::filesystem::exists(path)) return;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_directory()) {
                    bool open = ImGui::TreeNode(entry.path().filename().string().c_str());
                    acceptPrefabCreation(entry.path());
                    if (ImGui::IsItemClicked()) {
                        m_CurrentDirectory = entry.path();
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
                    m_CurrentDirectory = rootPath;
                }
                renderTree(rootPath);
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right Panel: File List
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);

        ProcessAssetCreation(assetAuthoringAPI, root);
        DrawTimelineCreationPopup(assetAuthoringAPI);

        static float padding = 10.0f;
        static float thumbnailSize = 64.0f;
        float cellSize = thumbnailSize + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        if (ImGui::BeginTable("ProjectAssets", columnCount))
        {
            if (std::filesystem::exists(m_CurrentDirectory)) {
                for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
                    if (!entry.is_directory()) {
                        if (entry.path().extension() == ".meta")
                            continue;
                        ImGui::TableNextColumn();
                        ImGui::PushID(entry.path().string().c_str());

                        std::string filename = entry.path().filename().string();
                        if (ImGui::Button(filename.c_str(), ImVec2(thumbnailSize, thumbnailSize))) {
                            Vans::VansEditorSelectionService::Get().SelectAsset(entry.path(), "ProjectWindow");
                        }

                        if (root.projectLoaded && ImGui::BeginDragDropSource())
                        {
                            const Vans::EditorAPI::AssetDragPayload payload =
                                assetAPI.CreateAssetDragPayload(entry.path().string());
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
							const Vans::VansAssetType assetType =
								Vans::VansAssetDatabase::Classify(entry.path());
							const bool gameplayAsset = Vans::VansGameplayAssetSchemaRegistry::IsGameplayAssetType(
								assetType);
							if (assetType == Vans::VansAssetType::AnimatorController ||
								assetType == Vans::VansAssetType::BoneMask ||
								assetType == Vans::VansAssetType::Timeline ||
								assetType == Vans::VansAssetType::Prefab || gameplayAsset)
							{
								VansEditorWindow::OpenAssetForAuthoring(entry.path().string());
							}
							else
							{
							std::string readError;
							if (Vans::VansSceneDocumentLoader::IsSceneDocumentFile(entry.path(), &readError))
							{
								std::string scenePath = entry.path().string();
								VANS_LOG("[Project] Deferring Scene load: " << scenePath);
								VansEditorWindow::RequestSceneLoad(scenePath);
							}
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
        acceptPrefabCreation(m_CurrentDirectory);

        ImGui::End();
    }
}
