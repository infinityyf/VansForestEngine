#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace Vans { struct ProfileFrame; }

namespace VansGraphics
{
    enum class GICostStage : uint8_t
    {
        ProbeTrace, ProbeUpdate, ProbeRadiance, ProbeAtlas, ProbeState, Receiver,
        ScreenTrace, ScreenReconstruct, ScreenTemporal, ScreenFilter1, ScreenFilter2,
        AmbientSky, WorldUpload, ProbePrepare, WorldPublish, WorldBuild, WorldCapture,
        WorldRecord, FeedbackWait, Count
    };
    struct GICostStageDescription { const char* scope; const char* label; bool gpu; };
    // 同一个目录定义生产计时名称与诊断阶段；不在 Editor 或预算消费者复制名称匹配规则。
    inline constexpr std::array<GICostStageDescription, size_t(GICostStage::Count)> GICostStages{{
        {"DDGI.RayTrace", "Probe trace / transfers", true},
        {"DDGI.Update", "Probe lighting / integration", true},
        {"DDGI.RadianceShade", "  Hit lighting / scattering", true},
        {"DDGI.AtlasPrefilter", "  Atlas integration", true},
        {"DDGI.ProbeState", "  Probe state", true},
        {"DDGI.ReceiverVisibility", "Receiver visibility", true},
        {"SSGI.ScreenProbeCacheTrace", "Screen gather", true},
        {"SSGI.Reconstruct", "Screen reconstruction", true},
        {"SSGI.Temporal", "Screen temporal", true},
        {"SSGI.Atrous1", "Screen spatial filter 1", true},
        {"SSGI.Atrous2", "Screen spatial filter 2", true},
        {"GI.AmbientSkyCache", "Ambient specular sky cache", true},
        {"GIWorld.Upload", "  World page / terrain upload", true},
        {"GI.PrepareProbes", "Probe scheduling / preparation", false},
        {"GIWorld.Publish", "  World CPU publication", false},
        {"GIWorld.Build", "World background build / retirement", false},
        {"GIWorld.Capture", "World source capture", false},
        {"GIWorld.Record", "World upload recording", false},
        {"Vulkan::WaitFence.GIProbeFeedback", "Probe feedback wait", false}
    }};
    constexpr const char* GIProfileScope(GICostStage stage) { return GICostStages[size_t(stage)].scope; }

    struct GIStageCost
    {
        double milliseconds = 0.0;
        uint32_t samples = 0; // 0 表示该帧没有此阶段样本，不能解释成零成本。
    };
    struct GIFrameCostSnapshot
    {
        uint64_t frameIndex = 0;
        bool hasFrame = false, capturing = false, cpuValid = false, gpuValid = false;
        // 同一线程/队列内取区间并集，再跨线程/队列相加；不是帧关键路径或 GPU busy time。
        double cpuScopedMilliseconds = 0.0, gpuScopedMilliseconds = 0.0;
        std::array<GIStageCost, size_t(GICostStage::Count)> stages{};
    };
    // 仅消费 profiler 已发布的不可变退役帧，不触发 fence 等待或 GPU 读回。
    GIFrameCostSnapshot BuildGIFrameCostSnapshot(const Vans::ProfileFrame& frame, bool capturing);
}
