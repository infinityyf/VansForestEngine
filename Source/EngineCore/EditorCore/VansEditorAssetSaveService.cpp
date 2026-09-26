#include "VansEditorAssetSaveService.h"
#include "VansAssetDocumentTypeRegistry.h"

#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../EngineAPILayer/Public/IAssetAuthoringEditorAPI.h"
#include "../EngineAPILayer/Public/IPcgEditorAPI.h"

#include "../SceneCore/VansSceneDocument.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include <unordered_set>
#include <utility>

namespace Vans
{
namespace
{
struct StagedAssetDocument
{
    VansAssetDocument* document = nullptr;
    VansAssetDocumentSaveStage stage;
};

void AppendError(VansAssetSaveResult& result, const std::filesystem::path& path, const std::string& error)
{
    result.ok = false;
    result.errors.push_back(path.string() + ": " + error);
    if (result.message.empty())
        result.message = error;
}

VansEditorAssetSaveOperations OperationsFor(EditorAPI::IEngineEditorAPI& editorAPI)
{
    EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI = editorAPI;
    EditorAPI::IPcgEditorAPI& pcgAPI = editorAPI;
    auto* assetAuthoring = &assetAuthoringAPI;
    auto* pcg = &pcgAPI;
    return {
        [pcg](const std::string& guid, std::string& error)
        {
            const auto built = pcg->BuildPcgPlantLods(guid);
            error = built.message;
            return built.success;
        },
        [assetAuthoring](const std::filesystem::path& path, std::string& error)
        {
            const auto refreshed = assetAuthoring->RefreshProjectAsset(path.string(), false);
            error = refreshed.message;
            return refreshed.success;
        }
    };
}
}

VansEditorAssetSaveService& VansEditorAssetSaveService::Get()
{
    static VansEditorAssetSaveService service;
    return service;
}

VansAssetSaveResult VansEditorAssetSaveService::SaveAsset(
    EditorAPI::IEngineEditorAPI& editorAPI,
    const std::filesystem::path& sourcePath)
{
    return SaveAsset(editorAPI, VansAssetDocumentRegistry::Get().GetOrOpen(sourcePath));
}

VansAssetSaveResult VansEditorAssetSaveService::SaveAsset(
    EditorAPI::IEngineEditorAPI& editorAPI,
    const std::shared_ptr<VansOpenAssetDocument>& document)
{
    return SaveDocuments(OperationsFor(editorAPI), {document}, nullptr);
}

VansAssetSaveResult VansEditorAssetSaveService::SaveAllDirtyAssets(EditorAPI::IEngineEditorAPI& editorAPI)
{
    return SaveDocuments(OperationsFor(editorAPI), VansAssetDocumentRegistry::Get().DirtyDocuments(), nullptr);
}

VansAssetSaveResult VansEditorAssetSaveService::SaveSceneAndOwnedAssets(
    EditorAPI::IEngineEditorAPI& editorAPI, VansSceneDocument* scene)
{
    return SaveDocuments(OperationsFor(editorAPI), VansAssetDocumentRegistry::Get().SceneOwnedDirtyDocuments(), scene);
}

VansAssetSaveResult VansEditorAssetSaveService::SaveSceneAndAssets(
    EditorAPI::IEngineEditorAPI& editorAPI, VansSceneDocument& scene,
    const std::vector<std::shared_ptr<VansOpenAssetDocument>>& documents)
{
    return SaveDocuments(OperationsFor(editorAPI), documents, &scene);
}

VansAssetSaveResult VansEditorAssetSaveService::SaveSceneAndAssets(
    const VansEditorAssetSaveOperations& operations, VansSceneDocument& scene,
    const std::vector<std::shared_ptr<VansOpenAssetDocument>>& documents)
{
    return SaveDocuments(operations, documents, &scene);
}

VansAssetSaveResult VansEditorAssetSaveService::SaveDocuments(const VansEditorAssetSaveOperations& operations,
    const std::vector<std::shared_ptr<VansOpenAssetDocument>>& documents, VansSceneDocument* scene)
{
    VansScopedIOContext io(VansIODomain::Authoring, "EditorDocuments.Save", true);
    VansAssetSaveResult result;
    VansStagedFileTransaction transaction;
    std::vector<StagedAssetDocument> staged;
    std::vector<std::shared_ptr<VansOpenAssetDocument>> changed;
    std::vector<std::pair<std::shared_ptr<IVansAssetDocumentCompanion>, std::filesystem::path>> companions;
    SceneDocumentSaveStage sceneStage;
    std::string error;
    if (scene && !scene->StageSave(sceneStage, error))
    { AppendError(result, scene->SourcePath(), error); return result; }
    if (!sceneStage.targetPath.empty()) transaction.Add({sceneStage.targetPath, sceneStage.temporaryPath});
    std::unordered_set<const VansOpenAssetDocument*> seen;
    for (const auto& document : documents)
    {
        if (!document) { AppendError(result, {}, "No asset document"); return result; }
        if (!seen.insert(document.get()).second) continue;
        if (!document->sourceDocument.IsLoaded())
        { AppendError(result, document->sourcePath, "Asset authoring document is unavailable"); return result; }
        // 所有显式保存入口（含 Save All）在提交作者文档前准备派生模型。
        if (VansAssetDatabase::Classify(document->sourcePath) == VansAssetType::PlantType)
        {
            const auto meta = document->metaDocument.SerializedRootSnapshot();
            std::string guid;
            for (const auto& field : meta.objectFields)
                if (field.first == "guid") guid = field.second.stringValue;
            if (!operations.buildPcgPlantLods)
            {
                AppendError(result, document->sourcePath, "Plant LOD build operation is unavailable");
                return result;
            }
            if (!operations.buildPcgPlantLods(guid, document->lastError))
            { AppendError(result, document->sourcePath, document->lastError); return result; }
        }

        document->lastError.clear();
        const bool sourceDirty = document->sourceDocument.IsDirty();
        const bool metaDirty = document->metaDocument.IsDirty();

        std::vector<VansStagedFile> companionStages;
        const std::shared_ptr<IVansAssetDocumentCompanion> companion =
            document->companion.lock();
        if (sourceDirty)
        {
            const VansAssetType type = VansAssetDatabase::Classify(document->sourcePath);
            for (const VansAssetDocumentDiagnostic& diagnostic :
                VansAssetDocumentTypeRegistry::Get().ValidateBeforeSave(
                    type, document->sourcePath, document->sourceDocument.SerializedRootSnapshot()))
            {
                if (diagnostic.severity != VansAssetDocumentDiagnosticSeverity::Error)
                    continue;
                document->lastError = diagnostic.propertyPath.empty()
                    ? diagnostic.message
                    : diagnostic.propertyPath + ": " + diagnostic.message;
                AppendError(result, document->sourcePath, document->lastError);
                return result;
            }
            StagedAssetDocument item;
            item.document = &document->sourceDocument;
            if (!document->sourceDocument.StageSave(item.stage, document->lastError))
            {
                AppendError(result, document->sourcePath, document->lastError);
                return result;
            }
            transaction.Add({ item.stage.targetPath, item.stage.temporaryPath, item.stage.requireAbsent });
            staged.push_back(std::move(item));
        }

        if (metaDirty)
        {
            StagedAssetDocument item;
            item.document = &document->metaDocument;
            if (!document->metaDocument.StageSave(item.stage, document->lastError))
            {
                AppendError(result, document->metaPath, document->lastError);
                return result;
            }
            transaction.Add({ item.stage.targetPath, item.stage.temporaryPath, item.stage.requireAbsent });
            staged.push_back(std::move(item));
        }

        if (companion && companion->IsDirty())
        {
            if (!companion->StageSave(companionStages, document->lastError))
            {
                AppendError(result, document->sourcePath, document->lastError);
                return result;
            }
            for (const VansStagedFile& file : companionStages)
                transaction.Add(file);
        }


        if (!companionStages.empty()) companions.push_back({companion, document->sourcePath});
        if (sourceDirty || metaDirty || !companionStages.empty()) changed.push_back(document);
    }
    if (transaction.Empty()) return result;
    if (!transaction.PreparePublish(error))
    { AppendError(result, {}, error); return result; }
    for (const auto& item : staged)
        if (!item.document->ObservePublishedSave(item.stage, error))
        { AppendError(result, item.stage.targetPath, error); return result; }
    for (const auto& item : companions)
        if (!item.first->ObservePublishedSave(error))
        { AppendError(result, item.second, error); return result; }
    if (scene && !scene->ObservePublishedSave(sceneStage, error))
    { AppendError(result, scene->SourcePath(), error); return result; }
    for (const auto& document : changed)
    {
        if (!operations.refreshProjectAsset)
        {
            AppendError(result, document->sourcePath, "Asset refresh operation is unavailable");
            transaction.Cleanup();
            for (const auto& restored : changed)
                VansAssetDocumentRegistry::Get().PublishWorkingCopy(restored->sourceDocument);
            return result;
        }
        std::string refreshError;
        if (!operations.refreshProjectAsset(document->sourcePath, refreshError))
        {
            AppendError(result, document->sourcePath, "Asset refresh failed: " + refreshError);
            transaction.Cleanup();
            // 磁盘回滚后重新发布仍未保存的作者内存，保持编辑状态可重试。
            for (const auto& restored : changed)
                VansAssetDocumentRegistry::Get().PublishWorkingCopy(restored->sourceDocument);
            return result;
        }
    }
    transaction.Commit();
    for (const auto& item : staged) item.document->AdoptObservedSave(item.stage);
    for (const auto& item : companions) item.first->AdoptObservedSave();
    if (scene) scene->AdoptObservedSave(sceneStage);
    result.wroteFile = true;
    result.savedDocuments = changed.size() + (sceneStage.targetPath.empty() ? 0 : 1);
    return result;
}
}
