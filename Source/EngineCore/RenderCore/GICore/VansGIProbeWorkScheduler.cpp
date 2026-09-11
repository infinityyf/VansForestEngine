#include "VansGIProbeWorkScheduler.h"

#include <limits>
#include <utility>

namespace VansGraphics
{
    bool VansGIProbeWorkScheduler::Configure(std::vector<GIProbeWorkRegion> descriptions,
        uint32_t maxProbeUpdates, uint32_t maxRays, std::string& error)
    {
        error.clear();
        if (!maxProbeUpdates || !maxRays)
        { error = "GI work budgets must be positive"; return false; }
        std::vector<Region> regions;
        std::vector<GIProbeRegionWork> frame;
        for (auto& description : descriptions)
        {
            if (description.raysPerProbe < 2u || description.raysPerProbe > 4096u)
            { error = "GI rays per probe must be in [2, 4096]"; return false; }
            std::sort(description.probeIndices.begin(), description.probeIndices.end());
            if (std::adjacent_find(description.probeIndices.begin(), description.probeIndices.end()) != description.probeIndices.end())
            { error = "GI placed probe addresses must be unique within a region"; return false; }
            const uint32_t rays = description.raysPerProbe;
            if (!description.probeIndices.empty() && rays > maxRays)
            { error = "GI ray budget cannot hold one complete probe"; return false; }
            Region region;
            region.probes.resize(description.probeIndices.size());
            region.description = std::move(description);
            GIProbeRegionWork work;
            work.raysPerProbeUpdate = rays;
            work.entries.reserve((std::min)({region.probes.size(), size_t(maxProbeUpdates), size_t(maxRays / rays)}));
            regions.push_back(std::move(region));
            frame.push_back(std::move(work));
        }
        m_Regions = std::move(regions); m_Frame = std::move(frame);
        m_MaxUpdates = maxProbeUpdates; m_MaxRays = maxRays; m_RegionCursor = 0; m_TimeSeconds = 0.0;
        return true;
    }

    void VansGIProbeWorkScheduler::ResetLighting()
    {
        // 重建历史不重置轮询位置、采样序号和完成统计，反复改灯也不会饿死队尾。
        for (auto& region : m_Regions)
            for (auto& probe : region.probes)
            { probe.resetLighting = true; probe.awaitingFeedback = false; }
    }

    const std::vector<GIProbeRegionWork>& VansGIProbeWorkScheduler::NextFrame(double deltaSeconds)
    {
        m_TimeSeconds += std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0, 10.0) : 1.0 / 60.0;
        for (auto& work : m_Frame) work.entries.clear();
        uint32_t updatesLeft = m_MaxUpdates, raysLeft = m_MaxRays;
        size_t exhaustedRegions = 0;
        while (updatesLeft && !m_Regions.empty() && exhaustedRegions < m_Regions.size())
        {
            auto& region = m_Regions[m_RegionCursor];
            auto& work = m_Frame[m_RegionCursor];
            if (work.entries.size() == region.probes.size())
            {
                // 空区域或本帧已全部更新的区域不重复调度。
                m_RegionCursor = (m_RegionCursor + 1u) % m_Regions.size();
                ++exhaustedRegions;
                continue;
            }
            // 剩余额度不足时保留当前区域，下一帧优先处理它。
            // 不能跳过后再让廉价区域耗尽预算，否则昂贵区域可能永久停更。
            if (work.raysPerProbeUpdate > raysLeft) break;
            exhaustedRegions = 0;
            const size_t slot = region.cursor;
            auto& probe = region.probes[slot];
            GIProbeWorkEntry entry;
            entry.probeIndex = region.description.probeIndices[slot];
            entry.elapsedSeconds = float((std::max)(m_TimeSeconds - probe.lastUpdateSeconds, 1.0 / 1000.0));
            entry.cycleIndex = probe.updateSequence++;
            entry.flags = probe.resetLighting ? GIWorkResetLighting : 0u;
            probe.lastUpdateSeconds = m_TimeSeconds;
            probe.lastWork = entry; probe.awaitingFeedback = true;
            region.cursor = (slot + 1u) % region.probes.size();
            work.entries.push_back(entry);
            --updatesLeft; raysLeft -= work.raysPerProbeUpdate;
            m_RegionCursor = (m_RegionCursor + 1u) % m_Regions.size();
        }
        // GPU 调试按物理 ID 二分查询；排序不改变跨帧轮询位置。
        for (auto& work : m_Frame)
            std::sort(work.entries.begin(), work.entries.end(), [](const auto& a, const auto& b) {
                return a.probeIndex < b.probeIndex;
            });
        return m_Frame;
    }

    bool VansGIProbeWorkScheduler::ApplyFeedback(size_t index, const GIProbeWorkEntry& issued,
        const GIProbeFeedback& feedback)
    {
        if (index >= m_Regions.size() || feedback.probeIndex != issued.probeIndex ||
            feedback.elapsedSeconds != issued.elapsedSeconds || feedback.cycleIndex != issued.cycleIndex ||
            feedback.completed != 1u) return false;
        auto& region = m_Regions[index];
        const auto found = std::lower_bound(region.description.probeIndices.begin(), region.description.probeIndices.end(), issued.probeIndex);
        if (found == region.description.probeIndices.end() || *found != issued.probeIndex) return false;
        auto& probe = region.probes[size_t(found - region.description.probeIndices.begin())];
        const auto& last = probe.lastWork;
        if (!probe.awaitingFeedback || last.elapsedSeconds != issued.elapsedSeconds || last.cycleIndex != issued.cycleIndex ||
            last.flags != issued.flags) return false;
        probe.awaitingFeedback = false; probe.resetLighting = false;
        ++probe.completedUpdates;
        return true;
    }

    uint32_t VansGIProbeWorkScheduler::RegionCapacity(size_t region) const
    {
        if (region >= m_Regions.size()) return 0;
        return uint32_t((std::min)({m_Regions[region].probes.size(), size_t(m_MaxUpdates),
            size_t(m_MaxRays / m_Frame[region].raysPerProbeUpdate)}));
    }

    uint32_t VansGIProbeWorkScheduler::RegionRayCapacity(size_t region) const
    {
        if (region >= m_Regions.size()) return 0;
        return RegionCapacity(region) * m_Frame[region].raysPerProbeUpdate;
    }

    GIProbeUpdateCoverage VansGIProbeWorkScheduler::RegionCoverage(size_t region) const
    {
        GIProbeUpdateCoverage coverage;
        if (region >= m_Regions.size() || m_Regions[region].probes.empty()) return coverage;
        coverage.placed = m_Regions[region].probes.size();
        coverage.minCompletedUpdates = (std::numeric_limits<uint64_t>::max)();
        for (const auto& probe : m_Regions[region].probes)
        {
            coverage.updated += probe.completedUpdates != 0u;
            coverage.minCompletedUpdates = (std::min)(coverage.minCompletedUpdates, probe.completedUpdates);
            coverage.maxCompletedUpdates = (std::max)(coverage.maxCompletedUpdates, probe.completedUpdates);
        }
        return coverage;
    }
}
