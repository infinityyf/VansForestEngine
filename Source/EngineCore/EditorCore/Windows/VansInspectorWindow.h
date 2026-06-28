#pragma once

#include "VansBaseWindowComponent.h"
#include "../VansInspectorLiveEditService.h"
#include "../VansMaterialLiveEditService.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../../RenderCore/VansScene.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace VansGraphics
{
class VansInspectorWindow final : public VansBaseWindowComponent
{
public:
    void RegistScene(VansScene* scene) { m_Scene = scene; m_LiveEdit.Bind(m_Scene, nullptr); m_MaterialLiveEdit.Bind(m_Scene); }

private:
    using Json = nlohmann::ordered_json;

    void ShowWindow(VansVKDevice& device) override;
    void DrawSceneEntity();
	void DrawSceneSettings();
    void DrawAsset();
    bool DrawJsonValue(const std::string& label, Json& value, const std::string& pointer,
        bool readOnly = false, const std::string& componentType = {}, const std::string& parentKey = {});
    bool DrawAssetReference(const std::string& label, Json& reference,
        const std::string& pointer, int expectedAssetType);
    bool DrawComponent(Json& component, const std::string& pointer, bool& removeRequested);
    void ApplyComponentEnabled(const std::string& jsonPointer, bool enabled);
    bool LoadAssetDocuments(const std::filesystem::path& sourcePath);
    bool SaveAssetDocuments();

    std::filesystem::path m_AssetPath;
    std::filesystem::path m_MetaPath;
    Vans::VansAssetDocument m_AssetDocument;
    Vans::VansAssetDocument m_MetaDocument;
    std::string m_Error;
    VansScene* m_Scene = nullptr;
    VansInspectorLiveEditService m_LiveEdit;
    VansMaterialLiveEditService m_MaterialLiveEdit;
};
}
