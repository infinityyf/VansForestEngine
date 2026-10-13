#include "VansNavigationAvoidance.h"
#include "VansNavigationSourceSnapshot.h"
#include <DetourObstacleAvoidance.h>
#include <DetourCommon.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Vans
{
namespace
{
bool Finite(const glm::vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
struct VansNavigationAvoidanceBody
{
    std::uint64_t identity=0;
    std::uint32_t map=0,profileMask=0;
    glm::vec3 position{0},velocity{0},halfExtents{0};
    float radius=0,yaw=0,minimumY=0,maximumY=0;
    int x=0,z=0;
    VansNavigationObstacleGeometryKind kind=VansNavigationObstacleGeometryKind::Cylinder;
    bool obstacle=false,reciprocal=true;
    VansNavigationAvoidanceYield yield;
};
struct VansNavigationAvoidanceGridEntry { int x,z,body,next; };
std::size_t BucketCount(int maximumAgents,const VansNavigationAvoidanceSettings& settings)
{
    const auto bodies=static_cast<std::size_t>(maximumAgents)+kMaximumNavigationObstacles;
    const auto entries=(std::min)(static_cast<std::size_t>(settings.maximumGridEntries),bodies*settings.maximumCellsPerBody);
    std::size_t count=1; while(count<entries*2) count*=2; return count;
}
glm::vec2 BoxLocal(const VansNavigationAvoidanceBody& box,const glm::vec3& point)
{
    const float c=std::cos(box.yaw),s=std::sin(box.yaw); const auto d=point-box.position;
    return {c*d.x-s*d.z,s*d.x+c*d.z};
}
float DistanceSquared(const VansNavigationAvoidanceBody& body,const glm::vec3& point)
{
    if(body.obstacle && body.kind==VansNavigationObstacleGeometryKind::Box)
    {
        const auto local=BoxLocal(body,point);
        const glm::vec2 d((std::max)(0.f,std::abs(local.x)-body.halfExtents.x),(std::max)(0.f,std::abs(local.y)-body.halfExtents.z));
        return glm::dot(d,d);
    }
    const glm::vec2 offset(body.position.x-point.x,body.position.z-point.z);
    // Agent 延续按中心距离选邻居的既有语义；普通物体按实际外表面距离。
    if(!body.obstacle) return glm::dot(offset,offset);
    const float distance=(std::max)(0.f,glm::length(offset)-body.radius); return distance*distance;
}
glm::vec2 PassageClearance(const VansNavigationLocalBoundary& boundary,const glm::vec3& point,const glm::vec3& direction)
{
    const glm::vec3 normal(-direction.z,0,direction.x);
    glm::vec2 clearance((std::numeric_limits<float>::max)());
    for(std::size_t i=0;i<boundary.count;++i)
    {
        const auto a=boundary.segments[i][0]-point,b=boundary.segments[i][1]-point;
        const float alongA=glm::dot(a,direction),alongB=glm::dot(b,direction),denominator=alongB-alongA;
        if(std::abs(denominator)<1.e-5f) continue;
        const float t=-alongA/denominator; if(t<0 || t>1) continue;
        const float lateral=glm::dot(a+(b-a)*t,normal);
        if(lateral>=0) clearance.x=(std::min)(clearance.x,lateral);
        else clearance.y=(std::min)(clearance.y,-lateral);
    }
    return clearance;
}
}
struct VansNavigationAvoidance::State
{
    VansNavigationAvoidanceSettings settings;
    std::vector<VansNavigationAvoidanceBody> bodies;
    std::vector<VansNavigationAvoidanceGridEntry> entries;
    std::vector<int> buckets;
    std::vector<std::uint32_t> visited;
    int maximumAgents=0,agents=0,obstacles=0,count=0,entryCount=0,remainingSamples=0;
    std::uint32_t visit=0;
    bool incomplete=false;
    std::unique_ptr<dtObstacleAvoidanceQuery,decltype(&dtFreeObstacleAvoidanceQuery)> query{nullptr,&dtFreeObstacleAvoidanceQuery};
    std::size_t Hash(int x,int z,std::uint32_t map) const
    {
        return (static_cast<std::uint32_t>(x)*73856093u ^ static_cast<std::uint32_t>(z)*19349663u ^ map*83492791u)&(buckets.size()-1);
    }
    int Insert(VansNavigationAvoidanceBody body,const glm::vec3& minimum,const glm::vec3& maximum)
    {
        std::array<int,4> cells{}; std::string error;
        if(!NavigationAvoidanceGridBounds(minimum,maximum,settings,cells,error)) return -1;
        const auto needed=static_cast<std::uint64_t>(cells[1]-cells[0]+1)*static_cast<std::uint64_t>(cells[3]-cells[2]+1);
        if(needed>entries.size()-static_cast<std::size_t>(entryCount)) { incomplete=true; return -1; }
        body.x=static_cast<int>(std::floor(static_cast<double>(body.position.x)/settings.queryRangeMeters));
        body.z=static_cast<int>(std::floor(static_cast<double>(body.position.z)/settings.queryRangeMeters));
        bodies[static_cast<std::size_t>(count)]=body;
        for(int z=cells[2];z<=cells[3];++z) for(int x=cells[0];x<=cells[1];++x)
        {
            const auto hash=Hash(x,z,body.map);
            entries[static_cast<std::size_t>(entryCount)]={x,z,count,buckets[hash]}; buckets[hash]=entryCount++;
        }
        return count++;
    }
};
std::uint64_t EstimateNavigationAvoidanceBytes(int maximumAgents,const VansNavigationAvoidanceSettings& settings)
{
    const auto bodies=static_cast<std::uint64_t>(maximumAgents)+kMaximumNavigationObstacles;
    // 包含旧状态和候选初始化的共存峰值，结构尺寸与实际分配来自同一实现。
    return 2u*(bodies*(sizeof(VansNavigationAvoidanceBody)+sizeof(std::uint32_t))+
        static_cast<std::uint64_t>(settings.maximumGridEntries)*sizeof(VansNavigationAvoidanceGridEntry)+BucketCount(maximumAgents,settings)*sizeof(int)+
        settings.maximumNeighbors*sizeof(dtObstacleCircle)+(settings.maximumBoundarySegments+4u*settings.maximumNeighbors)*sizeof(dtObstacleSegment)+
        sizeof(dtObstacleAvoidanceQuery)+sizeof(VansNavigationAvoidance)+1024u);
}
VansNavigationAvoidance::VansNavigationAvoidance()=default;
VansNavigationAvoidance::~VansNavigationAvoidance()=default;
bool VansNavigationAvoidance::Initialize(int maximumAgents,const VansNavigationAvoidanceSettings& settings,std::string& error)
try
{
    if(maximumAgents<1 || maximumAgents>4096) { error="Navigation avoidance requires 1 to 4096 frame Agents"; return false; }
    if(!ValidateNavigationAvoidanceSettings(settings,error)) return false;
    auto candidate=std::make_unique<State>(); candidate->settings=settings; candidate->maximumAgents=maximumAgents;
    candidate->bodies.resize(static_cast<std::size_t>(maximumAgents)+kMaximumNavigationObstacles);
    candidate->visited.resize(candidate->bodies.size(),0);
    candidate->entries.resize(static_cast<std::size_t>(settings.maximumGridEntries));
    candidate->buckets.resize(BucketCount(maximumAgents,settings),-1);
    candidate->query.reset(dtAllocObstacleAvoidanceQuery());
    if(!candidate->query || !candidate->query->init(settings.maximumNeighbors,settings.maximumBoundarySegments+4*settings.maximumNeighbors))
    { error="Navigation avoidance native scratch allocation failed"; return false; }
    m_State=std::move(candidate); BeginFrame(); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation avoidance frame scratch allocation failed"; return false; }
void VansNavigationAvoidance::BeginFrame()
{
    if(!m_State) return;
    auto& state=*m_State; state.count=state.entryCount=state.agents=state.obstacles=0; state.incomplete=false;
    state.remainingSamples=state.settings.maximumSamplesPerFrame;
    std::fill(state.buckets.begin(),state.buckets.end(),-1);
}
int VansNavigationAvoidance::AddAgent(const VansNavigationAvoidanceAgent& agent)
{
    if(!m_State || m_State->agents>=m_State->maximumAgents || !Finite(agent.position) || !Finite(agent.velocity) ||
        !agent.identity || agent.profile>=16 || !std::isfinite(agent.radiusMeters) || agent.radiusMeters<=0 ||
        !std::isfinite(agent.heightMeters) || agent.heightMeters<=0 || !std::isfinite(agent.position.y+agent.heightMeters)) return -1;
    VansNavigationAvoidanceBody body; body.identity=agent.identity; body.map=agent.map; body.profileMask=1u<<agent.profile;
    body.position=agent.position; body.velocity=agent.velocity; body.radius=agent.radiusMeters;
    body.minimumY=agent.position.y; body.maximumY=agent.position.y+agent.heightMeters; body.reciprocal=agent.reciprocal;
    if(!Finite(agent.yield.direction) || !std::isfinite(agent.yield.seconds) || agent.yield.seconds<0 ||
        (agent.yield.peer && (agent.yield.peer>=agent.identity || std::abs(glm::length(agent.yield.direction)-1)>1.e-3f || std::abs(agent.yield.direction.y)>1.e-4f))) return -1;
    body.yield=agent.yield;
    const int result=m_State->Insert(body,agent.position,agent.position); if(result>=0) ++m_State->agents; return result;
}
bool VansNavigationAvoidance::AddObstacle(std::uint64_t identity,std::uint32_t map,std::uint32_t profileMask,
    const VansNavigationObstacleGeometry& geometry,const glm::vec3& velocity)
{
    if(!m_State) return false;
    std::string error;
    if(!identity || !profileMask || (profileMask&~0xffffu) || !Finite(velocity) ||
        m_State->obstacles>=static_cast<int>(kMaximumNavigationObstacles) || !ValidateNavigationObstacleGeometry(geometry,error))
    { m_State->incomplete=true; return false; }
    glm::vec3 minimum,maximum; NavigationObstacleGeometryBounds(geometry,minimum,maximum);
    VansNavigationAvoidanceBody body; body.identity=identity; body.map=map; body.profileMask=profileMask; body.obstacle=true; body.reciprocal=false;
    body.position=geometry.center; body.velocity=velocity; body.kind=geometry.kind; body.halfExtents=geometry.halfExtents;
    body.radius=geometry.radius; body.yaw=geometry.yawRadians; body.minimumY=minimum.y; body.maximumY=maximum.y;
    if(m_State->Insert(body,minimum,maximum)<0) { m_State->incomplete=true; return false; }
    ++m_State->obstacles; return true;
}
VansNavigationAvoidanceResult VansNavigationAvoidance::SelectVelocity(int agent,const glm::vec3& preferred,float maximumSpeed,const VansNavigationLocalBoundary& boundary)
{
    VansNavigationAvoidanceResult result;
    if(!m_State || m_State->incomplete || agent<0 || agent>=m_State->count || m_State->bodies[static_cast<std::size_t>(agent)].obstacle ||
        !Finite(preferred) || !std::isfinite(maximumSpeed) || maximumSpeed<0 || boundary.pending ||
        boundary.failure!=VansNavigationPathFailure::None || boundary.count>static_cast<std::size_t>(m_State->settings.maximumBoundarySegments))
    { result.limited=true; return result; }
    if(maximumSpeed==0) return result;
    auto& state=*m_State; const auto& settings=state.settings; const auto& self=state.bodies[static_cast<std::size_t>(agent)];
    result.yield=self.yield;
    for(std::size_t i=0;i<boundary.count;++i)
        if(!Finite(boundary.segments[i][0]) || !Finite(boundary.segments[i][1])) { result.limited=true; return result; }
    const int sampleBound=(settings.adaptiveDivisions*settings.adaptiveRings+1)*settings.adaptiveDepth;
    if(state.remainingSamples<sampleBound) { result.limited=true; return result; }
    if(++state.visit==0) { std::fill(state.visited.begin(),state.visited.end(),0); ++state.visit; }
    struct Neighbor { float distance; int index; };
    std::array<Neighbor,32> neighbors{}; std::size_t count=0;
    const VansNavigationAvoidanceBody* yieldingPeer=nullptr;
    const float rangeSquared=settings.queryRangeMeters*settings.queryRangeMeters;
    for(int z=self.z-1;z<=self.z+1;++z) for(int x=self.x-1;x<=self.x+1;++x)
    {
        for(int entry=state.buckets[state.Hash(x,z,self.map)];entry>=0;entry=state.entries[static_cast<std::size_t>(entry)].next)
        {
            if(result.scans>=static_cast<std::uint32_t>(settings.maximumNeighborScans)) { result.limited=true; return result; }
            ++result.scans; const auto& node=state.entries[static_cast<std::size_t>(entry)];
            if(node.x!=x || node.z!=z) continue;
            const auto index=static_cast<std::size_t>(node.body); const auto& item=state.bodies[index];
            if(item.map!=self.map || state.visited[index]==state.visit) continue; state.visited[index]=state.visit;
            if(node.body==agent || item.minimumY>=self.maximumY || self.minimumY>=item.maximumY ||
                (item.obstacle && !(item.profileMask&self.profileMask))) continue;
            const float distance=DistanceSquared(item,self.position); if(distance>rangeSquared) continue;
            if(!item.obstacle && item.identity==self.yield.peer) yieldingPeer=&item;
            Neighbor next{distance,node.body}; std::size_t slot=0;
            const auto before=[&](const Neighbor& other)
            {
                const auto& existing=state.bodies[static_cast<std::size_t>(other.index)];
                return other.distance<distance || (other.distance==distance &&
                    (existing.obstacle<item.obstacle || (existing.obstacle==item.obstacle && existing.identity<item.identity)));
            };
            while(slot<count && before(neighbors[slot])) ++slot;
            if(slot>=static_cast<std::size_t>(settings.maximumNeighbors)) { result.limited=true; continue; }
            if(count<static_cast<std::size_t>(settings.maximumNeighbors)) ++count; else result.limited=true;
            for(std::size_t i=count-1;i>slot;--i) neighbors[i]=neighbors[i-1]; neighbors[slot]=next;
        }
    }
    glm::vec3 desired(preferred.x,0,preferred.z); const float length=glm::length(desired);
    if(length>maximumSpeed) desired*=maximumSpeed/length;
    // 严格身份顺序阻止互相让行；只使用同一有界近邻查询与本 Corridor 的实际边界。
    if(result.yield.peer && (!yieldingPeer || !yieldingPeer->reciprocal ||
        (length>1.e-4f && glm::dot(desired,result.yield.direction)<=0) ||
        glm::dot(yieldingPeer->position-self.position,result.yield.direction)<-(self.radius+yieldingPeer->radius))) { result.yield={}; yieldingPeer=nullptr; }
    if(!result.yield.peer && glm::length(desired)>1.e-4f)
    {
        const auto direction=desired/glm::length(desired),normal=glm::vec3(-direction.z,0,direction.x);
        for(std::size_t i=0;i<count;++i)
        {
            const auto& peer=state.bodies[static_cast<std::size_t>(neighbors[i].index)];
            const auto delta=peer.position-self.position;
            if(peer.obstacle || !peer.reciprocal || peer.identity>=self.identity ||
                glm::dot(delta,direction)<=0 || std::abs(glm::dot(delta,normal))>self.radius+peer.radius ||
                glm::dot(peer.velocity,direction)>=-.01f) continue;
            const auto clearance=PassageClearance(boundary,self.position+direction*(std::min)(settings.queryRangeMeters*.5f,glm::length(delta)*.5f),direction);
            // 边界已经扣除了自己的半径；并排还需要对方的直径，跨 Profile 使用其真实半径。
            if(clearance.x+clearance.y>2*peer.radius+.1f) continue;
            if(!yieldingPeer || peer.identity<yieldingPeer->identity) yieldingPeer=&peer;
        }
        if(yieldingPeer) { result.yield.peer=yieldingPeer->identity; result.yield.direction=direction; }
    }
    if(result.yield.peer && yieldingPeer)
    {
        const auto direction=result.yield.direction,normal=glm::vec3(-direction.z,0,direction.x);
        const auto clearance=PassageClearance(boundary,self.position,direction);
        const float separation=self.radius+yieldingPeer->radius+.1f;
        if(clearance.x+clearance.y<=2*yieldingPeer->radius+.1f) desired=-direction*(maximumSpeed*.5f);
        else
        {
            const float lateral=glm::dot(self.position-yieldingPeer->position,normal);
            const float side=std::abs(lateral)>.05f?(lateral>0?1.f:-1.f):(clearance.x>=clearance.y?1.f:-1.f);
            desired=std::abs(lateral)>=separation?glm::vec3(0):normal*(side*maximumSpeed*.5f);
        }
    }
    state.query->reset(); result.neighbors=static_cast<std::uint32_t>(count); bool insideBox=false;
    for(std::size_t i=0;i<count;++i)
    {
        const auto& neighbor=state.bodies[static_cast<std::size_t>(neighbors[i].index)];
        const float p[]{neighbor.position.x,neighbor.position.y,neighbor.position.z},v[]{neighbor.velocity.x,0,neighbor.velocity.z};
        if(neighbor.obstacle) ++result.obstacles;
        if(!neighbor.obstacle || neighbor.kind==VansNavigationObstacleGeometryKind::Cylinder)
            state.query->addCircle(p,neighbor.radius,v,v,neighbor.reciprocal && neighbor.yield.peer!=self.identity && result.yield.peer!=neighbor.identity);
        else
        {
            const auto local=BoxLocal(neighbor,self.position);
            insideBox|=std::abs(local.x)<neighbor.halfExtents.x && std::abs(local.y)<neighbor.halfExtents.z;
            const float c=std::cos(neighbor.yaw),s=std::sin(neighbor.yaw);
            const glm::vec2 offsets[4]{{-neighbor.halfExtents.x,-neighbor.halfExtents.z},{neighbor.halfExtents.x,-neighbor.halfExtents.z},
                {neighbor.halfExtents.x,neighbor.halfExtents.z},{-neighbor.halfExtents.x,neighbor.halfExtents.z}};
            std::array<std::array<float,3>,4> corners{};
            for(std::size_t j=0;j<corners.size();++j) corners[j]={p[0]+c*offsets[j].x+s*offsets[j].y,p[1],p[2]-s*offsets[j].x+c*offsets[j].y};
            for(std::size_t j=0;j<corners.size();++j) state.query->addSegment(corners[j].data(),corners[(j+1)%4].data(),v,self.radius);
        }
    }
    const float p[]{self.position.x,self.position.y,self.position.z},v[]{self.velocity.x,0,self.velocity.z},stationary[]{0.f,0.f,0.f};
    for(std::size_t i=0;i<boundary.count;++i)
    {
        const auto& segment=boundary.segments[i]; const float a[]{segment[0].x,segment[0].y,segment[0].z},b[]{segment[1].x,segment[1].y,segment[1].z};
        if(dtTriArea2D(p,a,b)>=0) state.query->addSegment(a,b,stationary,0);
    }
    const float d[]{desired.x,0,desired.z}; float selected[3]{};
    dtObstacleAvoidanceParams params{}; params.velBias=.4f; params.weightDesVel=2; params.weightCurVel=.75f;
    params.weightSide=.75f; params.weightToi=2.5f; params.horizTime=settings.horizonSeconds;
    params.adaptiveDivs=static_cast<unsigned char>(settings.adaptiveDivisions); params.adaptiveRings=static_cast<unsigned char>(settings.adaptiveRings);
    params.adaptiveDepth=static_cast<unsigned char>(settings.adaptiveDepth);
    result.samples=static_cast<std::uint32_t>(state.query->sampleVelocityAdaptive(p,self.radius,maximumSpeed,v,d,selected,&params));
    state.remainingSamples-=sampleBound;
    result.velocity={selected[0],0,selected[2]}; result.limited|=boundary.limited;
    if(insideBox || !Finite(result.velocity) || glm::length(result.velocity)>maximumSpeed+1.e-4f) { result.velocity=glm::vec3(0); result.limited=true; }
    return result;
}
}
