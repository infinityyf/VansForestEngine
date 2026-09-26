#pragma once
#include "VansPcgExecutor.h"
#include <map>
#include <optional>
#include <tuple>
namespace Vans
{
struct VansPcgBatchKey
{
    std::string region, layer, variant;
    std::int64_t x=0,z=0;
    auto Tuple() const { return std::tie(region,layer,variant,x,z); }
    bool operator<(const VansPcgBatchKey& other) const { return Tuple()<other.Tuple(); }
    bool operator==(const VansPcgBatchKey& other) const { return Tuple()==other.Tuple(); }
};
struct VansPcgBatchSource
{
    VansPcgBatchKey key;
    std::shared_ptr<const VansPlantTypeAsset> plant;
    std::vector<VansPcgPoint> points;
};
struct VansPcgBatchUpdate
{
    std::string region, layer;
    bool replaceAll=false;
    float cellSize=0;
    // 无范围表示替换整层，包括本次生成为空的所有旧批次。
    std::optional<VansPcgBounds> coverage;
    std::map<VansPcgBatchKey,std::shared_ptr<const VansPcgBatchSource>> batches;
    bool Contains(const VansPcgBatchKey& key) const;
};
enum class VansPcgUpdateScope
{
    LocalCoverage,
    WholeRegion
};
VansPcgUpdateScope ResolvePcgUpdateScope(const VansPcgLayer& layer);
bool BuildPcgBatchUpdate(const VansPcgRegion& region,const VansPcgLayerResult& result,
    const std::optional<VansPcgBounds>& coverage,VansPcgBatchUpdate& update,std::string& error);
bool EqualPcgBatchSources(const VansPcgBatchSource& a,const VansPcgBatchSource& b);
bool PcgCellCoordinate(float position,float cellSize,std::int64_t& cell);
std::optional<VansPcgBounds> PcgMaskUpdateCoverage(const VansPcgRegion& region,
    const VansPcgLayer& layer,const VansPlantTypeAsset& plant,const VansPcgMask& mask,
    const VansPcgPixelRect& changed);
}
