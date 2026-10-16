#pragma once
#include "VansGISettings.h"
#include <array>
#include <cstdint>

namespace VansGraphics
{
    struct VansGIMemoryUsage
    {
        uint64_t bufferCapacityBytes = 0;
        uint64_t bufferAllocationBytes = 0;
        uint64_t imageAllocationBytes = 0;
        uint64_t AllocationBytes() const { return bufferAllocationBytes + imageAllocationBytes; }
    };

    struct VansGIRegionMemory
    {
        uint32_t stableId = 0, physicalProbes = 0;
        // Physical probe storage, independent of the authored volume grid.
        glm::uvec3 storageDimensions{0u};
        uint64_t rayCapacity = 0;
        VansGIMemoryUsage storage;
    };

    // 探针/世界资源由 RT owner 作为一组发布，独立于屏幕端消费者。
    struct VansGIProbeMemory
    {
        bool available = false;
        uint32_t regionCount = 0;
        std::array<VansGIRegionMemory, kGIMaximumRegionCount> regions{};
        VansGIMemoryUsage world, shared, previews;
        uint64_t ActiveAllocationBytes() const
        {
            uint64_t bytes = world.AllocationBytes() + shared.AllocationBytes() + previews.AllocationBytes();
            for (uint32_t i = 0; i < regionCount; ++i) bytes += regions[i].storage.AllocationBytes();
            return bytes;
        }
    };

    // 各 owner 最后发布的资源及原退役队列保留的旧资源；不是 OS 显存驻留。
    struct VansGIMemorySnapshot
    {
        VansGIProbeMemory probes;
        VansGIMemoryUsage receiver, ssgi, ambientSky;
        uint64_t retiredAllocationBytes = 0;
        // 同一设备发布生命周期的 active + retired 峰值，排除失败候选和 staging。
        uint64_t publishedRetainedPeakBytes = 0;
        uint64_t ActiveAllocationBytes() const
        {
            return probes.ActiveAllocationBytes() + receiver.AllocationBytes() +
                ssgi.AllocationBytes() + ambientSky.AllocationBytes();
        }
        bool Available() const
        { return probes.available || ActiveAllocationBytes() != 0 || retiredAllocationBytes != 0; }
    };
}
