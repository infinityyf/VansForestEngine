#include "../EngineCore/RenderCore/BRDFData/VansTextureRadianceChange.h"
#include <chrono>
#include <cstdlib>
#include "../EngineCore/RenderCore/GICore/VansGIProbeWorkScheduler.h"
#include "../EngineCore/RenderCore/GICore/VansGIFrameCost.h"
#include "../EngineCore/RenderCore/GICore/VansGIMemoryPublication.h"
#include "../EngineCore/RenderCore/GICore/VansGIFrameWorkPublication.h"
#include "../EngineCore/Util/VansProfiler.h"
#include <cstdio>
#include "../EngineCore/RenderCore/GICore/VansGILightingChangeTracker.h"
#include "../EngineCore/RenderCore/GICore/VansSSGIHistory.h"
#include "../EngineCore/RenderCore/GICore/VansGIReceiverVisibility.h"
#include "../EngineCore/RenderCore/GICore/VansGIScrollingGrid.h"
#include "../EngineCore/RenderCore/VansRenderSceneSnapshot.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/EditorCore/VansEditorAssetSaveService.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/SceneCore/VansSceneRenderSettingsConfigReader.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <limits>
#include <array>
#include <set>
#include <stdexcept>

namespace
{
    using namespace VansGraphics;
    void Check(bool passed, const char* message) { if (!passed) throw std::runtime_error(message); }

    void MemoryPublicationOwnership()
    {
        VansGIMemoryPublication publication;
        Check(!publication.Read().Available(), "Empty GI memory publication became available");
        publication.PublishConsumer(&VansGIMemorySnapshot::receiver, {10, 16, 0});
        publication.PublishConsumer(&VansGIMemorySnapshot::ssgi, {20, 32, 64});
        publication.PublishConsumer(&VansGIMemorySnapshot::ambientSky, {4, 8, 16});
        Check(publication.Read().ActiveAllocationBytes() == 136, "Consumer memory did not sum actual allocations");
        VansGIProbeMemory probes;
        probes.available = true; probes.regionCount = 1;
        probes.regions[0].stableId = 7; probes.regions[0].storage = {30, 48, 64};
        probes.world = {8, 16, 0};
        publication.PublishProbes(probes, 0);
        auto memory = publication.Read();
        Check(memory.probes.regions[0].stableId == 7 && memory.ActiveAllocationBytes() == 264 &&
            memory.publishedRetainedPeakBytes == 264 && memory.ssgi.imageAllocationBytes == 64,
            "Probe publication replaced consumer data or omitted its peak");
        const auto retiredBytes = memory.probes.ActiveAllocationBytes();
        publication.PublishProbes({}, retiredBytes);
        memory = publication.Read();
        Check(memory.Available() && !memory.probes.available && memory.ActiveAllocationBytes() == 136 &&
            memory.retiredAllocationBytes == 128 && memory.publishedRetainedPeakBytes == 264,
            "Probe retirement counted or cleared independent consumer allocations");
        publication.PublishConsumer(&VansGIMemorySnapshot::ssgi, {20, 32, 128});
        Check(publication.Read().publishedRetainedPeakBytes == 328, "Consumer resize omitted simultaneous retired allocations");
        publication.Retire(128);
        publication.PublishConsumer(&VansGIMemorySnapshot::receiver, {});
        publication.PublishConsumer(&VansGIMemorySnapshot::ssgi, {});
        publication.PublishConsumer(&VansGIMemorySnapshot::ambientSky, {});
        memory = publication.Read();
        Check(!memory.Available() && memory.ActiveAllocationBytes() == 0 && memory.retiredAllocationBytes == 0 &&
            memory.publishedRetainedPeakBytes == 328, "GI cleanup lost peak or retained active storage");
    }
    void WorkPublicationOwnership()
    {
        VansGIFrameWorkPublication publication;
        Check(!publication.Read().probes.available && !publication.Read().consumers.available,
            "Empty work publication fabricated a completed frame");
        VansGIProbeFrameWork probes;
        probes.available = true; probes.renderFrameNumber = 40; probes.profilerFrameIndex = 400;
        probes.regionCount = 1; probes.regions[0].stableId = 7; probes.regions[0].primaryRays = 16;
        publication.PublishProbes(probes);
        VansGIConsumerFrameWork screen;
        screen.renderFrameNumber = 41; screen.profilerFrameIndex = 410;
        screen.receiver.visibilityWorldInvocations = 65536;
        screen.ssgi.cacheTexels = 35; screen.ambientSky.updatedCells = 22;
        publication.PublishConsumers(screen);
        Check(!publication.Read().consumers.available, "Uncompleted screen work became available");
        screen.available = true;
        publication.PublishConsumers(screen);
        auto result = publication.Read();
        Check(result.probes.renderFrameNumber == 40 && result.probes.profilerFrameIndex == 400 &&
            result.probes.regions[0].stableId == 7 && result.consumers.renderFrameNumber == 41 &&
            result.consumers.profilerFrameIndex == 410 && result.consumers.ssgi.cacheTexels == 35,
            "Independent probe/screen completion identity or work was lost");
        publication.PublishProbes({});
        Check(!publication.Read().probes.available && publication.Read().consumers.ambientSky.updatedCells == 22,
            "Probe resource replacement erased completed screen work");
        auto old = screen; old.renderFrameNumber = 39; old.ssgi.cacheTexels = 99;
        publication.PublishConsumers(old);
        Check(publication.Read().consumers.ssgi.cacheTexels == 35,
            "Out-of-order slot retirement replaced newer screen work");
        // 后续成功空帧必须清除上一帧工作量；不是把零工作误判为没有新完成帧。
        screen = {}; screen.available = true; screen.renderFrameNumber = 42;
        publication.PublishConsumers(screen);
        Check(publication.Read().consumers.renderFrameNumber == 42 &&
            publication.Read().consumers.receiver.visibilityWorldInvocations == 0 &&
            publication.Read().consumers.ssgi.cacheTexels == 0,
            "Completed zero-work frame retained prior work");
        std::cout << "[GIFrameWork] PASS: independent identities, uncompleted rejection, probe reset, slot order and completed zero work\n";
    }

    void FrameCostOwnership()
    {
        auto storage = std::make_unique<Vans::ProfileFrame>(); auto& frame = *storage;
        Check(!BuildGIFrameCostSnapshot(frame, false).hasFrame, "Empty profiler frame became a zero-cost GI sample");
        frame.frameIndex = 37; frame.gpuExpected = true; frame.gpuComplete = true;
        const auto noScopes = BuildGIFrameCostSnapshot(frame, true);
        Check(noScopes.hasFrame && !noScopes.cpuValid && !noScopes.gpuValid,
            "A completed frame without GI scopes became a measured zero cost");
        frame.trackCount = 3;
        for (uint32_t index = 0; index < frame.trackCount; ++index)
        {
            frame.tracks[index].trackId = index + 1;
            frame.tracks[index].type = index ? Vans::ProfileTrackType::GpuQueue : Vans::ProfileTrackType::CpuThread;
        }
        const auto add = [&](GICostStage stage, uint32_t track, double begin, double end) -> Vans::ProfileEvent& {
            auto& event = frame.events[frame.eventCount++];
            std::snprintf(event.name, sizeof(event.name), "%s", GIProfileScope(stage));
            event.trackId = track; event.startUs = begin; event.endUs = end;
            event.flags = GICostStages[size_t(stage)].gpu ? Vans::ProfileEventFlagGpu : Vans::ProfileEventFlagNone;
            return event;
        };
        add(GICostStage::ProbeUpdate, 2, 0, 1000);
        add(GICostStage::ProbeRadiance, 2, 0, 600);
        add(GICostStage::ProbeRadiance, 2, 400, 800); // 同阶段重叠也只统计一次。
        add(GICostStage::ProbeAtlas, 2, 800, 1000);
        add(GICostStage::AmbientSky, 3, 100, 600); // 跨队列不能当作关键路径并集。
        add(GICostStage::WorldUpload, 2, 1100, 1100); // 实测零与未采集不同。
        add(GICostStage::ProbePrepare, 1, 0, 200);
        add(GICostStage::WorldPublish, 1, 50, 100);
        auto result = BuildGIFrameCostSnapshot(frame, true);
        const auto close = [](double a, double b) { return std::abs(a-b) < 1e-10; };
        Check(result.hasFrame && result.capturing && result.cpuValid && result.gpuValid && result.frameIndex == 37,
            "GI cost lost resolved frame identity or capture state");
        Check(close(result.gpuScopedMilliseconds, 1.5) && close(result.cpuScopedMilliseconds, .2),
            "GI cost double counted nested/overlapping scopes or collapsed distinct queues");
        Check(close(result.stages[size_t(GICostStage::ProbeRadiance)].milliseconds, .8) &&
            result.stages[size_t(GICostStage::ProbeRadiance)].samples == 2 &&
            result.stages[size_t(GICostStage::WorldUpload)].samples == 1 &&
            result.stages[size_t(GICostStage::ScreenTrace)].samples == 0, "GI stage union or absent sample semantics differ");
        frame.gpuComplete = false; result = BuildGIFrameCostSnapshot(frame, true);
        Check(!result.gpuValid && result.cpuValid && result.gpuScopedMilliseconds == 0 &&
            result.stages[size_t(GICostStage::ProbeUpdate)].samples == 0, "Unretired GPU frame published cost");
        frame.gpuComplete = true; frame.droppedGpuEvents = 1;
        Check(!BuildGIFrameCostSnapshot(frame, true).gpuValid, "Dropped GPU samples published partial cost");
        frame.droppedGpuEvents = 0;
        frame.events[0].endUs = std::numeric_limits<double>::quiet_NaN();
        Check(!BuildGIFrameCostSnapshot(frame, true).gpuValid, "Nonfinite GPU cost accepted");
        frame.events[0].endUs = -1;
        Check(!BuildGIFrameCostSnapshot(frame, true).gpuValid, "Negative GPU duration accepted");
        frame.events[0].endUs = 1000; frame.events[0].trackId = 1;
        Check(!BuildGIFrameCostSnapshot(frame, true).gpuValid, "GPU scope on a CPU track accepted");
        frame.events[0].trackId = 2; frame.events[0].flags = 0;
        Check(!BuildGIFrameCostSnapshot(frame, true).gpuValid, "Missing GPU flag silently removed GI cost");
        frame.events[0].flags = Vans::ProfileEventFlagGpu; frame.overflow = true;
        result = BuildGIFrameCostSnapshot(frame, false);
        Check(!result.cpuValid && !result.gpuValid, "Overflow cost frame accepted");
        frame.overflow = false; result = BuildGIFrameCostSnapshot(frame, false);
        Check(!result.capturing && result.gpuValid && result.frameIndex == 37, "Paused capture disguised last sample as live data");
        // 经真实 profiler 的 CPU/GPU 组帧发布后由 API 读取，不能另建时间轴。
        auto& profiler = Vans::VansProfiler::Get();
        const bool captured = profiler.IsCaptureEnabled(), paused = profiler.IsPaused();
        struct RestoreCapture
        {
            Vans::VansProfiler& profiler; bool captured, paused;
            ~RestoreCapture() { profiler.EndFrame(); profiler.SetPaused(paused); profiler.SetCaptureEnabled(captured); }
        } restore{profiler, captured, paused};
        profiler.SetPaused(false); profiler.SetCaptureEnabled(true); profiler.BeginFrame();
        const auto publishedIndex = profiler.GetActiveFrameIndex();
        profiler.MarkCurrentFrameHasRender(); profiler.EndFrame();
        Vans::VansCpuProfiler::Get().BindCurrentThreadToFrame(publishedIndex);
        Vans::VansCpuProfiler::Get().EndRenderFrame(publishedIndex);
        Vans::VansGpuResolvedFrame resolved;
        resolved.frameIndex = publishedIndex; resolved.eventCount = 6;
        resolved.laneEventCount[0] = 5; resolved.laneEventCount[1] = 1;
        for (uint32_t index = 0; index < resolved.eventCount; ++index)
        {
            resolved.events[index] = frame.events[index];
            resolved.events[index].trackId -= 1;
            resolved.events[index].eventId = index + 1;
            resolved.events[index].category = Vans::ProfileCategory::GPU;
        }
        profiler.SubmitGpuFrame(resolved); profiler.BeginFrame();
        auto scene = std::make_unique<VansScene>();
        Vans::EditorAPI::EngineAPIImpl api(scene.get(), nullptr);
        const auto apiCost = api.GetGIFrameCost();
        Check(apiCost.hasFrame && apiCost.gpuValid && apiCost.frameIndex == publishedIndex &&
            close(apiCost.gpuScopedMilliseconds, 1.5), "GI API did not consume the actual resolved profiler frame");
        std::cout << "[GIFrameCost] PASS: resolved identity, nested/overlap unions, independent queues, absent/zero, incomplete/dropped/invalid rejection, paused capture and API ownership\n";
    }

    void LightingNotificationBenchmark()
    {
        if (!std::getenv("FORESTENGINE_GI_LIGHTING_BENCHMARK")) return;
        for(uint32_t count : {1024u,65536u,262144u})
        {
            VansGIProbeWorkScheduler scheduler; std::string error;
            GIProbeWorkRegion region; region.raysPerProbe=64; region.probeIndices.resize(count);
            for(uint32_t i=0;i<count;++i) region.probeIndices[i]=i;
            Check(scheduler.Configure({std::move(region)},256,16384,error),"Lighting benchmark configuration failed");
            auto complete=[&]()
            {
                for(const auto& entry:scheduler.NextFrame()[0].entries)
                    Check(scheduler.RetireFeedback(0,entry,{entry.probeIndex,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex}),"Lighting benchmark feedback failed");
            };
            for(uint32_t i=0;i<count/256u;++i) complete();
            double notificationSeconds=0.0;
            const auto started=std::chrono::steady_clock::now();
            for(uint32_t frame=0;frame<1024;++frame)
            {
                const auto before=std::chrono::steady_clock::now();
                scheduler.NotifyGlobalLighting(1.0f);
                const auto after=std::chrono::steady_clock::now();
                notificationSeconds+=std::chrono::duration<double>(after-before).count();
                complete();
            }
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            std::cout<<"[GILightingBenchmark] placed="<<count<<" updatesPerFrame=256 frames=1024 notificationMeanMs="
                <<notificationSeconds*1000.0/1024.0<<" totalMeanMs="<<seconds*1000.0/1024.0<<"\n";
        }
    }

    void ScreenLightingHistoryOwnership()
    {
        VansSSGIHistory history;
        Check(!history.HasHistory() && history.WriteIndex()==0, "New SSGI history must be unavailable");
        const auto first=history.PrepareFrame(0,0,{.01f,-.02f});
        Check(first.screenSize==glm::vec4(1) && first.frameParams==glm::vec4(0,.01f,-.02f,0), "SSGI parameter contract is invalid");
        bool rejected=false;
        try { history.PrepareFrame(1,1,{}); } catch(const std::logic_error&) { rejected=true; }
        Check(rejected,"An unresolved SSGI frame was silently replaced");
        history.ResolveRecordedFrame(true);
        history.ResolveRecordedFrame(false);
        Check(history.HasHistory() && history.WriteIndex()==1,"Empty frame-start cancellation invalidated submitted history");
        VansGILightingChanges change; change.local=VansGILightingBounds{{-1,-2,-3},{1,2,3}};
        history.NotifyLighting(change,4); history.SetProbeLightingPending(true);
        auto local=history.PrepareFrame(17,13,{});
        Check(history.WriteIndex()==1 && local.frameParams.x==1 && local.frameParams.w==2 &&
            local.lightingMinimum==glm::vec4(-5,-6,-7,1) && local.lightingMaximum==glm::vec4(5,6,7,0),
            "Local light response discarded the surface history or lost its gather reach");
        history.ResolveRecordedFrame(true);
        change.local=VansGILightingBounds{{20,30,40},{21,31,41}}; history.NotifyLighting(change,0);
        local=history.PrepareFrame(17,13,{});
        Check(local.lightingMinimum==glm::vec4(-5,-6,-7,1) && local.lightingMaximum==glm::vec4(21,31,41,0),
            "Unconsumed old and new light response bounds were not merged");
        history.ResolveRecordedFrame(true);
        change.global=true;
        for(uint32_t i=0;i<100;++i)
        {
            const auto write=history.WriteIndex(); history.NotifyLighting(change,0);
            const auto parameters=history.PrepareFrame(17,13,{});
            Check(parameters.frameParams.x==1 && parameters.frameParams.w==1,"Continuous lighting disabled screen history");
            history.ResolveRecordedFrame(true);
            Check(history.WriteIndex()==(write^1u),"Continuous lighting restarted the physical ping-pong");
        }
        const auto failedWrite=history.WriteIndex(); history.PrepareFrame(17,13,{}); history.ResolveRecordedFrame(false);
        Check(!history.HasHistory() && history.WriteIndex()==failedWrite,"Failed submission advanced or validated SSGI history");
        auto recovery=history.PrepareFrame(17,13,{});
        Check(recovery.frameParams.x==0 && recovery.frameParams.w==1,"Failed submission consumed the light response request");
        history.ResolveRecordedFrame(true);
        history.SetProbeLightingPending(false);
        for(uint32_t i=0;i<3;++i)
        {
            const auto parameters=history.PrepareFrame(17,13,{});
            Check(parameters.frameParams.x==1 && parameters.frameParams.w==(i<2?1.0f:0.0f),
                "Light response did not cover the last published probe and deferred radiance latency");
            history.ResolveRecordedFrame(true);
        }
        const auto beforeCut=history.WriteIndex(); history.Invalidate();
        Check(!history.HasHistory() && history.WriteIndex()==beforeCut,"Camera invalidation changed resource ownership");
        history.PrepareFrame(17,13,{}); history.ResolveRecordedFrame(true);
        const auto omittedWrite=history.WriteIndex(); history.ResolveRecordedFrame(true,true);
        Check(!history.HasHistory() && history.WriteIndex()==omittedWrite,"An omitted SSGI pass paired old surfaces with new scene radiance");
        history.Reset();
        Check(history.WriteIndex()==0 && history.PrepareFrame(17,13,{}).frameParams==glm::vec4(0),"Scene reset retained SSGI state");
        history.ResolveRecordedFrame(false);
        // 突变必须覆盖延迟输入，但不能在低预算 probe 追赶期间永久丢弃降噪。
        VansSSGIHistory pulse; change.global=true; change.changeAmount=1.0f;
        pulse.NotifyLighting(change,0); pulse.SetProbeLightingPending(true);
        for(uint32_t i=0;i<4;++i)
        {
            change.changeAmount=.125f; pulse.NotifyLighting(change,0);
            const auto parameters=pulse.PrepareFrame(17,13,{});
            Check(parameters.lightingMinimum.w==(i<2?1.0f:.125f),"Abrupt response outlived its previous-radiance pulse");
            pulse.ResolveRecordedFrame(true);
        }
        change.changeAmount=1.0f; pulse.NotifyLighting(change,0);
        pulse.PrepareFrame(17,13,{}); pulse.ResolveRecordedFrame(false);
        Check(pulse.PrepareFrame(17,13,{}).lightingMinimum.w==1.0f,"Failed submission consumed an unrendered lighting pulse");
        pulse.ResolveRecordedFrame(true);
    }

    void IndependentLightingPublication()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{64,{0,1,2},4}},3,192,error),"Independent lighting fixture failed");
        auto apply=[&](const GIProbeWorkEntry& entry,uint32_t status)
        { Check(scheduler.RetireFeedback(0,entry,{entry.probeIndex,status,entry.elapsedSeconds,entry.cycleIndex},true),"Independent lighting feedback failed"); };
        for(auto entry:scheduler.NextFrame(.02)[0].entries)apply(entry,GIProbeComplete);
        scheduler.NotifyGlobalLighting(.01f);
        auto issued=scheduler.NextFrame(.02)[0].entries;
        for(const auto& entry:issued)Check(entry.flags==0u && std::abs(entry.primaryLightingChange-.01f)<1e-6f && std::abs(entry.primaryAgeSeconds-.02f)<1e-6f,"Small global change discarded history or publication age");
        apply(issued[0],GIProbePrimaryPublished|GIProbeScatterVoxelPage);
        apply(issued[1],GIProbeComplete);apply(issued[2],GIProbeVoxelPage);
        Check(scheduler.HasPendingLightingUpdates(),"Partial/failed work retired all lighting requests");
        scheduler.NotifyGlobalLighting(.02f);
        Check(scheduler.NotifyLocalLighting(0,{0,0},.03f,error),"Local contribution failed");
        issued=scheduler.NextFrame(.1)[0].entries;
        Check(std::abs(issued[0].primaryLightingChange-.05f)<1e-6f && std::abs(issued[0].scatterLightingChange-.06f)<1e-6f,
            "Component source changes were duplicated or consumed together");
        Check(std::abs(issued[0].primaryAgeSeconds-.1f)<1e-6f && std::abs(issued[0].scatterAgeSeconds-.12f)<1e-6f,
            "Failed scattering used dispatch age instead of successful publication age");
        Check(std::abs(issued[1].primaryLightingChange-.02f)<1e-6f && std::abs(issued[2].primaryLightingChange-.03f)<1e-6f,
            "Successful and failed primary publication consumed the same source history");
        for(auto entry:issued)apply(entry,GIProbeComplete);
        Check(!scheduler.HasPendingLightingUpdates(),"Full publications retained a source change");
        scheduler.NotifyGlobalLighting(.002f);
        Check(scheduler.NotifyLocalLighting(0,{1},1.0f,error),"Simultaneous world color change failed");
        issued=scheduler.NextFrame(.02)[0].entries;
        Check(issued[1].primaryLightingChange==1.f && issued[1].scatterLightingChange==1.f &&
            issued[0].primaryLightingChange<.003f,"Small global change masked a local abrupt change");
        auto corrupt=issued[0];corrupt.scatterAgeSeconds+=.01f;
        Check(!scheduler.RetireFeedback(0,corrupt,{corrupt.probeIndex,GIProbeComplete,corrupt.elapsedSeconds,corrupt.cycleIndex}),
            "Changed work payload consumed a source request");
        for(auto entry:issued)apply(entry,GIProbeComplete);
        scheduler.NotifyGlobalLighting(.001f);
        issued=scheduler.NextFrame(2.0)[0].entries;
        for(auto entry:issued)
        {
            Check(std::abs(entry.primaryLightingChange-.001f)<1e-6f && std::abs(entry.primaryAgeSeconds-2.0f)<1e-6f,
                "Low budget interval lost physical age or magnified source change");
            apply(entry,GIProbeComplete);
        }
        Check(scheduler.NotifyLocalLighting(0,{2},.01f,error),"Local recycle fixture failed");
        Check(scheduler.UpdatePlacedProbes(0,{0,1,2,3},{},error,{2}),"Lighting recycle failed");
        Check(!scheduler.HasPendingLightingUpdates(),"New/recycled slots inherited old position's light response");
        bool invalid=false;try {scheduler.NotifyGlobalLighting(-1);}catch(const std::invalid_argument&){invalid=true;}
        Check(invalid && !scheduler.HasPendingLightingUpdates(),"Invalid source change mutated pending state");
        std::cout<<"[GIProbeWork] independent source changes, accepted publication ages, partial failures and recycled identities PASS\n";
    }

    void PendingLightingPropagation()
    {
        VansGIProbeWorkScheduler scheduler; std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{64,{0,1},4}},2,128,error),"Lighting publication fixture failed");
        auto complete=[&](uint32_t status=GIProbeComplete)
        {
            const auto work=scheduler.NextFrame()[0].entries;
            for(const auto& entry:work)
                Check(scheduler.RetireFeedback(0,entry,{entry.probeIndex,status,entry.elapsedSeconds,entry.cycleIndex},true),"Publication feedback was rejected");
        };
        Check(!scheduler.HasPendingLightingUpdates(),"Unrelated cold probes prolonged an explicit light response");
        complete(); Check(!scheduler.HasPendingLightingUpdates(),"Complete publications retained a pending light request");
        for(int i=0;i<5;++i) Check(scheduler.NotifyLocalLighting(0,{0,0,1},1.0f,error),"Repeated lighting notification failed");
        complete(GIProbePrimaryPublished|GIProbeScatterVoxelPage);
        Check(scheduler.HasPendingLightingUpdates(),"Primary-only publication falsely completed scattering response");
        complete(); Check(!scheduler.HasPendingLightingUpdates(),"Repeated notifications inflated pending publication counts");
        Check(scheduler.NotifyLocalLighting(0,{0},1.0f,error),"Local publication invalidation failed");
        Check(scheduler.UpdatePlacedProbes(0,{1},{},error) && !scheduler.HasPendingLightingUpdates(),"Removed probe retained a pending request");
        Check(scheduler.UpdatePlacedProbes(0,{1,2},{},error) && !scheduler.HasPendingLightingUpdates(),"New unrelated placement prolonged a light response");
        complete(); scheduler.NotifyGlobalLighting(1.0f); scheduler.ResetGeometry(); complete(GIProbeVoxelPage);
        Check(scheduler.HasPendingLightingUpdates(),"Failed query consumed lighting publication");
        complete(); Check(!scheduler.HasPendingLightingUpdates(),"Geometry recovery failed to retire lighting request");
        Check(scheduler.NotifyLocalLighting(0,{1},1.0f,error),"Recycled lighting fixture failed");
        Check(scheduler.UpdatePlacedProbes(0,{1,2},{},error,{1}) && !scheduler.HasPendingLightingUpdates(),"Recycled slot inherited an old position's light request");
        Check(scheduler.UpdatePlacedProbes(0,{}, {},error) && !scheduler.HasPendingLightingUpdates(),"Empty region retained pending publications");
    }

    void ReceiverGeometryOwnership()
    {
        VansGIReceiverVisibility receiver;
        receiver.frame = 100;
        VansGIWorldQueryPublication world{20, 0, {}};
        Check(receiver.PrepareGeometry(10, world, 0x800au).regions.x == ((1u << kGIMaximumRegionCount) - 1u), "first scene geometry affects every receiver region");
        receiver.ResolveRecordedFrame(true);
        Check(receiver.frame == 101, "geometry invalidation must preserve receiver rotation");
        world.generation = 21;
        Check(receiver.PrepareGeometry(11, world, 0x800au).regions.x == ((1u << kGIMaximumRegionCount) - 1u),
            "simultaneous geometry changes must not cancel through XOR (10^20 == 11^21)");
        receiver.ResolveRecordedFrame(true);
        world.sequence = 1;
        world.changes.geometry = GIWorldBounds{{-2,-3,-4},{5,6,7}};
        world.changes.residency = GIWorldBounds{{10,20,30},{40,50,60}};
        const auto change = receiver.PrepareGeometry(11, world, 0x800au);
        Check(change.regions.x == 0 && change.regions.y == 0x800au &&
            change.geometryMin == glm::vec4(-2,-3,-4,1) && change.geometryMax == glm::vec4(5,6,7,0) &&
            change.residencyMin == glm::vec4(10,20,30,1) && change.residencyMax == glm::vec4(40,50,60,0),
            "contiguous world publication must carry both bounds and preserve hardware-only regions");
        Check(receiver.frame == 102, "world publication must not restart receiver ages or fair rotation");
        receiver.ResolveRecordedFrame(false);
        Check(receiver.frame == 0 && receiver.PrepareGeometry(11, world, 0x800au).regions.y == 0x800au,
            "failed frame must discard history and retain unconsumed geometry invalidation");
        receiver.ResolveRecordedFrame(true);
        Check(!receiver.PrepareGeometry(11, world, 0x800au).Any(), "submitted geometry must not invalidate again");
        receiver.ResolveRecordedFrame(true);
        world.sequence = 3;
        Check(receiver.PrepareGeometry(11, world, 0x800au).regions.x == 0x800au,
            "skipped publication must invalidate all world queries instead of using incomplete bounds");
        receiver.ResolveRecordedFrame(true);
        world.generation = 22; world.sequence = 0;
        Check(receiver.PrepareGeometry(11, world, 0x800au).regions.x == 0x800au,
            "replaced world owner must invalidate every world query");
        receiver.ResolveRecordedFrame(true);
        ++world.sequence; world.changes = {};
        Check(!receiver.PrepareGeometry(11, world, 0x800au).Any(), "empty-world publication must retain visibility");
        receiver.ResolveRecordedFrame(true);
        ++world.sequence; world.changes.geometry = GIWorldBounds{{-1,-1,-1},{1,1,1}};
        Check(!receiver.PrepareGeometry(11, world, 0u).Any(), "unused world provider must not invalidate hardware-only regions");
    }

	class CapturingSceneAuthoringHost final : public Vans::IVansSceneAuthoringHost
	{
	public:
		Vans::VansSceneDocument* SceneDocument() const override { return document; }
		Vans::VansSceneAuthoringResult SetSceneValue(
			const Vans::DocumentPropertyPath& path,
			Vans::VansSerializedValue value) override
		{
			captured.push_back({ path.propertyPointer, value });
			if (!edits)
				return { true, {} };
			const Vans::SceneEditResult result = edits->Set(path, std::move(value));
			return { result.success, result.message };
		}
        Vans::VansSceneAuthoringResult CommitSceneValues(std::vector<Vans::VansSceneAuthoringValue> values,
            std::uint64_t expectedState,const Vans::VansSceneAuthoringCommit& commit) override
        {
            if(!edits) return {false,"Capture-only host cannot commit a Scene transaction"};
            const auto result=edits->CommitValues(std::move(values),expectedState,commit);
            return {result.success,result.message};
        }
		std::vector<Vans::EditorAPI::ScenePropertyEdit> Take()
		{
			auto result = std::move(captured);
			captured.clear();
			return result;
		}

		Vans::VansSceneDocument* document = nullptr;
		Vans::VansSceneEditService* edits = nullptr;

	private:
		std::vector<Vans::EditorAPI::ScenePropertyEdit> captured;
	};


    void TextureLightingContentOwnership()
    {
        VansRenderLightFrameData frame;
        VansRectLight rect{}; rect.m_Position=glm::vec3(20,0,0); rect.m_Color=glm::vec3(1);
        rect.m_Intensity=1; rect.m_Range=3; rect.m_TextureSlot=7;
        frame.rectLights.push_back(rect); rect.m_Position.x=60; frame.rectLights.push_back(rect);
        VansPointLight point{}; point.m_Position=glm::vec3(-40,0,0); point.m_Color=glm::vec3(1);
        point.m_Intensity=1; point.m_Radius=2; frame.pointLights.push_back(point);
        VansDirectionalLight sun{}; sun.m_Direction=glm::vec3(0,1,0); sun.m_Color=glm::vec3(1);
        sun.m_Intensity=1; frame.directionalLights.push_back(sun);
        VansGILightingChangeTracker tracker; tracker.Update(frame);
        VansGILightingContentChanges content; content.AddRectLayer(7,.02f);
        auto change=tracker.Update(frame,content);
        Check(!change.global && change.local && change.local->IntersectsProbe(glm::vec3(20,0,0),0) &&
            change.local->IntersectsProbe(glm::vec3(60,0,0),0) && !change.local->IntersectsProbe(glm::vec3(-40,0,0),0) &&
            std::abs(change.changeAmount-.02f)<1e-6f,"Shared video layer omitted a bound light or invalidated unrelated light bounds");
        Check(!tracker.Update(frame).Any(),"Texture content change was replayed without a new publication");
        content={}; content.AddRectLayer(0,1);
        Check(!tracker.Update(frame,content).Any(),"Rect light index was confused with the texture layer");
        const auto pointSlot=VansLightCookieOffset(1);
        content={}; content.AddCookie(pointSlot,.01f);
        Check(!tracker.Update(frame,content).Any(),"Disabled cookie content invalidated GI");
        frame.cookies.data[pointSlot].options.x=1; tracker.Update(frame);
        change=tracker.Update(frame,content);
        Check(!change.global && change.local && change.local->IntersectsProbe(glm::vec3(-40,0,0),0) &&
            std::abs(change.changeAmount-.01f)<1e-6f,"Same-GUID cookie resource publication was ignored");
        frame.cookies.data[0].options.x=1; tracker.Update(frame);
        content={}; content.AddCookie(0,.01f); change=tracker.Update(frame,content);
        Check(change.global && !change.local && std::abs(change.changeAmount-.01f)<1e-6f,"Directional texture content omitted global response");
        content={}; content.AddSurface(.002f); content.AddRectLayer(7,1); change=tracker.Update(frame,content);
        Check(change.global && change.local && change.changeAmount==1,"Small surface change masked an abrupt local texture change");
        content={}; content.AddSurface(.01f); content.AddSurface(.02f);
        Check(std::abs(tracker.Update(frame,content).changeAmount-.03f)<1e-6f,"Pending source publications did not accumulate");
        content.AddSurface(1); Check(content.surfaces==1,"Texture event accumulator exceeded its bounded range");
        auto invalid=frame; invalid.pointLights[0].m_Intensity=2;
        content.rectLayers[2]=std::numeric_limits<float>::quiet_NaN();
        bool rejected=false;try {tracker.Update(invalid,content);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected && !tracker.Update(frame).Any(),"Invalid texture content mutated committed lighting state");
        content={}; rejected=false;try {content.AddCookie(pointSlot,-.1f);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected && content.cookies[pointSlot]==0,"Invalid resource change mutated pending contents");
    }

    void TextureRadianceMeasurement()
    {
        using Vans::VansColorEncoding;
        Check(std::abs(Vans::DecodeColor8(128,VansColorEncoding::SRGB)-.2158605f)<.000001f,"sRGB midtone is not linearized");
        Check(std::abs(Vans::DecodeColor8(128,VansColorEncoding::Linear)-.5019608f)<.000001f,"Linear data was gamma corrected");
        Check(Vans::EncodeColor8(.5f,VansColorEncoding::SRGB)==188,"Half energy has the wrong sRGB encoding");
        for(auto encoding:{VansColorEncoding::Linear,VansColorEncoding::SRGB})
        {
            Check(Vans::EncodeColor8(-1,encoding)==0&&Vans::EncodeColor8(2,encoding)==255,"Color encoding did not clamp endpoints");
            for(int i=0;i<256;++i)Check(Vans::EncodeColor8(Vans::DecodeColor8(uint8_t(i),encoding),encoding)==i,"RGBA8 transfer function roundtrip lost a code value");
        }
        std::vector<uint8_t> first(16*16*4,128),second=first;
        Check(MeasureTextureRadianceChange(first,second,16,16,true)==0,"Repeated decoded frame changed radiance");
        Check(MeasureTextureRadianceChange({},second,16,16,true)==1,"First frame failed to establish content");
        for(size_t i=0;i<second.size();i+=4)second[i]=129;
        for(bool srgb:{false,true})
        {
            const float delta=MeasureTextureRadianceChange(first,second,16,16,srgb);
            Check(delta>0 && delta<.05f,"Small decoded-frame variation discarded accumulation");
        }
        second=first; second[0]=255;
        Check(MeasureTextureRadianceChange(first,second,16,16,true)>0,"Unsampled local change was lost");
        first.assign(first.size(),0);second.assign(second.size(),255);
        Check(MeasureTextureRadianceChange(first,second,16,16,true)==1,"Abrupt decoded-frame change retained stale light");
        bool rejected=false;try {MeasureTextureRadianceChange(first,second,16,15,true);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"Malformed decoded pixels were accepted");
        if(std::getenv("FOREST_GI_TEXTURE_BENCHMARK"))
        {
            for(const auto size:{std::pair<int,int>{1920,1080},{3840,2160}})
            {
                first.assign(size.first*size.second*4,128);second=first;float sum=0;
                const auto start=std::chrono::steady_clock::now();
                for(int i=0;i<128;++i)sum+=MeasureTextureRadianceChange(first,second,size.first,size.second,true);
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/128;
                Check(sum==0,"Static benchmark changed lighting");
                std::cout<<"[GITextureBenchmark] width="<<size.first<<" height="<<size.second<<" identicalMeanMs="<<ms<<" frames=128\n";
            }
        }
    }

    void LightingResponseMagnitude()
    {
        VansRenderLightFrameData frame;
        VansPointLight point{}; point.m_Color=glm::vec3(1); point.m_Intensity=1; point.m_Radius=20;
        frame.pointLights.push_back(point);
        VansGILightingChangeTracker tracker; tracker.Update(frame);
        auto check=[&](float expected,const char* message)
        { const auto changed=tracker.Update(frame); Check(changed.Any() && (expected==.125f ? changed.changeAmount>0.0f && changed.changeAmount<.125f : std::abs(changed.changeAmount-expected)<1e-5f),message); };
        frame.pointLights[0].m_Intensity=1.01f; check(.125f,"Small intensity change discarded accumulation");
        frame.pointLights[0].m_Position.x=.02f; check(.125f,"Small light movement discarded accumulation");
        const auto slot=VansLightCookieOffset(1);
        frame.cookies.data[slot].options.x=1; check(1,"Cookie enable did not respond immediately");
        frame.cookies.data[slot].scaleOffset.z=.001f; check(.125f,"Continuous cookie scroll was treated as a discrete edit");
        frame.cookies.data[slot].worldToLight[3].x=.01f; check(.125f,"Continuous cookie projection was treated as a discrete edit");
        frame.cookies.textures[slot]="new-cookie"; check(1,"Cookie replacement did not respond immediately");
        frame.pointLights[0].m_IESProfileIndex=3; check(1,"IES replacement did not respond immediately");
        frame.pointLights[0].m_Intensity=0; check(1,"Light-off did not respond immediately");
        frame.pointLights[0].m_Intensity=1; check(1,"Light-on did not respond immediately");
        frame.pointLights[0].m_Color=glm::vec3(0,0,1); check(1,"Large chromatic change was masked by luminance");
        frame.pointLights[0].m_Position.x=20; check(1,"Light teleport retained old response");
        VansDirectionalLight sun{}; sun.m_Color=glm::vec3(1); sun.m_Intensity=1; sun.m_Direction=glm::vec3(0,1,0);
        frame.directionalLights.push_back(sun); check(1,"Directional addition was treated as smooth motion");
        frame.directionalLights[0].m_Direction=glm::normalize(glm::vec3(.001f,1,0)); check(.125f,"Slow celestial motion discarded accumulation");
        frame.skyLighting.radianceScale=1; tracker.Update(frame);
        frame.skyLighting.radianceScale=1.001f; check(.125f,"Slow sky fade discarded accumulation");
        frame.skyLighting.radianceScale=0; check(1,"Sky-off retained old response");
        Check(!tracker.Update(frame).Any(),"Consumed response kept invalidating lighting");
    }

    void LightingChangeOwnership()
    {
        VansRenderLightFrameData frame;
        VansPointLight point{};
        point.m_Position = glm::vec3(0.0f);
        point.m_Color = glm::vec3(1.0f);
        point.m_Intensity = 1.0f;
        point.m_Radius = 2.0f;
        frame.pointLights.push_back(point);
        VansGILightingChangeTracker tracker;
        Check(!tracker.Update(frame).Any() && !tracker.Update(frame).Any(), "Initial or unchanged GI lighting invalidated history");
        frame.frameSequence = 1234.0f;
        frame.punctualShadowMapWidth = 2048;
        frame.pointLights[0].m_ShadowMetaIndex = 19;
        const size_t pointCookie = VansLightCookieOffset(1);
        frame.cookies.data[pointCookie].worldToLight[3].x = 12.0f;
        frame.cookies.textures[pointCookie] = "disabled-cookie";
        Check(!tracker.Update(frame).Any(), "Frame, shadow residency or disabled cookie dirtied GI");

        frame.pointLights[0].m_Position.x = 20.0f;
        auto moved = tracker.Update(frame);
        Check(!moved.global && moved.local && moved.local->IntersectsProbe(glm::vec3(-3,0,0), 1.0f) &&
            moved.local->IntersectsProbe(glm::vec3(23,0,0), 1.0f) &&
            !moved.local->IntersectsProbe(glm::vec3(30,0,0), 1.0f), "Moved light omitted old/new influence or dirtied a distant probe");
        Check(!tracker.Update(frame).Any(), "Consumed local change remained pending in tracker");
        frame.pointLights[0].m_IESProfileIndex = 4.0f;
        Check(tracker.Update(frame).local.has_value(), "Point IES change was ignored");
        frame.cookies.data[pointCookie].options.x = 1.0f;
        Check(tracker.Update(frame).local.has_value(), "Cookie enable was ignored");
        frame.cookies.textures[pointCookie] = "replacement-cookie";
        Check(tracker.Update(frame).local.has_value(), "Cookie texture change was ignored");
        frame.cookies.data[pointCookie].worldToLight[3].y = 2.0f;
        Check(tracker.Update(frame).local.has_value(), "Cookie projection change was ignored");
        frame.pointLights[0].m_Intensity = 0.0f;
        Check(tracker.Update(frame).local.has_value(), "Turning a light off retained stale radiance");
        frame.pointLights.clear();
        const auto removed = tracker.Update(frame);
        Check(removed.local && removed.local->IntersectsProbe(glm::vec3(20,0,0), 0.0f), "Removed light lost its previous influence");
        frame.pointLights.push_back(point);
        Check(tracker.Update(frame).local.has_value(), "Added light was ignored");

        VansSpotLight spot{};
        spot.m_Position = glm::vec3(50,0,0); spot.m_Direction = glm::vec3(0,-1,0);
        spot.m_Color = glm::vec3(1.0f); spot.m_Intensity = 1.0f; spot.m_Radius = 5.0f;
        frame.spotLights.push_back(spot);
        Check(tracker.Update(frame).local.has_value(), "Added spot light was ignored");
        frame.spotLights[0].m_OuterCutOff = 0.9f;
        Check(tracker.Update(frame).local.has_value(), "Spot cone change was ignored");
        frame.spotLights[0].m_IESIntensityScale = 2.0f;
        Check(tracker.Update(frame).local.has_value(), "Spot IES scale was ignored");
        VansRectLight rect{};
        rect.m_Position = glm::vec3(100,0,0); rect.m_Range = 10.0f;
        rect.m_HalfWidth = 3.0f; rect.m_HalfHeight = 4.0f; rect.m_Intensity = 1.0f;
        rect.m_Color = glm::vec3(1.0f);
        frame.rectLights.push_back(rect);
        const auto addedRect = tracker.Update(frame);
        Check(addedRect.local && addedRect.local->IntersectsProbe(glm::vec3(115,0,0), 0.0f), "Rect influence omitted emitter extent");
        frame.rectLights[0].m_TextureSlot = 3.0f;
        Check(tracker.Update(frame).local.has_value(), "Rect emitter texture change was ignored");
        frame.rectLights[0].m_TexLodBias = 1.0f;
        Check(tracker.Update(frame).local.has_value(), "Rect texture LOD change was ignored");
        frame.rectLights[0].m_TwoSided = 1.0f;
        Check(tracker.Update(frame).local.has_value(), "Rect sidedness change was ignored");
        frame.rectLights[0].m_AttenuationExp = 2.0f;
        Check(tracker.Update(frame).local.has_value(), "Rect attenuation change was ignored");

        VansDirectionalLight directional{};
        directional.m_Direction = glm::vec3(0,1,0); directional.m_Color = glm::vec3(1.0f); directional.m_Intensity = 1.0f;
        frame.directionalLights.push_back(directional);
        Check(tracker.Update(frame).global, "Added directional light did not invalidate global radiance");
        frame.directionalLights[0].m_ShadowMatrix[0] = glm::mat4(2.0f);
        frame.directionalLights[0].m_CascadeSplits = glm::vec4(3.0f);
        Check(!tracker.Update(frame).Any(), "Camera-dependent directional shadows invalidated GI");
        frame.directionalLights[0].m_Direction = glm::vec3(1,0,0);
        Check(tracker.Update(frame).global, "Directional movement was ignored");
        frame.skyLighting.radianceScale = 2.0f;
        Check(tracker.Update(frame).global, "Sky radiance change was ignored");
        frame.cookies.data[0].options.x = 1.0f;
        Check(tracker.Update(frame).global, "Directional cookie change was ignored");
        frame.directionalLights.clear();
        Check(tracker.Update(frame).global, "Removed directional light retained global radiance");
        auto invalid = frame;
        invalid.pointLights.resize(VANS_MAX_POINT_LIGHTS + 1);
        bool rejected = false;
        try { tracker.Update(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected && !tracker.Update(frame).Any(), "Invalid light frame changed committed tracker state");

        // 单探针预算、连续局部变化必须给远处保留普通轮转，且不重置其几何/辐射。
        VansGIProbeWorkScheduler scheduler;
        std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{64,{0,1,2,3}}},1,64,error), "Light response budget fixture failed");
        for (int i = 0; i < 4; ++i)
        {
            const auto work = scheduler.NextFrame()[0].entries[0];
            Check(scheduler.RetireFeedback(0,work,{work.probeIndex,GIProbeComplete,work.elapsedSeconds,work.cycleIndex}), "Light response fixture feedback failed");
        }
        frame = {}; frame.pointLights.push_back(point); tracker = {};
        tracker.Update(frame);
        std::array<uint32_t,4> visits{};
        for (int i = 0; i < 32; ++i)
        {
            frame.pointLights[0].m_Intensity = float(i + 2);
            const auto changed = tracker.Update(frame);
            std::vector<uint32_t> dirty;
            for (uint32_t probe = 0; probe < 4; ++probe)
                if (changed.local->IntersectsProbe(glm::vec3(float(probe) * 100.0f,0,0), 5.0f)) dirty.push_back(probe);
            Check(scheduler.NotifyLocalLighting(0,dirty,1.0f,error), "Continuous lighting invalidation failed");
            const auto batch = scheduler.NextFrame();
            Check(batch[0].entries.size()==1 && batch[0].RayCount()==64, "Local response exceeded the shared budget");
            const auto work = batch[0].entries[0];
            Check(work.flags==0u && work.primaryLightingChange==(work.probeIndex==0 ? 1.0f : 0.0f), "Local response reset unrelated or geometry history");
            ++visits[work.probeIndex];
            Check(scheduler.RetireFeedback(0,work,{work.probeIndex,GIProbeComplete,work.elapsedSeconds,work.cycleIndex}), "Continuous lighting feedback failed");
        }
        Check(visits[1]>=4 && visits[2]>=4 && visits[3]>=4, "Continuous lighting changes starved ordinary probe updates");
        std::cout << "[GIProbeWork] light-owned semantic changes, old/new bounds, cookies/IES and bounded fair local response PASS\n";
    }

    void LocalLightingEdits()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{64,{0,2,4,6}},GIProbeWorkRegion{64,{1,3,5,7}}},8,512,error),"Lighting fixture configuration failed");
        auto first=scheduler.NextFrame();
        Check(!scheduler.NotifyLocalLighting(0,{2},1.0f,error),"Lighting edit accepted pending feedback");
        for(size_t r=0;r<first.size();++r)for(auto entry:first[r].entries)
            Check(scheduler.RetireFeedback(r,entry,{entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),"Lighting fixture feedback failed");
        const auto coverage=scheduler.RegionCoverage(0);
        Check(!scheduler.NotifyLocalLighting(0,{0,9},1.0f,error) && !scheduler.NotifyLocalLighting(2,{0},1.0f,error),"Invalid lighting address accepted");
        for(int i=0;i<40;++i)Check(scheduler.NotifyLocalLighting(0,{2,6,2},1.0f,error),"Repeated local edit rejected");
        Check(scheduler.PlacedProbes(0)==std::vector<uint32_t>({0,2,4,6}) && scheduler.RegionCoverage(0).minCompletedUpdates==coverage.minCompletedUpdates,
            "Color edit changed layout or completed history");
        for(int frame=0;frame<3;++frame)
        {
            const auto batch=scheduler.NextFrame();
            for(size_t r=0;r<batch.size();++r)
            {
                std::set<uint32_t> unique;
                for(auto entry:batch[r].entries)
                {
                    Check(unique.insert(entry.probeIndex).second && entry.cycleIndex==uint32_t(frame+1),"Color priority duplicated or reset probe sequence");
                    const bool dirty=frame==0 && r==0 && (entry.probeIndex==2 || entry.probeIndex==6);
                    Check(entry.flags==0u && entry.primaryLightingChange==(dirty?1.0f:0.0f),"Color edit lost targeted reset, retained duplicate request or reset unrelated region");
                    Check(scheduler.RetireFeedback(r,entry,{entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),"Local edit feedback failed");
                }
                Check(unique.size()==4,"Color edit changed full frame capacity");
            }
        }
        scheduler.ResetGeometry();
        scheduler.NotifyGlobalLighting(1.0f);
        const auto geometry=scheduler.NextFrame();
        for(size_t r=0;r<geometry.size();++r)for(const auto& entry:geometry[r].entries)
        {
            Check(entry.flags==GIWorkResetGeometry,"Lighting request downgraded pending geometry invalidation");
            Check(scheduler.RetireFeedback(r,entry,{entry.probeIndex,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex}),"Geometry reset feedback failed");
        }
        scheduler.NotifyGlobalLighting(1.0f);
        const auto lighting=scheduler.NextFrame();
        for(const auto& region:lighting)for(const auto& entry:region.entries)
            Check(entry.flags==0u && entry.primaryLightingChange==1.0f,"Global light change reset geometry history");
        std::cout<<"[GIProbeWork] targeted lighting invalidation, duplicates, atomic rejection and indoor isolation PASS\n";
    }

    void CompleteBudgetWork()
    {
        std::vector<GIProbeWorkRegion> regions(kGIMaximumRegionCount);
        for (size_t r=0;r<regions.size();++r) regions[r].raysPerProbe = r%3==0 ? 256u : r%3==1 ? 1024u : 33u;
        for (auto& region : regions) for (uint32_t i = 0; i < 97; ++i) region.probeIndices.push_back(i * 3u);
        VansGIProbeWorkScheduler scheduler; std::string error;
        Check(scheduler.Configure(regions, 9, 1536, error), "mixed complete probe configuration failed");
        std::array<std::map<uint32_t,uint32_t>,kGIMaximumRegionCount> visits;
        for (uint32_t frame = 0; frame < 1200; ++frame)
        {
            uint64_t rays = 0, updates = 0;
            const auto& batch = scheduler.NextFrame(.01);
            for (size_t r = 0; r < batch.size(); ++r)
            {
                rays += batch[r].RayCount(); updates += batch[r].entries.size();
                Check(batch[r].raysPerProbeUpdate == regions[r].raysPerProbe, "probe update omitted directions");
                Check(batch[r].RayCount() <= scheduler.RegionRayCapacity(r) &&
                    batch[r].entries.size() <= scheduler.RegionCapacity(r), "allocation capacity exceeded");
                std::set<uint32_t> ids;
                for (const auto& entry : batch[r].entries)
                {
                    Check(ids.insert(entry.probeIndex).second, "duplicate update in one frame");
                    auto& ordinal = visits[r][entry.probeIndex];
                    Check(entry.cycleIndex == ordinal && entry.flags == (ordinal == 0u ? GIWorkResetGeometry : 0u),
                        "budget delay changed full-sphere rotation sequence");
                    Check(entry.elapsedSeconds > 0.0f && std::isfinite(entry.elapsedSeconds), "invalid temporal duration");
                    ++ordinal;
                    Check(scheduler.RetireFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),
                        "complete lighting feedback rejected");
                    Check(!scheduler.RetireFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),
                        "duplicate feedback accepted");
                }
            }
            Check(rays <= 1536 && updates <= 9, "shared frame budget exceeded");
        }
        for (const auto& region : visits) Check(region.size() == 97, "region or offscreen probe starved");
        Check(!scheduler.Configure({GIProbeWorkRegion{256,{4,4}}}, 9,512,error), "duplicate addresses accepted");
        Check(!scheduler.Configure({GIProbeWorkRegion{256,{4}}}, 9,255,error), "partial probe budget accepted");
        Check(scheduler.Configure({GIProbeWorkRegion{256,{4}}}, 9,256,error), "exact probe budget rejected");
        auto old = scheduler.NextFrame()[0].entries[0];
        bool rejected=false;try { scheduler.NotifyGlobalLighting(1.0f); } catch(const std::logic_error&) { rejected=true; }
        Check(rejected,"Global lighting notification consumed in-flight work");
        scheduler.ResetGeometry(); scheduler.NotifyGlobalLighting(1.0f); auto fresh = scheduler.NextFrame()[0].entries[0];
        Check(fresh.flags == GIWorkResetGeometry && fresh.cycleIndex != old.cycleIndex, "reset reused stale sequence");
        Check(!scheduler.RetireFeedback(0, old, {4,1,old.elapsedSeconds,old.cycleIndex}), "pre-reset feedback accepted");
    }

    void EditedPlacement()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{32,{1,4},4}},4,128,error),"Dynamic placement configuration failed");
        const auto issued=scheduler.NextFrame()[0].entries;
        Check(!scheduler.UpdatePlacedProbes(0,{1,2,4},{},error),"Placement changed before feedback retired");
        for(auto e:issued)Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Placement fixture feedback failed");
        Check(scheduler.UpdatePlacedProbes(0,{1,2,4},{4},error),"Localized placement update failed");
        auto work=scheduler.NextFrame()[0];Check(work.RayCount()==96 && scheduler.RegionCapacity(0)==4,"Dynamic placement exceeded or shrank its capacity");
        for(auto e:work.entries)
        {
            Check(e.flags==(e.probeIndex==1?0u:GIWorkResetGeometry),"Placement lost unrelated history or failed to reset changed lighting");
            Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Edited placement feedback failed");
        }
        Check(scheduler.UpdatePlacedProbes(0,{}, {},error),"Removing all placed probes failed");
        Check(scheduler.NextFrame()[0].entries.empty() && scheduler.RegionCapacity(0)==4,"Empty active list lost reserved capacity");
        Check(scheduler.UpdatePlacedProbes(0,{1,2,3,4},{},error),"Digging terrain could not reactivate probes");
        for(auto e:scheduler.NextFrame()[0].entries)
        {
            Check(e.flags==GIWorkResetGeometry && e.cycleIndex>work.entries.back().cycleIndex,"Reactivated address reused stale identity/history");
            Check(!scheduler.RetireFeedback(0,issued[0],{issued[0].probeIndex,1,issued[0].elapsedSeconds,issued[0].cycleIndex}),"Retired terrain placement feedback accepted");
            Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Reactivated feedback failed");
        }
        Check(!scheduler.UpdatePlacedProbes(0,{0,1,2,3,4},{},error),"Placement overflow accepted");
        Check(scheduler.PlacedProbes(0)==std::vector<uint32_t>({1,2,3,4}),"Rejected placement mutated work addresses");
        GIProbeWorkRegion large;large.raysPerProbe=32;large.maxPlacedProbes=101;
        for(uint32_t i=0;i<100;++i)large.probeIndices.push_back(i);
        Check(scheduler.Configure({large},100,3200,error),"Priority fixture configure failed");
        for(auto e:scheduler.NextFrame()[0].entries)Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Priority initial completion failed");
        large.probeIndices.push_back(100);
        Check(scheduler.UpdatePlacedProbes(0,large.probeIndices,{99},error),"Priority edit failed");
        const auto batch=scheduler.NextFrame()[0];std::set<uint32_t> unique;
        for(auto e:batch.entries)unique.insert(e.probeIndex);
        Check(unique.size()==batch.entries.size() && unique.count(99) && unique.count(100),"Dirty priority duplicated work or starved new probes");
    }

    void ScrollingPlacement()
    {
        VansGIScrollingGrid grid;std::string error;
        Check(grid.Initialize({9,10,11},1,{0,0,0},error),error.c_str());
        GIProbeWorkRegion region;region.raysPerProbe=32;
        for(uint32_t id=0;id<grid.ProbeCount();++id)region.probeIndices.push_back(id);
        VansGIProbeWorkScheduler scheduler;
        Check(scheduler.Configure({region},16,512,error),error.c_str());
        std::vector<GIProbeWorkEntry> old(grid.ProbeCount());
        for(uint32_t frame=0;frame<62;++frame)
            for(const auto e:scheduler.NextFrame()[0].entries)
            { old[e.probeIndex]=e;Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Scrolling initial feedback failed"); }
        const uint32_t capacity=scheduler.RegionCapacity(0);
        std::vector<uint32_t> entering;
        Check(grid.Move({1.01f,0,0},entering,error),error.c_str());
        Check(scheduler.UpdatePlacedProbes(0,region.probeIndices,{},error,entering),error.c_str());
        Check(scheduler.RegionCoverage(0).updated==grid.ProbeCount()-entering.size() && scheduler.RegionCapacity(0)==capacity,
            "Recycled probes retained old coverage or resized frame resources");
        std::set<uint32_t> pending(entering.begin(),entering.end());
        for(uint32_t frame=0;frame<80;++frame)
        {
            const auto batch=scheduler.NextFrame()[0];
            Check(batch.entries.size()<=16 && batch.RayCount()<=512,"Scrolling escaped shared ray/update budget");
            for(const auto e:batch.entries)
            {
                const bool fresh=pending.erase(e.probeIndex)!=0;
                Check(e.flags==(fresh?GIWorkResetGeometry:0u),"Scroll reset unchanged probe or failed to reset new world position");
                const auto stale=old[e.probeIndex];
                Check(!scheduler.RetireFeedback(0,stale,{stale.probeIndex,1,stale.elapsedSeconds,stale.cycleIndex}),"Scroll accepted feedback from old world identity");
                Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Scrolling feedback failed");
            }
        }
        Check(pending.empty() && scheduler.RegionCoverage(0).updated==grid.ProbeCount(),"Scrolling new planes starved under fixed budget");
        std::cout << "[GIProbeWork] scrolling: recycled=" << entering.size() << " retained=" << grid.ProbeCount()-entering.size()
            << " raysPerFrame<=512 staleFeedbackRejected=1 PASS\n";
    }

    void BalancedWorldSweeps()
    {
        std::vector<GIProbeWorkRegion> regions(2);
        for(uint32_t i=0;i<65856;++i)regions[0].probeIndices.push_back(i);
        for(uint32_t i=0;i<4800;++i)regions[1].probeIndices.push_back(i);
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure(regions,4096,65536,error,true),"Balanced world scheduler configuration failed");
        for(uint32_t frame=0;frame<277;++frame)
        {
            uint64_t rays=0;
            const auto& batch=scheduler.NextFrame();
            for(size_t r=0;r<batch.size();++r)
            {
                rays+=batch[r].RayCount();
                for(const auto& e:batch[r].entries)
                    Check(scheduler.RetireFeedback(r,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Balanced feedback failed");
            }
            Check(rays<=65536,"Balanced world work exceeded shared budget");
            Check(batch[0].entries.size()>=237&&batch[1].entries.size()<=19,"Small outdoor region stole the indoor update budget");
        }
        Check(scheduler.RegionCoverage(0).minCompletedUpdates>=1&&scheduler.RegionCoverage(1).minCompletedUpdates>=1,
            "Balanced regions did not complete the same sweep");
    }

    void WorldPrewarmBudget()
    {
        const auto run=[](bool warm)
        {
            std::vector<GIProbeWorkRegion> regions(4);
            const uint32_t counts[]={65856,6912,6912,6912},rays[]={256,256,128,64};
            for(size_t r=0;r<4;++r)
            {
                regions[r].raysPerProbe=rays[r];
                if(warm && r)regions[r].prewarmSpacing=float(1u<<(2u*uint32_t(r)));
                for(uint32_t id=0;id<counts[r];++id)regions[r].probeIndices.push_back(id);
            }
            VansGIProbeWorkScheduler scheduler;std::string error;
            Check(scheduler.Configure(regions,4096,65536,error,true),"World prewarm configure failed");
            const auto step=[&](std::set<uint32_t>* pending)
            {
                const auto& batch=scheduler.NextFrame();uint64_t rayCount=0,updates=0,warmRays=0;
                for(size_t r=0;r<batch.size();++r)
                {
                    rayCount+=batch[r].RayCount();updates+=batch[r].entries.size();std::set<uint32_t> unique;
                    warmRays+=uint64_t(batch[r].prewarmUpdates)*batch[r].raysPerProbeUpdate;
                    for(const auto e:batch[r].entries)
                    {
                        Check(unique.insert(e.probeIndex).second,"Prewarm duplicated a regular update in one frame");
                        if(r==1 && pending)pending->erase(e.probeIndex);
                        Check(scheduler.RetireFeedback(r,e,{e.probeIndex,GIProbeComplete,e.elapsedSeconds,e.cycleIndex}),"Prewarm feedback failed");
                    }
                }
                Check(rayCount<=65536 && updates<=4096,"Prewarm exceeded shared work/ray budget");
                Check(warmRays<=8192 && (warm || warmRays==0),"Prewarm exceeded its reserved fraction or ran while disabled");
            };
            for(uint32_t frame=0;frame<1000;++frame)step(nullptr);
            std::vector<uint32_t> entering;for(uint32_t id=counts[1]-288;id<counts[1];++id)entering.push_back(id);
            Check(scheduler.UpdatePlacedProbes(1,regions[1].probeIndices,{},error,entering),"Prewarm plane recycle failed");
            std::set<uint32_t> pending(entering.begin(),entering.end());uint32_t latency=0;
            while(!pending.empty() && latency<128){step(&pending);++latency;}
            Check(pending.empty(),"New world plane did not publish within the bounded fixture interval");
            if(warm)
            {
                const auto indoorBefore=scheduler.RegionCoverage(0).minCompletedUpdates;
                const auto farBefore=scheduler.RegionCoverage(3).minCompletedUpdates;
                for(uint32_t frame=0;frame<700;++frame)
                {
                    if(frame%4==0)
                    {
                        entering.clear();const uint32_t start=512+(frame/4*128)%(counts[1]-1024);
                        for(uint32_t id=start;id<start+128;++id)entering.push_back(id);
                        Check(scheduler.UpdatePlacedProbes(1,regions[1].probeIndices,{},error,entering),"Continuous prewarm recycle failed");
                    }
                    step(nullptr);
                }
                Check(scheduler.RegionCoverage(0).minCompletedUpdates>indoorBefore && scheduler.RegionCoverage(3).minCompletedUpdates>farBefore,
                    "Continuous near prewarm starved indoor or far regular sweeps");
                Check(scheduler.RegionCoverage(0).oldestAttemptAge<400 && scheduler.RegionCoverage(3).oldestAttemptAge<400,
                    "Prewarm broke the retained-region update-age bound");
            }
            return latency;
        };
        const auto baseline=run(false),prewarm=run(true);
        Check(prewarm<=12 && prewarm*2<baseline,"Global prewarm did not accelerate the new near plane");

        VansGIProbeWorkScheduler scheduler;std::string error;GIProbeWorkRegion first,late;
        first.raysPerProbe=late.raysPerProbe=32;late.maxPlacedProbes=32;late.prewarmSpacing=4;
        for(uint32_t id=0;id<64;++id)first.probeIndices.push_back(id);
        Check(scheduler.Configure({first,late},1,32,error,true),"Late activation configure failed");
        const auto finish=[&](uint32_t* ordinary)
        {
            const auto& batch=scheduler.NextFrame();
            for(size_t r=0;r<batch.size();++r)for(auto e:batch[r].entries)
            {if(r==0 && ordinary)++*ordinary;Check(scheduler.RetireFeedback(r,e,{e.probeIndex,GIProbeComplete,e.elapsedSeconds,e.cycleIndex}),"Late activation feedback failed");}
        };
        for(uint32_t frame=0;frame<640;++frame)finish(nullptr);
        for(uint32_t id=0;id<32;++id)late.probeIndices.push_back(id);
        Check(scheduler.UpdatePlacedProbes(1,late.probeIndices,{},error),"Late activation failed");
        uint32_t ordinary=0;for(uint32_t frame=0;frame<16;++frame)finish(&ordinary);
        Check(ordinary>=6,"Late region tried to catch up on nonexistent historical work");
        for(uint32_t frame=0;frame<104;++frame)finish(nullptr);
        Check(scheduler.RegionCoverage(1).updated==32,"Single-probe budget starved prewarm or normal work");
        GIProbeWorkRegion retries{32,{0,1,2,3},4,4.0f};
        Check(scheduler.Configure({retries},2,64,error,true),"Prewarm retry configure failed");
        uint64_t issuedWarm=0;
        for(uint32_t frame=0;frame<40;++frame)
        {
            const auto batch=scheduler.NextFrame()[0];issuedWarm+=batch.prewarmUpdates;
            std::set<uint32_t> unique;
            for(auto e:batch.entries)
            {
                Check(unique.insert(e.probeIndex).second,"Prewarm retry duplicated a work item");
                const uint32_t status=e.probeIndex==0 && frame<24?GIProbeVoxelPage:GIProbeComplete;
                Check(scheduler.RetireFeedback(0,e,{e.probeIndex,status,e.elapsedSeconds,e.cycleIndex},true),"Prewarm incomplete feedback failed");
            }
            Check(issuedWarm<=uint64_t(frame+1)*2/8,"Tiny work budget exceeded cumulative prewarm credit");
        }
        Check(scheduler.RegionCoverage(0).updated==4 && scheduler.RegionCoverage(0).incompleteAttempts>0,
            "Page readiness did not resume prewarm or blocked other probes");
        GIProbeWorkRegion cold{32,{},16,4.0f};for(uint32_t id=0;id<16;++id)cold.probeIndices.push_back(id);
        Check(scheduler.Configure({cold},8,256,error,true),"Readiness gate configure failed");
        for(uint32_t frame=0;frame<13;++frame)
        {
            const bool ready=frame>=10;scheduler.SetPrewarmReady(ready);
            const auto batch=scheduler.NextFrame()[0];
            Check(batch.entries.size()==8 && (ready || batch.prewarmUpdates==0),"Unready sources wasted extra prewarm budget or blocked ordinary work");
            if(frame==10)Check(batch.prewarmUpdates==1,"Published coarse field did not resume prewarm");
            for(auto e:batch.entries)Check(scheduler.RetireFeedback(0,e,{e.probeIndex,ready?GIProbeComplete:GIProbeVoxelPage,e.elapsedSeconds,e.cycleIndex},true),
                "Readiness gate feedback failed");
        }
        Check(scheduler.RegionCoverage(0).updated==16,"Coarse publication failed to drain cold prewarm requests");
        late.prewarmSpacing=std::numeric_limits<float>::quiet_NaN();
        Check(!scheduler.Configure({late},1,32,error,true),"Invalid prewarm scale accepted");
        std::cout<<"[GIProbeWork] global prewarm: near plane baseline="<<baseline<<" frames prewarm="<<prewarm
            <<" frames; continuous movement retains indoor/far sweeps; late empty activation and tiny budget PASS\n";
    }

    void RejectedFeedbackRetirement()
    {
        VansGIProbeWorkScheduler scheduler;
        std::string error;
        Check(scheduler.Configure({{32,{0,1}}},2,64,error),"Rejected feedback fixture configure failed");
        scheduler.NotifyGlobalLighting(.25f);
        const auto issued=scheduler.NextFrame(.1)[0].entries;
        Check(!scheduler.RetireFeedback(0,issued[0],{0,0u,issued[0].elapsedSeconds,issued[0].cycleIndex},true),
            "Unwritten completed feedback was accepted");
        Check(scheduler.RetireFeedback(0,issued[1],{1,GIProbeComplete,issued[1].elapsedSeconds,issued[1].cycleIndex}),
            "Valid neighbouring feedback was rejected");
        // 此时对应 fence 已完成，坏记录也不能遗留在途身份并阻断下一帧改灯。
        scheduler.NotifyGlobalLighting(.125f);
        const auto retry=scheduler.NextFrame(.2)[0].entries;
        Check(retry[0].flags==GIWorkResetGeometry && retry[1].flags==0u &&
            retry[0].primaryLightingChange==.375f && retry[0].scatterLightingChange==.375f &&
            retry[1].primaryLightingChange==.125f && retry[1].scatterLightingChange==.125f,
            "Rejected feedback consumed lighting or reset an unaffected probe");
        Check(scheduler.RegionCoverage(0).updated==1 && scheduler.RegionCoverage(0).maxCompletedUpdates==1,
            "Rejected feedback advanced completed coverage");
        for(const auto& entry:retry)
            Check(scheduler.RetireFeedback(0,entry,{entry.probeIndex,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex}),
                "Rejected feedback did not recover on the next publication");
        Check(!scheduler.HasPendingLightingUpdates(),"Recovered feedback retained a lighting request");
        for(bool warm:{false,true})
        {
            VansGIProbeWorkScheduler pending;
            Check(pending.Configure({{32,{0},1,1.0f}},1,32,error),"Malformed feedback fixture configure failed");
            if(warm)
                for(const auto& entry:pending.NextFrame(.1)[0].entries)
                    Check(pending.RetireFeedback(0,entry,{0,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex}),
                        "Malformed feedback fixture prewarm failed");
            pending.NotifyGlobalLighting(.25f);
            Check(pending.NotifyLocalLighting(0,{0},.125f,error),"Malformed feedback fixture local lighting failed");
            const auto entry=pending.NextFrame(.1)[0].entries[0];
            const std::array<GIProbeFeedback,8> malformed{{
                {0,0u,entry.elapsedSeconds,entry.cycleIndex},
                {1,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex},
                {0,GIProbeComplete,entry.elapsedSeconds+.1f,entry.cycleIndex},
                {0,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex+1u},
                {0,GIProbeComplete,std::numeric_limits<float>::quiet_NaN(),entry.cycleIndex},
                {0,GIProbeComplete|GIProbeVoxelPage,entry.elapsedSeconds,entry.cycleIndex},
                {0,1024u,entry.elapsedSeconds,entry.cycleIndex},
                {0,GIProbePrimaryPublished|GIProbeVoxelPage,entry.elapsedSeconds,entry.cycleIndex}
            }};
            for(const auto& bad:malformed)
            {
                auto attempt=pending;
                Check(!attempt.RetireFeedback(0,entry,bad,true),"Malformed completed feedback accepted");
                Check(!attempt.RetireFeedback(0,entry,{0,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex}),
                    "Retired GPU issue accepted another completion");
                attempt.NotifyGlobalLighting(.125f);
                Check(attempt.NotifyLocalLighting(0,{0},.125f,error),"Rejected feedback blocked local lighting");
                Check(attempt.UpdatePlacedProbes(0,{0},{},error),"Rejected feedback blocked placement reconciliation");
                const auto next=attempt.NextFrame(.2)[0].entries[0];
                const auto coverage=attempt.RegionCoverage(0);
                Check(next.flags==GIWorkResetGeometry && next.primaryLightingChange==.625f &&
                    next.scatterLightingChange==.625f && std::abs(next.primaryAgeSeconds-.3f)<1e-6f &&
                    std::abs(next.scatterAgeSeconds-.3f)<1e-6f && coverage.updated==uint64_t(warm) &&
                    coverage.incompleteAttempts==0u && coverage.primaryOnlyPublications==0u && coverage.pageFailures==0u,
                    "Rejected feedback consumed pending lighting, publication age or completion statistics");
                Check(attempt.RetireFeedback(0,next,{0,GIProbeComplete,next.elapsedSeconds,next.cycleIndex}) &&
                    !attempt.HasPendingLightingUpdates(),"Malformed feedback failed to recover");
            }
        }
        std::cout<<"[GIProbeWork] rejected feedback retirement: cold/warm identity, lighting, placement and isolated recovery PASS\n";
    }

    void ActiveUpdateAges()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({{32,{1,2,3},4}},1,32,error),"Age fixture configure failed");
        for(uint32_t frame=0;frame<9;++frame)
            for(const auto e:scheduler.NextFrame()[0].entries)
                Check(scheduler.RetireFeedback(0,e,{e.probeIndex,e.probeIndex==1?uint32_t(GIProbeVoxelBudget):uint32_t(GIProbeComplete),e.elapsedSeconds,e.cycleIndex},true),
                    "Age fixture completion failed");
        auto coverage=scheduler.RegionCoverage(0);
        Check(coverage.placed==3 && coverage.attempted==3 && coverage.updated==2 && coverage.incompleteAttempts==3 &&
            coverage.oldestAttemptAge==2 && coverage.oldestCompletionAge==1 && coverage.oldestUnpublishedAge==9 &&
            coverage.stepFailures==3 && coverage.heightFailures==0 && coverage.pageFailures==0 && coverage.coverageFailures==0,
            "Audit did not distinguish scheduled unknown probes from successful lighting");
        Check(scheduler.UpdatePlacedProbes(0,{2,3,4},{},error,{4}),"Age fixture recycle failed");
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.attempted==2 && coverage.updated==2 && coverage.incompleteAttempts==0 && coverage.oldestUnpublishedAge==0,
            "Removed probes or old world identities leaked into active age statistics");
        for(const auto e:scheduler.NextFrame()[0].entries)
            Check(scheduler.RetireFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Age fixture prewarm failed");
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.attempted==3 && coverage.updated==3 && coverage.oldestCompletionAge==2,
            "New placement age started at scene creation or lost retained completion history");
        std::cout<<"[GIProbeWork] active ages: unknown retries, success, removal and recycled identity PASS\n";
        // 非法记录各自在独立批次中退役；原调度器继续验证合法的不完整反馈统计。
        for(uint32_t reason:{2u,4u,8u,16u,30u})
        {
            const auto e=scheduler.NextFrame()[0].entries[0];
            Check(!VansGIProbeWorkScheduler(scheduler).RetireFeedback(0,e,{e.probeIndex,reason,e.elapsedSeconds,e.cycleIndex}),"Hardware feedback accepted a world failure");
            Check(!VansGIProbeWorkScheduler(scheduler).RetireFeedback(0,e,{e.probeIndex,reason|1u,e.elapsedSeconds,e.cycleIndex},true),"Mixed success/failure feedback accepted");
            Check(!VansGIProbeWorkScheduler(scheduler).RetireFeedback(0,e,{e.probeIndex,512u,e.elapsedSeconds,e.cycleIndex},true),"Unknown failure bit accepted");
            Check(!VansGIProbeWorkScheduler(scheduler).RetireFeedback(0,e,{e.probeIndex,0u,e.elapsedSeconds,e.cycleIndex},true),"Unwritten feedback accepted as an incomplete query");
            Check(scheduler.RetireFeedback(0,e,{e.probeIndex,reason,e.elapsedSeconds,e.cycleIndex},true),"World failure reason rejected");
        }
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.heightFailures==2 && coverage.pageFailures==2 && coverage.stepFailures==5 && coverage.coverageFailures==2,
            "Failure reason accounting changed across placement or combined causes");
        for(uint32_t reason:{32u,64u,128u,256u,480u})
        {
            const auto e=scheduler.NextFrame()[0].entries[0];
            Check(!VansGIProbeWorkScheduler(scheduler).RetireFeedback(0,e,{e.probeIndex,reason,e.elapsedSeconds,e.cycleIndex}),"Hardware feedback accepted auxiliary world-query failure");
            Check(scheduler.RetireFeedback(0,e,{e.probeIndex,reason,e.elapsedSeconds,e.cycleIndex},true),"Auxiliary world-query failure rejected");
        }
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.heightFailures==2 && coverage.pageFailures==2 && coverage.stepFailures==5 && coverage.coverageFailures==2 &&
            coverage.scatterHeightFailures==2 && coverage.scatterPageFailures==2 && coverage.scatterStepFailures==2 && coverage.scatterCoverageFailures==2,
            "Auxiliary failures contaminated primary geometry diagnostics or lost combined reasons");
        const auto e=scheduler.NextFrame()[0].entries[0];
        Check(scheduler.RetireFeedback(0,e,{e.probeIndex,GIProbeVoxelPage|GIProbeScatterVoxelBudget,e.elapsedSeconds,e.cycleIndex},true),
            "Combined primary and auxiliary failure rejected");
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.pageFailures==3 && coverage.scatterStepFailures==3,"Combined primary/auxiliary failure was not counted independently");
        VansGIProbeWorkScheduler partial;Check(partial.Configure({{256,{0}}},1,256,error),"Partial scheduler setup failed");
        constexpr uint32_t status=GIProbePrimaryPublished|GIProbeScatterVoxelPage;
        auto request=partial.NextFrame()[0].entries[0];
        Check(!VansGIProbeWorkScheduler(partial).RetireFeedback(0,request,{0,status,request.elapsedSeconds,request.cycleIndex},true),"Cold primary-only publication accepted");
        Check(partial.RetireFeedback(0,request,{0,GIProbeComplete,request.elapsedSeconds,request.cycleIndex}),"Partial fixture prewarm failed");
        partial.NotifyGlobalLighting(1.0f);
        for(uint32_t i=0;i<4;++i)
        {
            request=partial.NextFrame()[0].entries[0];
            Check(request.flags==0u && request.primaryLightingChange==(i==0?1.0f:0.0f) && request.scatterLightingChange==1.0f,"Partial publication replayed the consumed primary lighting reset");
            Check(!VansGIProbeWorkScheduler(partial).RetireFeedback(0,request,{0,status|GIProbeVoxelPage,request.elapsedSeconds,request.cycleIndex},true),"Primary failure was published as valid");
            Check(!VansGIProbeWorkScheduler(partial).RetireFeedback(0,request,{0,status,request.elapsedSeconds,request.cycleIndex}),"Hardware accepted partial world publication");
            Check(partial.RetireFeedback(0,request,{0,status,request.elapsedSeconds,request.cycleIndex},true),"Valid partial world publication rejected");
            Check(!partial.RetireFeedback(0,request,{0,status,request.elapsedSeconds,request.cycleIndex},true),"Replayed partial feedback accepted");
        }
        const auto partialCoverage=partial.RegionCoverage(0);
        Check(partialCoverage.minCompletedUpdates==1&&partialCoverage.primaryOnlyPublications==4&&partialCoverage.incompleteAttempts==4&&
            partialCoverage.oldestCompletionAge==4&&partialCoverage.scatterPageFailures==4,"Partial publication hid stale full lighting or inflated completion");
        partial.ResetGeometry();request=partial.NextFrame()[0].entries[0];
        Check(!VansGIProbeWorkScheduler(partial).RetireFeedback(0,request,{0,status,request.elapsedSeconds,request.cycleIndex},true),"Geometry reset accepted old scattering");
        Check(partial.RetireFeedback(0,request,{0,GIProbeScatterVoxelPage,request.elapsedSeconds,request.cycleIndex},true),"Geometry failure rejected");
        request=partial.NextFrame()[0].entries[0];Check(request.flags==GIWorkResetGeometry,"Geometry failure consumed reset request");
        Check(partial.RetireFeedback(0,request,{0,GIProbeComplete,request.elapsedSeconds,request.cycleIndex}),"Full recovery rejected");
        Check(partial.RegionCoverage(0).minCompletedUpdates==2,"Recovery lost complete update accounting");
        std::cout<<"[GIProbeWork] primary-only feedback: strict validity, reset consumption, full age, repeated failure and recovery PASS\n";
    }

    void AllPlacedCoverage()
    {
        // 小预算、整除/非整除数量、队尾回绕及持续静态光照都必须反复覆盖所有地址。
        for (uint32_t budget : {64u, 256u, 2048u})
        for (uint32_t count : {1u, 127u, 128u})
        {
            GIProbeWorkRegion region; region.raysPerProbe = 64;
            for (uint32_t i = 0; i < count; ++i) region.probeIndices.push_back(i * 7u);
            VansGIProbeWorkScheduler scheduler; std::string error;
            Check(scheduler.Configure({region}, 32, budget, error), "all placed setup failed");
            const uint32_t perFrame = (std::min)({count, 32u, budget / 64u});
            const uint32_t cycleFrames = (count + perFrame - 1u) / perFrame;
            for (uint32_t frame = 0; frame < cycleFrames * 3u; ++frame)
            {
                const auto& batch = scheduler.NextFrame()[0];
                Check(batch.RayCount() == perFrame * 64u && batch.entries.size() == perFrame, "complete work lost budget or directions");
                std::set<uint32_t> ids;
                for (const auto& entry : batch.entries)
                {
                    Check(ids.insert(entry.probeIndex).second, "wrapped queue updated same probe twice in a frame");
                    Check(scheduler.RetireFeedback(0, entry, {entry.probeIndex,1u,entry.elapsedSeconds,entry.cycleIndex}),
                        "completion feedback rejected");
                }
            }
            const auto coverage = scheduler.RegionCoverage(0);
            Check(coverage.placed == count && coverage.updated == count && coverage.minCompletedUpdates >= 3u,
                "placed probe did not complete three updates under static lighting");
        }

        // 旧策略会让 1024-ray 区域永远错过只剩 768 rays 的帧尾，下一帧又先处理廉价区域。
        VansGIProbeWorkScheduler mixed; std::string error;
        Check(mixed.Configure({{256,{0,1}}, {1024,{4,8}}}, 2, 1024, error), "mixed cost setup failed");
        for (uint32_t frame = 0; frame < 12; ++frame)
        {
            const auto& work = mixed.NextFrame();
            uint64_t rays = 0;
            for (size_t r = 0; r < work.size(); ++r)
            {
                rays += work[r].RayCount();
                for (const auto& entry : work[r].entries)
                    Check(mixed.RetireFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}), "mixed feedback rejected");
            }
            Check(rays <= 1024, "fairness exceeded the ray budget");
        }
        for (size_t r = 0; r < 2; ++r)
            Check(mixed.RegionCoverage(r).minCompletedUpdates >= 3, "mixed-cost region starved");

        // DustV3 的实际容量/预算：每帧 256 个完整 probe，256 帧覆盖全部 65536 个。
        // 每帧重置光照也不能重置游标；取消提交不算完成，仍必须继续轮到所有地址。
        for (bool resetEveryFrame : {false, true})
        {
            GIProbeWorkRegion region; region.raysPerProbe = 256;
            for (uint32_t i = 0; i < 65536; ++i) region.probeIndices.push_back(i);
            VansGIProbeWorkScheduler scheduler;
            Check(scheduler.Configure({region}, 4096, 65536, error), "DustV3 budget setup failed");
            for (uint32_t frame = 0; frame < 768; ++frame)
            {
                if (resetEveryFrame) scheduler.NotifyGlobalLighting(1.0f);
                const auto& batch = scheduler.NextFrame()[0];
                Check(batch.entries.size() == 256 && batch.RayCount() == 65536, "DustV3 frame budget changed");
                if (frame == 0)
                    Check(scheduler.RegionCoverage(0).updated == 0, "issued work counted as GPU completion");
                for (const auto& entry : batch.entries)
                {
                    Check(entry.flags == (frame < 256 ? GIWorkResetGeometry : 0u) && entry.primaryLightingChange==(resetEveryFrame?1.0f:0.0f), "lighting reset lost its pending work");
                    Check(scheduler.RetireFeedback(0, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}), "DustV3 completion rejected");
                }
                if ((frame + 1u) % 256u == 0u)
                {
                    const auto coverage = scheduler.RegionCoverage(0);
                    Check(coverage.updated == 65536 && coverage.minCompletedUpdates == (frame + 1u) / 256u &&
                        coverage.maxCompletedUpdates == coverage.minCompletedUpdates, "DustV3 all-placed coverage interval exceeded");
                }
            }
        }
        VansGIProbeWorkScheduler retry;
        Check(retry.Configure({{256,{}}, {256,{4}}},1,256,error), "empty region setup failed");
        const auto dropped = retry.NextFrame()[1].entries[0];
        const auto fresh = retry.NextFrame()[1].entries[0];
        Check(fresh.flags == GIWorkResetGeometry && fresh.cycleIndex != dropped.cycleIndex, "lost completion suppressed retry");
        Check(!retry.RetireFeedback(1,dropped,{4,1,dropped.elapsedSeconds,dropped.cycleIndex}), "stale completion accepted");
        Check(!retry.RetireFeedback(1,fresh,{4,0,fresh.elapsedSeconds,fresh.cycleIndex}), "unfinished GPU work counted as complete");
        retry.NotifyGlobalLighting(.25f);
        const auto recovered = retry.NextFrame()[1].entries[0];
        Check(recovered.cycleIndex != fresh.cycleIndex && recovered.flags == GIWorkResetGeometry,
            "Rejected GPU work did not issue a fresh recovery");
        Check(!retry.RetireFeedback(1,fresh,{4,1,fresh.elapsedSeconds,fresh.cycleIndex}), "Retired feedback consumed the new issue");
        Check(retry.RetireFeedback(1,recovered,{4,1,recovered.elapsedSeconds,recovered.cycleIndex}), "retry completion rejected");
        Check(retry.RegionCoverage(1).minCompletedUpdates == 1, "dropped work inflated coverage");
        retry.NotifyGlobalLighting(1.0f);
        const auto pending=retry.NextFrame()[1].entries[0];
        Check(retry.RetireFeedback(1,pending,{4,GIProbeVoxelPage,pending.elapsedSeconds,pending.cycleIndex},true),"World unknown feedback was not consumed");
        Check(retry.RegionCoverage(1).minCompletedUpdates==1,"World unknown feedback inflated completion count");
        Check(retry.NextFrame()[1].entries[0].primaryLightingChange==1.0f,"World unknown feedback lost the reset request");
        std::cout << "[GIProbeWork] all placed repeated coverage, mixed-cost fairness, DustV3 65536 probes / 256-frame sweep, reset and completion retry PASS\n";
    }

    void TracePolicyOwnership()
    {
        VansGISettings settings;
        settings.regions[0].trace = {79.0f, .25f, 2.0f};
        const auto trace = ResolveGIRegion(settings.regions[0]).trace;
        for (float spacing : {.125f, 4.0f, 64.0f})
        {
            auto region = settings.regions[0];
            region.probeSpacing = spacing;
            for (auto mode : {GIProbeRegionMode::RegularGrid, GIProbeRegionMode::Adaptive})
            {
                region.mode = mode;
                Check(GIProbeTraceSettingsEqual(ResolveGIRegion(region).trace, trace),
                    "Placement density or mode changed the explicit query policy");
            }
        }
        auto changed = settings;
        changed.regions[0].trace = {192.0f, 1.0f, 4.0f};
        Check(GISettingsResourceLayoutEquals(settings, changed), "Query policy rebuilt resource layout");
        auto scene = std::make_unique<VansScene>();
        scene->SetGISettings(settings);
        scene->ClearGIProbeResourcesDirty();
        scene->SetGISettings(changed);
        Check(!scene->AreGIProbeResourcesDirty() && GIProbeTraceSettingsEqual(scene->GetGISettings().regions[0].trace, changed.regions[0].trace),
            "Query edit did not preserve scene resource ownership");

        VansGIProbeWorkScheduler scheduler;
        std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{64,{0,1}},GIProbeWorkRegion{64,{0,1}}},4,256,error),
            "Query reset fixture failed");
        const auto initial = scheduler.NextFrame();
        for (size_t region = 0; region < initial.size(); ++region)
            for (const auto& entry : initial[region].entries)
                Check(scheduler.RetireFeedback(region,entry,{entry.probeIndex,GIProbeComplete,entry.elapsedSeconds,entry.cycleIndex}),
                    "Query reset initial feedback failed");
        scheduler.NotifyGlobalLighting(.5f);
        scheduler.ResetGeometry(0);
        bool rejected = false;
        try { scheduler.ResetGeometry(2); } catch (const std::out_of_range&) { rejected = true; }
        Check(rejected, "Invalid query region was accepted");
        const auto work = scheduler.NextFrame();
        for (size_t region = 0; region < work.size(); ++region)
            for (const auto& entry : work[region].entries)
            {
                Check(((entry.flags & GIWorkResetGeometry) != 0u) == (region == 0),
                    "Query reset changed another region's geometry history");
                Check(entry.primaryLightingChange > 0 && entry.scatterLightingChange > 0,
                    "Query reset discarded unconsumed lighting changes");
            }
    }

    void AmbientSkyBudgetOwnership()
    {
        VansGISettings settings;
        settings.ambientSkyCache.enabled = true;
        Check(settings.ambientSkyCache.cellUpdatesPerFrame == 22u,
            "Sky default changed the previous effective 132 directional queries");
        auto scene = std::make_unique<VansScene>();
        scene->SetGISettings(settings);
        scene->ClearGIProbeResourcesDirty();
        for (uint32_t cells : {1u, 22u, GIAmbientSkyCacheSettings::MaxCellUpdatesPerFrame})
        {
            auto edited = settings;
            edited.ambientSkyCache.cellUpdatesPerFrame = cells;
            scene->SetGISettings(edited);
            Check(GISettingsResourceLayoutEquals(settings, edited) && !scene->AreGIProbeResourcesDirty() &&
                scene->GetGISettings().ambientSkyCache.cellUpdatesPerFrame == cells,
                "Sky update budget recreated GI resources or lost the authored cell count");
        }
        auto resized = settings;
        resized.ambientSkyCache.gridSpacing *= 2.0f;
        Check(!GISettingsResourceLayoutEquals(settings, resized), "Sky spatial change failed to invalidate cache layout");
        auto normalized = settings.ambientSkyCache;
        normalized.cellUpdatesPerFrame = UINT32_MAX;
        NormalizeGIAmbientSkyCacheSettings(normalized);
        Check(normalized.cellUpdatesPerFrame == 43u, "Sky maximum changed the previous effective 258 directional queries");
        normalized.cellUpdatesPerFrame = 0;
        NormalizeGIAmbientSkyCacheSettings(normalized);
        Check(normalized.cellUpdatesPerFrame == 1u, "Sky minimum no longer updates one complete cell");
    }

    void ConfigurationOwnership(const Vans::VansSerializedValue& environment)
    {
        // 通过生产 API、文档编辑/保存与加载器验证，不复制一套设置投影。
        auto scene = std::make_unique<VansScene>();
        Vans::EditorAPI::EngineAPIImpl api(scene.get(), nullptr);
		CapturingSceneAuthoringHost authoring;
		api.BindSceneAuthoring(&authoring);
        struct Detach { Vans::EditorAPI::EngineAPIImpl& api; ~Detach() { api.BindSceneAuthoring(nullptr); api.BindRuntime(nullptr, nullptr); } } detach{api};
        VansGISettings initial;
        initial.regions.resize(2);
        initial.regions[0].size = {13.2f, 8.1f, 6.7f}; initial.regions[0].probeSpacing = 1.25f;
        initial.regions[0].center = {-7,-11,3}; initial.regions[0].overrideGridDimensions = false;
        initial.regions[1].stableId = 8; initial.regions[1].name = "Authored explicit grid";
        initial.regions[1].overrideGridDimensions = true; initial.regions[1].gridDimensions = {9,10,11};
        initial.regions[1].probeSpacing = 2.5f; initial.regions[1].enabled = false;
        initial.regions[0].trace = {79.0f,.25f,1.0f};
        initial.regions[1].trace = {192.0f,1.0f,2.0f};
        scene->SetGISettings(initial);
        scene->ClearGIProbeResourcesDirty();
        api.SetGIProbeVisualization(true, true, 3);
        Check(scene->GetGISettings().showProbeGizmos && scene->GetGISettings().showProbeVolume &&
            scene->GetGISettings().gizmoStride == 3 && !scene->AreGIProbeResourcesDirty() &&
            GISettingsResourceLayoutEquals(initial, scene->GetGISettings()),
            "position display changed GI layout or requested resource recreation");
		Check(authoring.Take().empty() && !api.GetGIProbeDebugSnapshot(),
            "display controls staged a scene write or exposed positions before resources were ready");
        api.SetGIProbeVisualization(false, false, 0);
        Check(!scene->GetGISettings().showProbeGizmos && !scene->GetGISettings().showProbeVolume &&
            scene->GetGISettings().gizmoStride == 1 && !scene->AreGIProbeResourcesDirty(),
            "position display toggle or stride normalization failed");
        api.SaveReflectionProbeConfiguration();
		const auto reflectionEdits = authoring.Take();
        Check(reflectionEdits.size() == 1 && reflectionEdits[0].propertyPointer == "/settings/reflectionProbes",
            "reflection configuration saved outside the scene settings block");
        const auto& reflectionBefore = reflectionEdits[0].value;

        const auto directory = std::filesystem::temp_directory_path() /
            ("ForestGIConfig_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        struct Cleanup
        {
            std::filesystem::path directory;
            ~Cleanup() { std::error_code error; std::filesystem::remove(directory / "Scene.json", error); std::filesystem::remove(directory, error); }
        } cleanup{directory};
        const auto path = directory / "Scene.json";
        auto fixture = nlohmann::json::parse(R"({"schemaVersion":2,"sceneGuid":"677b6e10-e731-4a26-b3a7-f15a42cd972b","name":"GI config contract","settings":{"reflectionProbes":{"placement":{"enabled":false},"probes":[]}},"entities":[]})");
        fixture["settings"]["environment"] = Vans::EncodeSerializedValueJson<nlohmann::json>(environment);
        { std::ofstream output(path); output << fixture.dump(); }
        auto document = Vans::VansSceneDocumentLoader::Load(path);
        Check(bool(document), "GI save fixture failed to load");
        Vans::VansSceneEditService edits(*document.document);
		authoring.document = document.document.get();
		authoring.edits = &edits;
        for (bool enabled : {false, true, false})
        {
            const auto fingerprint = Vans::VansSceneDocumentLoader::Fingerprint(path);
            auto settings = api.GetGISettings();
            Check(settings.maximumAmbientSkyCellUpdatesPerFrame == GIAmbientSkyCacheSettings::MaxCellUpdatesPerFrame,
                "Editor sky budget capacity diverged from the GI owner");
            settings.ambientSkyCache = {true, 6.0f, enabled ? 43u : 1u};
            settings.world.enabled=enabled;settings.world.voxelSize=.5f;settings.world.coverageDistance=128;
            settings.regions[1].worldOnly=true;
            settings.regions[1].followView=true;
            settings.world.extinctionScale=1.25f;settings.world.levelCount=4;settings.world.maxBricks=2048;settings.world.bricksPerFrame=32;settings.world.maxTraceSteps=384;
            settings.regions[0].mode = enabled ? Vans::EditorAPI::GIRegionPlacementMode::Adaptive : Vans::EditorAPI::GIRegionPlacementMode::Uniform;
            settings.regions[0].trace = {83.0f,.125f,1.5f};
            settings.placement.minProbeSpacing = 0.125f;
            settings.placement.maxProbeSpacing = 3.25f;
            settings.placement.parentProbeMaxSize = 2.75f;
            settings.placement.maxProbeCount = 90001;
            settings.placement.maxProbeUpdatesPerFrame = 777;
            settings.placement.maxRaysPerFrame = 23456;
            Check(api.ApplyGISettings(settings), "GI authoring settings Apply failed");
			Check(authoring.Take().empty(), "Apply GI implicitly staged a scene write");
            const auto& applied = scene->GetGISettings();
            Check(applied.ambientSkyCache.cellUpdatesPerFrame == settings.ambientSkyCache.cellUpdatesPerFrame &&
                applied.ambientSkyCache.enabled && applied.ambientSkyCache.gridSpacing == 6.0f,
                "Sky cell budget did not reach native settings");
            Check(applied.regions[0].trace.maxDistance == 83.0f && applied.regions[0].trace.minVoxelSize == .125f &&
                applied.regions[0].trace.scatterMinVoxelSize == 1.5f && applied.regions[1].trace.maxDistance == 192.0f &&
                applied.regions[1].trace.minVoxelSize == 1.0f && applied.regions[1].trace.scatterMinVoxelSize == 2.0f,
                "Independent regional trace settings were lost in the editor adapter");
            Check(applied.regions[1].worldOnly&&!applied.regions[0].worldOnly,"World region scope did not pass the editor API");
            Check(applied.regions[1].followView&&!applied.regions[0].followView,"Scrolling mode did not pass the editor API");
            Check(applied.world.enabled==enabled&&applied.world.voxelSize==.5f&&applied.world.coverageDistance==128&&applied.world.extinctionScale==1.25f&&
                applied.world.levelCount==4&&applied.world.maxBricks==2048&&applied.world.bricksPerFrame==32&&applied.world.maxTraceSteps==384,"World GI controls did not reach native settings");
            Check((applied.regions[0].mode == GIProbeRegionMode::Adaptive) == enabled && applied.placement.minProbeSpacing == 0.125f
                && applied.placement.maxProbeSpacing == 3.25f && applied.placement.parentProbeMaxSize == 2.75f && applied.placement.maxProbeCount == 90001
                && applied.placement.maxProbeUpdatesPerFrame == 777 && applied.placement.maxRaysPerFrame == 23456,
                "GI placement controls did not reach native settings");
            Check(!applied.regions[0].overrideGridDimensions && applied.regions[0].size == initial.regions[0].size
                && applied.regions[0].center == initial.regions[0].center && applied.regions[1].overrideGridDimensions
                && applied.regions[1].gridDimensions == initial.regions[1].gridDimensions && !applied.regions[1].enabled,
                "GI placement toggle modified the authored region definition");
            api.SaveGIConfiguration();
			const auto saved = authoring.Take();
            Check(saved.size() == 1 && saved[0].propertyPointer == "/settings/globalIllumination", "GI configuration save path is not loader-visible");
            Check(Vans::VansSceneDocumentLoader::Fingerprint(path) == fingerprint, "Apply or document staging wrote the file before Save Scene");
            const Vans::VansEditorAssetSaveOperations saveOperations;
            Check(bool(Vans::VansEditorAssetSaveService::Get().SaveSceneAndAssets(
                saveOperations, *document.document, {})), "explicit GI scene save failed");
            auto reloaded = Vans::VansSceneDocumentLoader::Load(path);
            Check(bool(reloaded), "saved GI document failed to reload");
            const auto json = Vans::EncodeSerializedValueJson<nlohmann::json>(reloaded.document->SerializedRootSnapshot());
            Check(!json["settings"]["globalIllumination"]["placement"].contains("enabled") &&
                json["settings"]["globalIllumination"]["regions"][0]["mode"] == (enabled ? "adaptive" : "uniform") &&
                json["settings"]["globalIllumination"]["regions"][1]["mode"] == "uniform",
                "Per-region placement was not saved as the only mode authority");
            Check(!json.contains("globalIllumination") && !json.contains("reflectionProbes")
                && json["settings"]["reflectionProbes"]["placement"]["enabled"] == false, "GI save changed reflection settings or wrote a parallel root block");
            Vans::VansSceneRenderSettingsConfig config; std::string error;
            if (!Vans::VansSceneRenderSettingsConfigReader::Read(Vans::DecodeSerializedValueJson(json["settings"]), config, error))
                throw std::runtime_error("GI saved settings failed production decode: " + error);
            Check(config.globalIllumination.has_value(), "GI saved settings missing after production decode");
            const auto& decoded = *config.globalIllumination;
            for (int invalid = 0; invalid < 16; ++invalid)
            {
                auto rejected = json["settings"];
                auto& gi = rejected["globalIllumination"];
                if (invalid == 0) gi["regions"][0]["mode"] = "unknown";
                if (invalid == 1) gi["regions"][0].erase("mode");
                if (invalid == 2) gi["placement"]["enabled"] = true;
                if (invalid == 3) { gi["regions"][0]["mode"] = "adaptive"; gi["regions"][0]["followView"] = true; }
                if (invalid == 4) gi["regions"][0].erase("trace");
                if (invalid == 5) gi["regions"][0]["maxRayDistance"] = 100;
                if (invalid == 6) gi["regions"][0]["trace"]["minVoxelSize"] = -1;
                if (invalid == 7) gi["regions"][0]["trace"].erase("scatterMinVoxelSize");
                if (invalid == 8) gi["regions"][0]["trace"]["maxDistance"] = 0;
                if (invalid == 9) gi["regions"][0]["trace"]["maxDistance"] = "invalid";
                if (invalid == 10) gi["ambientSkyCache"]["queriesPerFrame"] = 128;
                if (invalid == 11) gi["ambientSkyCache"].erase("cellUpdatesPerFrame");
                if (invalid == 12) gi["ambientSkyCache"]["cellUpdatesPerFrame"] = 0;
                if (invalid == 13) gi["ambientSkyCache"]["cellUpdatesPerFrame"] = -1;
                if (invalid == 14) gi["ambientSkyCache"]["cellUpdatesPerFrame"] = 1.5;
                if (invalid == 15) gi["ambientSkyCache"]["cellUpdatesPerFrame"] = uint64_t(UINT32_MAX) + 1;
                Vans::VansSceneRenderSettingsConfig rejectedConfig;
                Check(!Vans::VansSceneRenderSettingsConfigReader::Read(Vans::DecodeSerializedValueJson(rejected), rejectedConfig, error) && !error.empty(),
                    "Invalid or obsolete GI placement config was silently accepted");
            }
            Check(decoded.ambientSkyCache.cellUpdatesPerFrame == (enabled ? 43u : 1u) &&
                decoded.ambientSkyCache.enabled == true && decoded.ambientSkyCache.gridSpacing == 6.0f &&
                !json["settings"]["globalIllumination"]["ambientSkyCache"].contains("queriesPerFrame"),
                "Sky cell budget did not round trip through the sole current schema");
            Check(decoded.regions[0].trace.maxDistance == 83.0f && decoded.regions[0].trace.minVoxelSize == .125f &&
                decoded.regions[0].trace.scatterMinVoxelSize == 1.5f && decoded.regions[1].trace.maxDistance == 192.0f &&
                decoded.regions[1].trace.minVoxelSize == 1.0f && decoded.regions[1].trace.scatterMinVoxelSize == 2.0f &&
                !json["settings"]["globalIllumination"]["regions"][0].contains("maxRayDistance"),
                "Trace settings did not round trip through the current scene schema");
            Check(decoded.regions[1].worldOnly==true && decoded.regions[0].worldOnly==false,"World region scope lost during explicit save/reload");
            Check(decoded.regions[1].followView==true && decoded.regions[0].followView==false,"Scrolling mode lost during explicit save/reload");
            Check(decoded.world.enabled==enabled&&decoded.world.voxelSize==.5f&&decoded.world.coverageDistance==128&&decoded.world.extinctionScale==1.25f&&
                decoded.world.levelCount==4&&decoded.world.maxBricks==2048&&decoded.world.bricksPerFrame==32&&decoded.world.maxTraceSteps==384,"World GI settings lost values during explicit save/reload");
            Check((decoded.regions[0].mode == "adaptive") == enabled && decoded.placement.minProbeSpacing == 0.125f
                && decoded.placement.maxProbeSpacing == 3.25f && decoded.placement.parentProbeMaxSize == 2.75f && decoded.placement.maxProbeCount == 90001
                && decoded.placement.maxProbeUpdatesPerFrame == 777 && decoded.placement.maxRaysPerFrame == 23456,
                "GI placement lost values during explicit save/reload");
            Check(decoded.regions.size() == 2 && decoded.regions[0].size.has_value() && !decoded.regions[0].gridDimensions.has_value()
                && decoded.regions[1].gridDimensions.has_value() && !decoded.regions[1].size.has_value()
                && decoded.regions[1].enabled == false, "saved derived grid replaced authored region mode");
            api.SaveReflectionProbeConfiguration();
			const auto reflectionAfter = authoring.Take();
            Check(reflectionAfter.size() == 1 && Vans::EncodeSerializedValueJson<nlohmann::json>(reflectionBefore)
                == Vans::EncodeSerializedValueJson<nlohmann::json>(reflectionAfter[0].value),
                "GI configuration changed reflection ownership");
        }
    }
}

bool TestGIProbeWorkContract(const Vans::VansSerializedValue& environment)
{
    try
    {
        RejectedFeedbackRetirement();
        FrameCostOwnership();
        MemoryPublicationOwnership();
        WorkPublicationOwnership();
        TextureLightingContentOwnership(); TextureRadianceMeasurement(); LightingNotificationBenchmark(); ScreenLightingHistoryOwnership(); PendingLightingPropagation(); IndependentLightingPublication(); ReceiverGeometryOwnership(); LightingResponseMagnitude(); LightingChangeOwnership(); LocalLightingEdits(); CompleteBudgetWork(); EditedPlacement(); ScrollingPlacement(); BalancedWorldSweeps(); WorldPrewarmBudget(); ActiveUpdateAges(); AllPlacedCoverage(); TracePolicyOwnership(); AmbientSkyBudgetOwnership(); ConfigurationOwnership(environment);
        std::cout << "[GIProbeWork] PASS: complete probe sequence, cost-aware budgets, native API controls, independent ownership and explicit save/reload\n";
        return true;
    }
    catch (const std::exception& error) { std::cerr << "[GIProbeWork] FAIL: " << error.what() << '\n'; return false; }
}
