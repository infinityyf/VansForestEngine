#pragma once

#include "VansNavigationTypes.h"

#include <memory>
#include <optional>

namespace Vans
{
class VansNavigationMap;
struct VansNavigationQueryState;
struct VansNavigationCorridorRoute;

inline constexpr std::size_t kMaximumNavigationBoundarySegments=32;
struct VansNavigationLocalBoundary
{
    std::array<std::array<glm::vec3,2>,kMaximumNavigationBoundarySegments> segments{};
    std::size_t count=0;
    bool limited=false;
    bool pending=false;
    VansNavigationPathFailure failure=VansNavigationPathFailure::None;
};
struct VansNavigationLinkEntrance
{
    VansNavigationLinkKey key;
    glm::vec3 entry{0},exit{0};
    float radiusMeters=0;
    bool bidirectional=false;
};
struct VansNavigationSteering
{
	bool valid = false;
	bool pending = false;
	// 有界 corner 窗口包含终点时，remainingDistance 才是到终点的完整可走距离。
	bool hasEndpointDistance = false;
	glm::vec3 direction{ 0.0f };
	glm::vec3 endpoint{ 0.0f };
	float remainingDistance = 0.0f;
	VansNavigationPathFailure failure = VansNavigationPathFailure::None;
    std::optional<VansNavigationLinkEntrance> link;
};

class VansNavigationQueryContext
{
public:
	VansNavigationQueryContext();
	~VansNavigationQueryContext();
	VansNavigationQueryContext(const VansNavigationQueryContext&) = delete;
	VansNavigationQueryContext& operator=(const VansNavigationQueryContext&) = delete;

	bool Initialize(std::shared_ptr<const VansNavigationMap> mesh,
		const VansNavigationQuerySettings& settings, const std::string& filterId, std::string& error);
    // 只由此 context 的 owner 更新；成功变更撤销旧搜索/路径，失败保留原状态。
    // 空能力集排除全部非步行连接。调用者只发布其真实 traversal consumer 支持的身份。
    bool SetLinkCapabilities(const std::vector<VansNavigationLinkKey>& keys,std::string& error);
	// 一个 context 仅由一个执行者使用；不同 context 可以并发查询同一 Map 的只读量子。
	VansNavigationPath FindPath(const glm::vec3& start, const glm::vec3& end,
		const glm::vec3& nearestExtents = glm::vec3(1.0f, 2.0f, 1.0f),
		VansNavigationGoalAcceptance acceptance=VansNavigationGoalAcceptance::ProjectedPoint,float projectionToleranceMeters=.05f);
	VansNavigationPointProjection ProjectPoint(const glm::vec3& position,const glm::vec3& extents);
	VansNavigationRaycast NavRaycast(const glm::vec3& start,const glm::vec3& end,const glm::vec3& extents);
	VansNavigationRaycast BeginRaycast(const glm::vec3& start,const glm::vec3& end,const glm::vec3& extents);
	VansNavigationRaycast AdvanceRaycast(int maximumIterations,int& iterationsUsed);
	VansNavigationBoundary FindNearestBoundary(const glm::vec3& position,float radiusMeters,const glm::vec3& extents);
	VansNavigationBoundary BeginNearestBoundary(const glm::vec3& position,float radiusMeters,const glm::vec3& extents);
	VansNavigationBoundary AdvanceNearestBoundary(int maximumIterations,int& iterationsUsed);
	// None 表示尚未完成；同步入口也驱动这一份 sliced 算法。
	VansNavigationPath BeginPath(const glm::vec3& start, const glm::vec3& end,
		const glm::vec3& nearestExtents = glm::vec3(1.0f, 2.0f, 1.0f),
		VansNavigationGoalAcceptance acceptance=VansNavigationGoalAcceptance::ProjectedPoint,float projectionToleranceMeters=.05f);
	VansNavigationPath AdvancePath(int maximumIterations, int& iterationsUsed);
	void ResetQuery();
	// worker 只发布拥有型路线；native scratch 不与主线程 motor 共享。
	std::shared_ptr<const VansNavigationCorridorRoute> CaptureCorridorRoute() const;
	VansNavigationPathFailure AdoptCorridorRoute(const std::shared_ptr<const VansNavigationCorridorRoute>& route);
	VansNavigationSteering FollowCorridor(const glm::vec3& actualPosition,
		float maximumHorizontalError, float maximumVerticalError, int maximumIterations, int& iterationsUsed);
    void CancelCorridorLocalQuery();
    VansNavigationSteering CaptureCorridorSteering(const glm::vec3& actualPosition);
	// 局部搜索消费显式节点量子；pending 保留权威路径和元数据。
	VansNavigationPathFailure TryRepairTarget(const glm::vec3& goal,const glm::vec3& extents,
		VansNavigationGoalAcceptance acceptance,float toleranceMeters,int maximumIterations,int& iterationsUsed,
        bool& pending,VansNavigationPath& path);
	// 优化仍是完整有界操作；调用者在执行前预留保守额度。
	int CorridorOptimizationIterationBudget() const;
	// Cache hits use no nodes. A miss waits until the explicit quantum covers the
	// native 64-node neighborhood and bounded wall decode; no partial cache is published.
	VansNavigationLocalBoundary CaptureLocalBoundary(const glm::vec3& position,float radiusMeters,
        int maximumPolygons,int maximumSegments,int maximumIterations,int& iterationsUsed);
	VansNavigationPathFailure TryOptimizeCorridor(float visibilityRangeMeters,
		VansNavigationPath& path, bool& changed);

private:
    struct ReadLease;
	VansNavigationPathFailure CheckReadyUnderLease(bool requireStableSlice=false) const;
    VansNavigationPathFailure ValidateCorridorUnderLease(const std::vector<VansNavigationTileDependency>& tiles) const;
    void ResetQueryUnderLease();
	VansNavigationPointProjection ProjectPointUnderLease(const glm::vec3& position,const glm::vec3& extents);
	VansNavigationPath FinishPath();
    VansNavigationSteering CaptureCorridorSteeringUnderLease(const glm::vec3& actualPosition);
	VansNavigationPath CapturePolygonPath(int polygonCount,const glm::vec3& start,const glm::vec3& end,
		const VansNavigationPath& metadata,bool complete);
	std::shared_ptr<const VansNavigationMap> m_Map;
	std::unique_ptr<VansNavigationQueryState> m_State;
	std::uint64_t m_ContentRevision = 0;
	std::string m_FilterId;
};
}
