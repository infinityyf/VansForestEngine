#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/VansSceneNavigationComponentReader.h"
#include "../../NavigationCore/VansNavigationSourceSnapshot.h"
#include "../../NavigationCore/Serialization/VansNavigationSettingsJsonCodec.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../../RenderCore/VansScene.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../AuthoringCore/Navigation/VansNavigationAuthoringSession.h"
#include "../../AuthoringCore/Navigation/VansNavigationBakePublication.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../SceneCore/VansSceneNavigationGeometry.h"
#include "../../PhysicsCore/VansPhysicsCollisionGeometry.h"
#include <algorithm>
#include <set>
#include <variant>
#include <type_traits>

namespace Vans::EditorAPI
{
namespace
{
Vec3 NavigationDTO(const glm::vec3& p) { return {p.x,p.y,p.z}; }
glm::vec3 NavigationPosition(const Vec3& p) { return {p.x,p.y,p.z}; }
NavigationPreviewQueryKind PreviewKind(VansNavigationQueryKind kind)
{
    switch(kind)
    {
    case VansNavigationQueryKind::Path: return NavigationPreviewQueryKind::Path;
    case VansNavigationQueryKind::PointProjection: return NavigationPreviewQueryKind::PointProjection;
    case VansNavigationQueryKind::Raycast: return NavigationPreviewQueryKind::Raycast;
    case VansNavigationQueryKind::NearestBoundary: return NavigationPreviewQueryKind::NearestBoundary;
    case VansNavigationQueryKind::ReachablePoint: return NavigationPreviewQueryKind::ReachablePoint;
    }
    return NavigationPreviewQueryKind::Path;
}
}
bool EngineAPIImpl::ApplyNavigationComponentPreview(const std::vector<RuntimeNavigationComponentEdit>& edits)
{
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    auto* runtime=scene?scene->GetRuntimeWorld():nullptr;
    if(m_PlayState!=EnginePlayState::Edit || !runtime) return false;
    using Payload=std::variant<VansRuntimeNavigationSurfaceComponent,VansRuntimeNavigationSourceComponent,
        VansRuntimeNavigationModifierComponent,VansRuntimeNavigationAgentComponent,VansRuntimeNavigationObstacleComponent,VansRuntimeNavigationLinkComponent>;
    struct Pending { VansComponentHandle handle; Payload value; };
    std::vector<Pending> pending;
    std::set<std::string> identities;
    const auto decode=[&](const auto& edit,auto value,std::uint16_t typeId)
    {
        std::string error;
        const auto handle=runtime->FindComponentByGuid(edit.componentGuid,typeId);
        const auto* header=runtime->GetComponentHeader(handle);
        const auto* owner=header?runtime->Entities().Get(header->owner):nullptr;
        const auto* storage=runtime->FindStorage<std::decay_t<decltype(value)>>(typeId);
        if(!owner || owner->stableGuid!=edit.entityGuid || !storage || !storage->Get(handle) ||
            !identities.insert(edit.componentGuid).second || !ValidateNavigationComponent(edit.componentType,edit.data).valid ||
            !VansSceneNavigationComponentReader::Read(edit.data,value,error)) return false;
        pending.push_back({handle,std::move(value)}); return true;
    };
    // 所有值及身份先校验；任一编辑不能预览时，由现有编辑器场景重建消费作者文档。
    for(const auto& edit:edits)
    {
        bool valid=false;
        if(edit.componentType=="NavigationSurface") valid=decode(edit,VansRuntimeNavigationSurfaceComponent{},VansRuntimeComponentType_NavigationSurface);
        else if(edit.componentType=="NavigationSource") valid=decode(edit,VansRuntimeNavigationSourceComponent{},VansRuntimeComponentType_NavigationSource);
        else if(edit.componentType=="NavigationModifier") valid=decode(edit,VansRuntimeNavigationModifierComponent{},VansRuntimeComponentType_NavigationModifier);
        else if(edit.componentType=="NavigationObstacle") valid=decode(edit,VansRuntimeNavigationObstacleComponent{},VansRuntimeComponentType_NavigationObstacle);
        else if(edit.componentType=="NavigationLink") valid=decode(edit,VansRuntimeNavigationLinkComponent{},VansRuntimeComponentType_NavigationLink);
        else if(edit.componentType=="NavigationAgent") valid=decode(edit,VansRuntimeNavigationAgentComponent{},VansRuntimeComponentType_NavigationAgent);
        if(!valid) return false;
    }
    for(auto& item:pending) std::visit([&](auto& value)
    {
        auto* storage=runtime->FindStorage<std::decay_t<decltype(value)>>(item.handle.typeId);
        using std::swap; swap(*storage->Get(item.handle),value);
    },item.value);
    return true;
}

NavigationComponentValidation EngineAPIImpl::BuildNavigationPreview(const std::string& mapId)
{
    NavigationComponentValidation result;
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    const auto* document=m_SceneAuthoringHost?m_SceneAuthoringHost->SceneDocument():nullptr;
    const auto& project=VansProjectManager::Get();
    if(m_PlayState!=EnginePlayState::Edit || !project.IsProjectLoaded() || !scene || !scene->IsSceneReady() || !document)
    { result.error="Open an editable scene before building navigation preview"; return result; }
    if(m_NavigationAuthoringSession && m_NavigationAuthoringSession->Capture().busy)
    { result.error="A navigation preview build is still in flight"; return result; }
    const auto sourceScene=document->SourcePath().lexically_relative(project.GetProjectRootPath()).generic_string();
    if(!ValidateNavigationSourceIdentity(sourceScene,result.error)) return result;
    const auto settings=project.GetProjectSettings().GetNavigationSettings();
    if(!FindNavigationMap(settings,mapId)) { result.error="Navigation preview Map is unavailable: "+mapId; return result; }
    const auto authoring=document->SerializedRootSnapshot();
    const auto* entities=FindObjectField(authoring,"entities");
    std::vector<RuntimeNavigationComponentEdit> navigationEdits;
    std::vector<RuntimeComponentEnabledEdit> enabledEdits;
    if(entities) for(const auto& entity:entities->arrayItems)
        if(const auto* components=FindObjectField(entity,"components")) for(const auto& component:components->arrayItems)
        {
            const auto type=ReadSerializedStringField(component,"type");
            if(type!="NavigationSurface" && type!="NavigationSource" && type!="NavigationModifier" && type!="NavigationAgent" && type!="NavigationObstacle" && type!="NavigationLink") continue;
            const auto* data=FindObjectField(component,"data");
            const auto* enabled=FindObjectField(component,"enabled");
            if(!data || !enabled || enabled->kind!=VansSerializedValue::Kind::Bool)
            { result.error="Navigation authoring component is incomplete"; return result; }
            const auto entityGuid=ReadSerializedStringField(entity,"id"),componentGuid=ReadSerializedStringField(component,"id");
            navigationEdits.push_back({entityGuid,componentGuid,type,*data});
            enabledEdits.push_back({entityGuid,componentGuid,type,enabled->boolValue});
        }
    // 显式构建也消费当前作者文档，保证外部命令/Undo 的数据未遗漏 Inspector 的实时投影。
    if(!ApplyNavigationComponentPreview(navigationEdits))
    { result.error="Navigation authoring components differ from the Scene preview; rebuild the Scene preview before navigation"; return result; }
    for(const auto& edit:enabledEdits)
        if(!SetRuntimeComponentEnabled(edit.entityGuid,edit.componentGuid,edit.componentType,edit.enabled))
        { result.error="Navigation authoring enable state could not be projected"; return result; }
    VansNavigationSourceSnapshot sources; std::uint64_t observation=0;
    if(!scene->CaptureNavigationSources(settings,sources,observation,result.error)) return result;
    if(!m_NavigationAuthoringSession) m_NavigationAuthoringSession=std::make_unique<VansNavigationAuthoringSession>();
    result.valid=m_NavigationAuthoringSession->BuildPreview(std::move(sources),settings,mapId,sourceScene,observation,result.error);
    if(result.valid)
    {
        m_NavigationPreviewDocument=document; m_NavigationPreviewSceneState=document->CurrentStateId();
        m_NavigationPreviewProjectRoot=project.GetProjectRootPath();
    }
    return result;
}

NavigationBuildPreviewSnapshot EngineAPIImpl::GetNavigationBuildPreview()
{
    NavigationBuildPreviewSnapshot result;
    const auto& project=VansProjectManager::Get();
    const auto* scene=static_cast<const VansGraphics::VansScene*>(m_Scene);
    const auto* document=m_SceneAuthoringHost?m_SceneAuthoringHost->SceneDocument():nullptr;
    result.available=project.IsProjectLoaded() && scene && scene->IsSceneReady() && document;
    result.editable=result.available && m_PlayState==EnginePlayState::Edit;
    if(!m_NavigationAuthoringSession) return result;
    const auto observation=scene?scene->GetNavigationSourceObservation():std::optional<std::uint64_t>{};
    if(!result.editable || document!=m_NavigationPreviewDocument || !document ||
        document->CurrentStateId()!=m_NavigationPreviewSceneState || project.GetProjectRootPath()!=m_NavigationPreviewProjectRoot ||
        !observation || !m_NavigationAuthoringSession->MatchesSources(project.GetProjectSettings().GetNavigationSettings(),*observation))
        m_NavigationAuthoringSession->Invalidate("Navigation preview sources or authoring context changed; build a new preview");
    const auto snapshot=m_NavigationAuthoringSession->Capture();
    switch(snapshot.state)
    {
    case VansNavigationPreviewState::Empty: result.state="Empty"; break;
    case VansNavigationPreviewState::Building: result.state="Building"; break;
    case VansNavigationPreviewState::Ready: result.state="Ready"; break;
    case VansNavigationPreviewState::Failed: result.state="Failed"; break;
    case VansNavigationPreviewState::Cancelling: result.state="Cancelling"; break;
    case VansNavigationPreviewState::Cancelled: result.state="Cancelled"; break;
    case VansNavigationPreviewState::Stale: result.state="Stale"; break;
    }
    result.busy=snapshot.busy; result.ready=snapshot.state==VansNavigationPreviewState::Ready;
    result.requestId=snapshot.requestId; result.mapId=snapshot.mapId; result.diagnostic=snapshot.diagnostic;
    result.capturedSources=snapshot.sourceCount; result.capturedSurfaces=snapshot.surfaceCount;
    for(const auto& excluded:snapshot.exclusions) result.exclusions.push_back({excluded.id,excluded.reason,excluded.instanceCount});
    result.elapsedMilliseconds=snapshot.elapsedMilliseconds;
    if(snapshot.asset)
    {
        result.payloadBytes=snapshot.asset->GetPayloadBytes(); result.residentBytes=snapshot.asset->EstimateResidentBytes();
        for(const auto& profile:snapshot.asset->GetProfiles()) result.profiles.push_back({profile.profileId,profile.tiles.size()});
        for(const auto& filter:project.GetProjectSettings().GetNavigationSettings().queryFilters) result.filters.push_back(filter.id);
        const auto* surfaces=scene->GetRuntimeWorld()->FindStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface);
        std::set<std::string> guids;
        if(surfaces) for(const auto& surface:surfaces->DenseData())
            if(surface.mapId==snapshot.mapId) guids.insert(surface.navigationMeshGuid);
        if(guids.size()==1)
        {
            VansAssetGuid guid;
            if(VansAssetGuid::TryParse(*guids.begin(),guid))
                if(const auto record=project.FindAssetRecord(guid))
                    result.suggestedOutputPath=record->sourcePath.lexically_relative(project.GetProjectRootPath()).generic_string();
        }
    }
    return result;
}

void EngineAPIImpl::CancelNavigationPreview()
{
    if(m_NavigationAuthoringSession) m_NavigationAuthoringSession->Cancel();
}

void EngineAPIImpl::TickNavigationAuthoringPreview()
{
    if(!m_NavigationAuthoringSession) return;
    GetNavigationBuildPreview();
    m_NavigationAuthoringSession->Update();
}

NavigationPreviewGeometry EngineAPIImpl::GetNavigationPreviewGeometry(const NavigationPreviewView& view)
try
{
    NavigationPreviewGeometry result;
    const auto preview=GetNavigationBuildPreview();
    if(!preview.ready || preview.requestId!=view.previewId)
    { result.diagnostic="Navigation preview is unavailable or changed"; return result; }
    VansNavigationDebugView domain;
    for(std::size_t i=0;i<domain.planes.size();++i)
    { const auto& p=view.planes[i]; domain.planes[i]=glm::vec4(p.x,p.y,p.z,p.w); }
    domain.maximumTiles=view.maximumTiles; domain.maximumTriangles=view.maximumTriangles;
    domain.maximumLinks=view.maximumLinks;
    VansNavigationDebugGeometry geometry;
    if(!m_NavigationAuthoringSession->CaptureDebugGeometry(domain,view.profileId,view.filterId,geometry,result.diagnostic)) return result;
    result.triangles.reserve(geometry.triangles.size()); result.tileCount=geometry.tileCount;
    for(const auto& triangle:geometry.triangles)
    {
        NavigationPreviewTriangle value; value.area=triangle.area; value.traversable=triangle.traversable;
        value.tileEdges=triangle.tileEdges;
        for(int corner=0;corner<3;++corner) value.positions[corner]=NavigationDTO(triangle.positions[corner]);
        result.triangles.push_back(value);
    }
    result.links.reserve(geometry.links.size());
    for(const auto& link:geometry.links)
    {
        NavigationPreviewLink value; value.identity=link.key.value; value.radiusMeters=link.radiusMeters;
        value.bidirectional=link.bidirectional; value.areaAllowed=link.areaAllowed;
        for(int i=0;i<2;++i) value.positions[i]=NavigationDTO(link.positions[i]);
        result.links.push_back(value);
    }
    result.available=true; result.limited=geometry.limited;
    return result;
}
catch(const std::bad_alloc&) { NavigationPreviewGeometry result; result.diagnostic="Navigation viewport snapshot allocation failed"; return result; }

NavigationComponentValidation EngineAPIImpl::SubmitNavigationPreviewQuery(const NavigationPreviewQueryRequest& request)
try
{
    NavigationComponentValidation result;
    const auto preview=GetNavigationBuildPreview();
    if(!preview.ready || preview.requestId!=request.previewId)
    { result.error="Navigation preview changed; select points on a current candidate"; return result; }
    VansNavigationQueryRequest domain;
    switch(request.kind)
    {
    case NavigationPreviewQueryKind::Path: domain.kind=VansNavigationQueryKind::Path; break;
    case NavigationPreviewQueryKind::PointProjection: domain.kind=VansNavigationQueryKind::PointProjection; break;
    case NavigationPreviewQueryKind::Raycast: domain.kind=VansNavigationQueryKind::Raycast; break;
    case NavigationPreviewQueryKind::NearestBoundary: domain.kind=VansNavigationQueryKind::NearestBoundary; break;
    case NavigationPreviewQueryKind::ReachablePoint: domain.kind=VansNavigationQueryKind::ReachablePoint; break;
    default: result.error="Navigation preview query kind is invalid"; return result;
    }
    domain.mapId=preview.mapId; domain.profileId=request.profileId; domain.filterId=request.filterId;
    domain.start=NavigationPosition(request.start); domain.goal=NavigationPosition(request.goal);
    if(domain.kind==VansNavigationQueryKind::ReachablePoint) domain.goal=domain.start;
    domain.nearestExtents=NavigationPosition(request.nearestExtents);
    domain.radius=request.radius; domain.seed=request.seed; domain.maximumAttempts=request.maximumAttempts;
    domain.goalAcceptance=request.requireRequestedGoal?VansNavigationGoalAcceptance::RequestedPoint:VansNavigationGoalAcceptance::ProjectedPoint;
    domain.goalProjectionToleranceMeters=request.goalProjectionToleranceMeters;
    result.valid=m_NavigationAuthoringSession->SubmitQuery(domain,result.error); return result;
}
catch(const std::bad_alloc&) { NavigationComponentValidation result; result.error="Navigation preview query allocation failed"; return result; }

NavigationPreviewQuerySnapshot EngineAPIImpl::GetNavigationPreviewQuery()
try
{
    NavigationPreviewQuerySnapshot result;
    const auto preview=GetNavigationBuildPreview();
    if(!preview.ready) { result.diagnostic=preview.diagnostic; return result; }
    const auto snapshot=m_NavigationAuthoringSession->CaptureQuery(result.diagnostic);
    if(!snapshot) return result;
    result.available=true; result.previewId=preview.requestId; result.queryId=snapshot->handle.serial;
    result.kind=PreviewKind(snapshot->kind); result.profileId=snapshot->profileId; result.filterId=snapshot->filterId;
    switch(snapshot->lifecycle)
    {
    case VansNavigationQueryLifecycle::Queued: result.state="Queued"; result.busy=true; break;
    case VansNavigationQueryLifecycle::Running: result.state="Running"; result.busy=true; break;
    case VansNavigationQueryLifecycle::Finished: result.state="Finished"; break;
    case VansNavigationQueryLifecycle::Cancelled: result.state="Cancelled"; break;
    case VansNavigationQueryLifecycle::WaitingForData: result.state="WaitingForData"; result.busy=true; break;
    }
    const auto& path=snapshot->path;
    result.requestedStart=NavigationDTO(path.requestedStart); result.requestedEnd=NavigationDTO(path.requestedEnd);
    result.failure=NavigationPathFailureName(path.failure); result.diagnostic=path.diagnostic;
    result.iterations=snapshot->iterations; result.attempts=snapshot->attempts; result.tileCount=path.tiles.size();
    switch(path.status)
    {
    case VansNavigationPathStatus::None: result.result=result.busy?"Pending":"Finished"; break;
    case VansNavigationPathStatus::Complete: result.result="Complete"; break;
    case VansNavigationPathStatus::Partial: result.result="Partial"; break;
    case VansNavigationPathStatus::Failed: result.result="Failed"; break;
    }
    if(path.hasProjection)
    {
        result.hasProjection=true;
        result.projectedStart=NavigationDTO(path.projectedStart); result.projectedEnd=NavigationDTO(path.projectedEnd);
        result.startProjectionDistance=path.startProjectionDistance; result.endProjectionDistance=path.endProjectionDistance;
    }
    if(!path.points.empty())
    {
        result.hasResult=true; result.reachedEnd=NavigationDTO(path.reachedEnd);
        for(const auto& point:path.points) result.points.push_back(NavigationDTO(point));
    }
    if(snapshot->point)
    {
        result.hasResult=true; result.reachedEnd=NavigationDTO(*snapshot->point);
        result.result=snapshot->kind==VansNavigationQueryKind::ReachablePoint?"ReachablePoint":"Point";
        if(snapshot->kind==VansNavigationQueryKind::PointProjection)
        {
            result.hasProjection=true; result.projectedStart=result.reachedEnd;
            result.startProjectionDistance=snapshot->projectionDistance;
        }
    }
    if(snapshot->raycast && snapshot->raycast->valid)
    {
        const auto& ray=*snapshot->raycast;
        result.hasProjection=true; result.hasResult=true; result.hit=ray.hit; result.result=ray.hit?"Hit":"Clear";
        result.projectedStart=NavigationDTO(ray.projectedStart); result.reachedEnd=NavigationDTO(ray.position);
        result.normal=NavigationDTO(ray.normal); result.startProjectionDistance=ray.projectionDistance;
        result.tileCount=ray.tiles.size(); result.hitFraction=ray.fraction;
    }
    if(snapshot->boundary && snapshot->boundary->valid)
    {
        const auto& boundary=*snapshot->boundary;
        result.hasProjection=true; result.hasResult=boundary.found; result.hit=boundary.found;
        result.result=boundary.found?"Boundary":"NoBoundary";
        result.projectedStart=NavigationDTO(boundary.projected); result.reachedEnd=NavigationDTO(boundary.position);
        result.normal=NavigationDTO(boundary.normal); result.startProjectionDistance=boundary.projectionDistance;
        result.boundaryDistanceMeters=boundary.distance;
    }
    return result;
}
catch(const std::bad_alloc&) { NavigationPreviewQuerySnapshot result; result.diagnostic="Navigation preview query snapshot allocation failed"; return result; }

void EngineAPIImpl::CancelNavigationPreviewQuery()
{
    if(m_NavigationAuthoringSession) m_NavigationAuthoringSession->CancelQuery();
}

NavigationComponentValidation EngineAPIImpl::BakeNavigationPreview(const std::string& output)
try
{
    NavigationComponentValidation result;
    if(!GetNavigationBuildPreview().ready) { result.error="Build a current navigation preview before Bake"; return result; }
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    auto& project=VansProjectManager::Get();
    auto* document=m_SceneAuthoringHost->SceneDocument();
    const auto snapshot=m_NavigationAuthoringSession->Capture();
    const auto settings=project.GetProjectSettings().GetNavigationSettings();
    // 显式写盘前重新冻结现场来源，不能依赖上一帧缓存的新鲜度判断。
    VansNavigationSourceSnapshot sources; std::uint64_t observation=0;
    if(!scene->CaptureNavigationSources(settings,sources,observation,result.error)) return result;
    if(!m_NavigationAuthoringSession->MatchesSources(settings,observation))
    { m_NavigationAuthoringSession->Invalidate("Navigation sources changed before Bake"); result.error="Navigation sources changed; build a new preview"; return result; }
    const std::filesystem::path relative(output);
    if(relative.empty() || relative.is_absolute()) { result.error="Navigation Bake requires a project-relative output path"; return result; }
    auto* database=project.GetAssetDatabase();
    if(!database) { result.error="Navigation Bake asset index is unavailable"; return result; }
    auto publication=VansNavigationBakePublication::Prepare(*database,project.GetAssetObjectRepository(),snapshot.asset,
        std::filesystem::path(project.GetProjectRootPath())/relative,result.error);
    if(!publication) return result;
    const auto reference=VansSerializedValue::Object({{"guid",VansSerializedValue::String(publication->Record().guid.ToString())}});
    const auto root=document->SerializedRootSnapshot();
    const auto* entities=FindObjectField(root,"entities");
    if(!entities || entities->kind!=VansSerializedValue::Kind::Array) { result.error="Navigation Bake Scene entities are unavailable"; return result; }
    std::vector<VansSceneAuthoringValue> values;
    std::vector<RuntimeNavigationComponentEdit> previews;
    std::vector<std::pair<VansComponentHandle,VansRuntimeNavigationSurfaceComponent>> previous;
    auto* runtime=scene->GetRuntimeWorld();
    auto* storage=runtime->FindStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface);
    for(std::size_t entityIndex=0;entityIndex<entities->arrayItems.size();++entityIndex)
    {
        const auto& entity=entities->arrayItems[entityIndex];
        const auto* components=FindObjectField(entity,"components");
        if(!components || components->kind!=VansSerializedValue::Kind::Array) continue;
        for(std::size_t componentIndex=0;componentIndex<components->arrayItems.size();++componentIndex)
        {
            const auto& component=components->arrayItems[componentIndex];
            const auto* data=FindObjectField(component,"data");
            if(ReadSerializedStringField(component,"type")!="NavigationSurface" || !data || ReadSerializedStringField(*data,"mapId")!=snapshot.mapId) continue;
            const auto componentGuid=ReadSerializedStringField(component,"id");
            const auto handle=runtime->FindComponentByGuid(componentGuid,VansRuntimeComponentType_NavigationSurface);
            const auto* header=runtime->GetComponentHeader(handle);
            const auto* owner=header?runtime->Entities().Get(header->owner):nullptr;
            const auto* current=storage?storage->Get(handle):nullptr;
            if(!owner || owner->stableGuid!=ReadSerializedStringField(entity,"id") || !current)
            { result.error="Navigation Surface authoring identity differs from the current Scene preview; reload the Scene"; return result; }
            auto updated=*data; SetSerializedObjectField(updated,"navigationMesh",reference);
            if(!ValidateNavigationComponent("NavigationSurface",updated).valid)
            { result.error="Navigation Bake Surface no longer matches current directories or sources"; return result; }
            previews.push_back({owner->stableGuid,componentGuid,"NavigationSurface",std::move(updated)});
            previous.emplace_back(handle,*current);
            values.push_back({{DocumentPropertySpace::Scene,"/entities/"+std::to_string(entityIndex)+"/components/"+
                std::to_string(componentIndex)+"/data/navigationMesh"},reference});
        }
    }
    if(values.empty()) { result.error="Navigation Bake has no authored Surface for the selected Map"; return result; }
    const auto committed=m_SceneAuthoringHost->CommitSceneValues(std::move(values),m_NavigationPreviewSceneState,[&](std::string& error)
    {
        if(!ApplyNavigationComponentPreview(previews)) { error="Navigation Bake could not project its Surface bindings"; return false; }
        if(publication->Commit(error)) return true;
        for(auto& [handle,value]:previous) { using std::swap; swap(*storage->Get(handle),value); }
        return false;
    });
    result.valid=committed.success; result.error=committed.message;
    if(result.valid) scene->PrepareNavigation();
    return result;
}
catch(const std::exception& error) { return {false,error.what()}; }

NavigationSettingsSnapshot EngineAPIImpl::GetNavigationSettings() const
{
    const auto& project=VansProjectManager::Get();
    NavigationSettingsSnapshot result;
    if(!project.IsProjectLoaded()) return result;
    result.available=true; result.editable=m_PlayState==EnginePlayState::Edit;
    result.projectRoot=project.GetProjectRootPath();
    result.document=VansNavigationSettingsJsonCodec::Encode(project.GetProjectSettings().GetNavigationSettings());
    const auto defaults=VansNavigationSettingsJsonCodec::Encode(VansNavigationSettings{});
    const auto add=[&](const char* path,const VansSerializedValue& element,std::size_t maximum)
    { result.arrays.push_back({path,element,maximum}); };
    add("profiles",FindObjectField(defaults,"profiles")->arrayItems.front(),kMaximumNavigationProfiles);
    add("maps",FindObjectField(defaults,"maps")->arrayItems.front(),kMaximumNavigationMaps);
    add("maps/profiles",FindObjectField(FindObjectField(defaults,"maps")->arrayItems.front(),"profiles")->arrayItems.front(),kMaximumNavigationProfiles);
    add("queryFilters",FindObjectField(defaults,"queryFilters")->arrayItems.front(),kMaximumNavigationQueryFilters);
    add("queryFilters/excludedAreas",*FindObjectField(*FindObjectField(defaults,"areas"),"default"),kMaximumNavigationAreaId+1u);
    add("areas/definitions",FindObjectField(*FindObjectField(defaults,"areas"),"definitions")->arrayItems.front(),kMaximumNavigationAreaId+1u);
    NavigationSettingsChoiceField modes; modes.path="maps/updateMode";
    for(const auto mode:{VansNavigationMapUpdateMode::Baked,VansNavigationMapUpdateMode::Runtime})
    {
        const auto* name=NavigationMapUpdateModeName(mode);
        modes.choices.push_back({VansSerializedValue::String(name),name});
    }
    result.choiceFields.push_back(std::move(modes));
    if(const auto* scene=static_cast<const VansGraphics::VansScene*>(m_Scene)) result.runtimeDiagnostic=scene->GetNavigationSynchronizationError();
    return result;
}

NavigationComponentValidation EngineAPIImpl::ValidateNavigationSettingsDocument(const VansSerializedValue& document) const
{
    NavigationComponentValidation result; VansNavigationSettings settings;
    result.valid=VansNavigationSettingsJsonCodec::Decode(document,settings,result.error); return result;
}

NavigationComponentValidation EngineAPIImpl::ApplyNavigationSettings(const VansSerializedValue& document,
    const VansSerializedValue& expectedDocument)
{
    NavigationComponentValidation result;
    if(m_PlayState!=EnginePlayState::Edit) { result.error="Navigation project settings can only be applied in Edit mode"; return result; }
    VansNavigationSettings settings,expected;
    if(!VansNavigationSettingsJsonCodec::Decode(document,settings,result.error) ||
        !VansNavigationSettingsJsonCodec::Decode(expectedDocument,expected,result.error)) return result;
    if(!SameNavigationSettings(expected,VansProjectManager::Get().GetProjectSettings().GetNavigationSettings()))
    { result.error="Navigation project settings changed after this draft was opened; reload the current settings"; return result; }
    result.valid=VansProjectManager::Get().SetNavigationSettings(settings,result.error);
    if(result.valid) if(auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene)) scene->PrepareNavigation();
    return result;
}

NavigationAuthoringSchema EngineAPIImpl::GetNavigationAuthoringSchema() const
{
    const auto& settings=VansProjectManager::Get().GetProjectSettings().GetNavigationSettings();
    NavigationAuthoringSchema result;
    std::vector<std::string> maps,filters,layers,areas;
    for(const auto& item:settings.maps) maps.push_back(item.id);
    for(const auto& item:settings.queryFilters) filters.push_back(item.id);
    for(const auto& item:settings.areas.definitions) areas.push_back(item.name);
    const auto& collision=VansEngine::VansCollisionLayerManager::Get();
    for(int i=0;i<collision.GetLayerCount();++i) layers.push_back(collision.GetLayerName(i));
    const auto add=[&](const char* type,const char* field,const std::vector<std::string>& choices,VansSerializedValue element={},std::string mapId={})
    {
        NavigationAuthoringField item; item.componentType=type; item.field=field; item.mapId=std::move(mapId); item.arrayElement=std::move(element);
        for(const auto& choice:choices) item.choices.push_back({VansSerializedValue::String(choice),choice});
        result.fields.push_back(std::move(item));
    };
    for(const auto* type:{"NavigationSurface","NavigationModifier","NavigationAgent","NavigationObstacle","NavigationLink"})
    {
        add(type,"mapId",maps);
        for(const auto& map:settings.maps)
            add(type,type==std::string("NavigationAgent")?"profileId":"profiles",map.profiles,{},map.id);
    }
    add("NavigationAgent","queryFilter",filters);
    add("NavigationSurface","collection",{"All","Hierarchy","ExplicitSources"});
    add("NavigationSurface","collisionLayers",layers);
    NavigationAuthoringField sources; sources.componentType="NavigationSurface"; sources.field="sources";
    const auto* scene=static_cast<const VansGraphics::VansScene*>(m_Scene);
    if(scene) result.runtimeDiagnostic=scene->GetNavigationSynchronizationError();
    result.obstacleCapabilities="Carve/Avoidance/Both: upright Box/Cylinder; Collider binding: actual Box only. Carve requires a Runtime Map; Avoidance also supports Baked Maps. Obstacle capacity "+
        std::to_string(kMaximumNavigationObstacles)+", avoidance cells/body "+std::to_string(settings.avoidance.maximumCellsPerBody)+
        ", grid entries "+std::to_string(settings.avoidance.maximumGridEntries)+
        ", neighbor scans "+std::to_string(settings.avoidance.maximumNeighborScans)+
        ", selected neighbors "+std::to_string(settings.avoidance.maximumNeighbors)+". Tilted or unsupported shapes are rejected.";
    const auto* runtime=scene?scene->GetRuntimeWorld():nullptr;
    const auto* sourceStorage=runtime?runtime->FindStorage<VansRuntimeNavigationSourceComponent>(VansRuntimeComponentType_NavigationSource):nullptr;
    if(sourceStorage) for(const auto& header:sourceStorage->Headers())
        if(const auto* owner=runtime->Entities().Get(header.owner)) sources.choices.push_back({
            MakeSerializedSceneComponentObjectReference(owner->stableGuid,header.stableGuid,"NavigationSource"),
            owner->name+" / "+header.stableGuid.substr(0,8)});
    std::sort(sources.choices.begin(),sources.choices.end(),[](const auto& a,const auto& b){return a.label<b.label;});
    result.fields.push_back(std::move(sources));
    add("NavigationSurface","geometryMode",{"Collision"});
    add("NavigationSource","mode",{"Include","Exclude","Area"});
    add("NavigationSource","area",areas);
    add("NavigationModifier","area",areas);
    add("NavigationLink","area",areas);
    add("NavigationModifier","shape",{"Box","Convex"});
    NavigationAuthoringField obstacleModes; obstacleModes.componentType="NavigationObstacle"; obstacleModes.field="mode";
    for(const auto& entry:kNavigationObstacleModes)
    { const std::string name(entry.second); obstacleModes.choices.push_back({VansSerializedValue::String(name),name}); }
    result.fields.push_back(std::move(obstacleModes));
    NavigationAuthoringField obstacleShapes; obstacleShapes.componentType="NavigationObstacle"; obstacleShapes.field="shape";
    std::set<std::string> shapeFields;
    for(const auto* shape:{"Box","Cylinder","Collider"})
    {
        auto fields=VansSceneNavigationComponentReader::CreateObstacleShapeData(shape);
        for(const auto& [key,value]:fields.objectFields) shapeFields.insert(key);
        obstacleShapes.choices.push_back({VansSerializedValue::String(shape),shape,std::move(fields)});
    }
    obstacleShapes.replacedFields.assign(shapeFields.begin(),shapeFields.end());
    result.fields.push_back(std::move(obstacleShapes));
    NavigationAuthoringField colliders; colliders.componentType="NavigationObstacle"; colliders.field="collider";
    const auto* physics=runtime?runtime->FindStorage<VansRuntimePhysicsComponent>(VansRuntimeComponentType_Physics):nullptr;
    if(physics) for(std::size_t index=0;index<physics->Headers().size();++index)
    {
        const auto& header=physics->Headers()[index]; const auto* owner=runtime->Entities().Get(header.owner);
        const auto* node=physics->DenseData()[index].physicsNode;
        if(owner && node && node->GetProperties().colliderType==VansEngine::PhysicsColliderType::Box)
            colliders.choices.push_back({MakeSerializedSceneComponentObjectReference(owner->stableGuid,header.stableGuid,"Physics"),
                owner->name+" / Physics / "+header.stableGuid});
    }
    result.fields.push_back(std::move(colliders));
    add("NavigationModifier","convexVerticesMeters",{},VansSerializedValue::Array({VansSerializedValue::Float(0),VansSerializedValue::Float(0)}));
    return result;
}

NavigationComponentValidation EngineAPIImpl::ValidateNavigationComponent(const std::string& type,const VansSerializedValue& data) const
{
    const auto& settings=VansProjectManager::Get().GetProjectSettings().GetNavigationSettings();
    NavigationComponentValidation result;
    const auto references=[&](const std::string& mapId,const std::vector<std::string>& profiles)
    {
        const auto* map=FindNavigationMap(settings,mapId);
        if(!map) { result.error="Navigation component references an unavailable Map: "+mapId; return false; }
        for(const auto& profile:profiles)
            if(!FindNavigationProfile(settings,profile) || std::find(map->profiles.begin(),map->profiles.end(),profile)==map->profiles.end())
            { result.error="Navigation Profile is unavailable in the selected Map: "+profile; return false; }
        return true;
    };
    if(type=="NavigationSurface")
    {
        VansRuntimeNavigationSurfaceComponent value;
        if(!VansSceneNavigationComponentReader::Read(data,value,result.error) || !references(value.mapId,value.profiles)) return result;
        const auto* scene=static_cast<const VansGraphics::VansScene*>(m_Scene);
        const auto* runtime=scene?scene->GetRuntimeWorld():nullptr;
        const auto* storage=runtime?runtime->FindStorage<VansRuntimeNavigationSourceComponent>(VansRuntimeComponentType_NavigationSource):nullptr;
        for(const auto& source:value.sources)
        {
            const auto* header=storage?storage->GetHeader(storage->FindByStableGuid(source.componentGuid)):nullptr;
            const auto* owner=header?runtime->Entities().Get(header->owner):nullptr;
            if(!owner || owner->stableGuid!=source.entityGuid)
            { result.error="Navigation Surface references an unavailable or incorrectly owned NavigationSource: "+source.componentGuid; return result; }
        }
        for(const auto& layer:value.collisionLayers)
        {
            int index=-1;
            if(!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(layer,index))
            { result.error="Navigation Surface references an unavailable collision layer: "+layer; return result; }
        }
        result.valid=true;
    }
    else if(type=="NavigationSource")
    {
        VansRuntimeNavigationSourceComponent value;
        if(!VansSceneNavigationComponentReader::Read(data,value,result.error)) return result;
        if(value.mode==VansNavigationSourceMode::Area && !FindNavigationAreaByName(settings.areas,value.area))
        { result.error="NavigationSource references an unavailable Area: "+value.area; return result; }
        result.valid=true;
    }
    else if(type=="NavigationModifier")
    {
        VansNavigationModifierRecord value; value.id="AuthoringModifier";
        if(!VansSceneNavigationComponentReader::Read(data,value.modifier,result.error)) return result;
        result.valid=ValidateNavigationModifierRecord(value,settings,value.modifier.mapId,result.error);
    }
    else if(type=="NavigationLink")
    {
        VansNavigationLinkRecord record; record.id="AuthoringLink";
        if(!VansSceneNavigationComponentReader::Read(data,record.link,result.error)) return result;
        VansNavigationLinkGeometry link; result.valid=FreezeNavigationLinkGeometry(record,settings,link,result.error);
    }
    else if(type=="NavigationObstacle")
    {
        VansRuntimeNavigationObstacleComponent value;
        if(!VansSceneNavigationComponentReader::Read(data,value,result.error) || !references(value.mapId,value.profiles)) return result;
        result.valid=!NavigationObstacleCarves(value.mode) || FindNavigationMap(settings,value.mapId)->updateMode==VansNavigationMapUpdateMode::Runtime;
        if(!result.valid) result.error="NavigationObstacle Carve requires a Runtime Map";
        if(result.valid) if(const auto* binding=std::get_if<VansNavigationObstacleCollider>(&value.shape))
        {
            const auto* scene=static_cast<const VansGraphics::VansScene*>(m_Scene);
            const auto* runtime=scene?scene->GetRuntimeWorld():nullptr;
            const auto* storage=runtime?runtime->FindStorage<VansRuntimePhysicsComponent>(VansRuntimeComponentType_Physics):nullptr;
            const auto handle=storage?storage->FindByStableGuid(binding->componentGuid):VansComponentHandle{};
            const auto* header=storage?storage->GetHeader(handle):nullptr;
            const auto* owner=header?runtime->Entities().Get(header->owner):nullptr;
            const auto* component=storage?storage->Get(handle):nullptr;
            result.valid=owner && owner->stableGuid==binding->entityGuid && component && component->physicsNode;
            if(!result.valid) result.error="NavigationObstacle Collider binding is unavailable or incorrectly owned";
            else if(!header->effectiveEnabled || !component->physicsNode->IsEnabled())
            {
                result.valid=component->physicsNode->GetProperties().colliderType==VansEngine::PhysicsColliderType::Box;
                if(!result.valid) result.error="NavigationObstacle Collider binding requires a Box collider";
            }
            else
            {
                VansEngine::VansPhysicsBoxShapeSnapshot box; VansNavigationObstacleGeometry shape;
                result.valid=VansEngine::VansPhysicsCollisionGeometry::CaptureBoxShape(*component->physicsNode,box,result.error) &&
                    VansSceneNavigationGeometry::FreezeColliderBox(box,"AuthoringCollider",shape,result.error);
                if(!result.valid) result.error="NavigationObstacle Collider requires an upright Box: "+result.error;
            }
        }
    }
    else if(type=="NavigationAgent")
    {
        VansRuntimeNavigationAgentComponent value;
        if(!VansSceneNavigationComponentReader::Read(data,value,result.error) || !references(value.mapId,{value.profileId})) return result;
        result.valid=std::any_of(settings.queryFilters.begin(),settings.queryFilters.end(),[&](const auto& item){return item.id==value.queryFilter;});
        if(!result.valid) result.error="Navigation Agent references an unavailable Query Filter: "+value.queryFilter;
    }
    else result.error="Component is not a navigation authoring component";
    return result;
}
}
