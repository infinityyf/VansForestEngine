#include "VansSceneViewCommands.h"
#include "../SceneCore/VansSceneEntityFactory.h"
#include "VansEditorWindow.h"
#include "VansPrefabEditService.h"
#include "../AuthoringCore/VansAssetDocumentEditService.h"
#include "VansEditorAssetSaveService.h"
#include "VansEditorSelectionService.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneSchema.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../EngineAPILayer/Public/IPlayModeEditorAPI.h"
#include "../EngineAPILayer/Public/IRuntimeSceneEditorAPI.h"
#include "../EngineAPILayer/Public/ISceneInteractionEditorAPI.h"
#include "../Util/VansLog.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <algorithm>
#include <cstdint>

namespace VansGraphics
{
namespace
{
using Json = nlohmann::ordered_json;
using PrefabRequest = VansPrefabRequest;
using PrefabSession = VansEditorPrefabStage;
Json JsonOf(const Vans::VansSerializedValue& value) { return Vans::EncodeSerializedValueJson<Json>(value); }
}

bool VansEditorWindow::HasPrefabSession() { return m_PrefabSession.HasStage(); }
bool VansEditorWindow::HasPendingPrefabRequests() { return m_PrefabSession.HasPendingRequests(); }
std::string VansEditorWindow::ActiveDocumentToken()
{
    if (!GetSceneDocument()) return {};
    return JsonOf(GetSceneDocument()->SerializedRootSnapshot()).value("sceneGuid", std::string{});
}
void VansEditorWindow::QueuePrefabCreation(std::string entity, std::string directory, std::string token)
{
    m_PrefabSession.Queue({PrefabRequest::Kind::Create, std::move(entity), std::move(directory), std::move(token)});
}
void VansEditorWindow::QueuePrefabPlacement(std::string asset, std::string parent, float x, float y, float z)
{
    PrefabRequest request{PrefabRequest::Kind::Place, std::move(asset)};
    request.parent = std::move(parent); request.x = x; request.y = y; request.z = z;
    request.token = ActiveDocumentToken(); m_PrefabSession.Queue(std::move(request));
}
void VansEditorWindow::QueuePrefabDuplicate(std::string root) { m_PrefabSession.Queue({PrefabRequest::Kind::Duplicate, std::move(root), {}, ActiveDocumentToken()}); }
void VansEditorWindow::QueuePrefabDelete(std::string root) { m_PrefabSession.Queue({PrefabRequest::Kind::Delete, std::move(root), {}, ActiveDocumentToken()}); }

void VansEditorWindow::QueuePrefabOpen(std::string path)
{
    m_PrefabSession.Queue({PrefabRequest::Kind::Open, {}, std::move(path)});
}

bool VansEditorWindow::RefreshActiveScenePreview()
{
    if (!GetSceneDocument() || !GetEditorAPI()) return false;
	auto* session = m_PrefabSession.Stage();
	auto& status = m_PrefabSession.Status();
	auto& runtimeSceneAPI =
		static_cast<Vans::EditorAPI::IRuntimeSceneEditorAPI&>(*GetEditorAPI());
    Vans::EditorAPI::RuntimeSceneLoadRequest request;
    request.mode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
    request.ensureResourceDependencies = true;
    request.document.sourcePath = session ? session->asset->sourcePath.string() : GetSceneDocument()->SourcePath().string();
    auto preview = JsonOf(GetSceneDocument()->SerializedRootSnapshot());
    if (session)
    {
        // 灯光只属于运行预览，不进入作者文档、Hierarchy 或资产文件。
        auto light = JsonOf(Vans::VansSceneEntityFactory::BuildEmptyEntity({},
            Vans::VansAssetGuid::FromStableName(preview.at("sceneGuid"), "prefab-preview-light").ToString()));
        light["name"] = "Prefab Preview Light";
        light["components"][0]["data"]["rotation"] = {-0.3535534, 0.3535534, 0.1464466, 0.8535534};
        light["components"].push_back({{"id", Vans::VansAssetGuid::FromStableName(preview.at("sceneGuid"), "prefab-preview-light-component").ToString()},
            {"type", "DirectionalLight"}, {"version", 1}, {"enabled", true}, {"data", {{"color", {1.0, 0.98, 0.95}}, {"intensity", 3.0}}}});
        preview["entities"].push_back(std::move(light));
    }
    request.document.canonicalJson = preview.dump();
    request.document.authoringStateId = GetSceneDocument()->CurrentStateId();
    const auto result = runtimeSceneAPI.LoadRuntimeScene(request);
    if (!result)
    {
        status = "Prefab preview failed";
        for (const auto& diagnostic : result.diagnostics) status += ": " + diagnostic.message;
        return false;
    }
    DetachEditorViewportCamerasFromSceneTransforms();
    return true;
}

bool VansEditorWindow::SavePrefabSession()
{
	auto* session = m_PrefabSession.Stage();
	auto& status = m_PrefabSession.Status();
    if (!session || !GetSceneDocument()) return false;
    const auto scene = JsonOf(GetSceneDocument()->SerializedRootSnapshot());
    Vans::VansPrefabAsset asset{session->root, Vans::DecodeSerializedValueJson(scene.at("entities"))};
    if (!Vans::VansPrefabCodec::Validate(asset, status)) return false;
    const auto next = Vans::VansPrefabCodec::Encode(asset);
    const bool changedSource = JsonOf(next) != JsonOf(session->asset->sourceDocument.SerializedRootSnapshot());
    if (changedSource)
    {
        const auto edit = Vans::VansAssetDocumentEditService::ReplaceRoot(session->asset->sourceDocument, next);
        if (!edit) { status = edit.message; return false; }
    }
    const auto result = Vans::VansEditorAssetSaveService::Get().SaveAsset(*GetEditorAPI(), session->asset);
    if (!result)
    {
        status = result.message;
        if (changedSource)
        {
            const auto rollback = Vans::VansAssetDocumentEditService::Undo(session->asset->sourceDocument);
            if (!rollback) status += "; working copy rollback failed: " + rollback.message;
        }
        return false;
    }
    session->savedState = GetSceneDocument()->CurrentStateId();
    status = "Prefab saved";
    return true;
}

void VansEditorWindow::DrawPrefabToolbar()
{
	auto* session = m_PrefabSession.Stage();
	auto& status = m_PrefabSession.Status();
	auto& toolbarCache = m_PrefabSession.ToolbarCache();
    if (session)
    {
        ImGui::Text("Prefab: %s%s", session->asset->sourcePath.filename().string().c_str(),
            GetSceneDocument() && GetSceneDocument()->CurrentStateId() != session->savedState ? " *" : "");
        if (ImGui::Button("Save Prefab")) SavePrefabSession();
        ImGui::SameLine();
        if (ImGui::Button("Back to Scene")) m_PrefabSession.Queue({PrefabRequest::Kind::Close});
        ImGui::SameLine();
        if (ImGui::Button("Discard")) m_PrefabSession.Queue({PrefabRequest::Kind::Discard});
    }
    else if (GetSceneDocument())
    {
        const auto selected = Vans::VansEditorSelectionService::Get().EntityGuid();
        const auto selectionRevision = Vans::VansEditorSelectionService::Get().Snapshot().revision;
        const auto documentSnapshot = GetSceneDocument()->CreateSnapshot();
        const bool cacheDirty = !toolbarCache.valid
            || toolbarCache.document != GetSceneDocument()
            || toolbarCache.authoringRoot != documentSnapshot.authoringRoot
            || toolbarCache.documentState != GetSceneDocument()->CurrentStateId()
            || toolbarCache.selectionRevision != selectionRevision
            || toolbarCache.selectedEntity != selected;
        if (cacheDirty)
        {
            toolbarCache.document = GetSceneDocument();
            toolbarCache.authoringRoot = documentSnapshot.authoringRoot;
            toolbarCache.documentState = GetSceneDocument()->CurrentStateId();
            toolbarCache.selectionRevision = selectionRevision;
            toolbarCache.selectedEntity = selected;
            toolbarCache.sourceAsset = Vans::VansPrefabEditService::SourceAsset(*GetSceneDocument(), selected);
            toolbarCache.valid = true;
        }
        if (!toolbarCache.sourceAsset.empty())
        {
            ImGui::TextUnformatted("Prefab Instance");
            if (ImGui::Button("Apply Overrides")) m_PrefabSession.Queue({PrefabRequest::Kind::Apply, selected});
            ImGui::SameLine();
            if (ImGui::Button("Revert Overrides")) m_PrefabSession.Queue({PrefabRequest::Kind::Revert, selected});
            ImGui::SameLine();
            if (ImGui::Button("Unpack")) m_PrefabSession.Queue({PrefabRequest::Kind::Unpack, selected});
        }
    }
    if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
}

void VansEditorWindow::ProcessPrefabRequests()
{
    if (!m_PrefabSession.HasPendingRequests()) return;
    auto* api = GetEditorAPI();
    if (!api) return;
	Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI = *api;
	Vans::EditorAPI::ISceneInteractionEditorAPI& sceneInteractionAPI = *api;
	Vans::EditorAPI::IPlayModeEditorAPI& playModeAPI = *api;
    auto pending = m_PrefabSession.TakePendingRequests();
    if (playModeAPI.GetPlayState() != Vans::EditorAPI::EnginePlayState::Edit) return;
    for (const auto& request : pending)
    {
		auto& status = m_PrefabSession.Status();
        try
        {
            auto* session = m_PrefabSession.Stage();
            status.clear();
            if (request.kind == PrefabRequest::Kind::Open)
            {
                if (session) { status = "Close the current Prefab before opening another"; continue; }
                auto assetDocument = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(request.path);
                Vans::VansPrefabAsset asset;
                if (!assetDocument || !Vans::VansPrefabCodec::Decode(assetDocument->sourceDocument.SerializedRootSnapshot(), asset, status)) continue;
                Vans::VansSceneData data; data.sceneGuid = Vans::VansAssetGuid::New();
                data.settings = Vans::VansSceneSchema::MakeDefaultSettings();
                auto stage = Vans::VansSceneSchema::SerializeSceneJson(data);
                stage["entities"] = JsonOf(asset.entities);
                auto document = Vans::VansSceneDocument::CreateInMemory(Vans::DecodeSerializedValueJson(stage), Vans::VansPrefabEditService::Lookup(*api), status);
                if (!document) continue;
                auto candidate = std::make_unique<PrefabSession>();
                candidate->asset = assetDocument; candidate->root = asset.rootEntity;
                candidate->previousSelection = Vans::VansEditorSelectionService::Get().Snapshot();
                candidate->previousCamera = sceneInteractionAPI.CaptureEditorViewportCamera();
                candidate->savedState = document->CurrentStateId();
                candidate->previousSceneState = m_SceneDocumentSession.ReplaceDocument(
                    std::move(document),
                    [] { return RefreshActiveScenePreview(); });
                m_PrefabSession.Begin(std::move(candidate));
                session = m_PrefabSession.Stage();
                if (!RefreshActiveScenePreview())
                {
                    const auto camera = session->previousCamera;
                    auto previousSceneState = std::move(session->previousSceneState);
                    m_SceneDocumentSession.Restore(std::move(previousSceneState));
                    m_PrefabSession.End();
                    session = nullptr;
                    if (GetSceneDocument()) RefreshActiveScenePreview();
                    else runtimeSceneAPI.UnloadRuntimeScene();
                    sceneInteractionAPI.RestoreEditorViewportCamera(camera);
                    VANS_LOG_ERROR("[Prefab] Stage open failed: " << status); continue;
                }
                Vans::VansEditorSelectionService::Get().SelectEntity(asset.rootEntity, "PrefabStage");
                Vans::VansSceneViewCommands::RequestFrameSelection();
                VANS_LOG("[Prefab] Opened isolated stage: " << request.path << " entities=" << asset.entities.arrayItems.size());
                continue;
            }
            if (request.kind == PrefabRequest::Kind::Close || request.kind == PrefabRequest::Kind::Discard)
            {
                if (!session) continue;
                if (request.kind == PrefabRequest::Kind::Close && GetSceneDocument()->CurrentStateId() != session->savedState)
                { status = "Save Prefab or choose Discard before returning to the scene"; continue; }
                if (session->previousSceneState.Document() &&
                    !session->previousSceneState.Document()->RefreshPrefabView(status)) continue;
                auto selection = session->previousSelection;
                const auto camera = session->previousCamera;
                auto previousSceneState = std::move(session->previousSceneState);
                m_SceneDocumentSession.Restore(std::move(previousSceneState));
                m_PrefabSession.End();
                session = nullptr;
                if (GetSceneDocument())
                {
                    RefreshActiveScenePreview();
                }
                else runtimeSceneAPI.UnloadRuntimeScene();
                Vans::VansSceneViewCommands::Clear();
                sceneInteractionAPI.RestoreEditorViewportCamera(camera);
                Vans::VansEditorSelectionService::Get().Apply(Vans::EditorSelectionOperation::Replace, selection.objects, selection.active, "PrefabStageReturn");
                continue;
            }
            if (!GetSceneDocument() || !GetSceneEditService()) { status = "Open a scene first"; continue; }
            if (!request.token.empty() && request.token != ActiveDocumentToken()) { status = "The dragged object belongs to another document"; continue; }
            Vans::SceneEditResult result;
            const auto refresh = [] { return VansEditorWindow::RefreshActiveScenePreview(); };
            Vans::SceneEditLifecycleHooks hooks{refresh, refresh, refresh};
            if (request.kind == PrefabRequest::Kind::Create)
            {
                if (session) { status = "Create Prefab assets from a scene"; continue; }
                std::filesystem::path path;
                result = Vans::VansPrefabEditService::Create(*api, *GetSceneDocument(), *GetSceneEditService(), request.target, request.path, path);
                if (result) status = "Created " + path.filename().string();
            }
            else if (request.kind == PrefabRequest::Kind::Place)
            {
                if (session) { status = "Nested Prefabs are not supported"; continue; }
                Json placement{{"parent", nullptr}, {"position", {request.x, request.y, request.z}}, {"rotation", {0,0,0,1}}};
                if (!request.parent.empty()) placement["parent"] = {{"kind", "entity"}, {"entityGuid", request.parent}};
                result = Vans::VansPrefabEditService::Place(*GetSceneDocument(), *GetSceneEditService(), request.target, Vans::DecodeSerializedValueJson(placement), hooks);
            }
            else if (request.kind == PrefabRequest::Kind::Apply)
            {
                result = Vans::VansPrefabEditService::Apply(*api, *GetSceneDocument(), *GetSceneEditService(), request.target);
                if (result) status = "Overrides applied in memory; Save Scene / Save All saves the template";
            }
            else if (request.kind == PrefabRequest::Kind::Duplicate)
            {
                Vans::VansSerializedValue duplicated; std::string root, error;
                if (!Vans::VansPrefabResolver::DuplicateSubtree(GetSceneDocument()->SerializedRootSnapshot(), GetSceneDocument()->PrefabLookup(), request.target, duplicated, root, error))
                    result = {false, error};
                else
                {
                    result = GetSceneEditService()->ReplaceRoot(std::move(duplicated), hooks);
                    if (result) Vans::VansEditorSelectionService::Get().SelectEntity(root, "PrefabDuplicate");
                }
            }
            else if (request.kind == PrefabRequest::Kind::Delete)
            {
                auto scene = JsonOf(GetSceneDocument()->SerializedRootSnapshot());
                Vans::VansSerializedValue removed; std::string error;
                if (!Vans::ExtractSceneObjectSubtree(Vans::DecodeSerializedValueJson(scene["entities"]), request.target, removed, error)) result = {false, error};
                else
                {
                    std::unordered_set<std::string> ids;
                    for (const auto& object : JsonOf(removed)) ids.insert(object.at("id"));
                    auto& objects = scene["entities"];
                    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const auto& object) { return ids.count(object.at("id")); }), objects.end());
                    result = GetSceneEditService()->ReplaceRoot(Vans::DecodeSerializedValueJson(scene), hooks);
                    if (result) Vans::VansEditorSelectionService::Get().Clear("PrefabDelete");
                }
            }
            else if (request.kind == PrefabRequest::Kind::Unpack)
                result = Vans::VansPrefabEditService::Unpack(*GetSceneDocument(), *GetSceneEditService(), request.target);
            else if (request.kind == PrefabRequest::Kind::Revert)
                result = Vans::VansPrefabEditService::Revert(*GetSceneDocument(), *GetSceneEditService(), request.target, hooks);
            if (!result) status = result.message;
        }
        catch (const std::exception& e) { status = e.what(); }
        if (!status.empty()) VANS_LOG("[Prefab] " << status);
    }
}
}
