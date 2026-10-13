#pragma once
#include <cstdint>
#include <memory>

namespace Vans
{
struct VansNavigationBuildContextStats
{
    std::uint64_t retainedBytes=0,baseCellsBuilt=0,reusedCells=0,evictedCells=0;
};
// 单个 Map/Profile 的 CPU 构建状态；每量子只允许一个 worker。
// 外部只能读取已发布计数，不持有运行 Map 或 Scene。
class VansNavigationBuildContext final
{
public:
    explicit VansNavigationBuildContext(std::uint64_t maximumRetainedBytes);
    ~VansNavigationBuildContext();
    VansNavigationBuildContext(const VansNavigationBuildContext&)=delete;
    VansNavigationBuildContext& operator=(const VansNavigationBuildContext&)=delete;
    VansNavigationBuildContextStats CaptureStats() const;
private:
    friend struct VansNavigationBuildContextAccess;
    struct State;
    std::unique_ptr<State> m_State;
};
}
