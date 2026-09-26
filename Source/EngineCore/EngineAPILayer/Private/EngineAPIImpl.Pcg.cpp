#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../../PcgCore/VansPcgMaskAsset.h"
#include "../../PcgCore/VansPcgMaskBrush.h"
#include "../../PcgCore/VansPcgBatchPlan.h"
#include "../../PcgCore/VansPcgUpdatePlanner.h"
#include <algorithm>
#include <chrono>

#include "../../AuthoringCore/Pcg/VansPcgMaskAuthoringSession.h"
#include "../../AuthoringCore/VansAssetDocumentEditService.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../TerrainCore/VansTerrainAsset.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VegetationCore/VansVegetationCollection.h"
#include <cmath>
#include <limits>
#include "../../PcgCore/Serialization/VansPcgMaskAssetCodec.h"
#include "../../AssetCore/Storage/VansFileStorage.h"

namespace Vans::EditorAPI {
namespace {
constexpr auto PcgMaskPreviewThrottle = std::chrono::milliseconds(100);

bool PcgMaskUnlocked(const PcgBrushTarget& target)
{
 VansAssetGuid guid;VansAssetGuid::TryParse(target.recipeGuid,guid);
 const auto recipe=VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansVegetationConfigAsset>(guid);
 if (!recipe) return false;
 for (const auto& region:recipe->config.regions) if (region.id==target.regionId)
  for (const auto& layer:region.layers) if (layer.id==target.layerId) return !layer.locked;
 return false;
}
}

struct EngineAPIImpl::PcgAuthoringState
{
 std::string projectRoot;
 std::uint64_t sceneRevision=0;
 std::unordered_map<std::string,std::shared_ptr<VansPcgMaskAuthoringSession>> sessions;
 std::unordered_map<std::string,VansPcgPixelRect> pendingPixels;
 std::chrono::steady_clock::time_point lastRefresh;
 PcgBrushTarget target;
	VansPcgBrushSettings brush;
 bool enabled=false;
 PcgBrushSpace strokeSpace=PcgBrushSpace::Scene;
 bool fullRefresh=false;
 std::string message;
};
void EngineAPIImpl::EnsurePcgAuthoringContext() const
{
 const auto& root=VansProjectManager::Get().GetProjectRootPath();
 if (!m_PcgAuthoring) m_PcgAuthoring=std::make_shared<PcgAuthoringState>();
 auto& state=*m_PcgAuthoring;
 if (state.projectRoot!=root) {
  // 项目关闭的保存/丢弃流程由文档系统处理，此处不得向新项目仓库发布旧像素。
  for (auto& entry : state.sessions) { std::string error; entry.second->CancelStroke(error); }
  state=PcgAuthoringState{};
  state.projectRoot=root;
 }
 if (state.sceneRevision!=m_SceneContentRevision) {
  for (auto& entry : state.sessions) if (entry.second->StrokeActive()) {
   std::string error;
   if (!entry.second->EndStroke(error)) state.message=error;
  }
  state.enabled=false;
  state.sceneRevision=m_SceneContentRevision;
 }
}
PcgEditorOperationResult EngineAPIImpl::RefreshPcgRecipePreview()
{
 EnsurePcgAuthoringContext();
 if (!m_Scene) return {true,{}};
 if (m_PcgSceneRecipeGuid.empty()) {
  auto update=std::make_shared<VansPcgBatchUpdate>();update->replaceAll=true;
  static_cast<VansGraphics::VansScene*>(m_Scene)->QueueVegetationUpdate(std::move(update));
  return {true,{}};
 }
 VansAssetGuid guid;VansAssetGuid::TryParse(m_PcgSceneRecipeGuid,guid);
 const auto& repository=VansProjectManager::Get().GetAssetObjectRepository();
 const auto recipe=repository.ResolveLatest<VansVegetationConfigAsset>(guid);
 if (!recipe) return {false,"The scene PCG recipe is unavailable."};
 VansPcgUpdatePlan plan;
 std::string error;
 if (!VansPcgUpdatePlanner::PlanRecipe(recipe->config,repository,
  [&](const VansPcgSurfaceBinding& binding,std::string& message){
   return CreatePcgTerrainSurface(static_cast<VansGraphics::VansScene*>(m_Scene)->ResolveEffectiveTerrain(binding.terrain),message);
  },std::nullopt,static_cast<VansGraphics::VansScene*>(m_Scene)->GetSplineFieldSnapshot(),
  VansPcgUpdatePartition::PerLayer,plan,error))
  return {false,"Configuration applied; preview retained: "+error};
 auto update=std::make_shared<VansPcgBatchUpdate>();
 update->replaceAll=true;
 for (const auto& part : plan.updates)
  update->batches.insert(part.batches.begin(),part.batches.end());
 return CommitPcgPreview(std::move(update));
}
PcgEditorOperationResult EngineAPIImpl::RefreshPcgMaskVegetation(bool force)
{
 auto& state=*m_PcgAuthoring;
 const auto found=state.sessions.find(state.target.maskGuid);
 if (found==state.sessions.end()) return {true,{}};
 auto& pending=state.pendingPixels[state.target.maskGuid];
 pending.Include(found->second->TakePendingPixelChange());
 state.fullRefresh=found->second->TakeFullRefresh() || state.fullRefresh;
 if (pending.Empty()) return {true,{}};
 const auto now=std::chrono::steady_clock::now();
 if (!force && now-state.lastRefresh<PcgMaskPreviewThrottle) return {true,{}};
 state.lastRefresh=now;
 std::string error;
 if (!found->second->PublishWorkingSnapshot(error)) return {false,error};
 if (!m_Scene || state.target.recipeGuid!=m_PcgSceneRecipeGuid) {pending={};state.fullRefresh=false;return {true,{}};}
 VansAssetGuid guid; VansAssetGuid::TryParse(state.target.recipeGuid,guid);
 const auto& repository=VansProjectManager::Get().GetAssetObjectRepository();
 const auto asset=repository.ResolveLatest<VansVegetationConfigAsset>(guid);
 if (!asset) return {false,"The PCG recipe is unavailable in memory."};
 const auto region=std::find_if(asset->config.regions.begin(),asset->config.regions.end(),
  [&](const auto& value){return value.id==state.target.regionId;});
 if (region==asset->config.regions.end()) return {false,"The PCG region no longer exists."};
 const auto layer=std::find_if(region->layers.begin(),region->layers.end(),
  [&](const auto& value){return value.id==state.target.layerId;});
 if (layer==region->layers.end()) return {false,"The PCG layer no longer exists."};
 const auto plant=repository.ResolveLatest<VansPlantTypeAsset>(layer->plant);
 if (!plant) return {false,"Assign a plant before previewing this Mask."};
 const auto* collection=static_cast<VansGraphics::VansScene*>(m_Scene)->GetVegetationCollection();
 const auto coverage=state.fullRefresh || (collection && !collection->LastUpdateError().empty())?std::nullopt:
  PcgMaskUpdateCoverage(*region,*layer,*plant,found->second->WorkingAsset().mask,pending);
 VansPcgUpdatePlan plan;
 if (!VansPcgUpdatePlanner::PlanLayer(asset->config.name,*region,*layer,repository,
  [&](const VansPcgSurfaceBinding& binding,std::string& message){
   return CreatePcgTerrainSurface(static_cast<VansGraphics::VansScene*>(m_Scene)->ResolveEffectiveTerrain(binding.terrain),message);
  },coverage,static_cast<VansGraphics::VansScene*>(m_Scene)->GetSplineFieldSnapshot(),
  VansPcgUpdatePartition::PerLayer,true,plan,error))
 {state.message=error;return {false,error};}
 if (plan.updates.size()!=1) return {false,"PCG layer update planner returned an invalid update count."};
 static_cast<VansGraphics::VansScene*>(m_Scene)->QueueVegetationUpdate(
  std::make_shared<VansPcgBatchUpdate>(std::move(plan.updates.front())));
 pending={};state.fullRefresh=false;state.lastRefresh=now;state.message.clear();
 return {true,{}};
}
PcgEditorOperationResult EngineAPIImpl::FinishPcgStroke(bool cancel)
{
 EnsurePcgAuthoringContext();
 auto& state=*m_PcgAuthoring;
 const auto found=state.sessions.find(state.target.maskGuid);
 std::string error;
 if (found!=state.sessions.end() && found->second->StrokeActive() &&
     !(cancel?found->second->CancelStroke(error):found->second->EndStroke(error)))
  return {false,error};
 const auto refreshed=RefreshPcgMaskVegetation(true);
 if (!refreshed.success) state.message=refreshed.message;
 // 预览失败不得阻止结束笔画、切换工具或保存已完成的作者编辑。
 return {true,{}};
}

PcgEditorSnapshot EngineAPIImpl::GetPcgEditorSnapshot() const
{
 PcgEditorSnapshot snapshot;
 snapshot.sceneRecipeGuid=m_PcgSceneRecipeGuid;
 const auto& manager=VansProjectManager::Get();
 const auto* database=manager.GetAssetDatabase();
 if (!database) return snapshot;
 const auto& repository=manager.GetAssetObjectRepository();
 for (const auto& record : database->All()) {
  if (record.type!=VansAssetType::VegetationConfig) continue;
  const auto asset=repository.ResolveLatest<VansVegetationConfigAsset>(record.guid);
  if (!asset) { snapshot.errors.push_back("PCG asset is unavailable: "+record.guid.ToString()); continue; }
  for (const auto& region : asset->config.regions) for (const auto& layer : region.layers) {
   PcgLayerSnapshot item;
   item.recipeGuid=record.guid.ToString(); item.recipeName=asset->config.name;
   item.regionId=region.id; item.regionName=region.name; item.layerId=layer.id; item.name=layer.name;
   item.tree=layer.category==VansPlantCategory::Tree; item.enabled=region.enabled&&layer.enabled; item.locked=layer.locked;
   item.plantGuid=layer.plant.IsValid()?layer.plant.ToString():"";
   item.densityMaskGuid=layer.densityMask.IsValid()?layer.densityMask.ToString():"";
   item.exclusionMaskGuid=layer.exclusionMask.IsValid()?layer.exclusionMask.ToString():"";
   item.source=layer.source==VansPcgSourceMode::Density?"density":layer.source==VansPcgSourceMode::Count?"count":"fixed";
   item.density=layer.placement.density; item.treeTargetCount=layer.targetCount;
   item.fixedCount=layer.fixedInstances.size(); item.addedCount=layer.addedInstances.size();
   item.boundsMin=region.bounds.min; item.boundsMax=region.bounds.max;
   if (const auto plant=repository.ResolveLatest<VansPlantTypeAsset>(layer.plant)) {
    item.plantName=plant->name; item.variantCount=plant->variants.size();
   }
   snapshot.layers.push_back(std::move(item));
  }
 }
 return snapshot;
}
PcgMaskPreviewSnapshot EngineAPIImpl::GetPcgMaskPreview(const std::string& text) const
{
 EnsurePcgAuthoringContext();
 PcgMaskPreviewSnapshot preview;
 VansAssetGuid guid;
 if (!VansAssetGuid::TryParse(text,guid)) { preview.error="No Mask selected."; return preview; }
 const auto published=VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansPcgMaskAsset>(guid);
 const auto session=m_PcgAuthoring->sessions.find(text);
 const auto* asset=session!=m_PcgAuthoring->sessions.end()?&session->second->WorkingAsset():published.get();
 if (!asset || !asset->mask.IsValid()) { preview.error="Mask pixels are unavailable in memory."; return preview; }
 preview.width=asset->mask.width; preview.height=asset->mask.height;
 preview.boundsMin=asset->mask.bounds.min;preview.boundsMax=asset->mask.bounds.max;
 preview.previewWidth=std::min(preview.width,64u); preview.previewHeight=std::min(preview.height,64u);
 preview.pixels.reserve(preview.previewWidth*preview.previewHeight);
 for (uint32_t y=0;y<preview.previewHeight;++y) for (uint32_t x=0;x<preview.previewWidth;++x) {
  const uint32_t sx=std::min(static_cast<uint32_t>((x+0.5f)*preview.width/preview.previewWidth),preview.width-1);
  const uint32_t sy=std::min(static_cast<uint32_t>((y+0.5f)*preview.height/preview.previewHeight),preview.height-1);
  preview.pixels.push_back(static_cast<uint8_t>(asset->mask.pixels[static_cast<size_t>(sy)*preview.width+sx]>>8));
 }
 preview.available=true;
 return preview;
}

PcgBrushSnapshot EngineAPIImpl::GetPcgBrushSnapshot() const
{
 EnsurePcgAuthoringContext();
 const auto& state=*m_PcgAuthoring;
 PcgBrushSnapshot snapshot;
 snapshot.target=state.target; snapshot.message=state.message;
 if (m_Scene) if (const auto* collection=static_cast<VansGraphics::VansScene*>(m_Scene)->GetVegetationCollection())
  if (const auto error=collection->LastUpdateError();!error.empty()) snapshot.message=error;
 const auto found=state.sessions.find(state.target.maskGuid);
 if (found==state.sessions.end()) return snapshot;
 std::string error;
 if (!found->second->SyncDefinitionFromDocument(error)) { snapshot.message=error; return snapshot; }
	const auto& brush=state.brush;
 snapshot.settings={static_cast<PcgBrushOperation>(brush.operation),brush.radius,brush.strength,
  brush.hardness,brush.targetValue,brush.spacingFraction};
 snapshot.available=true;
 snapshot.canvasEditable=m_PlayState==EnginePlayState::Edit && PcgMaskUnlocked(state.target);
 snapshot.editable=m_PlayState==EnginePlayState::Edit && state.target.recipeGuid==m_PcgSceneRecipeGuid && PcgMaskUnlocked(state.target);
 snapshot.enabled=state.enabled&&snapshot.editable;
 snapshot.strokeActive=found->second->StrokeActive();
 snapshot.canvasStrokeActive=snapshot.strokeActive && state.strokeSpace==PcgBrushSpace::MaskCanvas;
 snapshot.dirty=found->second->Document()->IsDirty();
 snapshot.canUndo=VansAssetDocumentEditService::CanUndo(found->second->Document()->sourceDocument);
 snapshot.canRedo=VansAssetDocumentEditService::CanRedo(found->second->Document()->sourceDocument);
 return snapshot;
}
PcgEditorOperationResult EngineAPIImpl::SelectPcgBrushTarget(const PcgBrushTarget& target, bool enabled)
{
 EnsurePcgAuthoringContext();
 auto& state=*m_PcgAuthoring;
 if (!enabled && target.maskGuid.empty()) {
  const auto result=FinishPcgStroke(false);
  if (result.success) state.enabled=false;
  return result;
 }
 if (m_PlayState!=EnginePlayState::Edit) return {false,"PCG painting is available only in Edit mode."};
 if (enabled && target.recipeGuid!=m_PcgSceneRecipeGuid)
  return {false,"Open the scene that uses this PCG recipe before painting."};
 VansAssetGuid recipeGuid,maskGuid;
 if (!VansAssetGuid::TryParse(target.recipeGuid,recipeGuid) || !VansAssetGuid::TryParse(target.maskGuid,maskGuid))
  return {false,"Select a valid PCG recipe and owned Mask."};
 auto& manager=VansProjectManager::Get();
 auto& repository=manager.GetAssetObjectRepository();
 const auto recipe=repository.ResolveLatest<VansVegetationConfigAsset>(recipeGuid);
 if (!recipe) return {false,"The PCG recipe is unavailable in memory."};
 const auto region=std::find_if(recipe->config.regions.begin(),recipe->config.regions.end(),
  [&](const auto& value) {return value.id==target.regionId;});
 if (region==recipe->config.regions.end()) return {false,"The selected PCG region does not exist."};
 const auto layer=std::find_if(region->layers.begin(),region->layers.end(),
  [&](const auto& value) {return value.id==target.layerId;});
 if (layer==region->layers.end() || (layer->densityMask!=maskGuid && layer->exclusionMask!=maskGuid))
  return {false,"This Mask does not belong to the selected distribution layer."};
 if (layer->locked && enabled) return {false,"Unlock the distribution layer before editing its Mask."};
 if (enabled && region->surface.kind==VansPcgSurfaceKind::Unassigned)
  return {false,"Assign a painting surface to this region first."};
 const auto mask=repository.ResolveLatest<VansPcgMaskAsset>(maskGuid);
 if (!mask || mask->mask.target.regionId!=target.regionId || mask->mask.target.layerId!=target.layerId ||
     mask->mask.target.maskId!=target.maskGuid) return {false,"Mask ownership is invalid."};
 auto found=state.sessions.find(target.maskGuid);
 std::string error;
 if (found==state.sessions.end()) {
  const auto* database=manager.GetAssetDatabase();
  if (!database) return {false,"The project asset index is unavailable."};
  const auto record=database->Find(maskGuid), pixel=database->Find(mask->pixelAsset);
  if (!record || !pixel) return {false,"The Mask or its independent pixel asset is missing."};
  auto session=VansPcgMaskAuthoringSession::Open(*record,mask,pixel->sourcePath,repository,error);
  if (!session) return {false,error};
  found=state.sessions.emplace(target.maskGuid,std::move(session)).first;
 }
 const auto finished=FinishPcgStroke(false);
 if (!finished.success) return finished;
 if (enabled && m_TerrainBrushConfiguration.enabled) {
  auto terrainConfiguration=m_TerrainBrushConfiguration;
  terrainConfiguration.enabled=false;
  const auto disabled=ConfigureTerrainBrush(terrainConfiguration);
  if (!disabled.success) return {false,disabled.message};
 }
 state.target=target; state.enabled=enabled; state.message.clear();
 return {true,{}};
}
PcgEditorOperationResult EngineAPIImpl::ConfigurePcgBrush(const PcgBrushSettings& settings)
{
 EnsurePcgAuthoringContext();
 if (m_PlayState!=EnginePlayState::Edit) return {false,"PCG brush editing is available only in Edit mode."};
 if (!PcgMaskUnlocked(m_PcgAuthoring->target)) return {false,"Unlock the layer before editing its brush."};
 const auto found=m_PcgAuthoring->sessions.find(m_PcgAuthoring->target.maskGuid);
 if (found==m_PcgAuthoring->sessions.end()) return {false,"Select a Mask first."};
	VansPcgBrushSettings brush;
	brush.operation=static_cast<VansPcgBrushOperation>(settings.operation);
	brush.radius=settings.radius; brush.strength=settings.strength; brush.hardness=settings.hardness;
	brush.targetValue=settings.targetValue; brush.spacingFraction=settings.spacingFraction;
	if (!brush.IsValid()) return {false,"PCG brush settings are outside their valid range."};
	if (found->second->StrokeActive())
		return {false,"Finish the current PCG stroke before changing brush settings."};
	m_PcgAuthoring->brush=brush;
	return {true,{}};
}
PcgBrushResult EngineAPIImpl::ApplyPcgBrushInput(const PcgBrushInput& input)
{
 PcgBrushResult result;
 EnsurePcgAuthoringContext();
 auto& state=*m_PcgAuthoring;
 if (!(input.target==state.target)) { result.message="The brush event belongs to a different Mask target."; return result; }
 const auto found=state.sessions.find(state.target.maskGuid);
 if (found==state.sessions.end()) { result.message="No PCG Mask is selected."; return result; }
 const auto session=found->second;
 if (session->StrokeActive() && input.space!=state.strokeSpace) {
  result.message="Another brush surface owns the current stroke.";return result;
 }
 if (input.phase==PcgBrushPhase::End || input.phase==PcgBrushPhase::Cancel) {
  const auto finished=FinishPcgStroke(input.phase==PcgBrushPhase::Cancel);
  result.success=finished.success; result.message=finished.message;
  result.strokeActive=session->StrokeActive(); return result;
 }
 const bool canvas=input.space==PcgBrushSpace::MaskCanvas;
 if (m_PlayState!=EnginePlayState::Edit || (!canvas && (!state.enabled || state.target.recipeGuid!=m_PcgSceneRecipeGuid))) {
  result.message="The PCG scene brush is disabled."; return result;
 }
 if (input.phase==PcgBrushPhase::Break) {
  session->BreakSegment(); result.success=true; result.strokeActive=session->StrokeActive(); return result;
 }
 VansAssetGuid recipeGuid; VansAssetGuid::TryParse(state.target.recipeGuid,recipeGuid);
 const auto& repository=VansProjectManager::Get().GetAssetObjectRepository();
 const auto recipe=repository.ResolveLatest<VansVegetationConfigAsset>(recipeGuid);
 if (!recipe) { result.message="The PCG recipe is no longer available."; return result; }
 const auto region=std::find_if(recipe->config.regions.begin(),recipe->config.regions.end(),
  [&](const auto& value) {return value.id==state.target.regionId;});
 if (region==recipe->config.regions.end()) { result.message="The PCG region is no longer available."; return result; }
 const auto layer=std::find_if(region->layers.begin(),region->layers.end(),
  [&](const auto& value) {return value.id==state.target.layerId;});
 if (layer==region->layers.end() || layer->locked) {
  FinishPcgStroke(false); state.enabled=false; result.message="The PCG layer was removed or locked."; return result;
 }
 VansPcgSurfaceHit hit;
 VansPcgSurfaceSampler surface;
 if (canvas) {
  const auto& bounds=session->WorkingAsset().mask.bounds;
  result.hit=std::isfinite(input.maskUV[0]) && std::isfinite(input.maskUV[1]) &&
   input.maskUV[0]>=0 && input.maskUV[0]<1 && input.maskUV[1]>=0 && input.maskUV[1]<1;
  hit.position={bounds.min[0]+input.maskUV[0]*(bounds.max[0]-bounds.min[0]),0,
   bounds.min[1]+input.maskUV[1]*(bounds.max[1]-bounds.min[1])};
 } else if (region->surface.kind==VansPcgSurfaceKind::Terrain) {
  const auto terrain=static_cast<VansGraphics::VansScene*>(m_Scene)->ResolveEffectiveTerrain(region->surface.terrain);
  surface=CreatePcgTerrainSurface(terrain,result.message);
  if (!surface) return result;
  result.hit=RaycastPcgTerrainSurface(terrain,input.rayOrigin,input.rayDirection,100000.0f,hit);
 } else if (region->surface.kind==VansPcgSurfaceKind::Plane) {
  const float y=region->surface.planeHeight;
  surface=[y](float,float,VansPcgSurfacePoint& value) {value.height=y;value.normal={0,1,0};return true;};
  bool finite=true; for (std::size_t axis=0;axis<3;++axis)
   finite=finite&&std::isfinite(input.rayOrigin[axis])&&std::isfinite(input.rayDirection[axis]);
  if (finite && std::abs(input.rayDirection[1])>1e-8f) {
   const float t=(y-input.rayOrigin[1])/input.rayDirection[1];
   if (t>=0 && std::isfinite(t)) {
    hit.position={input.rayOrigin[0]+t*input.rayDirection[0],y,input.rayOrigin[2]+t*input.rayDirection[2]};
    hit.normal={0,1,0}; result.hit=true;
   }
  }
 } else { result.message="A painting surface has not been assigned."; return result; }
 const auto& mask=session->WorkingAsset().mask;
 if (result.hit && !mask.bounds.Contains(hit.position[0],hit.position[2])) result.hit=false;
 result.success=true; result.position=hit.position; result.normal=hit.normal;
 if (result.hit && !canvas) {
	  const float radius=state.brush.radius;
  for (int i=0;i<=64;++i) {
   const float angle=static_cast<float>(i)*6.28318530718f/64;
   const float x=hit.position[0]+std::cos(angle)*radius,z=hit.position[2]+std::sin(angle)*radius;
   VansPcgSurfacePoint point;
   const float nan=std::numeric_limits<float>::quiet_NaN();
   result.ring.push_back(surface(x,z,point)?std::array<float,3>{x,point.height+.035f,z}:std::array<float,3>{nan,nan,nan});
  }
 }
 if (input.phase==PcgBrushPhase::Hover) { result.strokeActive=session->StrokeActive(); return result; }
 if (input.phase==PcgBrushPhase::Begin) {
  if (!result.hit) return result;
	  if (!session->BeginStroke(mask.target,state.brush,result.message)) { result.success=false;return result; }
  state.strokeSpace=input.space;
 }
 if (session->StrokeActive()) {
  if (result.hit) result.success=session->AddPoint(mask.target,hit.position[0],hit.position[2],input.erase,result.message);
  else session->BreakSegment();
 }
 result.strokeActive=session->StrokeActive();
 if (result.success) {
  const auto refreshed=RefreshPcgMaskVegetation(false);
  if (!refreshed.success) {state.message=refreshed.message;result.message=refreshed.message;}
 }
 return result;
}
PcgEditorOperationResult EngineAPIImpl::EditPcgMaskDocument(PcgMaskDocumentAction action)
{
 EnsurePcgAuthoringContext();
 if (m_PlayState!=EnginePlayState::Edit) return {false,"PCG Mask editing is available only in Edit mode."};
 if (action!=PcgMaskDocumentAction::Save && !PcgMaskUnlocked(m_PcgAuthoring->target))
  return {false,"Unlock the layer before editing its Mask history."};
 const auto found=m_PcgAuthoring->sessions.find(m_PcgAuthoring->target.maskGuid);
 if (found==m_PcgAuthoring->sessions.end()) return {false,"Select a Mask first."};
 const auto session=found->second;
 const auto finished=FinishPcgStroke(false);
 if (!finished.success) return finished;
 if (action==PcgMaskDocumentAction::Save) {
  const auto saved=SaveAuthoringDocument(session->Document());
  return {saved.success,saved.message};
 }
 AssetDocumentEditResult edit;
 switch (action) {
 case PcgMaskDocumentAction::Undo: edit=VansAssetDocumentEditService::Undo(session->Document()->sourceDocument);break;
 case PcgMaskDocumentAction::Redo: edit=VansAssetDocumentEditService::Redo(session->Document()->sourceDocument);break;
 case PcgMaskDocumentAction::Revert: edit=VansAssetDocumentEditService::RevertToSaved(session->Document()->sourceDocument);break;
 default:return {false,"Unknown Mask document operation."};
 }
 if (!edit) return {false,edit.message};
 std::string error;
 if (!session->SyncDefinitionFromDocument(error) || !session->PublishWorkingSnapshot(error)) return {false,error};
 return RefreshPcgMaskVegetation(true);
}

PcgEditorOperationResult EngineAPIImpl::EditPcgMaskData(const PcgMaskDataRequest& request)
{
 EnsurePcgAuthoringContext();
 if (!(request.target==m_PcgAuthoring->target)) return {false,"Select the Mask again before editing its data."};
 const auto found=m_PcgAuthoring->sessions.find(request.target.maskGuid);
 if (found==m_PcgAuthoring->sessions.end()) return {false,"Select a Mask first."};
 if (m_PlayState!=EnginePlayState::Edit || !PcgMaskUnlocked(request.target)) return {false,"Edit an unlocked layer in Edit mode."};
 if (found->second->StrokeActive()) return {false,"Finish the stroke first."};
 std::string error;
 if (!found->second->SyncDefinitionFromDocument(error)) return {false,error};
 auto asset=found->second->WorkingAsset();
 switch (request.action) {
 case PcgMaskDataAction::Fill:
  if (!std::isfinite(request.fill) || request.fill<0 || request.fill>1) return {false,"Fill must be in [0,1]."};
  std::fill(asset.mask.pixels.begin(),asset.mask.pixels.end(),static_cast<uint16_t>(std::lround(request.fill*65535)));
  break;
 case PcgMaskDataAction::Import: {
  std::error_code ec;
  const auto size=std::filesystem::file_size(request.path,ec);
  if (ec || size>MaximumPcgMaskImportBytes) return {false,"Choose an image smaller than 256 MiB."};
  VansScopedIOContext io(VansIODomain::Authoring,"Pcg.ImportMask");
  std::string bytes;
  if (!VansFileStorage::ReadAllBytes(request.path,bytes,error)) return {false,error};
  if (!VansPcgMaskAssetCodec::ImportPixels(bytes,request.channel,asset,error)) return {false,error};
  break;
 }
 case PcgMaskDataAction::Remap: {
  const auto source=asset.mask;
  const VansPcgBounds bounds{request.boundsMin,request.boundsMax};
  if (!bounds.IsValid() || !request.width || !request.height ||
      request.width>MaximumPcgMaskDimension || request.height>MaximumPcgMaskDimension)
   return {false,"Choose valid world bounds and dimensions in [1,8192]."};
  asset.mask.bounds=bounds;asset.mask.width=request.width;asset.mask.height=request.height;
  asset.mask.pixels.resize(static_cast<size_t>(request.width)*request.height);
  const auto& sampleBounds=request.preserveWorld?bounds:source.bounds;
  for (uint32_t y=0;y<request.height;++y) for (uint32_t x=0;x<request.width;++x) {
   const float wx=sampleBounds.min[0]+(x+.5f)/request.width*(sampleBounds.max[0]-sampleBounds.min[0]);
   const float wz=sampleBounds.min[1]+(y+.5f)/request.height*(sampleBounds.max[1]-sampleBounds.min[1]);
   asset.mask.pixels[static_cast<size_t>(y)*request.width+x]=static_cast<uint16_t>(std::lround(source.Sample(wx,wz)*65535));
  }
  break;
 }
 case PcgMaskDataAction::Export: {
  const std::filesystem::path path(request.path);
  std::error_code ec;
  if (!path.is_absolute() || path.extension()!=".png" || std::filesystem::exists(path,ec))
   return {false,"Choose a new absolute .png export path. Use Save Mask to update the owned asset."};
  std::string bytes;
  if (!VansPcgMaskAssetCodec::EncodePixels(asset,bytes,error)) return {false,error};
  VansScopedIOContext io(VansIODomain::Authoring,"Pcg.ExportMask",true);
  return {VansFileStorage::WriteAtomicBytes(path,bytes,error),error};
 }
 default:return {false,"Unknown Mask data operation."};
 }
 if (!found->second->ReplaceContent(std::move(asset),error)) return {false,error};
 const auto refresh=RefreshPcgMaskVegetation(true);
 return {true,refresh.success?"Mask updated in memory. Save Mask to keep the edit.":refresh.message};
}

}
