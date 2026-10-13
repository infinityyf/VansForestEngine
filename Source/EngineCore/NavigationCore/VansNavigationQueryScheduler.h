#pragma once
#include "VansNavigationQueryContext.h"
#include "../SceneRuntime/VansRuntimeHandle.h"
#include <deque>
#include <map>
#include <optional>

namespace Vans
{
class VansJobGroup;
enum class VansNavigationQueryKind : std::uint8_t { Path, ReachablePoint,PointProjection,Raycast,NearestBoundary };
enum class VansNavigationQueryLifecycle : std::uint8_t { Queued, Running, Finished, Cancelled, WaitingForData };
struct VansNavigationQueryHandle
{
    std::uint64_t session=0,serial=0;
    bool IsValid() const { return session!=0 && serial!=0; }
};
struct VansNavigationQueryRequest
{
    VansNavigationQueryKind kind=VansNavigationQueryKind::Path;
    std::string mapId="World",profileId="Humanoid",filterId="Default";
    glm::vec3 start{0},goal{0},nearestExtents{1,2,1};
    float radius=4;
    std::uint32_t seed=0;
    int maximumAttempts=12;
    int priority=0;
    VansNavigationGoalAcceptance goalAcceptance=VansNavigationGoalAcceptance::ProjectedPoint;
    float goalProjectionToleranceMeters=.05f;
    // 请求拥有的能力事实；worker 不能借用 Scene/Lua consumer 或可变容器。
    std::vector<VansNavigationLinkKey> linkCapabilities;
};
struct VansNavigationQuerySnapshot
{
    VansNavigationQueryHandle handle;
    VansNavigationQueryLifecycle lifecycle=VansNavigationQueryLifecycle::Queued;
    VansNavigationQueryKind kind=VansNavigationQueryKind::Path;
    std::string mapId,profileId,filterId;
    VansNavigationPath path;
    std::shared_ptr<const VansNavigationCorridorRoute> route;
    std::optional<glm::vec3> point;
    std::optional<VansNavigationRaycast> raycast;
    std::optional<VansNavigationBoundary> boundary;
    float projectionDistance=0;
    std::uint32_t attempts=0,iterations=0;
};
struct VansNavigationQuerySchedulerStats
{
    std::size_t queued=0,running=0,history=0;
    // worker 预留额度与主线程实际/保守局部额度之和；每个请求另记实际用量。
    int iterationsLastFrame=0,startsLastFrame=0;
    std::size_t inFlight=0;
};

// World 唯一查询队列：移动、通用路径及可达采样共享节点、启动与生命周期预算。
class VansNavigationQueryScheduler
{
public:
    void Initialize(const VansNavigationQuerySettings& settings,std::uint64_t session);
    void Shutdown();
    VansNavigationQueryHandle Submit(const VansNavigationQueryRequest& request,const std::string& owner,
        std::shared_ptr<const VansNavigationMap> map,VansEntityHandle entity,std::string& error);
    std::optional<VansNavigationQuerySnapshot> Capture(VansNavigationQueryHandle handle,const std::string& owner,std::string& error) const;
    bool Cancel(VansNavigationQueryHandle handle,const std::string& owner,std::string& error);
    bool Release(VansNavigationQueryHandle handle,const std::string& owner,std::string& error);
    void CancelOwner(const std::string& owner);
    void CancelEntity(VansEntityHandle entity);
    void CancelMap(const VansNavigationMap* map);
    void Update();
    void BeginFrame();
    void Dispatch(int maximumIterations);
    int RemainingIterationBudget() const { return m_Settings.maximumIterationsPerFrame-m_IterationsLastFrame; }
    bool ReserveLocalQueryBudget(int iterations,int pathStarts);
    VansNavigationQuerySchedulerStats CaptureStats() const;
    std::vector<std::shared_ptr<const VansNavigationMap>> CollectRetainedMaps() const;
    // 只读取 owner 的请求事实；不读取运行中 worker 的上下文或输出。
private:
    struct Flight;
    struct Task
    {
        VansNavigationQueryRequest request;
        VansNavigationQuerySnapshot snapshot;
        std::string owner;
        VansEntityHandle entity;
        std::shared_ptr<const VansNavigationMap> map;
        std::shared_ptr<VansNavigationQueryContext> context;
        std::shared_ptr<Flight> flight;
        std::uint64_t submittedFrame=0;
        std::uint64_t waitingRevision=0;
        bool searching=false;
    };
    Task* Resolve(VansNavigationQueryHandle handle,const std::string& owner,std::string& error);
    void Finish(Task& task,VansNavigationPathFailure failure=VansNavigationPathFailure::None);
    void PruneHistory();
    void CollectCompleted();
    VansNavigationQuerySettings m_Settings;
    std::map<std::uint64_t,Task> m_Tasks;
    std::deque<std::uint64_t> m_History;
    std::vector<std::shared_ptr<Flight>> m_Flights;
    std::uint64_t m_Session=0,m_NextSerial=0,m_Frame=0,m_Cursor=0;
    int m_IterationsLastFrame=0,m_StartsLastFrame=0;
};
}
