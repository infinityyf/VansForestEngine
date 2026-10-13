#include "VansVFXActionService.h"
#include "VansVFXActionCapability.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../PhysicsCore/VansPhysicsQuery.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans
{
VansVFXActionService::VansVFXActionService(IVansGameplayServiceRuntime& runtime,VansVFXSceneBackend backend)
    : m_Runtime(runtime),m_Backend(std::move(backend))
{
    if (!m_Backend.surfaceFields) m_Backend.surfaceFields=std::make_shared<VansSurfaceFieldRegistry>();
}
const VansActionServiceCapability& VansVFXActionService::Capability() const { return VansVFXActionCapability(); }
VansActionCommandResult VansVFXActionService::Execute(const VansActionCommand& command)
{
    if (command.stableName == "VFX.SurfaceField")
    {
        const auto reject=[](const char* reason) { return VansActionCommandResult{VansActionError::Rejected,{},{},reason}; };
        const auto number=[&](const char* name,float fallback) {
            const auto* value=FindObjectField(command.payload,name); return value ? static_cast<float>(ReadSerializedNumber(*value,fallback)) : fallback;
        };
        const auto* point=FindObjectField(command.payload,"position");
        const auto* layers=FindObjectField(command.payload,"surfaceLayers");
        if (!point || !layers || layers->kind!=VansSerializedValue::Kind::Array || !m_Backend.spawn
            || !m_Backend.setEmissionPoints || !VansEngine::VansPhysicsQuery::IsAvailable())
            return reject("Surface field requires Physics, position, layers and emission point backend");
        const auto coordinate=[&](const char* name) { const auto* value=FindObjectField(*point,name); return value ? static_cast<float>(ReadSerializedNumber(*value,NAN)) : NAN; };
        SurfaceField field;
        field.origin={coordinate("x"),coordinate("y"),coordinate("z")};
        field.radius=number("radius",0); field.spacing=number("spacing",1);
        field.probeHeight=number("probeHeight",1); field.maxDrop=number("maxDrop",3);
        field.minNormalY=number("minNormalY",0.25f);
        field.duration=number("duration",0); field.spreadDuration=number("spreadDuration",0);
        field.spawnInterval=number("spawnInterval",0.1f);
        const auto maximum=ReadSerializedIntField(command.payload,"maxSurfaces",16);
        const auto capacity=ReadSerializedIntField(command.payload,"maxConcurrentPerSource",8);
        VansVFXSpawnRequest request; request.owner=command.context.Entity(VansActionContextSlots::Owner);
        if (!request.owner.IsValid() || !VansAssetGuid::TryParse(ReadSerializedStringField(command.payload,"effect"),request.effect) || !request.effect.IsValid()
            || !std::isfinite(glm::length(field.origin)) || !std::isfinite(field.radius) || field.radius<=0 || field.radius>30
            || !std::isfinite(field.spacing) || field.spacing<0.1f || field.spacing>field.radius
            || !std::isfinite(field.probeHeight) || field.probeHeight<=0 || field.probeHeight>30
            || !std::isfinite(field.maxDrop) || field.maxDrop<=0 || field.maxDrop>60
            || !std::isfinite(field.minNormalY) || field.minNormalY<0 || field.minNormalY>1
            || !std::isfinite(field.duration) || field.duration<=0 || field.duration>60
            || !std::isfinite(field.spreadDuration) || field.spreadDuration<=0 || field.spreadDuration>field.duration
            || !std::isfinite(field.spawnInterval) || field.spawnInterval<0.02f || field.spawnInterval>1
            || maximum<1 || maximum>16 || capacity<1 || capacity>16) return reject("Surface field parameters are invalid");
        unsigned fields=0; m_Effects.ForEach([&](auto,const Effect& effect) { if (effect.field) ++fields; });
        if (fields>=16) return reject("Scene surface field capacity reached");
        for (const auto& layer : layers->arrayItems) {
            int index=0;
            if (layer.kind!=VansSerializedValue::Kind::String || !VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(layer.stringValue,index))
                return reject("Surface field layer is unknown");
            field.mask |= 1u<<index;
        }
        if (!field.mask) return reject("Surface field needs at least one surface layer");
        field.maximum=static_cast<std::uint32_t>(maximum);
        // Balanced concentric candidates are a generic bounded placement policy.
        // They are not Valve's unrecovered inferno propagation algorithm.
        unsigned remaining=field.maximum-1;
        for (unsigned ring=1;remaining && ring*field.spacing<=field.radius;++ring) {
            const unsigned count=std::min(remaining,static_cast<unsigned>(std::floor(6.2831853f*ring)));
            for (unsigned i=0;i<count;++i) {
                const float angle=6.2831853f*i/count+(ring%2 ? 0 : 0.3926991f);
                field.candidates.push_back(field.origin+glm::vec3(std::cos(angle),0,std::sin(angle))*(ring*field.spacing));
            }
            remaining-=count;
        }
        std::uint32_t budget=12;
        if (!PlaceSurfaceCell(field,field.origin,budget)) return reject("Surface field origin has no visible supported surface");
        request.worldPosition=field.cells.front().point.position;
        request.emissionPoints=field.points; request.maxConcurrentPerSource=static_cast<std::uint32_t>(capacity);
        std::string error; const auto instance=m_Backend.spawn(request,error);
        if (!instance.IsValid()) return {VansActionError::Rejected,{},{},std::move(error)};
        field.nextSpawn=field.spawnInterval;
        Effect effect; effect.instance=instance; effect.field=std::move(field); effect.owner=request.owner; effect.action=command.action;
        const auto handle=m_Effects.Emplace(std::move(effect));
        PublishSurfaceField(handle,*m_Effects.Resolve(handle));
        using V=VansSerializedValue;
        return {VansActionError::None,handle,V::Object({{"field",V::Object({
            {"index",V::Int(handle.index)},{"generation",V::Int(handle.generation)}})}}), {}};
    }
    if (command.stableName == "VFX.Spawn" || command.stableName == "VFX.Pulse" || command.stableName == "VFX.ExtinguishingVolume")
    {
        VansVFXSpawnRequest request;
        request.owner = command.context.Entity(VansActionContextSlots::Owner);
        std::string error;
        const auto* source = FindObjectField(command.payload,"source");
        const auto* position=FindObjectField(command.payload,"position");
        if (position && !position->objectFields.empty())
        {
            if (!FindObjectField(*position,"x") || !FindObjectField(*position,"y") || !FindObjectField(*position,"z"))
                return {VansActionError::Rejected,{},{},"VFX position requires x, y and z"};
            const auto number=[&](const char* key) { return static_cast<float>(ReadSerializedNumber(*FindObjectField(*position,key),NAN)); };
            const glm::vec3 p(number("x"),number("y"),number("z"));
            if (!std::isfinite(glm::length(p)) || (source && !source->objectFields.empty()))
                return {VansActionError::Rejected,{},{},"VFX needs exactly one finite world position or source binding"};
            request.worldPosition=p;
        }
        const auto limit = ReadSerializedIntField(command.payload,"maxConcurrentPerSource",
            command.stableName=="VFX.ExtinguishingVolume" ? 8 : 16);
        if (!request.owner.IsValid() || !VansAssetGuid::TryParse(ReadSerializedStringField(command.payload,"effect"),request.effect)
            || (!request.worldPosition && (!source || !TryReadSceneParentReference(*source,request.source,error))) || limit < 1 || limit > 64)
            return {VansActionError::Rejected,{},{},error.empty() ? "VFX requires an effect GUID, source reference, and capacity in [1,64]" : error};
        request.maxConcurrentPerSource = static_cast<std::uint32_t>(limit);
        std::optional<ExtinguishingVolume> volume;
        if (command.stableName=="VFX.ExtinguishingVolume") {
            if (!m_Backend.sampleExtinction || !request.worldPosition || limit>8)
                return {VansActionError::Dependency,{}, {},"Extinguishing volume needs a world position, sampled medium backend and capacity <=8"};
            unsigned count=0; m_Effects.ForEach([&](auto,const Effect& effect) { if (effect.volume) ++count; });
            if (count>=8) return {VansActionError::Rejected,{}, {},"Scene extinguishing volume capacity reached"};
            const auto number=[&](const char* name) { const auto* value=FindObjectField(command.payload,name); return value ? static_cast<float>(ReadSerializedNumber(*value,NAN)) : NAN; };
            volume.emplace(); volume->origin=*request.worldPosition;
            volume->extinctionThreshold=number("extinctionThreshold");
            volume->fraction=number("fullExtinguishFraction"); volume->period=number("samplePeriod");
            const auto minimum=ReadSerializedIntField(command.payload,"minimumCells",0);
            const auto* offsets=FindObjectField(command.payload,"sampleOffsets");
            const auto* zeroDirection=FindObjectField(command.payload,"zeroDistanceDirection");
            const auto component=[&](const char* name) {
                const auto* value=zeroDirection ? FindObjectField(*zeroDirection,name) : nullptr;
                return value ? static_cast<float>(ReadSerializedNumber(*value,NAN)) : NAN;
            };
            volume->zeroDistanceDirection={component("x"),component("y"),component("z")};
            const double zeroLength=glm::length(glm::dvec3(volume->zeroDistanceDirection));
            if (!std::isfinite(volume->extinctionThreshold) || volume->extinctionThreshold<=0 || volume->extinctionThreshold>100
                || !std::isfinite(volume->fraction) || volume->fraction<0 || volume->fraction>1
                || !std::isfinite(volume->period) || volume->period<.02f || volume->period>1 || minimum<1 || minimum>16
                || !offsets || offsets->kind!=VansSerializedValue::Kind::Array || offsets->arrayItems.empty() || offsets->arrayItems.size()>16
                || !std::isfinite(zeroLength) || zeroLength==0
                || !VansAssetGuid::TryParse(ReadSerializedStringField(command.payload,"extinguishEffect"),volume->extinguishEffect)
                || !volume->extinguishEffect.IsValid()) return {VansActionError::Rejected,{}, {},"Extinguishing volume parameters are invalid"};
            volume->minimumCells=static_cast<std::uint32_t>(minimum);
            volume->zeroDistanceDirection=glm::vec3(glm::dvec3(volume->zeroDistanceDirection)/zeroLength);
            for (const auto& offset : offsets->arrayItems) {
                const auto coordinate=[&](const char* name) { const auto* value=FindObjectField(offset,name); return value ? static_cast<float>(ReadSerializedNumber(*value,NAN)) : NAN; };
                const glm::vec3 p(coordinate("x"),coordinate("y"),coordinate("z"));
                if (!std::isfinite(glm::length(p)) || glm::length(p)>30) return {VansActionError::Rejected,{}, {},"Extinguishing volume sample offset is invalid"};
                volume->sampleOffsets.push_back(p);
            }
        }
        if (command.stableName == "VFX.Pulse")
        {
            if (!m_Backend.pulse) return {VansActionError::Dependency,{},{},"VFX pulse backend is unavailable"};
            if (!m_Backend.pulse(request,error)) return {VansActionError::Rejected,{},{},std::move(error)};
            return {};
        }
        if (!m_Backend.spawn) return {VansActionError::Dependency,{},{},"VFX scene backend is unavailable"};
        const auto instance = m_Backend.spawn(request,error);
        if (!instance.IsValid()) return {VansActionError::Rejected,{},{},std::move(error)};
        Effect effect; effect.instance=instance; effect.owner=request.owner; effect.action=command.action; effect.volume=std::move(volume);
        return {VansActionError::None,m_Effects.Emplace(std::move(effect)),{}, {}};
    }
    if (command.stableName != "VFX.Stop") return {VansActionError::InvalidDefinition,{},{},"Unknown VFX command"};
    const auto* value = FindObjectField(command.payload,"resource");
    const auto index = value ? ReadSerializedIntField(*value,"index",-1) : -1;
    const auto generation = value ? ReadSerializedIntField(*value,"generation",0) : 0;
    if (index < 0 || index > UINT32_MAX || generation <= 0 || generation > UINT32_MAX)
        return {VansActionError::Rejected,{},{},"VFX resource handle is invalid"};
    auto* effect = m_Effects.Resolve({static_cast<std::uint32_t>(index),static_cast<std::uint32_t>(generation)});
    if (!effect) return {VansActionError::Rejected,{},{},"VFX resource handle is stale"};
    const auto mode = ReadSerializedStringField(command.payload,"mode","Drain");
    if (mode != "Drain" && mode != "DetachAndDrain" && mode != "Immediate")
        return {VansActionError::Rejected,{},{},"VFX stop mode must be Drain, DetachAndDrain or Immediate"};
    if (!effect->instance.IsValid()) return {};
    if (!m_Backend.stop || !m_Backend.stop(effect->instance,
        mode == "Immediate" ? VansVFXStopMode::Immediate : mode == "DetachAndDrain" ? VansVFXStopMode::DetachAndDrain : VansVFXStopMode::Drain))
        return {VansActionError::Execution,{},{},"VFX stop was rejected"};
    if (effect->field) {
        const VansGenerationHandle handle{static_cast<std::uint32_t>(index),static_cast<std::uint32_t>(generation)};
        effect->field->stopped=true; effect->field->stopReason="Stopped";
        PublishSurfaceField(handle,*effect); NotifySurfaceFieldStopped(handle,*effect);
    }
    return {};
}
bool VansVFXActionService::Release(VansGenerationHandle resource,std::string& error)
{
    auto* effect = m_Effects.Resolve(resource);
    if (!effect) { error = "VFX resource handle is stale"; return false; }
    if (effect->instance.IsValid() && (!m_Backend.destroy || !m_Backend.destroy(effect->instance)))
    { error = "VFX instance destruction failed"; return false; }
    m_Backend.surfaceFields->Remove(resource);
    return m_Effects.Release(resource);
}
void VansVFXActionService::PublishSurfaceField(VansGenerationHandle handle,const Effect& effect)
{
    if (!effect.field) return;
    const auto& field=*effect.field;
    VansSurfaceFieldSnapshot snapshot;
    snapshot.owner=effect.owner; snapshot.origin=field.origin;
    snapshot.radius=field.radius; snapshot.duration=field.duration; snapshot.age=field.age;
    snapshot.active=!field.stopped && effect.instance.IsValid(); snapshot.cellCount=field.cells.size();
    for (std::size_t i=0;i<snapshot.cellCount;++i) {
        const auto& point=field.cells[i].point;
        snapshot.cells[i]={point.position,point.normal,point.bornTime,point.active};
    }
    if (!m_Backend.surfaceFields->Publish(handle,snapshot))
        VANS_LOG_ERROR("[VFX] Surface field snapshot capacity or identity was rejected");
}
bool VansVFXActionService::PlaceSurfaceCell(SurfaceField& field,glm::vec3 candidate,std::uint32_t& budget)
{
    if (budget<12 || field.cells.size()>=field.maximum) return false;
    const auto raycast=[&](glm::vec3 from,glm::vec3 direction,float length,VansEngine::VansPhysicsQueryHit& hit) {
        if (!budget || length<=0) return false;
        --budget;
        VansEngine::VansPhysicsRaycastRequest ray; ray.origin=from; ray.direction=direction; ray.distance=length;
        ray.filter.layerMask=field.mask; ray.filter.includeTriggers=false; ray.filter.includeControllers=false;
        return VansEngine::VansPhysicsQuery::RaycastClosest(ray,hit);
    };
    VansEngine::VansPhysicsQueryHit surface;
    if (!raycast(candidate+glm::vec3(0,field.probeHeight,0),{0,-1,0},field.probeHeight+field.maxDrop,surface)
        || surface.normal.y<field.minNormalY || glm::length(glm::vec2(surface.position.x-field.origin.x,surface.position.z-field.origin.z))>field.radius) return false;
    std::uint32_t parent=0;
    float nearest=std::numeric_limits<float>::max();
    for (std::uint32_t i=0;i<field.cells.size();++i) {
        const auto distance=glm::distance(field.cells[i].point.position,surface.position);
        if (distance<field.spacing*0.7f) return false;
        if (field.cells[i].point.active && distance<nearest) { nearest=distance; parent=i; }
    }
    if (!field.cells.empty() && nearest==std::numeric_limits<float>::max()) return false;
    const auto target=surface.position+surface.normal*0.025f;
    const auto from=field.cells.empty() ? field.origin+glm::vec3(0,0.08f,0) : field.cells[parent].point.position+glm::vec3(0,0.12f,0);
    const auto delta=target+glm::vec3(0,0.12f,0)-from; const auto length=glm::length(delta);
    VansEngine::VansPhysicsQueryHit obstruction;
    if (length>0.05f && raycast(from,delta/length,length-0.04f,obstruction)) return false;
    VansGraphics::VansParticleEmissionPoint core;
    core.id=static_cast<std::uint32_t>(field.cells.size())*16+1;
    core.channel=1; core.position=target; core.normal=surface.normal; core.bornTime=field.age;
    field.cells.push_back({core,parent}); field.points.push_back(core);
    // Boundary samples are traced independently; an edge cannot float across a ledge.
    for (unsigned i=0;i<4;++i) {
        const float angle=i*1.5707963f;
        const glm::vec3 direction(std::cos(angle),0,std::sin(angle));
        const auto edge=target+direction*(field.spacing*0.48f);
        if (raycast(edge+glm::vec3(0,0.35f,0),{0,-1,0},0.7f,surface) && surface.normal.y>=field.minNormalY) {
            auto point=core; point.id=core.id+i+1; point.channel=2;
            point.position=surface.position+surface.normal*0.035f; point.normal=surface.normal; point.scale=0.55f;
            field.points.push_back(point);
        }
        // A climbing layer exists only where a real nearby wall was hit.
        if (raycast(target+glm::vec3(0,0.3f,0),direction,field.spacing*0.65f,surface) && std::abs(surface.normal.y)<0.5f) {
            auto point=core; point.id=core.id+i+5; point.channel=3;
            point.position=surface.position+surface.normal*0.035f; point.normal=surface.normal; point.scale=0.7f;
            field.points.push_back(point);
        }
    }
    // Interior edge sprites are disabled as neighbouring cells arrive. Stable IDs
    // retain animation clocks and existing particles drain instead of restarting.
    for (auto& point : field.points) if (point.channel==2) {
        point.active=field.cells[(point.id-1)/16].point.active;
        if (!point.active) continue;
        for (const auto& cell : field.cells) {
            if (!cell.point.active) continue;
            if ((point.id-1)/16== (cell.point.id-1)/16) continue;
            if (glm::distance(point.position,cell.point.position)<field.spacing*0.6f) { point.active=false; break; }
        }
    }
    return true;
}
void VansVFXActionService::ExtendSurfaceField(Effect& effect,float seconds,std::uint32_t& budget)
{
    auto& field=*effect.field; field.age+=seconds;
    if (field.stopped) return;
    if (field.age>=field.duration) {
        field.stopped=true;
        for (auto& point : field.points) point.active=false;
        if (!m_Backend.setEmissionPoints(effect.instance,field.points))
            VANS_LOG_ERROR("[VFX] Surface field could not disable its emission points at expiry");
        if (!m_Backend.stop || !m_Backend.stop(effect.instance,VansVFXStopMode::Drain))
            VANS_LOG_ERROR("[VFX] Surface field drain was rejected at expiry");
        return;
    }
    if (field.age>field.spreadDuration || field.age<field.nextSpawn || budget<12 || field.candidate>=field.candidates.size()) return;
    const auto candidate=field.candidates[field.candidate++];
    field.nextSpawn=field.age+field.spawnInterval;
    if (PlaceSurfaceCell(field,candidate,budget) && !m_Backend.setEmissionPoints(effect.instance,field.points)) {
        field.stopped=true;
        VANS_LOG_ERROR("[VFX] Surface field emission point update was rejected");
        if (!m_Backend.stop || !m_Backend.stop(effect.instance,VansVFXStopMode::Drain))
            VANS_LOG_ERROR("[VFX] Surface field drain was rejected after a point update failure");
    }
}
void VansVFXActionService::NotifySurfaceFieldStopped(VansGenerationHandle handle,Effect& effect)
{
    if (!effect.field || !effect.field->stopped || effect.completionNotified) return;
    effect.completionNotified=true;
    if (!m_Runtime.HasHost(effect.owner) || !m_Runtime.IsActionActive(effect.owner,effect.action)) return;
    using V=VansSerializedValue;
    VansActionEvent event; event.stableName="VFX.SurfaceFieldStopped";
    event.type=VansMakeStableId<VansActionFieldIdTag>(event.stableName); event.source=effect.owner; event.target=effect.owner;
    const auto& position=effect.field->origin;
    event.payload=V::Object({{"field",V::Object({{"index",V::Int(handle.index)},{"generation",V::Int(handle.generation)}})},
        {"reason",V::String(effect.field->stopReason)},
        {"position",V::Object({{"x",V::Float(position.x)},{"y",V::Float(position.y)},{"z",V::Float(position.z)}})}});
    std::string error;
    if (!m_Runtime.EnqueueActionEvent(effect.owner,effect.action,std::move(event),error))
        VANS_LOG_WARN("[VFX] Could not report stopped surface field: " << error);
}
void VansVFXActionService::SampleExtinguishingVolumes(const std::vector<VansGenerationHandle>& volumes)
{
    m_Effects.ForEach([&](auto handle,Effect& effect) {
        if (!effect.field || effect.field->stopped || !effect.instance.IsValid()) return;
        auto& field=*effect.field;
        std::vector<std::size_t> cells;
        for (std::size_t i=0;i<field.cells.size();++i) if (field.cells[i].point.active) {
            cells.push_back(i);
        }
        if (cells.empty()) return;
        std::vector<const ExtinguishingVolume*> coverage(field.cells.size(),nullptr);
        const ExtinguishingVolume* policy=nullptr;
        // 同一采样批次合并所有烟雾覆盖，完整熄灭比例以本批次开始的火点数为准。
        for (auto volumeHandle : volumes) {
            const auto* volumeEffect=m_Effects.Resolve(volumeHandle);
            if (!volumeEffect || !volumeEffect->volume || !volumeEffect->instance.IsValid()) continue;
            const auto& volume=*volumeEffect->volume;
            std::vector<glm::vec3> positions;
            for (auto i : cells)
                for (const auto& offset : volume.sampleOffsets) positions.push_back(field.cells[i].point.position+offset);
            std::vector<float> values;
            if (!m_Backend.sampleExtinction(volumeEffect->instance,positions,values)) continue;
            if (values.size()!=positions.size() || std::any_of(values.begin(),values.end(),[](float value) {
                return !std::isfinite(value) || value<0;
            })) { VANS_LOG_ERROR("[VFX] Medium sampler returned invalid extinction values"); continue; }
            for (std::size_t i=0;i<cells.size();++i)
                for (std::size_t j=0;j<volume.sampleOffsets.size();++j)
                    if (values[i*volume.sampleOffsets.size()+j]>volume.extinctionThreshold) {
                        if (!coverage[cells[i]]) coverage[cells[i]]=&volume;
                        if (!policy) policy=&volume;
                        break;
                    }
        }
        if (!policy) return;
        std::vector<std::size_t> affected;
        for (auto i : cells) if (coverage[i]) affected.push_back(i);
        const bool full=cells.size()>=policy->minimumCells && affected.size()>cells.size()*policy->fraction;
        const auto disabled=full ? cells : affected;
        auto updated=field.points;
        for (auto& point : updated)
            if (std::find(disabled.begin(),disabled.end(),(point.id-1)/16)!=disabled.end()) point.active=false;
        if (!m_Backend.setEmissionPoints(effect.instance,updated)) {
            VANS_LOG_ERROR("[VFX] Extinguished surface emission point update was rejected"); return;
        }
        for (auto i : disabled) field.cells[i].point.active=false;
        field.points=std::move(updated);
        if (full || affected.size()==cells.size()) {
            field.stopped=true;
            field.stopReason=full ? "Extinguished" : "Empty";
            if (!m_Backend.stop || !m_Backend.stop(effect.instance,VansVFXStopMode::Drain))
                VANS_LOG_ERROR("[VFX] Extinguished surface field drain was rejected");
        }
        PublishSurfaceField(handle,effect);
        NotifySurfaceFieldStopped(handle,effect);
        // Each disabled core cell owns one root pulse. Edge/climbing render points
        // are children of that cell and must not create additional feedback roots.
        unsigned feedbackCount=0,directedCount=0;
        for (auto i : disabled) {
            const auto* covering=coverage[i];
            VansVFXSpawnRequest feedback; feedback.owner=effect.owner;
            feedback.emissionGroup=handle;
            feedback.effect=(covering ? covering : policy)->extinguishEffect;
            feedback.worldPosition=field.cells[i].point.position; feedback.maxConcurrentPerSource=16;
            if (covering) {
                const glm::dvec3 direction=glm::dvec3(*feedback.worldPosition)-glm::dvec3(covering->origin);
                const double length=glm::length(direction);
                feedback.worldDirection=length>0 ? glm::vec3(direction/length) : covering->zeroDistanceDirection;
                ++directedCount;
            }
            std::string error;
            if (!m_Backend.pulse || !m_Backend.pulse(feedback,error))
                VANS_LOG_WARN("[VFX] Extinguish feedback was rejected: " << error);
            else ++feedbackCount;
        }
        VANS_LOG("[VFX Extinguish] field=" << handle.index << ":" << handle.generation
            << " initial=" << cells.size() << " affected=" << affected.size() << " full=" << full
            << " feedback=" << feedbackCount << " directed=" << directedCount
            << " identity=" << disabled.size()-directedCount);
    });
}
void VansVFXActionService::Tick(double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds<0) return;
    std::vector<VansGenerationHandle> completed,fields;
    m_Effects.ForEach([&](auto handle,Effect& effect) { if (effect.field && effect.instance.IsValid()) fields.push_back(handle); });
    std::uint32_t queryBudget=64;
    for (size_t i=0;i<fields.size();++i)
        if (auto* effect=m_Effects.Resolve(fields[(i+m_FieldCursor)%fields.size()]))
        {
            const auto handle=fields[(i+m_FieldCursor)%fields.size()];
            ExtendSurfaceField(*effect,static_cast<float>(deltaSeconds),queryBudget);
            PublishSurfaceField(handle,*effect);
            NotifySurfaceFieldStopped(handle,*effect);
        }
    ++m_FieldCursor;
    std::vector<VansGenerationHandle> volumes;
    m_Effects.ForEach([&](auto handle,Effect& effect) {
        if (!effect.volume || !effect.instance.IsValid()) return;
        auto& volume=*effect.volume; volume.age+=static_cast<float>(deltaSeconds);
        if (volume.age+1.e-6f<volume.nextSample) return;
        volume.nextSample=volume.age+volume.period;
        volumes.push_back(handle);
    });
    if (!volumes.empty()) SampleExtinguishingVolumes(volumes);
    m_Effects.ForEach([&](auto handle,Effect& effect) {
        if (effect.justSpawned) { effect.justSpawned = false; return; }
        if (effect.instance.IsValid() && m_Backend.finished && m_Backend.finished(effect.instance))
        {
            if (!m_Backend.destroy || !m_Backend.destroy(effect.instance)) return;
            effect.instance = {};
            if (effect.field) { effect.field->stopped=true; PublishSurfaceField(handle,effect); NotifySurfaceFieldStopped(handle,effect); }
        }
        if (!effect.instance.IsValid() && m_Runtime.ForgetCompletedWorldResource(Capability().service,handle))
            completed.push_back(handle);
    });
    for (auto handle : completed) {
        m_Backend.surfaceFields->Remove(handle);
        if (!m_Effects.Release(handle)) VANS_LOG_ERROR("[VFX] Completed resource release was rejected");
    }
}
}
