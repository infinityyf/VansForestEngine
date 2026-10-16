#pragma once

#include "VansGISettings.h"
#include <string>
#include <deque>

namespace VansGraphics
{
    constexpr uint32_t GIWorkResetGeometry = 1u;
    // GPU 的唯一更新地址：物理 probe ID、距上次更新秒数、旋转序号、工作标志。
    // ID 与位置/叶块分开，规则网格和后续稀疏 atlas 共用这一份工作列表。
    struct alignas(16) GIProbeWorkEntry
    {
        uint32_t probeIndex = 0;
        float elapsedSeconds = 0.0f;
        uint32_t cycleIndex = 0;
        uint32_t flags = 0;
        // 两份分量各自消费光源变化量和成功发布年龄；不能把预算间隔当成样本次数。
        float primaryLightingChange = 0.0f, scatterLightingChange = 0.0f;
        float primaryAgeSeconds = 0.0f, scatterAgeSeconds = 0.0f;
    };
    static_assert(sizeof(GIProbeWorkEntry) == 32);

    struct GIProbeRegionWork
    {
        uint32_t raysPerProbeUpdate = 0;
        std::vector<GIProbeWorkEntry> entries;
        uint32_t prewarmUpdates = 0; // CPU 诊断，不上传到 GPU 工作表。
        uint64_t RayCount() const { return uint64_t(entries.size()) * raysPerProbeUpdate; }
    };

    enum GIProbeFeedbackStatus : uint32_t
    {
        GIProbeComplete = 1u,
        GIProbeHeightBudget = 2u,
        GIProbeVoxelPage = 4u,
        GIProbeVoxelBudget = 8u,
        GIProbeVoxelCoverage = 16u,
        GIProbePrimaryFailureMask = 30u,
        GIProbeScatterHeightBudget = 32u,
        GIProbeScatterVoxelPage = 64u,
        GIProbeScatterVoxelBudget = 128u,
        GIProbeScatterVoxelCoverage = 256u,
        GIProbeScatterFailureMask = 480u,
        GIProbePrimaryPublished = 512u, // 仅与辅助失败位组合；不代表完整更新。
        GIProbeFailureMask = GIProbePrimaryFailureMask | GIProbeScatterFailureMask
    };
    struct alignas(16) GIProbeFeedback
    {
        uint32_t probeIndex, status;
        float elapsedSeconds;
        uint32_t cycleIndex;
    };
    static_assert(sizeof(GIProbeFeedback) == 16);

    struct GIProbeWorkRegion
    {
        uint32_t raysPerProbe = 256;
        // 场景中所有已放置的物理地址；几何命中结果不改变更新资格。
        std::vector<uint32_t> probeIndices;
        uint32_t maxPlacedProbes = 0; // 动态资格列表的容量；0 表示使用初始化数量。
        float prewarmSpacing = 0.0f; // 0 关闭跨区域预热；同龄请求优先较细的滚动区域。
    };

    struct GIProbeUpdateCoverage
    {
        uint64_t placed = 0, updated = 0, minCompletedUpdates = 0, maxCompletedUpdates = 0;
        uint64_t attempted = 0, incompleteAttempts = 0;
        uint64_t primaryOnlyPublications = 0;
        uint64_t oldestAttemptAge = 0, oldestCompletionAge = 0, oldestUnpublishedAge = 0;
        // 区域生命周期累计；每次未完成工作合并全部射线的原因，非失败射线数。
        uint64_t heightFailures = 0, pageFailures = 0, stepFailures = 0, coverageFailures = 0;
        uint64_t scatterHeightFailures = 0, scatterPageFailures = 0, scatterStepFailures = 0, scatterCoverageFailures = 0;
    };

    class VansGIProbeWorkScheduler
    {
    public:
        bool Configure(std::vector<GIProbeWorkRegion> regions, uint32_t maxProbeUpdates,
            uint32_t maxRays, std::string& error, bool balanceRegionSweeps = false);
        // changeAmount 为确定光源的累计相对变化，1 表示需立即替换历史；调用前必须退役反馈。
        void NotifyGlobalLighting(float changeAmount);
        // 包含主光照已发布但散射尚未完整刷新的请求；最多检查八个区域的计数。
        bool HasPendingLightingUpdates() const;
        // 几何/覆盖发生变化或提交状态不可信时，重建辐射和距离历史。
        void ResetGeometry();
        void ResetGeometry(size_t region);
        // 颜色/辐射修改只通知受影响历史；不改变放置、身份、普通轮转或共享预算。
        bool NotifyLocalLighting(size_t region,const std::vector<uint32_t>& indices,float changeAmount,std::string& error);
        // 来源未就绪时仅关闭额外预热，普通轮转仍可利用已经可用的局部数据。
        void SetPrewarmReady(bool ready) { m_PrewarmReady=ready; }
        // 仅在上一批反馈已退役后变更；保留未变探针的轮转进度和历史。
        // recycled 是槽位不变但世界格点已变的探针；它们获得全新身份及完成统计。
        bool UpdatePlacedProbes(size_t region,std::vector<uint32_t> indices,
            const std::vector<uint32_t>& resetGeometry,std::string& error,
            const std::vector<uint32_t>& recycled = {});
        const std::vector<uint32_t>& PlacedProbes(size_t region) const { return m_Regions.at(region).description.probeIndices; }
        const std::vector<GIProbeRegionWork>& NextFrame(double deltaSeconds = 1.0 / 60.0);
        uint32_t RegionCapacity(size_t region) const;
        uint32_t RegionRayCapacity(size_t region) const;
        // 仅诊断按需扫描，统计由完成 fence 后匹配的 GPU 回读确认。
        GIProbeUpdateCoverage RegionCoverage(size_t region) const;
        // 仅在 fence 完成后调用；匹配的在途工作总会退役，false 表示记录拒绝并保留重试请求。
        // 过期或损坏的 CPU issued 身份不会消费当前工作。
        bool RetireFeedback(size_t region, const GIProbeWorkEntry& issued, const GIProbeFeedback& feedback, bool allowIncomplete = false);

    private:
        struct Region
        {
            struct Probe
            {
                uint64_t completedUpdates = 0;
                double lastUpdateSeconds = 0.0;
                // 只追踪显式改灯请求；无关冷探针或几何预热不能延长屏幕响应。
                glm::dvec2 consumedGlobalLighting{0.0};
                glm::dvec2 lastPublicationSeconds{0.0};
                uint64_t lastIssuedFrame = UINT64_MAX;
                uint64_t placedFrame = 0, lastCompletedFrame = 0, incompleteAttempts = 0;
                glm::vec2 localLightingChange{0.0f};
                uint32_t updateSequence = 0;
                float issuedElapsedSeconds = 0.0f;
                uint32_t issuedCycleIndex = 0, issuedFlags = 0;
                uint32_t pendingReset = GIWorkResetGeometry;
                bool awaitingFeedback = false, prewarmQueued = false, priorityQueued = false;

                glm::vec4 LightingState(double globalChange, double timeSeconds) const
                {
                    // 在途工作禁止接收光源通知，因此退役时可由同一状态复算，无需常驻第二份 GPU 工作项。
                    return glm::vec4(
                        float(std::min(1.0,globalChange-consumedGlobalLighting.x+localLightingChange.x)),
                        float(std::min(1.0,globalChange-consumedGlobalLighting.y+localLightingChange.y)),
                        float(std::max(0.0,timeSeconds-lastPublicationSeconds.x)),
                        float(std::max(0.0,timeSeconds-lastPublicationSeconds.y)));
                }
            };
            GIProbeWorkRegion description;
            std::vector<Probe> probes;
            size_t cursor = 0;
            double sweepProgress = 0.0;
            uint32_t nextPlacementSequence = 0;
            std::deque<size_t> priority;
            std::deque<size_t> prewarm;
            bool preferPriority = true;
            uint64_t primaryOnlyPublications = 0;
            // 全局量为光源变化的数值积分，不是资源版本；全局通知只更新每区域常量。
            double globalLightingChange = 0.0;
            uint64_t pendingGlobalLighting = 0, pendingLocalLighting = 0, awaitingFeedbackCount = 0;
            std::vector<size_t> lightingScratch;
            uint64_t heightFailures = 0, pageFailures = 0, stepFailures = 0, coverageFailures = 0;
            uint64_t scatterHeightFailures = 0, scatterPageFailures = 0, scatterStepFailures = 0, scatterCoverageFailures = 0;
        };
        std::vector<Region> m_Regions;
        std::vector<GIProbeRegionWork> m_Frame;
        uint32_t m_MaxUpdates = 0, m_MaxRays = 0;
        size_t m_RegionCursor = 0;
        double m_TimeSeconds = 0.0;
        bool m_BalanceRegionSweeps = false;
        uint64_t m_FrameNumber = 0;
        bool m_PrewarmEnabled = false;
        bool m_PrewarmReady = true;
        double m_PrewarmRayCredit = 0.0, m_PrewarmUpdateCredit = 0.0;
        uint32_t m_MaxPrewarmRayCost = 0;
    };
}
