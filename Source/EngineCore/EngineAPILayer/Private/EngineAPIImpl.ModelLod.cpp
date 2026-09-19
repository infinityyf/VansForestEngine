#include "EngineAPIImpl.h"
#include "../../EditorCore/ModelLod/VansModelLodBuilder.h"
#include "../../EditorCore/VansAssetDocumentRegistry.h"
#include "../../EditorCore/VansAssetDocumentEditService.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../PcgCore/Serialization/VansPlantTypeAssetCodec.h"
#include "../../RenderCore/VansScene.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"

namespace Vans::EditorAPI
{
ModelLodBuildResult EngineAPIImpl::BuildModelLods(const ModelLodBuildRequest& request)
{
    ModelLodBuildResult result;
    if(m_PlayState!=EnginePlayState::Edit){result.message="Build model LODs in Edit mode.";return result;}
    auto& manager=VansProjectManager::Get();auto* database=manager.GetAssetDatabase();
    if(!database){result.message="Open a project before building model LODs.";return result;}
    std::vector<VansModelLodSourcePart> parts;
    std::vector<ModelLodSourcePart> requestedParts = request.parts;
    if (requestedParts.empty() && !request.entityGuid.empty())
    {
        auto* scene = static_cast<VansGraphics::VansScene*>(m_Scene);
        auto* object = scene ? scene->FindObjectByGuid(request.entityGuid) : nullptr;
        auto* render = object ? object->GetComponent<VansScriptRenderComponent>() : nullptr;
        if (!object || !render)
        {
            result.message = "LODGroup source entity has no ModelRenderer.";
            return result;
        }
        for (auto* node : render->m_RenderNodes)
        {
            if (!node || !node->m_Material || object->m_ModelAssetGuid.empty()) continue;
            requestedParts.push_back({object->m_ModelAssetGuid, node->m_Material->m_AssetName,
                node->m_SubmeshIndex == UINT32_MAX ? -1 : static_cast<int>(node->m_SubmeshIndex), false});
        }
        if (requestedParts.empty())
        {
            result.message = "LODGroup source entity has no renderable mesh parts.";
            return result;
        }
    }
    for(const auto& source:requestedParts){VansModelLodSourcePart part;
        if(!VansAssetGuid::TryParse(source.model,part.model)||!VansAssetGuid::TryParse(source.material,part.material)){
            result.message="Every model LOD part requires valid model and material references.";return result;}
        part.submesh=source.submesh;part.alphaTest=source.alphaTest;parts.push_back(part);
    }
    VansModelLodSettings settings;settings.ratios=request.ratios;settings.maximumError=request.maximumError;
    VansModelLodAsset asset;
    if(!VansModelLodBuilder::Build(*database,parts,settings,asset,result.message))return result;
    result.success=true;result.buildKey=asset.buildKey;result.centerRadius=asset.centerRadius;
    for(const auto& level:asset.levels){ModelLodLevel output;
        for(const auto& part:level.parts)output.parts.push_back({part.model.ToString(),part.material.ToString(),part.submesh,part.sourcePart,part.triangleCount,part.error});
        result.levels.push_back(std::move(output));}
    return result;
}
PcgEditorOperationResult EngineAPIImpl::BuildPcgPlantLods(const std::string& text)
{
    if(m_PlayState!=EnginePlayState::Edit)return {false,"Build model LODs in Edit mode."};
    VansAssetGuid guid;
    if(!VansAssetGuid::TryParse(text,guid))return {false,"Invalid plant GUID."};
    auto* database=VansProjectManager::Get().GetAssetDatabase();const auto record=database?database->Find(guid):std::nullopt;
    if(!record||record->type!=VansAssetType::PlantType)return {false,"Select a plant asset."};
    auto document=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
    if(!document||!document->sourceDocument.IsLoaded())return {false,"Cannot open plant document."};
    const auto state=document->sourceDocument.CurrentStateId();VansPlantTypeAsset plant;std::string error;
    if(!VansPlantTypeAssetCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),plant,error))return {false,error};
    if(plant.category!=VansPlantCategory::Tree)return {true,{}};
    for(auto& variant:plant.variants){
        if(variant.geometry!=VansPlantGeometry::Mesh||variant.parts.empty())continue;
        ModelLodBuildRequest request;request.ratios=variant.lodSettings.ratios;request.maximumError=variant.lodSettings.maximumError;
        bool ready=true;
        for(const auto& part:variant.parts){if(!part.mesh.IsValid()||!part.material.IsValid()){ready=false;break;}
            request.parts.push_back({part.mesh.ToString(),part.material.ToString(),part.submesh,part.kind==VansPlantPartKind::Leaves});}
        if(!ready)continue;
        const auto built=BuildModelLods(request);if(!built.success)return {false,variant.name+": "+built.message};
        variant.lod={};variant.lod.buildKey=built.buildKey;variant.lod.centerRadius=built.centerRadius;
        for(const auto& level:built.levels){VansModelLodLevel output;
            for(const auto& part:level.parts){VansModelLodPart value;VansAssetGuid::TryParse(part.model,value.model);VansAssetGuid::TryParse(part.material,value.material);
                value.submesh=part.submesh;value.sourcePart=part.sourcePart;value.triangleCount=part.triangleCount;value.error=part.error;output.parts.push_back(value);}
            variant.lod.levels.push_back(std::move(output));}
    }
    if(document->sourceDocument.CurrentStateId()!=state)return {false,"Plant changed during LOD construction; apply again."};
    VansSerializedValue root;if(!VansPlantTypeAssetCodec::Encode(plant,root,error))return {false,error};
    const auto edited=VansAssetDocumentEditService::ReplaceRoot(document->sourceDocument,std::move(root));
    if(!edited)return {false,edited.message};
    const auto preview=RefreshPcgRecipePreview();
    return {true,preview.success?"Model LODs are ready. Save plant to persist its references.":preview.message};
}
}
