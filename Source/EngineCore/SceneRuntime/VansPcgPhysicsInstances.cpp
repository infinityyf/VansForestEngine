#include "VansPcgPhysicsInstances.h"
#include "VansRuntimeWorld.h"
#include "VansRuntimeComponentTypes.h"
#include "Transform/VansTransformStore.h"
#include "../PcgCore/VansPcgRuntimePolicy.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../SceneCore/VansSceneRuntimeProjection.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <new>

namespace Vans
{
bool ResolvePcgCollisionInput(const VansPcgCollisionInstance& instance,
    VansEngine::PhysicsNodeProperties& properties,VansTransform& pose,std::string& error)
{
    if(!instance.prototype.mesh.IsValid() || instance.prototype.layer.empty() || instance.prototype.layer.size()>128)
    { error="PCG collision requires an explicit CPU Model and layer"; return false; }
    double length=0;
    for(const auto value:instance.point.rotation) length+=static_cast<double>(value)*value;
    if(!std::isfinite(length) || std::abs(length-1)>0.001)
    { error="PCG collision point quaternion is not normalized"; return false; }
    for(std::size_t axis=0;axis<3;++axis)
        if(!std::isfinite(instance.point.position[axis]) || !std::isfinite(instance.point.scale[axis]) || instance.point.scale[axis]<=0)
        { error="PCG collision point requires finite position and positive scale"; return false; }
    const auto angles=ProjectSceneQuaternionToEulerDegrees(instance.point.rotation,false);
    pose.m_Position={instance.point.position[0],instance.point.position[1],instance.point.position[2]};
    pose.m_Rotation={angles[0],angles[1],angles[2]};
    pose.m_Scale={instance.point.scale[0],instance.point.scale[1],instance.point.scale[2]};
    properties={}; properties.enabled=true;
    properties.colliderType=VansEngine::PhysicsColliderType::Mesh; properties.useMeshCollider=true;
    properties.layerName=instance.prototype.layer;
    error.clear(); return true;
}

struct VansPcgPhysicsInstances::Entry
{
    VansEntityHandle owner;
    VansComponentHandle collider;
    VansComponentHandle transformComponent;
    std::uint32_t transform=UINT32_MAX;
    std::uint64_t transformRevision=0;
    VansEngine::PhysicsNodeProperties properties;
    std::shared_ptr<const VansTriangleMeshData> geometry;
    VansEngine::VansPhysicsNode node;
    ~Entry() { node.Shutdown(); if(transform!=UINT32_MAX) VansTransformStore::Release(transform); }
};
VansPcgPhysicsInstances::VansPcgPhysicsInstances(VansRuntimeWorld& world):m_World(world){}

VansPcgPhysicsInstances::~VansPcgPhysicsInstances()
{
    for(const auto& entry:m_Entries) if(m_World.IsAlive(entry->owner)) m_World.DestroyEntity(entry->owner);
    m_Entries.clear();
}
void VansPcgPhysicsInstances::SynchronizeLifecycle()
{
    m_Entries.erase(std::remove_if(m_Entries.begin(),m_Entries.end(),[&](const auto& entry)
    {
        if(!m_World.IsAlive(entry->owner) || !m_World.GetComponentHeader(entry->collider) || !m_World.GetComponentHeader(entry->transformComponent))
        {
            if(m_World.IsAlive(entry->owner)) m_World.DestroyEntity(entry->owner);
            return true;
        }
        entry->node.SetEnabled(m_World.IsComponentEffectivelyEnabled(entry->collider));
        const auto revision=VansTransformStore::GetTransformRevision(entry->transform);
        if(entry->node.IsEnabled() && revision!=entry->transformRevision)
        { entry->node.SyncActorFromTransform(); entry->transformRevision=revision; }
        return false;
    }),m_Entries.end());
}
bool VansPcgPhysicsInstances::Create(VansRuntimeWorld& world,const VansPcgCollisionPlan& plan,
    const VansPcgCollisionMeshAccess& meshes,std::unique_ptr<VansPcgPhysicsInstances>& output,std::string& error)
try
{
    if(plan.instances.size()>MaximumPcgCollisionInstances)
    { error="PCG Physics instance capacity exceeded"; return false; }
    auto candidate=std::unique_ptr<VansPcgPhysicsInstances>(new VansPcgPhysicsInstances(world));
    candidate->m_Excluded=plan.noCollisionPrototype;
    candidate->m_Entries.reserve(plan.instances.size());
    std::map<VansAssetGuid,std::shared_ptr<const VansTriangleMeshData>> prototypes;
    std::set<std::string> identities;
    std::vector<VansEngine::VansStaticMeshPhysicsInput> actors;
    actors.reserve(plan.instances.size());
    std::uint64_t peakBytes=0;
    for(const auto& instance:plan.instances)
    {
        VansAssetGuid ownerId,componentId;
        if(!identities.insert(instance.ownerGuid).second || !VansAssetGuid::TryParse(instance.ownerGuid,ownerId) || !ownerId.IsValid() ||
            !VansAssetGuid::TryParse(instance.componentGuid,componentId) || !componentId.IsValid() ||
            world.FindComponentByGuid(instance.componentGuid).IsValid() || world.Entities().FindByGuid(instance.ownerGuid).IsValid())
        { error="PCG Physics has an empty or duplicate committed identity"; return false; }
        auto entry=std::make_unique<Entry>(); VansTransform pose;
        if(!ResolvePcgCollisionInput(instance,entry->properties,pose,error)) return false;
        int layer=-1;
        if(!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(entry->properties.layerName,layer))
        { error="PCG Physics references unknown collision layer: "+entry->properties.layerName; return false; }
        auto& prototype=prototypes[instance.prototype.mesh];
        const bool newPrototype=!prototype;
        if(!prototype && !meshes) { error="PCG Physics has no CPU Model resolver"; return false; }
        if(!prototype && !meshes(instance.prototype.mesh,prototype,error)) return false;
        if(!prototype || (newPrototype && !prototype->Validate(MaximumPcgCollisionWorkingBytes/4,error))) return false;
        const auto bytes=prototype->positions.size()*sizeof(float)+prototype->indices.size()*sizeof(std::uint32_t);
        // Conservative per-actor native cooking/residency allowance; checked before actor publication.
        const auto estimate=4096ull+(newPrototype?4*bytes:0);
        if(estimate>MaximumPcgCollisionWorkingBytes-peakBytes)
        { error="PCG Physics actor/cooking working memory budget exceeded"; return false; }
        peakBytes+=estimate; entry->geometry=prototype;
        entry->transform=VansTransformStore::Allocate(); VansTransformStore::Write(entry->transform,pose);
        entry->owner=world.CreateEntity({instance.ownerGuid,"PCG "+instance.ownerGuid,{},false});
        if(!entry->owner.IsValid()) { error="PCG Physics entity publication failed"; return false; }
        candidate->m_Entries.push_back(std::move(entry));
        auto& published=*candidate->m_Entries.back();
        published.collider=world.AddComponent(published.owner,VansRuntimeComponentType_Physics,
            VansRuntimePhysicsComponent{&published.node,VansRuntimePhysicsSourceKind::Pcg},instance.componentGuid);
        published.transformComponent=world.AddComponent(published.owner,VansRuntimeComponentType_Transform,
            VansRuntimeTransformComponent{published.transform},VansAssetGuid::FromStableName("PcgTransform",instance.ownerGuid).ToString());
        if(!published.collider.IsValid() || !published.transformComponent.IsValid()) { error="PCG Physics component publication failed"; return false; }
        published.node.SetName("PCG "+instance.ownerGuid);
        actors.push_back({&published.node,published.properties,published.transform,published.geometry});
    }
    if(!VansEngine::VansPhysicsNode::InitializeStaticMeshBatch(actors,error)) return false;
    for(auto& entry:candidate->m_Entries)
    {
        entry->geometry.reset();
        entry->transformRevision=VansTransformStore::GetTransformRevision(entry->transform);
        if(!world.SetEntityActive(entry->owner,true)) { error="PCG Physics entity activation failed"; return false; }
    }
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="PCG Physics immutable input/actor allocation failed"; return false; }
}
