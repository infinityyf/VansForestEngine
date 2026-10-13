#include "VansSceneNavigationRuntime.h"
#include "VansRuntimeWorld.h"
#include "VansRuntimeComponentTypes.h"
#include "Transform/VansTransformStore.h"
#include "../NavigationCore/VansNavigationMap.h"
#include "../NavigationCore/VansNavigationSourceSnapshot.h"
#include "../NavigationCore/VansNavigationSourceCollector.h"
#include "../NavigationCore/Detail/VansNavigationBuildInputMemory.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../Util/VansProfiler.h"

#include <cmath>
#include <algorithm>
#include <set>

namespace Vans
{
namespace
{
constexpr char SceneObstacleOwner[]="Scene.NavigationObstacle";
std::uint64_t Key(VansEntityHandle entity)
{
	return (static_cast<std::uint64_t>(entity.generation) << 32u) | entity.index;
}
bool SameConfig(const VansRuntimeNavigationAgentComponent& left, const VansRuntimeNavigationAgentComponent& right)
{
	return left.mapId == right.mapId && left.profileId == right.profileId && left.queryFilter == right.queryFilter && left.maxSpeed == right.maxSpeed &&
		left.acceleration == right.acceleration && left.avoidance == right.avoidance &&
		left.repathInterval == right.repathInterval && left.targetMoveThreshold == right.targetMoveThreshold &&
		left.forceRepathDistance == right.forceRepathDistance;
}
bool MotorFitsProfile(const VansNavigationBakeSettings& profile,const VansEngine::CharControllerProperties& properties)
{
    return profile.agentRadius+1.e-4f>=properties.m_Radius &&
        profile.agentHeight+1.e-4f>=properties.m_Height+2.f*properties.m_Radius &&
        profile.agentMaxClimb<=properties.m_StepOffset+1.e-4f &&
        std::cos(glm::radians(profile.agentMaxSlopeDegrees))+1.e-4f>=properties.m_SlopeLimit;
}
class CCTNavigationMotor final : public IVansNavigationMotor
{
public:
	CCTNavigationMotor(VansRuntimeWorld& runtime, VansEntityHandle entity) : m_Runtime(runtime), m_Entity(entity) {}
	VansNavigationMotorSample Read() const override
	{
		VansNavigationMotorSample sample;
		const auto* controller = Resolve();
		if (!controller) return sample;
		sample.position = controller->GetPosition() - controller->GetProperties().m_PositionOffset;
		sample.velocity = controller->GetTrajectory().currentVelocityWorld;
		sample.enabled = controller->IsEnabled();
		sample.movementBlocked = controller->IsGameplayMovementBlocked();
		return sample;
	}
	void Submit(const VansNavigationMotion& motion) override
	{
		auto* controller = Resolve();
		if (!controller) return;
		VansCharacterMotionIntent intent;
		intent.valid = true;
		const float length = glm::length(motion.direction);
		intent.moveInputLocal = motion.desiredSpeed > 0.0f && length > 1.e-4f ? glm::vec2(0.0f, 1.0f) : glm::vec2(0.0f);
		intent.movementReferenceYaw = length > 1.e-4f ? glm::degrees(std::atan2(motion.direction.x, motion.direction.z)) : 0.0f;
		intent.desiredSpeed = motion.desiredSpeed;
		intent.hasFacing = motion.facingYaw.has_value() || length > 1.e-4f;
		intent.desiredFacingYaw = motion.facingYaw.value_or(intent.movementReferenceYaw);
		if (motion.acceleration > 0.0f)
		{
			VansCharacterAccelerationModel dynamics;
			dynamics.grounded.maxAcceleration = motion.acceleration;
			dynamics.grounded.brakingDeceleration = motion.acceleration;
			dynamics.airborne = dynamics.grounded;
			intent.accelerationModel = dynamics;
		}
		controller->SetMotionIntent(intent);
	}
private:
	VansEngine::VansCharacterControllerNode* Resolve() const
	{
		if (!m_Runtime.IsAlive(m_Entity)) return nullptr;
		const auto* storage = m_Runtime.FindStorage<VansRuntimeCharacterControllerComponent>(VansRuntimeComponentType_CharacterController);
		const auto* component = storage ? storage->FindFirstEffectiveOwnedBy(m_Entity) : nullptr;
		return component ? component->controllerNode : nullptr;
	}
	VansRuntimeWorld& m_Runtime;
	VansEntityHandle m_Entity;
};
}
bool VansSceneNavigationRuntime::Initialize(VansRuntimeWorld& runtime,
	const VansAssetObjectRepository& assets, const VansNavigationSettings& settings, std::string& error)
{
	Shutdown();
	m_Runtime = &runtime;
	m_Assets = &assets;
	m_Settings = settings;
	if (m_World.Initialize(settings, error) && m_TileUpdater.Configure(settings,error)) return true;
	Shutdown();
	return false;
}
void VansSceneNavigationRuntime::Shutdown()
{
	m_World.Shutdown();
    m_TileUpdater.Shutdown();
	m_Registrations.clear();
    m_Maps.clear();
	m_Inputs.clear(); m_SourceErrors.clear(); m_SourceObservation=0; m_CarveObstacleObservation=0;
	m_Runtime = nullptr;
	m_Assets = nullptr;
}
void VansSceneNavigationRuntime::WithdrawSources()
{
    m_World.CancelOwner(SceneObstacleOwner);
    m_TileUpdater.RetainMaps({});
	for(const auto& [key,registration]:m_Registrations) m_World.UnregisterAgent(registration.entity);
    for(const auto& [key,map]:m_Maps) m_World.WithdrawMap(map->GetAsset()->GetMapId());
	m_Registrations.clear(); m_Maps.clear(); m_Inputs.clear(); m_SourceErrors.clear(); m_SourceObservation=0; m_CarveObstacleObservation=0;
}
bool VansSceneNavigationRuntime::Reconfigure(const VansNavigationSettings& settings,std::string& error)
{
    if(SameNavigationSettings(m_Settings,settings)) return true;
    if(!ValidateNavigationSettings(settings,error)) { WithdrawSources(); return false; }
    auto candidate=settings;
    if(!m_World.Initialize(candidate,error)) return false;
    if(!m_TileUpdater.Configure(candidate,error)) return false;
    m_Settings=std::move(candidate); m_Registrations.clear(); m_Maps.clear(); m_Inputs.clear(); m_SourceErrors.clear(); m_SourceObservation=0; m_CarveObstacleObservation=0;
    return true;
}
bool VansSceneNavigationRuntime::Synchronize(VansNavigationSourceSnapshot sources,std::uint64_t observation,std::string& error)
{
	if (!m_Runtime || !m_Assets) { error = "Scene navigation is not initialized"; return false; }
	const auto* storage = m_Runtime->FindStorage<VansRuntimeNavigationAgentComponent>(VansRuntimeComponentType_NavigationAgent);
	const auto* motors = m_Runtime->FindStorage<VansRuntimeCharacterControllerComponent>(VansRuntimeComponentType_CharacterController);
    std::map<std::uint64_t,std::size_t> enabledAgents;
    if(storage) for(const auto& header:storage->Headers())
        if(header.effectiveEnabled && m_Runtime->IsAlive(header.owner)) ++enabledAgents[Key(header.owner)];
    // 组件生命周期撤销独立于后续 Map 装配是否成功；有效 Map 的查询仍可继续。
    for(auto iterator=m_Registrations.begin();iterator!=m_Registrations.end();)
    {
        const auto& old=iterator->second;
        const auto* header=storage?storage->GetHeader(old.component):nullptr;
        const auto* config=storage?storage->Get(old.component):nullptr;
        const auto* motor=motors?motors->FindFirstEffectiveOwnedBy(old.entity):nullptr;
        const auto profile=old.map->GetBakeSettings();
        const bool valid=header && config && header->owner==old.entity && header->effectiveEnabled &&
            m_Runtime->IsAlive(old.entity) && enabledAgents[iterator->first]==1 &&
            config->mapId==old.config.mapId && config->profileId==old.config.profileId && config->queryFilter==old.config.queryFilter &&
            motor && motor->controllerNode && motor->controllerNode->IsEnabled() && profile &&
            MotorFitsProfile(*profile,motor->controllerNode->GetProperties());
        if(valid) { ++iterator; continue; }
        m_World.UnregisterAgent(old.entity); iterator=m_Registrations.erase(iterator);
    }
	std::map<std::uint64_t, Registration> candidates;
	std::vector<VansNavigationAgentRegistration> additions;
    std::map<std::pair<VansAssetGuid, std::string>, std::shared_ptr<VansNavigationMap>> maps;
    std::map<std::string, VansAssetGuid> mapAssets;
    std::map<std::string,std::vector<VansNavigationSurfaceRecord>> surfacesByMap;
    std::map<std::pair<std::string,std::string>,std::shared_ptr<const VansNavigationMap>> logicalMaps;
    const auto invalidateMap=[&](const std::string& id,const std::string& reason)
    {
        m_World.WithdrawMap(id);
        m_TileUpdater.WithdrawMap(id);
        for(auto iterator=m_Registrations.begin();iterator!=m_Registrations.end();)
        {
            if(iterator->second.config.mapId!=id) { ++iterator; continue; }
            m_World.UnregisterAgent(iterator->second.entity); iterator=m_Registrations.erase(iterator);
        }
        for(auto iterator=m_Maps.begin();iterator!=m_Maps.end();)
            if(iterator->second->GetAsset()->GetMapId()==id) iterator=m_Maps.erase(iterator); else ++iterator;
        error=reason; return false;
    };
    if(!m_World.ReplaceObstacles(SceneObstacleOwner,sources.obstacles,error))
    { WithdrawSources(); return false; }
    const auto obstacleRevision=m_World.GetCarveObstacleRevision();
    if(observation!=m_SourceObservation || m_SourceObservation==0 || obstacleRevision!=m_CarveObstacleObservation)
    {
        VANS_PROFILE_SCOPE("Navigation::RefreshBuildInput", Vans::ProfileCategory::Script);
        if(!m_World.CaptureObstacles(sources.obstacles,error)) { WithdrawSources(); return false; }
        auto previousInputs=std::move(m_Inputs);
        m_Inputs.clear(); m_SourceErrors.clear();
        std::set<std::string> mapIds;
        for(const auto& surface:sources.surfaces) mapIds.insert(surface.surface.mapId);
        for(const auto& mapId:mapIds)
        {
            VansNavigationBuildInput input; std::string collectionError;
            if(!VansNavigationSourceCollector::Collect(sources,m_Settings,mapId,input,collectionError))
            { m_SourceErrors.emplace(mapId,std::move(collectionError)); continue; }
            auto& hashes=m_Inputs[mapId];
            for(auto& profile:input.profiles)
            {
                std::uint64_t hash=0;
                if(!ComputeNavigationBuildInputHash(profile,hash,collectionError))
                { m_SourceErrors.emplace(mapId,std::move(collectionError)); hashes.clear(); break; }
                std::sort(profile.contributions.begin(),profile.contributions.end(),[](const auto& a,const auto& b){return a.id<b.id;});
                ProfileInput value; value.hash=hash;
                const auto profileId=profile.profileId;
                if(FindNavigationMap(m_Settings,mapId)->updateMode==VansNavigationMapUpdateMode::Runtime)
                {
                    const auto previousMap=previousInputs.find(mapId);
                    if(previousMap!=previousInputs.end())
                    {
                        const auto previous=previousMap->second.find(profileId);
                        if(previous!=previousMap->second.end() && previous->second.hash==hash) value.frozen=previous->second.frozen;
                    }
                    if(!value.frozen) value.frozen=std::make_shared<const VansNavigationProfileBuildInput>(std::move(profile));
                }
                else value.contributions=std::move(profile.contributions);
                hashes.emplace(profileId,std::move(value));
            }
        }
        m_SourceObservation=observation;
        m_CarveObstacleObservation=obstacleRevision;
    }
    const auto inputFreshnessError=[&](const std::string& mapId,const std::string& profileId,
        const VansNavigationProfileBuildData* layer)->std::string
    {
        const auto sourceError=m_SourceErrors.find(mapId);
        if(sourceError!=m_SourceErrors.end()) return "SourceUnavailable: "+sourceError->second;
        const auto current=m_Inputs.find(mapId);
        if(current==m_Inputs.end()) return "SourceUnavailable: Navigation Map has no frozen build input";
        if(!layer) return "NavigationSurface Profile is absent from its asset: "+profileId;
        const auto currentProfile=current->second.find(profileId);
        if(currentProfile==current->second.end()) return "SourceUnavailable: Navigation Profile has no frozen build input: "+profileId;
        if(FindNavigationMap(m_Settings,mapId)->updateMode==VansNavigationMapUpdateMode::Runtime) return {};
        if(currentProfile->second.hash!=layer->inputHash)
        {
            const auto& actual=currentProfile->second.Contributions();
            std::vector<const VansNavigationContribution*> baked;
            baked.reserve(layer->contributions.size());
            for(const auto& item:layer->contributions) baked.push_back(&item);
            std::sort(baked.begin(),baked.end(),[](const auto* a,const auto* b){return a->id<b->id;});
            std::size_t changed=0,left=0,right=0; std::string details;
            const auto append=[&](const std::string& id,const char* reason)
            {
                if(changed<4) details+="; "+std::string(reason)+" "+id;
                ++changed;
            };
            while(left<actual.size() || right<baked.size())
            {
                if(right==baked.size() || (left<actual.size() && actual[left].id<baked[right]->id))
                    append(actual[left++].id,"added");
                else if(left==actual.size() || baked[right]->id<actual[left].id)
                    append(baked[right++]->id,"removed");
                else
                {
                    if(actual[left].contentHash!=baked[right]->contentHash) append(actual[left].id,"changed");
                    ++left; ++right;
                }
            }
            return "Stale: current collision/source/Terrain/Modifier input differs from the baked Map/Profile: "+profileId+
                "; baked="+std::to_string(layer->inputHash)+" current="+std::to_string(currentProfile->second.hash)+
                "; changed contributions="+std::to_string(changed)+details;
        }
        return {};
    };
    // 第一张 Map 的错误不能跳过其他已发布 Map 的失效撤销。这里只读观察缓存，
    // 源几何/哈希仍只由上方的 observation 变化路径计算一次。
    std::map<std::string,std::string> stalePublishedMaps;
    for(const auto& [key,map]:m_Maps)
    {
        const auto asset=map->GetAsset();
        const auto reason=inputFreshnessError(asset->GetMapId(),key.second,asset->FindProfile(key.second));
        if(!reason.empty()) stalePublishedMaps.emplace(asset->GetMapId(),reason);
    }
    for(const auto& [id,reason]:stalePublishedMaps) invalidateMap(id,reason);
    const auto* surfaces=m_Runtime->FindStorage<VansRuntimeNavigationSurfaceComponent>(VansRuntimeComponentType_NavigationSurface);
    const auto* transforms=m_Runtime->FindStorage<VansRuntimeTransformComponent>(VansRuntimeComponentType_Transform);
    if(surfaces) for(std::size_t i=0;i<surfaces->Headers().size();++i)
    {
        const auto& header=surfaces->Headers()[i]; const auto& data=surfaces->DenseData()[i];
        if(!m_Runtime->IsAlive(header.owner)) continue;
        const auto* transform=transforms?transforms->Get(transforms->FindFirstOwnedBy(header.owner)):nullptr;
        if(!transform || !VansTransformStore::IsAllocated(transform->transformStoreId))
            return invalidateMap(data.mapId,"NavigationSurface requires an allocated Transform");
        const auto* owner=m_Runtime->Entities().Get(header.owner);
        VansNavigationSurfaceRecord record{header.stableGuid,owner->stableGuid,header.effectiveEnabled,
            VansTransformStore::Read(transform->transformStoreId).GetModelMatrix(),data};
        std::uint64_t hash=0;
        if(record.id.empty() || !ComputeNavigationSurfaceContributionHash(record,hash,error) || !FindNavigationMap(m_Settings,data.mapId) || data.profiles.empty())
            return invalidateMap(data.mapId,"NavigationSurface has invalid identity, Map or bounds");
        for(const auto& layer:data.collisionLayers)
        {
            int index=-1;
            if(!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(layer,index))
                return invalidateMap(data.mapId,"NavigationSurface references unknown collision layer: "+layer);
        }
        VansAssetGuid guid;
        if(!VansAssetGuid::TryParse(data.navigationMeshGuid,guid))
            return invalidateMap(data.mapId,"NavigationSurface requires a valid Navigation Mesh asset GUID");
        const auto previous=mapAssets.find(data.mapId);
        if(previous!=mapAssets.end() && previous->second!=guid)
            return invalidateMap(data.mapId,"One logical Navigation Map cannot bind conflicting Surface asset GUIDs");
        mapAssets.emplace(data.mapId,guid); surfacesByMap[data.mapId].push_back(std::move(record));
    }
    for(const auto& [mapId,records]:surfacesByMap)
    {
        const bool runtime=FindNavigationMap(m_Settings,mapId)->updateMode==VansNavigationMapUpdateMode::Runtime;
        const auto guid=mapAssets.at(mapId);
        const auto asset=m_Assets->ResolveLatest<VansNavigationMeshAsset>(guid);
        if(!asset || !asset->IsReady() || asset->GetMapId()!=mapId)
            return invalidateMap(mapId,"NavigationSurface asset is unavailable or belongs to another Map");
        std::set<std::string> profiles;
        for(const auto& record:records) for(const auto& profile:record.surface.profiles) profiles.insert(profile);
        for(const auto& profileId:profiles)
        {
            const auto* layer=asset->FindProfile(profileId);
            const auto freshness=inputFreshnessError(mapId,profileId,layer);
            if(!freshness.empty()) return invalidateMap(mapId,freshness);
            std::map<std::string,std::uint64_t> expected;
            bool enabled=false;
            for(const auto& record:records)
            {
                if(std::find(record.surface.profiles.begin(),record.surface.profiles.end(),profileId)==record.surface.profiles.end()) continue;
                std::uint64_t hash=0;
                if(!ComputeNavigationSurfaceContributionHash(record,hash,error)) return invalidateMap(mapId,error);
                if(!expected.emplace("surface/"+record.id,hash).second) return invalidateMap(mapId,"NavigationSurface identity is duplicated");
                enabled=enabled||record.enabled;
            }
            std::map<std::string,std::uint64_t> baked;
            for(const auto& contribution:layer->contributions)
                if(contribution.id.rfind("surface/",0)==0) baked.emplace(contribution.id,contribution.contentHash);
            if(!runtime && expected!=baked) return invalidateMap(mapId,"Stale: NavigationSurface contributions differ from the baked Map/Profile");
            if(!enabled) continue;
            const auto mapKey=std::make_pair(guid,profileId);
            std::shared_ptr<VansNavigationMap> map;
            const auto old=m_Maps.find(mapKey);
            if(old!=m_Maps.end() && old->second->GetAsset()==asset) map=old->second;
            else
            {
                auto candidate=std::make_shared<VansNavigationMap>();
                if(!candidate->Initialize(asset,m_Settings,profileId,error)) return invalidateMap(mapId,error);
                map=std::move(candidate);
            }
            if(runtime)
            {
                const auto& input=m_Inputs.at(mapId).at(profileId);
                if(!m_TileUpdater.Observe(map,input.frozen,input.hash,error)) return invalidateMap(mapId,error);
            }
            maps.emplace(mapKey,map); logicalMaps.emplace(std::make_pair(mapId,profileId),std::move(map));
        }
    }
	if (storage)
	{
		const auto& headers = storage->Headers();
		const auto& data = storage->DenseData();
		for (std::size_t i = 0; i < headers.size(); ++i)
		{
			const auto& header = headers[i];
			if (!header.effectiveEnabled || !m_Runtime->IsAlive(header.owner)) continue;
			const auto key = Key(header.owner);
			if (candidates.count(key)) return invalidateMap(data[i].mapId,"Entity has more than one enabled NavigationAgent");
			const auto* motor = motors ? motors->FindFirstEffectiveOwnedBy(header.owner) : nullptr;
			if (!motor || !motor->controllerNode) return invalidateMap(data[i].mapId,"NavigationAgent requires an enabled CharacterController motor");
            const auto logical=logicalMaps.find({data[i].mapId,data[i].profileId});
            if(logical==logicalMaps.end()) return invalidateMap(data[i].mapId,"SurfaceUnavailable: NavigationAgent has no enabled Map/Profile Surface");
            const auto map=logical->second;
            const auto& profile=map->GetAsset()->FindProfile(data[i].profileId)->bake;
            const auto& properties=motor->controllerNode->GetProperties();
            if(!MotorFitsProfile(profile,properties))
            return invalidateMap(data[i].mapId,"Navigation profile clearance/climb is incompatible with the CharacterController motor");
            Registration registration{ header.owner, header.self, data[i], map };
			candidates.emplace(key, registration);
			const auto old = m_Registrations.find(key);
			if (old != m_Registrations.end() && old->second.component == header.self && old->second.map == map &&
				SameConfig(old->second.config, data[i])) continue;
			additions.push_back({ header.owner, data[i], map, std::make_shared<CCTNavigationMotor>(*m_Runtime, header.owner) });
		}
	}
    std::vector<std::shared_ptr<const VansNavigationMap>> registeredMaps;
    for(const auto& [key,map]:logicalMaps) registeredMaps.push_back(map);
    m_TileUpdater.RetainMaps(registeredMaps);
    std::uint64_t residentBytes = EstimateNavigationQueryBytes(m_Settings.query)+EstimateNavigationAvoidanceBytes(m_Settings.query.maximumActiveAgents,m_Settings.avoidance)+m_World.EstimateObstacleResidentBytes(), residentTiles = 0;
    std::set<VansAssetGuid> countedAssets;
    for (const auto& [mapKey, map] : maps)
    {
        residentBytes += map->EstimateResidentBytes();
        const auto asset = map->GetAsset();
        residentTiles += map->GetResidentTileCount();
        if (countedAssets.insert(mapKey.first).second) residentBytes += asset->EstimateResidentBytes();
    }
    // 已注册 Runtime 输入由 updater 计量；其余缓存及 Baked 指纹也占实际驻留空间。
    VansNavigationBuildInputMemory inactiveInputs;
    for(const auto& [mapId,profiles]:m_Inputs)
    {
        residentBytes+=mapId.capacity()+1+96;
        for(const auto& [profileId,input]:profiles)
        {
            residentBytes+=sizeof(ProfileInput)+96+profileId.capacity()+1+input.contributions.capacity()*sizeof(VansNavigationContribution);
            for(const auto& contribution:input.contributions) residentBytes+=contribution.id.capacity()+1;
            if(input.frozen && !logicalMaps.count({mapId,profileId})) inactiveInputs.Add(*input.frozen);
        }
    }
    residentBytes+=inactiveInputs.bytes;
    const auto updateStats=m_TileUpdater.CaptureStats();
    if (residentBytes+updateStats.retainedBytes > static_cast<std::uint64_t>(m_Settings.maximumResidentBytes) ||
        residentTiles > static_cast<std::uint64_t>(m_Settings.maximumResidentTiles))
    { error = "Scene Navigation Maps exceed aggregate resident memory/Tile capacity; Maps/query="+std::to_string(residentBytes)+
        "; runtime updates="+std::to_string(updateStats.retainedBytes)+"; cache contexts="+std::to_string(updateStats.cachedContextBytes)+
        "; base/reused/evicted cells="+std::to_string(updateStats.baseCellsBuilt)+"/"+std::to_string(updateStats.reusedCells)+"/"+
        std::to_string(updateStats.evictedCells)+"; Tiles="+std::to_string(residentTiles); return false; }
    if(!m_World.RegisterMaps(registeredMaps,error)) return false;
	if (!m_World.RegisterAgents(additions, error)) return false;
	for (const auto& [key, old] : m_Registrations)
		if (!candidates.count(key)) m_World.UnregisterAgent(old.entity);
	m_Registrations = std::move(candidates);
    m_Maps = std::move(maps);
    return m_TileUpdater.Tick(residentBytes,residentTiles,error);
}
}
