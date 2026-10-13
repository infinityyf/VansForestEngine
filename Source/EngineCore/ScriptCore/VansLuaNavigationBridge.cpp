#include "VansLuaNavigationBridge.h"
#include "VansScriptContext.h"
#include "../RenderCore/VansScene.h"
#include "../NavigationCore/VansNavigationWorld.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EventCore/VansEventBus.h"
#include <array>
#include <cmath>
#include <limits>
extern "C" {
#include <lauxlib.h>
}
namespace VansRuntime
{
namespace
{
const VansLuaNavigationAccess& Access(lua_State* state)
{
    lua_getfield(state,LUA_REGISTRYINDEX,"Vans.NavigationAccess");
    const auto* access=static_cast<const VansLuaNavigationAccess*>(lua_touserdata(state,-1));
    lua_pop(state,1); return *access;
}
glm::vec3 ReadVec3(lua_State* state,int index)
{
    luaL_checktype(state,index,LUA_TTABLE); index=lua_absindex(state,index);
    glm::vec3 value;
    const char* fields[]={"x","y","z"};
    for(int i=0;i<3;++i)
    { lua_getfield(state,index,fields[i]); value[i]=static_cast<float>(luaL_checknumber(state,-1)); lua_pop(state,1); }
    return value;
}
void PushVec3(lua_State* state,const glm::vec3& value)
{
    lua_createtable(state,0,3); const char* fields[]={"x","y","z"};
    for(int i=0;i<3;++i) { lua_pushnumber(state,value[i]); lua_setfield(state,-2,fields[i]); }
}
void PushTileDependencies(lua_State* state,const std::vector<Vans::VansNavigationTileDependency>& tiles)
{
    lua_createtable(state,static_cast<int>(tiles.size()),0);
    for(std::size_t i=0;i<tiles.size();++i)
    {
        lua_createtable(state,0,3); lua_pushinteger(state,tiles[i].x); lua_setfield(state,-2,"x");
        lua_pushinteger(state,tiles[i].z); lua_setfield(state,-2,"z");
        lua_pushinteger(state,tiles[i].layer); lua_setfield(state,-2,"layer"); lua_rawseti(state,-2,i+1);
    }
    lua_setfield(state,-2,"tiles");
}
void PushPathMetadata(lua_State* state,const Vans::VansNavigationPath& path)
{
    lua_createtable(state,static_cast<int>(path.points.size()),0);
    for(std::size_t i=0;i<path.points.size();++i) { PushVec3(state,path.points[i]); lua_rawseti(state,-2,i+1); }
    lua_setfield(state,-2,"points");
    lua_createtable(state,static_cast<int>(path.cornerFlags.size()),0);
    for(std::size_t i=0;i<path.cornerFlags.size();++i) { lua_pushinteger(state,path.cornerFlags[i]); lua_rawseti(state,-2,i+1); }
    lua_setfield(state,-2,"corner_flags");
    PushTileDependencies(state,path.tiles);
    if(!path.hasRequestCoordinates) return;
    lua_pushstring(state,path.goalAcceptance==Vans::VansNavigationGoalAcceptance::RequestedPoint?"requested":"projected");
    lua_setfield(state,-2,"goal_acceptance");
    PushVec3(state,path.requestedStart); lua_setfield(state,-2,"requested_start");
    PushVec3(state,path.requestedEnd); lua_setfield(state,-2,"requested_end");
    if(path.points.empty()) return;
    for(const auto item:{std::pair<const char*,const glm::vec3*>{"projected_start",&path.projectedStart},
        {"projected_end",&path.projectedEnd},{"reached_end",&path.reachedEnd}})
    { PushVec3(state,*item.second); lua_setfield(state,-2,item.first); }
    lua_pushnumber(state,path.startProjectionDistance); lua_setfield(state,-2,"start_projection_distance");
    lua_pushnumber(state,path.endProjectionDistance); lua_setfield(state,-2,"end_projection_distance");
}
struct LuaNavigationRequest
{
	Vans::VansNavigationRequestHandle request;
};

Vans::VansNavigationWorld* NavigationWorld()
{
	const auto* context = VansScriptContext::GetInstance();
	auto* scene = context ? context->GetScene() : nullptr;
	return scene ? scene->GetNavigationWorld() : nullptr;
}
int NavigationError(lua_State* L, const std::string& error)
{
	lua_pushnil(L);
	lua_pushlstring(L, error.data(), error.size());
	return 2;
}
void PushNavigationRequest(lua_State* L, Vans::VansNavigationRequestHandle request)
{
	auto* value = static_cast<LuaNavigationRequest*>(lua_newuserdatauv(L, sizeof(LuaNavigationRequest), 0));
	new (value) LuaNavigationRequest{ request };
	luaL_getmetatable(L, "Vans.NavigationRequest");
	lua_setmetatable(L, -2);
}
void ReadGoalAcceptance(lua_State* state,int index,Vans::VansNavigationGoalAcceptance& acceptance,float& tolerance)
{
    lua_getfield(state,index,"goal_acceptance");
    if(!lua_isnil(state,-1))
    {
        const std::string policy=luaL_checkstring(state,-1);
        if(policy=="requested") acceptance=Vans::VansNavigationGoalAcceptance::RequestedPoint;
        else if(policy=="projected") acceptance=Vans::VansNavigationGoalAcceptance::ProjectedPoint;
        else luaL_error(state,"goal_acceptance must be requested or projected");
    }
    lua_pop(state,1); lua_getfield(state,index,"goal_projection_tolerance");
    if(!lua_isnil(state,-1)) tolerance=static_cast<float>(luaL_checknumber(state,-1)); lua_pop(state,1);
}
int LuaNavigationRequestMove(lua_State* L)
{
	const auto entity = Access(L).readEntity(L, 1);
	const auto goal = ReadVec3(L, 2);
	Vans::VansNavigationMoveOptions options;
	if (!lua_isnoneornil(L, 3))
	{
		luaL_checktype(L, 3, LUA_TTABLE);
		ReadGoalAcceptance(L,3,options.goalAcceptance,options.goalProjectionToleranceMeters);
		lua_getfield(L, 3, "arrival_radius");
		if (!lua_isnil(L, -1)) options.arrivalRadius = static_cast<float>(luaL_checknumber(L, -1));
		lua_pop(L, 1);
		lua_getfield(L, 3, "height_tolerance");
		if (!lua_isnil(L, -1)) options.heightTolerance = static_cast<float>(luaL_checknumber(L, -1));
		lua_pop(L, 1);
		lua_getfield(L, 3, "allow_partial");
		if (!lua_isnil(L, -1)) { luaL_checktype(L, -1, LUA_TBOOLEAN); options.allowPartial = lua_toboolean(L, -1) != 0; }
		lua_pop(L, 1);
		lua_getfield(L, 3, "facing_yaw");
		if (!lua_isnil(L, -1)) options.facingYaw = static_cast<float>(luaL_checknumber(L, -1));
		lua_pop(L, 1);
	}
	auto* navigation = NavigationWorld();
	if (!navigation) return NavigationError(L, "Scene navigation is not initialized");
	std::string error;
	const auto request = navigation->RequestMove(entity, Access(L).readOwner(L), goal, options, error);
	if (!request.IsValid()) return NavigationError(L, error);
	PushNavigationRequest(L, request);
	return 1;
}
int LuaNavigationUpdateGoal(lua_State* L)
{
	const auto request = static_cast<LuaNavigationRequest*>(luaL_checkudata(L, 1, "Vans.NavigationRequest"))->request;
	const auto goal = ReadVec3(L, 2);
	std::optional<float> facing;
	if (!lua_isnoneornil(L, 3)) facing = static_cast<float>(luaL_checknumber(L, 3));
	auto* navigation = NavigationWorld();
	if (!navigation) return NavigationError(L, "Scene navigation is not initialized");
	std::string error;
	if (!navigation->UpdateGoal(request, Access(L).readOwner(L), goal, facing, error)) return NavigationError(L, error);
	lua_pushboolean(L, 1);
	return 1;
}
template<bool (Vans::VansNavigationWorld::*Control)(Vans::VansNavigationRequestHandle, const std::string&, std::string&)>
int LuaNavigationControl(lua_State* L)
{
	const auto request = static_cast<LuaNavigationRequest*>(luaL_checkudata(L, 1, "Vans.NavigationRequest"))->request;
	auto* navigation = NavigationWorld();
	if (!navigation) return NavigationError(L, "Scene navigation is not initialized");
	std::string error;
	if (!(navigation->*Control)(request, Access(L).readOwner(L), error)) return NavigationError(L, error);
	lua_pushboolean(L, 1);
	return 1;
}
int LuaNavigationGetState(lua_State* L)
{
	const auto request = static_cast<LuaNavigationRequest*>(luaL_checkudata(L, 1, "Vans.NavigationRequest"))->request;
	auto* navigation = NavigationWorld();
	const auto snapshot = navigation ? navigation->CaptureAgent(request.agent) : std::nullopt;
	if (!snapshot || snapshot->request.session != request.session || snapshot->request.serial != request.serial)
		return NavigationError(L, "Navigation request handle is stale");
    if(snapshot->owner!=Access(L).readOwner(L)) return NavigationError(L,"OwnerConflict");
	const char* state = "idle";
	switch (snapshot->state)
	{
	case Vans::VansNavigationMoveState::Idle: break;
	case Vans::VansNavigationMoveState::Queued: state = "queued"; break;
	case Vans::VansNavigationMoveState::Following: state = "following"; break;
	case Vans::VansNavigationMoveState::Paused: state = "paused"; break;
	case Vans::VansNavigationMoveState::Finished: state = "finished"; break;
    case Vans::VansNavigationMoveState::WaitingForData: state = "waiting_for_data"; break;
    case Vans::VansNavigationMoveState::TraversingLink: state = "traversing_link"; break;
	}
	const char* result = "none";
	switch (snapshot->result)
	{
	case Vans::VansNavigationMoveResult::None: break;
	case Vans::VansNavigationMoveResult::ReachedGoal: result = "reached_goal"; break;
	case Vans::VansNavigationMoveResult::ReachedPartialEnd: result = "reached_partial_end"; break;
	case Vans::VansNavigationMoveResult::Cancelled: result = "cancelled"; break;
	case Vans::VansNavigationMoveResult::Failed: result = "failed"; break;
	case Vans::VansNavigationMoveResult::Stuck: result = "stuck"; break;
	}
	lua_newtable(L);
	lua_pushstring(L, state); lua_setfield(L, -2, "state");
	lua_pushstring(L, result); lua_setfield(L, -2, "result");
	lua_pushnumber(L, snapshot->desiredSpeed); lua_setfield(L, -2, "desired_speed");
	lua_pushboolean(L, snapshot->movementBlocked); lua_setfield(L, -2, "movement_blocked");
	lua_pushlstring(L, snapshot->path.diagnostic.data(), snapshot->path.diagnostic.size()); lua_setfield(L, -2, "diagnostic");
	lua_pushinteger(L, snapshot->recoveryCount); lua_setfield(L, -2, "recovery_count");
	lua_pushboolean(L,snapshot->avoidance.limited); lua_setfield(L,-2,"avoidance_limited");
	lua_pushinteger(L,snapshot->avoidance.neighbors); lua_setfield(L,-2,"avoidance_neighbors");
    lua_pushinteger(L,snapshot->avoidance.obstacles); lua_setfield(L,-2,"avoidance_obstacles");
	lua_pushinteger(L,snapshot->avoidance.scans); lua_setfield(L,-2,"avoidance_scans");
	lua_pushinteger(L,snapshot->avoidance.samples); lua_setfield(L,-2,"avoidance_samples");
    lua_pushboolean(L,snapshot->avoidance.yield.peer!=0); lua_setfield(L,-2,"avoidance_yielding");
    lua_pushnumber(L,snapshot->avoidance.yield.seconds); lua_setfield(L,-2,"avoidance_yield_seconds");
	PushPathMetadata(L,snapshot->path);
	return 1;
}

struct LuaNavigationQuery
{
    Vans::VansNavigationQueryHandle handle;
    std::string owner;
};
void PushQuery(lua_State* state,Vans::VansNavigationQueryHandle handle)
{
    auto* value=static_cast<LuaNavigationQuery*>(lua_newuserdatauv(state,sizeof(LuaNavigationQuery),0));
    new(value) LuaNavigationQuery{handle,Access(state).readOwner(state)};
    luaL_getmetatable(state,"Vans.NavigationQuery"); lua_setmetatable(state,-2);
}
int QueryGC(lua_State* state)
{
    auto* value=static_cast<LuaNavigationQuery*>(luaL_checkudata(state,1,"Vans.NavigationQuery"));
    if(auto* world=NavigationWorld()) { std::string error; world->ReleaseQuery(value->handle,value->owner,error); }
    value->~LuaNavigationQuery(); return 0;
}
void ReadQueryOptions(lua_State* state,int index,Vans::VansNavigationQueryRequest& request)
{
    if(!lua_isnoneornil(state,index))
    {
        luaL_checktype(state,index,LUA_TTABLE);
        ReadGoalAcceptance(state,index,request.goalAcceptance,request.goalProjectionToleranceMeters);
        for(auto entry:{std::pair<const char*,std::string*>{"map_id",&request.mapId},{"profile_id",&request.profileId},{"query_filter",&request.filterId}})
        { lua_getfield(state,index,entry.first); if(!lua_isnil(state,-1)) *entry.second=luaL_checkstring(state,-1); lua_pop(state,1); }
        lua_getfield(state,index,"nearest_extents"); if(!lua_isnil(state,-1)) request.nearestExtents=ReadVec3(state,-1); lua_pop(state,1);
    }
}
int SubmitQuery(lua_State* state,const Vans::VansNavigationQueryRequest& request)
{
    auto* world=NavigationWorld(); if(!world) return NavigationError(state,"Scene navigation is not initialized");
    std::string error; const auto handle=world->RequestQuery(request,Access(state).readOwner(state),error);
    if(!handle.IsValid()) return NavigationError(state,error); PushQuery(state,handle); return 1;
}
int RequestPath(lua_State* state)
{
    Vans::VansNavigationQueryRequest request; request.start=ReadVec3(state,1); request.goal=ReadVec3(state,2);
    ReadQueryOptions(state,3,request); return SubmitQuery(state,request);
}
int ProjectPoint(lua_State* state)
{
    Vans::VansNavigationQueryRequest request; request.kind=Vans::VansNavigationQueryKind::PointProjection;
    request.start=ReadVec3(state,1); ReadQueryOptions(state,2,request); return SubmitQuery(state,request);
}
int NavRaycast(lua_State* state)
{
 Vans::VansNavigationQueryRequest request; request.kind=Vans::VansNavigationQueryKind::Raycast;
 request.start=ReadVec3(state,1); request.goal=ReadVec3(state,2);
 ReadQueryOptions(state,3,request); return SubmitQuery(state,request);
}
int NearestBoundary(lua_State* state)
{
 Vans::VansNavigationQueryRequest request; request.kind=Vans::VansNavigationQueryKind::NearestBoundary;
 request.start=ReadVec3(state,1); request.radius=static_cast<float>(luaL_checknumber(state,2));
 ReadQueryOptions(state,3,request); return SubmitQuery(state,request);
}
int SampleReachablePoint(lua_State* state)
{
    const auto entity=Access(state).readEntity(state,1); const auto anchor=ReadVec3(state,2);
    const float radius=static_cast<float>(luaL_checknumber(state,3)); const auto seed=luaL_optinteger(state,4,0);
    if(seed<0 || static_cast<std::uint64_t>(seed)>(std::numeric_limits<std::uint32_t>::max)())
        return NavigationError(state,"Sampling seed must be a uint32");
    auto* world=NavigationWorld(); if(!world) return NavigationError(state,"Scene navigation is not initialized");
    std::string error; const auto handle=world->SampleReachablePoint(entity,Access(state).readOwner(state),anchor,radius,static_cast<std::uint32_t>(seed),error);
    if(!handle.IsValid()) return NavigationError(state,error); PushQuery(state,handle); return 1;
}
int QueryState(lua_State* state)
{
    const auto* value=static_cast<LuaNavigationQuery*>(luaL_checkudata(state,1,"Vans.NavigationQuery"));
    auto* world=NavigationWorld(); if(!world) return NavigationError(state,"Scene navigation is not initialized");
    std::string error; const auto result=world->CaptureQuery(value->handle,Access(state).readOwner(state),error);
    if(!result) return NavigationError(state,error);
    const char* lifecycle="queued";
    switch(result->lifecycle)
    { case Vans::VansNavigationQueryLifecycle::Queued:break; case Vans::VansNavigationQueryLifecycle::Running:lifecycle="running";break;
      case Vans::VansNavigationQueryLifecycle::Finished:lifecycle="finished";break; case Vans::VansNavigationQueryLifecycle::Cancelled:lifecycle="cancelled";break;
      case Vans::VansNavigationQueryLifecycle::WaitingForData:lifecycle="waiting_for_data";break; }
    const char* topology="none";
    switch(result->path.status)
    { case Vans::VansNavigationPathStatus::None:break; case Vans::VansNavigationPathStatus::Complete:topology="complete";break;
      case Vans::VansNavigationPathStatus::Partial:topology="partial";break; case Vans::VansNavigationPathStatus::Failed:topology="failed";break; }
    lua_newtable(state); lua_pushstring(state,lifecycle); lua_setfield(state,-2,"state");
    const auto kind=result->kind==Vans::VansNavigationQueryKind::Path?"path":
        result->kind==Vans::VansNavigationQueryKind::ReachablePoint?"reachable_point":
        result->kind==Vans::VansNavigationQueryKind::PointProjection?"project_point":
        result->kind==Vans::VansNavigationQueryKind::Raycast?"raycast":"nearest_boundary";
    lua_pushstring(state,kind); lua_setfield(state,-2,"kind");
    lua_pushboolean(state,result->lifecycle==Vans::VansNavigationQueryLifecycle::Finished &&
        (result->point.has_value() || (result->raycast && result->raycast->valid) ||
         (result->boundary && result->boundary->valid) || result->path.status==Vans::VansNavigationPathStatus::Complete || result->path.status==Vans::VansNavigationPathStatus::Partial));
    lua_setfield(state,-2,"success");
    lua_pushstring(state,topology); lua_setfield(state,-2,"topology");
    lua_pushstring(state,Vans::NavigationPathFailureName(result->path.failure)); lua_setfield(state,-2,"failure");
    lua_pushlstring(state,result->path.diagnostic.data(),result->path.diagnostic.size()); lua_setfield(state,-2,"diagnostic");
    lua_pushinteger(state,result->attempts); lua_setfield(state,-2,"attempts");
    lua_pushinteger(state,result->iterations); lua_setfield(state,-2,"iterations");
    lua_pushlstring(state,result->mapId.data(),result->mapId.size()); lua_setfield(state,-2,"map_id");
    lua_pushlstring(state,result->profileId.data(),result->profileId.size()); lua_setfield(state,-2,"profile_id");
    lua_pushlstring(state,result->filterId.data(),result->filterId.size()); lua_setfield(state,-2,"query_filter");
    if(result->point) { PushVec3(state,*result->point); lua_setfield(state,-2,"point"); }
    if(result->kind==Vans::VansNavigationQueryKind::PointProjection)
    { lua_pushnumber(state,result->projectionDistance); lua_setfield(state,-2,"projection_distance"); }
    PushPathMetadata(state,result->path);
    if(result->raycast && result->raycast->valid)
    {
        const auto& ray=*result->raycast;
        lua_pushboolean(state,ray.hit); lua_setfield(state,-2,"hit");
        lua_pushnumber(state,ray.fraction); lua_setfield(state,-2,"fraction");
        lua_pushnumber(state,ray.projectionDistance); lua_setfield(state,-2,"projection_distance");
        for(const auto item:{std::pair<const char*,const glm::vec3*>{"requested_start",&ray.requestedStart},
            {"requested_end",&ray.requestedEnd},{"projected_start",&ray.projectedStart},{"point",&ray.position},{"normal",&ray.normal}})
        { PushVec3(state,*item.second); lua_setfield(state,-2,item.first); }
        PushTileDependencies(state,ray.tiles);
    }
    if(result->boundary && result->boundary->valid)
    {
        const auto& boundary=*result->boundary;
        lua_pushboolean(state,boundary.found); lua_setfield(state,-2,"found");
        lua_pushnumber(state,boundary.distance); lua_setfield(state,-2,"distance");
        lua_pushnumber(state,boundary.projectionDistance); lua_setfield(state,-2,"projection_distance");
        PushVec3(state,boundary.requested); lua_setfield(state,-2,"requested_point");
        PushVec3(state,boundary.projected); lua_setfield(state,-2,"projected_point");
        if(boundary.found)
        { PushVec3(state,boundary.position); lua_setfield(state,-2,"point");
          PushVec3(state,boundary.normal); lua_setfield(state,-2,"normal"); }
    }
    return 1;
}
template<bool (Vans::VansNavigationWorld::*Control)(Vans::VansNavigationQueryHandle,const std::string&,std::string&)>
int QueryControl(lua_State* state)
{
    const auto* value=static_cast<LuaNavigationQuery*>(luaL_checkudata(state,1,"Vans.NavigationQuery"));
    auto* world=NavigationWorld(); if(!world) return NavigationError(state,"Scene navigation is not initialized");
    std::string error; if(!(world->*Control)(value->handle,Access(state).readOwner(state),error)) return NavigationError(state,error);
    lua_pushboolean(state,1); return 1;
}
constexpr char SubscriptionRegistry[]="Vans.NavigationSubscriptions";
struct NavigationEventMailbox
{
    static constexpr std::size_t Capacity=8;
    std::array<Vans::VansNavigationLifecycleEvent,Capacity> events;
    Vans::VansNavigationSubscriptionHandle subscription;
    std::size_t head=0,count=0;
    bool overflow=false;
    NavigationEventMailbox()
    { for(auto& event:events) { event.traversal.diagnostic.reserve(256); event.requestOwner.reserve(256); } }
    void Push(const Vans::VansNavigationLifecycleEvent& event)
    {
        if(event.subscription.session!=subscription.session || event.subscription.serial!=subscription.serial ||
            !(event.subscription.agent==subscription.agent)) return;
        if(overflow) return;
        if(count==Capacity || event.traversal.diagnostic.size()>256 || event.requestOwner.size()>256) { overflow=true; return; }
        events[(head+count)%Capacity]=event; ++count;
    }
    Vans::VansNavigationLifecycleEvent Pop()
    {
        auto event=events[head]; events[head].traversal.diagnostic.clear();
        head=(head+1)%Capacity; --count; return event;
    }
};
struct LuaNavigationSubscription
{
    Vans::VansNavigationSubscriptionHandle handle;
    std::string owner;
    int callback=LUA_NOREF;
    std::shared_ptr<NavigationEventMailbox> mailbox;
    Vans::VansEventConnection connection;
};
const char* TraversalPhaseName(Vans::VansNavigationTraversalPhase phase)
{
    switch(phase)
    {
    case Vans::VansNavigationTraversalPhase::Idle:return "idle";
    case Vans::VansNavigationTraversalPhase::Approaching:return "approaching";
    case Vans::VansNavigationTraversalPhase::TraversalRequested:return "traversal_requested";
    case Vans::VansNavigationTraversalPhase::Traversing:return "traversing";
    case Vans::VansNavigationTraversalPhase::Completed:return "completed";
    case Vans::VansNavigationTraversalPhase::Aborted:return "aborted";
    }
    return "idle";
}
void PushTraversal(lua_State* state,const Vans::VansNavigationTraversalSnapshot& value)
{
    lua_newtable(state); lua_pushliteral(state,"traversal"); lua_setfield(state,-2,"kind");
    lua_pushstring(state,TraversalPhaseName(value.phase)); lua_setfield(state,-2,"phase");
    auto* handle=static_cast<Vans::VansNavigationTraversalHandle*>(lua_newuserdatauv(state,sizeof(Vans::VansNavigationTraversalHandle),0));
    *handle=value.handle; luaL_getmetatable(state,"Vans.NavigationTraversal"); lua_setmetatable(state,-2);
    lua_setfield(state,-2,"traversal");
    lua_pushinteger(state,value.link.key.value); lua_setfield(state,-2,"link_identity");
    PushVec3(state,value.link.entry); lua_setfield(state,-2,"entry");
    PushVec3(state,value.link.exit); lua_setfield(state,-2,"exit");
    lua_pushnumber(state,value.link.radiusMeters); lua_setfield(state,-2,"radius_meters");
    lua_pushnumber(state,value.elapsedSeconds); lua_setfield(state,-2,"elapsed_seconds");
    lua_pushlstring(state,value.diagnostic.data(),value.diagnostic.size()); lua_setfield(state,-2,"diagnostic");
}
// Lua 快照强持有 userdata；回调可以取消/替换订阅而不破坏遍历或对象生命周期。
int SubscriptionSnapshot(lua_State* state,const std::string* owner=nullptr)
{
    lua_getfield(state,LUA_REGISTRYINDEX,SubscriptionRegistry); const int source=lua_gettop(state);
    lua_newtable(state); const int snapshot=lua_gettop(state); int count=0;
    const auto append=[&](int records)
    {
        records=lua_absindex(state,records);
        lua_pushnil(state);
        while(lua_next(state,records))
        { lua_pushvalue(state,-1); lua_rawseti(state,snapshot,++count); lua_pop(state,1); }
    };
    if(lua_istable(state,source))
    {
        if(owner)
        {
            lua_pushlstring(state,owner->data(),owner->size()); lua_rawget(state,source);
            if(lua_istable(state,-1)) append(-1);
            lua_pop(state,1);
        }
        else
        {
            lua_pushnil(state);
            while(lua_next(state,source))
            { if(lua_istable(state,-1)) append(-1); lua_pop(state,1); }
        }
    }
    lua_remove(state,source); return lua_gettop(state);
}
void RetireSubscription(lua_State* state,LuaNavigationSubscription& value)
{
    value.connection.Disconnect(); value.mailbox.reset();
    if(value.handle.IsValid())
    {
        if(auto* world=NavigationWorld()) { std::string error; world->Unsubscribe(value.handle,value.owner,error); }
        lua_getfield(state,LUA_REGISTRYINDEX,SubscriptionRegistry);
        if(lua_istable(state,-1))
        {
            const int root=lua_gettop(state); lua_pushlstring(state,value.owner.data(),value.owner.size()); lua_rawget(state,root);
            if(lua_istable(state,-1))
            {
                lua_pushnil(state); lua_rawseti(state,-2,static_cast<lua_Integer>(value.handle.serial));
                lua_pushnil(state); const bool empty=lua_next(state,-2)==0;
                if(!empty) lua_pop(state,2);
                if(empty) { lua_pushlstring(state,value.owner.data(),value.owner.size()); lua_pushnil(state); lua_rawset(state,root); }
            }
            lua_pop(state,1);
        }
        lua_pop(state,1); value.handle={};
    }
    if(value.callback!=LUA_NOREF) { luaL_unref(state,LUA_REGISTRYINDEX,value.callback); value.callback=LUA_NOREF; }
}
int SubscriptionGC(lua_State* state)
{
    auto* value=static_cast<LuaNavigationSubscription*>(luaL_checkudata(state,1,"Vans.NavigationSubscription"));
    RetireSubscription(state,*value); value->~LuaNavigationSubscription(); return 0;
}
void CancelSubscriptions(lua_State* state,const std::string& owner)
{
    const int top=lua_gettop(state),snapshot=SubscriptionSnapshot(state,&owner);
    const auto count=lua_rawlen(state,snapshot);
    for(std::size_t i=1;i<=count;++i)
    {
        lua_rawgeti(state,snapshot,i);
        auto* value=static_cast<LuaNavigationSubscription*>(luaL_testudata(state,-1,"Vans.NavigationSubscription"));
        if(value && value->owner==owner) RetireSubscription(state,*value);
        lua_pop(state,1);
    }
    lua_settop(state,top);
}
int Subscribe(lua_State* state)
{
    luaL_checktype(state,2,LUA_TFUNCTION);
    if(!lua_isnoneornil(state,3)) luaL_checktype(state,3,LUA_TTABLE);
    auto* world=NavigationWorld(); if(!world) return NavigationError(state,"Scene navigation is not initialized");
    const auto entity=Access(state).readEntity(state,1);
    float maximumSeconds=8; std::vector<Vans::VansNavigationLinkKey> links;
    if(!lua_isnoneornil(state,3))
    {
        lua_getfield(state,3,"maximum_traversal_seconds");
        if(!lua_isnil(state,-1)) maximumSeconds=static_cast<float>(luaL_checknumber(state,-1)); lua_pop(state,1);
        lua_getfield(state,3,"links");
        if(!lua_isnil(state,-1))
        {
            luaL_checktype(state,-1,LUA_TTABLE); const int list=lua_gettop(state); const auto count=lua_rawlen(state,list);
            if(count>Vans::kMaximumNavigationLinkCapabilities) { lua_pop(state,1); return NavigationError(state,"Navigation subscription Link capacity exceeded"); }
            auto* context=VansScriptContext::GetInstance(); auto* scene=context?context->GetScene():nullptr;
            auto* runtime=scene?scene->GetRuntimeWorld():nullptr;
            const auto* storage=runtime?runtime->FindStorage<Vans::VansRuntimeNavigationLinkComponent>(Vans::VansRuntimeComponentType_NavigationLink):nullptr;
            links.reserve(count);
            for(std::size_t i=1;i<=count;++i)
            {
                lua_rawgeti(state,list,i); const auto linkEntity=Access(state).readEntity(state,-1); lua_pop(state,1);
                const auto component=storage?storage->FindFirstOwnedBy(linkEntity):Vans::VansComponentHandle{};
                const auto* header=storage?storage->GetHeader(component):nullptr;
                if(!runtime || !runtime->IsAlive(linkEntity) || !header || !header->effectiveEnabled)
                { lua_pop(state,1); return NavigationError(state,"Navigation subscription requires an enabled NavigationLink entity"); }
                const auto key=Vans::DeriveNavigationLinkKey(header->stableGuid);
                if(!key.value) { lua_pop(state,1); return NavigationError(state,"Navigation Link stable identity is invalid"); }
                links.push_back(key);
            }
        }
        lua_pop(state,1);
    }
    const auto owner=Access(state).readOwner(state);
    // 有待消费的终态必须保留到其 owner 更新；总订阅记录仍受同一 Agent 容量约束。
    const int snapshot=SubscriptionSnapshot(state); const auto count=lua_rawlen(state,snapshot);
    std::size_t retained=0;
    for(std::size_t i=1;i<=count;++i)
    {
        lua_rawgeti(state,snapshot,i);
        auto* value=static_cast<LuaNavigationSubscription*>(luaL_testudata(state,-1,"Vans.NavigationSubscription"));
        if(value && !world->HasSubscription(value->handle,value->owner) &&
            (!value->mailbox || (!value->mailbox->count && !value->mailbox->overflow))) RetireSubscription(state,*value);
        if(value && value->handle.IsValid()) ++retained;
        lua_pop(state,1);
    }
    lua_pop(state,1);
    if(retained>=static_cast<std::size_t>(world->GetSubscriptionCapacity()))
        return NavigationError(state,"Navigation subscription records exceed the bounded Agent capacity");
    auto* value=static_cast<LuaNavigationSubscription*>(lua_newuserdatauv(state,sizeof(LuaNavigationSubscription),0));
    new(value) LuaNavigationSubscription{}; value->owner=owner;
    luaL_getmetatable(state,"Vans.NavigationSubscription"); lua_setmetatable(state,-2);
    lua_pushvalue(state,2); value->callback=luaL_ref(state,LUA_REGISTRYINDEX);
    std::string error; value->handle=world->Subscribe(entity,owner,links,maximumSeconds,error);
    if(!value->handle.IsValid()) { RetireSubscription(state,*value); lua_pop(state,1); return NavigationError(state,error); }
    try
    {
        value->mailbox=std::make_shared<NavigationEventMailbox>(); value->mailbox->subscription=value->handle;
        value->connection=Vans::VansEventBus::Get().Subscribe<Vans::VansNavigationLifecycleEvent>(
            [mailbox=std::weak_ptr<NavigationEventMailbox>(value->mailbox)](const auto& event)
            { if(const auto target=mailbox.lock()) target->Push(event); },Vans::VansEventLane::MainThread);
    }
    catch(const std::bad_alloc&)
    { RetireSubscription(state,*value); lua_pop(state,1); return NavigationError(state,"Navigation event subscription allocation failed"); }
    const int userdata=lua_gettop(state);
    lua_getfield(state,LUA_REGISTRYINDEX,SubscriptionRegistry); const int root=lua_gettop(state);
    lua_pushlstring(state,owner.data(),owner.size()); lua_rawget(state,root);
    if(!lua_istable(state,-1))
    {
        lua_pop(state,1); lua_newtable(state); lua_pushlstring(state,owner.data(),owner.size());
        lua_pushvalue(state,-2); lua_rawset(state,root);
    }
    lua_pushvalue(state,userdata); lua_rawseti(state,-2,static_cast<lua_Integer>(value->handle.serial)); lua_pop(state,2);
    return 1;
}
int Unsubscribe(lua_State* state)
{
    auto* value=static_cast<LuaNavigationSubscription*>(luaL_checkudata(state,1,"Vans.NavigationSubscription"));
    if(value->owner!=Access(state).readOwner(state)) return NavigationError(state,"OwnerConflict");
    RetireSubscription(state,*value); lua_pushboolean(state,1); return 1;
}
template<bool(Vans::VansNavigationWorld::*Control)(Vans::VansNavigationTraversalHandle,const std::string&,std::string&)>
int TraversalControl(lua_State* state)
{
    const auto* handle=static_cast<Vans::VansNavigationTraversalHandle*>(luaL_checkudata(state,1,"Vans.NavigationTraversal"));
    auto* world=NavigationWorld(); if(!world) return NavigationError(state,"Scene navigation is not initialized");
    std::string error; if(!(world->*Control)(*handle,Access(state).readOwner(state),error)) return NavigationError(state,error);
    lua_pushboolean(state,1); return 1;
}
bool InvokeSubscription(lua_State* state,LuaNavigationSubscription& subscription,std::string& error)
{
    if(lua_pcall(state,1,0,0)==LUA_OK) return true;
    size_t length=0; const char* message=lua_tolstring(state,-1,&length);
    error=message?std::string(message,length):"Navigation event callback failed";
    lua_pop(state,1); RetireSubscription(state,subscription); return false;
}
bool DispatchSubscriptions(lua_State* state,const std::string& owner,std::string& error)
{
    const int top=lua_gettop(state);
    lua_getfield(state,LUA_REGISTRYINDEX,SubscriptionRegistry);
    if(lua_istable(state,-1)) { lua_pushlstring(state,owner.data(),owner.size()); lua_rawget(state,-2); }
    const bool subscribed=lua_istable(state,-1) && lua_gettop(state)>top+1;
    lua_settop(state,top);
    if(!subscribed) { error.clear(); return true; }
    const int list=SubscriptionSnapshot(state,&owner); const auto count=lua_rawlen(state,list);
    for(std::size_t i=1;i<=count;++i)
    {
        lua_rawgeti(state,list,i);
        auto* value=static_cast<LuaNavigationSubscription*>(luaL_testudata(state,-1,"Vans.NavigationSubscription"));
        if(!value || value->owner!=owner || value->callback==LUA_NOREF) { lua_pop(state,1); continue; }
        if(value->mailbox && value->mailbox->overflow)
        {
            error="Navigation event mailbox exceeded its bounded capacity";
            RetireSubscription(state,*value); lua_settop(state,top); return false;
        }
        const auto pending=value->mailbox?value->mailbox->count:0;
        for(std::size_t eventIndex=0;eventIndex<pending && value->callback!=LUA_NOREF && value->mailbox;++eventIndex)
        {
            const auto event=value->mailbox->Pop();
            lua_rawgeti(state,LUA_REGISTRYINDEX,value->callback);
            if(event.kind==Vans::VansNavigationLifecycleKind::Traversal) PushTraversal(state,event.traversal);
            else
            {
                lua_newtable(state); lua_pushliteral(state,"completion"); lua_setfield(state,-2,"kind");
                const char* result="failed";
                switch(event.result)
                {
                case Vans::VansNavigationMoveResult::ReachedGoal:result="reached_goal";break;
                case Vans::VansNavigationMoveResult::ReachedPartialEnd:result="reached_partial_end";break;
                case Vans::VansNavigationMoveResult::Cancelled:result="cancelled";break;
                case Vans::VansNavigationMoveResult::Stuck:result="stuck";break;
                default:break;
                }
                lua_pushstring(state,result); lua_setfield(state,-2,"result");
            }
            PushNavigationRequest(state,event.request); lua_setfield(state,-2,"request");
            lua_pushlstring(state,event.requestOwner.data(),event.requestOwner.size()); lua_setfield(state,-2,"request_owner");
            if(!InvokeSubscription(state,*value,error)) { lua_settop(state,top); return false; }
        }
        // A callback can change Scene ownership; do not retain a World pointer across Lua calls.
        auto* world=NavigationWorld();
        if(value->callback!=LUA_NOREF && (!world || !world->HasSubscription(value->handle,owner))) RetireSubscription(state,*value);
        lua_pop(state,1);
    }
    lua_settop(state,top); error.clear(); return true;
}
}
void VansLuaNavigationBridge::CancelOwner(const std::string& owner)
{
    if(auto* world=NavigationWorld()) world->CancelOwner(owner);
    auto* context=VansScriptContext::GetInstance();
    auto* state=context?context->GetLuaState():nullptr;
    if(state) CancelSubscriptions(state,owner);
}
bool VansLuaNavigationBridge::DispatchOwner(lua_State* state,const std::string& owner,std::string& error)
{ return DispatchSubscriptions(state,owner,error); }
void VansLuaNavigationBridge::Register(lua_State* state,const VansLuaNavigationAccess& access)
{
    auto* stored=static_cast<VansLuaNavigationAccess*>(lua_newuserdatauv(state,sizeof(VansLuaNavigationAccess),0));
    *stored=access; lua_setfield(state,LUA_REGISTRYINDEX,"Vans.NavigationAccess");
    luaL_newmetatable(state,"Vans.NavigationRequest"); lua_pushliteral(state,"Navigation request handle");
    lua_setfield(state,-2,"__metatable"); lua_pop(state,1);
    luaL_newmetatable(state,"Vans.NavigationQuery"); lua_pushcfunction(state,QueryGC); lua_setfield(state,-2,"__gc");
    lua_pushliteral(state,"Navigation query handle"); lua_setfield(state,-2,"__metatable"); lua_pop(state,1);
    luaL_newmetatable(state,"Vans.NavigationTraversal"); lua_pushliteral(state,"Navigation traversal handle");
    lua_setfield(state,-2,"__metatable"); lua_pop(state,1);
    luaL_newmetatable(state,"Vans.NavigationSubscription"); lua_pushcfunction(state,SubscriptionGC); lua_setfield(state,-2,"__gc");
    lua_pushliteral(state,"Navigation subscription handle"); lua_setfield(state,-2,"__metatable"); lua_pop(state,1);
    lua_getfield(state,LUA_REGISTRYINDEX,SubscriptionRegistry);
    if(!lua_istable(state,-1)) { lua_newtable(state); lua_setfield(state,LUA_REGISTRYINDEX,SubscriptionRegistry); }
    lua_pop(state,1);
    const luaL_Reg methods[]={
        {"request_move",LuaNavigationRequestMove},{"update_goal",LuaNavigationUpdateGoal},
        {"stop",LuaNavigationControl<&Vans::VansNavigationWorld::Stop>},
        {"pause",LuaNavigationControl<&Vans::VansNavigationWorld::Pause>},
        {"resume",LuaNavigationControl<&Vans::VansNavigationWorld::Resume>},{"get_state",LuaNavigationGetState},
        {"subscribe",Subscribe},{"unsubscribe",Unsubscribe},
        {"begin_traversal",TraversalControl<&Vans::VansNavigationWorld::BeginTraversal>},
        {"complete_traversal",TraversalControl<&Vans::VansNavigationWorld::CompleteTraversal>},
        {"abort_traversal",TraversalControl<&Vans::VansNavigationWorld::AbortTraversal>},
        {"request_path",RequestPath},{"project_point",ProjectPoint},{"nav_raycast",NavRaycast},{"nearest_boundary",NearestBoundary},{"sample_reachable_point",SampleReachablePoint},{"get_query_state",QueryState},
        {"cancel_query",QueryControl<&Vans::VansNavigationWorld::CancelQuery>},
        {"release_query",QueryControl<&Vans::VansNavigationWorld::ReleaseQuery>},{nullptr,nullptr}};
    lua_newtable(state); luaL_setfuncs(state,methods,0); lua_setfield(state,-2,"navigation");
}
}
