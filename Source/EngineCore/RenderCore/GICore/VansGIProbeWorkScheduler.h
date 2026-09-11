#pragma once

#include "VansGISettings.h"
#include <string>

namespace VansGraphics
{
    constexpr uint32_t GIWorkResetLighting = 1u;
    // GPU 的唯一更新地址：物理 probe ID、距上次更新秒数、旋转序号、工作标志。
    // ID 与位置/叶块分开，规则网格和后续稀疏 atlas 共用这一份工作列表。
    struct alignas(16) GIProbeWorkEntry
    {
        uint32_t probeIndex = 0;
        float elapsedSeconds = 0.0f;
        uint32_t cycleIndex = 0;
        uint32_t flags = 0;
    };
    static_assert(sizeof(GIProbeWorkEntry) == 16);

    struct GIProbeRegionWork
    {
        uint32_t raysPerProbeUpdate = 0;
        std::vector<GIProbeWorkEntry> entries;
        uint64_t RayCount() const { return uint64_t(entries.size()) * raysPerProbeUpdate; }
    };

    struct alignas(16) GIProbeFeedback
    {
        uint32_t probeIndex, completed;
        float elapsedSeconds;
        uint32_t cycleIndex;
    };
    static_assert(sizeof(GIProbeFeedback) == 16);

    struct GIProbeWorkRegion
    {
        uint32_t raysPerProbe = 256;
        // 场景中所有已放置的物理地址；几何命中结果不改变更新资格。
        std::vector<uint32_t> probeIndices;
    };

    struct GIProbeUpdateCoverage
    {
        uint64_t placed = 0, updated = 0, minCompletedUpdates = 0, maxCompletedUpdates = 0;
    };

    class VansGIProbeWorkScheduler
    {
    public:
        bool Configure(std::vector<GIProbeWorkRegion> regions, uint32_t maxProbeUpdates,
            uint32_t maxRays, std::string& error);
        void ResetLighting();
        const std::vector<GIProbeRegionWork>& NextFrame(double deltaSeconds = 1.0 / 60.0);
        uint32_t RegionCapacity(size_t region) const;
        uint32_t RegionRayCapacity(size_t region) const;
        // 仅诊断按需扫描，统计由完成 fence 后匹配的 GPU 回读确认。
        GIProbeUpdateCoverage RegionCoverage(size_t region) const;
        bool ApplyFeedback(size_t region, const GIProbeWorkEntry& issued, const GIProbeFeedback& feedback);

    private:
        struct Region
        {
            struct Probe
            {
                uint64_t completedUpdates = 0;
                uint32_t updateSequence = 0;
                double lastUpdateSeconds = 0.0;
                GIProbeWorkEntry lastWork{};
                bool resetLighting = true, awaitingFeedback = false;
            };
            GIProbeWorkRegion description;
            std::vector<Probe> probes;
            size_t cursor = 0;
        };
        std::vector<Region> m_Regions;
        std::vector<GIProbeRegionWork> m_Frame;
        uint32_t m_MaxUpdates = 0, m_MaxRays = 0;
        size_t m_RegionCursor = 0;
        double m_TimeSeconds = 0.0;
    };
}
