#pragma once

#include "VansBaseWindowComponent.h"
#include "../VansAssetDocumentRegistry.h"

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace VansGraphics
{
class VansInspectorWindow final : public VansBaseWindowComponent
{
public:

private:
    using Json = nlohmann::ordered_json;

    struct PendingAssetReferenceEdit
    {
        std::string pointer;
        std::string guid;
        int expectedAssetType = 0;
        bool writeObjectReference = true;
    };

    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
    void DrawSceneEntity(Vans::EditorAPI::IEngineEditorAPI& api);
	void DrawSceneSettings();
    void DrawAsset(Vans::EditorAPI::IEngineEditorAPI& api);
    bool DrawJsonValue(const std::string& label, Json& value, const std::string& pointer,
        bool readOnly = false, const std::string& componentType = {}, const std::string& parentKey = {});
    bool DrawAssetReference(const std::string& label, Json& reference,
        const std::string& pointer, int expectedAssetType, bool writeObjectReference = true);
    bool DrawComponent(Vans::EditorAPI::IEngineEditorAPI& api, Json& component,
        const std::string& pointer, bool& removeRequested);
    void ApplyComponentEnabled(Vans::EditorAPI::IEngineEditorAPI& api,
        const std::string& componentType, bool enabled);
    bool LoadAssetDocuments(const std::filesystem::path& sourcePath);
    bool SaveAssetDocuments(bool reloadSceneOnSuccess = true);

    std::filesystem::path m_AssetPath;
    std::shared_ptr<Vans::VansOpenAssetDocument> m_AssetDocuments;
    std::string m_Error;
    std::vector<std::string> m_CollisionLayerNames;
    Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
    bool m_PendingVehicleRebuild = false;
    std::optional<PendingAssetReferenceEdit> m_PendingAssetReferenceEdit;
};
}
