#include "EngineAPIImpl.h"
#include "VansMaterialLiveEditService.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../AssetCore/VansMaterialAuthoringAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../RenderCore/PcgCore/VansPcgPreviewProjector.h"

namespace Vans::EditorAPI
{
PcgEditorOperationResult EngineAPIImpl::CommitPcgPreview(std::shared_ptr<const VansPcgBatchUpdate> update)
{
    if (!m_Scene) return {true,{}};
    auto& scene=*static_cast<VansGraphics::VansScene*>(m_Scene);
    auto& manager=VansProjectManager::Get();
    auto* database=manager.GetAssetDatabase();
    if (!database) return {false,"The PCG resource index is unavailable."};
    // 只投影植被依赖，不重新加载场景、音频、游戏逻辑或其他渲染模块。
    const auto root=VansSerializedValue::Object({
        {"schemaVersion",VansSerializedValue::Int(2)},{"entities",VansSerializedValue::Array({})},
        {"settings",VansSerializedValue::Object({{"vegetation",VansSerializedValue::Object({
            {"asset",VansSerializedValue::Object({{"guid",VansSerializedValue::String(m_PcgSceneRecipeGuid)}})}
        })}})}});
    auto* authoringDocument=m_PcgSceneDocument;
    const auto path=authoringDocument?authoringDocument->SourcePath():database->AssetsRoot();
    auto dependencies=VansSceneAssetDependencyBuilder::BuildResourcePlan(*database,root,path,{},
        manager.GetAssetObjectRepository(),manager.GetBuiltInAssetDatabase());
    if (!dependencies.success) return {false,dependencies.errors.empty()?"PCG dependencies could not be resolved.":dependencies.errors.front()};
    const auto context=VansSceneResourceLoadContext::ForEditor(manager.GetProjectRootPath(),{},
        manager.EnumerateAssetRecords());
    auto materialGuids=dependencies.requiredMaterials;
    VansGraphics::VansPcgPreviewProjection projection{
        std::move(dependencies.resourcePlan),dependencies.requiredMaterials,context,std::move(update),
        [&manager,materialGuids=std::move(materialGuids)](VansGraphics::VansScene& scene) {
            VansGraphics::VansMaterialLiveEditService live;
            for (const auto& text:materialGuids) {
                VansAssetGuid guid;VansAssetGuid::TryParse(text,guid);
                const auto material=manager.GetAssetObjectRepository().ResolveLatest<VansMaterialAuthoringAsset>(guid);
                if (!material || material->textures.kind!=VansSerializedValue::Kind::Object) continue;
                for (const auto& entry:material->textures.objectFields) {
                    const auto textureGuid=ReadSerializedStringField(entry.second,"guid");
                    if (!textureGuid.empty()) live.ApplyMaterialTexture(&scene,text,entry.first,textureGuid);
                }
            }
        }};
    std::string error;
    return {VansGraphics::VansPcgPreviewProjector::Project(scene,std::move(projection),error),error};
}
}
