#include "VansPcgResourcePlan.h"
#include "VansPcgMaskAsset.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include <map>
#include <unordered_set>
namespace Vans {
VansPcgResourcePlan BuildPcgResourcePlan(const VansPcgRecipeAsset& recipe,
 const VansAssetObjectRepository& repository,
 const std::function<std::optional<VansAssetRecord>(VansAssetGuid)>& findRecord)
{
 VansPcgResourcePlan result;
 const auto errors=ValidatePcgRecipe(recipe,false);
 if (!errors.empty()) { result.error=errors.front(); return result; }
 std::map<VansAssetGuid,VansPcgResource> resources;
 std::unordered_set<VansAssetGuid> pixels;
 const auto add=[&](VansAssetGuid guid,VansAssetType type,bool cpu=false,bool data=false) {
  if (!guid.IsValid()) return true;
  const auto record=findRecord?findRecord(guid):std::nullopt;
  if (!record || record->type!=type || record->state==VansAssetState::Missing) {
   result.error="PCG dependency is missing or has the wrong asset type: "+guid.ToString(); return false;
  }
  auto& resource=resources[guid];
  resource.guid=guid; resource.type=type; resource.meshCpuData|=cpu; resource.maskPixels|=data;
  return true;
 };
 for (const auto& region : recipe.regions) {
  if (!add(region.surface.terrain,VansAssetType::Terrain)) return result;
  for (const auto& layer : region.layers) {
   if (!add(layer.plant,VansAssetType::PlantType)) return result;
   if (layer.plant.IsValid()) {
    const auto plant=repository.ResolveLatest<VansPlantTypeAsset>(layer.plant);
    if (!plant || plant->category!=layer.category) { result.error="PCG plant snapshot/category is invalid: "+layer.plant.ToString(); return result; }
    const auto plantErrors=ValidatePlantTypeAsset(*plant,false);
    if (!plantErrors.empty()) { result.error=plantErrors.front(); return result; }
    for(const auto& variant:plant->variants) for(const auto& level:variant.lod.levels) for(const auto& part:level.parts)
     if(!add(part.model,VansAssetType::Model)||!add(part.material,VansAssetType::Material))return result;
    for (const auto& variant : plant->variants) for (const auto& part : variant.parts) {
     if (!add(part.mesh,VansAssetType::Model,plant->category==VansPlantCategory::Grass) ||
         !add(part.material,VansAssetType::Material)) return result;
    }
   }
   for (const auto guid : {layer.densityMask,layer.exclusionMask}) {
    if (!guid.IsValid()) continue;
    if (!add(guid,VansAssetType::PcgMask)) return result;
   }
   VansPcgMaskBinding masks;
   std::string maskError;
   if (!ResolvePcgMaskBinding(region,layer,repository,true,pixels,masks,maskError)) {
    result.error="PCG Mask binding is invalid: "+maskError; return result;
   }
   if (!add(masks.density->pixelAsset,VansAssetType::Texture,false,true) ||
       (masks.exclusion && !add(masks.exclusion->pixelAsset,VansAssetType::Texture,false,true))) return result;
  }
 }
 for (const auto& item : resources) result.resources.push_back(item.second);
 return result;
}
}
