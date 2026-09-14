#include "VansPcgBatchPlan.h"
#include <algorithm>
#include <cmath>
namespace Vans
{
std::optional<VansPcgBounds> PcgMaskUpdateCoverage(const VansPcgRegion& region,
    const VansPcgLayer& layer,const VansPlantTypeAsset& plant,const VansPcgMask& mask,
    const VansPcgPixelRect& changed)
{
    // 数量补齐、人工覆盖和偏移后的区块归属具有整层依赖，不能截断后替换局部批次。
    if (layer.source!=VansPcgSourceMode::Density || !layer.addedInstances.empty() ||
        !layer.overrides.empty() || layer.placement.rootOffset!=0 || !mask.IsValid() ||
        changed.Empty() || !std::isfinite(region.cellSize) || region.cellSize<=0) return std::nullopt;
    double radius=0;
    for (const auto& variant : plant.variants)
        radius=std::max(radius,static_cast<double>(variant.footprintRadius)*
            std::max(layer.placement.scaleMax[0],layer.placement.scaleMax[2]));
    const double halo=std::max(static_cast<double>(layer.placement.minimumSpacing),radius*2);
    VansPcgBounds bounds;
    const std::uint32_t lo[2]={changed.minX,changed.minY},hi[2]={changed.maxX,changed.maxY};
    const std::uint32_t dimensions[2]={mask.width,mask.height};
    for (int axis=0;axis<2;++axis)
    {
        const double texel=(static_cast<double>(mask.bounds.max[axis])-mask.bounds.min[axis])/dimensions[axis];
        // 双线性采样影响邻接像素；包含间距竞争半径后按完整渲染区块更新。
        const double minimum=mask.bounds.min[axis]+(static_cast<double>(lo[axis])-1)*texel-halo;
        const double maximum=mask.bounds.min[axis]+(static_cast<double>(hi[axis])+1)*texel+halo;
        bounds.min[axis]=static_cast<float>(std::floor(minimum/region.cellSize)*region.cellSize);
        bounds.max[axis]=static_cast<float>(std::ceil(maximum/region.cellSize)*region.cellSize);
    }
    return bounds.IsValid()?std::optional<VansPcgBounds>(bounds):std::nullopt;
}
bool PcgCellCoordinate(float position,float cellSize,std::int64_t& cell)
{
    if (!std::isfinite(position) || !std::isfinite(cellSize) || cellSize<=0) return false;
    const double value=std::floor(static_cast<double>(position)/cellSize);
    if (!std::isfinite(value) || std::abs(value)>4503599627370495.0) return false;
    cell=static_cast<std::int64_t>(value);
    return true;
}
bool VansPcgBatchUpdate::Contains(const VansPcgBatchKey& key) const
{
    if (replaceAll) return true;
    if (key.region!=region || key.layer!=layer) return false;
    if (!coverage) return true;
    const double x=static_cast<double>(key.x)*cellSize,z=static_cast<double>(key.z)*cellSize;
    return x<coverage->max[0] && x+cellSize>coverage->min[0] &&
        z<coverage->max[1] && z+cellSize>coverage->min[1];
}
bool BuildPcgBatchUpdate(const VansPcgRegion& region,const VansPcgLayerResult& result,
    const std::optional<VansPcgBounds>& coverage,VansPcgBatchUpdate& output,std::string& error)
{
    error.clear();
    if (region.id!=result.regionId || result.layerId.empty() || !result.plant ||
        !std::isfinite(region.cellSize) || region.cellSize<=0 || (coverage && !coverage->IsValid()))
    {error="Invalid PCG batch target, cell size or coverage.";return false;}
    VansPcgBatchUpdate update;
    update.region=region.id;update.layer=result.layerId;update.cellSize=region.cellSize;update.coverage=coverage;
    std::map<VansPcgBatchKey,std::shared_ptr<VansPcgBatchSource>> building;
    for (const auto& point : result.points)
    {
        if (point.variantIndex>=result.variantIds.size()) {error="Invalid PCG model variant index.";return false;}
        VansPcgBatchKey key{region.id,result.layerId,result.variantIds[point.variantIndex]};
        if (!PcgCellCoordinate(point.position[0],region.cellSize,key.x) ||
            !PcgCellCoordinate(point.position[2],region.cellSize,key.z) || !update.Contains(key))
        {error="PCG point is outside the supported update cells.";return false;}
        auto& batch=building[key];
        if (!batch) {batch=std::make_shared<VansPcgBatchSource>();batch->key=key;batch->plant=result.plant;}
        batch->points.push_back(point);
    }
    for (auto& entry : building)
    {
        auto& points=entry.second->points;
        std::sort(points.begin(),points.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        update.batches.emplace(entry.first,std::move(entry.second));
    }
    output=std::move(update);
    return true;
}
bool EqualPcgBatchSources(const VansPcgBatchSource& a,const VansPcgBatchSource& b)
{
    if (!(a.key==b.key) || a.plant!=b.plant || a.points.size()!=b.points.size()) return false;
    for (std::size_t i=0;i<a.points.size();++i)
    {
        const auto& x=a.points[i];const auto& y=b.points[i];
        if (x.id!=y.id || x.position!=y.position || x.rotation!=y.rotation || x.scale!=y.scale) return false;
    }
    return true;
}
}
