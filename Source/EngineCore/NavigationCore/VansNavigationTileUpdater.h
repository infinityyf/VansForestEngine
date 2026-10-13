#pragma once
#include "VansNavigationMap.h"
#include "VansNavigationSourceSnapshot.h"

namespace Vans
{
struct VansNavigationTileUpdateStats
{
    std::size_t activeBuilds=0,retiringBuilds=0,pendingMaps=0,failedMaps=0;
    std::uint64_t retainedBytes=0,concurrentBuildBytes=0,startedBuilds=0,publishedCells=0;
    std::uint64_t cachedContextBytes=0,baseCellsBuilt=0,reusedCells=0,evictedCells=0;
};

// 主线程 owner：观察冻结输入、调度 CPU 构建、发布 Map 事务；不持有 Scene/Physics。
class VansNavigationTileUpdater final
{
public:
    VansNavigationTileUpdater();
    ~VansNavigationTileUpdater();
    VansNavigationTileUpdater(const VansNavigationTileUpdater&)=delete;
    VansNavigationTileUpdater& operator=(const VansNavigationTileUpdater&)=delete;
    bool Configure(const VansNavigationSettings& settings,std::string& error);
    bool Observe(const std::shared_ptr<VansNavigationMap>& map,
        std::shared_ptr<const VansNavigationProfileBuildInput> input,std::uint64_t inputHash,std::string& error);
    // 立即释放退出的 Map owner；其 CPU worker 继续计入退役容量，普通帧不等待。
    void RetainMaps(const std::vector<std::shared_ptr<const VansNavigationMap>>& maps);
    void WithdrawMap(const std::string& mapId);
    bool Tick(std::uint64_t externalResidentBytes,std::uint64_t externalResidentTiles,std::string& error);
    VansNavigationTileUpdateStats CaptureStats() const;
    void Shutdown();
private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
