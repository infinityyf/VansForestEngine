#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../EditorCore/VansAssetDocumentRegistry.h"
#include "../../EditorCore/VansAssetDocumentEditService.h"
#include "../../EditorCore/VansEditorAssetSaveService.h"
#include "../../PcgCore/Serialization/VansPlantTypeAssetCodec.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include <algorithm>

namespace Vans::EditorAPI
{
namespace
{
std::shared_ptr<VansOpenAssetDocument> OpenConfiguration(const std::string& text,VansAssetType type,std::string& error)
{
    VansAssetGuid guid;
    if (!VansAssetGuid::TryParse(text,guid)) {error="Select a configuration asset.";return {};}
    const auto* database=VansProjectManager::Get().GetAssetDatabase();
    const auto record=database?database->Find(guid):std::nullopt;
    if (!record || record->type!=type) {error="The configuration asset is unavailable.";return {};}
    auto document=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
    if (!document || !document->sourceDocument.IsLoaded()) {
        error=document?document->lastError:"The configuration document is unavailable.";return {};
    }
    return document;
}
template<class Snapshot> void ReadHistory(const VansOpenAssetDocument& document,Snapshot& result)
{
    result.available=true;result.dirty=document.IsDirty();
    result.documentState=document.sourceDocument.CurrentStateId();
    result.canUndo=VansAssetDocumentEditService::CanUndo(document.sourceDocument);
    result.canRedo=VansAssetDocumentEditService::CanRedo(document.sourceDocument);
}
bool ReadGuid(const std::string& text,VansAssetGuid& guid,std::string& error)
{
    if (text.empty()) {guid={};return true;}
    if (VansAssetGuid::TryParse(text,guid)) return true;
    error="Invalid asset reference: "+text;return false;
}
template<class Recipe> auto FindRegion(Recipe& recipe,const PcgBrushTarget& target)
{
    return std::find_if(recipe.regions.begin(),recipe.regions.end(),[&](const auto& value){return value.id==target.regionId;});
}
template<class Region> auto FindLayer(Region& region,const PcgBrushTarget& target)
{
    return std::find_if(region.layers.begin(),region.layers.end(),[&](const auto& value){return value.id==target.layerId;});
}
}
PcgPlantConfiguration EngineAPIImpl::GetPcgPlantConfiguration(const std::string& guid)
{
    PcgPlantConfiguration result;result.guid=guid;
    const auto document=OpenConfiguration(guid,VansAssetType::PlantType,result.message);
    if (!document) return result;
    VansPlantTypeAsset plant;
    if (!VansPlantTypeAssetCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),plant,result.message)) return result;
    ReadHistory(*document,result);
    result.name=plant.name;result.tree=plant.category==VansPlantCategory::Tree;
    result.grass.boneCount=plant.grass.boneCount;
    result.grass.subBladeCount=plant.grass.subBladeCount;
    result.grass.bladeHeight=plant.grass.bladeHeight;
    result.grass.leanDeviation=plant.grass.leanDeviation;
    result.grass.restTipBendDegrees=plant.grass.restTipBendDegrees;
    result.grass.restRootBendDegrees=plant.grass.restRootBendDegrees;
    result.grass.scatterSeed=plant.grass.scatterSeed;
    result.grass.scatterRadiusMin=plant.grass.scatterRadiusMin;
    result.grass.scatterRadiusMax=plant.grass.scatterRadiusMax;
    result.grass.windDirection=plant.grass.windDirection;
    result.grass.windStrength=plant.grass.windStrength;
    result.grass.windFrequency=plant.grass.windFrequency;
    result.grass.windSpeed=plant.grass.windSpeed;
    result.grass.windBendMultiplier=plant.grass.windBendMultiplier;
    result.grass.stiffness=plant.grass.stiffness;
    result.grass.damping=plant.grass.damping;
    result.grass.softness=plant.grass.softness;
    result.grass.simulationFullDistance=plant.grass.simulationFullDistance;
    result.grass.simulationFadeDistance=plant.grass.simulationFadeDistance;
    result.grass.subBladeLodMidDistance=plant.grass.subBladeLodMidDistance;
    result.grass.subBladeLodFarDistance=plant.grass.subBladeLodFarDistance;
    result.render.cullingEnabled=plant.render.cullingEnabled;
    result.render.hizEnabled=plant.render.hizEnabled;
    result.render.castShadows=plant.render.castShadows;
    result.render.cullDistance=plant.render.cullDistance;
    result.render.hizBias=plant.render.hizBias;
    result.render.lodDistances=plant.render.lodDistances;result.render.lodHysteresis=plant.render.lodHysteresis;
    for (const auto& variant : plant.variants) {
        PcgPlantVariant item;
        item.id=variant.id;item.name=variant.name;item.geometry=static_cast<PcgGeometry>(variant.geometry);
    item.weight=variant.weight;
    item.footprintRadius=variant.footprintRadius;
    item.cullingRadius=variant.cullingRadius;
    item.bladeWidth=variant.bladeWidth;
    item.offset=variant.offset;
    item.scale=variant.scale;
    item.rotation=variant.rotation;
    item.lodRatios=variant.lodSettings.ratios;item.lodMaximumError=variant.lodSettings.maximumError;
    item.lodBuildKey=variant.lod.buildKey;
    for(const auto& level:variant.lod.levels){ModelLodLevel output;for(const auto& part:level.parts)
        output.parts.push_back({part.model.ToString(),part.material.ToString(),part.submesh,part.sourcePart,part.triangleCount,part.error});
        item.lodLevels.push_back(std::move(output));}
        for (const auto& part : variant.parts) {
            PcgPlantPart value;
            value.id=part.id;value.kind=static_cast<PcgPartKind>(part.kind);value.submesh=part.submesh;
            if (part.mesh.IsValid()) value.mesh=part.mesh.ToString();
            if (part.material.IsValid()) value.material=part.material.ToString();
            item.parts.push_back(std::move(value));
        }
        result.variants.push_back(std::move(item));
    }
    return result;
}
PcgLayerConfiguration EngineAPIImpl::GetPcgLayerConfiguration(const PcgBrushTarget& target)
{
    PcgLayerConfiguration result;
    const auto document=OpenConfiguration(target.recipeGuid,VansAssetType::VegetationConfig,result.message);
    if (!document) return result;
    VansVegetationConfigAsset asset;
    if (!VansVegetationConfigCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),asset,result.message)) return result;
    const auto region=FindRegion(asset.config,target);
    if (region==asset.config.regions.end()) {result.message="The region no longer exists.";return result;}
    const auto layer=FindLayer(*region,target);
    if (layer==region->layers.end()) {result.message="The layer no longer exists.";return result;}
    ReadHistory(*document,result);
    result.name=layer->name;result.enabled=layer->enabled;result.locked=layer->locked;
    result.plantGuid=layer->plant.IsValid()?layer->plant.ToString():"";
    result.source=static_cast<PcgSourceMode>(layer->source);result.seed=layer->seed;result.treeTargetCount=layer->targetCount;
    result.maxCandidates=layer->budget.maxCandidates;result.maxInstances=layer->budget.maxInstances;
    result.placement.density=layer->placement.density;
    result.placement.positionJitter=layer->placement.positionJitter;
    result.placement.minimumSpacing=layer->placement.minimumSpacing;
    result.placement.scaleMin=layer->placement.scaleMin;
    result.placement.scaleMax=layer->placement.scaleMax;
    result.placement.uniformScale=layer->placement.uniformScale;
    result.placement.yawMinDegrees=layer->placement.yawMinDegrees;
    result.placement.yawMaxDegrees=layer->placement.yawMaxDegrees;
    result.placement.normalAlignment=layer->placement.normalAlignment;
    result.placement.maximumTiltDegrees=layer->placement.maximumTiltDegrees;
    result.placement.rootOffset=layer->placement.rootOffset;
    result.placement.maskThreshold=layer->placement.maskThreshold;
    result.placement.maskMultiplier=layer->placement.maskMultiplier;
    result.placement.invertMask=layer->placement.invertMask;
    result.regionName=region->name;result.regionEnabled=region->enabled;
    result.boundsMin=region->bounds.min;result.boundsMax=region->bounds.max;result.cellSize=region->cellSize;result.regionSeed=region->seed;
    result.surface=static_cast<PcgSurfaceKind>(region->surface.kind);result.planeHeight=region->surface.planeHeight;
    result.terrainGuid=region->surface.terrain.IsValid()?region->surface.terrain.ToString():"";
    return result;
}
PcgEditorOperationResult EngineAPIImpl::ApplyPcgPlantConfiguration(const PcgPlantConfiguration& configuration)
{
    if (m_PlayState!=EnginePlayState::Edit) return {false,"Edit plant assets in Edit mode."};
    std::string error;
    const auto document=OpenConfiguration(configuration.guid,VansAssetType::PlantType,error);
    if (!document) return {false,error};
    if (document->sourceDocument.CurrentStateId()!=configuration.documentState)
        return {false,"This plant changed in another editor. Reload the draft before applying."};
    VansPlantTypeAsset previous,plant;
    if (!VansPlantTypeAssetCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),previous,error)) return {false,error};
    plant.name=configuration.name;plant.category=previous.category;
    plant.grass.boneCount=configuration.grass.boneCount;
    plant.grass.subBladeCount=configuration.grass.subBladeCount;
    plant.grass.bladeHeight=configuration.grass.bladeHeight;
    plant.grass.leanDeviation=configuration.grass.leanDeviation;
    plant.grass.restTipBendDegrees=configuration.grass.restTipBendDegrees;
    plant.grass.restRootBendDegrees=configuration.grass.restRootBendDegrees;
    plant.grass.scatterSeed=configuration.grass.scatterSeed;
    plant.grass.scatterRadiusMin=configuration.grass.scatterRadiusMin;
    plant.grass.scatterRadiusMax=configuration.grass.scatterRadiusMax;
    plant.grass.windDirection=configuration.grass.windDirection;
    plant.grass.windStrength=configuration.grass.windStrength;
    plant.grass.windFrequency=configuration.grass.windFrequency;
    plant.grass.windSpeed=configuration.grass.windSpeed;
    plant.grass.windBendMultiplier=configuration.grass.windBendMultiplier;
    plant.grass.stiffness=configuration.grass.stiffness;
    plant.grass.damping=configuration.grass.damping;
    plant.grass.softness=configuration.grass.softness;
    plant.grass.simulationFullDistance=configuration.grass.simulationFullDistance;
    plant.grass.simulationFadeDistance=configuration.grass.simulationFadeDistance;
    plant.grass.subBladeLodMidDistance=configuration.grass.subBladeLodMidDistance;
    plant.grass.subBladeLodFarDistance=configuration.grass.subBladeLodFarDistance;
    plant.render.cullingEnabled=configuration.render.cullingEnabled;
    plant.render.hizEnabled=configuration.render.hizEnabled;
    plant.render.castShadows=configuration.render.castShadows;
    plant.render.cullDistance=configuration.render.cullDistance;
    plant.render.hizBias=configuration.render.hizBias;
    plant.render.lodDistances=configuration.render.lodDistances;plant.render.lodHysteresis=configuration.render.lodHysteresis;
    for (const auto& variant : configuration.variants) {
        VansPlantVariant item;
        item.id=variant.id.empty()?VansAssetGuid::New().ToString():variant.id;
        item.name=variant.name;item.geometry=static_cast<VansPlantGeometry>(variant.geometry);
    item.weight=variant.weight;
    item.footprintRadius=variant.footprintRadius;
    item.cullingRadius=variant.cullingRadius;
    item.bladeWidth=variant.bladeWidth;
    item.offset=variant.offset;
    item.scale=variant.scale;
    item.rotation=variant.rotation;
        for (const auto& part : variant.parts) {
            VansPlantPart value;
            value.id=part.id.empty()?VansAssetGuid::New().ToString():part.id;
            value.kind=static_cast<VansPlantPartKind>(part.kind);value.submesh=part.submesh;
            if (!ReadGuid(part.mesh,value.mesh,error) || !ReadGuid(part.material,value.material,error)) return {false,error};
            item.parts.push_back(std::move(value));
        }
        item.lodSettings.ratios=variant.lodRatios;item.lodSettings.maximumError=variant.lodMaximumError;

        const auto old=std::find_if(previous.variants.begin(),previous.variants.end(),[&](const auto& v){return v.id==item.id;});
        if(old!=previous.variants.end() && old->lodSettings.ratios==item.lodSettings.ratios && old->lodSettings.maximumError==item.lodSettings.maximumError &&
            old->parts.size()==item.parts.size() && std::equal(old->parts.begin(),old->parts.end(),item.parts.begin(),[](const auto& a,const auto& b){
                return a.mesh==b.mesh&&a.material==b.material&&a.submesh==b.submesh&&a.kind==b.kind;}))item.lod=old->lod;
        plant.variants.push_back(std::move(item));
    }
    VansSerializedValue root;
    if (!VansPlantTypeAssetCodec::Encode(plant,root,error)) return {false,error};
    const auto finish=FinishPcgStroke(false);if (!finish.success) return finish;
    const auto edited=VansAssetDocumentEditService::ReplaceRoot(document->sourceDocument,std::move(root));
    if (!edited) return {false,edited.message};
    const auto preview=RefreshPcgRecipePreview();
    return {true,preview.success?"":preview.message};
}
PcgEditorOperationResult EngineAPIImpl::ApplyPcgLayerConfiguration(const PcgBrushTarget& target,const PcgLayerConfiguration& configuration)
{
    if (m_PlayState!=EnginePlayState::Edit) return {false,"Edit distribution in Edit mode."};
    std::string error;
    const auto document=OpenConfiguration(target.recipeGuid,VansAssetType::VegetationConfig,error);
    if (!document) return {false,error};
    if (document->sourceDocument.CurrentStateId()!=configuration.documentState)
        return {false,"This recipe changed in another editor. Reload the draft before applying."};
    VansVegetationConfigAsset asset;
    if (!VansVegetationConfigCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),asset,error)) return {false,error};
    auto region=FindRegion(asset.config,target);
    if (region==asset.config.regions.end()) return {false,"The region no longer exists."};
    auto layer=FindLayer(*region,target);
    if (layer==region->layers.end()) return {false,"The layer no longer exists."};
    layer->name=configuration.name;layer->enabled=configuration.enabled;layer->locked=configuration.locked;
    if (!ReadGuid(configuration.plantGuid,layer->plant,error)) return {false,error};
    layer->source=static_cast<VansPcgSourceMode>(configuration.source);layer->seed=configuration.seed;layer->targetCount=configuration.treeTargetCount;
    layer->budget={configuration.maxCandidates,configuration.maxInstances};
    layer->placement.density=configuration.placement.density;
    layer->placement.positionJitter=configuration.placement.positionJitter;
    layer->placement.minimumSpacing=configuration.placement.minimumSpacing;
    layer->placement.scaleMin=configuration.placement.scaleMin;
    layer->placement.scaleMax=configuration.placement.scaleMax;
    layer->placement.uniformScale=configuration.placement.uniformScale;
    layer->placement.yawMinDegrees=configuration.placement.yawMinDegrees;
    layer->placement.yawMaxDegrees=configuration.placement.yawMaxDegrees;
    layer->placement.normalAlignment=configuration.placement.normalAlignment;
    layer->placement.maximumTiltDegrees=configuration.placement.maximumTiltDegrees;
    layer->placement.rootOffset=configuration.placement.rootOffset;
    layer->placement.maskThreshold=configuration.placement.maskThreshold;
    layer->placement.maskMultiplier=configuration.placement.maskMultiplier;
    layer->placement.invertMask=configuration.placement.invertMask;
    region->name=configuration.regionName;region->enabled=configuration.regionEnabled;
    region->bounds={configuration.boundsMin,configuration.boundsMax};region->cellSize=configuration.cellSize;region->seed=configuration.regionSeed;
    region->surface.kind=static_cast<VansPcgSurfaceKind>(configuration.surface);region->surface.planeHeight=configuration.planeHeight;
    if (!ReadGuid(configuration.terrainGuid,region->surface.terrain,error)) return {false,error};
    VansSerializedValue root;
    if (!VansVegetationConfigCodec::Encode(asset.config,root,error)) return {false,error};
    const auto finish=SelectPcgBrushTarget({},false);if (!finish.success) return finish;
    const auto edited=VansAssetDocumentEditService::ReplaceRoot(document->sourceDocument,std::move(root));
    if (!edited) return {false,edited.message};
    const auto preview=RefreshPcgRecipePreview();
    return {true,preview.success?"":preview.message};
}
PcgEditorOperationResult EngineAPIImpl::EditPcgConfiguration(const std::string& guid,PcgConfigurationAction action)
{
    if (m_PlayState!=EnginePlayState::Edit) return {false,"Edit configurations in Edit mode."};
    VansAssetGuid id;VansAssetGuid::TryParse(guid,id);
    const auto* database=VansProjectManager::Get().GetAssetDatabase();
    const auto record=database?database->Find(id):std::nullopt;
    if (!record || (record->type!=VansAssetType::PlantType && record->type!=VansAssetType::VegetationConfig))
        return {false,"Select a plant or recipe configuration."};
    std::string error;
    const auto document=OpenConfiguration(guid,record->type,error);
    if (!document) return {false,error};
    const auto finish=SelectPcgBrushTarget({},false);if (!finish.success) return finish;
    if (action==PcgConfigurationAction::Save) {
        const auto saved=VansEditorAssetSaveService::Get().SaveAsset(*this,document);
        return {static_cast<bool>(saved),saved.message};
    }
    AssetDocumentEditResult edited;
    switch (action) {
    case PcgConfigurationAction::Undo:edited=VansAssetDocumentEditService::Undo(document->sourceDocument);break;
    case PcgConfigurationAction::Redo:edited=VansAssetDocumentEditService::Redo(document->sourceDocument);break;
    case PcgConfigurationAction::Revert:edited=VansAssetDocumentEditService::RevertToSaved(document->sourceDocument);break;
    default:return {false,"Unknown configuration operation."};
    }
    if (!edited) return {false,edited.message};
    const auto preview=RefreshPcgRecipePreview();
    return {true,preview.success?"":preview.message};
}
}
