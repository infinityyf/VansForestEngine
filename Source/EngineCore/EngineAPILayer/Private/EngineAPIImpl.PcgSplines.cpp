#include "EngineAPIImpl.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../PcgCore/VansPcgSplineField.h"
#include "../../PcgCore/VansPcgInfluence.h"
#include "../../PcgCore/VansPcgUpdatePlanner.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../PcgCore/Storage/VansPcgSplineFieldStorage.h"
#include "../../PcgCore/Serialization/VansPcgSplineAssetCodec.h"
#include "../../AuthoringCore/Pcg/VansPcgSplineAuthoringSession.h"
#include "../../AuthoringCore/VansAssetDocumentRegistry.h"
#include "../../AuthoringCore/VansAssetDocumentEditService.h"
#include "../../AuthoringCore/VansAuthoringAssetCreationService.h"
#include "../../AuthoringCore/Terrain/VansTerrainAuthoringSession.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansCamera.h"
#include "../../RenderCore/VansCameraControlArbiter.h"
#include "../../RenderCore/PcgCore/VansPcgSplinePreviewSession.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans::EditorAPI
{
namespace
{
PcgSplineItem ToPublic(const VansPcgSpline& source)
{
    auto resolved=source;
    VansPcgSplineEvaluator::ResolveAutoTangents(resolved);
    PcgSplineItem result;
    result.id=source.id;result.name=source.name;result.materialGuid=source.material.IsValid()?source.material.ToString():"";
    result.kind=static_cast<PcgSplineKind>(source.kind);result.enabled=source.enabled;result.locked=source.locked;
    result.roadDecalMaterialGuid=source.roadDecalMaterial.IsValid()?source.roadDecalMaterial.ToString():"";
    result.roadRenderMode=static_cast<PcgRoadRenderMode>(source.roadRenderMode);result.projectedDepth=source.projectedDepth;
    result.excludeVegetation=source.excludeVegetation;result.vegetationFade=source.vegetationFade;
    result.priority=source.priority;result.shoulder=source.shoulder;result.blendWidth=source.blendWidth;
    result.waterSurfaceDrop=source.waterSurfaceDrop;
    result.waterBlendWidthMeters=source.waterBlendWidthMeters;
    result.waterBlendStartMeters=source.waterBlendStartMeters;result.waterBlendEndMeters=source.waterBlendEndMeters;
    result.carveRiverbed=source.carveRiverbed;
    result.wetBankWidthMeters=source.wetBankWidthMeters;result.wetnessStrength=source.wetnessStrength;
    result.surfaceOffset=source.surfaceOffset;result.textureRepeat=source.textureRepeat;result.flowSign=static_cast<float>(source.flowSign);
    result.fadeInDistance=source.fadeInDistance;result.fadeOutDistance=source.fadeOutDistance;
	result.coordinateOffset=source.coordinateOffset;result.coordinateSign=source.coordinateSign;
	result.continuation=source.continuation;result.envelopeOffset=source.envelopeOffset;
	result.envelopeLength=source.envelopeLength;
    result.normalFlowEnabled=source.normalFlowEnabled;
    for (const auto& p:resolved.points)
    {
        PcgSplinePoint q;q.id=p.id;q.position=p.position;q.arrive=p.arrive;q.leave=p.leave;
        q.tangentMode=static_cast<PcgSplineTangentMode>(p.tangentMode);q.outgoing=static_cast<PcgSplineSegmentMode>(p.outgoing);
        q.leftWidth=p.leftWidth;q.rightWidth=p.rightWidth;q.linkedWidth=p.linkedWidth;
        q.bankAngleDegrees=p.bankAngleDegrees;q.depth=p.depth;q.speed=p.speed;result.points.push_back(q);
        result.points.back().bankSteepness=p.bankSteepness;
    }
    return result;
}
bool FromPublic(const PcgSplineItem& source,VansPcgSpline& result,std::string& error)
{
    if (source.kind!=static_cast<PcgSplineKind>(result.kind)) {error="Spline kind cannot change.";return false;}
    result.name=source.name;result.enabled=source.enabled;result.locked=source.locked;result.priority=source.priority;
    if (source.materialGuid.empty()) result.material={};
    else if (!VansAssetGuid::TryParse(source.materialGuid,result.material)) {error="Invalid road material reference.";return false;}
    if (source.roadDecalMaterialGuid.empty()) result.roadDecalMaterial={};
    else if (!VansAssetGuid::TryParse(source.roadDecalMaterialGuid,result.roadDecalMaterial)) {error="Invalid road decal material reference.";return false;}
    result.excludeVegetation=source.excludeVegetation;result.vegetationFade=source.vegetationFade;
    result.shoulder=source.shoulder;result.blendWidth=source.blendWidth;result.surfaceOffset=source.surfaceOffset;
    result.roadRenderMode=static_cast<VansPcgRoadRenderMode>(source.roadRenderMode);result.projectedDepth=source.projectedDepth;
    result.waterSurfaceDrop=source.waterSurfaceDrop;
    result.waterBlendWidthMeters=source.waterBlendWidthMeters;
    result.waterBlendStartMeters=source.waterBlendStartMeters;result.waterBlendEndMeters=source.waterBlendEndMeters;
    result.carveRiverbed=source.carveRiverbed;
    result.wetBankWidthMeters=source.wetBankWidthMeters;result.wetnessStrength=source.wetnessStrength;
    result.textureRepeat=source.textureRepeat;result.flowSign=source.flowSign<0?-1:1;
    result.fadeInDistance=source.fadeInDistance;result.fadeOutDistance=source.fadeOutDistance;
    result.normalFlowEnabled=source.normalFlowEnabled;
    const auto previousPoints=result.points;
    result.points.clear();
    for (const auto& p:source.points)
    {
        VansPcgSplinePoint q;q.id=p.id;q.position=p.position;q.arrive=p.arrive;q.leave=p.leave;
        q.tangentMode=static_cast<VansPcgSplineTangentMode>(p.tangentMode);q.outgoing=static_cast<VansPcgSplineSegmentMode>(p.outgoing);
        q.leftWidth=p.leftWidth;q.rightWidth=p.linkedWidth?p.leftWidth:p.rightWidth;q.linkedWidth=p.linkedWidth;
        q.bankAngleDegrees=p.bankAngleDegrees;q.depth=p.depth;q.bankSteepness=p.bankSteepness;q.speed=p.speed;result.points.push_back(q);
        auto& resolved=result.points.back();
        if (q.tangentMode==VansPcgSplineTangentMode::Aligned || q.tangentMode==VansPcgSplineTangentMode::Mirrored)
        {
            const auto old=std::find_if(previousPoints.begin(),previousPoints.end(),[&](const auto& item){return item.id==q.id;});
            const bool incoming=old!=previousPoints.end() && old->arrive!=q.arrive && old->leave==q.leave;
            const auto& handle=incoming?q.arrive:q.leave;
            auto& opposite=incoming?resolved.leave:resolved.arrive;
            const glm::vec3 direction(handle[0],handle[1],handle[2]);
            const float length=glm::length(direction);
            if (length>1e-6f)
            {
                const float otherLength=q.tangentMode==VansPcgSplineTangentMode::Mirrored?length:
                    glm::length(glm::vec3(opposite[0],opposite[1],opposite[2]));
                const auto value=-direction*(otherLength/length);opposite={value.x,value.y,value.z};
            }
        }
    }
    VansPcgSplineEvaluator::ResolveAutoTangents(result);
    return true;
}
std::string SceneSplineGuid(const VansSceneDocument* document)
{
    if (!document) return {};
    const auto snapshot=document->CreateSnapshot();
    if (const auto* settings=FindObjectField(snapshot.Root(),"settings"))
        if (const auto* value=FindObjectField(*settings,"pcgSplines"))
        {
            SerializedObjectReferenceValue reference;
            if (TryReadSerializedObjectReference(*value,reference)) return reference.guid;
        }
    return {};
}
}

void EngineAPIImpl::TickPcgSplineAuthoring()
{
    auto& manager=VansProjectManager::Get();
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    if (!m_PcgSplineAuthoring || !m_PcgSplineAuthoring->MatchesContext(
        manager.GetProjectRootPath(),m_SceneContentRevision))
    {
        m_PcgSplineAuthoring=std::make_unique<VansPcgSplineAuthoringSession>();
        m_PcgSplineAuthoring->ResetContext(manager.GetProjectRootPath(),m_SceneContentRevision);
        m_PcgSplinePreview=std::make_unique<VansGraphics::VansPcgSplinePreviewSession>();
    }
    auto& state=*m_PcgSplineAuthoring;
    auto* authoringDocument=m_PcgSceneDocument;
    const auto guid=authoringDocument?SceneSplineGuid(authoringDocument):
        (scene && scene->GetSplineAssetGuid().IsValid()?scene->GetSplineAssetGuid().ToString():"");
    VansAssetGuid unboundTerrain;
    if (guid.empty() && scene && scene->GetSplineFieldSnapshot())
        unboundTerrain=scene->GetSplineFieldSnapshot()->terrainGuid;
    if (state.BindAsset(guid,manager.GetAssetDatabase(),manager.GetAssetObjectRepository(),unboundTerrain))
    {
        VansAssetGuid assetGuid;VansAssetGuid::TryParse(guid,assetGuid);
        if (scene) scene->SetSplineAssetGuid(assetGuid);
    }
    state.SynchronizeDocument(manager.GetAssetObjectRepository(),m_PlayState==EnginePlayState::Edit);
    if (m_PlayState!=EnginePlayState::Edit) return;
    if (scene && scene->IsSceneReady() && m_PcgSplinePreview)
    {
        auto message=state.Message();
        m_PcgSplinePreview->Tick(*scene,m_PcgSceneRecipeGuid,manager.GetAssetObjectRepository(),
            state.RequestId(),state.Dragging(),message);
        state.SetMessage(std::move(message));
		const auto terrainPreviewNow=VansPcgTerrainPreviewScheduler::Clock::now();
		if (state.IsTerrainPreviewRequestDue(
			!m_PcgSplinePreview || m_PcgSplinePreview->IsBuilding(),terrainPreviewNow))
		{
			std::string terrainPreviewError;
			const auto coalescedHeightChanges=state.RequestPendingTerrainPreview(
				manager.GetAssetObjectRepository(),ResolvePcgSplineTerrainOverride(),
				m_PcgSplinePreview->IsBuilding(),terrainPreviewError,terrainPreviewNow);
			if (!terrainPreviewError.empty()) state.SetMessage(std::move(terrainPreviewError));
			if (coalescedHeightChanges)
				VANS_LOG("[PcgTerrainPreview] coalescedHeightChanges=" << coalescedHeightChanges <<
					" request=" << state.RequestId() << " minimumIntervalMs=" <<
					VansPcgTerrainPreviewScheduler::MinimumRequestInterval.count());
		}
        if (state.NeedsBuild() && !m_PcgSplinePreview->IsBuilding())
            m_PcgSplinePreview->QueueBuild(state.TakeBuildRequest(),scene->GetSplineFieldSnapshot());
    }
}

std::shared_ptr<const VansTerrainAsset> EngineAPIImpl::ResolvePcgSplineTerrainOverride() const
{
	if (!m_PcgSplineAuthoring || !m_TerrainAuthoringSession ||
		GetTerrainEditorSnapshot().assetGuid!=m_PcgSplineAuthoring->WorkingAsset().terrain.ToString())
		return {};
	const auto& repository=VansProjectManager::Get().GetAssetObjectRepository();
	const auto base=repository.ResolveLatest<VansTerrainAsset>(
		m_PcgSplineAuthoring->WorkingAsset().terrain);
	const auto& working=m_TerrainAuthoringSession->WorkingAsset();
	if (base && base->heights==working.heights &&
		base->settings.terrainSize==working.settings.terrainSize &&
		base->settings.maxHeight==working.settings.maxHeight &&
		base->settings.heightOffset==working.settings.heightOffset)
		return {};
	return std::make_shared<VansTerrainAsset>(working);
}

PcgEditorOperationResult EngineAPIImpl::RequestPcgSplinePreview()
{
    auto& state=*m_PcgSplineAuthoring;
    auto& repository=VansProjectManager::Get().GetAssetObjectRepository();
    std::string error;
    if (!state.RequestPreview(repository,ResolvePcgSplineTerrainOverride(),error)) return {false,error};
    TickPcgSplineAuthoring();return {true,{}};
}

PcgSplineSnapshot EngineAPIImpl::GetPcgSplineSnapshot()
{
    TickPcgSplineAuthoring();const auto& state=*m_PcgSplineAuthoring;PcgSplineSnapshot result;
    result.assetGuid=state.Guid();result.toolEnabled=state.ToolEnabled();result.selectedSpline=state.SelectedSpline();
    result.selectedPoint=state.SelectedPoint();result.message=state.Message();
    result.building=state.NeedsBuild()||(m_PcgSplinePreview&&m_PcgSplinePreview->IsBuilding());
    const auto document=state.Document();
    if (!document) return result;
    result.available=true;result.editable=m_PlayState==EnginePlayState::Edit;result.dirty=document->IsDirty()||state.Dragging();
    result.documentState=document->sourceDocument.CurrentStateId();
	const VansAuthoringHistorySnapshot history =
		VansAssetDocumentEditService::HistorySnapshot(document->sourceDocument);
    result.canUndo=!state.Dragging()&&history.CanUndo();
    result.canRedo=!state.Dragging()&&history.CanRedo();
	result.undoSequence=result.canUndo?history.undoSequence:0;
	result.redoSequence=result.canRedo?history.redoSequence:0;
    const auto& working=state.WorkingAsset();
    result.terrainGuid=working.terrain.ToString();result.fieldTexelSize=working.fieldTexelSize;
    result.minimumRiverTransitionWidth=MinimumPcgRiverTransitionWidth(working.fieldTexelSize);
    result.sampleSpacing=working.sampleSpacing;result.curveTolerance=working.curveTolerance;
    result.heightConflictThreshold=working.heightConflictThreshold;
    for (const auto& spline:working.splines)
    {
        result.splines.push_back(ToPublic(spline));
        if (!spline.enabled) continue;
        VansPcgEvaluatedSpline evaluated;std::string error;
        if (!VansPcgSplineEvaluator::Evaluate(spline,working.sampleSpacing,working.curveTolerance,evaluated,error)) continue;
        for (std::size_t i=0;i<spline.points.size();++i)
            result.splines.back().points[i].effectiveFlowSpeed=spline.points[i].speed*
                VansPcgSplineEvaluator::EndpointFade(spline,evaluated.pointDistances[i],evaluated.length);
        if (!state.ToolEnabled()) continue;
        PcgSplineGuide guide;guide.id=spline.id;
        const auto point=[](glm::vec3 p) {return std::array<float,3>{p.x,p.y,p.z};};
        for (const auto& sample:evaluated.samples)
        {
            auto right=sample.right;right.y=std::tan(glm::radians(sample.bankAngleDegrees));
            guide.center.push_back(point(sample.position));guide.left.push_back(point(sample.position-right*sample.leftWidth));
            guide.right.push_back(point(sample.position+right*sample.rightWidth));
        }
        result.guides.push_back(std::move(guide));
    }
    const auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
    if (scene && scene->GetSplineFieldSnapshot())
    {
        const auto& field=*scene->GetSplineFieldSnapshot();result.warnings=field.warnings;
        for (const auto& p:field.uncoveredBankPoints) result.uncoveredBankPoints.push_back({p.x,p.y,p.z});
        result.activeTiles=field.tiles.size();result.rebuiltTiles=field.rebuiltTileCount;
        if (state.ToolEnabled())
        {
            PcgSplineGuide flow;flow.id="resolved-flow";
            const auto stride=std::max(1u,static_cast<std::uint32_t>(std::ceil(6.f/field.texelSize)));
            for (const auto& [key,tile]:field.tiles) if (tile->hasRiver)
                for (std::uint32_t z=0;z<VANS_SPLINE_TILE_SIZE;++z)
                    for (std::uint32_t x=0;x<VANS_SPLINE_TILE_SIZE;++x)
                    {
                        const auto gx=tile->x*VANS_SPLINE_TILE_SIZE+x,gz=tile->z*VANS_SPLINE_TILE_SIZE+z;
                        if (gx%stride || gz%stride || flow.flowOrigins.size()>=2048) continue;
                        const auto pixel=std::size_t(z+VANS_SPLINE_TILE_BORDER)*VANS_SPLINE_TILE_EXTENT+x+VANS_SPLINE_TILE_BORDER;
                        if (tile->coverage[pixel].y<.1f) continue;
                        const auto velocity=tile->velocities[pixel];
                        flow.flowOrigins.push_back({(gx+.5f)*field.texelSize-field.worldSize*.5f,
                            tile->heights[pixel].y+.1f,(gz+.5f)*field.texelSize-field.worldSize*.5f});
                        flow.flowVectors.push_back({velocity.x,0,velocity.y});
                    }
            result.guides.push_back(std::move(flow));
        }
    }
    return result;
}

PcgEditorOperationResult EngineAPIImpl::SelectPcgSpline(const std::string& spline,const std::string& point,bool enabled)
{
    TickPcgSplineAuthoring();auto& state=*m_PcgSplineAuthoring;
    if (state.Dragging()) return {false,"Finish the current spline edit first."};
    if (enabled)
    {
        if (!state.Document() || m_PlayState!=EnginePlayState::Edit) return {false,"Select an editable spline asset."};
        SelectPcgBrushTarget({},false);
        // Scene 输入优先级在窗口统一仲裁，切换到样条工具不改变地形笔刷参数。
    }
    state.SetSelection(spline,point);state.SetToolEnabled(enabled);return {true,{}};
}

PcgEditorOperationResult EngineAPIImpl::FinishPcgSplineEdit()
{
    if (!m_PcgSplineAuthoring) return {true,{}};
    auto& state=*m_PcgSplineAuthoring;
    std::string error;
    if (!state.FinishEdit(error)) return {false,error};
    return {true,{}};
}

PcgEditorOperationResult EngineAPIImpl::ApplyPcgSplineEdit(const PcgSplineEditRequest& request)
{
    TickPcgSplineAuthoring();auto& state=*m_PcgSplineAuthoring;
    const auto openDocument=state.Document();
    if (!openDocument || m_PlayState!=EnginePlayState::Edit) return {false,"Open an editable spline asset."};
    auto& document=openDocument->sourceDocument;
    if (request.phase==PcgSplineEditPhase::Cancel)
    {state.CancelEdit();return RequestPcgSplinePreview();}
    if (request.documentState!=document.CurrentStateId())
    {state.CancelEdit();RequestPcgSplinePreview();return {false,"Spline changed in another editor. Reload the selection."};}
    if (request.phase==PcgSplineEditPhase::Begin)
    {
        std::string error;
        return {state.BeginEdit(error),error};
    }
    auto asset=state.WorkingAsset();
    auto found=std::find_if(asset.splines.begin(),asset.splines.end(),[&](const auto& item){return item.id==request.spline.id;});
    if (found==asset.splines.end()) return {false,"The spline no longer exists."};
    if (found->locked && request.spline.locked) return {false,"Unlock the spline before editing."};
    std::string error;
    if (!FromPublic(request.spline,*found,error)) return {false,error};
    if (request.phase==PcgSplineEditPhase::Update)
    {
        if (!state.ReplaceDraft(std::move(asset),error)) return {false,error};
        return RequestPcgSplinePreview();
    }
    if (!state.CommitAsset(std::move(asset),error)) return {false,error};
    return RequestPcgSplinePreview();
}

PcgEditorOperationResult EngineAPIImpl::BindPcgSplineAsset(const std::string& text)
{
    auto* document=m_PcgSceneDocument;
    if (!document || m_PlayState!=EnginePlayState::Edit)
        return {false,"Open an editable scene before binding splines."};
    VansAssetGuid guid;
    if (!VansAssetGuid::TryParse(text,guid)) return {false,"Select a spline asset."};
    const auto asset=VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansPcgSplineAsset>(guid);
    if (!asset || asset->terrain.ToString()!=GetTerrainEditorSnapshot().assetGuid)
        return {false,"Spline asset must reference this scene's terrain."};
    const auto edit=m_SceneAuthoringHost->SetSceneValue({DocumentPropertySpace::Scene,"/settings/pcgSplines"},
        MakeSerializedProjectAssetObjectReference(text,"pcgSpline"));
    if (!edit) return {false,edit.message};
    TickPcgSplineAuthoring();return {true,"Spline asset bound. Save the scene to retain its binding."};
}

PcgEditorOperationResult EngineAPIImpl::CreatePcgSplineAsset(const std::string& name)
{
    auto& manager=VansProjectManager::Get();auto* database=manager.GetAssetDatabase();
    auto* document=m_PcgSceneDocument;
    if (!database || !document || m_PlayState!=EnginePlayState::Edit) return {false,"Open an editable project scene."};
    VansPcgSplineAsset asset;asset.name=name;
    if (name.empty() || !VansAssetGuid::TryParse(GetTerrainEditorSnapshot().assetGuid,asset.terrain))
        return {false,"Name the spline asset and load a terrain first."};
    const auto guid=VansAssetGuid::New();
    const auto path=database->AssetsRoot()/"PCG"/(guid.ToString()+".vpcgspline");
    VansSerializedValue root;std::string error;
    if (!VansPcgSplineAssetCodec::Encode(asset,root,error)) return {false,error};
	VansAuthoringAssetCreateItem item;
	item.sourcePath=path;
	item.guid=guid;
	item.type=VansAssetType::PcgSpline;
	item.serializedRoot=std::move(root);
	const auto created=VansAuthoringAssetCreationService::CreateBundle(
		*database,manager.GetAssetObjectRepository(),path.parent_path(),
		{std::move(item)},"Pcg.CreateSplineAsset",{},false);
	if (!created) return {false,created.message};
    return BindPcgSplineAsset(guid.ToString());
}

PcgEditorOperationResult EngineAPIImpl::AppendPcgSplinePoint(const Ray& ray)
{
    const auto snapshot=GetPcgSplineSnapshot();
    auto& state=*m_PcgSplineAuthoring;
    if (!snapshot.editable || !state.ToolEnabled() || state.Dragging()) return {false,"Enable spline editing in Edit mode."};
    auto found=std::find_if(snapshot.splines.begin(),snapshot.splines.end(),[&](const auto& item){return item.id==snapshot.selectedSpline;});
    if (found==snapshot.splines.end() || found->locked) return {false,"Select an unlocked spline."};
    const bool prepend=!found->points.empty() && state.SelectedPoint()==found->points.front().id;
    if (!found->points.empty() && !prepend && state.SelectedPoint()!=found->points.back().id)
        return {false,"Select an open endpoint to extend the spline."};
    auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);VansPcgSurfaceHit hit;
    if (!scene || !RaycastPcgTerrainSurface(scene->ResolveEffectiveTerrain(state.WorkingAsset().terrain),
        {ray.origin.x,ray.origin.y,ray.origin.z},{ray.direction.x,ray.direction.y,ray.direction.z},100000.f,hit))
        return {false,"The pointer does not hit the terrain."};
    PcgSplineEditRequest request;request.documentState=snapshot.documentState;request.spline=*found;
    auto point=request.spline.points.empty()?PcgSplinePoint{}:(prepend?request.spline.points.front():request.spline.points.back());
    const float previousHeight=point.position[1];
    point.id=VansAssetGuid::New().ToString();point.position=hit.position;
    if (found->kind==PcgSplineKind::River && !found->points.empty()) point.position[1]=previousHeight;
    point.tangentMode=PcgSplineTangentMode::Auto;
    if (prepend) request.spline.points.insert(request.spline.points.begin(),point);
    else request.spline.points.push_back(point);
    const auto result=ApplyPcgSplineEdit(request);
    if (result.success) state.SetSelectedPoint(point.id);
    return result;
}

PcgEditorOperationResult EngineAPIImpl::ExecutePcgSplineCommand(const PcgSplineCommandRequest& request)
{
    TickPcgSplineAuthoring();auto& state=*m_PcgSplineAuthoring;
    const auto openDocument=state.Document();
    if (!openDocument || m_PlayState!=EnginePlayState::Edit || state.Dragging()) return {false,"Finish editing before this operation."};
    auto& document=openDocument->sourceDocument;
    if (request.command==PcgSplineCommand::Save)
    {const auto saved=SaveAuthoringDocument(openDocument);return {saved.success,saved.message};}
    if (request.command==PcgSplineCommand::Bake)
    {
        auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
        const auto field=scene?scene->GetSplineFieldSnapshot():nullptr;
        if (!field || (m_PcgSplinePreview&&m_PcgSplinePreview->IsBuilding()) || state.NeedsBuild() ||
            field->sourceFingerprint!=VansPcgSplineAssetCodec::ContentHash(state.WorkingAsset()))
            return {false,"Wait for a successful field update before baking."};
        VansAssetGuid guid;VansAssetGuid::TryParse(state.Guid(),guid);std::string error;
        if (!VansPcgSplineFieldStorage::Save(VansPcgSplineFieldStorage::CachePath(state.ProjectRoot(),guid),*field,error)) return {false,error};
        return {true,"Fields baked. Save source edits and the scene binding to retain the complete authoring state."};
    }
    if (request.command==PcgSplineCommand::Undo || request.command==PcgSplineCommand::Redo)
    {
        const auto edit=request.command==PcgSplineCommand::Undo?VansAssetDocumentEditService::Undo(document):VansAssetDocumentEditService::Redo(document);
        if (!edit) return {false,edit.message};return RequestPcgSplinePreview();
    }
    auto asset=state.WorkingAsset();std::string error;
    if (request.command==PcgSplineCommand::ConfigureFields)
    {
        asset.fieldTexelSize=request.fieldSettings[0];asset.sampleSpacing=request.fieldSettings[1];
        asset.curveTolerance=request.fieldSettings[2];asset.heightConflictThreshold=request.fieldSettings[3];
        if (!state.CommitAsset(std::move(asset),error)) return {false,error};
        return RequestPcgSplinePreview();
    }
    auto found=std::find_if(asset.splines.begin(),asset.splines.end(),[&](const auto& item){return item.id==request.splineId;});
    if (request.command==PcgSplineCommand::Focus)
    {
        auto* scene=static_cast<VansGraphics::VansScene*>(m_Scene);
        if (found==asset.splines.end() || found->points.empty() || !scene || !scene->GetCamera()) return {false,"Select a spline with points in the scene."};
        glm::vec3 minimum(std::numeric_limits<float>::max()),maximum(-std::numeric_limits<float>::max());
        for (const auto& point:found->points)
        {
            const glm::vec3 position(point.position[0],point.position[1],point.position[2]);
            minimum=glm::min(minimum,position);maximum=glm::max(maximum,position);
        }
        const auto center=(minimum+maximum)*.5f;auto* camera=scene->GetCamera();auto view=camera->CaptureView();
        const float distance=std::max(15.f,glm::length(maximum-minimum)*.65f/std::tan(glm::radians(view.lens.fieldOfView*.5f)));
        view.pose.position=center+glm::normalize(glm::vec3(.65f,.85f,1.f))*distance;
        const auto forward=glm::normalize(center-view.pose.position);
        view.pose.rotationDegrees={glm::degrees(std::asin(forward.y)),glm::degrees(std::atan2(forward.z,forward.x)),0};
        camera->ApplyView(view);return {true,{}};
    }
    if (request.command==PcgSplineCommand::AddRoad || request.command==PcgSplineCommand::AddRiver)
    {
        VansPcgSpline spline;spline.id=VansAssetGuid::New().ToString();
        spline.kind=request.command==PcgSplineCommand::AddRoad?VansPcgSplineKind::Road:VansPcgSplineKind::River;
        spline.name=spline.kind==VansPcgSplineKind::Road?"Road":"River";
        if (spline.kind==VansPcgSplineKind::Road && !VansAssetGuid::TryParse(request.materialGuid,spline.material))
            return {false,"Choose a road PBR material."};
        VansPcgSplinePoint point;point.id=VansAssetGuid::New().ToString();point.position=request.position;
        spline.points.push_back(point);
        VansPcgSplineEvaluator::ResolveAutoTangents(spline);
        state.SetSelection(spline.id,spline.points.front().id);asset.splines.push_back(std::move(spline));
    }
    else
    {
        if (found==asset.splines.end()) return {false,"Select a spline."};
        if (found->locked) return {false,"Unlock the spline first."};
        switch(request.command)
        {
        case PcgSplineCommand::Remove:asset.splines.erase(found);state.ClearSelection();break;
        case PcgSplineCommand::Duplicate:
        {
            auto copy=*found;copy.id=VansAssetGuid::New().ToString();copy.name+=" Copy";
            for (auto& point:copy.points) point.id=VansAssetGuid::New().ToString();
            state.SetSelection(copy.id,{});asset.splines.push_back(std::move(copy));break;
        }
        case PcgSplineCommand::Reverse:
        {
            VansPcgEvaluatedSpline evaluated;
            if (!VansPcgSplineEvaluator::Evaluate(*found,asset.sampleSpacing,asset.curveTolerance,evaluated,error)) return {false,error};
            VansPcgSplineEvaluator::ReversePointOrder(*found,evaluated.length);break;
        }
        case PcgSplineCommand::InsertPoint:
        {
            const auto id=VansAssetGuid::New().ToString();
            if (!VansPcgSplineEvaluator::InsertPoint(*found,request.segment,request.parameter,id,error)) return {false,error};
            state.SetSelectedPoint(id);break;
        }
        case PcgSplineCommand::RemovePoint:
            found->points.erase(std::remove_if(found->points.begin(),found->points.end(),[&](const auto& p){return p.id==request.pointId;}),found->points.end());
            VansPcgSplineEvaluator::ResolveAutoTangents(*found);state.SetSelectedPoint({});break;
        default:return {false,"Unsupported spline command."};
        }
    }
    if (!state.CommitAsset(std::move(asset),error)) return {false,error};
    return RequestPcgSplinePreview();
}
}
