#pragma once

#include "VansAssetDocumentRegistry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
class IEngineEditorAPI;
}

namespace Vans
{
struct VansAssetSaveResult
{
    bool ok = true;
    bool wroteFile = false;
    std::size_t savedDocuments = 0;
    std::vector<std::string> errors;
    std::string message;

    explicit operator bool() const { return ok; }
};

class VansEditorAssetSaveService
{
public:
	static VansEditorAssetSaveService& Get();

    VansAssetSaveResult SaveAsset(EditorAPI::IEngineEditorAPI& editorAPI, const std::filesystem::path& sourcePath);
    VansAssetSaveResult SaveAsset(EditorAPI::IEngineEditorAPI& editorAPI, const std::shared_ptr<VansOpenAssetDocument>& document);
    VansAssetSaveResult SaveAllDirtyAssets(EditorAPI::IEngineEditorAPI& editorAPI);
	VansAssetSaveResult SaveSceneOwnedAssets(EditorAPI::IEngineEditorAPI& editorAPI);
};
}
