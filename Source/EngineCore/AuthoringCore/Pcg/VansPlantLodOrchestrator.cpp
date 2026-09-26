#include "VansPlantLodOrchestrator.h"

#include "../../AssetCore/VansAssetDatabase.h"

namespace Vans
{
bool VansPlantLodOrchestrator::Build(VansAssetDatabase& database,
    const VansPlantTypeAsset& source,
    VansModelLodBuildMode mode,
    VansPlantTypeAsset& result,
    std::vector<VansPlantLodVariantSummary>& summaries,
    std::string& error)
{
    error.clear();
    summaries.clear();
    if(source.category!=VansPlantCategory::Tree)
    {
        error="Select a tree plant.";
        return false;
    }
    VansPlantTypeAsset candidate=source;
    for(auto& variant:candidate.variants)
    {
        if(variant.geometry!=VansPlantGeometry::Mesh||variant.parts.empty())continue;
        std::vector<VansModelLodSourcePart> parts;
        parts.reserve(variant.parts.size());
        for(const auto& part:variant.parts)
        {
            if(!part.mesh.IsValid()||!part.material.IsValid())
            {
                error=variant.name+": Every model LOD part requires valid model and material references.";
                return false;
            }
            parts.push_back({part.mesh,part.material,part.submesh,
                part.kind==VansPlantPartKind::Leaves});
        }
        VansModelLodAsset built;
        if(!VansModelLodBuilder::Build(
            database,parts,variant.lodSettings,mode,built,error))
        {
            error=variant.name+": "+error;
            return false;
        }
        variant.lod=std::move(built);
        VansPlantLodVariantSummary summary;
        summary.variantId=variant.id;
        summary.buildKey=variant.lod.buildKey;
        for(const auto& level:variant.lod.levels)
        {
            std::uint64_t triangles=0;
            for(const auto& part:level.parts)triangles+=part.triangleCount;
            summary.triangleCounts.push_back(triangles);
        }
        summaries.push_back(std::move(summary));
    }
    result=std::move(candidate);
    return true;
}
}
