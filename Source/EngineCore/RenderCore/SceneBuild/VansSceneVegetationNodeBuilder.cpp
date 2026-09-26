#include "VansSceneEnvironmentNodeBuilder.h"
#include "../VansScene.h"
#include "../VegetationCore/VansVegetationCollection.h"
#include "../../PcgCore/VansPcgUpdatePlanner.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../TerrainCore/VansTerrainAsset.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include <algorithm>
namespace VansGraphics
{
bool VansSceneEnvironmentNodeBuilder::BuildVegetationNode(
    VansScene& scene,
    VkDevice& device,
    const Vans::VansPcgRecipeAsset& recipe,
    std::string& error)
{
    auto& repository=Vans::VansProjectManager::Get().GetAssetObjectRepository();
    Vans::VansPcgUpdatePlan plan;
    if (!Vans::VansPcgUpdatePlanner::PlanRecipe(recipe,repository,
        [&](const Vans::VansPcgSurfaceBinding& binding,std::string& error){
            return Vans::CreatePcgTerrainSurface(scene.ResolveEffectiveTerrain(binding.terrain),error);
        },std::nullopt,scene.GetSplineFieldSnapshot(),Vans::VansPcgUpdatePartition::PerLayer,plan,error))
    {
        error = "Recipe '" + recipe.name + "' update planning failed: " + error;
        return false;
    }
    auto collection=std::make_unique<VansVegetationCollection>();
    for (const auto& update : plan.updates)
    {
        std::string detail;
        if (!collection->Apply(scene,device,update,nullptr,detail))
        {
            error = "Recipe '" + recipe.name + "', region '" + update.region +
                "', layer '" + update.layer + "' apply failed: " + detail;
            return false;
        }
    }
    auto node=std::make_unique<VansVegetationRenderNode>(device,RenderNodeType::VEGETATION_NODE);
    node->SetVegetationCollection(collection.get());
    node->SetName(recipe.name);
    scene.RegistRenderNode(node.get(),RenderNodeType::VEGETATION_NODE);
    scene.SetVegetationCollection(std::move(collection));
    node.release();
    return true;
}
}
