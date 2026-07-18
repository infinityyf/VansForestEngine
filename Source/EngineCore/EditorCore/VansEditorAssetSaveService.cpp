#include "VansEditorAssetSaveService.h"

#include "../EngineAPILayer/Public/IEngineEditorAPI.h"

namespace Vans
{
namespace
{
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

    if (sourceDirty)
    {
        if (!document->sourceDocument.Save(document->lastError))
        {
            AppendError(result, document->sourcePath, document->lastError);
            return result;
        }
        result.wroteFile = true;
    }

    if (metaDirty)
    {
        if (!document->metaDocument.Save(document->lastError))
        {
            AppendError(result, document->metaPath, document->lastError);
            return result;
        }
        result.wroteFile = true;
    }

    if (result.wroteFile)
    {
        const EditorAPI::AssetRefreshResult refresh =
            editorAPI.RefreshProjectAsset(document->sourcePath.string(), false);
        if (!refresh.success)
            AppendError(result, document->sourcePath, "Asset refresh failed: " + refresh.message);
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
}
