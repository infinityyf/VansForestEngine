#include "VansRuntimeNavigationSources.h"
#include "VansRuntimeWorld.h"
#include "VansRuntimeComponentTypes.h"
#include "Transform/VansTransformStore.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Util/VansFileFingerprint.h"
#include "../SceneCore/VansSceneNavigationGeometry.h"
#include <algorithm>
#include <limits>
#include <set>
#include <new>

namespace Vans
{
namespace
{
std::uint64_t Key(VansComponentHandle h) { return (static_cast<std::uint64_t>(h.generation)<<32)|h.index; }
class Observation
{
public:
	template<class T> void Add(const T& value) { hash=ContinueMemoryFnv1a64(hash,&value,sizeof(value)); }
	void Add(const std::string& s) { Add(s.size()); hash=ContinueMemoryFnv1a64(hash,s.data(),s.size()); }
	void Add(const glm::vec3& p) { Add(p.x); Add(p.y); Add(p.z); }
	void Add(const glm::mat4& matrix) { for(int c=0;c<4;++c) for(int r=0;r<4;++r) Add(matrix[c][r]); }
	void Add(const std::vector<std::string>& values) { Add(values.size()); for(const auto& s:values) Add(s); }
	std::uint64_t hash=VANS_FNV1A64_OFFSET_BASIS;
};
bool WorldTransform(const VansRuntimeWorld& runtime,VansEntityHandle owner,glm::mat4& world,std::string& error)
{
	const auto* storage=runtime.FindStorage<VansRuntimeTransformComponent>(VansRuntimeComponentType_Transform);
	const auto* transform=storage?storage->Get(storage->FindFirstOwnedBy(owner)):nullptr;
	if(!transform || !VansTransformStore::IsAllocated(transform->transformStoreId))
	{ error="Navigation source requires an allocated runtime Transform"; return false; }
	world=VansTransformStore::Read(transform->transformStoreId).GetModelMatrix(); return true;
}
void Bounds(VansNavigationSourceRecord& record)
{
	record.boundsMinimum=glm::vec3((std::numeric_limits<float>::max)()); record.boundsMaximum=-record.boundsMinimum;
	for(std::size_t i=0;i<record.geometry->VertexCount();++i)
	{
		const auto& data=record.geometry->positions;
		const auto p=glm::vec3(record.worldTransform*glm::vec4(data[i*3],data[i*3+1],data[i*3+2],1));
		record.boundsMinimum=glm::min(record.boundsMinimum,p); record.boundsMaximum=glm::max(record.boundsMaximum,p);
	}
}
bool Selected(const VansNavigationSourceRecord& source,const std::vector<VansNavigationSurfaceRecord>& surfaces,
    const VansNavigationSettings& settings,bool runtimeOnly=false)
{
	for(const auto& record:surfaces)
	{
		const auto& s=record.surface;
        const auto* map=FindNavigationMap(settings,s.mapId);
        if(runtimeOnly && (!map || map->updateMode!=VansNavigationMapUpdateMode::Runtime)) continue;
		if(!record.enabled || (!s.collisionLayers.empty() && std::find(s.collisionLayers.begin(),s.collisionLayers.end(),source.collisionLayer)==s.collisionLayers.end())) continue;
		if(s.collection==VansNavigationCollection::Hierarchy && source.ownerId!=record.ownerId &&
			std::find(source.ancestors.begin(),source.ancestors.end(),record.ownerId)==source.ancestors.end()) continue;
		if(s.collection==VansNavigationCollection::ExplicitSources && !ReferencesNavigationSource(s,source.ownerId,source.id)) continue;
		return true;
	}
	return false;
}
}
bool VansRuntimeNavigationSources::Capture(const VansRuntimeWorld& runtime,const VansNavigationSettings& settings,
	const VansNavigationCpuMeshAccess& meshes,const std::vector<VansNavigationSourceRecord>& environment,
	VansNavigationSourceSnapshot& output,std::uint64_t& observation,std::string& error)
try
{
	VansNavigationSourceSnapshot snapshot;
	const auto& layers=VansEngine::VansCollisionLayerManager::Get();
	for(int i=0;i<layers.GetLayerCount();++i) snapshot.collisionLayers.push_back(layers.GetLayerName(i));
	const auto* surfaces=runtime.FindStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface);
	if(surfaces) for(std::size_t i=0;i<surfaces->Headers().size();++i)
	{
		const auto& h=surfaces->Headers()[i]; const auto* owner=runtime.Entities().Get(h.owner); if(!owner) continue;
		VansNavigationSurfaceRecord record{h.stableGuid,owner->stableGuid,h.effectiveEnabled,glm::mat4(1),surfaces->DenseData()[i]};
		if(!WorldTransform(runtime,h.owner,record.worldTransform,error)) return false;
		snapshot.surfaces.push_back(std::move(record));
	}
	if(snapshot.surfaces.size()>1024) { error="Runtime Navigation Surface capacity exceeded"; return false; }
    const auto* links=runtime.FindStorage<VansRuntimeNavigationLinkComponent>(VansRuntimeComponentType_NavigationLink);
    if(links) for(std::size_t i=0;i<links->Headers().size();++i)
    {
        const auto& header=links->Headers()[i];
        if(!runtime.Entities().Get(header.owner)) continue;
        if(snapshot.links.size()==kMaximumNavigationLinkCapabilities) { error="Runtime Navigation Link capacity exceeded before allocation"; return false; }
        VansNavigationLinkRecord record{header.stableGuid,header.effectiveEnabled,glm::mat4(1),links->DenseData()[i]};
        if(!WorldTransform(runtime,header.owner,record.worldTransform,error)) return false;
        snapshot.links.push_back(std::move(record));
    }
    const auto* obstacles=runtime.FindStorage<VansRuntimeNavigationObstacleComponent>(VansRuntimeComponentType_NavigationObstacle);
    const auto* physics=runtime.FindStorage<VansRuntimePhysicsComponent>(VansRuntimeComponentType_Physics);
    if(obstacles) for(std::size_t i=0;i<obstacles->Headers().size();++i)
    {
        const auto& h=obstacles->Headers()[i]; if(!runtime.IsAlive(h.owner) || !h.effectiveEnabled) continue;
        const auto& value=obstacles->DenseData()[i];
        VansNavigationObstacleRecord record; record.mode=value.mode; record.mapId=value.mapId; record.profiles=value.profiles; record.enabled=h.effectiveEnabled;
        if(const auto* binding=std::get_if<VansNavigationObstacleCollider>(&value.shape))
        {
            const auto handle=physics?physics->FindByStableGuid(binding->componentGuid):VansComponentHandle{};
            const auto* header=physics?physics->GetHeader(handle):nullptr;
            const auto* owner=header?runtime.Entities().Get(header->owner):nullptr;
            const auto* component=physics?physics->Get(handle):nullptr;
            if(!owner || owner->stableGuid!=binding->entityGuid || !component || !component->physicsNode)
            { error="NavigationObstacle Collider binding is unavailable or incorrectly owned: "+binding->componentGuid; return false; }
            if(!header->effectiveEnabled || !component->physicsNode->IsEnabled()) continue;
            VansEngine::VansPhysicsBoxShapeSnapshot box;
            if(!VansEngine::VansPhysicsCollisionGeometry::CaptureBoxShape(*component->physicsNode,box,error) ||
                !VansSceneNavigationGeometry::FreezeColliderBox(box,h.stableGuid,record.shape,error))
            { error="NavigationObstacle Collider requires an upright Box: "+error; return false; }
        }
        else
        {
            glm::mat4 world(1);
            if(!WorldTransform(runtime,h.owner,world,error) || !FreezeNavigationObstacleGeometry(value.shape,world,h.stableGuid,record.shape,error)) return false;
        }
        if(snapshot.obstacles.size()==kMaximumNavigationObstacles)
        { error="Runtime NavigationObstacle capacity exceeded before allocation"; return false; }
        snapshot.obstacles.push_back(std::move(record));
    }
	const auto* modifiers=runtime.FindStorage<VansRuntimeNavigationModifierComponent>(VansRuntimeComponentType_NavigationModifier);
	if(modifiers) for(std::size_t i=0;i<modifiers->Headers().size();++i)
	{
		const auto& h=modifiers->Headers()[i]; if(!runtime.IsAlive(h.owner)) continue;
		VansNavigationModifierRecord record{h.stableGuid,h.effectiveEnabled,glm::mat4(1),modifiers->DenseData()[i]};
		if(!WorldTransform(runtime,h.owner,record.worldTransform,error)) return false;
		snapshot.modifiers.push_back(std::move(record));
	}
	float tolerance=(std::numeric_limits<float>::max)();
	for(const auto& map:settings.maps) tolerance=(std::min)(tolerance,map.cellSizeMeters*.25f);
	std::set<std::uint64_t> activeCollisions;
	if(!snapshot.surfaces.empty())
	{
		const auto* sources=runtime.FindStorage<VansRuntimeNavigationSourceComponent>(VansRuntimeComponentType_NavigationSource);
		std::map<std::uint64_t,VansEntityHandle> owners;
		const auto ownerKey=[](VansEntityHandle h){return (static_cast<std::uint64_t>(h.generation)<<32)|h.index;};
		if(physics) for(const auto& h:physics->Headers()) if(runtime.IsAlive(h.owner)) owners.emplace(ownerKey(h.owner),h.owner);
		if(sources) for(const auto& h:sources->Headers()) if(runtime.IsAlive(h.owner)) owners.emplace(ownerKey(h.owner),h.owner);
		if(owners.size()+environment.size()>65536 || snapshot.modifiers.size()>4096)
		{ error="Runtime Navigation source/Modifier capacity exceeded"; return false; }
        const auto sourceBudget=static_cast<std::uint64_t>(settings.maximumConcurrentBuildBytes)/2;
        std::uint64_t retainedBytes=snapshot.links.capacity()*sizeof(VansNavigationLinkRecord);
        for(const auto& record:snapshot.links)
        {
            retainedBytes+=record.id.capacity()+record.link.mapId.capacity()+record.link.area.capacity()+3;
            for(const auto& profile:record.link.profiles) retainedBytes+=sizeof(profile)+profile.capacity()+1;
        }
		std::set<const void*> counted;
        const auto geometryBytes=[](const std::shared_ptr<const VansTriangleMeshData>& mesh)
        { return mesh?mesh->positions.capacity()*sizeof(float)+mesh->indices.capacity()*sizeof(std::uint32_t):0; };
        for(const auto& [key,cached]:m_Collisions)
            if(cached.geometry && counted.insert(cached.geometry.get()).second) retainedBytes+=geometryBytes(cached.geometry);
        for(const auto& record:environment)
            if(record.heightfield && counted.insert(record.heightfield.get()).second) retainedBytes+=record.heightfield->samples.capacity()*sizeof(std::int16_t);
        if(retainedBytes>sourceBudget) { error="Runtime frozen Navigation sources exceed allocation budget"; m_Collisions.clear(); return false; }
		for(const auto& [ignored,handle]:owners)
		{
			const auto* owner=runtime.Entities().Get(handle);
			const auto sourceHandle=sources?sources->FindFirstOwnedBy(handle):VansComponentHandle{};
			const auto* source=sources?sources->Get(sourceHandle):nullptr;
			const auto physicsHandle=physics?physics->FindFirstOwnedBy(handle):VansComponentHandle{};
			const auto* collision=physics?physics->Get(physicsHandle):nullptr;
			const auto* node=collision?collision->physicsNode:nullptr;
			VansNavigationSourceRecord record; record.ownerId=owner->stableGuid;
			if(collision && collision->sourceKind==VansRuntimePhysicsSourceKind::Pcg) record.kind=VansNavigationGeometryKind::Pcg;
			record.hasExplicitSource=source!=nullptr;
			if(source) { record.source=*source; record.sourceEnabled=runtime.IsComponentEffectivelyEnabled(sourceHandle); }
			const auto* header=runtime.GetComponentHeader(source?sourceHandle:physicsHandle);
			record.id=header->stableGuid;
			const bool custom=source && record.sourceEnabled && !source->cpuMeshGuid.empty();
            const bool missing=source && record.sourceEnabled && source->mode!=VansNavigationSourceMode::Exclude && !custom && !collision;
            record.enabled=owner->hierarchyActive && (custom || missing || (runtime.IsComponentEffectivelyEnabled(physicsHandle) && node && node->IsEnabled()));
            record.isStatic=custom || missing || (node && node->GetProperties().bodyType==VansEngine::PhysicsBodyType::Static);
			record.isTrigger=!custom && node && node->GetProperties().isTrigger;
			record.collisionLayer=node?node->GetProperties().layerName:"Default";
			auto parent=owner->parent;
			while(parent.IsValid())
			{
				const auto* p=runtime.Entities().Get(parent);
				if(!p || record.ancestors.size()>1024 || std::find(record.ancestors.begin(),record.ancestors.end(),p->stableGuid)!=record.ancestors.end())
				{ error="Runtime Navigation source hierarchy is unavailable or cyclic"; return false; }
				record.ancestors.push_back(p->stableGuid); parent=p->parent;
			}
            const bool dynamicRuntime=record.hasExplicitSource && record.sourceEnabled && Selected(record,snapshot.surfaces,settings,true);
			if(record.enabled && (record.isStatic || dynamicRuntime) && !record.isTrigger && Selected(record,snapshot.surfaces,settings) &&
				!(source && record.sourceEnabled && source->mode==VansNavigationSourceMode::Exclude))
			{
				if(custom)
				{
					record.kind=VansNavigationGeometryKind::CustomMesh;
					if(!WorldTransform(runtime,handle,record.worldTransform,error)) return false;
					if(!meshes || !meshes(source->cpuMeshGuid,record.geometry,record.geometryError))
						if(record.geometryError.empty()) record.geometryError="Included NavigationSource CPU Model is unavailable";
				}
                else if(missing) record.geometryError="Included NavigationSource requires Physics collision or an explicit CPU Model";
                else
				{
					const auto key=Key(physicsHandle); activeCollisions.insert(key);
					auto& cached=m_Collisions[key];
                    const auto oldBytes=geometryBytes(cached.geometry); const auto* oldGeometry=cached.geometry.get();
                    if(VansEngine::VansPhysicsCollisionGeometry::Capture(*node,tolerance,sourceBudget-retainedBytes,cached,record.geometryError))
                    {
                        record.geometry=cached.geometry; record.boundsMinimum=cached.boundsMinimum; record.boundsMaximum=cached.boundsMaximum;
                        if(oldGeometry!=cached.geometry.get())
                        {
                            counted.erase(oldGeometry); counted.insert(cached.geometry.get());
                            retainedBytes=retainedBytes-oldBytes+geometryBytes(cached.geometry);
                        }
                    }
				}
				if(record.geometry)
				{
                    if(custom) Bounds(record);
					if(counted.insert(record.geometry.get()).second)
                        retainedBytes+=geometryBytes(record.geometry);
                    if(retainedBytes>sourceBudget) { error="Runtime frozen Navigation sources exceed aggregate allocation budget"; return false; }
				}
			}
			snapshot.sources.push_back(std::move(record));
		}
		for(const auto& record:environment)
		{
			snapshot.sources.push_back(record);
		}
        if(retainedBytes>sourceBudget)
		{ error="Runtime frozen Navigation sources exceed aggregate memory budget"; return false; }
	}
	for(auto it=m_Collisions.begin();it!=m_Collisions.end();) if(!activeCollisions.count(it->first)) it=m_Collisions.erase(it); else ++it;
	Observation stamp; stamp.Add(snapshot.collisionLayers);
	for(const auto& record:snapshot.surfaces)
	{
		std::uint64_t hash=0; if(!ComputeNavigationSurfaceContributionHash(record,hash,error)) return false;
		stamp.Add(record.id); stamp.Add(record.ownerId); stamp.Add(hash); stamp.Add(record.surface.navigationMeshGuid);
	}
	for(const auto& r:snapshot.sources)
	{
        // 隐式动态物体不参与构建输入；显式声明始终保留观察和校验，
        // 包括 Baked 模式下不合法的动态源。
        if (!r.hasExplicitSource && (!r.enabled || r.isTrigger || !r.isStatic ||
            !Selected(r, snapshot.surfaces, settings))) continue;
		stamp.Add(r.id); stamp.Add(r.ownerId); stamp.Add(r.ancestors); stamp.Add(r.kind); stamp.Add(r.enabled); stamp.Add(r.isStatic); stamp.Add(r.isTrigger);
		stamp.Add(r.worldTransform); stamp.Add(r.boundsMinimum); stamp.Add(r.boundsMaximum); stamp.Add(r.collisionLayer); stamp.Add(r.area);
		stamp.Add(r.hasExplicitSource); stamp.Add(r.sourceEnabled); stamp.Add(r.source.mode); stamp.Add(r.source.area); stamp.Add(r.source.cpuMeshGuid); stamp.Add(r.geometryError);
		stamp.Add(r.geometry.get()); stamp.Add(r.heightfield.get());
	}
	for(const auto& r:snapshot.modifiers)
	{
		const auto& m=r.modifier; stamp.Add(r.id); stamp.Add(r.enabled); stamp.Add(r.worldTransform); stamp.Add(m.mapId); stamp.Add(m.profiles); stamp.Add(m.shape);
		stamp.Add(m.centerMeters); stamp.Add(m.halfExtentsMeters); stamp.Add(m.minimumHeightMeters); stamp.Add(m.maximumHeightMeters); stamp.Add(m.area); stamp.Add(m.exclude); stamp.Add(m.priority);
		stamp.Add(m.convexVerticesMeters.size()); for(const auto& p:m.convexVerticesMeters) { stamp.Add(p.x); stamp.Add(p.y); }
	}
    for(const auto& record:snapshot.links)
    {
        const auto& link=record.link; stamp.Add(record.id); stamp.Add(record.enabled); stamp.Add(record.worldTransform);
        stamp.Add(link.mapId); stamp.Add(link.profiles); stamp.Add(link.startMeters); stamp.Add(link.endMeters);
        stamp.Add(link.radiusMeters); stamp.Add(link.area); stamp.Add(link.bidirectional);
    }
    // 障碍总是先提交 World；有效 Carve 变更只由 World 的同一记录集观测，纯避让运动不重收集源几何。
	output=std::move(snapshot); observation=stamp.hash; error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Runtime Navigation source capture allocation failed"; return false; }
}
