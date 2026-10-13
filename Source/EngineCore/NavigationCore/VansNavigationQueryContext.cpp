#include "VansNavigationQueryContext.h"
#include "VansNavigationMap.h"
#include "Detail/VansNavigationTraversalState.h"

#include <DetourNavMeshQuery.h>
#include <DetourPathCorridor.h>
#include <DetourCommon.h>
#include <array>

#include <algorithm>
#include <cmath>
#include <new>
#include <limits>
#include <tuple>

namespace Vans
{
class VansNavigationDetourFilter final : public dtQueryFilter
{
public:
    std::array<bool, kMaximumNavigationAreaId + 1u> allowed{};
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities> links{};
    std::size_t linkCount=0;
    const VansNavigationTraversalState* traversal=nullptr;
    mutable bool encounteredUnavailable=false;
    bool passFilter(dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const override
    {
        const auto area = poly->getArea();
        if(area>=allowed.size() || !allowed[area] || !dtQueryFilter::passFilter(ref,tile,poly)) return false;
        if(poly->getType()==DT_POLYTYPE_OFFMESH_CONNECTION)
        {
            const auto index=(poly-tile->polys)-tile->header->offMeshBase;
            if(index<0 || index>=tile->header->offMeshConCount ||
                !std::binary_search(links.begin(),links.begin()+linkCount,VansNavigationLinkKey{tile->offMeshCons[index].userId})) return false;
        }
        if(traversal && (traversal->unavailable || traversal->Find({tile->header->x,tile->header->y})))
        { encounteredUnavailable=true; return false; }
        return true;
    }
};
// Retain every bounded native wall candidate, including back-facing edges.
// Re-evaluate distance/facing and nearest K at the actual position on every hit.
struct VansNavigationBoundaryCache
{
    std::array<std::array<glm::vec3,2>,32u*DT_VERTS_PER_POLYGON*3u> segments{};
    std::size_t count=0;
    glm::vec3 center{0};
    float radius=0;
    int maximumPolygons=0;
    dtPolyRef firstPolygon=0;
    std::uint64_t traversalRevision=0;
};
struct VansNavigationQueryState
{
	explicit VansNavigationQueryState(const VansNavigationQuerySettings& settings)
		: polygons(static_cast<std::size_t>(settings.maximumCorridorPolygons)),
		  pathPoints(static_cast<std::size_t>(settings.maximumPathPoints) * 3u)
	{
		pathFlags.resize(static_cast<std::size_t>(settings.maximumPathPoints));
        corridorTiles.reserve(static_cast<std::size_t>(settings.maximumCorridorPolygons));
        candidateTiles.reserve(static_cast<std::size_t>(settings.maximumCorridorPolygons));
	}
	std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{ nullptr, &dtFreeNavMeshQuery };
	VansNavigationDetourFilter filter;
	dtPathCorridor corridor;
 dtPathCorridor repairCandidate;
	float nearestStart[3]{};
	float nearestEnd[3]{};
	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;
	dtStatus searchStatus = 0;
	bool searching = false;
	bool corridorReady = false;
    std::uint64_t sliceRevision=0;
    bool encounteredUnavailable=false;
    std::vector<VansNavigationTileDependency> corridorTiles;
    std::vector<VansNavigationTileDependency> candidateTiles;
	int optimizationIterations = 0;
	bool uniformTraversalCost = true;
	std::vector<dtPolyRef> polygons;
	std::vector<float> pathPoints;
	std::vector<unsigned char> pathFlags;
	glm::vec3 requestedStart{0},requestedEnd{0};
	VansNavigationGoalAcceptance goalAcceptance=VansNavigationGoalAcceptance::ProjectedPoint;
 enum class SpatialKind { None,Raycast,Boundary };
 SpatialKind spatialKind=SpatialKind::None;
 dtPolyRef projectedRef=0;
 dtSlicedRaycastState rayState{};
 dtSlicedWallDistanceState boundaryState{};
 dtSlicedMoveAlongSurfaceState followState{};
 glm::vec3 followActual{0};
 bool followPending=false;
 bool targetPending=false;
 glm::vec3 repairGoal{0},repairExtents{0};
 VansNavigationGoalAcceptance repairAcceptance=VansNavigationGoalAcceptance::ProjectedPoint;
 float repairTolerance=0;
 VansNavigationPointProjection repairProjection;
 dtPolyRef repairRef=0;
 VansNavigationRaycast rayResult;
 VansNavigationBoundary boundaryResult;
 VansNavigationBoundaryCache localBoundary;

};

std::uint64_t EstimateNavigationQueryBytes(const VansNavigationQuerySettings& settings)
{
    // Native nodes/hash/open list, corridors, fixed state, and both the owned
    // boundary cache and its atomic refresh candidate are charged before allocation.
    return static_cast<std::uint64_t>(settings.maximumActiveAgents+settings.maximumActiveQueries) *
        (static_cast<std::uint64_t>(settings.maximumSearchNodes)*128u+
         static_cast<std::uint64_t>(settings.maximumCorridorPolygons)*(64u+4u*sizeof(VansNavigationTileDependency))+
         static_cast<std::uint64_t>(settings.maximumPathPoints)*32u+8192u+sizeof(VansNavigationQueryState)+
         sizeof(VansNavigationQueryContext)+sizeof(VansNavigationBoundaryCache))+
        static_cast<std::uint64_t>(settings.maximumActiveAgents)*(2u*kMaximumNavigationLinkCapabilities*sizeof(VansNavigationLinkKey)+16384u)+
        static_cast<std::uint64_t>(settings.maximumActiveQueries)*kMaximumNavigationLinkCapabilities*sizeof(VansNavigationLinkKey)+
        static_cast<std::uint64_t>(settings.maximumQueuedQueries)*(4096u+kMaximumNavigationLinkCapabilities*sizeof(VansNavigationLinkKey))+
        static_cast<std::uint64_t>(settings.maximumQueryHistory)*
            (static_cast<std::uint64_t>(settings.maximumPathPoints)*32u+
             static_cast<std::uint64_t>(settings.maximumCorridorPolygons)*(16u+2u*sizeof(VansNavigationTileDependency))+
             4096u+kMaximumNavigationLinkCapabilities*sizeof(VansNavigationLinkKey));
}

// 仅在领域实现中解释 polygon 身份；调用者只能传递不可变路线。
struct VansNavigationCorridorRoute
{
	std::weak_ptr<const VansNavigationMap> map;
	std::uint64_t contentRevision=0;
	std::string filterId;
	glm::vec3 start{0},end{0};
	std::vector<dtPolyRef> polygons;
    std::vector<VansNavigationTileDependency> tiles;
};

struct VansNavigationQueryContext::ReadLease
{
    std::shared_lock<std::shared_mutex> mesh;
    std::shared_ptr<const VansNavigationTraversalState> traversal;
    VansNavigationDetourFilter* filter=nullptr;
    explicit ReadLease(const VansNavigationQueryContext& context)
    {
        if(context.m_Map)
        {
            mesh=std::shared_lock<std::shared_mutex>(context.m_Map->m_AccessMutex);
            traversal=std::atomic_load(&context.m_Map->m_Traversal);
        }
        if(context.m_State)
        {
            filter=&context.m_State->filter;
            filter->traversal=traversal.get(); filter->encounteredUnavailable=false;
        }
    }
    ~ReadLease() { if(filter) { filter->traversal=nullptr; } }
};

namespace
{
bool IsFinite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool ProjectionFits(const dtNavMesh& mesh,const glm::vec3& point,const glm::vec3& extents)
{
    const auto* grid=mesh.getParams();
    if(!IsFinite(point-extents) || !IsFinite(point+extents)) return false;
    for(int axis:{0,2})
    {
        const double width=axis==0?grid->tileWidth:grid->tileHeight;
        const auto lower=std::floor((static_cast<double>(point[axis])-extents[axis]-grid->orig[axis])/width);
        const auto upper=std::floor((static_cast<double>(point[axis])+extents[axis]-grid->orig[axis])/width);
        // 在 native float→int 换算前拒绝坐标溢出及无界 AABB 扫描。
        if(!std::isfinite(lower) || !std::isfinite(upper) || lower<-(1<<28) || upper>(1<<28) || upper-lower>=32) return false;
    }
    return true;
}
// Detour 的 AABB 只筛选 polygon 包围盒；最终投影还必须满足调用方的精确范围。
// 保留 native 的净空/台阶评分，在有效候选中选最近点，不把首个越界候选当作无解。
dtStatus FindBoundedProjection(const dtNavMeshQuery& query,const float* center,const float* extents,
    const dtQueryFilter& filter,dtPolyRef& nearestRef,float* nearestPoint)
{
    class VansNavigationBoundedProjection final : public dtPolyQuery
    {
    public:
        const dtNavMeshQuery& query;
        const float* center;
        const float* extents;
        dtPolyRef ref=0;
        float point[3]{};
        float distanceSquared=(std::numeric_limits<float>::max)();
        dtStatus status=DT_SUCCESS;
        VansNavigationBoundedProjection(const dtNavMeshQuery& query,const float* center,const float* extents)
            :query(query),center(center),extents(extents) {}
        void process(const dtMeshTile* tile,dtPoly**,dtPolyRef* refs,int count) override
        {
            for(int index=0;index<count;++index)
            {
                float candidate[3]{}; bool overPolygon=false;
                const auto result=query.closestPointOnPoly(refs[index],center,candidate,&overPolygon);
                if(dtStatusFailed(result)) { status=result; continue; }
                const glm::vec3 delta(center[0]-candidate[0],center[1]-candidate[1],center[2]-candidate[2]);
                if(!IsFinite(delta) || std::abs(delta.x)>extents[0] ||
                    std::abs(delta.y)>extents[1] || std::abs(delta.z)>extents[2]) continue;
                const auto vertical=(std::max)(0.f,std::abs(delta.y)-tile->header->walkableClimb);
                const auto score=overPolygon?vertical*vertical:glm::dot(delta,delta);
                if(score<distanceSquared)
                {
                    distanceSquared=score; ref=refs[index]; dtVcopy(point,candidate);
                }
            }
        }
    } projection(query,center,extents);
    const auto status=query.queryPolygons(center,extents,&filter,&projection);
    nearestRef=0;
    if(dtStatusFailed(status)) return status;
    if(dtStatusFailed(projection.status)) return projection.status;
    nearestRef=projection.ref;
    if(nearestRef) dtVcopy(nearestPoint,projection.point);
    return DT_SUCCESS;
}
VansNavigationPath MakePathFailure(VansNavigationPathFailure failure,
	const char* diagnostic)
{
	VansNavigationPath result;
	result.status = VansNavigationPathStatus::Failed;
	result.failure = failure;
	result.diagnostic = diagnostic;
	return result;
}

}

VansNavigationQueryContext::VansNavigationQueryContext() = default;
VansNavigationQueryContext::~VansNavigationQueryContext() = default;

bool VansNavigationQueryContext::Initialize(std::shared_ptr<const VansNavigationMap> mesh,
	const VansNavigationQuerySettings& settings, const std::string& filterId, std::string& error)
{
    if (!mesh) { error = "Navigation query requires a map"; return false; }
    std::shared_lock<std::shared_mutex> lease(mesh->m_AccessMutex);
    if (!mesh->m_NavMesh)
	{
		error = "Navigation query requires a ready mesh";
		return false;
	}
    const auto filter = std::find_if(mesh->m_Filters.begin(), mesh->m_Filters.end(),
        [&](const auto& candidate) { return candidate.id == filterId; });
    if (filter == mesh->m_Filters.end())
    { error = "Navigation query filter is unavailable: " + filterId; return false; }
	if (!ValidateNavigationQuerySettings(settings, error)) return false;
	std::unique_ptr<VansNavigationQueryState> candidate;
	try
	{
		candidate = std::make_unique<VansNavigationQueryState>(settings);
	}
	catch (const std::bad_alloc&)
	{
		error = "Could not allocate navigation query buffers";
		return false;
	}
	candidate->query.reset(dtAllocNavMeshQuery());
	if (!candidate->query || dtStatusFailed(candidate->query->init(mesh->m_NavMesh, settings.maximumSearchNodes)))
	{
		error = "Detour could not initialize navigation query state";
		return false;
	}
	if (!candidate->corridor.init(settings.maximumCorridorPolygons) ||
        !candidate->repairCandidate.init(settings.maximumCorridorPolygons))
	{
		error = "Could not allocate navigation corridor";
		return false;
	}
	candidate->filter.setIncludeFlags(0x1u);
	candidate->filter.setExcludeFlags(0);
	candidate->filter.allowed = filter->allowed;
    for (std::size_t area = 0; area < filter->costs.size(); ++area)
        candidate->filter.setAreaCost(static_cast<int>(area), filter->costs[area]);
	float firstCost = 0;
	for (std::size_t area = 0; area < filter->costs.size(); ++area)
		if (filter->allowed[area])
		{
			if (firstCost == 0) firstCost = filter->costs[area];
			else if (firstCost != filter->costs[area]) candidate->uniformTraversalCost = false;
		}
	candidate->optimizationIterations = (std::min)(32, settings.maximumIterationsPerSlice);
	m_ContentRevision = mesh->m_ContentRevision;
	// 先销毁旧 scratch，再释放其资源 owner，防止查询留下 native 悬挂引用。
	m_State = std::move(candidate);
	m_Map = std::move(mesh);
	m_FilterId=filterId;
	error.clear();
	return true;
}

bool VansNavigationQueryContext::SetLinkCapabilities(const std::vector<VansNavigationLinkKey>& keys,std::string& error)
{
    if(!m_State || !m_Map) { error="Navigation Link capabilities require an initialized query owner"; return false; }
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities> candidate{};
    if(!CompileNavigationLinkCapabilities(keys,candidate,error)) return false;
    ReadLease lease(*this);
    auto& filter=m_State->filter;
    if(filter.linkCount!=keys.size() || !std::equal(candidate.begin(),candidate.begin()+keys.size(),filter.links.begin()))
    {
        ResetQueryUnderLease(); filter.links=candidate; filter.linkCount=keys.size();
    }
    error.clear(); return true;
}

VansNavigationPathFailure VansNavigationQueryContext::CheckReadyUnderLease(bool requireStableSlice) const
{
	if (!m_State || !m_Map) return VansNavigationPathFailure::QueryNotReady;
	if (m_ContentRevision != m_Map->m_ContentRevision) return VansNavigationPathFailure::MeshChanged;
	if (!m_Map->m_NavMesh) return VansNavigationPathFailure::MeshNotReady;
    const auto* traversal=m_State->filter.traversal;
    if(!traversal || traversal->unavailable) return VansNavigationPathFailure::DataUnavailable;
    if(requireStableSlice && m_State->sliceRevision!=traversal->revision) return VansNavigationPathFailure::MeshChanged;
	return VansNavigationPathFailure::None;
}

VansNavigationPathFailure VansNavigationQueryContext::ValidateCorridorUnderLease(
    const std::vector<VansNavigationTileDependency>& tiles) const
{
    const auto ready=CheckReadyUnderLease();
    if(ready!=VansNavigationPathFailure::None) return ready;
    for(const auto& tile:tiles)
    {
        if(m_State->filter.traversal->Find({tile.x,tile.z})) return VansNavigationPathFailure::DataUnavailable;
        const auto ref=m_Map->m_NavMesh->getTileRefAt(tile.x,tile.z,tile.layer);
        if(!ref || !tile.contentEpoch ||
            m_Map->m_TileData[m_Map->m_NavMesh->decodePolyIdTile(ref)].contentEpoch!=tile.contentEpoch)
            return VansNavigationPathFailure::MeshChanged;
    }
    return VansNavigationPathFailure::None;
}

VansNavigationPath VansNavigationQueryContext::FindPath(const glm::vec3& start,
	const glm::vec3& end, const glm::vec3& nearestExtents,VansNavigationGoalAcceptance acceptance,float projectionToleranceMeters)
{
	VansNavigationPath result = BeginPath(start, end, nearestExtents,acceptance,projectionToleranceMeters);
	while (result.status == VansNavigationPathStatus::None)
	{
		int iterationsUsed = 0;
		result = AdvancePath(256, iterationsUsed);
	}
	if(IsFinite(start) && IsFinite(end))
	{ result.hasRequestCoordinates=true; result.requestedStart=start; result.requestedEnd=end; result.goalAcceptance=acceptance; }
	return result;
}

VansNavigationPointProjection VansNavigationQueryContext::ProjectPoint(const glm::vec3& position,const glm::vec3& extents)
{
 ReadLease lease(*this);
 return ProjectPointUnderLease(position,extents);
}
VansNavigationPointProjection VansNavigationQueryContext::ProjectPointUnderLease(const glm::vec3& position,const glm::vec3& extents)
{
 VansNavigationPointProjection result; result.requested=position;
	result.failure=CheckReadyUnderLease();
	if(result.failure!=VansNavigationPathFailure::None) { result.diagnostic="Navigation query is not ready or has changed"; return result; }
	if(!IsFinite(position) || !IsFinite(extents) || glm::any(glm::lessThanEqual(extents,glm::vec3(0))) ||
		!ProjectionFits(*m_Map->m_NavMesh,position,extents))
	{ result.failure=VansNavigationPathFailure::InvalidInput; result.diagnostic="Projection exceeds supported coordinate or Tile scan bounds"; return result; }
	auto& polygon=m_State->projectedRef; polygon=0; float point[3]{};
	const float input[]{position.x,position.y,position.z},range[]{extents.x,extents.y,extents.z};
    int cellX=0,cellZ=0; m_Map->m_NavMesh->calcTileLoc(input,&cellX,&cellZ);
    if(m_State->filter.traversal->Find({cellX,cellZ}))
    { result.failure=VansNavigationPathFailure::DataUnavailable; result.diagnostic="Projection starts in a Dirty cell"; return result; }
    const auto projected=FindBoundedProjection(*m_State->query,input,range,m_State->filter,polygon,point);
    if(m_State->filter.encounteredUnavailable)
    { result.failure=VansNavigationPathFailure::DataUnavailable; result.diagnostic="Projection intersects unavailable Tile data"; return result; }
	if(dtStatusFailed(projected) || !polygon)
	{ result.failure=VansNavigationPathFailure::StartOutsideMesh; result.diagnostic="No navigable point within projection bounds"; return result; }
	result.position={point[0],point[1],point[2]}; result.distance=glm::length(result.position-position);
	result.valid=true; result.diagnostic="Projected"; return result;
}

namespace
{
template<class T> T SpatialFailure(VansNavigationPathFailure failure,const char* diagnostic)
{ T result; result.finished=true; result.failure=failure; result.diagnostic=diagnostic; return result; }
template<class EpochLookup>
bool CaptureTileDependencies(const dtNavMesh& mesh,const dtPolyRef* refs,int count,
    std::vector<VansNavigationTileDependency>& result,const EpochLookup& epoch)
{
 result.clear(); result.reserve(static_cast<std::size_t>(count));
 for(int index=0;index<count;++index)
 {
  const dtMeshTile* tile=nullptr; const dtPoly* polygon=nullptr;
  if(dtStatusFailed(mesh.getTileAndPolyByRef(refs[index],&tile,&polygon))) return false;
  result.push_back({tile->header->x,tile->header->y,tile->header->layer,epoch(refs[index])});
 }
 std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return std::tie(a.x,a.z,a.layer)<std::tie(b.x,b.z,b.layer);});
 result.erase(std::unique(result.begin(),result.end(),[](const auto& a,const auto& b)
 { return a.x==b.x && a.z==b.z && a.layer==b.layer; }),result.end());
 return true;
}
}
VansNavigationRaycast VansNavigationQueryContext::NavRaycast(const glm::vec3& start,const glm::vec3& end,const glm::vec3& extents)
{
 auto result=BeginRaycast(start,end,extents);
 while(!result.finished) { int used=0; result=AdvanceRaycast(256,used); }
 return result;
}
VansNavigationRaycast VansNavigationQueryContext::BeginRaycast(const glm::vec3& start,const glm::vec3& end,const glm::vec3& extents)
{
 ReadLease lease(*this);
 const auto ready=CheckReadyUnderLease();
 if(ready!=VansNavigationPathFailure::None) return SpatialFailure<VansNavigationRaycast>(ready,"Navigation Map is not ready or has changed");
 std::string error;
 if(!IsFinite(end) || !ValidateNavigationSpatialDistance(glm::length(glm::dvec3(end)-glm::dvec3(start)),error))
  return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::InvalidInput,"Ray endpoints exceed the finite spatial distance limit");
 const auto projected=ProjectPointUnderLease(start,extents);
 if(!projected.valid) return SpatialFailure<VansNavigationRaycast>(projected.failure,projected.diagnostic.c_str());
 ResetQueryUnderLease();
 auto& result=m_State->rayResult; result={}; result.requestedStart=start; result.requestedEnd=end;
 result.projectedStart=projected.position; result.projectionDistance=projected.distance;
 const float from[]{projected.position.x,projected.position.y,projected.position.z},to[]{end.x,end.y,end.z};
 const auto status=m_State->query->initSlicedRaycast(m_State->projectedRef,from,to,&m_State->filter,0,
  m_State->polygons.data(),static_cast<int>(m_State->polygons.size()),m_State->rayState);
 if(dtStatusFailed(status)) return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::SpatialQueryFailed,"Could not begin navigation raycast");
 m_State->spatialKind=VansNavigationQueryState::SpatialKind::Raycast;
 m_State->sliceRevision=m_State->filter.traversal->revision;
 return result;
}
VansNavigationRaycast VansNavigationQueryContext::AdvanceRaycast(int maximumIterations,int& iterationsUsed)
{
 iterationsUsed=0;
 ReadLease lease(*this);
 const auto ready=CheckReadyUnderLease(true);
 if(ready!=VansNavigationPathFailure::None) return SpatialFailure<VansNavigationRaycast>(ready,"Navigation Map changed during raycast");
 if(maximumIterations<1 || m_State->spatialKind!=VansNavigationQueryState::SpatialKind::Raycast)
  return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::InvalidInput,"Raycast needs an active request and positive iteration budget");
 auto& result=m_State->rayResult;
 const auto status=m_State->query->updateSlicedRaycast(m_State->rayState,maximumIterations,&iterationsUsed);
 m_State->encounteredUnavailable=m_State->encounteredUnavailable || m_State->filter.encounteredUnavailable;
 if(dtStatusDetail(status,DT_BUFFER_TOO_SMALL))
  return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::RaycastCapacityExceeded,"Raycast exceeded its polygon result capacity");
 if(dtStatusFailed(status)) return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::SpatialQueryFailed,"Navigation raycast failed");
 if(dtStatusInProgress(status)) return result;
 m_State->spatialKind=VansNavigationQueryState::SpatialKind::None;
 const auto& hit=m_State->rayState.hit;
 if(hit.t<=1 && m_State->encounteredUnavailable)
  return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::DataUnavailable,"Raycast reached unavailable Tile data");
 result.hit=hit.t<=1; result.fraction=result.hit?hit.t:1;
 result.position=glm::mix(result.projectedStart,result.requestedEnd,result.fraction);
 result.normal={hit.hitNormal[0],hit.hitNormal[1],hit.hitNormal[2]};
 const float point[]{result.position.x,result.position.y,result.position.z}; float surface[3]{};
 // 射线与 polygon 边界的浮点交点可能略在外侧；同一 polygon 的最近点同时解析边界和 detail 高度。
 if(hit.pathCount<=0 || dtStatusFailed(m_State->query->closestPointOnPoly(
     m_State->polygons[hit.pathCount-1],point,surface,nullptr)) ||
     glm::length(glm::vec2(surface[0]-point[0],surface[2]-point[2]))>.01f)
  return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::SpatialQueryFailed,"Could not resolve raycast surface height");
 result.position={surface[0],surface[1],surface[2]};
 if(!CaptureTileDependencies(*m_Map->m_NavMesh,m_State->polygons.data(),hit.pathCount,result.tiles,
    [&](auto ref){return m_Map->m_TileData[m_Map->m_NavMesh->decodePolyIdTile(ref)].contentEpoch;}))
  return SpatialFailure<VansNavigationRaycast>(VansNavigationPathFailure::MeshChanged,"Raycast contains an unavailable Tile");
 result.finished=true; result.valid=true; result.diagnostic=result.hit?"BoundaryHit":"Clear";
 return result;
}
VansNavigationBoundary VansNavigationQueryContext::FindNearestBoundary(const glm::vec3& position,float radiusMeters,const glm::vec3& extents)
{
 auto result=BeginNearestBoundary(position,radiusMeters,extents);
 while(!result.finished) { int used=0; result=AdvanceNearestBoundary(256,used); }
 return result;
}
VansNavigationBoundary VansNavigationQueryContext::BeginNearestBoundary(const glm::vec3& position,float radiusMeters,const glm::vec3& extents)
{
 ReadLease lease(*this);
 const auto ready=CheckReadyUnderLease();
 if(ready!=VansNavigationPathFailure::None) return SpatialFailure<VansNavigationBoundary>(ready,"Navigation Map is not ready or has changed");
 std::string error;
 if(!ValidateNavigationSpatialDistance(radiusMeters,error)) return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::InvalidInput,error.c_str());
 const auto projected=ProjectPointUnderLease(position,extents);
 if(!projected.valid) return SpatialFailure<VansNavigationBoundary>(projected.failure,projected.diagnostic.c_str());
 ResetQueryUnderLease();
 auto& result=m_State->boundaryResult; result={}; result.requested=position; result.projected=projected.position;
 result.projectionDistance=projected.distance;
 const float point[]{projected.position.x,projected.position.y,projected.position.z};
 const auto status=m_State->query->initSlicedDistanceToWall(m_State->projectedRef,point,radiusMeters,&m_State->filter,m_State->boundaryState);
 if(dtStatusFailed(status)) return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::SpatialQueryFailed,"Could not begin boundary search");
 m_State->spatialKind=VansNavigationQueryState::SpatialKind::Boundary;
 m_State->sliceRevision=m_State->filter.traversal->revision;
 return result;
}
VansNavigationBoundary VansNavigationQueryContext::AdvanceNearestBoundary(int maximumIterations,int& iterationsUsed)
{
 iterationsUsed=0;
 ReadLease lease(*this);
 const auto ready=CheckReadyUnderLease(true);
 if(ready!=VansNavigationPathFailure::None) return SpatialFailure<VansNavigationBoundary>(ready,"Navigation Map changed during boundary search");
 if(maximumIterations<1 || m_State->spatialKind!=VansNavigationQueryState::SpatialKind::Boundary)
  return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::InvalidInput,"Boundary search needs an active request and positive iteration budget");
 const auto status=m_State->query->updateSlicedDistanceToWall(m_State->boundaryState,maximumIterations,&iterationsUsed);
 m_State->encounteredUnavailable=m_State->encounteredUnavailable || m_State->filter.encounteredUnavailable;
 if(dtStatusDetail(status,DT_BUFFER_TOO_SMALL))
  return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::BoundaryCapacityExceeded,"Boundary polygon exceeded its segment or portal interval capacity");
 if(dtStatusDetail(status,DT_OUT_OF_NODES))
  return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::SearchNodeCapacityExceeded,"Boundary search exceeded its node capacity");
 if(dtStatusFailed(status)) return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::SpatialQueryFailed,"Navigation boundary search failed");
 auto& result=m_State->boundaryResult;
 if(dtStatusInProgress(status)) return result;
 if(m_State->encounteredUnavailable)
  return SpatialFailure<VansNavigationBoundary>(VansNavigationPathFailure::DataUnavailable,"Boundary search reached unavailable Tile data");
 m_State->spatialKind=VansNavigationQueryState::SpatialKind::None;
 const auto& boundary=m_State->boundaryState;
 result.found=boundary.found; result.distance=std::sqrt(boundary.radiusSqr);
 result.position={boundary.hitPos[0],boundary.hitPos[1],boundary.hitPos[2]};
 result.normal={boundary.hitNormal[0],boundary.hitNormal[1],boundary.hitNormal[2]};
 result.finished=true; result.valid=true; result.diagnostic=result.found?"BoundaryFound":"NoBoundaryWithinRadius";
 return result;
}

VansNavigationPath VansNavigationQueryContext::BeginPath(const glm::vec3& start,
	const glm::vec3& end, const glm::vec3& nearestExtents,VansNavigationGoalAcceptance acceptance,float projectionToleranceMeters)
{
    // 仅当前 native 调用量子持有读 lease；sliced 暂停期间不阻挡 Map 写事务。
    ReadLease lease(*this);

	const auto readiness = CheckReadyUnderLease();
	if (readiness != VansNavigationPathFailure::None)
		return MakePathFailure(readiness, "Navigation query resource is not ready or has changed");
	std::string acceptanceError;
	if(!ValidateNavigationGoalProjection(acceptance,projectionToleranceMeters,acceptanceError))
		return MakePathFailure(VansNavigationPathFailure::InvalidInput,acceptanceError.c_str());
	if (!IsFinite(start) || !IsFinite(end) || !IsFinite(nearestExtents) ||
		nearestExtents.x <= 0.0f || nearestExtents.y <= 0.0f || nearestExtents.z <= 0.0f)
		return MakePathFailure(VansNavigationPathFailure::InvalidInput, "Navigation points must be finite and projection extents positive and finite");
    if(!ProjectionFits(*m_Map->m_NavMesh,start,nearestExtents) || !ProjectionFits(*m_Map->m_NavMesh,end,nearestExtents))
        return MakePathFailure(VansNavigationPathFailure::InvalidInput,"Projection exceeds supported coordinate or Tile scan bounds");
	ResetQueryUnderLease();
	m_State->requestedStart=start; m_State->requestedEnd=end;
	m_State->goalAcceptance=acceptance;
	const dtQueryFilter& filter = m_State->filter;
	const float extents[3] = { nearestExtents.x, nearestExtents.y, nearestExtents.z };
	const float startPoint[3] = { start.x, start.y, start.z };
	const float endPoint[3] = { end.x, end.y, end.z };
    int cellX=0,cellZ=0;
    for(const auto* point:{startPoint,endPoint})
    {
        m_Map->m_NavMesh->calcTileLoc(point,&cellX,&cellZ);
        if(m_State->filter.traversal->Find({cellX,cellZ}))
            return MakePathFailure(VansNavigationPathFailure::DataUnavailable,"Path endpoint lies in a Dirty cell");
    }
	auto& startRef = m_State->startRef;
	auto& endRef = m_State->endRef;
	auto& nearestStart = m_State->nearestStart;
	auto& nearestEnd = m_State->nearestEnd;
    const auto startStatus=FindBoundedProjection(*m_State->query,startPoint,extents,filter,startRef,nearestStart);
    if(m_State->filter.encounteredUnavailable)
        return MakePathFailure(VansNavigationPathFailure::DataUnavailable,"Path start projection intersects unavailable Tile data");
	if (dtStatusFailed(startStatus) || startRef == 0)
		return MakePathFailure(VansNavigationPathFailure::StartOutsideMesh,
			"Start point is outside the navigation mesh");
    const auto endStatus=FindBoundedProjection(*m_State->query,endPoint,extents,filter,endRef,nearestEnd);
    if(m_State->filter.encounteredUnavailable)
        return MakePathFailure(VansNavigationPathFailure::DataUnavailable,"Path end projection intersects unavailable Tile data");
	if (dtStatusFailed(endStatus) || endRef == 0)
		return MakePathFailure(VansNavigationPathFailure::EndOutsideMesh,
			"End point is outside the navigation mesh");
	if(acceptance==VansNavigationGoalAcceptance::RequestedPoint &&
		glm::length(end-glm::vec3(nearestEnd[0],nearestEnd[1],nearestEnd[2]))>projectionToleranceMeters)
	{
        auto result=MakePathFailure(VansNavigationPathFailure::EndProjectionRejected,"Projected endpoint exceeds the requested-point acceptance tolerance");
        result.hasRequestCoordinates=true; result.hasProjection=true; result.goalAcceptance=acceptance;
        result.requestedStart=start; result.requestedEnd=end;
        result.projectedStart={nearestStart[0],nearestStart[1],nearestStart[2]};
        result.projectedEnd={nearestEnd[0],nearestEnd[1],nearestEnd[2]};
        result.startProjectionDistance=glm::length(start-result.projectedStart);
        result.endProjectionDistance=glm::length(end-result.projectedEnd);
        return result;
    }
	m_State->searchStatus = m_State->query->initSlicedFindPath(startRef, endRef,
		nearestStart, nearestEnd, &m_State->filter);
	if (dtStatusFailed(m_State->searchStatus))
		return MakePathFailure(VansNavigationPathFailure::CorridorSearchFailed,
			"Detour could not begin a polygon search");
	m_State->searching = true;
    m_State->sliceRevision=m_State->filter.traversal->revision;
	return {};
}

VansNavigationPath VansNavigationQueryContext::AdvancePath(int maximumIterations,
	int& iterationsUsed)
{
    // 仅当前 native 调用量子持有读 lease；sliced 暂停期间不阻挡 Map 写事务。
    ReadLease lease(*this);

	iterationsUsed = 0;
	const auto readiness = CheckReadyUnderLease(true);
	if (readiness != VansNavigationPathFailure::None)
		return MakePathFailure(readiness, "Navigation query resource is not ready or has changed");
	if (maximumIterations < 1 || !m_State->searching)
		return MakePathFailure(VansNavigationPathFailure::InvalidInput,
			"Navigation sliced search requires an active request and positive iteration budget");
	m_State->searchStatus = m_State->query->updateSlicedFindPath(maximumIterations, &iterationsUsed);
    m_State->encounteredUnavailable=m_State->encounteredUnavailable || m_State->filter.encounteredUnavailable;
	if (dtStatusInProgress(m_State->searchStatus)) return {};
	m_State->searching = false;
	return FinishPath();
}

VansNavigationPath VansNavigationQueryContext::FinishPath()
{
	auto& polygons = m_State->polygons;
	int polygonCount = 0;
	const dtStatus pathStatus = m_State->query->finalizeSlicedFindPath(polygons.data(),
		&polygonCount, static_cast<int>(polygons.size())) | (m_State->searchStatus & DT_STATUS_DETAIL_MASK);
	if (dtStatusDetail(pathStatus, DT_OUT_OF_NODES))
		return MakePathFailure(VansNavigationPathFailure::SearchNodeCapacityExceeded,
			"Navigation path search exceeded its node capacity");
	if (dtStatusDetail(pathStatus, DT_BUFFER_TOO_SMALL))
		return MakePathFailure(VansNavigationPathFailure::CorridorCapacityExceeded,
			"Navigation polygon corridor exceeded its configured capacity");
	if (dtStatusFailed(pathStatus) || polygonCount <= 0)
		return MakePathFailure(VansNavigationPathFailure::CorridorSearchFailed,
			"Detour could not find a polygon corridor");
	const auto& nearestStart = m_State->nearestStart;
	const auto& nearestEnd = m_State->nearestEnd;
	const auto endRef = m_State->endRef;
	float straightEnd[3] = { nearestEnd[0], nearestEnd[1], nearestEnd[2] };
	const bool complete = polygons[static_cast<std::size_t>(polygonCount - 1)] == endRef;
    if(!complete && m_State->encounteredUnavailable)
        return MakePathFailure(VansNavigationPathFailure::DataUnavailable,"Path search reached unavailable Tile data");
	if (!complete)
	{
		if (dtStatusFailed(m_State->query->closestPointOnPoly(
			polygons[static_cast<std::size_t>(polygonCount - 1)], nearestEnd,
			straightEnd, nullptr)))
			return MakePathFailure(
				VansNavigationPathFailure::PartialEndpointResolutionFailed,
				"Detour could not resolve a reachable partial endpoint");
	}

 VansNavigationPath metadata;
 metadata.goalAcceptance=m_State->goalAcceptance; metadata.hasRequestCoordinates=true;
 metadata.requestedStart=m_State->requestedStart; metadata.requestedEnd=m_State->requestedEnd;
 metadata.projectedStart={nearestStart[0],nearestStart[1],nearestStart[2]};
 metadata.projectedEnd={nearestEnd[0],nearestEnd[1],nearestEnd[2]};
 metadata.startProjectionDistance=glm::length(metadata.requestedStart-metadata.projectedStart);
 metadata.endProjectionDistance=glm::length(metadata.requestedEnd-metadata.projectedEnd);
 const glm::vec3 endpoint(straightEnd[0],straightEnd[1],straightEnd[2]);
 auto result=CapturePolygonPath(polygonCount,metadata.projectedStart,endpoint,metadata,complete);
 if(result.status==VansNavigationPathStatus::Failed) return result;
 m_State->corridorTiles=result.tiles;
 m_State->corridor.reset(m_State->startRef,nearestStart);
 m_State->corridor.setCorridor(straightEnd,polygons.data(),polygonCount);
 m_State->corridorReady=true;
 return result;
}
VansNavigationPath VansNavigationQueryContext::CapturePolygonPath(int polygonCount,const glm::vec3& start,const glm::vec3& end,
 const VansNavigationPath& metadata,bool complete)
{
 auto& polygons=m_State->polygons;
 const float nativeStart[]{start.x,start.y,start.z},nativeEnd[]{end.x,end.y,end.z};
	std::vector<float>& straightPoints = m_State->pathPoints;
	int straightCount = 0;
	const dtStatus straightStatus = m_State->query->findStraightPath(nativeStart,
		nativeEnd, polygons.data(), polygonCount, straightPoints.data(),
		m_State->pathFlags.data(), nullptr, &straightCount,
		static_cast<int>(m_State->pathPoints.size() / 3u));
	if (dtStatusDetail(straightStatus, DT_BUFFER_TOO_SMALL))
		return MakePathFailure(VansNavigationPathFailure::CornerCapacityExceeded,
			"Navigation path corners exceeded their configured capacity");
	if (dtStatusFailed(straightStatus) || straightCount <= 0)
		return MakePathFailure(VansNavigationPathFailure::CornerExtractionFailed,
			"Detour could not extract path corners");
	VansNavigationPath result;
 result.hasProjection=true;
 result.goalAcceptance=metadata.goalAcceptance; result.hasRequestCoordinates=metadata.hasRequestCoordinates;
 result.requestedStart=metadata.requestedStart; result.requestedEnd=metadata.requestedEnd;
 result.projectedStart=metadata.projectedStart; result.projectedEnd=metadata.projectedEnd;
 result.reachedEnd=end; result.startProjectionDistance=metadata.startProjectionDistance;
 result.endProjectionDistance=metadata.endProjectionDistance;
	result.cornerFlags.reserve(static_cast<std::size_t>(straightCount));
	result.points.reserve(static_cast<std::size_t>(straightCount));
	for (int index = 0; index < straightCount; ++index)
	{
		const float* point = straightPoints.data() + index * 3;
		result.points.emplace_back(point[0], point[1], point[2]);
		std::uint8_t flags=0;
		if(m_State->pathFlags[index]&DT_STRAIGHTPATH_START) flags|=static_cast<std::uint8_t>(VansNavigationCornerFlag::Start);
		if(m_State->pathFlags[index]&DT_STRAIGHTPATH_END) flags|=static_cast<std::uint8_t>(VansNavigationCornerFlag::End);
		if(m_State->pathFlags[index]&DT_STRAIGHTPATH_OFFMESH_CONNECTION) flags|=static_cast<std::uint8_t>(VansNavigationCornerFlag::Link);
		result.cornerFlags.push_back(flags);
	}
 if(!CaptureTileDependencies(*m_Map->m_NavMesh,polygons.data(),polygonCount,result.tiles,
    [&](auto ref){return m_Map->m_TileData[m_Map->m_NavMesh->decodePolyIdTile(ref)].contentEpoch;}))
  return MakePathFailure(VansNavigationPathFailure::MeshChanged,"Path contains an unavailable Tile");
 const auto validity=ValidateCorridorUnderLease(result.tiles);
 if(validity!=VansNavigationPathFailure::None) return MakePathFailure(validity,"Path contains Dirty or replaced Tile data");
 result.status=complete?VansNavigationPathStatus::Complete:VansNavigationPathStatus::Partial;
 result.diagnostic=complete?"Complete":"Partial";
 return result;
}

std::shared_ptr<const VansNavigationCorridorRoute> VansNavigationQueryContext::CaptureCorridorRoute() const
{
	if(!m_Map) return {};
	ReadLease lease(*this);
	if(CheckReadyUnderLease()!=VansNavigationPathFailure::None || !m_State->corridorReady || m_State->searching) return {};
    if(ValidateCorridorUnderLease(m_State->corridorTiles)!=VansNavigationPathFailure::None) return {};
	auto route=std::make_shared<VansNavigationCorridorRoute>();
	route->map=m_Map; route->contentRevision=m_ContentRevision; route->filterId=m_FilterId;
	const auto* start=m_State->corridor.getPos(); const auto* end=m_State->corridor.getTarget();
	route->start={start[0],start[1],start[2]}; route->end={end[0],end[1],end[2]};
	route->polygons.assign(m_State->corridor.getPath(),m_State->corridor.getPath()+m_State->corridor.getPathCount());
    route->tiles=m_State->corridorTiles;
	return route;
}
VansNavigationPathFailure VansNavigationQueryContext::AdoptCorridorRoute(const std::shared_ptr<const VansNavigationCorridorRoute>& route)
{
	if(!m_Map) return VansNavigationPathFailure::QueryNotReady;
	ReadLease lease(*this);
	const auto ready=CheckReadyUnderLease();
	if(ready!=VansNavigationPathFailure::None) return ready;
	if(!route || route->map.lock()!=m_Map || route->contentRevision!=m_ContentRevision || route->filterId!=m_FilterId)
		return VansNavigationPathFailure::MeshChanged;
	if(route->polygons.empty() || route->polygons.size()>m_State->polygons.size()) return VansNavigationPathFailure::CorridorCapacityExceeded;
    const auto validity=ValidateCorridorUnderLease(route->tiles);
    if(validity!=VansNavigationPathFailure::None) return validity;
	for(const auto polygon:route->polygons)
		if(!m_State->query->isValidPolyRef(polygon,&m_State->filter)) return VansNavigationPathFailure::MeshChanged;
    auto dependencies=route->tiles;
	ResetQueryUnderLease();
	const float start[]{route->start.x,route->start.y,route->start.z},end[]{route->end.x,route->end.y,route->end.z};
	m_State->corridor.reset(route->polygons.front(),start);
	m_State->corridor.setCorridor(end,route->polygons.data(),static_cast<int>(route->polygons.size()));
	m_State->corridorReady=true;
    m_State->corridorTiles=std::move(dependencies);
    m_State->followPending=false; m_State->targetPending=false;
	return VansNavigationPathFailure::None;
}

void VansNavigationQueryContext::ResetQuery()
{
    ReadLease lease(*this);
    ResetQueryUnderLease();
}
void VansNavigationQueryContext::ResetQueryUnderLease()
{
	if (!m_State) return;
	m_State->localBoundary.firstPolygon=0;
	if (m_State->searching && CheckReadyUnderLease(true) == VansNavigationPathFailure::None)
	{
		int count = 0;
		m_State->query->finalizeSlicedFindPath(m_State->polygons.data(), &count,
			static_cast<int>(m_State->polygons.size()));
	}
	m_State->searching = false;
	m_State->corridorReady = false;
    m_State->corridorTiles.clear(); m_State->encounteredUnavailable=false;
    m_State->followPending=false;
    m_State->targetPending=false;
 m_State->spatialKind=VansNavigationQueryState::SpatialKind::None;
 m_State->rayResult={}; m_State->boundaryResult={};
}

VansNavigationLocalBoundary VansNavigationQueryContext::CaptureLocalBoundary(const glm::vec3& position,float radiusMeters,
    int maximumPolygons,int maximumSegments,int maximumIterations,int& iterationsUsed)
{
    iterationsUsed=0;
    VansNavigationLocalBoundary result;
    if(!m_Map) { result.failure=VansNavigationPathFailure::QueryNotReady; return result; }
    ReadLease lease(*this);
    result.failure=CheckReadyUnderLease(); if(result.failure!=VansNavigationPathFailure::None) return result;
    result.failure=ValidateCorridorUnderLease(m_State->corridorTiles); if(result.failure!=VansNavigationPathFailure::None) return result;
    if(!m_State->corridorReady || m_State->followPending || m_State->targetPending || !IsFinite(position) ||
        !std::isfinite(radiusMeters) || radiusMeters<=0 || radiusMeters>100 || maximumPolygons<1 || maximumPolygons>32 ||
        maximumSegments<1 || maximumSegments>static_cast<int>(kMaximumNavigationBoundarySegments) || maximumIterations<0)
    { result.failure=VansNavigationPathFailure::InvalidInput; return result; }
    const float p[]{position.x,position.y,position.z};
    auto& cache=m_State->localBoundary;
    const float margin=radiusMeters*.25f;
    const auto displacement=position-cache.center;
    const auto firstPolygon=m_State->corridor.getFirstPoly();
    if(!cache.firstPolygon || cache.firstPolygon!=firstPolygon || cache.radius!=radiusMeters ||
        cache.maximumPolygons!=maximumPolygons || cache.traversalRevision!=lease.traversal->revision ||
        glm::dot(displacement,displacement)>margin*margin)
    {
        // Reserve the conservative native work inside this read quantum. The
        // serial World charges iterationsUsed against the same remaining budget.
        const int required=64+maximumPolygons;
        if(maximumIterations<required) { result.pending=true; return result; }
        iterationsUsed=required;
        VansNavigationBoundaryCache candidate;
        candidate.center=position; candidate.radius=radiusMeters; candidate.maximumPolygons=maximumPolygons;
        candidate.firstPolygon=firstPolygon; candidate.traversalRevision=lease.traversal->revision;
        const float captureRadius=radiusMeters+margin;
        std::array<dtPolyRef,32> polygons{}; int polygonCount=0;
        const auto neighborhood=m_State->query->findLocalNeighbourhood(firstPolygon,p,captureRadius,&m_State->filter,
            polygons.data(),nullptr,&polygonCount,maximumPolygons);
        if(m_State->filter.encounteredUnavailable) { result.failure=VansNavigationPathFailure::DataUnavailable; return result; }
        if(dtStatusFailed(neighborhood) || dtStatusDetail(neighborhood,DT_BUFFER_TOO_SMALL) || dtStatusDetail(neighborhood,DT_OUT_OF_NODES))
        { result.failure=VansNavigationPathFailure::BoundaryCapacityExceeded; return result; }
        for(int polygon=0;polygon<polygonCount;++polygon)
        {
            // The same native wall/partial-portal decoder serves spatial queries and avoidance.
            std::array<float,DT_VERTS_PER_POLYGON*3*6> segments{}; int count=0;
            const auto status=m_State->query->getPolyWallSegments(polygons[static_cast<std::size_t>(polygon)],&m_State->filter,
                segments.data(),nullptr,&count,DT_VERTS_PER_POLYGON*3);
            if(m_State->filter.encounteredUnavailable) { result.failure=VansNavigationPathFailure::DataUnavailable; return result; }
            if(dtStatusFailed(status) || dtStatusDetail(status,DT_BUFFER_TOO_SMALL))
            { result.failure=VansNavigationPathFailure::BoundaryCapacityExceeded; return result; }
            for(int index=0;index<count;++index)
            {
                const float* a=segments.data()+index*6; const float* b=a+3; float parameter=0;
                if(dtDistancePtSegSqr2D(p,a,b,parameter)>captureRadius*captureRadius) continue;
                if(candidate.count>=candidate.segments.size())
                { result.failure=VansNavigationPathFailure::BoundaryCapacityExceeded; return result; }
                candidate.segments[candidate.count++]={glm::vec3(a[0],a[1],a[2]),glm::vec3(b[0],b[1],b[2])};
            }
        }
        cache=std::move(candidate);
    }
    std::array<float,kMaximumNavigationBoundarySegments> distances{};
    for(std::size_t index=0;index<cache.count;++index)
    {
        const auto& segment=cache.segments[index]; const float* a=&segment[0].x; const float* b=&segment[1].x; float parameter=0;
        const float distance=dtDistancePtSegSqr2D(p,a,b,parameter);
        if(distance>radiusMeters*radiusMeters || dtTriArea2D(p,a,b)<0) continue;
        std::size_t slot=0; while(slot<result.count && distances[slot]<=distance) ++slot;
        if(slot>=static_cast<std::size_t>(maximumSegments)) { result.limited=true; continue; }
        if(result.count<static_cast<std::size_t>(maximumSegments)) ++result.count; else result.limited=true;
        for(std::size_t i=result.count-1;i>slot;--i) { result.segments[i]=result.segments[i-1]; distances[i]=distances[i-1]; }
        distances[slot]=distance; result.segments[slot]=segment;
    }
    return result;
}
VansNavigationPathFailure VansNavigationQueryContext::TryRepairTarget(const glm::vec3& goal,const glm::vec3& extents,
 VansNavigationGoalAcceptance acceptance,float toleranceMeters,int maximumIterations,int& iterationsUsed,
 bool& pending,VansNavigationPath& path)
{
 iterationsUsed=0; pending=false;
 ReadLease lease(*this);
 const auto ready=CheckReadyUnderLease(); if(ready!=VansNavigationPathFailure::None) return ready;
 const auto validity=ValidateCorridorUnderLease(m_State->corridorTiles);
 if(validity!=VansNavigationPathFailure::None) return validity;
 if(m_State->sliceRevision!=m_State->filter.traversal->revision)
 { m_State->followPending=false; m_State->targetPending=false; }
 if(!m_State->corridorReady || m_State->searching || m_State->spatialKind!=VansNavigationQueryState::SpatialKind::None ||
  path.status!=VansNavigationPathStatus::Complete || !path.hasRequestCoordinates) return VansNavigationPathFailure::QueryNotReady;
 std::string error;
 if(maximumIterations<1 || !ValidateNavigationGoalProjection(acceptance,toleranceMeters,error)) return VansNavigationPathFailure::InvalidInput;
 auto& candidate=m_State->repairCandidate; const auto& corridor=m_State->corridor;
 if(!m_State->targetPending || goal!=m_State->repairGoal || extents!=m_State->repairExtents ||
  acceptance!=m_State->repairAcceptance || toleranceMeters!=m_State->repairTolerance)
 {
  m_State->followPending=false; m_State->targetPending=false;
  const auto projected=ProjectPointUnderLease(goal,extents);
  if(!projected.valid) return projected.failure==VansNavigationPathFailure::StartOutsideMesh?VansNavigationPathFailure::EndOutsideMesh:projected.failure;
  if(acceptance==VansNavigationGoalAcceptance::RequestedPoint && projected.distance>toleranceMeters) return VansNavigationPathFailure::EndProjectionRejected;
  const auto* previousTarget=corridor.getTarget();
  if(glm::length(glm::dvec3(projected.position)-glm::dvec3(previousTarget[0],previousTarget[1],previousTarget[2]))>
   kMaximumNavigationSpatialDistanceMeters) return VansNavigationPathFailure::InvalidInput;
  candidate.reset(corridor.getFirstPoly(),corridor.getPos());
  candidate.setCorridor(corridor.getTarget(),corridor.getPath(),corridor.getPathCount());
  const float target[]{projected.position.x,projected.position.y,projected.position.z};
  const auto status=m_State->query->beginMoveAlongSurface(corridor.getLastPoly(),corridor.getTarget(),target,&m_State->filter,m_State->followState);
  if(dtStatusFailed(status)) return VansNavigationPathFailure::CorridorSearchFailed;
  m_State->repairGoal=goal; m_State->repairExtents=extents; m_State->repairAcceptance=acceptance; m_State->repairTolerance=toleranceMeters;
  m_State->repairProjection=projected; m_State->repairRef=m_State->projectedRef; m_State->targetPending=true;
  m_State->sliceRevision=m_State->filter.traversal->revision;
 }
 auto localStatus=m_State->query->advanceMoveAlongSurface(m_State->followState,maximumIterations,iterationsUsed);
 if(m_State->filter.encounteredUnavailable)
 { m_State->targetPending=false; return VansNavigationPathFailure::DataUnavailable; }
 if(dtStatusInProgress(localStatus)) { pending=true; return VansNavigationPathFailure::None; }
 const auto projected=m_State->repairProjection;
 const float target[]{projected.position.x,projected.position.y,projected.position.z};
 std::array<dtPolyRef,16> visited{}; int countVisited=0; float result[3]{};
 if(dtStatusSucceed(localStatus)) localStatus=m_State->query->finalizeMoveAlongSurface(m_State->followState,result,visited.data(),countVisited,static_cast<int>(visited.size()));
 m_State->targetPending=false;
 if(dtStatusDetail(localStatus,DT_OUT_OF_NODES)) return VansNavigationPathFailure::SearchNodeCapacityExceeded;
 if(dtStatusDetail(localStatus,DT_BUFFER_TOO_SMALL)) return VansNavigationPathFailure::CorridorCapacityExceeded;
 if(dtStatusFailed(localStatus) || !candidate.mergeTargetPosition(result,visited.data(),countVisited,&localStatus))
  return dtStatusDetail(localStatus,DT_BUFFER_TOO_SMALL)?VansNavigationPathFailure::CorridorCapacityExceeded:VansNavigationPathFailure::CorridorSearchFailed;
 const auto* reached=candidate.getTarget();
 if(candidate.getLastPoly()!=m_State->repairRef ||
  glm::length(glm::vec2(reached[0]-target[0],reached[2]-target[2]))>.01f ||
  !candidate.isValid(candidate.getPathCount(),m_State->query.get(),&m_State->filter))
  return VansNavigationPathFailure::CorridorSearchFailed;
 const int count=candidate.getPathCount();
 std::copy(candidate.getPath(),candidate.getPath()+count,m_State->polygons.begin());
 VansNavigationPath metadata;
 metadata.goalAcceptance=acceptance; metadata.hasRequestCoordinates=true;
 metadata.requestedStart=path.requestedStart; metadata.projectedStart=path.projectedStart;
 metadata.startProjectionDistance=path.startProjectionDistance;
 metadata.requestedEnd=goal; metadata.projectedEnd=projected.position; metadata.endProjectionDistance=projected.distance;
 const auto* current=corridor.getPos();
 auto replacement=CapturePolygonPath(count,{current[0],current[1],current[2]},projected.position,metadata,true);
 if(replacement.status!=VansNavigationPathStatus::Complete) return replacement.failure;
 // 只在整个候选通过目标/过滤/容量/角点验证后替换唯一权威 Corridor。
 m_State->corridorTiles=replacement.tiles;
 m_State->corridor.setCorridor(target,candidate.getPath(),count);
 path=std::move(replacement);
 return VansNavigationPathFailure::None;
}

int VansNavigationQueryContext::CorridorOptimizationIterationBudget() const
{
	return m_State ? m_State->optimizationIterations * (m_State->uniformTraversalCost ? 2 : 1) : 0;
}

VansNavigationPathFailure VansNavigationQueryContext::TryOptimizeCorridor(float visibilityRangeMeters,
	VansNavigationPath& path, bool& changed)
{
	changed = false;
	ReadLease lease(*this);
	const auto ready = CheckReadyUnderLease();
	if (ready != VansNavigationPathFailure::None) return ready;
    const auto validity=ValidateCorridorUnderLease(m_State->corridorTiles);
    if(validity!=VansNavigationPathFailure::None) return validity;
	if (!m_State->corridorReady || m_State->searching || m_State->followPending || m_State->targetPending ||
		m_State->spatialKind != VansNavigationQueryState::SpatialKind::None ||
		(path.status != VansNavigationPathStatus::Complete && path.status != VansNavigationPathStatus::Partial))
		return VansNavigationPathFailure::QueryNotReady;
	std::string error;
	if (!ValidateNavigationSpatialDistance(visibilityRangeMeters, error)) return VansNavigationPathFailure::InvalidInput;
	auto& candidate = m_State->repairCandidate;
	const auto& corridor = m_State->corridor;
	candidate.reset(corridor.getFirstPoly(), corridor.getPos());
	candidate.setCorridor(corridor.getTarget(), corridor.getPath(), corridor.getPathCount());
	const auto check = [](dtStatus status)
	{
		if (dtStatusDetail(status, DT_OUT_OF_NODES)) return VansNavigationPathFailure::SearchNodeCapacityExceeded;
		if (dtStatusDetail(status, DT_BUFFER_TOO_SMALL)) return VansNavigationPathFailure::CorridorCapacityExceeded;
		return dtStatusFailed(status) ? VansNavigationPathFailure::CorridorSearchFailed : VansNavigationPathFailure::None;
	};
	// Visibility is geometric; cost-sensitive filters use the weighted topology search alone.
	if (m_State->uniformTraversalCost)
	{
		std::array<float, 12> corners{};
		std::array<unsigned char, 4> flags{};
		std::array<dtPolyRef, 4> refs{};
		const int count = candidate.findCorners(corners.data(), flags.data(), refs.data(), 4,
			m_State->query.get(), &m_State->filter);
		if (count > 0)
		{
			const int next = (std::min)(1, count - 1);
			if (!(flags[next] & DT_STRAIGHTPATH_OFFMESH_CONNECTION))
			{
				const auto failure = check(candidate.optimizePathVisibility(corners.data() + next * 3,
					visibilityRangeMeters, m_State->query.get(), &m_State->filter, m_State->optimizationIterations, nullptr));
				if (failure != VansNavigationPathFailure::None) return failure;
			}
		}
	}
	const auto failure = check(candidate.optimizePathTopology(m_State->query.get(), &m_State->filter,
		m_State->optimizationIterations, nullptr));
	if (failure != VansNavigationPathFailure::None) return failure;
    if(m_State->filter.encounteredUnavailable) return VansNavigationPathFailure::DataUnavailable;
	const int count = candidate.getPathCount();
	if (count == corridor.getPathCount() && std::equal(candidate.getPath(), candidate.getPath() + count, corridor.getPath()))
		return VansNavigationPathFailure::None;
	if (candidate.getLastPoly() != corridor.getLastPoly() ||
		!candidate.isValid(count, m_State->query.get(), &m_State->filter)) return VansNavigationPathFailure::CorridorSearchFailed;
	std::copy(candidate.getPath(), candidate.getPath() + count, m_State->polygons.begin());
	const auto* start = corridor.getPos(); const auto* target = corridor.getTarget();
	auto replacement = CapturePolygonPath(count, {start[0], start[1], start[2]},
		{target[0], target[1], target[2]}, path, path.status == VansNavigationPathStatus::Complete);
	if (replacement.status == VansNavigationPathStatus::Failed) return replacement.failure;
    m_State->corridorTiles=replacement.tiles;
	m_State->corridor.setCorridor(target, candidate.getPath(), count);
	path = std::move(replacement); changed = true;
	return VansNavigationPathFailure::None;
}

VansNavigationSteering VansNavigationQueryContext::FollowCorridor(
	const glm::vec3& actualPosition, float maximumHorizontalError, float maximumVerticalError, int maximumIterations, int& iterationsUsed)
{
    // 仅当前 native 调用量子持有读 lease；sliced 暂停期间不阻挡 Map 写事务。
    ReadLease lease(*this);

	iterationsUsed=0;
    VansNavigationSteering result;
	result.failure = CheckReadyUnderLease();
	if (result.failure != VansNavigationPathFailure::None) return result;
    result.failure=ValidateCorridorUnderLease(m_State->corridorTiles);
    if(result.failure!=VansNavigationPathFailure::None) return result;
    if(m_State->sliceRevision!=m_State->filter.traversal->revision)
    { m_State->followPending=false; m_State->targetPending=false; }
	if (!IsFinite(actualPosition) || !std::isfinite(maximumHorizontalError) ||
		!std::isfinite(maximumVerticalError) || maximumHorizontalError <= 0.0f || maximumVerticalError <= 0.0f || maximumIterations<1)
	{
		result.failure = VansNavigationPathFailure::InvalidInput;
		return result;
	}
	if (m_State->searching || m_State->targetPending || !m_State->corridorReady ||
		m_State->spatialKind != VansNavigationQueryState::SpatialKind::None)
	{
		result.failure = VansNavigationPathFailure::QueryNotReady;
		return result;
	}
	const auto& corridor = m_State->corridor;
	auto& candidate = m_State->repairCandidate;
	const float actual[3]{ actualPosition.x, actualPosition.y, actualPosition.z };
	if (!m_State->corridor.isValid(corridor.getPathCount(), m_State->query.get(), &m_State->filter))
	{
		result.failure = VansNavigationPathFailure::CorridorSearchFailed;
		return result;
	}
	if(!m_State->followPending || m_State->followActual!=actualPosition)
    {
        m_State->followPending=false;
        candidate.reset(corridor.getFirstPoly(),corridor.getPos());
        candidate.setCorridor(corridor.getTarget(),corridor.getPath(),corridor.getPathCount());
        const auto status=m_State->query->beginMoveAlongSurface(corridor.getFirstPoly(),corridor.getPos(),actual,
            &m_State->filter,m_State->followState);
        if(dtStatusFailed(status)) { result.failure=VansNavigationPathFailure::CorridorSearchFailed; return result; }
        m_State->followActual=actualPosition; m_State->followPending=true;
        m_State->sliceRevision=m_State->filter.traversal->revision;
    }
    auto localStatus=m_State->query->advanceMoveAlongSurface(m_State->followState,maximumIterations,iterationsUsed);
    if(m_State->filter.encounteredUnavailable)
    { m_State->followPending=false; result.failure=VansNavigationPathFailure::DataUnavailable; return result; }
    if(dtStatusInProgress(localStatus)) { result.pending=true; return result; }
    std::array<dtPolyRef,16> visited{}; int visitedCount=0; float projected[3]{};
    if(dtStatusSucceed(localStatus)) localStatus=m_State->query->finalizeMoveAlongSurface(m_State->followState,
        projected,visited.data(),visitedCount,static_cast<int>(visited.size()));
    m_State->followPending=false;
    if(dtStatusFailed(localStatus) || dtStatusDetail(localStatus,DT_OUT_OF_NODES) ||
        dtStatusDetail(localStatus,DT_BUFFER_TOO_SMALL) || !candidate.mergePosition(projected,visited.data(),visitedCount,m_State->query.get(),&localStatus))
    {
        result.failure=dtStatusDetail(localStatus,DT_OUT_OF_NODES)?VansNavigationPathFailure::SearchNodeCapacityExceeded:
            dtStatusDetail(localStatus,DT_BUFFER_TOO_SMALL)?VansNavigationPathFailure::CorridorCapacityExceeded:
            VansNavigationPathFailure::CorridorSearchFailed;
        return result;
    }
    const float* position = candidate.getPos();
	if (glm::length(glm::vec2(position[0] - actual[0], position[2] - actual[2])) > maximumHorizontalError ||
		std::abs(position[1] - actual[1]) > maximumVerticalError)
	{
		result.failure = VansNavigationPathFailure::StartOutsideMesh;
		return result;
	}
	if (candidate.getLastPoly() != corridor.getLastPoly() ||
		!candidate.isValid(candidate.getPathCount(), m_State->query.get(), &m_State->filter))
	{
		result.failure = VansNavigationPathFailure::CorridorSearchFailed;
		return result;
	}
	// 真实脚点、容量、过滤和层高度均接受后，才原子采用候选。
    auto& dependencies=m_State->candidateTiles;
    if(!CaptureTileDependencies(*m_Map->m_NavMesh,candidate.getPath(),candidate.getPathCount(),dependencies,
        [&](auto ref){return m_Map->m_TileData[m_Map->m_NavMesh->decodePolyIdTile(ref)].contentEpoch;}))
    { result.failure=VansNavigationPathFailure::MeshChanged; return result; }
    result.failure=ValidateCorridorUnderLease(dependencies);
    if(result.failure!=VansNavigationPathFailure::None) return result;
	m_State->corridor.reset(candidate.getFirstPoly(), candidate.getPos());
	m_State->corridor.setCorridor(candidate.getTarget(), candidate.getPath(), candidate.getPathCount());
    m_State->corridorTiles.swap(dependencies);
	return CaptureCorridorSteeringUnderLease(actualPosition);
}
void VansNavigationQueryContext::CancelCorridorLocalQuery()
{
    if(m_State) { m_State->followPending=false; m_State->targetPending=false; }
}
VansNavigationSteering VansNavigationQueryContext::CaptureCorridorSteering(const glm::vec3& actualPosition)
{
    ReadLease lease(*this);
    VansNavigationSteering result; result.failure=CheckReadyUnderLease();
    if(result.failure!=VansNavigationPathFailure::None) return result;
    result.failure=ValidateCorridorUnderLease(m_State->corridorTiles);
    if(result.failure!=VansNavigationPathFailure::None) return result;
    if(!IsFinite(actualPosition)) { result.failure=VansNavigationPathFailure::InvalidInput; return result; }
    if(!m_State->corridorReady || m_State->followPending || m_State->targetPending)
    { result.failure=VansNavigationPathFailure::QueryNotReady; return result; }
    return CaptureCorridorSteeringUnderLease(actualPosition);
}
VansNavigationSteering VansNavigationQueryContext::CaptureCorridorSteeringUnderLease(const glm::vec3& actualPosition)
{
    VansNavigationSteering result;
    const auto& corridor=m_State->corridor;
    const float actual[]{actualPosition.x,actualPosition.y,actualPosition.z};
    const float* endpoint = corridor.getTarget();
	result.endpoint = { endpoint[0], endpoint[1], endpoint[2] };
	std::array<float, 12> corners{};
	std::array<unsigned char, 4> flags{};
	std::array<dtPolyRef, 4> refs{};
	const int count = m_State->corridor.findCorners(corners.data(), flags.data(), refs.data(),
		4, m_State->query.get(), &m_State->filter);
	result.hasEndpointDistance = corridor.getFirstPoly() == corridor.getLastPoly() ||
		(count > 0 && (flags[count - 1] & DT_STRAIGHTPATH_END) != 0);
	glm::vec3 previous(actualPosition);
	for (int i = 0; i < count; ++i)
	{
		const glm::vec3 corner(corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]);
		result.remainingDistance += glm::length(glm::vec2(corner.x - previous.x, corner.z - previous.z));
		previous = corner;
	}
	if (count > 0)
	{
		if(flags[0]&DT_STRAIGHTPATH_OFFMESH_CONNECTION)
		{
            const auto* connection=m_Map->m_NavMesh->getOffMeshConnectionByRef(refs[0]);
            const auto* route=corridor.getPath(); int position=1;
            while(position<corridor.getPathCount() && route[position]!=refs[0]) ++position;
            float entry[3]{},exit[3]{};
            if(!connection || position>=corridor.getPathCount() || !connection->userId || connection->rad<=0 ||
                dtStatusFailed(m_Map->m_NavMesh->getOffMeshConnectionPolyEndPoints(route[position-1],refs[0],entry,exit)))
            { result.failure=VansNavigationPathFailure::CorridorSearchFailed; return result; }
            result.link=VansNavigationLinkEntrance{{connection->userId},{entry[0],entry[1],entry[2]},
                {exit[0],exit[1],exit[2]},connection->rad,(connection->flags&DT_OFFMESH_CON_BIDIR)!=0};
		}
		glm::vec3 direction(corners[0] - actual[0], 0.0f, corners[2] - actual[2]);
		const float length = glm::length(direction);
		if (length > 1.e-4f) result.direction = direction / length;
	}
	result.valid = true;
	return result;
}

}
