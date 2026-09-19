#include "../EngineCore/RenderCore/GICore/VansGIProbeWorkScheduler.h"
#include "../EngineCore/RenderCore/GICore/VansGIScrollingGrid.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/EngineAPILayer/Private/ScenePropertyValueBuilders.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/SceneCore/VansSceneSaveService.h"
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


    void LocalLightingEdits()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{64,{0,2,4,6}},GIProbeWorkRegion{64,{1,3,5,7}}},8,512,error),"Lighting fixture configuration failed");
        auto first=scheduler.NextFrame();
        Check(!scheduler.InvalidateLighting(0,{2},error),"Lighting edit accepted pending feedback");
        for(size_t r=0;r<first.size();++r)for(auto entry:first[r].entries)
            Check(scheduler.ApplyFeedback(r,entry,{entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),"Lighting fixture feedback failed");
        const auto coverage=scheduler.RegionCoverage(0);
        Check(!scheduler.InvalidateLighting(0,{0,9},error) && !scheduler.InvalidateLighting(2,{0},error),"Invalid lighting address accepted");
        for(int i=0;i<40;++i)Check(scheduler.InvalidateLighting(0,{2,6,2},error),"Repeated local edit rejected");
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
                    Check(entry.flags==(dirty?GIWorkResetLighting:0u),"Color edit lost targeted reset, retained duplicate request or reset unrelated region");
                    Check(scheduler.ApplyFeedback(r,entry,{entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),"Local edit feedback failed");
                }
                Check(unique.size()==4,"Color edit changed full frame capacity");
            }
        }
        std::cout<<"[GIProbeWork] targeted lighting invalidation, duplicates, atomic rejection and indoor isolation PASS\n";
    }

    void CompleteBudgetWork()
    {
        std::vector<GIProbeWorkRegion> regions(3);
        regions[0].raysPerProbe = 256; regions[1].raysPerProbe = 1024; regions[2].raysPerProbe = 33;
        for (auto& region : regions) for (uint32_t i = 0; i < 97; ++i) region.probeIndices.push_back(i * 3u);
        VansGIProbeWorkScheduler scheduler; std::string error;
        Check(scheduler.Configure(regions, 9, 1536, error), "mixed complete probe configuration failed");
        std::array<std::map<uint32_t,uint32_t>,3> visits;
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
                    Check(entry.cycleIndex == ordinal && entry.flags == (ordinal == 0u ? GIWorkResetLighting : 0u),
                        "budget delay changed full-sphere rotation sequence");
                    Check(entry.elapsedSeconds > 0.0f && std::isfinite(entry.elapsedSeconds), "invalid temporal duration");
                    ++ordinal;
                    Check(scheduler.ApplyFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),
                        "complete lighting feedback rejected");
                    Check(!scheduler.ApplyFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}),
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
        scheduler.ResetLighting(); auto fresh = scheduler.NextFrame()[0].entries[0];
        Check(fresh.flags == GIWorkResetLighting && fresh.cycleIndex != old.cycleIndex, "reset reused stale sequence");
        Check(!scheduler.ApplyFeedback(0, old, {4,1,old.elapsedSeconds,old.cycleIndex}), "pre-reset feedback accepted");
    }

    void EditedPlacement()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({GIProbeWorkRegion{32,{1,4},4}},4,128,error),"Dynamic placement configuration failed");
        const auto issued=scheduler.NextFrame()[0].entries;
        Check(!scheduler.UpdatePlacedProbes(0,{1,2,4},{},error),"Placement changed before feedback retired");
        for(auto e:issued)Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Placement fixture feedback failed");
        Check(scheduler.UpdatePlacedProbes(0,{1,2,4},{4},error),"Localized placement update failed");
        auto work=scheduler.NextFrame()[0];Check(work.RayCount()==96 && scheduler.RegionCapacity(0)==4,"Dynamic placement exceeded or shrank its capacity");
        for(auto e:work.entries)
        {
            Check(e.flags==(e.probeIndex==1?0u:GIWorkResetLighting),"Placement lost unrelated history or failed to reset changed lighting");
            Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Edited placement feedback failed");
        }
        Check(scheduler.UpdatePlacedProbes(0,{}, {},error),"Removing all placed probes failed");
        Check(scheduler.NextFrame()[0].entries.empty() && scheduler.RegionCapacity(0)==4,"Empty active list lost reserved capacity");
        Check(scheduler.UpdatePlacedProbes(0,{1,2,3,4},{},error),"Digging terrain could not reactivate probes");
        for(auto e:scheduler.NextFrame()[0].entries)
        {
            Check(e.flags==GIWorkResetLighting && e.cycleIndex>work.entries.back().cycleIndex,"Reactivated address reused stale identity/history");
            Check(!scheduler.ApplyFeedback(0,issued[0],{issued[0].probeIndex,1,issued[0].elapsedSeconds,issued[0].cycleIndex}),"Retired terrain placement feedback accepted");
            Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Reactivated feedback failed");
        }
        Check(!scheduler.UpdatePlacedProbes(0,{0,1,2,3,4},{},error),"Placement overflow accepted");
        Check(scheduler.PlacedProbes(0)==std::vector<uint32_t>({1,2,3,4}),"Rejected placement mutated work addresses");
        GIProbeWorkRegion large;large.raysPerProbe=32;large.maxPlacedProbes=101;
        for(uint32_t i=0;i<100;++i)large.probeIndices.push_back(i);
        Check(scheduler.Configure({large},100,3200,error),"Priority fixture configure failed");
        for(auto e:scheduler.NextFrame()[0].entries)Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Priority initial completion failed");
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
            { old[e.probeIndex]=e;Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Scrolling initial feedback failed"); }
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
                Check(e.flags==(fresh?GIWorkResetLighting:0u),"Scroll reset unchanged probe or failed to reset new world position");
                const auto stale=old[e.probeIndex];
                Check(!scheduler.ApplyFeedback(0,stale,{stale.probeIndex,1,stale.elapsedSeconds,stale.cycleIndex}),"Scroll accepted feedback from old world identity");
                Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Scrolling feedback failed");
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
                    Check(scheduler.ApplyFeedback(r,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Balanced feedback failed");
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
                        Check(scheduler.ApplyFeedback(r,e,{e.probeIndex,GIProbeComplete,e.elapsedSeconds,e.cycleIndex}),"Prewarm feedback failed");
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
            {if(r==0 && ordinary)++*ordinary;Check(scheduler.ApplyFeedback(r,e,{e.probeIndex,GIProbeComplete,e.elapsedSeconds,e.cycleIndex}),"Late activation feedback failed");}
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
                Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,status,e.elapsedSeconds,e.cycleIndex},true),"Prewarm incomplete feedback failed");
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
            for(auto e:batch.entries)Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,ready?GIProbeComplete:GIProbeVoxelPage,e.elapsedSeconds,e.cycleIndex},true),
                "Readiness gate feedback failed");
        }
        Check(scheduler.RegionCoverage(0).updated==16,"Coarse publication failed to drain cold prewarm requests");
        late.prewarmSpacing=std::numeric_limits<float>::quiet_NaN();
        Check(!scheduler.Configure({late},1,32,error,true),"Invalid prewarm scale accepted");
        std::cout<<"[GIProbeWork] global prewarm: near plane baseline="<<baseline<<" frames prewarm="<<prewarm
            <<" frames; continuous movement retains indoor/far sweeps; late empty activation and tiny budget PASS\n";
    }

    void ActiveUpdateAges()
    {
        VansGIProbeWorkScheduler scheduler;std::string error;
        Check(scheduler.Configure({{32,{1,2,3},4}},1,32,error),"Age fixture configure failed");
        for(uint32_t frame=0;frame<9;++frame)
            for(const auto e:scheduler.NextFrame()[0].entries)
                Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,e.probeIndex==1?uint32_t(GIProbeVoxelBudget):uint32_t(GIProbeComplete),e.elapsedSeconds,e.cycleIndex},true),
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
            Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,1,e.elapsedSeconds,e.cycleIndex}),"Age fixture prewarm failed");
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.attempted==3 && coverage.updated==3 && coverage.oldestCompletionAge==2,
            "New placement age started at scene creation or lost retained completion history");
        std::cout<<"[GIProbeWork] active ages: unknown retries, success, removal and recycled identity PASS\n";
        for(uint32_t reason:{2u,4u,8u,16u,30u})
        {
            const auto e=scheduler.NextFrame()[0].entries[0];
            Check(!scheduler.ApplyFeedback(0,e,{e.probeIndex,reason,e.elapsedSeconds,e.cycleIndex}),"Hardware feedback accepted a world failure");
            Check(!scheduler.ApplyFeedback(0,e,{e.probeIndex,reason|1u,e.elapsedSeconds,e.cycleIndex},true),"Mixed success/failure feedback accepted");
            Check(!scheduler.ApplyFeedback(0,e,{e.probeIndex,32u,e.elapsedSeconds,e.cycleIndex},true),"Unknown failure bit accepted");
            Check(scheduler.ApplyFeedback(0,e,{e.probeIndex,reason,e.elapsedSeconds,e.cycleIndex},true),"World failure reason rejected");
        }
        coverage=scheduler.RegionCoverage(0);
        Check(coverage.heightFailures==2 && coverage.pageFailures==2 && coverage.stepFailures==5 && coverage.coverageFailures==2,
            "Failure reason accounting changed across placement or combined causes");
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
                    Check(scheduler.ApplyFeedback(0, entry, {entry.probeIndex,1u,entry.elapsedSeconds,entry.cycleIndex}),
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
                    Check(mixed.ApplyFeedback(r, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}), "mixed feedback rejected");
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
                if (resetEveryFrame) scheduler.ResetLighting();
                const auto& batch = scheduler.NextFrame()[0];
                Check(batch.entries.size() == 256 && batch.RayCount() == 65536, "DustV3 frame budget changed");
                if (frame == 0)
                    Check(scheduler.RegionCoverage(0).updated == 0, "issued work counted as GPU completion");
                for (const auto& entry : batch.entries)
                {
                    Check(entry.flags == (resetEveryFrame || frame < 256 ? GIWorkResetLighting : 0u), "lighting reset lost its pending work");
                    Check(scheduler.ApplyFeedback(0, entry, {entry.probeIndex,1,entry.elapsedSeconds,entry.cycleIndex}), "DustV3 completion rejected");
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
        Check(fresh.flags == GIWorkResetLighting && fresh.cycleIndex != dropped.cycleIndex, "lost completion suppressed retry");
        Check(!retry.ApplyFeedback(1,dropped,{4,1,dropped.elapsedSeconds,dropped.cycleIndex}), "stale completion accepted");
        Check(!retry.ApplyFeedback(1,fresh,{4,0,fresh.elapsedSeconds,fresh.cycleIndex}), "unfinished GPU work counted as complete");
        Check(retry.ApplyFeedback(1,fresh,{4,1,fresh.elapsedSeconds,fresh.cycleIndex}), "retry completion rejected");
        Check(retry.RegionCoverage(1).minCompletedUpdates == 1, "dropped work inflated coverage");
        retry.ResetLighting();
        const auto pending=retry.NextFrame()[1].entries[0];
        Check(retry.ApplyFeedback(1,pending,{4,0,pending.elapsedSeconds,pending.cycleIndex},true),"World unknown feedback was not consumed");
        Check(retry.RegionCoverage(1).minCompletedUpdates==1,"World unknown feedback inflated completion count");
        Check(retry.NextFrame()[1].entries[0].flags==GIWorkResetLighting,"World unknown feedback lost the reset request");
        std::cout << "[GIProbeWork] all placed repeated coverage, mixed-cost fairness, DustV3 65536 probes / 256-frame sweep, reset and completion retry PASS\n";
    }

    void ConfigurationOwnership(const Vans::VansSerializedValue& environment)
    {
        // 通过生产 API、文档编辑/保存与加载器验证，不复制一套设置投影。
        auto scene = std::make_unique<VansScene>();
        Vans::EditorAPI::EngineAPIImpl api(scene.get(), nullptr);
        struct Detach { Vans::EditorAPI::EngineAPIImpl& api; ~Detach() { api.BindRuntime(nullptr, nullptr); } } detach{api};
        VansGISettings initial;
        initial.regions.resize(2);
        initial.regions[0].size = {13.2f, 8.1f, 6.7f}; initial.regions[0].probeSpacing = 1.25f;
        initial.regions[0].center = {-7,-11,3}; initial.regions[0].overrideGridDimensions = false;
        initial.regions[1].stableId = 8; initial.regions[1].name = "Authored explicit grid";
        initial.regions[1].overrideGridDimensions = true; initial.regions[1].gridDimensions = {9,10,11};
        initial.regions[1].probeSpacing = 2.5f; initial.regions[1].enabled = false;
        scene->SetGISettings(initial);
        scene->ClearGIProbeResourcesDirty();
        api.SetGIProbeVisualization(true, true, 3);
        Check(scene->GetGISettings().showProbeGizmos && scene->GetGISettings().showProbeVolume &&
            scene->GetGISettings().gizmoStride == 3 && !scene->AreGIProbeResourcesDirty() &&
            GISettingsResourceLayoutEquals(initial, scene->GetGISettings()),
            "position display changed GI layout or requested resource recreation");
        Check(api.ConsumeScenePropertyEdits().empty() && !api.GetGIProbeDebugSnapshot(),
            "display controls staged a scene write or exposed positions before resources were ready");
        api.SetGIProbeVisualization(false, false, 0);
        Check(!scene->GetGISettings().showProbeGizmos && !scene->GetGISettings().showProbeVolume &&
            scene->GetGISettings().gizmoStride == 1 && !scene->AreGIProbeResourcesDirty(),
            "position display toggle or stride normalization failed");
        api.SaveReflectionProbeConfiguration();
        const auto reflectionEdits = api.ConsumeScenePropertyEdits();
        Check(reflectionEdits.size() == 1 && reflectionEdits[0].propertyPointer == "/settings/reflectionProbes",
            "reflection configuration saved outside the scene settings block");
        const auto reflectionBefore = Vans::EditorAPI::ScenePropertyValues::ToSerializedValue(reflectionEdits[0].value);

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
        for (bool enabled : {false, true, false})
        {
            const auto fingerprint = Vans::VansSceneDocumentLoader::Fingerprint(path);
            auto settings = api.GetGISettings();
            settings.world.enabled=enabled;settings.world.voxelSize=.5f;settings.world.coverageDistance=128;
            settings.regions[1].worldOnly=true;
            settings.regions[1].followView=true;
            settings.world.extinctionScale=1.25f;settings.world.levelCount=4;settings.world.maxBricks=2048;settings.world.bricksPerFrame=32;settings.world.maxTraceSteps=384;
            settings.placement.enabled = enabled;
            settings.placement.minProbeSpacing = 0.125f;
            settings.placement.maxProbeSpacing = 3.25f;
            settings.placement.parentProbeMaxSize = 2.75f;
            settings.placement.maxProbeCount = 90001;
            settings.placement.maxProbeUpdatesPerFrame = 777;
            settings.placement.maxRaysPerFrame = 23456;
            Check(api.ApplyGISettings(settings), "GI authoring settings Apply failed");
            Check(api.ConsumeScenePropertyEdits().empty(), "Apply GI implicitly staged a scene write");
            const auto& applied = scene->GetGISettings();
            Check(applied.regions[1].worldOnly&&!applied.regions[0].worldOnly,"World region scope did not pass the editor API");
            Check(applied.regions[1].followView&&!applied.regions[0].followView,"Scrolling mode did not pass the editor API");
            Check(applied.world.enabled==enabled&&applied.world.voxelSize==.5f&&applied.world.coverageDistance==128&&applied.world.extinctionScale==1.25f&&
                applied.world.levelCount==4&&applied.world.maxBricks==2048&&applied.world.bricksPerFrame==32&&applied.world.maxTraceSteps==384,"World GI controls did not reach native settings");
            Check(applied.placement.enabled == enabled && applied.placement.minProbeSpacing == 0.125f
                && applied.placement.maxProbeSpacing == 3.25f && applied.placement.parentProbeMaxSize == 2.75f && applied.placement.maxProbeCount == 90001
                && applied.placement.maxProbeUpdatesPerFrame == 777 && applied.placement.maxRaysPerFrame == 23456,
                "GI placement controls did not reach native settings");
            Check(!applied.regions[0].overrideGridDimensions && applied.regions[0].size == initial.regions[0].size
                && applied.regions[0].center == initial.regions[0].center && applied.regions[1].overrideGridDimensions
                && applied.regions[1].gridDimensions == initial.regions[1].gridDimensions && !applied.regions[1].enabled,
                "GI placement toggle modified the authored region definition");
            api.SaveGIConfiguration();
            const auto saved = api.ConsumeScenePropertyEdits();
            Check(saved.size() == 1 && saved[0].propertyPointer == "/settings/globalIllumination", "GI configuration save path is not loader-visible");
            Check(bool(edits.Set(Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, saved[0].propertyPointer),
                Vans::EditorAPI::ScenePropertyValues::ToSerializedValue(saved[0].value))), "GI configuration could not enter the scene document");
            Check(Vans::VansSceneDocumentLoader::Fingerprint(path) == fingerprint, "Apply or document staging wrote the file before Save Scene");
            Check(bool(Vans::VansSceneSaveService().Save(*document.document)), "explicit GI scene save failed");
            auto reloaded = Vans::VansSceneDocumentLoader::Load(path);
            Check(bool(reloaded), "saved GI document failed to reload");
            const auto json = Vans::EncodeSerializedValueJson<nlohmann::json>(reloaded.document->SerializedRootSnapshot());
            Check(!json.contains("globalIllumination") && !json.contains("reflectionProbes")
                && json["settings"]["reflectionProbes"]["placement"]["enabled"] == false, "GI save changed reflection settings or wrote a parallel root block");
            Vans::VansSceneRenderSettingsConfig config; std::string error;
            if (!Vans::VansSceneRenderSettingsConfigReader::Read(Vans::DecodeSerializedValueJson(json["settings"]), config, error))
                throw std::runtime_error("GI saved settings failed production decode: " + error);
            Check(config.globalIllumination.has_value(), "GI saved settings missing after production decode");
            const auto& decoded = *config.globalIllumination;
            Check(decoded.regions[1].worldOnly==true && decoded.regions[0].worldOnly==false,"World region scope lost during explicit save/reload");
            Check(decoded.regions[1].followView==true && decoded.regions[0].followView==false,"Scrolling mode lost during explicit save/reload");
            Check(decoded.world.enabled==enabled&&decoded.world.voxelSize==.5f&&decoded.world.coverageDistance==128&&decoded.world.extinctionScale==1.25f&&
                decoded.world.levelCount==4&&decoded.world.maxBricks==2048&&decoded.world.bricksPerFrame==32&&decoded.world.maxTraceSteps==384,"World GI settings lost values during explicit save/reload");
            Check(decoded.placement.enabled == enabled && decoded.placement.minProbeSpacing == 0.125f
                && decoded.placement.maxProbeSpacing == 3.25f && decoded.placement.parentProbeMaxSize == 2.75f && decoded.placement.maxProbeCount == 90001
                && decoded.placement.maxProbeUpdatesPerFrame == 777 && decoded.placement.maxRaysPerFrame == 23456,
                "GI placement lost values during explicit save/reload");
            Check(decoded.regions.size() == 2 && decoded.regions[0].size.has_value() && !decoded.regions[0].gridDimensions.has_value()
                && decoded.regions[1].gridDimensions.has_value() && !decoded.regions[1].size.has_value()
                && decoded.regions[1].enabled == false, "saved derived grid replaced authored region mode");
            api.SaveReflectionProbeConfiguration();
            const auto reflectionAfter = api.ConsumeScenePropertyEdits();
            Check(reflectionAfter.size() == 1 && Vans::EncodeSerializedValueJson<nlohmann::json>(reflectionBefore)
                == Vans::EncodeSerializedValueJson<nlohmann::json>(Vans::EditorAPI::ScenePropertyValues::ToSerializedValue(reflectionAfter[0].value)),
                "GI configuration changed reflection ownership");
        }
    }
}

bool TestGIProbeWorkContract(const Vans::VansSerializedValue& environment)
{
    try
    {
        LocalLightingEdits(); CompleteBudgetWork(); EditedPlacement(); ScrollingPlacement(); BalancedWorldSweeps(); WorldPrewarmBudget(); ActiveUpdateAges(); AllPlacedCoverage(); ConfigurationOwnership(environment);
        std::cout << "[GIProbeWork] PASS: complete probe sequence, cost-aware budgets, native API controls, independent ownership and explicit save/reload\n";
        return true;
    }
    catch (const std::exception& error) { std::cerr << "[GIProbeWork] FAIL: " << error.what() << '\n'; return false; }
}
