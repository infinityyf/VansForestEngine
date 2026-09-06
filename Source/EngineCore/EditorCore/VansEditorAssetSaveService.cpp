#include "VansEditorAssetSaveService.h"
#include "VansAssetDocumentTypeRegistry.h"

#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"

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
    VansAssetSaveResult result;
    if (!document)
    {
        result.ok = false;
        result.message = "No asset document";
        return result;
    }

    document->lastError.clear();
    const bool sourceDirty = document->sourceDocument.IsDirty();
    const bool metaDirty = document->metaDocument.IsDirty();

    std::vector<StagedAssetDocument> staged;
    VansStagedFileTransaction transaction;
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
        transaction.Add({ item.stage.targetPath, item.stage.temporaryPath });
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
        transaction.Add({ item.stage.targetPath, item.stage.temporaryPath });
        staged.push_back(std::move(item));
    }

    if (!transaction.Empty())
    {
        if (!transaction.Publish(document->lastError))
        {
            AppendError(result, document->sourcePath, document->lastError);
            return result;
        }

        for (StagedAssetDocument& item : staged)
        {
            if (item.document && !item.document->ObservePublishedSave(item.stage, document->lastError))
            {
                AppendError(result, item.stage.targetPath, document->lastError);
                return result;
            }
        }
        result.wroteFile = true;
    }

    if (result.wroteFile)
    {
        const EditorAPI::AssetRefreshResult refresh =
            editorAPI.RefreshProjectAsset(document->sourcePath.string(), false);
        if (!refresh.success)
        {
            AppendError(result, document->sourcePath, "Asset refresh failed: " + refresh.message);
			return result;
		}
		for (StagedAssetDocument& item : staged)
			if (item.document) item.document->AdoptObservedSave(item.stage);
        if (result)
            result.savedDocuments = 1;
    }
    return result;
}

VansAssetSaveResult VansEditorAssetSaveService::SaveAllDirtyAssets(EditorAPI::IEngineEditorAPI& editorAPI)
{
    VansAssetSaveResult result;
    for (const auto& document : VansAssetDocumentRegistry::Get().DirtyDocuments())
    {
        VansAssetSaveResult item = SaveAsset(editorAPI, document);
        result.wroteFile = result.wroteFile || item.wroteFile;
        result.savedDocuments += item.savedDocuments;
        if (!item)
        {
            result.ok = false;
            result.errors.insert(result.errors.end(), item.errors.begin(), item.errors.end());
            if (result.message.empty())
                result.message = item.message;
        }
    }
    return result;
}

VansAssetSaveResult VansEditorAssetSaveService::SaveSceneOwnedAssets(
	EditorAPI::IEngineEditorAPI& editorAPI)
{
	VansAssetSaveResult result;
	for (const auto& document : VansAssetDocumentRegistry::Get().SceneOwnedDirtyDocuments())
	{
		VansAssetSaveResult item = SaveAsset(editorAPI, document);
		result.wroteFile = result.wroteFile || item.wroteFile;
		result.savedDocuments += item.savedDocuments;
		if (!item)
		{
			result.ok = false;
			result.errors.insert(result.errors.end(), item.errors.begin(), item.errors.end());
			if (result.message.empty()) result.message = item.message;
		}
	}
	return result;
}
}
