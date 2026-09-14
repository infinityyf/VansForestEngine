#pragma once
#include "VansPcgRecipeAsset.h"
#include "../AssetCore/VansAssetDatabase.h"
#include <functional>
namespace Vans {
class VansAssetObjectRepository;
struct VansPcgResource {
 VansAssetGuid guid;
 VansAssetType type=VansAssetType::Unknown;
 bool meshCpuData=false;
 bool maskPixels=false;
};
struct VansPcgResourcePlan {
 std::vector<VansPcgResource> resources;
 std::string error;
 explicit operator bool() const { return error.empty(); }
};
VansPcgResourcePlan BuildPcgResourcePlan(const VansPcgRecipeAsset& recipe,
 const VansAssetObjectRepository& repository,
 const std::function<std::optional<VansAssetRecord>(VansAssetGuid)>& findRecord);
}
