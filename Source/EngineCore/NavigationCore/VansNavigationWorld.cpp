#include "VansNavigationWorld.h"
#include "VansNavigationMap.h"
#include "../Util/VansProfiler.h"
#include "../EventCore/VansEventBus.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <set>

namespace Vans
{
namespace
{
std::atomic<std::uint64_t> NavigationSession{ 0 };
bool Finite(const glm::vec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float HorizontalDistance(const glm::vec3& left, const glm::vec3& right)
{
	return glm::length(glm::vec2(left.x - right.x, left.z - right.z));
}
bool Active(VansNavigationMoveState state)
{
	return state != VansNavigationMoveState::Idle && state != VansNavigationMoveState::Finished;
}
bool SameObstacleGeometry(const VansNavigationObstacleRecord& left,const VansNavigationObstacleRecord& right)
{
    const auto& a=left.shape; const auto& b=right.shape;
    return left.mapId==right.mapId && left.enabled==right.enabled &&
        left.profiles.size()==right.profiles.size() &&
        std::is_permutation(left.profiles.begin(),left.profiles.end(),right.profiles.begin()) &&
        a.id==b.id && a.kind==b.kind && glm::all(glm::equal(a.center,b.center)) &&
        glm::all(glm::equal(a.halfExtents,b.halfExtents)) && a.radius==b.radius && a.height==b.height && a.yawRadians==b.yawRadians;
}
bool SameObstacle(const VansNavigationObstacleRecord& left,const VansNavigationObstacleRecord& right)
{ return left.mode==right.mode && SameObstacleGeometry(left,right); }
bool Carves(const VansNavigationObstacleRecord& record)
{ return record.enabled && NavigationObstacleCarves(record.mode); }
}

VansNavigationWorld::VansNavigationWorld() = default;
VansNavigationWorld::~VansNavigationWorld() { Shutdown(); }
std::uint64_t VansNavigationWorld::EntityKey(VansEntityHandle entity)
{
	return (static_cast<std::uint64_t>(entity.generation) << 32u) | entity.index;
}
bool VansNavigationWorld::CanMutate(std::string& error) const
{
    if(!m_PublishingLifecycle) return true;
    error="Navigation lifecycle observers must defer commands until their own update";
    return false;
}
void VansNavigationWorld::PublishLifecycle(const AgentRuntime& agent,VansNavigationLifecycleKind kind)
{
    if(!agent.consumer) return;
    VansNavigationLifecycleEvent event;
    event.kind=kind; event.subscription=agent.consumer->handle; event.request=agent.snapshot.request;
    event.requestOwner=agent.snapshot.owner;
    event.traversal=agent.snapshot.traversal; event.result=agent.snapshot.result;
    struct PublishingScope
    {
        bool& publishing;
        explicit PublishingScope(bool& value):publishing(value) { publishing=true; }
        ~PublishingScope() { publishing=false; }
    } scope(m_PublishingLifecycle);
    VansEventBus::Get().PublishNow(event);
}
bool VansNavigationWorld::Initialize(const VansNavigationSettings& settings, std::string& error)
try
{
	if(!CanMutate(error)) return false;
	if (!ValidateNavigationSettings(settings, error)) return false;
	auto candidateSettings = settings;
	auto candidateAvoidance=std::make_unique<VansNavigationAvoidance>();
    if(!candidateAvoidance->Initialize(settings.query.maximumActiveAgents,settings.avoidance,error)) return false;
	Shutdown();
	m_Settings = std::move(candidateSettings);
	m_Avoidance=std::move(candidateAvoidance);
	m_Session = ++NavigationSession;
	m_NextRequest = 0;
    m_NextConsumerIdentity=0;
    m_Queries.Initialize(m_Settings.query,m_Session);
	m_Initialized = true;
	return true;
}
catch (const std::bad_alloc&)
{
	error = "Navigation World settings allocation failed";
	return false;
}
void VansNavigationWorld::Shutdown()
{
    if(m_PublishingLifecycle) return;
	for (auto& [key, agent] : m_Agents) Finish(agent, VansNavigationMoveResult::Cancelled);
	m_Agents.clear();
    m_Queries.Shutdown(); m_Maps.clear();
    m_Avoidance.reset();
    std::vector<OwnedObstacle>().swap(m_Obstacles);
    m_ObstacleBytes=0; m_CarveObstacleRevision=0;
	m_Initialized = false;
	m_Session = 0;
	m_AgentCursor = 0;
    m_WorkerFirst=true;
}
bool VansNavigationWorld::RegisterAgents(const std::vector<VansNavigationAgentRegistration>& agents,
	std::string& error)
try
{
    if(!CanMutate(error)) return false;
	if (!m_Initialized) { error = "Navigation World is not initialized"; return false; }
    if (agents.size() > static_cast<std::size_t>(m_Settings.query.maximumActiveAgents))
    { error = "Navigation Agent registration batch exceeds capacity"; return false; }
    std::set<std::uint64_t> incoming;
    std::size_t newCount = m_Agents.size();
    for (const auto& registration : agents)
    {
        const auto key = EntityKey(registration.entity);
        if (!registration.entity.IsValid() || !incoming.insert(key).second)
        { error = "Entity has invalid identity or duplicate navigation driver"; return false; }
        if (!m_Agents.count(key)) ++newCount;
    }
    if (newCount > static_cast<std::size_t>(m_Settings.query.maximumActiveAgents))
    { error = "Navigation active Agent capacity exceeded"; return false; }
    // 候选与旧请求同时驻留时也计算峰值；容量拒绝先于 native query 分配。
    std::set<const VansNavigationMap*> countedMaps;
    std::set<const VansNavigationMeshAsset*> countedAssets;
	std::uint64_t residentBytes = m_ObstacleBytes+EstimateNavigationAvoidanceBytes(m_Settings.query.maximumActiveAgents,m_Settings.avoidance), residentTiles = 0;
    const auto includeMap = [&](const std::shared_ptr<const VansNavigationMap>& map)
    {
        if (!map || !map->IsReady() || !countedMaps.insert(map.get()).second) return;
        const auto asset = map->GetAsset();
        residentBytes += map->EstimateResidentBytes();
        residentTiles += map->GetResidentTileCount();
        if (countedAssets.insert(asset.get()).second) residentBytes += asset->EstimateResidentBytes();
    };
    for (const auto& [key, agent] : m_Agents) includeMap(agent.registration.map);
    for(const auto& [key,map]:m_Maps) includeMap(map);
    for(const auto& map:m_Queries.CollectRetainedMaps()) includeMap(map);
    for (const auto& registration : agents) includeMap(registration.map);
    auto peakQueries = m_Settings.query;
    peakQueries.maximumActiveAgents = static_cast<int>(m_Agents.size() + agents.size());
    residentBytes += EstimateNavigationQueryBytes(peakQueries);
    if (residentBytes > static_cast<std::uint64_t>(m_Settings.maximumResidentBytes) ||
        residentTiles > static_cast<std::uint64_t>(m_Settings.maximumResidentTiles))
    { error = "Navigation World registration exceeds aggregate resident capacity"; return false; }
	std::map<std::uint64_t, AgentRuntime> candidates;
	for (const auto& registration : agents)
	{
		const auto key = EntityKey(registration.entity);
		if (!registration.entity.IsValid() || candidates.count(key))
		{ error = "Entity has invalid identity or duplicate navigation driver"; return false; }
		if (!registration.motor || !registration.map || !registration.map->IsReady() ||
			!ValidateNavigationAgentTiming(registration.config, error))
		{ if (error.empty()) error = "NavigationAgent requires a ready mesh and motor"; return false; }
		if (!std::isfinite(registration.config.maxSpeed) || registration.config.maxSpeed <= 0.0f ||
			!std::isfinite(registration.config.acceleration) || registration.config.acceleration <= 0.0f)
		{ error = "NavigationAgent speed and acceleration must be positive and finite"; return false; }
		if (registration.map->GetProfileId() != registration.config.profileId ||
            registration.map->GetAsset()->GetMapId() != registration.config.mapId)
        { error = "NavigationAgent Map/Profile is incompatible with its registered Map"; return false; }
        AgentRuntime candidate;
		candidate.registration = registration;
		candidate.query = std::make_unique<VansNavigationQueryContext>();
		if (!candidate.query->Initialize(registration.map, m_Settings.query, registration.config.queryFilter, error)) return false;
		candidate.snapshot.request.agent = registration.entity;
		candidates.emplace(key, std::move(candidate));
	}
	for (const auto& [key, candidate] : candidates)
	{
		const auto old = m_Agents.find(key);
		if (old != m_Agents.end()) { Finish(old->second, VansNavigationMoveResult::Cancelled); m_Agents.erase(old); }
	}
	m_Agents.merge(candidates);
	error.clear();
	return true;
}
catch (const std::bad_alloc&)
{
	error = "Navigation Agent candidate allocation failed";
	return false;
}
void VansNavigationWorld::UnregisterAgent(VansEntityHandle entity)
{
    if(m_PublishingLifecycle) return;
    m_Queries.CancelEntity(entity);
	const auto found = m_Agents.find(EntityKey(entity));
	if (found == m_Agents.end()) return;
	Finish(found->second, VansNavigationMoveResult::Cancelled);
	m_Agents.erase(found);
}
bool VansNavigationWorld::HasAgent(VansEntityHandle entity) const
{
	return m_Agents.count(EntityKey(entity)) != 0;
}
void VansNavigationWorld::CancelOwner(const std::string& owner)
{
    if(m_PublishingLifecycle) return;
	if (owner.empty()) return;
    m_Queries.CancelOwner(owner);
    const auto oldSize=m_Obstacles.size();
    const bool carveChanged=std::any_of(m_Obstacles.begin(),m_Obstacles.end(),
        [&](const auto& item){return item.owner==owner && Carves(item.record);});
    m_Obstacles.erase(std::remove_if(m_Obstacles.begin(),m_Obstacles.end(),
        [&](const auto& item){return item.owner==owner;}),m_Obstacles.end());
    if(m_Obstacles.size()!=oldSize)
    {
        if(m_Obstacles.empty()) std::vector<OwnedObstacle>().swap(m_Obstacles);
        ObserveObstacleChange(carveChanged);
    }
	for (auto& [key, agent] : m_Agents)
		if (agent.snapshot.owner == owner && Active(agent.snapshot.state))
			Finish(agent, VansNavigationMoveResult::Cancelled);
    for(auto& [key,agent]:m_Agents)
        if(agent.consumer && agent.consumer->owner==owner)
        {
            const auto handle=agent.consumer->handle;
            std::string error;
            Unsubscribe(handle,owner,error);
        }
}
void VansNavigationWorld::ObserveObstacleChange(bool carveChanged)
{
    if(carveChanged && ++m_CarveObstacleRevision==0) ++m_CarveObstacleRevision;
    m_ObstacleBytes=m_Obstacles.capacity()*sizeof(OwnedObstacle);
    for(const auto& item:m_Obstacles)
    {
        m_ObstacleBytes+=item.owner.capacity()+1+item.record.mapId.capacity()+1+item.record.shape.id.capacity()+1+
            item.record.profiles.capacity()*sizeof(std::string);
        for(const auto& profile:item.record.profiles) m_ObstacleBytes+=profile.capacity()+1;
    }
}
bool VansNavigationWorld::ReplaceObstacles(const std::string& owner,
    const std::vector<VansNavigationObstacleRecord>& obstacles,std::string& error)
try
{
    if(!CanMutate(error)) return false;
    if(!m_Initialized) { error="Navigation World is not initialized"; return false; }
    if(owner.empty() || owner.size()>256 || std::any_of(owner.begin(),owner.end(),[](unsigned char c){return c<32 || c==127;}))
    { error="Navigation obstacle owner is invalid"; return false; }
    if(obstacles.size()>kMaximumNavigationObstacles)
    { error="Navigation World obstacle count exceeds capacity"; return false; }
    std::array<const VansNavigationObstacleRecord*,kMaximumNavigationObstacles> incoming;
    std::size_t ownedCount=0;
    for(const auto& item:m_Obstacles) if(item.owner==owner) ++ownedCount;
    const auto count=m_Obstacles.size()-ownedCount+obstacles.size();
    if(count>kMaximumNavigationObstacles)
    { error="Navigation World obstacle count exceeds capacity"; return false; }
    for(std::size_t i=0;i<obstacles.size();++i) incoming[i]=&obstacles[i];
    const auto incomingEnd=incoming.begin()+obstacles.size();
    std::sort(incoming.begin(),incomingEnd,[](const auto* a,const auto* b){return a->shape.id<b->shape.id;});
    if(std::adjacent_find(incoming.begin(),incomingEnd,
        [](const auto* a,const auto* b){return a->shape.id==b->shape.id;})!=incomingEnd)
    { error="Navigation World obstacle identity is duplicated"; return false; }
    bool unchanged=ownedCount==obstacles.size();
    for(const auto& record:obstacles)
    {
        if(!ValidateNavigationObstacleRecord(record,m_Settings,error)) return false;
        const auto found=std::lower_bound(m_Obstacles.begin(),m_Obstacles.end(),record.shape.id,
            [](const auto& item,const auto& id){return item.record.shape.id<id;});
        const bool exists=found!=m_Obstacles.end() && found->record.shape.id==record.shape.id;
        if(exists && found->owner!=owner) { error="Navigation obstacle OwnerConflict"; return false; }
        unchanged=unchanged && exists && SameObstacle(found->record,record);
    }
    if(unchanged) { error.clear(); return true; }
    // 候选深拷贝前计算旧集合、候选和查询/Map 的共存峰值。字符串长度和数量已经有界。
    std::uint64_t candidateBytes=count*sizeof(OwnedObstacle);
    const auto includeRecord=[&](const std::string& recordOwner,const VansNavigationObstacleRecord& record)
    {
        const auto stringBytes=[](const std::string& value){return (std::max)(std::uint64_t(32),2u*(value.size()+1u));};
        candidateBytes+=stringBytes(recordOwner)+stringBytes(record.mapId)+stringBytes(record.shape.id)+
            record.profiles.size()*sizeof(std::string);
        for(const auto& profile:record.profiles) candidateBytes+=stringBytes(profile);
    };
    for(const auto& item:m_Obstacles) if(item.owner!=owner) includeRecord(item.owner,item.record);
    for(const auto& record:obstacles) includeRecord(owner,record);
    std::uint64_t retainedBytes=EstimateNavigationQueryBytes(m_Settings.query)+EstimateNavigationAvoidanceBytes(m_Settings.query.maximumActiveAgents,m_Settings.avoidance)+m_ObstacleBytes;
    std::set<const VansNavigationMap*> maps;
    std::set<const VansNavigationMeshAsset*> assets;
    const auto includeMap=[&](const std::shared_ptr<const VansNavigationMap>& map)
    {
        if(!map || !maps.insert(map.get()).second) return;
        retainedBytes+=map->EstimateResidentBytes();
        const auto asset=map->GetAsset();
        if(assets.insert(asset.get()).second) retainedBytes+=asset->EstimateResidentBytes();
    };
    for(const auto& [key,map]:m_Maps) includeMap(map);
    for(const auto& [key,agent]:m_Agents) includeMap(agent.registration.map);
    for(const auto& map:m_Queries.CollectRetainedMaps()) includeMap(map);
    if(retainedBytes+candidateBytes>static_cast<std::uint64_t>(m_Settings.maximumResidentBytes))
    { error="Navigation World obstacle replacement exceeds aggregate resident capacity before allocation"; return false; }
    std::vector<OwnedObstacle> candidate; candidate.reserve(count);
    for(const auto& item:m_Obstacles) if(item.owner!=owner) candidate.push_back(item);
    for(const auto& record:obstacles)
    {
        OwnedObstacle item{owner,record}; item.previousCenter=record.shape.center;
        const auto previous=std::lower_bound(m_Obstacles.begin(),m_Obstacles.end(),record.shape.id,
            [](const auto& value,const auto& id){return value.record.shape.id<id;});
        if(previous!=m_Obstacles.end() && previous->record.shape.id==record.shape.id &&
            previous->record.enabled && record.enabled && previous->record.shape.kind==record.shape.kind)
        { item.previousCenter=previous->previousCenter; item.observedVelocity=previous->observedVelocity; }
        candidate.push_back(std::move(item));
        auto& profiles=candidate.back().record.profiles;
        std::sort(profiles.begin(),profiles.end());
    }
    std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.record.shape.id<b.record.shape.id;});
    // 只比较有效 Carve 输入；纯 Avoidance 平移和 Carve/Both 互换不会触发几何重收集。
    bool carveChanged=false; auto previous=m_Obstacles.begin(),next=candidate.begin();
    for(;;)
    {
        while(previous!=m_Obstacles.end() && !Carves(previous->record)) ++previous;
        while(next!=candidate.end() && !Carves(next->record)) ++next;
        if(previous==m_Obstacles.end() || next==candidate.end())
        { carveChanged=(previous==m_Obstacles.end())!=(next==candidate.end()); break; }
        if(!SameObstacleGeometry(previous->record,next->record)) { carveChanged=true; break; }
        ++previous; ++next;
    }
    m_Obstacles.swap(candidate); ObserveObstacleChange(carveChanged); error.clear(); return true;
}
catch(const std::bad_alloc&)
{ error="Navigation World obstacle candidate allocation failed"; return false; }
bool VansNavigationWorld::CaptureObstacles(std::vector<VansNavigationObstacleRecord>& output,std::string& error) const
try
{
    if(!m_Initialized) { error="Navigation World is not initialized"; return false; }
    std::vector<VansNavigationObstacleRecord> candidate; candidate.reserve(m_Obstacles.size());
    for(const auto& item:m_Obstacles) candidate.push_back(item.record);
    output.swap(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&)
{ error="Navigation World obstacle snapshot allocation failed"; return false; }
VansNavigationSubscriptionHandle VansNavigationWorld::Subscribe(VansEntityHandle entity,const std::string& owner,
    const std::vector<VansNavigationLinkKey>& links,float maximumTraversalSeconds,std::string& error)
try
{
    if(!CanMutate(error)) return {};
    const auto found=m_Agents.find(EntityKey(entity));
    if(found==m_Agents.end()) { error="NavigationAgent is not registered"; return {}; }
    if(owner.empty() || owner.size()>256 || !std::isfinite(maximumTraversalSeconds) ||
        maximumTraversalSeconds<.1f || maximumTraversalSeconds>60.f)
    { error="Navigation subscription requires an owner and bounded traversal timeout"; return {}; }
    auto& agent=found->second;
    if(agent.consumer) { error=agent.consumer->owner!=owner?"OwnerConflict":"NavigationAgent already has a traversal subscription"; return {}; }
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities> compiled{};
    if(!CompileNavigationLinkCapabilities(links,compiled,error)) return {};
    for(const auto key:links) if(!agent.registration.map->HasLink(key))
    { error="Navigation Link is absent from the Agent Map/Profile"; return {}; }
    if(m_NextConsumerIdentity==(std::numeric_limits<std::uint64_t>::max)())
    { error="Navigation consumer identity exhausted"; return {}; }
    AgentRuntime::Consumer candidate{{m_Session,m_NextConsumerIdentity+1,entity},owner,links,maximumTraversalSeconds};
    if(!agent.query->SetLinkCapabilities(candidate.links,error)) return {};
    agent.consumer=std::move(candidate); ++m_NextConsumerIdentity;
    if(Active(agent.snapshot.state))
    {
        if(agent.snapshot.state==VansNavigationMoveState::Paused) agent.pausedWithCorridor=false;
        else Queue(agent,VansNavigationRequestReason::RequestOptions);
        agent.registration.motor->Submit({});
    }
    error.clear(); return agent.consumer->handle;
}
catch(const std::bad_alloc&) { error="Navigation subscription allocation failed"; return {}; }
bool VansNavigationWorld::Unsubscribe(VansNavigationSubscriptionHandle handle,const std::string& owner,std::string& error)
{
    if(!CanMutate(error)) return false;
    const auto found=m_Agents.find(EntityKey(handle.agent));
    if(!handle.IsValid() || handle.session!=m_Session || found==m_Agents.end() || !found->second.consumer ||
        found->second.consumer->handle.serial!=handle.serial)
    { error="Navigation subscription handle is stale"; return false; }
    auto& agent=found->second;
    if(agent.consumer->owner!=owner || owner.empty()) { error="OwnerConflict"; return false; }
    CancelTraversal(agent,"Navigation traversal consumer unsubscribed");
    agent.consumer.reset();
    if(!agent.query->SetLinkCapabilities({},error)) return false;
    if(Active(agent.snapshot.state))
    {
        if(agent.snapshot.state==VansNavigationMoveState::Paused) agent.pausedWithCorridor=false;
        else Queue(agent,VansNavigationRequestReason::RequestOptions);
    }
    agent.registration.motor->Submit({}); error.clear(); return true;
}
bool VansNavigationWorld::HasSubscription(VansNavigationSubscriptionHandle handle,const std::string& owner) const
{
    const auto found=m_Agents.find(EntityKey(handle.agent));
    return handle.IsValid() && handle.session==m_Session && found!=m_Agents.end() && found->second.consumer &&
        found->second.consumer->handle.serial==handle.serial && found->second.consumer->owner==owner && !owner.empty();
}
VansNavigationWorld::AgentRuntime* VansNavigationWorld::ResolveTraversal(VansNavigationTraversalHandle handle,
    const std::string& owner,std::string& error)
{
    if(!CanMutate(error)) return nullptr;
    const auto found=m_Agents.find(EntityKey(handle.agent));
    if(!handle.IsValid() || handle.session!=m_Session || found==m_Agents.end() ||
        found->second.snapshot.traversal.handle.serial!=handle.serial || !found->second.consumer ||
        found->second.snapshot.traversal.subscription.serial!=found->second.consumer->handle.serial)
    { error="Navigation traversal handle is stale"; return nullptr; }
    if(owner.empty() || found->second.consumer->owner!=owner) { error="OwnerConflict"; return nullptr; }
    error.clear(); return &found->second;
}
bool VansNavigationWorld::HasTraversalControl(VansEntityHandle entity,const std::string& owner) const
{
    const auto found=m_Agents.find(EntityKey(entity));
    return found!=m_Agents.end() && found->second.consumer && found->second.consumer->owner==owner &&
        found->second.snapshot.state==VansNavigationMoveState::TraversingLink &&
        found->second.snapshot.traversal.phase==VansNavigationTraversalPhase::Traversing &&
        !found->second.traversalCompletionRequested;
}
bool VansNavigationWorld::BeginTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error)
{
    auto* agent=ResolveTraversal(handle,owner,error); if(!agent) return false;
    if(agent->snapshot.state!=VansNavigationMoveState::TraversingLink ||
        agent->snapshot.traversal.phase!=VansNavigationTraversalPhase::TraversalRequested)
    { error="Navigation Link has not requested traversal"; return false; }
    const auto motor=agent->registration.motor->Read(); const auto& link=agent->snapshot.traversal.link;
    if(!motor.enabled || motor.movementBlocked || HorizontalDistance(motor.position,link.entry)>link.radiusMeters ||
        std::abs(motor.position.y-link.entry.y)>agent->options.heightTolerance)
    { error="Navigation Link entrance no longer admits the actual motor"; return false; }
    agent->snapshot.traversal.phase=VansNavigationTraversalPhase::Traversing;
    PublishLifecycle(*agent,VansNavigationLifecycleKind::Traversal);
    return true;
}
bool VansNavigationWorld::CompleteTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error)
{
    auto* agent=ResolveTraversal(handle,owner,error); if(!agent) return false;
    if(agent->snapshot.traversal.phase!=VansNavigationTraversalPhase::Traversing || agent->traversalCompletionRequested)
    { error="Navigation Link is not accepting a completion report"; return false; }
    const auto motor=agent->registration.motor->Read(); const auto& link=agent->snapshot.traversal.link;
    if(!motor.enabled || motor.movementBlocked || HorizontalDistance(motor.position,link.exit)>link.radiusMeters ||
        std::abs(motor.position.y-link.exit.y)>agent->options.heightTolerance)
    { error="Navigation Link completion requires the actual motor at its exit"; return false; }
    agent->traversalCompletionRequested=true;
    // consumer 在回报前清除自己的动作意图；导航验证仍经唯一查询调度器。
    agent->registration.motor->Submit({});
    return true;
}
bool VansNavigationWorld::AbortTraversal(VansNavigationTraversalHandle handle,const std::string& owner,std::string& error)
{
    auto* agent=ResolveTraversal(handle,owner,error); if(!agent) return false;
    const auto phase=agent->snapshot.traversal.phase;
    if(phase!=VansNavigationTraversalPhase::TraversalRequested && phase!=VansNavigationTraversalPhase::Traversing)
    { error="Navigation Link traversal is not active"; return false; }
    CancelTraversal(*agent,"Navigation traversal consumer aborted the action");
    Finish(*agent,VansNavigationMoveResult::Failed); return true;
}
void VansNavigationWorld::CancelTraversal(AgentRuntime& agent,const char* diagnostic)
{
    if(agent.traversalQuery.IsValid())
    { std::string error; m_Queries.Release(agent.traversalQuery,agent.snapshot.owner,error); agent.traversalQuery={}; }
    agent.traversalCompletionRequested=false;
    auto& traversal=agent.snapshot.traversal;
    if(traversal.phase==VansNavigationTraversalPhase::Approaching ||
        traversal.phase==VansNavigationTraversalPhase::TraversalRequested || traversal.phase==VansNavigationTraversalPhase::Traversing)
    { traversal.phase=VansNavigationTraversalPhase::Aborted; traversal.diagnostic=diagnostic;
      VansNavigationMotion hold; hold.facingYaw=agent.options.facingYaw; agent.registration.motor->Submit(hold);
      PublishLifecycle(agent,VansNavigationLifecycleKind::Traversal); }
}
void VansNavigationWorld::UpdateTraversal(AgentRuntime& agent,const VansNavigationMotorSample& motor,float dt)
{
    auto& traversal=agent.snapshot.traversal;
    const auto reject=[&](const char* reason)
    { CancelTraversal(agent,reason); agent.snapshot.path.diagnostic=reason; Finish(agent,VansNavigationMoveResult::Failed); };
    if(!agent.consumer || motor.movementBlocked) { reject("Navigation traversal consumer or motor control unavailable"); return; }
    traversal.elapsedSeconds+=dt;
    if(traversal.elapsedSeconds>=agent.consumer->maximumSeconds)
    { reject("Navigation Link traversal exceeded its bounded duration"); return; }
    const auto current=agent.query->CaptureCorridorSteering(traversal.link.entry);
    if(!current.valid || !current.link || !(current.link->key==traversal.link.key) ||
        glm::distance(current.link->entry,traversal.link.entry)>.001f || glm::distance(current.link->exit,traversal.link.exit)>.001f)
    { reject("Navigation Link endpoints or dependent Tile data changed"); return; }
    agent.hadMoveIntent=false; agent.snapshot.desiredSpeed=0;
    if(traversal.phase==VansNavigationTraversalPhase::TraversalRequested)
    { agent.registration.motor->Submit({}); return; }
    if(!agent.traversalCompletionRequested) return;
    agent.registration.motor->Submit({});
    if(HorizontalDistance(motor.position,traversal.link.exit)>traversal.link.radiusMeters ||
        std::abs(motor.position.y-traversal.link.exit.y)>agent.options.heightTolerance)
    { reject("Navigation Link actual motor left its reported exit"); return; }
    std::string error;
    if(!agent.traversalQuery.IsValid())
    {
        VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::Raycast;
        request.mapId=agent.registration.config.mapId; request.profileId=agent.registration.config.profileId;
        request.filterId=agent.registration.config.queryFilter; request.start=traversal.link.exit; request.goal=motor.position;
        request.nearestExtents={traversal.link.radiusMeters,agent.options.heightTolerance,traversal.link.radiusMeters};
        agent.traversalQuery=m_Queries.Submit(request,agent.snapshot.owner,agent.registration.map,agent.registration.entity,error);
        if(!agent.traversalQuery.IsValid()) { reject("Navigation Link exit validation could not enter the query scheduler"); return; }
    }
    const auto result=m_Queries.Capture(agent.traversalQuery,agent.snapshot.owner,error);
    if(!result || result->lifecycle==VansNavigationQueryLifecycle::Cancelled || result->lifecycle==VansNavigationQueryLifecycle::WaitingForData)
    { reject("Navigation Link exit data unavailable during completion"); return; }
    if(result->lifecycle!=VansNavigationQueryLifecycle::Finished) return;
    if(!result->raycast || !result->raycast->valid || result->raycast->hit)
    { reject("Navigation Link actual exit is outside its reachable ground surface"); return; }
    // 完成报告后 CCT 仍可能处于合法减速。异步结果只能证明提交时的脚点，
    // 若当前脚点已经移动，继续在同一超时与查询预算中验证新位置。
    if(glm::distance(result->raycast->requestedEnd,motor.position)>.05f)
    {
        m_Queries.Release(agent.traversalQuery,agent.snapshot.owner,error); agent.traversalQuery={};
        return;
    }
    if(HorizontalDistance(result->raycast->position,motor.position)>.05f ||
        std::abs(result->raycast->position.y-motor.position.y)>agent.options.heightTolerance)
    { reject("Navigation Link actual exit does not match its validated ground surface"); return; }
    traversal.phase=VansNavigationTraversalPhase::Completed;
    m_Queries.Release(agent.traversalQuery,agent.snapshot.owner,error); agent.traversalQuery={};
    agent.traversalCompletionRequested=false;
    Queue(agent,VansNavigationRequestReason::PathRecovery);
    PublishLifecycle(agent,VansNavigationLifecycleKind::Traversal);
}
void VansNavigationWorld::Queue(AgentRuntime& agent, VansNavigationRequestReason reason)
{
    CancelTraversal(agent,"Navigation request replanned during Link traversal");
    agent.snapshot.avoidance.yield={};
    std::string error;
    if(agent.queryRequest.IsValid()) m_Queries.Release(agent.queryRequest,agent.snapshot.owner,error);
    agent.queryRequest={};
	agent.query->ResetQuery();
	agent.followWaitFrames=0;
	agent.hasProgressWindow=false; agent.hadMoveIntent=false;
	agent.snapshot.state = VansNavigationMoveState::Queued;
	agent.snapshot.reason = reason;
	agent.snapshot.desiredSpeed = 0.0f;
}
void VansNavigationWorld::Finish(AgentRuntime& agent, VansNavigationMoveResult result)
{
    const bool wasActive=Active(agent.snapshot.state);
    CancelTraversal(agent,result==VansNavigationMoveResult::Cancelled?
        "Navigation request cancelled":"Navigation request ended during Link traversal");
    agent.snapshot.avoidance.yield={};
    std::string error;
    if(agent.queryRequest.IsValid()) m_Queries.Release(agent.queryRequest,agent.snapshot.owner,error);
    agent.queryRequest={};
	agent.query->ResetQuery();
	agent.hasProgressWindow=false; agent.hadMoveIntent=false;
	agent.snapshot.state = VansNavigationMoveState::Finished;
	agent.snapshot.result = result;
	agent.snapshot.desiredSpeed = 0.0f;
	VansNavigationMotion hold;
	hold.facingYaw = agent.options.facingYaw;
	agent.registration.motor->Submit(hold);
    if(wasActive && agent.snapshot.request.IsValid()) PublishLifecycle(agent,VansNavigationLifecycleKind::Completion);
}
void VansNavigationWorld::WaitForData(AgentRuntime& agent,bool resumeCorridor)
{
    agent.query->CancelCorridorLocalQuery();
    agent.hadMoveIntent=false; agent.followWaitFrames=0;
    agent.avoidanceWaitFrames=0; agent.avoidanceBlockedSeconds=0; agent.snapshot.avoidance={};
    agent.waitingWithCorridor=resumeCorridor;
    agent.dataWaitRevision=agent.registration.map->GetTraversalRevision();
    agent.snapshot.state=VansNavigationMoveState::WaitingForData;
    agent.snapshot.path.failure=VansNavigationPathFailure::DataUnavailable;
    agent.snapshot.path.diagnostic="Waiting for changed Tile data";
    agent.snapshot.desiredSpeed=0;
    VansNavigationMotion hold; hold.facingYaw=agent.options.facingYaw;
    agent.registration.motor->Submit(hold);
}
VansNavigationRequestHandle VansNavigationWorld::RequestMove(VansEntityHandle entity,
	const std::string& owner, const glm::vec3& goal,
	const VansNavigationMoveOptions& options, std::string& error)
{
    if(!CanMutate(error)) return {};
	error.clear();
	const auto found = m_Agents.find(EntityKey(entity));
	if (found == m_Agents.end()) { error = "NavigationAgent is not registered"; return {}; }
	if (owner.empty() || owner.size()>256 || !Finite(goal) || !std::isfinite(options.arrivalRadius) || options.arrivalRadius < 0.0f ||
		!std::isfinite(options.heightTolerance) || options.heightTolerance <= 0.0f ||
		(options.facingYaw && !std::isfinite(*options.facingYaw)))
	{ error = "Navigation move requires an owner, finite goal and valid arrival/facing options"; return {}; }
	if(!ValidateNavigationGoalProjection(options.goalAcceptance,options.goalProjectionToleranceMeters,error)) return {};
	auto& agent = found->second;
	if (Active(agent.snapshot.state))
	{
		if (agent.snapshot.owner != owner) { error = "OwnerConflict"; return {}; }
		const bool paused=agent.snapshot.state==VansNavigationMoveState::Paused;
		const bool queryOptionsChanged=agent.options.goalAcceptance!=options.goalAcceptance ||
			agent.options.goalProjectionToleranceMeters!=options.goalProjectionToleranceMeters ||
			agent.options.heightTolerance!=options.heightTolerance || agent.options.allowPartial!=options.allowPartial;
		agent.options = options;
		UpdateGoal(agent.snapshot.request, owner, goal, options.facingYaw, error);
		if(queryOptionsChanged)
		{ Queue(agent,VansNavigationRequestReason::RequestOptions); if(paused) Pause(agent.snapshot.request,owner,error); }
		return agent.snapshot.request;
	}
	if (agent.snapshot.state == VansNavigationMoveState::Finished && agent.snapshot.owner == owner)
	{
		const auto position = agent.registration.motor->Read().position;
		const bool sameProjectionPolicy=agent.options.goalAcceptance==options.goalAcceptance &&
			agent.options.goalProjectionToleranceMeters==options.goalProjectionToleranceMeters;
		const bool reached = agent.snapshot.result == VansNavigationMoveResult::ReachedGoal &&
			sameProjectionPolicy && HorizontalDistance(position, goal) <= options.arrivalRadius && std::abs(position.y - goal.y) <= options.heightTolerance;
		const bool failedUnchanged = (agent.snapshot.result == VansNavigationMoveResult::Failed || agent.snapshot.result == VansNavigationMoveResult::Stuck) &&
			glm::distance(agent.snapshot.goal, goal) <= agent.registration.config.targetMoveThreshold &&
            agent.options.arrivalRadius == options.arrivalRadius && agent.options.heightTolerance == options.heightTolerance &&
            agent.options.allowPartial == options.allowPartial && sameProjectionPolicy;
		if (reached || failedUnchanged)
		{
			agent.snapshot.goal = goal;
			agent.options = options;
			VansNavigationMotion hold;
			hold.facingYaw = options.facingYaw;
			agent.registration.motor->Submit(hold);
			return agent.snapshot.request;
		}
	}
	if (m_NextRequest == (std::numeric_limits<std::uint64_t>::max)())
	{ error = "Navigation request identity exhausted"; return {}; }
	agent.snapshot = {};
	agent.snapshot.request = { m_Session, ++m_NextRequest, entity };
	agent.snapshot.owner = owner;
	agent.snapshot.goal = goal;
	agent.options = options;
	agent.hasQueriedGoal = false;
	agent.stuckSeconds = 0.0f;
    agent.avoidanceWaitFrames=0; agent.avoidanceBlockedSeconds=0;
	agent.dataWaitFrames=0; agent.waitingWithCorridor=false;
	Queue(agent, VansNavigationRequestReason::InitialTarget);
	return agent.snapshot.request;
}
VansNavigationWorld::AgentRuntime* VansNavigationWorld::ResolveRequest(
	VansNavigationRequestHandle request, const std::string& owner, std::string& error)
{
    if(!CanMutate(error)) return nullptr;
	const auto found = m_Agents.find(EntityKey(request.agent));
	if (!request.IsValid() || request.session != m_Session || found == m_Agents.end() ||
		found->second.snapshot.request.serial != request.serial)
	{ error = "Navigation request handle is stale"; return nullptr; }
	if (owner.empty() || found->second.snapshot.owner != owner)
	{ error = "OwnerConflict"; return nullptr; }
	error.clear();
	return &found->second;
}
bool VansNavigationWorld::UpdateGoal(VansNavigationRequestHandle request, const std::string& owner,
	const glm::vec3& goal, std::optional<float> facingYaw, std::string& error)
{
	if (!Finite(goal) || (facingYaw && !std::isfinite(*facingYaw)))
	{ error = "Navigation goal and facing must be finite"; return false; }
	auto* agent = ResolveRequest(request, owner, error);
	if (!agent) return false;
	if (!Active(agent->snapshot.state)) { error = "Navigation request has finished"; return false; }
	agent->snapshot.goal = goal;
	agent->options.facingYaw = facingYaw;
	return true;
}
bool VansNavigationWorld::Stop(VansNavigationRequestHandle request, const std::string& owner, std::string& error)
{
	auto* agent = ResolveRequest(request, owner, error);
	if (!agent) return false;
	Finish(*agent, VansNavigationMoveResult::Cancelled);
	return true;
}
bool VansNavigationWorld::Pause(VansNavigationRequestHandle request, const std::string& owner, std::string& error)
{
	auto* agent = ResolveRequest(request, owner, error);
	if (!agent) return false;
	if (!Active(agent->snapshot.state)) { error = "Navigation request has finished"; return false; }
    CancelTraversal(*agent,"Navigation request paused during Link traversal");
	agent->pausedWithCorridor = agent->snapshot.state == VansNavigationMoveState::Following;
	agent->query->CancelCorridorLocalQuery(); agent->followWaitFrames=0;
	agent->hasProgressWindow=false; agent->hadMoveIntent=false;
	if (!agent->pausedWithCorridor) agent->query->ResetQuery();
    if(agent->queryRequest.IsValid()) m_Queries.Release(agent->queryRequest,owner,error);
    agent->queryRequest={};
	agent->snapshot.state = VansNavigationMoveState::Paused;
	agent->snapshot.desiredSpeed = 0.0f;
	agent->registration.motor->Submit({});
	return true;
}
bool VansNavigationWorld::Resume(VansNavigationRequestHandle request, const std::string& owner, std::string& error)
{
	auto* agent = ResolveRequest(request, owner, error);
	if (!agent) return false;
	if (agent->snapshot.state != VansNavigationMoveState::Paused)
	{ error = "Navigation request is not paused"; return false; }
	if (agent->pausedWithCorridor) agent->snapshot.state = VansNavigationMoveState::Following;
	else Queue(*agent, VansNavigationRequestReason::PathRecovery);
	return true;
}
std::optional<VansNavigationAgentSnapshot> VansNavigationWorld::CaptureAgent(VansEntityHandle entity) const
{
	const auto found = m_Agents.find(EntityKey(entity));
	if (found == m_Agents.end()) return std::nullopt;
	return found->second.snapshot;
}
bool VansNavigationWorld::RegisterMaps(const std::vector<std::shared_ptr<const VansNavigationMap>>& maps,std::string& error)
try
{
    if(!CanMutate(error)) return false;
    if(!m_Initialized) { error="Navigation World is not initialized"; return false; }
    std::map<std::pair<std::string,std::string>,std::shared_ptr<const VansNavigationMap>> candidate;
    std::set<const VansNavigationMeshAsset*> assets;
    std::set<const VansNavigationMap*> counted;
    std::uint64_t bytes=EstimateNavigationQueryBytes(m_Settings.query)+EstimateNavigationAvoidanceBytes(m_Settings.query.maximumActiveAgents,m_Settings.avoidance)+m_ObstacleBytes,tiles=0;
    const auto include=[&](const std::shared_ptr<const VansNavigationMap>& map)
    {
        if(!counted.insert(map.get()).second) return;
        const auto asset=map->GetAsset();
        bytes+=map->EstimateResidentBytes(); tiles+=map->GetResidentTileCount();
        if(assets.insert(asset.get()).second) bytes+=asset->EstimateResidentBytes();
    };
    for(const auto& [key,map]:m_Maps) include(map);
    for(const auto& [key,agent]:m_Agents) include(agent.registration.map);
    for(const auto& map:m_Queries.CollectRetainedMaps()) include(map);
    for(const auto& map:maps)
    {
        if(!map || !map->IsReady()) { error="MapUnavailable"; return false; }
        const auto asset=map->GetAsset();
        if(!candidate.emplace(std::make_pair(asset->GetMapId(),map->GetProfileId()),map).second)
        { error="Navigation Map/Profile registration is duplicated"; return false; }
        include(map);
    }
    if(bytes>static_cast<std::uint64_t>(m_Settings.maximumResidentBytes) || tiles>static_cast<std::uint64_t>(m_Settings.maximumResidentTiles))
    { error="Navigation Maps and query capacities exceed aggregate resident budget"; return false; }
    for(const auto& [key,map]:m_Maps)
        if(!candidate.count(key) || candidate.at(key)!=map) m_Queries.CancelMap(map.get());
    m_Maps=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation Map registration allocation failed"; return false; }
void VansNavigationWorld::WithdrawMap(const std::string& mapId)
{
    if(m_PublishingLifecycle) return;
    for(auto iterator=m_Maps.begin();iterator!=m_Maps.end();)
        if(iterator->first.first==mapId) { m_Queries.CancelMap(iterator->second.get()); iterator=m_Maps.erase(iterator); }
        else ++iterator;
    for(auto iterator=m_Agents.begin();iterator!=m_Agents.end();)
        if(iterator->second.registration.config.mapId==mapId)
        {
            m_Queries.CancelEntity(iterator->second.registration.entity);
            Finish(iterator->second,VansNavigationMoveResult::Cancelled); iterator=m_Agents.erase(iterator);
        }
        else ++iterator;
}
VansNavigationQueryHandle VansNavigationWorld::RequestQuery(const VansNavigationQueryRequest& request,
    const std::string& owner,std::string& error)
{
    if(!CanMutate(error)) return {};
    const auto map=m_Maps.find({request.mapId,request.profileId});
    if(map==m_Maps.end()) { error="MapUnavailable"; return {}; }
    return m_Queries.Submit(request,owner,map->second,{},error);
}
VansNavigationQueryHandle VansNavigationWorld::SampleReachablePoint(VansEntityHandle entity,
    const std::string& owner,const glm::vec3& anchor,float radius,std::uint32_t seed,std::string& error)
{
    if(!CanMutate(error)) return {};
    const auto found=m_Agents.find(EntityKey(entity));
    if(found==m_Agents.end()) { error="Reachable sampling requires a registered Agent"; return {}; }
    const auto& registration=found->second.registration;
    VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::ReachablePoint;
    request.mapId=registration.config.mapId; request.profileId=registration.config.profileId; request.filterId=registration.config.queryFilter;
    request.start=registration.motor->Read().position; request.goal=anchor; request.radius=radius; request.seed=seed;
    return m_Queries.Submit(request,owner,registration.map,entity,error);
}
std::optional<VansNavigationQuerySnapshot> VansNavigationWorld::CaptureQuery(VansNavigationQueryHandle handle,
    const std::string& owner,std::string& error) const { return m_Queries.Capture(handle,owner,error); }
bool VansNavigationWorld::CancelQuery(VansNavigationQueryHandle handle,const std::string& owner,std::string& error)
{ return CanMutate(error) && m_Queries.Cancel(handle,owner,error); }
bool VansNavigationWorld::ReleaseQuery(VansNavigationQueryHandle handle,const std::string& owner,std::string& error)
{ return CanMutate(error) && m_Queries.Release(handle,owner,error); }
void VansNavigationWorld::UpdateProgressWindow(AgentRuntime& agent, const glm::vec3& actualPosition,
	const VansNavigationSteering& steering, float minimumProgress)
{
	bool rebase=!agent.hasProgressWindow;
	if(agent.hasProgressWindow)
	{
		const glm::vec2 displacement(actualPosition.x-agent.progressPosition.x,actualPosition.z-agent.progressPosition.z);
		const bool moved=glm::length(displacement)>=minimumProgress;
		const float alongWindow=glm::dot(displacement,glm::vec2(agent.progressDirection.x,agent.progressDirection.z));
		const float alongCurrent=glm::dot(displacement,glm::vec2(steering.direction.x,steering.direction.z));
		// 目标修复不能伪装成角色进度；摆动返回旧位置也不构成新的窗口净推进。
		const bool distanceReduced=steering.endpoint==agent.progressEndpoint &&
			agent.progressRemainingDistance-steering.remainingDistance>=minimumProgress;
		if(moved && (alongWindow>=minimumProgress || (distanceReduced && alongCurrent>=minimumProgress*.25f)))
		{
			agent.stuckSeconds=0; rebase=true;
		}
		else if(moved && glm::dot(agent.progressDirection,steering.direction)<.5f)
		{
			// 跨拐角时切换度量方向；换方向本身不能重置已累计的受阻时间。
			rebase=true;
		}
	}
	if(rebase)
	{
		agent.progressPosition=actualPosition; agent.progressDirection=steering.direction;
		agent.progressEndpoint=steering.endpoint; agent.progressRemainingDistance=steering.remainingDistance;
		agent.hasProgressWindow=true;
	}
}
void VansNavigationWorld::Update(double deltaSeconds)
{
    if(m_PublishingLifecycle) return;
	VANS_PROFILE_SCOPE("Navigation::Update", Vans::ProfileCategory::Script);
	if (!m_Initialized) return;
    // 速度只由连续冻结姿态观测；不引入 Physics 回调、第二份位置积分或 Scene 运动历史。
    for(auto& obstacle:m_Obstacles)
    {
        obstacle.observedVelocity=glm::vec3(0);
        if(std::isfinite(deltaSeconds) && deltaSeconds>0 && obstacle.record.enabled)
            for(int axis=0;axis<3;++axis) obstacle.observedVelocity[axis]=static_cast<float>(
                (static_cast<double>(obstacle.record.shape.center[axis])-obstacle.previousCenter[axis])/deltaSeconds);
        obstacle.previousCenter=obstacle.record.shape.center;
    }
    if(!std::isfinite(deltaSeconds) || deltaSeconds<=0) return;
	const float dt = static_cast<float>(deltaSeconds);
	if (!std::isfinite(dt)) return;
    m_Queries.BeginFrame();
    // 交替首个量子的归属；即使整帧只有一个节点，搜索和跟随也不会互相饿死。
    if(m_WorkerFirst) m_Queries.Dispatch(m_Settings.query.maximumIterationsPerSlice);
    m_WorkerFirst=!m_WorkerFirst;
    const bool avoidanceFrame=std::any_of(m_Agents.begin(),m_Agents.end(),[](const auto& item){return item.second.registration.config.avoidance;});
    if(avoidanceFrame)
    {
        m_Avoidance->BeginFrame();
        for(auto& [key,agent]:m_Agents)
        {
            agent.frameMotor=agent.registration.motor->Read(); agent.avoidanceIndex=-1;
            const auto profile=agent.registration.map->GetBakeSettings();
            if(!agent.frameMotor.enabled || !profile) continue;
            const auto map=std::find_if(m_Settings.maps.begin(),m_Settings.maps.end(),[&](const auto& item){return item.id==agent.registration.config.mapId;});
            if(map==m_Settings.maps.end()) continue;
            VansNavigationAvoidanceAgent fact; fact.identity=key; fact.map=static_cast<std::uint32_t>(map-m_Settings.maps.begin());
            const auto profileId=std::find(map->profiles.begin(),map->profiles.end(),agent.registration.config.profileId);
            if(profileId==map->profiles.end()) continue;
            fact.profile=static_cast<std::uint32_t>(profileId-map->profiles.begin());
            fact.reciprocal=agent.registration.config.avoidance && agent.snapshot.state==VansNavigationMoveState::Following;
            fact.position=agent.frameMotor.position; fact.velocity=agent.frameMotor.velocity;
            fact.radiusMeters=profile->agentRadius; fact.heightMeters=profile->agentHeight;
            fact.yield=agent.snapshot.avoidance.yield;
            agent.avoidanceIndex=m_Avoidance->AddAgent(fact);
        }
        for(std::size_t i=0;i<m_Obstacles.size();++i)
        {
            const auto& obstacle=m_Obstacles[i];
            if(!obstacle.record.enabled || !NavigationObstacleAvoids(obstacle.record.mode)) continue;
            const auto map=std::find_if(m_Settings.maps.begin(),m_Settings.maps.end(),[&](const auto& item){return item.id==obstacle.record.mapId;});
            std::uint32_t profileMask=0;
            for(std::size_t p=0;p<map->profiles.size();++p)
                if(std::find(obstacle.record.profiles.begin(),obstacle.record.profiles.end(),map->profiles[p])!=obstacle.record.profiles.end()) profileMask|=1u<<p;
            if(!m_Avoidance->AddObstacle(i+1,static_cast<std::uint32_t>(map-m_Settings.maps.begin()),profileMask,
                obstacle.record.shape,obstacle.observedVelocity)) break; // 不完整帧由采样器明确输出零速度并进入有界等待。
        }
    }
	auto it = m_Agents.upper_bound(m_AgentCursor);
	if (it == m_Agents.end()) it = m_Agents.begin();
	if (it != m_Agents.end()) m_AgentCursor = it->first;
	for (std::size_t visited = 0; visited < m_Agents.size(); ++visited)
	{
		if (it == m_Agents.end()) it = m_Agents.begin();
		auto& [key, agent] = *it++;
		auto& snapshot = agent.snapshot;
		const auto motor = avoidanceFrame?agent.frameMotor:agent.registration.motor->Read();
        const auto previousYield=snapshot.avoidance.yield;
        snapshot.avoidance={}; snapshot.avoidance.yield=previousYield;
		// 本次反馈对应上一次已提交的移动意图；合法等待没有运动时间可累计。
		if(agent.hadMoveIntent && snapshot.state==VansNavigationMoveState::Following && !motor.movementBlocked)
			agent.stuckSeconds+=dt;
		agent.hadMoveIntent=false;
		snapshot.movementBlocked = motor.movementBlocked;
		if (!motor.enabled) { if (Active(snapshot.state)) Finish(agent, VansNavigationMoveResult::Cancelled); continue; }
        if(snapshot.state==VansNavigationMoveState::TraversingLink)
        { UpdateTraversal(agent,motor,dt); continue; }
		if (!Active(snapshot.state) || snapshot.state == VansNavigationMoveState::Paused || motor.movementBlocked)
		{
			agent.hasProgressWindow=false;
			snapshot.desiredSpeed = 0.0f;
			VansNavigationMotion hold;
			hold.facingYaw = agent.options.facingYaw;
			agent.registration.motor->Submit(hold);
			continue;
		}
		const auto& config = agent.registration.config;
        if(snapshot.state==VansNavigationMoveState::WaitingForData && !agent.queryRequest.IsValid())
        {
            if(++agent.dataWaitFrames>m_Settings.query.maximumQueryFrames)
            { snapshot.path.failure=VansNavigationPathFailure::QueryDeadlineExceeded; Finish(agent,VansNavigationMoveResult::Failed); continue; }
            const auto revision=agent.registration.map->GetTraversalRevision();
            if(revision!=agent.dataWaitRevision)
            {
                agent.dataWaitRevision=revision;
                if(agent.waitingWithCorridor)
                {
                    const auto current=agent.query->CaptureCorridorSteering(motor.position);
                    if(current.valid)
                    { snapshot.state=VansNavigationMoveState::Following; snapshot.path.failure=VansNavigationPathFailure::None; agent.dataWaitFrames=0; }
                    else if(current.failure!=VansNavigationPathFailure::DataUnavailable)
                        Queue(agent,VansNavigationRequestReason::PathRecovery);
                }
                else Queue(agent,VansNavigationRequestReason::PathRecovery);
            }
            if(snapshot.state==VansNavigationMoveState::WaitingForData)
            {
                snapshot.desiredSpeed=0; VansNavigationMotion hold; hold.facingYaw=agent.options.facingYaw;
                agent.registration.motor->Submit(hold); continue;
            }
        }
		agent.repathRemaining = (std::max)(0.0f, agent.repathRemaining - dt);
		agent.optimizationRemaining = (std::max)(0.0f, agent.optimizationRemaining - dt);
		const float movement = agent.hasQueriedGoal ? glm::distance(snapshot.goal, agent.queriedGoal) : 0.0f;
		if (snapshot.state == VansNavigationMoveState::Following &&
			(movement >= config.forceRepathDistance || (agent.repathRemaining <= 0.0f && movement > 0.0f && movement >= config.targetMoveThreshold)))
			Queue(agent, movement >= config.forceRepathDistance ? VansNavigationRequestReason::ForceTargetMoved : VansNavigationRequestReason::TargetMoved);
        if(snapshot.state==VansNavigationMoveState::Queued || snapshot.state==VansNavigationMoveState::WaitingForData)
        {
            std::string error;
            if(!agent.queryRequest.IsValid())
            {
                VansNavigationQueryRequest request;
                request.mapId=config.mapId; request.profileId=config.profileId; request.filterId=config.queryFilter;
                request.start=motor.position; request.goal=snapshot.goal; request.nearestExtents=glm::vec3(1,agent.options.heightTolerance,1);
                request.goalAcceptance=agent.options.goalAcceptance; request.goalProjectionToleranceMeters=agent.options.goalProjectionToleranceMeters;
                request.priority=snapshot.reason==VansNavigationRequestReason::ForceTargetMoved?2:0;
                if(agent.consumer) request.linkCapabilities=agent.consumer->links;
                agent.queryRequest=m_Queries.Submit(request,snapshot.owner,agent.registration.map,agent.registration.entity,error);
                agent.queriedGoal=snapshot.goal; agent.hasQueriedGoal=true;
                if(!agent.queryRequest.IsValid())
                {
                    snapshot.path.status=VansNavigationPathStatus::Failed;
                    snapshot.path.failure=error=="QueueFull"?VansNavigationPathFailure::QueueFull:VansNavigationPathFailure::QueryNotReady;
                    snapshot.path.diagnostic=error; Finish(agent,VansNavigationMoveResult::Failed); continue;
                }
            }
            const auto result=m_Queries.Capture(agent.queryRequest,snapshot.owner,error);
            if(!result) { snapshot.path.diagnostic=error; Finish(agent,VansNavigationMoveResult::Failed); continue; }
            if(result->lifecycle==VansNavigationQueryLifecycle::WaitingForData)
            {
                snapshot.state=VansNavigationMoveState::WaitingForData; snapshot.path=result->path; snapshot.desiredSpeed=0;
                VansNavigationMotion hold; hold.facingYaw=agent.options.facingYaw; agent.registration.motor->Submit(hold); continue;
            }
            snapshot.state=VansNavigationMoveState::Queued;
            if(result->lifecycle==VansNavigationQueryLifecycle::Finished || result->lifecycle==VansNavigationQueryLifecycle::Cancelled)
            {
                snapshot.path=result->path; m_Queries.Release(agent.queryRequest,snapshot.owner,error); agent.queryRequest={};
                if(snapshot.path.status==VansNavigationPathStatus::Failed ||
                    (snapshot.path.status==VansNavigationPathStatus::Partial && !agent.options.allowPartial))
                { Finish(agent,VansNavigationMoveResult::Failed); continue; }
                auto adoption=agent.query->AdoptCorridorRoute(result->route);
                if(adoption!=VansNavigationPathFailure::None)
                {
                    if(adoption==VansNavigationPathFailure::DataUnavailable)
                    { WaitForData(agent,false); continue; }
                    if(adoption==VansNavigationPathFailure::MeshChanged && ++snapshot.recoveryCount<=3 &&
                        agent.query->Initialize(agent.registration.map,m_Settings.query,config.queryFilter,error) &&
                        (!agent.consumer || agent.query->SetLinkCapabilities(agent.consumer->links,error)))
                    { Queue(agent,VansNavigationRequestReason::PathRecovery); continue; }
                    snapshot.path.status=VansNavigationPathStatus::Failed; snapshot.path.failure=adoption;
                    snapshot.path.diagnostic="Search route could not be adopted into the current motor Corridor";
                    Finish(agent,VansNavigationMoveResult::Failed); continue;
                }
                snapshot.state=VansNavigationMoveState::Following; agent.repathRemaining=config.repathInterval;
                agent.dataWaitFrames=0;
				agent.optimizationRemaining=config.repathInterval;
            }
        }
		VansNavigationMotion motion;
		motion.facingYaw = agent.options.facingYaw;
		motion.acceleration = config.acceleration;
		if (snapshot.state == VansNavigationMoveState::Following)
		{
			const float localTargetChange=glm::distance(snapshot.goal,snapshot.path.requestedEnd);
			if(localTargetChange>0)
			{
				if(snapshot.path.status!=VansNavigationPathStatus::Complete)
					Queue(agent,VansNavigationRequestReason::TargetMoved);
				else if(localTargetChange<config.targetMoveThreshold && localTargetChange<config.forceRepathDistance)
				{
					const auto repairBudget=(std::min)(m_Settings.query.maximumIterationsPerSlice,m_Queries.RemainingIterationBudget());
                    int repairIterations=0; bool pending=false;
                    auto failure=VansNavigationPathFailure::None;
                    if(repairBudget>0)
                    {
                        failure=agent.query->TryRepairTarget(snapshot.goal,{1,agent.options.heightTolerance,1},
                            agent.options.goalAcceptance,agent.options.goalProjectionToleranceMeters,
                            repairBudget,repairIterations,pending,snapshot.path);
                        m_Queries.ReserveLocalQueryBudget(repairIterations,0);
                    }
                    if(repairBudget<1 || pending)
                    {
                        if(++agent.followWaitFrames>m_Settings.query.maximumQueryFrames)
                        { snapshot.path.failure=VansNavigationPathFailure::QueryDeadlineExceeded; Finish(agent,VansNavigationMoveResult::Failed); continue; }
                        snapshot.desiredSpeed=0; agent.registration.motor->Submit(motion); continue;
                    }
                    if(failure==VansNavigationPathFailure::DataUnavailable)
                    { WaitForData(agent,true); continue; }
                    if(failure!=VansNavigationPathFailure::None) Queue(agent,VansNavigationRequestReason::TargetMoved);
				}
				if(snapshot.state!=VansNavigationMoveState::Following)
				{ snapshot.desiredSpeed=0; agent.registration.motor->Submit(motion); continue; }
			}
			const auto profile = agent.registration.map->GetBakeSettings();
			if (!profile) { Finish(agent, VansNavigationMoveResult::Failed); continue; }
            const auto followBudget=(std::min)(m_Settings.query.maximumIterationsPerSlice,m_Queries.RemainingIterationBudget());
            if(followBudget<1)
            {
                if(++agent.followWaitFrames>m_Settings.query.maximumQueryFrames)
                { snapshot.path.failure=VansNavigationPathFailure::QueryDeadlineExceeded; Finish(agent,VansNavigationMoveResult::Failed); continue; }
                snapshot.desiredSpeed=0; agent.registration.motor->Submit(motion); continue;
            }
            int followIterations=0;
			auto steering = agent.query->FollowCorridor(motor.position,
				(std::max)(profile->agentRadius * 2.0f, profile->cellSize * 2.0f), agent.options.heightTolerance,followBudget,followIterations);
            m_Queries.ReserveLocalQueryBudget(followIterations,0);
            if(steering.pending)
            {
                if(++agent.followWaitFrames>m_Settings.query.maximumQueryFrames)
                { snapshot.path.failure=VansNavigationPathFailure::QueryDeadlineExceeded; Finish(agent,VansNavigationMoveResult::Failed); continue; }
                snapshot.desiredSpeed=0; agent.registration.motor->Submit(motion); continue;
            }
            agent.followWaitFrames=0;
			if (steering.valid && agent.optimizationRemaining <= 0 &&
				m_Queries.ReserveLocalQueryBudget(agent.query->CorridorOptimizationIterationBudget(),1))
			{
				bool changed = false;
				const auto optimized = agent.query->TryOptimizeCorridor(
					(std::max)(profile->agentRadius * 12.0f, profile->cellSize * 8.0f), snapshot.path, changed);
				agent.optimizationRemaining = config.repathInterval;
				if (optimized == VansNavigationPathFailure::MeshChanged || optimized == VansNavigationPathFailure::MeshNotReady ||
                    optimized==VansNavigationPathFailure::DataUnavailable)
				{ steering.valid = false; steering.failure = optimized; }
				else if (changed) steering = agent.query->CaptureCorridorSteering(motor.position);
			}
			if (!steering.valid)
			{
                if(steering.failure==VansNavigationPathFailure::DataUnavailable)
                { WaitForData(agent,true); continue; }
				if (++snapshot.recoveryCount > 3u) { snapshot.path.failure = steering.failure; Finish(agent, VansNavigationMoveResult::Failed); continue; }
				Queue(agent, VansNavigationRequestReason::PathRecovery);
			}
			else if (glm::distance(snapshot.goal,snapshot.path.requestedEnd)==0 &&
				steering.hasEndpointDistance && steering.remainingDistance <= agent.options.arrivalRadius &&
				HorizontalDistance(motor.position, steering.endpoint) <= agent.options.arrivalRadius &&
				std::abs(motor.position.y - steering.endpoint.y) <= agent.options.heightTolerance)
			{ Finish(agent, snapshot.path.status == VansNavigationPathStatus::Partial ? VansNavigationMoveResult::ReachedPartialEnd : VansNavigationMoveResult::ReachedGoal); continue; }
			else
			{
				motion.direction = steering.direction;
				// 根据完整可走距离减速，既允许停在相邻 polygon，也避免隔墙、跨层或截断窗口提前停止。
                const bool endpointHeightReached=std::abs(motor.position.y-steering.endpoint.y)<=agent.options.heightTolerance;
                const float arrivalAllowance=steering.hasEndpointDistance && endpointHeightReached?agent.options.arrivalRadius:0.f;
				const float distance = (std::max)(0.0f, steering.remainingDistance - arrivalAllowance);
				motion.desiredSpeed = (std::min)(config.maxSpeed, std::sqrt(2.0f * config.acceleration * distance));
                if(steering.link)
                {
                    if(!agent.consumer || std::find(agent.consumer->links.begin(),agent.consumer->links.end(),steering.link->key)==agent.consumer->links.end())
                    { snapshot.path.diagnostic="Navigation Link has no matching traversal consumer"; Finish(agent,VansNavigationMoveResult::Failed); continue; }
                    auto& traversal=snapshot.traversal;
                    if(traversal.phase!=VansNavigationTraversalPhase::Approaching || !(traversal.link.key==steering.link->key) ||
                        traversal.subscription.serial!=agent.consumer->handle.serial)
                    {
                        if(m_NextConsumerIdentity==(std::numeric_limits<std::uint64_t>::max)())
                        { Finish(agent,VansNavigationMoveResult::Failed); continue; }
                        traversal={}; traversal.handle={m_Session,++m_NextConsumerIdentity,agent.registration.entity};
                        traversal.subscription=agent.consumer->handle; traversal.link=*steering.link;
                        traversal.phase=VansNavigationTraversalPhase::Approaching;
                        PublishLifecycle(agent,VansNavigationLifecycleKind::Traversal);
                    }
                    else if(HorizontalDistance(motor.position,traversal.link.entry)<=traversal.link.radiusMeters &&
                        std::abs(motor.position.y-traversal.link.entry.y)<=agent.options.heightTolerance)
                    {
                        traversal.phase=VansNavigationTraversalPhase::TraversalRequested;
                        snapshot.state=VansNavigationMoveState::TraversingLink;
                        snapshot.desiredSpeed=0; agent.hadMoveIntent=false; agent.hasProgressWindow=false;
                        agent.registration.motor->Submit({}); PublishLifecycle(agent,VansNavigationLifecycleKind::Traversal); continue;
                    }
                    motion.desiredSpeed=(std::min)(motion.desiredSpeed,std::sqrt(2.f*config.acceleration*
                        HorizontalDistance(motor.position,traversal.link.entry)));
                }
                if(config.avoidance && motion.desiredSpeed>0)
                {
                    if(agent.avoidanceIndex<0)
                    { snapshot.path.failure=VansNavigationPathFailure::InvalidInput; snapshot.path.diagnostic="Navigation avoidance motor facts are outside its bounded grid";
                      Finish(agent,VansNavigationMoveResult::Failed); continue; }
                    const auto& settings=m_Settings.avoidance;
                    int boundaryIterations=0;
                    const auto boundary=agent.query->CaptureLocalBoundary(motor.position,settings.queryRangeMeters,
                        settings.maximumBoundaryPolygons,settings.maximumBoundarySegments,m_Queries.RemainingIterationBudget(),boundaryIterations);
                    m_Queries.ReserveLocalQueryBudget(boundaryIterations,0);
                    if(boundary.pending)
                    {
                        snapshot.avoidance.limited=true; snapshot.desiredSpeed=0; motion.desiredSpeed=0;
                        if(++agent.avoidanceWaitFrames>m_Settings.query.maximumQueryFrames)
                        { snapshot.path.failure=VansNavigationPathFailure::QueryDeadlineExceeded; Finish(agent,VansNavigationMoveResult::Failed); continue; }
                        agent.registration.motor->Submit(motion); continue;
                    }
                    if(boundary.failure!=VansNavigationPathFailure::None)
                    {
                        if(boundary.failure==VansNavigationPathFailure::DataUnavailable) { WaitForData(agent,true); continue; }
                        snapshot.path.failure=boundary.failure; snapshot.path.diagnostic="Navigation avoidance boundary capture rejected";
                        Finish(agent,VansNavigationMoveResult::Failed); continue;
                    }
                    snapshot.avoidance=m_Avoidance->SelectVelocity(agent.avoidanceIndex,motion.direction*motion.desiredSpeed,motion.desiredSpeed,boundary);
                    motion.desiredSpeed=glm::length(snapshot.avoidance.velocity);
                    motion.direction=motion.desiredSpeed>1.e-4f?snapshot.avoidance.velocity/motion.desiredSpeed:glm::vec3(0);
                    if(snapshot.avoidance.samples==0)
                    {
                        snapshot.avoidance.yield=previousYield;
                        if(++agent.avoidanceWaitFrames>m_Settings.query.maximumQueryFrames)
                        { snapshot.path.failure=VansNavigationPathFailure::QueryDeadlineExceeded; Finish(agent,VansNavigationMoveResult::Failed); continue; }
                    }
                    else
                    {
                        agent.avoidanceWaitFrames=0;
                        if(snapshot.avoidance.yield.peer)
                        {
                            snapshot.avoidance.yield.seconds+=dt;
                            if(snapshot.avoidance.yield.seconds>=settings.maximumYieldSeconds)
                            {
                                snapshot.path.diagnostic="Narrow passage yield exceeded its bounded duration";
                                if(++snapshot.recoveryCount>3) { Finish(agent,VansNavigationMoveResult::Stuck); continue; }
                                Queue(agent,VansNavigationRequestReason::PathRecovery); motion.desiredSpeed=0;
                            }
                        }
                        agent.avoidanceBlockedSeconds=motion.desiredSpeed>1.e-4f || snapshot.avoidance.yield.peer?0:agent.avoidanceBlockedSeconds+dt;
                        if(agent.avoidanceBlockedSeconds>=2)
                        {
                            agent.avoidanceBlockedSeconds=0;
                            if(++snapshot.recoveryCount>3) { Finish(agent,VansNavigationMoveResult::Stuck); continue; }
                            Queue(agent,VansNavigationRequestReason::PathRecovery); motion.desiredSpeed=0;
                        }
                    }
                }
                if(snapshot.avoidance.yield.peer)
                { agent.stuckSeconds=0; agent.hasProgressWindow=false; }
                else if (motion.desiredSpeed > 0.0f && glm::dot(motion.direction,motion.direction)>1.e-8f)
				{
					// 保留 cell 尺度的容差，并允许低速配置在两秒窗口内证明真实推进。
					const float minimumProgress=(std::max)(1.e-4f,(std::min)(profile->cellSize*.25f,motion.desiredSpeed*.5f));
					UpdateProgressWindow(agent,motor.position,steering,minimumProgress);
					if (agent.stuckSeconds >= 2.0f)
					{
						agent.stuckSeconds = 0.0f;
						if (++snapshot.recoveryCount > 3u)
						{
							snapshot.path.diagnostic="No effective Corridor progress after bounded recovery attempts";
							Finish(agent, VansNavigationMoveResult::Stuck); continue;
						}
						Queue(agent, VansNavigationRequestReason::PathRecovery);
						motion.desiredSpeed = 0.0f;
					}
					else agent.hadMoveIntent=true;
				}
				else agent.hasProgressWindow=false;
			}
		}
		snapshot.desiredSpeed = motion.desiredSpeed;
		agent.registration.motor->Submit(motion);
	}
    m_Queries.Dispatch(m_Settings.query.maximumIterationsPerFrame);
}
}
