#pragma once
#include "VansGISettings.h"
#include <array>
#include <cstdint>

namespace VansGraphics
{
    // 世界 owner 在实际录制上传时产生；尚未代表已提交或已完成。
    struct VansGIWorldUploadWork
    {
        uint64_t heightBytes=0, rangeBytes=0, colorBytes=0;
        uint64_t pageTableBytes=0, parameterBytes=0, voxelBytes=0, materialBytes=0;
        uint64_t bufferCapacityBytes=0; // VkBuffer 请求尺寸之和，不是 VMA 分配量或显存驻留。
        uint32_t updateCommands=0, uploadedBricks=0;
        uint32_t mappedBricks=0, readyBricks=0, brickCapacity=0, pendingBuildBricks=0;
        uint64_t UploadBytes() const
        { return heightBytes+rangeBytes+colorBytes+pageTableBytes+parameterBytes+voxelBytes+materialBytes; }
    };

    struct VansGIRegionFrameWork
    {
        uint32_t stableId=0, physicalProbes=0, probeUpdates=0, prewarmUpdates=0;
        uint64_t primaryRays=0, hardwareRays=0, worldRays=0;
        uint32_t rejectedFeedback=0;
    };

    // 固定大小的最后完成批次；帧号沿用 Render/Profiler 身份，不创建第二条计时历史。
    struct VansGIProbeFrameWork
    {
        bool available=false, worldRecorded=false;
        uint64_t renderFrameNumber=0, profilerFrameIndex=UINT64_MAX;
        uint64_t probeUploadBytes=0, feedbackReadbackBytes=0, diagnosticReadbackBytes=0;
        VansGIWorldUploadWork world;
        uint32_t regionCount=0;
        std::array<VansGIRegionFrameWork, kGIMaximumRegionCount> regions{};
    };
    // 命令提交的处理规模；Shader 可提前退出，不代表有效样本、实际查询或写入数。
    struct VansGIReceiverFrameWork
    {
        uint64_t stableAnchors=0, preparedAnchors=0;
        uint64_t maintainedRecords=0, claimedAnchors=0, publishedRecords=0;
        uint64_t biasHardwareInvocations=0, biasWorldInvocations=0;
        uint64_t visibilityHardwareInvocations=0, visibilityWorldInvocations=0;
        uint64_t clearBytes=0, historyCopyBytes=0;
    };
    struct VansSSGIFrameWork
    {
        uint64_t cacheTexels=0, reconstructedPixels=0, temporalPixels=0, filteredPixels=0;
    };
    struct VansAmbientSkyFrameWork
    {
        uint64_t updatedCells=0, clearTestCells=0;
    };
    struct VansGIConsumerFrameWork
    {
        bool available=false;
        uint64_t renderFrameNumber=0, profilerFrameIndex=UINT64_MAX;
        VansGIReceiverFrameWork receiver;
        VansSSGIFrameWork ssgi;
        VansAmbientSkyFrameWork ambientSky;
    };
    // 探针反馈和屏幕阶段沿各自既有 fence 退役，必须保留独立帧身份。
    struct VansGIFrameWork
    {
        VansGIProbeFrameWork probes;
        VansGIConsumerFrameWork consumers;
    };
    static_assert(sizeof(VansGIFrameWork)<=512+sizeof(VansGIRegionFrameWork)*kGIMaximumRegionCount);
}
