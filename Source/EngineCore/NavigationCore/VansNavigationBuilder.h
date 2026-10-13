#pragma once

#include "VansNavigationMeshAsset.h"
#include "VansNavigationSourceSnapshot.h"
#include <atomic>

namespace Vans
{
class VansNavigationBuildContext;
// 构建控制由请求 owner 持有；领域输入快照不存回调或场景指针。
class VansNavigationBuildControl final
{
public:
    void Cancel() { m_Cancelled.store(true, std::memory_order_release); }
    bool IsCancelled() const { return m_Cancelled.load(std::memory_order_acquire); }
private:
    std::atomic<bool> m_Cancelled{false};
};
class VansNavigationBuilder
{
public:
    // sourceScene 仅记录 provenance，构建器不访问 Scene 或文件系统。
    static bool Build(const VansNavigationBuildInput& input, const VansNavigationSettings& settings,
        const std::string& sourceScene, const VansNavigationBuildControl* control,
        std::shared_ptr<const VansNavigationMeshAsset>& output,
        std::string& error);
    // The Map supplies its original vertical raster origin. Extending the input
    // below it preserves the established voxel phase across unchanged cells.
    static bool BuildSelectedTiles(const VansNavigationProfileBuildInput& input,
        const VansNavigationSettings& settings,const std::string& mapId,
        const std::vector<VansNavigationTileCell>& cells,float rasterOriginMeters,
        std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
        VansNavigationTileBuildOutput& output,std::string& error,
        VansNavigationBuildContext* context=nullptr,const std::vector<VansNavigationTileLayerData>* seedLayers=nullptr);
};
}
