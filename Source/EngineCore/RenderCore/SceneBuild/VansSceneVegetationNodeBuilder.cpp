#include "VansSceneEnvironmentNodeBuilder.h"
#include "../VansScene.h"
#include "../VegetationCore/VansVegetationCollection.h"
#include "../../PcgCore/VansPcgBatchPlan.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../TerrainCore/VansTerrainAsset.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../Util/VansLog.h"
#include <algorithm>
namespace VansGraphics
{
bool VansSceneEnvironmentNodeBuilder::AddVegetationNode(VansScene& scene,VkDevice& device,
    const Vans::VansPcgRecipeAsset& recipe)
{
    auto& repository=Vans::VansProjectManager::Get().GetAssetObjectRepository();
    const auto generated=Vans::VansPcgExecutor::Generate(recipe,repository,
        [&](const Vans::VansPcgSurfaceBinding& binding,std::string& error){
            return Vans::CreatePcgTerrainSurface(scene.ResolveEffectiveTerrain(binding.terrain),error);
        },std::nullopt,scene.GetSplineFieldSnapshot());
    if (!generated)
    {
        VANS_LOG_ERROR("[PCG] Recipe '" << recipe.name << "' generation failed: " << generated.error);
        return false;
    }
    auto collection=std::make_unique<VansVegetationCollection>();
    for (const auto& layer : generated.layers)
    {
        const auto region=std::find_if(recipe.regions.begin(),recipe.regions.end(),
            [&](const auto& value){return value.id==layer.regionId;});
        Vans::VansPcgBatchUpdate update;
        std::string error;
        if (!Vans::BuildPcgBatchUpdate(*region,layer,std::nullopt,update,error) ||
            !collection->Apply(scene,device,update,nullptr,error))
        {
            VANS_LOG_ERROR("[PCG] Recipe '" << recipe.name << "', region '" << layer.regionId
                << "', layer '" << layer.layerId << "' build failed: " << error);
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
