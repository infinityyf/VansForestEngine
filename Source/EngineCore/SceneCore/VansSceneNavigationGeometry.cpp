#include "VansSceneNavigationGeometry.h"

#include "VansSceneObjectBuildPlan.h"
#include "VansSceneContentBuildPlan.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../PcgCore/VansPcgCollisionPlan.h"
#include "../PcgCore/VansPcgTerrainSurface.h"
#include "../PcgCore/VansPcgSplineField.h"
#include "../SceneRuntime/VansPcgPhysicsInstances.h"
#include "../PhysicsCore/VansPhysicsCollisionGeometry.h"
#include "../TerrainCore/VansTerrainHeightEncoding.h"
#include "../SceneRuntime/Transform/VansTransform.h"
#include "../SceneRuntime/Transform/VansTransformGraph.h"
#include "../SceneRuntime/VansEntityRegistry.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
	return glm::vec3(value[0], value[1], value[2]);
}

std::string ObjectLabel(const VansSceneObjectBuildConfig& object)
{
	return object.name.empty() ? object.entityGuid : object.name;
}

// 离线只组合计划身份和临时存储；世界变换、循环和活动层级仍由运行时 owner 求解。
class VansSceneNavigationTransformContext final
{
public:
    explicit VansSceneNavigationTransformContext(const VansSceneObjectBuildPlan& plan)
        : m_Plan(plan), m_TransformIds(plan.objects.size(),UINT32_MAX)
    { m_Entities.reserve(plan.objects.size()); }

    ~VansSceneNavigationTransformContext()
    {
        m_Graph.Clear();
        for(const auto id:m_TransformIds)
            if(id!=UINT32_MAX) VansTransformStore::Release(id);
    }

    bool Initialize(std::string& error)
    {
        for(std::size_t index=0;index<m_Plan.objects.size();++index)
        {
            const auto& object=m_Plan.objects[index];
            if(!object.entityGuid.empty() && !m_Indices.emplace(object.entityGuid,index).second)
            { error="Navigation geometry contains duplicate entity GUID: "+object.entityGuid; return false; }
            m_Entities.push_back(m_Registry.CreateEntity({object.entityGuid,object.name,{},object.active}));
        }
        for(std::size_t index=0;index<m_Plan.objects.size();++index)
            if(const auto& parent=m_Plan.objects[index].parent)
            {
                const auto found=m_Indices.find(parent->entityGuid.ToString());
                if(found==m_Indices.end() || !m_Registry.SetParent(m_Entities[index],m_Entities[found->second]))
                { error="Navigation geometry has an invalid runtime entity parent: "+ObjectLabel(m_Plan.objects[index]); return false; }
            }
        return true;
    }

    bool Prepare(std::size_t index,std::string& error)
    {
        std::vector<std::size_t> chain;
        while(m_TransformIds.at(index)==UINT32_MAX)
        {
            const auto& object=m_Plan.objects[index];
            if(!object.transform)
            { error="Navigation geometry parent chain has no Transform: "+ObjectLabel(object); return false; }
            chain.push_back(index);
            if(!object.parent) break;
            if(!object.parent->IsEntity())
            { error="Navigation geometry cannot bake bone/socket parent: "+ObjectLabel(object); return false; }
            index=m_Indices.at(object.parent->entityGuid.ToString());
        }
        for(auto item=chain.rbegin();item!=chain.rend();++item)
        {
            const auto& object=m_Plan.objects[*item];
            const auto id=VansTransformStore::Allocate();
            m_TransformIds[*item]=id;
            VansTransform local;
            local.m_Position=ToVec3(object.transform->position);
            local.m_Rotation=ToVec3(object.transform->rotation);
            local.m_Scale=ToVec3(object.transform->scale);
            VansTransformStore::Write(id,local);
            if(object.parent && !m_Graph.SetParent(id,
                m_TransformIds[m_Indices.at(object.parent->entityGuid.ToString())],VansTransformReparentMode::KeepLocal))
            { error=m_Graph.GetLastError(); return false; }
            m_Dirty=true;
        }
        return true;
    }

    bool ResolvePrepared(std::string& error)
    {
        if(m_Dirty && !m_Graph.Resolve()) { error=m_Graph.GetLastError(); return false; }
        m_Dirty=false; return true;
    }
    const VansTransform& WorldData(std::size_t index) const
    { return VansTransformStore::Read(m_TransformIds.at(index)); }
    bool IsActive(std::size_t index) const { return m_Registry.IsHierarchyActive(m_Entities.at(index)); }

private:
    const VansSceneObjectBuildPlan& m_Plan;
    std::unordered_map<std::string,std::size_t> m_Indices;
    std::vector<std::uint32_t> m_TransformIds;
    VansEntityRegistry m_Registry;
    std::vector<VansEntityHandle> m_Entities;
    VansTransformGraph m_Graph;
    bool m_Dirty=false;
};


std::string ComponentId(const VansSceneObjectBuildConfig& object, const char* key)
{
    const auto found=object.componentGuids.find(key);
    return found==object.componentGuids.end()?std::string{}:found->second;
}
void ComputeBounds(VansNavigationSourceRecord& record)
{
    record.boundsMinimum=glm::vec3((std::numeric_limits<float>::max)());
    record.boundsMaximum=-record.boundsMinimum;
    if(!record.geometry) return;
    for(std::size_t i=0;i<record.geometry->VertexCount();++i)
    {
        const auto& vertices=record.geometry->positions;
        const auto p=glm::vec3(record.worldTransform*glm::vec4(vertices[i*3],vertices[i*3+1],vertices[i*3+2],1));
        record.boundsMinimum=glm::min(record.boundsMinimum,p);
        record.boundsMaximum=glm::max(record.boundsMaximum,p);
    }
}
bool PhysicsProperties(const VansScenePhysicsNodeConfig& config,VansEngine::PhysicsNodeProperties& properties,std::string& error)
{
    properties.enabled=config.enabled.value_or(true);
    const auto body=config.bodyType.value_or("static"),shape=config.colliderType.value_or("box");
    if(body=="static") properties.bodyType=VansEngine::PhysicsBodyType::Static;
    else if(body=="dynamic") properties.bodyType=VansEngine::PhysicsBodyType::Dynamic;
    else if(body=="kinematic") properties.bodyType=VansEngine::PhysicsBodyType::Kinematic;
    else { error="Unknown Physics body type: "+body; return false; }
    if(shape=="box") properties.colliderType=VansEngine::PhysicsColliderType::Box;
    else if(shape=="sphere") properties.colliderType=VansEngine::PhysicsColliderType::Sphere;
    else if(shape=="capsule") properties.colliderType=VansEngine::PhysicsColliderType::Capsule;
    else if(shape=="mesh") properties.colliderType=VansEngine::PhysicsColliderType::Mesh;
    else if(shape=="convex") properties.colliderType=VansEngine::PhysicsColliderType::ConvexMesh;
    else { error="Unknown Physics collider type: "+shape; return false; }
    if(config.shapeOffset && config.colliderOffset) { error="Collider cannot define both shapeOffset and colliderOffset"; return false; }
    properties.shapeOffset=config.colliderOffset?ToVec3(*config.colliderOffset):(config.shapeOffset?ToVec3(*config.shapeOffset):glm::vec3(0));
    if(config.boxExtents) properties.boxExtents=ToVec3(*config.boxExtents);
    if(config.sphereRadius) properties.sphereRadius=*config.sphereRadius;
    if(config.capsuleRadius) properties.capsuleRadius=*config.capsuleRadius;
    if(config.capsuleHalfHeight) properties.capsuleHalfHeight=*config.capsuleHalfHeight;
    properties.useMeshCollider=config.useMeshCollider.value_or(false);
    properties.layerName=config.layer.value_or("Default"); properties.isTrigger=config.isTrigger.value_or(false);
    return true;
}
bool AppendTerrain(const VansSceneTerrainNodeConfig& terrain,const VansNavigationSettings& settings,
    VansNavigationSourceSnapshot& snapshot,std::string& error)
{
    if(!terrain.collision || !terrain.collision->enabled.value_or(false)) return true;
    if(!terrain.asset || !terrain.asset->HasPixelData() || !terrain.collision->layer || terrain.collision->layer->empty())
    { error="Navigation Terrain collision has no valid CPU height field or explicit layer"; return false; }
    const auto& asset=*terrain.asset;
    if(asset.width<2 || asset.height<2 || !std::isfinite(asset.settings.terrainSize) || asset.settings.terrainSize<=0 ||
        !std::isfinite(asset.settings.maxHeight) || asset.settings.maxHeight<=0 || !std::isfinite(asset.settings.heightOffset))
    { error="Navigation Terrain collision dimensions are invalid"; return false; }
    glm::vec3 minimum((std::numeric_limits<float>::max)()),maximum=-minimum;
    bool selected=false;
    for(const auto& record:snapshot.surfaces)
    {
        const auto& surface=record.surface;
        if(!record.enabled || surface.collection==VansNavigationCollection::ExplicitSources || surface.collection==VansNavigationCollection::Hierarchy ||
            (!surface.collisionLayers.empty() && std::find(surface.collisionLayers.begin(),surface.collisionLayers.end(),*terrain.collision->layer)==surface.collisionLayers.end())) continue;
        for(int corner=0;corner<8;++corner)
        {
            const auto local=surface.boundsCenterMeters+surface.boundsHalfExtentsMeters*glm::vec3(corner&1?1.f:-1.f,corner&2?1.f:-1.f,corner&4?1.f:-1.f);
            const auto p=glm::vec3(record.worldTransform*glm::vec4(local,1)); minimum=glm::min(minimum,p); maximum=glm::max(maximum,p);
        }
        selected=true;
    }
    if(!selected) return true;
    const float half=asset.settings.terrainSize*.5f;
    if(maximum.x < -half || minimum.x > half || maximum.z < -half || minimum.z > half) return true;
    const std::uint64_t sampleBytes=static_cast<std::uint64_t>(asset.width)*asset.height*sizeof(std::int16_t);
    if(sampleBytes>static_cast<std::uint64_t>(settings.maximumConcurrentBuildBytes)/2)
    { error="Navigation Terrain immutable samples exceed build memory budget"; return false; }
    auto heightfield=std::make_shared<VansHeightfieldData>();
    heightfield->columns=asset.width; heightfield->rows=asset.height;
    heightfield->origin=glm::vec3(-half,asset.settings.heightOffset,-half);
    heightfield->spacingX=asset.settings.terrainSize/(asset.width-1);
    heightfield->spacingZ=asset.settings.terrainSize/(asset.height-1);
    heightfield->heightScale=VansTerrainHeightEncoding::PhysicsHeightScale(asset.settings.maxHeight);
    heightfield->samples.reserve(asset.heights.size());
    std::int16_t minimumSample=32767,maximumSample=-32768;
    for(const auto pixel:asset.heights)
    {
        const auto sample=VansTerrainHeightEncoding::EncodePhysics(pixel);
        heightfield->samples.push_back(sample);
        minimumSample=(std::min)(minimumSample,sample); maximumSample=(std::max)(maximumSample,sample);
    }
    VansNavigationSourceRecord record; record.id="terrain/"+terrain.assetGuid; record.ownerId=record.id;
    record.kind=VansNavigationGeometryKind::Terrain; record.collisionLayer=*terrain.collision->layer;
    record.boundsMinimum=glm::vec3(-half,asset.settings.heightOffset+minimumSample*heightfield->heightScale,-half);
    record.boundsMaximum=glm::vec3(half,asset.settings.heightOffset+maximumSample*heightfield->heightScale,half);
    record.heightfield=std::move(heightfield);
    snapshot.sources.push_back(std::move(record)); return true;
}
}

bool VansSceneNavigationGeometry::FreezeColliderBox(const VansEngine::VansPhysicsBoxShapeSnapshot& box,const std::string& id,
    VansNavigationObstacleGeometry& output,std::string& error)
{
    VansNavigationObstacleBox shape; shape.halfExtentsMeters=box.halfExtents;
    return FreezeNavigationObstacleGeometry(shape,box.worldTransform,id,output,error);
}
bool VansSceneNavigationGeometry::CaptureSnapshot(const VansSceneContentBuildPlan& scene,
    const VansAssetObjectRepository& repository,const VansNavigationSettings& settings,const std::vector<std::string>& collisionLayers,
    const VansNavigationMeshResolver& resolveMesh,VansNavigationSourceSnapshot& output,std::string& error)
try
{
    if(!ValidateNavigationSettings(settings,error)) return false;
    VansNavigationSourceSnapshot snapshot; snapshot.collisionLayers=collisionLayers;
    VansSceneNavigationTransformContext transforms(scene.objects);
    if(!transforms.Initialize(error)) return false;
    std::unordered_map<std::string,const VansSceneObjectBuildConfig*> owners;
    for(const auto& object:scene.objects.objects) owners.emplace(object.entityGuid,&object);
    float tolerance=(std::numeric_limits<float>::max)();
    for(const auto& map:settings.maps) tolerance=(std::min)(tolerance,map.cellSizeMeters*.25f);
    for(std::size_t index=0;index<scene.objects.objects.size();++index)
    {
        const auto& object=scene.objects.objects[index];
        if((object.navigationSurface || object.navigationModifier || object.navigationObstacle) && !transforms.Prepare(index,error)) return false;
    }
    if(!transforms.ResolvePrepared(error)) return false;
    std::map<std::size_t,std::size_t> colliderBindings;
    for(std::size_t index=0;index<scene.objects.objects.size();++index)
    {
        const auto& object=scene.objects.objects[index];
        if(!object.navigationObstacle || !object.navigationObstacle->enabled || !transforms.IsActive(index)) continue;
        const auto* binding=std::get_if<VansNavigationObstacleCollider>(&object.navigationObstacle->runtime.shape);
        if(!binding) continue;
        const auto target=owners.find(binding->entityGuid);
        if(target==owners.end() || !target->second->physicsComponents.physics || ComponentId(*target->second,"physics")!=binding->componentGuid)
        { error="NavigationObstacle Collider binding is unavailable or incorrectly owned: "+binding->componentGuid; return false; }
        const auto targetIndex=static_cast<std::size_t>(target->second-scene.objects.objects.data());
        if(!transforms.Prepare(targetIndex,error)) return false;
        if((colliderBindings.size()+1u)*128u>static_cast<std::uint64_t>(settings.maximumConcurrentBuildBytes)/2u)
        { error="NavigationObstacle Collider binding metadata exceeds allocation budget"; return false; }
        colliderBindings.emplace(index,targetIndex);
    }
    if(!colliderBindings.empty() && !transforms.ResolvePrepared(error)) return false;
    for(std::size_t index=0;index<scene.objects.objects.size();++index)
    {
        const auto& object=scene.objects.objects[index];
        if(!object.navigationSurface && !object.navigationModifier && !object.navigationObstacle && !object.navigationLink) continue;
        const auto world=transforms.WorldData(index).GetModelMatrix();
        const bool active=transforms.IsActive(index);
        if(object.navigationSurface) snapshot.surfaces.push_back({ComponentId(object,"navigation_surface"),object.entityGuid,active&&object.navigationSurface->enabled,world,object.navigationSurface->runtime});
        if(object.navigationModifier) snapshot.modifiers.push_back({ComponentId(object,"navigation_modifier"),active&&object.navigationModifier->enabled,world,object.navigationModifier->runtime});
        if(object.navigationLink)
        {
            if(snapshot.links.size()==kMaximumNavigationLinkCapabilities) { error="Navigation Link capacity exceeded before allocation"; return false; }
            snapshot.links.push_back({ComponentId(object,"navigation_link"),active&&object.navigationLink->enabled,world,object.navigationLink->runtime});
        }
        if(object.navigationObstacle && active && object.navigationObstacle->enabled)
        {
            const auto& obstacle=object.navigationObstacle->runtime;
            VansNavigationObstacleRecord record; record.mode=obstacle.mode; record.mapId=obstacle.mapId; record.profiles=obstacle.profiles;
            record.enabled=active&&object.navigationObstacle->enabled;
            const auto binding=colliderBindings.find(index);
            if(binding!=colliderBindings.end())
            {
                const auto targetIndex=binding->second; const auto& target=scene.objects.objects[targetIndex];
                const auto& physics=*target.physicsComponents.physics;
                if(!transforms.IsActive(targetIndex) || !physics.enabled.value_or(true)) continue;
                VansEngine::PhysicsNodeProperties properties; VansEngine::VansPhysicsBoxShapeSnapshot box;
                if(!PhysicsProperties(physics,properties,error) ||
                    !VansEngine::VansPhysicsCollisionGeometry::BuildBoxShape(properties,transforms.WorldData(targetIndex),ToVec3(target.transform->scale),box,error) ||
                    !FreezeColliderBox(box,ComponentId(object,"navigation_obstacle"),record.shape,error))
                { error="NavigationObstacle Collider requires an upright Box: "+error; return false; }
            }
            else if(!FreezeNavigationObstacleGeometry(obstacle.shape,world,ComponentId(object,"navigation_obstacle"),record.shape,error)) return false;
            if(snapshot.obstacles.size()==kMaximumNavigationObstacles)
            { error="NavigationObstacle capacity exceeded before allocation"; return false; }
            snapshot.obstacles.push_back(std::move(record));
        }
    }
    if(snapshot.surfaces.empty()) { output=std::move(snapshot); error.clear(); return true; }
    std::unordered_map<std::string,std::shared_ptr<const VansTriangleMeshData>> meshes;
    const auto meshData=[&](const std::string& guid,std::shared_ptr<const VansTriangleMeshData>& mesh,std::string& resolveError)
    {
        const auto existing=meshes.find(guid); if(existing!=meshes.end()) { mesh=existing->second; return true; }
        if(!resolveMesh) { resolveError="CPU mesh resolver is unavailable"; return false; }
        auto candidate=std::make_shared<VansTriangleMeshData>();
        if(!resolveMesh(guid,*candidate,resolveError)) return false;
        mesh=candidate; meshes.emplace(guid,std::move(candidate)); return true;
    };
    const auto selected=[&](const VansNavigationSourceRecord& record)
    {
        for(const auto& surface:snapshot.surfaces)
        {
            const auto& policy=surface.surface;
            if(!surface.enabled || (!policy.collisionLayers.empty() && std::find(policy.collisionLayers.begin(),policy.collisionLayers.end(),record.collisionLayer)==policy.collisionLayers.end()) ||
                (policy.collection==VansNavigationCollection::Hierarchy && record.ownerId!=surface.ownerId && std::find(record.ancestors.begin(),record.ancestors.end(),surface.ownerId)==record.ancestors.end()) ||
                (policy.collection==VansNavigationCollection::ExplicitSources && !ReferencesNavigationSource(policy,record.ownerId,record.id))) continue;
            return true;
        }
        return false;
    };
    std::vector<std::pair<std::size_t,std::size_t>> preparedSources;
    for(std::size_t index=0;index<scene.objects.objects.size();++index)
    {
        const auto& object=scene.objects.objects[index];
        if(!object.physicsComponents.physics && !object.navigationSource) continue;
        VansNavigationSourceRecord record; record.ownerId=object.entityGuid;
        record.hasExplicitSource=object.navigationSource.has_value();
        record.id=ComponentId(object,record.hasExplicitSource?"navigation_source":"physics");
        if(record.hasExplicitSource) { record.source=object.navigationSource->runtime; record.sourceEnabled=object.navigationSource->enabled; }
        if(record.id.empty()) { error="Navigation collision/source requires a stable component identity: "+ObjectLabel(object); return false; }
        const bool custom=record.hasExplicitSource && record.sourceEnabled && !record.source.cpuMeshGuid.empty();
        const auto* physics=object.physicsComponents.physics?&*object.physicsComponents.physics:nullptr;
        const bool missing=record.hasExplicitSource && record.sourceEnabled && record.source.mode!=VansNavigationSourceMode::Exclude && !custom && !physics;
        record.collisionLayer=physics?physics->layer.value_or("Default"):"Default";
        record.isStatic=custom || missing || (physics && physics->bodyType.value_or("static")=="static");
        record.isTrigger=!custom && physics && physics->isTrigger.value_or(false);
        record.enabled=custom || missing || (physics && physics->enabled.value_or(true));
        const auto* parent=&object;
        while(parent->parent && parent->parent->IsEntity())
        {
            const auto id=parent->parent->entityGuid.ToString();
            if(std::find(record.ancestors.begin(),record.ancestors.end(),id)!=record.ancestors.end()) { error="Navigation source hierarchy contains a cycle"; return false; }
            record.ancestors.push_back(id); const auto found=owners.find(id);
            if(found==owners.end()) { error="Navigation source hierarchy parent is unavailable: "+id; return false; }
            parent=found->second;
        }
        if(!selected(record) || !record.enabled || record.isTrigger || !record.isStatic || (record.hasExplicitSource && record.sourceEnabled && record.source.mode==VansNavigationSourceMode::Exclude))
        { snapshot.sources.push_back(std::move(record)); continue; }
        if(!transforms.Prepare(index,error)) return false;
        preparedSources.emplace_back(index,snapshot.sources.size());
        snapshot.sources.push_back(std::move(record));
    }
    // 所有参与源和祖先只装配一次；整批沿同一 TransformGraph 求解，避免逐源重复求解。
    if(!transforms.ResolvePrepared(error)) return false;
    for(const auto& [index,sourceIndex]:preparedSources)
    {
        const auto& object=scene.objects.objects[index];
        auto& record=snapshot.sources[sourceIndex];
        const bool custom=record.hasExplicitSource && record.sourceEnabled && !record.source.cpuMeshGuid.empty();
        const auto* physics=object.physicsComponents.physics?&*object.physicsComponents.physics:nullptr;
        const bool missing=record.hasExplicitSource && record.sourceEnabled && record.source.mode!=VansNavigationSourceMode::Exclude && !custom && !physics;
        const auto world=transforms.WorldData(index).GetModelMatrix();
        record.enabled=record.enabled&&transforms.IsActive(index);
        if(!record.enabled) { continue; }
        std::shared_ptr<const VansTriangleMeshData> mesh;
        if(missing)
        { record.geometryError="Included NavigationSource requires Physics collision or an explicit CPU Model"; continue; }
        if(custom)
        {
            record.kind=VansNavigationGeometryKind::CustomMesh; record.worldTransform=world;
            if(!meshData(record.source.cpuMeshGuid,mesh,record.geometryError)) { continue; }
        }
        else
        {
            VansEngine::PhysicsNodeProperties properties;
            if(!physics || !PhysicsProperties(*physics,properties,record.geometryError)) { continue; }
            std::shared_ptr<const VansTriangleMeshData> source;
            if(properties.colliderType==VansEngine::PhysicsColliderType::Mesh || properties.colliderType==VansEngine::PhysicsColliderType::ConvexMesh)
            {
                if(!physics->mesh || !meshData(*physics->mesh,source,record.geometryError))
                { if(record.geometryError.empty()) record.geometryError="Included collider has no Model GUID"; continue; }
            }
            auto candidate=std::make_shared<VansTriangleMeshData>();
            if(!VansEngine::VansPhysicsCollisionGeometry::Build(properties,transforms.WorldData(index),ToVec3(object.transform->scale),source.get(),tolerance,
                static_cast<std::uint64_t>(settings.maximumConcurrentBuildBytes)/2,*candidate,record.geometryError))
            { continue; }
            mesh=std::move(candidate);
        }
        record.geometry=std::move(mesh); ComputeBounds(record);
    }
    auto effectiveTerrain=scene.terrain;
    std::shared_ptr<const VansPcgSplineFieldSnapshot> splineField;
    if(scene.splines && scene.terrain && scene.terrain->asset)
    {
        splineField=VansPcgSplineFieldBuilder::Build(*scene.splines,scene.terrain->asset,{},error);
        if(!splineField) return false;
        effectiveTerrain->asset=splineField->effectiveTerrain;
    }
    if(scene.vegetation)
    {
        VansPcgUpdatePlan plan; VansPcgCollisionPlan collisions;
        const auto terrain=[&](const VansPcgSurfaceBinding& binding,std::string& detail)
        {
            auto asset=splineField && binding.terrain==splineField->terrainGuid?splineField->effectiveTerrain:
                repository.ResolveLatest<VansTerrainAsset>(binding.terrain);
            return CreatePcgTerrainSurface(std::move(asset),detail);
        };
        if(!VansPcgUpdatePlanner::PlanRecipe(*scene.vegetation,repository,terrain,std::nullopt,splineField,
            VansPcgUpdatePartition::PerLayer,plan,error) || !BuildPcgCollisionPlan(plan,collisions,error)) return false;
        for(const auto& excluded:collisions.noCollisionPrototype)
            snapshot.exclusions.push_back({excluded.id,VansNavigationGeometryKind::Pcg,excluded.instanceCount,"No collision prototype"});
        for(const auto& instance:collisions.instances)
        {
            VansNavigationSourceRecord record; record.id=instance.componentGuid; record.ownerId=instance.ownerGuid;
            record.kind=VansNavigationGeometryKind::Pcg; record.collisionLayer=instance.prototype.layer;
            if(!selected(record)) { snapshot.sources.push_back(std::move(record)); continue; }
            std::shared_ptr<const VansTriangleMeshData> source;
            if(!meshData(instance.prototype.mesh.ToString(),source,record.geometryError))
            { snapshot.sources.push_back(std::move(record)); continue; }
            VansEngine::PhysicsNodeProperties properties; VansTransform pose;
            auto candidate=std::make_shared<VansTriangleMeshData>();
            if(!ResolvePcgCollisionInput(instance,properties,pose,record.geometryError) ||
                !VansEngine::VansPhysicsCollisionGeometry::Build(properties,pose,pose.m_Scale,source.get(),tolerance,
                    static_cast<std::uint64_t>(settings.maximumConcurrentBuildBytes)/2,*candidate,record.geometryError))
            { snapshot.sources.push_back(std::move(record)); continue; }
            record.geometry=std::move(candidate); ComputeBounds(record); snapshot.sources.push_back(std::move(record));
        }
    }
    if(effectiveTerrain && !AppendTerrain(*effectiveTerrain,settings,snapshot,error)) return false;
    output=std::move(snapshot); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Scene navigation CPU snapshot allocation failed"; return false; }
}
