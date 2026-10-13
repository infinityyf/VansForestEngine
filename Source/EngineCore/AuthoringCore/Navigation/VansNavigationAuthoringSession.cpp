#include "VansNavigationAuthoringSession.h"
#include "../../NavigationCore/VansNavigationSourceCollector.h"
#include "../../NavigationCore/VansNavigationMap.h"
#include "../../Util/VansJobSystem.h"
#include <chrono>
#include <new>
#include <utility>
#include <atomic>
#include <limits>
#include <set>
#include <algorithm>

namespace Vans
{
namespace
{
std::atomic<std::uint64_t> g_NextNavigationPreviewId{0};
constexpr const char* kQueryOwner="Navigation.AuthoringPreview";
std::uint64_t AllocatePreviewId()
{
    auto current=g_NextNavigationPreviewId.load();
    while(current!=(std::numeric_limits<std::uint64_t>::max)())
        if(g_NextNavigationPreviewId.compare_exchange_weak(current,current+1)) return current+1;
    return 0;
}
}
struct VansNavigationAuthoringSession::Queries
{
    VansNavigationQueryScheduler scheduler;
    VansNavigationQueryHandle handle;
    VansNavigationQueryRequest request;
    std::shared_ptr<VansNavigationMap> map;
    std::uint64_t session=0;
    std::string diagnostic;
    ~Queries() { scheduler.Shutdown(); }
};
struct VansNavigationAuthoringSession::Task
{
    VansNavigationSourceSnapshot sources;
    VansNavigationSettings settings;
    std::string mapId, sourceScene, error;
    VansNavigationBuildControl control;
    std::shared_ptr<VansJobGroup> group;
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    double elapsedMilliseconds = 0;
};

VansNavigationAuthoringSession::VansNavigationAuthoringSession() = default;
VansNavigationAuthoringSession::~VansNavigationAuthoringSession()
{
    // 只有 owner 销毁才等待；普通轮询、取消和下一次请求均不阻塞主线程。
    if (m_Task)
    {
        m_Task->control.Cancel();
    }
    m_Queries.reset();
    if(m_Task && m_Task->group) m_Task->group->Wait();
}

void VansNavigationAuthoringSession::Poll()
{
    if (!m_Task || !m_Task->group || !m_Task->group->IsComplete()) return;
    const auto task = std::move(m_Task);
    m_Snapshot.busy = false;
    m_Snapshot.elapsedMilliseconds = task->elapsedMilliseconds;
    if (m_Snapshot.state == VansNavigationPreviewState::Stale) return;
    if (task->control.IsCancelled())
    {
        m_Snapshot.state = VansNavigationPreviewState::Cancelled;
        m_Snapshot.diagnostic = "Navigation preview was cancelled";
    }
    else if (task->group->HasErrors() || !task->asset)
    {
        m_Snapshot.state = VansNavigationPreviewState::Failed;
        m_Snapshot.diagnostic = task->group->HasErrors() ? task->group->GetError() : task->error;
    }
    else
    {
        m_Snapshot.state = VansNavigationPreviewState::Ready;
        m_Snapshot.asset = task->asset;
        m_Snapshot.diagnostic.clear();
    }
}

bool VansNavigationAuthoringSession::BuildPreview(VansNavigationSourceSnapshot sources,
    VansNavigationSettings settings, std::string mapId, std::string sourceScene,
    std::uint64_t sourceObservation, std::string& error)
try
{
    Poll();
    // 被取消的任务仍计入容量，直到它真正退出，不能靠重复点击堆积任务。
    if (m_Task) { error = "Wait for the current navigation build to finish cancelling"; return false; }
    // 退役查询仍保留 native Map 和 scratch；新构建不能绕过总驻留峰值。
    if(m_Queries && m_Queries->scheduler.CaptureStats().inFlight)
    { error="Wait for navigation preview query workers to retire before building"; return false; }
    if (!ValidateNavigationSettings(settings,error) || !ValidateNavigationSourceIdentity(sourceScene,error)) return false;
    if (!FindNavigationMap(settings,mapId)) { error = "Navigation preview Map is unavailable: " + mapId; return false; }
    auto task = std::make_shared<Task>();
    task->sources = std::move(sources); task->settings = std::move(settings);
    task->mapId = std::move(mapId); task->sourceScene = std::move(sourceScene);
    VansNavigationPreviewSnapshot snapshot;
    snapshot.state = VansNavigationPreviewState::Building; snapshot.busy = true;
    snapshot.requestId = AllocatePreviewId();
    if(!snapshot.requestId) { error="Navigation preview identity exhausted"; return false; }
    snapshot.mapId = task->mapId; snapshot.sourceCount = task->sources.sources.size();
    snapshot.surfaceCount = task->sources.surfaces.size();
    snapshot.exclusions = task->sources.exclusions;
    auto settingsToPublish=task->settings;
    task->group = VansJobSystem::Get().QueueJobGroup({[task]()
    {
        struct Completion
        {
            Task& task;
            ~Completion()
            {
                // 失败、取消及异常也立即释放冻结源，不依赖隐藏窗口继续轮询。
                task.sources = {};
                task.elapsedMilliseconds = std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-task.started).count();
            }
        } completion{*task};
        VansNavigationBuildInput input;
        if (!task->control.IsCancelled() && VansNavigationSourceCollector::Collect(
                task->sources,task->settings,task->mapId,input,task->error))
        {
            task->sources = {};
            VansNavigationBuilder::Build(input,task->settings,task->sourceScene,
                &task->control,task->asset,task->error);
        }
    }});
    ClearQueries();
    m_Settings=std::move(settingsToPublish); m_Observation=sourceObservation;
    m_Task = std::move(task); m_Snapshot = std::move(snapshot);
    error.clear(); return true;
}
catch (const std::bad_alloc&) { error = "Navigation preview allocation failed"; return false; }

VansNavigationPreviewSnapshot VansNavigationAuthoringSession::Capture()
{
    Poll();
    auto result = m_Snapshot;
    if (m_Task) result.elapsedMilliseconds = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-m_Task->started).count();
    return result;
}

bool VansNavigationAuthoringSession::MatchesSources(const VansNavigationSettings& settings,
    std::uint64_t sourceObservation) const
{
    return m_Snapshot.requestId != 0 && m_Observation == sourceObservation && SameNavigationSettings(m_Settings,settings);
}

VansNavigationSettings VansNavigationAuthoringSession::QuerySettings() const
{
    auto settings=m_Settings;
    // 作者工具只有一个当前查询；节点/路径容量及每帧/slice 额度仍来自应用后的项目设置。
    settings.query.maximumActiveAgents=1;
    settings.query.maximumActiveQueries=1;
    settings.query.maximumQueuedQueries=1;
    settings.query.maximumQueryHistory=1;
    settings.query.maximumPathStartsPerFrame=1;
    return settings;
}

void VansNavigationAuthoringSession::ClearQueries()
{
    if(!m_Queries) return;
    if(m_Queries->handle.IsValid())
    {
        std::string error;
        if(!m_Queries->scheduler.Release(m_Queries->handle,kQueryOwner,error)) m_Queries->diagnostic=std::move(error);
    }
    m_Queries->handle={}; m_Queries->map.reset();
}

void VansNavigationAuthoringSession::Update()
{
    Poll();
    // 失效/取消后的退役 worker 也在帧安全点回收，窗口可见性不拥有其生命周期。
    if(m_Queries && m_Queries->session) m_Queries->scheduler.Update();
}

bool VansNavigationAuthoringSession::PrepareMap(const std::string& profileId,std::string& error)
{
    Poll();
    if(m_Snapshot.state!=VansNavigationPreviewState::Ready || !m_Snapshot.asset)
    { error="Build a current navigation preview before query or visualization"; return false; }
    if(!m_Queries) m_Queries=std::make_unique<Queries>();
    if(m_Queries->map && m_Queries->map->GetAsset()==m_Snapshot.asset && m_Queries->map->GetProfileId()==profileId)
    { error.clear(); return true; }
    const auto* profile=m_Snapshot.asset->FindProfile(profileId);
    if(!profile) { error="Navigation preview Profile is unavailable: "+profileId; return false; }
    const auto settings=QuerySettings();
    std::set<const VansNavigationMap*> maps;
    std::set<const VansNavigationMeshAsset*> assets;
    std::uint64_t bytes=EstimateNavigationQueryBytes(settings.query),tiles=profile->tiles.size();
    bytes+=m_Snapshot.asset->EstimateResidentBytes()+VansNavigationMap::EstimateInitializationBytes(*m_Snapshot.asset,settings,profileId);
    assets.insert(m_Snapshot.asset.get());
    const auto include=[&](const std::shared_ptr<const VansNavigationMap>& map)
    {
        if(!map || !maps.insert(map.get()).second) return;
        const auto asset=map->GetAsset();
        bytes+=map->EstimateResidentBytes(); tiles+=asset->FindProfile(map->GetProfileId())->tiles.size();
        if(assets.insert(asset.get()).second) bytes+=asset->EstimateResidentBytes();
    };
    include(m_Queries->map);
    for(const auto& map:m_Queries->scheduler.CollectRetainedMaps()) include(map);
    if(bytes>static_cast<std::uint64_t>(m_Settings.maximumResidentBytes) || tiles>static_cast<std::uint64_t>(m_Settings.maximumResidentTiles))
    { error="Navigation preview query would exceed aggregate resident capacity"; return false; }
    auto map=std::make_shared<VansNavigationMap>();
    if(!map->Initialize(m_Snapshot.asset,settings,profileId,error)) return false;
    m_Queries->map=std::move(map); error.clear(); return true;
}

bool VansNavigationAuthoringSession::SubmitQuery(const VansNavigationQueryRequest& request,std::string& error)
try
{
    if(m_Queries && m_Queries->handle.IsValid())
    {
        const auto current=m_Queries->scheduler.Capture(m_Queries->handle,kQueryOwner,error);
        if(!current) return false;
        if(current->lifecycle==VansNavigationQueryLifecycle::Queued || current->lifecycle==VansNavigationQueryLifecycle::Running)
        { error="Cancel the current navigation preview query before starting another"; return false; }
    }
    if(!PrepareMap(request.profileId,error)) return false;
    if(m_Queries->session!=m_Snapshot.requestId)
    {
        if(m_Queries->scheduler.CaptureStats().inFlight)
        { error="Previous navigation preview query workers are still retiring"; return false; }
        m_Queries->scheduler.Initialize(QuerySettings().query,m_Snapshot.requestId);
        m_Queries->session=m_Snapshot.requestId;
    }
    auto requestToPublish=request;
    const auto handle=m_Queries->scheduler.Submit(request,kQueryOwner,m_Queries->map,{},error);
    if(!handle.IsValid()) return false;
    if(m_Queries->handle.IsValid())
    {
        std::string releaseError;
        if(!m_Queries->scheduler.Release(m_Queries->handle,kQueryOwner,releaseError)) m_Queries->diagnostic=std::move(releaseError);
    }
    m_Queries->handle=handle; m_Queries->request=std::move(requestToPublish); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation preview query allocation failed"; return false; }

std::optional<VansNavigationQuerySnapshot> VansNavigationAuthoringSession::CaptureQuery(std::string& error) const
{
    if(!m_Queries || !m_Queries->handle.IsValid())
    { error=m_Queries?m_Queries->diagnostic:std::string{}; return {}; }
    auto result=m_Queries->scheduler.Capture(m_Queries->handle,kQueryOwner,error);
    if(result && !result->path.hasRequestCoordinates)
    {
        result->path.hasRequestCoordinates=true;
        result->path.requestedStart=m_Queries->request.start; result->path.requestedEnd=m_Queries->request.goal;
    }
    return result;
}

void VansNavigationAuthoringSession::CancelQuery()
{
    if(!m_Queries || !m_Queries->handle.IsValid()) return;
    std::string error;
    if(!m_Queries->scheduler.Cancel(m_Queries->handle,kQueryOwner,error)) m_Queries->diagnostic=std::move(error);
}

bool VansNavigationAuthoringSession::CaptureDebugGeometry(const VansNavigationDebugView& view,
    const std::string& profileId,const std::string& filterId,VansNavigationDebugGeometry& output,std::string& error)
try
{
    return PrepareMap(profileId,error) && m_Queries->map->CaptureDebugGeometry(view,filterId,output,error);
}
catch(const std::bad_alloc&) { error="Navigation preview visualization allocation failed"; return false; }

void VansNavigationAuthoringSession::Invalidate(std::string reason)
{
    if (!m_Snapshot.requestId) return;
    if (m_Task) m_Task->control.Cancel();
    ClearQueries();
    m_Snapshot.asset.reset(); m_Snapshot.state = VansNavigationPreviewState::Stale;
    m_Snapshot.diagnostic = std::move(reason);
}

void VansNavigationAuthoringSession::Cancel()
{
    Poll();
    if (m_Task) m_Task->control.Cancel();
    ClearQueries();
    m_Snapshot.asset.reset();
    m_Snapshot.state = m_Task ? VansNavigationPreviewState::Cancelling : VansNavigationPreviewState::Cancelled;
    m_Snapshot.diagnostic = "Navigation preview was cancelled";
}
}
