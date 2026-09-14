#include "EngineAPIImpl.h"
#include "../../RenderCore/VansScene.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../EditorCore/VansAssetDocumentRegistry.h"
#include "../../EditorCore/VansAssetDocumentEditService.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../../PcgCore/VansPcgExecutor.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../TerrainCore/VansTerrainAsset.h"
#include <algorithm>
#include <unordered_set>

namespace Vans::EditorAPI
{
namespace {
struct InstanceDocument {
 std::shared_ptr<VansOpenAssetDocument> document;
 VansVegetationConfigAsset asset;
 VansPcgRegion* region=nullptr;
 VansPcgLayer* layer=nullptr;
 bool Open(const PcgBrushTarget& target,std::string& error) {
  VansAssetGuid guid;VansAssetGuid::TryParse(target.recipeGuid,guid);
  const auto* database=VansProjectManager::Get().GetAssetDatabase();
  const auto record=database?database->Find(guid):std::nullopt;
  if (!record || record->type!=VansAssetType::VegetationConfig) {error="The PCG recipe is unavailable.";return false;}
  document=VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
  if (!document || !VansVegetationConfigCodec::Decode(document->sourceDocument.SerializedRootSnapshot(),asset,error)) return false;
  for (auto& r:asset.config.regions) if (r.id==target.regionId) {
   region=&r;for (auto& l:r.layers) if (l.id==target.layerId) {layer=&l;return true;}
  }
  error="The distribution layer no longer exists.";return false;
 }
};
}
PcgInstanceSnapshot EngineAPIImpl::GetPcgInstances(const PcgBrushTarget& target,uint64_t offset)
{
 PcgInstanceSnapshot result;result.offset=offset;
 InstanceDocument source;if (!source.Open(target,result.message)) return result;
 result.documentState=source.document->sourceDocument.CurrentStateId();
 const auto& layer=*source.layer;
 const auto append=[&](PcgInstanceItem item) {
  if (result.total>=offset && result.items.size()<128) result.items.push_back(std::move(item));
  ++result.total;
 };
 std::unordered_set<std::string> authored,seenOverrides;
 for (const auto* list:{&layer.fixedInstances,&layer.addedInstances}) for (const auto& entry:*list) {
  PcgInstanceItem item;item.id=VansPcgPointGenerator::PointIdText(VansPcgPointGenerator::AuthoredInstanceId(source.region->id,layer.id,entry.id));
  item.authoredId=entry.id;item.variant=entry.variant;item.position=entry.position;item.rotation=entry.rotation;item.scale=entry.scale;
  item.origin=list==&layer.fixedInstances?PcgInstanceOrigin::Fixed:PcgInstanceOrigin::Added;
  authored.insert(item.id);append(std::move(item));
 }
 const auto& repository=VansProjectManager::Get().GetAssetObjectRepository();
 VansPcgRecipeAsset selected;selected.name=source.asset.config.name;selected.regions={*source.region};selected.regions[0].layers={layer};
 const auto generated=VansPcgExecutor::Generate(selected,repository,[&](const VansPcgSurfaceBinding& binding,std::string& error) {
  return CreatePcgTerrainSurface((m_Scene?static_cast<VansGraphics::VansScene*>(m_Scene)->ResolveEffectiveTerrain(binding.terrain):repository.ResolveLatest<VansTerrainAsset>(binding.terrain)),error);
 },std::nullopt,m_Scene?static_cast<VansGraphics::VansScene*>(m_Scene)->GetSplineFieldSnapshot():nullptr);
 if (!generated) result.message="Author edits available; generation failed: "+generated.error;
 else for (const auto& output:generated.layers) for (const auto& point:output.points) {
  const auto id=VansPcgPointGenerator::PointIdText(point.id);if (authored.count(id)) continue;
  PcgInstanceItem item;item.id=id;item.variant=output.variantIds[point.variantIndex];
  item.position=point.position;item.rotation=point.rotation;item.scale=point.scale;
  const auto edit=std::find_if(layer.overrides.begin(),layer.overrides.end(),[&](const auto& e){return e.target==id;});
  if (edit!=layer.overrides.end()) {seenOverrides.insert(id);item.locked=edit->kind==VansPcgOverrideKind::Lock;}
  append(std::move(item));
 }
 for (const auto& edit:layer.overrides) if (!seenOverrides.count(edit.target)) {
  PcgInstanceItem item;item.id=edit.target;item.variant=edit.variant;item.origin=PcgInstanceOrigin::Override;
  item.removed=edit.kind==VansPcgOverrideKind::Remove;item.locked=edit.kind==VansPcgOverrideKind::Lock;
  item.orphan=!item.removed && !item.locked;item.position=edit.position;item.rotation=edit.rotation;item.scale=edit.scale;
  append(std::move(item));
 }
 result.success=true;return result;
}
PcgEditorOperationResult EngineAPIImpl::EditPcgInstance(const PcgInstanceEditRequest& request)
{
 if (m_PlayState!=EnginePlayState::Edit) return {false,"Edit instances in Edit mode."};
 InstanceDocument source;std::string error;if (!source.Open(request.target,error)) return {false,error};
 if (source.layer->locked) return {false,"Unlock the layer before editing instances."};
 if (source.document->sourceDocument.CurrentStateId()!=request.documentState) return {false,"The recipe changed. Refresh the instance list first."};
 const auto finished=FinishPcgStroke(false);if (!finished.success) return finished;
 auto& layer=*source.layer;const auto& item=request.instance;
 const auto plant=VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansPlantTypeAsset>(layer.plant);
 const auto validVariant=[&] {return plant && std::any_of(plant->variants.begin(),plant->variants.end(),[&](const auto& v){return v.id==item.variant;});};
 const auto clearOverride=[&] {layer.overrides.erase(std::remove_if(layer.overrides.begin(),layer.overrides.end(),[&](const auto& e){return e.target==item.id;}),layer.overrides.end());};
 if (request.action==PcgInstanceAction::Add || request.action==PcgInstanceAction::AddFixed) {
  if (!validVariant()) return {false,"Choose an existing plant variant."};
  VansPcgAuthoredInstance entry;entry.id=VansAssetGuid::New().ToString();entry.variant=item.variant;
  entry.position=item.position;entry.rotation=item.rotation;entry.scale=item.scale;
  (request.action==PcgInstanceAction::AddFixed?layer.fixedInstances:layer.addedInstances).push_back(std::move(entry));
 } else if (request.action==PcgInstanceAction::ResetOverride) {
  clearOverride();
 } else if (item.origin==PcgInstanceOrigin::Fixed || item.origin==PcgInstanceOrigin::Added) {
  auto& list=item.origin==PcgInstanceOrigin::Fixed?layer.fixedInstances:layer.addedInstances;
  const auto entry=std::find_if(list.begin(),list.end(),[&](const auto& value){return value.id==item.authoredId;});
  if (entry==list.end()) return {false,"The authored instance no longer exists."};
  if (request.action==PcgInstanceAction::Remove) {list.erase(entry);clearOverride();}
  else if (request.action==PcgInstanceAction::Transform) {
   if (!validVariant()) return {false,"Choose an existing variant."};
   entry->variant=item.variant;entry->position=item.position;entry->rotation=item.rotation;entry->scale=item.scale;
  } else return {false,"Authored instances already retain their transforms during generation."};
 } else {
  uint64_t id=0;if (!VansPcgPointGenerator::ReadPointIdText(item.id,id)) return {false,"Select a stable instance ID."};
  VansPcgInstanceOverride edit;edit.target=item.id;edit.position=item.position;edit.rotation=item.rotation;edit.scale=item.scale;
  if (request.action==PcgInstanceAction::Remove) edit.kind=VansPcgOverrideKind::Remove;
  else if (request.action==PcgInstanceAction::Lock || (request.action==PcgInstanceAction::Transform && item.locked)) {
   if (!validVariant()) return {false,"Choose an existing variant to retain when locking."};
   edit.kind=VansPcgOverrideKind::Lock;edit.variant=item.variant;
  } else if (request.action==PcgInstanceAction::Transform) edit.kind=VansPcgOverrideKind::Transform;
  else return {false,"Unknown instance operation."};
  clearOverride();layer.overrides.push_back(std::move(edit));
 }
 VansSerializedValue root;if (!VansVegetationConfigCodec::Encode(source.asset.config,root,error)) return {false,error};
 const auto edited=VansAssetDocumentEditService::ReplaceRoot(source.document->sourceDocument,std::move(root));
 if (!edited) return {false,edited.message};
 const auto preview=RefreshPcgRecipePreview();
 return {true,preview.success?"Instance edit applied. Save recipe to keep it.":preview.message};
}
}
