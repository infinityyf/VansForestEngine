#include "NavigationAIContractTests.h"

#include "../EngineCore/AICore/VansAIBlackboard.h"
#include "../EngineCore/AICore/VansAIWorld.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneAssembly.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/AICore/VansAIEvents.h"
#include "../EngineCore/AICore/VansAIPerception.h"
#include "../EngineCore/AICore/VansAIRuntimeComponents.h"
#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/NavigationCore/VansNavigationMap.h"
#include "../EngineCore/NavigationCore/VansNavigationBuilder.h"
#include "../EngineCore/NavigationCore/VansNavigationBuildContext.h"
#include "../EngineCore/Util/VansFileFingerprint.h"
#include "../EngineCore/NavigationCore/VansNavigationTileUpdater.h"
#include "../EngineCore/NavigationCore/Detail/VansNavigationTileCacheBuild.h"
#include "../EngineCore/NavigationCore/Detail/VansNavigationTileCacheLayerCodec.h"
#include "../EngineCore/NavigationCore/Detail/VansNavigationNativeTileTransaction.h"
#include "../EngineCore/AuthoringCore/Navigation/VansNavigationAuthoringSession.h"
#include "../EngineCore/AuthoringCore/Navigation/VansNavigationBakePublication.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/SceneCore/VansSceneDocument.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/NavigationCore/Serialization/VansNavigationMeshCodec.h"
#include "../EngineCore/NavigationCore/Storage/VansNavigationMeshStorage.h"
#include "../EngineCore/NavigationCore/VansNavigationWorld.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/SceneRuntime/VansSceneNavigationRuntime.h"
#include "../EngineCore/SceneRuntime/VansRuntimeNavigationSources.h"
#include "../EngineCore/SceneRuntime/VansPcgPhysicsInstances.h"
#include "../EngineCore/PcgCore/VansPcgResourcePlan.h"
#include "../EngineCore/PcgCore/VansPcgMaskAsset.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/Transform/VansTransformStore.h"
#include "../EngineCore/SceneRuntime/Transform/VansTransformGraph.h"
#include "../EngineCore/PhysicsCore/VansPhysics.h"
#include "../EngineCore/PhysicsCore/VansPhysicsNativeAccess.h"
#include "../EngineCore/PhysicsCore/VansCollisionLayerManager.h"
#include "../EngineCore/PhysicsCore/VansPhysicsNode.h"
#include "../EngineCore/PhysicsCore/VansTerrainPhysicsNode.h"
#include "../EngineCore/NavigationCore/Detail/VansNavigationHeightfieldInput.h"
#include "../EngineCore/PhysicsCore/VansCharacterControllerNode.h"
#include "../EngineCore/RuntimeCore/VansRuntimePhaseGuard.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/RuntimeCore/VansFramePhase.h"
#include "../EngineCore/NavigationCore/VansNavigationRuntimeComponents.h"
#include "../EngineCore/NavigationCore/VansNavigationQueryContext.h"
#include "../EngineCore/NavigationCore/Serialization/VansNavigationSettingsJsonCodec.h"
#include "../EngineCore/NavigationCore/Storage/VansNavigationSettingsStorage.h"
#include "../EngineCore/SceneCore/VansSceneNavigationGeometry.h"
#include "../EngineCore/SceneCore/VansSceneNavigationComponentReader.h"
#include "../EngineCore/SceneCore/VansComponentTypeCatalog.h"
#include "../EngineCore/SceneCore/Prefab/VansPrefabAsset.h"
#include "../EngineCore/SceneCore/VansSceneEntityFactory.h"
#include "../EngineCore/SceneCore/VansSceneParentReference.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/EditorCore/VansEditorRuntimePreviewProjector.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AICore/VansAIBehaviorAsset.h"
#include "../EngineCore/AnimationCore/VansAnimationNode.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/GameplayActionAdapters/VansGameplayPrimitivesContributor.h"
#include "../EngineCore/SceneCore/VansScenePhysicsComponentReader.h"
#include <Recast.h>
#include <nlohmann/json.hpp>
#include "../EngineCore/SceneCore/VansSceneContentBuildPlan.h"
#include "../EngineCore/NavigationCore/VansNavigationSourceCollector.h"
#include "../EngineCore/RuntimeCore/VansCharacterTrajectoryGenerator.h"
#include "../EngineCore/RuntimeCore/VansCharacterMotion.h"
#include "../EngineCore/SceneCore/VansSceneObjectBuildPlan.h"
#include "../EngineCore/SceneCore/VansSceneRuntimeProjection.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include "../EngineCore/Util/VansSceneViewMath.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <future>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <type_traits>
#include <DetourNavMesh.h>
#include <DetourAlloc.h>
#include <DetourNavMeshQuery.h>
#include <DetourNavMeshBuilder.h>
#include <DetourPathCorridor.h>
#include <iterator>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>
#include <utility>
#include <vector>

extern "C" {
#include <lauxlib.h>
}

namespace
{
class ScopedFileRemoval
{
public:
	explicit ScopedFileRemoval(std::filesystem::path path)
		: m_Path(std::move(path))
	{
	}

	~ScopedFileRemoval()
	{
		std::error_code error;
		std::filesystem::remove(m_Path, error);
	}

private:
	std::filesystem::path m_Path;
};

bool CaptureMapLayerSeeds(const std::shared_ptr<Vans::VansNavigationMap>& map,
    const Vans::VansNavigationTileUpdate& update,std::uint64_t maximumBytes,
    std::vector<Vans::VansNavigationTileLayerData>& output,std::string& error)
{
    Vans::VansNavigationLayerRead request;
    if(!map->CaptureLayerRead(update,maximumBytes,request,error)) return false;
    const auto* profile=request.asset->FindProfile(request.profileId);
    return request.Read(profile->baseInputHash,nullptr,output,error);
}

// 模拟帧在真实 worker 待执行时也提供一个 60 Hz 帧间隔；不改变帧数、预算或行为断言。
void PaceNavigationWorkerFrame(const Vans::VansNavigationWorld& world)
{
 if(world.CaptureQueryStats().inFlight) std::this_thread::sleep_for(std::chrono::microseconds(16667));
}

Vans::VansNavigationSteering FollowContractCorridor(Vans::VansNavigationQueryContext& query,
    const glm::vec3& actual,float horizontal,float vertical)
{
    int used=0;
    return query.FollowCorridor(actual,horizontal,vertical,64,used);
}
Vans::VansNavigationPathFailure RepairContractTarget(Vans::VansNavigationQueryContext& query,
    const glm::vec3& goal,const glm::vec3& extents,Vans::VansNavigationGoalAcceptance acceptance,
    float tolerance,Vans::VansNavigationPath& path)
{
    int used=0; bool pending=false;
    return query.TryRepairTarget(goal,extents,acceptance,tolerance,64,used,pending,path);
}

void AppendEnvironmentBox(Vans::VansSceneObjectBuildPlan& plan,
	const char* name,
	const std::array<float, 3>& position,
	const std::array<float, 3>& extents,
	const char* navigationArea = nullptr)
{
	Vans::VansSceneObjectBuildConfig object;
	object.entityGuid = Vans::VansAssetGuid::FromStableName(
		"NavigationContract", name).ToString();
	object.name = name;
	object.transform = Vans::VansSceneTransformConfig{};
	object.transform->position = position;
	Vans::VansScenePhysicsNodeConfig physics;
	physics.enabled = true;
	physics.bodyType = "static";
	physics.colliderType = "box";
	physics.boxExtents = extents;
	physics.layer = "Environment";
	physics.isTrigger = false;
	object.componentGuids["physics"]=Vans::VansAssetGuid::FromStableName("NavigationContract",std::string(name)+".Physics").ToString();
    if(navigationArea)
    {
        object.navigationSource=Vans::VansSceneNavigationSourceConfig{};
        object.navigationSource->runtime.mode=Vans::VansNavigationSourceMode::Area;
        object.navigationSource->runtime.area=navigationArea;
        object.componentGuids["navigation_source"]=Vans::VansAssetGuid::FromStableName("NavigationContract",std::string(name)+".Source").ToString();
    }
	object.physicsComponents.physics = physics;
	plan.objects.push_back(std::move(object));
}

bool Expect(bool condition, const std::string& message)
{
	if (condition) return true;
	std::cerr << "[NavigationAIContractTests] " << message << '\n';
	return false;
}

Vans::VansNavigationBakeSettings ContractBake(const Vans::VansNavigationSettings& settings)
{
    Vans::VansNavigationBakeSettings bake;
    std::string error;
    if (!Vans::ResolveNavigationBakeSettings(settings, "World", "Humanoid", bake, error)) throw std::logic_error(error);
    return bake;
}

Vans::VansNavigationSurfaceRecord ContractSurface(const Vans::VansNavigationSettings& settings)
{
    Vans::VansNavigationSurfaceRecord record;
    record.id="NavigationContract.Surface"; record.ownerId="NavigationContract.SurfaceOwner";
    record.surface.boundsHalfExtentsMeters=glm::vec3(1000);
    record.surface.profiles=settings.maps.front().profiles;
    record.surface.collisionLayers={"Environment"};
    return record;
}
bool CollectContractGeometry(const Vans::VansSceneObjectBuildPlan& objects,const Vans::VansNavigationAreaSettings& areas,
    const Vans::VansNavigationMeshResolver& resolve,Vans::VansNavigationGeometry& geometry,std::string& error)
{
    Vans::VansNavigationSettings settings; settings.areas=areas;
    Vans::VansSceneContentBuildPlan scene; scene.objects=objects;
    for(auto& object:scene.objects.objects)
        if(object.physicsComponents.physics && !object.componentGuids.count("physics"))
            object.componentGuids["physics"]=object.entityGuid+".Physics";
    const auto definition=ContractSurface(settings);
    Vans::VansSceneObjectBuildConfig surface;
    surface.entityGuid=Vans::VansAssetGuid::FromStableName("NavigationContract","SurfaceOwner").ToString();
    surface.transform=Vans::VansSceneTransformConfig{};
    surface.componentGuids["navigation_surface"]=definition.id;
    surface.navigationSurface=Vans::VansSceneNavigationSurfaceConfig{}; surface.navigationSurface->runtime=definition.surface;
    scene.objects.objects.push_back(surface);
    Vans::VansNavigationSourceSnapshot snapshot;
    if(resolve && !VansEngine::VansPhysicsSystem::GetInstance().Initialize()) { error="Physics fixture cooking initialization failed"; return false; }
    if(!Vans::VansSceneNavigationGeometry::CaptureSnapshot(scene,Vans::VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default","Environment"},resolve,snapshot,error)) return false;
    if(std::none_of(snapshot.sources.begin(),snapshot.sources.end(),[](const auto& source){return source.enabled && source.isStatic && !source.isTrigger && (source.geometry || !source.geometryError.empty());}))
    { geometry={}; error.clear(); return true; }
    Vans::VansNavigationBuildInput input;
    if(!Vans::VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error)) return false;
    geometry=*input.profiles.front().geometry; return true;
}
bool BuildContractNavigation(const Vans::VansNavigationGeometry& geometry,const Vans::VansNavigationSettings& settings,
    const std::string& mapId,const std::string& scene,std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset,std::string& error)
{
    Vans::VansNavigationBuildInput input; input.mapId=mapId;
    const auto* map=Vans::FindNavigationMap(settings,mapId);
    if(!map) { error="Contract Map is unavailable"; return false; }
    const auto surface=ContractSurface(settings); std::uint64_t hash=0;
    if(!Vans::ComputeNavigationSurfaceContributionHash(surface,hash,error)) return false;
    for(const auto& id:map->profiles)
        input.profiles.push_back({id,std::make_shared<const Vans::VansNavigationGeometry>(geometry),{{"surface/"+surface.id,hash}},{}});
    return Vans::VansNavigationBuilder::Build(input,settings,scene,nullptr,asset,error);
}

bool CheckNativeTileRollback(const Vans::VansNavigationProfileBuildData& baked,const Vans::VansNavigationQuerySettings& settings)
{
    using namespace Vans;
    if(baked.tiles.size()<2) return Expect(false,"Tile rollback fixture requires two populated cells");
    std::vector<std::vector<unsigned char>> owned;
    owned.reserve(baked.tiles.size());
    for(const auto& tile:baked.tiles) owned.push_back(tile.bytes);
    std::unique_ptr<dtNavMesh,decltype(&dtFreeNavMesh)> mesh(dtAllocNavMesh(),&dtFreeNavMesh);
    if(!Expect(mesh!=nullptr,"Tile rollback fixture cannot allocate a native Map")) return false;
    dtNavMeshParams params{};
    params.orig[0]=baked.bake.tileOrigin.x; params.orig[1]=baked.bake.tileOrigin.y; params.orig[2]=baked.bake.tileOrigin.z;
    params.tileWidth=params.tileHeight=baked.bake.cellSize*baked.bake.tileSizeCells;
    params.maxTiles=baked.bake.maximumTiles; params.maxPolys=baked.bake.maximumPolygonsPerTile;
    if(!Expect(dtStatusSucceed(mesh->init(&params)),"Tile rollback fixture native layout failed")) return false;
    std::vector<VansNavigationNativeTileBuffer> initial;
    for(auto& bytes:owned) initial.push_back({bytes.data(),static_cast<int>(bytes.size())});
    if(!Expect(ApplyNavigationNativeTileTransaction(*mesh,0,{},initial).state==VansNavigationNativeTileCommit::Applied,
        "Tile rollback fixture registration failed")) return false;
    std::vector<dtTileRef> originals;
    std::array<glm::vec3,2> positions{glm::vec3(0),glm::vec3(0)};
    for(std::size_t index=0;index<2;++index)
    {
        const auto& key=baked.tiles[index]; const auto* tile=mesh->getTileAt(key.x,key.z,key.layer);
        originals.push_back(mesh->getTileRef(tile));
        const auto& poly=tile->polys[0];
        for(int vertex=0;vertex<poly.vertCount;++vertex)
        { const auto* p=&tile->verts[poly.verts[vertex]*3]; positions[index]+=glm::vec3(p[0],p[1],p[2]); }
        positions[index]/=static_cast<float>(poly.vertCount);
    }
    std::unique_ptr<dtNavMeshQuery,decltype(&dtFreeNavMeshQuery)> query(dtAllocNavMeshQuery(),&dtFreeNavMeshQuery);
    if(!Expect(query && dtStatusSucceed(query->init(mesh.get(),settings.maximumSearchNodes)),"Tile rollback query initialization failed")) return false;
    dtQueryFilter filter;
    std::vector<dtPolyRef> originalPath(static_cast<std::size_t>(settings.maximumCorridorPolygons));
    int originalCount=0;
    const auto originalStatus=query->findPath(originals[0],originals[1],&positions[0].x,&positions[1].x,
        &filter,originalPath.data(),&originalCount,static_cast<int>(originalPath.size()));
    if(!Expect(dtStatusSucceed(originalStatus) && originalCount>0,"Tile rollback fixture has no original native path")) return false;
    auto nextFirst=baked.tiles[0].bytes,nextSecond=baked.tiles[1].bytes;
    dtMeshHeader rejected{}; std::memcpy(&rejected,nextSecond.data(),sizeof(rejected));
    rejected.magic^=1; std::memcpy(nextSecond.data(),&rejected,sizeof(rejected));
    const std::vector<VansNavigationNativeTileBuffer> candidates{
        {nextFirst.data(),static_cast<int>(nextFirst.size())},{nextSecond.data(),static_cast<int>(nextSecond.size())}};
    const auto failed=ApplyNavigationNativeTileTransaction(*mesh,baked.tiles.size(),originals,candidates);
    if(!Expect(failed.state==VansNavigationNativeTileCommit::RolledBack && dtStatusDetail(failed.status,DT_WRONG_MAGIC),
        "A second native Tile registration failure did not roll back the entire batch")) return false;
    for(std::size_t index=0;index<2;++index)
    {
        const auto* restored=mesh->getTileByRef(originals[index]);
        if(!Expect(restored && restored->data==owned[index].data() && !(restored->flags & DT_TILE_FREE_DATA),
            "Native Tile rollback lost its original reference, Map-owned bytes or ownership flags")) return false;
    }
    std::vector<dtPolyRef> restoredPath(originalPath.size()); int restoredCount=0;
    const auto restoredStatus=query->findPath(originals[0],originals[1],&positions[0].x,&positions[1].x,
        &filter,restoredPath.data(),&restoredCount,static_cast<int>(restoredPath.size()));
    if(!Expect(restoredStatus==originalStatus && restoredCount==originalCount &&
        std::equal(originalPath.begin(),originalPath.begin()+originalCount,restoredPath.begin()),
        "Native batch rollback changed the accepted connectivity or exact polygon route")) return false;
    std::cout<<"[NavigationTileTransaction] PASS: second-add native rejection restores original Tile refs, borrowed bytes and exact path\n";
    return true;
}

bool CheckLocalTilePublication(const Vans::VansNavigationProfileBuildInput& source,
    const Vans::VansNavigationSettings& settings,const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset)
{
    using namespace Vans;
    VansNavigationProfileBuildData bakedData; std::string readError;
    if(!Expect(asset->ReadProfile(source.profileId,bakedData,readError),readError)) return false;
    const auto* baked=&bakedData;
    std::array<const VansNavigationTilePayload*,2> original{};
    for(const auto& tile:baked->tiles)
    {
        if(!original[0]) original[0]=&tile;
        else if(tile.x!=original[0]->x || tile.z!=original[0]->z) { original[1]=&tile; break; }
    }
    if(!Expect(original[1]!=nullptr,"Local Tile publication needs two actual native cells")) return false;
    const auto groundPoint=[](const VansNavigationTilePayload& tile)
    {
        dtMeshHeader header{}; std::memcpy(&header,tile.bytes.data(),sizeof(header));
        const std::size_t vertices=(sizeof(header)+3u)&~3u;
        const std::size_t polygons=vertices+((header.vertCount*3u*sizeof(float)+3u)&~3u);
        for(int index=0;index<header.polyCount;++index)
        {
            dtPoly poly{}; std::memcpy(&poly,tile.bytes.data()+polygons+index*sizeof(poly),sizeof(poly));
            if(poly.getType()!=DT_POLYTYPE_GROUND) continue;
            glm::vec3 point(0);
            for(int corner=0;corner<poly.vertCount;++corner)
            {
                std::array<float,3> position{};
                std::memcpy(position.data(),tile.bytes.data()+vertices+poly.verts[corner]*3u*sizeof(float),sizeof(position));
                point+=glm::vec3(position[0],position[1],position[2]);
            }
            return point/static_cast<float>(poly.vertCount);
        }
        throw std::logic_error("Native selected Tile has no ground polygon");
    };
    const std::array<glm::vec3,2> points{groundPoint(*original[0]),groundPoint(*original[1])};
    const std::array<VansNavigationTileCell,2> cells{{{original[0]->x,original[0]->z},{original[1]->x,original[1]->z}}};
    const glm::vec3 extents(.01f,100.f,.01f);
    auto map=std::make_shared<VansNavigationMap>(); std::string error;
    VansNavigationQueryContext affected,unaffected,active,verifier;
    if(!Expect(map->Initialize(asset,settings,source.profileId,error) &&
        affected.Initialize(map,settings.query,"Default",error) && unaffected.Initialize(map,settings.query,"Default",error) &&
        active.Initialize(map,settings.query,"Default",error) && verifier.Initialize(map,settings.query,"Default",error),error)) return false;
    const auto first=affected.FindPath(points[0],points[0],extents);
    const auto second=unaffected.FindPath(points[1],points[1],extents);
    const auto affectedRoute=affected.CaptureCorridorRoute(),unaffectedRoute=unaffected.CaptureCorridorRoute();
    if(!Expect(first.status==VansNavigationPathStatus::Complete && second.status==VansNavigationPathStatus::Complete &&
        affectedRoute && unaffectedRoute,"Actual native per-cell routes are unavailable")) return false;
    if(!Expect(active.BeginPath(points[1],points[1],extents).status==VansNavigationPathStatus::None,
        "Local Tile fixture cannot suspend an actual query")) return false;
    const auto resident=map->GetResidentTileCount();
    VansNavigationTileUpdate request;
    if(!Expect(map->InvalidateTiles({cells[0]},error) && map->CaptureTileUpdate(request,error) && request.cells.size()==1,error)) return false;
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansNavigationQueryScheduler scheduler; scheduler.Initialize(settings.query,8781);
    VansRuntimePhaseGuard schedulerCleanup([&](){scheduler.Shutdown(); jobs.Shutdown();});
    VansNavigationQueryRequest queryRequest; queryRequest.mapId=asset->GetMapId(); queryRequest.profileId=source.profileId;
    queryRequest.start=queryRequest.goal=points[0]; queryRequest.nearestExtents=extents;
    const auto handle=scheduler.Submit(queryRequest,"Local.Tile.Wait",map,{},error);
    if(!Expect(handle.IsValid(),error)) return false;
    const auto waitFor=[&](VansNavigationQueryLifecycle expected)
    {
        for(int frame=0;frame<40;++frame)
        {
            scheduler.Update();
            const auto result=scheduler.Capture(handle,"Local.Tile.Wait",error);
            if(result && result->lifecycle==expected) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    if(!Expect(waitFor(VansNavigationQueryLifecycle::WaitingForData),"Real worker query did not retain its Dirty-data wait")) return false;
    for(int frame=0;frame<20;++frame)
    {
        scheduler.Update(); const auto stats=scheduler.CaptureStats();
        if(!Expect(!stats.inFlight && !stats.startsLastFrame && !stats.iterationsLastFrame &&
            scheduler.Capture(handle,"Local.Tile.Wait",error)->lifecycle==VansNavigationQueryLifecycle::WaitingForData,
            "Unchanged Dirty data dispatched repeated worker queries or lost the original handle")) return false;
    }
    int used=0;
    if(!Expect(active.AdvancePath(1,used).failure==VansNavigationPathFailure::MeshChanged &&
        verifier.AdoptCorridorRoute(affectedRoute)==VansNavigationPathFailure::DataUnavailable &&
        verifier.AdoptCorridorRoute(unaffectedRoute)==VansNavigationPathFailure::None &&
        unaffected.CaptureCorridorSteering(points[1]).valid &&
        affected.FindPath(points[0],points[0],extents).failure==VansNavigationPathFailure::DataUnavailable &&
        affected.ProjectPoint(points[0],extents).failure==VansNavigationPathFailure::DataUnavailable,
        "Dirty cell remained navigable, or an unrelated accepted Corridor stopped")) return false;
    VansNavigationTileBuildOutput candidate;
    if(!Expect(VansNavigationBuilder::BuildSelectedTiles(source,settings,asset->GetMapId(),{cells[0]},
        request.rasterOriginMeters,settings.maximumConcurrentBuildBytes,nullptr,candidate,error),error)) return false;
    const auto before=candidate.tiles.front().bytes;
    if(!Expect(map->ApplyTileUpdate(request,candidate,1,error).state==VansNavigationTileCommitState::Rejected &&
        map->GetResidentTileCount()==resident && candidate.tiles.front().bytes==before &&
        verifier.AdoptCorridorRoute(affectedRoute)==VansNavigationPathFailure::DataUnavailable,
        "Rejected Tile allocation budget changed native data or restored Dirty validity")) return false;
    auto invalid=candidate; invalid.tiles.front().bytes.front()^=1u;
    if(!Expect(map->ApplyTileUpdate(request,invalid,settings.maximumConcurrentBuildBytes,error).state==VansNavigationTileCommitState::Rejected &&
        map->GetResidentTileCount()==resident,"Malformed selected Tile changed the active native Map")) return false;
    auto commit=map->ApplyTileUpdate(request,candidate,settings.maximumConcurrentBuildBytes,error);
    if(!Expect(waitFor(VansNavigationQueryLifecycle::Finished) &&
        scheduler.Capture(handle,"Local.Tile.Wait",error)->path.status==VansNavigationPathStatus::Complete,
        "Tile publication did not resume the same waiting worker request")) return false;
    if(!Expect(commit.state==VansNavigationTileCommitState::Applied && commit.appliedCells==1 &&
        verifier.AdoptCorridorRoute(affectedRoute)==VansNavigationPathFailure::MeshChanged &&
        verifier.AdoptCorridorRoute(unaffectedRoute)==VansNavigationPathFailure::None &&
        unaffected.CaptureCorridorSteering(points[1]).valid &&
        affected.FindPath(points[0],points[0],extents).status==VansNavigationPathStatus::Complete,
        "Local publication lost unrelated native refs, accepted an old route, or failed to reopen its cell")) return false;
    if(!Expect(map->InvalidateTiles({cells[0],cells[1]},error) && map->CaptureTileUpdate(request,error) &&
        map->InvalidateTiles({cells[0]},error),error)) return false;
    if(!Expect(VansNavigationBuilder::BuildSelectedTiles(source,settings,asset->GetMapId(),{cells[0],cells[1]},
        request.rasterOriginMeters,settings.maximumConcurrentBuildBytes,nullptr,candidate,error),error)) return false;
    commit=map->ApplyTileUpdate(request,candidate,settings.maximumConcurrentBuildBytes,error);
    if(!Expect(commit.state==VansNavigationTileCommitState::Applied && commit.appliedCells==1 &&
        map->CaptureTileUpdate(request,error) && request.cells.size()==1 && request.cells.front().cell==cells[0] &&
        affected.ProjectPoint(points[0],extents).failure==VansNavigationPathFailure::DataUnavailable &&
        unaffected.FindPath(points[1],points[1],extents).status==VansNavigationPathStatus::Complete,
        "One newly changed cell starved a valid completed cell or published its stale bytes")) return false;
    const auto retainedRoute=unaffected.CaptureCorridorRoute();
    commit=map->ApplyTileUpdate(request,{},settings.maximumConcurrentBuildBytes,error);
    const auto removedLayers=std::count_if(baked->tiles.begin(),baked->tiles.end(),[&](const auto& tile)
    { return tile.x==cells[0].x && tile.z==cells[0].z; });
    if(!Expect(commit.state==VansNavigationTileCommitState::Applied &&
        map->GetResidentTileCount()==resident-removedLayers &&
        verifier.AdoptCorridorRoute(retainedRoute)==VansNavigationPathFailure::None &&
        !affected.ProjectPoint(points[0],extents).valid && map->CaptureTileUpdate(request,error) && request.cells.empty(),
        "Empty selected replacement retained deleted geometry or removed a different contribution")) return false;
    if(!Expect(map->InvalidateTiles({cells[0]},error) && map->CaptureTileUpdate(request,error) &&
        map->Initialize(asset,settings,source.profileId,error) &&
        map->ApplyTileUpdate(request,{},settings.maximumConcurrentBuildBytes,error).state==VansNavigationTileCommitState::Stale &&
        map->GetResidentTileCount()==resident,"Completion from a replaced Map was accepted")) return false;
    if(!Expect(map->InvalidateTiles({cells[0]},error),error)) return false;
    auto deadlineSettings=settings.query; deadlineSettings.maximumQueryFrames=6;
    scheduler.Shutdown(); scheduler.Initialize(deadlineSettings,8782);
    const auto deadline=scheduler.Submit(queryRequest,"Local.Tile.Deadline",map,{},error);
    for(int frame=0;frame<12;++frame)
    { scheduler.Update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    const auto timedOut=scheduler.Capture(deadline,"Local.Tile.Deadline",error);
    if(!Expect(timedOut && timedOut->lifecycle==VansNavigationQueryLifecycle::Finished &&
        timedOut->path.failure==VansNavigationPathFailure::QueryDeadlineExceeded,
        "Dirty-data wait extended the original query deadline or became business Partial")) return false;
    std::cout<<"[NavigationLocalPublication] PASS: Dirty traversal denial, unrelated Corridor continuity, stamped subset commit, atomic rejection, native deletion and replaced-Map rejection\n";
    return true;
}

bool CheckRuntimeTileUpdater(const Vans::VansNavigationProfileBuildInput& original,const Vans::VansNavigationSettings& sourceSettings,
    const std::shared_ptr<const Vans::VansNavigationMeshAsset>& originalAsset)
{
    using namespace Vans;
    auto settings=sourceSettings;
    for(auto& config:settings.maps) if(config.id==originalAsset->GetMapId())
    { config.updateMode=VansNavigationMapUpdateMode::Runtime; config.profiles={original.profileId}; }
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansRuntimePhaseGuard jobsCleanup([&](){jobs.Shutdown();});
    auto map=std::make_shared<VansNavigationMap>(); std::string error;
    VansNavigationBuildInput runtimeInput; runtimeInput.mapId=originalAsset->GetMapId(); runtimeInput.profiles={original};
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!VansNavigationBuilder::Build(runtimeInput,settings,originalAsset->GetSource().scene,nullptr,asset,error) ||
        !map->Initialize(asset,settings,original.profileId,error)) return Expect(false,error);
    auto source=std::make_shared<VansNavigationProfileBuildInput>(original);
    glm::vec3 minimum((std::numeric_limits<float>::max)()),maximum=-minimum;
    for(std::size_t vertex=0;vertex<source->geometry->VertexCount();++vertex)
    {
        const auto& v=source->geometry->vertices; const glm::vec3 point(v[vertex*3],v[vertex*3+1],v[vertex*3+2]);
        minimum=glm::min(minimum,point); maximum=glm::max(maximum,point);
    }
    for(const auto& field:source->heightfields)
    {minimum=glm::min(minimum,field.boundsMinimum); maximum=glm::max(maximum,field.boundsMaximum);}
    std::uint64_t sourceHash=0;
    if(!ComputeNavigationBuildInputHash(*source,sourceHash,error)) return Expect(false,error);
    source->footprints={{"runtime/coverage",sourceHash,minimum,maximum}};
    VansNavigationTileUpdater updater;
    if(!Expect(updater.Configure(settings,error) && updater.Observe(map,source,sourceHash,error) && !map->GetPendingCellCount(),error)) return false;
    const auto outsideBytes=EstimateNavigationQueryBytes(settings.query)+
        EstimateNavigationAvoidanceBytes(settings.query.maximumActiveAgents,settings.avoidance)+map->EstimateResidentBytes()+asset->EstimateResidentBytes();
    const auto residentTiles=map->GetResidentTileCount();
    auto invalid=std::make_shared<VansNavigationProfileBuildInput>(*source);
    auto invalidGeometry=std::make_shared<VansNavigationGeometry>(*source->geometry);
    if(invalidGeometry->areas.empty())
    {
        // Terrain 也走同一 Builder 验证；冻结输入中额外的非法 Area 不能进入 native。
        invalidGeometry->vertices={minimum.x,minimum.y,minimum.z,maximum.x,minimum.y,minimum.z,minimum.x,minimum.y,maximum.z};
        invalidGeometry->indices={0,2,1};
    }
    invalidGeometry->areas.assign(invalidGeometry->TriangleCount(),63);
    invalid->geometry=invalidGeometry;
    std::uint64_t invalidHash=0;
    if(!ComputeNavigationBuildInputHash(*invalid,invalidHash,error)) return Expect(false,error);
    invalid->footprints.front().contentHash=invalidHash;
    if(!Expect(updater.Observe(map,invalid,invalidHash,error) && map->GetPendingCellCount()>0,error)) return false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!updater.CaptureStats().failedMaps && std::chrono::steady_clock::now()<deadline)
    {updater.Tick(outsideBytes,residentTiles,error); std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    const auto failedStats=updater.CaptureStats();
    if(!Expect(failedStats.failedMaps==1 && map->IsReady() && map->GetPendingCellCount()>0 &&
        map->GetResidentTileCount()==residentTiles,"Failed Runtime worker changed native data or made Dirty data usable")) return false;
    for(int frame=0;frame<12;++frame) if(!Expect(!updater.Tick(outsideBytes,residentTiles,error) &&
        updater.CaptureStats().startedBuilds==failedStats.startedBuilds,"Unchanged failed CPU input was repeatedly rebuilt")) return false;
    if(!Expect(updater.Observe(map,source,sourceHash,error),error)) return false;
    const auto restoredDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(map->GetPendingCellCount() && std::chrono::steady_clock::now()<restoredDeadline)
    {
        if(!Expect(updater.Tick(outsideBytes,residentTiles,error),error)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!Expect(!map->GetPendingCellCount() && !updater.CaptureStats().failedMaps,"Valid current input did not recover a failed Dirty build")) return false;
    {
        std::atomic<bool> release{false}; std::atomic<int> entered{0};
        const auto occupy=[&](){++entered; while(!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        const auto gate=jobs.QueueJobGroup({occupy,occupy});
        VansRuntimePhaseGuard gateCleanup([&](){release=true; gate->Wait();});
        const auto gateDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(entered.load()<2 && std::chrono::steady_clock::now()<gateDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!Expect(entered.load()==2,"Runtime retirement fixture cannot occupy workers")) return false;
        auto changed=std::make_shared<VansNavigationProfileBuildInput>(*source);
        changed->contributions.front().contentHash^=1u;
        std::uint64_t hash=0;
        if(!ComputeNavigationBuildInputHash(*changed,hash,error)) return Expect(false,error);
        changed->footprints.front().contentHash=hash;
        if(!Expect(updater.Observe(map,changed,hash,error) && updater.Tick(outsideBytes,residentTiles,error) &&
            updater.CaptureStats().activeBuilds==1,error)) return false;
        std::weak_ptr<VansNavigationMap> lifetime=map;
        updater.WithdrawMap(asset->GetMapId()); map.reset();
        const auto retiring=updater.CaptureStats();
        if(!Expect(lifetime.expired() && retiring.retiringBuilds==1 && retiring.concurrentBuildBytes>0 &&
            retiring.concurrentBuildBytes<=static_cast<std::uint64_t>(settings.maximumConcurrentBuildBytes),
            "Cancelled CPU worker retained its native Map or released its real build quota before completion")) return false;
    }
    updater.Shutdown();
    if(!Expect(updater.CaptureStats().retiringBuilds==0 && updater.CaptureStats().concurrentBuildBytes==0,
        "Runtime shutdown retained worker reservations")) return false;
    std::cout<<"[NavigationRuntimeUpdater] PASS: real failed worker, Dirty/native preservation, no retry storm, current-input recovery, immediate Map release and retiring quota\n";
    return true;
}

bool CheckSelectedTileBuild(const Vans::VansNavigationBuildInput& input,const Vans::VansNavigationSettings& settings,
    const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset)
{
    using namespace Vans;
    std::string error;
    for(const auto& source:input.profiles)
    {
        VansNavigationProfileBuildData bakedData; std::string readError;
    if(!Expect(asset->ReadProfile(source.profileId,bakedData,readError),readError)) return false;
    const auto* baked=&bakedData;
        if(!Expect(baked && !baked->tiles.empty(),"Selected Tile fixture has no native baseline")) return false;
        if(!CheckNativeTileRollback(*baked,settings.query)) return false;
        if(!CheckLocalTilePublication(source,settings,asset)) return false;
        if(!CheckRuntimeTileUpdater(source,settings,asset)) return false;
        dtMeshHeader header{};
        std::memcpy(&header,baked->tiles.front().bytes.data(),sizeof(header));
        std::vector<VansNavigationTileCell> cells;
        for(const auto& tile:baked->tiles)
        {
            const VansNavigationTileCell cell{tile.x,tile.z};
            if(std::find(cells.begin(),cells.end(),cell)==cells.end()) cells.push_back(cell);
            if(cells.size()==kMaximumNavigationTileUpdateBatch) break;
        }
        VansNavigationTileBuildOutput selected;
        const bool built=VansNavigationBuilder::BuildSelectedTiles(source,settings,input.mapId,cells,header.bmin[1],
            settings.maximumConcurrentBuildBytes,nullptr,selected,error);
        if(!Expect(built,error)) return false;
        std::size_t expectedCount=0;
        for(const auto& original:baked->tiles)
        {
            if(std::find(cells.begin(),cells.end(),VansNavigationTileCell{original.x,original.z})==cells.end()) continue;
            ++expectedCount;
            const auto actual=std::find_if(selected.tiles.begin(),selected.tiles.end(),[&](const auto& tile)
            { return tile.x==original.x && tile.z==original.z && tile.layer==original.layer; });
            if(!Expect(actual!=selected.tiles.end() && actual->bytes==original.bytes,
                "Selected Recast cells changed the existing native Tile payload or seam/vertical phase")) return false;
        }
        if(!Expect(selected.tiles.size()==expectedCount,"Selected build emitted an unrequested Tile")) return false;
        const auto retained=selected.tiles.front().bytes;
        auto repeated=cells; repeated.push_back(cells.front());
        VansNavigationBuildControl cancelled; cancelled.Cancel();
        const bool rejectedDuplicate=!VansNavigationBuilder::BuildSelectedTiles(source,settings,input.mapId,repeated,header.bmin[1],
            settings.maximumConcurrentBuildBytes,nullptr,selected,error);
        const bool rejectedBudget=!VansNavigationBuilder::BuildSelectedTiles(source,settings,input.mapId,cells,header.bmin[1],1,nullptr,selected,error);
        const bool rejectedCancellation=!VansNavigationBuilder::BuildSelectedTiles(source,settings,input.mapId,cells,header.bmin[1],
            settings.maximumConcurrentBuildBytes,&cancelled,selected,error);
        if(!Expect(rejectedDuplicate && rejectedBudget && rejectedCancellation && selected.tiles.front().bytes==retained,
            "Rejected selected Tile input/budget/cancellation overwrote the accepted candidate")) return false;
        VansNavigationProfileBuildInput removed; removed.profileId=source.profileId;
        removed.geometry=std::make_shared<const VansNavigationGeometry>();
        const bool deleted=VansNavigationBuilder::BuildSelectedTiles(removed,settings,input.mapId,cells,header.bmin[1],
            settings.maximumConcurrentBuildBytes,nullptr,selected,error);
        if(!Expect(deleted && selected.tiles.empty(),"Removing the final contribution did not produce an empty Tile replacement")) return false;
    }
    std::cout<<"[NavigationSelectedTiles] PASS: native payload equivalence, exact selection, atomic failure/cancellation and final-contribution deletion\n";
    return true;
}

bool CheckPhysicsNavigationSourceEquivalence()
{
    using namespace Vans; using namespace VansEngine;
    VANS_INIT_MAIN_THREAD(); VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
    auto& physics=VansPhysicsSystem::GetInstance(); std::string error;
    if(!Expect(physics.Initialize(),"Native collision equivalence cannot initialize Physics")) return false;
    VansRuntimePhaseGuard physicsCleanup([&](){physics.Shutdown();});
    std::vector<std::uint32_t> transforms;
    VansRuntimePhaseGuard transformCleanup([&](){for(const auto id:transforms) VansTransformStore::Release(id);});
    const auto allocate=[&](const VansTransform& value){const auto id=VansTransformStore::Allocate(); transforms.push_back(id); VansTransformStore::Write(id,value); return id;};
    VansTransform pose; pose.m_Position={1.23f,2.5f,-3.14f}; pose.m_Rotation={17,43,-11}; pose.m_Scale={1.2f,.8f,1.1f};
    std::vector<VansTransform> poses{pose};
    for(const std::array<float,4>& quaternion:std::array<std::array<float,4>,3>{{
        {0,-.7071068286895752f,0,.7071068286895752f},
        {0,.707109808921814f,0,-.7071038484573364f},
        {0,.7070977687835693f,0,.7071158289909363f}}})
    {
        auto sample=pose;
        const auto rotation=ProjectSceneQuaternionToEulerDegrees(quaternion,false);
        sample.m_Rotation={rotation[0],rotation[1],rotation[2]};
        poses.push_back(sample);
    }
    for(const auto& sample:poses)
    for(const auto type:{PhysicsColliderType::Box,PhysicsColliderType::Sphere,PhysicsColliderType::Capsule})
    {
        PhysicsNodeProperties properties; properties.enabled=true; properties.colliderType=type; properties.shapeOffset={.17f,-.11f,.3f};
        VansPhysicsNode node; const auto id=allocate(sample); node.Initialize(properties,id);
        VansPhysicsCollisionGeometrySnapshot captured; VansTriangleMeshData authored;
        if(!Expect(node.HasActor() && VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) &&
            VansPhysicsCollisionGeometry::Build(properties,sample,sample.m_Scale,nullptr,.05f,4*1024*1024,authored,error) &&
            captured.geometry->positions==authored.positions && captured.geometry->indices==authored.indices,
            "Native Box/Sphere/Capsule differs from the authoring exporter"))
        {
            std::cerr<<"Collider="<<static_cast<int>(type)<<", error="<<error<<'\n';
            if(captured.geometry) std::cerr<<"Captured="<<captured.geometry->positions.size()<<'/'<<captured.geometry->indices.size()<<", authored="<<authored.positions.size()<<'/'<<authored.indices.size()<<'\n';
            if(captured.geometry) for(std::size_t i=0;i<(std::min)(captured.geometry->positions.size(),authored.positions.size());++i)
                if(captured.geometry->positions[i]!=authored.positions[i])
                { std::cerr<<"First differing position["<<i<<"]="<<std::hexfloat<<captured.geometry->positions[i]<<" vs "<<authored.positions[i]<<std::defaultfloat<<'\n'; break; }
            return false;
        }
        const auto unchanged=captured.geometry;
        if(type==PhysicsColliderType::Box)
        {
            VansPhysicsBoxShapeSnapshot nativeBox,offlineBox;
            if(!Expect(VansPhysicsCollisionGeometry::CaptureBoxShape(node,nativeBox,error) &&
                VansPhysicsCollisionGeometry::BuildBoxShape(properties,sample,sample.m_Scale,offlineBox,error) &&
                nativeBox.worldTransform==offlineBox.worldTransform && nativeBox.halfExtents==offlineBox.halfExtents,
                "Actual/offline Box primitive facts differ from the shared physical pose/dimensions")) return false;
        }
        else
        {
            VansPhysicsBoxShapeSnapshot preserved; preserved.halfExtents=glm::vec3(77);
            if(!Expect(!VansPhysicsCollisionGeometry::CaptureBoxShape(node,preserved,error) && preserved.halfExtents==glm::vec3(77) &&
                !VansPhysicsCollisionGeometry::BuildBoxShape(properties,sample,sample.m_Scale,preserved,error) && preserved.halfExtents==glm::vec3(77),
                "Unsupported collider was approximated as a Box or replaced the last valid primitive facts")) return false;
        }
        if(!Expect(VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) && captured.geometry==unchanged,
            "Native collision capture reallocates unchanged CPU geometry")) return false;
        auto changed=sample; changed.m_Scale={-2.3f,.001f,.7f};
        VansTransformStore::Write(id,changed); node.SyncActorFromTransform();
        if(!Expect(!VansPhysicsCollisionGeometry::Capture(node,.05f,1,captured,error) && captured.geometry==unchanged,
            "Collision capture exceeds a tiny allocation budget or replaces valid frozen data on failure")) return false;
        if(!Expect(VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) && captured.geometry!=unchanged &&
            VansPhysicsCollisionGeometry::Build(properties,changed,sample.m_Scale,nullptr,.05f,4*1024*1024,authored,error) &&
            captured.geometry->positions==authored.positions && captured.geometry->indices==authored.indices,
            "Native absolute/clamped scale update differs from the authoring exporter")) return false;
        if(type==PhysicsColliderType::Box)
        {
            VansPhysicsBoxShapeSnapshot nativeBox,offlineBox;
            if(!Expect(VansPhysicsCollisionGeometry::CaptureBoxShape(node,nativeBox,error) &&
                VansPhysicsCollisionGeometry::BuildBoxShape(properties,changed,sample.m_Scale,offlineBox,error) &&
                nativeBox.worldTransform==offlineBox.worldTransform && nativeBox.halfExtents==offlineBox.halfExtents,
                "Box primitive facts do not preserve the actual absolute/clamped scale-update convention")) return false;
        }
    }
    VansTransform prototypePose; prototypePose.m_Position={0,0,0}; prototypePose.m_Rotation={0,0,0}; prototypePose.m_Scale={1,1,1};
    PhysicsNodeProperties prototype; prototype.enabled=true; prototype.boxExtents={.7f,.4f,1.2f};
    VansTriangleMeshData prototypeGeometry;
    if(!Expect(VansPhysicsCollisionGeometry::Build(prototype,prototypePose,prototypePose.m_Scale,nullptr,.05f,
        4*1024*1024,prototypeGeometry,error),error)) return false;
    for(const auto& sample:poses)
    for(const auto type:{PhysicsColliderType::Mesh,PhysicsColliderType::ConvexMesh})
    {
        PhysicsNodeProperties properties; properties.enabled=true; properties.colliderType=type;
        properties.useMeshCollider=true; properties.shapeOffset={.17f,-.11f,.3f};
        auto source=std::make_shared<VansTriangleMeshData>(prototypeGeometry);
        std::weak_ptr<const VansTriangleMeshData> sourceLifetime=source;
        VansPhysicsNode node; const auto id=allocate(sample); node.Initialize(properties,id,source);
        VansPhysicsCollisionGeometrySnapshot captured; VansTriangleMeshData authored;
        if(!Expect(node.HasActor() && VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) &&
            VansPhysicsCollisionGeometry::Build(properties,sample,sample.m_Scale,source.get(),.05f,4*1024*1024,authored,error) &&
            captured.geometry->positions==authored.positions && captured.geometry->indices==authored.indices,
            "CPU Mesh/Convex cooking differs from the authoring collision exporter"))
        { std::cerr<<"Collider="<<static_cast<int>(type)<<", error="<<error<<'\n'; return false; }
        // Release caller data immediately: subsequent actor/scale updates must only use the native mesh.
        source.reset();
        if(!Expect(sourceLifetime.expired(),"Cooked node retains the caller's CPU geometry")) return false;
        auto changed=sample; changed.m_Position+=glm::vec3(.25f,.15f,-.5f); changed.m_Scale={1.7f,.9f,.6f};
        VansTransformStore::Write(id,changed); node.SyncActorFromTransform();
        if(!Expect(VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) &&
            VansPhysicsCollisionGeometry::Build(properties,changed,sample.m_Scale,&prototypeGeometry,.05f,
                4*1024*1024,authored,error) && captured.geometry->positions==authored.positions &&
            captured.geometry->indices==authored.indices,
            "CPU mesh lifetime leaked into actor transform/shape updates")) return false;
    }
    for(const auto type:{PhysicsColliderType::Mesh,PhysicsColliderType::ConvexMesh})
    {
        PhysicsNodeProperties properties; properties.colliderType=type; properties.useMeshCollider=true;
        auto source=std::make_shared<VansTriangleMeshData>(prototypeGeometry);
        std::weak_ptr<const VansTriangleMeshData> sourceLifetime=source;
        VansPhysicsNode node; const auto id=allocate(pose); node.Initialize(properties,id,source); source.reset();
        if(!Expect(!node.HasActor() && !node.IsEnabled() && !sourceLifetime.expired(),
            "Disabled node loses its immutable CPU input before deferred cooking")) return false;
        node.SetEnabled(true);
        VansPhysicsCollisionGeometrySnapshot captured; VansTriangleMeshData authored;
        if(!Expect(node.HasActor() && node.IsInScene() && sourceLifetime.expired() &&
            VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) &&
            VansPhysicsCollisionGeometry::Build(properties,pose,pose.m_Scale,&prototypeGeometry,.05f,
                4*1024*1024,authored,error) && captured.geometry->positions==authored.positions &&
            captured.geometry->indices==authored.indices,
            "Deferred CPU Mesh/Convex activation differs from authoring geometry")) return false;
        const auto actor=node.GetActorIdentity(); const auto frozen=captured.geometry;
        node.SetEnabled(false);
        if(!Expect(!node.IsInScene(),"Disabled cooked mesh remains in the Physics scene")) return false;
        node.SetEnabled(true);
        if(!Expect(node.GetActorIdentity()==actor && node.IsInScene() &&
            VansPhysicsCollisionGeometry::Capture(node,.05f,4*1024*1024,captured,error) && captured.geometry==frozen,
            "Cooked mesh enable toggle changes the actor or its frozen navigation geometry")) return false;
    }
    {
        auto malformed=prototypeGeometry; malformed.indices.front()=static_cast<std::uint32_t>(malformed.VertexCount());
        if(!Expect(!malformed.Validate(4*1024*1024,error),"Neutral mesh accepts an out-of-range index")) return false;
        malformed=prototypeGeometry; malformed.positions.front()=(std::numeric_limits<float>::infinity)();
        if(!Expect(!malformed.Validate(4*1024*1024,error),"Neutral mesh accepts a non-finite vertex")) return false;
        malformed=prototypeGeometry; malformed.positions.pop_back();
        if(!Expect(!malformed.Validate(4*1024*1024,error),"Neutral mesh accepts an incomplete packed vertex")) return false;
        malformed=prototypeGeometry; malformed.indices.pop_back();
        if(!Expect(!malformed.Validate(4*1024*1024,error),"Neutral mesh accepts an incomplete packed triangle")) return false;
        const auto bytes=prototypeGeometry.positions.size()*sizeof(float)+prototypeGeometry.indices.size()*sizeof(std::uint32_t);
        if(!Expect(prototypeGeometry.Validate(bytes,error) && !prototypeGeometry.Validate(bytes-1,error),
            "Neutral mesh allocation budget does not include indices")) return false;
    }
    for(const bool hasGrandparent:{false,true})
    {
    VansSceneContentBuildPlan scene; VansSceneObjectBuildConfig parent;
    parent.entityGuid=VansAssetGuid::FromStableName("NavigationEquivalence","Parent").ToString(); parent.transform=VansSceneTransformConfig{};
    parent.transform->position={3,.5f,-2}; parent.transform->rotation={11,31,-7}; parent.transform->scale={2,2,2};
    VansTransform grandPose; grandPose.m_Position={-2,.7f,1}; grandPose.m_Rotation={-8,27,5}; grandPose.m_Scale={1.4f,1.4f,1.4f};
    VansSceneObjectBuildConfig grand;
    if(hasGrandparent)
    {
        grand.entityGuid=VansAssetGuid::FromStableName("NavigationEquivalence","Grandparent").ToString();
        grand.transform=VansSceneTransformConfig{};
        grand.transform->position={-2,.7f,1}; grand.transform->rotation={-8,27,5}; grand.transform->scale={1.4f,1.4f,1.4f};
        scene.objects.objects.push_back(grand);
        parent.parent=VansSceneParentReference{}; VansEntityGuid::TryParse(grand.entityGuid,parent.parent->entityGuid);
    }
    scene.objects.objects.push_back(parent);
    AppendEnvironmentBox(scene.objects,"NestedCollision",{.3f,.2f,-.6f},{1,.2f,1});
    auto& child=scene.objects.objects.back(); child.transform->rotation={13,-24,19}; child.transform->scale={.7f,1.3f,1.1f};
    child.physicsComponents.physics->layer="Default"; child.physicsComponents.physics->shapeOffset=std::array<float,3>{.17f,-.11f,.3f};
    child.parent=VansSceneParentReference{}; VansEntityGuid::TryParse(parent.entityGuid,child.parent->entityGuid);
    child.navigationSurface=VansSceneNavigationSurfaceConfig{}; child.navigationSurface->runtime.boundsHalfExtentsMeters={5,5,5};
    child.componentGuids["navigation_surface"]="Equivalence.Surface";
    child.navigationLink=VansSceneNavigationLinkConfig{};
    child.componentGuids["navigation_link"]="Equivalence.Link";
    VansNavigationSettings settings; VansNavigationSourceSnapshot offline,live; VansNavigationBuildInput offlineInput,liveInput;
    if(!Expect(VansSceneNavigationGeometry::CaptureSnapshot(scene,Vans::VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},offline,error),error)) return false;
    VansTransform parentPose; parentPose.m_Position={3,.5f,-2}; parentPose.m_Rotation={11,31,-7}; parentPose.m_Scale={2,2,2};
    VansTransform local; local.m_Position={.3f,.2f,-.6f}; local.m_Rotation={13,-24,19}; local.m_Scale={.7f,1.3f,1.1f};
    const auto parentTransform=allocate(parentPose),childTransform=allocate(local);
    PhysicsNodeProperties properties; properties.enabled=true; properties.boxExtents={1,.2f,1}; properties.shapeOffset={.17f,-.11f,.3f};
    VansPhysicsNode node; node.Initialize(properties,childTransform);
    VansTransformGraph graph;
    if(hasGrandparent && !graph.SetParent(parentTransform,allocate(grandPose),VansTransformReparentMode::KeepLocal))
        return Expect(false,graph.GetLastError().c_str());
    if(!Expect(graph.SetParent(childTransform,parentTransform,VansTransformReparentMode::KeepLocal) && graph.Resolve(),graph.GetLastError().c_str())) return false;
    node.SyncActorFromTransform();
    VansRuntimeWorld runtime; VansEntityCreateDesc parentDesc; parentDesc.stableGuid=parent.entityGuid;
    if(hasGrandparent) parentDesc.parent=runtime.CreateEntity({grand.entityGuid,"Grandparent",{},true});
    const auto parentOwner=runtime.CreateEntity(parentDesc);
    VansEntityCreateDesc childDesc; childDesc.stableGuid=child.entityGuid; childDesc.parent=parentOwner;
    const auto owner=runtime.CreateEntity(childDesc);
    runtime.AddComponent(owner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&node},child.componentGuids.at("physics"));
    runtime.AddComponent(owner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{childTransform},"Equivalence.Transform");
    runtime.AddComponent(owner,VansRuntimeComponentType_NavigationSurface,child.navigationSurface->runtime,"Equivalence.Surface");
    runtime.AddComponent(owner,VansRuntimeComponentType_NavigationLink,child.navigationLink->runtime,"Equivalence.Link");
    VansRuntimeNavigationSources provider; std::uint64_t observation=0,offlineHash=0,liveHash=0;
    if(!Expect(provider.Capture(runtime,settings,{}, {},live,observation,error) &&
        VansNavigationSourceCollector::Collect(offline,settings,"World",offlineInput,error) &&
        VansNavigationSourceCollector::Collect(live,settings,"World",liveInput,error) &&
        ComputeNavigationBuildInputHash(offlineInput.profiles.front(),offlineHash,error) &&
        ComputeNavigationBuildInputHash(liveInput.profiles.front(),liveHash,error) && offlineHash==liveHash,
        "Real TransformGraph hierarchy/shape offset differs from the offline Surface/source fingerprint")) return false;
    const auto currentWorld=VansTransformStore::Read(childTransform).GetModelMatrix();
    const auto currentGeneration=VansTransformStore::GetGeneration(childTransform);
    scene.objects.objects.front().active=false;
    if(!VansSceneNavigationGeometry::CaptureSnapshot(scene,Vans::VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},offline,error)) return Expect(false,error);
    if(!Expect(!offline.surfaces.front().enabled && VansTransformStore::Read(childTransform).GetModelMatrix()==currentWorld &&
        VansTransformStore::GetGeneration(childTransform)==currentGeneration,
        "Offline hierarchy ignored inactive ancestors or modified an existing live Transform")) return false;
    const auto preserved=offline.sources.front().geometry;
    auto& root=scene.objects.objects.front(); root.parent=VansSceneParentReference{};
    VansEntityGuid::TryParse(child.entityGuid,root.parent->entityGuid);
    if(!Expect(!VansSceneNavigationGeometry::CaptureSnapshot(scene,Vans::VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},offline,error) &&
        offline.sources.front().geometry==preserved && VansTransformStore::Read(childTransform).GetModelMatrix()==currentWorld,
        "Cyclic offline hierarchy published a partial snapshot or changed live Transform storage")) return false;
    }
    {
        VansNavigationSettings settings; settings.maps.front().updateMode=VansNavigationMapUpdateMode::Runtime;
        VansSceneContentBuildPlan scene;
        VansSceneObjectBuildConfig parent; parent.entityGuid=VansAssetGuid::New().ToString(); parent.transform=VansSceneTransformConfig{};
        parent.transform->position={1,.5f,-2}; parent.transform->rotation={0,37,0}; parent.transform->scale={1.3f,1.3f,1.3f};
        scene.objects.objects.push_back(parent);
        AppendEnvironmentBox(scene.objects,"BoundCollider.Target",{.3f,.2f,-.6f},{.7f,1,.6f});
        auto& target=scene.objects.objects.back(); target.transform->rotation={0,-24,0}; target.transform->scale={.7f,1.3f,1.1f};
        target.physicsComponents.physics->shapeOffset=std::array<float,3>{.17f,-.11f,.3f};
        target.parent=VansSceneParentReference{}; VansEntityGuid::TryParse(parent.entityGuid,target.parent->entityGuid);
        const auto targetGuid=target.entityGuid,physicsGuid=target.componentGuids.at("physics");
        const auto targetConfig=target;
        VansSceneObjectBuildConfig navigation; navigation.entityGuid=VansAssetGuid::New().ToString(); navigation.transform=VansSceneTransformConfig{};
        navigation.transform->position={100,0,100}; navigation.navigationObstacle=VansSceneNavigationObstacleConfig{};
        navigation.navigationObstacle->runtime.shape=VansNavigationObstacleCollider{targetGuid,physicsGuid};
        const auto navigationGuid=VansAssetGuid::New().ToString(); navigation.componentGuids["navigation_obstacle"]=navigationGuid;
        scene.objects.objects.push_back(navigation);
        VansNavigationSourceSnapshot offline,live;
        if(!Expect(VansSceneNavigationGeometry::CaptureSnapshot(scene,VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},offline,error) &&
            offline.obstacles.size()==1,error)) return false;
        const auto transform=[](const VansSceneTransformConfig& value)
        {
            VansTransform result; result.m_Position={value.position[0],value.position[1],value.position[2]};
            result.m_Rotation={value.rotation[0],value.rotation[1],value.rotation[2]}; result.m_Scale={value.scale[0],value.scale[1],value.scale[2]}; return result;
        };
        const auto parentTransform=allocate(transform(*parent.transform)),targetTransform=allocate(transform(*targetConfig.transform));
        PhysicsNodeProperties properties; properties.enabled=true; properties.boxExtents={.7f,1,.6f}; properties.shapeOffset={.17f,-.11f,.3f};
        VansPhysicsNode box; box.Initialize(properties,targetTransform);
        if(!Expect(box.HasActor(),"Collider equivalence fixture did not create its physical actor")) return false;
        VansTransformGraph graph;
        if(!Expect(graph.SetParent(targetTransform,parentTransform,VansTransformReparentMode::KeepLocal) && graph.Resolve(),graph.GetLastError())) return false;
        box.SyncActorFromTransform();
        VansRuntimeWorld runtime;
        const auto targetOwner=runtime.CreateEntity({targetGuid,"Target",{},true});
        runtime.AddComponent(targetOwner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&box},physicsGuid);
        const auto navigationOwner=runtime.CreateEntity({navigation.entityGuid,"Obstacle",{},true});
        runtime.AddComponent(navigationOwner,VansRuntimeComponentType_NavigationObstacle,navigation.navigationObstacle->runtime,navigationGuid);
        VansRuntimeNavigationSources provider; std::uint64_t observation=0;
        if(!Expect(provider.Capture(runtime,settings,{}, {},live,observation,error) && live.obstacles.size()==1 &&
            live.obstacles.front().shape.center==offline.obstacles.front().shape.center &&
            live.obstacles.front().shape.halfExtents==offline.obstacles.front().shape.halfExtents &&
            live.obstacles.front().shape.yawRadians==offline.obstacles.front().shape.yawRadians,
            "Collider binding offline/live facts differ across parent TRS, physical offset and scale-update phase")) return false;
        auto wrong=scene; wrong.objects.objects.back().navigationObstacle->runtime.shape=VansNavigationObstacleCollider{parent.entityGuid,physicsGuid};
        if(!Expect(!VansSceneNavigationGeometry::CaptureSnapshot(wrong,VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},offline,error) &&
            offline.obstacles.size()==1,"Incorrectly owned offline Collider binding changed the last valid snapshot")) return false;
        scene.objects.objects[1].physicsComponents.physics->enabled=false;
        if(!Expect(VansSceneNavigationGeometry::CaptureSnapshot(scene,VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},offline,error) &&
            offline.obstacles.empty(),"Disabled offline Collider retained a bound obstacle")) return false;
        std::cout<<"[NavigationColliderSnapshot] PASS: actual/offline parent TRS and physical offset/scale facts, independent owner, invalid ownership rollback and disabled withdrawal\n";
    }
    std::cout<<"[NavigationPhysics] PASS: native shape/pose, absolute scale update, immutable capture, shared 2/3-level hierarchy, activity/cycle rollback and live Transform isolation\n";
    return true;
}

bool CheckCommittedPcgCollisionSources()
{
    using namespace Vans; using namespace VansEngine;
    VANS_INIT_MAIN_THREAD(); VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
    auto& physics=VansPhysicsSystem::GetInstance(); std::string error;
    const auto check=[&](bool condition,const char* message){ return Expect(condition,(std::string(message)+": "+error).c_str()); };
    if(!check(physics.Initialize(),"PCG collision fixture cannot initialize Physics")) return false;
    VansRuntimePhaseGuard physicsCleanup([&](){physics.Shutdown();});
    auto& layers=VansCollisionLayerManager::Get(); VansCollisionLayerConfig previous;
    previous.layerCount=layers.GetLayerCount();
    for(int i=0;i<previous.layerCount;++i) { previous.layerNames[i]=layers.GetLayerName(i); previous.collisionMasks[i]=layers.GetCollisionMask(i); }
    VansRuntimePhaseGuard layerCleanup([&](){layers.ApplyConfig(previous);});
    VansCollisionLayerConfig fixture; fixture.ResetToDefaults(); fixture.layerCount=2;
    fixture.layerNames[1]="Environment"; fixture.collisionMasks[0]=fixture.collisionMasks[1]=3; layers.ApplyConfig(fixture);
    std::vector<std::uint32_t> transforms;
    VansRuntimePhaseGuard transformCleanup([&](){for(const auto id:transforms) VansTransformStore::Release(id);});
    const auto allocate=[&](const glm::vec3& position)
    {
        const auto id=VansTransformStore::Allocate(); transforms.push_back(id);
        VansTransform pose; pose.m_Position=position; pose.m_Rotation={0,0,0}; pose.m_Scale={1,1,1};
        VansTransformStore::Write(id,pose); return id;
    };
    const auto guid=[](const char* name){return VansAssetGuid::FromStableName("PcgCollisionContract",name);};
    const auto plantGuid=guid("Plant"), modelGuid=guid("Collision"), materialGuid=guid("Material");
    VansPlantTypeAsset plant; plant.name="Explicit collision fixture"; plant.category=VansPlantCategory::Grass;
    plant.grass.boneCount=3; plant.grass.bladeHeight=1;
    VansPlantVariant variant; variant.id="solid"; variant.name=variant.id; variant.geometry=VansPlantGeometry::Mesh;
    variant.weight=1; variant.cullingRadius=4; variant.parts={{"surface",VansPlantPartKind::Surface,guid("Appearance"),-1,materialGuid}};
    variant.offset={100,20,30}; variant.scale={3,2,4}; // Render calibration must not move the collision prototype.
    variant.collision=VansPlantCollisionPrototype{modelGuid,"Environment"}; plant.variants.push_back(variant);
    variant.id="visual"; variant.name=variant.id; variant.collision.reset(); plant.variants.push_back(variant);
    VansAssetObjectRepository repository;
    if(!check(repository.Publish<VansPlantTypeAsset>(plantGuid,VansAssetType::PlantType,1,
        std::make_shared<const VansPlantTypeAsset>(plant),plant.Dependencies(),error).IsValid(),"PCG collision operation")) return false;
    VansPcgRecipeAsset recipe; recipe.name="Committed collision";
    VansPcgRegion region; region.id="region"; region.name=region.id; region.enabled=true;
    region.bounds={{-5,-5},{5,5}}; region.cellSize=4; region.surface.kind=VansPcgSurfaceKind::Plane;
    VansPcgLayer layer; layer.id="layer"; layer.name=layer.id; layer.enabled=true; layer.plant=plantGuid;
    layer.source=VansPcgSourceMode::Fixed; layer.budget={100,100};
    const auto maskGuid=guid("Mask"), pixelGuid=guid("Pixels"); layer.densityMask=maskGuid;
    VansPcgMaskAsset mask; mask.name="Explicit mask"; mask.pixelAsset=pixelGuid;
    mask.mask.target={region.id,layer.id,maskGuid.ToString()}; mask.mask.bounds=region.bounds;
    mask.mask.width=mask.mask.height=2; mask.mask.pixels.assign(4,65535);
    if(!check(repository.Publish<VansPcgMaskAsset>(maskGuid,VansAssetType::PcgMask,2,
        std::make_shared<const VansPcgMaskAsset>(mask),mask.Dependencies(),error).IsValid(),"PCG collision operation")) return false;
    layer.fixedInstances={{"wall","solid",{0,1.5f,0}}, {"visual-a","visual",{-3,0,-3}}, {"visual-b","visual",{3,0,3}}};
    region.layers={layer}; recipe.regions={region};
    VansPcgUpdatePlan update; VansPcgCollisionPlan collisions;
    if(!check(VansPcgUpdatePlanner::PlanRecipe(recipe,repository,{},std::nullopt,{},VansPcgUpdatePartition::PerLayer,update,error) &&
        BuildPcgCollisionPlan(update,collisions,error) && collisions.instances.size()==1 && collisions.noCollisionPrototype.size()==1 &&
        collisions.noCollisionPrototype.front().instanceCount==2,("Actual recipe planner lost collision or render-only exclusions: "+error).c_str())) return false;
    const auto identity=collisions.instances.front();
    auto moved=recipe; moved.regions.front().cellSize=2; moved.regions.front().layers.front().fixedInstances.front().position[0]=4;
    VansPcgCollisionPlan movedCollisions;
    if(!check(VansPcgUpdatePlanner::PlanRecipe(moved,repository,{},std::nullopt,{},VansPcgUpdatePartition::PerLayer,update,error) &&
        BuildPcgCollisionPlan(update,movedCollisions,error) && movedCollisions.instances.front().ownerGuid==identity.ownerGuid &&
        movedCollisions.instances.front().componentGuid==identity.componentGuid,"PCG identity changed with position/cell partition")) return false;
    const auto resources=BuildPcgResourcePlan(recipe,repository,[&](VansAssetGuid id)->std::optional<VansAssetRecord>
    {
        VansAssetRecord record; record.guid=id;
        record.type=id==plantGuid?VansAssetType::PlantType:id==materialGuid?VansAssetType::Material:
            id==maskGuid?VansAssetType::PcgMask:id==pixelGuid?VansAssetType::Texture:VansAssetType::Model; return record;
    });
    if(!check(resources && std::any_of(resources.resources.begin(),resources.resources.end(),[&](const auto& item)
        {return item.guid==modelGuid && item.type==VansAssetType::Model && item.meshCpuData;}),"Collision CPU Model is absent from the real resource closure")) return false;
    PhysicsNodeProperties box; box.enabled=true; box.boxExtents={.5f,1.5f,2};
    auto mesh=std::make_shared<VansTriangleMeshData>(); VansTransform origin;
    origin.m_Position={0,0,0}; origin.m_Rotation={0,0,0}; origin.m_Scale={1,1,1};
    if(!check(VansPhysicsCollisionGeometry::Build(box,origin,origin.m_Scale,nullptr,.05f,4*1024*1024,*mesh,error),"PCG collision operation")) return false;
    VansRuntimeWorld runtime; std::unique_ptr<VansPcgPhysicsInstances> instances;
    const auto resolve=[&](const VansAssetGuid& id,std::shared_ptr<const VansTriangleMeshData>& output,std::string& detail)
    { if(id!=modelGuid) { detail="Missing fixture Model"; return false; } output=mesh; detail.clear(); return true; };
    if(!check(VansPcgPhysicsInstances::Create(runtime,collisions,resolve,instances,error) && instances &&
        runtime.Entities().FindByGuid(identity.ownerGuid).IsValid() && runtime.FindComponentByGuid(identity.componentGuid).IsValid(),"PCG collision operation")) return false;
    VansSceneContentBuildPlan scene; scene.vegetation=recipe;
    AppendEnvironmentBox(scene.objects,"PcgFloor",{0,-.1f,0},{5,.1f,5});
    const auto& authoredFloor=scene.objects.objects.front();
    box.boxExtents={5,.1f,5}; box.layerName="Environment";
    VansPhysicsNode floor; floor.Initialize(box,allocate({0,-.1f,0}));
    const auto floorOwner=runtime.CreateEntity({authoredFloor.entityGuid,"Floor",{},true});
    runtime.AddComponent(floorOwner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&floor},authoredFloor.componentGuids.at("physics"));
    runtime.AddComponent(floorOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{floor.GetTransformID()},"PcgFloor.Transform");
    VansNavigationSettings settings; auto surface=ContractSurface(settings);
    VansSceneObjectBuildConfig surfaceObject; surfaceObject.entityGuid=guid("Surface").ToString(); surfaceObject.transform=VansSceneTransformConfig{};
    surfaceObject.navigationSurface=VansSceneNavigationSurfaceConfig{}; surfaceObject.navigationSurface->runtime=surface.surface;
    surfaceObject.componentGuids["navigation_surface"]=surface.id; scene.objects.objects.push_back(surfaceObject);
    const auto surfaceOwner=runtime.CreateEntity({surfaceObject.entityGuid,"Surface",{},true});
    runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,0})},"PcgSurface.Transform");
    runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationSurface,surface.surface,surface.id);
    VansRuntimeNavigationSources provider; VansNavigationSourceSnapshot live,offline; std::uint64_t observation=0;
    const auto capture=[&](){instances->SynchronizeLifecycle(); return provider.Capture(runtime,settings,{}, {},live,observation,error);};
    if(!check(capture() && live.sources.size()==2 &&
        std::any_of(live.sources.begin(),live.sources.end(),[&](const auto& source)
        {return source.kind==VansNavigationGeometryKind::Pcg && source.id==identity.componentGuid && source.geometry && source.boundsMaximum.x<1;}),
        "Committed real Physics collider was not captured as stable point-local PCG geometry")) return false;
    if(!check(VansSceneNavigationGeometry::CaptureSnapshot(scene,repository,settings,{"Default","Environment"},
        [&](const std::string& id,VansTriangleMeshData& output,std::string& detail)
        { if(id!=modelGuid.ToString()) { detail="Missing CPU prototype"; return false; } output=*mesh; detail.clear(); return true; },offline,error) &&
        offline.exclusions.size()==1 && offline.exclusions.front().instanceCount==2,"PCG collision operation")) return false;
    VansNavigationBuildInput liveInput,offlineInput; std::uint64_t liveHash=0,offlineHash=0;
    if(!check(VansNavigationSourceCollector::Collect(live,settings,"World",liveInput,error) &&
        VansNavigationSourceCollector::Collect(offline,settings,"World",offlineInput,error) &&
        ComputeNavigationBuildInputHash(liveInput.profiles.front(),liveHash,error) &&
        ComputeNavigationBuildInputHash(offlineInput.profiles.front(),offlineHash,error) && liveHash==offlineHash,
        "Actual committed PCG collision and offline recipe/cooking produced different navigation inputs")) return false;
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!check(VansNavigationBuilder::Build(liveInput,settings,"Scenes/PcgCollisionContract.json",nullptr,asset,error),"PCG collision operation")) return false;
    if(!CheckSelectedTileBuild(liveInput,settings,asset)) return false;
    auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query;
    if(!check(map->Initialize(asset,settings,"Humanoid",error) && query.Initialize(map,settings.query,"Default",error),"PCG collision operation")) return false;
    const auto ray=query.NavRaycast({-4,0,0},{4,0,0},{1,2,1}); const auto path=query.FindPath({-4,0,0},{4,0,0});
    if(!check(ray.valid && ray.hit && path.status==VansNavigationPathStatus::Complete,
        "Navigation did not block/reroute around the actual PCG collider")) return false;
    CharControllerProperties properties; properties.m_Radius=.35f; properties.m_Height=1.1f;
    properties.m_StepOffset=.25f; properties.m_ContactOffset=.02f; properties.m_PositionOffset={0,.9f,0};
    VansCharacterControllerNode controller;
    if(!check(controller.Initialize(properties,allocate({-4,.02f,0}),{-4,.92f,0}),"PCG real CCT initialization failed")) return false;
    const auto move=[&]()
    {
        controller.QueueMove({.04f,-.02f,0},1.f/60);
        std::lock_guard<std::mutex> lease(physics.GetSimulationMutex()); controller.FlushMoveAndSync();
    };
    for(int i=0;i<200;++i) move();
    const auto blockedPosition=controller.GetPosition();
    if(!check(blockedPosition.x>-1.2f && blockedPosition.x<-.7f,"Real CCT crossed the committed PCG triangle collider")) return false;
    const auto collider=runtime.FindComponentByGuid(identity.componentGuid);
    if(!check(runtime.SetComponentEnabled(collider,false) && capture() &&
        std::none_of(live.sources.begin(),live.sources.end(),[](const auto& source){return source.kind==VansNavigationGeometryKind::Pcg && source.enabled;}),
        "Runtime disabling did not withdraw the committed PCG collision")) return false;
    for(int i=0;i<100;++i) move();
    if(!check(controller.GetPosition().x>2,"PCG disabled collider still blocked the real CCT")) return false;
    if(!check(runtime.SetComponentEnabled(collider,true) && capture(),"PCG collision operation")) return false;
    const auto* transformsStorage=runtime.FindStorage<VansRuntimeTransformComponent>(VansRuntimeComponentType_Transform);
    const auto collisionOwner=runtime.Entities().FindByGuid(identity.ownerGuid);
    const auto transformSlot=transformsStorage->Get(transformsStorage->FindFirstOwnedBy(collisionOwner))->transformStoreId;
    auto movedPose=VansTransformStore::Read(transformSlot); movedPose.m_Position.x=1;
    VansTransformStore::Write(transformSlot,movedPose); VansTransformStore::MarkDirty(transformSlot);
    if(!check(capture() && std::any_of(live.sources.begin(),live.sources.end(),[](const auto& source)
        {return source.kind==VansNavigationGeometryKind::Pcg && source.geometry && source.boundsMinimum.x>.49f;}),
        "Committed PCG transform did not synchronize its actual native collider")) return false;
    const auto retained=instances.get();
    auto invalid=collisions; invalid.instances.push_back(identity);
    invalid.instances[0].ownerGuid=guid("FailedA").ToString(); invalid.instances[0].componentGuid=guid("FailedACollider").ToString();
    invalid.instances[1].ownerGuid=guid("FailedB").ToString(); invalid.instances[1].componentGuid=guid("FailedBCollider").ToString();
    invalid.instances[1].prototype.mesh=guid("Broken");
    auto degenerate=std::make_shared<VansTriangleMeshData>(); degenerate->positions={0,0,0,0,0,0,0,0,0}; degenerate->indices={0,1,2};
    if(!check(!VansPcgPhysicsInstances::Create(runtime,invalid,[&](const VansAssetGuid& id,
        std::shared_ptr<const VansTriangleMeshData>& output,std::string& detail)
        { output=id==modelGuid?mesh:degenerate; detail.clear(); return true; },instances,error) && instances.get()==retained &&
        !runtime.Entities().FindByGuid(invalid.instances[0].ownerGuid).IsValid() &&
        !runtime.Entities().FindByGuid(invalid.instances[1].ownerGuid).IsValid() && capture() && live.sources.size()==2,
        "Failed native PCG cooking published a partial entity/actor set or replaced the retained collection")) return false;
    {
        VansPhysicsNode first,second; PhysicsNodeProperties triangle;
        triangle.enabled=true; triangle.colliderType=PhysicsColliderType::Mesh; triangle.useMeshCollider=true;
        const auto firstTransform=allocate({20,1.5f,20}),secondTransform=allocate({30,1.5f,30});
        auto scaled=VansTransformStore::Read(secondTransform); scaled.m_Scale={2,.7f,.5f}; VansTransformStore::Write(secondTransform,scaled);
        if(!check(VansPhysicsNode::InitializeStaticMeshBatch({{&first,triangle,firstTransform,mesh},
            {&second,triangle,secondTransform,mesh}},error),"Shared static prototype batch failed")) return false;
        physx::PxShape* firstShape=nullptr; physx::PxShape* secondShape=nullptr;
        auto* firstActor=static_cast<const physx::PxRigidActor*>(first.GetActorIdentity());
        auto* secondActor=static_cast<const physx::PxRigidActor*>(second.GetActorIdentity());
        firstActor->getShapes(&firstShape,1); secondActor->getShapes(&secondShape,1);
        const physx::PxGeometryHolder firstNative(firstShape->getGeometry()),secondNative(secondShape->getGeometry());
        const auto firstGeometry=firstNative.triangleMesh(),secondGeometry=secondNative.triangleMesh();
        if(!check(firstGeometry.triangleMesh==secondGeometry.triangleMesh &&
            firstGeometry.scale.scale.x==1 && secondGeometry.scale.scale.x==2,
            "Batch did not share one cooked prototype with independent native shape scales")) return false;
        first.Shutdown(); VansPhysicsCollisionGeometrySnapshot secondCapture;
        if(!check(second.HasActor() && VansPhysicsCollisionGeometry::Capture(second,.05f,4*1024*1024,secondCapture,error) &&
            secondCapture.geometry && secondCapture.boundsMaximum.x>30.9f,
            "Releasing one shared mesh actor invalidated the other actor's native prototype")) return false;
    }
    const auto owner=runtime.Entities().FindByGuid(identity.ownerGuid);
    if(!check(runtime.DestroyEntity(owner) && capture() && live.sources.size()==1,
        "Destroyed PCG instance left a native collision or navigation source")) return false;
    instances.reset();
    std::cout<<"[PcgCollision] PASS: real recipe, CPU closure, stable identity, offline/live equivalence, real CCT block/disable and cooking rollback\n";
    return true;
}

bool CheckNavigationSourceCollection()
{
    using namespace Vans;
    VansNavigationSettings settings;
    settings.areas.definitions.push_back({"Slow",1u,2.f,true});
    VansNavigationSourceSnapshot snapshot; snapshot.collisionLayers={"Default","Other"};
    auto mesh=std::make_shared<VansTriangleMeshData>();
    mesh->positions={-10,0,-10,10,0,-10,-10,0,10,10,0,10}; mesh->indices={0,2,1,1,2,3};
    VansNavigationSourceRecord source; source.id="Floor"; source.ownerId="Child"; source.ancestors={"SurfaceOwner"};
    source.geometry=mesh; source.collisionLayer="Default";
    source.boundsMinimum=glm::vec3(-10,0,-10); source.boundsMaximum=glm::vec3(10,0,10);
    snapshot.sources.push_back(source);
    auto surface=ContractSurface(settings); surface.id="A"; surface.ownerId="SurfaceOwner";
    surface.surface.boundsHalfExtentsMeters=glm::vec3(2,5,1); surface.surface.collisionLayers.clear();
    snapshot.surfaces.push_back(surface);
    auto second=surface; second.id="B"; second.surface.boundsCenterMeters.x=2;
    snapshot.surfaces.push_back(second);
    VansNavigationBuildInput input; std::string error;
    const auto area=[](const VansNavigationGeometry& geometry)
    {
        double total=0;
        for(std::size_t triangle=0;triangle<geometry.TriangleCount();++triangle)
        {
            std::array<glm::vec3,3> p;
            for(int corner=0;corner<3;++corner)
            {
                const auto index=geometry.indices[triangle*3+corner]*3;
                p[corner]=glm::vec3(geometry.vertices[index],geometry.vertices[index+1],geometry.vertices[index+2]);
            }
            total+=glm::length(glm::cross(p[1]-p[0],p[2]-p[0]))*.5;
        }
        return total;
    };
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        std::abs(area(*input.profiles.front().geometry)-12)<1.e-5,"Overlapping Surface union duplicates or loses floor coverage")) return false;
    std::uint64_t firstHash=0,secondHash=0;
    if(!ComputeNavigationBuildInputHash(input.profiles.front(),firstHash,error)) return false;
    std::reverse(snapshot.surfaces.begin(),snapshot.surfaces.end());
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        ComputeNavigationBuildInputHash(input.profiles.front(),secondHash,error) && firstHash==secondHash,
        "Surface authoring order changes the canonical input fingerprint")) return false;
    const auto contributionHash=[&](const char* id)
    {
        for(const auto& contribution:input.profiles.front().contributions)
            if(contribution.id==id) return contribution.contentHash;
        return std::uint64_t{0};
    };
    const auto floorHash=contributionHash("source/A/Floor");
    mesh->indices={1,2,3,0,2,1};
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        contributionHash("source/A/Floor")==floorHash,
        "Per-source collision fingerprint depends on triangle order")) return false;
    for(std::size_t vertex=0;vertex<mesh->VertexCount();++vertex) mesh->positions[vertex*3+1]=.1f;
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        contributionHash("source/A/Floor")!=floorHash,
        "Per-source collision fingerprint omits its actual clipped geometry")) return false;
    for(std::size_t vertex=0;vertex<mesh->VertexCount();++vertex) mesh->positions[vertex*3+1]=0;
    mesh->indices={0,2,1,1,2,3};
    snapshot.surfaces={surface};
    snapshot.surfaces.front().worldTransform=glm::mat4_cast(glm::angleAxis(glm::radians(45.f),glm::vec3(0,1,0)));
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        std::abs(area(*input.profiles.front().geometry)-8)<1.e-4,"Rotated local bounds are clipped as an axis-aligned world box")) return false;
    {
        auto bounded=snapshot; bounded.surfaces.front().surface.boundsCenterMeters={2,0,1};
        bounded.surfaces.front().surface.boundsHalfExtentsMeters={4,5,3};
        bounded.surfaces.front().worldTransform=
            glm::mat4_cast(glm::angleAxis(glm::radians(30.f),glm::vec3(0,1,0)))*
            glm::scale(glm::mat4(1),glm::vec3(1.5f,1,.75f));
        auto localSettings=settings; localSettings.maps.front().minimumRegionSizeCells=2;
        VansNavigationBuildInput boundedInput; std::shared_ptr<const VansNavigationMeshAsset> boundedAsset;
        auto boundedMap=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext boundedQuery;
        if(!Expect(VansNavigationSourceCollector::Collect(bounded,localSettings,"World",boundedInput,error) &&
            VansNavigationBuilder::Build(boundedInput,localSettings,"Scenes/BoundedNavigation.json",nullptr,boundedAsset,error) &&
            boundedMap->Initialize(boundedAsset,localSettings,"Humanoid",error) &&
            boundedQuery.Initialize(boundedMap,localSettings.query,"Default",error),error)) return false;
        const auto world=[&](glm::vec3 point)
            { return glm::vec3(bounded.surfaces.front().worldTransform*glm::vec4(point,1)); };
        const auto start=world({0,0,1}),goal=world({4,0,1}),outside=world({6.5f,0,1});
        const auto route=boundedQuery.FindPath(start,goal);
        const auto ray=boundedQuery.NavRaycast(start,outside,{.1f,1,.1f});
        const auto hitLocal=glm::vec3(glm::inverse(bounded.surfaces.front().worldTransform)*glm::vec4(ray.position,1));
        const auto outsideProjection=boundedQuery.ProjectPoint(outside,{.05f,1,.05f});
        std::cout<<"[NavigationSurfaceBoundary] route="<<static_cast<int>(route.status)<<" diagnostic="<<route.diagnostic
            <<" outside="<<outsideProjection.valid<<" ray="<<ray.valid<<","<<ray.hit<<","<<ray.fraction
            <<" localHit="<<hitLocal.x<<","<<hitLocal.z<<" diagnostic="<<ray.diagnostic<<'\n';
        if(!Expect(route.status==VansNavigationPathStatus::Complete &&
            !outsideProjection.valid &&
            ray.valid && ray.hit && ray.fraction<1 && hitLocal.x<=6 &&
            std::all_of(route.points.begin(),route.points.end(),[&](const auto& point)
            { const auto local=glm::vec3(glm::inverse(bounded.surfaces.front().worldTransform)*glm::vec4(point,1));
              return local.x>=-2 && local.x<=6 && local.z>=-2 && local.z<=4; }),
            "Native navigation escaped its transformed authored Surface boundary")) return false;
        std::cout<<"[NavigationSurfaceBoundary] PASS: center/extent with rotation and scale, native path inside region, outside projection rejection and boundary ray hit\n";
    }
    snapshot.surfaces.front()=surface;
    snapshot.sources.front().hasExplicitSource=true;
    snapshot.sources.front().source.mode=VansNavigationSourceMode::Area; snapshot.sources.front().source.area="Slow";
    snapshot.surfaces.front().surface.collection=VansNavigationCollection::ExplicitSources;
    snapshot.surfaces.front().surface.sources={{snapshot.sources.front().ownerId,"Floor"}};
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        std::all_of(input.profiles.front().geometry->areas.begin(),input.profiles.front().geometry->areas.end(),[](auto id){return id==1;}),
        "Explicit Source selection does not preserve its Area override")) return false;
    const auto preserved=input.profiles.front().geometry;
    snapshot.surfaces.front().surface.sources.front().entityGuid="WrongOwner";
    if(!Expect(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) && input.profiles.front().geometry==preserved,
        "Explicit Source selection accepted a component paired with the wrong entity")) return false;
    snapshot.surfaces.front().surface.sources.front().entityGuid=snapshot.sources.front().ownerId;
    snapshot.sources.front().source.mode=VansNavigationSourceMode::Exclude;
    if(!Expect(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) && input.profiles.front().geometry==preserved,
        "Excluded-only Surface publishes a partial replacement")) return false;
    snapshot.sources.front()=source;
    snapshot.surfaces.front()=surface; snapshot.surfaces.front().surface.collection=VansNavigationCollection::Hierarchy;
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error),"Hierarchy collection misses an effective descendant")) return false;
    snapshot.sources.front().ancestors.clear();
    if(!Expect(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error),"Hierarchy collection leaks a sibling source")) return false;
    snapshot.sources.front()=source; snapshot.surfaces.front()=surface;
    snapshot.surfaces.front().surface.collisionLayers={"Missing"};
    if(!Expect(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error),"Unknown layer was silently ignored")) return false;
    snapshot.surfaces.front()=surface;
    auto duplicate=source; duplicate.id="SecondFloor";
    auto cycled=std::make_shared<VansTriangleMeshData>(*mesh); cycled->indices={2,1,0,2,3,1}; duplicate.geometry=cycled;
    snapshot.sources.push_back(duplicate);
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        std::abs(area(*input.profiles.front().geometry)-8)<1.e-5,"Cyclic triangle ordering defeats duplicate source coverage removal")) return false;
    VansNavigationModifierRecord modifier; modifier.id="Star"; modifier.modifier.shape=VansNavigationModifierShape::Convex;
    for(int i=0;i<5;++i) { const float angle=glm::radians(i*144.f); modifier.modifier.convexVerticesMeters.push_back({std::cos(angle),std::sin(angle)}); }
    if(!Expect(!ValidateNavigationModifierRecord(modifier,settings,"World",error),"A self-intersecting star is accepted as a convex Modifier")) return false;

    auto terrain=std::make_shared<VansTerrainAsset>(); terrain->width=9; terrain->height=5;
    terrain->settings.terrainSize=32; terrain->settings.maxHeight=2; terrain->settings.heightOffset=3;
    for(std::uint32_t z=0;z<terrain->height;++z) for(std::uint32_t x=0;x<terrain->width;++x)
        terrain->heights.push_back(static_cast<std::uint16_t>(x*1000+z*2000+(x+z)%3*1000));
    for(auto& pixels:terrain->splatPixels) pixels.resize(terrain->heights.size()*4);
    VansSceneContentBuildPlan scene;
    VansSceneObjectBuildConfig authoredSurface; authoredSurface.entityGuid="TerrainSurfaceOwner";
    authoredSurface.transform=VansSceneTransformConfig{};
    authoredSurface.navigationSurface=VansSceneNavigationSurfaceConfig{};
    authoredSurface.navigationSurface->runtime.boundsHalfExtentsMeters=glm::vec3(20,10,20);
    authoredSurface.componentGuids["navigation_surface"]="TerrainSurface";
    scene.objects.objects.push_back(authoredSurface);
    scene.terrain=VansSceneTerrainNodeConfig{}; scene.terrain->assetGuid="TerrainAsset"; scene.terrain->asset=terrain;
    scene.terrain->collision=VansSceneTerrainCollisionConfig{}; scene.terrain->collision->enabled=true; scene.terrain->collision->layer="Default";
    if(!Expect(VansSceneNavigationGeometry::CaptureSnapshot(scene,Vans::VansProjectManager::Get().GetAssetObjectRepository(),settings,{"Default"},{},snapshot,error) &&
        snapshot.sources.size()==1 && snapshot.sources.front().heightfield && !snapshot.sources.front().geometry &&
        VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error),"Terrain provider fails to publish frozen physical height samples")) return false;
    VansNavigationGeometry tile;
    if(!Expect(CollectNavigationHeightfieldTile(input.profiles.front().heightfields,glm::vec3(-16,0,-16),glm::vec3(16,8,16),4*1024*1024,tile,error),
        "Terrain Tile geometry collection failed")) return false;
    auto& physics=VansEngine::VansPhysicsSystem::GetInstance();
    if(!Expect(physics.Initialize(),"Terrain equivalence fixture cannot initialize Physics")) return false;
    VansRuntimePhaseGuard physicsCleanup([&](){physics.Shutdown();});
    VansEngine::VansTerrainPhysicsNode physical;
    VansEngine::TerrainPhysicsProperties properties; properties.enabled=true; properties.surface=terrain; properties.layerName="Default";
    if(!Expect(physical.Initialize(properties),"Terrain equivalence fixture cannot create its real heightfield")) return false;
    VansEngine::VansPhysicsHeightfieldSnapshot liveTerrain;
    if(!Expect(physical.CaptureCollisionSurface(4*1024*1024,liveTerrain,error) &&
        liveTerrain.heightfield->samples==snapshot.sources.front().heightfield->samples &&
        liveTerrain.heightfield->origin==snapshot.sources.front().heightfield->origin &&
        liveTerrain.heightfield->spacingX==snapshot.sources.front().heightfield->spacingX &&
        liveTerrain.heightfield->spacingZ==snapshot.sources.front().heightfield->spacingZ &&
        liveTerrain.heightfield->heightScale==snapshot.sources.front().heightfield->heightScale,
        "Runtime Terrain capture differs from the offline physical samples")) return false;
    const auto frozenTerrain=liveTerrain.heightfield;
    if(!Expect(!physical.CaptureCollisionSurface(1,liveTerrain,error) && liveTerrain.heightfield==frozenTerrain,
        "Cached Terrain capture bypasses a reduced allocation budget")) return false;
    if(!Expect(physical.CaptureCollisionSurface(4*1024*1024,liveTerrain,error) && liveTerrain.heightfield==frozenTerrain,
        "Unchanged Terrain collision capture copies its samples again")) return false;
    auto editedTerrain=std::make_shared<VansTerrainAsset>(*terrain); editedTerrain->heights[7]+=1000;
    if(!Expect(physical.UpdateSurface(editedTerrain) && physical.CaptureCollisionSurface(4*1024*1024,liveTerrain,error) &&
        liveTerrain.heightfield!=frozenTerrain && liveTerrain.heightfield->samples[7]!=frozenTerrain->samples[7] &&
        frozenTerrain->samples==snapshot.sources.front().heightfield->samples && physical.UpdateSurface(terrain),
        "Terrain change reused a stale capture or modified frozen samples")) return false;
    physx::PxShape* shape=nullptr; physical.GetActor()->getShapes(&shape,1);
    for(const auto point:std::array<glm::vec2,6>{{{-13,-13},{-10,-10},{-5,-1},{1,5},{5,9},{13,13}}})
    {
        physx::PxRaycastHit hit;
        const auto count=physx::PxGeometryQuery::raycast(physx::PxVec3(point.x,10,point.y),physx::PxVec3(0,-1,0),
            shape->getGeometry(),physical.GetActor()->getGlobalPose()*shape->getLocalPose(),20,physx::PxHitFlag::ePOSITION,1,&hit);
        float collectedHeight=(std::numeric_limits<float>::quiet_NaN)();
        for(std::size_t t=0;t<tile.TriangleCount();++t)
        {
            std::array<glm::vec3,3> p;
            for(int corner=0;corner<3;++corner)
            {
                const auto index=tile.indices[t*3+corner]*3; p[corner]=glm::vec3(tile.vertices[index],tile.vertices[index+1],tile.vertices[index+2]);
            }
            const auto e0=p[1]-p[0],e1=p[2]-p[0]; const float determinant=e0.x*e1.z-e1.x*e0.z;
            if(std::abs(determinant)<1.e-8f) continue;
            const auto delta=glm::vec2(point.x-p[0].x,point.y-p[0].z);
            const float u=(delta.x*e1.z-e1.x*delta.y)/determinant,v=(e0.x*delta.y-delta.x*e0.z)/determinant;
            if(u>=-1.e-6f && v>=-1.e-6f && u+v<=1+1.e-6f) { collectedHeight=p[0].y+u*e0.y+v*e1.y; break; }
        }
        if(!Expect(count==1 && std::isfinite(collectedHeight) && std::abs(collectedHeight-hit.position.y)<2.e-5f,
            "Navigation Terrain differs from real PhysX quantization, row/column mapping or cell diagonal")) return false;
    }
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!Expect(VansNavigationBuilder::Build(input,settings,"Scenes/TerrainContract.json",nullptr,asset,error) && asset->GetTileCount()>=4,
        "Terrain-only tiled build failed or collapsed its fixed-grid coverage")) return false;
    if(!CheckSelectedTileBuild(input,settings,asset)) return false;
    auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query;
    if(!Expect(map->Initialize(asset,settings,"Humanoid",error) && query.Initialize(map,settings.query,"Default",error) &&
        query.FindPath(glm::vec3(-12,3.3f,-12),glm::vec3(12,3.4f,12)).status==VansNavigationPathStatus::Complete,
        "Terrain path cannot cross Tile seams")) return false;
    std::cout<<"[NavigationSources] PASS: Surface union/rotation, generic layers, hierarchy, explicit Area/Exclude, canonical dedup, convex validation and real PhysX Terrain seams\n";
    return true;
}

bool CheckNavigationPrefabReferences()
{
    using namespace Vans;
    using Json=nlohmann::ordered_json;
    const auto rootId=VansAssetGuid::New().ToString(),childId=VansAssetGuid::New().ToString();
    const auto sourceId=VansAssetGuid::New().ToString(),surfaceId=VansAssetGuid::New().ToString();
    const auto json=[](const VansSerializedValue& value){return EncodeSerializedValueJson<Json>(value);};
    auto root=json(VansSceneEntityFactory::BuildEmptyEntity({},rootId));
    auto child=json(VansSceneEntityFactory::BuildEmptyEntity({},childId));
    child["parent"]=json(WriteEntityParentReference(rootId));
    auto surface=VansComponentTypeCatalog::CreateDefaultData("NavigationSurface");
    SetSerializedObjectField(surface,"collection",VansSerializedValue::String("ExplicitSources"));
    SetSerializedObjectField(surface,"sources",VansSerializedValue::Array({
        MakeSerializedSceneComponentObjectReference(childId,sourceId,"NavigationSource")}));
    root["components"].push_back({{"id",surfaceId},{"type","NavigationSurface"},{"version",1},{"enabled",true},{"data",json(surface)}});
    child["components"].push_back({{"id",sourceId},{"type","NavigationSource"},{"version",1},{"enabled",true},
        {"data",json(VansComponentTypeCatalog::CreateDefaultData("NavigationSource"))}});
    const auto physicsId=VansAssetGuid::New().ToString();
    auto obstacle=VansSceneNavigationComponentReader::CreateObstacleShapeData("Collider");
    SetSerializedObjectField(obstacle,"mode",VansSerializedValue::String("Carve"));
    SetSerializedObjectField(obstacle,"mapId",VansSerializedValue::String("World"));
    SetSerializedObjectField(obstacle,"profiles",VansSerializedValue::Array({VansSerializedValue::String("Humanoid")}));
    SetSerializedObjectField(obstacle,"collider",MakeSerializedSceneComponentObjectReference(childId,physicsId,"Physics"));
    root["components"].push_back({{"id",VansAssetGuid::New().ToString()},{"type","NavigationObstacle"},{"version",1},{"enabled",true},{"data",json(obstacle)}});
    root["components"].push_back({{"id",VansAssetGuid::New().ToString()},{"type","NavigationLink"},{"version",1},{"enabled",true},
        {"data",json(VansComponentTypeCatalog::CreateDefaultData("NavigationLink"))}});
    child["components"].push_back({{"id",physicsId},{"type","Physics"},{"version",1},{"enabled",true},
        {"data",json(VansComponentTypeCatalog::CreateDefaultData("Physics"))}});
    Json scene{{"schemaVersion",VansSceneSchemaVersion},{"sceneGuid",VansAssetGuid::New().ToString()},
        {"settings",json(VansSceneSchema::MakeDefaultSettings())},{"entities",Json::array({root,child})}};
    std::string error;
    VansRuntimeNavigationSurfaceComponent decoded;
    if(!VansSceneNavigationComponentReader::Read(surface,decoded,error)) return Expect(false,error);
    const auto check=[&](const VansSerializedValue& entities)
    {
        const auto data=json(entities);
        VansRuntimeNavigationSurfaceComponent value;
        if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(data[0]["components"][1]["data"]),value,error)) return false;
        VansRuntimeNavigationObstacleComponent bound;
        if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(data[0]["components"][2]["data"]),bound,error)) return false;
        VansRuntimeNavigationLinkComponent link;
        if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(data[0]["components"][3]["data"]),link,error) || !link.bidirectional || link.endMeters!=glm::vec3(2,0,0)) return false;
        const auto* collider=std::get_if<VansNavigationObstacleCollider>(&bound.shape);
        return value.sources.size()==1 && value.sources[0].entityGuid==data[1]["id"].get<std::string>() &&
            value.sources[0].componentGuid==data[1]["components"][1]["id"].get<std::string>() && collider &&
            collider->entityGuid==data[1]["id"].get<std::string>() && collider->componentGuid==data[1]["components"][2]["id"].get<std::string>();
    };
    VansPrefabExtraction extraction;
    const auto assetId=VansAssetGuid::New();
    if(!VansPrefabResolver::Extract(DecodeSerializedValueJson(scene),rootId,assetId,extraction,error)) return Expect(false,error);
    auto asset=std::make_shared<const VansPrefabAsset>(extraction.asset);
    const VansPrefabLookup lookup=[&](const std::string& id){return id==assetId.ToString()?asset:nullptr;};
    VansSerializedValue first,second;
    if(!VansPrefabResolver::Instantiate(*asset,VansPrefabResolver::MakeInstance(assetId),first,error) ||
        !VansPrefabResolver::Instantiate(*asset,VansPrefabResolver::MakeInstance(assetId),second,error)) return Expect(false,error);
    if(!Expect(check(first) && check(second) && json(first)[1]["components"][1]["id"]!=json(second)[1]["components"][1]["id"] &&
        json(first)[0]["components"][3]["id"]!=json(second)[0]["components"][3]["id"],
        "NavigationSource references or Link identities were not remapped per Prefab instance")) return false;
    VansSerializedValue captured,resolved;
    if(!VansPrefabResolver::CaptureScene(extraction.scene,lookup,captured,error) ||
        !VansPrefabResolver::ResolveScene(captured,lookup,resolved,error)) return Expect(false,error);
    if(!Expect(check(*FindObjectField(resolved,"entities")),"Prefab capture/resolve lost an explicit NavigationSource reference")) return false;
    VansSerializedValue duplicated; std::string duplicatedRoot;
    if(!VansPrefabResolver::DuplicateSubtree(extraction.scene,lookup,rootId,duplicated,duplicatedRoot,error)) return Expect(false,error);
    const auto& entities=FindObjectField(duplicated,"entities")->arrayItems;
    VansSerializedValue copy=VansSerializedValue::Array({entities.at(2),entities.at(3)});
    if(!Expect(check(copy),"Duplicated Prefab retained references to the original NavigationSource")) return false;
    auto malformed=surface;
    auto* references=FindObjectField(malformed,"sources");
    references->arrayItems[0]=VansSerializedValue::String(sourceId);
    const auto before=decoded.sources;
    if(!Expect(!VansSceneNavigationComponentReader::Read(malformed,decoded,error) && decoded.sources[0].componentGuid==before[0].componentGuid,
        "Source string schema was accepted or failed decoding changed output")) return false;
    std::cout<<"[NavigationPrefab] PASS: typed source/Collider references and Link data, independent component identities, capture/resolve and duplicated Prefab identity\n";
    return true;
}

bool CheckNavigationBakePublication(const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset,
    const Vans::VansNavigationSettings& settings)
{
    using namespace Vans;
    using Json=nlohmann::ordered_json;
    using Value=VansSerializedValue;
    std::string error;
    const auto temporary=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto directory=temporary/("ForestNavigationBakeContract-"+VansAssetGuid::New().ToString());
    // 本合约只创建并清理一个位于系统临时目录的独立随机 fixture。
    if(!Expect(directory.parent_path()==temporary && !std::filesystem::exists(directory),"Invalid Bake fixture ownership")) return false;
    VansRuntimePhaseGuard cleanup([&](){std::error_code ignored; std::filesystem::remove_all(directory,ignored);});
    const auto assets=directory/"Assets";
    std::filesystem::create_directories(assets);
    VansAssetDatabase database(assets);
    VansAssetObjectRepository repository;
    const auto path=assets/"Navigation/Map.vnavmesh";
    const auto originalEntity=VansAssetGuid::New().ToString(),childEntity=VansAssetGuid::New().ToString();
    const auto json=[](const Value& value){return EncodeSerializedValueJson<Json>(value);};
    auto first=json(VansSceneEntityFactory::BuildEmptyEntity({},originalEntity));
    auto second=json(VansSceneEntityFactory::BuildEmptyEntity({},childEntity));
    second["parent"]=json(WriteEntityParentReference(originalEntity));
    for(auto* entity:{&first,&second}) entity->operator[]("components").push_back({
        {"id",VansAssetGuid::New().ToString()},{"type","NavigationSurface"},{"version",1},{"enabled",true},
        {"data",json(VansComponentTypeCatalog::CreateDefaultData("NavigationSurface"))}});
    const auto source=DecodeSerializedValueJson(Json{{"schemaVersion",VansSceneSchemaVersion},
        {"sceneGuid",VansAssetGuid::New().ToString()},{"settings",json(VansSceneSchema::MakeDefaultSettings())},
        {"entities",Json::array({first,second})}});
    VansPrefabExtraction extraction; const auto prefabId=VansAssetGuid::New();
    if(!VansPrefabResolver::Extract(source,originalEntity,prefabId,extraction,error)) return Expect(false,error);
    auto prefab=std::make_shared<const VansPrefabAsset>(extraction.asset);
    const VansPrefabLookup lookup=[&](const std::string& id){return id==prefabId.ToString()?prefab:nullptr;};
    VansSerializedValue authoringSource;
    if(!VansPrefabResolver::CaptureScene(extraction.scene,lookup,authoringSource,error)) return Expect(false,error);
    auto document=VansSceneDocument::CreateInMemory(std::move(authoringSource),lookup,error);
    if(!document) return Expect(false,error);
    VansSceneEditService edits(*document);
    const auto originalAuthoring=document->AuthoringRootSnapshot();
    const auto originalState=document->CurrentStateId();
    const auto bindings=[&](VansAssetGuid guid)
    {
        const auto reference=Value::Object({{"guid",Value::String(guid.ToString())}});
        return std::vector<VansSceneAuthoringValue>{
            {{DocumentPropertySpace::Scene,"/entities/0/components/1/data/navigationMesh"},reference},
            {{DocumentPropertySpace::Scene,"/entities/1/components/1/data/navigationMesh"},reference}};
    };
    const auto matches=[&](const std::string& guid)
    {
        const auto root=document->SerializedRootSnapshot();
        for(const auto* pointer:{"/entities/0/components/1/data/navigationMesh/guid","/entities/1/components/1/data/navigationMesh/guid"})
        {
            const auto* value=FindSerializedPointer(root,pointer);
            if(!value || value->stringValue!=guid) return false;
        }
        return true;
    };
    auto publication=VansNavigationBakePublication::Prepare(database,repository,asset,path,error);
    if(!publication) return Expect(false,error);
    const auto failedGuid=publication->Record().guid;
    if(!Expect(database.Find(path).has_value() && std::filesystem::exists(path) && !repository.ResolveLatest<VansNavigationMeshAsset>(failedGuid),
        "Bake candidate published its repository object before the authoring transaction")) return false;
    int commits=0;
    const auto failed=edits.CommitValues(bindings(failedGuid),originalState,[&](std::string& reason)
    { ++commits; reason="Injected authoring commit rejection"; return false; });
    publication.reset();
    if(!Expect(!failed && commits==1 && document->CurrentStateId()==originalState &&
        SerializedValuesEqual(originalAuthoring,document->AuthoringRootSnapshot()) && !edits.CanUndo() &&
        !database.Find(path) && !std::filesystem::exists(path) && !std::filesystem::exists(VansAssetMeta::MetaPathFor(path)) &&
        !std::filesystem::exists(path.parent_path()),"Rejected Bake retained a payload, index, Prefab override, Undo entry or temporary directory")) return false;
    publication=VansNavigationBakePublication::Prepare(database,repository,asset,path,error);
    if(!publication) return Expect(false,error);
    const auto guid=publication->Record().guid;
    const auto committed=edits.CommitValues(bindings(guid),originalState,[&](std::string& reason)
    { ++commits; return publication->Commit(reason); });
    if(!Expect(committed.success && matches(guid.ToString()) && edits.CanUndo() && document->IsDirty() &&
        FindObjectField(document->AuthoringRootSnapshot(),"prefabInstances"),"Bake did not bind both Prefab Surfaces in one authoring transaction")) return false;
    publication.reset();
    VansAssetObjectHandle<VansNavigationMeshAsset> priorHandle;
    const auto published=repository.ResolveLatest<VansNavigationMeshAsset>(guid,&priorHandle);
    const auto priorRecord=database.Find(path);
    std::string priorBytes,currentBytes;
    if(!VansFileStorage::ReadAllBytes(path,priorBytes,error)) return Expect(false,error);
    const auto ioBeforeUndo=VansIOAudit::Snapshot().size();
    if(!Expect(edits.Undo().success && matches("") && SerializedValuesEqual(originalAuthoring,document->AuthoringRootSnapshot()) &&
        edits.Redo().success && matches(guid.ToString()) && commits==2 && VansIOAudit::Snapshot().size()==ioBeforeUndo,
        "Bake Undo/Redo lost Prefab identity, split Surface edits or repeated file publication")) return false;
    auto changed=settings; changed.areas.definitions.front().traversalCost=2.f;
    VansNavigationGeometry geometry; geometry.vertices={-10,0,-10,10,0,-10,-10,0,10,10,0,10};
    geometry.indices={0,2,1,1,2,3}; geometry.areas={0,0};
    std::shared_ptr<const VansNavigationMeshAsset> replacement;
    if(!BuildContractNavigation(geometry,changed,"World","Scenes/PreviewContract.json",replacement,error)) return Expect(false,error);
    publication=VansNavigationBakePublication::Prepare(database,repository,replacement,path,error);
    if(!publication) return Expect(false,error);
    if(!Expect(publication->Record().guid==guid,"Rebake replaced the formal asset GUID")) return false;
    publication.reset();
    if(!VansFileStorage::ReadAllBytes(path,currentBytes,error)) return Expect(false,error);
    if(!Expect(currentBytes==priorBytes && database.Find(path)->sourceHash==priorRecord->sourceHash && repository.Resolve(priorHandle)==published,
        "Abandoned rebake did not restore the prior payload, content index or repository handle")) return false;
    const auto beforeNoChange=document->CurrentStateId();
    publication=VansNavigationBakePublication::Prepare(database,repository,replacement,path,error);
    if(!publication) return Expect(false,error);
    if(!Expect(edits.CommitValues(bindings(guid),beforeNoChange,[&](std::string& reason){++commits;return publication->Commit(reason);}).success &&
        document->CurrentStateId()==beforeNoChange && commits==3 && !repository.Resolve(priorHandle) &&
        repository.ResolveLatest<VansNavigationMeshAsset>(guid)->GetBakedAreas().definitions.front().traversalCost==2.f,
        "Rebake with unchanged bindings created a Scene edit or retained the obsolete asset handle")) return false;
    publication.reset();
    for(const auto& item:std::filesystem::directory_iterator(path.parent_path()))
        if(!Expect(item.path()==path || item.path()==VansAssetMeta::MetaPathFor(path),"Bake retained a staging file or backup")) return false;
    if(!Expect(!VansNavigationBakePublication::Prepare(database,repository,asset,directory/"Outside.vnavmesh",error),
        "Bake accepted a destination outside the project Assets root")) return false;
    {
        VansScopedIOContext fixtureIO(VansIODomain::Authoring,"Navigation.CatalogContract",true);
        VansProjectConfig config; config.SetDefaults("NavigationCatalogContract");
        std::filesystem::create_directories(directory/"Scenes");
        if(!config.SaveToFile((directory/"ForestProject.json").string())) return Expect(false,"Could not save catalog fixture config");
        auto& project=VansProjectManager::Get();
        if(!Expect(!project.IsProjectLoaded(),"Catalog fixture cannot replace an existing project session")) return false;
        VansRuntimePhaseGuard close([&](){project.CloseProject();});
        VansProjectOpenRequest open; open.m_ProjectRootPath=directory.generic_string();
        open.m_Options.m_UpdateRecentProjects=false;
        open.m_Options.m_LoadProjectSettings=false;
        const auto valid=project.OpenProject(open);
        if(!Expect(valid.m_Opened,valid.m_Message.c_str())) return false;
        project.CloseProject();
        if(!VansFileStorage::WriteAtomicBytes(path,"invalid cooked navigation",error)) return Expect(false,error);
        open.m_Options.m_AssetLoadMode=VansProjectAssetLoadMode::Catalog;
        const auto opened=project.OpenProject(open);
        if(!Expect(opened.m_Opened,opened.m_Message.c_str())) return false;
        const auto* catalog=project.GetAssetDatabase();
        if(!Expect(catalog && catalog->Find(guid) && project.FindAssetRecord(guid) &&
            !project.GetAssetObjectRepository().ResolveLatest<VansNavigationMeshAsset>(guid),
            "Catalog-only project open decoded invalid cooked data or used a second asset index")) return false;
        const auto catalogRoot=project.GetProjectRootPath();
        open.m_Options.m_AssetLoadMode=VansProjectAssetLoadMode::Objects;
        const auto rejected=project.OpenProject(open);
        if(!Expect(!rejected.m_Opened && rejected.m_Failure==VansProjectOpenFailure::AssetObjects &&
            project.GetAssetDatabase()==catalog && project.GetProjectRootPath()==catalogRoot && project.FindAssetRecord(guid),
            "Normal object bootstrap accepted invalid cooked navigation or replaced the valid catalog session")) return false;
    }
    std::cout<<"[NavigationBake] PASS: multi-Surface Prefab transaction, rejected commit rollback, zero-I/O Undo/Redo, stable GUID rebake, staging cleanup and catalog-only repair bootstrap\n";
    return true;
}

bool CheckNavigationAuthoringPreview()
{
    using namespace Vans;
    VansNavigationSettings settings;
    settings.queryFilters.push_back({"Blocked",{"Walkable"},{}});
    VansNavigationSourceSnapshot sources; sources.collisionLayers={"Environment"};
    sources.exclusions.push_back({"pcg/region/layer/visual",VansNavigationGeometryKind::Pcg,17,"No collision prototype"});
    auto geometry=std::make_shared<VansTriangleMeshData>();
    geometry->positions={-10,0,-10,10,0,-10,-10,0,10,10,0,10}; geometry->indices={0,2,1,1,2,3};
    VansNavigationSourceRecord floor; floor.id="PreviewFloor"; floor.ownerId="PreviewFloorOwner";
    floor.geometry=geometry; floor.collisionLayer="Environment";
    floor.boundsMinimum={-10,0,-10}; floor.boundsMaximum={10,0,10};
    sources.sources.push_back(floor);
    auto upper=floor; upper.id="PreviewUpperFloor"; upper.ownerId="PreviewUpperOwner";
    upper.worldTransform[3].y=4; upper.boundsMinimum.y=upper.boundsMaximum.y=4;
    sources.sources.push_back(upper); sources.surfaces.push_back(ContractSurface(settings));
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansRuntimePhaseGuard shutdown([&](){jobs.Shutdown();});
    VansNavigationAuthoringSession session;
    std::promise<void> unblock;
    const auto gate=unblock.get_future().share();
    std::atomic<int> entered{0};
    const auto block=[&](){entered.fetch_add(1); gate.wait();};
    const auto blockers=jobs.QueueJobGroup({block,block});
    bool opened=false;
    const auto open=[&](){if(!opened) { opened=true; unblock.set_value(); }};
    VansRuntimePhaseGuard releaseGate([&](){open(); blockers->Wait();});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(entered.load()!=2 && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    if(!Expect(entered.load()==2,"Preview fixture could not occupy the actual workers")) return false;
    const auto ioBefore=VansIOAudit::Snapshot().size();
    std::string error;
    const auto before=std::chrono::steady_clock::now();
    auto cancelledSources=sources;
    auto cancelledGeometry=std::make_shared<VansTriangleMeshData>(*geometry);
    std::weak_ptr<const VansTriangleMeshData> cancelledGeometryLifetime=cancelledGeometry;
    cancelledSources.sources.front().geometry=std::move(cancelledGeometry);
    if(!Expect(session.BuildPreview(std::move(cancelledSources),settings,"World","Scenes/PreviewContract.json",17,error) &&
        std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100),
        "Navigation preview waited for blocked workers")) return false;
    auto result=session.Capture(); const auto cancelledId=result.requestId;
    if(!Expect(result.busy && result.state==VansNavigationPreviewState::Building && !result.asset,
        "Preview exposed an unfinished candidate")) return false;
    session.Cancel();
    if(!Expect(session.Capture().busy && !session.BuildPreview(sources,settings,"World","Scenes/PreviewContract.json",17,error) &&
        session.Capture().requestId==cancelledId,"Cancelled preview stopped counting its in-flight memory or queued another build")) return false;
    open(); deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!cancelledGeometryLifetime.expired() && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    if(!Expect(cancelledGeometryLifetime.expired(),
        "Completed cancelled preview retained frozen geometry until the next UI poll")) return false;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do
    {
        result=session.Capture(); if(!result.busy) break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    } while(std::chrono::steady_clock::now()<deadline);
    if(!Expect(!result.busy && result.state==VansNavigationPreviewState::Cancelled && !result.asset,
        "Cancelled worker published a preview candidate")) return false;
    if(!Expect(session.BuildPreview(sources,settings,"World","Scenes/PreviewContract.json",17,error),error)) return false;
    // 调用返回后修改调用者的记录，worker 必须继续使用自己的冻结值。
    sources.sources.front().worldTransform[3].x=100;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do
    {
        result=session.Capture(); if(!result.busy) break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    } while(std::chrono::steady_clock::now()<deadline);
    if(!Expect(result.state==VansNavigationPreviewState::Ready && result.asset && result.asset->GetTileCount()>1 &&
        result.requestId>cancelledId && result.asset->GetSource().scene=="Scenes/PreviewContract.json" &&
        result.exclusions.size()==1 && result.exclusions.front().instanceCount==17 &&
        result.exclusions.front().reason=="No collision prototype",result.diagnostic.c_str())) return false;
    auto map=std::make_shared<VansNavigationMap>();
    if(!map->Initialize(result.asset,settings,"Humanoid",error) ||
        !Expect([&](){VansNavigationQueryContext query; return query.Initialize(map,settings.query,"Default",error) && query.FindPath({-4,0,0},{4,0,0}).status==VansNavigationPathStatus::Complete;}(),
            "Frozen preview geometry no longer covered the original caller's floor")) return false;
    // 视口只取实际 native detail 三角形，楼层高度、接缝和过滤规则不得由工具重建。
    VansNavigationDebugView view;
    view.planes={glm::vec4(1,0,0,20),glm::vec4(-1,0,0,20),glm::vec4(0,1,0,1),
        glm::vec4(0,-1,0,8),glm::vec4(0,0,1,20),glm::vec4(0,0,-1,20)};
    VansNavigationDebugGeometry visible;
    if(!Expect(session.CaptureDebugGeometry(view,"Humanoid","Default",visible,error) &&
        !visible.limited && visible.tileCount==result.asset->GetTileCount() && !visible.triangles.empty(),error)) return false;
    bool hasLower=false,hasUpper=false,hasSeam=false;
    float nearest=10; bool picked=false;
    for(const auto& triangle:visible.triangles)
    {
        hasLower|=triangle.positions.front().y<1; hasUpper|=triangle.positions.front().y>3;
        hasSeam|=std::any_of(triangle.tileEdges.begin(),triangle.tileEdges.end(),[](bool edge){return edge;});
        float distance=0;
        if(VansSceneViewMath::IntersectTriangle({0,8,0},{0,-1,0},triangle.positions,nearest,distance))
        { nearest=distance; picked=true; }
    }
    if(!Expect(hasLower && hasUpper && hasSeam && picked && std::abs((8-nearest)-4)<.2f,
        "Preview lost native floor height, Tile seams or nearest visible upper-floor picking")) return false;
    auto lowerView=view; lowerView.planes[3].w=1;
    VansNavigationDebugGeometry lowerGeometry;
    if(!Expect(session.CaptureDebugGeometry(lowerView,"Humanoid","Default",lowerGeometry,error) &&
        !lowerGeometry.triangles.empty() && lowerGeometry.triangles.size()<visible.triangles.size() &&
        std::all_of(lowerGeometry.triangles.begin(),lowerGeometry.triangles.end(),[](const auto& triangle)
        { return std::all_of(triangle.positions.begin(),triangle.positions.end(),[](const auto& point){return point.y<1;}); }),
        "Viewport frustum retained detail geometry from an excluded floor")) return false;
    auto limitedView=view; limitedView.maximumTriangles=1;
    VansNavigationDebugGeometry limitedGeometry;
    if(!Expect(session.CaptureDebugGeometry(limitedView,"Humanoid","Default",limitedGeometry,error) &&
        limitedGeometry.limited && limitedGeometry.triangles.size()==1,"Preview triangle capacity was silently exceeded")) return false;
    limitedView=view; limitedView.maximumTiles=1;
    if(!Expect(session.CaptureDebugGeometry(limitedView,"Humanoid","Default",limitedGeometry,error) &&
        limitedGeometry.limited && limitedGeometry.tileCount==1,"Preview Tile capacity was silently exceeded")) return false;
    auto outsideView=view; outsideView.planes[0].w=-30; outsideView.planes[1].w=40;
    if(!Expect(session.CaptureDebugGeometry(outsideView,"Humanoid","Default",limitedGeometry,error) &&
        !limitedGeometry.limited && !limitedGeometry.tileCount && limitedGeometry.triangles.empty(),
        "Preview copied NavMesh geometry outside the requested viewport")) return false;
    VansNavigationDebugGeometry blockedGeometry;
    if(!Expect(session.CaptureDebugGeometry(view,"Humanoid","Blocked",blockedGeometry,error) &&
        blockedGeometry.triangles.size()==visible.triangles.size() &&
        std::none_of(blockedGeometry.triangles.begin(),blockedGeometry.triangles.end(),[](const auto& t){return t.traversable;}),
        "Preview traversability did not use the selected compiled Filter")) return false;
    const auto priorCount=visible.triangles.size();
    auto invalidView=view; invalidView.planes[0].x=0;
    if(!Expect(!session.CaptureDebugGeometry(invalidView,"Humanoid","Default",visible,error) &&
        !session.CaptureDebugGeometry(view,"Missing","Default",visible,error) &&
        !session.CaptureDebugGeometry(view,"Humanoid","Missing",visible,error) && visible.triangles.size()==priorCount,
        "Invalid preview inputs replaced the previous owned geometry snapshot")) return false;
    invalidView=view; invalidView.maximumTriangles=kMaximumNavigationDebugTriangles+1;
    if(!Expect(!session.CaptureDebugGeometry(invalidView,"Humanoid","Default",visible,error) &&
        visible.triangles.size()==priorCount,"Preview accepted an unbounded geometry request")) return false;
    float missedDistance=0;
    if(!Expect(!VansSceneViewMath::IntersectTriangle({0,8,0},{0,-1,0},visible.triangles.front().positions,1,missedDistance) &&
        !VansSceneViewMath::IntersectTriangle({0,8,0},{1,0,0},visible.triangles.front().positions,20,missedDistance),
        "Viewport picking crossed its ray limit or accepted a parallel floor ray")) return false;
    const auto peakAsset=result.asset;
    auto tightSettings=settings;
    tightSettings.maximumResidentBytes=static_cast<int>(EstimateNavigationQueryBytes(settings.query)+
        EstimateNavigationAvoidanceBytes(settings.query.maximumActiveAgents,settings.avoidance)+
        peakAsset->EstimateResidentBytes()+VansNavigationMap::EstimateInitializationBytes(*peakAsset,settings,"Humanoid"));
    VansNavigationMap peakMap;
    if(!Expect(peakMap.Initialize(peakAsset,tightSettings,"Humanoid",error) &&
        !peakMap.Initialize(peakAsset,tightSettings,"Humanoid",error) && peakMap.IsReady() &&
        peakMap.GetAsset()==peakAsset,"Native Map replacement exceeded its peak memory budget or damaged the live Map")) return false;
    const auto finishQuery=[&]()
    {
        std::optional<VansNavigationQuerySnapshot> query;
        const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        do
        {
            session.Update(); query=session.CaptureQuery(error);
            if(query && query->lifecycle!=VansNavigationQueryLifecycle::Queued && query->lifecycle!=VansNavigationQueryLifecycle::Running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(std::chrono::steady_clock::now()<timeout);
        return query;
    };
    VansNavigationQueryRequest previewQuery; previewQuery.start={-4,.3f,0}; previewQuery.goal={4,.3f,0};
    if(!Expect(session.SubmitQuery(previewQuery,error),error)) return false;
    const auto queuedQuery=session.CaptureQuery(error);
    for(int read=0;read<3;++read)
        if(!Expect(session.CaptureQuery(error)->lifecycle==VansNavigationQueryLifecycle::Queued,
            "Read-only preview capture advanced the query scheduler")) return false;
    if(!Expect(!session.SubmitQuery(previewQuery,error) && session.CaptureQuery(error)->handle.serial==queuedQuery->handle.serial,
        "Preview queued duplicate work while its current query was pending")) return false;
    auto query=finishQuery();
    if(!Expect(query && query->lifecycle==VansNavigationQueryLifecycle::Finished &&
        query->path.status==VansNavigationPathStatus::Complete && query->path.hasRequestCoordinates &&
        query->path.startProjectionDistance>.1f && !query->path.tiles.empty(),"Author preview lost path projection or Tile provenance")) return false;
    previewQuery.goal={4,4,0};
    if(!session.SubmitQuery(previewQuery,error)) return Expect(false,error);
    query=finishQuery();
    if(!Expect(query && query->path.status==VansNavigationPathStatus::Partial && query->path.reachedEnd.y<1 && query->path.projectedEnd.y>3,
        "Author preview joined separate floors or presented a Partial path as reaching the requested floor")) return false;
    previewQuery.goal={4,1.5f,0}; previewQuery.goalAcceptance=VansNavigationGoalAcceptance::RequestedPoint;
    previewQuery.goalProjectionToleranceMeters=.05f;
    if(!session.SubmitQuery(previewQuery,error)) return Expect(false,error);
    query=finishQuery();
    if(!Expect(query && query->path.status==VansNavigationPathStatus::Failed && query->path.failure==VansNavigationPathFailure::EndProjectionRejected &&
        query->path.hasProjection && query->path.projectedEnd.y<1 && query->path.endProjectionDistance>1,
        "Author preview omitted the requested-goal projection failure")) return false;
    previewQuery.goalAcceptance=VansNavigationGoalAcceptance::ProjectedPoint;
    for(const auto kind:{VansNavigationQueryKind::PointProjection,VansNavigationQueryKind::Raycast,
        VansNavigationQueryKind::NearestBoundary,VansNavigationQueryKind::ReachablePoint})
    {
        previewQuery.kind=kind; previewQuery.goal=kind==VansNavigationQueryKind::ReachablePoint?previewQuery.start:glm::vec3(4,.3f,0);
        previewQuery.radius=kind==VansNavigationQueryKind::NearestBoundary?20:4;
        if(!session.SubmitQuery(previewQuery,error)) return Expect(false,error);
        query=finishQuery();
        if(!Expect(query && query->lifecycle==VansNavigationQueryLifecycle::Finished && query->path.hasRequestCoordinates &&
            query->path.requestedStart==previewQuery.start &&
            ((kind==VansNavigationQueryKind::PointProjection && query->point && query->projectionDistance>.1f) ||
             (kind==VansNavigationQueryKind::Raycast && query->raycast && query->raycast->valid && !query->raycast->hit) ||
             (kind==VansNavigationQueryKind::NearestBoundary && query->boundary && query->boundary->valid && query->boundary->found) ||
             (kind==VansNavigationQueryKind::ReachablePoint && query->point && query->path.status==VansNavigationPathStatus::Complete)),
            "Author query did not consume the shared native scheduler or preserve its typed result")) return false;
    }
    // 第二 owner 的标识不得复用；关闭视口后仍由帧 safe-point 回收退役查询。
    VansNavigationAuthoringSession other;
    auto otherSources=sources; otherSources.sources.front().worldTransform=glm::mat4(1);
    if(!other.BuildPreview(otherSources,settings,"World","Scenes/PreviewContract.json",17,error)) return Expect(false,error);
    auto otherPreview=other.Capture(); deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(otherPreview.busy && std::chrono::steady_clock::now()<deadline)
    { std::this_thread::sleep_for(std::chrono::milliseconds(1)); otherPreview=other.Capture(); }
    if(!Expect(otherPreview.state==VansNavigationPreviewState::Ready && otherPreview.requestId>result.requestId,
        "Separate preview owners reused a build identity")) return false;
    std::weak_ptr<const VansNavigationMeshAsset> retiredAsset=otherPreview.asset; otherPreview.asset.reset();
    std::promise<void> queryUnblock; const auto queryGate=queryUnblock.get_future().share(); std::atomic<int> queryEntered{0};
    const auto queryBlock=[&](){queryEntered.fetch_add(1); queryGate.wait();};
    const auto queryBlockers=jobs.QueueJobGroup({queryBlock,queryBlock}); bool queryOpened=false;
    const auto openQueries=[&](){if(!queryOpened) { queryOpened=true; queryUnblock.set_value(); }};
    VansRuntimePhaseGuard releaseQueryGate([&](){openQueries(); queryBlockers->Wait();});
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(queryEntered.load()!=2 && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    if(!Expect(queryEntered.load()==2,"Query cancellation fixture could not occupy actual workers")) return false;
    previewQuery.kind=VansNavigationQueryKind::Path; previewQuery.goal={4,.3f,0};
    if(!other.SubmitQuery(previewQuery,error)) return Expect(false,error);
    other.Update();
    const auto cancelStarted=std::chrono::steady_clock::now(); other.CancelQuery();
    if(!Expect(other.CaptureQuery(error)->lifecycle==VansNavigationQueryLifecycle::Cancelled &&
        std::chrono::steady_clock::now()-cancelStarted<std::chrono::milliseconds(100) &&
        other.SubmitQuery(previewQuery,error),"Author query cancellation waited for its worker or prevented a bounded retry")) return false;
    other.Update();
    if(!Expect(other.CaptureQuery(error)->lifecycle==VansNavigationQueryLifecycle::Queued,
        "Author retry exceeded the single active-query limit while a worker was retiring")) return false;
    other.Invalidate("Source changed while querying");
    if(!Expect(!other.CaptureQuery(error) && !retiredAsset.expired() &&
        !other.BuildPreview(otherSources,settings,"World","Scenes/PreviewContract.json",18,error),
        "Stale preview exposed a result, freed in-flight native data, or overcommitted a new build")) return false;
    openQueries(); deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!retiredAsset.expired() && std::chrono::steady_clock::now()<deadline)
    { other.Update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    if(!Expect(retiredAsset.expired(),"Retired author query retained its native asset after frame-safe collection")) return false;
    auto changed=settings; changed.maps.front().cellSizeMeters=.25f;
    if(!Expect(session.MatchesSources(settings,17) && !session.MatchesSources(changed,17) && !session.MatchesSources(settings,18),
        "Preview accepted changed source observation or build settings")) return false;
    session.Invalidate("Source changed");
    const auto stale=session.Capture();
    if(!Expect(stale.state==VansNavigationPreviewState::Stale && !stale.asset && !stale.busy,
        "Invalidated preview retained a publishable candidate")) return false;
    VansNavigationBuildInput input;
    sources.sources.front().worldTransform=glm::mat4(1);
    if(!VansNavigationSourceCollector::Collect(sources,settings,"World",input,error)) return Expect(false,error);
    auto preserved=result.asset;
    VansNavigationBuildControl cancelled; cancelled.Cancel();
    if(!Expect(!VansNavigationBuilder::Build(input,settings,"Scenes/PreviewContract.json",&cancelled,preserved,error) &&
        preserved==result.asset,"A cancelled build replaced its prior valid output")) return false;
    if(!Expect(VansIOAudit::Snapshot().size()==ioBefore,"Preview, cancellation or invalidation performed file I/O")) return false;
    std::cout<<"[NavigationPreview] PASS: real workers, frozen input, viewport/floor/seam/filter limits, five queries, Partial/projection failure, native peak rollback, nonblocking cancel/retirement and zero I/O\n";
    return CheckNavigationBakePublication(result.asset,settings);
}

class NavigationContractAuthoringHost final : public Vans::IVansSceneAuthoringHost
{
public:
    NavigationContractAuthoringHost(Vans::VansSceneDocument& document,Vans::VansSceneEditService& edits)
        : m_Document(document),m_Edits(edits) {}
    Vans::VansSceneDocument* SceneDocument() const override { return &m_Document; }
    Vans::VansSceneAuthoringResult SetSceneValue(const Vans::DocumentPropertyPath& path,Vans::VansSerializedValue value) override
    { const auto result=m_Edits.Set(path,std::move(value)); return {result.success,result.message}; }
    Vans::VansSceneAuthoringResult CommitSceneValues(std::vector<Vans::VansSceneAuthoringValue> values,
        std::uint64_t state,const Vans::VansSceneAuthoringCommit& commit) override
    { const auto result=m_Edits.CommitValues(std::move(values),state,commit); return {result.success,result.message}; }
private:
    Vans::VansSceneDocument& m_Document;
    Vans::VansSceneEditService& m_Edits;
};

bool CheckNavigationPreviewAPIAdmission(Vans::EditorAPI::EngineAPIImpl& editor,const std::string& surfaceOwner,const std::string& surfaceComponent,
    const Vans::VansAssetGuid& meshGuid,std::string& error)
{
    using namespace Vans;
    using namespace Vans::EditorAPI;
    using Value=VansSerializedValue;
    auto& project=VansProjectManager::Get();
    auto authoredSurface=VansSceneEntityFactory::BuildEmptyEntity({},surfaceOwner);
    auto data=VansComponentTypeCatalog::CreateDefaultData("NavigationSurface");
    SetSerializedObjectField(data,"navigationMesh",Value::Object({{"guid",Value::String(meshGuid.ToString())}}));
    SetSerializedObjectField(data,"bounds",Value::Object({{"centerMeters",Value::Array({Value::Int(0),Value::Int(0),Value::Int(0)})},
        {"halfExtentsMeters",Value::Array({Value::Int(1000),Value::Int(1000),Value::Int(1000)})}}));
    SetSerializedObjectField(data,"collisionLayers",Value::Array({Value::String("Environment")}));
    FindObjectField(authoredSurface,"components")->arrayItems.push_back(Value::Object({
        {"id",Value::String(surfaceComponent)},{"type",Value::String("NavigationSurface")},
        {"version",Value::Int(1)},{"enabled",Value::Bool(true)},{"data",std::move(data)}}));
    const auto root=Value::Object({{"schemaVersion",Value::Int(VansSceneSchemaVersion)},
        {"sceneGuid",Value::String(VansAssetGuid::New().ToString())},{"settings",VansSceneSchema::MakeDefaultSettings()},
        {"entities",Value::Array({std::move(authoredSurface)})}});
    const auto directory=(std::filesystem::path(project.GetProjectRootPath())/"Scenes").lexically_normal();
    const auto path=directory/("NavigationPreviewContract-"+VansAssetGuid::New().ToString()+".json");
    if(!Expect(std::filesystem::is_directory(directory) && !std::filesystem::exists(path),"Invalid author preview fixture ownership")) return false;
    ScopedFileRemoval sourceCleanup(path);
    {
        VansScopedIOContext fixtureIO(VansIODomain::Authoring,"Navigation.PreviewContractBootstrap",true);
        if(!VansJsonFileStorage::WriteAtomic(path,EncodeSerializedValueJson<nlohmann::ordered_json>(root),error)) return Expect(false,error);
    }
    auto loaded=VansSceneDocumentLoader::Load(path);
    if(!loaded) for(const auto& diagnostic:loaded.diagnostics) std::cerr<<diagnostic.propertyPointer<<": "<<diagnostic.message<<'\n';
    if(!Expect(static_cast<bool>(loaded),"Author preview fixture is not a valid current Scene document")) return false;
    VansSceneEditService edits(*loaded.document);
    NavigationContractAuthoringHost host(*loaded.document,edits);
    editor.BindSceneAuthoring(&host);
    VansRuntimePhaseGuard detach([&](){editor.BindSceneAuthoring(nullptr); editor.CancelNavigationPreview();});
    const auto ioBefore=VansIOAudit::Snapshot().size();
    const auto build=editor.BuildNavigationPreview("World");
    NavigationPreviewView view; view.previewId=1; view.profileId="Humanoid"; view.filterId="Default";
    NavigationPreviewQueryRequest request; request.previewId=1; request.profileId="Humanoid"; request.filterId="Default";
    request.start={-4,.5f,0}; request.goal={4,.5f,0};
    if(!Expect(!build.valid && !editor.GetNavigationBuildPreview().available &&
        !editor.GetNavigationPreviewGeometry(view).available && !editor.SubmitNavigationPreviewQuery(request).valid &&
        !editor.GetNavigationPreviewQuery().available,
        "CPU domain assembly incorrectly admitted an author preview before the Scene was ready")) return false;
    editor.CancelNavigationPreviewQuery(); editor.CancelNavigationPreview();
    const DocumentPropertyPath componentPath{DocumentPropertySpace::Scene,"/entities/0/components/1/enabled"};
    if(!Expect(edits.Set(componentPath,Value::Bool(false)).success && edits.Undo().success &&
        !editor.GetNavigationBuildPreview().ready && VansIOAudit::Snapshot().size()==ioBefore,
        "Rejected author preview or ordinary document edits performed I/O or exposed a candidate")) return false;
    std::cout<<"[NavigationPreviewAdmission] PASS: valid Scene document, real CPU-only Scene/API ready gate, rejected query/geometry, safe cancellation and zero I/O\n";
    return true;
}

bool CheckNavigationSettingsCodec()
{
	using namespace Vans;
	VansNavigationSettings authored;
	authored.profiles.front().radiusMeters = 0.55f;
	authored.maps.front().detailSamplingEnabled = false;
    authored.maps.front().updateMode=VansNavigationMapUpdateMode::Runtime;
    const auto originalBakeHash=HashNavigationBakeSettings(ContractBake(authored));
    auto bakedMode=authored; bakedMode.maps.front().updateMode=VansNavigationMapUpdateMode::Baked;
    if(!Expect(HashNavigationBakeSettings(ContractBake(bakedMode))!=originalBakeHash &&
        !SameNavigationSettings(authored,bakedMode),"Map layer build policy was ignored by cooked freshness or runtime reconfiguration")) return false;
	authored.query.maximumSearchNodes = 4096;
	authored.areas.definitions.push_back({ "Restricted", 62u, 0.5f, false });
	authored.queryFilters.front().excludedAreas = { "Restricted" };
	authored.queryFilters.front().areaCosts = { { "Walkable", 0.125f } };
	static_assert(std::is_polymorphic_v<dtQueryFilter>);
	VansNavigationBakeQuantization quantized;
	std::string quantizationError;
	if (!Expect(QuantizeNavigationBakeSettings(ContractBake(authored), quantized, quantizationError) &&
		quantized.heightCells == 18 && quantized.radiusCells == 3 && quantized.climbCells == 2 &&
		quantized.minimumRegionCellArea == 4 && quantized.detailSampleDistanceMeters == 0.0f,
		"Bake unit quantization changed valid authored dimensions")) return false;
	const auto preservedQuantization = quantized;
	for (int invalidCase = 0; invalidCase < 5; ++invalidCase)
	{
		auto invalidBake = ContractBake(authored);
		if (invalidCase == 0) invalidBake.cellHeight = 1.e-30f;
		if (invalidCase == 1) invalidBake.cellSize = 1.e-30f;
		if (invalidCase == 2) invalidBake.regionMergeSize = 1.e20f;
		if (invalidCase == 3) invalidBake.edgeMaxLength = 1.e20f;
		if (invalidCase == 4) invalidBake.agentHeight = 0.1f;
		if (!Expect(!QuantizeNavigationBakeSettings(invalidBake, quantized, quantizationError) &&
			quantized.heightCells == preservedQuantization.heightCells && quantized.radiusCells == preservedQuantization.radiusCells,
			"Invalid native quantization overflow was accepted or replaced a valid report")) return false;
	}
	const VansSerializedValue encoded = VansNavigationSettingsJsonCodec::Encode(authored);
	VansNavigationSettings decoded;
	std::string error;
	// 完整地图的碰撞收集与全局 worker 预算采用相同的有界上限；配置不分配这些字节。
	for (const int bytes : { 768 * 1024 * 1024, 1024 * 1024 * 1024 })
	{
		auto large = authored;
		large.maps.front().maximumBuildBytes = bytes;
		large.maximumConcurrentBuildBytes = bytes;
		VansNavigationSettings accepted;
		if (!Expect(VansNavigationSettingsJsonCodec::Decode(VansNavigationSettingsJsonCodec::Encode(large), accepted, error) &&
			SameNavigationSettings(large, accepted), "Bounded full-map build budget was rejected or changed")) return false;
		auto overflow = large;
		overflow.maps.front().maximumBuildBytes = 1024 * 1024 * 1024 + 1;
		if (!Expect(!VansNavigationSettingsJsonCodec::Decode(VansNavigationSettingsJsonCodec::Encode(overflow), accepted, error) &&
			SameNavigationSettings(large, accepted), "Oversized map budget was accepted or replaced valid settings")) return false;
	}
	const auto matches = [&]()
	{
		return HashNavigationBakeSettings(ContractBake(decoded)) == HashNavigationBakeSettings(ContractBake(authored)) &&
            decoded.maps.front().updateMode==VansNavigationMapUpdateMode::Runtime &&
            SameNavigationSettings(authored,decoded) &&
			decoded.query.maximumSearchNodes == authored.query.maximumSearchNodes &&
			decoded.areas.definitions.size() == 2u &&
			decoded.areas.definitions.back().id == 62u &&
			decoded.areas.definitions.back().traversalCost == 0.5f &&
			!decoded.areas.definitions.back().traversable &&
			decoded.queryFilters.size() == 1u && decoded.queryFilters.front().id == "Default" &&
			decoded.queryFilters.front().excludedAreas == authored.queryFilters.front().excludedAreas &&
			decoded.queryFilters.front().areaCosts == authored.queryFilters.front().areaCosts;
	};
	if (!Expect(VansNavigationSettingsJsonCodec::Decode(encoded, decoded, error) && matches(),
		"Navigation settings round trip changed bake, query or area semantics")) return false;
	const auto field = [](VansSerializedValue& object, const char* name) -> VansSerializedValue&
	{
		for (auto& entry : object.objectFields)
			if (entry.first == name) return entry.second;
		throw std::logic_error("Invalid navigation settings contract fixture");
	};
	const auto reject = [&](VansSerializedValue invalid, const char* path)
	{
		return Expect(!VansNavigationSettingsJsonCodec::Decode(invalid, decoded, error) &&
			error.find(path) != std::string::npos && matches(),
			"Invalid navigation settings were accepted or replaced the last valid value");
	};
	VansSerializedValue invalid = encoded;
	invalid = encoded;
	invalid.objectFields.push_back(invalid.objectFields.front());
	if (!reject(invalid, "profiles")) return false;
	invalid = encoded;
	field(field(invalid, "query"), "maximumSearchNodes") = VansSerializedValue::Float(2048.5);
	if (!reject(invalid, "maximumSearchNodes")) return false;
	invalid = encoded;
	field(invalid, "profiles").arrayItems.push_back(field(invalid, "profiles").arrayItems.front());
	if (!reject(invalid, "unique")) return false;
	invalid = encoded;
	field(field(invalid, "maps").arrayItems.front(), "profiles").arrayItems.front() = VansSerializedValue::String("Missing");
	if (!reject(invalid, "unavailable")) return false;
	invalid = encoded;
	field(invalid, "maximumResidentBytes") = VansSerializedValue::Int(1024 * 1024);
	if (!reject(invalid, "capacity")) return false;
	invalid = encoded;
	field(field(invalid, "maps").arrayItems.front(), "cellSizeMeters") = VansSerializedValue::Bool(true);
	if (!reject(invalid, "cellSizeMeters")) return false;
	invalid = encoded;
	field(field(invalid, "maps").arrayItems.front(), "cellSizeMeters") = VansSerializedValue::Float(
		std::numeric_limits<double>::infinity());
	if (!reject(invalid, "cellSizeMeters")) return false;
	invalid = encoded;
	field(field(invalid, "areas"), "definitions").arrayItems.back().objectFields.pop_back();
	if (!reject(invalid, "traversable")) return false;
	invalid = encoded;
	field(field(field(invalid, "areas"), "definitions").arrayItems.back(), "id") =
		VansSerializedValue::Int(63);
	if (!reject(invalid, "id")) return false;
	invalid = encoded;
	field(field(field(invalid, "areas"), "definitions").arrayItems.back(), "id") =
		VansSerializedValue::Int(0);
	if (!reject(invalid, "unique")) return false;
	invalid = encoded;
	invalid.objectFields.erase(std::remove_if(invalid.objectFields.begin(), invalid.objectFields.end(),
		[](const auto& entry) { return entry.first == "queryFilters"; }), invalid.objectFields.end());
	if (!reject(invalid, "queryFilters")) return false;
	invalid = encoded;
	field(invalid, "queryFilters").arrayItems.push_back(field(invalid, "queryFilters").arrayItems.front());
	if (!reject(invalid, "unique")) return false;
	invalid = encoded;
	field(field(invalid, "queryFilters").arrayItems.front(), "excludedAreas").arrayItems.front() = VansSerializedValue::String("Missing");
	if (!reject(invalid, "Area")) return false;
	invalid = encoded;
	auto& invalidCosts = field(field(invalid, "queryFilters").arrayItems.front(), "areaCosts");
	invalidCosts.objectFields.push_back(invalidCosts.objectFields.front());
	if (!reject(invalid, "unique")) return false;
	VansNavigationCompiledFilter compiled;
	if (!Expect(CompileNavigationFilter(authored.areas, authored.queryFilters.front(), compiled, error) &&
		compiled.allowed[0] && !compiled.allowed[62] && compiled.costs[0] == 1.0f && compiled.minimumCost == 0.125f,
		"Filter normalization changed allowed areas or relative costs")) return false;
	const auto preservedFilter = compiled;
	auto invalidFilter = authored.queryFilters.front();
	invalidFilter.areaCosts.front().second = std::numeric_limits<float>::denorm_min();
	auto expensiveAreas = authored.areas;
	expensiveAreas.definitions.back().traversable = true;
	expensiveAreas.definitions.back().traversalCost = std::numeric_limits<float>::max();
	invalidFilter.excludedAreas.clear();
	if (!Expect(!CompileNavigationFilter(expensiveAreas, invalidFilter, compiled, error) &&
		compiled.costs == preservedFilter.costs && compiled.allowed == preservedFilter.allowed,
		"Unrepresentable filter cost was accepted or replaced the valid compiled filter")) return false;

	const auto path = std::filesystem::temp_directory_path() / "ForestNavigationSettingsContract.json";
	ScopedFileRemoval removal(path);
	if (!Expect(VansNavigationSettingsStorage::Save(path, authored, error), error)) return false;
	VansNavigationSettings invalidSettings = authored;
	invalidSettings.maps.front().cellSizeMeters = 0.0f;
	if (!Expect(!VansNavigationSettingsStorage::Save(path, invalidSettings, error) &&
		VansNavigationSettingsStorage::Load(path, decoded, error) && matches(),
		"Invalid navigation settings save damaged the prior document")) return false;
	std::cout << "[NavigationSettings] PASS: domain codec, strict fields/types, atomic failure and authoring I/O\n";
	return true;
}

bool CheckImmutableAssetCodec(const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset)
{
    using namespace Vans;
    static_assert(!std::is_default_constructible_v<VansNavigationMeshAsset>);
    static_assert(!std::is_copy_assignable_v<VansNavigationMeshAsset>);
    std::string bytes, error;
    std::shared_ptr<const VansNavigationMeshAsset> decoded;
    if (!Expect(VansNavigationMeshCodec::Encode(*asset, bytes, error) &&
        VansNavigationMeshCodec::Decode(bytes, decoded, error) && decoded->IsReady() &&
        decoded->GetPayloadBytes() == asset->GetPayloadBytes() &&
        decoded->GetSource().inputHash == asset->GetSource().inputHash,
        "Immutable navigation asset codec failed the owned memory round trip")) return false;
    const auto preserved = decoded;
    const auto rejects = [&](const std::string& invalid)
    {
        return Expect(!VansNavigationMeshCodec::Decode(invalid, decoded, error) && decoded == preserved,
            "Malformed navigation bytes were accepted or replaced the prior immutable asset");
    };
    const auto payload = bytes.size() - asset->GetPayloadBytes();
	if (asset->GetTileCount() > 1u)
	{
		const auto index = payload - asset->GetTileCount() * 20u;
		auto duplicate = bytes;
		duplicate.replace(index + 20u, 12u, bytes.substr(index, 12u));
		if (!rejects(duplicate)) return false;
		auto excessive = bytes;
		for (std::size_t byte = 0; byte < 8u; ++byte) excessive[index + 12u + byte] = '\xff';
		if (!rejects(excessive)) return false;
	}
    const auto malformedHeader = [&](std::size_t offset, std::uint32_t value)
    {
        auto invalid = bytes;
        for (unsigned byte = 0; byte < 4; ++byte)
            invalid[payload + offset + byte] = static_cast<char>(value >> (byte * 8u));
        return rejects(invalid);
    };
    if (!rejects(bytes.substr(0, bytes.size() - 1u)) || !rejects(bytes + "x") || !rejects(bytes.substr(0, 12u)) ||
        !malformedHeader(offsetof(dtMeshHeader, polyCount), 0x7fffffffu) ||
        !malformedHeader(offsetof(dtMeshHeader, maxLinkCount), 0x7fffffffu) ||
        !malformedHeader(offsetof(dtMeshHeader, vertCount), 2u) ||
        !malformedHeader(offsetof(dtMeshHeader, detailMeshCount), 0u) ||
        !malformedHeader(offsetof(dtMeshHeader, bvNodeCount), 0x7fffffffu) ||
        !malformedHeader(offsetof(dtMeshHeader, offMeshBase), 0x7fffffffu) ||
        !malformedHeader(offsetof(dtMeshHeader, walkableHeight), 0x7f800000u)) return false;
    std::cout << "[NavigationAsset] PASS: immutable owned codec, byte/count/ABI/dimension validation and rollback\n";
    return true;
}

bool BuildNavigationMap(const Vans::VansNavigationGeometry& geometry,
    const Vans::VansNavigationSettings& settings, const std::shared_ptr<Vans::VansNavigationMap>& map,
    std::string& error)
{
    std::shared_ptr<const Vans::VansNavigationMeshAsset> asset;
    return BuildContractNavigation(geometry, settings, "World", "Scenes/NavigationContract.json", asset, error) &&
        map->Initialize(std::move(asset), settings, "Humanoid", error);
}

bool LoadNavigationMap(const std::filesystem::path& path, const Vans::VansNavigationSettings& settings,
    const std::shared_ptr<Vans::VansNavigationMap>& map, std::string& error)
{
    std::shared_ptr<const Vans::VansNavigationMeshAsset> asset;
    return Vans::VansNavigationMeshStorage::Load(path, asset, error) && map->Initialize(std::move(asset), settings, "Humanoid", error);
}

Vans::VansNavigationPath QueryNavigationPath(
	const std::shared_ptr<const Vans::VansNavigationMap>& mesh,
	const Vans::VansNavigationQuerySettings& settings,
	const glm::vec3& start, const glm::vec3& end, const std::string& filterId = "Default")
{
	Vans::VansNavigationQueryContext query;
	std::string error;
	if (!query.Initialize(mesh, settings, filterId, error))
	{
		Vans::VansNavigationPath failed;
		failed.status = Vans::VansNavigationPathStatus::Failed;
		failed.failure = Vans::VansNavigationPathFailure::QueryNotReady;
		failed.diagnostic = std::move(error);
		return failed;
	}
	return query.FindPath(start, end);
}

bool CheckTiledNavigation()
{
	using namespace Vans;
	VansSceneObjectBuildPlan plan;
	AppendEnvironmentBox(plan, "TileGround", { 0.0f, -0.1f, 0.0f }, { 22.0f, 0.1f, 22.0f });
	VansNavigationSettings settings;
	settings.maps.front().originMeters = { 3.0f, 0.0f, -7.0f };
	VansNavigationGeometry geometry;
	std::string error;
	if (!Expect(CollectContractGeometry(plan, settings.areas, {}, geometry, error), error))
		return false;
	for (const int cells : { 32, 64, 128 })
	{
		settings.maps.front().tileSizeCells = cells;
		auto map = std::make_shared<VansNavigationMap>();
		if (!Expect(BuildNavigationMap(geometry, settings, map, error), error)) return false;
		const auto path = QueryNavigationPath(map, settings.query, { -18.0f, 0.0f, -18.0f }, { 18.0f, 0.0f, 18.0f });
		if (!Expect(map->GetAsset()->GetTileCount() > 1u && path.status == VansNavigationPathStatus::Complete &&
			!path.points.empty() && glm::distance(glm::vec2(path.points.back().x, path.points.back().z), glm::vec2(18.0f)) < 0.05f,
			"Tile borders failed to connect across negative coordinates and a nonzero grid origin")) return false;
		if(!Expect(path.tiles.size()>1 && path.cornerFlags.size()==path.points.size() &&
			(path.cornerFlags.front()&static_cast<std::uint8_t>(VansNavigationCornerFlag::Start)) &&
			(path.cornerFlags.back()&static_cast<std::uint8_t>(VansNavigationCornerFlag::End)),
			"Cross-Tile path omitted its stable Tile dependencies or endpoint flags")) return false;
		const auto prior = map->GetAsset();
		for (int capacity = 0; capacity < 3; ++capacity)
		{
			auto invalid = settings;
			if (capacity == 0) invalid.maps.front().maximumTiles = 1;
			if (capacity == 1) invalid.maps.front().maximumTiles = 65536;
			if (capacity == 2) invalid.maps.front().maximumBuildBytes = 1024 * 1024;
			if (!Expect(!BuildNavigationMap(geometry, invalid, map, error) && map->GetAsset() == prior,
				"Tile capacity/build-budget failure replaced the prior valid map")) return false;
		}
	}
	AppendEnvironmentBox(plan, "TileUpperFloor", { 0.0f, 3.9f, 0.0f }, { 22.0f, 0.1f, 22.0f });
	if (!Expect(CollectContractGeometry(plan, settings.areas, {}, geometry, error), error)) return false;
	auto layered = std::make_shared<VansNavigationMap>();
	if (!Expect(BuildNavigationMap(geometry, settings, layered, error), error)) return false;
	const auto upper = QueryNavigationPath(layered, settings.query, { -18.0f, 4.0f, -18.0f }, { 18.0f, 4.0f, 18.0f });
	const auto crossFloor = QueryNavigationPath(layered, settings.query, { -18.0f, 0.0f, -18.0f }, { 18.0f, 4.0f, 18.0f });
	if (!Expect(upper.status == VansNavigationPathStatus::Complete && crossFloor.status == VansNavigationPathStatus::Partial &&
		!crossFloor.points.empty() && crossFloor.points.back().y < 0.5f,
		"Tile portal stitching connected separate vertical floors or returned the unreachable upper endpoint")) return false;
	if(!Expect(crossFloor.requestedEnd.y==4 && crossFloor.projectedEnd.y>3 && crossFloor.reachedEnd.y<.5f &&
		glm::length(crossFloor.reachedEnd-crossFloor.points.back())<1.e-5,
		"Partial path metadata connected the reachable endpoint to the requested upper floor")) return false;
	std::cout << "[NavigationTiles] PASS: 32/64/128 cells, negative/nonzero grid, seams, floor isolation and budget rollback\n";
	return true;
}

bool CheckCorridorPositionRepair(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& settings)
{
    using namespace Vans;
    const std::array<dtPolyRef,4> original{1,2,3,4};
    auto start=original, end=original;
    const std::array<dtPolyRef,3> startVisited{1,5,6},endVisited{4,5,6};
    if(!Expect(dtMergeCorridorStartMoved(start.data(),4,4,startVisited.data(),3)<0 && start==original &&
        dtMergeCorridorEndMoved(end.data(),4,4,endVisited.data(),3)<0 && end==original,
        "Capacity-exhausted native repair discarded the route tail or partially adopted a new target")) return false;
    std::array<dtPolyRef,6> exactStart{1,2,3,4,0,0},exactEnd=exactStart;
    if(!Expect(dtMergeCorridorStartMoved(exactStart.data(),4,6,startVisited.data(),3)==6 &&
        exactStart==std::array<dtPolyRef,6>{6,5,1,2,3,4} &&
        dtMergeCorridorEndMoved(exactEnd.data(),4,6,endVisited.data(),3)==6 &&
        exactEnd==std::array<dtPolyRef,6>{1,2,3,4,5,6},
        "Exactly-full native repair rejected or changed the complete route")) return false;
    std::string error;
    VansNavigationQueryContext query;
    if(!query.Initialize(map,settings.query,"Default",error)) return Expect(false,error);
    if(!Expect(FollowContractCorridor(query,{-4,.1f,0},1,1).failure==VansNavigationPathFailure::QueryNotReady,
        "Missing Corridor reported a successful failure code")) return false;
    const auto path=query.FindPath({-4,0,0},{4,0,0});
    const auto route=query.CaptureCorridorRoute();
    if(!Expect(path.status==VansNavigationPathStatus::Complete && route,
        "Position repair fixture has no accepted wall route")) return false;
    // moveAlongSurface 使用独立的 64-node local pool；用真实密集 Tile 图耗尽它。
    VansSceneObjectBuildPlan densePlan;
    AppendEnvironmentBox(densePlan,"PositionRepairGround",{0,-.1f,0},{48,.1f,48});
    auto denseSettings=settings; denseSettings.maps.front().tileSizeCells=16;
    VansNavigationGeometry denseGeometry;
    if(!CollectContractGeometry(densePlan,denseSettings.areas,{},denseGeometry,error)) return Expect(false,error);
    auto denseMap=std::make_shared<VansNavigationMap>();
    if(!BuildNavigationMap(denseGeometry,denseSettings,denseMap,error)) return Expect(false,error);
    VansNavigationQueryContext limited;
    if(!limited.Initialize(denseMap,denseSettings.query,"Default",error)) return Expect(false,error);
    auto densePath=limited.FindPath({-44,0,-44},{44,0,44});
    if(!Expect(densePath.status==VansNavigationPathStatus::Complete,
        "Dense position repair fixture has no complete cross-Tile path")) return false;
    const auto acceptedDenseRoute=limited.CaptureCorridorRoute();
    VansNavigationQueryContext localVerifier;
    if(!Expect(localVerifier.Initialize(denseMap,denseSettings.query,"Default",error) &&
        localVerifier.AdoptCorridorRoute(acceptedDenseRoute)==VansNavigationPathFailure::None,error)) return false;
    const auto acceptedDenseSteering=localVerifier.CaptureCorridorSteering({-44,.1f,-44});
    int localUsed=0,totalLocalUsed=0;
    auto exhausted=limited.FollowCorridor({44,.1f,44},1,1,1,localUsed);
    if(!Expect(exhausted.pending && !exhausted.valid && localUsed==1 &&
        localVerifier.AdoptCorridorRoute(limited.CaptureCorridorRoute())==VansNavigationPathFailure::None &&
        localVerifier.CaptureCorridorSteering({-44,.1f,-44}).direction==acceptedDenseSteering.direction &&
        localVerifier.CaptureCorridorSteering({-44,.1f,-44}).endpoint==acceptedDenseSteering.endpoint,
        "One-node local quantum changed the live Corridor or exhausted capacity before searching it")) return false;
    totalLocalUsed+=localUsed;
    for(int quantum=0;quantum<64 && exhausted.pending;++quantum)
    {
        exhausted=limited.FollowCorridor({44,.1f,44},1,1,1,localUsed);
        if(!Expect(localUsed>=0 && localUsed<=1,"Local movement exceeded its one-node quantum")) return false;
        totalLocalUsed+=localUsed;
    }
    std::cout<<"[NavigationPositionRepair] local result="<<NavigationPathFailureName(exhausted.failure)<<'\n';
    if(!Expect(!exhausted.pending && !exhausted.valid && totalLocalUsed>1 && totalLocalUsed<=64 &&
        exhausted.failure==VansNavigationPathFailure::SearchNodeCapacityExceeded &&
        FollowContractCorridor(limited,{-44,.1f,-44},1,1).valid,
        "Motor position repair ignored local node exhaustion or modified its accepted Corridor")) return false;
    const auto previousTargetPath=densePath;
    bool targetPending=false;
    auto targetFailure=limited.TryRepairTarget({-44,.1f,-44},{1,1,1},VansNavigationGoalAcceptance::ProjectedPoint,
        .05f,1,localUsed,targetPending,densePath);
    if(!Expect(targetPending && targetFailure==VansNavigationPathFailure::None && localUsed==1 &&
        densePath.requestedEnd==previousTargetPath.requestedEnd && densePath.points==previousTargetPath.points,
        "Pending target quantum published candidate metadata or failed before its full search")) return false;
    for(int quantum=0;quantum<64 && targetPending;++quantum)
    {
        targetFailure=limited.TryRepairTarget({-44,.1f,-44},{1,1,1},VansNavigationGoalAcceptance::ProjectedPoint,
            .05f,1,localUsed,targetPending,densePath);
        if(!Expect(localUsed<=1,"Target repair exceeded a one-node quantum")) return false;
    }
    if(!Expect(!targetPending && targetFailure==VansNavigationPathFailure::SearchNodeCapacityExceeded &&
        densePath.points==previousTargetPath.points && FollowContractCorridor(limited,{-44,.1f,-44},1,1).valid,
        "Sliced target capacity failure partially committed the live route or path metadata")) return false;
    const auto pausedLocal=limited.FollowCorridor({44,.1f,44},1,1,1,localUsed);
    const auto frozenAsset=denseMap->GetAsset();
    if(!Expect(pausedLocal.pending && denseMap->Initialize(frozenAsset,denseSettings,"Humanoid",error) &&
        limited.FollowCorridor({44,.1f,44},1,1,1,localUsed).failure==VansNavigationPathFailure::MeshChanged && localUsed==0,
        "Suspended local work held a read lease or dereferenced stale native data after publication")) return false;
    const auto before=FollowContractCorridor(query,{-4,.1f,0},1,1);
    const auto wrongFloor=FollowContractCorridor(query,{4,8,0},1,1);
    const auto after=FollowContractCorridor(query,{-4,.1f,0},1,1);
    if(!Expect(before.valid && !wrongFloor.valid && wrongFloor.failure==VansNavigationPathFailure::StartOutsideMesh &&
        after.valid && glm::distance(before.endpoint,after.endpoint)<1.e-5f &&
        glm::distance(before.direction,after.direction)<1.e-5f && std::abs(before.remainingDistance-after.remainingDistance)<1.e-5f,
        "Rejected motor floor projection changed the accepted Corridor or its steering")) return false;
    VansNavigationQueryContext boundary;
    if(!Expect(boundary.Initialize(map,settings.query,"Default",error),error)) return false;
    // 实际 CCT 墙角轨迹：局部边段投影会因 float 舍入落在严格 polygon 内测之外。
    for(const glm::vec3 actual:{glm::vec3(-.702777565f,.0214999914f,2.45754218f),
        glm::vec3(-.563254774f,.0214999914f,2.44599509f),glm::vec3(-.417273521f,.0214999914f,2.43530035f)})
    {
        const auto edgePath=boundary.FindPath(actual,{4,0,0});
        const auto edgeFollow=FollowContractCorridor(boundary,actual,1,1);
        if(!Expect(edgePath.status==VansNavigationPathStatus::Complete && edgeFollow.valid &&
            glm::distance(edgeFollow.endpoint,edgePath.reachedEnd)<1.e-5f,
            "Native boundary rounding rejected a valid detail projection or changed the route endpoint")) return false;
    }
    std::cout<<"[NavigationPositionRepair] PASS: explicit node/corridor failure, complete tail, exact capacity, rejected-height rollback and native corner-boundary projection\n";
    return true;
}

bool CheckCorridorTargetRepair(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& settings)
{
 using namespace Vans; std::string error; VansNavigationQueryContext query;
 if(!Expect(query.Initialize(map,settings.query,"Default",error),error)) return false;
 auto path=query.FindPath({-4,0,0},{4,0,0});
 if(!Expect(path.status==VansNavigationPathStatus::Complete && query.CaptureCorridorRoute(),"Local target repair has no accepted owned route")) return false;
 const glm::vec3 repaired(4,0,.2f);
 if(!Expect(RepairContractTarget(query,repaired,{1,2,1},VansNavigationGoalAcceptance::ProjectedPoint,.05f,path)==VansNavigationPathFailure::None &&
  glm::distance(path.requestedEnd,repaired)<1.e-5f && std::abs(path.reachedEnd.z-.2f)<1.e-4f &&
  path.cornerFlags.size()==path.points.size() && !path.tiles.empty(),"Local target repair lost its accepted endpoint or owned path data")) return false;
 const auto previous=path;
 if(!Expect(RepairContractTarget(query,{4,1,.2f},{1,2,1},VansNavigationGoalAcceptance::RequestedPoint,.01f,path)==VansNavigationPathFailure::EndProjectionRejected &&
  glm::distance(path.requestedEnd,previous.requestedEnd)==0 && glm::distance(path.points.back(),previous.points.back())==0,
  "Rejected target repair partially published candidate path metadata")) return false;
 const auto steering=FollowContractCorridor(query,{-4,.1f,0},1,1);
 if(!Expect(steering.valid && glm::distance(steering.endpoint,previous.reachedEnd)<1.e-4f,
  "Rejected target repair mutated the live Corridor")) return false;
 std::cout<<"[NavigationTargetRepair] PASS: bounded local target, accepted metadata and rejected-candidate rollback\n";
 return true;
}

bool CheckCorridorOptimization(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& settings)
{
 using namespace Vans; std::string error; VansNavigationQueryContext query;
 if(!Expect(query.Initialize(map,settings.query,"Default",error),error)) return false;
 auto path=query.FindPath({-4,0,0},{4,0,0});
 const auto originalRoute=query.CaptureCorridorRoute();
 if(!Expect(path.status==VansNavigationPathStatus::Complete && originalRoute &&
  query.CorridorOptimizationIterationBudget()>0 && query.CorridorOptimizationIterationBudget()<=64,
  "Corridor optimization has no bounded owned route")) return false;
 auto limitedSettings=settings.query; limitedSettings.maximumSearchNodes=4;
 VansNavigationQueryContext limited;
 if(!Expect(limited.Initialize(map,limitedSettings,"Default",error) &&
  limited.AdoptCorridorRoute(originalRoute)==VansNavigationPathFailure::None,error)) return false;
 auto rejected=path; bool changed=false;
 const auto failed=limited.TryOptimizeCorridor(12,rejected,changed);
 if(!Expect(failed==VansNavigationPathFailure::SearchNodeCapacityExceeded && !changed &&
  rejected.points==path.points && FollowContractCorridor(limited,{-4,.1f,0},1,1).valid,
  "Capacity-exhausted optimization published a partial candidate or lost the valid corridor")) return false;
 // Move to the opposite side of the original wall detour without changing the request.
 const float detourSign=path.points.size()>2 && path.points[1].z<0 ? -1.f : 1.f;
 const glm::vec3 actual(-4,.1f,-3*detourSign);
 const auto before=FollowContractCorridor(query,actual,1,1);
 if(!Expect(before.valid,"Actual motor position could not update the test corridor")) return false;
 if(!Expect(query.TryOptimizeCorridor(12,path,changed)==VansNavigationPathFailure::None && changed &&
  path.status==VansNavigationPathStatus::Complete && glm::distance(path.requestedEnd,{4,0,0})<1.e-5f &&
  glm::distance(path.reachedEnd,path.projectedEnd)<1.e-4f && path.cornerFlags.size()==path.points.size() && !path.tiles.empty(),
  "A local topology optimization failed to repair the wall detour or lost request metadata")) return false;
 const auto after=FollowContractCorridor(query,actual,1,1);
 std::cout<<"[NavigationOptimization] steering z "<<before.direction.z<<" -> "<<after.direction.z
  <<", remaining "<<before.remainingDistance<<" -> "<<after.remainingDistance<<'\n';
 if(!Expect(after.valid && path.points.size()>2 && path.points[1].z*detourSign<0 &&
  after.direction.z*detourSign<before.direction.z*detourSign,
  "Optimized corridor still steers toward the obsolete opposite wall detour")) return false;
 const auto prior=path;
 if(!Expect(query.TryOptimizeCorridor(0,path,changed)==VansNavigationPathFailure::InvalidInput && !changed &&
  path.points==prior.points,"Invalid optimization range modified the accepted route")) return false;
 std::cout<<"[NavigationOptimization] PASS: bounded topology, actual-position detour repair, capacity and input rollback\n";
 return true;
}

bool CheckSpatialNavigationQueries(const std::shared_ptr<Vans::VansNavigationMap>& obstacleMap,const Vans::VansNavigationSettings& sourceSettings)
{
 using namespace Vans;
 std::string error; VansNavigationQueryContext query;
 if(!Expect(query.Initialize(obstacleMap,sourceSettings.query,"Default",error),error)) return false;
 const glm::vec3 extents(1,2,1),from(-4,0,0),to(4,0,0);
 const auto blocked=query.NavRaycast(from,to,extents);
 std::cout<<"[NavigationSpatial] blocked valid="<<blocked.valid<<" hit="<<blocked.hit<<" fraction="<<blocked.fraction
  <<" point="<<blocked.position.x<<','<<blocked.position.y<<','<<blocked.position.z
  <<" normal="<<glm::length(blocked.normal)<<" tiles="<<blocked.tiles.size()<<" failure="<<blocked.diagnostic<<'\n';
 if(!Expect(blocked.valid && blocked.hit && blocked.fraction>0 && blocked.fraction<1 &&
  blocked.position.x<0 && glm::length(blocked.normal)>.99f && !blocked.tiles.empty(),"NavRaycast crossed the physical wall or lost its owned hit")) return false;
 const auto clear=query.NavRaycast({-4,0,-3},{4,0,-3},extents);
 if(!Expect(clear.valid && !clear.hit && clear.fraction==1 && std::abs(clear.position.x-4)<1.e-4f,
  "Clear NavRaycast was converted into a path search or false boundary hit")) return false;
 auto sliced=query.BeginRaycast(from,to,extents); int rayIterations=0;
 for(int count=0;count<512 && !sliced.finished;++count)
 {
  int used=0; sliced=query.AdvanceRaycast(1,used); rayIterations+=used;
  if(!Expect(used<=1,"Raycast exceeded a one-polygon quantum")) return false;
 }
 if(!Expect(sliced.valid && rayIterations>0 && glm::distance(sliced.position,blocked.position)<1.e-5f,
  "Synchronous raycast differs from its sliced driver")) return false;
 const auto boundary=query.FindNearestBoundary(from,2,extents);
 auto slicedBoundary=query.BeginNearestBoundary(from,2,extents); int wallIterations=0;
 for(int count=0;count<512 && !slicedBoundary.finished;++count)
 {
  int used=0; slicedBoundary=query.AdvanceNearestBoundary(1,used); wallIterations+=used;
  if(!Expect(used<=1,"Boundary search exceeded a one-node quantum")) return false;
 }
 if(!Expect(boundary.valid && boundary.found && boundary.distance>0 && boundary.distance<2 && wallIterations>0 &&
  slicedBoundary.valid && slicedBoundary.found && glm::distance(boundary.position,slicedBoundary.position)<1.e-5f,
  "Nearest reachable boundary differs between synchronous and sliced execution")) return false;
 const auto absent=query.FindNearestBoundary({-3,0,-3},.01f,extents);
 if(!Expect(absent.valid && !absent.found && std::abs(absent.distance-.01f)<1.e-6f && glm::length(absent.normal)==0,
  "A successful search with no boundary returned an uninitialized or fake hit")) return false;
 if(!Expect(query.NavRaycast(from,from,extents).failure==VansNavigationPathFailure::InvalidInput &&
  query.NavRaycast(from,{10001,0,0},extents).failure==VansNavigationPathFailure::InvalidInput &&
  query.FindNearestBoundary(from,0,extents).failure==VansNavigationPathFailure::InvalidInput &&
  query.FindNearestBoundary(from,std::numeric_limits<float>::infinity(),extents).failure==VansNavigationPathFailure::InvalidInput,
  "Spatial query repaired an invalid distance or accepted unbounded input")) return false;
 VansNavigationGeometry splitGeometry;
 const auto quad=[&](float minX,float maxX,float minZ,float maxZ,std::uint8_t area)
 {
  const int first=static_cast<int>(splitGeometry.VertexCount());
  splitGeometry.vertices.insert(splitGeometry.vertices.end(),{minX,0,minZ,minX,0,maxZ,maxX,0,maxZ,maxX,0,minZ});
  splitGeometry.indices.insert(splitGeometry.indices.end(),{first,first+1,first+2,first,first+2,first+3});
  splitGeometry.areas.insert(splitGeometry.areas.end(),{area,area});
 };
 quad(-12,0,-12,12,0); quad(0,12,-12,3.2f,0); quad(0,12,3.2f,12,1);
 VansNavigationSettings filtered; filtered.maps.front().tileSizeCells=32;
 filtered.areas.definitions.push_back({"Blocked",1,1,false});
 auto filteredMap=std::make_shared<VansNavigationMap>();
 if(!Expect(BuildNavigationMap(splitGeometry,filtered,filteredMap,error) &&
  query.Initialize(filteredMap,filtered.query,"Default",error),error)) return false;
 const auto closedPortal=query.FindNearestBoundary({-.2f,0,5},1,extents);
 const auto openPortal=query.FindNearestBoundary({-.2f,0,1},1,extents);
 if(!Expect(closedPortal.valid && closedPortal.found && closedPortal.distance<.4f &&
  std::abs(closedPortal.position.x)<.05f && openPortal.valid && !openPortal.found,
  "Partial filtered Tile portal lost its solid span or produced a wall across the open span")) return false;
 VansSceneObjectBuildPlan plan;
 AppendEnvironmentBox(plan,"SpatialLongGround",{0,-.1f,0},{22,.1f,22});
 VansNavigationGeometry geometry;
 auto settings=sourceSettings; settings.maps.front().tileSizeCells=32;
 if(!Expect(CollectContractGeometry(plan,settings.areas,{},geometry,error),error)) return false;
 auto map=std::make_shared<VansNavigationMap>();
 if(!Expect(BuildNavigationMap(geometry,settings,map,error),error)) return false;
 auto limited=settings.query; limited.maximumSearchNodes=4;
 if(!Expect(query.Initialize(map,limited,"Default",error),error)) return false;
 if(!Expect(query.FindNearestBoundary({0,0,0},20,extents).failure==VansNavigationPathFailure::SearchNodeCapacityExceeded,
  "Boundary node exhaustion became a partial or successful boundary result")) return false;
 if(!Expect(query.Initialize(map,settings.query,"Default",error),error)) return false;
 const auto longRay=query.NavRaycast({-18,0,0},{18,0,0},extents);
 if(!Expect(longRay.valid && !longRay.hit && longRay.tiles.size()>2,"Cross-Tile spatial ray missed stitched portals")) return false;
 auto tiny=settings.query; tiny.maximumCorridorPolygons=2;
 if(!Expect(query.Initialize(map,tiny,"Default",error),error)) return false;
 if(!Expect(query.NavRaycast({-18,0,0},{18,0,0},extents).failure==VansNavigationPathFailure::RaycastCapacityExceeded,
  "Raycast result capacity was reported as a valid hit or business Partial")) return false;
 if(!Expect(query.Initialize(map,settings.query,"Default",error),error)) return false;
 auto pending=query.BeginRaycast({-18,0,0},{18,0,0},extents); int used=0;
 pending=query.AdvanceRaycast(1,used);
 if(!Expect(!pending.finished && used==1,"Long ray did not yield after one polygon")) return false;
 const auto asset=map->GetAsset();
 if(!Expect(map->Initialize(asset,settings,"Humanoid",error) &&
  query.AdvanceRaycast(1,used).failure==VansNavigationPathFailure::MeshChanged && used==0,
  "Sliced ray used native references after a Map commit")) return false;
 if(!Expect(query.Initialize(map,settings.query,"Default",error),error)) return false;
 auto pendingBoundary=query.BeginNearestBoundary({0,0,0},20,extents);
 pendingBoundary=query.AdvanceNearestBoundary(1,used);
 if(!Expect(!pendingBoundary.finished && used==1,"Large boundary search did not yield after one node")) return false;
 if(!Expect(map->Initialize(asset,settings,"Humanoid",error) &&
  query.AdvanceNearestBoundary(1,used).failure==VansNavigationPathFailure::MeshChanged && used==0,
  "Boundary search read a retired Map between quanta")) return false;
 settings.query.maximumIterationsPerFrame=1; settings.query.maximumIterationsPerSlice=1;
 settings.query.maximumPathStartsPerFrame=1;
 VansNavigationWorld world;
 if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error),error)) return false;
 VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::Raycast;
 request.start={-18,0,0}; request.goal={18,0,0};
 const auto rayHandle=world.RequestQuery(request,"spatial-ray",error);
 request.kind=VansNavigationQueryKind::NearestBoundary; request.start={0,0,0}; request.radius=20;
 const auto boundaryHandle=world.RequestQuery(request,"spatial-boundary",error);
 if(!Expect(rayHandle.IsValid() && boundaryHandle.IsValid(),error)) return false;
 std::optional<VansNavigationQuerySnapshot> rayResult,boundaryResult;
 for(int frame=0;frame<512;++frame)
 {
  world.Update(1./60.); const auto stats=world.CaptureQueryStats();
  if(!Expect(stats.iterationsLastFrame<=1 && stats.startsLastFrame<=1,"Spatial queries escaped the shared frame budget")) return false;
  rayResult=world.CaptureQuery(rayHandle,"spatial-ray",error); boundaryResult=world.CaptureQuery(boundaryHandle,"spatial-boundary",error);
  if(rayResult && boundaryResult && rayResult->lifecycle==VansNavigationQueryLifecycle::Finished &&
   boundaryResult->lifecycle==VansNavigationQueryLifecycle::Finished) break;
 }
 if(!Expect(rayResult && rayResult->raycast && rayResult->raycast->valid && rayResult->iterations>1 &&
  boundaryResult && boundaryResult->boundary && boundaryResult->boundary->valid && boundaryResult->iterations>1 &&
  rayResult->path.status==VansNavigationPathStatus::None && boundaryResult->path.status==VansNavigationPathStatus::None,
  "Owned scheduler spatial results contain fake path topology or did not finish")) return false;
 request.kind=VansNavigationQueryKind::Raycast; request.start={-18,0,0}; request.goal={18,0,0};
 const auto cancelled=world.RequestQuery(request,"spatial-cancel",error); world.Update(1./60.);
 if(!Expect(world.CancelQuery(cancelled,"spatial-cancel",error) &&
  world.CaptureQuery(cancelled,"spatial-cancel",error)->lifecycle==VansNavigationQueryLifecycle::Cancelled,
  "Spatial cancellation lost the unified owner/session lifecycle")) return false;
 world.Shutdown();
 std::cout<<"[NavigationSpatial] PASS: wall/clear ray, nearest/no boundary, one-node slices, shared budget, capacity and stale Map rejection\n";
 return true;
}

class StationaryNavigationContractMotor final : public Vans::IVansNavigationMotor
{
public:
	Vans::VansNavigationMotorSample Read() const override { return { {}, {}, true, false }; }
	void Submit(const Vans::VansNavigationMotion&) override {}
};

class BudgetNavigationContractMotor final : public Vans::IVansNavigationMotor
{
public:
    Vans::VansNavigationMotorSample Read() const override { return {{-4,.1f,0},{},true,false}; }
    void Submit(const Vans::VansNavigationMotion& value) override { motion=value; }
    Vans::VansNavigationMotion motion;
};

class SampledNavigationContractMotor final : public Vans::IVansNavigationMotor
{
public:
    Vans::VansNavigationMotorSample Read() const override { return sample; }
    void Submit(const Vans::VansNavigationMotion& value) override { motion=value; }
    Vans::VansNavigationMotorSample sample{{-4,.1f,0},{},true,false};
    Vans::VansNavigationMotion motion;
};

bool CheckNavigationBoundaryCache()
{
    using namespace Vans;
    VansNavigationSettings settings; VansNavigationGeometry geometry; std::string error;
    VansSceneObjectBuildPlan plan;
    AppendEnvironmentBox(plan,"BoundaryCacheFloor",{0,-.1f,0},{16,.1f,16});
    AppendEnvironmentBox(plan,"BoundaryCacheWall",{0,1,0},{.2f,1,2});
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    auto map=std::make_shared<VansNavigationMap>();
    if(!Expect(CollectContractGeometry(plan,settings.areas,{},geometry,error) &&
        BuildContractNavigation(geometry,settings,"World","Scenes/BoundaryCacheContract.json",asset,error) &&
        map->Initialize(asset,settings,"Humanoid",error),error)) return false;
    const glm::vec3 start{-3.2f,.1f,0},goal{4,.1f,0};
    VansNavigationQueryContext query;
    if(!Expect(query.Initialize(map,settings.query,"Default",error),error)) return false;
    const auto path=query.FindPath(start,goal);
    if(!Expect(path.status==VansNavigationPathStatus::Complete && !path.tiles.empty(),"Boundary cache fixture has no native Corridor")) return false;
    const auto route=query.CaptureCorridorRoute();
    const auto& a=settings.avoidance; int used=0;
    const auto capture=[&](const glm::vec3& position,int budget,float radius=-1.0f,int polygons=32)
    { return query.CaptureLocalBoundary(position,radius>0?radius:a.queryRangeMeters,polygons,a.maximumBoundarySegments,budget,used); };
    const auto deferred=capture(start,0);
    if(!Expect(deferred.pending && !used && !deferred.count && deferred.failure==VansNavigationPathFailure::None,
        "Cold boundary query exceeded a zero quantum or published partial edges")) return false;
    const auto cold=capture(start,settings.query.maximumIterationsPerFrame);
    if(!Expect(!cold.pending && cold.failure==VansNavigationPathFailure::None && cold.count>0 && used>0 &&
        used<=settings.query.maximumIterationsPerFrame,"Cold boundary capture failed its explicit quantum")) return false;
    const auto sameBoundary=[](const VansNavigationLocalBoundary& left,const VansNavigationLocalBoundary& right)
    {
        if(left.count!=right.count || left.limited!=right.limited || left.failure!=right.failure || left.pending!=right.pending) return false;
        for(std::size_t i=0;i<left.count;++i) for(std::size_t end=0;end<2;++end)
            if(glm::distance(left.segments[i][end],right.segments[i][end])>1.e-5f) return false;
        return true;
    };
    if(!Expect(sameBoundary(cold,capture(start,0)) && used==0,"Warm boundary cache still consumed nodes or changed its edges")) return false;
    const auto nearPosition=start+glm::vec3(.15f,0,0);
    const auto reused=capture(nearPosition,0);
    VansNavigationQueryContext fresh;
    if(!Expect(fresh.Initialize(map,settings.query,"Default",error) &&
        fresh.AdoptCorridorRoute(route)==VansNavigationPathFailure::None,error)) return false;
    int freshUsed=0;
    const auto rebuilt=fresh.CaptureLocalBoundary(nearPosition,a.queryRangeMeters,a.maximumBoundaryPolygons,a.maximumBoundarySegments,
        settings.query.maximumIterationsPerFrame,freshUsed);
    if(!Expect(!reused.pending && !used && freshUsed>0 && sameBoundary(reused,rebuilt),
        "Moved cache lost or misranked an edge relative to a fresh native neighborhood")) return false;
    if(!Expect(capture(start+glm::vec3(a.queryRangeMeters*.5f,0,0),0).pending && !used &&
        !capture(start+glm::vec3(a.queryRangeMeters*.5f,0,0),settings.query.maximumIterationsPerFrame).pending && used>0 &&
        capture(start,0,a.queryRangeMeters*.6f).pending && !used &&
        !capture(start,settings.query.maximumIterationsPerFrame).pending && used>0,
        "Boundary movement/radius changes reused uncovered geometry or failed to refresh")) return false;
    const auto capacity=capture(start,settings.query.maximumIterationsPerFrame,-1,1);
    if(!Expect(capacity.failure==VansNavigationPathFailure::BoundaryCapacityExceeded && !capacity.count && used>0 &&
        sameBoundary(cold,capture(start,0)) && used==0,"Rejected native refresh replaced the last valid boundary cache")) return false;
    const auto invalid=capture({(std::numeric_limits<float>::quiet_NaN)(),0,0},0);
    if(!Expect(invalid.failure==VansNavigationPathFailure::InvalidInput && !used && !invalid.count &&
        sameBoundary(cold,capture(start,0)),"Invalid capture published old geometry or corrupted valid cache state")) return false;
    std::vector<VansNavigationTileCell> cells;
    if(!Expect(map->CaptureResidentCells(cells,error),error)) return false;
    std::optional<VansNavigationTileCell> remoteCell;
    const auto& mapSettings=settings.maps.front(); const float width=mapSettings.cellSizeMeters*mapSettings.tileSizeCells;
    for(const auto cell:cells)
    {
        const float x=mapSettings.originMeters.x+cell.x*width,z=mapSettings.originMeters.z+cell.z*width;
        const float dx=(std::max)({x-start.x,start.x-x-width,0.0f}),dz=(std::max)({z-start.z,start.z-z-width,0.0f});
        const bool corridor=std::any_of(path.tiles.begin(),path.tiles.end(),[&](const auto& tile){return tile.x==cell.x && tile.z==cell.z;});
        if(!corridor && dx*dx+dz*dz>4*a.queryRangeMeters*a.queryRangeMeters) { remoteCell=cell; break; }
    }
    if(!Expect(remoteCell.has_value(),"Boundary cache fixture has no remote resident Tile")) return false;
    const auto restore=[&](VansNavigationTileCell cell)
    {
        VansNavigationTileUpdate update; if(!map->CaptureTileUpdate(update,error)) return false;
        VansNavigationTileBuildOutput output;
        VansNavigationProfileBuildData savedTiles;
    if(!Expect(asset->ReadProfile("Humanoid",savedTiles,error),error)) return false;
    for(const auto& tile:savedTiles.tiles)
            if(tile.x==cell.x && tile.z==cell.z) output.tiles.push_back(tile);
        return map->ApplyTileUpdate(update,output,settings.maximumConcurrentBuildBytes,error).state==VansNavigationTileCommitState::Applied;
    };
    if(!Expect(map->InvalidateTiles({*remoteCell},error) && capture(start,0).pending && !used &&
        capture(start,settings.query.maximumIterationsPerFrame).failure==VansNavigationPathFailure::None && used>0 &&
        restore(*remoteCell) && capture(start,0).pending && !used &&
        capture(start,settings.query.maximumIterationsPerFrame).failure==VansNavigationPathFailure::None && used>0,
        "Traversal invalidation/publication reused an old boundary snapshot")) return false;
    const VansNavigationTileCell current{path.tiles.front().x,path.tiles.front().z};
    if(!Expect(map->InvalidateTiles({current},error) && capture(start,0).failure==VansNavigationPathFailure::DataUnavailable &&
        !used && restore(current) && capture(start,0).failure==VansNavigationPathFailure::MeshChanged && !used,
        "Dirty/replaced Corridor Tile exposed stale cached edges")) return false;
    if(!Expect(query.FindPath(start,goal).status==VansNavigationPathStatus::Complete && capture(start,0).pending &&
        !capture(start,settings.query.maximumIterationsPerFrame).pending,"Replaced Corridor did not rebuild its boundary state")) return false;
    query.ResetQuery();
    if(!Expect(capture(start,0).failure==VansNavigationPathFailure::InvalidInput && !used,
        "Reset retained a usable boundary without its Corridor")) return false;

    // Real World consumers must all sample in one shared-budget frame. Repeated
    // native neighborhood work per Agent could never satisfy this at 512 nodes.
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansNavigationWorld world; VansRuntimeWorld runtime;
    VansRuntimePhaseGuard cleanup([&](){world.Shutdown();jobs.Shutdown();});
    std::vector<VansNavigationAgentRegistration> registrations;
    std::vector<std::shared_ptr<SampledNavigationContractMotor>> motors;
    for(int i=0;i<64;++i)
    {
        const auto entity=runtime.CreateEntity({VansAssetGuid::FromStableName("BoundaryCacheWorld",std::to_string(i)).ToString(),"BoundaryCacheAgent",{},true});
        auto motor=std::make_shared<SampledNavigationContractMotor>();
        motor->sample.position={-10.5f+(i%8)*3.0f,.1f,-10.5f+(i/8)*3.0f};
        VansRuntimeNavigationAgentComponent config; config.avoidance=true;
        registrations.push_back({entity,config,map,motor}); motors.push_back(std::move(motor));
    }
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error) && world.RegisterAgents(registrations,error),error)) return false;
    for(std::size_t i=0;i<registrations.size();++i)
        if(!Expect(world.RequestMove(registrations[i].entity,"BoundaryCache.World",motors[i]->sample.position+glm::vec3(.6f,0,0),{},error).IsValid(),error)) return false;
    bool allSampled=false; int warmFrame=0,warmNodes=0;
    for(int frame=0;frame<100 && !allSampled;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0); allSampled=true;
        const auto stats=world.CaptureQueryStats();
        if(!Expect(stats.iterationsLastFrame<=settings.query.maximumIterationsPerFrame,"World boundary refresh exceeded shared node budget")) return false;
        for(const auto& registration:registrations)
        {
            const auto snapshot=world.CaptureAgent(registration.entity);
            if(!Expect(snapshot && snapshot->state!=VansNavigationMoveState::Finished && !snapshot->recoveryCount,
                "Cache warmup starved or falsely recovered a valid Agent")) return false;
            allSampled=allSampled && snapshot->avoidance.samples>0;
        }
        if(allSampled) { warmFrame=frame+1; warmNodes=stats.iterationsLastFrame; }
    }
    if(!Expect(allSampled,"64 World Agents could not all use their cached boundaries within one shared-budget frame")) return false;
    std::cout<<"[NavigationBoundaryCache] PASS: current-position equivalence, zero-node hits, bounded/atomic refresh, move/radius/Tile/Corridor invalidation; 64 Agent warm frame="<<warmFrame<<", nodes="<<warmNodes<<"\n";
    return true;
}

bool CheckNavigationObstacleAvoidance(const Vans::VansNavigationAvoidanceSettings& settings)
{
    using namespace Vans; std::string error; VansNavigationAvoidance avoidance;
    if(!Expect(avoidance.Initialize(64,settings,error),error)) return false;
    VansNavigationAvoidanceAgent agent; agent.identity=1; agent.radiusMeters=.35f; agent.heightMeters=1.8f;
    const auto sample=[&](const VansNavigationObstacleGeometry& shape,const glm::vec3& velocity,std::uint32_t map=0,std::uint32_t profiles=1)
    {
        avoidance.BeginFrame(); const int index=avoidance.AddAgent(agent);
        if(!avoidance.AddObstacle(1,map,profiles,shape,velocity)) return VansNavigationAvoidanceResult{};
        return avoidance.SelectVelocity(index,{1,0,0},1,{});
    };
    avoidance.BeginFrame(); const auto clear=avoidance.SelectVelocity(avoidance.AddAgent(agent),{1,0,0},1,{});
    VansNavigationObstacleGeometry shape; shape.id="Avoidance.Physical"; shape.center={1.25f,0,0}; shape.radius=.35f;
    const auto cylinder=sample(shape,{0,0,0}),approaching=sample(shape,{-1,0,0}),departing=sample(shape,{2,0,0});
    if(!Expect(cylinder.obstacles==1 && cylinder.samples>0 && glm::distance(cylinder.velocity,clear.velocity)>.1f &&
        approaching.samples>0 && departing.samples>0 && glm::distance(approaching.velocity,departing.velocity)>.1f &&
        glm::distance(departing.velocity,clear.velocity)<.05f,
        "Physical Cylinder did not use uncooperative translation velocity")) return false;
    shape.kind=VansNavigationObstacleGeometryKind::Box; shape.center={0,1,2}; shape.halfExtents={8,1,.1f};
    const auto parallel=sample(shape,{0,0,0});
    if(!Expect(parallel.obstacles==1 && parallel.neighbors==1 && parallel.samples>0 && glm::distance(parallel.velocity,clear.velocity)<1.e-5f,
        "Long Box was approximated by a bounding circle or duplicated across grid cells")) return false;
    shape.center={1,1,0}; shape.yawRadians=1.57079632679f;
    const auto rotated=sample(shape,{0,0,0});
    if(!Expect(rotated.obstacles==1 && rotated.samples>0 && glm::distance(rotated.velocity,clear.velocity)>.1f,
        "Rotated Box did not use its physical finite edges")) return false;
    shape.center={20,1,0}; shape.halfExtents={19.4f,1,.15f}; shape.yawRadians=0;
    const auto distantCenter=sample(shape,{0,0,0});
    if(!Expect(distantCenter.obstacles==1 && distantCenter.samples>0 && glm::distance(distantCenter.velocity,clear.velocity)>.1f,
        "Near Box edge was missed because its center is outside the query cells")) return false;
    shape.center={1,1,0}; shape.halfExtents={.1f,1,1};
    const auto movingBox=sample(shape,{-1,0,0}),escapingBox=sample(shape,{2,0,0});
    if(!Expect(movingBox.samples>0 && escapingBox.samples>0 && glm::distance(movingBox.velocity,escapingBox.velocity)>.1f &&
        glm::distance(escapingBox.velocity,clear.velocity)<1.e-5f,"Box capsule sweep ignored relative translation")) return false;
    const auto differentMap=sample(shape,{},1),differentProfile=sample(shape,{},0,2);
    shape.center.y=5; const auto upperFloor=sample(shape,{});
    if(!Expect(!differentMap.obstacles && !differentProfile.obstacles && !upperFloor.obstacles &&
        glm::distance(upperFloor.velocity,clear.velocity)<1.e-5f,"Obstacle Map/Profile/physical height isolation failed")) return false;
    shape.center={0,1,0}; shape.halfExtents={2,1,2};
    const auto interior=sample(shape,{});
    if(!Expect(interior.obstacles==1 && interior.samples>0 && interior.limited && interior.velocity==glm::vec3(0),
        "Agent inside a physical Box published unchecked motion")) return false;
    shape.halfExtents={100,1,100}; avoidance.BeginFrame(); const int oversized=avoidance.AddAgent(agent);
    if(!Expect(!avoidance.AddObstacle(1,0,1,shape,{}) && avoidance.SelectVelocity(oversized,{1,0,0},1,{}).samples==0,
        "Oversized body exceeded its cell footprint or published incomplete frame motion")) return false;
    auto entryLimited=settings; entryLimited.maximumCellsPerBody=4; entryLimited.maximumGridEntries=64;
    if(!Expect(avoidance.Initialize(64,entryLimited,error),error)) return false;
    for(int i=0;i<64;++i) { auto fact=agent; fact.identity=i+1; if(!Expect(avoidance.AddAgent(fact)>=0,"Exact entry capacity rejected")) return false; }
    shape.kind=VansNavigationObstacleGeometryKind::Cylinder; shape.center={1,0,0}; shape.radius=.35f;
    const bool admitted=avoidance.AddObstacle(1,0,1,shape,{});
    const auto full=avoidance.SelectVelocity(0,{1,0,0},1,{});
    if(!Expect(!admitted && full.limited && !full.samples && full.velocity==glm::vec3(0),"Grid entry exhaustion published partial motion")) return false;
    avoidance.BeginFrame(); const auto renewed=sample(shape,{});
    if(!Expect(renewed.obstacles==1 && renewed.samples>0,"New frame retained an earlier capacity failure")) return false;
    std::cout<<"[NavigationObstacleAvoidance] PASS: translating Box/Cylinder, exact rotated/long edges, footprint dedup, Map/Profile/floor isolation, interior and atomic capacity rejection\n";
    return true;
}
bool CheckNavigationLinkAuthoring(const Vans::VansNavigationSettings& originalSettings)
{
    using namespace Vans; std::string error;
    const auto* descriptor=VansComponentTypeCatalog::Find("NavigationLink");
    auto data=VansComponentTypeCatalog::CreateDefaultData("NavigationLink"); VansRuntimeNavigationLinkComponent component;
    if(!Expect(descriptor && descriptor->inspectorAddable && descriptor->runtimeTypeId==VansRuntimeComponentType_NavigationLink &&
        VansSceneNavigationComponentReader::Read(data,component,error),"NavigationLink catalog/default/reader is incomplete")) return false;
    const auto preservedComponent=component;
    SetSerializedObjectField(data,"radiusMeters",VansSerializedValue::Float(0));
    if(!Expect(!VansSceneNavigationComponentReader::Read(data,component,error) && component.endMeters==preservedComponent.endMeters,
        "Invalid Link authoring replaced the last valid component")) return false;
    auto settings=originalSettings; settings.maps.front().tileSizeCells=32;
    auto excluded=settings.profiles.front(); excluded.id="LinkExcluded"; settings.profiles.push_back(excluded); settings.maps.front().profiles.push_back(excluded.id);
    auto mesh=std::make_shared<VansTriangleMeshData>();
    mesh->positions={-4,0,-8,-4,0,-1,4,0,-1,4,0,-8,-4,0,1,-4,0,8,4,0,8,4,0,1};
    mesh->indices={0,1,2,0,2,3,4,5,6,4,6,7};
    VansNavigationSourceSnapshot snapshot; snapshot.collisionLayers={"Default","Environment"}; snapshot.surfaces.push_back(ContractSurface(settings));
    VansNavigationSourceRecord floor; floor.id="LinkAuthoring.Floor"; floor.ownerId="LinkAuthoring.Owner"; floor.geometry=mesh;
    floor.boundsMinimum={-4,0,-8}; floor.boundsMaximum={4,0,8}; floor.collisionLayer="Environment"; snapshot.sources.push_back(floor);
    VansNavigationLinkRecord authored; authored.id=VansAssetGuid::FromStableName("NavigationLinkAuthoring","Connection").ToString();
    authored.link.startMeters={.2f,0,-2}; authored.link.endMeters={.2f,0,2}; authored.link.radiusMeters=.6f; snapshot.links.push_back(authored);
    VansNavigationBuildInput input; std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) && input.profiles.front().links.size()==1 &&
        VansNavigationBuilder::Build(input,settings,"Scenes/LinkAuthoring.json",nullptr,asset,error),error)) return false;
    const auto key=input.profiles.front().links.front().key;
    const auto keptInput=input.profiles.front().geometry;
    auto bounded=snapshot;
    bounded.surfaces.front().surface.boundsHalfExtentsMeters={4,5,5};
    bounded.links.front().link.endMeters={.2f,0,6};
    if(!Expect(!VansNavigationSourceCollector::Collect(bounded,settings,"World",input,error) &&
        error.find("Surface boundary")!=std::string::npos && input.profiles.front().geometry==keptInput,
        "An outbound Link bypassed the local Surface boundary or changed the accepted input")) return false;
    bounded=snapshot;
    bounded.surfaces.front().surface.boundsHalfExtentsMeters={4,5,1};
    bounded.surfaces.front().surface.boundsCenterMeters.z=-2;
    auto exitSurface=bounded.surfaces.front(); exitSurface.id="LinkAuthoring.Exit";
    exitSurface.surface.boundsCenterMeters.z=2; bounded.surfaces.push_back(exitSurface);
    if(!Expect(!VansNavigationSourceCollector::Collect(bounded,settings,"World",input,error) &&
        error.find("Surface boundary")!=std::string::npos && input.profiles.front().geometry==keptInput,
        "A Link crossed an unauthored gap between bounded Surfaces")) return false;
    exitSurface.surface.boundsCenterMeters.z=0; exitSurface.surface.boundsHalfExtentsMeters.z=3;
    bounded.surfaces.back()=exitSurface;
    if(!Expect(VansNavigationSourceCollector::Collect(bounded,settings,"World",input,error),
        "A Link inside overlapping Surface union was rejected: "+error)) return false;
    if(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error)) return Expect(false,error);
    const auto connectionCount=[](const auto& tiles)
    {
        int count=0; for(const auto& tile:tiles) { dtMeshHeader header{}; std::memcpy(&header,tile.bytes.data(),sizeof(header)); count+=header.offMeshConCount; }
        return count;
    };
    VansNavigationProfileBuildData linkedProfile,excludedProfile;
    if(!Expect(asset->ReadProfile(asset->GetProfiles().front().profileId,linkedProfile,error) &&
        asset->ReadProfile(asset->GetProfiles().back().profileId,excludedProfile,error),error)) return false;
    if(!Expect(connectionCount(linkedProfile.tiles)==1 && connectionCount(excludedProfile.tiles)==0,
        "Link was duplicated at a Tile border or admitted to an excluded Profile")) return false;
    std::string encoded; std::shared_ptr<const VansNavigationMeshAsset> cold;
    if(!Expect(VansNavigationMeshCodec::Encode(*asset,encoded,error) && VansNavigationMeshCodec::Decode(encoded,cold,error),error)) return false;
    auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query;
    const glm::vec3 start(.2f,0,-4),goal(.2f,0,4);
    if(!Expect(map->Initialize(cold,settings,"Humanoid",error) && query.Initialize(map,settings.query,"Default",error) &&
        query.FindPath(start,goal).status!=VansNavigationPathStatus::Complete && query.SetLinkCapabilities({key},error) &&
        query.FindPath(start,goal).status==VansNavigationPathStatus::Complete,"Authored cross-Tile Link did not survive the cold codec/capability boundary")) return false;
    VansNavigationDebugView view;
    view.planes={glm::vec4(1,0,0,20),glm::vec4(-1,0,0,20),glm::vec4(0,1,0,1),glm::vec4(0,-1,0,8),glm::vec4(0,0,1,20),glm::vec4(0,0,-1,20)};
    VansNavigationDebugGeometry visible,hidden;
    if(!Expect(map->CaptureDebugGeometry(view,"Default",visible,error) && visible.links.size()==1 && visible.links.front().key==key &&
        visible.links.front().bidirectional && visible.links.front().areaAllowed && visible.links.front().radiusMeters==.6f,
        "Viewport snapshot lost native Link identity/direction/radius")) return false;
    auto withoutLinks=view; withoutLinks.maximumLinks=0;
    if(!Expect(map->CaptureDebugGeometry(withoutLinks,"Default",hidden,error) && hidden.links.empty() && hidden.triangles.size()==visible.triangles.size(),
        "Disabling Link snapshots changed existing triangle geometry")) return false;
    auto invalidView=view; invalidView.maximumLinks=kMaximumNavigationDebugLinks+1;
    if(!Expect(!map->CaptureDebugGeometry(invalidView,"Default",visible,error) && visible.links.size()==1 && visible.links.front().key==key,
        "Invalid Link debug budget replaced the last valid viewport data")) return false;
    snapshot.links.front().link.bidirectional=false;
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        VansNavigationBuilder::Build(input,settings,"Scenes/LinkAuthoring.json",nullptr,asset,error) && map->Initialize(asset,settings,"Humanoid",error) &&
        query.Initialize(map,settings.query,"Default",error) && query.SetLinkCapabilities({key},error) &&
        query.FindPath(start,goal).status==VansNavigationPathStatus::Complete && query.FindPath(goal,start).status!=VansNavigationPathStatus::Complete,
        "Authored one-way direction was lost in native construction")) return false;
    const auto preserved=asset; snapshot.links.front().link.endMeters={100,0,100};
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        !VansNavigationBuilder::Build(input,settings,"Scenes/LinkAuthoring.json",nullptr,asset,error) && asset==preserved,
        "Unattached Link endpoint published a partial cooked asset")) return false;
    snapshot.links.front()=authored; snapshot.links.push_back(authored); const auto oldKey=input.profiles.front().links.front().key;
    if(!Expect(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) && input.profiles.front().links.front().key==oldKey,
        "Duplicate Link identity replaced the frozen build input")) return false;
    snapshot.links.pop_back(); settings.maps.front().updateMode=VansNavigationMapUpdateMode::Runtime;
    if(!Expect(VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) && input.profiles.front().footprints.size()>=2 &&
        VansNavigationBuilder::Build(input,settings,"Scenes/LinkAuthoringRuntime.json",nullptr,asset,error) &&
        VansNavigationMeshCodec::Encode(*asset,encoded,error) && VansNavigationMeshCodec::Decode(encoded,cold,error) &&
        !cold->GetProfiles().front().layerData.empty() && map->Initialize(cold,settings,"Humanoid",error),error)) return false;
    std::vector<VansNavigationTileCell> cells; VansNavigationTileUpdate update;
    if(!Expect(map->CaptureResidentCells(cells,error) && map->InvalidateTiles(cells,error) && map->CaptureTileUpdate(update,error),error)) return false;
    std::vector<VansNavigationTileLayerData> seeds;
    if(!Expect(CaptureMapLayerSeeds(map,update,8u*1024u*1024u,seeds,error) && !seeds.empty(),error)) return false;
    auto source=input.profiles.front(); VansNavigationObstacleGeometry carve; carve.id="LinkAuthoring.Carve"; carve.center={2,0,-4}; source.carveObstacles={carve};
    VansNavigationBuildContext cache(8u*1024u*1024u); VansNavigationTileBuildOutput rebuilt;
    if(!Expect(VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cells,update.rasterOriginMeters,
        settings.maximumConcurrentBuildBytes,nullptr,rebuilt,error,&cache,&seeds) && connectionCount(rebuilt.tiles)==1 &&
        map->ApplyTileUpdate(update,rebuilt,settings.maximumConcurrentBuildBytes,error).state==VansNavigationTileCommitState::Applied &&
        query.Initialize(map,settings.query,"Default",error) && query.SetLinkCapabilities({key},error) &&
        query.FindPath(start,goal).status==VansNavigationPathStatus::Complete,
        "Cold compressed-layer Carve/republication removed the Link connection: "+error)) return false;
    source.carveObstacles.front().center.x=3;
    if(!Expect(map->InvalidateTiles(cells,error) && map->CaptureTileUpdate(update,error) &&
        VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cells,update.rasterOriginMeters,
            settings.maximumConcurrentBuildBytes,nullptr,rebuilt,error,&cache) && connectionCount(rebuilt.tiles)==1 &&
        map->ApplyTileUpdate(update,rebuilt,settings.maximumConcurrentBuildBytes,error).state==VansNavigationTileCommitState::Applied &&
        query.Initialize(map,settings.query,"Default",error) && query.SetLinkCapabilities({key},error) && query.FindPath(start,goal).status==VansNavigationPathStatus::Complete,
        "Warm TileCache update lost its owned Link parameters: "+error)) return false;
    std::cout<<"[NavigationLinkAuthoring] PASS: strict component, Scene/runtime hierarchy producer equivalence, profile admission, cross-Tile ownership/direction, cold asset, atomic endpoint rejection and cold/warm TileCache publication\n";
    return true;
}
bool CheckNavigationLinkQuery(const Vans::VansNavigationSettings& settings)
{
    using namespace Vans; std::string error;
    VansNavigationGeometry geometry;
    geometry.vertices={1,0,1,1,0,5,7,0,5,7,0,1}; geometry.indices={0,1,2,0,2,3}; geometry.areas={0,0};
    std::shared_ptr<const VansNavigationMeshAsset> base;
    if(!Expect(BuildContractNavigation(geometry,settings,"World","Scenes/LinkContract.json",base,error) &&
        base->GetProfiles().size()==1 && base->GetTileCount()==1 && base->GetProfiles().front().layerData.empty(),error)) return false;
    VansNavigationProfileBuildData profile;
    if(!Expect(base->ReadProfile(base->GetProfiles().front().profileId,profile,error),error)) return false;
    const auto& tile=profile.tiles.front();
    dtMeshHeader original{}; std::memcpy(&original,tile.bytes.data(),sizeof(original));
    const unsigned short vertices[]{5,0,10,5,0,20,15,0,20,15,0,10,25,0,10,25,0,20,35,0,20,35,0,10};
    const unsigned short polygons[]{0,1,2,3,0xffff,0xffff,0xffff,0xffff,4,5,6,7,0xffff,0xffff,0xffff,0xffff};
    const unsigned short flags[]{1,1}; const unsigned char areas[]{0,0};
    const float floorY=original.bmin[1],linkVertices[]{3,floorY,3,5,floorY,3},radii[]{.4f};
    const unsigned short linkFlags[]{1}; const unsigned char linkAreas[]{0},directions[]{DT_OFFMESH_CON_BIDIR}; const unsigned int identities[]{7};
    dtNavMeshCreateParams params{}; params.tileX=tile.x; params.tileY=tile.z; params.tileLayer=tile.layer;
    params.verts=vertices; params.vertCount=8; params.polys=polygons; params.polyFlags=flags; params.polyAreas=areas; params.polyCount=2; params.nvp=4;
    params.offMeshConVerts=linkVertices; params.offMeshConRad=radii; params.offMeshConFlags=linkFlags;
    params.offMeshConAreas=linkAreas; params.offMeshConDir=directions; params.offMeshConUserID=identities; params.offMeshConCount=1;
    params.walkableHeight=profile.bake.agentHeight; params.walkableRadius=profile.bake.agentRadius; params.walkableClimb=profile.bake.agentMaxClimb;
    std::copy(std::begin(original.bmin),std::end(original.bmin),params.bmin); std::copy(std::begin(original.bmax),std::end(original.bmax),params.bmax);
    params.cs=profile.bake.cellSize; params.ch=profile.bake.cellHeight; params.buildBvTree=true;
    unsigned char* native=nullptr; int nativeBytes=0;
    if(!Expect(dtCreateNavMeshData(&params,&native,&nativeBytes) && native,"Could not create real off-mesh fixture")) return false;
    std::unique_ptr<unsigned char,decltype(&dtFree)> nativeOwner(native,&dtFree);
    std::string encoded;
    if(!Expect(VansNavigationMeshCodec::Encode(*base,encoded,error),error)) return false;
    const auto payload=encoded.size()-base->GetPayloadBytes();
    for(unsigned int byte=0;byte<8;++byte) encoded[payload-8+byte]=static_cast<char>(static_cast<std::uint64_t>(nativeBytes)>>(8*byte));
    encoded.replace(payload,tile.bytes.size(),reinterpret_cast<const char*>(native),static_cast<std::size_t>(nativeBytes));
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!Expect(VansNavigationMeshCodec::Decode(encoded,asset,error),error)) return false;
    const auto preserved=asset;
    const auto rejects=[&](std::size_t offset,const void* value,std::size_t bytes)
    {
        auto invalid=encoded; std::memcpy(invalid.data()+payload+offset,value,bytes);
        return Expect(!VansNavigationMeshCodec::Decode(invalid,asset,error) && asset==preserved,"Invalid Link replaced a valid cold asset");
    };
    const auto connection=static_cast<std::size_t>(nativeBytes)-sizeof(dtOffMeshConnection);
    const std::uint32_t zero=0; const float zeroRadius=0;
    if(!rejects(connection+offsetof(dtOffMeshConnection,userId),&zero,sizeof(zero)) ||
        !rejects(connection+offsetof(dtOffMeshConnection,rad),&zeroRadius,sizeof(zeroRadius)) ||
        !rejects(connection+offsetof(dtOffMeshConnection,pos)+3*sizeof(float),linkVertices,3*sizeof(float))) return false;
    auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query,unsupported;
    if(!Expect(map->Initialize(asset,settings,"Humanoid",error) && query.Initialize(map,settings.query,"Default",error) &&
        unsupported.Initialize(map,settings.query,"Default",error),error)) return false;
    const glm::vec3 start(2,floorY,3),goal(6,floorY,3);
    if(!Expect(query.FindPath(start,goal).status!=VansNavigationPathStatus::Complete,"Missing Link consumer capabilities permitted a gap crossing")) return false;
    if(!Expect(query.SetLinkCapabilities({{7}},error),error)) return false;
    const auto linked=query.FindPath(start,goal); const auto route=query.CaptureCorridorRoute();
    const auto entrance=query.CaptureCorridorSteering(start);
    if(!Expect(linked.status==VansNavigationPathStatus::Complete && route && entrance.valid && entrance.link &&
        entrance.link->key.value==7 && entrance.link->bidirectional && entrance.link->radiusMeters==.4f &&
        glm::distance(entrance.link->entry,glm::vec3(3,floorY,3))<1.e-4f && glm::distance(entrance.link->exit,glm::vec3(5,floorY,3))<1.e-4f,
        "Real Detour Link flags/identity/projected endpoints were lost")) return false;
    if(!Expect(unsupported.AdoptCorridorRoute(route)==VansNavigationPathFailure::MeshChanged,
        "A route bypassed the destination consumer capability filter")) return false;
    if(!Expect(!query.SetLinkCapabilities({{0}},error) && !query.SetLinkCapabilities({{7},{7}},error) &&
        query.CaptureCorridorSteering(start).valid,"Rejected capabilities destroyed the current owned Corridor")) return false;
    std::vector<VansNavigationLinkKey> excessive(kMaximumNavigationLinkCapabilities+1);
    for(std::size_t i=0;i<excessive.size();++i) excessive[i].value=static_cast<std::uint32_t>(i+1);
    if(!Expect(!query.SetLinkCapabilities(excessive,error) && query.CaptureCorridorSteering(start).valid,
        "Oversized Link directory replaced the current capability/Corridor")) return false;
    const auto reversed=query.FindPath(goal,start); const auto reverseEntrance=query.CaptureCorridorSteering(goal);
    if(!Expect(reversed.status==VansNavigationPathStatus::Complete && reverseEntrance.link && reverseEntrance.link->entry.x==5 && reverseEntrance.link->exit.x==3,
        "Bidirectional Link did not preserve the actual approach order")) return false;
    if(!Expect(query.SetLinkCapabilities({},error) && !query.CaptureCorridorSteering(goal).valid &&
        query.FindPath(start,goal).status!=VansNavigationPathStatus::Complete,"Revoked capabilities retained the old Link Corridor")) return false;
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2); VansRuntimePhaseGuard jobCleanup([&](){jobs.Shutdown();});
    VansNavigationWorld world;
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error),error)) return false;
    VansNavigationQueryRequest request; request.start=start; request.goal=goal; request.linkCapabilities={{7}};
    const auto handle=world.RequestQuery(request,"Link.Consumer.Query",error);
    if(!Expect(handle.IsValid(),error)) return false;
    request.linkCapabilities.clear(); // worker 必须消费提交时的拥有型事实。
    std::optional<VansNavigationQuerySnapshot> completed;
    for(int frame=0;frame<settings.query.maximumQueryFrames;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1./60.); completed=world.CaptureQuery(handle,"Link.Consumer.Query",error);
        if(completed && completed->lifecycle==VansNavigationQueryLifecycle::Finished) break;
    }
    if(!Expect(completed && completed->path.status==VansNavigationPathStatus::Complete && completed->route &&
        unsupported.AdoptCorridorRoute(completed->route)==VansNavigationPathFailure::MeshChanged,"Worker lost Link capabilities or bypassed motor-owner admission")) return false;
    request.linkCapabilities={{0}};
    if(!Expect(!world.RequestQuery(request,"Invalid.Link.Consumer",error).IsValid(),"Scheduler accepted an invalid Link capability")) return false;
    VansRuntimeWorld runtime; auto motor=std::make_shared<SampledNavigationContractMotor>();
    motor->sample.position=start;
    const auto owner=runtime.CreateEntity({VansAssetGuid::FromStableName("NavigationLinkQuery","UnsupportedMotor").ToString(),"UnsupportedLinkMotor",{},true});
    if(!Expect(world.RegisterAgents({{owner,{},map,motor}},error) &&
        world.RequestMove(owner,"Unsupported.Link.Move",goal,{},error).IsValid(),error)) return false;
    std::optional<VansNavigationAgentSnapshot> stopped;
    for(int frame=0;frame<settings.query.maximumQueryFrames;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1./60.); stopped=world.CaptureAgent(owner);
        if(!Expect(motor->motion.desiredSpeed==0,"Missing Link consumer submitted a gap-crossing motor intent")) return false;
        if(stopped && stopped->state==VansNavigationMoveState::Finished) break;
    }
    if(!Expect(stopped && stopped->state==VansNavigationMoveState::Finished && stopped->result==VansNavigationMoveResult::Failed,
        "Missing Link consumer did not terminate the unreachable movement request")) return false;
    std::cout<<"[NavigationLinkQuery] PASS: real cold native connection, capability admission/revocation, atomic rejection, route isolation, bidirectional entrance identity/endpoints and unsupported World motor hold\n";
    return true;
}
bool CheckNavigationYield(const Vans::VansNavigationAvoidanceSettings& settings)
{
    using namespace Vans; std::string error; VansNavigationAvoidance avoidance;
    if(!Expect(avoidance.Initialize(4,settings,error),error)) return false;
    VansNavigationAvoidanceAgent winner,loser;
    winner.identity=1; winner.radiusMeters=.35f; winner.heightMeters=1.8f; winner.position={-1,0,0}; winner.velocity={1,0,0};
    loser=winner; loser.identity=2; loser.position={1,0,0}; loser.velocity={-1,0,0};
    VansNavigationLocalBoundary boundary; boundary.count=2;
    boundary.segments[0]={glm::vec3(-2,0,.2f),glm::vec3(2,0,.2f)};
    boundary.segments[1]={glm::vec3(2,0,-.2f),glm::vec3(-2,0,-.2f)};
    const auto sample=[&](const VansNavigationLocalBoundary& walls)
    {
        avoidance.BeginFrame(); avoidance.AddAgent(winner); const int index=avoidance.AddAgent(loser);
        return avoidance.SelectVelocity(index,{-1,0,0},2,walls);
    };
    const auto retreat=sample(boundary);
    if(!Expect(retreat.samples>0 && retreat.yield.peer==winner.identity && retreat.velocity.x>0,"Narrow counterflow did not select the stable lower identity or retreat")) return false;
    loser.yield=retreat.yield; loser.yield.seconds=.75f;
    const auto latched=sample(boundary);
    if(!Expect(latched.yield.peer==winner.identity && latched.yield.seconds==.75f,"Frame sampler created a second yield clock or lost its latch")) return false;
    avoidance.BeginFrame(); avoidance.AddAgent(winner); const int changed=avoidance.AddAgent(loser);
    if(!Expect(!avoidance.SelectVelocity(changed,{1,0,0},2,boundary).yield.peer,"Changed request direction retained its obsolete yield")) return false;
    loser.position.x=2.5f; winner.position.x=.5f;
    const auto vacate=sample(boundary);
    if(!Expect(vacate.yield.peer==winner.identity && std::abs(vacate.velocity.z)>.1f,"Yield did not vacate the passage after reaching its wide mouth")) return false;
    winner.position.x=3.5f;
    if(!Expect(!sample(boundary).yield.peer,"Passed priority peer retained a yield latch")) return false;
    loser.position.x=1; winner.position.x=-1;
    winner.reciprocal=false;
    if(!Expect(!sample(boundary).yield.peer,"Inactive priority peer retained a yield latch")) return false;
    winner.reciprocal=true; winner.position.y=4;
    if(!Expect(!sample(boundary).yield.peer,"Another floor retained a yield latch")) return false;
    winner.position.y=0; winner.profile=1;
    if(!Expect(sample(boundary).yield.peer==winner.identity,"Physically overlapping different Profile lost its yield latch")) return false;
    winner.profile=0; winner.map=1;
    if(!Expect(!sample(boundary).yield.peer,"Another Map retained a yield latch")) return false;
    winner.map=0; loser.yield={};
    auto wide=boundary; for(std::size_t i=0;i<wide.count;++i) for(auto& point:wide.segments[i]) point.z*=6;
    if(!Expect(!sample(wide).yield.peer,"Wide passage was treated as single-person counterflow")) return false;
    std::cout<<"[NavigationYield] PASS: stable order, retreat, mouth clearance, single clock, passed/inactive peer, cross-Profile collision and Map/floor isolation\n";
    return true;
}
bool CheckNavigationAvoidance(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& settings)
{
    using namespace Vans; std::string error;
    if(!CheckNavigationObstacleAvoidance(settings.avoidance)) return false;
    if(!CheckNavigationYield(settings.avoidance)) return false;
    auto invalid=settings.avoidance; invalid.adaptiveDivisions=(std::numeric_limits<int>::max)();
    if(!Expect(!ValidateNavigationAvoidanceSettings(invalid,error),"Avoidance pattern overflow was accepted")) return false;
    const auto encoded=VansNavigationSettingsJsonCodec::Encode(settings); VansNavigationSettings decoded;
    if(!Expect(VansNavigationSettingsJsonCodec::Decode(encoded,decoded,error) && SameNavigationSettings(settings,decoded),error)) return false;
    auto missingYield=encoded;
    for(auto& field:missingYield.objectFields) if(field.first=="avoidance") EraseSerializedObjectField(field.second,"maximumYieldSeconds");
    if(!Expect(!VansNavigationSettingsJsonCodec::Decode(missingYield,decoded,error) && SameNavigationSettings(settings,decoded),
        "Missing yield duration was defaulted or partially published")) return false;
    invalid=settings.avoidance; invalid.maximumYieldSeconds=0;
    if(!Expect(!ValidateNavigationAvoidanceSettings(invalid,error),"Unbounded yield duration was accepted")) return false;
    auto obsolete=encoded; EraseSerializedObjectField(obsolete,"avoidance");
    if(!Expect(!VansNavigationSettingsJsonCodec::Decode(obsolete,decoded,error) && SameNavigationSettings(settings,decoded),
        "Missing current avoidance settings silently defaulted or changed the valid document")) return false;
    auto author=VansComponentTypeCatalog::CreateDefaultData("NavigationAgent"); VansRuntimeNavigationAgentComponent component;
    if(!Expect(VansSceneNavigationComponentReader::Read(author,component,error) && !component.avoidance,error)) return false;
    EraseSerializedObjectField(author,"avoidance");
    if(!Expect(!VansSceneNavigationComponentReader::Read(author,component,error) && !component.avoidance,"Agent missing its current avoidance switch was accepted")) return false;
    author.objectFields.emplace_back("avoidance",VansSerializedValue::Bool(true));
    if(!Expect(VansSceneNavigationComponentReader::Read(author,component,error) && component.avoidance,error)) return false;
    VansNavigationAvoidance avoidance;
    if(!Expect(avoidance.Initialize(64,settings.avoidance,error),error)) return false;
    VansNavigationAvoidanceAgent self; self.identity=1; self.radiusMeters=.35f; self.heightMeters=1.8f;
    const auto add=[&](std::uint64_t identity,const glm::vec3& position,std::uint32_t mapId=0)
    { auto fact=self; fact.identity=identity; fact.position=position; fact.map=mapId; return avoidance.AddAgent(fact); };
    const int first=add(1,{0,0,0});
    const auto clear=avoidance.SelectVelocity(first,{1,0,0},1,{});
    if(!Expect(clear.samples>0 && clear.neighbors==0 && glm::distance(clear.velocity,glm::vec3(1,0,0))<.2f,"Unobstructed native velocity sampler lost its desired velocity")) return false;
    VansNavigationLocalBoundary pendingBoundary; pendingBoundary.pending=true;
    const auto incomplete=avoidance.SelectVelocity(first,{1,0,0},1,pendingBoundary);
    if(!Expect(incomplete.limited && !incomplete.samples && incomplete.velocity==glm::vec3(0),
        "Velocity sampler accepted an incomplete boundary quantum")) return false;
    avoidance.BeginFrame(); const int lower=add(1,{0,0,0}); add(2,{.6f,3,0}); add(3,{.6f,0,0},1);
    const auto separated=avoidance.SelectVelocity(lower,{1,0,0},1,{});
    if(!Expect(separated.neighbors==0 && glm::distance(separated.velocity,clear.velocity)<1.e-5f,"Different floors or logical Maps interacted through the grid")) return false;
    avoidance.BeginFrame(); const int mixed=add(1,{0,0,0}); auto shortNeighbor=self;
    shortNeighbor.identity=2; shortNeighbor.position={.8f,1.7f,0}; shortNeighbor.heightMeters=.5f;
    avoidance.AddAgent(shortNeighbor);
    if(!Expect(avoidance.SelectVelocity(mixed,{1,0,0},1,{}).neighbors==1,"Different Profile heights lost an overlapping ground-anchor interval")) return false;
    avoidance.BeginFrame(); const int moving=add(1,{0,0,0}); add(2,{.8f,0,0});
    const auto blocked=avoidance.SelectVelocity(moving,{1,0,0},1,{});
    if(!Expect(blocked.neighbors==1 && blocked.samples>0 && glm::distance(blocked.velocity,clear.velocity)>.1f && glm::length(blocked.velocity)<=1.0001f,
        "Actual nearby CPU obstacle did not change a bounded native velocity choice")) return false;
    auto dense=settings.avoidance; dense.maximumNeighborScans=16;
    if(!Expect(avoidance.Initialize(64,dense,error),error)) return false;
    for(int i=0;i<64;++i) if(!Expect(add(static_cast<std::uint64_t>(i+1),{0,0,0})>=0,"Avoidance grid rejected its exact frame capacity")) return false;
    const auto rejected=avoidance.SelectVelocity(0,{1,0,0},1,{});
    if(!Expect(rejected.limited && rejected.scans==16 && rejected.samples==0 && rejected.velocity==glm::vec3(0) && add(65,{0,0,0})<0,
        "Dense cell scan exceeded capacity or published an incompletely scanned movement")) return false;
    auto quota=settings.avoidance; quota.maximumSamplesPerFrame=(quota.adaptiveDivisions*quota.adaptiveRings+1)*quota.adaptiveDepth;
    if(!Expect(avoidance.Initialize(64,quota,error),error)) return false;
    const int once=add(1,{0,0,0}); const auto accepted=avoidance.SelectVelocity(once,{1,0,0},1,{});
    const auto exhausted=avoidance.SelectVelocity(once,{1,0,0},1,{});
    if(!Expect(accepted.samples>0 && accepted.samples<=static_cast<std::uint32_t>(quota.maximumSamplesPerFrame) && exhausted.limited && exhausted.samples==0 &&
        !avoidance.Initialize(64,invalid,error),"Frame sample budget or atomic invalid reconfiguration failed")) return false;
    avoidance.BeginFrame(); const int renewed=add(1,{0,0,0});
    if(!Expect(avoidance.SelectVelocity(renewed,{1,0,0},1,{}).samples>0 && add(2,{(std::numeric_limits<float>::max)(),0,0})<0,
        "Frame reset retained old scratch or grid coordinates overflowed")) return false;
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2); VansRuntimePhaseGuard jobCleanup([&](){jobs.Shutdown();});
    VansNavigationWorld world; VansRuntimeWorld runtime; auto motor=std::make_shared<SampledNavigationContractMotor>();
    const auto owner=runtime.CreateEntity({VansAssetGuid::New().ToString(),"Avoidance",{},true});
    if(!Expect(world.Initialize(settings,error) && world.RegisterAgents({{owner,component,map,motor}},error),error)) return false;
    const auto request=world.RequestMove(owner,"Avoidance.Contract",{4,0,0},{},error); if(!Expect(request.IsValid(),error)) return false;
    bool sampled=false;
    for(int frame=0;frame<90 && !sampled;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0); const auto snapshot=world.CaptureAgent(owner);
        if(!Expect(snapshot && snapshot->state!=VansNavigationMoveState::Finished && world.CaptureQueryStats().iterationsLastFrame<=settings.query.maximumIterationsPerFrame,
            "Actual World boundary/avoidance consumer failed or exceeded its shared node budget")) return false;
        sampled=snapshot->avoidance.samples>0 && motor->motion.desiredSpeed>0;
        if(!Expect(snapshot->avoidance.scans<=static_cast<std::uint32_t>(settings.avoidance.maximumNeighborScans) &&
            snapshot->avoidance.samples<=static_cast<std::uint32_t>(settings.avoidance.maximumSamplesPerFrame),"World bypassed avoidance quotas")) return false;
    }
    if(!Expect(sampled,"World never sampled its accepted corridor")) return false;
    VansNavigationObstacleRecord obstacle; obstacle.mode=VansNavigationObstacleMode::Avoidance;
    obstacle.shape.id="World.Avoidance.Obstacle"; obstacle.shape.center=motor->sample.position+glm::vec3(.9f,0,0);
    obstacle.shape.radius=.35f;
    if(!Expect(world.ReplaceObstacles("World.ObstacleOwner",{obstacle},error) && world.GetCarveObstacleRevision()==0,error)) return false;
    world.Update(1.0/60.0); const auto physical=world.CaptureAgent(owner);
    if(!Expect(physical && physical->avoidance.samples>0 && physical->avoidance.obstacles==1,
        "World did not freeze and sample its ordinary obstacle")) return false;
    obstacle.shape.center.x+=2.f/60.f;
    if(!Expect(world.ReplaceObstacles("World.ObstacleOwner",{obstacle},error),error)) return false;
    world.Update(1.0/60.0); const auto translating=world.CaptureAgent(owner);
    if(!Expect(translating && translating->avoidance.samples>0 && translating->avoidance.obstacles==1 &&
        glm::distance(translating->avoidance.velocity,physical->avoidance.velocity)>.05f,
        "World replacement lost the previous frozen pose or treated translation as stationary")) return false;
    world.CancelOwner("World.ObstacleOwner"); world.Update(1.0/60.0);
    const auto removed=world.CaptureAgent(owner);
    if(!Expect(removed && !removed->avoidance.obstacles && !world.GetCarveObstacleRevision(),
        "Owner cancellation retained ordinary avoidance influence or dirtied geometry")) return false;
    if(!Expect(world.Stop(request,"Avoidance.Contract",error) && motor->motion.desiredSpeed==0,
        "World did not submit sampled MotionIntent or cancel it through the existing owner")) return false;
    std::cout<<"[NavigationAvoidance] PASS: native sampling, height/Map grid isolation, dense scans, exact capacity, shared frame budgets, current schema and real World motor consumption\n";
    return true;
}
bool CheckNavigationDataWait()
{
    using namespace Vans;
    VansNavigationSettings settings; VansNavigationGeometry geometry; std::string error;
    VansSceneObjectBuildPlan plan;
    AppendEnvironmentBox(plan,"DataWaitFloor",{0,-.1f,0},{8,.1f,8});
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    auto map=std::make_shared<VansNavigationMap>();
    if(!Expect(CollectContractGeometry(plan,settings.areas,{},geometry,error) &&
        BuildContractNavigation(geometry,settings,"World","Scenes/DataWaitContract.json",asset,error) &&
        map->Initialize(asset,settings,"Humanoid",error),error)) return false;
    VansRuntimeWorld runtime;
    const auto owner=runtime.CreateEntity({VansAssetGuid::FromStableName("NavigationDataWait","Motor").ToString(),"DataWaitMotor",{},true});
    auto motor=std::make_shared<SampledNavigationContractMotor>();
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansNavigationWorld world;
    VansRuntimePhaseGuard cleanup([&](){world.Shutdown();jobs.Shutdown();});
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error) &&
        world.RegisterAgents({{owner,{},map,motor}},error),error)) return false;
    const auto request=world.RequestMove(owner,"DataWait.Move",{4,.1f,0},{},error);
    for(int frame=0;frame<80 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Following;++frame)
    { PaceNavigationWorkerFrame(world); world.Update(1.0/60.0); }
    const auto accepted=world.CaptureAgent(owner);
    if(!Expect(accepted && accepted->state==VansNavigationMoveState::Following && !accepted->path.tiles.empty(),
        "Data-wait fixture did not follow an actual native Corridor")) return false;
    const auto& dependency=accepted->path.tiles.front();
    const VansNavigationTileCell cell{dependency.x,dependency.z};
    if(!Expect(map->InvalidateTiles({cell},error),error)) return false;
    for(int frame=0;frame<240;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0);
        const auto waiting=world.CaptureAgent(owner);
        if(!Expect(waiting && waiting->state==VansNavigationMoveState::WaitingForData &&
            waiting->request.serial==request.serial && waiting->result==VansNavigationMoveResult::None &&
            waiting->recoveryCount==0 && !waiting->desiredSpeed && !motor->motion.desiredSpeed,
            "Dirty Corridor wait drove its motor, changed identity or consumed Stuck recovery time")) return false;
    }
    VansNavigationTileUpdate update;
    if(!Expect(map->CaptureTileUpdate(update,error),error)) return false;
    VansNavigationTileBuildOutput tiles;
    VansNavigationProfileBuildData savedTiles;
    if(!Expect(asset->ReadProfile("Humanoid",savedTiles,error),error)) return false;
    for(const auto& tile:savedTiles.tiles)
        if(tile.x==cell.x && tile.z==cell.z) tiles.tiles.push_back(tile);
    if(!Expect(map->ApplyTileUpdate(update,tiles,settings.maximumConcurrentBuildBytes,error).state==VansNavigationTileCommitState::Applied,
        error)) return false;
    bool resumed=false;
    for(int frame=0;frame<80;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0);
        const auto state=world.CaptureAgent(owner);
        if(state->state==VansNavigationMoveState::Following && motor->motion.desiredSpeed>0)
        { resumed=state->request.serial==request.serial && state->recoveryCount==0; break; }
    }
    if(!Expect(resumed,"Published Tile did not resume the same move request without a false recovery")) return false;
    world.Shutdown();
    settings.query.maximumQueryFrames=6;
    if(!Expect(map->InvalidateTiles({cell},error) && world.Initialize(settings,error) && world.RegisterMaps({map},error) &&
        world.RegisterAgents({{owner,{},map,motor}},error),error)) return false;
    const auto deadline=world.RequestMove(owner,"DataWait.Deadline",{4,.1f,0},{},error);
    for(int frame=0;frame<20;++frame) { PaceNavigationWorkerFrame(world); world.Update(1.0/60.0); }
    const auto result=world.CaptureAgent(owner);
    if(!Expect(result && deadline.IsValid() && result->request.serial==deadline.serial &&
        result->state==VansNavigationMoveState::Finished && result->result==VansNavigationMoveResult::Failed &&
        result->path.failure==VansNavigationPathFailure::QueryDeadlineExceeded && !result->recoveryCount,
        "Unchanged data wait missed the original deadline or became Stuck/Partial")) return false;
    std::cout<<"[NavigationDataWait] PASS: 240 held frames, same move identity, zero motor intent/recovery, resumed Corridor and unchanged deadline\n";
    return true;
}

bool CheckNavigationProgressWindow()
{
    using namespace Vans;
    VansNavigationSettings settings; VansNavigationGeometry geometry; std::string error;
    VansSceneObjectBuildPlan plan;
    AppendEnvironmentBox(plan,"ProgressFloor",{0,-.1f,0},{8,.1f,8});
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    auto map=std::make_shared<VansNavigationMap>();
    if(!Expect(CollectContractGeometry(plan,settings.areas,{},geometry,error) &&
        BuildContractNavigation(geometry,settings,"World","Scenes/ProgressContract.json",asset,error) &&
        map->Initialize(asset,settings,"Humanoid",error),error)) return false;
    VansNavigationWorld world; VansRuntimeWorld runtime;
    const auto entity=runtime.CreateEntity({VansAssetGuid::FromStableName("NavigationProgress","Motor").ToString(),"ProgressMotor",{},true});
    auto motor=std::make_shared<SampledNavigationContractMotor>();
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error) &&
        world.RegisterAgents({{entity,{},map,motor}},error),error)) return false;
    const auto tick=[&](double dt){PaceNavigationWorkerFrame(world); world.Update(dt);};
    const auto awaitFollow=[&]()
    {
        for(int frame=0;frame<120;++frame)
        {
            tick(.05);
            const auto state=world.CaptureAgent(entity);
            if(state->state==VansNavigationMoveState::Following && state->desiredSpeed>0) return true;
            if(state->state==VansNavigationMoveState::Finished) return false;
        }
        return false;
    };
    // 侧向摆动和沿路径来回摆动都不能无限刷新停滞时钟。
    for(const int axis:{0,2})
    {
        motor->sample.position={-4,.1f,0};
        const std::string owner="Progress.Oscillation."+std::to_string(axis);
        const auto oscillating=world.RequestMove(entity,owner,{4,0,0},{},error);
        if(!Expect(oscillating.IsValid() && awaitFollow(),"Oscillation fixture could not start a real navigation path")) return false;
        for(int frame=0;frame<420 && world.CaptureAgent(entity)->state!=VansNavigationMoveState::Finished;++frame)
        {
            motor->sample.position[axis]=(axis==0 ? -4.f:0.f)+(frame%2 ? -.15f:.15f); tick(.05);
            if(!Expect(world.CaptureAgent(entity)->recoveryCount<=4 &&
                world.CaptureQueryStats().iterationsLastFrame<=settings.query.maximumIterationsPerFrame,
                "Oscillation exceeded bounded recovery or query capacity")) return false;
        }
        const auto ended=world.CaptureAgent(entity);
        if(!Expect(ended->result==VansNavigationMoveResult::Stuck && ended->recoveryCount==4 && ended->desiredSpeed==0,
            "Oscillation kept a blocked request alive without net Corridor progress")) return false;
        for(int repeat=0;repeat<32;++repeat)
            if(!Expect(world.RequestMove(entity,owner,{4,0,0},{},error).serial==oscillating.serial &&
                world.CaptureAgent(entity)->state==VansNavigationMoveState::Finished,
                "An unchanged Stuck request restarted its recovery loop")) return false;
    }

    motor->sample.position={-4,.1f,0};
    const auto waiting=world.RequestMove(entity,"Progress.Wait",{4,0,0},{},error);
    if(!Expect(waiting.IsValid() && awaitFollow(),"Legal waiting fixture did not begin following")) return false;
    for(int frame=0;frame<5;++frame) tick(.1);
    if(!Expect(world.Pause(waiting,"Progress.Wait",error),error)) return false;
    for(int frame=0;frame<100;++frame) tick(.1);
    if(!Expect(world.CaptureAgent(entity)->state==VansNavigationMoveState::Paused &&
        world.CaptureAgent(entity)->recoveryCount==0 && world.Resume(waiting,"Progress.Wait",error),
        "Pause accumulated a blocked-movement recovery")) return false;
    tick(.1); motor->sample.movementBlocked=true;
    for(int frame=0;frame<100;++frame) tick(.1);
    if(!Expect(world.CaptureAgent(entity)->recoveryCount==0 && world.CaptureAgent(entity)->desiredSpeed==0,
        "A motor movement lock accumulated blocked-movement time")) return false;
    motor->sample.movementBlocked=false; tick(.1);
    for(int frame=0;frame<12;++frame)
    {
        motor->sample.position+=motor->motion.direction*.05f; tick(.1);
        if(!Expect(world.CaptureAgent(entity)->recoveryCount==0 &&
            world.CaptureAgent(entity)->state==VansNavigationMoveState::Following,
            "Real progress after legal waiting triggered an obsolete stalled window")) return false;
    }
    VansRuntimeNavigationAgentComponent slow; slow.maxSpeed=.005f;
    motor->sample.position={-4,.1f,0};
    if(!Expect(world.RegisterAgents({{entity,slow,map,motor}},error) &&
        world.RequestMove(entity,"Progress.Slow",{4,0,0},{},error).IsValid() && awaitFollow(),error)) return false;
    for(int frame=0;frame<100;++frame)
    {
        motor->sample.position+=motor->motion.direction*motor->motion.desiredSpeed*.1f; tick(.1);
        if(!Expect(world.CaptureAgent(entity)->recoveryCount==0 &&
            world.CaptureAgent(entity)->state==VansNavigationMoveState::Following,
            "A slow configured motor made real progress but was declared stuck")) return false;
    }
    if(!Expect(motor->sample.position.x>-3.96f,"Slow motor did not make measurable actual progress")) return false;
    for(int frame=0;frame<420 && world.CaptureAgent(entity)->state!=VansNavigationMoveState::Finished;++frame) tick(.05);
    if(!Expect(world.CaptureAgent(entity)->result==VansNavigationMoveResult::Stuck &&
        world.CaptureAgent(entity)->recoveryCount==4,"A blocked low-speed motor bypassed the recovery bound")) return false;
    std::cout<<"[NavigationProgress] PASS: lateral/forward oscillation terminates, unchanged Stuck is bounded, Pause/lock time excluded, low-speed progress and low-speed blockage distinguished\n";
    return true;
}

bool CheckUnifiedFollowBudget(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& settings)
{
    using namespace Vans;
    auto limited=settings; limited.query.maximumIterationsPerFrame=1; limited.query.maximumIterationsPerSlice=1;
    VansNavigationWorld world; VansRuntimeWorld runtime; std::string error;
    if(!Expect(world.Initialize(limited,error) && world.RegisterMaps({map},error),error)) return false;
    std::array<VansEntityHandle,3> owners;
    std::array<std::shared_ptr<BudgetNavigationContractMotor>,3> motors;
    std::vector<VansNavigationAgentRegistration> registrations;
    for(int index=0;index<3;++index)
    {
        owners[index]=runtime.CreateEntity({VansAssetGuid::FromStableName("FollowBudget",std::to_string(index)).ToString(),"BudgetAgent",{},true});
        motors[index]=std::make_shared<BudgetNavigationContractMotor>();
        registrations.push_back({owners[index],{},map,motors[index]});
    }
    if(!Expect(world.RegisterAgents(registrations,error),error)) return false;
    for(const auto owner:owners)
        if(!Expect(world.RequestMove(owner,"Budget.Move",{4,0,0},{},error).IsValid(),error)) return false;
    VansNavigationQueryRequest request; request.start={-4,0,0}; request.goal={4,0,0};
    const auto background=world.RequestQuery(request,"Budget.Background",error);
    if(!Expect(background.IsValid(),error)) return false;
    std::array<bool,3> followed{}; bool searched=false;
    for(int frame=0;frame<120;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0);
        const auto stats=world.CaptureQueryStats();
        if(!Expect(stats.iterationsLastFrame<=1 && stats.startsLastFrame<=limited.query.maximumPathStartsPerFrame,
            "Worker and Corridor local work exceeded the one-node frame budget")) return false;
        const auto result=world.CaptureQuery(background,"Budget.Background",error);
        searched=result && result->lifecycle==VansNavigationQueryLifecycle::Finished && result->path.status==VansNavigationPathStatus::Complete;
        for(int index=0;index<3;++index)
        {
            const auto agent=world.CaptureAgent(owners[index]);
            if(!Expect(agent && agent->recoveryCount==0 && agent->state!=VansNavigationMoveState::Finished,
                "Budget deferral caused a false recovery or terminal movement failure")) return false;
            followed[index]=followed[index] || motors[index]->motion.desiredSpeed>0;
        }
        if(searched && std::all_of(followed.begin(),followed.end(),[](bool value){return value;})) break;
    }
    if(!Expect(searched && std::all_of(followed.begin(),followed.end(),[](bool value){return value;}),
        "One-node frames starved a worker search or one of the round-robin motor Corridors")) return false;
    const auto targetHandle=world.CaptureAgent(owners[0])->request;
    if(!Expect(world.UpdateGoal(targetHandle,"Budget.Move",{4,0,.2f},{},error),error)) return false;
    bool targetRepaired=false,heldForFollow=false;
    for(int frame=0;frame<60;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0);
        const auto agent=world.CaptureAgent(owners[0]);
        if(!Expect(agent && agent->state==VansNavigationMoveState::Following && agent->recoveryCount==0 &&
            world.CaptureQueryStats().iterationsLastFrame<=1 && world.CaptureQueryStats().startsLastFrame==0,
            "One-node target repair replanned, consumed a path start or exceeded the frame budget")) return false;
        const bool current=std::abs(agent->path.requestedEnd.z-.2f)<1.e-5f;
        if(!Expect(current || agent->desiredSpeed==0,"Deferred target work drove the obsolete endpoint")) return false;
        heldForFollow=heldForFollow || (current && agent->desiredSpeed==0);
        targetRepaired=current && agent->desiredSpeed>0;
        if(targetRepaired) break;
    }
    if(!Expect(targetRepaired && heldForFollow,"Small target repair did not finish and defer its separate motor-position quantum")) return false;
    std::cout<<"[NavigationFollowBudget] PASS: one-node frames, real Detour workers, three motor Corridors, target repair, fairness and zero false recovery\n";
    return true;
}

bool CheckMultipleProfiles()
{
	using namespace Vans;
	VansSceneObjectBuildPlan plan;
	AppendEnvironmentBox(plan, "ProfileStartRoom", { -4.0f, -0.1f, 0.0f }, { 2.5f, 0.1f, 2.5f });
	AppendEnvironmentBox(plan, "ProfileEndRoom", { 4.0f, -0.1f, 0.0f }, { 2.5f, 0.1f, 2.5f });
	AppendEnvironmentBox(plan, "ProfileNarrowPassage", { 0.0f, -0.1f, 0.0f }, { 2.0f, 0.1f, 0.65f });
	VansNavigationSettings settings;
	settings.profiles.push_back({ "Large", 2.4f, 0.8f, 0.25f, 40.0f });
	settings.maps.front().profiles.push_back("Large");
	VansNavigationGeometry geometry;
	std::string error, bytes;
	std::shared_ptr<const VansNavigationMeshAsset> asset, decoded;
	if (!Expect(CollectContractGeometry(plan, settings.areas, {}, geometry, error) &&
		BuildContractNavigation(geometry, settings, "World", "Scenes/MultiProfile.json", asset, error) &&
		VansNavigationMeshCodec::Encode(*asset, bytes, error) && VansNavigationMeshCodec::Decode(bytes, decoded, error), error)) return false;
	if (!Expect(decoded->GetMapId() == "World" && decoded->GetProfiles().size() == 2u &&
		decoded->FindProfile("Humanoid") && decoded->FindProfile("Large") && !decoded->FindProfile("Missing"),
		"Multi-Profile asset lost stable Map/Profile identities")) return false;
	auto humanoidMap = std::make_shared<VansNavigationMap>(), largeMap = std::make_shared<VansNavigationMap>();
	if (!Expect(humanoidMap->Initialize(decoded, settings, "Humanoid", error) && largeMap->Initialize(decoded, settings, "Large", error), error)) return false;
	VansNavigationQueryContext smallQuery, largeQuery;
	if (!Expect(smallQuery.Initialize(humanoidMap, settings.query, "Default", error) &&
		largeQuery.Initialize(largeMap, settings.query, "Default", error), error)) return false;
	for (int i = 0; i < 16; ++i)
	{
		const auto smallPath = smallQuery.FindPath({ -4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f });
		const auto largePath = largeQuery.FindPath({ -4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f });
		if (!Expect(smallPath.status == VansNavigationPathStatus::Complete && largePath.status == VansNavigationPathStatus::Partial &&
			!largePath.points.empty() && largePath.points.back().x < 0.0f && humanoidMap->GetAsset() == largeMap->GetAsset(),
			"Profile clearance leaked across runtime Maps or connected an impassable narrow passage")) return false;
	}
	auto changed = settings;
	changed.profiles.back().radiusMeters = 0.9f;
	if (!Expect(!largeMap->Initialize(decoded, changed, "Large", error) && error.find("Stale") != std::string::npos && largeMap->IsReady() &&
		!humanoidMap->Initialize(decoded, settings, "Missing", error) && humanoidMap->GetProfileId() == "Humanoid",
		"Changed or missing Profile replaced a valid native Map")) return false;
	changed = settings;
	changed.maximumResidentTiles = 1;
	const auto preserved = asset;
	if (!Expect(!BuildContractNavigation(geometry, changed, "World", "Scenes/MultiProfile.json", asset, error) && asset == preserved,
		"Aggregate multi-Profile Tile capacity was ignored or published incomplete output")) return false;
	VansNavigationSettings boundedWorldSettings = settings;
	boundedWorldSettings.query.maximumActiveAgents = 4;
	boundedWorldSettings.query.maximumSearchNodes = 4;
	boundedWorldSettings.query.maximumPathStartsPerFrame = 4;
	boundedWorldSettings.maximumResidentBytes = static_cast<int>((std::max)(std::uint64_t(1024 * 1024),
		static_cast<std::uint64_t>(humanoidMap->EstimateResidentBytes() + decoded->GetPayloadBytes()) +
		EstimateNavigationQueryBytes(boundedWorldSettings.query) + EstimateNavigationAvoidanceBytes(boundedWorldSettings.query.maximumActiveAgents,boundedWorldSettings.avoidance) + 4096u));
	VansNavigationWorld boundedWorld;
	VansRuntimeWorld runtime;
	std::vector<VansNavigationAgentRegistration> registrations;
	for (int i = 0; i < 4; ++i)
	{
		VansEntityCreateDesc desc;
		desc.name = "CapacityAgent" + std::to_string(i);
		desc.stableGuid = VansAssetGuid::FromStableName("NavigationContract", desc.name).ToString();
		const auto entity = runtime.CreateEntity(desc);
		auto independentMap = std::make_shared<VansNavigationMap>();
		if (!Expect(independentMap->Initialize(decoded, settings, "Humanoid", error), error)) return false;
		registrations.push_back({ entity, {}, independentMap, std::make_shared<StationaryNavigationContractMotor>() });
	}
	if (!Expect(boundedWorld.Initialize(boundedWorldSettings, error) &&
		boundedWorld.RegisterAgents({ registrations.front() }, error), error)) return false;
	const auto firstAgent = registrations.front().entity;
	const auto activeRequest = boundedWorld.RequestMove(firstAgent, "capacity-contract", { 4.0f, 0.0f, 0.0f }, {}, error);
	registrations.erase(registrations.begin());
	if (!Expect(activeRequest.IsValid() && !boundedWorld.RegisterAgents(registrations, error) &&
		error.find("capacity") != std::string::npos && boundedWorld.HasAgent(firstAgent) &&
		!boundedWorld.HasAgent(registrations.front().entity) &&
		boundedWorld.CaptureAgent(firstAgent)->request.serial == activeRequest.serial,
		"World aggregate capacity failure published a partial batch or cancelled the valid request")) return false;
	std::cout << "[NavigationProfiles] PASS: shared immutable asset, independent narrow-passage topology, strict references and aggregate rollback\n";
	return true;
}

bool CheckDecisionOnlyAIAndLuaBindings()
{
	using namespace Vans;
	VANS_INIT_MAIN_THREAD();
	VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	std::string error;
	VansRuntimeWorld runtime;
	VansNavigationWorld navigation;
	if (!Expect(navigation.Initialize({}, error), error)) return false;
	VansEntityCreateDesc desc;
	desc.name = "DecisionOnlyAI";
	desc.stableGuid = VansAssetGuid::FromStableName("NavigationContract", desc.name).ToString();
	const auto entity = runtime.CreateEntity(desc);
	const auto transformId = VansTransformStore::Allocate();
	VansRuntimePhaseGuard transformCleanup([&]() { VansTransformStore::Release(transformId); });
	VansTransform transform;
	transform.m_Position = {};
	transform.m_Rotation = {};
	transform.m_Scale = glm::vec3(1.0f);
	VansTransformStore::Write(transformId, transform);
	runtime.GetOrRegisterStorage<VansRuntimeTransformComponent>(VansRuntimeComponentType_Transform)->Add(
		entity, { transformId }, "DecisionOnly.Transform");
	auto behavior = std::make_shared<VansAIBehaviorAsset>();
	behavior->name = "DecisionOnly";
	behavior->bindings = { "Activate", "Release", "Target" };
	behavior->blackboard = {
		{ "Activate", VansAIValueType::Bool, false },
		{ "Release", VansAIValueType::Bool, false },
		{ "Target", VansAIValueType::Entity, VansEntityHandle{} }
	};
	behavior->initialState = "Hold";
	behavior->states.push_back({ "Hold", VansAITaskKind::Hold, std::nullopt, {} });
	VansAssetObjectRepository assets;
	const auto guid = VansAssetGuid::FromStableName("NavigationContract", "DecisionOnlyBehavior");
	if (!Expect(assets.Publish<VansAIBehaviorAsset>(guid, VansAssetType::AIBehavior, 1u, behavior, {}, error).IsValid(), error)) return false;
	VansRuntimeAIAgentComponent config;
	config.behaviorGuid = guid.ToString();
	config.sight.enabled = false;
	runtime.GetOrRegisterStorage<VansRuntimeAIAgentComponent>(VansRuntimeComponentType_AIAgent)->Add(entity, config, "DecisionOnly.AI");
	VansAIWorld ai;
	if (!Expect(ai.Initialize(runtime, nullptr, assets, navigation, error), error)) return false;
	ai.Update(1.0 / 60.0);
	const auto snapshot = ai.CaptureDiagnostics();
	if (!Expect(snapshot.totalAgents == 1u && snapshot.agents.front().initialized &&
		snapshot.agents.front().currentState == "Hold" && !navigation.HasAgent(entity),
		"Decision-only AI was forced to create a navigation driver or motor")) return false;
	ai.Shutdown();
	VansScriptContext scripts;
	scripts.VansScriptSetup();
	const char* contract = R"lua(
local nav = vans.navigation
for _, name in ipairs({"request_move", "update_goal", "stop", "pause", "resume", "get_state", "request_path",
    "project_point", "nav_raycast", "nearest_boundary", "sample_reachable_point", "get_query_state", "cancel_query", "release_query",
    "subscribe", "unsubscribe", "begin_traversal", "complete_traversal", "abort_traversal"}) do
    assert(type(nav[name]) == "function", name)
end
local request, error = nav.request_move("unbound", {x=0,y=0,z=0})
assert(request == nil and error == "Scene navigation is not initialized")
assert(not pcall(nav.pause, {}))
assert(nav.requestMove == nil and nav.getState == nil)
)lua";
	if (!Expect(luaL_dostring(scripts.GetLuaState(), contract) == LUA_OK,
		"Lua navigation registration, error result, userdata validation or single API spelling failed")) return false;
	scripts.ShutdownLua();
	std::cout << "[NavigationWorld] PASS: decision-only AI without motor; Lua request/control registration and strict handles\n";
	return true;
}

// Scene Timeline 装配需要相机；该测试只使用其 CPU 镜头和姿势，不初始化 GPU。
class NavigationSceneCameraDevice final : public VansGraphics::VansGraphicsDevice
{
public:
    NavigationSceneCameraDevice() { m_RenderWidth = 1280; m_RenderHeight = 720; m_GraphicsAPI = VansGraphics::INVALIDE; }
    bool BeforeRendering() override { return false; }
    void Rendering() override { throw std::logic_error("Navigation CPU fixture cannot render"); }
    bool CanRecordCurrentFrame() const override { return false; }
    bool CurrentFrameRequiresSurfaceResize() const override { return false; }
    bool HasDeviceLost() const override { return false; }
    void Present() override { throw std::logic_error("Navigation CPU fixture cannot present"); }
    void AfterRendering() override {}
    void RetireAfterCurrentFrame(std::function<void()> destroy) override { destroy(); }
    bool OnWindowResize(std::uint32_t, std::uint32_t) override { return false; }
    void* GetNativeGraphicsDevice() override { return nullptr; }
    void* GetNativeCommandBuffer() override { return nullptr; }
};

void AddRuntimeCollision(Vans::VansRuntimeWorld& runtime,VansEngine::VansPhysicsNode& node,const std::string& name)
{
    using namespace Vans;
    VansEntityCreateDesc desc; desc.stableGuid=VansAssetGuid::FromStableName("NavigationRuntimeCollision",name).ToString(); desc.name=name;
    const auto owner=runtime.CreateEntity(desc);
    runtime.AddComponent(owner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&node},name+".Physics");
    runtime.AddComponent(owner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{node.GetTransformID()},name+".Transform");
}
bool BuildLiveContractNavigation(const Vans::VansRuntimeWorld& runtime,const Vans::VansNavigationSettings& settings,
    std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset,std::string& error)
{
    using namespace Vans;
    VansRuntimeNavigationSources provider; VansNavigationSourceSnapshot snapshot; std::uint64_t observation=0;
    VansNavigationBuildInput input;
    return provider.Capture(runtime,settings,{}, {},snapshot,observation,error) &&
        VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
        VansNavigationBuilder::Build(input,settings,"Scenes/RuntimeNavigationContract.json",nullptr,asset,error);
}
bool CheckAvoidanceCCT(VansEngine::VansPhysicsSystem& physics,const Vans::VansNavigationSettings& settings)
{
    using namespace Vans; using namespace VansEngine; std::string error;
    std::vector<std::uint32_t> transforms;
    VansRuntimePhaseGuard cleanup([&](){for(const auto transform:transforms) VansTransformStore::Release(transform);});
    const auto allocate=[&](const glm::vec3& position)
    {
        const auto id=VansTransformStore::Allocate(); transforms.push_back(id); VansTransform transform;
        transform.m_Position=position; transform.m_Rotation=glm::vec3(0); transform.m_Scale=glm::vec3(1); VansTransformStore::Write(id,transform); return id;
    };
    PhysicsNodeProperties box; box.enabled=true; box.layerName="Environment"; box.boxExtents={5,.1f,5};
    VansPhysicsNode floor; floor.Initialize(box,allocate({0,-.1f,100}));
    box.boxExtents={.5f,1.5f,2}; VansPhysicsNode wall; wall.Initialize(box,allocate({0,1.5f,100}));
    if(!Expect(floor.HasActor() && wall.HasActor(),"Avoidance CCT fixture has no physical floor/wall")) return false;
    CharControllerProperties properties; properties.m_Radius=.35f; properties.m_Height=1.1f; properties.m_StepOffset=.25f;
    properties.m_ContactOffset=.02f; properties.m_PositionOffset={0,.9f,0};
    VansCharacterControllerNode controller;
    if(!Expect(controller.Initialize(properties,allocate({-4,.02f,100}),{-4,.92f,100}),"Avoidance CCT creation failed")) return false;
    controller.SeedGroundedContact(true);
    VansRuntimeWorld runtime; const auto owner=runtime.CreateEntity({VansAssetGuid::New().ToString(),"AvoidanceCCT",{},true});
    runtime.AddComponent(owner,VansRuntimeComponentType_CharacterController,VansRuntimeCharacterControllerComponent{&controller},"Avoidance.CCT");
    VansRuntimeNavigationAgentComponent agent; agent.avoidance=true;
    runtime.AddComponent(owner,VansRuntimeComponentType_NavigationAgent,agent,"Avoidance.Agent");
    AddRuntimeCollision(runtime,floor,"Avoidance.Floor"); AddRuntimeCollision(runtime,wall,"Avoidance.Wall");
    const auto surfaceOwner=runtime.CreateEntity({VansAssetGuid::New().ToString(),"AvoidanceSurface",{},true});
    runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,100})},"Avoidance.SurfaceTransform");
    const auto meshGuid=VansAssetGuid::New(); auto surface=ContractSurface(settings).surface; surface.navigationMeshGuid=meshGuid.ToString();
    runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationSurface,surface,"Avoidance.Surface");
    std::shared_ptr<const VansNavigationMeshAsset> asset; VansAssetObjectRepository assets;
    if(!Expect(BuildLiveContractNavigation(runtime,settings,asset,error) &&
        assets.Publish<VansNavigationMeshAsset>(meshGuid,VansAssetType::NavigationMesh,1,asset,{},error).IsValid(),error)) return false;
    VansRuntimeNavigationSources provider; VansNavigationSourceSnapshot snapshot; std::uint64_t observation=0;
    VansSceneNavigationRuntime integration;
    if(!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error) && integration.Initialize(runtime,assets,settings,error) &&
        integration.Synchronize(std::move(snapshot),observation,error),error)) return false;
    auto& world=integration.World(); const auto request=world.RequestMove(owner,"Avoidance.CCT.Move",{4,0,100},{},error);
    if(!Expect(request.IsValid() && !runtime.FindStorage<VansRuntimeAIAgentComponent>(VansRuntimeComponentType_AIAgent),error)) return false;
    VansCharacterMotionSettings motion; motion.driveMode=VansLocomotionDriveMode::Capsule;
    bool sampled=false; float lateral=0; std::uint64_t samples=0;
    for(int frame=0;frame<1500;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1.0/60.0);
        const auto state=world.CaptureAgent(owner); sampled|=state->avoidance.samples>0; samples+=state->avoidance.samples;
        if(!Expect(world.CaptureQueryStats().iterationsLastFrame<=settings.query.maximumIterationsPerFrame && state->desiredSpeed<=agent.maxSpeed+1.e-4f,
            "Avoidance CCT escaped its query/speed constraints")) return false;
        controller.PrepareLocomotion(1.f/60,motion);
        controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motion,glm::vec3(1));
        {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex()); controller.FlushMoveAndSync();}
        lateral=(std::max)(lateral,std::abs(controller.GetPosition().z-100));
        if(state->state==VansNavigationMoveState::Finished) break;
    }
    const auto completed=world.CaptureAgent(owner);
    std::cout<<"[NavigationAvoidanceCCT] endpoint=("<<controller.GetPosition().x<<','<<controller.GetPosition().z<<") result="
        <<static_cast<int>(completed->result)<<" recovery="<<completed->recoveryCount<<" lateral="<<lateral<<" samples="<<samples
        <<" diagnostic="<<completed->path.diagnostic<<'\n';
    if(!Expect(sampled && completed->result==VansNavigationMoveResult::ReachedGoal && completed->recoveryCount==0 && lateral>2,
        "Enabled avoidance did not traverse the real CCT wall corridor")) return false;
    integration.Shutdown();
    std::cout<<"[NavigationAvoidanceCCT] PASS: real Physics/Surface/Source composition, native wall sampling, actual CCT feedback and sole locomotion integration\n";
    return true;
}
bool CheckMovingObstacleCCT(VansEngine::VansPhysicsSystem& physics,const Vans::VansNavigationSettings& settings)
{
    using namespace Vans; using namespace VansEngine; std::string error;
    for(const float step:{1.f/30.f,1.f/60.f,1.f/120.f})
    {
        constexpr float anchorZ=140;
        std::vector<std::uint32_t> transforms;
        VansRuntimePhaseGuard cleanup([&](){for(const auto id:transforms) VansTransformStore::Release(id);});
        const auto allocate=[&](const glm::vec3& position)
        {
            const auto id=VansTransformStore::Allocate(); transforms.push_back(id);
            VansTransform pose; pose.m_Position=position; pose.m_Rotation=glm::vec3(0); pose.m_Scale=glm::vec3(1);
            VansTransformStore::Write(id,pose); return id;
        };
        PhysicsNodeProperties properties; properties.enabled=true; properties.layerName="Environment"; properties.boxExtents={5,.1f,5};
        VansPhysicsNode floor; floor.Initialize(properties,allocate({0,-.1f,anchorZ}));
        properties.bodyType=PhysicsBodyType::Kinematic; properties.boxExtents={.4f,1,.75f}; properties.canCharacterStepUp=false;
        const auto boxTransform=allocate({-.5f,1,anchorZ-1.5f});
        auto boxPose=VansTransformStore::Read(boxTransform); boxPose.m_Rotation.y=20; VansTransformStore::Write(boxTransform,boxPose);
        VansPhysicsNode movingBox; movingBox.Initialize(properties,boxTransform);
        if(!Expect(floor.HasActor() && movingBox.HasActor(),"Moving obstacle fixture has no actual colliders")) return false;
        CharControllerProperties capsule; capsule.m_Radius=.35f; capsule.m_Height=1.1f; capsule.m_StepOffset=.25f;
        capsule.m_ContactOffset=.02f; capsule.m_PositionOffset={0,.9f,0};
        VansCharacterControllerNode controller;
        if(!Expect(controller.Initialize(capsule,allocate({-4,.02f,anchorZ}),{-4,.92f,anchorZ}),"Moving obstacle CCT initialization failed")) return false;
        controller.SeedGroundedContact(true);
        VansRuntimeWorld runtime;
        const auto owner=runtime.CreateEntity({VansAssetGuid::New().ToString(),"MovingObstacleAgent",{},true});
        runtime.AddComponent(owner,VansRuntimeComponentType_CharacterController,VansRuntimeCharacterControllerComponent{&controller},"Moving.CCT");
        VansRuntimeNavigationAgentComponent agent; agent.avoidance=true;
        runtime.AddComponent(owner,VansRuntimeComponentType_NavigationAgent,agent,"Moving.Agent");
        AddRuntimeCollision(runtime,floor,"Moving.Floor");
        const auto boxGuid=VansAssetGuid::New().ToString(),physicsGuid=VansAssetGuid::New().ToString();
        const auto boxOwner=runtime.CreateEntity({boxGuid,"MovingBox",{},true});
        runtime.AddComponent(boxOwner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&movingBox},physicsGuid);
        runtime.AddComponent(boxOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{boxTransform},"Moving.BoxTransform");
        VansRuntimeNavigationObstacleComponent obstacle; obstacle.mode=VansNavigationObstacleMode::Avoidance;
        obstacle.shape=VansNavigationObstacleCollider{boxGuid,physicsGuid};
        runtime.AddComponent(boxOwner,VansRuntimeComponentType_NavigationObstacle,obstacle,"Moving.BoxObstacle");
        const auto surfaceOwner=runtime.CreateEntity({VansAssetGuid::New().ToString(),"MovingSurface",{},true});
        runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,anchorZ})},"Moving.SurfaceTransform");
        const auto meshGuid=VansAssetGuid::New(); auto surface=ContractSurface(settings).surface; surface.navigationMeshGuid=meshGuid.ToString();
        runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationSurface,surface,"Moving.Surface");
        std::shared_ptr<const VansNavigationMeshAsset> asset; VansAssetObjectRepository assets;
        if(!Expect(BuildLiveContractNavigation(runtime,settings,asset,error) &&
            assets.Publish<VansNavigationMeshAsset>(meshGuid,VansAssetType::NavigationMesh,1,asset,{},error).IsValid(),error)) return false;
        VansRuntimeNavigationSources provider; VansSceneNavigationRuntime integration; VansNavigationSourceSnapshot snapshot;
        std::uint64_t observation=0;
        if(!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error) && integration.Initialize(runtime,assets,settings,error) &&
            integration.Synchronize(std::move(snapshot),observation,error),error)) return false;
        const auto initialObservation=observation;
        auto& world=integration.World(); const auto request=world.RequestMove(owner,"Moving.Box.CCT",{4,0,anchorZ},{},error);
        if(!Expect(request.IsValid(),error)) return false;
        VansCharacterMotionSettings motion; motion.driveMode=VansLocomotionDriveMode::Capsule;
        float elapsed=0,lateral=0,maximumPenetration=0; std::uint64_t samples=0; bool sampledObstacle=false;
        for(int frame=0;elapsed<20;++frame)
        {
            elapsed+=step; boxPose.m_Position.z=anchorZ+(std::min)(1.5f,-1.5f+.8f*elapsed);
            VansTransformStore::Write(boxTransform,boxPose); movingBox.SyncActorFromTransform();
            {
                std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
                auto* scene=VansPhysicsNativeAccess::Scene(physics); scene->simulate(step); scene->fetchResults(true);
            }
            if(!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error) &&
                observation==initialObservation && integration.Synchronize(std::move(snapshot),observation,error) &&
                world.GetCarveObstacleRevision()==0,"Pure moving avoidance changed Baked geometry or failed composition: "+error)) return false;
            PaceNavigationWorkerFrame(world); world.Update(step);
            const auto state=world.CaptureAgent(owner); samples+=state->avoidance.samples;
            sampledObstacle|=state->avoidance.obstacles==1 && state->avoidance.samples>0;
            if(!Expect(world.CaptureQueryStats().iterationsLastFrame<=settings.query.maximumIterationsPerFrame &&
                state->desiredSpeed<=agent.maxSpeed+1.e-4f,"Moving CCT exceeded query or speed budget")) return false;
            controller.PrepareLocomotion(step,motion); controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motion,glm::vec3(1));
            {
                std::lock_guard<std::mutex> lock(physics.GetSimulationMutex()); controller.FlushMoveAndSync();
                const auto* actor=static_cast<const physx::PxRigidActor*>(movingBox.GetActorIdentity()); physx::PxShape* shape=nullptr;
                if(!Expect(actor && actor->getShapes(&shape,1)==1,"Moving Box lost its authoritative native shape")) return false;
                const auto position=controller.GetPosition();
                const physx::PxCapsuleGeometry geometry(capsule.m_Radius,capsule.m_Height*.5f);
                const physx::PxTransform pose(physx::PxVec3(position.x,position.y,position.z),physx::PxQuat(physx::PxHalfPi,physx::PxVec3(0,0,1)));
                physx::PxVec3 direction; float depth=0;
                if(physx::PxGeometryQuery::computePenetration(direction,depth,geometry,pose,shape->getGeometry(),actor->getGlobalPose()*shape->getLocalPose()))
                    maximumPenetration=(std::max)(maximumPenetration,depth);
            }
            lateral=(std::max)(lateral,std::abs(controller.GetPosition().z-anchorZ));
            if(state->state==VansNavigationMoveState::Finished) break;
        }
        const auto completed=world.CaptureAgent(owner);
        std::cout<<"[NavigationMovingObstacleCCT] dt="<<step<<" end=("<<controller.GetPosition().x<<','<<controller.GetPosition().z
            <<") result="<<static_cast<int>(completed->result)<<" recovery="<<completed->recoveryCount<<" lateral="<<lateral
            <<" penetration="<<maximumPenetration<<" samples="<<samples<<'\n';
        if(!Expect(sampledObstacle && completed->result==VansNavigationMoveResult::ReachedGoal && !completed->recoveryCount &&
            lateral>.15f && maximumPenetration<=.005f,"Real moving collider avoidance failed to arrive or penetrated its actual Box")) return false;
        integration.Shutdown();
    }
    std::cout<<"[NavigationMovingObstacleCCT] PASS: actual kinematic rotation/translation, typed Collider, Baked source stability, native capsule penetration oracle and 30/60/120 Hz arrival\n";
    return true;
}
bool CheckNarrowPassageCCT(VansEngine::VansPhysicsSystem& physics,const Vans::VansNavigationSettings& sourceSettings)
{
    using namespace Vans; using namespace VansEngine; std::string error;
    for(const int scenario:{0,1,2,3})
    for(const float step:{1.f/30.f,1.f/60.f,1.f/120.f})
    {
        const bool reverse=scenario==1,timeout=scenario==2,mixedProfile=scenario==3;
        if((timeout || mixedProfile) && step!=1.f/60.f) continue;
        auto settings=sourceSettings; if(timeout) settings.avoidance.maximumYieldSeconds=.1f;
        if(mixedProfile)
        {
            auto alternate=settings.profiles.front(); alternate.id="Alternate";
            settings.profiles.push_back(alternate); settings.maps.front().profiles.push_back(alternate.id);
        }
        constexpr float anchorZ=180;
        std::vector<std::uint32_t> transforms;
        VansRuntimePhaseGuard cleanup([&](){for(const auto id:transforms) VansTransformStore::Release(id);});
        const auto allocate=[&](const glm::vec3& position)
        {
            const auto id=VansTransformStore::Allocate(); transforms.push_back(id);
            VansTransform pose; pose.m_Position=position; pose.m_Rotation=glm::vec3(0); pose.m_Scale=glm::vec3(1);
            VansTransformStore::Write(id,pose); return id;
        };
        PhysicsNodeProperties properties; properties.enabled=true; properties.layerName="Environment"; properties.boxExtents={6,.1f,3};
        VansPhysicsNode floor; floor.Initialize(properties,allocate({0,-.1f,anchorZ}));
        properties.boxExtents={2,1.5f,1.175f}; properties.canCharacterStepUp=false;
        VansPhysicsNode upperWall,lowerWall;
        upperWall.Initialize(properties,allocate({0,1.5f,anchorZ+1.825f}));
        lowerWall.Initialize(properties,allocate({0,1.5f,anchorZ-1.825f}));
        if(!Expect(floor.HasActor() && upperWall.HasActor() && lowerWall.HasActor(),"Narrow passage lost physical geometry")) return false;
        CharControllerProperties capsule; capsule.m_Radius=.35f; capsule.m_Height=1.1f; capsule.m_StepOffset=.25f;
        capsule.m_ContactOffset=.02f; capsule.m_PositionOffset={0,.9f,0};
        std::array<VansCharacterControllerNode,2> controllers;
        VansRuntimeWorld runtime; std::array<VansEntityHandle,2> owners;
        VansRuntimeNavigationAgentComponent agent; agent.avoidance=true;
        for(std::size_t i=0;i<controllers.size();++i)
        {
            const float start=(i==0)!=reverse?-4.f:4.f;
            if(!Expect(controllers[i].Initialize(capsule,allocate({start,.02f,anchorZ}),{start,.92f,anchorZ}),"Narrow CCT initialization failed")) return false;
            controllers[i].SeedGroundedContact(true);
            owners[i]=runtime.CreateEntity({VansAssetGuid::New().ToString(),"NarrowAgent"+std::to_string(i),{},true});
            runtime.AddComponent(owners[i],VansRuntimeComponentType_CharacterController,VansRuntimeCharacterControllerComponent{&controllers[i]},"Narrow.CCT"+std::to_string(i));
            auto configuration=agent; if(mixedProfile && i==1) configuration.profileId="Alternate";
            runtime.AddComponent(owners[i],VansRuntimeComponentType_NavigationAgent,configuration,"Narrow.Agent"+std::to_string(i));
        }
        AddRuntimeCollision(runtime,floor,"Narrow.Floor"); AddRuntimeCollision(runtime,upperWall,"Narrow.Upper"); AddRuntimeCollision(runtime,lowerWall,"Narrow.Lower");
        const auto surfaceOwner=runtime.CreateEntity({VansAssetGuid::New().ToString(),"NarrowSurface",{},true});
        runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,anchorZ})},"Narrow.SurfaceTransform");
        const auto meshGuid=VansAssetGuid::New(); auto surface=ContractSurface(settings).surface; surface.navigationMeshGuid=meshGuid.ToString();
        runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationSurface,surface,"Narrow.Surface");
        std::shared_ptr<const VansNavigationMeshAsset> asset; VansAssetObjectRepository assets;
        if(!Expect(BuildLiveContractNavigation(runtime,settings,asset,error) &&
            assets.Publish<VansNavigationMeshAsset>(meshGuid,VansAssetType::NavigationMesh,1,asset,{},error).IsValid(),error)) return false;
        VansRuntimeNavigationSources provider; VansSceneNavigationRuntime integration; VansNavigationSourceSnapshot snapshot; std::uint64_t observation=0;
        if(!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error) && integration.Initialize(runtime,assets,settings,error) &&
            integration.Synchronize(std::move(snapshot),observation,error),error)) return false;
        auto& world=integration.World();
        std::array<VansNavigationRequestHandle,2> requests;
        for(std::size_t i=0;i<owners.size();++i)
        {
            requests[i]=world.RequestMove(owners[i],"Narrow.Move",{(i==0)!=reverse?4.f:-4.f,0,anchorZ},{},error);
            if(!Expect(requests[i].IsValid(),error)) return false;
        }
        VansCharacterMotionSettings motion; motion.driveMode=VansLocomotionDriveMode::Capsule;
        float elapsed=0,maximumPenetration=0; std::uint64_t samples=0; std::array<int,2> yieldingFrames{};
        bool paused=false,resumed=false; float pauseEnd=0,pausedSeconds=0;
        for(int frame=0;elapsed<30;++frame)
        {
            elapsed+=step;
            {
                std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
                auto* scene=VansPhysicsNativeAccess::Scene(physics); scene->simulate(step); scene->fetchResults(true);
            }
            if(paused && !resumed && elapsed>=pauseEnd)
            { if(!Expect(world.Resume(requests[1],"Narrow.Move",error),error)) return false; resumed=true; }
            PaceNavigationWorkerFrame(world); world.Update(step);
            const auto yielding=world.CaptureAgent(owners[1]);
            if(reverse && step>1.f/61.f && step<1.f/59.f && !paused && yielding->avoidance.yield.peer)
            {
                pausedSeconds=yielding->avoidance.yield.seconds; pauseEnd=elapsed+.25f;
                if(!Expect(world.Pause(requests[1],"Narrow.Move",error),error)) return false; paused=true;
            }
            bool finished=true;
            for(std::size_t i=0;i<owners.size();++i)
            {
                const auto state=world.CaptureAgent(owners[i]); samples+=state->avoidance.samples;
                if(state->avoidance.yield.peer) ++yieldingFrames[i];
                if(!Expect(state->avoidance.yield.seconds<settings.avoidance.maximumYieldSeconds,
                    "Published yield exceeded its configured time limit")) return false;
                if(i==1 && paused && !resumed && !Expect(state->state==VansNavigationMoveState::Paused &&
                    state->desiredSpeed==0 && state->avoidance.yield.seconds==pausedSeconds,"Paused yield moved or advanced its clock")) return false;
                finished&=state->state==VansNavigationMoveState::Finished;
                if(!Expect(world.CaptureQueryStats().iterationsLastFrame<=settings.query.maximumIterationsPerFrame &&
                    state->desiredSpeed<=agent.maxSpeed+1.e-4f,"Narrow CCT exceeded query or speed budget")) return false;
                controllers[i].PrepareLocomotion(step,motion);
                controllers[i].ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motion,glm::vec3(1));
            }
            {
                std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
                for(auto& controller:controllers) controller.FlushMoveAndSync();
                const auto a=controllers[0].GetPosition(),b=controllers[1].GetPosition();
                const physx::PxCapsuleGeometry geometry(capsule.m_Radius,capsule.m_Height*.5f);
                const physx::PxQuat rotation(physx::PxHalfPi,physx::PxVec3(0,0,1));
                physx::PxVec3 direction; float depth=0;
                if(physx::PxGeometryQuery::computePenetration(direction,depth,geometry,physx::PxTransform(physx::PxVec3(a.x,a.y,a.z),rotation),
                    geometry,physx::PxTransform(physx::PxVec3(b.x,b.y,b.z),rotation))) maximumPenetration=(std::max)(maximumPenetration,depth);
            }
            if(finished) break;
        }
        bool reached=true;
        for(std::size_t i=0;i<owners.size();++i)
        {
            const auto state=world.CaptureAgent(owners[i]); const auto position=controllers[i].GetPosition();
            std::cout<<"[NavigationNarrowCCT] reverse="<<reverse<<" timeout="<<timeout<<" mixed_profile="<<mixedProfile<<" dt="<<step<<" agent="<<i<<" end=("<<position.x<<','<<position.z<<") result="
                <<static_cast<int>(state->result)<<" recovery="<<state->recoveryCount<<" penetration="<<maximumPenetration<<" samples="<<samples
                <<" yielding_frames="<<yieldingFrames[i]<<" diagnostic="<<state->path.diagnostic<<'\n';
            reached&=state->result==VansNavigationMoveResult::ReachedGoal && state->recoveryCount==0 && !state->avoidance.yield.peer;
        }
        const auto yieldingResult=world.CaptureAgent(owners[1]);
        if(!Expect(maximumPenetration<=.005f,"Narrow CCT timeout/passing penetrated its physical peer")) return false;
        if(timeout)
        {
            if(!Expect(yieldingResult->result==VansNavigationMoveResult::Stuck && yieldingResult->recoveryCount==4 &&
                yieldingResult->path.diagnostic=="Narrow passage yield exceeded its bounded duration" &&
                !yieldingResult->avoidance.yield.peer && yieldingResult->desiredSpeed==0,"Yield timeout escaped its bounded recovery/cleanup")) return false;
        }
        else if(!Expect(reached && yieldingFrames[0]==0 && yieldingFrames[1]>0 && maximumPenetration<=.005f,
            "Opposing real CCTs did not pass with stable identity priority")) return false;
        if(reverse && step>1.f/61.f && step<1.f/59.f && !Expect(paused && resumed,"Real narrow-passage pause/resume was not exercised")) return false;
        integration.Shutdown();
    }
    std::cout<<"[NavigationNarrowCCT] PASS: actual two-way CCT passage, identity priority in both directions, cross-Profile collision, pause/resume, bounded timeout, zero penetration and 30/60/120 Hz arrival\n";
    return true;
}
bool CheckStandaloneLuaDriver(VansEngine::VansPhysicsNode& floor,VansEngine::VansPhysicsNode& wall,
    VansEngine::VansCharacterControllerNode& controller, VansEngine::VansPhysicsSystem& physics,
    const Vans::VansCharacterMotionSettings& motionSettings)
{
    using namespace Vans;
    using namespace VansGraphics;
    std::string error;
    auto& project = VansProjectManager::Get();
    bool openedFixture = false;
    VansRuntimePhaseGuard projectCleanup([&]() { if (openedFixture) project.CloseProject(); });
    if (!project.GetGAFProjectConfiguration())
    {
        auto workspace = std::filesystem::current_path();
        for (int depth = 0; depth < 8 && !std::filesystem::exists(workspace / "TestV2Project/ForestProject.json"); ++depth)
            workspace = workspace.parent_path();
        VansProjectOpenRequest open;
        open.m_ProjectRootPath = (workspace / "TestV2Project").generic_string();
        open.m_Options.m_UpdateRecentProjects = false;
        open.m_Options.m_AssetLoadMode = Vans::VansProjectAssetLoadMode::None;
        const auto result = project.OpenProject(open);
        if (!Expect(result.m_Opened, result.m_Message.c_str())) return false;
        openedFixture = true;
    }
    const auto guid = VansAssetGuid::FromStableName("NavigationContract", "StandaloneLuaMesh");
    auto& assets = project.GetAssetObjectRepository();
    if(!Expect(!assets.ResolveLatest<VansNavigationMeshAsset>(guid),"Lua fixture asset identity is already used")) return false;
    VansRuntimePhaseGuard assetCleanup([&]() { assets.Remove(guid); });
    NavigationSceneCameraDevice cameraDevice;
    VansCamera camera(&cameraDevice);
    VansScene scene;
    scene.InjectCamera(&camera);
    VansScriptContext scripts;
    scripts.VansScriptSetup();
    scripts.AttachSceneWithoutRebuild(&scene);
    VansRuntimePhaseGuard sceneCleanup([&]()
    {
        scripts.AttachSceneWithoutRebuild(nullptr);
        scene.UnloadScene(nullptr);
    });
    VansSceneObjectBuildPlan plan;
    VansSceneObjectBuildConfig object;
    object.entityGuid = VansAssetGuid::FromStableName("NavigationContract", "StandaloneLuaOwner").ToString();
    object.name = "StandaloneLuaNavigation";
    object.transform = VansSceneTransformConfig{};
    plan.objects.push_back(object);
    VkDevice device = VK_NULL_HANDLE;
    auto built = VansSceneAssembly::BuildObjects(scene, device, plan, "");
    if(built.m_Built) built=VansSceneAssembly::InitializeDomains(scene);
    if (!Expect(built.m_Built, built.m_Error)) return false;
    auto* runtime = scene.GetRuntimeWorld();
    const auto entity = runtime->Entities().FindByGuid(object.entityGuid);
    VansRuntimeNavigationAgentComponent config;
    VansEntityCreateDesc surfaceDesc; surfaceDesc.stableGuid=VansAssetGuid::FromStableName("NavigationContract","LuaSurfaceOwner").ToString();
    const auto surfaceOwner=runtime->CreateEntity(surfaceDesc);
    const auto surfaceTransform=VansTransformStore::Allocate();
    VansTransform surfacePose; surfacePose.m_Position=glm::vec3(0); surfacePose.m_Rotation=glm::vec3(0); surfacePose.m_Scale=glm::vec3(1);
    VansTransformStore::Write(surfaceTransform,surfacePose);
    VansRuntimePhaseGuard surfaceCleanup([&](){VansTransformStore::Release(surfaceTransform);});
    runtime->AddComponent(surfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{surfaceTransform},"LuaSurface.Transform");
    auto surfaceConfig=ContractSurface(project.GetProjectSettings().GetNavigationSettings()).surface;
    surfaceConfig.navigationMeshGuid=guid.ToString();
    const auto surfaceComponent=VansAssetGuid::FromStableName("NavigationContract","LuaSurface").ToString();
    runtime->AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationSurface,surfaceConfig,surfaceComponent);
    runtime->AddComponent(entity, VansRuntimeComponentType_NavigationAgent, config, "StandaloneLua.Navigation");
    runtime->AddComponent(entity, VansRuntimeComponentType_CharacterController,
        VansRuntimeCharacterControllerComponent{ &controller }, "StandaloneLua.CCT");
    AddRuntimeCollision(*runtime,floor,"Floor"); AddRuntimeCollision(*runtime,wall,"Wall");
    const auto sourceGuid=VansAssetGuid::FromStableName("NavigationContract","LuaFloorSource").ToString();
    const auto floorEntity=runtime->Entities().FindByGuid(VansAssetGuid::FromStableName("NavigationRuntimeCollision","Floor").ToString());
    runtime->AddComponent(floorEntity,VansRuntimeComponentType_NavigationSource,VansRuntimeNavigationSourceComponent{},sourceGuid);
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!Expect(BuildLiveContractNavigation(*runtime,project.GetProjectSettings().GetNavigationSettings(),asset,error) &&
        assets.Publish<VansNavigationMeshAsset>(guid,VansAssetType::NavigationMesh,1u,asset,{},error).IsValid(),error)) return false;
    scene.PrepareNavigation();
    auto* navigation = scene.GetNavigationWorld();
    if (!Expect(navigation && navigation->HasAgent(entity), "Lua fixture Scene did not register its independent navigation motor")) return false;
    {
        using namespace Vans::EditorAPI;
        EngineAPIImpl editor;
        editor.BindScriptContext(&scripts);
        editor.BindRuntime(&scene,nullptr);
        VansRuntimePhaseGuard editorCleanup([&](){ editor.BindRuntime(nullptr,nullptr); scripts.AttachSceneWithoutRebuild(&scene); });
        using Value=VansSerializedValue;
        auto agentData=VansComponentTypeCatalog::CreateDefaultData("NavigationAgent");
        SetSerializedObjectField(agentData,"maxSpeed",Value::Float(2.0));
        auto authoredAgent=Value::Object({{"id",Value::String(object.entityGuid)},{"name",Value::String(object.name)},
            {"active",Value::Bool(true)},{"components",Value::Array({Value::Object({
                {"id",Value::String("StandaloneLua.Navigation")},{"type",Value::String("NavigationAgent")},
                {"enabled",Value::Bool(true)},{"data",agentData}})})}});
        const auto agentPreview=BuildRuntimeEntityPreviewChange(authoredAgent);
        auto* agentStorage=runtime->FindStorage<VansRuntimeNavigationAgentComponent>(VansRuntimeComponentType_NavigationAgent);
        const auto agentHandle=agentStorage->FindFirstOwnedBy(entity);
        if(!Expect(agentPreview.navigationEdits.size()==1 && editor.ApplyRuntimeEntityPreviewChange(agentPreview) &&
            agentStorage->Get(agentHandle)->maxSpeed==2.f,"Inspector navigation data did not reach the actual runtime component")) return false;
        auto atomicFailure=agentPreview;
        SetSerializedObjectField(atomicFailure.navigationEdits.front().data,"maxSpeed",Value::Float(3.0));
        auto wrongOwner=atomicFailure.navigationEdits.front(); wrongOwner.entityGuid=surfaceDesc.stableGuid;
        atomicFailure.navigationEdits.push_back(std::move(wrongOwner));
        if(!Expect(!editor.ApplyRuntimeEntityPreviewChange(atomicFailure) && agentStorage->Get(agentHandle)->maxSpeed==2.f,
            "A later invalid navigation preview edit partially changed earlier payloads")) return false;
        SetSerializedObjectField(agentData,"maxSpeed",Value::Float(config.maxSpeed));
        auto restore=agentPreview; restore.navigationEdits.front().data=agentData;
        if(!Expect(editor.ApplyRuntimeEntityPreviewChange(restore) && agentStorage->Get(agentHandle)->maxSpeed==config.maxSpeed,
            "Navigation authoring preview could not restore the original Agent parameters")) return false;
        std::cout<<"[NavigationAuthoringProjection] PASS: Inspector producer, actual typed component consumer and atomic invalid-owner rejection\n";
        const auto schema=editor.GetNavigationAuthoringSchema();
        const auto obstacleShapes=std::find_if(schema.fields.begin(),schema.fields.end(),[](const auto& field)
            {return field.componentType=="NavigationObstacle" && field.field=="shape";});
        const auto colliderChoices=std::find_if(schema.fields.begin(),schema.fields.end(),[](const auto& field)
            {return field.componentType=="NavigationObstacle" && field.field=="collider";});
        const auto obstacleModes=std::find_if(schema.fields.begin(),schema.fields.end(),[](const auto& field)
            {return field.componentType=="NavigationObstacle" && field.field=="mode";});
        if(!Expect(obstacleModes!=schema.fields.end() && obstacleModes->choices.size()==kNavigationObstacleModes.size() &&
            !schema.obstacleCapabilities.empty(),"Inspector omitted obstacle modes or actual capacity policy")) return false;
        if(!Expect(obstacleShapes!=schema.fields.end() && obstacleShapes->choices.size()==3 &&
            std::find(obstacleShapes->replacedFields.begin(),obstacleShapes->replacedFields.end(),"collider")!=obstacleShapes->replacedFields.end() &&
            colliderChoices!=schema.fields.end(),"Inspector producer omitted current shape templates or Collider choices")) return false;
        for(const auto& choice:obstacleShapes->choices)
        {
            auto data=choice.componentFields;
            if(!Expect(data.kind==Value::Kind::Object && ReadSerializedStringField(data,"shape")==choice.value.stringValue,
                "Inspector choice did not publish its current schema fields")) return false;
            SetSerializedObjectField(data,"mapId",Value::String("World")); SetSerializedObjectField(data,"profiles",Value::Array({Value::String("Humanoid")}));
            SetSerializedObjectField(data,"mode",Value::String("Both"));
            if(choice.value.stringValue=="Collider") SetSerializedObjectField(data,"collider",MakeSerializedSceneComponentObjectReference(
                VansAssetGuid::New().ToString(),VansAssetGuid::New().ToString(),"Physics"));
            VansRuntimeNavigationObstacleComponent decoded;
            if(!Expect(VansSceneNavigationComponentReader::Read(data,decoded,error) && decoded.mode==VansNavigationObstacleMode::Both &&
                std::find(obstacleShapes->replacedFields.begin(),obstacleShapes->replacedFields.end(),"mode")==obstacleShapes->replacedFields.end(),
                "Inspector published an invalid shape template or replaced the chosen mode")) return false;
        }
        const auto sourceChoices=std::find_if(schema.fields.begin(),schema.fields.end(),[](const auto& field)
        { return field.componentType=="NavigationSurface" && field.field=="sources"; });
        if(!Expect(sourceChoices!=schema.fields.end() && sourceChoices->choices.size()==1 &&
            ReadSerializedStringField(sourceChoices->choices.front().value,"componentGuid")==sourceGuid,"Editor source choices lost the actual component GUID")) return false;
        auto explicitSurface=VansComponentTypeCatalog::CreateDefaultData("NavigationSurface");
        SetSerializedObjectField(explicitSurface,"collection",VansSerializedValue::String("ExplicitSources"));
        SetSerializedObjectField(explicitSurface,"sources",VansSerializedValue::Array({sourceChoices->choices.front().value}));
        if(!Expect(editor.ValidateNavigationComponent("NavigationSurface",explicitSurface).valid,"Editor rejected its actual Source component reference")) return false;
        auto* sourceReference=FindObjectField(explicitSurface,"sources");
        SetSerializedObjectField(sourceReference->arrayItems.front(),"entityGuid",VansSerializedValue::String(VansAssetGuid::New().ToString()));
        if(!Expect(!editor.ValidateNavigationComponent("NavigationSurface",explicitSurface).valid,"Editor accepted a Source paired with the wrong owner")) return false;
        for(const auto* type:{"NavigationSurface","NavigationSource","NavigationModifier","NavigationAgent"})
        {
            auto data=VansComponentTypeCatalog::CreateDefaultData(type);
            if(!Expect(editor.ValidateNavigationComponent(type,data).valid,"Editor rejected a current default navigation component")) return false;
            if(std::string(type)!="NavigationSource")
            {
                SetSerializedObjectField(data,"mapId",VansSerializedValue::String("UnavailableMap"));
                if(!Expect(!editor.ValidateNavigationComponent(type,data).valid,"Editor accepted an unavailable Map")) return false;
            }
        }
        auto modifier=VansComponentTypeCatalog::CreateDefaultData("NavigationModifier");
        SetSerializedObjectField(modifier,"shape",VansSerializedValue::String("Convex"));
        SetSerializedObjectField(modifier,"convexVerticesMeters",Value::Array({
            Value::Array({Value::Int(-1),Value::Int(-1)}),Value::Array({Value::Int(1),Value::Int(1)}),
            Value::Array({Value::Int(-1),Value::Int(1)}),Value::Array({Value::Int(1),Value::Int(-1)})}));
        if(!Expect(!editor.ValidateNavigationComponent("NavigationModifier",modifier).valid,"Editor accepted a self-intersecting modifier")) return false;
        if(!CheckNavigationPreviewAPIAdmission(editor,surfaceDesc.stableGuid,surfaceComponent,guid,error)) return false;
        editor.SetPlayState(EnginePlayState::Play);
        if(!Expect(editor.GetPlayState()==EnginePlayState::Play,"Valid navigation prevented entering Play")) return false;
        editor.SetPlayState(EnginePlayState::Edit);
        wall.SetEnabled(false);
        editor.SetPlayState(EnginePlayState::Play);
        if(!Expect(editor.GetPlayState()==EnginePlayState::Edit && !navigation->HasAgent(entity),
            "Stale physical input entered Play or retained a navigation driver")) return false;
        auto behavior=std::make_shared<VansAIBehaviorAsset>();
        behavior->name="NavigationAuthoringPatrol"; behavior->bindings={"Activate","Release","Target"};
        behavior->blackboard={{"Activate",VansAIValueType::Bool,false},{"Release",VansAIValueType::Bool,false},
            {"Target",VansAIValueType::Entity,VansEntityHandle{}}};
        behavior->initialState="Patrol";
        behavior->states.push_back({"Patrol",VansAITaskKind::Patrol,VansAIPatrolTaskConfig{1,.25f},{}});
        VansAssetObjectRepository behaviorAssets;
        const auto behaviorGuid=VansAssetGuid::FromStableName("NavigationContract","AuthoringPatrol");
        if(!Expect(behaviorAssets.Publish<VansAIBehaviorAsset>(behaviorGuid,VansAssetType::AIBehavior,1,behavior,{},error).IsValid(),error)) return false;
        VansRuntimeAIAgentComponent aiConfig; aiConfig.behaviorGuid=behaviorGuid.ToString(); aiConfig.sight.enabled=false;
        const auto aiComponent=runtime->AddComponent(entity,VansRuntimeComponentType_AIAgent,aiConfig,"NavigationContract.Patrol");
        VansRuntimePhaseGuard aiComponentCleanup([&](){runtime->RemoveComponent(aiComponent);});
        VansAIWorld patrolAI;
        if(!Expect(!patrolAI.Initialize(*runtime,nullptr,behaviorAssets,*navigation,error),
            "Formal AI initialization accepted unavailable navigation")) return false;
        if(!Expect(patrolAI.Initialize(*runtime,nullptr,behaviorAssets,*navigation,error,VansAIInitializationPolicy::Authoring),error)) return false;
        patrolAI.Update(1./60.);
        if(!Expect(navigation->CaptureQueryStats().queued==0,"Authoring AI scheduled movement on stale geometry")) return false;
        patrolAI.Shutdown();
        wall.SetEnabled(true);
        editor.SetPlayState(EnginePlayState::Play);
        if(!Expect(editor.GetPlayState()==EnginePlayState::Play && navigation->HasAgent(entity),
            "Restoring valid physical input did not recover the Play boundary")) return false;
        if(!Expect(patrolAI.Initialize(*runtime,nullptr,behaviorAssets,*navigation,error),error)) return false;
        patrolAI.Update(1./60.);
        if(!Expect(navigation->CaptureQueryStats().queued==1,"Patrol did not submit one asynchronous reachable-point request")) return false;
        for(int frame=0;frame<120;++frame)
        {
            PaceNavigationWorkerFrame(*navigation);
            navigation->Update(1./60.); patrolAI.Update(1./60.);
            const auto movement=navigation->CaptureAgent(entity);
            if(movement && movement->request.IsValid()) break;
        }
        const auto patrolMove=navigation->CaptureAgent(entity);
        if(!Expect(patrolMove && patrolMove->request.IsValid() && patrolMove->owner=="NavigationContract.Patrol",
            "Actual AI Patrol did not consume its sampled point through the common move request")) return false;
        patrolAI.Shutdown();
        if(!Expect(navigation->CaptureQueryStats().queued==0 && navigation->CaptureQueryStats().running==0,
            "AI shutdown retained a patrol query")) return false;
        editor.SetPlayState(EnginePlayState::Edit);
        std::cout<<"[NavigationEditor] PASS: actual source catalogue, component validation, stale Play rejection and recovery\n";

    }
    const auto path = std::filesystem::temp_directory_path() / "ForestNavigationLuaContract.lua";
    ScopedFileRemoval fileCleanup(path);
    {
        std::ofstream file(path);
        file << R"lua(return {
    on_enable = function(self)
        local nav = vans.navigation
        lua_nav_cancel_events = 0
        self.subscription = assert(nav.subscribe(self:get_object(), function(event)
            if event.kind == 'completion' and event.result == 'cancelled' then
                lua_nav_cancel_events = lua_nav_cancel_events + 1
            end
        end))
        for i=1,2 do
            local request = assert(nav.request_move(self:get_object(), {x=4,y=0,z=0}))
            assert(nav.stop(request))
        end
        lua_nav_path = assert(nav.request_path({x=4,y=0,z=0}, {x=-4,y=0,z=0}))
        lua_nav_projection = assert(nav.project_point({x=-4,y=.6,z=0}))
        lua_nav_raycast = assert(nav.nav_raycast({x=4,y=0,z=0},{x=-4,y=0,z=0}))
        lua_nav_boundary = assert(nav.nearest_boundary({x=-4,y=.6,z=0},3))
        lua_nav_strict = assert(nav.request_path({x=4,y=0,z=0},{x=-4,y=.6,z=0},
            {goal_acceptance='requested',goal_projection_tolerance=.01}))
        lua_nav_sample = assert(nav.sample_reachable_point(self:get_object(), {x=-4,y=0,z=0}, 1, 0))
        lua_nav_request = assert(nav.request_move(self:get_object(), {x=-4,y=0,z=0}, {arrival_radius=.45}))
        assert(nav.pause(lua_nav_request))
        assert(nav.get_state(lua_nav_request).state == 'paused')
        assert(nav.resume(lua_nav_request))
        assert(nav.update_goal(lua_nav_request, {x=-4,y=0,z=0}))
    end,
    on_update = function(self, dt)
        if lua_nav_path then
            local result = assert(vans.navigation.get_query_state(lua_nav_path))
            if result.state == 'finished' then
                assert(result.topology == 'complete' and #result.points >= 2)
                assert(result.success and result.query_filter == 'Default')
                assert(result.goal_acceptance == 'projected')
                assert(result.requested_start.x == 4 and result.requested_end.x == -4)
                assert(result.projected_end and result.reached_end and result.end_projection_distance >= 0)
                assert(#result.corner_flags == #result.points and #result.tiles > 0)
                lua_nav_path_completed = true
                assert(vans.navigation.release_query(lua_nav_path))
                lua_nav_path = nil
            end
        end
        if lua_nav_projection then
            local result = assert(vans.navigation.get_query_state(lua_nav_projection))
            if result.state == 'finished' then
                assert(result.kind == 'project_point' and result.success and result.point and result.point.y < .6)
                assert(result.projection_distance > 0 and #result.points == 0 and result.topology == 'none')
                lua_nav_projection_completed = true
                assert(vans.navigation.release_query(lua_nav_projection))
                lua_nav_projection = nil
            end
        end
        if lua_nav_raycast then
            local result = assert(vans.navigation.get_query_state(lua_nav_raycast))
            if result.state == 'finished' then
                assert(result.kind == 'raycast' and result.success and result.hit and result.fraction < 1)
                assert(result.point and result.normal and #result.tiles > 0 and result.topology == 'none')
                lua_nav_raycast_completed = true
                assert(vans.navigation.release_query(lua_nav_raycast)); lua_nav_raycast = nil
            end
        end
        if lua_nav_boundary then
            local result = assert(vans.navigation.get_query_state(lua_nav_boundary))
            if result.state == 'finished' then
                assert(result.kind == 'nearest_boundary' and result.success and result.found and result.distance > 0)
                assert(result.point and result.normal and result.projection_distance > 0 and result.topology == 'none')
                lua_nav_boundary_completed = true
                assert(vans.navigation.release_query(lua_nav_boundary)); lua_nav_boundary = nil
            end
        end
        if lua_nav_strict then
            local result = assert(vans.navigation.get_query_state(lua_nav_strict))
            if result.state == 'finished' then
                assert(not result.success and result.failure == 'EndProjectionRejected' and result.goal_acceptance == 'requested')
                assert(math.abs(result.requested_end.y - .6) < .000001 and #result.points == 0)
                lua_nav_strict_completed = true
                assert(vans.navigation.release_query(lua_nav_strict))
                lua_nav_strict = nil
            end
        end
        if lua_nav_sample then
            local result = assert(vans.navigation.get_query_state(lua_nav_sample))
            if result.state == 'finished' then
                assert(result.point and result.attempts > 0)
                lua_nav_sample_completed = true
                assert(vans.navigation.release_query(lua_nav_sample))
                lua_nav_sample = nil
            end
        end
        local state = assert(vans.navigation.get_state(lua_nav_request))
        if state.result == 'reached_goal' then
            lua_nav_reached = true
            lua_nav_request = assert(vans.navigation.request_move(self:get_object(), {x=4,y=0,z=0}))
            lua_nav_cancelled_query = assert(vans.navigation.request_path({x=-4,y=0,z=0}, {x=4,y=0,z=0}))
        end
    end
})lua";
        if (!Expect(static_cast<bool>(file), "Could not save bounded Lua navigation fixture")) return false;
    }
    auto* owner = scene.FindObjectByGuid(object.entityGuid);
    auto* script = new VansLuaScriptComponent();
    script->m_ComponentName = "LuaScript";
    script->m_ComponentGuid = VansAssetGuid::FromStableName("NavigationContract", "StandaloneLuaScript").ToString();
    script->m_ScriptPath = path.string();
    script->m_OwnerObject = owner;
    owner->AddComponent(script);
    runtime->AddComponent(entity, VansRuntimeComponentType_Script, VansRuntimeScriptComponent{}, script->m_ComponentGuid);
    if (!Expect(script->Instantiate() && scripts.RegisterScriptComponent(owner, script) &&
        script->m_State == VansLuaScriptState::Active, "Real Lua navigation module did not instantiate or request movement")) return false;
    const auto request = navigation->CaptureAgent(entity);
    if (!Expect(request && request->owner == script->m_ComponentGuid && request->request.IsValid(),
        "Lua navigation request lost the current script identity")) return false;
    if (!Expect(!navigation->RequestMove(entity, "Other.Driver", {}, {}, error).IsValid() && error == "OwnerConflict",
        "Lua driver permitted a second writer to take the motor")) return false;
    auto* lua = scripts.GetLuaState();
    bool reached = false;
    for (int frame = 0; frame < 1500 && !reached; ++frame)
    {
        PaceNavigationWorkerFrame(*navigation);
        script->CallUpdate(1.0f / 60.0f);
        scene.UpdateNavigation(1.0 / 60.0);
        controller.PrepareLocomotion(1.0f / 60.0f, motionSettings);
        controller.ResolveLocomotion({}, glm::quat(1, 0, 0, 0), false, {}, motionSettings, glm::vec3(1));
        {
            std::lock_guard<std::mutex> lease(physics.GetSimulationMutex());
            controller.FlushMoveAndSync();
        }
        lua_getglobal(lua, "lua_nav_reached");
        reached = lua_toboolean(lua, -1) != 0;
        lua_pop(lua, 1);
        if (script->m_State != VansLuaScriptState::Active) break;
    }
    if (!Expect(reached, "Real Lua navigation request did not reach the target through the CCT motor")) return false;
    lua_getglobal(lua,"lua_nav_cancel_events"); const auto cancellationEvents=lua_tointeger(lua,-1); lua_pop(lua,1);
    if(!Expect(cancellationEvents==2,"Same-frame Stop/RequestMove lost or duplicated an authoritative completion event")) return false;
    for(const auto* result:{"lua_nav_path_completed","lua_nav_sample_completed","lua_nav_projection_completed","lua_nav_strict_completed","lua_nav_raycast_completed","lua_nav_boundary_completed"})
    {
        lua_getglobal(lua,result); const bool completed=lua_toboolean(lua,-1)!=0; lua_pop(lua,1);
        if(!Expect(completed,"Real Lua general path or reachable sampling did not finish")) return false;
    }
    script->Disable();
    const auto cancelled = navigation->CaptureAgent(entity);
    if (!Expect(cancelled && cancelled->result == VansNavigationMoveResult::Cancelled &&
        cancelled->desiredSpeed == 0.0f, "Disabling the Lua script retained its second navigation request")) return false;
    if(!Expect(navigation->CaptureQueryStats().queued==0 && navigation->CaptureQueryStats().running==0,
        "Disabling Lua retained an owned generic navigation query")) return false;
    std::cout << "[LuaNavigation] PASS: real Scene/script source, CCT movement, controls, owned result and disable cancellation\n";
    return true;
}

bool CheckStandaloneLuaLinkDriver(VansEngine::VansPhysicsSystem& physics)
{
    using namespace Vans; using namespace VansGraphics; using namespace VansEngine;
    std::string error; auto& project=VansProjectManager::Get(); bool opened=false;
    VansRuntimePhaseGuard projectCleanup([&](){if(opened) project.CloseProject();});
    if(!project.GetGAFProjectConfiguration())
    {
        auto workspace=std::filesystem::current_path();
        for(int depth=0;depth<8 && !std::filesystem::exists(workspace/"TestV2Project/ForestProject.json");++depth) workspace=workspace.parent_path();
        VansProjectOpenRequest request; request.m_ProjectRootPath=(workspace/"TestV2Project").generic_string();
        request.m_Options.m_UpdateRecentProjects=false; request.m_Options.m_AssetLoadMode=VansProjectAssetLoadMode::None;
        const auto result=project.OpenProject(request);
        if(!Expect(result.m_Opened,result.m_Message.c_str())) return false; opened=true;
    }
    enum class Case { Forward,Reverse,Timeout,Cancel,Pause,Abort,Unsubscribe,DisableAgent,DisableLink,WithdrawMap,UnloadScene,Blocked,EventOnly,Overflow,
        CancelRequested,PauseRequested,AbortRequested,UnsubscribeRequested,DisableAgentRequested,DisableLinkRequested,
        WithdrawMapRequested,UnloadSceneRequested,BlockedRequested,ReplaceRequested,ReplaceTraversing };
    const std::pair<float,Case> cases[]{
        {1.f/30.f,Case::Forward},{1.f/60.f,Case::Forward},{1.f/120.f,Case::Forward},{1.f/60.f,Case::Reverse},
        {1.f/60.f,Case::Timeout},{1.f/60.f,Case::Cancel},{1.f/60.f,Case::Pause},{1.f/60.f,Case::Abort},
        {1.f/60.f,Case::Unsubscribe},{1.f/60.f,Case::DisableAgent},{1.f/60.f,Case::DisableLink},
        {1.f/60.f,Case::WithdrawMap},{1.f/60.f,Case::UnloadScene},{1.f/60.f,Case::Blocked},
        {1.f/60.f,Case::EventOnly},{1.f/60.f,Case::Overflow},
        {1.f/60.f,Case::CancelRequested},{1.f/60.f,Case::PauseRequested},{1.f/60.f,Case::AbortRequested},
        {1.f/60.f,Case::UnsubscribeRequested},{1.f/60.f,Case::DisableAgentRequested},{1.f/60.f,Case::DisableLinkRequested},
        {1.f/60.f,Case::WithdrawMapRequested},{1.f/60.f,Case::UnloadSceneRequested},{1.f/60.f,Case::BlockedRequested},
        {1.f/60.f,Case::ReplaceRequested},{1.f/60.f,Case::ReplaceTraversing}};
    for(const auto [step,scenario]:cases)
    {
        std::vector<std::uint32_t> extraTransforms;
        VansRuntimePhaseGuard transformCleanup([&](){for(const auto id:extraTransforms) VansTransformStore::Release(id);});
        const auto allocate=[&](const glm::vec3& position)
        {
            const auto id=VansTransformStore::Allocate(); extraTransforms.push_back(id); VansTransform pose;
            pose.m_Position=position; pose.m_Rotation=glm::vec3(0); pose.m_Scale=glm::vec3(1); VansTransformStore::Write(id,pose); return id;
        };
        PhysicsNodeProperties properties; properties.enabled=true; properties.layerName="Environment"; properties.boxExtents={4,.1f,3.5f};
        VansPhysicsNode firstFloor,secondFloor; firstFloor.Initialize(properties,allocate({0,-.1f,175.5f}));
        secondFloor.Initialize(properties,allocate({0,-.1f,184.5f}));
        if(!Expect(firstFloor.HasActor() && secondFloor.HasActor(),"Lua Link fixture has no physical platforms")) return false;
        VansCharacterControllerNode controller;
        const auto assetGuid=VansAssetGuid::FromStableName("NavigationContract","LuaLinkMesh");
        auto& assets=project.GetAssetObjectRepository();
        VansRuntimePhaseGuard assetCleanup([&](){assets.Remove(assetGuid);});
        NavigationSceneCameraDevice cameraDevice; VansCamera camera(&cameraDevice); VansScene scene; scene.InjectCamera(&camera);
        VansScriptContext scripts; scripts.VansScriptSetup(); scripts.AttachSceneWithoutRebuild(&scene);
        VansRuntimePhaseGuard sceneCleanup([&](){scripts.AttachSceneWithoutRebuild(nullptr);scene.UnloadScene(nullptr);});
        VansSceneObjectBuildPlan plan; VansSceneObjectBuildConfig actor,linkOwner;
        actor.entityGuid=VansAssetGuid::FromStableName("NavigationContract","LuaLinkActor").ToString(); actor.name="LuaLinkActor"; actor.transform=VansSceneTransformConfig{};
        linkOwner.entityGuid=VansAssetGuid::FromStableName("NavigationContract","LuaLinkOwner").ToString(); linkOwner.name="LuaLink"; linkOwner.transform=VansSceneTransformConfig{};
        actor.componentGuids["transform"]=VansAssetGuid::FromStableName("NavigationContract","LuaLinkActorTransform").ToString();
        linkOwner.componentGuids["transform"]=VansAssetGuid::FromStableName("NavigationContract","LuaLinkOwnerTransform").ToString();
        linkOwner.navigationLink=VansSceneNavigationLinkConfig{};
        linkOwner.navigationLink->runtime.startMeters={0,0,-2}; linkOwner.navigationLink->runtime.endMeters={0,0,2};
        linkOwner.navigationLink->runtime.radiusMeters=.6f;
        linkOwner.componentGuids["navigation_link"]=VansAssetGuid::FromStableName("NavigationContract","LuaLinkComponent").ToString();
        plan.objects={actor,linkOwner}; VkDevice device=VK_NULL_HANDLE;
        auto built=VansSceneAssembly::BuildObjects(scene,device,plan,""); if(built.m_Built) built=VansSceneAssembly::InitializeDomains(scene);
        if(!Expect(built.m_Built,built.m_Error)) return false;
        auto* runtime=scene.GetRuntimeWorld(); auto* object=scene.FindObjectByGuid(actor.entityGuid);
        auto* linkObject=scene.FindObjectByGuid(linkOwner.entityGuid);
        if(!Expect(runtime && object && linkObject,"Lua Link fixture did not publish its typed objects")) return false;
        const auto entity=runtime->Entities().FindByGuid(actor.entityGuid);
        const auto setPose=[](std::uint32_t id,const glm::vec3& position)
        { auto pose=VansTransformStore::Read(id); pose.m_Position=position; VansTransformStore::Write(id,pose); VansTransformStore::MarkDirty(id); };
        const bool completionOnly=scenario==Case::EventOnly || scenario==Case::Overflow;
        const float startZ=scenario==Case::Reverse?186.f:174.f,goalZ=completionOnly?176.f:(scenario==Case::Reverse?174.f:186.f);
        setPose(object->m_TransformID,{0,.02f,startZ}); setPose(linkObject->m_TransformID,{0,0,180});
        CharControllerProperties capsule; capsule.m_Radius=.35f; capsule.m_Height=1.1f; capsule.m_StepOffset=.25f;
        capsule.m_ContactOffset=.02f; capsule.m_PositionOffset={0,.9f,0};
        if(!Expect(controller.Initialize(capsule,object->m_TransformID,{0,.92f,startZ}),"Lua Link CCT creation failed")) return false;
        controller.SeedGroundedContact(true);
        const std::string controllerGuid=VansAssetGuid::FromStableName("NavigationContract","LuaLinkCCT").ToString();
        runtime->AddComponent(entity,VansRuntimeComponentType_CharacterController,VansRuntimeCharacterControllerComponent{&controller},controllerGuid);
        auto* facade=new VansScriptCharacterControllerComponent(); facade->m_ComponentGuid=controllerGuid; facade->m_ControllerNode=&controller; object->AddComponent(facade);
        runtime->AddComponent(entity,VansRuntimeComponentType_NavigationAgent,VansRuntimeNavigationAgentComponent{},"LuaLink.Agent");
        AddRuntimeCollision(*runtime,firstFloor,"LuaLink.FirstFloor"); AddRuntimeCollision(*runtime,secondFloor,"LuaLink.SecondFloor");
        const auto surface=runtime->CreateEntity({VansAssetGuid::New().ToString(),"LuaLinkSurface",{},true});
        runtime->AddComponent(surface,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,180})},"LuaLink.SurfaceTransform");
        auto surfaceConfig=ContractSurface(project.GetProjectSettings().GetNavigationSettings()).surface; surfaceConfig.navigationMeshGuid=assetGuid.ToString();
        runtime->AddComponent(surface,VansRuntimeComponentType_NavigationSurface,surfaceConfig,"LuaLink.Surface");
        std::shared_ptr<const VansNavigationMeshAsset> asset;
        if(!Expect(BuildLiveContractNavigation(*runtime,project.GetProjectSettings().GetNavigationSettings(),asset,error) &&
            assets.Publish<VansNavigationMeshAsset>(assetGuid,VansAssetType::NavigationMesh,1,asset,{},error).IsValid(),error)) return false;
        VansAssetObjectHandle<VansNavigationMeshAsset> initialAssetHandle;
        assets.ResolveLatest<VansNavigationMeshAsset>(assetGuid,&initialAssetHandle);
        scene.PrepareNavigation(); auto* world=scene.GetNavigationWorld();
        if(!Expect(world && world->HasAgent(entity),"Lua Link Scene has no motor registration")) return false;
        auto* script=new VansLuaScriptComponent(); script->m_ComponentGuid=VansAssetGuid::FromStableName("NavigationContract","LuaLinkScript").ToString();
        script->m_ComponentName="LuaScript"; script->m_OwnerObject=object;
        const auto example=completionOnly?"NavigationAgentMove.lua":"NavigationLinkTraversal.lua";
        script->m_ScriptPath=(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"EngineDoc/Examples"/example).string();
        VansScriptSerializedFieldValue linkField; linkField.type=VansScriptSerializedFieldType::ObjectReference;
        linkField.objectReference.domain="SceneEntity"; linkField.objectReference.entityGuid=linkOwner.entityGuid; script->m_SerializedFields["link"]=linkField;
        VansScriptSerializedFieldValue goal; goal.type=VansScriptSerializedFieldType::Float; goal.floatValue=goalZ; script->m_SerializedFields["goal_z"]=goal;
        if(scenario==Case::Timeout)
        { VansScriptSerializedFieldValue timeout; timeout.type=VansScriptSerializedFieldType::Float; timeout.floatValue=.1f; script->m_SerializedFields["maximum_seconds"]=timeout; }
        object->AddComponent(script); runtime->AddComponent(entity,VansRuntimeComponentType_Script,VansRuntimeScriptComponent{},script->m_ComponentGuid);
        if(!Expect(script->Instantiate() && scripts.RegisterScriptComponent(object,script) && script->m_State==VansLuaScriptState::Active,
            "Lua Link sample could not subscribe/request under the real script owner")) return false;
        const auto initialRequest=world->CaptureAgent(entity)->request;
        const auto scriptGuid=script->m_ComponentGuid;
        std::uint32_t eventPhases=0,completions=0; bool observersDeferred=true;
        auto events=VansEventBus::Get().Subscribe<VansNavigationLifecycleEvent>([&](const auto& event)
        {
            if(event.request.session!=initialRequest.session) return;
            std::string observerError;
            observersDeferred=observersDeferred && event.requestOwner==scriptGuid &&
                !world->Stop(event.request,scriptGuid,observerError) &&
                observerError=="Navigation lifecycle observers must defer commands until their own update";
            if(event.kind==VansNavigationLifecycleKind::Traversal) eventPhases|=1u<<static_cast<unsigned int>(event.traversal.phase);
            else ++completions;
        },VansEventLane::MainThread);
        if(completionOnly && !Expect(script->m_UpdateRef==LUA_NOREF,"Completion-only sample unexpectedly owns an update callback")) return false;
        if(scenario==Case::Overflow)
        {
            for(int i=0;i<9;++i)
            {
                const auto request=world->CaptureAgent(entity)->request;
                if(!Expect(world->Stop(request,scriptGuid,error) && world->RequestMove(entity,scriptGuid,{0,0,goalZ},{},error).IsValid(),error)) return false;
            }
            script->CallUpdate(step);
            const auto cancelled=world->CaptureAgent(entity);
            if(!Expect(script->m_State==VansLuaScriptState::Faulted && completions==9 && cancelled &&
                cancelled->result==VansNavigationMoveResult::Cancelled && cancelled->desiredSpeed==0 &&
                world->CaptureQueryStats().queued==0 && world->CaptureQueryStats().running==0,
                "Mailbox overflow did not fault and release the actual script/navigation owner")) return false;
            std::cout<<"[NavigationLinkLuaCCT] scenario="<<static_cast<int>(scenario)<<" PASS: eight-event mailbox overflow cancels actual owner\n";
            continue;
        }
        VansCharacterMotionSettings motion; motion.driveMode=VansLocomotionDriveMode::Capsule;
        const bool beforeAcknowledgement=scenario>=Case::CancelRequested && scenario<=Case::ReplaceRequested;
        const bool replacement=scenario==Case::ReplaceRequested || scenario==Case::ReplaceTraversing;
        std::uint32_t phases=0; bool wrongExitRejected=false,intervened=false,unloaded=false; float maximumHeight=0; bool reached=false;
        for(int frame=0;frame<1800 && !reached;++frame)
        {
            PaceNavigationWorkerFrame(*world); script->CallUpdate(step); scene.UpdateNavigation(step);
            const auto state=world->CaptureAgent(entity);
            if(!state) break;
            phases|=1u<<static_cast<unsigned int>(state->traversal.phase);
            if(state->traversal.phase==VansNavigationTraversalPhase::Traversing && !wrongExitRejected)
            {
                if(!Expect(world->HasTraversalControl(entity,script->m_ComponentGuid) && !world->HasTraversalControl(entity,"Other.Script") &&
                    !world->CompleteTraversal(state->traversal.handle,script->m_ComponentGuid,error),"Lua Link granted foreign control or accepted an entrance completion")) return false;
                wrongExitRejected=true;
            }
            if(!intervened && ((beforeAcknowledgement && state->traversal.phase==VansNavigationTraversalPhase::TraversalRequested) ||
                (!beforeAcknowledgement && state->traversal.phase==VansNavigationTraversalPhase::Traversing && maximumHeight>.1f)))
            {
                intervened=true;
                switch(scenario)
                {
                case Case::Cancel: case Case::CancelRequested:
                    if(!Expect(world->Stop(state->request,scriptGuid,error),error)) return false; break;
                case Case::Pause: case Case::PauseRequested:
                    if(!Expect(world->Pause(state->request,scriptGuid,error),error)) return false; break;
                case Case::Abort: case Case::AbortRequested:
                    if(!Expect(world->AbortTraversal(state->traversal.handle,scriptGuid,error),error)) return false; break;
                case Case::Unsubscribe: case Case::UnsubscribeRequested:
                    if(!Expect(world->Unsubscribe(state->traversal.subscription,scriptGuid,error),error)) return false; break;
                case Case::DisableAgent:
                case Case::DisableAgentRequested:
                {
                    const auto* storage=runtime->FindStorage<VansRuntimeNavigationAgentComponent>(VansRuntimeComponentType_NavigationAgent);
                    if(!Expect(runtime->SetComponentEnabled(storage->FindFirstOwnedBy(entity),false) && scene.SynchronizeNavigation(error),error)) return false;
                    break;
                }
                case Case::DisableLink:
                case Case::DisableLinkRequested:
                {
                    const auto* storage=runtime->FindStorage<VansRuntimeNavigationLinkComponent>(VansRuntimeComponentType_NavigationLink);
                    if(!Expect(runtime->SetComponentEnabled(storage->FindFirstOwnedBy(runtime->Entities().FindByGuid(linkOwner.entityGuid)),false) &&
                        !scene.SynchronizeNavigation(error),"Baked Link disable did not withdraw the stale Map")) return false;
                    break;
                }
                case Case::WithdrawMap: case Case::WithdrawMapRequested: world->WithdrawMap("World"); break;
                case Case::UnloadScene: case Case::UnloadSceneRequested: scene.UnloadScene(nullptr); unloaded=true; break;
                case Case::Blocked: case Case::BlockedRequested: controller.AcquireGameplayMovementBlock(); break;
                case Case::ReplaceRequested: case Case::ReplaceTraversing:
                {
                    // A real source change and repository publication keep the GUID, replace the Map,
                    // and cancel both the current action and an old worker that has not started yet.
                    std::promise<void> unblock; const auto gate=unblock.get_future().share();
                    std::atomic<int> entered{0};
                    const auto block=[&](){entered.fetch_add(1);gate.wait();};
                    const auto blockers=VansJobSystem::Get().QueueJobGroup({block,block});
                    bool openedGate=false;
                    const auto openGate=[&](){if(!openedGate){openedGate=true;unblock.set_value();}};
                    VansRuntimePhaseGuard releaseGate([&](){openGate();blockers->Wait();});
                    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                    while(entered.load()!=2 && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
                    if(!Expect(entered.load()==2,"Hot replacement did not occupy the actual navigation workers")) return false;
                    VansNavigationQueryRequest query; query.kind=VansNavigationQueryKind::PointProjection; query.start={0,0,175.5f};
                    const auto oldQuery=world->RequestQuery(query,"Link.HotReplace",error);
                    world->Update(step);
                    if(!Expect(oldQuery.IsValid() && world->CaptureQueryStats().inFlight==1,"Hot replacement has no outstanding old Map worker")) return false;
                    const std::weak_ptr<const VansNavigationMeshAsset> retiredAsset=asset;
                    const auto oldInputHash=asset->FindProfile("Humanoid")->inputHash;
                    auto widened=properties; widened.boxExtents.x=5;
                    firstFloor.Initialize(widened,firstFloor.GetTransformID());
                    std::shared_ptr<const VansNavigationMeshAsset> currentAsset;
                    if(!Expect(BuildLiveContractNavigation(*runtime,project.GetProjectSettings().GetNavigationSettings(),currentAsset,error) &&
                        currentAsset->FindProfile("Humanoid")->inputHash!=oldInputHash,"Hot replacement did not build changed native input")) return false;
                    const auto currentHandle=assets.Publish<VansNavigationMeshAsset>(assetGuid,VansAssetType::NavigationMesh,2,currentAsset,{},error);
                    asset.reset();
                    const auto before=std::chrono::steady_clock::now();
                    if(!Expect(currentHandle.IsValid() && currentHandle.generation!=initialAssetHandle.generation &&
                        !assets.Resolve(initialAssetHandle) && scene.SynchronizeNavigation(error),error)) return false;
                    const auto idle=world->CaptureAgent(entity);
                    const auto cancelled=world->CaptureQuery(oldQuery,"Link.HotReplace",error);
                    if(!Expect(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100) && idle &&
                        idle->state==VansNavigationMoveState::Idle && !idle->request.IsValid() &&
                        !world->HasSubscription(state->traversal.subscription,scriptGuid) && !world->HasTraversalControl(entity,scriptGuid) &&
                        !world->BeginTraversal(state->traversal.handle,scriptGuid,error) &&
                        !world->CompleteTraversal(state->traversal.handle,scriptGuid,error) &&
                        !world->AbortTraversal(state->traversal.handle,scriptGuid,error) &&
                        !world->Stop(initialRequest,scriptGuid,error) && cancelled &&
                        cancelled->path.failure==VansNavigationPathFailure::MapUnavailable &&
                        world->CaptureQueryStats().inFlight==1 && !retiredAsset.expired(),
                        "Hot replacement blocked, revived old controls or freed an in-flight Map")) return false;
                    openGate(); blockers->Wait();
                    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                    while(!retiredAsset.expired() && std::chrono::steady_clock::now()<deadline)
                    { world->Update(step); std::this_thread::sleep_for(std::chrono::microseconds(100)); }
                    const auto retired=world->CaptureQuery(oldQuery,"Link.HotReplace",error);
                    if(!Expect(retiredAsset.expired() && !world->CaptureQueryStats().inFlight && retired &&
                        !retired->point && !retired->route && retired->path.failure==VansNavigationPathFailure::MapUnavailable &&
                        world->ReleaseQuery(oldQuery,"Link.HotReplace",error),"Retired worker retained or republished old Map data")) return false;
                    // This point is outside the old platform and within the changed platform.
                    query.start={4.2f,0,175.5f}; query.nearestExtents={.05f,.5f,.05f};
                    const auto freshQuery=world->RequestQuery(query,"Link.HotReplace",error);
                    std::optional<VansNavigationQuerySnapshot> projected;
                    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                    do
                    {
                        world->Update(step); projected=world->CaptureQuery(freshQuery,"Link.HotReplace",error);
                        if(projected && projected->lifecycle==VansNavigationQueryLifecycle::Finished) break;
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    } while(std::chrono::steady_clock::now()<deadline);
                    if(!Expect(freshQuery.IsValid() && projected && projected->point && projected->point->x>4 &&
                        world->ReleaseQuery(freshQuery,"Link.HotReplace",error),"Replacement Agent query still used the old Map")) return false;
                    break;
                }
                default: break;
                }
                if(unloaded) break;
            }
            controller.PrepareLocomotion(step,motion); controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motion,glm::vec3(1));
            { std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
              auto* physicalScene=VansPhysicsNativeAccess::Scene(physics); physicalScene->simulate(step); physicalScene->fetchResults(true); controller.FlushMoveAndSync(); }
            maximumHeight=(std::max)(maximumHeight,controller.GetPosition().y-capsule.m_PositionOffset.y);
            reached=state->result==VansNavigationMoveResult::ReachedGoal;
            if(script->m_State!=VansLuaScriptState::Active || (state->state==VansNavigationMoveState::Finished && !reached)) break;
            if(intervened && (scenario==Case::Pause || scenario==Case::Unsubscribe ||
                scenario==Case::PauseRequested || scenario==Case::UnsubscribeRequested || replacement)) break;
        }
        if(unloaded)
        {
            if(!Expect(intervened && (eventPhases&32u) && completions==1 && observersDeferred && !scene.GetNavigationWorld(),
                "Scene unload retained the traversal, request or navigation owner")) return false;
            std::cout<<"[NavigationLinkLuaCCT] scenario="<<static_cast<int>(scenario)<<" PASS: actual Scene unload\n";
            continue;
        }
        // 终态由上一帧导航产生；下一帧脚本消费拥有型事件，再执行其普通 update。
        script->CallUpdate(step);
        const auto state=world->CaptureAgent(entity);
        auto* lua=scripts.GetLuaState(); lua_rawgeti(lua,LUA_REGISTRYINDEX,script->m_InstanceRef); lua_getfield(lua,-1,"phase");
        const std::string scriptPhase=lua_isstring(lua,-1)?lua_tostring(lua,-1):""; lua_pop(lua,2);
        lua_rawgeti(lua,LUA_REGISTRYINDEX,script->m_InstanceRef); lua_getfield(lua,-1,"last_result");
        const std::string scriptResult=lua_isstring(lua,-1)?lua_tostring(lua,-1):""; lua_pop(lua,2);
        std::cout<<"[NavigationLinkLuaCCT] scenario="<<static_cast<int>(scenario)<<" dt="<<step<<" end=("<<controller.GetPosition().x<<','<<controller.GetPosition().y<<','<<controller.GetPosition().z
            <<") result="<<(state?static_cast<int>(state->result):-1)<<" phases="<<phases<<" events="<<eventPhases<<" completions="<<completions
            <<" script="<<scriptPhase<<" max_height="<<maximumHeight;
        if(state) std::cout<<" state="<<static_cast<int>(state->state)<<" speed="<<state->desiredSpeed<<" goal=("<<state->path.projectedEnd.x<<','
            <<state->path.projectedEnd.y<<','<<state->path.projectedEnd.z<<") diagnostic="<<state->path.diagnostic;
        std::cout<<'\n';
        const bool success=scenario==Case::Forward || scenario==Case::Reverse;
        if(!Expect((wrongExitRejected || beforeAcknowledgement || completionOnly) && observersDeferred && script->m_State==VansLuaScriptState::Active &&
            !world->HasTraversalControl(entity,scriptGuid),"Link action retained control or violated event/owner dispatch")) return false;
        if(completionOnly)
        {
            if(!Expect(reached && completions==1 && eventPhases==0 && scriptResult=="reached_goal",
                "Event-only Lua sample lost completion without on_update")) return false;
        }
        else if(success)
        {
            if(!Expect(reached && state && maximumHeight>1 && (phases&30u)==30u && (eventPhases&30u)==30u &&
                completions==1 && scriptPhase=="completed" && scriptResult=="reached_goal" && state->recoveryCount==0,
                "Real Lua/CCT did not execute, validate and release the Link action")) return false;
        }
        else if(!Expect((eventPhases&32u) && scriptPhase=="aborted" && (scenario==Case::Timeout || intervened),
            "Interrupted Link action lost its authoritative Aborted event")) return false;
        if(scenario==Case::Pause || scenario==Case::Unsubscribe || scenario==Case::PauseRequested || scenario==Case::UnsubscribeRequested)
        { if(!Expect(completions==0,"Consumer retirement incorrectly finished the move twice")) return false; }
        else if(!Expect(completions==1,"Link lifecycle did not emit exactly one completion")) return false;
        if(beforeAcknowledgement)
        {
            lua_rawgeti(lua,LUA_REGISTRYINDEX,script->m_InstanceRef); lua_getfield(lua,-1,"last_action_error");
            const bool rejected=lua_isstring(lua,-1); lua_pop(lua,1); lua_getfield(lua,-1,"action");
            const bool inactive=lua_isnil(lua,-1); lua_pop(lua,2);
            if(!Expect(rejected && inactive && maximumHeight<.1f && !(eventPhases&8u),
                "A retired entrance notification started motion or lost its rejection diagnostic")) return false;
        }
        if(scenario==Case::Blocked || scenario==Case::BlockedRequested) controller.ReleaseGameplayMovementBlock();
        if(replacement)
        {
            if(!Expect(state && state->state==VansNavigationMoveState::Idle && scriptResult=="cancelled",
                "Replacement silently resumed the old owner request")) return false;
            script->Disable(); script->Enable();
            const auto restarted=world->CaptureAgent(entity);
            if(!Expect(script->m_State==VansLuaScriptState::Active && restarted && restarted->request.IsValid() &&
                restarted->request.serial!=initialRequest.serial && restarted->owner==scriptGuid &&
                world->Stop(restarted->request,scriptGuid,error),"Current Lua consumer could not explicitly subscribe after replacement")) return false;
            script->CallUpdate(step);
        }
        script->Disable();
        if(!Expect(world->CaptureQueryStats().queued==0 && world->CaptureQueryStats().running==0,
            "Link consumer disable retained native query work")) return false;
    }
    std::cout<<"[NavigationLinkLuaCCT] PASS: real Scene/Lua/CCT, forward/reverse, actual exit and 30/60/120 Hz; pre-acknowledgement cancellation and same-GUID hot replacement with worker retirement; bounded lifecycle dispatch\n";
    return true;
}
bool CheckUnifiedQueryScheduler(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& sourceSettings)
{
    using namespace Vans;
    auto settings=sourceSettings;
    settings.query.maximumIterationsPerFrame=1; settings.query.maximumIterationsPerSlice=1;
    settings.query.maximumPathStartsPerFrame=1; settings.query.maximumActiveQueries=2;
    settings.query.maximumQueuedQueries=32; settings.query.maximumQueryHistory=64; settings.query.maximumQueryFrames=2000;
    std::string error; VansNavigationWorld world;
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error),error)) return false;
    VansNavigationQueryRequest path; path.start={-4,0,0}; path.goal={4,0,0};
    auto invalidFilter=path; invalidFilter.filterId="MissingFilter";
    if(!Expect(!world.RequestQuery(invalidFilter,"scheduler-contract",error).IsValid() && error=="FilterInvalid",
        "Generic query accepted an unavailable compiled filter")) return false;
    std::vector<VansNavigationQueryHandle> handles;
    for(int i=0;i<16;++i)
    {
        path.priority=i==15?3:0;
        const auto handle=world.RequestQuery(path,"scheduler-contract",error);
        if(!Expect(handle.IsValid(),error)) return false;
        handles.push_back(handle);
    }
    VansNavigationQueryRequest sample=path; sample.kind=VansNavigationQueryKind::ReachablePoint; sample.goal={1000000,0,0};
    sample.maximumAttempts=12; sample.radius=4;
    const auto sampleHandle=world.RequestQuery(sample,"scheduler-contract",error);
    if(!Expect(sampleHandle.IsValid(),error)) return false;
    std::size_t completed=0; std::uint32_t lastAttempts=0;
    for(int frame=0;frame<2000 && completed<handles.size();++frame)
    {
        world.Update(1./60.); const auto stats=world.CaptureQueryStats();
        if(!Expect(stats.iterationsLastFrame<=1 && stats.startsLastFrame<=1 && stats.running<=2,
            "Unified scheduler exceeded node/start/active frame budgets")) return false;
        const auto sampling=world.CaptureQuery(sampleHandle,"scheduler-contract",error);
        if(!Expect(sampling && sampling->attempts-lastAttempts<=1,"Reachable sampling ran multiple candidates in one frame")) return false;
        lastAttempts=sampling->attempts;
        completed=0;
        for(const auto handle:handles)
        {
            const auto result=world.CaptureQuery(handle,"scheduler-contract",error);
            if(!Expect(result.has_value(),error)) return false;
            if(result->lifecycle==VansNavigationQueryLifecycle::Finished)
            {
                if(!Expect(result->path.status==VansNavigationPathStatus::Complete,"Fair scheduler lost a complete topology result")) return false;
                ++completed;
            }
        }
    }
    if(!Expect(completed==handles.size(),"Normal queries starved behind other queued work")) return false;
    for(int frame=0;frame<32;++frame)
    {
        const auto result=world.CaptureQuery(sampleHandle,"scheduler-contract",error);
        if(result && result->lifecycle==VansNavigationQueryLifecycle::Finished) break;
        world.Update(1./60.);
    }
    const auto sampled=world.CaptureQuery(sampleHandle,"scheduler-contract",error);
    if(!Expect(sampled && sampled->attempts==12 && sampled->path.failure==VansNavigationPathFailure::NoReachablePoint,
        "Unreachable sample did not terminate at the bounded attempt count")) return false;
    if(!Expect(!world.CancelQuery(handles.front(),"different-owner",error) && error=="OwnerConflict",
        "Generic query permitted another owner to cancel")) return false;
    settings.query.maximumQueuedQueries=3; settings.query.maximumActiveQueries=1; settings.query.maximumQueryHistory=2;
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error),error)) return false;
    if(!Expect(!world.CaptureQuery(handles.front(),"scheduler-contract",error),"New World session accepted an old generic query")) return false;
    handles.clear();
    for(int i=0;i<3;++i) handles.push_back(world.RequestQuery(path,"bounded-owner",error));
    if(!Expect(handles.back().IsValid() && !world.RequestQuery(path,"overflow-owner",error).IsValid() && error=="QueueFull",
        "Full query queue silently fell back to synchronous execution")) return false;
    world.CancelOwner("bounded-owner");
    if(!Expect(world.CaptureQueryStats().history==2 && world.CaptureQueryStats().queued==0 && world.CaptureQueryStats().running==0,
        "Owner cancellation retained work or unbounded query history")) return false;
    const auto mapCancelled=world.RequestQuery(path,"map-owner",error);
    world.WithdrawMap("World");
    const auto result=world.CaptureQuery(mapCancelled,"map-owner",error);
    if(!Expect(result && result->path.failure==VansNavigationPathFailure::MapUnavailable,
        "Map withdrawal retained a query against unavailable geometry")) return false;
    settings.query.maximumQueryFrames=1;
    if(!Expect(world.Initialize(settings,error) && world.RegisterMaps({map},error),error)) return false;
    const auto first=world.RequestQuery(path,"deadline-owner",error);
    const auto second=world.RequestQuery(path,"deadline-owner",error);
    world.Update(1./60.); world.Update(1./60.);
    const auto deadline=world.CaptureQuery(second,"deadline-owner",error);
    if(!Expect(first.IsValid() && deadline && deadline->path.failure==VansNavigationPathFailure::QueryDeadlineExceeded,
        "Waiting query did not enforce its lifetime budget")) return false;
    std::cout<<"[NavigationScheduler] PASS: paths without Agent, shared node/start budgets, fair queue, async sample, owner/session/Map cancellation and bounded history\n";
    return true;
}

bool CheckWorkerNavigationQueries(const std::shared_ptr<Vans::VansNavigationMap>& map,const Vans::VansNavigationSettings& original)
{
    using namespace Vans;
    auto settings=original;
    settings.query.maximumActiveQueries=2; settings.query.maximumQueuedQueries=4;
    settings.query.maximumIterationsPerFrame=32; settings.query.maximumIterationsPerSlice=16;
    settings.query.maximumPathStartsPerFrame=2; settings.query.maximumQueryFrames=36000;
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansNavigationWorld world;
    VansNavigationWorld budgetWorld;
    VansRuntimePhaseGuard cleanup([&](){budgetWorld.Shutdown(); world.Shutdown(); jobs.Shutdown();});
    std::promise<void> unblock;
    const auto gate=unblock.get_future().share();
    std::atomic<int> entered{0};
    const auto block=[&](){entered.fetch_add(1); gate.wait();};
    const auto blockers=jobs.QueueJobGroup({block,block});
    bool opened=false;
    const auto open=[&](){if(!opened) { opened=true; unblock.set_value(); }};
    VansRuntimePhaseGuard releaseGate([&](){open(); blockers->Wait();});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(entered.load()!=2 && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    if(!Expect(entered.load()==2,"Worker fixture could not occupy the actual JobSystem workers")) return false;
    std::string error;
    if(!world.Initialize(settings,error) || !world.RegisterMaps({map},error)) return Expect(false,error);
    VansNavigationQueryRequest request; request.start={-4,0,0}; request.goal={4,0,0};
    const auto cancelled=world.RequestQuery(request,"worker-cancel",error);
    const auto released=world.RequestQuery(request,"worker-release",error);
    const auto before=std::chrono::steady_clock::now();
    world.Update(1./60.);
    if(!Expect(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100) && world.CaptureQueryStats().inFlight==2,
        "Navigation frame waited for occupied workers or failed to dispatch bounded work")) return false;
    if(!world.CancelQuery(cancelled,"worker-cancel",error) || !world.ReleaseQuery(released,"worker-release",error)) return Expect(false,error);
    const auto next=world.RequestQuery(request,"worker-next",error);
    world.Update(1./60.);
    auto stats=world.CaptureQueryStats();
    if(!Expect(next.IsValid() && stats.inFlight==2 && stats.queued==1 && stats.startsLastFrame==0,
        "Cancelled in-flight workers were dropped from the context capacity budget")) return false;
    auto budgetSettings=settings;
    budgetSettings.maximumResidentTiles=static_cast<int>(map->GetAsset()->FindProfile("Humanoid")->tiles.size());
    auto replacement=std::make_shared<VansNavigationMap>();
    if(!replacement->Initialize(map->GetAsset(),budgetSettings,"Humanoid",error) || !budgetWorld.Initialize(budgetSettings,error) ||
        !budgetWorld.RegisterMaps({map},error)) return Expect(false,error);
    const auto retiring=budgetWorld.RequestQuery(request,"retired-map",error);
    budgetWorld.Update(1./60.); budgetWorld.WithdrawMap("World");
    if(!Expect(retiring.IsValid() && budgetWorld.CaptureQueryStats().inFlight==1 && !budgetWorld.RegisterMaps({replacement},error),
        "Map registration omitted native residency retained by a cancelled worker")) return false;
    open(); blockers->Wait();
    std::optional<VansNavigationQuerySnapshot> result;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do
    {
        world.Update(1./60.); stats=world.CaptureQueryStats();
        if(!Expect(stats.inFlight<=2 && stats.iterationsLastFrame<=32 && stats.startsLastFrame<=2,
            "Concurrent navigation workers exceeded shared allocation budgets")) return false;
        result=world.CaptureQuery(next,"worker-next",error);
        if(result && result->lifecycle==VansNavigationQueryLifecycle::Finished) break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    } while(std::chrono::steady_clock::now()<deadline);
    const auto cancelledResult=world.CaptureQuery(cancelled,"worker-cancel",error);
    if(!Expect(result && result->path.status==VansNavigationPathStatus::Complete && result->route &&
        cancelledResult && cancelledResult->lifecycle==VansNavigationQueryLifecycle::Cancelled &&
        !world.CaptureQuery(released,"worker-release",error),"Worker completion revived a cancelled or released request")) return false;
    VansNavigationQueryContext motor;
    if(!motor.Initialize(map,settings.query,"Default",error)) return Expect(false,error);
    if(!Expect(motor.AdoptCorridorRoute(result->route)==VansNavigationPathFailure::None &&
        FollowContractCorridor(motor,request.start,1,2).valid,"Owned worker route did not reach the main-thread motor Corridor")) return false;
    if(!Expect(result->path.cornerFlags.size()==result->path.points.size() && !result->path.tiles.empty() &&
        glm::length(result->path.requestedStart-request.start)<1.e-5 && glm::length(result->path.requestedEnd-request.goal)<1.e-5 &&
        glm::length(result->path.reachedEnd-result->path.points.back())<1.e-5,
        "Worker route omitted requested/projected endpoints, corner flags or Tile dependencies")) return false;
    const auto projected=motor.ProjectPoint({-4,.6f,0},{1,2,1});
    if(!Expect(projected.valid && projected.distance>0 && projected.position.y<.6f &&
        motor.ProjectPoint({1.e38f,0,0},{1,2,1}).failure==VansNavigationPathFailure::InvalidInput &&
        !motor.ProjectPoint({100000,0,0},{1,2,1}).valid,"Point projection accepted invalid bounds or ignored the actual Map")) return false;
    const glm::vec3 raisedGoal(4,.6f,0);
    const auto strictPath=motor.FindPath({-4,0,0},raisedGoal,{1,2,1},VansNavigationGoalAcceptance::RequestedPoint,.01f);
    const auto projectedPath=motor.FindPath({-4,0,0},raisedGoal,{1,2,1},VansNavigationGoalAcceptance::ProjectedPoint,.01f);
    if(!Expect(strictPath.failure==VansNavigationPathFailure::EndProjectionRejected && strictPath.hasRequestCoordinates &&
        glm::length(strictPath.requestedEnd-raisedGoal)<1.e-5 && projectedPath.status==VansNavigationPathStatus::Complete &&
        projectedPath.endProjectionDistance>.01f,"Requested and projected endpoint acceptance share an implicit fallback")) return false;
    request.kind=VansNavigationQueryKind::PointProjection; request.start={-4,.6f,0};
    const auto projectionQuery=world.RequestQuery(request,"worker-project",error);
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    std::optional<VansNavigationQuerySnapshot> projectionResult;
    do
    {
        world.Update(1./60.); projectionResult=world.CaptureQuery(projectionQuery,"worker-project",error);
        if(projectionResult && projectionResult->lifecycle==VansNavigationQueryLifecycle::Finished) break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    } while(std::chrono::steady_clock::now()<deadline);
    if(!Expect(projectionResult && projectionResult->point && projectionResult->projectionDistance>0 &&
        projectionResult->iterations==0 && projectionResult->path.points.empty(),"Generic worker projection was converted into a fake path search")) return false;
    const auto asset=map->GetAsset();
    if(!map->Initialize(asset,settings,"Humanoid",error)) return Expect(false,error);
    if(!motor.Initialize(map,settings.query,"Default",error)) return Expect(false,error);
    if(!Expect(motor.AdoptCorridorRoute(result->route)==VansNavigationPathFailure::MeshChanged,
        "Motor accepted a worker route from a replaced Map")) return false;
    world.Shutdown();
    if(!Expect(world.CaptureQueryStats().inFlight==0,"World unload retained navigation worker work")) return false;
    std::cout<<"[NavigationWorkers] PASS: real JobSystem, nonblocking frame, in-flight cancellation capacity, owned Corridor transfer and Map lifetime\n";
    return true;
}

bool CheckStandaloneNavigationDriver(const std::shared_ptr<Vans::VansNavigationMap>& mesh,
	const Vans::VansNavigationSettings& settings, const Vans::VansNavigationGeometry& geometry)
{
	using namespace Vans;
	using namespace VansEngine;
	auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
	VansRuntimePhaseGuard workerCleanup([&](){jobs.Shutdown();});
	VANS_INIT_MAIN_THREAD();
	VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
	auto& physics = VansPhysicsSystem::GetInstance();
	if (!Expect(physics.Initialize(), "Standalone navigation physics initialization failed")) return false;
	VansRuntimePhaseGuard physicsCleanup([&]() { physics.Shutdown(); });
	VansCollisionLayerConfig previousLayers;
	auto& layers = VansCollisionLayerManager::Get();
	previousLayers.layerCount = layers.GetLayerCount();
	for (int i = 0; i < previousLayers.layerCount; ++i)
	{
		previousLayers.layerNames[i] = layers.GetLayerName(i);
		previousLayers.collisionMasks[i] = layers.GetCollisionMask(i);
	}
	VansRuntimePhaseGuard layerCleanup([&]() { layers.ApplyConfig(previousLayers); });
    VansCollisionLayerConfig fixtureLayers; fixtureLayers.ResetToDefaults(); fixtureLayers.layerCount=2;
    fixtureLayers.layerNames[1]="Environment"; fixtureLayers.collisionMasks[0]=3; fixtureLayers.collisionMasks[1]=3;
    layers.ApplyConfig(fixtureLayers);
	std::vector<std::uint32_t> transforms;
	VansRuntimePhaseGuard transformCleanup([&]() { for (auto id : transforms) VansTransformStore::Release(id); });
	const auto allocate = [&](const glm::vec3& position)
	{
		const auto id = VansTransformStore::Allocate();
		transforms.push_back(id);
		VansTransform value;
		value.m_Position = position;
		value.m_Rotation = glm::vec3(0.0f);
		value.m_Scale = glm::vec3(1.0f);
		VansTransformStore::Write(id, value);
		return id;
	};
	PhysicsNodeProperties box;
	box.enabled = true;
    box.layerName="Environment";
	box.boxExtents = { 5.0f, 0.1f, 5.0f };
	VansPhysicsNode floor;
	floor.Initialize(box, allocate({ 0.0f, -0.1f, 0.0f }));
	if (!Expect(floor.GetActorIdentity() != nullptr, "Navigation floor collider failed")) return false;
	box.boxExtents = { 0.5f, 1.5f, 2.0f };
	VansPhysicsNode wall;
	wall.Initialize(box, allocate({ 0.0f, 1.5f, 0.0f }));
	if (!Expect(wall.GetActorIdentity() != nullptr, "Navigation wall collider failed")) return false;
	CharControllerProperties properties;
	properties.m_Radius = 0.35f;
	properties.m_Height = 1.1f;
	properties.m_StepOffset = 0.25f;
	properties.m_ContactOffset = 0.02f;
	properties.m_PositionOffset = { 0.0f, 0.9f, 0.0f };
	VansCharacterControllerNode controller;
	const auto characterTransform = allocate({ -4.0f, 0.02f, 0.0f });
	if (!Expect(controller.Initialize(properties, characterTransform, { -4.0f, 0.92f, 0.0f }), "Standalone navigation CCT failed")) return false;
	controller.SeedGroundedContact(true);
	VansRuntimeWorld runtime;
	VansEntityCreateDesc entity;
	entity.name = "StandaloneNavigation";
	entity.stableGuid = VansAssetGuid::FromStableName("NavigationContract", entity.name).ToString();
	const auto owner = runtime.CreateEntity(entity);
	auto* ccts = runtime.GetOrRegisterStorage<VansRuntimeCharacterControllerComponent>(VansRuntimeComponentType_CharacterController);
	VansRuntimeCharacterControllerComponent cct;
	cct.controllerNode = &controller;
	ccts->Add(owner, cct, "NavigationContract.CCT");
	auto* agents = runtime.GetOrRegisterStorage<VansRuntimeNavigationAgentComponent>(VansRuntimeComponentType_NavigationAgent);
	VansRuntimeNavigationAgentComponent navigation;
	const auto guid = VansAssetGuid::FromStableName("NavigationContract", "StandaloneMesh");
    VansEntityCreateDesc surfaceDesc; surfaceDesc.stableGuid=VansAssetGuid::FromStableName("NavigationContract","SurfaceOwner").ToString();
    const auto surfaceOwner=runtime.CreateEntity(surfaceDesc);
    runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,0})},"Surface.Transform");
    auto surfaceConfig=ContractSurface(settings).surface; surfaceConfig.navigationMeshGuid=guid.ToString();
    runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationSurface,surfaceConfig,"NavigationContract.Surface");
    navigation.targetMoveThreshold = 0.0f;
	const auto navigationComponent=agents->Add(owner, navigation, "NavigationContract.Agent");
	VansAssetObjectRepository assets;
	std::string error;
	AddRuntimeCollision(runtime,floor,"Floor"); AddRuntimeCollision(runtime,wall,"Wall");
    std::shared_ptr<const VansNavigationMeshAsset> liveAsset;
    if(!Expect(BuildLiveContractNavigation(runtime,settings,liveAsset,error) &&
        assets.Publish<VansNavigationMeshAsset>(guid,VansAssetType::NavigationMesh,1u,liveAsset,{},error).IsValid(),error)) return false;
	VansSceneNavigationRuntime integration;
	if (!Expect(integration.Initialize(runtime, assets, settings, error), error)) return false;
    VansRuntimeNavigationSources provider; VansNavigationSourceSnapshot snapshot; std::uint64_t observation=0;
    if(!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error),error)) return false;
    if(!Expect(integration.Synchronize(snapshot,observation,error),error)) return false;
    auto& world = integration.World();
    {
        auto casingSettings = settings;
        for (auto& map : casingSettings.maps) map.updateMode = VansNavigationMapUpdateMode::Baked;
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error),error)) return false;
        const auto bakedObservation = observation;
        VansPhysicsNode casing;
        PhysicsNodeProperties casingProperties;
        casingProperties.enabled = true;
        casingProperties.bodyType = PhysicsBodyType::Dynamic;
        casingProperties.colliderType = PhysicsColliderType::Box;
        casingProperties.layerName = "Environment";
        casingProperties.boxExtents = {.02f,.02f,.02f};
        const auto casingTransform = allocate({2,3,0});
        casing.Initialize(casingProperties,casingTransform);
        if (!Expect(casing.GetActorIdentity()!=nullptr,"Implicit casing initialization failed")) return false;
        AddRuntimeCollision(runtime,casing,"ImplicitCasing");
        const auto casingOwner = runtime.Entities().FindByGuid(VansAssetGuid::FromStableName("NavigationRuntimeCollision","ImplicitCasing").ToString());
        const auto casingComponent = runtime.FindComponentOwnedBy(casingOwner,VansRuntimeComponentType_Physics);
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error) && observation == bakedObservation,
            "Implicit dynamic casing invalidated baked navigation")) return false;
        runtime.SetComponentEnabled(casingComponent,false);
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error) && observation == bakedObservation,
            "Casing expiry invalidated baked navigation")) return false;
        runtime.SetComponentEnabled(casingComponent,true);
        const auto declaredSource = runtime.AddComponent(casingOwner,VansRuntimeComponentType_NavigationSource,VansRuntimeNavigationSourceComponent{},"ImplicitCasing.Source");
        VansNavigationBuildInput rejected;
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error) && observation != bakedObservation &&
            !VansNavigationSourceCollector::Collect(snapshot,casingSettings,"World",rejected,error) && error.find("requires a Runtime") != std::string::npos,
            "Explicit unsupported dynamic source was hidden from baked freshness validation")) return false;
        runtime.SetComponentEnabled(casingComponent,false);
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error),error)) return false;
        const auto disabledDeclarationObservation = observation;
        auto* declared = runtime.GetOrRegisterStorage<VansRuntimeNavigationSourceComponent>(VansRuntimeComponentType_NavigationSource)->Get(declaredSource);
        declared->mode = VansNavigationSourceMode::Area;
        declared->area.clear();
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error) && observation != disabledDeclarationObservation &&
            !VansNavigationSourceCollector::Collect(snapshot,casingSettings,"World",rejected,error),
            "Disabled physical body hid an invalid explicit navigation declaration")) return false;
        runtime.RemoveComponent(declaredSource);
        runtime.DestroyEntity(casingOwner);
        if (!Expect(provider.Capture(runtime,casingSettings,{}, {},snapshot,observation,error) && observation == bakedObservation,
            "Removing an implicit casing changed baked navigation inputs")) return false;
        std::cout << "[NavigationInputObservation] PASS: implicit dynamic lifecycle ignored, explicit declarations validated\n";
    }
    if (!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error),error)) return false;
	if (!Expect(!runtime.FindStorage<VansRuntimeAIAgentComponent>(VansRuntimeComponentType_AIAgent) && world.HasAgent(owner),
		"Navigation driver requires an AIAgent")) return false;
	VansCharacterMotionSettings motionSettings;
	motionSettings.driveMode = VansLocomotionDriveMode::Capsule;
	const auto tick = [&]()
	{
		PaceNavigationWorkerFrame(world);
		world.Update(1.0 / 60.0);
		controller.PrepareLocomotion(1.0f / 60.0f, motionSettings);
		controller.ResolveLocomotion({}, glm::quat(1, 0, 0, 0), false, {}, motionSettings, glm::vec3(1));
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		controller.FlushMoveAndSync();
	};
	VansNavigationMoveOptions options;
	const auto request = world.RequestMove(owner, "Script.Driver", { 4.0f, 0.0f, 0.0f }, options, error);
    if (!Expect(request.IsValid(), error)) return false;
	if (!Expect(!world.RequestMove(owner, "AI.Other", {}, options, error).IsValid() && error == "OwnerConflict",
		"A second owner replaced an active movement request")) return false;
	for (int frame = 0; frame < 40; ++frame) tick();
    if (!Expect(world.Pause(request, "Script.Driver", error), error)) return false;
	for (int frame = 0; frame < 120; ++frame) tick();
	const auto pausedPosition = controller.GetPosition();
	for (int frame = 0; frame < 60; ++frame) tick();
	if (!Expect(glm::distance(pausedPosition, controller.GetPosition()) < 0.01f &&
		world.CaptureAgent(owner)->state == VansNavigationMoveState::Paused, "Pause advanced navigation or movement")) return false;
	if (!Expect(world.Resume(request, "Script.Driver", error), error)) return false;
	controller.AcquireGameplayMovementBlock();
	const auto blockedPosition = controller.GetPosition();
	for (int frame = 0; frame < 180; ++frame) tick();
	if (!Expect(glm::distance(blockedPosition, controller.GetPosition()) < 0.01f &&
		world.CaptureAgent(owner)->desiredSpeed == 0.0f, "Movement lock advanced navigation")) return false;
	controller.ReleaseGameplayMovementBlock();
	float maximumLateral = 0.0f;
	for (int frame = 0; frame < 900; ++frame)
	{
		tick();
		maximumLateral = (std::max)(maximumLateral, std::abs(controller.GetPosition().z));
		const auto state = world.CaptureAgent(owner);
		if (state->state == VansNavigationMoveState::Finished) break;
	}
	const auto completed = world.CaptureAgent(owner);
	std::cout << "[NavigationWorld] CCT endpoint=(" << controller.GetPosition().x << ',' << controller.GetPosition().y << ','
		<< controller.GetPosition().z << ") lateral=" << maximumLateral << " result=" << static_cast<int>(completed->result)
		<< " recovery=" << completed->recoveryCount << " path=" << completed->path.diagnostic << '\n';
	if (!Expect(completed->result == VansNavigationMoveResult::ReachedGoal && maximumLateral > 2.0f,
		"Standalone CCT did not follow its corridor around the real wall")) return false;
    agents->Get(navigationComponent)->targetMoveThreshold=.75f;
    if(!Expect(integration.Synchronize(snapshot,observation,error),error)) return false;
    const auto repairRequest=world.RequestMove(owner,"Script.Driver",{-4,0,0},options,error);
    for(int frame=0;frame<16 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Following;++frame) tick();
    if(!Expect(world.CaptureAgent(owner)->state==VansNavigationMoveState::Following,"Real CCT target-repair fixture never obtained its Corridor")) return false;
    const glm::vec3 smallGoal(-4,0,.2f);
    if(!Expect(world.UpdateGoal(repairRequest,"Script.Driver",smallGoal,{},error),error)) return false;
    tick(); const auto repairedSnapshot=world.CaptureAgent(owner); const auto repairedBudget=world.CaptureQueryStats();
    if(!Expect(repairedSnapshot->state==VansNavigationMoveState::Following && repairedSnapshot->request.serial==repairRequest.serial &&
        glm::distance(repairedSnapshot->path.requestedEnd,smallGoal)<1.e-5f && repairedBudget.queued==0 && repairedBudget.running==0 &&
        repairedBudget.iterationsLastFrame>0 && repairedBudget.iterationsLastFrame<=settings.query.maximumIterationsPerFrame &&
        repairedBudget.startsLastFrame<=settings.query.maximumPathStartsPerFrame,
        "Small moving target started another full query or escaped the shared frame budget")) return false;
    if(!Expect(world.Pause(repairRequest,"Script.Driver",error) &&
        world.UpdateGoal(repairRequest,"Script.Driver",{-4,0,.3f},{},error),error)) return false;
    tick();
    if(!Expect(world.CaptureAgent(owner)->state==VansNavigationMoveState::Paused &&
        glm::distance(world.CaptureAgent(owner)->path.requestedEnd,smallGoal)==0,
        "Paused target repair changed the persistent path")) return false;
    if(!Expect(world.Resume(repairRequest,"Script.Driver",error),error)) return false;
    tick();
    if(!Expect(std::abs(world.CaptureAgent(owner)->path.requestedEnd.z-.3f)<1.e-4f &&
        world.CaptureAgent(owner)->state==VansNavigationMoveState::Following,"Resumed CCT did not repair its latest target")) return false;
    std::vector<VansNavigationQueryHandle> previewQueries;
    VansNavigationQueryRequest preview; preview.kind=VansNavigationQueryKind::PointProjection; preview.start={-4,0,.3f};
    for(int slot=0;slot<settings.query.maximumPathStartsPerFrame;++slot)
    {
        const auto query=world.RequestQuery(preview,"TargetRepair.Budget",error);
        if(!Expect(query.IsValid(),error)) return false;
        previewQueries.push_back(query);
    }
    if(!Expect(world.UpdateGoal(repairRequest,"Script.Driver",{-4,0,.4f},{},error),error)) return false;
    tick();
    if(!Expect(world.CaptureAgent(owner)->state==VansNavigationMoveState::Following &&
        std::abs(world.CaptureAgent(owner)->path.requestedEnd.z-.4f)<1.e-4f &&
        world.CaptureQueryStats().startsLastFrame<=settings.query.maximumPathStartsPerFrame &&
        world.CaptureQueryStats().iterationsLastFrame<=settings.query.maximumIterationsPerFrame,
        "Local target repair consumed full-path start slots, replanned or exceeded the shared node budget")) return false;
    for(const auto query:previewQueries)
        if(!Expect(world.ReleaseQuery(query,"TargetRepair.Budget",error),error)) return false;
    for(int frame=0;frame<16 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Following;++frame) tick();
    if(!Expect(world.CaptureAgent(owner)->state==VansNavigationMoveState::Following &&
        std::abs(world.CaptureAgent(owner)->path.requestedEnd.z-.4f)<1.e-4f && world.Stop(repairRequest,"Script.Driver",error),
        "Local repair did not retain its current target and movement request")) return false;
    // 恢复原零阈值夹具，后续八帧失败与 64 次同目标抑制仍验证原配置。
    agents->Get(navigationComponent)->targetMoveThreshold=0;
    if(!Expect(integration.Synchronize(snapshot,observation,error),error)) return false;
    auto strictOptions=options; strictOptions.goalAcceptance=VansNavigationGoalAcceptance::RequestedPoint;
    strictOptions.goalProjectionToleranceMeters=.01f;
    const glm::vec3 strictGoal(4,completed->path.projectedEnd.y+.2f,0);
    const auto strictRequest=world.RequestMove(owner,"Script.Driver",strictGoal,strictOptions,error);
    if(!Expect(strictRequest.IsValid() && strictRequest.serial!=request.serial && world.Pause(strictRequest,"Script.Driver",error),
        "Strict goal policy reused a completed projected-goal request")) return false;
    strictOptions.goalProjectionToleranceMeters=.02f;
    const auto pausedPolicyChange=world.RequestMove(owner,"Script.Driver",strictGoal,strictOptions,error);
    if(!Expect(pausedPolicyChange.serial==strictRequest.serial && world.CaptureAgent(owner)->state==VansNavigationMoveState::Paused &&
        world.Resume(strictRequest,"Script.Driver",error),"Changing query policy resumed a paused motor or replaced its request identity")) return false;
    for(int frame=0;frame<32 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Finished;++frame) tick();
    if(!Expect(world.CaptureAgent(owner)->result==VansNavigationMoveResult::Failed &&
        world.CaptureAgent(owner)->path.failure==VansNavigationPathFailure::EndProjectionRejected,
        "Motor reported ReachedGoal for a requested endpoint outside its acceptance tolerance")) return false;
	const auto second = world.RequestMove(owner, "Script.Driver", { -4.0f, 0.0f, 0.0f }, options, error);
	if (!Expect(second.IsValid() && second.serial != request.serial &&
		!world.Stop(request, "Script.Driver", error) && world.Stop(second, "Script.Driver", error),
		"Replacement request reused an old handle or Stop failed")) return false;
	if (!Expect(world.CaptureAgent(owner)->result == VansNavigationMoveResult::Cancelled, "Stop did not publish Cancelled")) return false;
	integration.Shutdown();
	if (!Expect(integration.Initialize(runtime, assets, settings, error) &&
        integration.Synchronize(snapshot,observation,error) &&
		!integration.World().Resume(second, "Script.Driver", error), "Old request survived the Scene session restart")) return false;
    const glm::vec3 unreachableGoal(1.e6f, 0.0f, 0.0f);
    const auto failedRequest = world.RequestMove(owner, "Script.Driver", unreachableGoal, options, error);
    for(int frame=0;frame<8;++frame)
    {
        tick();
        const auto pending=world.CaptureAgent(owner);
        if(pending && pending->state==VansNavigationMoveState::Finished) break;
    }
    if (!Expect(failedRequest.IsValid() && world.CaptureAgent(owner) && world.CaptureAgent(owner)->result == VansNavigationMoveResult::Failed,
        "Outside target did not fail before checking bounded repeated requests")) return false;
    for (int repeat = 0; repeat < 64; ++repeat)
    {
        const auto repeated = world.RequestMove(owner, "Script.Driver", unreachableGoal, options, error);
        if (!Expect(repeated.serial == failedRequest.serial, "Zero target threshold restarted the same failed request")) return false;
        tick();
    }
    auto changedOptions = options;
    changedOptions.allowPartial = true;
    const auto changedRequest = world.RequestMove(owner, "Script.Driver", unreachableGoal, changedOptions, error);
    if (!Expect(changedRequest.serial != failedRequest.serial && world.Stop(changedRequest, "Script.Driver", error),
        "Changing request semantics retained an earlier failed result")) return false;
    const auto unchangedObservation=observation;
    const auto unchangedGeometry=snapshot.sources.front().geometry;
    if(!Expect(provider.Capture(runtime,settings,{}, {},snapshot,observation,error) && observation==unchangedObservation &&
        snapshot.sources.front().geometry==unchangedGeometry && integration.Synchronize(snapshot,observation,error),
        "Unchanged runtime sources allocate new collision snapshots or invalidate the baked input")) return false;
    auto changedSettings=settings; changedSettings.profiles.front().radiusMeters+=.1f;
    if(!Expect(integration.Reconfigure(changedSettings,error) &&
        provider.Capture(runtime,changedSettings,{}, {},snapshot,observation,error) &&
        !integration.Synchronize(snapshot,observation,error) && !world.HasAgent(owner),
        "Current Profile changes retained the initialization-time navigation settings")) return false;
    if(!Expect(integration.Reconfigure(settings,error) && provider.Capture(runtime,settings,{}, {},snapshot,observation,error) &&
        integration.Synchronize(snapshot,observation,error) && world.HasAgent(owner),
        "Restoring current settings did not recover the valid Map")) return false;
    const auto synchronize=[&](){return provider.Capture(runtime,settings,{}, {},snapshot,observation,error) &&
        integration.Synchronize(snapshot,observation,error);};
    const auto wallPose=VansTransformStore::Read(wall.GetTransformID());
    auto movedWall=wallPose; movedWall.m_Position.x+=.5f;
    VansTransformStore::Write(wall.GetTransformID(),movedWall); wall.SyncActorFromTransform();
    if(!Expect(!synchronize() && error.find("Stale")!=std::string::npos && !world.HasAgent(owner),
        "Moved actual Physics collision continues to drive a stale NavigationAgent")) return false;
    VansTransformStore::Write(wall.GetTransformID(),wallPose); wall.SyncActorFromTransform();
    if(!Expect(synchronize() && world.HasAgent(owner),"Restored physical input cannot recover its valid baked Map")) return false;
    wall.SetEnabled(false);
    if(!Expect(!synchronize() && !world.HasAgent(owner),"Disabled physical source retains stale navigation movement")) return false;
    wall.SetEnabled(true);
    if(!Expect(synchronize(),error)) return false;
    VansRuntimeNavigationModifierComponent modifier;
    const auto modifierHandle=runtime.AddComponent(surfaceOwner,VansRuntimeComponentType_NavigationModifier,modifier,"Contract.Modifier");
    if(!Expect(!synchronize() && error.find("Stale")!=std::string::npos && !world.HasAgent(owner),
        "A new Modifier is ignored by source freshness validation")) return false;
    runtime.RemoveComponent(modifierHandle);
    if(!Expect(synchronize(),error)) return false;
    std::cout<<"[NavigationFreshness] PASS: cached actual sources, collision movement/disable, Modifier change and valid input restoration\n";
    auto multiMapSettings=settings;
    auto otherMap=multiMapSettings.maps.front(); otherMap.id="Other";
    multiMapSettings.maps.push_back(std::move(otherMap));
    VansEntityCreateDesc otherSurfaceDesc;
    otherSurfaceDesc.stableGuid=VansAssetGuid::FromStableName("NavigationContract","OtherSurfaceOwner").ToString();
    const auto otherSurfaceOwner=runtime.CreateEntity(otherSurfaceDesc);
    runtime.AddComponent(otherSurfaceOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{allocate({0,0,0})},"OtherSurface.Transform");
    auto otherSurface=surfaceConfig; otherSurface.mapId="Other";
    const auto otherGuid=VansAssetGuid::FromStableName("NavigationContract","OtherMesh");
    otherSurface.navigationMeshGuid=otherGuid.ToString();
    const auto otherSurfaceComponent=runtime.AddComponent(otherSurfaceOwner,VansRuntimeComponentType_NavigationSurface,otherSurface,"NavigationContract.OtherSurface");
    VansNavigationBuildInput otherInput; std::shared_ptr<const VansNavigationMeshAsset> otherAsset;
    if(!Expect(provider.Capture(runtime,multiMapSettings,{}, {},snapshot,observation,error) &&
        VansNavigationSourceCollector::Collect(snapshot,multiMapSettings,"Other",otherInput,error) &&
        VansNavigationBuilder::Build(otherInput,multiMapSettings,"Scenes/RuntimeNavigationContract.json",nullptr,otherAsset,error) &&
        assets.Publish<VansNavigationMeshAsset>(otherGuid,VansAssetType::NavigationMesh,1u,otherAsset,{},error).IsValid() &&
        integration.Reconfigure(multiMapSettings,error) && integration.Synchronize(snapshot,observation,error),error)) return false;
    const auto queryMap=[&](const char* id)
    {
        VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::PointProjection;
        request.mapId=id; request.profileId="Humanoid"; request.start={-4,0,0};
        return world.RequestQuery(request,"FreshnessAudit",error);
    };
    for(const char* id:{"Other","World"})
    {
        const auto request=queryMap(id);
        if(!Expect(request.IsValid() && world.ReleaseQuery(request,"FreshnessAudit",error),"Fresh Map cannot service an independent query")) return false;
    }
    VansTransformStore::Write(wall.GetTransformID(),movedWall); wall.SyncActorFromTransform();
    if(!Expect(provider.Capture(runtime,multiMapSettings,{}, {},snapshot,observation,error) &&
        !integration.Synchronize(snapshot,observation,error),"Changed shared collision did not invalidate both Maps")) return false;
    for(const char* id:{"Other","World"})
        if(!Expect(!queryMap(id).IsValid() && error=="MapUnavailable",
            "An early Map failure retained another stale published Map for queries")) return false;
    if(!Expect(!world.HasAgent(owner),"An early Map failure retained a stale motor registration")) return false;
    VansTransformStore::Write(wall.GetTransformID(),wallPose); wall.SyncActorFromTransform();
    if(!Expect(provider.Capture(runtime,multiMapSettings,{}, {},snapshot,observation,error) &&
        integration.Synchronize(snapshot,observation,error) && world.HasAgent(owner),"Restored shared collision cannot recover both Maps")) return false;
    std::cout<<"[NavigationFreshness] PASS: all published Maps withdraw stale shared sources before an early failure\n";
    const auto pendingMove=world.RequestMove(owner,"LifetimeAudit",{-4,0,0},options,error);
    auto* currentOtherSurface=runtime.GetOrRegisterStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface)->Get(otherSurfaceComponent);
    if(!Expect(pendingMove.IsValid() && currentOtherSurface && runtime.SetComponentEnabled(navigationComponent,false),
        "Could not prepare a disabled Agent beside an unavailable Map")) return false;
    currentOtherSurface->navigationMeshGuid=VansAssetGuid::FromStableName("NavigationContract","UnavailableMesh").ToString();
    if(!Expect(provider.Capture(runtime,multiMapSettings,{}, {},snapshot,observation,error) &&
        !integration.Synchronize(snapshot,observation,error) && !world.HasAgent(owner),
        "An unrelated Map error retained a disabled Agent's old registration")) return false;
    const auto survivingQuery=queryMap("World");
    if(!Expect(survivingQuery.IsValid() && world.ReleaseQuery(survivingQuery,"FreshnessAudit",error),
        "Disabling an Agent withdrew another Map's valid independent query service")) return false;
    currentOtherSurface->navigationMeshGuid=otherGuid.ToString();
    if(!Expect(runtime.SetComponentEnabled(navigationComponent,true) &&
        provider.Capture(runtime,multiMapSettings,{}, {},snapshot,observation,error) &&
        integration.Synchronize(snapshot,observation,error) && world.HasAgent(owner) &&
        !world.UpdateGoal(pendingMove,"LifetimeAudit",{4,0,0},{},error),
        "Reenabled Agent retained an old request identity after failed synchronization")) return false;
    std::cout<<"[NavigationLifetime] PASS: Agent disable before unrelated Map failure, independent queries and fresh reenable identity\n";
    // 未烘焙的临时物理阻挡只用于验证实际 CCT 反馈；不作为 Tile 动态更新的证据。
    VansNavigationRequestHandle transientRequest;
    {
        box.bodyType=PhysicsBodyType::Kinematic; box.boxExtents={.3f,1.5f,5.0f};
        VansPhysicsNode transientBlocker;
        transientBlocker.Initialize(box,allocate({-2,1.5f,0}));
        if(!Expect(transientBlocker.GetActorIdentity()!=nullptr,"Transient CCT blocker was not created")) return false;
        controller.SetPosition({-4,.92f,0}); controller.SeedGroundedContact(true);
        transientRequest=world.RequestMove(owner,"Physics.Transient",{4,0,0},options,error);
        if(!Expect(transientRequest.IsValid(),error)) return false;
        for(int frame=0;frame<150;++frame) tick();
        if(!Expect(controller.GetPosition().x< -2.5f && world.CaptureAgent(owner)->desiredSpeed>0 &&
            world.CaptureAgent(owner)->state==VansNavigationMoveState::Following,
            "Transient physical obstruction did not actually block an active CCT request")) return false;
    }
    for(int frame=0;frame<900 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Finished;++frame) tick();
    const auto transient=world.CaptureAgent(owner);
    std::cout<<"[NavigationTransientCCT] state="<<static_cast<int>(transient->state)<<" result="<<static_cast<int>(transient->result)
        <<" recovery="<<transient->recoveryCount<<" failure="<<NavigationPathFailureName(transient->path.failure)
        <<" position="<<controller.GetPosition().x<<','<<controller.GetPosition().y<<','<<controller.GetPosition().z
        <<" serial="<<transient->request.serial<<'/'<<transientRequest.serial<<" path="<<transient->path.diagnostic<<'\n';
    if(!Expect(world.CaptureAgent(owner)->result==VansNavigationMoveResult::ReachedGoal &&
        world.CaptureAgent(owner)->request.serial==transientRequest.serial,
        "A transient obstruction lost the original movement request after removal")) return false;
    {
        box.bodyType=PhysicsBodyType::Kinematic; box.boxExtents={.3f,1.5f,5.0f};
        VansPhysicsNode temporaryBlocker;
        temporaryBlocker.Initialize(box,allocate({-2,1.5f,0}));
        if(!Expect(temporaryBlocker.GetActorIdentity()!=nullptr,"Temporary CCT blocker was not created")) return false;
        for(const float dt:{1.f/30.f,1.f/60.f,1.f/120.f})
        {
            controller.SetPosition({-4,.92f,0}); controller.SeedGroundedContact(true);
            const std::string blockedOwner="Physics.Stall."+std::to_string(dt);
            if(!Expect(world.RequestMove(owner,blockedOwner,{4,0,0},options,error).IsValid(),error)) return false;
            int frame=0;
            for(;frame<static_cast<int>(40.f/dt) && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Finished;++frame)
            {
                PaceNavigationWorkerFrame(world); world.Update(dt);
                controller.PrepareLocomotion(dt,motionSettings);
                controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motionSettings,glm::vec3(1));
                std::lock_guard<std::mutex> lock(physics.GetSimulationMutex()); controller.FlushMoveAndSync();
            }
            const auto blocked=world.CaptureAgent(owner);
            std::cout<<"[NavigationBlockedCCT] dt="<<dt<<" state="<<static_cast<int>(blocked->state)
                <<" result="<<static_cast<int>(blocked->result)<<" recovery="<<blocked->recoveryCount
                <<" elapsed="<<frame*dt
                <<" position="<<controller.GetPosition().x<<','<<controller.GetPosition().y<<','<<controller.GetPosition().z
                <<" path="<<blocked->path.diagnostic<<'\n';
            if(!Expect(blocked->result==VansNavigationMoveResult::Stuck && blocked->recoveryCount==4 &&
                blocked->desiredSpeed==0 && controller.GetPosition().x< -2.5f,
                "A real blocked CCT did not terminate within the recovery bound at 30/60/120 Hz")) return false;
        }
    }
    for(const float dt:{1.f/30.f,1.f/60.f,1.f/120.f})
    {
        controller.SetPosition({-4,.92f,0}); controller.SeedGroundedContact(true);
        if(!Expect(world.RequestMove(owner,"Physics.Unblocked."+std::to_string(dt),{4,0,0},options,error).IsValid(),error)) return false;
        for(int frame=0;frame<static_cast<int>(18.f/dt) && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Finished;++frame)
        {
            PaceNavigationWorkerFrame(world); world.Update(dt);
            controller.PrepareLocomotion(dt,motionSettings);
            controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motionSettings,glm::vec3(1));
            std::lock_guard<std::mutex> lock(physics.GetSimulationMutex()); controller.FlushMoveAndSync();
        }
        if(!Expect(world.CaptureAgent(owner)->result==VansNavigationMoveResult::ReachedGoal &&
            world.CaptureAgent(owner)->recoveryCount==0,"Removing the physical blocker retained a false stalled window at 30/60/120 Hz")) return false;
    }
    std::cout<<"[NavigationBlockedCCT] PASS: real obstruction at 30/60/120 Hz, bounded Stuck and successful request after removal\n";
    // 使用真实 Scene provider、worker、native 发布与 CCT，验证 Runtime 更新接入。
    auto runtimeSettings=multiMapSettings;
    for(auto& map:runtimeSettings.maps) map.updateMode=VansNavigationMapUpdateMode::Runtime;
    const auto synchronizeRuntime=[&]()
    {
        if(!provider.Capture(runtime,runtimeSettings,{}, {},snapshot,observation,error))
        { integration.WithdrawSources(); return false; }
        return integration.Synchronize(snapshot,observation,error);
    };
    if(!Expect(integration.Reconfigure(runtimeSettings,error) && !synchronizeRuntime() && !world.HasAgent(owner) &&
        error.find("Stale")!=std::string::npos,"Runtime mode accepted an asset baked without its required layer policy")) return false;
    for(const auto& map:runtimeSettings.maps)
    {
        VansNavigationBuildInput input; std::shared_ptr<const VansNavigationMeshAsset> cooked,decoded;
        std::string bytes;
        if(!VansNavigationSourceCollector::Collect(snapshot,runtimeSettings,map.id,input,error) ||
            !VansNavigationBuilder::Build(input,runtimeSettings,"Scenes/RuntimeNavigationContract.json",nullptr,cooked,error) ||
            !VansNavigationMeshCodec::Encode(*cooked,bytes,error) || !VansNavigationMeshCodec::Decode(bytes,decoded,error) ||
            !assets.Publish<VansNavigationMeshAsset>(map.id=="World"?guid:otherGuid,VansAssetType::NavigationMesh,2u,decoded,{},error).IsValid())
            return Expect(false,error);
    }
    if(!Expect(synchronizeRuntime() && world.HasAgent(owner) && integration.CaptureTileUpdateStats().startedBuilds==0,
        "Fresh Runtime cooked layers did not initialize without an unnecessary geometry rebuild")) return false;
    controller.SetPosition({-4,.92f,0}); controller.SeedGroundedContact(true);
    const auto runtimeMove=world.RequestMove(owner,"Runtime.Geometry",{4,0,0},options,error);
    for(int frame=0;frame<30 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Following;++frame) tick();
    if(!Expect(runtimeMove.IsValid() && world.CaptureAgent(owner)->state==VansNavigationMoveState::Following,
        "Runtime Scene fixture cannot establish its actual CCT Corridor")) return false;
    {
        std::atomic<bool> release{false}; std::atomic<int> entered{0};
        const auto occupy=[&](){++entered; while(!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        const auto gate=jobs.QueueJobGroup({occupy,occupy});
        VansRuntimePhaseGuard gateCleanup([&](){release=true; gate->Wait();});
        const auto gateDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(entered.load()<2 && std::chrono::steady_clock::now()<gateDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!Expect(entered.load()==2,"Runtime update fixture cannot occupy real workers")) return false;
        auto changed=wallPose; changed.m_Position.z+=4;
        VansTransformStore::Write(wall.GetTransformID(),changed); wall.SyncActorFromTransform();
        const auto started=std::chrono::steady_clock::now();
        if(!Expect(synchronizeRuntime() && world.HasAgent(owner) && integration.CaptureTileUpdateStats().activeBuilds>0,
            "Runtime source movement withdrew its Agent or failed to dispatch a CPU tile build")) return false;
        if(!Expect(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(100),
            "Scene synchronization waited for occupied worker threads")) return false;
        const auto heldPosition=controller.GetPosition();
        for(int frame=0;frame<30;++frame)
        {
            if(!Expect(synchronizeRuntime(),error)) return false;
            tick();
        }
        const auto waiting=world.CaptureAgent(owner);
        // CCT 保持既有制动，停止意图不会直接改写物理位置；制动结束后不能漂移。
        const auto stoppedPosition=controller.GetPosition();
        for(int frame=0;frame<30;++frame)
        {
            if(!Expect(synchronizeRuntime(),error)) return false;
            tick();
        }
        std::cout<<"[NavigationRuntimeScene] brakingDistance="<<glm::distance(heldPosition,stoppedPosition)
            <<" heldDistance="<<glm::distance(stoppedPosition,controller.GetPosition())<<'\n';
        if(!Expect(waiting && waiting->state==VansNavigationMoveState::WaitingForData && waiting->request.serial==runtimeMove.serial &&
            waiting->recoveryCount==0 && waiting->desiredSpeed==0 && glm::distance(stoppedPosition,controller.GetPosition())<.01f,
            "Dirty runtime cells advanced the real CCT, cancelled its request or entered stuck recovery")) return false;
        // 构建尚未执行时再次移动，旧 completion 只能提交仍匹配的 cell stamp。
        changed.m_Position.z=-4;
        VansTransformStore::Write(wall.GetTransformID(),changed); wall.SyncActorFromTransform();
        if(!Expect(synchronizeRuntime(),error)) return false;
    }
    const auto settleRuntime=[&]()
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do
        {
            if(!synchronizeRuntime()) return false;
            const auto stats=integration.CaptureTileUpdateStats();
            if(!stats.pendingMaps && !stats.activeBuilds && !stats.retiringBuilds) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(std::chrono::steady_clock::now()<deadline);
        error="Runtime tile update fixture did not settle"; return false;
    };
    if(!Expect(settleRuntime() && integration.CaptureTileUpdateStats().publishedCells>0,error)) return false;
    for(int frame=0;frame<900 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Finished;++frame) tick();
    if(!Expect(world.CaptureAgent(owner)->result==VansNavigationMoveResult::ReachedGoal &&
        world.CaptureAgent(owner)->request.serial==runtimeMove.serial && world.CaptureAgent(owner)->recoveryCount==0,
        "Actual CCT did not resume its original request after the latest geometry commit")) return false;
    wall.SetEnabled(false); floor.SetEnabled(false);
    if(!Expect(synchronizeRuntime() && settleRuntime() && world.HasAgent(owner),
        "Removing the final Runtime source retained stale native geometry or withdrew an enabled Surface")) return false;
    VansNavigationQueryRequest emptyProjection; emptyProjection.kind=VansNavigationQueryKind::PointProjection; emptyProjection.start={-4,0,0};
    const auto emptyQuery=world.RequestQuery(emptyProjection,"Runtime.Empty",error);
    std::optional<VansNavigationQuerySnapshot> emptyResult;
    for(int frame=0;frame<30;++frame)
    {
        PaceNavigationWorkerFrame(world); world.Update(1./60.);
        emptyResult=world.CaptureQuery(emptyQuery,"Runtime.Empty",error);
        if(emptyResult && emptyResult->lifecycle==VansNavigationQueryLifecycle::Finished) break;
    }
    if(!Expect(emptyResult && emptyResult->path.failure==VansNavigationPathFailure::StartOutsideMesh &&
        world.ReleaseQuery(emptyQuery,"Runtime.Empty",error),"Empty Runtime rebuild left an old native floor queryable")) return false;
    floor.SetEnabled(true); wall.SetEnabled(true);
    VansTransformStore::Write(wall.GetTransformID(),wallPose); wall.SyncActorFromTransform();
    if(!Expect(synchronizeRuntime() && settleRuntime(),error)) return false;
    {
        PhysicsNodeProperties dynamicProperties=wall.GetProperties(); dynamicProperties.bodyType=PhysicsBodyType::Kinematic;
        VansPhysicsNode dynamicSource; dynamicSource.Initialize(dynamicProperties,allocate({-2,1.5f,0}));
        if(!Expect(dynamicSource.HasActor(),"Explicit Runtime dynamic collision was not created")) return false;
        VansEntityCreateDesc dynamicDesc; dynamicDesc.stableGuid=VansAssetGuid::FromStableName("Runtime.Contract","Dynamic.Owner").ToString();
        const auto dynamicOwner=runtime.CreateEntity(dynamicDesc);
        runtime.AddComponent(dynamicOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{dynamicSource.GetTransformID()},"Runtime.Dynamic.Transform");
        runtime.AddComponent(dynamicOwner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&dynamicSource},"Runtime.Dynamic.Physics");
        runtime.AddComponent(dynamicOwner,VansRuntimeComponentType_NavigationSource,VansRuntimeNavigationSourceComponent{},"Runtime.Dynamic.Source");
        VansRuntimePhaseGuard dynamicCleanup([&](){runtime.DestroyEntity(dynamicOwner);});
        if(!Expect(synchronizeRuntime() && settleRuntime(),error)) return false;
        const auto found=std::find_if(snapshot.sources.begin(),snapshot.sources.end(),[](const auto& source){return source.id=="Runtime.Dynamic.Source";});
        if(!Expect(found!=snapshot.sources.end() && !found->isStatic && found->geometry && !found->geometry->Empty() &&
            found->hasExplicitSource,"Runtime explicit dynamic body has no frozen native collision geometry")) return false;
        auto bakedSettings=runtimeSettings; for(auto& map:bakedSettings.maps) map.updateMode=VansNavigationMapUpdateMode::Baked;
        VansRuntimeNavigationSources bakedProvider; VansNavigationSourceSnapshot bakedSnapshot; std::uint64_t bakedObservation=0;
        VansNavigationBuildInput rejectedInput;
        if(!Expect(bakedProvider.Capture(runtime,bakedSettings,{}, {},bakedSnapshot,bakedObservation,error) &&
            !VansNavigationSourceCollector::Collect(bakedSnapshot,bakedSettings,"World",rejectedInput,error) &&
            error.find("requires a Runtime")!=std::string::npos,"Baked mode silently accepted an explicit dynamic source")) return false;
        const auto projectDynamicPoint=[&]()
        {
            VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::PointProjection;
            request.start={-2,0,0}; request.nearestExtents={.2f,.5f,.2f};
            const auto query=world.RequestQuery(request,"Runtime.Dynamic.Query",error);
            std::optional<VansNavigationQuerySnapshot> result;
            for(int frame=0;frame<30;++frame)
            {
                PaceNavigationWorkerFrame(world); world.Update(1./60.); result=world.CaptureQuery(query,"Runtime.Dynamic.Query",error);
                if(result && result->lifecycle==VansNavigationQueryLifecycle::Finished) break;
            }
            world.ReleaseQuery(query,"Runtime.Dynamic.Query",error); return result;
        };
        const auto occupied=projectDynamicPoint();
        if(!Expect(occupied && !occupied->point && occupied->path.failure==VansNavigationPathFailure::StartOutsideMesh,
            "Explicit dynamic body did not change actual native walkable geometry")) return false;
        if(!Expect(runtime.DestroyEntity(dynamicOwner) && synchronizeRuntime() && settleRuntime(),error)) return false;
        const auto removed=projectDynamicPoint();
        if(!Expect(removed && removed->point && removed->path.failure==VansNavigationPathFailure::None,
            "Removing explicit dynamic source retained its native exclusion")) return false;
    }
    {
        const auto transformId=VansTransformStore::Allocate();
        VansTransform pose; pose.m_Position={-2,0,0}; pose.m_Rotation=glm::vec3(0); pose.m_Scale=glm::vec3(1);
        VansTransformStore::Write(transformId,pose);
        VansEntityCreateDesc desc; desc.stableGuid="Runtime.Carve.Owner"; desc.name="Runtime Carve";
        const auto obstacleOwner=runtime.CreateEntity(desc);
        VansRuntimePhaseGuard cleanup([&](){runtime.DestroyEntity(obstacleOwner); VansTransformStore::Release(transformId);});
        runtime.AddComponent(obstacleOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{transformId},"Runtime.Carve.Transform");
        VansRuntimeNavigationObstacleComponent obstacle; std::get<VansNavigationObstacleCylinder>(obstacle.shape).radiusMeters=.7f;
        const auto obstacleHandle=runtime.AddComponent(obstacleOwner,VansRuntimeComponentType_NavigationObstacle,obstacle,"Runtime.Carve.Shape");
        const auto projectObstacle=[&](float x)
        {
            VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::PointProjection;
            request.start={x,0,0}; request.nearestExtents={.1f,.5f,.1f};
            const auto query=world.RequestQuery(request,"Runtime.Carve.Query",error);
            std::optional<VansNavigationQuerySnapshot> result;
            for(int frame=0;frame<30;++frame)
            {
                PaceNavigationWorkerFrame(world); world.Update(1./60.); result=world.CaptureQuery(query,"Runtime.Carve.Query",error);
                if(result && result->lifecycle==VansNavigationQueryLifecycle::Finished) break;
            }
            world.ReleaseQuery(query,"Runtime.Carve.Query",error);
            return result && result->point && glm::length(glm::vec2(result->point->x-x,result->point->z))<=.1f;
        };
        if(!Expect(synchronizeRuntime() && settleRuntime() && !projectObstacle(-2),error.empty()?"Runtime NavigationObstacle did not Carve native geometry":error)) return false;
        VansNavigationObstacleRecord coexisting; coexisting.shape.id="Runtime.Coexisting.Obstacle";
        coexisting.shape.center={-4,0,0}; coexisting.shape.radius=.7f;
        if(!Expect(world.ReplaceObstacles("Coexisting.Owner",{coexisting},error) &&
            integration.Synchronize(snapshot,observation,error) && settleRuntime() && !projectObstacle(-2) && !projectObstacle(-4),
            "Scene component replacement discarded an independent World owner")) return false;
        if(!Expect(runtime.SetComponentEnabled(obstacleHandle,false) && synchronizeRuntime() && settleRuntime() &&
            projectObstacle(-2) && !projectObstacle(-4),"Component withdrawal removed another owner's native obstacle")) return false;
        if(!Expect(runtime.SetComponentEnabled(obstacleHandle,true) && synchronizeRuntime() && settleRuntime() &&
            !projectObstacle(-2) && !projectObstacle(-4),error)) return false;
        world.CancelOwner("Coexisting.Owner");
        if(!Expect(integration.Synchronize(snapshot,observation,error) && settleRuntime() && !projectObstacle(-2) && projectObstacle(-4),
            "Cancelling an external owner removed the component's native obstacle")) return false;
        pose.m_Position.x=-4; VansTransformStore::Write(transformId,pose);
        if(!Expect(synchronizeRuntime() && settleRuntime() && projectObstacle(-2) && !projectObstacle(-4),
            "Runtime obstacle movement failed to update old and new coverage")) return false;
        if(!Expect(runtime.SetComponentEnabled(obstacleHandle,false) && synchronizeRuntime() && settleRuntime() && projectObstacle(-4),
            "Disabling NavigationObstacle retained its native Carve")) return false;
        if(!Expect(runtime.SetComponentEnabled(obstacleHandle,true) && synchronizeRuntime() && settleRuntime() && !projectObstacle(-4),
            "Reenabling NavigationObstacle did not replay its current desired shape")) return false;
        {
            VansTransform physicalPose; physicalPose.m_Position={-2,0,0}; physicalPose.m_Rotation={0,33,0}; physicalPose.m_Scale={1.4f,1,.8f};
            const auto physicalTransform=allocate(physicalPose.m_Position); VansTransformStore::Write(physicalTransform,physicalPose);
            PhysicsNodeProperties properties; properties.bodyType=PhysicsBodyType::Dynamic; properties.enabled=true;
            properties.boxExtents={.65f,1,.55f}; properties.shapeOffset={.1f,1,.2f};
            VansPhysicsNode physicalBox; physicalBox.Initialize(properties,physicalTransform);
            const auto physicalOwner=runtime.CreateEntity({"Runtime.Collider.Owner","Bound Box",{},true});
            VansRuntimePhaseGuard physicalCleanup([&](){runtime.DestroyEntity(physicalOwner);});
            runtime.AddComponent(physicalOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{physicalTransform},"Runtime.Collider.Transform");
            const auto physicalComponent=runtime.AddComponent(physicalOwner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{&physicalBox},"Runtime.Collider.Physics");
            auto* obstacleStorage=runtime.FindStorage<VansRuntimeNavigationObstacleComponent>(VansRuntimeComponentType_NavigationObstacle);
            const VansNavigationObstacleCollider binding{"Runtime.Collider.Owner","Runtime.Collider.Physics"};
            obstacleStorage->Get(obstacleHandle)->shape=binding;
            pose.m_Position={100,0,100}; VansTransformStore::Write(transformId,pose);
            if(!Expect(synchronizeRuntime() && settleRuntime() && projectObstacle(-4) && !projectObstacle(-2),
                "Collider binding used the NavigationObstacle owner's Transform or ignored actual shape offset/scale")) return false;
            physicalPose.m_Position.x=-4; physicalPose.m_Rotation.y=77;
            if(!Expect(physicalBox.ResetMotion(physicalPose.m_Position,physicalPose.m_Rotation,{},{}),"Cannot move the actual bound dynamic actor")) return false;
            auto staleAuthoredPose=physicalPose; staleAuthoredPose.m_Position={100,0,100};
            VansTransformStore::Write(physicalTransform,staleAuthoredPose);
            if(!Expect(synchronizeRuntime() && settleRuntime() && projectObstacle(-2) && !projectObstacle(-4),
                "Collider used stale authored Transform instead of replacing old/new coverage from the actual actor")) return false;
            VansTransformStore::Write(physicalTransform,physicalPose);
            if(!Expect(runtime.SetComponentEnabled(physicalComponent,false) && synchronizeRuntime() && settleRuntime() && projectObstacle(-4),
                "Disabled Collider retained its bound Carve effect")) return false;
            if(!Expect(runtime.SetComponentEnabled(physicalComponent,true) && synchronizeRuntime() && settleRuntime() && !projectObstacle(-4),
                "Reenabled Collider did not restore its actual bound shape")) return false;
            obstacleStorage->Get(obstacleHandle)->shape=VansNavigationObstacleCollider{"Wrong.Owner",binding.componentGuid};
            if(!Expect(!synchronizeRuntime() && !world.HasAgent(owner) && error.find("incorrectly owned")!=std::string::npos,
                "Incorrectly owned Collider binding retained a stale published driver")) return false;
            obstacleStorage->Get(obstacleHandle)->shape=binding;
            if(!Expect(synchronizeRuntime() && settleRuntime() && !projectObstacle(-4),
                "Corrected Collider binding could not recover its desired shape")) return false;
            physicalPose.m_Rotation={20,0,0};
            if(!Expect(physicalBox.ResetMotion(physicalPose.m_Position,physicalPose.m_Rotation,{},{}),"Cannot tilt the actual bound actor")) return false;
            if(!Expect(!synchronizeRuntime() && !world.HasAgent(owner) && error.find("upright Box")!=std::string::npos,
                "Tilted Collider was silently approximated or retained its stale driver")) return false;
            physicalPose.m_Rotation={0,77,0};
            if(!Expect(physicalBox.ResetMotion(physicalPose.m_Position,physicalPose.m_Rotation,{},{}),"Cannot restore the actual bound actor")) return false;
            if(!Expect(synchronizeRuntime() && settleRuntime() && !projectObstacle(-4),error)) return false;
            if(!Expect(runtime.RemoveComponent(physicalComponent) && !synchronizeRuntime() && !world.HasAgent(owner),
                "Removed Collider binding retained its stale navigation driver")) return false;
            obstacleStorage->Get(obstacleHandle)->shape=obstacle.shape;
            pose.m_Position={-4,0,0}; VansTransformStore::Write(transformId,pose);
            if(!Expect(synchronizeRuntime() && settleRuntime() && !projectObstacle(-4),
                "Switching from an unavailable Collider to owned Cylinder did not restore navigation")) return false;
            std::cout<<"[NavigationColliderRuntime] PASS: actual native pose/offset/scale, independent owner, movement, disable/enable, stale/tilted binding withdrawal and recovery\n";
        }
        if(!Expect(runtime.DestroyEntity(obstacleOwner) && synchronizeRuntime() && settleRuntime() && projectObstacle(-4),
            "NavigationObstacle entity exit retained its native Carve")) return false;
        // 通用 owner 的命令必须进入同一 Scene/TileUpdater；来源 observation 没变化也需要发布。
        VansNavigationObstacleRecord external; external.shape.id="Runtime.Generic.Obstacle";
        external.shape.center={-2,0,0}; external.shape.radius=.7f;
        const auto unchangedObservation=observation;
        if(!Expect(world.ReplaceObstacles("Generic.Obstacle.Owner",{external},error) &&
            integration.Synchronize(snapshot,unchangedObservation,error) && settleRuntime() && !projectObstacle(-2),
            "Generic World obstacle did not reach the real updater with an unchanged source observation")) return false;
        external.shape.center.x=-4;
        if(!Expect(world.ReplaceObstacles("Generic.Obstacle.Owner",{external},error) &&
            integration.Synchronize(snapshot,unchangedObservation,error) && settleRuntime() && projectObstacle(-2) && !projectObstacle(-4),
            "Generic World obstacle did not publish both old/new coverage")) return false;
        const auto preservedRevision=world.GetCarveObstacleRevision();
        if(!Expect(!world.ReplaceObstacles("Other.Owner",{external},error) &&
            world.GetCarveObstacleRevision()==preservedRevision && !projectObstacle(-4),
            "Another owner replaced a live generic obstacle")) return false;
        if(!Expect(world.ReplaceObstacles("Generic.Obstacle.Owner",{},error) &&
            integration.Synchronize(snapshot,unchangedObservation,error) && settleRuntime() && projectObstacle(-4),
            "Replacing an owner with an empty set retained native Carve")) return false;
        if(!Expect(world.ReplaceObstacles("Generic.Obstacle.Owner",{external},error) &&
            integration.Synchronize(snapshot,unchangedObservation,error) && settleRuntime() && !projectObstacle(-4),error)) return false;
        world.CancelOwner("Generic.Obstacle.Owner");
        if(!Expect(integration.Synchronize(snapshot,unchangedObservation,error) && settleRuntime() && projectObstacle(-4) &&
            world.EstimateObstacleResidentBytes()==0,"Owner cancellation retained desired/native obstacle state or capacity")) return false;
        std::cout<<"[NavigationGenericObstacle] PASS: World owner commands, unchanged source observation, native movement/removal and owner cancellation\n";
        std::cout<<"[NavigationObstacle] PASS: real Scene component, Transform move, native projection, disable/enable and entity removal\n";
    }
    const auto runtimeSurface=runtime.FindStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface)->FindFirstOwnedBy(surfaceOwner);
    if(!Expect(runtime.SetComponentEnabled(runtimeSurface,false) && !synchronizeRuntime() && !world.HasAgent(owner) &&
        error.find("SurfaceUnavailable")!=std::string::npos,"Disabling the last Surface retained its Runtime driver")) return false;
    if(!Expect(runtime.SetComponentEnabled(runtimeSurface,true) && synchronizeRuntime() && settleRuntime() && world.HasAgent(owner),
        "Reenabling Runtime Surface could not restore current geometry")) return false;
    auto* surfaceStorage=runtime.FindStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface);
    auto* localSurface=surfaceStorage->Get(runtimeSurface);
    const auto originalSurface=*localSurface;
    const auto projectLocal=[&](float x)
    {
        VansNavigationQueryRequest request; request.kind=VansNavigationQueryKind::PointProjection;
        request.start={x,0,2}; request.nearestExtents={.1f,.5f,.1f};
        const auto handle=world.RequestQuery(request,"Runtime.SurfaceBoundary",error);
        std::optional<VansNavigationQuerySnapshot> result;
        for(int frame=0;frame<30;++frame)
        {
            PaceNavigationWorkerFrame(world); world.Update(1./60.);
            result=world.CaptureQuery(handle,"Runtime.SurfaceBoundary",error);
            if(result && result->lifecycle==VansNavigationQueryLifecycle::Finished) break;
        }
        if(!world.ReleaseQuery(handle,"Runtime.SurfaceBoundary",error)) return false;
        return result && result->point.has_value();
    };
    localSurface->boundsCenterMeters={2,0,0}; localSurface->boundsHalfExtentsMeters={3,5,5};
    if(!Expect(synchronizeRuntime() && settleRuntime() && !projectLocal(-4) && projectLocal(4),
        "Runtime Surface resize retained old outside geometry or lost its inside geometry: "+error)) return false;
    controller.SetPosition({0,.92f,2}); controller.SeedGroundedContact(true);
    const auto boundedMove=world.RequestMove(owner,"Runtime.SurfaceBoundary",{4,0,2},options,error);
    if(!Expect(boundedMove.IsValid(),error)) return false;
    for(int frame=0;frame<900 && world.CaptureAgent(owner)->state!=VansNavigationMoveState::Finished;++frame)
    {
        tick();
        const auto position=controller.GetPosition();
        if(!Expect(position.x>=-1 && position.x<=5 && position.z>=-5 && position.z<=5,
            "CCT navigation left its authored local Surface")) return false;
    }
    if(!Expect(world.CaptureAgent(owner)->result==VansNavigationMoveResult::ReachedGoal &&
        world.CaptureAgent(owner)->recoveryCount==0,"CCT could not reach a valid local-region goal")) return false;
    localSurface->boundsCenterMeters.x=-2;
    if(!Expect(synchronizeRuntime() && settleRuntime() && projectLocal(-4) && !projectLocal(4),
        "Runtime Surface center move did not replace both old and new bounds: "+error)) return false;
    *localSurface=originalSurface;
    if(!Expect(synchronizeRuntime() && settleRuntime() && projectLocal(-4) && projectLocal(4),
        "Restoring Runtime Surface failed to restore its current geometry: "+error)) return false;
    std::cout<<"[NavigationRuntimeSurfaceBoundary] PASS: real Scene/worker resize, center move, old/new native coverage, bounded CCT arrival and restoration\n";
    std::cout<<"[NavigationRuntimeScene] PASS: frozen source movement, occupied workers, latest cell stamps, real CCT wait/resume, empty last-source commit, explicit dynamic native add/remove and Surface lifecycle\n";
    integration.Shutdown();
    if(!CheckAvoidanceCCT(physics,settings)) return false;
    if(!CheckMovingObstacleCCT(physics,settings)) return false;
    if(!CheckNarrowPassageCCT(physics,settings)) return false;
    if (!CheckStandaloneLuaDriver(floor,wall,controller,physics,motionSettings)) return false;
    if(!CheckStandaloneLuaLinkDriver(physics)) return false;
	std::cout << "[NavigationWorld] PASS: standalone PhysX CCT, wall route, owner conflict, Pause/Resume, movement lock, Stop and session lifetime\n";
	return true;
}

struct DetourAllocationProbe
{
    std::array<void*,512> live{};
    std::size_t allocation = 0;
    std::size_t failAt = (std::numeric_limits<std::size_t>::max)();
    bool overflow = false;
    std::size_t LiveCount() const
    { return static_cast<std::size_t>(std::count_if(live.begin(),live.end(),[](const auto value){return value!=nullptr;})); }
};
thread_local DetourAllocationProbe* activeDetourAllocationProbe = nullptr;
void* AllocateDetourContractData(std::size_t size,dtAllocHint)
{
    auto* probe=activeDetourAllocationProbe;
    if(probe && probe->allocation++==probe->failAt) return nullptr;
    void* result=std::malloc(size);
    if(probe && result)
    {
        const auto slot=std::find(probe->live.begin(),probe->live.end(),nullptr);
        if(slot==probe->live.end()) { probe->overflow=true; std::free(result); return nullptr; }
        *slot=result;
    }
    return result;
}
void FreeDetourContractData(void* data)
{
    if(auto* probe=activeDetourAllocationProbe)
    {
        const auto slot=std::find(probe->live.begin(),probe->live.end(),data);
        if(slot!=probe->live.end()) *slot=nullptr;
    }
    std::free(data);
}
bool CheckNavigationCompressedLayers()
{
    using namespace Vans;
    VansNavigationTileCacheLayerCompressor compressor;
    for(const int size:{1,2,3,127,128,129,255,256,257,8192})
        for(int pattern=0;pattern<3;++pattern)
        {
            std::vector<unsigned char> raw(static_cast<std::size_t>(size));
            std::uint32_t random=0xa15739u;
            for(int i=0;i<size;++i)
            {
                random=random*1664525u+1013904223u;
                raw[i]=pattern==0?17u:pattern==1?static_cast<unsigned char>(random>>24):
                    static_cast<unsigned char>((i/3)%5);
            }
            const int bound=compressor.maxCompressedSize(size);
            std::vector<unsigned char> encoded(static_cast<std::size_t>(bound)+32u,0xa5),decoded(static_cast<std::size_t>(size)+32u,0x5a);
            int written=-1,read=-1;
            if(!Expect(!dtStatusFailed(compressor.compress(raw.data(),size,encoded.data(),bound,&written)) &&
                written>0 && written<=bound &&
                !dtStatusFailed(compressor.decompress(encoded.data(),written,decoded.data(),size,&read)) && read==size &&
                std::equal(raw.begin(),raw.end(),decoded.begin()) &&
                std::all_of(encoded.begin()+bound,encoded.end(),[](auto byte){return byte==0xa5;}) &&
                std::all_of(decoded.begin()+size,decoded.end(),[](auto byte){return byte==0x5a;}),
                "Layer codec exceeded guarded buffers or changed lossless data")) return false;
        }
    unsigned char source=3,target=0xa5; int written=77;
    const unsigned char missingRun[]{0x82},shortLiteral[]{0x03,17,18};
    if(!Expect(compressor.maxCompressedSize((std::numeric_limits<int>::max)())==0 &&
        dtStatusFailed(compressor.compress(&source,(std::numeric_limits<int>::max)(),&target,
            (std::numeric_limits<int>::max)(),&written)) && target==0xa5 && written==77 &&
        dtStatusFailed(compressor.decompress(missingRun,1,&target,1,&written)) &&
        dtStatusFailed(compressor.decompress(shortLiteral,3,&target,8,&written)) && target==0xa5 && written==77,
        "Malformed/overflow layer stream touched caller buffers")) return false;
    VansNavigationSettings settings; const auto bake=ContractBake(settings); const auto& areas=settings.areas; std::string error;
    const auto pixels=static_cast<std::size_t>(bake.tileSizeCells)*bake.tileSizeCells;
    std::vector<unsigned char> heights(pixels,0),encodedAreas(pixels,1),connections(pixels,0);
    const int offsetX[]{-1,0,1,0},offsetZ[]{0,1,0,-1};
    for(int z=0;z<bake.tileSizeCells;++z) for(int x=0;x<bake.tileSizeCells;++x)
        for(int direction=0;direction<4;++direction)
        {
            const int nx=x+offsetX[direction],nz=z+offsetZ[direction];
            if(nx>=0 && nz>=0 && nx<bake.tileSizeCells && nz<bake.tileSizeCells)
                connections[static_cast<std::size_t>(x+z*bake.tileSizeCells)]|=static_cast<unsigned char>(1u<<direction);
        }
    const auto encodeLayer=[&](int layer,const std::vector<unsigned char>& h,const std::vector<unsigned char>& a,
        const std::vector<unsigned char>& c)
    {
        dtTileCacheLayerHeader header{};
        header.magic=DT_TILECACHE_MAGIC; header.version=DT_TILECACHE_VERSION; header.tlayer=layer;
        header.hmin=header.hmax=static_cast<unsigned short>(layer*25);
        header.bmin[1]=header.bmax[1]=header.hmin*bake.cellHeight;
        header.bmax[0]=header.bmax[2]=bake.tileSizeCells*bake.cellSize;
        header.width=header.height=static_cast<unsigned char>(bake.tileSizeCells);
        header.maxx=header.maxy=static_cast<unsigned char>(bake.tileSizeCells-1);
        unsigned char* data=nullptr; int bytes=0;
        const auto status=dtBuildTileCacheLayer(&compressor,&header,h.data(),a.data(),c.data(),&data,&bytes);
        std::unique_ptr<unsigned char,decltype(&dtFree)> owner(data,&dtFree);
        VansNavigationTileCacheLayerPayload payload; payload.layer=layer;
        if(!dtStatusFailed(status) && data && bytes>0) payload.bytes.assign(data,data+bytes);
        return payload;
    };
    std::vector<VansNavigationTileCacheLayerPayload> layers{encodeLayer(0,heights,encodedAreas,connections),
        encodeLayer(1,heights,encodedAreas,connections)};
    constexpr std::uint64_t budget=8u*1024u*1024u;
    if(!ValidateNavigationTileCacheLayerSet(layers,bake,areas,budget,error)) return Expect(false,error);
    const auto mutateHeader=[](auto& layer,const auto& mutate)
    {
        dtTileCacheLayerHeader header{}; std::memcpy(&header,layer.bytes.data(),sizeof(header));
        mutate(header); std::memcpy(layer.bytes.data(),&header,sizeof(header));
    };
    const auto reject=[&](const auto& mutate)
    {
        auto invalid=layers; mutate(invalid);
        return !ValidateNavigationTileCacheLayerSet(invalid,bake,areas,budget,error) &&
            ValidateNavigationTileCacheLayerSet(layers,bake,areas,budget,error);
    };
    if(!Expect(
        reject([](auto& value){value.front().bytes.resize(8);}) &&
        reject([](auto& value){value.front().bytes.pop_back();}) &&
        reject([](auto& value){value.front().bytes.push_back(0x80);}) &&
        reject([](auto& value){value.back()=value.front();}) &&
        reject([&](auto& value){mutateHeader(value.front(),[](auto& h){h.tx=1;});}) &&
        reject([&](auto& value){mutateHeader(value.front(),[](auto& h){h.width=0;});}) &&
        reject([&](auto& value){mutateHeader(value.front(),[](auto& h){h.minx=1;});}) &&
        reject([&](auto& value){mutateHeader(value.front(),[](auto& h){h.hmax=255;});}) &&
        reject([&](auto& value){mutateHeader(value.front(),[](auto& h){h.bmax[1]=.3f;});}) &&
        reject([&](auto& value){mutateHeader(value.front(),[](auto& h){h.bmin[0]=(std::numeric_limits<float>::quiet_NaN)();});}),
        "Invalid layer layout, key, raster metadata or usable bounds were accepted")) return false;
    auto invalidAreas=encodedAreas; invalidAreas.front()=255;
    auto invalidHeights=heights; invalidHeights.front()=255;
    auto invalidConnections=connections; invalidConnections.front()|=1u;
    auto conflictingConnections=connections; conflictingConnections[1]=0x11;
    for(const auto& malformed:{encodeLayer(0,heights,invalidAreas,connections),
        encodeLayer(0,invalidHeights,encodedAreas,connections),encodeLayer(0,heights,encodedAreas,invalidConnections),
        encodeLayer(0,heights,encodedAreas,conflictingConnections)})
        if(!Expect(!ValidateNavigationTileCacheLayerSet({malformed},bake,areas,budget,error),
            "Invalid Area/height/connection reached the native layer consumer")) return false;
    std::vector<VansNavigationTileCacheLayerPayload> excessive;
    for(int layer=0;layer<9;++layer) excessive.push_back(encodeLayer(layer,heights,encodedAreas,connections));
    if(!Expect(!ValidateNavigationTileCacheLayerSet(excessive,bake,areas,budget,error) &&
        !ValidateNavigationTileCacheLayerSet(layers,bake,areas,1,error) &&
        ValidateNavigationTileCacheLayerSet(layers,bake,areas,budget,error),
        "Layer/native/scratch capacity rejection damaged valid layer bytes")) return false;
    std::cout<<"[NavigationLayerCodec] PASS: guarded lossless streams, integer capacity, malformed layout/raster/Area/neighbor rejection, nine layers and scratch budget\n";
    return true;
}

bool CheckNavigationAssetStorage(const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset,
    const Vans::VansNavigationSettings& settings);
bool CheckNavigationRuntimeAsset(const Vans::VansNavigationBuildInput& input,const Vans::VansNavigationSettings& settings,
    const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset)
{
    using namespace Vans;
    std::string error,bytes;
    auto bakedSettings=settings; bakedSettings.maps.front().updateMode=VansNavigationMapUpdateMode::Baked;
    std::shared_ptr<const VansNavigationMeshAsset> baked,decoded;
    if(!CheckNavigationAssetStorage(asset,settings)) return false;
    if(!VansNavigationBuilder::Build(input,bakedSettings,asset->GetSource().scene,nullptr,baked,error) ||
        !VansNavigationMeshCodec::Encode(*asset,bytes,error) || !VansNavigationMeshCodec::Decode(bytes,decoded,error)) return Expect(false,error);
    VansNavigationProfileBuildData expected,restored,bakedProfile;
    if(!Expect(asset->ReadProfile(asset->GetProfiles().front().profileId,expected,error) &&
        decoded->ReadProfile(decoded->GetProfiles().front().profileId,restored,error) &&
        baked->ReadProfile(baked->GetProfiles().front().profileId,bakedProfile,error),error)) return false;
    if(!Expect(expected.bake.buildTileCacheLayers && !expected.layerData.empty() &&
        restored.baseInputHash==expected.baseInputHash && restored.layerData.size()==expected.layerData.size() &&
        baked->GetProfiles().front().layerData.empty() && !baked->GetProfiles().front().bake.buildTileCacheLayers &&
        bakedProfile.tiles.size()==expected.tiles.size() &&
        std::equal(bakedProfile.tiles.begin(),bakedProfile.tiles.end(),expected.tiles.begin(),
            [](const auto& a,const auto& b){return a.bytes==b.bytes;}),
        "Runtime cooked layers changed Baked query bytes or were lost in serialization")) return false;
    for(std::size_t row=0;row<expected.layerData.size();++row)
        for(std::size_t layer=0;layer<expected.layerData[row].layers.size();++layer)
            if(!Expect(restored.layerData[row].layers[layer].bytes==expected.layerData[row].layers[layer].bytes,
                "Runtime compressed layer bytes changed in the owned round trip")) return false;
    const auto preserved=decoded;
    const auto reject=[&](std::string invalid)
    { return Expect(!VansNavigationMeshCodec::Decode(invalid,decoded,error) && decoded==preserved,
        "Malformed Runtime layer asset replaced its last valid owned value"); };
    std::size_t queryBytes=0; for(const auto& tile:expected.tiles) queryBytes+=tile.bytes.size();
    const auto layerOffset=bytes.size()-asset->GetPayloadBytes()+queryBytes;
    auto invalid=bytes; invalid[layerOffset]^=1u;
    if(!reject(invalid) || !reject(bytes.substr(0,bytes.size()-1)) || !reject(bytes+"x")) return false;
    invalid=bytes; const std::uint32_t nan=0x7fc00000u;
    std::memcpy(invalid.data()+layerOffset+offsetof(dtTileCacheLayerHeader,bmin),&nan,sizeof(nan));
    if(!reject(invalid)) return false;
    std::size_t cursor=8;
    const auto u32=[&](){std::uint32_t n=0;for(int shift=0;shift<32;shift+=8)n|=static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[cursor++]))<<shift;return n;};
    const auto skipString=[&](){const auto n=u32();cursor+=n;};
    skipString(); skipString(); const auto sourceHashOffset=cursor; cursor+=8; skipString(); const auto areaCount=u32();
    for(std::uint32_t area=0;area<areaCount;++area){skipString();cursor+=6;}
    const auto profileCountOffset=cursor;
    if(!Expect(u32()==1,"Runtime asset corruption fixture requires one Profile")) return false;
    skipString(); const auto bakeOffset=cursor; cursor+=82+24; const auto contributionCount=u32();
    for(std::uint32_t contribution=0;contribution<contributionCount;++contribution){skipString();cursor+=8;}
    const auto rowCount=u32(); const auto firstRow=cursor;
    invalid=bytes;
    const std::uint32_t excessiveLayers=9;
    std::memcpy(invalid.data()+firstRow+32,&excessiveLayers,sizeof(excessiveLayers));
    if(!reject(invalid)) return false;
    invalid=bytes;
    const std::uint64_t excessiveBytes=(std::numeric_limits<std::uint64_t>::max)();
    std::memcpy(invalid.data()+firstRow+40,&excessiveBytes,sizeof(excessiveBytes));
    if(!reject(invalid)) return false;
    invalid=bytes; invalid[bakeOffset+81]=0;
    if(!reject(invalid)) return false;
    cursor+=32; const auto layers=u32(); cursor+=layers*12u;
    if(rowCount>1){invalid=bytes; invalid.replace(cursor,8,bytes.substr(firstRow,8)); if(!reject(invalid)) return false;}
    {
        // 小 payload 的大型索引也会扩张为大量 vector；文件长度合法不能代替
        // 解码峰值准入。整个攻击样本只在内存中存在，拒绝后立即释放。
        constexpr std::uint32_t profiles=16,cells=65536;
        auto largeBake=expected.bake; largeBake.maximumTiles=cells; largeBake.maximumPolygonsPerTile=64;
        std::string bakeBytes=bytes.substr(bakeOffset,82);
        std::memcpy(bakeBytes.data()+69,&largeBake.maximumTiles,sizeof(std::uint32_t));
        std::memcpy(bakeBytes.data()+73,&largeBake.maximumPolygonsPerTile,sizeof(std::uint32_t));
        std::string row(36,'\0');
        const std::uint32_t count=8;
        std::memcpy(row.data()+32,&count,sizeof(count));
        const auto append=[](std::string& target,const auto& value)
        { target.append(reinterpret_cast<const char*>(&value),sizeof(value)); };
        for(std::uint32_t layer=0;layer<count;++layer){append(row,layer);append(row,std::uint64_t{1});}
        std::string rows; rows.reserve(cells*row.size());
        for(std::uint32_t cell=0;cell<cells;++cell){std::memcpy(row.data(),&cell,sizeof(cell));rows+=row;}
        std::string attack=bytes.substr(0,profileCountOffset);
        attack.reserve(attack.size()+profiles*(rows.size()+256u)+profiles*(cells*count+1u));
        append(attack,profiles);
        auto sourceHash=VANS_FNV1A64_OFFSET_BASIS;
        for(std::uint32_t profile=0;profile<profiles;++profile)
        {
            const auto id="Index"+std::to_string(profile);
            append(attack,static_cast<std::uint32_t>(id.size())); attack+=id; attack+=bakeBytes;
            append(attack,HashNavigationBakeSettings(largeBake)); append(attack,expected.inputHash); append(attack,expected.baseInputHash);
            append(attack,std::uint32_t{0}); append(attack,cells); attack+=rows;
            append(attack,std::uint32_t{1});
            append(attack,std::int32_t{0});append(attack,std::int32_t{0});append(attack,std::uint32_t{0});append(attack,std::uint64_t{1});
            sourceHash=ContinueMemoryFnv1a64(sourceHash,&expected.inputHash,sizeof(expected.inputHash));
        }
        std::memcpy(attack.data()+sourceHashOffset,&sourceHash,sizeof(sourceHash));
        attack.append(profiles*(cells*count+1u),'\0');
        if(!Expect(attack.size()<VansNavigationMeshCodec::maximumAssetBytes &&
            !VansNavigationMeshCodec::Decode(attack,decoded,error) && decoded==preserved &&
            error.find("before allocation")!=std::string::npos,
            "Oversized decoded Runtime index was not rejected before allocating its full owned representation")) return false;
    }
    std::cout<<"[NavigationRuntimeAsset] PASS: exact Baked query bytes, owned compressed layers, strict policy, malformed indices/raster and atomic decode\n";
    return true;
}

bool CheckNavigationLayerPublication(const Vans::VansNavigationProfileBuildInput& source,
    const Vans::VansNavigationSettings& settings,const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset)
{
    using namespace Vans;
    std::string error;
    const std::vector<VansNavigationTileCell> cells{{0,-1},{-1,0},{0,0}};
    VansNavigationTileBuildOutput candidate;
    const auto budget=static_cast<std::uint64_t>(settings.maps.front().maximumBuildBytes);
    if(!VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cells,0,budget,nullptr,candidate,error))
        return Expect(false,error);
    if(!Expect(candidate.layerData.size()==cells.size() && candidate.baseInputHash,
        "Owned runtime result did not export immutable base layer rows across grid rows")) return false;
    auto map=std::make_shared<VansNavigationMap>();
    VansNavigationTileUpdate request;
    if(!map->Initialize(asset,settings,source.profileId,error) || !map->InvalidateTiles(cells,error) ||
        !map->CaptureTileUpdate(request,error)) return Expect(false,error);
    std::uint64_t initialLayerBytes=0;
    VansNavigationProfileBuildData initialLayerData;
    if(!Expect(asset->ReadProfile(asset->GetProfiles().front().profileId,initialLayerData,error),error)) return false;
    for(const auto& row:initialLayerData.layerData) initialLayerBytes+=row.EstimateBytes()+96u;
    if(!Expect(map->ApplyTileUpdate(request,candidate,budget,error).state==VansNavigationTileCommitState::Applied &&
        map->EstimateResidentBytes()>=initialLayerBytes,"Map did not own/account published immutable layer data")) return false;
    if(!map->InvalidateTiles(cells,error) || !map->CaptureTileUpdate(request,error)) return Expect(false,error);
    std::vector<VansNavigationTileLayerData> seeds;
    if(!CaptureMapLayerSeeds(map,request,budget,seeds,error)) return Expect(false,error);
    if(!Expect(seeds.size()==cells.size() && seeds.front().layers.front().bytes==candidate.layerData.front().layers.front().bytes,
        "Production Map capture lost its published layers")) return false;
    std::vector<VansNavigationTileLayerData> bounded;
    if(!Expect(CaptureMapLayerSeeds(map,request,1,bounded,error) && bounded.empty(),
        "Layer capture exceeded its owned CPU reuse budget")) return false;
    auto invalid=candidate; ++invalid.layerData.front().baseInputHash;
    if(!Expect(map->ApplyTileUpdate(request,invalid,budget,error).state==VansNavigationTileCommitState::Rejected,
        "Mismatched compressed-layer fingerprint was published")) return false;
    DetourAllocationProbe probe; activeDetourAllocationProbe=&probe; probe.failAt=0;
    dtAllocSetCustom(&AllocateDetourContractData,&FreeDetourContractData);
    const auto failed=map->ApplyTileUpdate(request,candidate,budget,error);
    dtAllocSetCustom(nullptr,nullptr); activeDetourAllocationProbe=nullptr;
    if(!Expect(failed.state==VansNavigationTileCommitState::Rejected && !probe.LiveCount() && !probe.overflow,
        "Failed native candidate allocation published layers or retained data")) return false;
    std::vector<VansNavigationTileLayerData> retained;
    if(!CaptureMapLayerSeeds(map,request,budget,retained,error)) return Expect(false,error);
    if(!Expect(retained.size()==seeds.size() && retained.front().baseInputHash==seeds.front().baseInputHash &&
        retained.front().layers.front().bytes==seeds.front().layers.front().bytes && map->GetPendingCellCount()==cells.size(),
        "Rejected candidate changed layer data or cleared its pending input")) return false;
    // A new private worker context consumes owned rows, with no Map/native pointer.
    auto moved=source; moved.carveObstacles.front().center.x+=.25f;
    VansNavigationBuildContext coldContext(8u*1024u*1024u);
    VansNavigationTileBuildOutput cold,reused;
    if(!VansNavigationBuilder::BuildSelectedTiles(moved,settings,"World",cells,0,budget,nullptr,cold,error) ||
        !VansNavigationBuilder::BuildSelectedTiles(moved,settings,"World",cells,0,budget,nullptr,reused,error,&coldContext,&seeds))
        return Expect(false,error);
    if(!Expect(coldContext.CaptureStats().baseCellsBuilt==0 && coldContext.CaptureStats().reusedCells==cells.size() &&
        cold.tiles.size()==reused.tiles.size() && std::equal(cold.tiles.begin(),cold.tiles.end(),reused.tiles.begin(),
            [](const auto& a,const auto& b){return a.bytes==b.bytes;}),
        "Cold private worker did not reuse the Map's owned base layers exactly")) return false;
    // A newer source invalidates just one stamp; the valid subset still commits
    // both its native bytes and matching layer row in the same transaction.
    auto changed=moved; auto geometry=std::make_shared<VansNavigationGeometry>(*source.geometry);
    geometry->vertices[0]+=.25f; changed.geometry=geometry;
    VansNavigationTileBuildOutput changedOutput;
    VansNavigationBuildContext changedContext(8u*1024u*1024u);
    if(!VansNavigationBuilder::BuildSelectedTiles(changed,settings,"World",cells,0,budget,nullptr,changedOutput,error,&changedContext,&seeds))
        return Expect(false,error);
    if(!Expect(changedContext.CaptureStats().baseCellsBuilt==cells.size() && changedOutput.baseInputHash!=candidate.baseInputHash,
        "Changed base input reused old published compressed layers")) return false;
    if(!map->InvalidateTiles({cells[1]},error)) return Expect(false,error);
    const auto subset=map->ApplyTileUpdate(request,changedOutput,budget,error);
    if(!Expect(subset.state==VansNavigationTileCommitState::Applied && subset.appliedCells==cells.size()-1,
        "Valid native/layer subset was starved by a changed input stamp")) return false;
    if(!map->InvalidateTiles({cells[0],cells[2]},error) || !map->CaptureTileUpdate(request,error) ||
        !CaptureMapLayerSeeds(map,request,budget,retained,error)) return Expect(false,error);
    if(!Expect(retained.size()==cells.size() && retained[0].baseInputHash==changedOutput.baseInputHash &&
        retained[1].baseInputHash==candidate.baseInputHash,
        "Stale layer subset was published independently of its native cell")) return false;
    if(!Expect(map->ApplyTileUpdate(request,{},budget,error).state==VansNavigationTileCommitState::Applied,
        "Empty cell replacement could not atomically remove tiles and layers")) return false;
    if(!map->InvalidateTiles(cells,error) || !map->CaptureTileUpdate(request,error) ||
        !CaptureMapLayerSeeds(map,request,budget,retained,error)) return Expect(false,error);
    if(!Expect(retained.empty(),"Deleted cells retained obsolete published base layers")) return false;
    if(!map->Initialize(asset,settings,source.profileId,error) || !map->InvalidateTiles(cells,error) ||
        !map->CaptureTileUpdate(request,error) || !CaptureMapLayerSeeds(map,request,budget,retained,error)) return Expect(false,error);
    if(!Expect(retained.size()==cells.size() && retained.front().baseInputHash==asset->GetProfiles().front().baseInputHash,
        "Reinitialized Map did not restore its cooked base layers")) return false;
    std::cout<<"[NavigationLayerPublication] PASS: atomic native/layer commit, bounded capture, cold seed reuse, source mismatch, stale subset, native OOM and deletion\n";
    return true;
}

bool CheckNavigationCarveBackend()
{
    using namespace Vans;
    VansRuntimeNavigationObstacleComponent authored;
    std::string error; VansNavigationObstacleGeometry frozen;
    if(!Expect(VansSceneNavigationComponentReader::Read(VansComponentTypeCatalog::CreateDefaultData("NavigationObstacle"),authored,error) &&
        FreezeNavigationObstacleGeometry(authored.shape,glm::mat4(1),"Carve.Frozen",frozen,error),error)) return false;
    auto tilted=glm::rotate(glm::mat4(1),.3f,glm::vec3(1,0,0));
    auto elliptic=glm::scale(glm::mat4(1),glm::vec3(2,1,1));
    if(!Expect(!FreezeNavigationObstacleGeometry(authored.shape,tilted,"Rejected",frozen,error) && frozen.id=="Carve.Frozen" &&
        !FreezeNavigationObstacleGeometry(authored.shape,elliptic,"Rejected",frozen,error) && frozen.id=="Carve.Frozen",
        "Unsupported obstacle Transform was approximated or replaced the last valid frozen shape")) return false;
    authored.shape=VansNavigationObstacleBox{};
    const auto boxTransform=glm::scale(glm::rotate(glm::mat4(1),.3f,glm::vec3(0,1,0)),glm::vec3(2,1,1));
    if(!Expect(FreezeNavigationObstacleGeometry(authored.shape,boxTransform,"Carve.BoxFrozen",frozen,error),
        "Supported upright Box Transform was rejected")) return false;
    {
        auto data=VansComponentTypeCatalog::CreateDefaultData("NavigationObstacle");
        VansRuntimeNavigationObstacleComponent decoded;
        if(!Expect(VansSceneNavigationComponentReader::Read(data,decoded,error) && std::holds_alternative<VansNavigationObstacleCylinder>(decoded.shape),error)) return false;
        if(!Expect(decoded.mode==VansNavigationObstacleMode::Carve,"Default obstacle changed its Carve behavior")) return false;
        for(const auto& mode:kNavigationObstacleModes)
        {
            SetSerializedObjectField(data,"mode",VansSerializedValue::String(std::string(mode.second)));
            if(!Expect(VansSceneNavigationComponentReader::Read(data,decoded,error) && decoded.mode==mode.first,error)) return false;
        }
        auto missingMode=data; EraseSerializedObjectField(missingMode,"mode");
        auto unknownMode=data; SetSerializedObjectField(unknownMode,"mode",VansSerializedValue::String("Unknown"));
        if(!Expect(!VansSceneNavigationComponentReader::Read(missingMode,decoded,error) &&
            !VansSceneNavigationComponentReader::Read(unknownMode,decoded,error) && decoded.mode==VansNavigationObstacleMode::Both,
            "Missing/unknown obstacle mode was defaulted or changed valid output")) return false;
        SetSerializedObjectField(data,"halfExtentsMeters",VansSerializedValue::Array({VansSerializedValue::Float(1),VansSerializedValue::Float(1),VansSerializedValue::Float(1)}));
        if(!Expect(!VansSceneNavigationComponentReader::Read(data,decoded,error) && std::holds_alternative<VansNavigationObstacleCylinder>(decoded.shape),
            "Inactive/old parallel shape fields were accepted or changed the current typed variant")) return false;
        for(const auto* kind:{"Box","Collider"})
        {
            data=VansSceneNavigationComponentReader::CreateObstacleShapeData(kind);
            SetSerializedObjectField(data,"mode",VansSerializedValue::String("Carve"));
            SetSerializedObjectField(data,"mapId",VansSerializedValue::String("World"));
            SetSerializedObjectField(data,"profiles",VansSerializedValue::Array({VansSerializedValue::String("Humanoid")}));
            if(std::string(kind)=="Collider") SetSerializedObjectField(data,"collider",MakeSerializedSceneComponentObjectReference(
                VansAssetGuid::New().ToString(),VansAssetGuid::New().ToString(),"Physics"));
            if(!Expect(VansSceneNavigationComponentReader::Read(data,decoded,error),error)) return false;
        }
        auto malformed=data; SetSerializedObjectField(*FindObjectField(malformed,"collider"),"componentType",VansSerializedValue::String("NavigationSource"));
        if(!Expect(!VansSceneNavigationComponentReader::Read(malformed,decoded,error) && std::holds_alternative<VansNavigationObstacleCollider>(decoded.shape),
            "Collider binding accepted another component type or changed output on failure")) return false;
        SetSerializedObjectField(data,"centerMeters",VansSerializedValue::Array({VansSerializedValue::Float(0),VansSerializedValue::Float(0),VansSerializedValue::Float(0)}));
        if(!Expect(!VansSceneNavigationComponentReader::Read(data,decoded,error),"Collider binding accepted simultaneous self-shape fields")) return false;
        if(!Expect(!FreezeNavigationObstacleGeometry(decoded.shape,glm::mat4(1),"Unresolved.Collider",frozen,error) && frozen.id=="Carve.BoxFrozen",
            "NavigationCore tried to resolve a Collider binding or changed output")) return false;
        std::cout<<"[NavigationObstacleSchema] PASS: mutually exclusive current variants, typed Physics reference, old/inactive field rejection and atomic decode\n";
    }
    VansNavigationSettings settings; settings.maps.front().updateMode=VansNavigationMapUpdateMode::Runtime;
    VansNavigationSourceSnapshot snapshot; snapshot.collisionLayers={"Environment"};
    snapshot.surfaces={ContractSurface(settings)};
    auto geometry=std::make_shared<VansTriangleMeshData>();
    for(const float y:{0.f,5.f})
    {
        const auto offset=static_cast<std::uint32_t>(geometry->positions.size()/3);
        const float points[]{-12,y,-12,12,y,-12,12,y,12,-12,y,12};
        geometry->positions.insert(geometry->positions.end(),std::begin(points),std::end(points));
        for(const auto index:{0u,2u,1u,0u,3u,2u}) geometry->indices.push_back(offset+index);
    }
    VansNavigationSourceRecord floor; floor.id="Carve.Floors"; floor.ownerId="Carve.Owner";
    floor.geometry=geometry; floor.collisionLayer="Environment"; floor.boundsMinimum={-12,0,-12}; floor.boundsMaximum={12,5,12};
    snapshot.sources={floor};
    VansNavigationBuildInput input;
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    const auto build=[&]()
    {
        return VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error) &&
            VansNavigationBuilder::Build(input,settings,"CarveContract.json",nullptr,asset,error);
    };
    if(!Expect(build(),error)) return false;
    const auto baseline=asset;
    if(!CheckNavigationRuntimeAsset(input,settings,baseline)) return false;
    const auto project=[&](const glm::vec3& point)
    {
        auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query;
        if(!map->Initialize(asset,settings,"Humanoid",error) || !query.Initialize(map,settings.query,"Default",error)) return false;
        const auto projected=query.ProjectPoint(point,{.05f,.5f,.05f});
        // nearest extents select candidate polygons; a hole can still project to their distant boundary.
        return projected.valid && std::abs(projected.position.y-point.y)<=.5f &&
            glm::length(glm::vec2(projected.position.x-point.x,projected.position.z-point.z))<=.05f;
    };
    if(!Expect(project({0,0,0}) && project({0,5,0}),"Carve fixture does not have two independent walkable floors")) return false;
    VansNavigationObstacleRecord cylinder; cylinder.shape.id="Carve.Cylinder"; cylinder.shape.radius=1;
    VansNavigationObstacleRecord box; box.shape.id="Carve.Box"; box.shape.kind=VansNavigationObstacleGeometryKind::Box;
    box.shape.center={-5,1,0}; box.shape.halfExtents={.7f,1,2}; box.shape.yawRadians=.55f;
    {
        VansNavigationWorld obstacles;
        std::vector<VansNavigationObstacleRecord> captured;
        if(!Expect(obstacles.Initialize(settings,error) && obstacles.ReplaceObstacles("First.Owner",{cylinder,box},error) &&
            obstacles.CaptureObstacles(captured,error) && captured.size()==2 && captured.front().shape.id==box.shape.id &&
            obstacles.EstimateObstacleResidentBytes()>0,error)) return false;
        const auto revision=obstacles.GetCarveObstacleRevision();
        if(!Expect(obstacles.ReplaceObstacles("First.Owner",{box,cylinder},error) && obstacles.GetCarveObstacleRevision()==revision,
            "Reordered unchanged desired state generated a new obstacle revision")) return false;
        auto invalid=cylinder; invalid.profiles={"Unavailable"};
        if(!Expect(!obstacles.ReplaceObstacles("First.Owner",{box,invalid},error) &&
            !obstacles.ReplaceObstacles("First.Owner",{box,box},error) &&
            !obstacles.ReplaceObstacles("Other.Owner",{box},error) && obstacles.GetCarveObstacleRevision()==revision &&
            obstacles.CaptureObstacles(captured,error) && captured.size()==2,
            "Invalid/conflicting obstacle batches changed the last valid desired state")) return false;
        auto other=cylinder; other.shape.id="Independent.Owner.Obstacle";
        if(!Expect(obstacles.ReplaceObstacles("Other.Owner",{other},error) &&
            obstacles.ReplaceObstacles("First.Owner",{},error) && obstacles.CaptureObstacles(captured,error) &&
            captured.size()==1 && captured.front().shape.id==other.shape.id,"Owner replacement removed another owner's obstacle")) return false;
        std::vector<VansNavigationObstacleRecord> full(kMaximumNavigationObstacles,cylinder);
        for(std::size_t i=0;i<full.size();++i) full[i].shape.id="Capacity.Obstacle."+std::to_string(i);
        if(!Expect(!obstacles.ReplaceObstacles("Capacity.Owner",full,error) && captured.size()==1,
            "Aggregate obstacle count admitted more than the World capacity")) return false;
        obstacles.CancelOwner("Other.Owner");
        if(!Expect(obstacles.ReplaceObstacles("Capacity.Owner",full,error) && obstacles.CaptureObstacles(captured,error) &&
            captured.size()==full.size(),"World rejected its exact supported obstacle capacity")) return false;
        obstacles.CancelOwner("Capacity.Owner");
        if(!Expect(obstacles.CaptureObstacles(captured,error) && captured.empty() && obstacles.EstimateObstacleResidentBytes()==0,
            "Owner cancellation retained obstacle memory")) return false;
        auto budget=settings;
        budget.maximumResidentBytes=static_cast<int>(EstimateNavigationQueryBytes(budget.query)+EstimateNavigationAvoidanceBytes(budget.query.maximumActiveAgents,budget.avoidance));
        if(!Expect(obstacles.Initialize(budget,error) && !obstacles.ReplaceObstacles("Budget.Owner",{cylinder},error) &&
            obstacles.GetCarveObstacleRevision()==0 && obstacles.CaptureObstacles(captured,error) && captured.empty(),
            "Obstacle replacement allocated/published beyond the aggregate resident budget")) return false;
        auto bakedSettings=settings; bakedSettings.maps.front().updateMode=VansNavigationMapUpdateMode::Baked;
        if(!Expect(obstacles.Initialize(bakedSettings,error) && !obstacles.ReplaceObstacles("Baked.Owner",{cylinder},error),
            "World accepted enabled Carve on a Baked Map")) return false;
        cylinder.mode=VansNavigationObstacleMode::Avoidance;
        if(!Expect(obstacles.ReplaceObstacles("Baked.Owner",{cylinder},error),"Baked Map rejected an Avoidance-only obstacle")) return false;
        cylinder.mode=VansNavigationObstacleMode::Both;
        if(!Expect(!obstacles.ReplaceObstacles("Baked.Owner",{cylinder},error) && obstacles.CaptureObstacles(captured,error) &&
            captured.size()==1 && captured.front().mode==VansNavigationObstacleMode::Avoidance,
            "Baked Both rejection changed the valid Avoidance obstacle")) return false;
        cylinder.mode=VansNavigationObstacleMode::Carve;
        cylinder.enabled=false;
        if(!Expect(obstacles.ReplaceObstacles("Baked.Owner",{cylinder},error) && obstacles.CaptureObstacles(captured,error) &&
            captured.size()==1,error)) return false;
        cylinder.enabled=true;
        obstacles.Shutdown();
        if(!Expect(!obstacles.CaptureObstacles(captured,error) && captured.size()==1 && obstacles.EstimateObstacleResidentBytes()==0 &&
            obstacles.Initialize(settings,error) && obstacles.CaptureObstacles(captured,error) && captured.empty(),
            "Shutdown/reinitialization retained an obstacle session")) return false;
        std::cout<<"[NavigationObstacleWorld] PASS: atomic owner batches, deterministic no-op, conflicts, capacity/budget, Baked rejection and session cleanup\n";
    }
    snapshot.obstacles={cylinder,box};
    {
        VansNavigationBuildInput carved,avoiding,combined; std::uint64_t carveHash=0,bothHash=0;
        if(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",carved,error)) return Expect(false,error);
        snapshot.obstacles.front().mode=VansNavigationObstacleMode::Avoidance;
        if(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",avoiding,error)) return Expect(false,error);
        snapshot.obstacles.front().mode=VansNavigationObstacleMode::Both;
        if(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",combined,error)) return Expect(false,error);
        if(!Expect(carved.profiles.front().carveObstacles.size()==2 && avoiding.profiles.front().carveObstacles.size()==1 &&
            combined.profiles.front().carveObstacles.size()==2 &&
            ComputeNavigationBuildInputHash(carved.profiles.front(),carveHash,error) &&
            ComputeNavigationBuildInputHash(combined.profiles.front(),bothHash,error) && carveHash==bothHash,
            "Avoidance changed baked geometry or Both changed the existing Carve input hash")) return false;
        snapshot.obstacles.front().mode=VansNavigationObstacleMode::Carve;
    }
    if(!build()) return Expect(false,error);
    const bool cylinderLower=project({0,0,0}),boxLower=project({-5,0,0}),cylinderUpper=project({0,5,0}),boxUpper=project({-5,5,0});
    if(cylinderLower || boxLower || !cylinderUpper || !boxUpper)
        std::cerr<<"[NavigationCarve] lower="<<cylinderLower<<","<<boxLower<<" upper="<<cylinderUpper<<","<<boxUpper
            <<" shapes="<<input.profiles.front().carveObstacles.size()<<" tiles="<<asset->GetTileCount()<<" error="<<error<<'\n';
    if(!Expect(!cylinderLower && !boxLower && cylinderUpper && boxUpper,
        "Box/Cylinder Carve leaked across floors or left their centers walkable")) return false;
    if(!Expect(std::any_of(asset->GetProfiles().front().tiles.begin(),asset->GetProfiles().front().tiles.end(),
        [](const auto& tile){return tile.layer>0;}),"Runtime Carve did not use native TileCache layers")) return false;
    const auto oldHash=input.profiles.front().footprints.front().contentHash;
    snapshot.obstacles.front().shape.center.x=4;
    if(!Expect(build() && project({0,0,0}) && !project({4,0,0}) &&
        input.profiles.front().footprints.front().contentHash==oldHash,"Moving a Carve retained its old shape or changed unrelated coverage")) return false;
    snapshot.obstacles.clear();
    if(!Expect(build() && project({0,0,0}) && project({-5,0,0}),"Removing Carve did not restore source geometry")) return false;
    VansNavigationProfileBuildData beforeProfile,afterProfile;
    if(!Expect(baseline->ReadProfile(baseline->GetProfiles().front().profileId,beforeProfile,error) &&
        asset->ReadProfile(asset->GetProfiles().front().profileId,afterProfile,error),error)) return false;
    const auto& before=beforeProfile.tiles; const auto& after=afterProfile.tiles;
    if(!Expect(before.size()==after.size() && std::equal(before.begin(),before.end(),after.begin(),
        [](const auto& a,const auto& b){return a.x==b.x && a.z==b.z && a.layer==b.layer && a.bytes==b.bytes;}),
        "Removing all Carve changed the ordinary Recast build bytes")) return false;
    for(int count=0;count<65;++count)
    {
        auto obstacle=cylinder; obstacle.shape.id="Carve.Batch"+std::to_string(1000+count);
        snapshot.obstacles.push_back(std::move(obstacle));
    }
    if(!Expect(build() && !project({0,0,0}) && project({0,5,0}),"Native request batching lost an obstacle beyond the 64-entry queue")) return false;
    snapshot.obstacles={cylinder}; snapshot.obstacles.front().shape.radius=80;
    if(!build()) return Expect(false,error);
    if(!Expect(!project({0,0,0}) && !project({8,0,8}) && project({0,5,0}),
        "Oversized Carve was truncated instead of using local Recast")) return false;
    {
        rcContext context; rcCompactHeightfield compact;
        for(int axis=0;axis<3;++axis){compact.bmin[axis]=-1;compact.bmax[axis]=1;}
        std::vector<VansNavigationTilePayload> preserved{{77,88,0,{1,2,3}}};
        const auto result=BuildNavigationCarvedTileCache(context,compact,0,1,baseline->GetProfiles().front().bake,
            settings.areas,0,0,input.profiles.front().carveObstacles,input.profiles.front().links,0,nullptr,preserved,error);
        if(!Expect(result==VansNavigationTileCacheBuildResult::RecastRequired && preserved.front().x==77,
            "Unsupported Carve range consumed layer budget or modified output before selecting Recast")) return false;
    }
    auto baked=settings; baked.maps.front().updateMode=VansNavigationMapUpdateMode::Baked;
    VansNavigationBuildInput unchanged=input;
    if(!Expect(!VansNavigationSourceCollector::Collect(snapshot,baked,"World",unchanged,error) &&
        unchanged.profiles.front().carveObstacles.size()==input.profiles.front().carveObstacles.size(),"Baked Carve rejection modified caller input")) return false;
    auto tallGeometry=std::make_shared<VansTriangleMeshData>(*geometry);
    for(int level=2;level<9;++level)
    {
        const float y=level*5.f; const auto offset=static_cast<std::uint32_t>(tallGeometry->positions.size()/3);
        const float points[]{-12,y,-12,12,y,-12,12,y,12,-12,y,12};
        tallGeometry->positions.insert(tallGeometry->positions.end(),std::begin(points),std::end(points));
        for(const auto index:{0u,2u,1u,0u,3u,2u}) tallGeometry->indices.push_back(offset+index);
    }
    snapshot.sources.front().geometry=tallGeometry; snapshot.sources.front().boundsMaximum.y=40;
    snapshot.obstacles={cylinder};
    if(!build()) return Expect(false,error);
    if(!Expect(!project({0,0,0}) && project({0,5,0}) && project({0,40,0}) &&
        std::all_of(asset->GetProfiles().front().tiles.begin(),asset->GetProfiles().front().tiles.end(),[](const auto& tile){return tile.layer==0;}),
        "Nine-floor Carve was truncated by native touched-layer limits")) return false;
    snapshot.sources.front().geometry=geometry; snapshot.sources.front().boundsMaximum.y=5;
    if(!Expect(build(),error)) return false;
    VansNavigationProfileBuildData profile;
    if(!Expect(asset->ReadProfile(asset->GetProfiles().front().profileId,profile,error),error)) return false;
    std::vector<VansNavigationTileCell> cells{{profile.tiles.front().x,profile.tiles.front().z}};
    dtMeshHeader header{}; std::memcpy(&header,profile.tiles.front().bytes.data(),sizeof(header));
    VansNavigationTileBuildOutput retained; retained.tiles={{77,88,0,{1,2,3}}};
    if(!Expect(!VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cells,header.bmin[1],
        1,nullptr,retained,error) && retained.tiles.front().x==77 && retained.tiles.front().bytes==std::vector<unsigned char>({1,2,3}),
        "Carve budget failure published partial candidate data")) return false;
    // 同一个实际 cell 保留 compressed layers；所有增删移动与冷构建逐字节比较。
    VansNavigationBuildContext persistent(8u*1024u*1024u);
    const std::vector<VansNavigationTileCell> cacheCells{{0,0}};
    const auto compareCached=[&]()
    {
        if(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error)) return false;
        VansNavigationTileBuildOutput fresh,reused;
        if(!VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cacheCells,0,
            settings.maps.front().maximumBuildBytes,nullptr,fresh,error) ||
            !VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cacheCells,0,
            settings.maps.front().maximumBuildBytes,nullptr,reused,error,&persistent)) return false;
        return Expect(fresh.tiles.size()==reused.tiles.size() && std::equal(fresh.tiles.begin(),fresh.tiles.end(),reused.tiles.begin(),
            [](const auto& a,const auto& b){return a.x==b.x && a.z==b.z && a.layer==b.layer && a.bytes==b.bytes;}),
            "Persistent TileCache candidate differs from the fresh shared kernel");
    };
    snapshot.obstacles.clear();
    if(!Expect(compareCached(),error)) return false;
    auto cachedCylinder=cylinder; cachedCylinder.shape.center={2,0,2};
    snapshot.obstacles={cachedCylinder};
    if(!Expect(compareCached(),error)) return false;
    const auto initialCache=persistent.CaptureStats();
    if(!Expect(initialCache.baseCellsBuilt==1 && initialCache.retainedBytes>0 &&
        initialCache.retainedBytes<=8u*1024u*1024u,"Compressed layers were not retained within capacity")) return false;
    snapshot.obstacles.front().shape.center.x=3;
    if(!Expect(compareCached(),error)) return false;
    auto cachedBox=box; cachedBox.shape.center={5,1,2};
    snapshot.obstacles.push_back(cachedBox);
    if(!Expect(compareCached(),error)) return false;
    snapshot.obstacles.front().shape.center.z=3;
    snapshot.obstacles.back().shape.center.x=6;
    if(!Expect(compareCached(),error)) return false;
    snapshot.obstacles.pop_back();
    if(!Expect(compareCached(),error)) return false;
    snapshot.obstacles.clear();
    if(!Expect(compareCached(),error)) return false;
    for(int count=0;count<65;++count)
    {
        auto obstacle=cachedCylinder; obstacle.shape.id="Carve.CacheBatch"+std::to_string(1000+count);
        snapshot.obstacles.push_back(std::move(obstacle));
    }
    if(!Expect(compareCached(),error)) return false;
    snapshot.obstacles={cachedCylinder};
    if(!Expect(compareCached(),error)) return false;
    if(!Expect(persistent.CaptureStats().baseCellsBuilt==initialCache.baseCellsBuilt &&
        persistent.CaptureStats().reusedCells>=7,"Obstacle changes rerasterized valid base layers")) return false;
    // 取消、不可用预算不修改 caller；同一 context 仍能重放最新期望状态。
    VansNavigationBuildControl cancelled; cancelled.Cancel();
    VansNavigationTileBuildOutput unchangedCandidate; unchangedCandidate.tiles={{77,88,0,{1,2,3}}};
    if(!Expect(!VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cacheCells,0,
        settings.maps.front().maximumBuildBytes,&cancelled,unchangedCandidate,error,&persistent) &&
        unchangedCandidate.tiles.front().x==77 &&
        !VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cacheCells,0,
            1,nullptr,unchangedCandidate,error,&persistent) && unchangedCandidate.tiles.front().x==77 &&
        compareCached(),"Cached cancellation/budget failure published data or poisoned context")) return false;
    // 实际 geometry 改动必须替换 base cache，并重放当前障碍。
    auto changedGeometry=std::make_shared<VansTriangleMeshData>(*geometry);
    changedGeometry->positions[0]+=.25f; snapshot.sources.front().geometry=changedGeometry;
    if(!Expect(compareCached() && persistent.CaptureStats().baseCellsBuilt==initialCache.baseCellsBuilt+1,
        "Changed source reused obsolete compressed layers")) return false;
    snapshot.sources.front().geometry=geometry;
    if(!Expect(compareCached(),error)) return false;
    if(!CheckNavigationLayerPublication(input.profiles.front(),settings,baseline)) return false;
    VansNavigationBuildContext tiny(1);
    VansNavigationTileBuildOutput tinyResult;
    if(!Expect(VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cacheCells,0,
        settings.maps.front().maximumBuildBytes,nullptr,tinyResult,error,&tiny) && tiny.CaptureStats().evictedCells==0 &&
        tiny.CaptureStats().retainedBytes<persistent.CaptureStats().retainedBytes,
        "Insufficient cache capacity changed build behavior or retained a cell")) return false;
    // LRU 驻留超限只驱逐计算缓存；所需 cell 下次按同一内核恢复。
    const auto cacheCapacity=persistent.CaptureStats().retainedBytes+2048;
    VansNavigationBuildContext limited(cacheCapacity);
    VansNavigationTileBuildOutput limitedResult;
    for(const auto& cell:std::vector<VansNavigationTileCell>{{0,0},{-1,0},{0,-1},{-1,-1},{0,0}})
        if(!VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",{cell},0,
            settings.maps.front().maximumBuildBytes,nullptr,limitedResult,error,&limited)) return Expect(false,error);
    if(!Expect(limited.CaptureStats().retainedBytes<=cacheCapacity && limited.CaptureStats().evictedCells>0,
        "Persistent cell LRU exceeded retention capacity or did not evict")) return false;
    std::uint64_t baseHash=0; VansNavigationBakeSettings contextBake;
    if(!ComputeNavigationBuildBaseHash(input.profiles.front(),baseHash,error) ||
        !ResolveNavigationBakeSettings(settings,"World","Humanoid",contextBake,error) ||
        !VansNavigationBuildContextAccess::Begin(persistent,baseHash,HashNavigationBakeSettings(contextBake),0,error)) return false;
    unchangedCandidate={}; unchangedCandidate.tiles={{77,88,0,{1,2,3}}};
    const bool rejectedLease=!VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cacheCells,0,
        settings.maps.front().maximumBuildBytes,nullptr,unchangedCandidate,error,&persistent);
    VansNavigationBuildContextAccess::End(persistent);
    if(!Expect(rejectedLease && unchangedCandidate.tiles.front().x==77 && compareCached(),
        "A second worker entered the same native context or lost its lease")) return false;
    std::cout<<"[NavigationPersistentCache] PASS: native cold/reuse byte equivalence, move/add/remove, 65 removal requests, exact base restore, geometry replay and bounded retention\n";
    {
        const auto fileIdentity=VansAssetGuid::New().ToString();
        const auto filePath=std::filesystem::temp_directory_path()/("ForestNavigationWorkerLayers-"+fileIdentity+".vnavmesh");
        ScopedFileRemoval fileRemoval(filePath);
        std::shared_ptr<const VansNavigationMeshAsset> fileAsset;
        if(!VansNavigationMeshStorage::Save(filePath,*baseline,error) ||
            !VansNavigationMeshStorage::Load(filePath,fileAsset,error)) return Expect(false,error);
        auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
        VansRuntimePhaseGuard jobsCleanup([&](){jobs.Shutdown();});
        auto map=std::make_shared<VansNavigationMap>();
        VansNavigationTileUpdater updater;
        if(!map->Initialize(fileAsset,settings,"Humanoid",error) || !updater.Configure(settings,error)) return Expect(false,error);
        const auto observe=[&]()
        {
            if(!VansNavigationSourceCollector::Collect(snapshot,settings,"World",input,error)) return false;
            auto source=std::make_shared<const VansNavigationProfileBuildInput>(input.profiles.front());
            std::uint64_t hash=0;
            return ComputeNavigationBuildInputHash(*source,hash,error) && updater.Observe(map,std::move(source),hash,error);
        };
        const auto settle=[&]()
        {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(map->GetPendingCellCount() && std::chrono::steady_clock::now()<deadline)
            {
                if(!updater.Tick(fileAsset->EstimateResidentBytes()+map->EstimateResidentBytes(),map->GetResidentTileCount(),error)) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return map->GetPendingCellCount()==0;
        };
        std::promise<void> unblock;
        const auto gate=unblock.get_future().share(); std::atomic<int> entered{0};
        const auto block=[&](){++entered; gate.wait();};
        const auto blockers=jobs.QueueJobGroup({block,block});
        bool opened=false;
        const auto open=[&](){if(!opened) {opened=true;unblock.set_value();}};
        VansRuntimePhaseGuard releaseGate([&](){open();blockers->Wait();});
        const auto gateDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(entered.load()!=2 && std::chrono::steady_clock::now()<gateDeadline) std::this_thread::yield();
        if(!Expect(entered.load()==2,"Layer read fixture cannot occupy its actual workers")) return false;
        VansIOAudit::Reset();
        snapshot.obstacles.clear();
        if(!observe()) return Expect(false,error);
        snapshot.obstacles={cachedCylinder};
        if(!observe()) return Expect(false,error);
        const auto scheduledAt=std::chrono::steady_clock::now();
        if(!Expect(updater.Tick(fileAsset->EstimateResidentBytes()+map->EstimateResidentBytes(),map->GetResidentTileCount(),error) &&
            updater.CaptureStats().activeBuilds==1 && VansIOAudit::Snapshot().empty() &&
            std::chrono::steady_clock::now()-scheduledAt<std::chrono::milliseconds(100),
            "Layer capture/scheduling read a file on the owner thread or waited for occupied workers: "+error)) return false;
        open(); blockers->Wait();
        if(!settle()) return Expect(false,error);
        const auto cold=updater.CaptureStats();
        const auto coldReads=VansIOAudit::Snapshot();
        if(!Expect(coldReads.empty(),
            "Runtime local seed worker performed disk I/O after asset publication")) return false;
        snapshot.obstacles.front().shape.center.x+=.25f;
        if(!observe() || !settle()) return Expect(false,error);
        const auto warm=updater.CaptureStats();
        if(!Expect(cold.reusedCells>0 && cold.cachedContextBytes>0 && warm.baseCellsBuilt==cold.baseCellsBuilt &&
            warm.reusedCells>cold.reusedCells && warm.publishedCells>cold.publishedCells &&
            warm.retainedBytes<static_cast<std::uint64_t>(settings.maximumResidentBytes),
            "Production TileUpdater did not reuse layers or publish/account the warm candidate")) return false;
        updater.RetainMaps({});
        if(!Expect(updater.CaptureStats().cachedContextBytes==0,"Map withdrawal retained its idle native context")) return false;
        snapshot.obstacles.front().shape.center.x+=.25f;
        if(!observe() || !settle()) return Expect(false,error);
        const auto reseeded=updater.CaptureStats();
        if(!Expect(reseeded.reusedCells>0 && reseeded.cachedContextBytes>0,
            "New production session did not seed its worker from committed Map layers")) return false;
        updater.RetainMaps({});
        if(!Expect(updater.CaptureStats().cachedContextBytes==0,"Reseeded Map withdrawal retained CPU context")) return false;
        std::promise<void> retireUnblock;
        const auto retireGate=retireUnblock.get_future().share(); std::atomic<int> retiringEntered{0};
        const auto retireBlock=[&](){++retiringEntered;retireGate.wait();};
        const auto retireBlockers=jobs.QueueJobGroup({retireBlock,retireBlock});
        bool retireOpened=false;
        const auto openRetiring=[&](){if(!retireOpened) {retireOpened=true;retireUnblock.set_value();}};
        VansRuntimePhaseGuard releaseRetiring([&](){openRetiring();retireBlockers->Wait();});
        const auto retiringDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(retiringEntered.load()!=2 && std::chrono::steady_clock::now()<retiringDeadline) std::this_thread::yield();
        if(!Expect(retiringEntered.load()==2,"Layer retirement fixture cannot occupy workers")) return false;
        snapshot.obstacles.front().shape.center.x+=.25f;
        VansIOAudit::Reset();
        if(!observe() || !updater.Tick(fileAsset->EstimateResidentBytes()+map->EstimateResidentBytes(),map->GetResidentTileCount(),error))
            return Expect(false,error);
        std::weak_ptr<const VansNavigationMeshAsset> retiredAsset=fileAsset;
        std::weak_ptr<VansNavigationMap> retiredMap=map;
        updater.RetainMaps({}); map.reset(); fileAsset.reset();
        const auto retiring=updater.CaptureStats();
        if(!Expect(retiredMap.expired() && !retiredAsset.expired() && retiring.retiringBuilds==1 &&
            retiring.concurrentBuildBytes>0 && retiring.retainedBytes>=baseline->EstimateResidentBytes(),
            "Cancelled build retained a native Map or dropped the owned asset/reservation before its worker exited")) return false;
        openRetiring(); retireBlockers->Wait();
        const auto completedDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!retiredAsset.expired() && std::chrono::steady_clock::now()<completedDeadline)
        {if(!updater.Tick(0,0,error)) return Expect(false,error);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(!Expect(retiredAsset.expired() && !updater.CaptureStats().retiringBuilds && VansIOAudit::Snapshot().empty(),
            "Cancelled queued build accessed disk or failed to release its owned source after completion")) return false;
        if(!VansNavigationMeshStorage::Load(filePath,fileAsset,error)) return Expect(false,error);
        map=std::make_shared<VansNavigationMap>();
        if(!map->Initialize(fileAsset,settings,"Humanoid",error)) return Expect(false,error);
        // 冷加载完成后删除作者文件，局部构建仍只消费拥有型 CPU 数据。
        std::error_code removalError;
        if(!std::filesystem::remove(filePath,removalError) || removalError)
            return Expect(false,"Owned asset fixture cannot remove its source file");
        snapshot.obstacles.front().shape.center.x+=.25f;
        VansIOAudit::Reset();
        if(!observe() || !settle() || !Expect(map->IsReady() && VansIOAudit::Snapshot().empty(),
            "Published local asset depended on a deleted source file")) return Expect(false,error);
        updater.RetainMaps({});
        std::cout<<"[NavigationRuntimeCache] PASS: owned CPU seed, zero runtime I/O/nonblocking scheduling, warm reuse, stamped publication, cancelled worker lifetime and source-file independence\n";
    }
    // Every native allocation in a selected Carve must unwind safely, including cache init OOM.
    DetourAllocationProbe probe; activeDetourAllocationProbe=&probe;
    dtAllocSetCustom(&AllocateDetourContractData,&FreeDetourContractData);
    VansRuntimePhaseGuard restoreAllocator([](){dtAllocSetCustom(nullptr,nullptr); activeDetourAllocationProbe=nullptr;});
    retained.tiles.clear();
    if(!Expect(VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cells,header.bmin[1],
        settings.maps.front().maximumBuildBytes,nullptr,retained,error),error)) return false;
    const auto allocationCount=probe.allocation;
    if(!Expect(!probe.LiveCount() && !probe.overflow && allocationCount>10,"Native Carve allocations escaped their worker context")) return false;
    for(std::size_t failure=0;failure<allocationCount;++failure)
    {
        probe.allocation=0; probe.failAt=failure; retained={}; retained.tiles={{77,88,0,{1,2,3}}};
        const bool built=VansNavigationBuilder::BuildSelectedTiles(input.profiles.front(),settings,"World",cells,header.bmin[1],
            settings.maps.front().maximumBuildBytes,nullptr,retained,error);
        if(!Expect(!built && retained.tiles.front().x==77 && !probe.LiveCount() && !probe.overflow,
            "Native Carve allocation failure leaked memory or published a partial candidate")) return false;
    }
    // 对已有 layers 的 native 增量更新逐点注入 OOM；失败后重放同一最新状态。
    std::size_t warmAllocationCount=0;
    const auto checkWarmFailure=[&](std::size_t failure,bool measure)
    {
        probe.failAt=(std::numeric_limits<std::size_t>::max)();
        {
            VansNavigationBuildContext context(8u*1024u*1024u);
            auto source=input.profiles.front();
            source.carveObstacles={cachedCylinder.shape};
            VansNavigationTileBuildOutput output;
            if(!VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cacheCells,0,
                settings.maps.front().maximumBuildBytes,nullptr,output,error,&context)) return Expect(false,error);
            const auto builtCells=context.CaptureStats().baseCellsBuilt;
            source.carveObstacles.front().center.x=3;
            output={}; output.tiles={{77,88,0,{1,2,3}}};
            probe.allocation=0; probe.failAt=failure;
            const bool built=VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cacheCells,0,
                settings.maps.front().maximumBuildBytes,nullptr,output,error,&context);
            if(measure) warmAllocationCount=probe.allocation;
            else if(!Expect(!built && output.tiles.front().x==77,"Warm cache OOM published a partial candidate")) return false;
            probe.failAt=(std::numeric_limits<std::size_t>::max)();
            VansNavigationTileBuildOutput fresh;
            if(!VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cacheCells,0,
                settings.maps.front().maximumBuildBytes,nullptr,output,error,&context) ||
                !VansNavigationBuilder::BuildSelectedTiles(source,settings,"World",cacheCells,0,
                    settings.maps.front().maximumBuildBytes,nullptr,fresh,error)) return Expect(false,error);
            if(!Expect(context.CaptureStats().baseCellsBuilt==builtCells && output.tiles.size()==fresh.tiles.size() &&
                std::equal(output.tiles.begin(),output.tiles.end(),fresh.tiles.begin(),[](const auto& a,const auto& b){return a.bytes==b.bytes;}),
                "Warm failure discarded immutable layers or poisoned obstacle replay")) return false;
        }
        return Expect(!probe.LiveCount() && !probe.overflow,"Persistent native context leaked allocations on retirement");
    };
    if(!checkWarmFailure((std::numeric_limits<std::size_t>::max)(),true) ||
        !Expect(warmAllocationCount>10,"Warm native update did not execute a real rebuild")) return false;
    for(std::size_t failure=0;failure<warmAllocationCount;++failure)
        if(!checkWarmFailure(failure,false)) return false;
    std::cout<<"[NavigationPersistentOOM] PASS: "<<warmAllocationCount<<" warm native allocation failure boundaries, replay and final retirement\n";
    std::cout<<"[NavigationCarve] PASS: native Box/Cylinder layers, multi-floor isolation, move/remove equivalence, 65 requests, oversized Recast, budget and "
        <<allocationCount<<" native allocation failure boundaries\n";
    return true;
}

bool CheckNavigationAssetStorage(const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset,
    const Vans::VansNavigationSettings& settings)
{
    using namespace Vans;
    std::string error,encoded;
    if(!Expect(VansNavigationMeshCodec::Encode(*asset,encoded,error),error)) return false;
    const auto path=std::filesystem::temp_directory_path()/
        ("ForestNavigationAssetStorage-"+VansAssetGuid::New().ToString()+".vnavmesh");
    ScopedFileRemoval removal(path);
    if(!Expect(VansFileStorage::WriteAtomicBytes(path,encoded,error),error)) return false;
    std::shared_ptr<const VansNavigationMeshAsset> loaded;
    if(!Expect(VansNavigationMeshStorage::Load(path,loaded,error),error)) return false;
    const auto profileId=loaded->GetProfiles().front().profileId;
    const auto residentBytes=loaded->EstimateResidentBytes();
    std::string roundTrip;
    if(!Expect(loaded->GetPayloadBytes()==asset->GetPayloadBytes() &&
        residentBytes>=loaded->GetPayloadBytes() && VansNavigationMeshCodec::Encode(*loaded,roundTrip,error) &&
        roundTrip==encoded,"Owned navigation storage lost payload or changed current binary layout")) return false;
    auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query;
    if(!Expect(map->Initialize(loaded,settings,profileId,error) &&
        query.Initialize(map,settings.query,"Default",error),error)) return false;
    const auto route=query.FindPath({-4,0,0},{4,0,0});
    if(!Expect(route.status==VansNavigationPathStatus::Complete,"Stored Map lost its original route")) return false;
    const auto previous=loaded;
    auto invalid=encoded; invalid.front()^=1;
    if(!Expect(VansFileStorage::WriteAtomicBytes(path,invalid,error) &&
        !VansNavigationMeshStorage::Load(path,loaded,error) && loaded==previous &&
        query.FindPath({-4,0,0},{4,0,0}).points==route.points,
        "Invalid cold load replaced the published asset or changed the accepted Map")) return false;
    if(!Expect(VansFileStorage::WriteAtomicBytes(path,encoded.substr(0,encoded.size()-1),error) &&
        !VansNavigationMeshStorage::Load(path,loaded,error) && loaded==previous,
        "Truncated cold load replaced the published asset")) return false;
    std::error_code removalError;
    if(!Expect(std::filesystem::remove(path,removalError) && !removalError,
        "Storage fixture could not remove its source file")) return false;
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansRuntimePhaseGuard workers([&](){jobs.Shutdown();});
    std::array<VansNavigationProfileBuildData,2> copies;
    std::array<std::string,2> errors;
    std::array<bool,2> accepted{};
    VansIOAudit::Reset();
    const auto group=jobs.QueueJobGroup({
        [&](){accepted[0]=loaded->ReadProfile(profileId,copies[0],errors[0]);},
        [&](){accepted[1]=loaded->ReadProfile(profileId,copies[1],errors[1]);}});
    group->Wait();
    const auto& expected=loaded->GetProfiles().front();
    if(!Expect(!group->HasErrors() && accepted[0] && accepted[1] &&
        copies[0].tiles.size()==expected.tiles.size() && copies[1].tiles.size()==expected.tiles.size() &&
        copies[0].tiles.front().bytes==expected.tiles.front().bytes &&
        copies[1].tiles.back().bytes==expected.tiles.back().bytes && VansIOAudit::Snapshot().empty(),
        "Concurrent CPU snapshots depend on source-file I/O or share mutable results")) return false;
    copies[0].tiles.front().bytes.front()^=1;
    if(!Expect(copies[1].tiles.front().bytes==expected.tiles.front().bytes &&
        loaded->EstimateResidentBytes()==residentBytes &&
        query.FindPath({-4,0,0},{4,0,0}).points==route.points,
        "A caller-owned snapshot modified the immutable asset or native Map")) return false;
    std::cout<<"[NavigationAssetStorage] PASS: byte-identical layout, complete owned payload="
        <<loaded->GetPayloadBytes()<<", residency="<<residentBytes
        <<"; failed-load rollback, concurrent snapshots and source-file independence\n";
    return true;
}
bool CheckMapCandidateAllocationFailures(const std::shared_ptr<const Vans::VansNavigationMeshAsset>& asset,
    const Vans::VansNavigationSettings& settings)
{
    using namespace Vans;
    std::string originalBytes;
    std::string error;
    if(!VansNavigationMeshCodec::Encode(*asset,originalBytes,error)) return Expect(false,error);
    DetourAllocationProbe probe;
    // All other query workers have finished before this synchronous fault scope.
    VansRuntimePhaseGuard restoreAllocator([](){dtAllocSetCustom(nullptr,nullptr); activeDetourAllocationProbe=nullptr;});
    activeDetourAllocationProbe=&probe;
    dtAllocSetCustom(&AllocateDetourContractData,&FreeDetourContractData);
    std::size_t failures=0;
    bool replaced=false;
    {
        auto map=std::make_shared<VansNavigationMap>();
        VansNavigationQueryContext query;
        if(!map->Initialize(asset,settings,"Humanoid",error) ||
            !query.Initialize(map,settings.query,"Default",error)) return Expect(false,error);
        const glm::vec3 start(-4,0,0),end(4,0,0);
        const auto originalPath=query.FindPath(start,end);
        if(!Expect(originalPath.status==VansNavigationPathStatus::Complete,"Allocation fixture has no accepted original route")) return false;
        for(std::size_t failedAllocation=0;failedAllocation<probe.live.size();++failedAllocation)
        {
            const auto previousLive=probe.LiveCount();
            const auto previousResident=map->EstimateResidentBytes();
            probe.allocation=0; probe.failAt=failedAllocation;
            replaced=map->Initialize(asset,settings,"Humanoid",error);
            probe.failAt=(std::numeric_limits<std::size_t>::max)();
            if(replaced)
            {
                if(!Expect(query.FindPath(start,end).failure==VansNavigationPathFailure::MeshChanged &&
                    query.Initialize(map,settings.query,"Default",error) &&
                    query.FindPath(start,end).points==originalPath.points,
                    "Successful native candidate replacement retained an expired binding or changed the route")) return false;
                break;
            }
            ++failures;
            const auto retainedPath=query.FindPath(start,end);
            if(!Expect(map->GetAsset()==asset && map->IsReady() &&
                retainedPath.status==VansNavigationPathStatus::Complete && retainedPath.points==originalPath.points &&
                map->EstimateResidentBytes()==previousResident && probe.LiveCount()==previousLive && !probe.overflow,
                "Native candidate allocation failure changed the accepted Map/route or leaked borrowed Tile data")) return false;
        }
    }
    if(!Expect(replaced && failures>=3 && !probe.overflow && probe.LiveCount()==0,
        "Native Map/query teardown leaked owned allocations or did not exercise the failure boundaries")) return false;
    std::string finalBytes;
    if(!VansNavigationMeshCodec::Encode(*asset,finalBytes,error) ||
        !Expect(finalBytes==originalBytes,"Native ownership or failed replacement changed the immutable cooked asset")) return false;
    std::cout<<"[NavigationMap] PASS: "<<failures<<" native allocation failures retain the exact route, immutable asset and zero leaked allocations\n";
    return true;
}

bool CheckExclusiveQueryContexts(const std::shared_ptr<Vans::VansNavigationMap>& mesh,
	const Vans::VansNavigationSettings& settings, const std::filesystem::path& assetPath)
{
	using namespace Vans;
	const glm::vec3 start(-4.0f, 0.0f, 0.0f);
	const glm::vec3 end(4.0f, 0.0f, 0.0f);
	VansNavigationQueryContext query;
	std::string error;
	if (!Expect(query.FindPath(start, end).failure == VansNavigationPathFailure::QueryNotReady &&
		query.Initialize(mesh, settings.query, "Default", error), "Navigation query initialization failed")) return false;
	const auto invalid = [&](const glm::vec3& from, const glm::vec3& to, const glm::vec3& extents)
	{
		const auto path = query.FindPath(from, to, extents);
		return Expect(path.status == VansNavigationPathStatus::Failed &&
			path.failure == VansNavigationPathFailure::InvalidInput && path.points.empty(),
			"Navigation projection accepted non-finite coordinates or invalid extents");
	};
	if (!invalid(glm::vec3(std::numeric_limits<float>::quiet_NaN()), end, glm::vec3(1.0f)) ||
		!invalid(start, glm::vec3(std::numeric_limits<float>::infinity()), glm::vec3(1.0f)) ||
		!invalid(start, end, glm::vec3(0.0f, 2.0f, 1.0f)) ||
		!invalid(start, end, glm::vec3(-1.0f)) ||
        !invalid(start, end, glm::vec3(std::numeric_limits<float>::infinity())) ||
        !invalid(glm::vec3(1.e38f),end,glm::vec3(1)) ||
        !invalid(start,end,glm::vec3(1.e38f))) return false;
	VansNavigationQuerySettings invalidBudget = settings.query;
	invalidBudget.maximumSearchNodes = 0;
	if (!Expect(!query.Initialize(mesh, invalidBudget, "Default", error) &&
		query.FindPath(start, end).status == VansNavigationPathStatus::Complete,
		"Failed query initialization destroyed the last valid context")) return false;
	invalidBudget.maximumSearchNodes = 65536;
	if (!Expect(!ValidateNavigationQuerySettings(invalidBudget, error) &&
		!query.Initialize(mesh, invalidBudget, "Default", error) &&
		query.FindPath(start, end).status == VansNavigationPathStatus::Complete,
		"A search-node budget beyond the native index range was admitted or destroyed the active context")) return false;

	std::vector<std::future<bool>> workers;
	for (int worker = 0; worker < 4; ++worker)
		workers.push_back(std::async(std::launch::async, [mesh, settings, worker, start, end]()
		{
			VansNavigationQueryContext context;
			VansNavigationQuerySettings budget = settings.query;
			const bool limited = (worker & 1) != 0;
			if (limited) budget.maximumSearchNodes = 4;
			std::string error;
			if (!context.Initialize(mesh, budget, "Default", error)) return false;
			for (int iteration = 0; iteration < 128; ++iteration)
			{
				const bool reverse = (iteration & 1) != 0;
				const auto result = context.FindPath(reverse ? end : start, reverse ? start : end);
				if (limited)
				{
					if (result.status != VansNavigationPathStatus::Failed ||
						result.failure != VansNavigationPathFailure::SearchNodeCapacityExceeded) return false;
				}
				else if (result.status != VansNavigationPathStatus::Complete || result.points.size() < 3u ||
					glm::distance(result.points.front(), reverse ? end : start) > 0.3f ||
					glm::distance(result.points.back(), reverse ? start : end) > 0.3f) return false;
			}
			return true;
		}));
	for (auto& worker : workers)
		if (!Expect(worker.get(), "Concurrent queries shared native/scratch state or query budgets")) return false;
	if (!Expect(LoadNavigationMap(assetPath, settings, mesh, error) &&
		query.FindPath(start, end).failure == VansNavigationPathFailure::MeshChanged &&
		query.Initialize(mesh, settings.query, "Default", error) &&
		query.FindPath(start, end).status == VansNavigationPathStatus::Complete,
		"Resource replacement left a query bound to stale native state")) return false;
    const auto asset = mesh->GetAsset();
    std::promise<void> yielded, replaced;
    auto yieldedSignal = yielded.get_future();
    auto replacementSignal = replaced.get_future();
    auto suspended = std::async(std::launch::async, [&]()
    {
        VansNavigationQueryContext context;
        std::string workerError;
        if (!context.Initialize(mesh, settings.query, "Default", workerError)) { yielded.set_value(); return false; }
        const auto started = context.BeginPath(start, end);
        yielded.set_value();
        replacementSignal.wait();
        int used = -1;
        const auto stale = context.AdvancePath(1, used);
        context.ResetQuery();
        return started.status == VansNavigationPathStatus::None && stale.failure == VansNavigationPathFailure::MeshChanged && used == 0;
    });
    yieldedSignal.wait();
    const bool replacedWhileSuspended = mesh->Initialize(asset, settings, "Humanoid", error);
    replaced.set_value();
    if (!Expect(suspended.get() && replacedWhileSuspended,
        "Sliced query retained a cross-frame lease or used old native memory after replacement")) return false;
    std::promise<void> startWorkers;
    const auto startSignal = startWorkers.get_future().share();
    workers.clear();
    for (int worker = 0; worker < 4; ++worker)
        workers.push_back(std::async(std::launch::async, [mesh, settings, start, end, startSignal]()
        {
            startSignal.wait();
            for (int iteration = 0; iteration < 128; ++iteration)
            {
                VansNavigationQueryContext context;
                std::string error;
                if (!context.Initialize(mesh, settings.query, "Default", error)) return false;
                const auto path = context.FindPath(start, end);
                if (path.status != VansNavigationPathStatus::Complete && path.failure != VansNavigationPathFailure::MeshChanged)
                    return false;
            }
            return true;
        }));
    startWorkers.set_value();
    unsigned published = 0, deferred = 0;
    for (int replacement = 0; replacement < 64; ++replacement)
    {
        if (mesh->Initialize(asset, settings, "Humanoid", error)) ++published;
        else if (error == "Navigation map write deferred by an active query") ++deferred;
        else return Expect(false, error);
    }
    for (auto& worker : workers)
        if (!Expect(worker.get(), "Map replacement raced native pointers or kept a cross-frame query lease")) return false;
    if (!Expect(mesh->Initialize(asset, settings, "Humanoid", error) &&
        query.FindPath(start, end).failure == VansNavigationPathFailure::MeshChanged,
        "Map replacement failed after readers finished or preserved an expired native binding")) return false;
    std::cout << "[NavigationMap] replacement published=" << published << " deferred=" << deferred << '\n';
	std::cout << "[NavigationQuery] PASS: 4 exclusive contexts/512 searches, independent budgets, invalid input and replacement lifetime\n";
	return true;
}
}

bool RunNavigationAIContractTests()
{
	using namespace Vans;
	if (!CheckNavigationAuthoringPreview() || !CheckNavigationSettingsCodec() || !CheckNavigationPrefabReferences() || !CheckPhysicsNavigationSourceEquivalence() || !CheckCommittedPcgCollisionSources() || !CheckNavigationSourceCollection() || !CheckNavigationCompressedLayers() || !CheckNavigationCarveBackend()) return false;
	if (!Expect(VansAssetDatabase::Classify("test.vnavmesh") ==
		VansAssetType::NavigationMesh, "Navigation asset classification failed") ||
		!Expect(VansAssetDatabase::Classify("test.vaibehavior") ==
			VansAssetType::AIBehavior, "AI Behavior asset classification failed") ||
		!Expect(VansAssetDatabase::ParseSerializedType("navigationMesh") ==
			VansAssetType::NavigationMesh, "Navigation serialized type failed") ||
		!Expect(VansAssetDatabase::ParseSerializedType("aiBehavior") ==
			VansAssetType::AIBehavior, "AI Behavior serialized type failed"))
	{
		return false;
	}
	const float correctedNonstandardYaw = ResolveModelOwnerFacingYaw(
		35.0f, glm::vec3(-1.0f, 0.0f, 0.0f));
	const glm::vec3 correctedNonstandardForward = glm::angleAxis(
		glm::radians(correctedNonstandardYaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::vec3(-1.0f, 0.0f, 0.0f);
	const glm::vec3 desiredForward = glm::angleAxis(
		glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::vec3(0.0f, 0.0f, 1.0f);
	const std::array<float, 3> projectedWhisperRotation =
		ProjectSceneQuaternionToEulerDegrees({
			5.1898859965e-7f, 0.9322234988f, 5.1898859965e-7f, 0.3618831038f }, true);
	const std::array<float, 3> defaultWhisperRotation =
		ProjectSceneQuaternionToEulerDegrees({
			5.1898859965e-7f, 0.9322234988f, 5.1898859965e-7f, 0.3618831038f }, false);
	const std::array<float, 3> projectedUEFNRotation =
		ProjectSceneQuaternionToEulerDegrees({
			-0.7071067812f, 0.0f, 0.0f, 0.7071067812f }, false);
	const float whisperForwardOwnerYaw = ResolveModelOwnerFacingYaw(
		0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
	const float whisperRightTargetVisualYaw = ResolveAIVisualFacingYawDegrees(
		glm::vec3(1.0f, 0.0f, 0.0f));
	const float whisperLeftTargetVisualYaw = ResolveAIVisualFacingYawDegrees(
		glm::vec3(-1.0f, 0.0f, 0.0f));
	const float whisperRightTargetOwnerYaw = ResolveModelOwnerFacingYaw(
		whisperRightTargetVisualYaw, glm::vec3(0.0f, 0.0f, 1.0f));
	const float whisperRightTurnDelta = std::remainder(
		whisperRightTargetOwnerYaw - whisperForwardOwnerYaw, 360.0f);
	VansCharacterTrajectoryGenerator whisperRightTurn;
	VansCharacterMotionIntent whisperRightTurnIntent;
	whisperRightTurnIntent.movementReferenceYaw = 0.0f;
	whisperRightTurnIntent.desiredFacingYaw = whisperRightTargetOwnerYaw;
	whisperRightTurnIntent.hasFacing = true;
	whisperRightTurnIntent.valid = true;
	VansCharacterMotionSettings whisperTurnSettings;
	whisperTurnSettings.facingHalfLife = 0.1f;
	whisperTurnSettings.maxFacingYawRate = 720.0f;
	whisperRightTurn.Reset(glm::vec3(0.0f), whisperForwardOwnerYaw);
	whisperRightTurn.Update(0.1f, whisperRightTurnIntent, whisperTurnSettings,
		glm::vec3(0.0f), whisperForwardOwnerYaw);
	const float plannedWhisperRightYaw = whisperRightTurn.GetPlannedFacingYaw();
	const glm::vec3 plannedWhisperForward = glm::angleAxis(
		glm::radians(plannedWhisperRightYaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::vec3(0.0f, 0.0f, 1.0f);
	VansRuntimeAIAgentComponent walkOnlyAgent;
	walkOnlyAgent.maxMovementState = 1;
	const glm::vec3 lateralFacing = ResolveAIChaseFacingDirection(
		glm::vec3(0.0f),
		glm::vec3(10.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f));
	if (!Expect(glm::dot(correctedNonstandardForward, desiredForward) > 0.9999f,
		"Nonstandard model forward was not corrected to the desired world facing") ||
		!Expect(std::abs(projectedWhisperRotation[0]) < 0.001f &&
			projectedWhisperRotation[1] > 137.5f && projectedWhisperRotation[1] < 137.7f &&
			std::abs(projectedWhisperRotation[2]) < 0.001f,
			"Configured Whisper scene rotation did not retain a stable Yaw branch") ||
		!Expect(std::abs(defaultWhisperRotation[0]) > 179.0f &&
			defaultWhisperRotation[1] > 42.3f && defaultWhisperRotation[1] < 42.6f &&
			std::abs(defaultWhisperRotation[2]) > 179.0f,
			"Unconfigured scene rotation unexpectedly entered the Whisper-only Yaw path") ||
		!Expect(projectedUEFNRotation[0] < -89.9f && projectedUEFNRotation[0] > -90.1f &&
			std::abs(projectedUEFNRotation[1]) < 0.001f &&
			std::abs(projectedUEFNRotation[2]) < 0.001f,
			"UEFN model-axis correction was incorrectly flattened to upright Yaw") ||
		!Expect(whisperRightTurnDelta > 89.9f && whisperRightTurnDelta < 90.1f &&
			whisperLeftTargetVisualYaw < -89.9f && whisperLeftTargetVisualYaw > -90.1f,
			"AI visual yaw did not preserve symmetric right/left turn signs") ||
		!Expect(plannedWhisperRightYaw > 0.1f && plannedWhisperForward.x > 0.1f,
			"Whisper facing interpolation did not approach a +X target") ||
		!Expect(std::abs(ResolveModelOwnerFacingYaw(
			35.0f, glm::vec3(0.0f, 0.0f, 1.0f)) - 35.0f) < 0.001f,
			"Engine +Z model forward should not receive a facing correction") ||
		!Expect(ResolveAIMovementState(4.0f, walkOnlyAgent) == 1,
			"Walk-only AI selected the Run movement state") ||
		!Expect(glm::dot(lateralFacing, glm::vec3(1.0f, 0.0f, 0.0f)) > 0.9999f,
			"Chase facing followed the route instead of a 90-degree target offset") ||
		!Expect(IsTargetInsideAIVisionCone(
			glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(10.0f, 0.0f, 0.0f), 14.0f, 240.0f),
			"Wide Whisper sight rejected a 90-degree lateral target") ||
		!Expect(!IsTargetInsideAIVisionCone(
			glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, -10.0f), 14.0f, 240.0f),
			"Wide Whisper sight accepted a target directly behind it") ||
		!Expect(!IsTargetInsideAIVisionCone(
			glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, 15.0f), 14.0f, 240.0f),
			"Whisper sight accepted a target outside its configured range"))
	{
		return false;
	}
	VansAIBlackboard blackboard;
	std::string error;
	VansRuntimeAIAgentComponent scheduledAgent;
	VansRuntimeNavigationAgentComponent scheduledNavigation;
	if (!Expect(ValidateAIRuntimeTiming(
			scheduledAgent, error) &&
		ValidateNavigationAgentTiming(scheduledNavigation, error), error))
	{
		return false;
	}
	VansRuntimeAIAgentComponent invalidTiming = scheduledAgent;
	invalidTiming.timing.perceptionInterval = 0.0f;
	if (!Expect(!ValidateAIRuntimeTiming(
			invalidTiming, error),
		"AI timing accepted a zero perception interval"))
	{
		return false;
	}
	VansRuntimeNavigationAgentComponent invalidRepath = scheduledNavigation;
	invalidRepath.forceRepathDistance = invalidRepath.targetMoveThreshold;
	if (!Expect(!ValidateNavigationAgentTiming(invalidRepath, error),
		"AI repath policy accepted a non-distinct force threshold"))
	{
		return false;
	}
	const std::vector<VansAIBlackboardEntryDefinition> blackboardSchema = {
		{ "ActivationRequested", VansAIValueType::Bool, false },
		{ "Target", VansAIValueType::Entity, VansEntityHandle{} }
	};
	if (!Expect(blackboard.Configure(blackboardSchema, error), error) ||
		!Expect(blackboard.SetBool("ActivationRequested", true,
			"script-guid", &error), error) ||
		!Expect(blackboard.GetBool("ActivationRequested"),
			"Typed Blackboard bool update failed") ||
		!Expect(!blackboard.Set("ActivationRequested", VansAIValue(std::int64_t{ 1 }),
			"invalid-test", &error),
			"Typed Blackboard accepted an invalid value type"))
	{
		return false;
	}
	const VansAIBlackboardDebugSnapshot blackboardDebug =
		blackboard.CaptureDebugSnapshot(1u);
	const VansAIActivationRequested activationEvent{
		VansEntityHandle{ 3u, 2u }, "script-guid" };
	if (!Expect(blackboardDebug.totalEntries == 2u && blackboardDebug.truncated &&
			blackboardDebug.entries.size() == 1u &&
			blackboardDebug.entries.front().name == "ActivationRequested" &&
			blackboardDebug.entries.front().lastWriter == "script-guid",
			"Blackboard diagnostics did not preserve stable order, bound, and writer") ||
		!Expect(activationEvent.target.IsValid() &&
			activationEvent.sourceGuid == "script-guid",
			"AI event did not preserve its source component identity"))
	{
		return false;
	}

	VansNavigationSettings settings;
	settings.maps.front().minimumRegionSizeCells = 1.0f;
	settings.maps.front().mergeRegionSizeCells = 2.0f;
	const auto testSource = [](const VansNavigationSettings& sourceSettings,
		const char* scene)
	{
		VansNavigationSource source;
		source.scene = scene;
		source.inputHash = 1u;

		return source;
	};
	VansSceneObjectBuildPlan plan;
	AppendEnvironmentBox(plan, "Floor", { 0.0f, -0.1f, 0.0f },
		{ 5.0f, 0.1f, 5.0f });
	AppendEnvironmentBox(plan, "Wall", { 0.0f, 1.5f, 0.0f },
		{ 0.5f, 1.5f, 2.0f });
	VansNavigationGeometry geometry;
	if (!Expect(CollectContractGeometry(
		plan, settings.areas, {}, geometry, error), error))
	{
		return false;
	}
	if (!Expect(geometry.VertexCount() == 16u && geometry.TriangleCount() == 24u &&
		geometry.areas.size() == geometry.TriangleCount() &&
		std::all_of(geometry.areas.begin(), geometry.areas.end(),
			[](std::uint8_t area) { return area == 0u; }),
		"Scene collider geometry projection failed"))
	{
		return false;
	}

	VansSceneObjectBuildPlan hierarchyPlan;
	VansSceneObjectBuildConfig parent;
	const VansEntityGuid parentGuid = VansAssetGuid::FromStableName(
		"NavigationContract", "HierarchyParent");
	parent.entityGuid = parentGuid.ToString();
	parent.name = "HierarchyParent";
	parent.transform = VansSceneTransformConfig{};
	parent.transform->position = { 10.0f, 0.0f, 0.0f };
	parent.transform->rotation = { 0.0f, 90.0f, 0.0f };
	parent.transform->scale = { 2.0f, 1.0f, 3.0f };
	hierarchyPlan.objects.push_back(parent);
	AppendEnvironmentBox(hierarchyPlan, "HierarchyChild", { 1.0f, 0.0f, 0.0f },
		{ 1.0f, 0.5f, 0.25f });
	VansSceneObjectBuildConfig& child = hierarchyPlan.objects.back();
	child.transform->scale = { 1.0f, 2.0f, 1.0f };
	child.physicsComponents.physics->shapeOffset = { 0.5f, 0.0f, 0.0f };
	child.parent = VansSceneParentReference{};
	child.parent->entityGuid = parentGuid;
	AppendEnvironmentBox(hierarchyPlan, "DynamicEnvironment",
		{ 100.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
	hierarchyPlan.objects.back().physicsComponents.physics->bodyType = "dynamic";
	VansNavigationGeometry hierarchyGeometry;
	if (!Expect(CollectContractGeometry(
		hierarchyPlan, settings.areas, {}, hierarchyGeometry, error), error) ||
		!Expect(hierarchyGeometry.VertexCount() == 8u &&
			hierarchyGeometry.TriangleCount() == 12u,
			"Navigation geometry included a non-static collider"))
	{
		return false;
	}
	glm::vec3 minimum(std::numeric_limits<float>::max());
	glm::vec3 maximum(std::numeric_limits<float>::lowest());
	for (std::size_t vertex = 0; vertex < hierarchyGeometry.VertexCount(); ++vertex)
	{
		const glm::vec3 point(
			hierarchyGeometry.vertices[vertex * 3u],
			hierarchyGeometry.vertices[vertex * 3u + 1u],
			hierarchyGeometry.vertices[vertex * 3u + 2u]);
		minimum = glm::min(minimum, point);
		maximum = glm::max(maximum, point);
	}
	if (!Expect(glm::all(glm::lessThan(glm::abs(
		minimum - glm::vec3(9.25f, -1.0f, -4.5f)), glm::vec3(0.001f))) &&
		glm::all(glm::lessThan(glm::abs(
			maximum - glm::vec3(10.75f, 1.0f, -0.5f)), glm::vec3(0.001f))),
		"Navigation geometry did not apply parent rotation, parent scale, child scale, and offset"))
	{
		return false;
	}

	VansSceneObjectBuildPlan conflictingOffsetPlan;
	AppendEnvironmentBox(conflictingOffsetPlan, "ConflictingOffset",
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
	conflictingOffsetPlan.objects.back().physicsComponents.physics->shapeOffset =
		{ 0.0f, 0.0f, 0.0f };
	conflictingOffsetPlan.objects.back().physicsComponents.physics->colliderOffset =
		{ 1.0f, 0.0f, 0.0f };
	VansNavigationGeometry preservedGeometry;
	preservedGeometry.vertices = { 42.0f, 43.0f, 44.0f };
	if (!Expect(!CollectContractGeometry(
		conflictingOffsetPlan, settings.areas, {}, preservedGeometry, error) &&
		error.find("both shapeOffset and colliderOffset") != std::string::npos &&
		preservedGeometry.vertices == std::vector<float>{ 42.0f, 43.0f, 44.0f },
		"Navigation geometry accepted conflicting offsets or published partial output"))
	{
		return false;
	}
	VansSceneObjectBuildPlan unknownAreaPlan;
	AppendEnvironmentBox(unknownAreaPlan, "UnknownArea",
		{ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, "Missing");
	VansNavigationGeometry preservedAreaGeometry;
	preservedAreaGeometry.vertices = { 7.0f, 8.0f, 9.0f };
	preservedAreaGeometry.areas = { 4u };
	if (!Expect(!CollectContractGeometry(
		unknownAreaPlan, settings.areas, {}, preservedAreaGeometry, error) &&
		error.find("unknown area 'Missing'") != std::string::npos &&
		preservedAreaGeometry.vertices == std::vector<float>{ 7.0f, 8.0f, 9.0f } &&
		preservedAreaGeometry.areas == std::vector<std::uint8_t>{ 4u },
		"Navigation geometry accepted an unknown area or published partial output"))
	{
		return false;
	}

	VansSceneObjectBuildPlan meshPlan;
	VansSceneObjectBuildConfig meshObject;
	meshObject.entityGuid = VansAssetGuid::FromStableName(
		"NavigationContract", "MeshCollider").ToString();
	meshObject.name = "MeshCollider";
	meshObject.transform = VansSceneTransformConfig{};
	meshObject.transform->position = { 2.0f, 0.0f, 3.0f };
	meshObject.transform->scale = { 2.0f, 1.0f, 3.0f };
	VansScenePhysicsNodeConfig meshPhysics;
	meshPhysics.enabled = true;
	meshPhysics.bodyType = "static";
	meshPhysics.colliderType = "mesh";
	meshPhysics.useMeshCollider = true;
	meshPhysics.layer = "Environment";
	meshPhysics.isTrigger = false;
	meshPhysics.mesh = "mesh-collider-guid";
	meshObject.physicsComponents.physics = meshPhysics;
	meshPlan.objects.push_back(std::move(meshObject));
	VansTriangleMeshData triangleMesh;
	triangleMesh.positions = {
		0.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f
	};
	triangleMesh.indices = { 0u, 1u, 2u };
	std::size_t meshResolveCount = 0;
	const VansNavigationMeshResolver meshResolver = [&triangleMesh, &meshResolveCount](
		const std::string& guid, VansTriangleMeshData& output, std::string& resolveError)
	{
		if (guid != "mesh-collider-guid")
		{
			resolveError = "Unexpected mesh collider GUID";
			return false;
		}
		++meshResolveCount;
		output = triangleMesh;
		return true;
	};
	VansNavigationGeometry meshGeometry;
	if (!Expect(CollectContractGeometry(
		meshPlan, settings.areas, meshResolver, meshGeometry, error), error) ||
		!Expect(meshResolveCount == 1u && meshGeometry.VertexCount() == 3u &&
			meshGeometry.TriangleCount() == 1u &&
			meshGeometry.vertices == std::vector<float>{
				2.0f, 0.0f, 3.0f, 4.0f, 0.0f, 3.0f, 2.0f, 0.0f, 6.0f },
			"Navigation mesh collider geometry did not preserve source topology and transform"))
	{
		return false;
	}

	auto mesh = std::make_shared<VansNavigationMap>();
	std::uint64_t originalGeometryHash = 0;
	if (!Expect(ComputeNavigationGeometryHash(geometry, originalGeometryHash, error), error)) return false;
	const auto fingerprintPlan = [&](const VansSceneObjectBuildPlan& source, std::uint64_t& hash)
	{
		VansNavigationGeometry projected;
		return CollectContractGeometry(source, settings.areas, {}, projected, error) &&
			ComputeNavigationGeometryHash(projected, hash, error);
	};
	auto unrelatedPlan = plan;
	for (auto& object : unrelatedPlan.objects)
	{
		object.name += "Renamed";
		object.physicsComponents.physics->mass = 99.0f;
		object.physicsComponents.physics->hitRegion = "UnrelatedRegion";
		object.physicsComponents.physics->material = VansScenePhysicsMaterialConfig{};
		object.physicsComponents.physics->material->restitution = 0.7f;
		object.render = VansSceneRenderNodeConfig{};
		object.render->mesh = "unrelated-render-mesh";
	}
	VansSceneObjectBuildConfig decoration;
	decoration.name = "LightAndScriptOnly";
	decoration.transform = VansSceneTransformConfig{};
	decoration.transform->position = { 100.0f, 200.0f, 300.0f };
	unrelatedPlan.objects.push_back(decoration);
	std::reverse(unrelatedPlan.objects.begin(), unrelatedPlan.objects.end());
	std::uint64_t changedGeometryHash = 0;
	if (!Expect(fingerprintPlan(unrelatedPlan, changedGeometryHash) && changedGeometryHash == originalGeometryHash,
		"Unrelated render/physics properties, non-colliders or entity order invalidated navigation")) return false;
	const auto expectGeometryChange = [&](const VansSceneObjectBuildPlan& changed, const char* message)
	{
		return Expect(fingerprintPlan(changed, changedGeometryHash) && changedGeometryHash != originalGeometryHash, message);
	};
	auto movedPlan = plan;
	movedPlan.objects.back().transform->position[0] += 1.0f;
	if (!expectGeometryChange(movedPlan, "Moving an Environment collider did not invalidate navigation")) return false;
	auto resizedPlan = plan;
	(*resizedPlan.objects.back().physicsComponents.physics->boxExtents)[2] += 1.0f;
	if (!expectGeometryChange(resizedPlan, "Changing collider extents did not invalidate navigation")) return false;
	auto disabledPlan = plan;
	disabledPlan.objects.back().physicsComponents.physics->enabled = false;
	if (!expectGeometryChange(disabledPlan, "Disabling a collider did not invalidate navigation")) return false;
	auto triggerPlan = plan;
	triggerPlan.objects.back().physicsComponents.physics->isTrigger = true;
	if (!expectGeometryChange(triggerPlan, "Making a collider a trigger did not invalidate navigation")) return false;
	auto layerPlan = plan;
	layerPlan.objects.back().physicsComponents.physics->layer = "Default";
	if (!expectGeometryChange(layerPlan, "Removing a collider from Environment did not invalidate navigation")) return false;
	auto reorderedGeometry = geometry;
	std::reverse(reorderedGeometry.areas.begin(), reorderedGeometry.areas.end());
	for (std::size_t triangle = 0; triangle < geometry.TriangleCount(); ++triangle)
		for (std::size_t corner = 0; corner < 3u; ++corner)
			reorderedGeometry.indices[triangle * 3u + corner] =
				geometry.indices[(geometry.TriangleCount() - 1u - triangle) * 3u + (corner + 1u) % 3u];
	if (!Expect(ComputeNavigationGeometryHash(reorderedGeometry, changedGeometryHash, error) &&
		changedGeometryHash == originalGeometryHash, "Triangle order or cyclic winding invalidated navigation")) return false;
	std::uint64_t meshGeometryHash = 0;
	if (!Expect(ComputeNavigationGeometryHash(meshGeometry, meshGeometryHash, error), error)) return false;
	auto changedMeshGeometry = meshGeometry;
	changedMeshGeometry.vertices[0] += 0.1f;
	if (!Expect(ComputeNavigationGeometryHash(changedMeshGeometry, changedGeometryHash, error) &&
		changedGeometryHash != meshGeometryHash, "Changing mesh collision vertices did not invalidate navigation")) return false;
	auto areaGeometryHashInput = geometry;
	areaGeometryHashInput.areas[0] = 1u;
	if (!Expect(ComputeNavigationGeometryHash(areaGeometryHashInput, changedGeometryHash, error) &&
		changedGeometryHash != originalGeometryHash, "Changing a triangle navigation area did not invalidate navigation")) return false;
	VansNavigationSource geometrySource = testSource(settings, "Scenes/NavigationContract.json");
	geometrySource.inputHash = originalGeometryHash;
	geometrySource.scene = "Scenes/RenamedScene.json";
	if (!Expect(geometrySource.IsValid() && geometrySource.inputHash == originalGeometryHash &&
        geometrySource.inputHash != changedGeometryHash,
		"Navigation source did not distinguish effective geometry changes")) return false;
	std::uint64_t hierarchyHash = 0;
	if (!Expect(ComputeNavigationGeometryHash(hierarchyGeometry, hierarchyHash, error), error)) return false;
	auto changedHierarchy = hierarchyPlan;
	changedHierarchy.objects.front().transform->position[0] += 2.0f;
	if (!Expect(fingerprintPlan(changedHierarchy, changedGeometryHash) && changedGeometryHash != hierarchyHash,
		"Changing a collider parent Transform did not invalidate navigation")) return false;
	changedHierarchy = hierarchyPlan;
	changedHierarchy.objects.front().active = false;
	if (!Expect(fingerprintPlan(changedHierarchy, changedGeometryHash) && changedGeometryHash != hierarchyHash,
		"Disabling a collider parent did not invalidate navigation")) return false;
	if (!Expect(BuildNavigationMap(geometry, settings, mesh, error), error))
		return false;
	const VansNavigationPath path = QueryNavigationPath(mesh, settings.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	const bool routedAroundWall = std::any_of(path.points.begin(), path.points.end(),
		[](const glm::vec3& point) { return std::abs(point.z) > 2.0f; });
	if (!Expect(path.status == VansNavigationPathStatus::Complete,
		"Detour did not produce a complete path") ||
		!Expect(path.points.size() >= 3u && routedAroundWall,
			"Path did not route around the Environment collider"))
	{
		return false;
	}

	VansNavigationSettings highCostSettings = settings;
	highCostSettings.areas.definitions.push_back({ "Slow", 1u, 50.0f, true });
	highCostSettings.queryFilters.push_back({ "Direct", {}, { { "Slow", 0.25f } } });
	highCostSettings.queryFilters.push_back({ "Exclude", { "Slow" }, {} });
	VansSceneObjectBuildPlan areaPlan;
	AppendEnvironmentBox(areaPlan, "AreaStart", { -4.0f, -0.1f, 0.0f },
		{ 1.2f, 0.1f, 3.1f });
	AppendEnvironmentBox(areaPlan, "AreaEnd", { 4.0f, -0.1f, 0.0f },
		{ 1.2f, 0.1f, 3.1f });
	AppendEnvironmentBox(areaPlan, "AreaDirect", { 0.0f, -0.1f, 0.0f },
		{ 3.0f, 0.1f, 1.0f }, "Slow");
	AppendEnvironmentBox(areaPlan, "AreaDetour", { 0.0f, -0.1f, 2.0f },
		{ 3.0f, 0.1f, 1.0f });
	VansNavigationGeometry areaGeometry;
	if (!Expect(CollectContractGeometry(
		areaPlan, highCostSettings.areas, {}, areaGeometry, error), error))
	{
		return false;
	}
	auto highCostMesh = std::make_shared<VansNavigationMap>();
	if (!Expect(BuildNavigationMap(areaGeometry, highCostSettings, highCostMesh, error), error))
		return false;
	const auto usesAreaDetour = [](const VansNavigationPath& candidate)
	{
		return std::any_of(candidate.points.begin(), candidate.points.end(),
			[](const glm::vec3& point) { return point.z > 0.9f; });
	};
	const VansNavigationPath highCostPath = QueryNavigationPath(highCostMesh, highCostSettings.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(highCostPath.status == VansNavigationPathStatus::Complete &&
		usesAreaDetour(highCostPath),
		"Navigation area traversal cost did not select the lower-cost route"))
	{
		return false;
	}
	VansNavigationQueryContext defaultFilterQuery, directFilterQuery, excludedFilterQuery;
	if (!Expect(defaultFilterQuery.Initialize(highCostMesh, highCostSettings.query, "Default", error) &&
		directFilterQuery.Initialize(highCostMesh, highCostSettings.query, "Direct", error) &&
		excludedFilterQuery.Initialize(highCostMesh, highCostSettings.query, "Exclude", error), error)) return false;
	const glm::vec3 areaStart(-4.0f, 0.0f, 0.0f), areaEnd(4.0f, 0.0f, 0.0f);
	for (int repeat = 0; repeat < 8; ++repeat)
	{
		const auto direct = directFilterQuery.FindPath(areaStart, areaEnd);
		const auto excluded = excludedFilterQuery.FindPath(areaStart, areaEnd);
		const auto costly = defaultFilterQuery.FindPath(areaStart, areaEnd);
		if (!Expect(direct.status == VansNavigationPathStatus::Complete && !usesAreaDetour(direct) &&
			excluded.status == VansNavigationPathStatus::Complete && usesAreaDetour(excluded) &&
			costly.status == VansNavigationPathStatus::Complete && usesAreaDetour(costly),
			"Independent filters on one Map changed another context's allowed areas or low-cost route")) return false;
	}
	if (!Expect(!directFilterQuery.Initialize(highCostMesh, highCostSettings.query, "Missing", error) &&
		!usesAreaDetour(directFilterQuery.FindPath(areaStart, areaEnd)),
		"An unavailable filter replaced the last valid query context")) return false;
	const std::filesystem::path areaAssetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationAreaContract.vnavmesh";
	ScopedFileRemoval areaAssetRemoval(areaAssetPath);
	if (!Expect(VansNavigationMeshStorage::Save(areaAssetPath, *highCostMesh->GetAsset(), error),
		error)) return false;
	VansNavigationSettings staleBakeSettings = highCostSettings;
	staleBakeSettings.profiles.front().radiusMeters += 0.1f;
    auto staleBakeMesh = std::make_shared<VansNavigationMap>();
    if (!Expect(!LoadNavigationMap(areaAssetPath, staleBakeSettings, staleBakeMesh, error) &&
        error.find("Stale") != std::string::npos && !staleBakeMesh->IsReady(),
        "Changed Map/Profile dimensions remained usable with stale baked clearance")) return false;

	VansNavigationSettings equalCostSettings = highCostSettings;
	equalCostSettings.areas.definitions.back().traversalCost = 1.0f;
	auto equalCostMesh = std::make_shared<VansNavigationMap>();
	if (!Expect(LoadNavigationMap(areaAssetPath, equalCostSettings, equalCostMesh, error), error))
		return false;
	VansNavigationSettings renumberedAreas = equalCostSettings;
	renumberedAreas.areas.definitions.back().id = 2u;
	const auto preservedAreaAsset = equalCostMesh->GetAsset();
	if (!Expect(!LoadNavigationMap(areaAssetPath, renumberedAreas, equalCostMesh, error) &&
		error.find("Stale") != std::string::npos && equalCostMesh->GetAsset() == preservedAreaAsset && equalCostMesh->IsReady(),
		"Changed Area IDs did not reject Stale data atomically")) return false;
	renumberedAreas.areas.definitions.pop_back();
	// 删除 Area 的配置也必须清理对应过滤器引用，否则先由领域配置校验拒绝。
	renumberedAreas.queryFilters.resize(1u);
	if (!Expect(!LoadNavigationMap(areaAssetPath, renumberedAreas, equalCostMesh, error) &&
		error.find("Stale") != std::string::npos && equalCostMesh->GetAsset() == preservedAreaAsset && equalCostMesh->IsReady(),
		"Removed Areas retained baked semantics or destroyed the valid Map")) return false;
	const VansNavigationPath equalCostPath = QueryNavigationPath(equalCostMesh, equalCostSettings.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(equalCostPath.status == VansNavigationPathStatus::Complete &&
		!usesAreaDetour(equalCostPath),
		"Equal-cost navigation areas did not preserve the shorter direct route"))
	{
		return false;
	}

	VansNavigationSettings forbiddenSettings = equalCostSettings;
	forbiddenSettings.areas.definitions.back().traversable = false;
	auto forbiddenMesh = std::make_shared<VansNavigationMap>();
	if (!Expect(LoadNavigationMap(areaAssetPath, forbiddenSettings, forbiddenMesh, error), error))
		return false;
	const VansNavigationPath forbiddenPath = QueryNavigationPath(forbiddenMesh, forbiddenSettings.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(forbiddenPath.status == VansNavigationPathStatus::Complete &&
		usesAreaDetour(forbiddenPath),
		"Forbidden navigation area remained queryable after asset loading"))
	{
		return false;
	}

	const std::filesystem::path assetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationContract.vnavmesh";
	ScopedFileRemoval assetRemoval(assetPath);
	const VansNavigationSource navigationTestSource =
		testSource(settings, "Scenes/NavigationContract.json");
	if (!Expect(VansNavigationMeshStorage::Save(assetPath, *mesh->GetAsset(), error), error))
		return false;
	auto loaded = std::make_shared<VansNavigationMap>();
	const bool loadedOk = LoadNavigationMap(assetPath, settings, loaded, error);
	if (!Expect(loadedOk, error))
		return false;
	auto excessiveGrid = geometry;
	for (std::size_t index = 0; index < excessiveGrid.vertices.size(); index += 3u)
		excessiveGrid.vertices[index] *= 1.e20f;
	if (!Expect(!BuildNavigationMap(excessiveGrid, settings, loaded, error) && error.find("capacity") != std::string::npos && loaded->IsReady(),
		"Native grid overflow was not rejected before allocation or destroyed the valid resource")) return false;
	if (!CheckImmutableAssetCodec(loaded->GetAsset())) return false;
    if (!CheckNavigationAssetStorage(loaded->GetAsset(),settings)) return false;
	if (!CheckTiledNavigation()) return false;
	if (!CheckMultipleProfiles()) return false;
	if (!CheckMapCandidateAllocationFailures(loaded->GetAsset(),settings)) return false;
	if (!CheckExclusiveQueryContexts(loaded, settings, assetPath)) return false;
	if (!CheckCorridorPositionRepair(loaded,settings) || !CheckCorridorTargetRepair(loaded,settings)) return false;
	if (!CheckCorridorOptimization(loaded,settings) || !CheckUnifiedFollowBudget(loaded,settings) ||
        !CheckNavigationDataWait() || !CheckNavigationProgressWindow() || !CheckNavigationBoundaryCache() || !CheckNavigationAvoidance(loaded,settings) ||
        !CheckNavigationLinkAuthoring(settings) || !CheckNavigationLinkQuery(settings)) return false;
	if (!CheckSpatialNavigationQueries(loaded,settings)) return false;
	if (!CheckUnifiedQueryScheduler(loaded,settings) || !CheckWorkerNavigationQueries(loaded,settings)) return false;
	if (!CheckStandaloneNavigationDriver(loaded, settings, geometry)) return false;
	if (!CheckDecisionOnlyAIAndLuaBindings()) return false;
	if (!Expect(loaded->GetAsset()->FindProfile("Humanoid")->inputHash != changedGeometryHash &&
		loaded->IsReady(), "Source mismatch invalidated the loaded navigation mesh")) return false;
	const VansNavigationPath loadedPath = QueryNavigationPath(loaded, settings.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(loadedPath.status == VansNavigationPathStatus::Complete &&
		loadedPath.points.size() >= 3u,
		"Saved navigation asset did not preserve query behavior"))
	{
		return false;
	}

	VansNavigationSettings corridorLimit = settings;
	corridorLimit.query.maximumCorridorPolygons = 2;
	auto corridorLimitedMesh = std::make_shared<VansNavigationMap>();
	if (!Expect(LoadNavigationMap(assetPath, corridorLimit, corridorLimitedMesh, error),
		error))
	{
		return false;
	}
	const VansNavigationPath corridorLimitedPath = QueryNavigationPath(corridorLimitedMesh, corridorLimit.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(corridorLimitedPath.status == VansNavigationPathStatus::Failed &&
		corridorLimitedPath.failure ==
			VansNavigationPathFailure::CorridorCapacityExceeded &&
		corridorLimitedPath.points.empty(),
		"Detour corridor capacity exhaustion was not reported structurally"))
	{
		return false;
	}

	VansNavigationSettings cornerLimit = settings;
	cornerLimit.query.maximumPathPoints = 2;
	auto cornerLimitedMesh = std::make_shared<VansNavigationMap>();
	if (!Expect(LoadNavigationMap(assetPath, cornerLimit, cornerLimitedMesh, error), error))
		return false;
	const VansNavigationPath cornerLimitedPath = QueryNavigationPath(cornerLimitedMesh, cornerLimit.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(cornerLimitedPath.status == VansNavigationPathStatus::Failed &&
		cornerLimitedPath.failure ==
			VansNavigationPathFailure::CornerCapacityExceeded &&
		cornerLimitedPath.points.empty(),
		"Detour corner capacity exhaustion was not reported structurally"))
	{
		return false;
	}

	VansNavigationSettings nodeLimit = settings;
	nodeLimit.query.maximumSearchNodes = 4;
	auto nodeLimitedMesh = std::make_shared<VansNavigationMap>();
	if (!Expect(LoadNavigationMap(assetPath, nodeLimit, nodeLimitedMesh, error), error))
		return false;
	const VansNavigationPath nodeLimitedPath = QueryNavigationPath(nodeLimitedMesh, nodeLimit.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	if (!Expect(nodeLimitedPath.status == VansNavigationPathStatus::Failed &&
		nodeLimitedPath.failure ==
			VansNavigationPathFailure::SearchNodeCapacityExceeded &&
		nodeLimitedPath.points.empty(),
		"Detour search-node exhaustion was not reported structurally"))
	{
		return false;
	}

	const std::filesystem::path corruptAssetPath =
		std::filesystem::temp_directory_path() / "ForestNavigationContractCorrupt.vnavmesh";
	ScopedFileRemoval corruptAssetRemoval(corruptAssetPath);
	std::ifstream validAsset(assetPath, std::ios::binary);
	const std::istreambuf_iterator<char> assetBegin(validAsset);
	const std::istreambuf_iterator<char> assetEnd;
	std::vector<unsigned char> corruptAsset(assetBegin, assetEnd);
	std::size_t areaHeaderSize = sizeof(std::uint32_t) * 2u + settings.areas.defaultArea.size();
	for (const auto& area : settings.areas.definitions)
		areaHeaderSize += sizeof(std::uint32_t) + area.name.size() +
			sizeof(std::uint8_t) * 2u + sizeof(float);
	const std::size_t headerSize = sizeof(std::uint32_t) * 2u +
		sizeof(float) * 12u + sizeof(int) + sizeof(std::uint8_t) +
		sizeof(std::uint32_t) + navigationTestSource.scene.size() +
		sizeof(std::uint64_t) * 3u + areaHeaderSize;
	if (!Expect(!validAsset.bad() && corruptAsset.size() > headerSize,
		"Could not prepare a corrupt navigation asset for rollback validation"))
	{
		return false;
	}
	std::fill(corruptAsset.begin() + static_cast<std::ptrdiff_t>(headerSize),
		corruptAsset.end(), 0u);
	std::ofstream corruptAssetStream(corruptAssetPath,
		std::ios::binary | std::ios::trunc);
	corruptAssetStream.write(reinterpret_cast<const char*>(corruptAsset.data()),
		static_cast<std::streamsize>(corruptAsset.size()));
	corruptAssetStream.close();
	if (!Expect(static_cast<bool>(corruptAssetStream),
		"Could not write the corrupt navigation asset"))
	{
		return false;
	}

	const bool corruptLoadSucceeded = LoadNavigationMap(corruptAssetPath, settings, loaded, error);
	const VansNavigationPath preservedPath = QueryNavigationPath(loaded, settings.query,
		glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f));
	return Expect(!corruptLoadSucceeded,
		"Corrupt navigation data unexpectedly loaded") &&
		Expect(loaded->IsReady() &&
			preservedPath.status == VansNavigationPathStatus::Complete &&
			preservedPath.points.size() >= 3u,
			"Failed navigation loading destroyed the previous query state");
}


bool RunDemoHallNavigationContractTests(const std::filesystem::path& root, VansGraphics::VansAnimationNode* whisperAnimation)
{
    using namespace Vans;
    nlohmann::ordered_json projectConfig, scene;
    std::string error;
    if(!VansJsonFileStorage::Read(root/"ForestProject.json",projectConfig,error)) return Expect(false,error);
    const auto scenePath=projectConfig.at("defaultScene").get<std::string>();
    if(!VansJsonFileStorage::Read(root/scenePath,scene,error)) return Expect(false,error);
    std::cout<<"[DemoHallNavigation] project="<<root.filename().string()<<'\n';
    const auto& entities=scene.at("entities");
    const auto whisperSource=std::find_if(entities.begin(),entities.end(),[](const auto& entity){return entity.at("name")=="Whisper";});
    if(!Expect(whisperSource!=entities.end(),"Navigation fixture has no Whisper"))return false;
    const auto& components=whisperSource->at("components");
    const auto authoredTransform=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="Transform";});
    if(!Expect(authoredTransform!=components.end(),"Navigation fixture has no Whisper transform"))return false;
    const auto& authoredPose=authoredTransform->at("data");
    const auto& p=authoredPose.at("position");const auto& q=authoredPose.at("rotation");
    const glm::vec3 authoredStart(p[0].get<float>(),p[1].get<float>(),p[2].get<float>());
    const glm::quat authoredRotation(q[3].get<float>(),q[0].get<float>(),q[1].get<float>(),q[2].get<float>());
    const glm::vec3 referenceStart(-110.354691696f,42.010624634f,-93.648826218f);
    const glm::quat referenceRotation(.36188310384750366f,5.189886e-7f,.9322234988212585f,5.189886e-7f);
    const auto frameRotation=glm::normalize(authoredRotation*glm::conjugate(glm::normalize(referenceRotation)));
    const auto point=[&](glm::vec3 reference){return authoredStart+frameRotation*(reference-referenceStart);};
    const auto frameForward=frameRotation*glm::vec3(0,0,1);
    const float frameYaw=glm::degrees(std::atan2(frameForward.x,frameForward.z));
    VansNavigationSettings settings;
    if(!VansNavigationSettingsStorage::Load((root/"ProjectSettings/NavigationSettings.json").string(),settings,error)) return Expect(false,error);
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    if(!VansNavigationMeshStorage::Load(root/"Assets/Navigation/DemoHall.vnavmesh",asset,error)) return Expect(false,error);
    std::string cooked;
    std::shared_ptr<const VansNavigationMeshAsset> memory;
    if(!VansNavigationMeshCodec::Encode(*asset,cooked,error) ||
        !VansNavigationMeshCodec::Decode(cooked,memory,error)) return Expect(false,error);
    std::string original;
    if(!VansFileStorage::ReadAllBytes(root/"Assets/Navigation/DemoHall.vnavmesh",original,error) ||
        !Expect(cooked==original && asset->GetPayloadBytes()==memory->GetPayloadBytes() &&
            asset->EstimateResidentBytes()>=asset->GetPayloadBytes(),
            "DemoHall owned asset changed cooked bytes or lost its complete source payload")) return false;
    glm::vec3 start(0); std::vector<glm::vec3> players;
    bool hasAgent=false,hasSurface=false;
    for(const auto& entity:scene.at("entities"))
    {
        glm::vec3 position(0); bool player=false;
        for(const auto& component:entity.at("components"))
        {
            const auto type=component.at("type").get<std::string>(); const auto& data=component.at("data");
            if(type=="Transform") { const auto& p=data.at("position"); position=glm::vec3(p[0].get<float>(),p[1].get<float>(),p[2].get<float>()); }
            if(type=="ActionHost")
                for(const auto& initializer:data.value("initializers",nlohmann::ordered_json::array()))
                    player=player || (initializer.value("type",std::string{})=="Gameplay.Tags.Initialize" &&
                        initializer.value("inputs",nlohmann::ordered_json::object()).value("tag",std::string{})=="Target.Character.Player");
        }
        if(player) players.push_back(position);
        for(const auto& component:entity.at("components"))
        {
            const auto type=component.at("type").get<std::string>();
            if(type=="NavigationAgent")
            {
                VansRuntimeNavigationAgentComponent agent;
                if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(component.at("data")),agent,error)) return Expect(false,error);
                if(!Expect(agent.mapId=="World" && agent.profileId=="Humanoid" && agent.queryFilter=="Default" &&
                    std::abs(agent.maxSpeed-1.5f)<1.e-5 && std::abs(agent.acceleration-6)<1.e-5,"DemoHall Agent movement changed during Surface migration")) return false;
                start=position; hasAgent=component.value("enabled",false);
            }
            if(type=="NavigationSurface")
            {
                VansRuntimeNavigationSurfaceComponent surface;
                if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(component.at("data")),surface,error)) return Expect(false,error);
                if(!Expect(surface.mapId=="World" && surface.navigationMeshGuid=="fcb8f186-b52d-48fc-bd57-8315d4b0fcad" &&
                    surface.profiles==std::vector<std::string>{"Humanoid"} && surface.collection==VansNavigationCollection::ExplicitSources &&
                    surface.collisionLayers==std::vector<std::string>{"Environment"},"DemoHall Surface changed its existing collision policy or asset identity")) return false;
                const auto* profile=asset->FindProfile("Humanoid");
                VansNavigationSurfaceRecord record{component.at("id").get<std::string>(),entity.at("id").get<std::string>(),component.value("enabled",false),glm::mat4(1),surface};
                record.worldTransform[3]=glm::vec4(position,1);
                std::uint64_t hash=0;
                if(!ComputeNavigationSurfaceContributionHash(record,hash,error)) return false;
                hasSurface=profile && std::any_of(profile->contributions.begin(),profile->contributions.end(),[&](const auto& c){return c.id=="surface/"+record.id && c.contentHash==hash;});
                if(!Expect(glm::distance(surface.boundsCenterMeters,point({-90.2f,44.5f,-86.1f}))<.001f &&
                    glm::distance(surface.boundsHalfExtentsMeters,glm::vec3(21,3,14))<.001f && surface.sources.size()==267,
                    "DemoHall indoor Surface bounds or explicit collision closure changed")) return false;
            }
        }
    }
    if(!Expect(hasAgent && hasSurface && players.size()==2 && asset->GetSource().scene==scenePath,
        "DemoHall current source closure lacks its Surface, Agent or two player targets")) return false;
    auto map=std::make_shared<VansNavigationMap>(); VansNavigationQueryContext query;
    if(!map->Initialize(asset,settings,"Humanoid",error) || !query.Initialize(map,settings.query,"Default",error)) return Expect(false,error);
    for(const auto& player:players)
        if(!Expect(query.FindPath(start,player).status==VansNavigationPathStatus::Complete,"Whisper cannot reach an indoor player spawn")) return false;
    const auto vector=[](const nlohmann::ordered_json& v){return glm::vec3(v[0].get<float>(),v[1].get<float>(),v[2].get<float>());};
    const auto* profile=asset->FindProfile("Humanoid");
    if(!Expect(profile && std::abs(profile->bake.cellSize-.1f)<1.e-5f && std::abs(profile->bake.cellHeight-.05f)<1.e-5f &&
        profile->bake.agentMaxClimb<=.25f && asset->GetTileCount()<64,"Indoor bake lost stair precision or included outdoor coverage")) return false;
    for(const auto& outdoorPoint:std::vector<glm::vec3>{{-87.2f,40,-65},{-87.2f,42,-70},{0,0,0},{-110,42,-103}})
        if(!Expect(!query.ProjectPoint(point(outdoorPoint),{.1f,.2f,.1f}).valid,"Outdoor point is walkable in indoor-only navigation")) return false;
    for(const float x:{-90.7f,-83.7f})
    {
        const auto bottom=point({x,42,-80}),top=point({x,45,-87.4f});
        for(const auto& route:std::vector<std::pair<glm::vec3,glm::vec3>>{{bottom,top},{top,bottom}})
        {
            const auto path=query.FindPath(route.first,route.second,{.25f,.3f,.25f});
            if(!Expect(path.status==VansNavigationPathStatus::Complete,"Indoor stairs are disconnected")) return false;
            std::cout<<"[DemoHallStairPath] x="<<x<<" fromY="<<route.first.y<<" toY="<<route.second.y<<" points="<<path.points.size()<<'\n';
        }
    }
    for(const auto& entity:scene.at("entities"))
    {
        if(entity.at("name")!="Couch" && entity.at("name")!="Desk_Large") continue;
        const auto& components=entity.at("components");
        const auto transform=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="Transform";});
        const auto physics=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="Physics";});
        const auto& pose=transform->at("data"); const auto& q=pose.at("rotation");
        const glm::quat rotation(q[3].get<float>(),q[0].get<float>(),q[1].get<float>(),q[2].get<float>());
        auto center=vector(pose.at("position"))+rotation*(vector(physics->at("data").at("shapeOffset"))*vector(pose.at("scale")));
        center.y=point({0,42,0}).y;
        if(!Expect(!query.ProjectPoint(center,{.02f,.15f,.02f}).valid,"Furniture footprint remained walkable")) return false;
        if(entity.at("name")=="Couch")
        {
            // 沙发贴墙，背面不是可走区域；验证占地阻挡，不要求穿过背面的路线。
            std::cout<<"[DemoHallFurniture] Couch footprint blocked\n";
            continue;
        }
        bool bypass=false;
        for(const float radius:{1.5f,2.2f,3.f}) for(int direction=0;direction<16 && !bypass;++direction)
        {
            const float angle=direction*glm::pi<float>()/8; const glm::vec3 offset(radius*std::cos(angle),0,radius*std::sin(angle));
            const auto ray=query.NavRaycast(center-offset,center+offset,{.1f,.2f,.1f});
            const auto path=query.FindPath(center-offset,center+offset,{.1f,.2f,.1f});
            bypass=ray.valid && ray.hit && path.status==VansNavigationPathStatus::Complete && path.points.size()>2;
        }
        if(!Expect(bypass,"No complete route around indoor furniture: "+entity.at("name").get<std::string>())) return false;
        std::cout<<"[DemoHallFurniture] "<<entity.at("name")<<" footprint blocked, ray blocked, bypass Complete\n";
    }
    // 使用源场景的真实碰撞、Whisper 胶囊及生产 Scene motor；不以路径连通替代实际爬阶。
    using namespace VansEngine;
    VANS_INIT_MAIN_THREAD(); VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);
    auto& jobs=VansJobSystem::Get(); jobs.Initialize(2);
    VansRuntimePhaseGuard jobsCleanup([&](){jobs.Shutdown();});
    auto& physics=VansPhysicsSystem::GetInstance();
    if(!Expect(physics.Initialize(),"DemoHall CCT physics failed")) return false;
    VansRuntimePhaseGuard physicsCleanup([&](){physics.Shutdown();});
    auto& layers=VansCollisionLayerManager::Get(); VansCollisionLayerConfig oldLayers;
    oldLayers.layerCount=layers.GetLayerCount();
    for(int i=0;i<oldLayers.layerCount;++i){oldLayers.layerNames[i]=layers.GetLayerName(i);oldLayers.collisionMasks[i]=layers.GetCollisionMask(i);}
    VansRuntimePhaseGuard layersCleanup([&](){layers.ApplyConfig(oldLayers);});
    VansCollisionLayerConfig fixtureLayers; fixtureLayers.ResetToDefaults(); fixtureLayers.layerCount=4;
    fixtureLayers.layerNames[1]="Environment"; fixtureLayers.layerNames[2]="Enemy";
    fixtureLayers.layerNames[3]="Player";
    fixtureLayers.collisionMasks[0]=15; fixtureLayers.collisionMasks[1]=13; fixtureLayers.collisionMasks[2]=11;
    fixtureLayers.collisionMasks[3]=7; layers.ApplyConfig(fixtureLayers);
    std::vector<std::uint32_t> transforms;
    VansRuntimePhaseGuard transformsCleanup([&](){for(auto id:transforms)VansTransformStore::Release(id);});
    const auto allocate=[&](const nlohmann::ordered_json& data,bool yawOnly)
    {
        const auto id=VansTransformStore::Allocate(); transforms.push_back(id); VansTransform pose;
        pose.m_Position=vector(data.at("position")); pose.m_Scale=vector(data.at("scale")); const auto& q=data.at("rotation");
        const auto rotation=ProjectSceneQuaternionToEulerDegrees({q[0].get<float>(),q[1].get<float>(),q[2].get<float>(),q[3].get<float>()},yawOnly);
        pose.m_Rotation={rotation[0],rotation[1],rotation[2]};
        VansTransformStore::Write(id,pose); return id;
    };
    std::vector<std::unique_ptr<VansPhysicsNode>> colliders; VansRuntimeWorld runtime;
    std::vector<VansPhysicsNode*> breakableWindows;
    VansEntityHandle agentOwner; std::uint32_t agentTransform=0; VansRuntimeNavigationAgentComponent agentConfig;
    CharControllerProperties capsule;
    for(const auto& entity:scene.at("entities"))
    {
        const auto& components=entity.at("components");
        const auto source=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="NavigationSource";});
        const auto surface=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="NavigationSurface";});
        const bool whisper=entity.at("name")=="Whisper";
        if(source==components.end() && surface==components.end() && !whisper) continue;
        VansEntityCreateDesc desc; desc.stableGuid=entity.at("id").get<std::string>(); desc.name=entity.at("name").get<std::string>();
        const auto owner=runtime.CreateEntity(desc);
        const auto transform=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="Transform";});
        const auto id=allocate(transform->at("data"),whisper);
        runtime.AddComponent(owner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{id},transform->at("id").get<std::string>());
        if(source!=components.end())
        {
            const auto component=std::find_if(components.begin(),components.end(),[](const auto& c){return c.at("type")=="Physics";}); const auto& data=component->at("data");
            PhysicsNodeProperties properties; properties.enabled=true; properties.layerName=data.at("layer").get<std::string>();
            properties.boxExtents=vector(data.at("boxExtents")); properties.shapeOffset=vector(data.at("shapeOffset"));
            properties.material.staticFriction=data.at("material").at("staticFriction");
            properties.material.dynamicFriction=data.at("material").at("dynamicFriction");
            properties.material.restitution=data.at("material").at("restitution");
            colliders.push_back(std::make_unique<VansPhysicsNode>()); colliders.back()->Initialize(properties,id);
            if(!Expect(colliders.back()->HasActor(),"Indoor collider not created")) return false;
            if(std::any_of(components.begin(),components.end(),[](const auto& c)
                {return c.at("type")=="Script" && c.at("data").value("entry",std::string{})=="GlassBreakShotReceiver";}))
                breakableWindows.push_back(colliders.back().get());
            runtime.AddComponent(owner,VansRuntimeComponentType_Physics,VansRuntimePhysicsComponent{colliders.back().get()},component->at("id").get<std::string>());
            VansRuntimeNavigationSourceComponent value;
            if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(source->at("data")),value,error)) return Expect(false,error);
            runtime.AddComponent(owner,VansRuntimeComponentType_NavigationSource,value,source->at("id").get<std::string>());
        }
        if(surface!=components.end())
        {
            VansRuntimeNavigationSurfaceComponent value;
            if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(surface->at("data")),value,error)) return Expect(false,error);
            runtime.AddComponent(owner,VansRuntimeComponentType_NavigationSurface,value,surface->at("id").get<std::string>());
        }
        if(whisper)
        {
            agentOwner=owner; agentTransform=id;
            for(const auto& component:components)
            {
                const auto& data=component.at("data");
                if(component.at("type")=="NavigationAgent")
                {
                    if(!VansSceneNavigationComponentReader::Read(DecodeSerializedValueJson(data),agentConfig,error)) return Expect(false,error);
                    runtime.AddComponent(owner,VansRuntimeComponentType_NavigationAgent,agentConfig,component.at("id").get<std::string>());
                }
                if(component.at("type")=="CharacterController")
                {
                    capsule.m_Radius=data.at("radius");capsule.m_Height=data.at("height");capsule.m_SlopeLimit=data.at("slopeLimit");
                    capsule.m_StepOffset=data.at("stepOffset");capsule.m_ContactOffset=data.at("contactOffset");capsule.m_PositionOffset=vector(data.at("positionOffset"));
                    capsule.m_LayerName=data.at("layer");capsule.m_ClimbingMode=VansCharacterClimbingMode::Constrained;
                }
            }
        }
    }
    VansCharacterControllerNode controller;
    if(!Expect(controller.Initialize(capsule,agentTransform,start+capsule.m_PositionOffset),"Whisper capsule not created")) return false;
    runtime.AddComponent(agentOwner,VansRuntimeComponentType_CharacterController,VansRuntimeCharacterControllerComponent{&controller},"Indoor.Whisper.CCT");
    VansAssetObjectRepository repository; VansAssetGuid guid; VansAssetGuid::TryParse("fcb8f186-b52d-48fc-bd57-8315d4b0fcad",guid);
    if(!repository.Publish<VansNavigationMeshAsset>(guid,VansAssetType::NavigationMesh,1,asset,{},error).IsValid()) return Expect(false,error);
    VansSceneNavigationRuntime integration; VansRuntimeNavigationSources provider; VansNavigationSourceSnapshot sources; std::uint64_t observation=0;
    if(!integration.Initialize(runtime,repository,settings,error) || !provider.Capture(runtime,settings,{}, {},sources,observation,error) ||
        !integration.Synchronize(sources,observation,error)) return Expect(false,error);
    auto& world=integration.World(); VansCharacterMotionSettings motion;motion.driveMode=VansLocomotionDriveMode::Capsule;
    const auto tick=[&]()
    {
        PaceNavigationWorkerFrame(world);world.Update(1./60.);controller.PrepareLocomotion(1.f/60,motion);
        controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motion,glm::vec3(1));
        std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());controller.FlushMoveAndSync();
    };
    std::vector<std::pair<glm::vec3,glm::vec3>> motorRoutes;
    for(const float x:{-90.7f,-83.7f})
    {
        const auto bottom=point({x,42,-80}),top=point({x,45,-87.4f});
        motorRoutes.push_back({bottom,top});motorRoutes.push_back({top,bottom});
    }
    motorRoutes.push_back({start,players.front()});
    for(const auto& route:motorRoutes)
    {
        const auto& from=route.first;const auto& to=route.second;
        auto pose=VansTransformStore::Read(agentTransform);pose.m_Position=from;
        VansTransformStore::Write(agentTransform,pose);
        {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());controller.SyncControllerFromTransform();}
        controller.SeedGroundedContact(true);
        for(int frame=0;frame<30;++frame)tick();
        VansNavigationMoveOptions options;options.arrivalRadius=.2f;
        const auto request=world.RequestMove(agentOwner,"Indoor.Stairs",to,options,error);
        if(!Expect(request.IsValid(),error)) return false;
        std::optional<VansNavigationAgentSnapshot> state;
        for(int frame=0;frame<3600;++frame){tick();state=world.CaptureAgent(agentOwner);if(state && state->state==VansNavigationMoveState::Finished)break;}
        const auto feet=controller.GetPosition()-capsule.m_PositionOffset;
        std::cout<<"[DemoHallIndoorCCT] from="<<from.x<<','<<from.y<<','<<from.z<<" feet="<<feet.x<<','<<feet.y<<','<<feet.z<<" result="<<(state?static_cast<int>(state->result):-1)
            <<" recovery="<<(state?state->recoveryCount:0)<<" diagnostic="<<(state?state->path.diagnostic:std::string{})<<'\n';
        if(!Expect(state && state->result==VansNavigationMoveResult::ReachedGoal && std::abs(feet.y-to.y)<.15f &&
            glm::distance(glm::vec2(feet.x,feet.z),glm::vec2(state->path.projectedEnd.x,state->path.projectedEnd.z))<options.arrivalRadius+.03f &&
            glm::distance(glm::vec2(to.x,to.z),glm::vec2(state->path.projectedEnd.x,state->path.projectedEnd.z))<.25f,
            "Whisper CCT failed actual indoor movement or stair traversal")) return false;
        if(!world.Stop(request,"Indoor.Stairs",error)) return Expect(false,error);
    }
    // 正常射击会撤销完整玻璃的碰撞；不能因此撤销整张室内 Map 和 Whisper motor。
    if(!Expect(!breakableWindows.empty(),"DemoHall has no destructible window fixture")) return false;
    for(auto* window:breakableWindows)
    {
        window->SetEnabled(false);
        if(!provider.Capture(runtime,settings,{}, {},sources,observation,error) ||
            !integration.Synchronize(sources,observation,error)) return Expect(false,"Window break invalidated indoor navigation: "+error);
        if(!Expect(world.HasAgent(agentOwner),"Window break withdrew Whisper navigation motor")) return false;
        for(int frame=0;frame<3600 && (integration.CaptureTileUpdateStats().activeBuilds ||
            integration.CaptureTileUpdateStats().pendingMaps);++frame)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(!integration.Synchronize(sources,observation,error)) return Expect(false,error);
        }
        const auto update=integration.CaptureTileUpdateStats();
        if(!Expect(!update.activeBuilds && !update.pendingMaps && update.failedMaps==0,
            "Window break did not commit its bounded local navigation update")) return false;
    }
    std::cout<<"[DemoHallWindowNavigation] PASS: destroyed="<<breakableWindows.size()
        <<" localCommits="<<integration.CaptureTileUpdateStats().publishedCells<<" motor retained\n";
    VansAIBehaviorAsset behavior;
    if(!VansAIBehaviorAssetStorage::Load(root/"Assets/AI/WhisperChase.vaibehavior",behavior,error)) return Expect(false,error);
    const auto* chase=behavior.FindState("Chase"); const auto* patrol=behavior.FindState("Patrol");
    if(!Expect(chase && chase->task==VansAITaskKind::MoveToTarget && std::abs(chase->moveToTarget.arrivalRadius-1.6f)<1.e-5 &&
        patrol && patrol->patrol && std::abs(patrol->patrol->radius-4)<1.e-5 && std::abs(patrol->patrol->waitSeconds-1.25f)<1.e-5,
        "DemoHall Chase arrival radius or Patrol behavior changed")) return false;
    if(whisperAnimation)
    {
        // 使用同一 GAF 验收已编译的项目 Animator；正常 AI 决策每帧先于导航和 CCT。
        VansSceneContentBuildPlan plan;
        const auto player=std::find_if(entities.begin(),entities.end(),[](const auto& object){return object.at("name")=="SurvivalCharacter";});
        if(!Expect(whisperSource!=entities.end() && player!=entities.end(),"Pursuit fixture lacks actual AI/player entities")) return false;
        VansAssetDatabase authoringAssets(root/"Assets",root/"Library/Artifacts");
        const auto scan=authoringAssets.Scan(VansAssetOperationPolicy::ReadOnly());
        if(!Expect(static_cast<bool>(scan),scan.errors.empty()?"Pursuit source asset index failed":scan.errors.front())) return false;
        if(!VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(VansSerializedValue::Array({DecodeSerializedValueJson(*whisperSource)}),
            root.string(),[&](VansAssetGuid requested){return authoringAssets.Find(requested);},plan,error)) return Expect(false,error);
        const auto whisper=plan.objects.objects.begin();
        const auto playerPhysics=VansScenePhysicsComponentReader::ReadAuthoringComponents(DecodeSerializedValueJson(*player));
        if(!Expect(whisper!=plan.objects.objects.end() && whisper->aiAgent && playerPhysics.characterController,
            "Pursuit fixture lacks actual AI/player configuration")) return false;
        VansAssetGuid behaviorGuid; VansAssetGuid::TryParse(whisper->aiAgent->runtime.behaviorGuid,behaviorGuid);
        if(!repository.Publish<VansAIBehaviorAsset>(behaviorGuid,VansAssetType::AIBehavior,1,
            std::make_shared<const VansAIBehaviorAsset>(behavior),{},error).IsValid()) return Expect(false,error);
        runtime.AddComponent(agentOwner,VansRuntimeComponentType_AIAgent,whisper->aiAgent->runtime,"Indoor.Whisper.AI");
        runtime.AddComponent(agentOwner,VansRuntimeComponentType_Animation,VansRuntimeAnimationComponent{whisperAnimation},"Indoor.Whisper.Animation");
        auto pose=VansTransformStore::Read(agentTransform);pose.m_Position=start;
        VansTransformStore::Write(agentTransform,pose);VansTransformStore::MarkDirty(agentTransform);
        {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());controller.SyncControllerFromTransform();}
        controller.SeedGroundedContact(true);
        whisperAnimation->SetTransformID(agentTransform);
        auto* animator=whisperAnimation->GetCharacterMotionController();animator->Play();
        animator->Update(0,whisperAnimation->GetSkeleton());
        const auto targetPosition=point({-107,42,-92});
        VansEntityCreateDesc playerDesc;playerDesc.name=player->at("name").get<std::string>();playerDesc.stableGuid=player->at("id").get<std::string>();
        const auto playerOwner=runtime.CreateEntity(playerDesc);
        nlohmann::ordered_json playerPose={{"position",{targetPosition.x,targetPosition.y,targetPosition.z}},
            {"rotation",{0,0,0,1}},{"scale",{1,1,1}}};
        const auto playerTransform=allocate(playerPose,false);
        runtime.AddComponent(playerOwner,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{playerTransform},"Indoor.Player.Transform");
        const auto& playerProperties=*playerPhysics.characterController;
        CharControllerProperties playerCapsule;
        playerCapsule.m_Radius=*playerProperties.radius;playerCapsule.m_Height=*playerProperties.height;
        playerCapsule.m_SlopeLimit=*playerProperties.slopeLimit;playerCapsule.m_StepOffset=*playerProperties.stepOffset;
        playerCapsule.m_ContactOffset=*playerProperties.contactOffset;playerCapsule.m_LayerName=*playerProperties.layer;
        const auto& playerOffset=*playerProperties.positionOffset;
        playerCapsule.m_PositionOffset={playerOffset[0],playerOffset[1],playerOffset[2]};
        playerCapsule.m_ClimbingMode=VansCharacterClimbingMode::Constrained;
        VansCharacterControllerNode playerController;
        if(!Expect(playerController.Initialize(playerCapsule,playerTransform,targetPosition+playerCapsule.m_PositionOffset),"Pursuit player CCT failed")) return false;
        runtime.AddComponent(playerOwner,VansRuntimeComponentType_CharacterController,VansRuntimeCharacterControllerComponent{&playerController},"Indoor.Player.CCT");
        VansAssetRecord tags;tags.type=VansAssetType::GameplayTagTree;
        VansAssetGuid::TryParse("d3174953-0889-46f4-904c-a3fc36fc5a49",tags.guid);
        tags.sourcePath=tags.authoringPath=root/"Assets/GAF/WindowBreak/DemoHallTags.vtagtree";
        const auto bootstrap=VansAssetObjectBootstrapper::Publish({tags},repository);
        if(!Expect(static_cast<bool>(bootstrap),bootstrap.errors.empty()?"Pursuit tag bootstrap failed":bootstrap.errors.front())) return false;
        VansGameplayRuntime gameplay;VansGameplayRuntimeDependencies dependencies;
        dependencies.contributors.push_back(VansMakeGameplayPrimitivesGAFContributor());
        if(!gameplay.Initialize({tags},repository,VansGAFSettings{},dependencies,error)) return Expect(false,error);
        VansGameplayActionHostSetup hostSetup;
        hostSetup.initializers.push_back({"Gameplay.Tags.Initialize",DecodeSerializedValueJson(
            nlohmann::ordered_json{{"tag","Target.Character.Player"},{"count",1}})});
        const auto playerHost=gameplay.CreateHost(playerOwner,hostSetup,error);
        if(!playerHost) return Expect(false,error);
        const auto targetTag=gameplay.Assets().Tags().FindId(whisper->aiAgent->runtime.targetTag);
        if(!Expect(targetTag && playerHost->Tags().Has(*targetTag) && playerController.IsEnabled(),
            "Pursuit player is not an enabled tagged target")) return false;
        VansAIWorld ai;
        if(!ai.Initialize(runtime,&gameplay,repository,world,error)) return Expect(false,error);
        const auto aiTick=[&]()
        {
            VansEventBus::Get().BeginFrame();VansEventBus::Get().Flush(VansEventLane::GameLogic);
            if(!provider.Capture(runtime,settings,{}, {},sources,observation,error) ||
                !integration.Synchronize(sources,observation,error)) return false;
            ai.Update(1./60.);PaceNavigationWorkerFrame(world);world.Update(1./60.);
            controller.PrepareLocomotion(1.f/60,motion);
            whisperAnimation->PrepareCharacterMotionFrame(1.f/60,controller.GetTrajectory());
            controller.ResolveLocomotion({},glm::quat(1,0,0,0),false,{},motion,glm::vec3(1));
            {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());controller.FlushMoveAndSync();}
            return true;
        };
        for(int frame=0;frame<30;++frame)if(!aiTick())return Expect(false,error);
        if(!Expect(ai.CaptureDiagnostics().agents.front().currentState=="Dormant" &&
            whisperAnimation->GetCurrentStateName()=="Dead","Whisper moved before its pickup wake gate")) return false;
        VansEventBus::Get().Enqueue(VansAIActivationRequested{agentOwner,"Indoor.Pickup.Script"},VansEventLane::GameLogic);
        VansEventBus::Get().Enqueue(VansAIGameplayReleased{agentOwner,"Indoor.Pickup.Script"},VansEventLane::GameLogic);
        animator->SetTrigger("WakeUp");
        bool awakened=false,chased=false,walked=false,reached=false;
        for(int frame=0;frame<1800;++frame)
        {
            if(!aiTick())return Expect(false,error);
            const auto diagnostic=ai.CaptureDiagnostics().agents.front();
            awakened=awakened || diagnostic.currentState=="Awakening";
            chased=chased || (diagnostic.currentState=="Chase" && diagnostic.target==playerOwner);
            walked=walked || (whisperAnimation->GetCurrentStateName()=="Walk1" && diagnostic.commandedSpeed>.1f);
            const auto move=world.CaptureAgent(agentOwner);
            if(chased && move && move->result==VansNavigationMoveResult::ReachedGoal){reached=true;break;}
        }
        const auto diagnostic=ai.CaptureDiagnostics().agents.front();
        const auto feet=controller.GetPosition()-capsule.m_PositionOffset;
        std::cout<<"[DemoHallWhisperPursuit] wake="<<awakened<<" chase="<<chased<<" walk="<<walked<<" reached="<<reached
            <<" state="<<diagnostic.currentState<<" animation="<<whisperAnimation->GetCurrentStateName()
            <<" target="<<diagnostic.targetName<<" feet="<<feet.x<<','<<feet.y<<','<<feet.z
            <<" diagnostic="<<diagnostic.diagnostic<<" path="<<diagnostic.pathDiagnostic<<'\n';
        if(!Expect(awakened && chased && walked && reached && world.HasAgent(agentOwner) &&
            glm::distance(glm::vec2(feet.x,feet.z),glm::vec2(start.x,start.z))>1.0f,
            "Normal Whisper wake/AI/Animator/CCT failed pursuit after glass destruction")) return false;
        const auto movedTarget=point({-107,42,-95});
        auto movedPose=VansTransformStore::Read(playerTransform);movedPose.m_Position=movedTarget;
        VansTransformStore::Write(playerTransform,movedPose);VansTransformStore::MarkDirty(playerTransform);
        {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());playerController.SyncControllerFromTransform();}
        const auto previousRequest=world.CaptureAgent(agentOwner)->request.serial;
        bool reachedMovedTarget=false;
        for(int frame=0;frame<1800;++frame)
        {
            if(!aiTick())return Expect(false,error);
            const auto move=world.CaptureAgent(agentOwner);
            const auto movingDiagnostic=ai.CaptureDiagnostics().agents.front();
            if(move && movingDiagnostic.currentState=="Chase" && movingDiagnostic.target==playerOwner &&
                glm::distance(move->goal,movedTarget)<.001f &&
                move->request.serial!=previousRequest && move->result==VansNavigationMoveResult::ReachedGoal)
            {reachedMovedTarget=true;break;}
        }
        const auto movedFeet=controller.GetPosition()-capsule.m_PositionOffset;
        const auto finalMove=world.CaptureAgent(agentOwner);
        const auto finalDiagnostic=ai.CaptureDiagnostics().agents.front();
        std::cout<<"[DemoHallWhisperMovingTarget] reached="<<reachedMovedTarget<<" feet="<<movedFeet.x<<','<<movedFeet.y<<','<<movedFeet.z
            <<" state="<<finalDiagnostic.currentState<<" target="<<finalDiagnostic.targetName
            <<" recovery="<<(finalMove?finalMove->recoveryCount:0)<<" path="<<(finalMove?finalMove->path.diagnostic:std::string{})<<'\n';
        if(!Expect(reachedMovedTarget && finalMove->recoveryCount==0 &&
            glm::distance(glm::vec2(movedFeet.x,movedFeet.z),glm::vec2(movedTarget.x,movedTarget.z))<=chase->moveToTarget.arrivalRadius &&
            glm::distance(glm::vec2(movedFeet.x,movedFeet.z),glm::vec2(feet.x,feet.z))>1.0f,
            "Whisper did not resume actual pursuit when its player target moved")) return false;
        const std::vector<std::pair<glm::vec3,glm::vec3>> chaseStairRoutes{
            {point({-87.2f,45,-88}),point({-87.2f,42,-82})},
            {point({-90.7f,45,-87.4f}),point({-90.7f,42,-78})},
            {point({-83.7f,45,-87.4f}),point({-83.7f,42,-78})}};
        for(const auto& route:chaseStairRoutes)
        {
            pose=VansTransformStore::Read(agentTransform);pose.m_Position=route.first;pose.m_Rotation.y=frameYaw;
            VansTransformStore::Write(agentTransform,pose);VansTransformStore::MarkDirty(agentTransform);
            movedPose=VansTransformStore::Read(playerTransform);movedPose.m_Position=route.second;
            VansTransformStore::Write(playerTransform,movedPose);VansTransformStore::MarkDirty(playerTransform);
            {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
                controller.SyncControllerFromTransform();playerController.SyncControllerFromTransform();}
            controller.SeedGroundedContact(true);
            bool pursued=false,reachedFloor=false,temporarilyOccluded=false;
            for(int frame=0;frame<3600;++frame)
            {
                if(!aiTick())return Expect(false,error);
                const auto stairDiagnostic=ai.CaptureDiagnostics().agents.front();
                const auto move=world.CaptureAgent(agentOwner);
                pursued=pursued || stairDiagnostic.currentState=="Chase";
                temporarilyOccluded=temporarilyOccluded || stairDiagnostic.lineOfSight.blocked;
                if(move && stairDiagnostic.currentState=="Chase" && move->result==VansNavigationMoveResult::ReachedGoal &&
                    glm::distance(move->goal,route.second)<.001f){reachedFloor=true;break;}
            }
            const auto stairDiagnostic=ai.CaptureDiagnostics().agents.front();
            const auto stairFeet=controller.GetPosition()-capsule.m_PositionOffset;
            std::cout<<"[DemoHallWhisperStairChase] from="<<route.first.x<<','<<route.first.y<<','<<route.first.z
                <<" target="<<route.second.x<<','<<route.second.y<<','<<route.second.z
                <<" feet="<<stairFeet.x<<','<<stairFeet.y<<','<<stairFeet.z
                <<" reached="<<reachedFloor<<" state="<<stairDiagnostic.currentState<<" occluded="<<temporarilyOccluded
                <<" hit="<<stairDiagnostic.lineOfSight.hitObject<<" path="<<stairDiagnostic.pathDiagnostic<<'\n';
            if(!Expect(pursued && reachedFloor && std::abs(stairFeet.y-route.second.y)<.15f,
                "Normal Whisper AI failed cross-floor pursuit through the indoor stairs")) return false;
        }
        // Sight loss retains an observed destination, never the unseen target's live position.
        pose=VansTransformStore::Read(agentTransform);pose.m_Position=point({-87.2f,45,-88});pose.m_Rotation.y=frameYaw;
        VansTransformStore::Write(agentTransform,pose);VansTransformStore::MarkDirty(agentTransform);
        const auto visibleTarget=point({-87.2f,42,-82});
        movedPose=VansTransformStore::Read(playerTransform);movedPose.m_Position=visibleTarget;
        VansTransformStore::Write(playerTransform,movedPose);VansTransformStore::MarkDirty(playerTransform);
        {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
            controller.SyncControllerFromTransform();playerController.SyncControllerFromTransform();}
        controller.SeedGroundedContact(true);
        bool observed=false;
        for(int frame=0;frame<120;++frame)
        {
            if(!aiTick())return Expect(false,error);
            const auto state=ai.CaptureDiagnostics().agents.front();const auto move=world.CaptureAgent(agentOwner);
            if(state.currentState=="Chase" && state.rawTargetVisible && move &&
                glm::distance(move->goal,visibleTarget)<.001f && move->state!=VansNavigationMoveState::Finished)
            {observed=true;break;}
        }
        if(!Expect(observed,"Last-seen pursuit fixture never observed its player"))return false;
        // Isolate perception memory from arrival; physical pursuit was verified above.
        if(!world.Pause(world.CaptureAgent(agentOwner)->request,"Indoor.Whisper.AI",error))return Expect(false,error);
        const auto unseenTarget=point({0,42,0});
        movedPose.m_Position=unseenTarget;VansTransformStore::Write(playerTransform,movedPose);VansTransformStore::MarkDirty(playerTransform);
        {std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());playerController.SyncControllerFromTransform();}
        for(int frame=0;frame<60;++frame)
        {
            if(!aiTick())return Expect(false,error);
            const auto move=world.CaptureAgent(agentOwner);
            if(!Expect(move && glm::distance(move->goal,visibleTarget)<.001f,
                "Between perception polls, pursuit read an unseen live target position"))return false;
        }
        const auto hiddenState=ai.CaptureDiagnostics().agents.front();const auto hiddenMove=world.CaptureAgent(agentOwner);
        std::cout<<"[DemoHallWhisperSightLoss] state="<<hiddenState.currentState<<" raw="<<hiddenState.rawTargetVisible
            <<" memory="<<hiddenState.targetVisible<<" result="<<(hiddenMove?static_cast<int>(hiddenMove->result):-1)
            <<" goal="<<(hiddenMove?hiddenMove->goal.x:0)<<','<<(hiddenMove?hiddenMove->goal.y:0)<<','<<(hiddenMove?hiddenMove->goal.z:0)
            <<" path="<<(hiddenMove?hiddenMove->path.diagnostic:std::string{})<<'\n';
        if(!Expect(!hiddenState.rawTargetVisible && hiddenState.targetVisible && hiddenState.currentState=="Chase" && hiddenMove &&
            glm::distance(hiddenMove->goal,visibleTarget)<.001f,
            "Whisper abandoned last-seen pursuit or tracked an unseen live target"))return false;
        for(int frame=0;frame<960;++frame)if(!aiTick())return Expect(false,error);
        const auto expired=ai.CaptureDiagnostics().agents.front();
        if(!Expect(!expired.targetVisible && expired.currentState=="Patrol",
            "Whisper sight memory did not expire within its configured bound"))return false;
        playerController.SetEnabled(false);
        for(int frame=0;frame<12;++frame)if(!aiTick())return Expect(false,error);
        if(!Expect(!ai.CaptureDiagnostics().agents.front().target.IsValid(),
            "Disabled player remained an AI pursuit target"))return false;
        std::cout<<"[DemoHallWhisperSightMemory] PASS: last-seen goal retained, unseen live position ignored, timeout returned to Patrol, disabled target released\n";
        ai.Shutdown();gameplay.Shutdown();
    }
    integration.Shutdown();
    std::cout<<"[DemoHallNavigation] PASS: current Surface/Agent/behavior closure, stable asset GUID and both player spawns Complete; tiles="<<asset->GetTileCount()
        <<", owned asset="<<asset->EstimateResidentBytes()<<", round-trip asset="<<memory->EstimateResidentBytes()
        <<", payload="<<asset->GetPayloadBytes()<<'\n';
    return true;
}

bool RunDustV3NavigationContractTests(const std::filesystem::path& root)
{
    using namespace Vans;
    std::string error;
    VansNavigationSettings settings;
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    auto map=std::make_shared<VansNavigationMap>();
    VansNavigationQueryContext query;
    if(!Expect(VansNavigationSettingsStorage::Load((root/"ProjectSettings/NavigationSettings.json").string(),settings,error) &&
        VansNavigationMeshStorage::Load(root/"Assets/Navigation/DustV3Ground.vnavmesh",asset,error) &&
        map->Initialize(asset,settings,"Humanoid",error) && query.Initialize(map,settings.query,"Default",error),error))return false;
    const glm::vec3 start(-18.5f,3.0524f,-34.f),player(-20.f,3.0524f,-40.f);
    if(!Expect(query.FindPath(start,player).status==VansNavigationPathStatus::Complete,"DustV3 enemy/player spawns are disconnected"))return false;
    nlohmann::ordered_json report;
    report["tiles"]=asset->GetTileCount();report["assetBytes"]=asset->EstimateResidentBytes();report["mapBytes"]=map->EstimateResidentBytes();
    const auto point=[](const glm::vec3& p){return nlohmann::ordered_json::array({p.x,p.y,p.z});};
    report["triangles"]=nlohmann::ordered_json::array();
    std::set<std::array<float,9>> seen;
    double completeArea=0,partialArea=0;std::size_t complete=0,partial=0;
    // 多个真实局部视口遵守既有 128 Tile 上限，不为导出放大运行时诊断预算。
    for(int x=0;x<4;++x)for(int z=0;z<4;++z)
    {
        const float xmin=-65.f+x*44.5f,xmax=xmin+44.5f,zmin=-100.f+z*45.f,zmax=zmin+45.f;
        VansNavigationDebugView view;view.maximumTiles=128;view.maximumTriangles=16384;
        view.planes={glm::vec4(1,0,0,-xmin),glm::vec4(-1,0,0,xmax),glm::vec4(0,1,0,6),
            glm::vec4(0,-1,0,8),glm::vec4(0,0,1,-zmin),glm::vec4(0,0,-1,zmax)};
        VansNavigationDebugGeometry geometry;
        if(!Expect(map->CaptureDebugGeometry(view,"Default",geometry,error) && !geometry.limited,error))return false;
        for(const auto& triangle:geometry.triangles)
        {
            if(!triangle.traversable)continue;
            std::array<std::array<float,3>,3> corners;
            for(int i=0;i<3;++i)corners[i]={triangle.positions[i].x,triangle.positions[i].y,triangle.positions[i].z};
            std::sort(corners.begin(),corners.end());std::array<float,9> key;
            for(int i=0;i<3;++i)for(int axis=0;axis<3;++axis)key[i*3+axis]=corners[i][axis];
            if(!seen.insert(key).second)continue;
            const auto center=(triangle.positions[0]+triangle.positions[1]+triangle.positions[2])/3.f;
            const auto path=query.FindPath(start,center,{.5f,2.f,.5f});
            if(!Expect(path.status==VansNavigationPathStatus::Complete || path.status==VansNavigationPathStatus::Partial,
                "DustV3 coverage query failed: "+path.diagnostic))return false;
            const auto cross=glm::cross(triangle.positions[1]-triangle.positions[0],triangle.positions[2]-triangle.positions[0]);
            const double area=std::abs(cross.y)*.5;
            const bool reachable=path.status==VansNavigationPathStatus::Complete;
            if(reachable){++complete;completeArea+=area;}else{++partial;partialArea+=area;}
            report["triangles"].push_back({{"points",{point(triangle.positions[0]),point(triangle.positions[1]),point(triangle.positions[2])}},
                {"reachable",reachable}});
        }
    }
    report["reachableTriangles"]=complete;report["disconnectedTriangles"]=partial;
    report["reachableAreaXZ"]=completeArea;report["disconnectedAreaXZ"]=partialArea;
    report["grid"]=nlohmann::ordered_json::array();
    for(float z:{-75.f,-50.f,-25.f,0.f,25.f,50.f})for(float x:{-45.f,-20.f,0.f,25.f,50.f,75.f,100.f})
    {
        const auto projected=query.ProjectPoint({x,1.5f,z},{4,5,4});
        if(!projected.valid)continue;
        const auto path=query.FindPath(start,projected.position);
        report["grid"].push_back({{"point",point(projected.position)}, {"complete",path.status==VansNavigationPathStatus::Complete},
            {"endpoint",path.points.empty()?point(start):point(path.points.back())},{"diagnostic",path.diagnostic}});
    }
    const auto destination=root/"Docs/NavigationAI20261012/native_coverage.json";
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream output(destination);output<<report.dump(2)<<'\n';output.close();
    if(!Expect(output.good() && complete>0,"DustV3 coverage export failed"))return false;
    std::cout<<"[DustV3Navigation] PASS: tiles="<<asset->GetTileCount()<<" completeTriangles="<<complete<<" completeAreaXZ="<<completeArea
        <<" disconnectedTriangles="<<partial<<" disconnectedAreaXZ="<<partialArea<<" report="<<destination.string()<<'\n';
    return true;
}
