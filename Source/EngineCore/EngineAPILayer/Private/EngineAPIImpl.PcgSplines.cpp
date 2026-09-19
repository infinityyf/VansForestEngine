#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../PcgCore/VansPcgSplineField.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../PcgCore/Storage/VansPcgSplineFieldStorage.h"
#include "../../PcgCore/Serialization/VansPcgSplineAssetCodec.h"
#include "../../EditorCore/VansAssetDocumentRegistry.h"
#include "../../EditorCore/VansAssetDocumentEditService.h"
#include "../../EditorCore/VansEditorAssetSaveService.h"
#include "../../EditorCore/VansSceneEditService.h"
#include "../../EditorCore/Terrain/VansTerrainAuthoringSession.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansAssetObjectBootstrapper.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansCamera.h"
#include "../../RenderCore/VansCameraControlArbiter.h"
#include "../../Util/VansJobSystem.h"
#include <nlohmann/json.hpp>
#include <future>
#include <deque>
#include <set>
#include "../../PcgCore/VansPcgBatchPlan.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../../Util/VansLog.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans::EditorAPI
{
namespace
{
PcgSplineItem ToPublic(const VansPcgSpline& source)
{
    auto resolved=source;
    VansPcgSplineEvaluator::ResolveAutoTangents(resolved);
    PcgSplineItem result;
    result.id=source.id;result.name=source.name;result.materialGuid=source.material.IsValid()?source.material.ToString():"";
    result.kind=static_cast<PcgSplineKind>(source.kind);result.enabled=source.enabled;result.locked=source.locked;
    result.excludeVegetation=source.excludeVegetation;result.vegetationFade=source.vegetationFade;
    result.priority=source.priority;result.shoulder=source.shoulder;result.blendWidth=source.blendWidth;
    result.waterSurfaceDrop=source.waterSurfaceDrop;
    result.waterBlendWidthMeters=source.waterBlendWidthMeters;
    result.waterBlendStartMeters=source.waterBlendStartMeters;result.waterBlendEndMeters=source.waterBlendEndMeters;
    result.carveRiverbed=source.carveRiverbed;
    result.wetBankWidthMeters=source.wetBankWidthMeters;result.wetnessStrength=source.wetnessStrength;
    result.surfaceOffset=source.surfaceOffset;result.textureRepeat=source.textureRepeat;result.flowSign=static_cast<float>(source.flowSign);
    result.fadeInDistance=source.fadeInDistance;result.fadeOutDistance=source.fadeOutDistance;
    result.normalFlowEnabled=source.normalFlowEnabled;
    for (const auto& p:resolved.points)
    {
        PcgSplinePoint q;q.id=p.id;q.position=p.position;q.arrive=p.arrive;q.leave=p.leave;
        q.tangentMode=static_cast<PcgSplineTangentMode>(p.tangentMode);q.outgoing=static_cast<PcgSplineSegmentMode>(p.outgoing);
        q.leftWidth=p.leftWidth;q.rightWidth=p.rightWidth;q.linkedWidth=p.linkedWidth;
        q.bankAngleDegrees=p.bankAngleDegrees;q.depth=p.depth;q.speed=p.speed;result.points.push_back(q);
        result.points.back().bankSteepness=p.bankSteepness;
    }
    return result;
}
bool FromPublic(const PcgSplineItem& source,VansPcgSpline& result,std::string& error)
{
    if (source.kind!=static_cast<PcgSplineKind>(result.kind)) {error="Spline kind cannot change.";return false;}
    result.name=source.name;result.enabled=source.enabled;result.locked=source.locked;result.priority=source.priority;
    if (source.materialGuid.empty()) result.material={};
    else if (!VansAssetGuid::TryParse(source.materialGuid,result.material)) {error="Invalid road material reference.";return false;}
    result.excludeVegetation=source.excludeVegetation;result.vegetationFade=source.vegetationFade;
    result.shoulder=source.shoulder;result.blendWidth=source.blendWidth;result.surfaceOffset=source.surfaceOffset;
    result.waterSurfaceDrop=source.waterSurfaceDrop;
    result.waterBlendWidthMeters=source.waterBlendWidthMeters;
    result.waterBlendStartMeters=source.waterBlendStartMeters;result.waterBlendEndMeters=source.waterBlendEndMeters;
    result.carveRiverbed=source.carveRiverbed;
    result.wetBankWidthMeters=source.wetBankWidthMeters;result.wetnessStrength=source.wetnessStrength;
    result.textureRepeat=source.textureRepeat;result.flowSign=source.flowSign<0?-1:1;
    result.fadeInDistance=source.fadeInDistance;result.fadeOutDistance=source.fadeOutDistance;
    result.normalFlowEnabled=source.normalFlowEnabled;
    const auto previousPoints=result.points;
    result.points.clear();
    for (const auto& p:source.points)
    {
        VansPcgSplinePoint q;q.id=p.id;q.position=p.position;q.arrive=p.arrive;q.leave=p.leave;
        q.tangentMode=static_cast<VansPcgSplineTangentMode>(p.tangentMode);q.outgoing=static_cast<VansPcgSplineSegmentMode>(p.outgoing);
        q.leftWidth=p.leftWidth;q.rightWidth=p.linkedWidth?p.leftWidth:p.rightWidth;q.linkedWidth=p.linkedWidth;
        q.bankAngleDegrees=p.bankAngleDegrees;q.depth=p.depth;q.bankSteepness=p.bankSteepness;q.speed=p.speed;result.points.push_back(q);
        auto& resolved=result.points.back();
        if (q.tangentMode==VansPcgSplineTangentMode::Aligned || q.tangentMode==VansPcgSplineTangentMode::Mirrored)
        {
            const auto old=std::find_if(previousPoints.begin(),previousPoints.end(),[&](const auto& item){return item.id==q.id;});
            const bool incoming=old!=previousPoints.end() && old->arrive!=q.arrive && old->leave==q.leave;
            const auto& handle=incoming?q.arrive:q.leave;
            auto& opposite=incoming?resolved.leave:resolved.arrive;
            const glm::vec3 direction(handle[0],handle[1],handle[2]);
            const float length=glm::length(direction);
            if (length>1e-6f)
            {
                const float otherLength=q.tangentMode==VansPcgSplineTangentMode::Mirrored?length:
                    glm::length(glm::vec3(opposite[0],opposite[1],opposite[2]));
                const auto value=-direction*(otherLength/length);opposite={value.x,value.y,value.z};
            }
        }
    }
    VansPcgSplineEvaluator::ResolveAutoTangents(result);
    return true;
}
std::string SceneSplineGuid(const VansSceneDocument* document)
{
    if (!document) return {};
    const auto snapshot=document->CreateSnapshot();
    if (const auto* settings=FindObjectField(snapshot.Root(),"settings"))
        if (const auto* value=FindObjectField(*settings,"pcgSplines"))
        {
            SerializedObjectReferenceValue reference;
            if (TryReadSerializedObjectReference(*value,reference)) return reference.guid;
        }
    return {};
}
struct SplineBuildResult
{
    std::shared_ptr<const VansPcgSplineFieldSnapshot> field;
    std::string error;
    std::uint64_t request=0;
    double milliseconds=0;
};
}

namespace
{
struct SplineVegetationResult
{
    std::string recipeGuid;
    std::shared_ptr<const VansPcgSplineFieldSnapshot> field;
    std::shared_ptr<const VansAssetObjectRepository> repository;
    std::vector<VansAssetGuid> dependencies;
    std::deque<std::shared_ptr<const VansPcgBatchUpdate>> updates;
    std::string error;
    std::uint64_t candidates=0;
    double milliseconds=0;
};
bool DependenciesMatch(const SplineVegetationResult& result,const VansAssetObjectRepository& repository)
{
    if (!result.repository) return true;
    for (const auto guid:result.dependencies)
    {
        VansAssetObjectSnapshotInfo before,now;
        if (!result.repository->FindInfo(guid,before) || !repository.FindInfo(guid,now) || before.generation!=now.generation) return false;
    }
    return true;
}
// 以最后已提交的植被场比较新旧范围；不能只使用最近一次拖动的 changedTiles，否则会漏掉更早的旧位置。
std::optional<VansPcgBounds> VegetationChanges(const VansPcgSplineFieldSnapshot& current,
    const std::shared_ptr<const VansPcgSplineFieldSnapshot>& previous)
{
    if (!previous || current.worldSize!=previous->worldSize || current.resolution!=previous->resolution ||
        current.terrainGuid!=previous->terrainGuid || current.terrainFingerprint!=previous->terrainFingerprint)
        return VansPcgBounds{{-current.worldSize*.5f,-current.worldSize*.5f},{current.worldSize*.5f,current.worldSize*.5f}};
    std::optional<VansPcgBounds> bounds;
    std::set<std::uint64_t> keys;
    for (const auto& [key,tile]:current.tiles) keys.insert(key);
    for (const auto& [key,tile]:previous->tiles) keys.insert(key);
    for (const auto key:keys)
    {
        const auto x=std::uint32_t(key),z=std::uint32_t(key>>32);
        const auto* a=current.FindTile(x,z);const auto* b=previous->FindTile(x,z);
        if (a==b) continue;
        if (a && b && (current.effectiveTerrain==previous->effectiveTerrain || a->terrainShapeFingerprint==b->terrainShapeFingerprint) &&
            a->vegetationExclusion==b->vegetationExclusion) continue;
        const float size=current.texelSize*VANS_SPLINE_TILE_SIZE;
        const float halo=std::max(current.texelSize,current.worldSize/current.effectiveTerrain->width);
        VansPcgBounds cell{{x*size-current.worldSize*.5f-halo,z*size-current.worldSize*.5f-halo},
            {(x+1)*size-current.worldSize*.5f+halo,(z+1)*size-current.worldSize*.5f+halo}};
        if (!bounds) bounds=cell;
        else for (int i=0;i<2;++i) {bounds->min[i]=std::min(bounds->min[i],cell.min[i]);bounds->max[i]=std::max(bounds->max[i],cell.max[i]);}
    }
    return bounds;
}
void GenerateSplineVegetation(const VansPcgRecipeAsset& recipe,const VansPcgBounds& dirty,SplineVegetationResult& result)
{
    const auto start=std::chrono::steady_clock::now();
    for (const auto& region:recipe.regions) if (region.enabled && region.surface.terrain==result.field->terrainGuid)
        for (const auto& layer:region.layers) if (layer.enabled)
        {
            if (region.bounds.max[0]<=dirty.min[0] || region.bounds.max[1]<=dirty.min[1] ||
                region.bounds.min[0]>=dirty.max[0] || region.bounds.min[1]>=dirty.max[1]) continue;
            std::optional<VansPcgBounds> coverage;
            if (layer.source==VansPcgSourceMode::Density && layer.overrides.empty() && layer.addedInstances.empty() && layer.placement.rootOffset==0)
            {
                VansPcgBounds bounds;
                for (int i=0;i<2;++i)
                {
                    bounds.min[i]=std::floor(std::max(region.bounds.min[i],dirty.min[i])/region.cellSize)*region.cellSize;
                    bounds.max[i]=std::ceil(std::min(region.bounds.max[i],dirty.max[i])/region.cellSize)*region.cellSize;
                }
                coverage=bounds;
            }
            VansPcgRecipeAsset selected;selected.name=recipe.name;selected.regions={region};selected.regions.front().layers={layer};
            const auto generated=VansPcgExecutor::Generate(selected,*result.repository,
                [&](const VansPcgSurfaceBinding&,std::string& error) {return CreatePcgTerrainSurface(result.field->effectiveTerrain,error);},coverage,result.field);
            if (!generated) {result.error=generated.error;return;}
            for (const auto& output:generated.layers)
            {
                result.candidates+=output.stats.candidates;
                auto update=std::make_shared<VansPcgBatchUpdate>();
                if (!BuildPcgBatchUpdate(region,output,coverage,*update,result.error)) return;
                // 小格逐帧提交，删除为空的格也必须提交；避免一次重建全部驻留 GPU 批次。
                if (!coverage) {result.updates.push_back(std::move(update));continue;}
                const auto minX=std::int64_t(std::floor(coverage->min[0]/region.cellSize));
                const auto maxX=std::int64_t(std::ceil(coverage->max[0]/region.cellSize));
                const auto minZ=std::int64_t(std::floor(coverage->min[1]/region.cellSize));
                const auto maxZ=std::int64_t(std::ceil(coverage->max[1]/region.cellSize));
                for (auto z=minZ;z<maxZ;++z) for (auto x=minX;x<maxX;++x)
                {
                    auto cell=std::make_shared<VansPcgBatchUpdate>();cell->region=region.id;cell->layer=layer.id;cell->cellSize=region.cellSize;
                    cell->coverage=VansPcgBounds{{float(x*region.cellSize),float(z*region.cellSize)},{float((x+1)*region.cellSize),float((z+1)*region.cellSize)}};
                    for (const auto& [key,batch]:update->batches) if(key.x==x && key.z==z) cell->batches.emplace(key,batch);
                    result.updates.push_back(std::move(cell));
                }
            }
        }
    result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}

struct EngineAPIImpl::PcgSplineAuthoringState
{
    std::string projectRoot,guid,selectedSpline,selectedPoint,message;
    std::uint64_t sceneRevision=0,request=0,observedDocumentState=0;
    std::shared_ptr<VansOpenAssetDocument> document;
    bool toolEnabled=false,dragging=false,needsBuild=false;
    VansPcgSplineAsset working;
    std::shared_ptr<const VansTerrainAsset> base;
    std::future<SplineBuildResult> future;
    std::future<SplineVegetationResult> vegetationFuture;
    SplineVegetationResult vegetationReady;
    std::shared_ptr<const VansPcgSplineFieldSnapshot> vegetationField;
    bool vegetationInitialized=false;
    std::chrono::steady_clock::time_point lastVegetationSubmit{};
};

void EngineAPIImpl::TickPcgSplineAuthoring()
{
    auto& manager=VansProjectManager::Get();
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    if (!m_PcgSplineAuthoring || m_PcgSplineAuthoring->projectRoot!=manager.GetProjectRootPath() ||
        m_PcgSplineAuthoring->sceneRevision!=m_SceneContentRevision)
    {
        m_PcgSplineAuthoring=std::make_shared<PcgSplineAuthoringState>();
        m_PcgSplineAuthoring->projectRoot=manager.GetProjectRootPath();
        m_PcgSplineAuthoring->sceneRevision=m_SceneContentRevision;
    }
    auto& state=*m_PcgSplineAuthoring;
    const auto guid=m_PcgSceneDocument?SceneSplineGuid(m_PcgSceneDocument):
        (scene && scene->GetSplineAssetGuid().IsValid()?scene->GetSplineAssetGuid().ToString():"");
    if (guid!=state.guid)
    {
        ++state.request;state.guid=guid;state.document.reset();state.dragging=false;state.needsBuild=false;
        state.observedDocumentState=0;state.selectedSpline.clear();state.selectedPoint.clear();state.toolEnabled=false;
        VansAssetGuid assetGuid;VansAssetGuid::TryParse(guid,assetGuid);
        const auto* database=manager.GetAssetDatabase();
        const auto record=database?database->Find(assetGuid):std::nullopt;
        if (record && record->type==VansAssetType::PcgSpline)
        {
            state.document=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
            if (state.document) state.document->saveWithScene=true;
        }
        if (scene) scene->SetSplineAssetGuid(assetGuid);
        if (guid.empty() && scene && scene->GetSplineFieldSnapshot())
        {
            // 解绑也从基础高度重新构建，清除旧页和生成道路。
            state.working={};state.working.name="Unbound splines";
            state.working.terrain=scene->GetSplineFieldSnapshot()->terrainGuid;
            state.base=manager.GetAssetObjectRepository().ResolveLatest<VansTerrainAsset>(state.working.terrain);
            state.needsBuild=bool(state.base);
        }
    }
    if (m_PlayState!=EnginePlayState::Edit) {state.toolEnabled=false;return;}
    if (state.document && !state.dragging && state.observedDocumentState!=state.document->sourceDocument.CurrentStateId())
    {
        state.observedDocumentState=state.document->sourceDocument.CurrentStateId();
        if (VansPcgSplineAssetCodec::Decode(state.document->sourceDocument.SerializedRootSnapshot(),state.working,state.message))
        {
            state.base=manager.GetAssetObjectRepository().ResolveLatest<VansTerrainAsset>(state.working.terrain);
            state.needsBuild=bool(state.base);++state.request;
        }
    }
    if (!state.vegetationInitialized && scene && scene->IsSceneReady())
    {state.vegetationField=scene->GetSplineFieldSnapshot();state.vegetationInitialized=true;}
    if (state.future.valid() && state.future.wait_for(std::chrono::seconds(0))==std::future_status::ready)
    {
        auto result=state.future.get();
        if ((result.request==state.request || (state.dragging && result.request<state.request)) && scene && scene->IsSceneReady())
        {
            if (!result.field) state.message=result.error;
            else
            {
                const auto start=std::chrono::steady_clock::now();
                if (scene->PublishSplineField(result.field,state.message))
                {
                    state.message.clear();
                    const double publishMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                    VANS_LOG("[PcgSplinePreview] fieldMs="<<result.milliseconds<<" publishMs="<<publishMs<<
                        " tiles="<<result.field->rebuiltTileCount<<" request="<<result.request);
                }
            }
        }
    }
    if (state.needsBuild && !state.future.valid() && scene && scene->IsSceneReady())
    {
        state.needsBuild=false;
        auto promise=std::make_shared<std::promise<SplineBuildResult>>();state.future=promise->get_future();
        const auto asset=state.working;const auto base=state.base;const auto previous=scene->GetSplineFieldSnapshot();
        const auto request=state.request;
        VansJobSystem::Get().QueueJob([promise,asset,base,previous,request]() {
            SplineBuildResult result;result.request=request;
            const auto start=std::chrono::steady_clock::now();
            try {result.field=VansPcgSplineFieldBuilder::Build(asset,base,previous,result.error);}
            catch (const std::exception& exception) {result.error=exception.what();}
            result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            promise->set_value(std::move(result));
        });
    }
    if (scene && scene->IsSceneReady())
    {
        auto& repository=manager.GetAssetObjectRepository();
        if (state.vegetationFuture.valid() && state.vegetationFuture.wait_for(std::chrono::seconds(0))==std::future_status::ready)
        {
            state.vegetationReady=state.vegetationFuture.get();
            VANS_LOG("[PcgSplinePreview] vegetationMs="<<state.vegetationReady.milliseconds<<" candidates="<<state.vegetationReady.candidates<<" cells="<<state.vegetationReady.updates.size());
            if (!state.vegetationReady.error.empty())
            {state.message=state.vegetationReady.error;state.vegetationReady.updates.clear();state.vegetationField=state.vegetationReady.field;}
        }
        if ((state.vegetationReady.repository && state.vegetationReady.recipeGuid!=m_PcgSceneRecipeGuid) ||
            !DependenciesMatch(state.vegetationReady,repository))
        {state.vegetationReady={};state.vegetationField.reset();}
        const auto now=std::chrono::steady_clock::now();
        if (!state.vegetationReady.updates.empty() && now-state.lastVegetationSubmit>=std::chrono::milliseconds(16))
        {
            scene->QueueVegetationUpdate(state.vegetationReady.updates.front());state.vegetationReady.updates.pop_front();
            state.lastVegetationSubmit=now;
        }
        if (state.vegetationReady.field && state.vegetationReady.updates.empty())
        {state.vegetationField=state.vegetationReady.field;state.vegetationReady={};}
        const auto current=scene->GetSplineFieldSnapshot();
        if (current && current!=state.vegetationField && !state.vegetationFuture.valid() && state.vegetationReady.updates.empty())
        {
            const auto bounds=VegetationChanges(*current,state.vegetationField);
            VansAssetGuid recipeGuid;VansAssetGuid::TryParse(m_PcgSceneRecipeGuid,recipeGuid);
            const auto recipe=repository.ResolveLatest<VansVegetationConfigAsset>(recipeGuid);
            if (!bounds || !recipe) state.vegetationField=current;
            else
            {
                SplineVegetationResult result;result.recipeGuid=m_PcgSceneRecipeGuid;
                result.field=current;result.repository=repository.CreateSnapshot();
                result.dependencies=recipe->config.Dependencies();result.dependencies.push_back(recipeGuid);
                auto promise=std::make_shared<std::promise<SplineVegetationResult>>();state.vegetationFuture=promise->get_future();
                VansJobSystem::Get().QueueJob([promise,recipe,bounds,result=std::move(result)]() mutable {
                    try {GenerateSplineVegetation(recipe->config,*bounds,result);}
                    catch (const std::exception& error) {result.error=error.what();result.updates.clear();}
                    promise->set_value(std::move(result));
                });
            }
        }
    }
}

PcgEditorOperationResult EngineAPIImpl::RequestPcgSplinePreview()
{
    auto& state=*m_PcgSplineAuthoring;
    if (!state.dragging && state.document &&
        !VansPcgSplineAssetCodec::Decode(state.document->sourceDocument.SerializedRootSnapshot(),state.working,state.message))
        return {false,state.message};
    state.base=VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansTerrainAsset>(state.working.terrain);
    if (m_TerrainAuthoringSession && GetTerrainEditorSnapshot().assetGuid==state.working.terrain.ToString())
    {
        const auto& working=m_TerrainAuthoringSession->WorkingAsset();
        if (!state.base || state.base->heights!=working.heights || state.base->settings.terrainSize!=working.settings.terrainSize ||
            state.base->settings.maxHeight!=working.settings.maxHeight || state.base->settings.heightOffset!=working.settings.heightOffset)
            state.base=std::make_shared<VansTerrainAsset>(working);
    }
    if (!state.base) return {false,"Spline terrain is unavailable."};
    state.needsBuild=true;++state.request;
    if (state.document) state.observedDocumentState=state.document->sourceDocument.CurrentStateId();
    TickPcgSplineAuthoring();return {true,{}};
}

PcgSplineSnapshot EngineAPIImpl::GetPcgSplineSnapshot()
{
    TickPcgSplineAuthoring();const auto& state=*m_PcgSplineAuthoring;PcgSplineSnapshot result;
    result.assetGuid=state.guid;result.toolEnabled=state.toolEnabled;result.selectedSpline=state.selectedSpline;
    result.selectedPoint=state.selectedPoint;result.message=state.message;
    result.building=state.needsBuild||state.future.valid();
    if (!state.document) return result;
    result.available=true;result.editable=m_PlayState==EnginePlayState::Edit;result.dirty=state.document->IsDirty()||state.dragging;
    result.documentState=state.document->sourceDocument.CurrentStateId();
    result.canUndo=!state.dragging&&VansAssetDocumentEditService::CanUndo(state.document->sourceDocument);
    result.canRedo=!state.dragging&&VansAssetDocumentEditService::CanRedo(state.document->sourceDocument);
    result.terrainGuid=state.working.terrain.ToString();result.fieldTexelSize=state.working.fieldTexelSize;
    result.sampleSpacing=state.working.sampleSpacing;result.curveTolerance=state.working.curveTolerance;
    result.heightConflictThreshold=state.working.heightConflictThreshold;
    for (const auto& spline:state.working.splines)
    {
        result.splines.push_back(ToPublic(spline));
        if (!spline.enabled) continue;
        VansPcgEvaluatedSpline evaluated;std::string error;
        if (!VansPcgSplineEvaluator::Evaluate(spline,state.working.sampleSpacing,state.working.curveTolerance,evaluated,error)) continue;
        for (std::size_t i=0;i<spline.points.size();++i)
            result.splines.back().points[i].effectiveFlowSpeed=spline.points[i].speed*
                VansPcgSplineEvaluator::EndpointFade(spline,evaluated.pointDistances[i],evaluated.length);
        if (!state.toolEnabled) continue;
        PcgSplineGuide guide;guide.id=spline.id;
        const auto point=[](glm::vec3 p) {return std::array<float,3>{p.x,p.y,p.z};};
        for (const auto& sample:evaluated.samples)
        {
            auto right=sample.right;right.y=std::tan(glm::radians(sample.bankAngleDegrees));
            guide.center.push_back(point(sample.position));guide.left.push_back(point(sample.position-right*sample.leftWidth));
            guide.right.push_back(point(sample.position+right*sample.rightWidth));
        }
        result.guides.push_back(std::move(guide));
    }
    const auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    if (scene && scene->GetSplineFieldSnapshot())
    {
        const auto& field=*scene->GetSplineFieldSnapshot();result.warnings=field.warnings;
        for (const auto& p:field.uncoveredBankPoints) result.uncoveredBankPoints.push_back({p.x,p.y,p.z});
        result.activeTiles=field.tiles.size();result.rebuiltTiles=field.rebuiltTileCount;
        if (state.toolEnabled)
        {
            PcgSplineGuide flow;flow.id="resolved-flow";
            const auto stride=std::max(1u,static_cast<std::uint32_t>(std::ceil(6.f/field.texelSize)));
            for (const auto& [key,tile]:field.tiles) if (tile->hasRiver)
                for (std::uint32_t z=0;z<VANS_SPLINE_TILE_SIZE;++z)
                    for (std::uint32_t x=0;x<VANS_SPLINE_TILE_SIZE;++x)
                    {
                        const auto gx=tile->x*VANS_SPLINE_TILE_SIZE+x,gz=tile->z*VANS_SPLINE_TILE_SIZE+z;
                        if (gx%stride || gz%stride || flow.flowOrigins.size()>=2048) continue;
                        const auto pixel=std::size_t(z+VANS_SPLINE_TILE_BORDER)*VANS_SPLINE_TILE_EXTENT+x+VANS_SPLINE_TILE_BORDER;
                        if (tile->coverage[pixel].y<.1f) continue;
                        const auto velocity=tile->velocities[pixel];
                        flow.flowOrigins.push_back({(gx+.5f)*field.texelSize-field.worldSize*.5f,
                            tile->heights[pixel].y+.1f,(gz+.5f)*field.texelSize-field.worldSize*.5f});
                        flow.flowVectors.push_back({velocity.x,0,velocity.y});
                    }
            result.guides.push_back(std::move(flow));
        }
    }
    return result;
}

PcgEditorOperationResult EngineAPIImpl::SelectPcgSpline(const std::string& spline,const std::string& point,bool enabled)
{
    TickPcgSplineAuthoring();auto& state=*m_PcgSplineAuthoring;
    if (state.dragging) return {false,"Finish the current spline edit first."};
    if (enabled)
    {
        if (!state.document || m_PlayState!=EnginePlayState::Edit) return {false,"Select an editable spline asset."};
        SelectPcgBrushTarget({},false);
        // Scene 输入优先级在窗口统一仲裁，切换到样条工具不改变地形笔刷参数。
    }
    state.selectedSpline=spline;state.selectedPoint=point;state.toolEnabled=enabled;return {true,{}};
}

PcgEditorOperationResult EngineAPIImpl::FinishPcgSplineEdit()
{
    if (!m_PcgSplineAuthoring) return {true,{}};
    auto& state=*m_PcgSplineAuthoring;
    if (state.dragging && state.document)
    {
        VansSerializedValue root;std::string error;
        if (!VansPcgSplineAssetCodec::Encode(state.working,root,error)) return {false,error};
        const auto edit=VansAssetDocumentEditService::ReplaceRoot(state.document->sourceDocument,std::move(root));
        if (!edit) return {false,edit.message};
        state.dragging=false;
    }
    state.toolEnabled=false;
    return {true,{}};
}

PcgEditorOperationResult EngineAPIImpl::ApplyPcgSplineEdit(const PcgSplineEditRequest& request)
{
    TickPcgSplineAuthoring();auto& state=*m_PcgSplineAuthoring;
    if (!state.document || m_PlayState!=EnginePlayState::Edit) return {false,"Open an editable spline asset."};
    auto& document=state.document->sourceDocument;
    if (request.phase==PcgSplineEditPhase::Cancel)
    {state.dragging=false;return RequestPcgSplinePreview();}
    if (request.documentState!=document.CurrentStateId())
    {state.dragging=false;RequestPcgSplinePreview();return {false,"Spline changed in another editor. Reload the selection."};}
    if (request.phase==PcgSplineEditPhase::Begin)
    {
        if (state.dragging) return {false,"A spline edit is already active."};
        state.dragging=true;return {true,{}};
    }
    auto asset=state.working;
    auto found=std::find_if(asset.splines.begin(),asset.splines.end(),[&](const auto& item){return item.id==request.spline.id;});
    if (found==asset.splines.end()) return {false,"The spline no longer exists."};
    if (found->locked && request.spline.locked) return {false,"Unlock the spline before editing."};
    std::string error;
    if (!FromPublic(request.spline,*found,error)) return {false,error};
    VansSerializedValue root;
    if (!VansPcgSplineAssetCodec::Encode(asset,root,error)) return {false,error};
    if (request.phase==PcgSplineEditPhase::Update)
    {
        if (!state.dragging) return {false,"Begin an edit before updating it."};
        state.working=std::move(asset);return RequestPcgSplinePreview();
    }
    const auto edit=VansAssetDocumentEditService::ReplaceRoot(document,std::move(root));
    if (!edit) return {false,edit.message};
    state.dragging=false;return RequestPcgSplinePreview();
}

PcgEditorOperationResult EngineAPIImpl::BindPcgSplineAsset(const std::string& text)
{
    if (!m_PcgSceneDocument || !m_PcgSceneEdits || m_PlayState!=EnginePlayState::Edit)
        return {false,"Open an editable scene before binding splines."};
    VansAssetGuid guid;
    if (!VansAssetGuid::TryParse(text,guid)) return {false,"Select a spline asset."};
    const auto asset=VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansPcgSplineAsset>(guid);
    if (!asset || asset->terrain.ToString()!=GetTerrainEditorSnapshot().assetGuid)
        return {false,"Spline asset must reference this scene's terrain."};
    const auto edit=m_PcgSceneEdits->Set({DocumentPropertySpace::Scene,"/settings/pcgSplines"},
        MakeSerializedProjectAssetObjectReference(text,"pcgSpline"));
    if (!edit) return {false,edit.message};
    TickPcgSplineAuthoring();return {true,"Spline asset bound. Save the scene to retain its binding."};
}

PcgEditorOperationResult EngineAPIImpl::CreatePcgSplineAsset(const std::string& name)
{
    auto& manager=VansProjectManager::Get();auto* database=manager.GetAssetDatabase();
    if (!database || !m_PcgSceneDocument || m_PlayState!=EnginePlayState::Edit) return {false,"Open an editable project scene."};
    VansPcgSplineAsset asset;asset.name=name;
    if (name.empty() || !VansAssetGuid::TryParse(GetTerrainEditorSnapshot().assetGuid,asset.terrain))
        return {false,"Name the spline asset and load a terrain first."};
    const auto guid=VansAssetGuid::New();
    const auto path=database->AssetsRoot()/"PCG"/(guid.ToString()+".vpcgspline");
    VansSerializedValue root;std::string error;
    if (!VansPcgSplineAssetCodec::Encode(asset,root,error)) return {false,error};
    VansScopedIOContext scope(VansIODomain::Authoring,"Pcg.CreateSplineAsset",true);
    VansStagedFileTransaction transaction;VansStagedFile file;
    if (!VansJsonFileStorage::StageWrite(path,EncodeSerializedValueJson<nlohmann::ordered_json>(root),file,error)) return {false,error};
    transaction.Add(std::move(file));VansAssetMeta meta;meta.guid=guid;meta.importer=VansAssetDatabase::ImporterFor(VansAssetType::PcgSpline);
    if (!VansAssetMetaStorage::StageSave(VansAssetMeta::MetaPathFor(path),meta,file,error)) return {false,error};
    transaction.Add(std::move(file));
    if (!transaction.Publish(error) || !database->RegisterOrRefresh(path,VansAssetOperationPolicy::ReadOnly(),error)) return {false,error};
    const auto published=VansAssetObjectBootstrapper::Publish({*database->Find(path)},manager.GetAssetObjectRepository(),database->All());
    if (!published) return {false,published.errors.empty()?"Cannot publish spline asset.":published.errors.front()};
    return BindPcgSplineAsset(guid.ToString());
}

PcgEditorOperationResult EngineAPIImpl::AppendPcgSplinePoint(const Ray& ray)
{
    const auto snapshot=GetPcgSplineSnapshot();
    auto& state=*m_PcgSplineAuthoring;
    if (!snapshot.editable || !state.toolEnabled || state.dragging) return {false,"Enable spline editing in Edit mode."};
    auto found=std::find_if(snapshot.splines.begin(),snapshot.splines.end(),[&](const auto& item){return item.id==snapshot.selectedSpline;});
    if (found==snapshot.splines.end() || found->locked) return {false,"Select an unlocked spline."};
    const bool prepend=!found->points.empty() && state.selectedPoint==found->points.front().id;
    if (!found->points.empty() && !prepend && state.selectedPoint!=found->points.back().id)
        return {false,"Select an open endpoint to extend the spline."};
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);VansPcgSurfaceHit hit;
    if (!scene || !RaycastPcgTerrainSurface(scene->ResolveEffectiveTerrain(state.working.terrain),
        {ray.origin.x,ray.origin.y,ray.origin.z},{ray.direction.x,ray.direction.y,ray.direction.z},100000.f,hit))
        return {false,"The pointer does not hit the terrain."};
    PcgSplineEditRequest request;request.documentState=snapshot.documentState;request.spline=*found;
    auto point=request.spline.points.empty()?PcgSplinePoint{}:(prepend?request.spline.points.front():request.spline.points.back());
    const float previousHeight=point.position[1];
    point.id=VansAssetGuid::New().ToString();point.position=hit.position;
    if (found->kind==PcgSplineKind::River && !found->points.empty()) point.position[1]=previousHeight;
    point.tangentMode=PcgSplineTangentMode::Auto;
    if (prepend) request.spline.points.insert(request.spline.points.begin(),point);
    else request.spline.points.push_back(point);
    const auto result=ApplyPcgSplineEdit(request);
    if (result.success) state.selectedPoint=point.id;
    return result;
}

PcgEditorOperationResult EngineAPIImpl::ExecutePcgSplineCommand(const PcgSplineCommandRequest& request)
{
    TickPcgSplineAuthoring();auto& state=*m_PcgSplineAuthoring;
    if (!state.document || m_PlayState!=EnginePlayState::Edit || state.dragging) return {false,"Finish editing before this operation."};
    auto& document=state.document->sourceDocument;
    if (request.command==PcgSplineCommand::Save)
    {const auto saved=VansEditorAssetSaveService::Get().SaveAsset(*this,state.document);return {bool(saved),saved.message};}
    if (request.command==PcgSplineCommand::Bake)
    {
        auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
        const auto field=scene?scene->GetSplineFieldSnapshot():nullptr;
        if (!field || state.future.valid() || state.needsBuild || field->sourceFingerprint!=VansPcgSplineAssetCodec::ContentHash(state.working))
            return {false,"Wait for a successful field update before baking."};
        VansAssetGuid guid;VansAssetGuid::TryParse(state.guid,guid);std::string error;
        if (!VansPcgSplineFieldStorage::Save(VansPcgSplineFieldStorage::CachePath(state.projectRoot,guid),*field,error)) return {false,error};
        return {true,"Fields baked. Save source edits and the scene binding to retain the complete authoring state."};
    }
    if (request.command==PcgSplineCommand::Undo || request.command==PcgSplineCommand::Redo)
    {
        const auto edit=request.command==PcgSplineCommand::Undo?VansAssetDocumentEditService::Undo(document):VansAssetDocumentEditService::Redo(document);
        if (!edit) return {false,edit.message};return RequestPcgSplinePreview();
    }
    auto asset=state.working;std::string error;
    if (request.command==PcgSplineCommand::ConfigureFields)
    {
        asset.fieldTexelSize=request.fieldSettings[0];asset.sampleSpacing=request.fieldSettings[1];
        asset.curveTolerance=request.fieldSettings[2];asset.heightConflictThreshold=request.fieldSettings[3];
        VansSerializedValue root;
        if (!VansPcgSplineAssetCodec::Encode(asset,root,error)) return {false,error};
        const auto edit=VansAssetDocumentEditService::ReplaceRoot(document,std::move(root));
        if (!edit) return {false,edit.message};return RequestPcgSplinePreview();
    }
    auto found=std::find_if(asset.splines.begin(),asset.splines.end(),[&](const auto& item){return item.id==request.splineId;});
    if (request.command==PcgSplineCommand::Focus)
    {
        auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
        if (found==asset.splines.end() || found->points.empty() || !scene || !scene->GetCamera()) return {false,"Select a spline with points in the scene."};
        glm::vec3 minimum(std::numeric_limits<float>::max()),maximum(-std::numeric_limits<float>::max());
        for (const auto& point:found->points)
        {
            const glm::vec3 position(point.position[0],point.position[1],point.position[2]);
            minimum=glm::min(minimum,position);maximum=glm::max(maximum,position);
        }
        const auto center=(minimum+maximum)*.5f;auto* camera=scene->GetCamera();auto pose=camera->CaptureControlPose();
        const float distance=std::max(15.f,glm::length(maximum-minimum)*.65f/std::tan(glm::radians(pose.fieldOfView*.5f)));
        pose.position=center+glm::normalize(glm::vec3(.65f,.85f,1.f))*distance;
        const auto forward=glm::normalize(center-pose.position);
        pose.rotationDegrees={glm::degrees(std::asin(forward.y)),glm::degrees(std::atan2(forward.z,forward.x)),0};
        camera->ApplyControlPose(pose);return {true,{}};
    }
    if (request.command==PcgSplineCommand::AddRoad || request.command==PcgSplineCommand::AddRiver)
    {
        VansPcgSpline spline;spline.id=VansAssetGuid::New().ToString();
        spline.kind=request.command==PcgSplineCommand::AddRoad?VansPcgSplineKind::Road:VansPcgSplineKind::River;
        spline.name=spline.kind==VansPcgSplineKind::Road?"Road":"River";
        if (spline.kind==VansPcgSplineKind::Road && !VansAssetGuid::TryParse(request.materialGuid,spline.material))
            return {false,"Choose a road PBR material."};
        VansPcgSplinePoint point;point.id=VansAssetGuid::New().ToString();point.position=request.position;
        spline.points.push_back(point);
        VansPcgSplineEvaluator::ResolveAutoTangents(spline);
        state.selectedSpline=spline.id;state.selectedPoint=spline.points.front().id;asset.splines.push_back(std::move(spline));
    }
    else
    {
        if (found==asset.splines.end()) return {false,"Select a spline."};
        if (found->locked) return {false,"Unlock the spline first."};
        switch(request.command)
        {
        case PcgSplineCommand::Remove:asset.splines.erase(found);state.selectedSpline.clear();state.selectedPoint.clear();break;
        case PcgSplineCommand::Duplicate:
        {
            auto copy=*found;copy.id=VansAssetGuid::New().ToString();copy.name+=" Copy";
            for (auto& point:copy.points) point.id=VansAssetGuid::New().ToString();
            state.selectedSpline=copy.id;state.selectedPoint.clear();asset.splines.push_back(std::move(copy));break;
        }
        case PcgSplineCommand::Reverse:
        {
            VansPcgEvaluatedSpline evaluated;
            if (!VansPcgSplineEvaluator::Evaluate(*found,asset.sampleSpacing,asset.curveTolerance,evaluated,error)) return {false,error};
            VansPcgSplineEvaluator::ReversePointOrder(*found,evaluated.length);break;
        }
        case PcgSplineCommand::InsertPoint:
        {
            const auto id=VansAssetGuid::New().ToString();
            if (!VansPcgSplineEvaluator::InsertPoint(*found,request.segment,request.parameter,id,error)) return {false,error};
            state.selectedPoint=id;break;
        }
        case PcgSplineCommand::RemovePoint:
            found->points.erase(std::remove_if(found->points.begin(),found->points.end(),[&](const auto& p){return p.id==request.pointId;}),found->points.end());
            VansPcgSplineEvaluator::ResolveAutoTangents(*found);state.selectedPoint.clear();break;
        default:return {false,"Unsupported spline command."};
        }
    }
    VansSerializedValue root;
    if (!VansPcgSplineAssetCodec::Encode(asset,root,error)) return {false,error};
    const auto edit=VansAssetDocumentEditService::ReplaceRoot(document,std::move(root));
    if (!edit) return {false,edit.message};return RequestPcgSplinePreview();
}
}
