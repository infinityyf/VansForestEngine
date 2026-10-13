#pragma once

#include "VansNavigationRuntimeComponents.h"
#include "VansNavigationQueryScheduler.h"
#include "VansNavigationSourceSnapshot.h"
#include "VansNavigationAvoidance.h"
#include "../SceneRuntime/VansRuntimeHandle.h"

#include <map>
#include <optional>

namespace Vans
{
struct VansNavigationMotorSample
{
	glm::vec3 position{ 0.0f };
	glm::vec3 velocity{ 0.0f };
	bool enabled = false;
	bool movementBlocked = false;
};

struct VansNavigationMotion
{
	glm::vec3 direction{ 0.0f };
	float desiredSpeed = 0.0f;
	float acceleration = 0.0f;
	std::optional<float> facingYaw;
};

// 导航不认识角色控制器、动画或最终 Transform。角色集成层拥有这些事实。
class IVansNavigationMotor
{
public:
	virtual ~IVansNavigationMotor() = default;
	virtual VansNavigationMotorSample Read() const = 0;
	virtual void Submit(const VansNavigationMotion& motion) = 0;
};

enum class VansNavigationMoveState : std::uint8_t
{
	Idle, Queued, Following, Paused, Finished, WaitingForData, TraversingLink
};
enum class VansNavigationMoveResult : std::uint8_t
{
	None, ReachedGoal, ReachedPartialEnd, Cancelled, Failed, Stuck
};
struct VansNavigationRequestHandle
{
	std::uint64_t session = 0;
	std::uint64_t serial = 0;
	VansEntityHandle agent;
	bool IsValid() const { return session != 0 && serial != 0 && agent.IsValid(); }
};
struct VansNavigationSubscriptionHandle
{
    std::uint64_t session=0,serial=0;
    VansEntityHandle agent;
    bool IsValid() const { return session && serial && agent.IsValid(); }
};
struct VansNavigationTraversalHandle
{
    std::uint64_t session=0,serial=0;
    VansEntityHandle agent;
    bool IsValid() const { return session && serial && agent.IsValid(); }
};
enum class VansNavigationTraversalPhase : std::uint8_t
{ Idle, Approaching, TraversalRequested, Traversing, Completed, Aborted };
struct VansNavigationTraversalSnapshot
{
    VansNavigationTraversalHandle handle;
    VansNavigationSubscriptionHandle subscription;
    VansNavigationLinkEntrance link;
    VansNavigationTraversalPhase phase=VansNavigationTraversalPhase::Idle;
    float elapsedSeconds=0;
    std::string diagnostic;
};
// 主线程上的拥有型事实通知。订阅者只记录事实，动作确认在自己的下一次更新中提交。
enum class VansNavigationLifecycleKind : std::uint8_t { Traversal, Completion };
struct VansNavigationLifecycleEvent
{
    VansNavigationLifecycleKind kind=VansNavigationLifecycleKind::Traversal;
    VansNavigationSubscriptionHandle subscription;
    VansNavigationRequestHandle request;
    std::string requestOwner;
    VansNavigationTraversalSnapshot traversal;
    VansNavigationMoveResult result=VansNavigationMoveResult::None;
};
struct VansNavigationMoveOptions
{
	float arrivalRadius = 0.45f;
	float heightTolerance = 0.5f;
	bool allowPartial = false;
	VansNavigationGoalAcceptance goalAcceptance=VansNavigationGoalAcceptance::ProjectedPoint;
	float goalProjectionToleranceMeters=.05f;
	std::optional<float> facingYaw;
};
struct VansNavigationAgentSnapshot
{
	VansNavigationRequestHandle request;
	std::string owner;
	VansNavigationMoveState state = VansNavigationMoveState::Idle;
	VansNavigationMoveResult result = VansNavigationMoveResult::None;
	VansNavigationRequestReason reason = VansNavigationRequestReason::None;
	VansNavigationPath path;
	glm::vec3 goal{ 0.0f };
	float desiredSpeed = 0.0f;
	bool movementBlocked = false;
	std::uint32_t recoveryCount = 0;
	VansNavigationAvoidanceResult avoidance;
    VansNavigationTraversalSnapshot traversal;
};
struct VansNavigationAgentRegistration
{
	VansEntityHandle entity;
	VansRuntimeNavigationAgentComponent config;
	std::shared_ptr<const VansNavigationMap> map;
	std::shared_ptr<IVansNavigationMotor> motor;
};

class VansNavigationWorld
{
public:
	VansNavigationWorld();
	~VansNavigationWorld();
	bool Initialize(const VansNavigationSettings& settings, std::string& error);
	void Shutdown();
	// 全批验证成功后才发布，新增实体不会重置已有请求。
	bool RegisterAgents(const std::vector<VansNavigationAgentRegistration>& agents, std::string& error);
    bool RegisterMaps(const std::vector<std::shared_ptr<const VansNavigationMap>>& maps,std::string& error);
    void WithdrawMap(const std::string& mapId);
	void UnregisterAgent(VansEntityHandle entity);
	void CancelOwner(const std::string& owner);
    // Owner 的完整期望集合。成功后撤销缺失项，其他 owner 保留；失败不改变状态。
    bool ReplaceObstacles(const std::string& owner,const std::vector<VansNavigationObstacleRecord>& obstacles,std::string& error);
    bool CaptureObstacles(std::vector<VansNavigationObstacleRecord>& output,std::string& error) const;
    std::uint64_t GetCarveObstacleRevision() const { return m_CarveObstacleRevision; }
    std::uint64_t EstimateObstacleResidentBytes() const { return m_ObstacleBytes; }
	bool HasAgent(VansEntityHandle entity) const;
	VansNavigationRequestHandle RequestMove(VansEntityHandle entity, const std::string& owner,
		const glm::vec3& goal, const VansNavigationMoveOptions& options, std::string& error);
	bool UpdateGoal(VansNavigationRequestHandle request, const std::string& owner,
		const glm::vec3& goal, std::optional<float> facingYaw, std::string& error);
	bool Stop(VansNavigationRequestHandle request, const std::string& owner, std::string& error);
	bool Pause(VansNavigationRequestHandle request, const std::string& owner, std::string& error);
	bool Resume(VansNavigationRequestHandle request, const std::string& owner, std::string& error);
	std::optional<VansNavigationAgentSnapshot> CaptureAgent(VansEntityHandle entity) const;
    VansNavigationQueryHandle RequestQuery(const VansNavigationQueryRequest& request,const std::string& owner,std::string& error);
    VansNavigationQueryHandle SampleReachablePoint(VansEntityHandle entity,const std::string& owner,
        const glm::vec3& anchor,float radius,std::uint32_t seed,std::string& error);
    std::optional<VansNavigationQuerySnapshot> CaptureQuery(VansNavigationQueryHandle handle,const std::string& owner,std::string& error) const;
    bool CancelQuery(VansNavigationQueryHandle handle,const std::string& owner,std::string& error);
    bool ReleaseQuery(VansNavigationQueryHandle handle,const std::string& owner,std::string& error);
    // 每个 Agent 的动作 consumer 独占订阅；能力来自该 consumer 实际支持的逻辑 Link。
    VansNavigationSubscriptionHandle Subscribe(VansEntityHandle entity,const std::string& owner,
        const std::vector<VansNavigationLinkKey>& links,float maximumTraversalSeconds,std::string& error);
    bool Unsubscribe(VansNavigationSubscriptionHandle handle,const std::string& owner,std::string& error);
    bool HasSubscription(VansNavigationSubscriptionHandle handle,const std::string& owner) const;
    bool BeginTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error);
    bool CompleteTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error);
    bool AbortTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error);
    bool HasTraversalControl(VansEntityHandle entity,const std::string& owner) const;
    int GetSubscriptionCapacity() const { return m_Settings.query.maximumActiveAgents; }
    VansNavigationQuerySchedulerStats CaptureQueryStats() const { return m_Queries.CaptureStats(); }
	void Update(double deltaSeconds);

private:
	struct AgentRuntime
	{
		VansNavigationAgentRegistration registration;
		std::unique_ptr<VansNavigationQueryContext> query;
        VansNavigationQueryHandle queryRequest;
		VansNavigationAgentSnapshot snapshot;
		VansNavigationMoveOptions options;
		glm::vec3 queriedGoal{ 0.0f };
		glm::vec3 progressPosition{ 0.0f };
		glm::vec3 progressDirection{ 0.0f };
		glm::vec3 progressEndpoint{ 0.0f };
		float progressRemainingDistance = 0.0f;
		float repathRemaining = 0.0f;
		float optimizationRemaining = 0.0f;
		float stuckSeconds = 0.0f;
        float avoidanceBlockedSeconds=0;
        int avoidanceWaitFrames=0,avoidanceIndex=-1;
        VansNavigationMotorSample frameMotor;
        int followWaitFrames=0;
        int dataWaitFrames=0;
        std::uint64_t dataWaitRevision=0;
        bool waitingWithCorridor=false;
		bool hasQueriedGoal = false;
		bool pausedWithCorridor = false;
		bool hasProgressWindow = false;
		bool hadMoveIntent = false;
        struct Consumer
        {
            VansNavigationSubscriptionHandle handle;
            std::string owner;
            std::vector<VansNavigationLinkKey> links;
            float maximumSeconds=0;
        };
        std::optional<Consumer> consumer;
        VansNavigationQueryHandle traversalQuery;
        bool traversalCompletionRequested=false;
	};
	static std::uint64_t EntityKey(VansEntityHandle entity);
	AgentRuntime* ResolveRequest(VansNavigationRequestHandle request,
		const std::string& owner, std::string& error);
	void Finish(AgentRuntime& agent, VansNavigationMoveResult result);
    AgentRuntime* ResolveTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error);
    void CancelTraversal(AgentRuntime& agent,const char* diagnostic);
    void UpdateTraversal(AgentRuntime& agent,const VansNavigationMotorSample& motor,float dt);
    void PublishLifecycle(const AgentRuntime& agent,VansNavigationLifecycleKind kind);
    bool CanMutate(std::string& error) const;
	void Queue(AgentRuntime& agent, VansNavigationRequestReason reason);
    void WaitForData(AgentRuntime& agent,bool resumeCorridor);
	static void UpdateProgressWindow(AgentRuntime& agent, const glm::vec3& actualPosition,
		const VansNavigationSteering& steering, float minimumProgress);
	VansNavigationSettings m_Settings;
	std::map<std::uint64_t, AgentRuntime> m_Agents;
    std::map<std::pair<std::string,std::string>,std::shared_ptr<const VansNavigationMap>> m_Maps;
    VansNavigationQueryScheduler m_Queries;
    std::unique_ptr<VansNavigationAvoidance> m_Avoidance;
    struct OwnedObstacle
    {
        std::string owner;
        VansNavigationObstacleRecord record;
        glm::vec3 previousCenter{0},observedVelocity{0};
    };
    std::vector<OwnedObstacle> m_Obstacles;
    std::uint64_t m_CarveObstacleRevision=0,m_ObstacleBytes=0;
    void ObserveObstacleChange(bool carveChanged);
	std::uint64_t m_Session = 0;
	std::uint64_t m_NextRequest = 0;
    std::uint64_t m_NextConsumerIdentity=0;
	std::uint64_t m_AgentCursor = 0;
    bool m_WorkerFirst=true;
	bool m_Initialized = false;
    bool m_PublishingLifecycle=false;
};
}
