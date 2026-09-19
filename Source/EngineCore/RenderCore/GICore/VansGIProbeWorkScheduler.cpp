#include "VansGIProbeWorkScheduler.h"

#include <limits>
#include <utility>

namespace VansGraphics
{
    bool VansGIProbeWorkScheduler::Configure(std::vector<GIProbeWorkRegion> descriptions,
        uint32_t maxProbeUpdates, uint32_t maxRays, std::string& error, bool balanceRegionSweeps)
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
            if(!std::isfinite(description.prewarmSpacing) || description.prewarmSpacing<0.0f)
            { error = "GI prewarm spacing must be finite and nonnegative"; return false; }
            std::sort(description.probeIndices.begin(), description.probeIndices.end());
            if (std::adjacent_find(description.probeIndices.begin(), description.probeIndices.end()) != description.probeIndices.end())
            { error = "GI placed probe addresses must be unique within a region"; return false; }
            const uint32_t rays = description.raysPerProbe;
            description.maxPlacedProbes=std::max(description.maxPlacedProbes,uint32_t(description.probeIndices.size()));
            if (description.maxPlacedProbes && rays > maxRays)
            { error = "GI ray budget cannot hold one complete probe"; return false; }
            Region region;
            region.probes.resize(description.probeIndices.size());
            region.description = std::move(description);
            if(region.description.prewarmSpacing>0.0f)
                for(size_t slot=0;slot<region.probes.size();++slot)
                { region.prewarm.push_back(slot);region.probes[slot].prewarmQueued=true; }
            GIProbeRegionWork work;
            work.raysPerProbeUpdate = rays;
            work.entries.reserve((std::min)({size_t(region.description.maxPlacedProbes), size_t(maxProbeUpdates), size_t(maxRays / rays)}));
            regions.push_back(std::move(region));
            frame.push_back(std::move(work));
        }
        m_Regions = std::move(regions); m_Frame = std::move(frame);
        m_MaxUpdates = maxProbeUpdates; m_MaxRays = maxRays; m_RegionCursor = 0; m_TimeSeconds = 0.0;
        m_BalanceRegionSweeps = balanceRegionSweeps;
        m_FrameNumber=0;
        m_PrewarmEnabled=false;m_PrewarmReady=true;m_MaxPrewarmRayCost=0;m_PrewarmRayCredit=0;m_PrewarmUpdateCredit=0;
        for(const auto& region:m_Regions)if(region.description.prewarmSpacing>0.0f)
        {m_PrewarmEnabled=true;m_MaxPrewarmRayCost=std::max(m_MaxPrewarmRayCost,region.description.raysPerProbe);}
        return true;
    }

    void VansGIProbeWorkScheduler::ResetLighting()
    {
        // 重建历史不重置轮询位置、采样序号和完成统计，反复改灯也不会饿死队尾。
        for (auto& region : m_Regions)
            for (size_t slot=0;slot<region.probes.size();++slot)
            {
                auto& probe=region.probes[slot];probe.resetLighting=true;probe.awaitingFeedback=false;
                if(region.description.prewarmSpacing>0.0f && !probe.completedUpdates && !probe.prewarmQueued)
                {region.prewarm.push_back(slot);probe.prewarmQueued=true;}
            }
    }

    bool VansGIProbeWorkScheduler::InvalidateLighting(size_t index,const std::vector<uint32_t>& indices,std::string& error)
    {
        error.clear();
        if(index>=m_Regions.size()){error="GI work region is invalid";return false;}
        auto& region=m_Regions[index];
        for(const auto& probe:region.probes)if(probe.awaitingFeedback)
        {error="GI lighting changed before retiring GPU feedback";return false;}
        std::vector<size_t> slots;slots.reserve(indices.size());
        const auto& placed=region.description.probeIndices;
        for(auto id:indices)
        {
            const auto found=std::lower_bound(placed.begin(),placed.end(),id);
            if(found==placed.end() || *found!=id){error="GI lighting references an unplaced probe";return false;}
            slots.push_back(size_t(found-placed.begin()));
        }
        std::vector<bool> queued(region.probes.size(),false);
        for(auto slot:region.priority)queued[slot]=true;
        for(auto slot:slots)
        {
            region.probes[slot].resetLighting=true;
            if(!queued[slot]){region.priority.push_back(slot);queued[slot]=true;}
        }
        if(!slots.empty())region.preferPriority=true;
        return true;
    }

    bool VansGIProbeWorkScheduler::UpdatePlacedProbes(size_t index,std::vector<uint32_t> indices,
        const std::vector<uint32_t>& resetLighting,std::string& error,const std::vector<uint32_t>& recycled)
    {
        error.clear();
        if(index>=m_Regions.size()){error="GI work region is invalid";return false;}
        auto& region=m_Regions[index];
        if(indices.size()>region.description.maxPlacedProbes){error="GI work placement exceeds reserved capacity";return false;}
        std::sort(indices.begin(),indices.end());
        if(std::adjacent_find(indices.begin(),indices.end())!=indices.end())
        {error="GI work placement has duplicate addresses";return false;}
        for(const auto& probe:region.probes)if(probe.awaitingFeedback)
        {error="GI placement changed before retiring GPU feedback";return false;}
        const auto& old=region.description.probeIndices;
        const uint32_t next=old.empty()?0:old[region.cursor];
        std::vector<Region::Probe> probes;probes.reserve(indices.size());
        auto replaced=recycled;std::sort(replaced.begin(),replaced.end());
        size_t previous=0;
        for(auto id:indices)
        {
            while(previous<old.size() && old[previous]<id)++previous;
            if(previous<old.size() && old[previous]==id && !std::binary_search(replaced.begin(),replaced.end(),id))probes.push_back(region.probes[previous]);
            else{Region::Probe probe;probe.updateSequence=region.nextPlacementSequence++;probe.placedFrame=m_FrameNumber;probes.push_back(probe);}
        }
        for(auto id:resetLighting)
        {
            const auto found=std::lower_bound(indices.begin(),indices.end(),id);
            if(found!=indices.end() && *found==id)probes[size_t(found-indices.begin())].resetLighting=true;
        }
        // 空区域重新加入当前轮转，而不是补偿它空置期间根本不存在的工作。
        if(old.empty() && !indices.empty())
        {
            double current=(std::numeric_limits<double>::max)();
            for(const auto& other:m_Regions)if(!other.probes.empty())current=std::min(current,other.sweepProgress);
            if(current!=(std::numeric_limits<double>::max)())region.sweepProgress=std::max(region.sweepProgress,current);
        }
        std::deque<size_t> prewarm;
        for(auto& probe:probes)probe.prewarmQueued=false;
        if(region.description.prewarmSpacing>0.0f)
        {
            // 保留旧请求顺序；连续滚动不会每帧重新按物理 ID 排队而压住队尾。
            for(auto slot:region.prewarm)
            {
                const auto id=old[slot];const auto found=std::lower_bound(indices.begin(),indices.end(),id);
                if(found==indices.end() || *found!=id || std::binary_search(replaced.begin(),replaced.end(),id))continue;
                const auto nextSlot=size_t(found-indices.begin());auto& probe=probes[nextSlot];
                if(!probe.completedUpdates && !probe.prewarmQueued){prewarm.push_back(nextSlot);probe.prewarmQueued=true;}
            }
            for(size_t slot=0;slot<probes.size();++slot)if(!probes[slot].completedUpdates && !probes[slot].prewarmQueued)
            {prewarm.push_back(slot);probes[slot].prewarmQueued=true;}
        }
        region.cursor=indices.empty()?0:size_t(std::lower_bound(indices.begin(),indices.end(),next)-indices.begin())%indices.size();
        region.probes=std::move(probes);region.description.probeIndices=std::move(indices);
        region.prewarm=std::move(prewarm);
        region.priority.clear();
        for(size_t slot=0;slot<region.probes.size();++slot)if(region.probes[slot].resetLighting)region.priority.push_back(slot);
        region.preferPriority=true;
        m_Frame[index].entries.clear();return true;
    }

    const std::vector<GIProbeRegionWork>& VansGIProbeWorkScheduler::NextFrame(double deltaSeconds)
    {
        ++m_FrameNumber;
        m_TimeSeconds += std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0, 10.0) : 1.0 / 60.0;
        for (auto& work : m_Frame) {work.entries.clear();work.prewarmUpdates=0;}
        // 最多借用 1/8 总预算预热，额度按完整探针积累；小预算也不会饿死昂贵请求。
        // 没有待预热请求时额度不消耗，普通轮转可使用整帧预算。
        if(m_PrewarmEnabled)
        {
            m_PrewarmRayCredit=std::min(m_PrewarmRayCredit+m_MaxRays/8.0,std::max(m_MaxRays/8.0,double(m_MaxPrewarmRayCost)));
            m_PrewarmUpdateCredit=std::min(m_PrewarmUpdateCredit+m_MaxUpdates/8.0,std::max(m_MaxUpdates/8.0,1.0));
        }
        uint32_t updatesLeft = m_MaxUpdates, raysLeft = m_MaxRays;
        size_t exhaustedRegions = 0;
        while (updatesLeft && !m_Regions.empty() && exhaustedRegions < m_Regions.size())
        {
            const size_t regularCursor=m_RegionCursor;
            size_t warmRegion=m_Regions.size();uint64_t oldest=UINT64_MAX;float finest=(std::numeric_limits<float>::max)();
            if(m_PrewarmEnabled && m_PrewarmReady && m_PrewarmUpdateCredit>=1.0)
                for(size_t index=0;index<m_Regions.size();++index)
                {
                    auto& candidate=m_Regions[index];
                    while(!candidate.prewarm.empty())
                    {
                        auto& probe=candidate.probes[candidate.prewarm.front()];
                        if(!probe.completedUpdates && !probe.awaitingFeedback && probe.lastIssuedFrame!=m_FrameNumber)break;
                        probe.prewarmQueued=false;candidate.prewarm.pop_front();
                    }
                    if(candidate.prewarm.empty() || candidate.description.raysPerProbe>raysLeft ||
                        candidate.description.raysPerProbe>m_PrewarmRayCredit)continue;
                    const auto age=candidate.probes[candidate.prewarm.front()].placedFrame;
                    if(age<oldest || (age==oldest && candidate.description.prewarmSpacing<finest))
                    {warmRegion=index;oldest=age;finest=candidate.description.prewarmSpacing;}
                }
            const bool prewarm=warmRegion<m_Regions.size();
            if(prewarm)m_RegionCursor=warmRegion;
            else if(m_BalanceRegionSweeps)
            {
                // 按下一次完整轮询的进度选择，避免小型户外区域占走室内一半预算。
                // 只扫描最多八个区域，不扫描探针；昂贵工作不够本帧预算时仍保留到下一帧。
                double earliest=(std::numeric_limits<double>::max)();
                size_t selected=m_Regions.size();
                for(size_t offset=0;offset<m_Regions.size();++offset)
                {
                    size_t index=(m_RegionCursor+offset)%m_Regions.size();
                    const auto& candidate=m_Regions[index];
                    if(m_Frame[index].entries.size()==candidate.probes.size())continue;
                    double deadline=candidate.sweepProgress+1.0/double(candidate.probes.size());
                    if(deadline<earliest){earliest=deadline;selected=index;}
                }
                if(selected==m_Regions.size())break;
                m_RegionCursor=selected;
            }
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
            while(!region.priority.empty() && (!region.probes[region.priority.front()].resetLighting ||
                region.probes[region.priority.front()].lastIssuedFrame==m_FrameNumber))region.priority.pop_front();
            const bool urgent=!prewarm && region.preferPriority && !region.priority.empty();
            size_t slot;
            if(prewarm){slot=region.prewarm.front();region.prewarm.pop_front();region.probes[slot].prewarmQueued=false;}
            else if(urgent){slot=region.priority.front();region.priority.pop_front();}
            else
            {
                while(region.probes[region.cursor].lastIssuedFrame==m_FrameNumber)region.cursor=(region.cursor+1u)%region.probes.size();
                slot=region.cursor;region.cursor=(slot+1u)%region.probes.size();
            }
            // 局部新探针和正常轮转交替，刷绘不能饿死未编辑区域。
            if(!prewarm)region.preferPriority=!urgent;
            auto& probe = region.probes[slot];
            GIProbeWorkEntry entry;
            entry.probeIndex = region.description.probeIndices[slot];
            entry.elapsedSeconds = float((std::max)(m_TimeSeconds - probe.lastUpdateSeconds, 1.0 / 1000.0));
            entry.cycleIndex = probe.updateSequence++;
            region.nextPlacementSequence=std::max(region.nextPlacementSequence,probe.updateSequence);
            entry.flags = probe.resetLighting ? GIWorkResetLighting : 0u;
            probe.lastUpdateSeconds = m_TimeSeconds;
            probe.lastWork = entry; probe.awaitingFeedback = true;
            probe.lastIssuedFrame=m_FrameNumber;
            work.entries.push_back(entry);
            if(prewarm)
            {m_PrewarmRayCredit-=work.raysPerProbeUpdate;m_PrewarmUpdateCredit-=1.0;++work.prewarmUpdates;}
            else region.sweepProgress+=1.0/double(region.probes.size());
            --updatesLeft; raysLeft -= work.raysPerProbeUpdate;
            m_RegionCursor = prewarm?regularCursor:(m_RegionCursor + 1u) % m_Regions.size();
        }
        // GPU 调试按物理 ID 二分查询；排序不改变跨帧轮询位置。
        for (auto& work : m_Frame)
            std::sort(work.entries.begin(), work.entries.end(), [](const auto& a, const auto& b) {
                return a.probeIndex < b.probeIndex;
            });
        return m_Frame;
    }

    bool VansGIProbeWorkScheduler::ApplyFeedback(size_t index, const GIProbeWorkEntry& issued,
        const GIProbeFeedback& feedback, bool allowIncomplete)
    {
        if (index >= m_Regions.size() || feedback.probeIndex != issued.probeIndex ||
            feedback.elapsedSeconds != issued.elapsedSeconds || feedback.cycleIndex != issued.cycleIndex ||
            (feedback.status != GIProbeComplete && (!allowIncomplete || (feedback.status & ~GIProbeFailureMask) != 0u))) return false;
        auto& region = m_Regions[index];
        const auto found = std::lower_bound(region.description.probeIndices.begin(), region.description.probeIndices.end(), issued.probeIndex);
        if (found == region.description.probeIndices.end() || *found != issued.probeIndex) return false;
        auto& probe = region.probes[size_t(found - region.description.probeIndices.begin())];
        const auto& last = probe.lastWork;
        if (!probe.awaitingFeedback || last.elapsedSeconds != issued.elapsedSeconds || last.cycleIndex != issued.cycleIndex ||
            last.flags != issued.flags) return false;
        probe.awaitingFeedback = false;
        // 有效的未完成反馈保留重置请求与更新计数，下一次完整球面重新尝试。
        if (feedback.status != GIProbeComplete)
        {
            ++probe.incompleteAttempts;
            region.heightFailures += (feedback.status & GIProbeHeightBudget) != 0u;
            region.pageFailures += (feedback.status & GIProbeVoxelPage) != 0u;
            region.stepFailures += (feedback.status & GIProbeVoxelBudget) != 0u;
            region.coverageFailures += (feedback.status & GIProbeVoxelCoverage) != 0u;
            if(region.description.prewarmSpacing>0.0f && !probe.completedUpdates && !probe.prewarmQueued)
            {region.prewarm.push_back(size_t(found-region.description.probeIndices.begin()));probe.prewarmQueued=true;}
            return true;
        }
        probe.resetLighting = false;
        ++probe.completedUpdates;
        probe.lastCompletedFrame = m_FrameNumber;
        return true;
    }

    uint32_t VansGIProbeWorkScheduler::RegionCapacity(size_t region) const
    {
        if (region >= m_Regions.size()) return 0;
        return uint32_t((std::min)({size_t(m_Regions[region].description.maxPlacedProbes), size_t(m_MaxUpdates),
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
        if (region >= m_Regions.size()) return coverage;
        coverage.heightFailures=m_Regions[region].heightFailures;
        coverage.pageFailures=m_Regions[region].pageFailures;
        coverage.stepFailures=m_Regions[region].stepFailures;
        coverage.coverageFailures=m_Regions[region].coverageFailures;
        if(m_Regions[region].probes.empty())return coverage;
        coverage.placed = m_Regions[region].probes.size();
        coverage.minCompletedUpdates = (std::numeric_limits<uint64_t>::max)();
        for (const auto& probe : m_Regions[region].probes)
        {
            coverage.updated += probe.completedUpdates != 0u;
            coverage.minCompletedUpdates = (std::min)(coverage.minCompletedUpdates, probe.completedUpdates);
            coverage.maxCompletedUpdates = (std::max)(coverage.maxCompletedUpdates, probe.completedUpdates);
            const bool attempted = probe.lastIssuedFrame != UINT64_MAX;
            coverage.attempted += attempted;
            coverage.incompleteAttempts += probe.incompleteAttempts;
            coverage.oldestAttemptAge = (std::max)(coverage.oldestAttemptAge,
                m_FrameNumber - (attempted ? probe.lastIssuedFrame : probe.placedFrame));
            if (probe.completedUpdates)
                coverage.oldestCompletionAge = (std::max)(coverage.oldestCompletionAge, m_FrameNumber - probe.lastCompletedFrame);
            else
                coverage.oldestUnpublishedAge = (std::max)(coverage.oldestUnpublishedAge, m_FrameNumber - probe.placedFrame);
        }
        return coverage;
    }
}
