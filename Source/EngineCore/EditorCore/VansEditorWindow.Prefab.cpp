#include "VansSceneViewCommands.h"
#include "../SceneCore/VansSceneEntityFactory.h"
#include "VansEditorWindow.h"
#include "VansPrefabEditService.h"
#include "VansAssetDocumentEditService.h"
#include "VansEditorAssetSaveService.h"
#include "VansEditorSelectionService.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneSchema.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../Util/VansLog.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <algorithm>

namespace VansGraphics
{
namespace
{
using Json = nlohmann::ordered_json;
struct PrefabRequest
{
    enum class Kind { Create, Place, Open, Close, Discard, Unpack, Revert, Apply, Duplicate, Delete } kind;
    std::string target, path, token, parent;
    float x = 0, y = 0, z = 0;
};
std::vector<PrefabRequest> requests;
struct PrefabSession
{
    std::shared_ptr<Vans::VansOpenAssetDocument> asset;
    std::string root;
    std::unique_ptr<Vans::VansSceneDocument> previousDocument;
    std::unique_ptr<Vans::VansSceneEditService> previousEdits;
    Vans::EditorSelectionSnapshot previousSelection;
    Vans::EditorAPI::EditorViewportCameraState previousCamera;
    Vans::SceneStateId savedState = 0;
};
std::unique_ptr<PrefabSession> session;
std::string status;
Json JsonOf(const Vans::VansSerializedValue& value) { return Vans::EncodeSerializedValueJson<Json>(value); }
}

bool VansEditorWindow::HasPrefabSession() { return session != nullptr; }
bool VansEditorWindow::HasPendingPrefabRequests() { return !requests.empty(); }
std::string VansEditorWindow::ActiveDocumentToken()
{
    if (!m_SceneDocument) return {};
    return JsonOf(m_SceneDocument->SerializedRootSnapshot()).value("sceneGuid", std::string{});
}
void VansEditorWindow::QueuePrefabCreation(std::string entity, std::string directory, std::string token)
{
    requests.push_back({PrefabRequest::Kind::Create, std::move(entity), std::move(directory), std::move(token)});
}
void VansEditorWindow::QueuePrefabPlacement(std::string asset, std::string parent, float x, float y, float z)
{
    PrefabRequest request{PrefabRequest::Kind::Place, std::move(asset)};
    request.parent = std::move(parent); request.x = x; request.y = y; request.z = z;
    request.token = ActiveDocumentToken(); requests.push_back(std::move(request));
}
void VansEditorWindow::QueuePrefabDuplicate(std::string root) { requests.push_back({PrefabRequest::Kind::Duplicate, std::move(root), {}, ActiveDocumentToken()}); }
void VansEditorWindow::QueuePrefabDelete(std::string root) { requests.push_back({PrefabRequest::Kind::Delete, std::move(root), {}, ActiveDocumentToken()}); }

void VansEditorWindow::QueuePrefabOpen(std::string path)
{
    requests.push_back({PrefabRequest::Kind::Open, {}, std::move(path)});
}

bool VansEditorWindow::RefreshActiveScenePreview()
{
    if (!m_SceneDocument || !GetEditorAPI()) return false;
    Vans::EditorAPI::RuntimeSceneLoadRequest request;
    request.mode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
    request.ensureResourceDependencies = true;
    request.document.sourcePath = session ? session->asset->sourcePath.string() : m_SceneDocument->SourcePath().string();
    auto preview = JsonOf(m_SceneDocument->SerializedRootSnapshot());
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
    request.document.authoringStateId = m_SceneDocument->CurrentStateId();
    const auto result = GetEditorAPI()->LoadRuntimeScene(request);
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
    if (!session || !m_SceneDocument) return false;
    const auto scene = JsonOf(m_SceneDocument->SerializedRootSnapshot());
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
    session->savedState = m_SceneDocument->CurrentStateId();
    status = "Prefab saved";
    return true;
}

void VansEditorWindow::DrawPrefabToolbar()
{
    if (session)
    {
        ImGui::Text("Prefab: %s%s", session->asset->sourcePath.filename().string().c_str(),
            m_SceneDocument && m_SceneDocument->CurrentStateId() != session->savedState ? " *" : "");
        if (ImGui::Button("Save Prefab")) SavePrefabSession();
        ImGui::SameLine();
        if (ImGui::Button("Back to Scene")) requests.push_back({PrefabRequest::Kind::Close});
        ImGui::SameLine();
        if (ImGui::Button("Discard")) requests.push_back({PrefabRequest::Kind::Discard});
    }
    else if (m_SceneDocument)
    {
        const auto selected = Vans::VansEditorSelectionService::Get().EntityGuid();
        if (!Vans::VansPrefabEditService::SourceAsset(*m_SceneDocument, selected).empty())
        {
            ImGui::TextUnformatted("Prefab Instance");
            if (ImGui::Button("Apply Overrides")) requests.push_back({PrefabRequest::Kind::Apply, selected});
            ImGui::SameLine();
            if (ImGui::Button("Revert Overrides")) requests.push_back({PrefabRequest::Kind::Revert, selected});
            ImGui::SameLine();
            if (ImGui::Button("Unpack")) requests.push_back({PrefabRequest::Kind::Unpack, selected});
        }
    }
    if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
}

void VansEditorWindow::ProcessPrefabRequests()
{
    if (requests.empty()) return;
    auto* api = GetEditorAPI();
    if (!api) return;
    auto pending = std::move(requests); requests.clear();
    if (api->GetPlayState() != Vans::EditorAPI::EnginePlayState::Edit) return;
    for (const auto& request : pending)
    {
        try
        {
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
                candidate->previousCamera = api->CaptureEditorViewportCamera();
                candidate->previousEdits = std::move(m_SceneEditService);
                candidate->previousDocument = std::move(m_SceneDocument);
                candidate->savedState = document->CurrentStateId();
                session = std::move(candidate); m_SceneDocument = std::move(document);
                m_SceneEditService = std::make_unique<Vans::VansSceneEditService>(*m_SceneDocument);
                m_SceneEditService->SetPrefabPreviewRefresh([] { return RefreshActiveScenePreview(); });
                if (!RefreshActiveScenePreview())
                {
                    const auto camera = session->previousCamera;
                    m_SceneEditService = std::move(session->previousEdits);
                    m_SceneDocument = std::move(session->previousDocument); session.reset();
                    if (m_SceneDocument) RefreshActiveScenePreview();
                    else api->UnloadRuntimeScene();
                    api->RestoreEditorViewportCamera(camera);
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
                if (request.kind == PrefabRequest::Kind::Close && m_SceneDocument->CurrentStateId() != session->savedState)
                { status = "Save Prefab or choose Discard before returning to the scene"; continue; }
                if (session->previousDocument && !session->previousDocument->RefreshPrefabView(status)) continue;
                auto selection = session->previousSelection;
                const auto camera = session->previousCamera;
                m_SceneEditService = std::move(session->previousEdits);
                m_SceneDocument = std::move(session->previousDocument); session.reset();
                if (m_SceneDocument)
                {
                    RefreshActiveScenePreview();
                }
                else api->UnloadRuntimeScene();
                Vans::VansSceneViewCommands::Clear();
                api->RestoreEditorViewportCamera(camera);
                Vans::VansEditorSelectionService::Get().Apply(Vans::EditorSelectionOperation::Replace, selection.objects, selection.active, "PrefabStageReturn");
                continue;
            }
            if (!m_SceneDocument || !m_SceneEditService) { status = "Open a scene first"; continue; }
            if (!request.token.empty() && request.token != ActiveDocumentToken()) { status = "The dragged object belongs to another document"; continue; }
            Vans::SceneEditResult result;
            const auto refresh = [] { return VansEditorWindow::RefreshActiveScenePreview(); };
            Vans::SceneEditLifecycleHooks hooks{refresh, refresh, refresh};
            if (request.kind == PrefabRequest::Kind::Create)
            {
                if (session) { status = "Create Prefab assets from a scene"; continue; }
                std::filesystem::path path;
                result = Vans::VansPrefabEditService::Create(*api, *m_SceneDocument, *m_SceneEditService, request.target, request.path, path);
                if (result) status = "Created " + path.filename().string();
            }
            else if (request.kind == PrefabRequest::Kind::Place)
            {
                if (session) { status = "Nested Prefabs are not supported"; continue; }
                Json placement{{"parent", nullptr}, {"position", {request.x, request.y, request.z}}, {"rotation", {0,0,0,1}}};
                if (!request.parent.empty()) placement["parent"] = {{"kind", "entity"}, {"entityGuid", request.parent}};
                result = Vans::VansPrefabEditService::Place(*m_SceneDocument, *m_SceneEditService, request.target, Vans::DecodeSerializedValueJson(placement), hooks);
            }
            else if (request.kind == PrefabRequest::Kind::Apply)
            {
                result = Vans::VansPrefabEditService::Apply(*api, *m_SceneDocument, *m_SceneEditService, request.target);
                if (result) status = "Overrides applied in memory; Save Scene / Save All saves the template";
            }
            else if (request.kind == PrefabRequest::Kind::Duplicate)
            {
                Vans::VansSerializedValue duplicated; std::string root, error;
                if (!Vans::VansPrefabResolver::DuplicateSubtree(m_SceneDocument->SerializedRootSnapshot(), m_SceneDocument->PrefabLookup(), request.target, duplicated, root, error))
                    result = {false, error};
                else
                {
                    result = m_SceneEditService->ReplaceRoot(std::move(duplicated), hooks);
                    if (result) Vans::VansEditorSelectionService::Get().SelectEntity(root, "PrefabDuplicate");
                }
            }
            else if (request.kind == PrefabRequest::Kind::Delete)
            {
                auto scene = JsonOf(m_SceneDocument->SerializedRootSnapshot());
                Vans::VansSerializedValue removed; std::string error;
                if (!Vans::ExtractSceneObjectSubtree(Vans::DecodeSerializedValueJson(scene["entities"]), request.target, removed, error)) result = {false, error};
                else
                {
                    std::unordered_set<std::string> ids;
                    for (const auto& object : JsonOf(removed)) ids.insert(object.at("id"));
                    auto& objects = scene["entities"];
                    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const auto& object) { return ids.count(object.at("id")); }), objects.end());
                    result = m_SceneEditService->ReplaceRoot(Vans::DecodeSerializedValueJson(scene), hooks);
                    if (result) Vans::VansEditorSelectionService::Get().Clear("PrefabDelete");
                }
            }
            else if (request.kind == PrefabRequest::Kind::Unpack)
                result = Vans::VansPrefabEditService::Unpack(*m_SceneDocument, *m_SceneEditService, request.target);
            else if (request.kind == PrefabRequest::Kind::Revert)
                result = Vans::VansPrefabEditService::Revert(*m_SceneDocument, *m_SceneEditService, request.target, hooks);
            if (!result) status = result.message;
        }
        catch (const std::exception& e) { status = e.what(); }
        if (!status.empty()) VANS_LOG("[Prefab] " << status);
    }
}
}
