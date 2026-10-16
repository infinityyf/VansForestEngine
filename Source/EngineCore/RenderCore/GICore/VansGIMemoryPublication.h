#pragma once

#include "VansGIMemorySnapshot.h"
#include <mutex>

namespace VansGraphics
{
    // 各资源 owner 只在生命周期边界更新自己的部分，查询只复制已发布的固定快照。
    class VansGIMemoryPublication
    {
    public:
        VansGIMemorySnapshot Read() const;
        void PublishProbes(VansGIProbeMemory current, uint64_t retiredBytes);
        void PublishConsumer(VansGIMemoryUsage VansGIMemorySnapshot::* component, VansGIMemoryUsage current);
        void Retire(uint64_t bytes);

    private:
        void UpdatePeak();
        mutable std::mutex m_Mutex;
        VansGIMemorySnapshot m_Snapshot;
    };
}
