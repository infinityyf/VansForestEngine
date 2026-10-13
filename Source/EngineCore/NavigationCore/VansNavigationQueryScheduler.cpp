#include "VansNavigationQueryScheduler.h"
#include "VansNavigationMap.h"
#include "../Util/VansJobSystem.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans
{
namespace
{
bool Finite(const glm::vec3& value) { return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z); }
bool Pending(VansNavigationQueryLifecycle state)
{ return state==VansNavigationQueryLifecycle::Queued || state==VansNavigationQueryLifecycle::Running ||
    state==VansNavigationQueryLifecycle::WaitingForData; }
}
struct VansNavigationQueryScheduler::Flight
{
    VansNavigationQueryHandle handle;
    VansNavigationQueryRequest request;
    VansNavigationQuerySettings settings;
    std::shared_ptr<const VansNavigationMap> map;
    std::shared_ptr<VansNavigationQueryContext> context;
    std::shared_ptr<VansJobGroup> group;
    glm::vec3 goal{0};
    bool begin=false;
    bool initializationFailed=false;
    int maximumIterations=0,iterationsUsed=0;
    VansNavigationPath path;
    std::shared_ptr<const VansNavigationCorridorRoute> route;
    VansNavigationPointProjection projection;
    VansNavigationRaycast raycast;
    VansNavigationBoundary boundary;
    std::uint64_t observedTraversalRevision=0;

    void Execute()
    {
        observedTraversalRevision=map->GetTraversalRevision();
        std::string error;
        if(!context)
        {
            context=std::make_shared<VansNavigationQueryContext>();
            if(!context->Initialize(map,settings,request.filterId,error) || !context->SetLinkCapabilities(request.linkCapabilities,error))
            { initializationFailed=true; path.status=VansNavigationPathStatus::Failed;
                path.failure=VansNavigationPathFailure::QueryNotReady; path.diagnostic=error; return; }
        }
        if(request.kind==VansNavigationQueryKind::PointProjection)
        { projection=context->ProjectPoint(request.start,request.nearestExtents); return; }
        if(request.kind==VansNavigationQueryKind::Raycast)
        {
            if(begin) raycast=context->BeginRaycast(request.start,request.goal,request.nearestExtents);
            if(!raycast.finished) raycast=context->AdvanceRaycast(maximumIterations,iterationsUsed);
            return;
        }
        if(request.kind==VansNavigationQueryKind::NearestBoundary)
        {
            if(begin) boundary=context->BeginNearestBoundary(request.start,request.radius,request.nearestExtents);
            if(!boundary.finished) boundary=context->AdvanceNearestBoundary(maximumIterations,iterationsUsed);
            return;
        }
        if(begin)
        {
            path=context->BeginPath(request.start,goal,request.nearestExtents,request.goalAcceptance,request.goalProjectionToleranceMeters);
            if(path.failure==VansNavigationPathFailure::MeshChanged && context->Initialize(map,settings,request.filterId,error) &&
                context->SetLinkCapabilities(request.linkCapabilities,error))
                path=context->BeginPath(request.start,goal,request.nearestExtents,request.goalAcceptance,request.goalProjectionToleranceMeters);
        }
        if(path.status==VansNavigationPathStatus::None)
            path=context->AdvancePath(maximumIterations,iterationsUsed);
        path.hasRequestCoordinates=true; path.requestedStart=request.start; path.requestedEnd=goal; path.goalAcceptance=request.goalAcceptance;
        if(path.status==VansNavigationPathStatus::Complete || path.status==VansNavigationPathStatus::Partial)
        {
            route=context->CaptureCorridorRoute();
            if(!route)
            { path.status=VansNavigationPathStatus::Failed; path.failure=VansNavigationPathFailure::MeshChanged;
                path.diagnostic="Tile validity changed before Corridor capture"; }
        }
    }
};
void VansNavigationQueryScheduler::Initialize(const VansNavigationQuerySettings& settings,std::uint64_t session)
{ Shutdown(); m_Settings=settings; m_Session=session; m_Flights.reserve(static_cast<std::size_t>(settings.maximumActiveQueries)); }
void VansNavigationQueryScheduler::Shutdown()
{
    // 卸载时收回独占 worker 上下文；普通帧仅轮询，绝不等待工作组。
    for(const auto& flight:m_Flights) flight->group->Wait();
    m_Flights.clear();
    for(auto& [id,task]:m_Tasks) if(task.context && Pending(task.snapshot.lifecycle)) task.context->ResetQuery();
    m_Tasks.clear(); m_History.clear(); m_Session=0; m_NextSerial=0; m_Frame=0; m_Cursor=0;
    m_IterationsLastFrame=0; m_StartsLastFrame=0;
}
VansNavigationQueryHandle VansNavigationQueryScheduler::Submit(const VansNavigationQueryRequest& request,
    const std::string& owner,std::shared_ptr<const VansNavigationMap> map,VansEntityHandle entity,
    std::string& error)
try
{
    if(!m_Session) { error="Navigation query scheduler is not initialized"; return {}; }
    if(!ValidateNavigationGoalProjection(request.goalAcceptance,request.goalProjectionToleranceMeters,error)) return {};
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities> capabilities{};
    if(!CompileNavigationLinkCapabilities(request.linkCapabilities,capabilities,error)) return {};
    if(owner.empty() || owner.size()>256 || !Finite(request.start) || !Finite(request.goal) || !Finite(request.nearestExtents) ||
        (request.kind!=VansNavigationQueryKind::Path && request.kind!=VansNavigationQueryKind::ReachablePoint &&
            request.kind!=VansNavigationQueryKind::PointProjection && request.kind!=VansNavigationQueryKind::Raycast &&
            request.kind!=VansNavigationQueryKind::NearestBoundary) ||
        glm::any(glm::lessThanEqual(request.nearestExtents,glm::vec3(0))) ||
        request.priority<0 || request.priority>3 || (request.kind==VansNavigationQueryKind::ReachablePoint &&
        (!std::isfinite(request.radius) || request.radius<=0 || request.maximumAttempts<1 || request.maximumAttempts>64)))
    { error="Navigation query contains invalid owner, coordinates, bounds or sampling budget"; return {}; }
    if((request.kind==VansNavigationQueryKind::NearestBoundary || request.kind==VansNavigationQueryKind::ReachablePoint) &&
        !ValidateNavigationSpatialDistance(request.radius,error)) return {};
    if(request.kind==VansNavigationQueryKind::Raycast &&
        !ValidateNavigationSpatialDistance(glm::length(glm::dvec3(request.goal)-glm::dvec3(request.start)),error)) return {};
    if(!map || !map->IsReady() || map->GetProfileId()!=request.profileId || map->GetAsset()->GetMapId()!=request.mapId)
    { error="MapUnavailable"; return {}; }
    if(!map->HasFilter(request.filterId)) { error="FilterInvalid"; return {}; }
    const auto pending=std::count_if(m_Tasks.begin(),m_Tasks.end(),[](const auto& entry){return Pending(entry.second.snapshot.lifecycle);});
    if(pending>=m_Settings.maximumQueuedQueries) { error="QueueFull"; return {}; }
    if(m_NextSerial==(std::numeric_limits<std::uint64_t>::max)()) { error="Navigation query identity exhausted"; return {}; }
    Task task; task.request=request; task.owner=owner; task.entity=entity; task.map=std::move(map);
    task.submittedFrame=m_Frame; task.snapshot.handle={m_Session,m_NextSerial+1}; task.snapshot.kind=request.kind;
    task.snapshot.mapId=request.mapId; task.snapshot.profileId=request.profileId; task.snapshot.filterId=request.filterId;
    if(request.kind==VansNavigationQueryKind::Path || request.kind==VansNavigationQueryKind::ReachablePoint)
    { task.snapshot.path.hasRequestCoordinates=true; task.snapshot.path.requestedStart=request.start;
        task.snapshot.path.requestedEnd=request.goal; task.snapshot.path.goalAcceptance=request.goalAcceptance; }
    const auto handle=task.snapshot.handle;
    m_Tasks.emplace(handle.serial,std::move(task)); ++m_NextSerial; error.clear(); return handle;
}
catch(const std::bad_alloc&) { error="Navigation query allocation failed"; return {}; }
VansNavigationQueryScheduler::Task* VansNavigationQueryScheduler::Resolve(VansNavigationQueryHandle handle,const std::string& owner,std::string& error)
{
    const auto found=m_Tasks.find(handle.serial);
    if(!handle.IsValid() || handle.session!=m_Session || found==m_Tasks.end())
    { error="Navigation query handle is stale"; return nullptr; }
    if(owner.empty() || found->second.owner!=owner) { error="OwnerConflict"; return nullptr; }
    error.clear(); return &found->second;
}
std::optional<VansNavigationQuerySnapshot> VansNavigationQueryScheduler::Capture(VansNavigationQueryHandle handle,
    const std::string& owner,std::string& error) const
{
    const auto found=m_Tasks.find(handle.serial);
    if(!handle.IsValid() || handle.session!=m_Session || found==m_Tasks.end()) { error="Navigation query handle is stale"; return {}; }
    if(owner.empty() || found->second.owner!=owner) { error="OwnerConflict"; return {}; }
    error.clear(); return found->second.snapshot;
}
void VansNavigationQueryScheduler::Finish(Task& task,VansNavigationPathFailure failure)
{
    if(!Pending(task.snapshot.lifecycle)) return;
    if(failure!=VansNavigationPathFailure::None)
    {
        task.snapshot.route.reset(); task.snapshot.point.reset(); task.snapshot.path.points.clear();
        task.snapshot.raycast.reset(); task.snapshot.boundary.reset();
        task.snapshot.path.cornerFlags.clear(); task.snapshot.path.tiles.clear();
        if(task.context && !task.flight) task.context->ResetQuery();
        task.snapshot.path.status=VansNavigationPathStatus::Failed; task.snapshot.path.failure=failure;
        task.snapshot.path.diagnostic=failure==VansNavigationPathFailure::Cancelled?"Query cancelled":
            failure==VansNavigationPathFailure::QueryDeadlineExceeded?"Query frame budget exceeded":
            failure==VansNavigationPathFailure::MapUnavailable?"Map unavailable":"No reachable sample within attempt budget";
    }
    task.searching=false;
    task.snapshot.lifecycle=failure==VansNavigationPathFailure::Cancelled?VansNavigationQueryLifecycle::Cancelled:VansNavigationQueryLifecycle::Finished;
    task.context.reset(); task.map.reset(); task.flight.reset();
    m_History.push_back(task.snapshot.handle.serial);
}
void VansNavigationQueryScheduler::PruneHistory()
{
    while(m_History.size()>static_cast<std::size_t>(m_Settings.maximumQueryHistory))
    { m_Tasks.erase(m_History.front()); m_History.pop_front(); }
}
bool VansNavigationQueryScheduler::Cancel(VansNavigationQueryHandle handle,const std::string& owner,std::string& error)
{ auto* task=Resolve(handle,owner,error); if(!task) return false; Finish(*task,VansNavigationPathFailure::Cancelled); PruneHistory(); return true; }
bool VansNavigationQueryScheduler::Release(VansNavigationQueryHandle handle,const std::string& owner,std::string& error)
{
    auto* task=Resolve(handle,owner,error); if(!task) return false;
    if(task->context && !task->flight && Pending(task->snapshot.lifecycle)) task->context->ResetQuery();
    m_Tasks.erase(handle.serial);
    m_History.erase(std::remove(m_History.begin(),m_History.end(),handle.serial),m_History.end()); return true;
}
void VansNavigationQueryScheduler::CancelOwner(const std::string& owner)
{ if(owner.empty()) return; for(auto& [id,task]:m_Tasks) if(task.owner==owner) Finish(task,VansNavigationPathFailure::Cancelled); PruneHistory(); }
void VansNavigationQueryScheduler::CancelEntity(VansEntityHandle entity)
{ if(!entity.IsValid()) return; for(auto& [id,task]:m_Tasks) if(task.entity==entity) Finish(task,VansNavigationPathFailure::Cancelled); PruneHistory(); }
void VansNavigationQueryScheduler::CancelMap(const VansNavigationMap* map)
{ for(auto& [id,task]:m_Tasks) if(task.map.get()==map) Finish(task,VansNavigationPathFailure::MapUnavailable); PruneHistory(); }
VansNavigationQuerySchedulerStats VansNavigationQueryScheduler::CaptureStats() const
{
    VansNavigationQuerySchedulerStats result; result.history=m_History.size(); result.iterationsLastFrame=m_IterationsLastFrame; result.startsLastFrame=m_StartsLastFrame;
    result.inFlight=m_Flights.size();
    for(const auto& [id,task]:m_Tasks)
    { result.queued+=task.snapshot.lifecycle==VansNavigationQueryLifecycle::Queued || task.snapshot.lifecycle==VansNavigationQueryLifecycle::WaitingForData;
        result.running+=task.snapshot.lifecycle==VansNavigationQueryLifecycle::Running; }
    return result;
}
std::vector<std::shared_ptr<const VansNavigationMap>> VansNavigationQueryScheduler::CollectRetainedMaps() const
{
    std::map<const VansNavigationMap*,std::shared_ptr<const VansNavigationMap>> retained;
    for(const auto& [id,task]:m_Tasks) if(task.map) retained.emplace(task.map.get(),task.map);
    for(const auto& flight:m_Flights) retained.emplace(flight->map.get(),flight->map);
    std::vector<std::shared_ptr<const VansNavigationMap>> result; result.reserve(retained.size());
    for(auto& [map,owner]:retained) result.push_back(std::move(owner));
    return result;
}
void VansNavigationQueryScheduler::CollectCompleted()
{
    for(auto iterator=m_Flights.begin();iterator!=m_Flights.end();)
    {
        const auto flight=*iterator;
        if(!flight->group->IsComplete()) { ++iterator; continue; }
        const auto found=m_Tasks.find(flight->handle.serial);
        if(found!=m_Tasks.end() && Pending(found->second.snapshot.lifecycle) && found->second.flight==flight)
        {
            auto& task=found->second;
            task.flight.reset(); task.context=flight->context;
            task.snapshot.path=std::move(flight->path); task.snapshot.route=std::move(flight->route);
            task.snapshot.iterations+=flight->iterationsUsed;
            if(flight->group->HasErrors())
            {
                task.snapshot.path.status=VansNavigationPathStatus::Failed;
                task.snapshot.path.failure=VansNavigationPathFailure::QueryNotReady;
                task.snapshot.path.diagnostic=flight->group->GetError();
            }
            if(flight->initializationFailed || flight->group->HasErrors())
            { Finish(task); iterator=m_Flights.erase(iterator); continue; }
            auto retryFailure=task.snapshot.path.failure;
            if(task.request.kind==VansNavigationQueryKind::PointProjection) retryFailure=flight->projection.failure;
            else if(task.request.kind==VansNavigationQueryKind::Raycast) retryFailure=flight->raycast.failure;
            else if(task.request.kind==VansNavigationQueryKind::NearestBoundary) retryFailure=flight->boundary.failure;
            if(retryFailure==VansNavigationPathFailure::DataUnavailable || retryFailure==VansNavigationPathFailure::MeshChanged)
            {
                task.searching=false; task.snapshot.route.reset();
                task.snapshot.path.status=VansNavigationPathStatus::Failed; task.snapshot.path.failure=retryFailure;
                task.snapshot.path.diagnostic=retryFailure==VansNavigationPathFailure::DataUnavailable?
                    "Waiting for changed Tile data":"Restarting a query after Tile publication";
                if(task.snapshot.attempts) --task.snapshot.attempts;
                if(retryFailure==VansNavigationPathFailure::MeshChanged)
                { task.context.reset(); task.snapshot.lifecycle=VansNavigationQueryLifecycle::Queued; }
                else
                {
                    task.context->ResetQuery(); task.waitingRevision=flight->observedTraversalRevision;
                    task.snapshot.lifecycle=VansNavigationQueryLifecycle::WaitingForData;
                }
                iterator=m_Flights.erase(iterator); continue;
            }
            if(task.request.kind==VansNavigationQueryKind::PointProjection)
            {
                if(flight->projection.valid && !flight->group->HasErrors())
                { task.snapshot.point=flight->projection.position; task.snapshot.projectionDistance=flight->projection.distance;
                    task.snapshot.path.diagnostic=flight->projection.diagnostic; }
                else if(!flight->group->HasErrors())
                { task.snapshot.path.status=VansNavigationPathStatus::Failed; task.snapshot.path.failure=flight->projection.failure;
                    task.snapshot.path.diagnostic=flight->projection.diagnostic; }
                Finish(task); iterator=m_Flights.erase(iterator); continue;
            }
            if(task.request.kind==VansNavigationQueryKind::Raycast || task.request.kind==VansNavigationQueryKind::NearestBoundary)
            {
                const bool ray=task.request.kind==VansNavigationQueryKind::Raycast;
                const bool finished=ray?flight->raycast.finished:flight->boundary.finished;
                if(finished || flight->group->HasErrors())
                {
                    if(!flight->group->HasErrors())
                    {
                        const auto failure=ray?flight->raycast.failure:flight->boundary.failure;
                        task.snapshot.path.failure=failure;
                        task.snapshot.path.diagnostic=ray?flight->raycast.diagnostic:flight->boundary.diagnostic;
                        if(failure!=VansNavigationPathFailure::None) task.snapshot.path.status=VansNavigationPathStatus::Failed;
                        if(ray) task.snapshot.raycast=std::move(flight->raycast);
                        else task.snapshot.boundary=std::move(flight->boundary);
                    }
                    Finish(task);
                }
                else task.searching=true;
                iterator=m_Flights.erase(iterator); continue;
            }
            task.searching=task.snapshot.path.status==VansNavigationPathStatus::None;
            if(!task.searching)
            {
                if(task.request.kind==VansNavigationQueryKind::Path) Finish(task);
                else if(task.snapshot.path.status==VansNavigationPathStatus::Complete && task.snapshot.path.points.size()>=2)
                { task.snapshot.point=task.snapshot.path.points.back(); Finish(task); }
                else if(task.snapshot.attempts>=static_cast<std::uint32_t>(task.request.maximumAttempts))
                    Finish(task,VansNavigationPathFailure::NoReachablePoint);
            }
        }
        // 已取消/释放的结果直接销毁；不访问实体、motor 或作者状态。
        iterator=m_Flights.erase(iterator);
    }
}
bool VansNavigationQueryScheduler::ReserveLocalQueryBudget(int maximumIterations,int pathStarts)
{
 if(maximumIterations<0 || pathStarts<0 || maximumIterations>RemainingIterationBudget() ||
  pathStarts>m_Settings.maximumPathStartsPerFrame-m_StartsLastFrame) return false;
 m_IterationsLastFrame+=maximumIterations; m_StartsLastFrame+=pathStarts; return true;
}
void VansNavigationQueryScheduler::Update()
{
    BeginFrame(); Dispatch(m_Settings.maximumIterationsPerFrame);
}
void VansNavigationQueryScheduler::BeginFrame()
{
    ++m_Frame; m_IterationsLastFrame=0; m_StartsLastFrame=0;
    CollectCompleted();
    for(auto& [id,task]:m_Tasks)
    {
        if(Pending(task.snapshot.lifecycle) && m_Frame-task.submittedFrame>static_cast<std::uint64_t>(m_Settings.maximumQueryFrames))
            Finish(task,VansNavigationPathFailure::QueryDeadlineExceeded);
        else if(task.snapshot.lifecycle==VansNavigationQueryLifecycle::WaitingForData &&
            task.map->GetTraversalRevision()!=task.waitingRevision)
            task.snapshot.lifecycle=VansNavigationQueryLifecycle::Queued;
    }
    std::size_t retired=0;
    for(const auto& flight:m_Flights)
    {
        const auto task=m_Tasks.find(flight->handle.serial);
        retired+=task==m_Tasks.end() || !Pending(task->second.snapshot.lifecycle);
    }
    auto active=CaptureStats().running+retired;
    while(active<static_cast<std::size_t>(m_Settings.maximumActiveQueries))
    {
        Task* selected=nullptr; std::uint64_t score=0;
        for(auto& [id,task]:m_Tasks) if(task.snapshot.lifecycle==VansNavigationQueryLifecycle::Queued)
        {
            const auto candidateScore=static_cast<std::uint64_t>(task.request.priority)+(m_Frame-task.submittedFrame)/30u;
            if(!selected || candidateScore>score) { selected=&task; score=candidateScore; }
        }
        if(!selected) break;
        selected->snapshot.lifecycle=VansNavigationQueryLifecycle::Running; ++active;
    }
    }
void VansNavigationQueryScheduler::Dispatch(int maximumIterations)
{
    int remaining=(std::max)(0,(std::min)(maximumIterations,RemainingIterationBudget()));
    int starts=m_Settings.maximumPathStartsPerFrame-m_StartsLastFrame;
    auto iterator=m_Tasks.upper_bound(m_Cursor);
    for(std::size_t visited=0,count=m_Tasks.size();visited<count && remaining>0;++visited)
    {
        if(iterator==m_Tasks.end()) iterator=m_Tasks.begin();
        auto& [id,task]=*iterator++;
        if(task.snapshot.lifecycle!=VansNavigationQueryLifecycle::Running || task.flight || (!task.searching && starts==0)) continue;
        try
        {
            auto flight=std::make_shared<Flight>();
            flight->handle=task.snapshot.handle; flight->request=task.request; flight->settings=m_Settings;
            flight->map=task.map; flight->context=task.context; flight->goal=task.request.goal;
            flight->begin=!task.searching; flight->maximumIterations=(std::min)(remaining,m_Settings.maximumIterationsPerSlice);
            if(flight->begin && task.request.kind==VansNavigationQueryKind::ReachablePoint)
            {
                const auto sample=task.request.seed+task.snapshot.attempts;
                const float angle=2.39996323f*static_cast<float>(sample);
                const float distance=task.request.radius*(.55f+.45f*static_cast<float>(sample%5u)/4.f);
                flight->goal+=glm::vec3(std::sin(angle)*distance,0,std::cos(angle)*distance);
            }
            flight->group=VansJobSystem::Get().QueueJobGroup({[flight](){flight->Execute();}});
            task.flight=flight; m_Flights.push_back(std::move(flight));
            if(!task.searching) { ++task.snapshot.attempts; --starts; ++m_StartsLastFrame; }
            // 为每个 worker 预留本帧节点额度；提前结束的余量不超额转发。
            remaining-=task.flight->maximumIterations; m_IterationsLastFrame+=task.flight->maximumIterations; m_Cursor=id;
        }
        catch(const std::bad_alloc&)
        {
            task.snapshot.path.status=VansNavigationPathStatus::Failed;
            task.snapshot.path.failure=VansNavigationPathFailure::QueryNotReady;
            task.snapshot.path.diagnostic="Could not allocate navigation worker quantum"; Finish(task);
        }
    }
    CollectCompleted();
    PruneHistory();
}
}
