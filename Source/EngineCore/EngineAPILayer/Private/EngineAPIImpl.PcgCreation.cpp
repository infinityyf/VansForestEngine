#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../EditorCore/VansAssetDocumentEditService.h"
#include "../../EditorCore/VansAssetDocumentRegistry.h"
#include "../../EditorCore/VansSceneEditService.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansAssetObjectBootstrapper.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../../PcgCore/Serialization/VansPlantTypeAssetCodec.h"
#include "../../PcgCore/Serialization/VansPcgMaskAssetCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Vans::EditorAPI
{
void EngineAPIImpl::BindPcgSceneAuthoring(VansSceneDocument* document,VansSceneEditService* edits)
{
    const bool changed=document!=m_PcgSceneDocument || (document && document->CurrentStateId()!=m_PcgSceneAuthoringState);
    m_PcgSceneDocument=document;
    m_PcgSceneEdits=edits;
    m_PcgSceneAuthoringState=document?document->CurrentStateId():0;
    TickPcgSplineAuthoring();
    if (!changed || !document || m_PlayState!=EnginePlayState::Edit) return;
    const auto snapshot=document->CreateSnapshot();
    std::string guid;
    if (const auto* settings=FindObjectField(snapshot.Root(),"settings"))
        if (const auto* reference=FindObjectField(*settings,"vegetation")) guid=VansVegetationConfigCodec::ReadReferenceGuid(*reference);
    if (guid!=m_PcgSceneRecipeGuid) {
        FinishPcgStroke(false);SelectPcgBrushTarget({},false);m_PcgSceneRecipeGuid=guid;
        RefreshPcgRecipePreview();
    }
}
PcgEditorOperationResult EngineAPIImpl::BindPcgRecipeToScene(const std::string& text)
{
    if (m_PlayState!=EnginePlayState::Edit || !m_PcgSceneDocument || !m_PcgSceneEdits)
        return {false,"Open an editable scene before binding a PCG recipe."};
    VansAssetGuid guid;
    if (!VansAssetGuid::TryParse(text,guid) ||
        !VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansVegetationConfigAsset>(guid))
        return {false,"Select a valid PCG recipe."};
    const auto finished=FinishPcgStroke(false);
    if (!finished.success) return finished;
    auto reference=VansSerializedValue::Object({{"asset",VansSerializedValue::Object({{"guid",VansSerializedValue::String(text)}})}});
    const auto edit=m_PcgSceneEdits->Set(
        {DocumentPropertySpace::Scene,"/settings/vegetation"},std::move(reference));
    if (!edit) return {false,edit.message};
    m_PcgSceneRecipeGuid=text;
    const auto preview=RefreshPcgRecipePreview();
    return {true,preview.success?"Recipe bound. Save the scene to keep this binding.":preview.message};
}
namespace
{
auto RegionIn(VansPcgRecipeAsset& recipe,const std::string& id)
{
    return std::find_if(recipe.regions.begin(),recipe.regions.end(),[&](const auto& r){return r.id==id;});
}
auto LayerIn(VansPcgRegion& region,const std::string& id)
{
    return std::find_if(region.layers.begin(),region.layers.end(),[&](const auto& r){return r.id==id;});
}
}
PcgLayerCreateResult EngineAPIImpl::CreatePcgLayer(const PcgLayerCreateRequest& request)
{
    PcgLayerCreateResult result;
    const auto fail=[&](std::string error){result.message=std::move(error);return result;};
    auto& manager=VansProjectManager::Get();
    auto* database=manager.GetAssetDatabase();
    if (m_PlayState!=EnginePlayState::Edit || !database) return fail("Open a project in Edit mode.");
    if (request.name.empty()) return fail("Enter a plant layer name.");
    const auto finished=FinishPcgStroke(false);
    if (!finished.success) return fail(finished.message);
    std::string error;
    VansVegetationConfigAsset recipeAsset;
    std::shared_ptr<VansOpenAssetDocument> recipeDocument;
    VansAssetGuid recipeGuid;
    const bool newRecipe=request.recipeGuid.empty();
    if (newRecipe) {
        if (request.recipeName.empty()) return fail("Enter a name for the new PCG recipe.");
        recipeGuid=VansAssetGuid::New();recipeAsset.config.name=request.recipeName;
    } else {
        if (!VansAssetGuid::TryParse(request.recipeGuid,recipeGuid)) return fail("Invalid recipe reference.");
        const auto record=database->Find(recipeGuid);
        if (!record || record->type!=VansAssetType::VegetationConfig) return fail("The recipe is unavailable.");
        recipeDocument=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
        if (!recipeDocument->sourceDocument.IsLoaded() ||
            !VansVegetationConfigCodec::Decode(recipeDocument->sourceDocument.SerializedRootSnapshot(),recipeAsset,error))
            return fail(error);
    }
    auto& recipe=recipeAsset.config;
    auto region=RegionIn(recipe,request.regionId);
    if (request.regionId.empty()) {
        if (request.regionName.empty()) return fail("Enter a region name and its world bounds.");
        VansPcgRegion created;created.id=VansAssetGuid::New().ToString();created.name=request.regionName;
        created.bounds={request.boundsMin,request.boundsMax};created.cellSize=request.cellSize;
        recipe.regions.push_back(created);region=std::prev(recipe.regions.end());
    } else if (region==recipe.regions.end()) return fail("The selected region no longer exists.");

    VansPlantTypeAsset plant;
    plant.name=request.name;plant.category=request.tree?VansPlantCategory::Tree:VansPlantCategory::Grass;
    VansPcgLayer layer;layer.id=VansAssetGuid::New().ToString();layer.name=request.name;layer.category=plant.category;
    std::vector<VansPcgMaskAsset> masks;
    const auto& repository=manager.GetAssetObjectRepository();
    if (!request.copyFrom.layerId.empty()) {
        VansAssetGuid sourceGuid;VansAssetGuid::TryParse(request.copyFrom.recipeGuid,sourceGuid);
        const auto source=repository.ResolveLatest<VansVegetationConfigAsset>(sourceGuid);
        if (!source) return fail("The source recipe is unavailable.");
        auto copy=source->config;
        auto sourceRegion=RegionIn(copy,request.copyFrom.regionId);
        if (sourceRegion==copy.regions.end()) return fail("The source region is unavailable.");
        auto sourceLayer=LayerIn(*sourceRegion,request.copyFrom.layerId);
        if (sourceLayer==sourceRegion->layers.end()) return fail("The source layer is unavailable.");
        if (!sourceLayer->overrides.empty())
            return fail("Remove instance overrides before copying this layer; they refer to its original point identities.");
        const auto sourcePlant=repository.ResolveLatest<VansPlantTypeAsset>(sourceLayer->plant);
        if (!sourcePlant || sourcePlant->category!=plant.category) return fail("The source plant category is invalid.");
        plant=*sourcePlant;plant.name=request.name;
        const auto id=layer.id;layer=*sourceLayer;layer.id=id;layer.name=request.name;layer.locked=false;
        for (const auto maskGuid : {sourceLayer->densityMask,sourceLayer->exclusionMask}) {
            if (!maskGuid.IsValid()) continue;
            const auto mask=repository.ResolveLatest<VansPcgMaskAsset>(maskGuid);
            if (!mask) return fail("A source Mask is unavailable.");
            masks.push_back(*mask);
        }
        if (masks.empty()) return fail("The source density Mask is unavailable.");
    } else {
        if (!request.maskWidth || !request.maskHeight || request.maskWidth>8192 || request.maskHeight>8192)
            return fail("Choose Mask dimensions from 1 to 8192.");
        VansPcgMaskAsset mask;mask.mask.bounds=region->bounds;mask.mask.width=request.maskWidth;mask.mask.height=request.maskHeight;
        mask.mask.pixels.assign(static_cast<std::size_t>(request.maskWidth)*request.maskHeight,0);
        masks.push_back(std::move(mask));
    }
    layer.plant=VansAssetGuid::New();layer.densityMask=VansAssetGuid::New();
    layer.exclusionMask=masks.size()>1?VansAssetGuid::New():VansAssetGuid{};
    for (std::size_t i=0;i<masks.size();++i) {
        auto& mask=masks[i];
        const auto id=i==0?layer.densityMask:layer.exclusionMask;
        mask.name=request.name+(i==0?" Density":" Exclusion");mask.pixelAsset=VansAssetGuid::New();
        mask.mask.target={region->id,layer.id,id.ToString()};
    }
    region->layers.push_back(layer);
    VansSerializedValue recipeRoot,plantRoot;
    if (!VansVegetationConfigCodec::Encode(recipe,recipeRoot,error) ||
        !VansPlantTypeAssetCodec::Encode(plant,plantRoot,error)) return fail(error);
    std::vector<VansSerializedValue> maskRoots(masks.size());
    std::vector<std::string> pixels(masks.size());
    for (std::size_t i=0;i<masks.size();++i)
        if (!VansPcgMaskAssetCodec::EncodeDefinition(masks[i],maskRoots[i],error) ||
            !VansPcgMaskAssetCodec::EncodePixels(masks[i],pixels[i],error)) return fail(error);

    // Create 是用户显式的资产创建动作；只发布新资产，已有配方仍留在作者内存中。
    // 全部新文件先分阶段写入，任一失败由既有文件事务回滚。
    namespace fs=std::filesystem;
    const auto directory=database->AssetsRoot()/"Vegetation"/layer.id;
    if (fs::exists(directory)) return fail("The new asset folder already exists.");
    struct CleanupNewBundle {
        fs::path directory;VansAssetDatabase* database;VansAssetObjectRepository* repository;
        std::vector<fs::path> sources;bool keep=false;
        ~CleanupNewBundle() {
            if (keep) return;
            for (const auto& path:sources) {
                if (const auto record=database->Find(path)) repository->Remove(record->guid);
                database->RemovePath(path);
            }
            std::error_code ignored;fs::remove_all(directory,ignored);
        }
    } cleanup{directory,database,&manager.GetAssetObjectRepository()};
    VansScopedIOContext scope(VansIODomain::Authoring,"Pcg.CreateLayer",true);
    VansStagedFileTransaction transaction;
    const auto stage=[&](const fs::path& path,VansAssetGuid guid,VansAssetType type,
        const VansSerializedValue* root,const std::string* bytes) {
        VansStagedFile file;
        if (!(root?VansJsonFileStorage::StageWrite(path,EncodeSerializedValueJson<nlohmann::ordered_json>(*root),file,error)
                  :VansFileStorage::StageWriteBytes(path,*bytes,file,error))) return false;
        transaction.Add(std::move(file));
        VansAssetMeta meta;meta.guid=guid;meta.importer=VansAssetDatabase::ImporterFor(type);
        if (type==VansAssetType::Texture) meta.SetSerializedSettings(VansSerializedValue::Object({
            {"colorSpace",VansSerializedValue::String("linear")},{"useCompress",VansSerializedValue::Bool(false)},
            {"needMip",VansSerializedValue::Bool(false)},{"importChannel",VansSerializedValue::Int(1)},
            {"precision",VansSerializedValue::String("mid16")},{"addressMode",VansSerializedValue::String("clamp")}}));
        if (!VansAssetMetaStorage::StageSave(VansAssetMeta::MetaPathFor(path),meta,file,error)) return false;
        transaction.Add(std::move(file));cleanup.sources.push_back(path);return true;
    };
    if (!stage(directory/"Plant.vplant",layer.plant,VansAssetType::PlantType,&plantRoot,nullptr)) return fail(error);
    for (std::size_t i=0;i<masks.size();++i) {
        const auto name=i==0?"Density":"Exclusion";
        if (!stage(directory/(std::string(name)+".png"),masks[i].pixelAsset,VansAssetType::Texture,nullptr,&pixels[i]) ||
            !stage(directory/(std::string(name)+".vpcgmask"),i==0?layer.densityMask:layer.exclusionMask,
                VansAssetType::PcgMask,&maskRoots[i],nullptr)) return fail(error);
    }
    if (newRecipe && !stage(directory/"Recipe.json",recipeGuid,VansAssetType::VegetationConfig,&recipeRoot,nullptr))
        return fail(error);
    if (!transaction.Publish(error)) return fail(error);
    std::vector<VansAssetRecord> records;
    for (const auto& path:cleanup.sources) {
        if (!database->RegisterOrRefresh(path,VansAssetOperationPolicy::ReadOnly(),error)) return fail(error);
        records.push_back(*database->Find(path));
    }
    const auto published=VansAssetObjectBootstrapper::Publish(records,manager.GetAssetObjectRepository(),database->All());
    if (!published) return fail(published.errors.empty()?"Cannot publish new PCG assets.":published.errors.front());
    if (recipeDocument) {
        const auto edit=VansAssetDocumentEditService::ReplaceRoot(recipeDocument->sourceDocument,std::move(recipeRoot));
        if (!edit) return fail(edit.message);
    }
    cleanup.keep=true;
    result.success=true;
    result.target={recipeGuid.ToString(),region->id,layer.id,layer.densityMask.ToString()};
    result.message="Created independent plant and Mask assets. Save the recipe to keep layer changes.";
    const auto selected=SelectPcgBrushTarget(result.target,false);
    if (!selected.success) result.message=selected.message;
    const auto refreshed=RefreshPcgRecipePreview();
    if (!refreshed.success) result.message=refreshed.message;
    return result;
}
PcgEditorOperationResult EngineAPIImpl::RemovePcgLayer(const PcgBrushTarget& target)
{
    if (m_PlayState!=EnginePlayState::Edit) return {false,"Remove layers in Edit mode."};
    VansAssetGuid guid;VansAssetGuid::TryParse(target.recipeGuid,guid);
    const auto* database=VansProjectManager::Get().GetAssetDatabase();
    const auto record=database?database->Find(guid):std::nullopt;
    if (!record || record->type!=VansAssetType::VegetationConfig) return {false,"The recipe is unavailable."};
    auto document=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
    VansVegetationConfigAsset asset;std::string error;
    if (!VansVegetationConfigCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),asset,error)) return {false,error};
    auto region=RegionIn(asset.config,target.regionId);
    if (region==asset.config.regions.end()) return {false,"The region is unavailable."};
    auto layer=LayerIn(*region,target.layerId);
    if (layer==region->layers.end() || layer->locked) return {false,"Select an unlocked layer."};
    const auto finished=FinishPcgStroke(false);if (!finished.success) return finished;
    region->layers.erase(layer);
    VansSerializedValue root;
    if (!VansVegetationConfigCodec::Encode(asset.config,root,error)) return {false,error};
    const auto edited=VansAssetDocumentEditService::ReplaceRoot(document->sourceDocument,std::move(root));
    if (!edited) return {false,edited.message};
    SelectPcgBrushTarget({},false);
    const auto refreshed=RefreshPcgRecipePreview();
    return {true,refreshed.success?"Layer removed. Its assets remain in the Project browser; Undo restores the layer.":refreshed.message};
}

PcgEditorOperationResult EngineAPIImpl::CreatePcgExclusionMask(const PcgBrushTarget& target)
{
    if (m_PlayState!=EnginePlayState::Edit) return {false,"Create a Mask in Edit mode."};
    auto& manager=VansProjectManager::Get();auto* database=manager.GetAssetDatabase();
    VansAssetGuid guid;VansAssetGuid::TryParse(target.recipeGuid,guid);
    const auto record=database?database->Find(guid):std::nullopt;
    if (!record) return {false,"The recipe is unavailable."};
    auto document=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
    VansVegetationConfigAsset asset;std::string error;
    if (!VansVegetationConfigCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),asset,error)) return {false,error};
    auto region=RegionIn(asset.config,target.regionId);
    if (region==asset.config.regions.end()) return {false,"The region is unavailable."};
    auto layer=LayerIn(*region,target.layerId);
    if (layer==region->layers.end() || layer->locked || layer->exclusionMask.IsValid())
        return {false,"Select an unlocked layer without an exclusion Mask."};
    const auto density=manager.GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(layer->densityMask);
    if (!density) return {false,"The density Mask is unavailable."};
    const auto finished=FinishPcgStroke(false);if (!finished.success) return finished;
    VansPcgMaskAsset mask=*density;
    const auto maskGuid=VansAssetGuid::New();mask.pixelAsset=VansAssetGuid::New();
    mask.mask.target={region->id,layer->id,maskGuid.ToString()};mask.name=layer->name+" Exclusion";
    std::fill(mask.mask.pixels.begin(),mask.mask.pixels.end(),0);
    layer->exclusionMask=maskGuid;
    VansSerializedValue maskRoot,recipeRoot;std::string pixels;
    if (!VansPcgMaskAssetCodec::EncodeDefinition(mask,maskRoot,error) ||
        !VansPcgMaskAssetCodec::EncodePixels(mask,pixels,error) ||
        !VansVegetationConfigCodec::Encode(asset.config,recipeRoot,error)) return {false,error};
    const auto directory=database->AssetsRoot()/"Vegetation"/maskGuid.ToString();
    if (std::filesystem::exists(directory)) return {false,"The new Mask folder already exists."};
    const auto maskPath=directory/"Exclusion.vpcgmask",pixelPath=directory/"Exclusion.png";
    struct Cleanup {
        std::filesystem::path directory;VansAssetDatabase* database;VansAssetObjectRepository* repository;
        std::vector<std::filesystem::path> sources;bool keep=false;
        ~Cleanup() {if (keep) return;for (const auto& path:sources) {
            if (const auto entry=database->Find(path)) repository->Remove(entry->guid);database->RemovePath(path);
        } std::error_code ec;std::filesystem::remove_all(directory,ec);}
    } cleanup{directory,database,&manager.GetAssetObjectRepository(),{pixelPath,maskPath}};
    VansScopedIOContext io(VansIODomain::Authoring,"Pcg.CreateExclusion",true);
    VansStagedFileTransaction transaction;VansStagedFile file;
    if (!VansFileStorage::StageWriteBytes(pixelPath,pixels,file,error)) return {false,error};transaction.Add(std::move(file));
    if (!VansJsonFileStorage::StageWrite(maskPath,EncodeSerializedValueJson<nlohmann::ordered_json>(maskRoot),file,error)) return {false,error};
    transaction.Add(std::move(file));
    VansAssetMeta pixelMeta;pixelMeta.guid=mask.pixelAsset;pixelMeta.importer=VansAssetDatabase::ImporterFor(VansAssetType::Texture);
    pixelMeta.SetSerializedSettings(VansSerializedValue::Object({
        {"colorSpace",VansSerializedValue::String("linear")},{"useCompress",VansSerializedValue::Bool(false)},
        {"needMip",VansSerializedValue::Bool(false)},{"importChannel",VansSerializedValue::Int(1)},
        {"precision",VansSerializedValue::String("mid16")},{"addressMode",VansSerializedValue::String("clamp")}}));
    VansAssetMeta maskMeta;maskMeta.guid=maskGuid;maskMeta.importer=VansAssetDatabase::ImporterFor(VansAssetType::PcgMask);
    if (!VansAssetMetaStorage::StageSave(VansAssetMeta::MetaPathFor(pixelPath),pixelMeta,file,error)) return {false,error};transaction.Add(std::move(file));
    if (!VansAssetMetaStorage::StageSave(VansAssetMeta::MetaPathFor(maskPath),maskMeta,file,error)) return {false,error};transaction.Add(std::move(file));
    if (!transaction.Publish(error)) return {false,error};
    std::vector<VansAssetRecord> records;
    for (const auto& path:cleanup.sources) {
        if (!database->RegisterOrRefresh(path,VansAssetOperationPolicy::ReadOnly(),error)) return {false,error};
        records.push_back(*database->Find(path));
    }
    const auto published=VansAssetObjectBootstrapper::Publish(records,manager.GetAssetObjectRepository(),database->All());
    if (!published) return {false,published.errors.empty()?"Cannot publish the Mask.":published.errors.front()};
    const auto edit=VansAssetDocumentEditService::ReplaceRoot(document->sourceDocument,std::move(recipeRoot));
    if (!edit) return {false,edit.message};
    cleanup.keep=true;
    const auto selected=SelectPcgBrushTarget({target.recipeGuid,target.regionId,target.layerId,maskGuid.ToString()},false);
    if (!selected.success) return selected;
    return {true,"Independent exclusion Mask created. Save the recipe to keep its binding."};
}
}
