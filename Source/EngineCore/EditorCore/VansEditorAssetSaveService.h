#pragma once

#include "../AuthoringCore/VansAssetDocumentRegistry.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
class IEngineEditorAPI;
}

namespace Vans
{
class VansSceneDocument;
struct VansAssetSaveResult
{
    bool ok = true;
    bool wroteFile = false;
    std::size_t savedDocuments = 0;
    std::vector<std::string> errors;
    std::string message;

    explicit operator bool() const { return ok; }
};

struct VansEditorAssetSaveOperations
{
    std::function<bool(const std::string& plantGuid, std::string& error)> buildPcgPlantLods;
    std::function<bool(const std::filesystem::path& sourcePath, std::string& error)> refreshProjectAsset;
};

class VansEditorAssetSaveService
{
public:
	static VansEditorAssetSaveService& Get();

    VansAssetSaveResult SaveAsset(EditorAPI::IEngineEditorAPI& editorAPI, const std::filesystem::path& sourcePath);
    VansAssetSaveResult SaveAsset(EditorAPI::IEngineEditorAPI& editorAPI, const std::shared_ptr<VansOpenAssetDocument>& document);
    VansAssetSaveResult SaveAllDirtyAssets(EditorAPI::IEngineEditorAPI& editorAPI);
    VansAssetSaveResult SaveSceneAndOwnedAssets(EditorAPI::IEngineEditorAPI& editorAPI, VansSceneDocument* scene);
    VansAssetSaveResult SaveSceneAndAssets(EditorAPI::IEngineEditorAPI& editorAPI, VansSceneDocument& scene,
        const std::vector<std::shared_ptr<VansOpenAssetDocument>>& documents);
    VansAssetSaveResult SaveSceneAndAssets(const VansEditorAssetSaveOperations& operations, VansSceneDocument& scene,
        const std::vector<std::shared_ptr<VansOpenAssetDocument>>& documents);
private:
    VansAssetSaveResult SaveDocuments(const VansEditorAssetSaveOperations& operations,
        const std::vector<std::shared_ptr<VansOpenAssetDocument>>& documents, VansSceneDocument* scene);
};
}
