#include "VansVegetationCollection.h"
#include "VansVegetationSystem.h"
#include "../VansScene.h"
#include "../VansMaterial.h"
#include "../GICore/VansGIVoxelSource.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansMesh.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include "../../PcgCore/VansPcgRuntimePolicy.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace VansGraphics
{
namespace
{
glm::mat4 PlacementMatrix(const std::array<float,3>& position,
    const std::array<float,4>& rotation, const std::array<float,3>& scale)
{
    return glm::translate(glm::mat4(1), glm::vec3(position[0],position[1],position[2])) *
        glm::mat4_cast(glm::quat(rotation[3],rotation[0],rotation[1],rotation[2])) *
        glm::scale(glm::mat4(1), glm::vec3(scale[0],scale[1],scale[2]));
}
std::string VoxelSourceKey(const Vans::VansPcgBatchKey& key)
{
    auto field=[](const std::string& value){return std::to_string(value.size())+":"+value+"/";};
    return "pcg/"+field(key.region)+field(key.layer)+field(key.variant)+std::to_string(key.x)+"/"+std::to_string(key.z);
}
GIVoxelSource CaptureVoxelSource(VansScene& scene,const Vans::VansPcgBatchSource& source)
{
    auto variant=std::find_if(source.plant->variants.begin(),source.plant->variants.end(),[&](const auto& v){return v.id==source.key.variant;});
    GIVoxelSource result;result.key=VoxelSourceKey(source.key);result.modelIdentity=source.plant;
    if(variant==source.plant->variants.end())
    { result.availability=GIVoxelSourceAvailability::TemporarilyUnavailable;return result; }
    for(const auto& part:variant->parts)
    {
        auto* mesh=static_cast<VansMesh*>(scene.FindMeshAsset(part.mesh.ToString()));
        auto* material=dynamic_cast<VansPBRMaterial*>(static_cast<VansMaterial*>(scene.FindMaterialAsset(part.material.ToString())));
        if(!mesh || !material)
        { result.parts.clear();result.instances.clear();result.availability=GIVoxelSourceAvailability::TemporarilyUnavailable;return result; }
        result.parts.push_back({mesh,material,part.submesh,part.kind==Vans::VansPlantPartKind::Leaves});
    }
    result.instances.reserve(source.points.size());
    const auto variantTransform=PlacementMatrix(variant->offset,variant->rotation,variant->scale);
    for(const auto& p:source.points)
    {
        auto transform=PlacementMatrix(p.position,p.rotation,p.scale)*variantTransform;
        if(std::abs(glm::determinant(glm::mat3(transform)))>=1e-10f)result.instances.push_back(transform);
    }
    return result;
}
struct VegetationBatch
{
    std::shared_ptr<const Vans::VansPlantTypeAsset> plant;
    float bladeWidth = 0;
    std::vector<GrassRenderConfig> grassParts;
    std::vector<TreePartConfig> treeParts;
    std::vector<GrassInstance> grass;
    std::vector<TreeInstanceGPU> trees;
};
void ResolveParts(VansScene& scene, const Vans::VansPlantVariant& variant, VegetationBatch& batch)
{
    for (const auto& part : variant.parts)
    {
        if (!part.material.IsValid())
            throw std::invalid_argument("PCG part requires an explicit material asset: " + part.id);
        auto* material = static_cast<VansMaterial*>(scene.FindMaterialAsset(part.material.ToString()));
        if (!material) throw std::invalid_argument("PCG material was not preloaded: " + part.material.ToString());
        std::vector<VansMesh*> meshes;
        const bool procedural = variant.geometry == Vans::VansPlantGeometry::ProceduralBlade;
        if (procedural) meshes.push_back(nullptr);
        else
        {
            auto* mesh = static_cast<VansMesh*>(scene.FindMeshAsset(part.mesh.ToString()));
            if (batch.plant->category == Vans::VansPlantCategory::Grass)
                if (auto* cpuMesh=static_cast<VansMesh*>(scene.FindMeshAsset("pcg-cpu/"+part.mesh.ToString()))) mesh=cpuMesh;
            if (!mesh) throw std::invalid_argument("PCG model was not preloaded: " + part.mesh.ToString());
            if (mesh->m_IsMultiMesh)
            {
                for (size_t i=0; i<mesh->m_SubMeshes.size(); ++i)
                    if (part.submesh < 0 || i == static_cast<size_t>(part.submesh)) meshes.push_back(mesh->m_SubMeshes[i]);
            }
            else if (part.submesh <= 0) meshes.push_back(mesh);
            if (meshes.empty()) throw std::invalid_argument("PCG part has no selected submesh: " + part.id);
        }
        for (auto* mesh : meshes)
        {
            if (!procedural && (!mesh || mesh->GetIndexCount() == 0))
                throw std::invalid_argument("PCG part has no drawable geometry: " + part.id);
            if (batch.plant->category == Vans::VansPlantCategory::Grass)
            {
                if (material->m_MaterialType != VansMaterialType::VAN_GRASS)
                    throw std::invalid_argument("PCG grass part requires a Grass material: " + part.id);
                if (!material->GetPassShader(VansPass::GBUFFER))
                    throw std::invalid_argument("PCG grass material has no GBuffer shader: " + part.material.ToString());
                batch.grassParts.push_back({mesh,material,procedural});
            }
            else
            {
                const auto kind = part.kind == Vans::VansPlantPartKind::Trunk ? TreePartType::Trunk :
                    part.kind == Vans::VansPlantPartKind::Leaves ? TreePartType::Leaves : TreePartType::Custom;
                batch.treeParts.push_back({mesh,material,kind});
            }
        }
    }
}
}

namespace
{
Vans::VansPcgBounds BatchRootBounds(const Vans::VansPcgBatchSource& source)
{
    const auto variant=std::find_if(source.plant->variants.begin(),source.plant->variants.end(),
        [&](const auto& item){return item.id==source.key.variant;});
    if (variant==source.plant->variants.end() || source.points.empty())
        throw std::invalid_argument("PCG batch requires a model variant and points.");
    const float limit=std::numeric_limits<float>::max();
    Vans::VansPcgBounds roots{{limit,limit},{-limit,-limit}};
    for (const auto& point:source.points) {
        const glm::vec3 offset=glm::quat(point.rotation[3],point.rotation[0],point.rotation[1],point.rotation[2]) *
            (glm::vec3(point.scale[0],point.scale[1],point.scale[2]) * glm::vec3(variant->offset[0],variant->offset[1],variant->offset[2]));
        const float x=point.position[0]+offset.x,z=point.position[2]+offset.z;
        roots.min[0]=std::min(roots.min[0],x);roots.max[0]=std::max(roots.max[0],x);
        roots.min[1]=std::min(roots.min[1],z);roots.max[1]=std::max(roots.max[1],z);
    }
    return roots;
}
const Vans::VansPlantVariant& FindVariant(const Vans::VansPcgBatchSource& source)
{
    const auto found = std::find_if(source.plant->variants.begin(), source.plant->variants.end(),
        [&](const auto& variant) { return variant.id == source.key.variant; });
    if (found == source.plant->variants.end())
        throw std::invalid_argument("PCG model variant is unavailable.");
    return *found;
}
GrassInstance MakeGrassInstance(const Vans::VansPcgBatchSource& source,
    const Vans::VansPlantVariant& variant, const Vans::VansPcgPoint& point)
{
    GrassInstance result;
    result.modelMatrix = PlacementMatrix(point.position, point.rotation, point.scale) *
        PlacementMatrix(variant.offset, variant.rotation, variant.scale);
    const auto& model = result.modelMatrix;
    const float extent = std::sqrt(glm::dot(glm::vec3(model[0]), glm::vec3(model[0])) +
        glm::dot(glm::vec3(model[1]), glm::vec3(model[1])) + glm::dot(glm::vec3(model[2]), glm::vec3(model[2])));
    result.randomSeed = static_cast<uint32_t>(point.id ^ (point.id >> 32));
    result.boundsRadius = (std::max(variant.cullingRadius, source.plant->grass.bladeHeight) +
        source.plant->grass.scatterRadiusMax) * extent;
    return result;
}
VansRenderAABB GrassCellBounds(const Vans::VansPcgBatchSource& source)
{
    const auto& variant = FindVariant(source);
    VansRenderAABB result;
    for (const auto& point : source.points)
    {
        const auto instance = MakeGrassInstance(source, variant, point);
        const glm::vec3 center(instance.modelMatrix[3]);
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
            !std::isfinite(instance.boundsRadius) || instance.boundsRadius < 0) return {};
        const glm::vec3 extent(instance.boundsRadius + 0.01f);
        result.min = glm::min(result.min, center - extent);
        result.max = glm::max(result.max, center + extent);
    }
    return result;
}
std::shared_ptr<VansVegetationSystem> CreateBatch(VansScene& scene,VkDevice device,
    const Vans::VansPcgBatchSource& source, std::vector<GrassInstance> grassInstances,
    std::vector<GrassInstanceRange> grassRanges)
{
    VegetationBatch batch;
    batch.plant=source.plant;
    const auto variant=std::find_if(batch.plant->variants.begin(),batch.plant->variants.end(),
        [&](const auto& item){return item.id==source.key.variant;});
    if (variant==batch.plant->variants.end()) throw std::invalid_argument("PCG model variant is unavailable.");
    batch.bladeWidth=variant->bladeWidth;
    batch.grass = std::move(grassInstances);
    ResolveParts(scene,*variant,batch);
    Vans::VansPlantTreeRuntimeBounds treeBounds;
    if(batch.plant->category==Vans::VansPlantCategory::Tree) {
        std::string boundsError;
        if(!Vans::ResolvePlantTreeRuntimeBounds(*variant,treeBounds,boundsError))
            throw std::invalid_argument("Tree variant "+boundsError+'.');
        for(size_t level=0;level<variant->lod.levels.size();++level) {
            Vans::VansPlantVariant selected=*variant;selected.parts.clear();
            for(const auto& source:variant->lod.levels[level].parts) {
                if(source.sourcePart>=variant->parts.size())throw std::invalid_argument("Invalid tree LOD source part.");
                auto part=variant->parts[source.sourcePart];part.mesh=source.model;part.material=source.material;part.submesh=source.submesh;
                selected.parts.push_back(std::move(part));
            }
            const auto first=batch.treeParts.size();ResolveParts(scene,selected,batch);
            for(size_t i=first;i<batch.treeParts.size();++i)batch.treeParts[i].lod=static_cast<uint32_t>(level+1);
        }
    }
    if (batch.plant->category == Vans::VansPlantCategory::Tree)
    for (const auto& point : source.points)
    {
        const glm::mat4 model=PlacementMatrix(point.position,point.rotation,point.scale) *
            PlacementMatrix(variant->offset,variant->rotation,variant->scale);
        const uint32_t seed=static_cast<uint32_t>(point.id^(point.id>>32));
        {
            TreeInstanceGPU instance;
            instance.modelMatrix=model;instance.randomSeed=seed;
            const glm::vec3 center(treeBounds.center[0],treeBounds.center[1],treeBounds.center[2]);
            const float radius=treeBounds.radius;
            // A^T A 最大绝对行和给出保守伸缩界；均匀缩放时不会额外膨胀 sqrt(3)。
            float maxScaleSquared=0;
            for(int i=0;i<3;++i){float row=0;for(int j=0;j<3;++j)row+=std::abs(glm::dot(glm::vec3(model[i]),glm::vec3(model[j])));maxScaleSquared=std::max(maxScaleSquared,row);}
            instance.boundsSphere=glm::vec4(glm::vec3(model*glm::vec4(center,1)),radius*std::sqrt(maxScaleSquared));
            batch.trees.push_back(instance);
        }
    }
    auto result=std::shared_ptr<VansVegetationSystem>(new VansVegetationSystem(),
        [device](VansVegetationSystem* system){system->Cleanup(device);delete system;});
    auto& system=*result;
    const auto& plant = *batch.plant;
    const auto& grass = plant.grass;
    system.SetRenderConfigs(batch.grassParts);
    system.SetTreeParts(batch.treeParts);
    system.SetTreeLodSettings(plant.render.lodDistances,plant.render.lodHysteresis);
    system.SetRenderOptions(plant.render.cullingEnabled,plant.render.cullDistance,plant.render.castShadows);
    system.SetBladeHeight(grass.bladeHeight);
    system.SetBladeWidth(batch.bladeWidth);
    system.SetInitWindDirection(glm::vec2(grass.windDirection[0],grass.windDirection[1]),grass.leanDeviation);
    system.SetSubBladeParams(grass.subBladeCount,grass.scatterRadiusMin,grass.scatterRadiusMax);
    system.SetRestShape(grass.restTipBendDegrees,grass.restRootBendDegrees,grass.scatterSeed);
    system.SetSubBladeLodDistances(grass.subBladeLodMidDistance,grass.subBladeLodFarDistance);
    system.SetSimParams(glm::vec2(grass.windDirection[0],grass.windDirection[1]),
        grass.windStrength,grass.windFrequency,grass.windSpeed,grass.windBendMultiplier,
        grass.stiffness,grass.damping,grass.softness,grass.simulationFullDistance,grass.simulationFadeDistance);
    if (plant.render.hizEnabled)
    {
        auto* manager = scene.GetMaterialManager();
        if (auto* hzb = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT))
            system.SetHiZDepth(hzb->GetImage().GetImageView(),hzb->GetImage().GetSampler(),
                static_cast<uint32_t>(manager->m_HIZMipCount),plant.render.hizBias);
    }

    system.Init(device,std::move(batch.grass),std::move(batch.trees),grass.boneCount,std::move(grassRanges));
    if (scene.GetGlobalDescriptorSet()!=VK_NULL_HANDLE)
    {
        system.SetGlobalDescriptorSet(scene.GetGlobalDescriptorSetLayout(),scene.GetGlobalDescriptorSet());
        for (const auto& part : system.GetRenderConfigsGPU())
        {
            auto* material=static_cast<VansGrassMaterial*>(part.material);
            if (material && material->m_GrassOwnedLayout==VK_NULL_HANDLE) material->BuildGrassTextureDescriptors();
        }
    }
    return result;
}
}
VansVegetationCollection::TreeSources VansVegetationCollection::GroupTreeBatches(const CellSources& cells)
{
    std::map<TreeBatchKey,std::shared_ptr<Vans::VansPcgBatchSource>> grouped;
    for(const auto& cell:cells) {
        const auto& source=cell.second;
        if(!source || source->plant->category!=Vans::VansPlantCategory::Tree)
            throw std::invalid_argument("Tree render grouping requires tree sources.");
        const TreeBatchKey key{source->plant,source->key.variant};
        auto& batch=grouped[key];
        if(!batch) {
            batch=std::make_shared<Vans::VansPcgBatchSource>();batch->plant=source->plant;
            batch->key.variant=source->key.variant;
        }
        batch->points.insert(batch->points.end(),source->points.begin(),source->points.end());
    }
    TreeSources result;
    for(auto& entry:grouped)result.emplace(entry.first,std::move(entry.second));
    return result;
}

struct VansVegetationCollection::GrassPoolBuild
{
    GrassPools pools;
    size_t replaced = 0, reused = 0, uploadPages = 0, uploadInstances = 0;
    struct Update
    {
        std::shared_ptr<VansVegetationSystem> owner;
        VansVegetationSystem::GrassPageUploads uploads;
    };
    std::vector<Update> updates;
    void CommitUploads() noexcept
    {
        for (auto& update : updates) update.owner->CommitGrassPageUpdates(std::move(update.uploads));
    }
    void Log(size_t poolCount) const
    {
        VANS_LOG("[GrassPool] pools="<<poolCount<<" replaced="<<replaced<<" reused="<<reused
            <<" uploadPages="<<uploadPages<<" uploadInstances="<<uploadInstances);
    }
};

VansVegetationCollection::GrassPoolBuild VansVegetationCollection::BuildGrassPools(
    VansScene& scene, VansVKDevice& resourceDevice, const GrassCells& cells) const
{
    VANS_PROFILE_SCOPE("Vegetation::BuildGrassPools", Vans::ProfileCategory::RenderPrepare);
    const auto device=resourceDevice.GetLogicDevice();
    std::map<TreeBatchKey, CellSources> grouped;
    for (const auto& entry : cells)
        if (entry.second.resident)
            grouped[{ entry.second.source->plant, entry.first.variant }].emplace(entry.first, entry.second.source);
    GrassPoolBuild build;
    auto& result = build.pools;
    for (const auto& group : grouped)
    {
        const auto& settings=group.first.first->grass;
        const auto limit=GrassPoolInstanceLimit(resourceDevice.GetDeviceProperties().limits.maxStorageBufferRange,
            settings.boneCount,settings.subBladeCount);
        struct PreviousPage
        {
            std::shared_ptr<VansVegetationSystem> owner;
            std::shared_ptr<const Vans::VansPcgBatchSource> source;
            GrassInstanceRange range;
        };
        std::map<GrassPageKey,PreviousPage> previousPages;
        for (const auto& pool:m_GrassPools)
            if (pool.first.first==group.first)
                for (const auto& page:pool.second.layout.pages)
                    previousPages.emplace(page.first,PreviousPage{pool.second.system,
                        pool.second.cells.at(page.first.first),page.second});
        std::map<GrassPageKey,uint32_t> counts;
        for (const auto& cell:group.second)
        {
            const uint64_t count=cell.second->points.size();
            for (uint64_t offset=0,ordinal=0;offset<count;offset+=limit,++ordinal)
            {
                if (ordinal>std::numeric_limits<uint32_t>::max())
                    throw std::invalid_argument("Grass cell exceeds its page address capacity.");
                counts.emplace(GrassPageKey{cell.first,static_cast<uint32_t>(ordinal)},
                    static_cast<uint32_t>(std::min<uint64_t>(count-offset,limit)));
            }
        }
        const auto partitions=PartitionGrassPages(counts,limit);
        for (uint32_t index=0;index<partitions.size();++index)
        {
            const GrassPoolKey key{group.first,index};
            const auto& requested=partitions[index];
            const auto old=m_GrassPools.find(key);
            CellSources poolCells;
            for (const auto& page:requested) poolCells.emplace(page.first.first,group.second.at(page.first.first));
            bool unchanged=old!=m_GrassPools.end() && old->second.cells.size()==poolCells.size() &&
                old->second.layout.pages.size()==requested.size();
            if (unchanged)
                for (const auto& page:requested)
                {
                    const auto previous=old->second.layout.pages.find(page.first);
                    const auto priorSource=old->second.cells.find(page.first.first);
                    const auto& source=poolCells.at(page.first.first);
                    if (previous==old->second.layout.pages.end() || previous->second.instanceCount!=page.second ||
                        priorSource==old->second.cells.end() || (priorSource->second!=source &&
                            !Vans::EqualPcgBatchSources(*priorSource->second,*source)))
                    { unchanged=false;break; }
                }
            if (unchanged) { ++build.reused;result.emplace(key,old->second);continue; }
            GrassPool pool;
            pool.cells=std::move(poolCells);
            pool.layout=BuildGrassPoolLayout(old==m_GrassPools.end()?GrassPoolLayout<GrassPageKey>{}:old->second.layout,requested);
            // Fragmentation can no longer fit in one descriptor range. Replace
            // its layout, preserving each surviving bone plane with its stride.
            if (pool.layout.capacity>limit) pool.layout=BuildGrassPoolLayout(GrassPoolLayout<GrassPageKey>{},requested);
            // Reuse the physical arena only if every unchanged page already
            // belongs to it at the same address/stride. Changed pages are staged
            // now and written by the next GPU submission, never by the CPU.
            bool reusable=old!=m_GrassPools.end() && CanUpdateGrassPoolInPlace(old->second.layout,pool.layout,counts);
            std::vector<VansVegetationSystem::GrassPageUpdate> updates;
            if (reusable)
                for (const auto& page:pool.layout.pages)
                {
                    const auto& source=pool.cells.at(page.first.first);
                    const auto previous=previousPages.find(page.first);
                    const bool sameSource=previous!=previousPages.end() &&
                        (previous->second.source==source || Vans::EqualPcgBatchSources(*previous->second.source,*source));
                    if (sameSource)
                    {
                        const auto& from=previous->second.range;
                        if (previous->second.owner!=old->second.system || from.baseInstance!=page.second.baseInstance ||
                            from.capacity!=page.second.capacity || from.instanceCount!=page.second.instanceCount)
                        { reusable=false;break; }
                    }
                    else
                    {
                        VansVegetationSystem::GrassPageUpdate update;
                        update.range=page.second;
                        const auto& variant=FindVariant(*source);
                        const uint64_t offset=uint64_t(page.first.second)*limit;
                        update.instances.reserve(page.second.instanceCount);
                        for (uint32_t local=0;local<page.second.instanceCount;++local)
                            update.instances.push_back(MakeGrassInstance(*source,variant,source->points.at(offset+local)));
                        updates.push_back(std::move(update));
                    }
                }
            if (reusable)
            {
                ++build.reused;
                build.uploadPages+=updates.size();
                for (const auto& update:updates) build.uploadInstances+=update.instances.size();
                pool.system=old->second.system;
                auto uploads=pool.system->PrepareGrassPageUpdates(updates);
                if (!uploads.empty()) build.updates.push_back({pool.system,std::move(uploads)});
                result.emplace(key,std::move(pool));
                continue;
            }
            std::vector<GrassInstance> instances(pool.layout.capacity);
            std::vector<GrassInstanceRange> storage;
            std::map<std::shared_ptr<VansVegetationSystem>,std::vector<VansVegetationSystem::GrassStateTransfer>> preserved;
            for (const auto& page:pool.layout.pages)
            {
                const auto& source=pool.cells.at(page.first.first);
                const auto& range=page.second;
                const auto& variant=FindVariant(*source);
                const uint64_t offset=uint64_t(page.first.second)*limit;
                for (uint32_t local=0;local<range.instanceCount;++local)
                    instances[range.baseInstance+local]=MakeGrassInstance(*source,variant,source->points.at(offset+local));
                storage.push_back(range);
                const auto previous=previousPages.find(page.first);
                if (previous!=previousPages.end() && (previous->second.source==source ||
                    Vans::EqualPcgBatchSources(*previous->second.source,*source)))
                    preserved[previous->second.owner].push_back({previous->second.range,range});
            }
            pool.system=CreateBatch(scene,device,*pool.cells.begin()->second,std::move(instances),std::move(storage));
            ++build.replaced;
            for (auto& source:preserved) pool.system->PreserveGrassState(source.first,std::move(source.second));
            result.emplace(key,std::move(pool));
        }
    }
    return build;
}
bool VansVegetationCollection::Apply(VansScene& scene,VansVKDevice& resourceDevice,
    const Vans::VansPcgBatchUpdate& update,std::string& error)
{
    error.clear();
    const auto device=resourceDevice.GetLogicDevice();
    try
    {
        auto next=m_GrassCells;
        auto treeCells=m_TreeCells;
        for(auto it=treeCells.begin();it!=treeCells.end();)
            if(update.Contains(it->first))
            {
                m_GIUnavailableTreeCells.erase(it->first);
                it=treeCells.erase(it);
            }
            else ++it;
        for(const auto& entry:update.batches) {
            if(!entry.second || !(entry.first==entry.second->key) || !update.Contains(entry.first))
                throw std::invalid_argument("PCG batch update contains a foreign target.");
            if(entry.second->plant->category==Vans::VansPlantCategory::Tree)treeCells[entry.first]=entry.second;
        }
        const auto treeSources=GroupTreeBatches(treeCells);
        auto trees=m_TreeBatches;
        for(auto it=trees.begin();it!=trees.end();)
            if(!treeSources.count(it->first))it=trees.erase(it);else ++it;
        for(const auto& entry:treeSources) {
            const auto old=trees.find(entry.first);
            if(old!=trees.end() && Vans::EqualPcgBatchSources(*old->second.source,*entry.second))continue;
            Batch batch;batch.source=entry.second;
            batch.system=CreateBatch(scene,device,*entry.second,{},{});
            trees[entry.first]=std::move(batch);
        }
        std::vector<std::shared_ptr<VansVegetationSystem>> retired;
        for (const auto& entry : m_GrassCells)
        {
            if (!update.Contains(entry.first)) continue;
            const auto source=update.batches.find(entry.first);
            if (source!=update.batches.end() && source->second &&
                Vans::EqualPcgBatchSources(*entry.second.source,*source->second)) continue;
            next.erase(entry.first);
        }
        for (const auto& entry : update.batches)
        {
            if (!entry.second || !(entry.first==entry.second->key) || !update.Contains(entry.first))
                throw std::invalid_argument("PCG batch update contains a foreign target.");
            if (entry.second->plant->category==Vans::VansPlantCategory::Tree || next.count(entry.first)) continue;
            GrassCell batch;
            batch.source=entry.second;batch.roots=BatchRootBounds(*entry.second);batch.cellSize=update.cellSize;
            batch.worldBounds=GrassCellBounds(*entry.second);
            const auto& render=batch.source->plant->render;
            batch.active=!render.cullingEnabled || (m_HasView && WithinDistance(batch.roots,m_CameraX,m_CameraZ,render.cullDistance));
            batch.resident=batch.active;
            next.emplace(entry.first,std::move(batch));
        }
        auto grassBuild=BuildGrassPools(scene,resourceDevice,next);
        auto& grassPools=grassBuild.pools;
        for(const auto& entry:m_GrassPools) {
            const auto replacement=grassPools.find(entry.first);
            if(replacement==grassPools.end() || replacement->second.system!=entry.second.system)
                retired.push_back(entry.second.system);
        }
        for(const auto& entry:m_TreeBatches) {
            const auto replacement=trees.find(entry.first);
            if(replacement==trees.end() || replacement->second.system!=entry.second.system)
                retired.push_back(entry.second.system);
        }
        // 保留旧资源至当前帧槽的 fence 完成；普通刷绘无需让整个设备空闲。
        if (!retired.empty())
            resourceDevice.EnqueueDeferredDelete([retired=std::move(retired)](){});
        GIVoxelSourceChanges giChanges;
        std::set<Vans::VansPcgBatchKey> giChangedTreeCells;
        std::set<std::string> giRemovedSources;
        if(scene.GetGIVoxelSourceManager().IsBound())
        {
            for(const auto& entry:m_TreeCells)if(!treeCells.count(entry.first))
            {
                const auto key=VoxelSourceKey(entry.first);
                giChanges.removed.push_back(key);
                giRemovedSources.insert(key);
                m_GIUnavailableTreeCells.erase(entry.first);
            }
            for(const auto& entry:treeCells)
            {
                const auto old=m_TreeCells.find(entry.first);
                if(old!=m_TreeCells.end() && (old->second==entry.second || Vans::EqualPcgBatchSources(*old->second,*entry.second)))continue;
                giChangedTreeCells.insert(entry.first);
                auto captured=CaptureVoxelSource(scene,*entry.second);
                if(captured.availability==GIVoxelSourceAvailability::Available)
                {
                    m_GIUnavailableTreeCells.erase(entry.first);
                    m_GIPendingRemovedSources.erase(VoxelSourceKey(entry.first));
                    giChanges.updated.push_back(std::move(captured));
                }
                else
                {
                    // 新快照仍保留在 PCG 区块表中；旧 GI 几何继续服务，资源到位后由每帧重试提交。
                    m_GIUnavailableTreeCells.insert(entry.first);
                }
            }
        }
        grassBuild.CommitUploads();
        m_GrassCells.swap(next);m_GrassPools.swap(grassPools);m_TreeBatches.swap(trees);m_TreeCells.swap(treeCells);
        grassBuild.Log(m_GrassPools.size());
        if(!giChanges.Empty() && !scene.GetGIVoxelSourceManager().Submit(std::move(giChanges)))
        {
            // GI 世界可能尚未分配或正在切换；保留键，下一帧由管理器重试，不能把一次拒绝当成删除。
            m_GIUnavailableTreeCells.insert(giChangedTreeCells.begin(),giChangedTreeCells.end());
            m_GIPendingRemovedSources.insert(giRemovedSources.begin(),giRemovedSources.end());
        }
        size_t treeInstances=0,treeDrawBatches=0;
        for(const auto& entry:m_TreeBatches) {
            treeInstances+=entry.second.source->points.size();
            treeDrawBatches+=entry.second.system->GetTreeDrawConfigsGPU().size();
        }
        VANS_LOG("[TreeLOD] cells="<<m_TreeCells.size()<<" species="<<m_TreeBatches.size()
            <<" instances="<<treeInstances<<" mainIndirectBatches="<<treeDrawBatches);
        {std::lock_guard<std::mutex> lock(m_StatusMutex);m_LastUpdateError.clear();}
        return true;
    }
    catch (const std::exception& exception) {
        error=exception.what();std::lock_guard<std::mutex> lock(m_StatusMutex);m_LastUpdateError=error;return false;
    }
}
void VansVegetationCollection::ForEach(const std::function<void(VansVegetationSystem&)>& visit) const
{
    for (const auto& entry : m_GrassPools)
        visit(*entry.second.system);
    for (const auto& entry : m_TreeBatches)
        if (entry.second.system) visit(*entry.second.system);
}
void VansVegetationCollection::ForEachViewRelevant(const std::function<void(VansVegetationSystem&)>& visit) const
{
    for (const auto& entry : m_GrassPools)
        if (entry.second.viewRelevant || entry.second.system->HasGrassStateTransfer())
            visit(*entry.second.system);
    for (const auto& entry : m_TreeBatches)
        if (entry.second.system) visit(*entry.second.system);
}
bool VansVegetationCollection::WithinDistance(const Vans::VansPcgBounds& roots,
    float cameraX,float cameraZ,float distance)
{
    const double x=std::max({static_cast<double>(roots.min[0])-cameraX,0.0,static_cast<double>(cameraX)-roots.max[0]});
    const double z=std::max({static_cast<double>(roots.min[1])-cameraZ,0.0,static_cast<double>(cameraZ)-roots.max[1]});
    return x*x+z*z<=static_cast<double>(distance)*distance;
}
bool VansVegetationCollection::UpdateResidency(VansScene& scene,VansVKDevice& resourceDevice,
    float cameraX,float cameraZ,const glm::mat4& worldToClip,uint32_t frameSlot,std::string& error)
{
    VANS_PROFILE_SCOPE("Vegetation::Residency", Vans::ProfileCategory::RenderPrepare);
    error.clear();
    if (!std::isfinite(cameraX) || !std::isfinite(cameraZ)) {error="Invalid vegetation view position.";return false;}
    try {
        for (auto& pool:m_GrassPools) pool.second.system->RetireGrassStateTransfer(resourceDevice);
        auto next=m_GrassCells;
        std::size_t created=0,evicted=0,activeBatches=0,viewRelevantBatches=0,grass=0,viewGrass=0,trees=0;
        for (auto& entry:next) {
            auto& batch=entry.second;
            const auto& render=batch.source->plant->render;
            batch.active=!render.cullingEnabled || WithinDistance(batch.roots,cameraX,cameraZ,render.cullDistance);
            if (batch.active && !batch.resident) { batch.resident=true;++created; }
            // 留一格缓存避免在边界来回移动时反复创建；缓存格不参与模拟、主画面或阴影。
            float exitDistance=0;
            if (!Vans::ResolvePcgGrassResidencyExitDistance(render.cullDistance,batch.cellSize,exitDistance))
                throw std::invalid_argument("Invalid PCG grass residency distance or cell size.");
            if (!batch.active && batch.resident && !WithinDistance(batch.roots,cameraX,cameraZ,exitDistance))
            { batch.resident=false;++evicted; }
            batch.viewRelevant = batch.active &&
                (render.castShadows || !render.cullingEnabled || !batch.worldBounds.IsValid() ||
                    RenderAABBIntersectsClipFrustum(batch.worldBounds.min,batch.worldBounds.max,worldToClip));
            if (batch.active) { ++activeBatches;grass+=batch.source->points.size(); }
            if (batch.viewRelevant) { ++viewRelevantBatches;viewGrass+=batch.source->points.size(); }
        }
        GrassPoolBuild build;
        if (created || evicted) build=BuildGrassPools(scene,resourceDevice,next);
        else build.pools=m_GrassPools;
        auto& pools=build.pools;
        std::vector<std::shared_ptr<VansVegetationSystem>> retired;
        for (const auto& old:m_GrassPools) {
            const auto replacement=pools.find(old.first);
            if (replacement==pools.end() || replacement->second.system!=old.second.system)
                retired.push_back(old.second.system);
        }
        auto* manager=scene.GetMaterialManager();
        auto* hzb=manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
        std::size_t poolGroups=0;
        for (auto& entry:pools) {
            auto& pool=entry.second;
            std::vector<GrassInstanceRange> work;
            for (const auto& page:pool.layout.pages)
                if (next.at(page.first.first).viewRelevant) work.push_back(page.second);
            pool.viewRelevant=!work.empty();
            for (const auto& range:work)
                poolGroups+=(uint64_t(range.instanceCount)+GrassWorkgroupWidth-1u)/GrassWorkgroupWidth;
            pool.system->SetGrassWorkRanges(frameSlot,work);
            const auto& render=entry.first.first.first->render;
            if (render.hizEnabled && hzb)
                pool.system->SetHiZDepth(hzb->GetImage().GetImageView(),hzb->GetImage().GetSampler(),
                    static_cast<uint32_t>(manager->m_HIZMipCount),render.hizBias);
        }
        for(auto& entry:m_TreeBatches) {
            auto& batch=entry.second;const auto& render=batch.source->plant->render;
            if(render.hizEnabled && hzb && batch.system)
                batch.system->SetHiZDepth(hzb->GetImage().GetImageView(),hzb->GetImage().GetSampler(),
                    static_cast<uint32_t>(manager->m_HIZMipCount),render.hizBias);
            ++activeBatches;++viewRelevantBatches;trees+=batch.source->points.size();
        }
        if (!retired.empty()) resourceDevice.EnqueueDeferredDelete([retired=std::move(retired)](){});
        build.CommitUploads();
        m_GrassCells.swap(next);m_GrassPools.swap(pools);
        if (created || evicted) build.Log(m_GrassPools.size());
        if (!m_HasView || created || evicted)
            VANS_LOG("[Vegetation] Nearby batches="<<activeBatches<<"/"<<(m_GrassCells.size()+m_TreeBatches.size())
                <<" viewRelevant="<<viewRelevantBatches
                <<" grassCandidates="<<grass<<" viewGrassCandidates="<<viewGrass
                <<" treeCandidates="<<trees
                <<" grassPools="<<m_GrassPools.size()<<" grassWorkgroups="<<poolGroups
                <<" created="<<created<<" retired="<<evicted);
        m_HasView=true;m_CameraX=cameraX;m_CameraZ=cameraZ;
        {std::lock_guard<std::mutex> lock(m_StatusMutex);m_LastUpdateError.clear();}
        return true;
    } catch (const std::exception& exception) {
        error=exception.what();std::lock_guard<std::mutex> lock(m_StatusMutex);m_LastUpdateError=error;return false;
    }
}

void VansVegetationCollection::CollectGIVoxelSources(VansScene& scene, std::vector<GIVoxelSource>& sources) const
{
    for (const auto& entry : m_TreeCells)
    {
        const auto& source = *entry.second;
        if (source.plant->category == Vans::VansPlantCategory::Grass) continue;
        auto captured=CaptureVoxelSource(scene,source);
        if(captured.availability==GIVoxelSourceAvailability::Available)
        {
            m_GIUnavailableTreeCells.erase(entry.first);
            sources.push_back(std::move(captured));
        }
        else m_GIUnavailableTreeCells.insert(entry.first);
    }
}

void VansVegetationCollection::RetryUnavailableGIVoxelSources(VansScene& scene)
{
    if(!scene.GetGIVoxelSourceManager().IsBound() ||
        (m_GIUnavailableTreeCells.empty() && m_GIPendingRemovedSources.empty()))return;
    GIVoxelSourceChanges changes;
    std::vector<Vans::VansPcgBatchKey> resolved;
    resolved.reserve(m_GIUnavailableTreeCells.size());
    for(const auto& key:m_GIPendingRemovedSources)changes.removed.push_back(key);
    for(const auto& key:m_GIUnavailableTreeCells)
    {
        const auto found=m_TreeCells.find(key);
        if(found==m_TreeCells.end())
        {
            // 该区块已删除；删除 tombstone 由 m_GIPendingRemovedSources 负责重试。
            continue;
        }
        auto captured=CaptureVoxelSource(scene,*found->second);
        if(captured.availability==GIVoxelSourceAvailability::Available)
        {
            changes.updated.push_back(std::move(captured));
            resolved.push_back(key);
        }
    }
    if(changes.Empty())return;
    if(!scene.GetGIVoxelSourceManager().Submit(std::move(changes)))return;
    for(const auto& key:resolved)m_GIUnavailableTreeCells.erase(key);
    m_GIPendingRemovedSources.clear();
}
}
