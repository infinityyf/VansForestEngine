#pragma once
#include "../VansNavigationBuilder.h"
#include "../VansNavigationBuildContext.h"

struct rcCompactHeightfield;
struct rcContext;
namespace Vans
{
enum class VansNavigationTileCacheBuildResult { Built, RecastRequired, Failed };
enum class VansNavigationCachedBuildResult { Built, Missing, Failed };
struct VansNavigationCachedCell;
struct VansNavigationBuildContextAccess
{
    static bool Begin(VansNavigationBuildContext& context,std::uint64_t baseHash,std::uint64_t bakeHash,float rasterOrigin,std::string& error);
    static void End(VansNavigationBuildContext& context);
    static std::uint64_t WorkingBytes(const VansNavigationBuildContext& context);
    static VansNavigationCachedBuildResult BuildCached(VansNavigationBuildContext& context,int x,int z,
        const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
        const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,
        std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
        std::vector<VansNavigationTilePayload>& output,std::string& error,VansNavigationTileLayerData* layerData=nullptr);
    static bool Seed(VansNavigationBuildContext& context,const std::vector<VansNavigationTileLayerData>& rows,
        const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,std::uint64_t maximumBytes,std::string& error);
    static void StoreBase(VansNavigationBuildContext& context,int x,int z,const std::vector<VansNavigationTilePayload>& tiles);
    static void StoreCell(VansNavigationBuildContext& context,int x,int z,std::unique_ptr<VansNavigationCachedCell> cell);
};
// Compress immutable base layers without constructing a native cache or staging mesh.
VansNavigationTileCacheBuildResult BuildNavigationTileCacheLayers(rcContext& context,const rcCompactHeightfield& compact,
    int border,int heightCells,const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    int x,int z,std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
    VansNavigationTileLayerData& output,std::string& error,VansNavigationBuildContext* buildContext=nullptr);
// 输入为共享 Recast 内核产生的 compact field；native cache/staging 由 CPU 构建上下文独占。
VansNavigationTileCacheBuildResult BuildNavigationCarvedTileCache(rcContext& context,const rcCompactHeightfield& compact,
    int border,int heightCells,const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    int x,int z,const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,std::uint64_t maximumBytes,
    const VansNavigationBuildControl* control,std::vector<VansNavigationTilePayload>& output,std::string& error,
    VansNavigationBuildContext* buildContext=nullptr,VansNavigationTileLayerData* layerData=nullptr);
// native width、polygon 或 touched 范围不支持时，在同一 compact field 上重建。
void MarkNavigationCarveAreas(rcCompactHeightfield& compact,const VansNavigationBakeSettings& bake,
    const std::vector<VansNavigationObstacleGeometry>& obstacles);
}
