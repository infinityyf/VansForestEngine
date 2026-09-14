#include "VansVegetationCollection.h"
#include "VansVegetationSystem.h"
#include "../VansScene.h"
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
            instance.boundsSphere=glm::vec4(glm::vec3(model[3]),variant->cullingRadius*extent);
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
bool VansVegetationCollection::Apply(VansScene& scene,VkDevice device,
    const Vans::VansPcgBatchUpdate& update,VansVKDevice* retirementDevice,std::string& error)
{
    error.clear();
    try
    {
        auto next=m_Batches;
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
            if (next.count(entry.first)) continue;
            Batch batch;
            batch.source=entry.second;batch.roots=BatchRootBounds(*entry.second);batch.cellSize=update.cellSize;
            const auto& render=batch.source->plant->render;
            batch.active=!render.cullingEnabled || (m_HasView && WithinDistance(batch.roots,m_CameraX,m_CameraZ,render.cullDistance));
            if (batch.active) batch.system=CreateBatch(scene,device,*entry.second);
            next.emplace(entry.first,std::move(batch));
        }
        // 保留旧资源至当前帧槽的 fence 完成；普通刷绘无需让整个设备空闲。
        if (retirementDevice && !retired.empty())
            retirementDevice->EnqueueDeferredDelete([retired=std::move(retired)](){});
        m_Batches.swap(next);
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
            const bool active=!render.cullingEnabled || WithinDistance(batch.roots,cameraX,cameraZ,render.cullDistance);
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
            batch.active=!render.cullingEnabled || WithinDistance(batch.roots,cameraX,cameraZ,render.cullDistance);
            if (!batch.active) continue;
            // 分辨率变化或缓存区块重新进入范围时，使用当前 HZB；绑定未变则不更新描述符。
            if (render.hizEnabled && hzb && batch.system)
                batch.system->SetHiZDepth(hzb->GetImage().GetImageView(),hzb->GetImage().GetSampler(),
                    static_cast<uint32_t>(manager->m_HIZMipCount),render.hizBias);
            ++activeBatches;
            if (batch.source->plant->category==Vans::VansPlantCategory::Grass) grass+=batch.source->points.size();
            else trees+=batch.source->points.size();
        }
        if (!m_HasView || !created.empty() || !evicted.empty())
            VANS_LOG("[Vegetation] Nearby batches="<<activeBatches<<"/"<<m_Batches.size()
                <<" grassCandidates="<<grass<<" treeCandidates="<<trees
                <<" created="<<created.size()<<" retired="<<evicted.size());
        m_HasView=true;m_CameraX=cameraX;m_CameraZ=cameraZ;
        {std::lock_guard<std::mutex> lock(m_StatusMutex);m_LastUpdateError.clear();}
        return true;
    } catch (const std::exception& exception) {
        error=exception.what();std::lock_guard<std::mutex> lock(m_StatusMutex);m_LastUpdateError=error;return false;
    }
}
}
