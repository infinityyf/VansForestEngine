#include "VansVegetationCollection.h"
#include "VansVegetationSystem.h"
#include "../VansScene.h"
#include "../VansMaterial.h"
#include "../GICore/VansGIVoxelSource.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansMesh.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
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
    std::string name;
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
std::shared_ptr<VansVegetationSystem> CreateBatch(VansScene& scene,VkDevice device,
    const Vans::VansPcgBatchSource& source)
{
    VegetationBatch batch;
    batch.plant=source.plant;
    const auto variant=std::find_if(batch.plant->variants.begin(),batch.plant->variants.end(),
        [&](const auto& item){return item.id==source.key.variant;});
    if (variant==batch.plant->variants.end()) throw std::invalid_argument("PCG model variant is unavailable.");
    batch.bladeWidth=variant->bladeWidth;
    ResolveParts(scene,*variant,batch);
    if(batch.plant->category==Vans::VansPlantCategory::Tree) {
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
    for (const auto& point : source.points)
    {
        const glm::mat4 model=PlacementMatrix(point.position,point.rotation,point.scale) *
            PlacementMatrix(variant->offset,variant->rotation,variant->scale);
        const float extent=std::sqrt(glm::dot(glm::vec3(model[0]),glm::vec3(model[0]))+
            glm::dot(glm::vec3(model[1]),glm::vec3(model[1]))+glm::dot(glm::vec3(model[2]),glm::vec3(model[2])));
        const uint32_t seed=static_cast<uint32_t>(point.id^(point.id>>32));
        if (batch.plant->category==Vans::VansPlantCategory::Grass)
        {
            GrassInstance instance;
            instance.modelMatrix=model;instance.randomSeed=seed;
            instance.boundsRadius=(std::max(variant->cullingRadius,batch.plant->grass.bladeHeight)+
                batch.plant->grass.scatterRadiusMax)*extent;
            batch.grass.push_back(instance);
        }
        else
        {
            TreeInstanceGPU instance;
            instance.modelMatrix=model;instance.randomSeed=seed;
            const glm::vec3 center=variant->lod.levels.empty()?glm::vec3(0):glm::vec3(variant->lod.centerRadius[0],variant->lod.centerRadius[1],variant->lod.centerRadius[2]);
            const float radius=variant->lod.levels.empty()?variant->cullingRadius:variant->lod.centerRadius[3];
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
    system.SetTreeLodSettings(plant.render.lodDistances[0],plant.render.lodDistances[1],plant.render.lodHysteresis);
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

    system.Init(device,std::move(batch.grass),std::move(batch.trees),grass.boneCount);
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

bool VansVegetationCollection::Apply(VansScene& scene,VkDevice device,
    const Vans::VansPcgBatchUpdate& update,VansVKDevice* retirementDevice,std::string& error)
{
    error.clear();
    try
    {
        auto next=m_Batches;
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
            Batch batch;batch.source=entry.second;batch.active=true;
            batch.system=CreateBatch(scene,device,*entry.second);
            trees[entry.first]=std::move(batch);
        }
        std::vector<std::shared_ptr<VansVegetationSystem>> retired;
        for (const auto& entry : m_Batches)
        {
            if (!update.Contains(entry.first)) continue;
            const auto source=update.batches.find(entry.first);
            if (source!=update.batches.end() && source->second &&
                Vans::EqualPcgBatchSources(*entry.second.source,*source->second)) continue;
            if (entry.second.system) retired.push_back(entry.second.system);
            next.erase(entry.first);
        }
        for (const auto& entry : update.batches)
        {
            if (!entry.second || !(entry.first==entry.second->key) || !update.Contains(entry.first))
                throw std::invalid_argument("PCG batch update contains a foreign target.");
            if (entry.second->plant->category==Vans::VansPlantCategory::Tree || next.count(entry.first)) continue;
            Batch batch;
            batch.source=entry.second;batch.roots=BatchRootBounds(*entry.second);batch.cellSize=update.cellSize;
            const auto& render=batch.source->plant->render;
            batch.active=batch.source->plant->category==Vans::VansPlantCategory::Tree || !render.cullingEnabled || (m_HasView && WithinDistance(batch.roots,m_CameraX,m_CameraZ,render.cullDistance));
            if (batch.active) batch.system=CreateBatch(scene,device,*entry.second);
            next.emplace(entry.first,std::move(batch));
        }
        for(const auto& entry:m_TreeBatches) {
            const auto replacement=trees.find(entry.first);
            if(replacement==trees.end() || replacement->second.system!=entry.second.system)
                retired.push_back(entry.second.system);
        }
        // 保留旧资源至当前帧槽的 fence 完成；普通刷绘无需让整个设备空闲。
        if (retirementDevice && !retired.empty())
            retirementDevice->EnqueueDeferredDelete([retired=std::move(retired)](){});
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
        m_Batches.swap(next);m_TreeBatches.swap(trees);m_TreeCells.swap(treeCells);
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
    for (const auto& entry : m_Batches)
        if (entry.second.active && entry.second.system) visit(*entry.second.system);
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
bool VansVegetationCollection::UpdateResidency(VansScene& scene,VkDevice device,VansVKDevice& retirementDevice,
    float cameraX,float cameraZ,std::string& error)
{
    VANS_PROFILE_SCOPE("Vegetation::Residency", Vans::ProfileCategory::RenderPrepare);
    error.clear();
    if (!std::isfinite(cameraX) || !std::isfinite(cameraZ)) {error="Invalid vegetation view position.";return false;}
    try {
        std::vector<std::pair<Batch*,std::shared_ptr<VansVegetationSystem>>> created;
        std::vector<Batch*> evicted;
        for (auto& entry:m_Batches) {
            auto& batch=entry.second;
            const auto& render=batch.source->plant->render;
            const bool active=batch.source->plant->category==Vans::VansPlantCategory::Tree || !render.cullingEnabled || WithinDistance(batch.roots,cameraX,cameraZ,render.cullDistance);
            if (active && !batch.system) created.emplace_back(&batch,CreateBatch(scene,device,*batch.source));
            // 留一格缓存避免在边界来回移动时反复创建；缓存格不参与模拟、主画面或阴影。
            if (!active && batch.system && !WithinDistance(batch.roots,cameraX,cameraZ,render.cullDistance+batch.cellSize))
                evicted.push_back(&batch);
        }
        std::vector<std::shared_ptr<VansVegetationSystem>> retired;
        for (auto* batch:evicted) retired.push_back(std::move(batch->system));
        for (auto& item:created) item.first->system=std::move(item.second);
        if (!retired.empty()) retirementDevice.EnqueueDeferredDelete([retired=std::move(retired)](){});
        std::size_t activeBatches=0,grass=0,trees=0;
        auto* manager=scene.GetMaterialManager();
        auto* hzb=manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
        for (auto& entry:m_Batches) {
            auto& batch=entry.second;const auto& render=batch.source->plant->render;
            batch.active=batch.source->plant->category==Vans::VansPlantCategory::Tree || !render.cullingEnabled || WithinDistance(batch.roots,cameraX,cameraZ,render.cullDistance);
            if (!batch.active) continue;
            // 分辨率变化或缓存区块重新进入范围时，使用当前 HZB；绑定未变则不更新描述符。
            if (render.hizEnabled && hzb && batch.system)
                batch.system->SetHiZDepth(hzb->GetImage().GetImageView(),hzb->GetImage().GetSampler(),
                    static_cast<uint32_t>(manager->m_HIZMipCount),render.hizBias);
            ++activeBatches;
            if (batch.source->plant->category==Vans::VansPlantCategory::Grass) grass+=batch.source->points.size();
            else trees+=batch.source->points.size();
        }
        for(auto& entry:m_TreeBatches) {
            auto& batch=entry.second;const auto& render=batch.source->plant->render;
            if(render.hizEnabled && hzb && batch.system)
                batch.system->SetHiZDepth(hzb->GetImage().GetImageView(),hzb->GetImage().GetSampler(),
                    static_cast<uint32_t>(manager->m_HIZMipCount),render.hizBias);
            ++activeBatches;trees+=batch.source->points.size();
        }
        if (!m_HasView || !created.empty() || !evicted.empty())
            VANS_LOG("[Vegetation] Nearby batches="<<activeBatches<<"/"<<BatchCount()
                <<" grassCandidates="<<grass<<" treeCandidates="<<trees
                <<" created="<<created.size()<<" retired="<<evicted.size());
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
