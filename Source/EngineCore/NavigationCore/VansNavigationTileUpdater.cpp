#include "VansNavigationTileUpdater.h"
#include "VansNavigationBuilder.h"
#include "VansNavigationBuildContext.h"
#include "Detail/VansNavigationBuildInputMemory.h"
#include "../Util/VansJobSystem.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string_view>

namespace Vans
{
namespace
{
std::uint64_t SettingsBytes(const VansNavigationSettings& settings)
{
    std::uint64_t bytes=sizeof(settings)+settings.profiles.capacity()*sizeof(VansNavigationAgentProfile)+
        settings.maps.capacity()*sizeof(VansNavigationMapSettings)+settings.areas.definitions.capacity()*sizeof(VansNavigationAreaDefinition)+
        settings.queryFilters.capacity()*sizeof(VansNavigationQueryFilterSettings)+settings.areas.defaultArea.capacity()+1;
    for(const auto& profile:settings.profiles) bytes+=profile.id.capacity()+1;
    for(const auto& map:settings.maps)
    { bytes+=map.id.capacity()+1+map.profiles.capacity()*sizeof(std::string); for(const auto& id:map.profiles) bytes+=id.capacity()+1; }
    for(const auto& area:settings.areas.definitions) bytes+=area.name.capacity()+1;
    for(const auto& filter:settings.queryFilters)
    {
        bytes+=filter.id.capacity()+1+filter.excludedAreas.capacity()*sizeof(std::string)+
            filter.areaCosts.capacity()*sizeof(std::pair<std::string,float>);
        for(const auto& id:filter.excludedAreas) bytes+=id.capacity()+1;
        for(const auto& cost:filter.areaCosts) bytes+=cost.first.capacity()+1;
    }
    return bytes;
}
std::uint64_t LayerBytes(const std::vector<VansNavigationTileLayerData>& rows)
{
    std::uint64_t bytes=rows.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& row:rows) bytes+=row.EstimateBytes()-sizeof(row);
    return bytes;
}

bool SameFootprint(const VansNavigationBuildFootprint& a,const VansNavigationBuildFootprint& b)
{ return a.contentHash==b.contentHash && a.boundsMinimum==b.boundsMinimum && a.boundsMaximum==b.boundsMaximum; }
bool ValidFootprints(const VansNavigationProfileBuildInput& input)
{
    if(input.footprints.size()>267264) return false;
    std::string_view previous;
    for(const auto& footprint:input.footprints)
    {
        if(footprint.id.empty() || footprint.id.size()>512 || footprint.id<=previous || !footprint.contentHash) return false;
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(footprint.boundsMinimum[axis]) || !std::isfinite(footprint.boundsMaximum[axis]) ||
                footprint.boundsMinimum[axis]>footprint.boundsMaximum[axis]) return false;
        previous=footprint.id;
    }
    return true;
}
bool AddCoverage(const VansNavigationBuildFootprint& footprint,const VansNavigationBakeSettings& bake,
    std::set<VansNavigationTileCell>& cells,std::string& error)
{
    const double width=static_cast<double>(bake.cellSize)*bake.tileSizeCells;
    const double border=(std::ceil(static_cast<double>(bake.agentRadius)/bake.cellSize)+3)*bake.cellSize;
    const double lowX=std::floor((footprint.boundsMinimum.x-border-bake.tileOrigin.x)/width);
    const double highX=std::floor((footprint.boundsMaximum.x+border-bake.tileOrigin.x)/width);
    const double lowZ=std::floor((footprint.boundsMinimum.z-border-bake.tileOrigin.z)/width);
    const double highZ=std::floor((footprint.boundsMaximum.z+border-bake.tileOrigin.z)/width);
    constexpr int limit=1<<28;
    if(!std::isfinite(lowX) || !std::isfinite(highX) || !std::isfinite(lowZ) || !std::isfinite(highZ) ||
        lowX< -limit || highX>limit || lowZ< -limit || highZ>limit ||
        (highX-lowX+1)*(highZ-lowZ+1)>bake.maximumTiles)
    { error="Navigation changed contribution exceeds fixed-grid cell capacity"; return false; }
    for(int z=static_cast<int>(lowZ);z<=static_cast<int>(highZ);++z)
        for(int x=static_cast<int>(lowX);x<=static_cast<int>(highX);++x)
        {
            const VansNavigationTileCell cell{x,z};
            if(cells.size()>=static_cast<std::size_t>(bake.maximumTiles) && !cells.count(cell))
            { error="Navigation changed contribution union exceeds Map cell capacity"; return false; }
            cells.insert(cell);
        }
    return true;
}
}

struct VansNavigationTileUpdater::State
{
    // Task 中只含拥有型 CPU 值；撤销 Map 不会让 worker 持有原 Scene 或 native 网格。
    struct Task
    {
        std::shared_ptr<const VansNavigationProfileBuildInput> input;
        std::shared_ptr<VansNavigationBuildContext> context;
        VansNavigationSettings settings;
        std::string mapId,error;
        std::uint64_t hash=0,maximumBytes=0,inputBytes=0,contextBytesAtStart=0;
        VansNavigationTileUpdate update;
        VansNavigationBuildControl control;
        std::shared_ptr<VansJobGroup> group;
        VansNavigationTileBuildOutput output;
        VansNavigationLayerRead layerRead;
        std::vector<VansNavigationTileLayerData> seedLayers;
        bool success=false;
        void Execute()
        {
            if(control.IsCancelled()) { error="Navigation layer read cancelled"; return; }
            std::uint64_t baseHash=0;
            if(!ComputeNavigationBuildBaseHash(*input,baseHash,error) ||
                !layerRead.Read(baseHash,&control,seedLayers,error)) return;
            std::vector<VansNavigationTileCell> cells; cells.reserve(update.cells.size());
            for(const auto& stamp:update.cells) cells.push_back(stamp.cell);
            success=VansNavigationBuilder::BuildSelectedTiles(*input,settings,mapId,cells,
                update.rasterOriginMeters,maximumBytes-layerRead.EstimateBytes(),&control,output,error,context.get(),&seedLayers);
        }
    };
    struct Session
    {
        std::shared_ptr<VansNavigationMap> map;
        std::shared_ptr<const VansNavigationProfileBuildInput> input;
        std::shared_ptr<Task> task;
        std::shared_ptr<VansNavigationBuildContext> context;
        std::uint64_t hash=0,failedHash=0;
        std::string diagnostic;
    };
    VansNavigationSettings settings;
    std::map<const VansNavigationMap*,Session> sessions;
    std::vector<std::shared_ptr<Task>> tasks;
    const VansNavigationMap* lastStarted=nullptr;
    std::uint64_t startedBuilds=0,publishedCells=0;
    bool configured=false;

    std::uint64_t ObservationReserve() const
    {
        VansNavigationBuildInputMemory memory; std::uint64_t metadata=0;
        for(const auto& [key,session]:sessions)
        {
            memory.Add(*session.input);
            const auto bake=session.map->GetBakeSettings();
            if(bake) metadata+=static_cast<std::uint64_t>(bake->maximumTiles)*sizeof(VansNavigationTileInputStamp)*
                (static_cast<std::uint64_t>(settings.query.maximumActiveQueries)+4u);
        }
        // worker 未退出时仍能接收下一份源快照与 Dirty policy，不能占满主线程观察空间。
        return memory.bytes+metadata;
    }

    VansNavigationTileUpdateStats Stats() const
    {
        VansNavigationTileUpdateStats result;
        result.startedBuilds=startedBuilds; result.publishedCells=publishedCells;
        VansNavigationBuildInputMemory memory;
        result.retainedBytes=sizeof(State)+SettingsBytes(settings)+sessions.size()*(sizeof(Session)+96)+tasks.capacity()*sizeof(std::shared_ptr<Task>);
        std::set<const Task*> attached;
        std::set<const VansNavigationBuildContext*> contexts;
        std::set<const VansNavigationMeshAsset*> assets;
        const auto accountContext=[&](const std::shared_ptr<VansNavigationBuildContext>& context,bool building,std::uint64_t atStart)
        {
            if(!context || !contexts.insert(context.get()).second) return;
            const auto stats=context->CaptureStats();
            const auto bytes=building?atStart:stats.retainedBytes;
            result.retainedBytes+=bytes; result.cachedContextBytes+=bytes;
            result.baseCellsBuilt+=stats.baseCellsBuilt; result.reusedCells+=stats.reusedCells; result.evictedCells+=stats.evictedCells;
        };
        for(const auto& [key,session]:sessions)
        {
            memory.Add(*session.input);
            assets.insert(session.map->GetAsset().get());
            result.retainedBytes+=session.diagnostic.capacity()+1;
            if(session.task) attached.insert(session.task.get());
            const bool building=session.task && session.task->group && !session.task->group->IsComplete();
            accountContext(session.context,building,session.task?session.task->contextBytesAtStart:0);
            if(session.failedHash==session.hash && session.failedHash) ++result.failedMaps;
            if(session.map->GetPendingCellCount()) ++result.pendingMaps;
        }
        for(const auto& task:tasks)
        {
            const bool complete=task->group && task->group->IsComplete();
            if(task->input) memory.Add(*task->input);
            accountContext(task->context,!complete,task->contextBytesAtStart);
            const auto taskBytes=sizeof(Task)+SettingsBytes(task->settings)+task->mapId.capacity()+1+
                task->update.cells.capacity()*sizeof(VansNavigationTileInputStamp)+1024+
                task->layerRead.EstimateBytes()-sizeof(VansNavigationLayerRead);
            result.retainedBytes+=taskBytes; result.concurrentBuildBytes+=taskBytes;
            if(task->layerRead.asset && assets.insert(task->layerRead.asset.get()).second)
                result.retainedBytes+=task->layerRead.asset->EstimateResidentBytes();
            // 未完成的 worker 输出不可读；整个 scratch reservation 持有至完成通知。
            if(!complete)
            {
                result.retainedBytes+=task->maximumBytes-task->inputBytes;
                result.concurrentBuildBytes+=task->maximumBytes;
                if(attached.count(task.get())) ++result.activeBuilds; else ++result.retiringBuilds;
            }
            else
            {
                result.retainedBytes+=task->output.EstimateBytes()+LayerBytes(task->seedLayers)+task->error.capacity()+1;
                result.concurrentBuildBytes+=task->output.EstimateBytes()+LayerBytes(task->seedLayers);
            }
        }
        result.retainedBytes+=memory.bytes; return result;
    }
};

VansNavigationTileUpdater::VansNavigationTileUpdater():m_State(std::make_unique<State>()) {}
VansNavigationTileUpdater::~VansNavigationTileUpdater() { Shutdown(); }
bool VansNavigationTileUpdater::Configure(const VansNavigationSettings& settings,std::string& error)
{
    if(!ValidateNavigationSettings(settings,error)) return false;
    auto candidate=settings;
    RetainMaps({}); m_State->settings=std::move(candidate); m_State->configured=true;
    error.clear(); return true;
}
void VansNavigationTileUpdater::RetainMaps(const std::vector<std::shared_ptr<const VansNavigationMap>>& maps)
{
    for(auto iterator=m_State->sessions.begin();iterator!=m_State->sessions.end();)
    {
        if(std::any_of(maps.begin(),maps.end(),[&](const auto& map){return map.get()==iterator->first;})) { ++iterator; continue; }
        if(iterator->second.task) iterator->second.task->control.Cancel();
        iterator=m_State->sessions.erase(iterator);
    }
    m_State->tasks.erase(std::remove_if(m_State->tasks.begin(),m_State->tasks.end(),[](const auto& task)
        {return task->control.IsCancelled() && task->group && task->group->IsComplete();}),m_State->tasks.end());
}
void VansNavigationTileUpdater::WithdrawMap(const std::string& mapId)
{
    for(auto iterator=m_State->sessions.begin();iterator!=m_State->sessions.end();)
    {
        if(iterator->second.map->GetAsset()->GetMapId()!=mapId) { ++iterator; continue; }
        if(iterator->second.task) iterator->second.task->control.Cancel();
        iterator=m_State->sessions.erase(iterator);
    }
}
bool VansNavigationTileUpdater::Observe(const std::shared_ptr<VansNavigationMap>& map,
    std::shared_ptr<const VansNavigationProfileBuildInput> input,std::uint64_t hash,std::string& error)
try
{
    if(!m_State->configured || !map || !map->IsReady() || !input || !input->geometry || !hash ||
        input->profileId!=map->GetProfileId() || !ValidFootprints(*input))
    { error="Navigation runtime update has invalid Map or frozen input"; return false; }
    const auto asset=map->GetAsset(); const auto* config=FindNavigationMap(m_State->settings,asset->GetMapId());
    const auto bake=map->GetBakeSettings();
    if(!config || config->updateMode!=VansNavigationMapUpdateMode::Runtime || !bake)
    { error="Navigation geometry updates require a configured Runtime Map"; return false; }
    const auto existing=m_State->sessions.find(map.get());
    if(existing!=m_State->sessions.end() && existing->second.hash==hash) { error.clear(); return true; }
    std::set<VansNavigationTileCell> changed;
    if(existing==m_State->sessions.end())
    {
        if(hash!=asset->FindProfile(input->profileId)->inputHash)
        {
            // 仅首次冷加载失配需要覆盖旧 cooked cell；后续观察只比较有效贡献范围。
            std::vector<VansNavigationTileCell> resident;
            if(!map->CaptureResidentCells(resident,error)) return false;
            changed.insert(resident.begin(),resident.end());
            for(const auto& item:input->footprints) if(!AddCoverage(item,*bake,changed,error)) return false;
        }
    }
    else
    {
        const auto& before=existing->second.input->footprints; const auto& after=input->footprints;
        std::size_t left=0,right=0;
        while(left<before.size() || right<after.size())
        {
            if(right==after.size() || (left<before.size() && before[left].id<after[right].id))
            { if(!AddCoverage(before[left++],*bake,changed,error)) return false; }
            else if(left==before.size() || after[right].id<before[left].id)
            { if(!AddCoverage(after[right++],*bake,changed,error)) return false; }
            else
            {
                if(!SameFootprint(before[left],after[right]) &&
                    (!AddCoverage(before[left],*bake,changed,error) || !AddCoverage(after[right],*bake,changed,error))) return false;
                ++left; ++right;
            }
        }
    }
    // 先准备可失败的 owner 分配，再标记 Dirty；发布后移动 shared_ptr 不再分配。
    std::vector<VansNavigationTileCell> changedCells(changed.begin(),changed.end());
    std::shared_ptr<VansNavigationBuildContext> context;
    if(existing==m_State->sessions.end())
        context=std::make_shared<VansNavigationBuildContext>(static_cast<std::uint64_t>((std::min)(
            config->maximumBuildBytes,m_State->settings.maximumResidentBytes/4)));
    auto [iterator,inserted]=m_State->sessions.try_emplace(map.get());
    if(!map->InvalidateTiles(changedCells,error))
    { if(inserted) m_State->sessions.erase(iterator); return false; }
    auto& session=iterator->second; if(context) session.context=std::move(context); session.map=map; session.input=std::move(input); session.hash=hash;
    session.failedHash=0; session.diagnostic.clear(); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation runtime update observation allocation failed"; return false; }

VansNavigationTileUpdateStats VansNavigationTileUpdater::CaptureStats() const { return m_State->Stats(); }
bool VansNavigationTileUpdater::Tick(std::uint64_t externalBytes,std::uint64_t externalTiles,std::string& error)
try
{
    auto& state=*m_State; error.clear();
    if(!state.configured) { error="Navigation tile updater is not configured"; return false; }
    // 已退出 Map 的任务只保留 CPU 数据，完成后回收；这里不等待。
    state.tasks.erase(std::remove_if(state.tasks.begin(),state.tasks.end(),[](const auto& task)
        {return task->control.IsCancelled() && task->group && task->group->IsComplete();}),state.tasks.end());
    const auto remember=[&](const std::string& message){if(error.empty()) error=message;};
    auto iterator=state.sessions.upper_bound(state.lastStarted);
    for(std::size_t visited=0;visited<state.sessions.size();++visited)
    {
        if(iterator==state.sessions.end()) iterator=state.sessions.begin();
        const auto key=iterator->first; auto& session=iterator->second; ++iterator;
        if(session.task && session.task->group->IsComplete())
        {
            const auto task=session.task;
            task->input.reset(); // acquire 完成通知后，worker 已不会再访问快照。
            std::vector<VansNavigationTileLayerData>().swap(task->seedLayers);
            task->layerRead={};
            if(!task->success || task->group->HasErrors())
            {
                if(task->hash==session.hash)
                {
                    session.failedHash=task->hash;
                    session.diagnostic=task->group->HasErrors()?task->group->GetError():task->error;
                    if(session.diagnostic.empty()) session.diagnostic="Navigation local build failed";
                }
            }
            else
            {
                const auto stats=state.Stats(); const auto payloadBytes=task->output.EstimateBytes();
                const auto maximum=static_cast<std::uint64_t>(state.settings.maximumResidentBytes);
                if(externalBytes+stats.retainedBytes>maximum ||
                    externalTiles+task->output.tiles.size()>static_cast<std::uint64_t>(state.settings.maximumResidentTiles))
                { remember("Navigation Tile publication waits for aggregate resident capacity"); continue; }
                const auto beforeBytes=session.map->EstimateResidentBytes(),beforeTiles=session.map->GetResidentTileCount();
                std::string commitError;
                const auto committed=session.map->ApplyTileUpdate(task->update,task->output,
                    maximum-externalBytes-stats.retainedBytes+payloadBytes,commitError);
                if(committed.state==VansNavigationTileCommitState::Deferred) continue;
                if(committed.state==VansNavigationTileCommitState::Applied)
                {
                    state.publishedCells+=committed.appliedCells;
                    externalBytes=externalBytes-beforeBytes+session.map->EstimateResidentBytes();
                    externalTiles=externalTiles-beforeTiles+session.map->GetResidentTileCount();
                }
                else if(committed.state!=VansNavigationTileCommitState::Stale && task->hash==session.hash)
                { session.failedHash=task->hash; session.diagnostic=std::move(commitError); }
            }
            session.task.reset();
            state.tasks.erase(std::remove(state.tasks.begin(),state.tasks.end(),task),state.tasks.end());
        }
        if(session.failedHash==session.hash && session.failedHash) { remember(session.diagnostic); continue; }
        if(session.task) continue;
        VansNavigationTileUpdate update; std::string captureError;
        if(!session.map->CaptureTileUpdate(update,captureError)) { remember(captureError); continue; }
        if(update.cells.empty()) continue;
        const auto stats=state.Stats();
        VansNavigationBuildInputMemory memory; memory.Add(*session.input);
        const auto* config=FindNavigationMap(state.settings,session.map->GetAsset()->GetMapId());
        const auto buildLimit=static_cast<std::uint64_t>((std::min)(config->maximumBuildBytes,state.settings.maximumConcurrentBuildBytes));
        const auto maximumResident=static_cast<std::uint64_t>(state.settings.maximumResidentBytes);
        const auto maximumConcurrent=static_cast<std::uint64_t>(state.settings.maximumConcurrentBuildBytes);
        const auto taskOverhead=sizeof(State::Task)+SettingsBytes(state.settings)+config->id.capacity()+1+
            update.cells.capacity()*sizeof(VansNavigationTileInputStamp)+4096;
        if(memory.bytes>=buildLimit)
        { session.failedHash=session.hash; session.diagnostic="Navigation frozen input exceeds local build capacity"; remember(session.diagnostic); continue; }
        if(externalBytes+stats.retainedBytes>=maximumResident || stats.concurrentBuildBytes+memory.bytes+taskOverhead>=maximumConcurrent) continue;
        const auto overhead=taskOverhead+state.ObservationReserve();
        if(maximumResident-externalBytes-stats.retainedBytes<=overhead) continue;
        const auto scratch=(std::min)({buildLimit-memory.bytes,maximumResident-externalBytes-stats.retainedBytes-overhead,
            maximumConcurrent-stats.concurrentBuildBytes-memory.bytes-taskOverhead});
        // 小于最小可用 scratch 时保留 Dirty，等待其他任务真正退出释放容量。
        if(scratch<64u*1024u) continue;
        if(!VansJobSystem::Get().IsRunning())
        { remember("Navigation Tile update requires the running worker service"); continue; }
        auto task=std::make_shared<State::Task>(); task->input=session.input;
        if(!session.map->CaptureLayerRead(update,scratch/4u,task->layerRead,captureError))
        { remember(captureError); continue; }
        task->inputBytes=memory.bytes+task->layerRead.EstimateBytes();
        task->context=session.context; task->contextBytesAtStart=session.context->CaptureStats().retainedBytes;
        task->settings=state.settings; task->mapId=config->id; task->hash=session.hash;
        task->maximumBytes=memory.bytes+scratch; task->update=std::move(update);
        state.tasks.push_back(task);
        try { task->group=VansJobSystem::Get().QueueJobGroup({[task](){task->Execute();}}); }
        catch(...) { state.tasks.pop_back(); throw; }
        session.task=std::move(task); state.lastStarted=key; ++state.startedBuilds;
    }
    return error.empty();
}
catch(const std::bad_alloc&) { error="Navigation Tile update scheduling allocation failed"; return false; }
void VansNavigationTileUpdater::Shutdown()
{
    for(const auto& task:m_State->tasks) task->control.Cancel();
    m_State->sessions.clear();
    for(const auto& task:m_State->tasks) if(task->group) task->group->Wait();
    m_State->tasks.clear(); m_State->configured=false;
}
}
